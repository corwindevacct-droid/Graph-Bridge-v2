# Installation Guide

## Prerequisites
- Unreal Engine 5.8.0 or later
- Python 3.9 or later (for Python toolset)
- Visual Studio 2022 (for Windows builds)

## Option 1: Fab Marketplace (Recommended)

1. Open Unreal Engine 5.8+
2. Click **Marketplace** in the top menu
3. Search for "GraphBridgev2"
4. Click **Install to Engine** or **Install to Project**
5. Restart the editor
6. Enable the plugin: **Edit** → **Plugins** → Search "GraphBridge" → Check **Enabled**

## Option 2: Manual Installation (source access only)

This path requires access to the plugin's source repository — it isn't
available to a Fab customer using the packaged plugin from Option 1.

1. Copy or clone the plugin source into your project's `Plugins/` folder
2. Regenerate Visual Studio project files
3. Compile the plugin
4. Restart the editor

## Verification

After installation, verify that:

1. **Plugin loads**: No "Incompatible or missing module" errors in the Output Log
2. **WebSocket server active**: `LogGraphBridge: GraphBridge: WebSocket server started on port 8080` appears in the Output Log
3. **MCP server active**: `LogGraphBridge: GraphBridge MCP: listening on http://127.0.0.1:8090/mcp` appears in the Output Log
4. **Session token minted**: `Saved/GraphBridge/session_token.txt` exists — every WebSocket client needs this to connect (see USAGE.md)

## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common issues and solutions.
