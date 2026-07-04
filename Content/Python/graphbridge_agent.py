# GraphBridge AI - graphbridge_agent.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
graphbridge_agent.py
AI-driven Blueprint automation - supports Anthropic Claude and OpenAI ChatGPT.

Usage:
    python graphbridge_agent.py "List all nodes in /Game/BP_MyCharacter.BP_MyCharacter"
    python graphbridge_agent.py --openai "Add a float Speed variable to /Game/BP_MyChar.BP_MyChar"
    python graphbridge_agent.py --bp /Game/BP_MyChar.BP_MyChar "Wire BeginPlay to register IMC"
    python graphbridge_agent.py --clear-session --bp /Game/BP_X.BP_X "Start fresh"

Setup:
    pip install anthropic openai websockets
    Set ANTHROPIC_API_KEY (or OPENAI_API_KEY for --openai) as an environment variable,
    or edit graphbridge_config.py.
"""

import asyncio
import os
import sys
import json
import re
from pathlib import Path
from graphbridge_bridge import UnrealBridge

try:
    import graphbridge_config as _cfg
    _ANTHROPIC_KEY = _cfg.ANTHROPIC_API_KEY or os.environ.get("ANTHROPIC_API_KEY", "")
    _OPENAI_KEY    = _cfg.OPENAI_API_KEY    or os.environ.get("OPENAI_API_KEY", "")
    _BRIDGE_URI    = _cfg.BRIDGE_URI
    _DEFAULT_MODEL = _cfg.DEFAULT_MODEL
except ImportError:
    _ANTHROPIC_KEY = os.environ.get("ANTHROPIC_API_KEY", "")
    _OPENAI_KEY    = os.environ.get("OPENAI_API_KEY", "")
    _BRIDGE_URI    = "ws://127.0.0.1:8080"
    _DEFAULT_MODEL = "claude-sonnet-4-6"

OPENAI_MODELS = ["gpt-4o", "gpt-4o-mini", "gpt-3.5-turbo"]

# ---------------------------------------------------------------------------
# System prompt - shared by all backends
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """You are an Unreal Engine Blueprint automation assistant.
You have access to a live Unreal Editor via the graph_command tool.

=======================================================
DISCOVERY RULES - always do this before any mutation
=======================================================
1. If the user did not provide a BP_PATH, call LIST_ASSETS to find it.
   With no filter returns all assets: LIST_ASSETS
   With a keyword filters results: LIST_ASSETS|Character
   Always use LIST_ASSETS (no pipe needed) to list everything.
2. If multiple assets match, pick the most specific one and tell the user
   which one you chose.
3. Once you have the BP_PATH, always call LIST_NODES before touching anything
   so you have real GUIDs and node names - never guess or invent them.
4. Always call GET_NODE_PINS on any node before attempting CONNECT_PINS -
   pin names vary by engine version and node type. Never guess pin names.

=======================================================
SPAWN_NODE RULES - read every time before spawning
=======================================================
- NEVER guess a node class name. Always call FIND_NODE_CLASS first.
- Example flow to spawn a Print String node:
    Step 1: FIND_NODE_CLASS|PrintString     <- get the exact class name
    Step 2: CLOSE_BLUEPRINT|BP_PATH         <- required before any spawn
    Step 3: SPAWN_NODE|BP_PATH|<exact class from step 1>|MyComment|200|100
    Step 4: SET_FUNCTION_REF|BP_PATH|<new node GUID>|KismetSystemLibrary|PrintString
    Step 5: GET_NODE_PINS|BP_PATH|<new node GUID>  <- check real pin names
    Step 6: GET_NODE_PINS|BP_PATH|<target node GUID> <- check source pin names
    Step 7: CONNECT_PINS|BP_PATH|<nodeA GUID>|<pinA>|<nodeB GUID>|<pinB>
    Step 8: COMPILE|BP_PATH
    Step 9: SAVE_BLUEPRINT|BP_PATH

