# GraphBridgev2 API Reference

**Complete command reference for all 129 GraphBridge tools.** Parameters are fully typed with bounds validation. Optional parameters show sensible defaults.

## Navigation

- **[Tier 1 (Fully Typed)](#tier-1-fully-typed)**: Core graph manipulation — SPAWN_NODE, CONNECT_PINS, etc. (7 commands)
- **[Tier 2 (Fully Typed)](#tier-2-fully-typed)**: Animation, character, material, input, level — 37 commands
- **[Tier 3 (Basic Types)](#tier-3-basic-types)**: Extended feature set, string-only params — 85 commands

## Calling Pattern

Every command below is written as a single pipe-delimited string:

```
COMMAND_NAME|param1|param2|...
```

Two transports carry that string:

- **WebSocket (port 8080)** — send it as a text frame. Every connection must
  present the per-session token from `Saved/GraphBridge/session_token.txt`
  as a `?token=...` query parameter on the connection URI, or the server
  closes the connection before any command can be dispatched. The bundled
  `graphbridge_bridge.py` (`UnrealBridge` class) handles this automatically
  — the examples below use it. See USAGE.md for connection setup.
- **MCP (port 8090)** — all 129 tools are exposed as typed MCP tools for
  Claude and other MCP-compatible clients; no manual command-string
  construction needed on that path.

**Response (JSON)**, either transport:
```json
{
  "Success": true|false,
  "Result": { /* command-specific */ },
  "Error": "error message if Success=false"
}
```

---

## Tier 1: Fully Typed

Core blueprint graph manipulation with strict type validation.

### SPAWN_NODE

Spawn a new node in a Blueprint's EventGraph.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| blueprint_path | String | - | Yes | - | Path like /Game/BP_MyBlueprint |
| node_class | String | - | Yes | - | Unreal class like K2Node_CallFunction |
| node_title | String | - | No | "" | Display name in editor |
| x | Integer | -100000 to 100000 | No | 0 | X position in graph |
| y | Integer | -100000 to 100000 | No | 0 | Y position in graph |

**Example**:
```python
await bridge._send_command("SPAWN_NODE|/Game/BP_MyBlueprint|K2Node_VariableGet|Get X|100|200")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
# → {"Success": true, "Result": {"NodeGUID": "..."}}
```

### CONNECT_PINS

Connect an output pin from one node to an input pin on another.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| blueprint_path | String | - | Yes | - | Blueprint containing both nodes |
| from_node_guid | String | - | Yes | - | Source node GUID (from SPAWN_NODE) |
| from_pin_name | String | - | Yes | - | Source pin name (e.g., "Output") |
| to_node_guid | String | - | Yes | - | Target node GUID |
| to_pin_name | String | - | Yes | - | Target pin name (e.g., "Input") |

**Example**:
```python
await bridge._send_command("CONNECT_PINS|/Game/BP_MyBlueprint|node_guid_1|Output|node_guid_2|Input")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
# → {"Success": true, "Result": {...}}
```

### ADD_FUNCTION_PARAM

Add a parameter to a function or macro.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| function_path | String | - | Yes | - | Path like /Game/BP_MyBlueprint.MyFunction |
| param_name | String | - | Yes | - | New parameter name |
| param_type | String | - | Yes | - | Type like "float", "int32", "FVector" |

**Example**:
```python
await bridge._send_command("ADD_FUNCTION_PARAM|/Game/BP_MyBlueprint.AttackFunction|DamageAmount|float")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
```

### SET_VARIABLE_DEFAULT

Set the default value for a variable.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| variable_path | String | - | Yes | - | Path like /Game/BP_MyBlueprint.MyVariable |
| default_value | String | - | Yes | - | Value to assign (type-agnostic) |

**Example**:
```python
await bridge._send_command("SET_VARIABLE_DEFAULT|/Game/BP_MyBlueprint.MaxHealth|100")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
```

### LIST_ASSETS

List assets in the project matching a filter.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| asset_class | String | - | No | "" | Filter by class (e.g., "Blueprint", "SkeletalMesh") |
| path_filter | String | - | No | "" | Filter by path prefix |

**Example**:
```python
await bridge._send_command("LIST_ASSETS|Blueprint|/Game/Characters")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
# → {"Success": true, "Result": {"Assets": [...]}}
```

### CREATE_ENUM

Create a new Enum asset.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| enum_path | String | - | Yes | - | Path like /Game/MyEnum |
| values | String | - | No | "" | Comma-separated enum values |

**Example**:
```python
await bridge._send_command("CREATE_ENUM|/Game/CharacterState|Idle,Walking,Running,Attacking")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
```

### CREATE_STRUCT

Create a new Struct asset.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| struct_path | String | - | Yes | - | Path like /Game/MyStruct |

**Example**:
```python
await bridge._send_command("CREATE_STRUCT|/Game/AttackInfo")
# Or via the bundled UnrealBridge wrapper method for this command,
# if one exists in graphbridge_bridge.py -- see USAGE.md.
```

---

## Tier 2: Fully Typed

Animation, character, input, material, level, and widget commands with strict type validation.

### Animation Commands

#### ADD_MONTAGE_NOTIFY
Add a notify (callback) to a montage section.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| montage_path | String | - | Yes | - | Path to AnimMontage asset |
| notify_class_name | Enum | AnimNotify_PlaySound, AnimNotify_PlayParticleEffect, AnimNotify_PlaySound2D | Yes | - | Notify type |
| time_seconds | Float | 0 to 100000 | No | 0.0 | Time in montage to trigger |

#### ADD_ANIM_SLOT_NODE
Add a slot node to an animation Blueprint.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| anim_bp_path | String | - | Yes | - | Animation Blueprint path |
| slot_name | String | - | Yes | - | Name like "DefaultSlot" |
| x | Integer | -100000 to 100000 | No | 0 | Graph X position |
| y | Integer | -100000 to 100000 | No | 0 | Graph Y position |

#### CREATE_ANIM_MONTAGE
Create a new animation montage.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| montage_path | String | - | Yes | - | Path for new montage |
| skeleton_path | String | - | Yes | - | Skeleton to use |

#### LIST_MONTAGE_SECTIONS
List all sections in a montage.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| montage_path | String | - | Yes | - | Path to AnimMontage |

#### ADD_MONTAGE_SECTION
Add a section to a montage.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| montage_path | String | - | Yes | - | Path to AnimMontage |
| section_name | String | - | Yes | - | Name like "Attack" |
| start_time | Float | 0 to 100000 | No | 0.0 | Section start in seconds |

### Character Commands

#### SET_CHARACTER_MESH
Set the skeletal mesh for a character Blueprint.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| character_bp | String | - | Yes | - | Character Blueprint path |
| mesh_path | String | - | Yes | - | Skeletal mesh asset path |

#### SET_CHARACTER_CAPSULE
Configure the character's capsule component.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| character_bp | String | - | Yes | - | Character Blueprint path |
| half_height | Float | 10 to 1000 | No | 88.0 | Capsule half-height in cm |
| radius | Float | 10 to 500 | No | 34.0 | Capsule radius in cm |

#### SET_CAMERA_BOOM
Configure the camera boom (spring arm).

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| character_bp | String | - | Yes | - | Character Blueprint path |
| arm_length | Float | 0 to 10000 | No | 300.0 | Distance from character in cm |
| offset_height | Float | -1000 to 1000 | No | 60.0 | Vertical offset in cm |
| offset_forward | Float | -1000 to 1000 | No | 0.0 | Forward offset in cm |
| socket_offset_scale | Float | 0.1 to 10 | No | 1.0 | Socket offset scale factor |

### Input Commands

#### CREATE_INPUT_ACTION
Create a new input action.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| action_path | String | - | Yes | - | Path like /Game/Input/IA_Move |
| value_type | Enum | Value, Axis1D, Axis2D, Axis3D, Digest | Yes | - | Input value type |

#### CREATE_INPUT_MAPPING_CONTEXT
Create an input mapping context.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| context_path | String | - | Yes | - | Path like /Game/Input/IMC_Default |

#### ADD_INPUT_MAPPING
Add a mapping to an input mapping context.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| context_path | String | - | Yes | - | Path to mapping context |
| action_path | String | - | Yes | - | Path to input action |
| key_name | String | - | Yes | - | Key like "Gamepad_RightTrigger" |

### Material Commands

#### CREATE_MATERIAL
Create a new material.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| material_path | String | - | Yes | - | Path like /Game/Materials/M_MyMaterial |
| blend_mode | Enum | Opaque, Masked, Translucent, Additive, Modulate | No | Opaque | Material blend mode |

#### CREATE_MATERIAL_INSTANCE
Create a material instance.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| instance_path | String | - | Yes | - | Path like /Game/Materials/M_MyMaterial_Inst |
| parent_path | String | - | Yes | - | Parent material path |

#### SET_MATERIAL_SCALAR_PARAM
Set a scalar parameter on a material.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| material_path | String | - | Yes | - | Material or material instance |
| param_name | String | - | Yes | - | Parameter name |
| value | Float | -999999 to 999999 | Yes | 0.0 | Parameter value |

### Level Commands

#### CAPTURE_VIEW
Capture viewport as image with settings.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| bp_path | String | - | No | "" | Blueprint to view (default: level viewport) |
| distance | Float | 1 to 100000 | No | 200.0 | Distance from subject |
| yaw | Float | -180 to 180 | No | 0.0 | Rotation in degrees |
| pitch | Float | -180 to 180 | No | 0.0 | Elevation in degrees |
| resolution | Integer | 256 to 4096 | No | 1024 | Image resolution |
| lighting_mode | Enum | lit, wireframe, unlit | No | lit | Viewport mode |
| angles | Integer | 1 to 360 | No | 1 | Number of rotation captures |

#### CREATE_LEVEL
Create a new level.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| level_path | String | - | Yes | - | Path like /Game/Maps/NewLevel |

### Widget Commands

#### CREATE_WIDGET_BLUEPRINT
Create a new widget Blueprint.

| Parameter | Type | Range | Required | Default | Notes |
| --- | --- | --- | --- | --- | --- |
| widget_path | String | - | Yes | - | Path like /Game/UI/W_MyWidget |

---

## Tier 3: Basic Types

Extended commands using string parameters. Full typing planned for v3.0.0.

### Animation (22 commands)
ADD_ANIM_NOTIFY, ADD_ANIM_SLOT, ADD_SKELETON_SOCKET, CREATE_ANIM_BLUEPRINT, CREATE_SKELETON, DELETE_MONTAGE_SECTION, DELETE_SKELETON_SOCKET, DUPLICATE_ANIMATION_SEQUENCE, DUPLICATE_MONTAGE, GET_MONTAGE_SECTION_TIME, GET_SKELETON_BONES, GET_SKELETON_SOCKETS, LIST_SKELETON_SOCKETS, RENAME_SKELETON_SOCKET, SET_ANIM_MONTAGE_SLOT, SET_ANIMATION_DEFAULT_SPEED, SET_MONTAGE_LOOP, SET_SKELETON_SOCKET_LOCATION, SYNC_MONTAGE_SECTIONS

### Blueprint (18 commands)
ADD_FUNCTION_LOCAL_VARIABLE, ADD_INTERFACE_IMPLEMENTATION, COMPILE_BLUEPRINT, CREATE_BLUEPRINT_CLASS, CREATE_BLUEPRINT_INTERFACE, CREATE_BLUEPRINT_STRUCT, CREATE_FUNCTION, CREATE_MACRO, CREATE_VARIABLE, DELETE_FUNCTION, DELETE_MACRO, DELETE_VARIABLE, DUPLICATE_FUNCTION, DUPLICATE_VARIABLE, RENAME_FUNCTION, RENAME_MACRO, RENAME_VARIABLE, SET_FUNCTION_ACCESS

### Character (12 commands)
ADD_CHARACTER_COMPONENT, CONFIGURE_CHARACTER_COLLISION, CREATE_CHARACTER_CLASS, DELETE_CHARACTER_COMPONENT, GET_CHARACTER_COMPONENTS, RENAME_CHARACTER_COMPONENT, SET_CHARACTER_ANIMATION_BLUEPRINT, SET_CHARACTER_CLASS_DEFAULT, SET_CHARACTER_COMPONENT_LOCATION, SET_CONTROLLER_ROTATION_RATE, SET_DEFAULT_MOVEMENT_MODE, SET_MAX_WALK_SPEED

### Input (8 commands)
ADD_INPUT_AXIS_MAPPING, ADD_INPUT_AXIS_VALUE, ADD_INPUT_KEY_MAPPING, BIND_INPUT_ACTION, DELETE_INPUT_ACTION, DELETE_INPUT_MAPPING, GET_INPUT_MAPPINGS, UNBIND_INPUT_ACTION

### Level (12 commands)
ADD_ACTOR_COMPONENT, ADD_LEVEL_ACTOR, ADD_LIGHT_TO_LEVEL, ADD_PRIMITIVE_TO_LEVEL, DELETE_ACTOR_COMPONENT, GET_LEVEL_ACTORS, SET_ACTOR_COMPONENT_LOCATION, SET_ACTOR_ROTATION, SET_LEVEL_STREAMING, SET_PRIMITIVE_MATERIAL, SPAWN_ACTOR_AT_LOCATION, UNLOAD_LEVEL

### Material (8 commands)
ADD_MATERIAL_COMPONENT_PARAMETER, CREATE_MATERIAL_PARAMETER_COLLECTION, DELETE_MATERIAL_PARAMETER, DUPLICATE_MATERIAL, DUPLICATE_MATERIAL_INSTANCE, GET_MATERIAL_PARAMETERS, SET_MATERIAL_COLOR_PARAM, SET_MATERIAL_TEXTURE_PARAM

### Utility (7 commands)
BUILD_LIGHTING, COMPRESS_MATERIAL_SHADER, DELETE_ASSET, DUPLICATE_ASSET, GET_ASSET_INFO, RENAME_ASSET, SET_EDITOR_VIEWPORT_LOCATION

---

## Response Patterns

### Success Response
```json
{
  "Success": true,
  "Result": {
    /* command-specific data */
  }
}
```

### Error Response
```json
{
  "Success": false,
  "Error": "Blueprint not found at /Game/NonExistent"
}
```

### Common Result Fields
| Field | Type | Meaning |
| --- | --- | --- |
| NodeGUID | String | Unique identifier for spawned node |
| Assets | Array | List of matching assets (from LIST_ASSETS) |
| Sections | Array | List of montage sections |
| Bones | Array | List of skeleton bones |
| Sockets | Array | List of skeleton sockets |

---

## Type System

### Parameter Types (Tier 1+2)

| Type | Example | Validation |
| --- | --- | --- |
| **String** | "/Game/BP_MyBlueprint" | No validation |
| **Integer** | "100" | Min/Max bounds enforced |
| **Float** | "3.14" | Min/Max bounds enforced |
| **Bool** | "true"/"false" | true/false only |
| **Enum** | "Idle" | Must be from enum values |

### Type Conversions

```python
# String is always safe
"100"  # → always String

# Integer (range: -100000 to 100000)
"100"  # → int (valid)
"-50000"  # → int (valid)
"200000"  # → error (exceeds max)

# Float (range: 0 to 100)
"3.14"  # → float (valid)
"50"  # → float (valid)
"150"  # → error (exceeds max)

# Bool
"true"  # → bool true
"false"  # → bool false
"1"  # → error

# Enum
"Opaque"  # → valid
"Invisible"  # → error (not in enum)
```

---

## Error Handling

### Common Errors
| Error | Cause | Solution |
| --- | --- | --- |
| "Blueprint not found" | Asset doesn't exist | Check path format (/Game/...) and asset name |
| "Invalid parameter type" | Type mismatch (e.g., "abc" for Integer) | Convert to correct type |
| "Parameter out of bounds" | Value exceeds min/max | Use value within range |
| "Invalid enum value" | Enum value not in list | Use one of listed enum values |
| "MCP server not running" | :8090 not accessible | Verify with curl http://localhost:8090/tools/list |

---

## Examples by Use Case

### Setup Character from Scratch
```python
# 1. Create character Blueprint
await bridge._send_command(f"CREATE_BLUEPRINT_CLASS|/Game/BP_MyCharacter|/Script/Engine.Character")
bp_path = "/Game/BP_MyCharacter"

# 2. Set mesh
await bridge._send_command(f"SET_CHARACTER_MESH|{bp_path}|/Game/Mannequin/SK_Mannequin")

# 3. Configure capsule
await bridge._send_command(f"SET_CHARACTER_CAPSULE|{bp_path}|90|42")

# 4. Setup camera
await bridge._send_command(f"SET_CAMERA_BOOM|{bp_path}|300|60")
```

### Create Attack Montage
```python
montage_path = "/Game/Montages/MontageAttack"
skeleton = "/Game/Mannequin/SK_Mannequin"

# 1. Create montage
await bridge._send_command(f"CREATE_ANIM_MONTAGE|{montage_path}|{skeleton}")

# 2. Add attack section
await bridge._send_command(f"ADD_MONTAGE_SECTION|{montage_path}|Attack|0")

# 3. Add impact notify at 0.5s
await bridge._send_command(f"ADD_MONTAGE_NOTIFY|{montage_path}|AnimNotify_PlaySound|0.5")
```

### Query Assets
```python
# List all Character Blueprints in /Game/Characters
result = await bridge._send_command("LIST_ASSETS|Character|/Game/Characters")
blueprints = result["Result"]["Assets"]
print(blueprints)
# → ["/Game/Characters/BP_Player", "/Game/Characters/BP_Enemy", ...]
```

---

## Python Client

`Content/Python/graphbridge_bridge.py`'s `UnrealBridge` class provides
typed, async, doc-commented methods for the common commands (see its
docstrings for the full method list) instead of hand-building pipe-delimited
strings:

```python
import asyncio
from graphbridge_bridge import UnrealBridge

async def main():
    bridge = UnrealBridge()
    await bridge.connect()

    result = await bridge.spawn_node(
        bp_path="/Game/BP_MyBlueprint",
        node_class="K2Node_VariableGet",
        node_title="Get X",
        x=100,
        y=200,
    )
    print(result["Result"]["NodeGUID"])

    await bridge.close()

asyncio.run(main())
```

Not every one of the 129 commands has a typed wrapper method yet — for
anything without one, use `bridge._send_command("OP|param1|param2|...")`
directly, matching the pipe-delimited format documented above.

---

## Versioning & Deprecation

GraphBridgev2 ships with no deprecated commands. All 129 tools are production-ready and will not be renamed or removed in v2.

v3.0.0 (future plugin) will add new commands; v2 receives critical bug fixes only.

---

**Last updated**: v2.0.0 final release
