# GraphBridge AI - init_unreal.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.
#
# Auto-executed by Unreal Engine on startup when GraphBridgev2 is enabled.
# Registers graphbridge_* modules so they are importable from any script.

import sys
import os
import unreal

_this_dir = os.path.dirname(os.path.abspath(__file__))
if _this_dir not in sys.path:
    sys.path.insert(0, _this_dir)

try:
    import graphbridge_config as _cfg
    unreal.log(f"[GraphBridge AI] Bridge URI: {_cfg.BRIDGE_URI}")
    if not _cfg.ANTHROPIC_API_KEY:
        unreal.log_warning("[GraphBridge AI] ANTHROPIC_API_KEY not set.")
except ImportError:
    unreal.log_warning("[GraphBridge AI] graphbridge_config.py not found - using defaults.")

try:
    import websockets  # noqa: F401
except ImportError:
    unreal.log_warning(
        "[GraphBridge AI] websockets not found. "
        "Run: pip install websockets anthropic openai"
    )

unreal.log("[GraphBridge AI] Python tools ready.")
unreal.log("  Modules: graphbridge_bridge, graphbridge_tools, graphbridge_scan,")
unreal.log("           graphbridge_variables, graphbridge_animation, graphbridge_agent,")
unreal.log("           graphbridge_server, graphbridge_http_utils")
