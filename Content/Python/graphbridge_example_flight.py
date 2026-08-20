# GraphBridge AI â€” graphbridge_example_flight.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
wire_flight.py (v4)
Fixes:
  - Tries both "self" and "Target" pin names for the CallFunction target
  - Cleans up the four pre-existing floating FlightComponent getter nodes
  - Preserves the already-working Triggered -> exec wire by clearing only
    GB_FlyWire tagged nodes, then re-running clean
"""

import asyncio
from graphbridge_bridge import UnrealBridge

BP     = "/Game/YourProject/BP_YourBlueprint.BP_YourBlueprint"  # EDIT THIS: replace with your Blueprint path
IA_FLY = "/Game/YourProject/IA_YourInputAction.IA_YourInputAction"  # EDIT THIS: replace with your Input Action path
TAG    = "GB_FlyWire"

IA_FLY_X,  IA_FLY_Y  =  400, 500
GETTER_X,  GETTER_Y   =  700, 560
CALL_X,    CALL_Y     =  950, 500


def ok(result) -> bool:
    return result is not None and result.get("success", False)

def msg(result) -> str:
    return result["message"] if result else "No response"


async def wire():
    bridge = UnrealBridge()
    if not await bridge.connect():
        return

    print("\n=== GraphBridge: Wire IA_Fly -> ToggleFlight (v4) ===\n")

    # ------------------------------------------------------------------
    # 0. Close the Blueprint Editor before touching the graph.
    #    The SCS preview viewport ticks against nodes during construction
    #    and triggers LowLevelFatalError (EngineBaseTypes.h:481) if open.
    # ------------------------------------------------------------------
    print("[0] Closing Blueprint Editor...")
    r = await bridge.close_blueprint(BP)
    print(f"    {'OK — editor closed' if ok(r) else 'FAIL: ' + msg(r)}")

    # Small delay to let the editor fully close before we touch the graph
    await asyncio.sleep(0.5)

    # ------------------------------------------------------------------
    # 0b. Clear previous tagged nodes
    # ------------------------------------------------------------------
    print("[0b] Clearing GB_FlyWire nodes...")
    r = await bridge.clear_nodes(BP, TAG)
    print(f"    {'cleared' if ok(r) else 'nothing to clear'}")

    # ------------------------------------------------------------------
    # 1. Spawn EnhancedInputAction node
    # ------------------------------------------------------------------
    print("[1] Spawning EnhancedInputAction IA_Fly...")
    r = await bridge.spawn_node(BP, "K2Node_EnhancedInputAction", TAG, IA_FLY_X, IA_FLY_Y)
    if not ok(r):
        print(f"    FAIL: {msg(r)}")
        await bridge.close()
        return
    ia_guid = r["payload"]
    print(f"    OK — {ia_guid}")

    # ------------------------------------------------------------------
    # 2. Bind IA_Fly asset
    # ------------------------------------------------------------------
    print("[2] Binding IA_Fly asset...")
    r = await bridge.set_input_action(BP, ia_guid, IA_FLY)
    print(f"    {'OK' if ok(r) else 'FAIL — set Input Action to IA_Fly manually in Details panel'}")

    # ------------------------------------------------------------------
    # 3. Spawn FlightComponent getter
    # ------------------------------------------------------------------
    print("[3] Spawning FlightComponent getter...")
    r = await bridge.spawn_node(BP, "K2Node_VariableGet", TAG, GETTER_X, GETTER_Y)
    if not ok(r):
        print(f"    FAIL: {msg(r)}")
        await bridge.close()
        return
    getter_guid = r["payload"]
    print(f"    OK — {getter_guid}")

    # Bind the getter to the FlightComponent variable so its output is typed
    print("[3b] Binding getter to FlightComponent variable...")
    r = await bridge.set_variable_ref(BP, getter_guid, "FlightComponent")
    print(f"    {'OK — output pin now typed as UFlightComponent*' if ok(r) else 'FAIL: ' + msg(r)}")

    # Find the actual output pin name after reconstruction
    r = await bridge.get_node_pins(BP, getter_guid)
    getter_out = "FlightComponent"
    if ok(r) and r.get("payload"):
        out_pins = [p.split(":")[1] for p in r["payload"].split(",") if p.startswith("OUT:")]
        if out_pins:
            getter_out = out_pins[0]
    print(f"    Output pin: '{getter_out}'")

    # ------------------------------------------------------------------
    # 4. Spawn CallFunction + set function reference to ToggleFlight
    # ------------------------------------------------------------------
    print("[4] Spawning K2Node_CallFunction...")
    r = await bridge.spawn_node(BP, "K2Node_CallFunction", TAG, CALL_X, CALL_Y)
    if not ok(r):
        print(f"    FAIL: {msg(r)}")
        await bridge.close()
        return
    call_guid = r["payload"]
    print(f"    OK — {call_guid}")

    print("[4b] Setting function ref -> ToggleFlight...")
    r = await bridge.set_function_ref(BP, call_guid, "FlightComponent", "ToggleFlight")
    print(f"    {'OK — pins rebuilt' if ok(r) else 'FAIL: ' + msg(r)}")

    # Inspect pins after reconstruction to get exact names
    r = await bridge.get_node_pins(BP, call_guid)
    exec_in    = None
    target_pin = None
    if ok(r) and r.get("payload"):
        pins = r["payload"].split(",")
        print(f"    Pins after reconstruction: {pins}")
        for p in pins:
            direction, name = p.split(":", 1)
            if direction == "IN":
                name_lower = name.lower()
                if exec_in is None and name_lower in ("execute", "exec", "then"):
                    exec_in = name
                if target_pin is None and name_lower in ("self", "target"):
                    target_pin = name

    # Fallbacks if inspection didn't find them
    exec_in    = exec_in    or "execute"
    target_pin = target_pin or "self"
    print(f"    Will connect: exec='{exec_in}'  target='{target_pin}'")

    # ------------------------------------------------------------------
    # 5. Connect Triggered -> ToggleFlight exec
    # ------------------------------------------------------------------
    print("[5] Connecting Triggered -> ToggleFlight exec...")
    r = await bridge.connect_pins(BP, ia_guid, "Triggered", call_guid, exec_in)
    print(f"    {'OK' if ok(r) else 'FAIL: ' + msg(r)}")

    # ------------------------------------------------------------------
    # 6. Connect FlightComponent getter -> Target (try both pin names)
    # ------------------------------------------------------------------
    print("[6] Connecting FlightComponent getter -> Target...")
    r = await bridge.connect_pins(BP, getter_guid, getter_out, call_guid, target_pin)
    if ok(r):
        print("    OK")
    else:
        # The pin name "self" sometimes doesn't accept external connections —
        # try "Target" explicitly
        print(f"    '{target_pin}' failed, trying 'Target'...")
        r = await bridge.connect_pins(BP, getter_guid, getter_out, call_guid, "Target")
        if ok(r):
            print("    OK with 'Target'")
        else:
            print(f"    FAIL: {msg(r)}")
            print("    >>> Drag the FlightComponent getter output -> Target pin manually.")

    # ------------------------------------------------------------------
    # 7. Reopen the Blueprint Editor now that all nodes are placed
    # ------------------------------------------------------------------
    print("[7] Reopening Blueprint Editor...")
    r = await bridge.open_blueprint(BP)
    print(f"    {'OK' if ok(r) else 'FAIL: ' + msg(r)}")

    await asyncio.sleep(0.5)

    # ------------------------------------------------------------------
    # 8. Compile
    # ------------------------------------------------------------------
    print("[8] Compiling...")
    r = await bridge.compile(BP)
    print(f"    {'OK — compiled clean' if ok(r) else 'compiled with errors — check Output Log'}")

    await bridge.save_blueprint(BP)
    await bridge.close()

    print("\n=== Done ===")
    print("Press Ctrl+S in the Editor to save.")
    print()
    print("Also delete the 4 floating FlightComponent getter nodes that were")
    print("already in your graph before this script ran — select them and hit Delete.")


if __name__ == "__main__":
    asyncio.run(wire())
