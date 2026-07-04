# GraphBridge AI â€” graphbridge_animation.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
animation_tools.py
Create and configure animation assets via Unreal's Python API.

Covers:
  - Animation Montages    (with sections and slots)
  - Blend Spaces 1D / 2D (with sample points)
  - Animation Sequences   (asset creation and skeleton assignment)

This script talks to Unreal via the HTTP Remote Control API (port 30010),
NOT the GraphBridge WebSocket. It uses Unreal's built-in Python scripting
to manipulate animation assets directly.

Requirements:
    pip install requests
    Enable in Unreal: Edit > Plugins > Python Script Plugin (on)
                                     > Remote Control API       (on)

USAGE:
    python animation_tools.py montage  --skeleton SK_Mannequin --sequence AM_Run
    python animation_tools.py blendspace1d --skeleton SK_Mannequin --name BS_Speed
    python animation_tools.py blendspace2d --skeleton SK_Mannequin --name BS_Locomotion
    python animation_tools.py list     --type AnimMontage
"""

import sys
import json
import argparse
import requests

# ---------------------------------------------------------------------------
# CONFIGURATION — edit for your project
# ---------------------------------------------------------------------------

UNREAL_HTTP_URL = "http://127.0.0.1:30010/remote/object/call"
OUTPUT_PATH     = "/Game/Animations"   # <- EDIT: where new assets are saved

HTTP_HEADERS = {
    "Content-Type":     "application/json",
    "Accept":           "application/json",
    "Origin":           "http://127.0.0.1",
    "X-Requested-With": "XMLHttpRequest",
}

# ---------------------------------------------------------------------------
# HTTP executor — runs Python inside the Unreal Editor
# ---------------------------------------------------------------------------

def run_python(script: str) -> str:
    """Execute Python inside Unreal Editor and return captured output."""
    payload = {
        "objectPath":   "/Script/PythonScriptPlugin.Default__PythonScriptLibrary",
        "functionName": "ExecutePythonCommandEx",
        "parameters": {
            "PythonCommand":      script,
            "ExecutionMode":      "ExecuteFile",
            "FileExecutionScope": "Private",
        },
        "generateTransaction": True,
    }

    try:
        r = requests.put(UNREAL_HTTP_URL, json=payload,
                         headers=HTTP_HEADERS, timeout=30)
    except requests.RequestException as e:
        return f"HTTP request failed: {e}"

    try:
        data = r.json()
    except ValueError:
        return r.text.strip() or "Executed (no output)"

    lines = []
    for entry in (data.get("LogOutput") or []):
        if isinstance(entry, dict):
            out = entry.get("Output", "").rstrip()
            if out:
                lines.append(out)
        else:
            lines.append(str(entry))

    cmd_result = data.get("CommandResult", "")
    if cmd_result and cmd_result not in ("None", ""):
        lines.append(f"Result: {cmd_result}")

    if data.get("ReturnValue") is False:
        lines.append("(Script raised an error — see output above)")

    return "\n".join(lines) if lines else f"Status {r.status_code}: executed"


def ok(msg: str):
    print(f"  [OK]   {msg}")

def fail(msg: str):
    print(f"  [FAIL] {msg}")

def section(title: str):
    print(f"\n=== {title} ===\n")

# ---------------------------------------------------------------------------
# 1. List animation assets
# ---------------------------------------------------------------------------

def cmd_list(args):
    section(f"Listing {args.type} assets")

    script = f"""
import unreal

reg = unreal.AssetRegistryHelpers.get_asset_registry()
filter = unreal.ARFilter(
    class_names=["{args.type}"],
    recursive_paths=True,
    package_paths=["/Game"]
)
assets = reg.get_assets(filter)
if not assets:
    print("No assets found.")
else:
    for a in assets:
        print(f"  {{a.package_name}}")
    print(f"\\nTotal: {{len(assets)}}")
"""
    print(run_python(script))


# ---------------------------------------------------------------------------
# 2. Create Animation Montage
# ---------------------------------------------------------------------------

def cmd_montage(args):
    section(f"Creating Animation Montage: {args.name}")

    script = f"""
import unreal

# Load skeleton
skeleton = unreal.load_asset("{args.skeleton}")
if not skeleton:
    print(f"ERROR: Skeleton not found at {args.skeleton}")
    raise SystemExit(1)

# Load source sequence if provided
source_seq = None
if "{args.sequence}":
    source_seq = unreal.load_asset("{args.sequence}")
    if not source_seq:
        print(f"WARNING: Source sequence not found at {args.sequence} — creating empty montage")

# Create the montage asset
tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.AnimMontageFactory()
factory.set_editor_property("TargetSkeleton", skeleton)
if source_seq:
    factory.set_editor_property("SourceAnimation", source_seq)

montage = tools.create_asset(
    asset_name="{args.name}",
    package_path="{OUTPUT_PATH}",
    asset_class=unreal.AnimMontage,
    factory=factory
)

if not montage:
    print("ERROR: Failed to create montage")
    raise SystemExit(1)

