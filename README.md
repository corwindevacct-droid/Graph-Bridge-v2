# GraphBridge AI

AI-powered Blueprint graph assistant for Unreal Engine 5. Chat with Claude or GPT directly inside the editor to inspect, summarize, and manipulate your Blueprint graphs using natural language.

---

## Installation

1. Download `GraphBridgev2_v1.0.10_UE5.7.zip`
2. Extract into `YourProject/Plugins/GraphBridgev2/`
3. Open your project in UE 5.7 â€” when prompted, enable the plugin and restart the editor
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

- Unreal Engine 5.7 or later
- Anthropic API key ([console.anthropic.com](https://console.anthropic.com)) — only needed for the in-editor chat panel, not for the WebSocket/MCP bridges themselves
- Python 3.x (for the companion WebSocket bridge server)
- Windows 64-bit

---

## Architecture

```
Slate Chat Panel -> C++ GraphBridgeLLMClient -> Anthropic API (claude-sonnet-4-6)
                            |
            C++ WebSocket Server (IXWebSocket, port 8080)
                            |
       MCP Server (JSON-RPC 2.0 over HTTP, port 8090) <- any MCP-compatible client
                            |
         Unreal Editor (Blueprint graph read/write via GraphBridgeAutomationLibrary)
```

Both the WebSocket bridge and the MCP server start automatically with the editor
(configurable in **Project Settings -> Plugins -> GraphBridge AI**) and route through
the exact same command dispatcher — there is exactly one command implementation to
maintain regardless of which transport a client connects over.

The Python scripts (graphbridge_bridge.py, graphbridge_tools.py, graphbridge_server.py) are development/debug utilities only - they are NOT part of the runtime path.

---

## MCP Transport

GraphBridge also speaks the standard [Model Context Protocol](https://modelcontextprotocol.io) (spec version `2025-06-18`), as a second transport alongside the WebSocket bridge above. Any MCP-compatible client — Claude Code, Cursor, Windsurf, VS Code — can connect directly with no custom client needed, unlike the WebSocket bridge which requires the Python or C++ tooling in this repo.

- **Endpoint:** `http://127.0.0.1:8090/mcp` (HTTP POST, JSON-RPC 2.0).
- **Starts automatically** with the editor, alongside the WebSocket bridge — no manual step needed. Configurable in **Project Settings -> Plugins -> GraphBridge AI**:
  - **Enable MCP Server** (default on)
  - **MCP Server Port** (default `8090`)

  For manual control without restarting the editor (e.g. toggling it off temporarily), the Blueprint-callable `Start MCP Server` / `Stop MCP Server` / `Is MCP Server Running` nodes (or their `UGraphBridgeAutomationLibrary` C++/Python equivalents) are still available independently of the automatic path.
- **Methods supported:** `initialize`, `tools/list`, `tools/call`, `ping`. Every one of GraphBridge's 81 bridge commands (`LIST_NODES`, `SPAWN_NODE`, `CONNECT_PINS`, `CREATE_FUNCTION`, `SPAWN_ACTOR_IN_LEVEL`, `CREATE_WIDGET_BLUEPRINT`, `ADD_MATERIAL_NODE`, etc.) is exposed as an MCP tool with an auto-generated JSON Schema.
- **Undo:** MCP calls route through the exact same command dispatcher the WebSocket bridge uses (`DispatchCommandSync`), so every mutating tool call gets the same `FScopedTransaction` undo/redo support — Ctrl+Z in the editor undoes an MCP-triggered change exactly like a WebSocket- or Slate-panel-triggered one.
- **Transport notes:** implements the Streamable HTTP transport without Server-Sent Events (every request gets a single JSON response, which the spec allows) and without session IDs (both optional for a single-client local tool server). Binds to `localhost` only and rejects any request carrying an `Origin` header, as a DNS-rebinding mitigation.

Example client config (Claude Code, Cursor, etc. — check your client's docs for the exact file):
```json
{
  "mcpServers": {
    "graphbridge": {
      "url": "http://127.0.0.1:8090/mcp"
    }
  }
}
```

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

The WebSocket bridge and the MCP transport share the same 81 commands (every command
is available on both — MCP is just a second way to reach the identical dispatcher):

**Graph Manipulation:** `SPAWN_NODE`, `SPAWN_NODE_IN_GRAPH`, `CONNECT_PINS`, `DISCONNECT_PINS`, `DELETE_NODE`, `CLEAR_NODES`, `SET_PIN_DEFAULT`, `GET_PIN_DEFAULT`, `GET_PIN_CONNECTIONS`, `SET_NODE_POSITION`, `CREATE_FUNCTION`

**Variables:** `SPAWN_VARIABLE`, `ADD_VARIABLE`, `SET_VARIABLE_DEFAULT`, `SET_VARIABLE_REF`, `SET_VARIABLE_TYPE`, `LIST_VARIABLES`

**Blueprint Lifecycle:** `CREATE_BLUEPRINT`, `COMPILE`, `GET_COMPILE_ERRORS`, `SAVE_BLUEPRINT`, `OPEN_BLUEPRINT`, `CLOSE_BLUEPRINT`

**Discovery:** `LIST_NODES`, `GET_NODE_PINS`, `LIST_ASSETS`, `FIND_NODE_CLASS`, `LIST_ASSET_PROPERTIES`, `GET_ASSET_PROPERTY`, `SET_ASSET_PROPERTY`

**Components & Input:** `ADD_COMPONENT`, `SET_INPUT_ACTION`, `SET_FUNCTION_REF`, `SET_EVENT_REF`, `CREATE_INPUT_ACTION`, `CREATE_IMC`, `ADD_IMC_MAPPING`, `ADD_IMC_TO_CHARACTER`

**Animation:** `SET_ANIM_CLASS`, `SET_MONTAGE_SLOT`, `ADD_MONTAGE_SECTION`, `ADD_MONTAGE_NOTIFY`, `ADD_MONTAGE_NOTIFY_STATE`, `LIST_BLENDSPACES`, `LIST_SKELETON_SOCKETS`, `ADD_SKELETON_SOCKET`

> `ADD_MONTAGE_NOTIFY` adds a single-frame `UAnimNotify`.
> `ADD_MONTAGE_NOTIFY_STATE` adds a `UAnimNotifyState` — a begin/end window with a
> required `duration > 0`, used for things like weapon-hitbox active frames.
> The two are separate opcodes because `UAnimNotify` and `UAnimNotifyState` are
> sibling classes, so neither call accepts the other's class.

**Level & Actors:** `SPAWN_ACTOR_IN_LEVEL`, `LIST_LEVEL_ACTORS`, `SET_ACTOR_TRANSFORM`, `DELETE_LEVEL_ACTOR`, `GET_PLAYER_START`, `SET_LEVEL_GAMEMODE`

**UMG Widgets** *(new in v1.0.8)*: `CREATE_WIDGET_BLUEPRINT`, `ADD_WIDGET_ELEMENT`, `SET_WIDGET_TEXT`

**Materials** *(new in v1.0.8)*: `CREATE_MATERIAL`, `ADD_MATERIAL_NODE`, `CONNECT_MATERIAL_PINS`, `SET_MATERIAL_RESULT`, `COMPILE_MATERIAL`, `CLOSE_MATERIAL`

**DataTables:** `LIST_DATATABLE_ROWS`, `ADD_DATATABLE_ROW`, `DELETE_DATATABLE_ROW`, `RENAME_DATATABLE_ROW`

---

## Known Limitations

- Windows 64-bit only (Mac/Linux planned for v1.2)
- Python bridge server must be running locally for graph introspection
- Requires an active Anthropic API key (billed at standard Anthropic rates)

---

## License

Copyright 2026 Corwin Hicks. All Rights Reserved.

Third-party: IXWebSocket (MIT License) - https://github.com/machinezone/IXWebSocket
