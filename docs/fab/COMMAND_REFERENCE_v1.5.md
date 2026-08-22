# GraphBridge AI — Command Reference (v1.5.0)

## CAPTURE_VIEW — Scene Capture & Visualization

Render the scene from a controlled viewpoint and return PNG image(s).

### Syntax
```
CAPTURE_VIEW|Target[|Focus[|Distance[|Yaw[|Pitch[|Res[|Mode[|Angles[|PinPose]]]]]]]]
```

### Parameters

| Param | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `Target` | string | required | — | Actor label or class name to position camera relative to |
| `Focus` | string | "none" | socket/bone name | Resolve camera focus to named socket on skeletal mesh component, or bone. "none" = actor origin |
| `Distance` | float | 500 | 0–∞ cm | Distance from focus point along camera direction |
| `Yaw` | float | 0 | 0–360° | Horizontal camera angle (relative to focus point) |
| `Pitch` | float | 0 | -90–90° | Vertical camera angle (relative to focus point) |
| `Res` | int | 512 | 256–1024 px | Output image resolution (square, RGBA8). Performance cost increases with resolution |
| `Mode` | string | "Unlit" | Lit / Unlit / Wireframe | Rendering mode. Unlit reveals silhouette & orientation; Lit often too dark for Paragon assets |
| `Angles` | int | 1 | 1–8 | Capture this many views at even yaw intervals (e.g. `Angles=3` captures 0°, 120°, 240°) |
| `PinPose` | string | "" | "true" | (Reserved) Force target skeletal mesh to reference pose before capture. Not yet implemented |

### Returns

**Success:**
- Single image (`Angles=1`): base64-encoded PNG in payload
- Multiple images (`Angles>1`): JSON array of base64 PNG strings in payload

Both are returned as MCP image content blocks with `type: "image"`, `mimeType: "image/png"`, and the base64 data.

**Error:** diagnostic message in standard error format

### Examples

#### Capture a character from the front
```
CAPTURE_VIEW|BP_Hero_C
```

#### Capture a character from 3 angles simultaneously
```
CAPTURE_VIEW|BP_Hero_C|||||||3
```

#### Focus on a hand socket and zoom in
```
CAPTURE_VIEW|BP_Hero_C|hand_r|200|0|0
```

#### Wireframe view of a weapon mesh at eye level
```
CAPTURE_VIEW|BP_Sword_C|||0|0|512|Wireframe
```

#### High-resolution capture for inspection
```
CAPTURE_VIEW|BP_Rigged_Mesh_C||500|0|0|1024|Unlit
```

### Behavior & Guarantees

- **Determinism:** Same inputs always produce identical images (no animated exposure, post-process, or lighting variance)
- **Transient resources:** Render target and capture actor are destroyed after each call, even on error
- **Threading:** Call on game thread (automatically enforced in editor/PIE)
- **Editor & PIE:** Works in both contexts; uses appropriate world
- **Performance:** ~50 ms per capture on modern hardware; scales with resolution
- **Fallback:** if `Focus` socket/bone not found, uses actor location

### Limitations

- No real-time video or streaming (single-frame captures only)
- Resolution capped at 1024px to control agent context size (~1 KB per 512px image after base64)
- No custom lighting setup (uses current level lighting)
- Rotation-only camera (no FOV override; uses default)

### Technical Notes

Internally:
1. Spawns transient `ASceneCapture2D` actor at computed location
2. Creates transient `UTextureRenderTarget2D` at requested resolution
3. Calls `CaptureScene()` on the capture component
4. Exports render target to PNG via `UKismetRenderingLibrary::ExportRenderTarget()`
5. Base64-encodes PNG and returns
6. Destroys capture actor and render target

### Comparison: When to Use CAPTURE_VIEW

| Goal | Tool |
|------|------|
| Verify blade attachment looks correct | CAPTURE_VIEW |
| Check mesh scale against character | CAPTURE_VIEW |
| Confirm rigging alignment | CAPTURE_VIEW |
| Get exact socket location (cm) | GET_ASSET_PROPERTY (socket offset) |
| Debug animation state | CAPTURE_VIEW (from keyframe) |
| Check material appearance | CAPTURE_VIEW (with `Mode=Lit`) |

---

## Other Commands (v1.4.1 and Earlier)

For the full list of 90+ commands (Blueprint graph editing, animation systems, level actors, materials, widgets, etc.), refer to:
- The GitHub repository command documentation
- Inline help in the plugin's MCP tool registry (`tools/list` endpoint)
- Example scripts in `Content/Python/`

**Notable v1.4.1 improvement:** All command failures now include diagnostic messages, not bare error codes. Example:

```
BEFORE (v1.4.0): DELETE_NODE|/Game/BP_X|...| → "false"
AFTER (v1.4.1):  DELETE_NODE|/Game/BP_X|...| → "ERR:NodeId '...' not found in EventGraph"
```

This change eliminates ambiguous failures caused by argument transposition or typos.