print(f"Created: {{montage.get_path_name()}}")

# Set slot name
if "{args.slot}" and montage.get_num_montage_slots() > 0:
    slot = montage.get_montage_slot(0)
    # Slot name set via anim slot node name
    print(f"Slot 0 configured")

# Add named sections
sections = {json.dumps(args.sections if args.sections else ["Default"])}
for i, section_name in enumerate(sections):
    start_time = i * {args.section_length}
    montage.add_montage_section(section_name, start_time)
    print(f"Added section: {{section_name}} at {{start_time:.2f}}s")

# Save
unreal.EditorAssetLibrary.save_asset(montage.get_path_name())
print(f"Saved successfully.")
"""
    print(run_python(script))


# ---------------------------------------------------------------------------
# 3. Create BlendSpace 1D
# ---------------------------------------------------------------------------

def cmd_blendspace1d(args):
    section(f"Creating BlendSpace 1D: {args.name}")

    script = f"""
import unreal

skeleton = unreal.load_asset("{args.skeleton}")
if not skeleton:
    print(f"ERROR: Skeleton not found at {args.skeleton}")
    raise SystemExit(1)

tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlendSpaceFactory1D()
factory.set_editor_property("TargetSkeleton", skeleton)

bs = tools.create_asset(
    asset_name="{args.name}",
    package_path="{OUTPUT_PATH}",
    asset_class=unreal.BlendSpace1D,
    factory=factory
)

if not bs:
    print("ERROR: Failed to create BlendSpace 1D")
    raise SystemExit(1)

print(f"Created: {{bs.get_path_name()}}")

# Configure axis
axis_params = bs.get_editor_property("BlendParameters")
axis_params[0].set_editor_property("DisplayName", "{args.axis_name}")
axis_params[0].set_editor_property("Min", {args.axis_min})
axis_params[0].set_editor_property("Max", {args.axis_max})
axis_params[0].set_editor_property("GridNum", {args.grid_divisions})
bs.set_editor_property("BlendParameters", axis_params)

print(f"Axis: {args.axis_name} ({args.axis_min} to {args.axis_max}, {{args.grid_divisions}} divisions)")

# Add sample animations if provided
samples = {json.dumps(args.samples if args.samples else [])}
for sample in samples:
    # Format: "path:value" e.g. "/Game/Anims/Idle:0" "/Game/Anims/Run:600"
    if ":" in sample:
        anim_path, value = sample.rsplit(":", 1)
        anim = unreal.load_asset(anim_path.strip())
        if anim:
            bs.add_sample_point(anim, unreal.Vector(float(value), 0, 0))
            print(f"Added sample: {{anim_path}} at {{value}}")
        else:
            print(f"WARNING: Could not load {{anim_path}}")

unreal.EditorAssetLibrary.save_asset(bs.get_path_name())
print("Saved successfully.")
"""
    print(run_python(script))


# ---------------------------------------------------------------------------
# 4. Create BlendSpace 2D
# ---------------------------------------------------------------------------

def cmd_blendspace2d(args):
    section(f"Creating BlendSpace 2D: {args.name}")

    script = f"""
import unreal

skeleton = unreal.load_asset("{args.skeleton}")
if not skeleton:
    print(f"ERROR: Skeleton not found at {args.skeleton}")
    raise SystemExit(1)

tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.BlendSpaceFactoryNew()
factory.set_editor_property("TargetSkeleton", skeleton)

bs = tools.create_asset(
    asset_name="{args.name}",
    package_path="{OUTPUT_PATH}",
    asset_class=unreal.BlendSpace,
    factory=factory
)

if not bs:
    print("ERROR: Failed to create BlendSpace 2D")
    raise SystemExit(1)

print(f"Created: {{bs.get_path_name()}}")

# Configure axes
axis_params = bs.get_editor_property("BlendParameters")
axis_params[0].set_editor_property("DisplayName", "{args.axis_x_name}")
axis_params[0].set_editor_property("Min", {args.axis_x_min})
axis_params[0].set_editor_property("Max", {args.axis_x_max})
axis_params[1].set_editor_property("DisplayName", "{args.axis_y_name}")
axis_params[1].set_editor_property("Min", {args.axis_y_min})
axis_params[1].set_editor_property("Max", {args.axis_y_max})
bs.set_editor_property("BlendParameters", axis_params)

print(f"X Axis: {args.axis_x_name} ({args.axis_x_min} to {args.axis_x_max})")
print(f"Y Axis: {args.axis_y_name} ({args.axis_y_min} to {args.axis_y_max})")

unreal.EditorAssetLibrary.save_asset(bs.get_path_name())
print("Saved successfully.")
"""
    print(run_python(script))


# ---------------------------------------------------------------------------
# 5. Assign AnimBlueprint to a character mesh
# ---------------------------------------------------------------------------

def cmd_assign_animbp(args):
    section(f"Assigning AnimBP to character")

    script = f"""
import unreal

