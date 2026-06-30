# -*- coding: utf-8 -*-
# GraphBridge AI - graphbridge_tools.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
tools.py
A unified CLI for all read-only discovery and inspection operations.
Replaces: find_asset.py, get_path.py, inspect_pins.py, discover_nodes.py

Usage examples:
    python tools.py find IA_Move IA_Fly
    python tools.py path BP_FlyingCharacter
    python tools.py pins /Game/BP_FlyingCharacter.BP_FlyingCharacter NodeA NodeB
    python tools.py discover EnhancedInputAction AddMovementInput
    python tools.py discover   # uses default search terms
"""

import asyncio
import sys
from graphbridge_bridge import UnrealBridge

# Default terms used when 'discover' is called with no arguments
DEFAULT_DISCOVER_TERMS = [
    "EnhancedInputAction",
    "AddMovementInput",
    "FunctionEntry",
    "CallFunction",
    "EnhancedInput",
]


# ---------------------------------------------------------------------------
# Individual operations
# ---------------------------------------------------------------------------

async def cmd_find(bridge: UnrealBridge, terms: list[str]):
    """List assets matching one or more search terms."""
    for term in terms:
        result = await bridge.list_assets(term)
        if result and result["success"]:
            paths = [p for p in result["payload"].split(",") if p]
            print(f"\n[{term}] — {len(paths)} result(s):")
            for p in paths:
                print(f"  {p}")
        else:
            msg = result["message"] if result else "No response"
            print(f"\n[{term}] — NOT FOUND ({msg})")


async def cmd_path(bridge: UnrealBridge, terms: list[str]):
    """Print full asset paths for each search term (alias for find, one term at a time)."""
    await cmd_find(bridge, terms)


async def cmd_pins(bridge: UnrealBridge, args: list[str]):
    """
    Inspect pins on one or more nodes.
    args[0] = blueprint path
    args[1:] = node names / GUIDs / comments
    """
    if len(args) < 2:
        print("Usage: python tools.py pins <BP_PATH> <Node1> [Node2 ...]")
        return

    bp_path = args[0]
    nodes = args[1:]

    for node in nodes:
        result = await bridge.get_node_pins(bp_path, node)
        if result and result["success"]:
            pins = [p for p in result["payload"].split(",") if p]
            print(f"\n[{node}] — {len(pins)} pin(s):")
            for pin in pins:
                print(f"  {pin}")
        else:
            msg = result["message"] if result else "No response"
            print(f"\n[{node}] — NOT FOUND ({msg})")


async def cmd_discover(bridge: UnrealBridge, terms: list[str]):
    """
    Search for node UClasses matching each term.
    Saves results to discovered_nodes.txt.
    """
    if not terms:
        terms = DEFAULT_DISCOVER_TERMS

    found: dict[str, list[str]] = {}
    print("\n--- Node Class Discovery ---\n")

    for term in terms:
        result = await bridge.find_node_class(term)
        if result and result["success"]:
            matches = [m for m in result["payload"].split(",") if m]
            found[term] = matches
            print(f"[{term}] — {len(matches)} match(es):")
            for m in matches:
                print(f"  {m}")
        else:
            print(f"[{term}] — no matches")
        print()

    out_path = "discovered_nodes.txt"
    with open(out_path, "w") as f:
        for term, matches in found.items():
            f.write(f"# {term}\n")
            for m in matches:
                f.write(f"{m}\n")
            f.write("\n")

    print(f"Results saved to {out_path}")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

async def cmd_nodes(bridge: UnrealBridge, args: list[str]):
    """List all nodes in a Blueprint EventGraph."""
    if not args:
        print("Usage: python tools.py nodes <BP_PATH>")
        return
    bp_path = args[0]
    nodes = await bridge.list_nodes(bp_path)
    if not nodes:
        print(f"No nodes found in {bp_path}")
        return
    print(f"\n{len(nodes)} node(s) in {bp_path}:\n")
    for n in nodes:
        comment = f"  comment: {n['comment']!r}" if n['comment'] else ""
        print(f"  [{n['class_name']}]")
        print(f"    title:   {n['title']!r}")
        if comment: print(comment)
        print(f"    guid:    {n['guid']}")


COMMANDS = {
    "find": cmd_find,
    "path": cmd_path,
    "pins": cmd_pins,
    "nodes": cmd_nodes,
    "discover": cmd_discover,
}

HELP = """
Usage: python tools.py <command> [args...]

Commands:
  find     <term> [term2 ...]    Search Blueprint + AnimBlueprint assets
  path     <term> [term2 ...]    Same as find — print full asset paths
  pins     <BP_PATH> <node> ...  Show pins on one or more graph nodes
  nodes    <BP_PATH>             List all nodes in a Blueprint EventGraph
  discover [term] [term2 ...]    Search for node UClasses; saves discovered_nodes.txt
"""


async def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(HELP)
        return

    command = sys.argv[1].lower()
    args = sys.argv[2:]

    if command not in COMMANDS:
        print(f"Unknown command: {command!r}\n{HELP}")
        return

    bridge = UnrealBridge()
    if not await bridge.connect():
        return

    try:
        await COMMANDS[command](bridge, args)
    finally:
        await bridge.close()


if __name__ == "__main__":
    asyncio.run(main())
