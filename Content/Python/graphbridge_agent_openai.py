# GraphBridge AI â€” graphbridge_agent_openai.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
run_agent_openai.py
AI-driven Blueprint automation using ChatGPT and the GraphBridge WebSocket.

Setup:
    pip install openai websockets

    Windows:   set OPENAI_API_KEY=your_key_here
    Mac/Linux: export OPENAI_API_KEY=your_key_here

Usage:
    python run_agent_openai.py "Add a float variable called Speed to /Game/BP_MyCharacter.BP_MyCharacter"
"""

import asyncio
import os
import sys
import json
import time
from openai import OpenAI
from graphbridge_bridge import UnrealBridge

# ---------------------------------------------------------------------------
# Model fallback list
# ---------------------------------------------------------------------------

MODELS = [
    "gpt-4o",
    "gpt-4o-mini",
    "gpt-3.5-turbo",
]

# ---------------------------------------------------------------------------
# Tool definition — OpenAI function calling format
# ---------------------------------------------------------------------------

TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "graph_command",
            "description": (
                "Send a pipe-delimited command to the Unreal GraphBridge and get the result. "
                "Use for ALL Blueprint graph operations. "
                "Commands: LIST_NODES, GET_NODE_PINS, FIND_NODE_CLASS, LIST_ASSETS, "
                "SPAWN_NODE, CONNECT_PINS, DISCONNECT_PINS, DELETE_NODE, CLEAR_NODES, "
                "SET_PIN_DEFAULT, SET_VARIABLE_REF, SET_FUNCTION_REF, SET_INPUT_ACTION, "
                "SPAWN_VARIABLE, SET_VARIABLE_DEFAULT, COMPILE, SAVE_BLUEPRINT, "
                "OPEN_BLUEPRINT, CLOSE_BLUEPRINT."
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

# ---------------------------------------------------------------------------
# System prompt — identical to Gemini version
# ---------------------------------------------------------------------------

SYSTEM_PROMPT = """You are an Unreal Engine Blueprint automation assistant.
You have access to a live Unreal Editor via the graph_command tool.

... (same SYSTEM_PROMPT as run_agent.py) ...
"""

# ---------------------------------------------------------------------------
# Retry helper
# ---------------------------------------------------------------------------

def chat_with_retry(client, messages, max_attempts: int = 3):
    for model in MODELS:
        for attempt in range(max_attempts):
            try:
                response = client.chat.completions.create(
                    model=model,
                    messages=messages,
                    tools=TOOLS,
                    tool_choice="auto",
                )
                if model != MODELS[0]:
                    print(f"  [Fallback] Completed using {model}")
                return response
            except Exception as e:
                err = str(e)
                if "rate_limit" in err.lower() or "429" in err:
                    wait = 30 * (attempt + 1)
                    print(f"  [Rate limit] {model} — waiting {wait}s...")
                    time.sleep(wait)
                elif "503" in err or "unavailable" in err.lower():
                    print(f"  [Unavailable] {model} — trying next model...")
                    break
                elif "404" in err or "not found" in err.lower():
                    print(f"  [Not found] {model} — trying next model...")
                    break
                else:
                    raise

    print("\n  [OpenAI] All models unavailable. Please try again later.")
    return None

# ---------------------------------------------------------------------------
# Tool executor
# ---------------------------------------------------------------------------

async def execute_tool(bridge: UnrealBridge, tool_call) -> str:
    args = json.loads(tool_call.function.arguments)
    command = args.get("command", "").strip()
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
    else:
        result = {"success": False, "message": "No response from GraphBridge"}
        print(f"  <- [ERR ] No response")

    return json.dumps(result)

# ---------------------------------------------------------------------------
# Agentic loop
# ---------------------------------------------------------------------------

async def run_openai_agent(task: str, max_turns: int = 20):
    api_key = os.environ.get("OPENAI_API_KEY")
    if not api_key:
        print("ERROR: OPENAI_API_KEY environment variable is not set.")
        print("  Windows:   set OPENAI_API_KEY=your_key")
        print("  Mac/Linux: export OPENAI_API_KEY=your_key")
        return

    client = OpenAI(api_key=api_key)

    bridge = UnrealBridge()
    if not await bridge.connect():
        return

    print(f"\n[Task] {task}\n")

    messages = [
        {"role": "system", "content": SYSTEM_PROMPT},
        {"role": "user",   "content": task},
    ]

    for turn in range(max_turns):
        print(f"\n--- Turn {turn + 1} ---")

        response = chat_with_retry(client, messages)
        if response is None:
            await bridge.close()
            return

        msg = response.choices[0].message
        messages.append(msg)

        if not msg.tool_calls:
            print(f"\n[ChatGPT] {msg.content}")
            break

        for tc in msg.tool_calls:
            result_text = await execute_tool(bridge, tc)
            messages.append({
                "role":         "tool",
                "tool_call_id": tc.id,
                "content":      result_text,
            })

    else:
        print(f"\n[Agent] Reached max_turns ({max_turns}) — stopping.")

    await bridge.close()


if __name__ == "__main__":
    task = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else (
        "List all Blueprint assets in the project"
    )
    asyncio.run(run_openai_agent(task))
