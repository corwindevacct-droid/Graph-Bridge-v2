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

## Option 2: Manual Installation

1. Clone the repository:
\\\ash
cd YourProject/Plugins
git clone https://github.com/YourOrg/GraphBridgev2.git
\\\

2. Regenerate Visual Studio project files
3. Compile the plugin
4. Restart the editor

## Verification

After installation, verify that:

1. **Plugin loads**: No "Incompatible or missing module" errors in the Output Log
2. **WebSocket server active**: Connect to http://localhost:8080
3. **MCP server active**: Connect to http://localhost:8090
4. **Python toolset available**: Content/Python/GraphBridgeToolset.py exists

## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common issues and solutions.
