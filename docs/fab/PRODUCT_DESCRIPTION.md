# GraphBridge AI — Product Description

## The Only Unreal AI Bridge That Checks Its Work

Build and verify Blueprints, Animation Blueprints, and full character rigs — from graph logic through skeletal mesh, physics, IK, and weapons — with real compile-error and pin-connection checks, not guesses. Now with **AI vision**: render scenes and see what the agent built.

### Core Capability: Automated Blueprint & Animation Editing

GraphBridge exposes Unreal's editor via MCP (Model Context Protocol), so Claude, GPT, or any MCP-capable AI agent can:

- **Create and edit Blueprints** — spawn nodes, connect pins, set defaults, compile, verify errors
- **Build complete character pipelines** — mesh, skeleton, animation blueprint, montages, physics assets
- **Construct animation systems** — state machines, blendspaces, montages with notifies, IK rigs
- **Design UI** — UMG widgets, buttons, text, hierarchies  
- **Populate levels** — actors, platforms, spawners, checkpoints
- **Debug and iterate** — get compile errors, read current values, re-run commands

Every command is verified against actual engine APIs. If a blueprint compiles, it compiles. If pins connect, the connection is real. Guesses are eliminated.

### New in 1.5.0: AI Vision

For the first time, the agent can *see* what it built.

**Why it matters:** Numeric verification is necessary but not sufficient. A dagger blade attached at perfect mathematical alignment can still pass through a palm. A weapon mesh scaled by measurement can still look comically large. Arena geometry can be built at wrong angles while measurement confirms the wrong value.

`CAPTURE_VIEW` renders from any viewpoint (any actor, any socket/bone, any distance/angle) and returns PNG images directly to the agent. Multi-angle support (up to 8 simultaneous views) catches orientation ambiguity that single viewpoints hide.

Images reach the agent as proper MCP image content blocks — not base64 text that requires explanation. The agent's vision model processes them directly.

### Workflow Example

Agent builds a weapon rig in 10 minutes:
1. Create character Blueprint → add skeletal mesh → add anim blueprint → compile
2. Create weapon Blueprint → attach to hand socket → apply material
3. `CAPTURE_VIEW` from 3 angles → agent sees blade positioning is wrong
4. Adjust bone transforms → `CAPTURE_VIEW` → verify alignment → iterate

Without vision, this workflow would require manual feedback at every step.

### What's Included

- **Full command reference** with examples (90+ commands)
- **Python integration** via UE's PythonScriptPlugin (both included with Unreal, no separate install)
- **MCP support** for Claude Code, Cursor, and any MCP client
- **WebSocket bridge** for custom LLM clients
- **Editor and PIE support** — commands work in editor and play-in-editor

### Requirements

- **Unreal Engine 5.8+** (uses enhanced input, MCP, and recent editor APIs)
- **Windows 64-bit** (plugin compiled for Win64; other platforms can build from source)
- **EnhancedInput plugin** (included with UE, enabled automatically)
- **PythonScriptPlugin** (included with UE, enabled automatically)

### Getting Started

1. Install plugin to your project's `Plugins/` directory
2. Open the project — plugin initializes on load
3. Start the WebSocket bridge: `UGraphBridgeAutomationLibrary::StartGraphBridgeServer()` (port 8080, configurable)
4. Or start the MCP server: `UGraphBridgeAutomationLibrary::StartMCPServer()` (port 8090, configurable)
5. Point your AI client at the running server
6. Call `LIST_ASSETS` or `LIST_NODES` to verify connection

Full command examples and workflows are in the documentation.

### Support

- GitHub issues: [corwindevacct-droid/Graph-Bridge-v2](https://github.com/corwindevacct-droid/Graph-Bridge-v2)
- All tool errors include diagnostic messages, not bare failure codes
- Commands emit JSON responses with message text and detailed payloads

### License & Attribution

Created by Corwin Hicks. Uses IXWebSocket (BSD 3-Clause) for WebSocket transport.
