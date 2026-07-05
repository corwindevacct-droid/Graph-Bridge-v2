# GraphBridge AI - graphbridge_agent_gemini.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
graphbridge_agent_gemini.py
AI-driven Blueprint automation using Google Gemini (google-genai SDK).

Mirrors graphbridge_agent.py's Anthropic/OpenAI backends exactly: same
SYSTEM_PROMPT, same session persistence, same single generic graph_command
tool, same turn loop shape. Reuses that module's shared infrastructure
directly instead of duplicating it.

Usage:
    python graphbridge_agent_gemini.py "List all nodes in /Game/BP_MyCharacter.BP_MyCharacter"
    python graphbridge_agent_gemini.py --bp /Game/BP_MyChar.BP_MyChar "Wire BeginPlay to register IMC"
    python graphbridge_agent_gemini.py --clear-session --bp /Game/BP_X.BP_X "Start fresh"

Setup:
    pip install google-genai websockets
    Set GEMINI_API_KEY (or GOOGLE_API_KEY) as an environment variable.

    NEVER hardcode an API key in this file, in any commit, or anywhere else
    in this repo. genai.Client() with no arguments reads GEMINI_API_KEY /
    GOOGLE_API_KEY from the environment automatically - that is the only
    supported way to supply credentials here.
"""

import asyncio
import os
import sys
import re
import json

from graphbridge_agent import (
    SYSTEM_PROMPT,
    load_session,
    save_session,
    build_session_context,
    execute_bridge_command,
    _session_path,
)
from graphbridge_bridge import UnrealBridge

try:
    import graphbridge_config as _cfg
    _BRIDGE_URI = _cfg.BRIDGE_URI
except ImportError:
    _BRIDGE_URI = "ws://127.0.0.1:8080"

# gemini-2.5-flash is the current, confirmed-stable model as of this writing.
# Gemini 1.0, 1.5, and 2.0 Flash have all been shut down - every request
# against those returns 404. Gemini 3.x models also exist as of this
# writing; worth revisiting once their stability is confirmed over time,
# the same way 2.5 superseded 2.0 here.
_GEMINI_MODEL = "gemini-2.5-flash"

# Single generic tool, matching _ANTHROPIC_TOOLS / _OPENAI_TOOLS in
# graphbridge_agent.py exactly - one graph_command(command: str) tool, with
# all command knowledge living in SYSTEM_PROMPT (imported above), not
# duplicated into a second command list here.
_GEMINI_TOOL_SCHEMA = {
    "name": "graph_command",
    "description": (
        "Send a pipe-delimited command to Unreal GraphBridge and get the result. "
        "Use for ALL Blueprint graph operations: LIST_NODES, GET_NODE_PINS, "
        "FIND_NODE_CLASS, LIST_ASSETS, SPAWN_NODE, CONNECT_PINS, DISCONNECT_PINS, "
        "DELETE_NODE, CLEAR_NODES, SET_PIN_DEFAULT, SET_VARIABLE_REF, "
        "SET_FUNCTION_REF, SET_INPUT_ACTION, SPAWN_VARIABLE, SET_VARIABLE_DEFAULT, "
        "COMPILE, SAVE_BLUEPRINT, OPEN_BLUEPRINT, CLOSE_BLUEPRINT."
    ),
    "parameters_json_schema": {
        "type": "object",
        "properties": {
            "command": {
                "type": "string",
                "description": "Pipe-delimited command e.g. LIST_NODES|/Game/BP_X.BP_X",
            }
        },
        "required": ["command"],
    },
}


async def run_gemini_agent(task: str, bp_path: str = "", max_turns: int = 20):
    """Agentic loop using Google Gemini."""
    try:
        from google import genai
        from google.genai import types
    except ImportError:
        print("ERROR: google-genai not installed. Run: pip install google-genai")
        return

    api_key = os.environ.get("GEMINI_API_KEY") or os.environ.get("GOOGLE_API_KEY")
    if not api_key:
        print("ERROR: GEMINI_API_KEY not set.")
        print("  Windows:   set GEMINI_API_KEY=<your-key>")
        print("  Mac/Linux: export GEMINI_API_KEY=<your-key>")
        return

    # No arguments - the client reads GEMINI_API_KEY / GOOGLE_API_KEY from
    # the environment itself. Do not pass api_key= here.
    client = genai.Client()

    tool = types.Tool(function_declarations=[
        types.FunctionDeclaration(
            name=_GEMINI_TOOL_SCHEMA["name"],
            description=_GEMINI_TOOL_SCHEMA["description"],
            parameters_json_schema=_GEMINI_TOOL_SCHEMA["parameters_json_schema"],
        )
    ])

    bridge = UnrealBridge(uri=_BRIDGE_URI)
    if not await bridge.connect():
        return

    session = load_session(bp_path) if bp_path else {}
    session_context = build_session_context(session)
    if session_context:
        task = session_context + "\n\nTASK: " + task
        print(f"[Session] Injecting context for {bp_path}")

    print(f"\n[Task] {task[:200]}{'...' if len(task) > 200 else ''}\n")
    print(f"[Model] {_GEMINI_MODEL} (Gemini)\n")

    config = types.GenerateContentConfig(
        system_instruction=SYSTEM_PROMPT,
        tools=[tool],
    )

    contents = [types.Content(role="user", parts=[types.Part.from_text(text=task)])]

    for turn in range(max_turns):
        print(f"\n--- Turn {turn + 1} [Gemini] ---")

        try:
            response = await client.aio.models.generate_content(
                model=_GEMINI_MODEL,
                contents=contents,
                config=config,
            )
        except Exception as e:
            print(f"ERROR: Gemini API error: {e}")
            break

        candidate = response.candidates[0] if response.candidates else None
        if candidate is None or candidate.content is None:
            print("ERROR: Empty response from Gemini.")
            break

        # The model's own turn (including any function_call parts) must be
        # replayed back into contents before the function_response turn -
        # same requirement as Claude's messages.append({"role": "assistant", ...})
        # and OpenAI's messages.append(oai_msg) in graphbridge_agent.py.
        contents.append(candidate.content)

        function_calls = response.function_calls or []
        if not function_calls:
            try:
                text = response.text
            except Exception:
                text = "(no text response)"
            print(f"\n[Gemini] {text}")
            break

        function_response_parts = []
        for fc in function_calls:
            args = dict(fc.args or {})
            cmd = args.get("command", "")

            # execute_bridge_command already handles an empty/malformed
            # command cleanly (returns {"success": false, "message": ...}
            # as JSON) rather than crashing - that error is fed straight
            # back to Gemini below, same error-recovery path SYSTEM_PROMPT
            # already teaches Claude/OpenAI to use (see ERROR RECOVERY
            # section: never give up after one failure, diagnose and retry).
            bridge_result_json = await execute_bridge_command(bridge, cmd, session)
            try:
                bridge_result = json.loads(bridge_result_json)
            except (TypeError, json.JSONDecodeError):
                bridge_result = {"success": False, "message": "Could not parse bridge result"}

            function_response_parts.append(
                types.Part.from_function_response(name=fc.name, response=bridge_result)
            )

        contents.append(types.Content(role="tool", parts=function_response_parts))

    else:
        print(f"\n[Agent] Reached max_turns ({max_turns}).")

    if bp_path and session:
        save_session(bp_path, session)

    await bridge.close()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    args = sys.argv[1:]

    clear_session = "--clear-session" in args
    args = [a for a in args if a != "--clear-session"]

    explicit_bp = ""
    if "--bp" in args:
        idx = args.index("--bp")
        if idx + 1 < len(args):
            explicit_bp = args[idx + 1]
            args = [a for i, a in enumerate(args) if i != idx and i != idx + 1]

    args = [a for a in args if not a.startswith("--")]

    task = " ".join(args) if args else "List all Blueprint assets in the project"

    bp_path = explicit_bp
    if not bp_path:
        match = re.search(r"/Game/\S+", task)
        if match:
            bp_path = match.group(0).rstrip(".,)")

    if clear_session and bp_path:
        session_file = _session_path(bp_path)
        if session_file.exists():
            session_file.unlink()
            print(f"[Session] Cleared session for {bp_path}")

    if bp_path:
        print(f"[Session] BP path detected: {bp_path}")

    asyncio.run(run_gemini_agent(task, bp_path=bp_path))
