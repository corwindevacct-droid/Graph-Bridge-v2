# GraphBridge AI

AI-powered Blueprint graph assistant for Unreal Engine 5. Chat with Claude directly inside the editor to inspect, summarize, and manipulate your Blueprint graphs using natural language.

---

## Installation

1. Download `GraphBridgev2_v1.0.4_UE5.5.zip`
2. Extract into `YourProject/Plugins/GraphBridgev2/`
3. Open your project in UE 5.5 â€” when prompted, enable the plugin and restart the editor
4. Go to **Window â†’ GraphBridge AI** to open the panel

---

## First Use

1. Click **Start Server** â€” confirm the green dot shows "Running on port 8080"
2. Enter your Anthropic API key (`sk-ant-...`) in the API Key field
3. Click **Save Settings**
4. Open any Blueprint in the editor
5. Click a preset task button or type a question and press **Run Task**
6. Claude will read your graph and respond in the chat panel

---

## Requirements

- Unreal Engine 5.5 or later
- Anthropic API key ([console.anthropic.com](https://console.anthropic.com))
- Python 3.x (for the companion WebSocket bridge server)
- Windows 64-bit

---

## Architecture

```
Slate Chat Panel -> C++ GraphBridgeLLMClient -> Anthropic API (claude-sonnet-4-6)
                            |
            C++ WebSocket Server (IXWebSocket, port 8080)
                            |
         Unreal Editor (Blueprint graph read/write via GraphBridgeAutomationLibrary)
```

The Python scripts (graphbridge_bridge.py, graphbridge_tools.py, graphbridge_server.py) are development/debug utilities only - they are NOT part of the runtime path.

---

## Python Tools

### Setup

1. Install Python dependencies:
   ```
   pip install websockets anthropic openai
   ```
2. Configure your API key — edit `Content/Python/graphbridge_config.py` and set
   your key, or use environment variables:
   ```
   # Windows
   set ANTHROPIC_API_KEY=<your-key-here>
   # Mac/Linux
   export ANTHROPIC_API_KEY=<your-key-here>
   ```
3. Open your Unreal project with GraphBridge AI enabled.
4. Click **Start Server** in the GraphBridge AI panel (**Window > GraphBridge AI**).
5. Confirm the green dot shows "Running on port 8080".

### graphbridge_tools.py — Find assets, inspect pins, list nodes

```
python graphbridge_tools.py find BP_MyCharacter
python graphbridge_tools.py nodes /Game/BP_MyCharacter.BP_MyCharacter
python graphbridge_tools.py pins /Game/BP_MyCharacter.BP_MyCharacter BeginPlay
```

### graphbridge_scan.py — Scan entire project, save manifest

```
python graphbridge_scan.py
python graphbridge_scan.py BP_MyCharacter
```

### graphbridge_variables.py — Bulk create Blueprint variables

Edit the `STAT_VARIABLES` list in the file, then run:

```
python graphbridge_variables.py
```

### graphbridge_animation.py — Create montages, blend spaces, assign AnimBPs

```
python graphbridge_animation.py list --type AnimMontage
python graphbridge_animation.py montage --skeleton /Game/SK_Hero --name AM_Attack
```

### graphbridge_agent.py — Claude agentic loop (natural language)

```
python graphbridge_agent.py "Add a float variable Speed to /Game/BP_MyChar.BP_MyChar"
python graphbridge_agent.py "Wire BeginPlay to register the IMC on /Game/BP_MyChar.BP_MyChar"
python graphbridge_agent.py --openai "List all nodes in /Game/BP_MyChar.BP_MyChar"
```

### graphbridge_server.py — Raw command debug shell

```
python graphbridge_server.py
> LIST_NODES|/Game/BP_MyCharacter.BP_MyCharacter
> COMPILE|/Game/BP_MyCharacter.BP_MyCharacter
```

### Example Scripts

- `graphbridge_example_flight.py` - Wire a flight ability Blueprint end-to-end
- `graphbridge_example_anim_flight.py` - Wire an animation Blueprint end-to-end

### From the Unreal Python Console

```python
import graphbridge_bridge
import asyncio
bridge = graphbridge_bridge.UnrealBridge()
asyncio.run(bridge.connect())
```

## Implemented Commands

The WebSocket bridge supports 50+ commands including:

**Graph Manipulation:** `SPAWN_NODE`, `CONNECT_PINS`, `DISCONNECT_PINS`, `DELETE_NODE`, `CLEAR_NODES`, `SET_PIN_DEFAULT`

**Variables:** `SPAWN_VARIABLE`, `SET_VARIABLE_DEFAULT`, `SET_VARIABLE_REF`, `SET_VARIABLE_TYPE`, `LIST_VARIABLES`

**Blueprint Lifecycle:** `COMPILE`, `SAVE_BLUEPRINT`, `OPEN_BLUEPRINT`, `CLOSE_BLUEPRINT`

**Discovery:** `LIST_NODES`, `GET_NODE_PINS`, `LIST_ASSETS`, `FIND_NODE_CLASS`

**Components & Input:** `ADD_COMPONENT`, `SET_INPUT_ACTION`, `SET_FUNCTION_REF`, `SET_EVENT_REF`, `ADD_IMC_TO_CHARACTER`

**Animation:** `SET_ANIM_CLASS`, `SET_MONTAGE_SLOT`, `ADD_MONTAGE_SECTION`, `LIST_BLENDSPACES`

---

## Known Limitations

- Windows 64-bit only (Mac/Linux planned for v1.2)
- Python bridge server must be running locally for graph introspection
- Requires an active Anthropic API key (billed at standard Anthropic rates)

---

## License

Copyright 2026 Corwin Hicks. All Rights Reserved.

Third-party: IXWebSocket (MIT License) - https://github.com/machinezone/IXWebSocket