SPAWN_NODE format: SPAWN_NODE|BP_PATH|NodeClass|Comment|X|Y
  - All 6 arguments are REQUIRED - never omit any.
  - Comment must not be empty - use the node purpose e.g. "PrintHello"
  - X and Y are integer screen coordinates e.g. 200 and 100
  - NodeClass must be the exact name from FIND_NODE_CLASS e.g. K2Node_CallFunction
  CORRECT: SPAWN_NODE|/Game/BP_X.BP_X|K2Node_CallFunction|PrintHello|200|100
  WRONG:   SPAWN_NODE|/Game/BP_X.BP_X|PrintString          <- missing args, wrong class

=======================================================
MUTATION WORKFLOW - order matters
=======================================================
1. CLOSE_BLUEPRINT before any SPAWN_NODE - prevents a fatal editor crash.
2. Use GUIDs (from LIST_NODES payload) for all node references - never titles.
3. SET_FUNCTION_REF or SET_VARIABLE_REF immediately after SPAWN_NODE,
   before connecting any pins - otherwise the node has no exec pins.
4. To add a new component to a Blueprint use ADD_COMPONENT first, then
   SPAWN_NODE K2Node_VariableGet + SET_VARIABLE_REF to get a reference.
   ADD_COMPONENT format: ADD_COMPONENT|BPPath|ComponentClass|ComponentName
   e.g. ADD_COMPONENT|/Game/BP_X.BP_X|ProjectileMovementComponent|ProjMovement
4. GET_NODE_PINS on both nodes before CONNECT_PINS - always.
5. COMPILE after all graph changes.
6. SAVE_BLUEPRINT to mark dirty (user presses Ctrl+S to save to disk).

=======================================================
COMMAND FORMATS
=======================================================
BP_PATH format: /Game/Folder/BP_Name.BP_Name

Variable type strings:
  bool, int32, int64, float, double, byte,
  FString, FName, FText, FVector, FVector2D,
  FRotator, FTransform, FLinearColor,
  object:ClassName, class:ClassName

CRITICAL - SET_VARIABLE_DEFAULT format:
  SET_VARIABLE_DEFAULT|BP_PATH|VarName|DefaultValue
  DefaultValue is the VALUE only - never include the type string.
  CORRECT:  SET_VARIABLE_DEFAULT|/Game/BP_X.BP_X|MyVar|1.0
  CORRECT:  SET_VARIABLE_DEFAULT|/Game/BP_X.BP_X|bCanDash|true
  CORRECT:  SET_VARIABLE_DEFAULT|/Game/BP_X.BP_X|Impulse|(X=1.0,Y=0.0,Z=0.0)
  WRONG:    SET_VARIABLE_DEFAULT|/Game/BP_X.BP_X|MyVar|float|1.0

Default value formats by type:
  bool        -> true / false
  int32/float -> 42 / 3.14
  FVector     -> (X=1.0,Y=0.0,Z=0.0)
  FRotator    -> (Pitch=0.0,Yaw=90.0,Roll=0.0)
  FLinearColor-> (R=1.0,G=0.0,B=0.0,A=1.0)
  FString     -> Hello World

=======================================================
ERROR RECOVERY
=======================================================
On failure (success=false):
  - Read the message field carefully.
  - "wrong arg count" -> recheck command format, all args present?
  - "not found" -> use LIST_NODES or FIND_NODE_CLASS to get real names.
  - "Spawn failed" -> use FIND_NODE_CLASS to get the correct class name.
  - Never give up after one failure - diagnose and retry with corrections.

