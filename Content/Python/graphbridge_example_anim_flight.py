# GraphBridge AI â€” graphbridge_example_anim_flight.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
wire_anim_flight.py (v2)
Wires bIsFlying into ABP_Unarmed2 using only what the bridge reliably supports.

Strategy:
  The AnimBP already has FlightRef (a FlightComponent object reference) being
  set in Initialize. In the Update loop we just need:

    Sequence (new branch) -> [Get bIsLevitating from FlightRef] -> Set bIsFlying

  Since bIsLevitating is a BlueprintReadWrite UPROPERTY (not a UFUNCTION),
  we access it via a K2Node_VariableGet on the FlightRef object — but that
  requires the node to be in the context of FlightRef's class, which the
  bridge can't do directly.

  Simplest reliable path via bridge:
    1. Add bIsFlying bool to ABP (SPAWN_VARIABLE) — done in the Update event
    2. Place nodes in the graph
    3. Manual step: connect FlightRef.bIsLevitating -> bIsFlying
       (just drag one wire in the editor — 10 seconds)

  The bridge does everything except that one cross-object property wire.
"""

import asyncio
from graphbridge_bridge import UnrealBridge

ABP = "/Game/YourProject/ABP_YourAnimBlueprint.ABP_YourAnimBlueprint"  # EDIT THIS: replace with your AnimBlueprint path
TAG = "GB_AnimFlight"

# Run graphbridge_scan.py on your project then paste the GUIDs from LIST_NODES output
SEQUENCE_GUID   = ""  # EDIT THIS: paste your Sequence node GUID from LIST_NODES output
FLIGHTREF_SET   = ""  # EDIT THIS: paste your SetFlightRef node GUID

# Layout — right of existing sequence cluster
FLIGHTREF_GET_X, FLIGHTREF_GET_Y = 1800, 500
SETBOOL_X,       SETBOOL_Y       = 2200, 500


def ok(r) -> bool:
    return r is not None and r.get("success", False)

def msg(r) -> str:
    return (r["message"] if r else "No response")


async def wire():
    bridge = UnrealBridge()
    if not await bridge.connect():
        return

    print("\n=== Wire bIsFlying into ABP_Unarmed2 (v2) ===\n")

    # ------------------------------------------------------------------
    # 0. Close editor + clear previous attempt
    # ------------------------------------------------------------------
    print("[0] Closing Blueprint Editor...")
    r = await bridge.close_blueprint(ABP)
    print(f"    {'OK' if ok(r) else msg(r)}")
    await asyncio.sleep(0.5)

    print("[0b] Clearing GB_AnimFlight nodes...")
    r = await bridge.clear_nodes(ABP, TAG)
    print(f"    {'cleared' if ok(r) else 'nothing to clear'}")

    # ------------------------------------------------------------------
    # 1. Add bIsFlying bool variable to the Anim BP
    # ------------------------------------------------------------------
    print("[1] Adding bIsFlying variable...")
    r = await bridge.spawn_variable(ABP, "bIsFlying", "bool", "Flight")
    if ok(r):
        print(f"    OK — '{r.get('payload', 'bIsFlying')}'")
    else:
        print(f"    Note: {msg(r)} (may already exist — continuing)")

    # ------------------------------------------------------------------
    # 2. Spawn a FlightRef getter (so we can drag its output to bIsLevitating)
    # ------------------------------------------------------------------
    print("[2] Spawning FlightRef getter...")
    r = await bridge.spawn_node(ABP, "K2Node_VariableGet", TAG, FLIGHTREF_GET_X, FLIGHTREF_GET_Y)
    if not ok(r):
        print(f"    FAIL: {msg(r)}")
        await bridge.close()
        return
    getter_guid = r["payload"]
    print(f"    OK — {getter_guid}")

    # Bind to FlightRef — this is an AnimBP variable so SET_VARIABLE_REF
    # should find it in NewVariables (Strategy 1)
    print("[2b] Binding to FlightRef variable...")
    r = await bridge.set_variable_ref(ABP, getter_guid, "FlightRef")
    print(f"    {'OK' if ok(r) else 'FAIL: ' + msg(r)}")

    r = await bridge.get_node_pins(ABP, getter_guid)
    getter_out = "FlightRef"
    if ok(r) and r.get("payload"):
        out_pins = [p.split(":")[1] for p in r["payload"].split(",")
                    if p.startswith("OUT:") and "exec" not in p.lower()]
        if out_pins:
            getter_out = out_pins[0]
    print(f"    Output pin: '{getter_out}'")

    # ------------------------------------------------------------------
    # 3. Spawn Set bIsFlying node
    # ------------------------------------------------------------------
    print("[3] Spawning Set bIsFlying...")
    r = await bridge.spawn_node(ABP, "K2Node_VariableSet", TAG, SETBOOL_X, SETBOOL_Y)
    if not ok(r):
        print(f"    FAIL: {msg(r)}")
        await bridge.close()
        return
    setbool_guid = r["payload"]
    print(f"    OK — {setbool_guid}")

    print("[3b] Binding Set node to bIsFlying...")
    r = await bridge.set_variable_ref(ABP, setbool_guid, "bIsFlying")
    print(f"    {'OK' if ok(r) else 'FAIL: ' + msg(r)}")

    # Check what input pins the Set node has
    r = await bridge.get_node_pins(ABP, setbool_guid)
    if ok(r) and r.get("payload"):
        print(f"    Set bIsFlying pins: {r['payload']}")

    # ------------------------------------------------------------------
    # 4. Wire Sequence -> Set bIsFlying exec
    # ------------------------------------------------------------------
    print("[4] Inspecting Sequence node for next free exec output...")
    r = await bridge.get_node_pins(ABP, SEQUENCE_GUID)
    seq_exec_out = "then_2"  # default — Sequence likely has then_0 and then_1 used
    if ok(r) and r.get("payload"):
        pins = r["payload"].split(",")
        out_execs = [p.split(":")[1] for p in pins
                     if p.startswith("OUT:") and "then" in p.lower()]
        print(f"    Sequence exec outputs: {out_execs}")
        # Use the last one — most likely to be unconnected
        if out_execs:
            seq_exec_out = out_execs[-1]

    print(f"    Connecting Sequence '{seq_exec_out}' -> Set bIsFlying...")
    r = await bridge.connect_pins(ABP, SEQUENCE_GUID, seq_exec_out, setbool_guid, "execute")
    print(f"    {'OK' if ok(r) else 'FAIL: ' + msg(r)}")

    # ------------------------------------------------------------------
    # 5. Reopen and compile
    # ------------------------------------------------------------------
    print("[5] Reopening Blueprint Editor...")
    await bridge.open_blueprint(ABP)
    await asyncio.sleep(0.5)

    print("[6] Compiling...")
    r = await bridge.compile(ABP)
    print(f"    {'OK' if ok(r) else 'compiled with errors'}")

    await bridge.save_blueprint(ABP)
    await bridge.close()

    print(f"""
=== Bridge work complete ===

One manual wire needed (10 seconds in the Editor):

  1. Find the new FlightRef getter node (comment: GB_AnimFlight)
  2. Drag its output pin -> into the SET bIsFlying node's input bool pin
     The FlightRef node has a sub-pin for bIsLevitating — expand it if needed,
     OR: right-click the FlightRef output -> "Split Struct Pin" won't apply here.
     Instead: drag from FlightRef output -> drop on Set bIsFlying's value pin,
     then in the context menu that appears type "bIsLevitating" and select
     "Get bIsLevitating" from FlightComponent.

  3. Compile and save ABP_Unarmed2

Then in the AnimGraph (5 minutes):
  1. Open AnimGraph -> double-click state machine
  2. Add "Flight" state -> wire in BS_8WayFly BlendSpace
  3. Transition Locomotion->Flight: condition bIsFlying == true
  4. Transition Flight->Locomotion: condition bIsFlying == false
  5. Compile + save

""")


if __name__ == "__main__":
    asyncio.run(wire())
