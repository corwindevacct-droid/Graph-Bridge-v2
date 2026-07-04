# GraphBridge AI â€” graphbridge_scan.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
scan_project.py
Scans Blueprints via LIST_NODES + GET_NODE_PINS and saves a JSON manifest.
Always run this BEFORE any manipulation script — it gives you ground truth
on every node GUID, title, comment, and pin layout.

Usage:
    python scan_project.py                    # scans all Blueprints
    python scan_project.py BP_FlyingCharacter # filter by name
    python scan_project.py /Game/BP_FlyingCharacter.BP_FlyingCharacter

Output: project_manifest.json

Import helpers in other scripts:
    from graphbridge_scan import load_manifest, find_node, find_pin, get_bp
"""

import asyncio
import json
import sys
import os
from datetime import datetime
from graphbridge_bridge import UnrealBridge

MANIFEST_FILE = "project_manifest.json"


# ---------------------------------------------------------------------------
# Public helpers — import these in automation scripts
# ---------------------------------------------------------------------------

def load_manifest() -> dict:
    """Load the saved manifest. Returns empty dict if file not found."""
    if os.path.exists(MANIFEST_FILE):
        with open(MANIFEST_FILE, "r") as f:
            return json.load(f)
    print(f"[Manifest] {MANIFEST_FILE} not found — run scan_project.py first")
    return {}


def get_bp(manifest: dict, bp_path: str) -> dict:
    """Return the Blueprint entry, or empty dict."""
    return manifest.get("blueprints", {}).get(bp_path, {})


def find_node(manifest: dict, bp_path: str, search: str) -> dict | None:
    """
    Find a node by title, comment, class name, or GUID substring.
    Case-insensitive. Returns the node dict or None.

    Example:
        node = find_node(manifest, BP, "IA_Fly")
        node = find_node(manifest, BP, "Toggle Flight")
        node = find_node(manifest, BP, "GB_FlyWire")
    """
    bp = get_bp(manifest, bp_path)
    search_lower = search.lower()
    for node in bp.get("nodes", []):
        if (search_lower in node.get("title",      "").lower() or
            search_lower in node.get("comment",    "").lower() or
            search_lower in node.get("class_name", "").lower() or
            search_lower in node.get("guid",       "").lower()):
            return node
    return None


def find_all_nodes(manifest: dict, bp_path: str, search: str) -> list[dict]:
    """Like find_node but returns all matches."""
    bp = get_bp(manifest, bp_path)
    search_lower = search.lower()
    return [
        n for n in bp.get("nodes", [])
        if (search_lower in n.get("title",      "").lower() or
            search_lower in n.get("comment",    "").lower() or
            search_lower in n.get("class_name", "").lower() or
            search_lower in n.get("guid",       "").lower())
    ]


def find_pin(node: dict, pin_name: str, direction: str = None) -> dict | None:
    """
    Find a pin on a node dict by name (case-insensitive).
    Optionally filter by direction: "IN" or "OUT".
    Returns { "name": ..., "direction": "IN"/"OUT" } or None.
    """
    search = pin_name.lower()
    for pin in node.get("pins", []):
        if pin.get("name", "").lower() == search:
            if direction is None or pin.get("direction") == direction:
                return pin
    return None


def print_bp_summary(manifest: dict, bp_path: str):
    """Print a human-readable summary of a Blueprint's nodes."""
    bp = get_bp(manifest, bp_path)
    nodes = bp.get("nodes", [])
    print(f"\n=== {bp_path} ({len(nodes)} nodes) ===")
    for node in nodes:
        pins = node.get("pins", [])
        in_pins  = [p["name"] for p in pins if p["direction"] == "IN"]
        out_pins = [p["name"] for p in pins if p["direction"] == "OUT"]
        print(f"  [{node['class_name']}] {node['title']!r}")
        if node["comment"]:
            print(f"    comment: {node['comment']!r}")
        print(f"    guid:    {node['guid']}")
        if in_pins:  print(f"    IN:      {', '.join(in_pins)}")
        if out_pins: print(f"    OUT:     {', '.join(out_pins)}")


# ---------------------------------------------------------------------------
# Scanner
# ---------------------------------------------------------------------------

async def scan_blueprint(bridge: UnrealBridge, bp_path: str) -> dict:
    """Scan one Blueprint — list all nodes then get pins for each."""
    result = {
        "asset_path":  bp_path,
        "scanned_at":  datetime.now().isoformat(),
        "nodes":       [],
        "error":       None,
    }

    # Step 1: get all nodes via LIST_NODES
    nodes = await bridge.list_nodes(bp_path)
    if not nodes:
        result["error"] = "LIST_NODES returned empty — BP may have no EventGraph nodes"
        print(f"  (no nodes found)")
        return result

    print(f"  Found {len(nodes)} node(s) — fetching pins...")

    # Step 2: get pins for every node
    enriched = []
    for node in nodes:
        guid = node["guid"]
        pin_result = await bridge.get_node_pins(bp_path, guid)
        pins = []
        if pin_result and pin_result.get("success") and pin_result.get("payload"):
            for p in pin_result["payload"].split(","):
                if ":" in p:
                    direction, name = p.split(":", 1)
                    pins.append({"direction": direction, "name": name})

        node["pins"] = pins
        enriched.append(node)

        title_short = node["title"][:40]
        print(f"  {title_short:<42} {len(pins)} pins  {guid[:16]}...")

    result["nodes"] = enriched
    return result


async def scan(filter_str: str = "") -> dict:
    bridge = UnrealBridge()
    if not await bridge.connect():
        return {}

    print(f"\n=== Project Scanner (filter: '{filter_str or 'all'}') ===\n")

    # Get Blueprint asset paths
    list_result = await bridge.list_assets(filter_str)
    if not list_result or not list_result.get("success") or not list_result.get("payload"):
        print("No assets found. Is the Editor open?")
        await bridge.close()
        return {}

    # Clean up asset registry path format
    raw = [p.strip() for p in list_result["payload"].split(",") if p.strip()]
    clean_paths = []
    for p in raw:
        if "'" in p:
            clean_paths.append(p.split("'")[1])
        else:
            clean_paths.append(p)

    print(f"Scanning {len(clean_paths)} Blueprint(s):\n")

    manifest = {
        "scanned_at": datetime.now().isoformat(),
        "filter":     filter_str,
        "blueprints": {},
    }

    for bp_path in clean_paths:
        print(f"\n--- {bp_path} ---")
        bp_data = await scan_blueprint(bridge, bp_path)
        manifest["blueprints"][bp_path] = bp_data

    await bridge.close()

    with open(MANIFEST_FILE, "w") as f:
        json.dump(manifest, f, indent=2)

    total_nodes = sum(
        len(bp.get("nodes", []))
        for bp in manifest["blueprints"].values()
    )
    print(f"\n=== Scan complete ===")
    print(f"  {len(clean_paths)} Blueprint(s), {total_nodes} total node(s)")
    print(f"  Saved to: {MANIFEST_FILE}")
    print()
    print("Usage in other scripts:")
    print("  from graphbridge_scan import load_manifest, find_node, find_pin")
    print("  manifest = load_manifest()")
    print("  node = find_node(manifest, '/Game/BP_FlyingCharacter...', 'IA_Fly')")

    return manifest


if __name__ == "__main__":
    filter_str = " ".join(sys.argv[1:]) if len(sys.argv) > 1 else ""
    asyncio.run(scan(filter_str))
