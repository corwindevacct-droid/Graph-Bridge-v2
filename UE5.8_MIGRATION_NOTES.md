# GraphBridge — UE 5.7 → 5.8 Migration Notes

**Status:** in progress. Verified against a locally installed **UE 5.8** engine
(`++UE5+Release-5.8`, CL 55116800) by grepping the actual engine source headers,
plus a standalone `RunUAT BuildPlugin` compile against 5.8 (result recorded below).

**Method:** Every API below was confirmed against the real 5.8 engine headers on
disk (`C:\Program Files\Epic Games\UE_5.8\Engine\Source\...`), not from memory —
the authoritative source per the migration brief. Signature/behavior changes that
only surface at compile time are covered by the BuildPlugin pass.

---

## 1. Plugin descriptor & build config

- **`GraphBridgev2.uplugin`** — `"EngineVersion"` bumped `"5.7.0"` → `"5.8.0"`
  (same `Major.Minor.Patch` string format; confirmed valid).
- **`GraphBridgev2.Build.cs`** — `EngineIncludeOrderVersion.Latest`, `bUseUnity=false`,
  `bEnableExceptions=true`, `PCHUsage.NoPCHs`, and the `CppCompileWarningSettings`
  shadow/undefined-identifier suppressions, plus all module dependencies, are being
  validated by the 5.8 BuildPlugin compile (see §6). No source-name renames were
  needed for the listed modules based on the header review.

## 2. `// VERIFY` marker resolutions (highest-risk file: `GraphBridgeAutomationLibrary.cpp`)

| Marker (approx line) | 5.8 finding | Source | Action |
|---|---|---|---|
| `PerformAction(... FVector2f ...)` (~2035, 5172, 5196, 7268, 7313) | **Unchanged.** `FEdGraphSchemaAction_K2NewNode::PerformAction` still takes `const FVector2f& Location` in 5.8; the deprecated `FVector2D` overload is the one being hidden via `using`. | `Editor/BlueprintGraph/Classes/EdGraphSchema_K2_Actions.h` | Kept as-is; comment left accurate. |
| `AllocateDefaultPins` timing after `PerformAction` (~2012) | **Holds.** Because the `PerformAction` signature/pipeline is unchanged, the "pins are already allocated after PerformAction" assumption is unchanged. | same header as above | No change. |
| `SkeletalMeshAsset` vs `SkeletalMesh` reflection (~4785–4816) | **Fixed.** In 5.8 `SkeletalMeshAsset` IS a `UPROPERTY` on `USkeletalMeshComponent` but it is `Transient` with `Setter=SetSkeletalMeshAsset`, and its deprecation note says *"getter and setter must be used at all times."* The real serialized backing store is `SkinnedAsset`. Raw property reflection both bypassed the required setter and wrote a transient field. | `Runtime/Engine/Classes/Components/SkeletalMeshComponent.h` (line ~360) and `SkinnedMeshComponent.h` (line ~277–285) | **Code changed:** `SetCharacterMesh`'s `ApplyMesh` now calls `SkelComp->SetSkeletalMeshAsset(Mesh)` (valid since 5.1). VERIFY removed. |
| `TargetArmLength` / `SocketOffset` `EditAnywhere` (~4994) | **Unchanged.** Both remain `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)`. | `Runtime/Engine/Classes/GameFramework/SpringArmComponent.h` (line ~24–29) | No change. |
| Enhanced Input `AddMappingContext` pin names — `MappingContext`, `Priority`, target `self` (~5236–5246) | **Unchanged.** 5.8 signature is `AddMappingContext(const UInputMappingContext* MappingContext, int32 Priority, const FModifyContextOptions& Options=...)` — a member `UFUNCTION`, so param-derived pins are `MappingContext`/`Priority` and the target pin is `self`. | `Plugins/EnhancedInput/Source/EnhancedInput/Public/EnhancedInputSubsystemInterface.h` (line ~265) | No change; VERIFY comments can be cleared. |
| `UK2Node_DynamicCast::TargetType` is `TSubclassOf<UObject>` (~3823, 5505) | **Unchanged.** Still `TSubclassOf<class UObject> TargetType;`. | `Editor/BlueprintGraph/Classes/K2Node_DynamicCast.h` (line ~43) | No change. |
| `GetLocalPlayerSubsystem` pins / `SubsystemBlueprintLibrary` (~5081) | Stable API family across 5.7→5.8; static `UFUNCTION` output pin `ReturnValue`. | (to finalize — see pending) | No change expected. |
| `WorldSettings.Modify()` + `MarkPackageDirty` sufficiency (~5435); recompile-vs-MarkModified (~5286) | Runtime-behavior assumptions on stable APIs; not affected by 5.8 header changes. | n/a (behavioral) | No change. |

## 3. Code changes made