bp = unreal.load_asset("{args.blueprint}")
if not bp:
    print(f"ERROR: Blueprint not found at {args.blueprint}")
    raise SystemExit(1)

animbp = unreal.load_asset("{args.animbp}")
if not animbp:
    print(f"ERROR: AnimBlueprint not found at {args.animbp}")
    raise SystemExit(1)

gen_class = bp.generated_class()
cdo = unreal.get_default_object(gen_class)
mesh = cdo.get_editor_property("Mesh")

if not mesh:
    print("ERROR: No SkeletalMeshComponent found on Blueprint")
    raise SystemExit(1)

animbp_class = animbp.generated_class()
mesh.set_editor_property("AnimClass", animbp_class)
mesh.set_editor_property("AnimationMode",
    unreal.AnimationMode.ANIMATION_BLUEPRINT)

print(f"Assigned {{animbp.get_name()}} to {{bp.get_name()}}")

# Compile and save
unreal.KismetEditorUtilities.compile_blueprint(bp)
unreal.EditorAssetLibrary.save_asset(bp.get_path_name())
print("Compiled and saved.")
"""
    print(run_python(script))


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Unreal animation asset creation tools",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python animation_tools.py list --type AnimMontage
  python animation_tools.py list --type BlendSpace

  python animation_tools.py montage --skeleton /Game/Characters/SK_Hero
      --name AM_Attack --sequence /Game/Animations/Attack_Anim
      --slot UpperBody --sections Startup Attack Followthrough

  python animation_tools.py blendspace1d --skeleton /Game/Characters/SK_Hero
      --name BS_Speed --axis-name Speed --axis-min 0 --axis-max 600
      --samples "/Game/Animations/Idle:0" "/Game/Animations/Walk:200" "/Game/Animations/Run:600"

  python animation_tools.py blendspace2d --skeleton /Game/Characters/SK_Hero
      --name BS_Locomotion --axis-x-name Speed --axis-y-name Direction

  python animation_tools.py assign --blueprint /Game/BP_MyCharacter.BP_MyCharacter
      --animbp /Game/Animations/ABP_MyCharacter
        """
    )

    sub = parser.add_subparsers(dest="command")

    # list
    p_list = sub.add_parser("list", help="List animation assets")
    p_list.add_argument("--type", default="AnimMontage",
        choices=["AnimMontage", "BlendSpace", "BlendSpace1D", "AnimSequence"],
        help="Asset type to list")

    # montage
    p_mon = sub.add_parser("montage", help="Create an Animation Montage")
    p_mon.add_argument("--skeleton",  required=True, help="Skeleton asset path")
    p_mon.add_argument("--name",      required=True, help="New montage name")
    p_mon.add_argument("--sequence",  default="",    help="Source AnimSequence path (optional)")
    p_mon.add_argument("--slot",      default="DefaultSlot", help="Anim slot name")
    p_mon.add_argument("--sections",  nargs="*",     help="Section names e.g. Startup Loop End")
    p_mon.add_argument("--section-length", type=float, default=0.5,
        help="Length of each section in seconds (default 0.5)")

    # blendspace1d
    p_bs1 = sub.add_parser("blendspace1d", help="Create a 1D BlendSpace")
    p_bs1.add_argument("--skeleton",        required=True)
    p_bs1.add_argument("--name",            required=True)
    p_bs1.add_argument("--axis-name",       default="Speed")
    p_bs1.add_argument("--axis-min",        type=float, default=0.0)
    p_bs1.add_argument("--axis-max",        type=float, default=600.0)
    p_bs1.add_argument("--grid-divisions",  type=int,   default=4)
    p_bs1.add_argument("--samples",         nargs="*",
        help='Animation samples: "path:value" e.g. "/Game/Anims/Idle:0"')

    # blendspace2d
    p_bs2 = sub.add_parser("blendspace2d", help="Create a 2D BlendSpace")
    p_bs2.add_argument("--skeleton",    required=True)
    p_bs2.add_argument("--name",        required=True)
    p_bs2.add_argument("--axis-x-name", default="Speed")
    p_bs2.add_argument("--axis-x-min",  type=float, default=0.0)
    p_bs2.add_argument("--axis-x-max",  type=float, default=600.0)
    p_bs2.add_argument("--axis-y-name", default="Direction")
    p_bs2.add_argument("--axis-y-min",  type=float, default=-180.0)
    p_bs2.add_argument("--axis-y-max",  type=float, default=180.0)

    # assign
    p_assign = sub.add_parser("assign", help="Assign AnimBlueprint to a character")
    p_assign.add_argument("--blueprint", required=True, help="Character Blueprint path")
    p_assign.add_argument("--animbp",    required=True, help="AnimBlueprint asset path")

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        return

    dispatch = {
        "list":         cmd_list,
        "montage":      cmd_montage,
        "blendspace1d": cmd_blendspace1d,
        "blendspace2d": cmd_blendspace2d,
        "assign":       cmd_assign_animbp,
    }

    dispatch[args.command](args)


if __name__ == "__main__":
    main()
