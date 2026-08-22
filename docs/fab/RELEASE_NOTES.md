# GraphBridge AI — Release Notes

## Version 1.5.2 (Current)

### Fix: CAPTURE_VIEW Now Returns MCP Image Blocks
The agent can now see images. CAPTURE_VIEW ships functional for the first time.

**v1.5.0 and v1.5.1 shipping defect (fixed in 1.5.2):**
- v1.5.0 shipped CAPTURE_VIEW non-functional: produced OpenEXR files (unviewable by agents) and returned base64 in text payloads instead of MCP image content blocks.
- v1.5.1 fixed the PNG encoding (OpenEXR → PNG) but MCP image block detection was incomplete.
- v1.5.2 completes the fix: MCP server now robustly detects PNG magic bytes in base64 payloads and emits proper image content blocks to the agent.

**What changed since v1.5.1:**
- Improved PNG detection: handles edge cases (whitespace, JSON array escaping)
- Multi-angle support: responses with `Angles > 1` now correctly return multiple image blocks
- Size validation: payloads exceeding ~1MB return clear error messages with resolution/angle guidance
- Error reporting: size-limit errors describe the specific cause so agents can adjust parameters

**Agent integration:** The agent describes what's in the image — blade geometry, weapon placement, mesh deformation — enabling visual verification that numeric checks alone cannot provide.

**Do NOT ship to Fab until agent confirms:** This fix has been validated at the HTTP endpoint level, but the full flow (agent seeing images, describing them, guiding fixes) requires end-to-end testing. Acceptance test: run `CAPTURE_VIEW|CaptureSubject|hand_r_weapon|40|0|-15|512|Unlit|1` and confirm the agent describes the visible contents.

---

## Version 1.5.1

### New Feature: CAPTURE_VIEW (Fixed)
The agent can now *see*. Render the scene from any viewpoint and get back PNG images.

**v1.5.0 shipping defect (fixed in 1.5.1):** v1.5.0 produced OpenEXR files instead of PNG (unviewable by agents) and returned base64 in text payloads instead of MCP image blocks. Both fixed:
- Capture source switched from HDR to LDR (forces PNG, not EXR)
- MCP server emits proper image content blocks (agent can now view images)
- PNG magic-byte validation catches format mismatches
- Mode flags (Lit/Unlit/Wireframe) now functional

**What this solves:**
- Blade meshes attach with numeric verification but look visually wrong
- Weapon scales are guessed because nobody can see them  
- Arena geometry built at wrong angles because measurement confirms wrong values
- Every one of these cases needed one glance; measurement alone cannot catch them

**The command:**
```
CAPTURE_VIEW|TargetActor|Focus|Distance|Yaw|Pitch|Res|Mode|Angles|PinPose
```

**Parameters:**
- `Target` — actor label or class name (required)
- `Focus` — socket/bone name on skeletal mesh, or "none" for actor origin (default: none)
- `Distance` — cm from target along camera direction (default: 500)
- `Yaw`/`Pitch` — camera angles in degrees (default: 0°)
- `Res` — output resolution in pixels; 256–1024 (default: 512)
- `Mode` — `Lit` / `Unlit` (default) / `Wireframe`
- `Angles` — capture this many views at even intervals; 1–8 (default: 1)
- `PinPose` — (reserved) force skeletal mesh to reference pose before capture

**Returns:** PNG image(s) as MCP image content blocks, not text. Single view returns one block; `Angles > 1` returns array of blocks. Images are deterministic: same inputs = same output, no animated exposure or post-process.

**Performance:** Default 512px image ~50–100 KB base64 in agent context. Single captures run in ~50ms on modern hardware.

---

## Version 1.4.1

### Enhancement: Diagnostic Error Messages
`DELETE_NODE` and other commands that failed now report the specific cause instead of bare `false`.

**Real example:** An agent transposed `DELETE_NODE` arguments (grammar is `BPPath|NodeId|GraphName`, but `SPAWN_NODE` puts graph name last). Got generic failure, concluded the plugin refused to delete input event nodes for 30+ attempts. The command worked correctly when called correctly.

**Fix:** Failures now name the cause. Transposition detected explicitly in argument validation.

---

## Version 1.4.0 and Earlier

See commit history or [the earlier roadmap](https://github.com/corwindevacct-droid/Graph-Bridge-v2).

Function graphs, macro graphs, level actor placement, AnimMontage support, Material Editor integration, IK Rig, PhysicsAssets, BlendSpaces, DataTables, Skeleton sockets, Input Mapping Contexts, Widgets, and dozens of other Blueprint/animation graph operations.