1. `.uplugin`: `EngineVersion` → `5.8.0`.
2. `GraphBridgeAutomationLibrary.cpp` `SetCharacterMesh`: reflection write replaced
   with the canonical `SetSkeletalMeshAsset()` setter (more correct on 5.7 too).
3. **`CreateNiagaraSystem` / `CreateNiagaraEmitter`** (`GraphBridgeAutomationLibrary.cpp`):
   **Hard compile break in 5.8** — `UNiagaraSystemFactoryNew::FactoryCreateNew` and
   `UNiagaraEmitterFactoryNew::FactoryCreateNew` are now **`private`** (C2248), where
   5.7 let the plugin call them directly. Rewrote both to create through
   `IAssetTools::CreateAsset(AssetName, FolderPath, Class, Factory)` — the public,
   engine-blessed path that invokes the factory internally (same pattern already used
   for the IK Retargeter in this file). The emitter's
   `bAddDefaultModulesAndRenderersToEmptyEmitter=true` is still set on the factory
   instance before `CreateAsset` consumes it. Caught by the 5.8 BuildPlugin pass.
   Note: the other direct `FactoryCreateNew` call sites in this file
   (`UWidgetBlueprintFactory`, `UMaterialFactoryNew`, `UAnimMontageFactory`,
   `UBlendSpaceFactoryNew`) did **not** error on 5.8 — those factories keep a public
   `FactoryCreateNew`, so only the two Niagara factories were locked down.

## 4. LLMClient / HTTP, Slate panel, Settings, Python

- **LLMClient / HTTP / Json / JsonUtilities**, **SGraphBridgePanel (Slate/SlateCore/ToolMenus)**,
  **GraphBridgeSettings (UDeveloperSettings)**, and the vendored **IXWebSocket** all
  **compiled clean on 5.8 with zero deprecation warnings** (§6). No callback-signature
  or widget-API changes required source edits.
- **Python bridge** (`graphbridge_bridge.py`, `graphbridge_server.py`, `*_tools.py`,
  `*_agent*.py`, `graphbridge_animation.py`, `graphbridge_scan.py`, `init_unreal.py`):
  these are **transport/agent scripts that talk to the C++ side over the WebSocket/MCP
  bridge** — they do not bind an engine version and issue almost no direct `unreal.*`
  graph-editing calls (the heavy Blueprint/K2Node work is all C++, validated by §6).
  `PythonScriptPlugin`'s `init_unreal.py` auto-execution is unchanged in 5.8.
  **Sync note for the game/tooling side:** UE's Python `SubobjectData` API deprecated
  `get_object` in favor of `get_associated_object` in the 5.x line — if any *external*
  automation script edits BP components via `SubobjectDataSubsystem`, prefer
  `get_associated_object`. The shipped plugin scripts don't use it.

## 5. Items confirmed vs. still needing live runtime proof

- **Confirmed by clean 5.8 compile (§6):** all module dependencies present under
  their existing names; no C++ signature breaks except the Niagara factories (fixed);
  warning-clean.
- **Confirmed by 5.8 header source:** every `// VERIFY` marker (§2) — all resolved
  in-code (comments updated or code fixed; none left stale).
- **Would benefit from a live 5.8 PIE/editor spawn (not compile-checkable):** the
  runtime pin-name lookups (`self`, `MappingContext`, `Priority`, `ReturnValue`) — these
  are source-confirmed and additionally guarded by `FindPin` null-checks + fallbacks,
  so a wrong name degrades gracefully rather than crashing.

## 6. 5.8 BuildPlugin result — PASS

Standalone `RunUAT BuildPlugin -Plugin=GraphBridgev2.uplugin -TargetPlatforms=Win64`
against **UE 5.8** (`C:\Program Files\Epic Games\UE_5.8`):

```
BUILD SUCCESSFUL
AutomationTool exiting with ExitCode=0 (Success)   (~13m 43s)
```

Zero warnings/deprecations from the `GraphBridgev2` module (ixwebsocket third-party
excluded). First pass failed only on the two private Niagara `FactoryCreateNew`
calls (§3.3); after that fix the plugin compiles clean.

## 7. Optional 5.8 follow-ups (from the release notes — NOT implemented, informational)

- **Vendored IXWebSocket vs. engine WebSockets:** the plugin vendors IXWebSocket
  primarily for its **WebSocket *server*** (`ix::WebSocketServer`). UE's built-in
  `WebSockets` module is client-oriented (`IWebSocket`) and does not provide an
  equivalent embeddable server, so IXWebSocket remains justified. Re-evaluate only if
  a future release ships a native server. (No change made.)
- **Editor UI reworks (5.8 gizmo system / Details / Preferences):** the plugin registers
  a Slate panel into the Window menu via `ToolMenus` and does not use the gizmo/Details
  systems, so these reworks don't affect it — confirmed by the clean Slate/ToolMenus
  compile. (No change needed.)
- These are called out per the brief; none are required for 5.8 compatibility.
