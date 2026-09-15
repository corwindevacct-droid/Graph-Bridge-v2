# Usage Guide

## Endpoints

GraphBridgev2 runs two servers:

| Server | Port | Protocol | Use Case |
|--------|------|----------|----------|
| **WebSocket** | 8080 | HTTP/JSON-RPC 2.0 | Native Python/CLI tools |
| **MCP** | 8090 | MCP 0.1+ | Claude/AI integration |

Both servers are safe to run simultaneously. They do not conflict with Epic's native MCP server (:8000).

## WebSocket Usage (Native)

### Python Example
\\\python
import requests
import json

# Spawn a node
response = requests.post('http://localhost:8080', json={
    'Op': 'SPAWN_NODE',
    'P': ['/Game/BP_Character', 'K2Node_CallFunction', 'MyComment', '100', '200']
})
print(response.json())
\\\

### Command Line
\\\ash
curl -X POST http://localhost:8080 \\
  -H "Content-Type: application/json" \\
  -d '{
    "Op": "LIST_GRAPHS",
    "P": ["/Game/BP_Character"]
  }'
\\\

## MCP Usage (AI Integration)

GraphBridgev2 is fully compatible with Claude via MCP. All 129 tools are available as typed MCP resources:

\\\python
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
\\\

## Configuration

Both servers start automatically when the plugin loads. Port configuration in GraphBridgeSettings.ini:

\\\ini
[GraphBridge.Server]
WebSocketPort=8080
MCPPort=8090
\\\

## Tool Categories

All 129 tools are organized by category. See [API.md](API.md) for the complete reference.