When using graph_command, output ONLY the command string in this format:
COMMAND|arg1|arg2|...
"""

# ---------------------------------------------------------------------------
# Session state - persists discovered GUIDs, pins, variables between runs
# ---------------------------------------------------------------------------

SESSION_DIR = Path("sessions")


def _session_path(bp_path: str) -> Path:
    safe = re.sub(r'[^a-zA-Z0-9_]', '_', bp_path.strip('/'))
    SESSION_DIR.mkdir(exist_ok=True)
    return SESSION_DIR / f"{safe}.json"


def load_session(bp_path: str) -> dict:
    path = _session_path(bp_path)
    if path.exists():
        try:
            with open(path) as f:
                state = json.load(f)
            print(f"[Session] Loaded from {path}")
            return state
        except Exception:
            pass
    return {}


def save_session(bp_path: str, state: dict):
    path = _session_path(bp_path)
    with open(path, "w") as f:
        json.dump(state, f, indent=2)
    print(f"[Session] Saved to {path}")


def update_session_from_command(state: dict, command: str, result: dict):
    if not result.get("success"):
        return

    parts = command.split("|")
    op = parts[0].strip() if parts else ""

    if len(parts) >= 2 and parts[1].startswith("/Game"):
        state.setdefault("bp_path", parts[1])

    if op == "LIST_NODES" and result.get("payload"):
        nodes = {}
        for entry in result["payload"].split("|"):
            p = entry.split("~", 3)
            if len(p) == 4:
                nodes[p[0]] = {
                    "guid":       p[0],
                    "title":      p[1],
                    "comment":    p[2],
                    "class_name": p[3],
                }
        state["nodes"] = nodes
        print(f"  [Session] Cached {len(nodes)} node(s)")

    elif op == "SPAWN_NODE" and result.get("payload"):
        guid = result["payload"].strip()
        comment = parts[3] if len(parts) >= 4 else ""
        class_name = parts[2] if len(parts) >= 3 else ""
        state.setdefault("nodes", {})[guid] = {
            "guid": guid, "title": "", "comment": comment, "class_name": class_name
        }
        state["last_spawned_guid"] = guid
        print(f"  [Session] Cached spawned node {guid[:16]}...")

    elif op == "SET_FUNCTION_REF" and len(parts) >= 5:
        guid = parts[2]
        if guid in state.get("nodes", {}):
            state["nodes"][guid]["function"] = f"{parts[3]}.{parts[4]}"

    elif op == "SET_EVENT_REF" and len(parts) >= 4:
        guid = parts[2]
        if guid in state.get("nodes", {}):
            state["nodes"][guid]["event"] = parts[3]

    elif op == "SET_VARIABLE_REF" and len(parts) >= 4:
        guid = parts[2]
        if guid in state.get("nodes", {}):
            state["nodes"][guid]["variable"] = parts[3]

    elif op == "GET_NODE_PINS" and result.get("payload"):
        node_id = parts[2] if len(parts) >= 3 else ""
        pins = []
        for p in result["payload"].split(","):
            if ":" in p:
                direction, name = p.split(":", 1)
                pins.append({"direction": direction, "name": name})
        state.setdefault("pins", {})[node_id] = pins

    elif op == "SPAWN_VARIABLE" and result.get("payload"):
        actual_name = result["payload"].strip()
        state.setdefault("variables", {})[actual_name] = {
            "type": parts[3] if len(parts) >= 4 else "",
            "category": parts[4] if len(parts) >= 5 else "",
        }

    elif op == "ADD_COMPONENT" and len(parts) >= 4:
        comp_name = parts[3]
        state.setdefault("components", {})[comp_name] = {"class": parts[2]}
        print(f"  [Session] Cached component {comp_name}")

    elif op == "DELETE_NODE" and len(parts) >= 3:
        guid = parts[2]
        state.get("nodes", {}).pop(guid, None)
        state.get("pins", {}).pop(guid, None)


def build_session_context(state: dict) -> str:
    if not state:
        return ""

    lines = ["SESSION CONTEXT (already discovered - do not re-scan):"]

    bp = state.get("bp_path")
    if bp:
        lines.append(f"  Blueprint: {bp}")

    nodes = state.get("nodes", {})
    if nodes:
        lines.append(f"  Nodes ({len(nodes)}):")
        for n in nodes.values():
            detail = ""
            if n.get("function"):  detail = f" -> {n['function']}"
            elif n.get("event"):   detail = f" -> Event:{n['event']}"
            elif n.get("variable"):detail = f" -> Var:{n['variable']}"
            lines.append(f"    {n['guid']} | {n['title'] or n['class_name']} | comment={n['comment']!r}{detail}")

    pins = state.get("pins", {})
    if pins:
        lines.append(f"  Pins cached for {len(pins)} node(s) - use GET_NODE_PINS only if wiring new nodes")

    variables = state.get("variables", {})
    if variables:
        lines.append(f"  Blueprint variables: {', '.join(variables.keys())}")

    components = state.get("components", {})
    if components:
        lines.append(f"  Components: {', '.join(f'{n} ({v[\"class\"]})' for n, v in components.items())}")

    lines.append("")
    lines.append("Use GUIDs above directly. Only call LIST_NODES if you need to find a node not listed here.")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# WebSocket bridge command executor
# ---------------------------------------------------------------------------

async def execute_bridge_command(bridge: UnrealBridge, command: str,
                                  session: dict | None = None) -> str:
    command = command.strip()
    if not command:
        return json.dumps({"success": False, "message": "Empty command"})

    print(f"  -> {command}")
    result = await bridge._send_command(command)

    if result:
        status = "OK  " if result.get("success") else "FAIL"
        print(f"  <- [{status}] {result.get('message', '')}")
        if result.get("payload"):
            preview = result["payload"][:120]
            print(f"         {preview}{'...' if len(result['payload']) > 120 else ''}")
        if session is not None:
            update_session_from_command(session, command, result)
    else:
        result = {"success": False, "message": "No response from GraphBridge"}
        print(f"  <- [ERR ] No response")

    return json.dumps(result)


# ---------------------------------------------------------------------------
# Anthropic backend
# ---------------------------------------------------------------------------

_ANTHROPIC_TOOLS = [{
    "name": "graph_command",
    "description": (
        "Send a pipe-delimited command to Unreal GraphBridge and get the result. "
        "Use for ALL Blueprint graph operations: LIST_NODES, GET_NODE_PINS, "
        "FIND_NODE_CLASS, LIST_ASSETS, SPAWN_NODE, CONNECT_PINS, DISCONNECT_PINS, "
        "DELETE_NODE, CLEAR_NODES, SET_PIN_DEFAULT, SET_VARIABLE_REF, "
        "SET_FUNCTION_REF, SET_INPUT_ACTION, SPAWN_VARIABLE, SET_VARIABLE_DEFAULT, "
        "COMPILE, SAVE_BLUEPRINT, OPEN_BLUEPRINT, CLOSE_BLUEPRINT."
    ),
    "input_schema": {
        "type": "object",
        "properties": {
            "command": {
                "type": "string",
                "description": "Pipe-delimited command e.g. LIST_NODES|/Game/BP_X.BP_X"
            }
        },
        "required": ["command"]
    }
}]


async def run_anthropic_agent(task: str, bp_path: str = "", max_turns: int = 20):
    """Agentic loop using Anthropic Claude."""
    try:
        import anthropic
    except ImportError:
        print("ERROR: anthropic not installed. Run: pip install anthropic")
        return

    api_key = _ANTHROPIC_KEY
    if not api_key:
        print("ERROR: ANTHROPIC_API_KEY not set.")
        print("  Windows:   set ANTHROPIC_API_KEY=<your-key>")
        print("  Mac/Linux: export ANTHROPIC_API_KEY=<your-key>")
        print("  Or edit graphbridge_config.py")
        return

    client = anthropic.AsyncAnthropic(api_key=api_key)

    bridge = UnrealBridge(uri=_BRIDGE_URI)
    if not await bridge.connect():
        return

    session = load_session(bp_path) if bp_path else {}
    session_context = build_session_context(session)
    if session_context:
        task = session_context + "\n\nTASK: " + task
        print(f"[Session] Injecting context for {bp_path}")

    print(f"\n[Task] {task[:200]}{'...' if len(task) > 200 else ''}\n")
    print(f"[Model] {_DEFAULT_MODEL} (Anthropic)\n")

    messages = [{"role": "user", "content": task}]

    for turn in range(max_turns):
        print(f"\n--- Turn {turn + 1} [Anthropic] ---")

        try:
            response = await client.messages.create(
                model=_DEFAULT_MODEL,
                max_tokens=4096,
                system=SYSTEM_PROMPT,
                tools=_ANTHROPIC_TOOLS,
                messages=messages,
            )
        except Exception as e:
            print(f"ERROR: Anthropic API error: {e}")
            break

        # Serialize content for message history (Anthropic SDK objects are not JSON-serializable)
        messages.append({"role": "assistant", "content": response.content})

        for block in response.content:
            if hasattr(block, "text") and block.text:
                text_preview = block.text[:200]
                print(f"  [Claude] {text_preview}{'...' if len(block.text) > 200 else ''}")

        if response.stop_reason == "end_turn":
            break

        if response.stop_reason != "tool_use":
            print(f"  Unexpected stop_reason: {response.stop_reason}")
            break

        tool_results = []
        for block in response.content:
            if block.type != "tool_use":
                continue
            cmd = block.input.get("command", "")
            bridge_result = await execute_bridge_command(bridge, cmd, session)
            tool_results.append({
                "type": "tool_result",
                "tool_use_id": block.id,
                "content": bridge_result,
            })

        messages.append({"role": "user", "content": tool_results})

    else:
        print(f"\n[Agent] Reached max_turns ({max_turns}).")

    if bp_path and session:
        save_session(bp_path, session)

    await bridge.close()


# ---------------------------------------------------------------------------
# OpenAI backend
# ---------------------------------------------------------------------------

_OPENAI_TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "graph_command",
            "description": (
                "Send a pipe-delimited command to the Unreal GraphBridge and get the result. "
                "Use for ALL Blueprint graph operations."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "command": {
                        "type": "string",
                        "description": "Pipe-delimited command e.g. LIST_NODES|/Game/BP_X.BP_X"
                    }
                },
                "required": ["command"]
            }
        }
    }
]


async def run_openai_agent(task: str, bp_path: str = "", max_turns: int = 20):
    """Agentic loop using OpenAI ChatGPT."""
    try:
        from openai import AsyncOpenAI
    except ImportError:
        print("ERROR: openai not installed. Run: pip install openai")
        return

    api_key = _OPENAI_KEY
    if not api_key:
        print("ERROR: OPENAI_API_KEY not set.")
        print("  Windows:   set OPENAI_API_KEY=...")
        print("  Mac/Linux: export OPENAI_API_KEY=...")
        print("  Or edit graphbridge_config.py")
        return

    client = AsyncOpenAI(api_key=api_key)

    bridge = UnrealBridge(uri=_BRIDGE_URI)
    if not await bridge.connect():
        return

    session = load_session(bp_path) if bp_path else {}
    session_context = build_session_context(session)
    if session_context:
        task = session_context + "\n\nTASK: " + task
        print(f"[Session] Injecting context for {bp_path}")

    print(f"\n[Task] {task[:200]}{'...' if len(task) > 200 else ''}\n")
    print(f"[Model] {OPENAI_MODELS[0]} (OpenAI)\n")

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user",   "content": task},
    ]

    for turn in range(max_turns):
        print(f"\n--- Turn {turn + 1} [OpenAI] ---")

        response = None
        for model in OPENAI_MODELS:
            try:
                response = await client.chat.completions.create(
                    model=model,
                    messages=messages,
                    tools=_OPENAI_TOOLS,
                    tool_choice="auto",
                )
                if model != OPENAI_MODELS[0]:
                    print(f"  [Fallback] Using {model}")
                break
            except Exception as e:
                err = str(e)
                if "429" in err or "rate_limit" in err.lower():
                    print(f"  [Rate limit] {model} — waiting 30s...")
                    await asyncio.sleep(30)
                elif "503" in err or "404" in err:
                    print(f"  [{model}] unavailable, trying next...")
                else:
                    print(f"ERROR: {e}")
                    await bridge.close()
                    return

        if response is None:
            print("ERROR: All OpenAI models unavailable. Please try again later.")
            break

        oai_msg = response.choices[0].message
        messages.append(oai_msg)

        if not oai_msg.tool_calls:
            print(f"\n[ChatGPT] {oai_msg.content}")
            break

        for tc in oai_msg.tool_calls:
            args = json.loads(tc.function.arguments)
            cmd = args.get("command", "")
            bridge_result = await execute_bridge_command(bridge, cmd, session)
            messages.append({
                "role":         "tool",
                "tool_call_id": tc.id,
                "content":      bridge_result,
            })

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

    use_openai    = "--openai" in args
    clear_session = "--clear-session" in args
    args = [a for a in args if a not in ("--openai", "--clear-session")]

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

    if use_openai:
        asyncio.run(run_openai_agent(task, bp_path=bp_path))
    else:
        asyncio.run(run_anthropic_agent(task, bp_path=bp_path))
