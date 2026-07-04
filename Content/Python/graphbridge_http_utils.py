# GraphBridge AI â€” graphbridge_http_utils.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
mcp_blueprint_utils.py
HTTP Remote Control channel — separate from the WebSocket bridge.
Uses Unreal's built-in Remote Control HTTP API (port 30010 by default).

This is useful for console commands and asset editor control that
don't need the full GraphBridge WebSocket round-trip.
"""

import json
import os
import requests

UNREAL_HTTP_URL = "http://127.0.0.1:30010/remote/object/call"
MANIFEST_PATH = "blueprint_manifest.json"


class BlueprintUtils:
    def __init__(self):
        self.manifest = self._load_manifest()

    # ------------------------------------------------------------------
    # Manifest (local JSON cache of known asset paths)
    # ------------------------------------------------------------------

    def _load_manifest(self) -> dict:
        if os.path.exists(MANIFEST_PATH):
            try:
                with open(MANIFEST_PATH, "r") as f:
                    return json.load(f)
            except (json.JSONDecodeError, OSError):
                pass
        return {"assets": {}}

    def save_manifest(self):
        with open(MANIFEST_PATH, "w") as f:
            json.dump(self.manifest, f, indent=4)

    # ------------------------------------------------------------------
    # HTTP Remote Control
    # ------------------------------------------------------------------

    def execute_console_command(self, cmd_string: str) -> bool:
        """
        Dispatch a native Unreal console command via the HTTP Remote Control API.
        Requires the Remote Control plugin to be enabled in the project.
        """
        try:
            response = requests.put(
                UNREAL_HTTP_URL,
                json={
                    "objectPath": "/Script/Engine.Default__KismetSystemLibrary",
                    "functionName": "ExecuteConsoleCommand",
                    "parameters": {"Command": cmd_string},
                },
                headers={"Content-Type": "application/json"},
                timeout=5.0,
            )
            return response.status_code == 200
        except requests.RequestException as e:
            print(f"[HTTP] Console command failed: {e}")
            return False

    def open_asset(self, asset_path: str) -> dict:
        """
        Force-open a Blueprint asset in the Editor via console command.
        asset_path: e.g. "/Game/Blueprints/BP_MyCharacter"
        """
        print(f"[HTTP] Opening asset: {asset_path}")
        ok = self.execute_console_command(f"AssetEditor.OpenAsset {asset_path}")
        return {
            "status": "Success" if ok else "Failed",
            "details": "Asset editor tab opened." if ok else "HTTP call failed.",
        }
