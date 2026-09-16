# Usage Guide

## Endpoints

GraphBridgev2 runs two servers:

| Server | Port | Protocol | Use Case |
|--------|------|----------|----------|
| **WebSocket** | 8080 | WebSocket, pipe-delimited text commands + per-session token | Native Python/CLI tools |
| **MCP** | 8090 | MCP 0.1+ | Claude/AI integration |

Both servers are safe to run simultaneously. They do not conflict with Epic's native MCP server (:8000).

## WebSocket Usage (Native)

Every WebSocket connection must present the per-session token minted each
time the server starts, as a `?token=...` query parameter on the connection
URI — otherwise the server closes the connection before any command can be
dispatched. The bundled client below reads that token automatically from
`Saved/GraphBridge/session_token.txt`; see the Security section of the main
README for why the token exists and what it does and doesn't protect
against.

### Python Example
```python
import asyncio
from graphbridge_bridge import UnrealBridge

async def main():
    bridge = UnrealBridge()  # reads the session token automatically
    await bridge.connect()
    result = await bridge._send_command(
        "SPAWN_NODE|/Game/BP_Character|K2Node_CallFunction|MyComment|100|200"
    )
    print(result)
    await bridge.close()

asyncio.run(main())
```

### Command Line
`graphbridge_server.py` (bundled under `Content/Python/`) is an interactive
REPL against the WebSocket server — it handles the token for you:
```bash
python graphbridge_server.py
# Connected. Type a command and press Enter. Type 'quit' to exit.
> LIST_GRAPHS|/Game/BP_Character
```

## MCP Usage (AI Integration)

GraphBridgev2 is fully compatible with Claude via MCP. All 129 tools are available as typed MCP resources:

```python
import anthropic

client = anthropic.Anthropic()

# Claude can now use GraphBridge tools
response = client.messages.create(
    model="claude-opus-5",
    max_tokens=2048,
    tools=[
        # All 129 GraphBridge tools available via MCP
    ],
    messages=[{
        "role": "user",
        "content": "Create a character controller blueprint for me"
    }]
)
```

## Configuration

Both servers start automatically when the plugin loads. Configure the
WebSocket port, MCP port, and whether MCP auto-starts from
**Project Settings → Plugins → GraphBridge AI**, or directly in
`Config/DefaultEditorPerProjectUserSettings.ini`:

```ini
[/Script/GraphBridgev2.GraphBridgeSettings]
ServerPort=8080
MCPServerPort=8090
bEnableMCPServer=True
```

The WebSocket port can also be overridden independently via
`Config/DefaultEditor.ini`:

```ini
[GraphBridge]
Port=8080
```

## Tool Categories

All 129 tools are organized by category. See [API.md](API.md) for the complete reference.
