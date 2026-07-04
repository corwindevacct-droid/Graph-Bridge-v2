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

# API Keys - set as environment variables or paste directly here
ANTHROPIC_API_KEY = os.environ.get("ANTHROPIC_API_KEY", "")
OPENAI_API_KEY    = os.environ.get("OPENAI_API_KEY", "")

# Default model
DEFAULT_MODEL = "claude-sonnet-4-6"

# Default Blueprint path - replace with your own
DEFAULT_BP = "/Game/YourProject/Blueprints/BP_YourCharacter"

# HTTP Remote Control port (Unreal default)
UNREAL_HTTP_PORT = 30010
