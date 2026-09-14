# GraphBridge AI - graphbridge_config.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
graphbridge_config.py
GraphBridge AI - Central configuration.
Edit this file once for your project then run any graphbridge_* script.
"""

import os

# WebSocket bridge URI - must match the port shown in the GraphBridge AI panel
BRIDGE_URI = "ws://127.0.0.1:8080"


def _read_session_token() -> str:
    """
    Reads the per-session auth token the C++ server mints on every
    StartGraphBridgeServer() call and mirrors to Saved/GraphBridge/session_token.txt.
    Every WebSocket connection must present this token or the server closes it
    before any command can be dispatched -- see the plugin's README.md
    Security section.

    This file (Content/Python/graphbridge_config.py) always lives at a fixed
    depth under the project root for any install of this plugin:
        <ProjectRoot>/Plugins/GraphBridgev2/Content/Python/graphbridge_config.py
    so walking up four parents from here reaches <ProjectRoot> reliably,
    without needing the `unreal` module (this file is also imported by
    standalone scripts run outside the editor process).
    """
    this_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", "..", ".."))
    token_path = os.path.join(project_root, "Saved", "GraphBridge", "session_token.txt")
    try:
        with open(token_path, "r", encoding="utf-8") as f:
            return f.read().strip()
    except OSError:
        return ""


# Session token required by every WebSocket connection. Regenerated each time
# the editor's GraphBridge server starts -- read fresh at import time, not
# cached across long-running processes.
SESSION_TOKEN = _read_session_token()

# API Keys for the standalone Python agent (graphbridge_agent.py) -- these are
# separate from the in-editor chat panel's key, which now lives in OS
# credential storage (Windows Credential Manager / macOS Keychain), not here.
# Environment variables only -- do not hardcode a key in this file, it is not
# gitignored by default and would land in version control.
ANTHROPIC_API_KEY = os.environ.get("ANTHROPIC_API_KEY", "")
OPENAI_API_KEY    = os.environ.get("OPENAI_API_KEY", "")

# Default model
DEFAULT_MODEL = "claude-sonnet-4-6"

# Default Blueprint path - replace with your own
DEFAULT_BP = "/Game/YourProject/Blueprints/BP_YourCharacter"

# HTTP Remote Control port (Unreal default)
UNREAL_HTTP_PORT = 30010
