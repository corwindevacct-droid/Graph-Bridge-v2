# GraphBridge AI â€” graphbridge_variables.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
variable_balancer.py
Spawns a block of typed Blueprint member variables and sets their defaults.

Workflow: spawn all -> set defaults -> compile -> save (mark dirty).
Press Ctrl+S once in the Unreal Editor after this script finishes.

USAGE:
    1. Edit the three config values below (BP, CATEGORY, STAT_VARIABLES).
    2. Run:  python variable_balancer.py
    3. Press Ctrl+S in the Unreal Editor to save to disk.
"""

import asyncio
from graphbridge_bridge import UnrealBridge

# ---------------------------------------------------------------------------
# CONFIGURATION — edit these for your project
# ---------------------------------------------------------------------------

# Full asset path to your Blueprint. Get this by running:
#   python tools.py path YourBlueprintName
BP = "/Game/YourFolder/BP_YourBlueprint.BP_YourBlueprint"  # <- EDIT THIS

# Category name shown in the Blueprint Variables panel
CATEGORY = "MyCategory"  # <- EDIT THIS

# List of variables to create: (name, type_string, default_value)
#
# type_string options:
#   bool, int32, int64, float, double, byte,
#   FString, FName, FText, FVector, FVector2D,
#   FRotator, FTransform, FLinearColor,
#   object:ClassName, class:ClassName
#
# default_value format by type:
#   float/int  ->  "600.0" / "42"
#   bool       ->  "true" / "false"
#   FVector    ->  "(X=1.0,Y=0.0,Z=0.0)"
#   FRotator   ->  "(Pitch=0.0,Yaw=90.0,Roll=0.0)"
#   FLinearColor-> "(R=1.0,G=0.0,B=0.0,A=1.0)"
#   FString    ->  "Hello World"

STAT_VARIABLES = [  # <- EDIT THIS LIST
    ("WalkSpeed",    "float",   "600.0"),
    ("JumpVelocity", "float",   "800.0"),
    ("MaxHealth",    "float",   "100.0"),
    ("bCanDash",     "bool",    "true"),
    ("DashImpulse",  "FVector", "(X=2000.0,Y=0.0,Z=500.0)"),
]

# ---------------------------------------------------------------------------
# Script — no edits needed below this line
# ---------------------------------------------------------------------------

async def build_stat_block():
    bridge = UnrealBridge()
    if not await bridge.connect():
        return

    # actual_name[requested] = name the engine assigned (may have _0 suffix)
    actual_names: dict[str, str] = {}

    # ------------------------------------------------------------------
    # Phase 1: Spawn all variables
    # ------------------------------------------------------------------
    print(f"\n=== Spawning {len(STAT_VARIABLES)} variables on {BP} ===\n")

    for var_name, type_string, _ in STAT_VARIABLES:
        result = await bridge.spawn_variable(BP, var_name, type_string, CATEGORY)

        if result and result["success"]:
            # The C++ side puts the actual assigned name in payload.
            actual = result.get("payload", "").strip()
            if not actual:
                try:
                    actual = result["message"].split("'")[1]
                except (IndexError, KeyError):
                    actual = var_name
            actual_names[var_name] = actual
            suffix = f" (renamed to '{actual}')" if actual != var_name else ""
            print(f"  [OK]   {var_name}{suffix}")
        else:
            msg = result["message"] if result else "No response"
            print(f"  [FAIL] {var_name}: {msg}")

    # ------------------------------------------------------------------
    # Phase 2: Set defaults using the engine-assigned names
    # ------------------------------------------------------------------
    print(f"\n=== Setting defaults ===\n")

    for var_name, _, default_value in STAT_VARIABLES:
        actual = actual_names.get(var_name)
        if not actual:
            print(f"  [SKIP] {var_name} — was not spawned successfully")
            continue

        result = await bridge.set_variable_default(BP, actual, default_value)

        if result and result["success"]:
            print(f"  [OK]   {actual} = {default_value}")
        else:
            msg = result["message"] if result else "No response"
            print(f"  [FAIL] {actual}: {msg}")

    # ------------------------------------------------------------------
    # Phase 3: Compile (bakes defaults into the CDO)
    # ------------------------------------------------------------------
    print(f"\n=== Compiling ===\n")
    result = await bridge.compile(BP)
    if result and result["success"]:
        print("  [OK]   Compiled")
    else:
        msg = result["message"] if result else "No response"
        print(f"  [FAIL] Compile: {msg}")

    # ------------------------------------------------------------------
    # Phase 4: Mark dirty (press Ctrl+S in the Editor to save to disk)
    # ------------------------------------------------------------------
    await bridge.save_blueprint(BP)
    print("\n  Blueprint marked dirty — press Ctrl+S in the Unreal Editor to save.")

    await bridge.close()
    print("\nDone.")


if __name__ == "__main__":
    asyncio.run(build_stat_block())
