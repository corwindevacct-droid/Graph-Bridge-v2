// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include <memory>

// Forward-declare ix types so we don't pull in IXWebSocket headers here.
// The full headers are included only in the .cpp, keeping compile times down.
namespace ix
{
    class WebSocketServer;
    class WebSocket;
}

class FGraphBridgeMCPServer;

// Editor-only forward declarations — placed here (before generated.h) so that
// the private #if WITH_EDITOR methods below compile in editor builds.
// UEdGraphNode* and UBlueprint* only need forward declarations (pointer use only);
// FEdGraphPinType is returned by value and requires its full definition.
#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
class UBlueprint;
class UEdGraphNode;
class UEdGraph;
class UEditorActorSubsystem;
#endif

// !! IMPORTANT: The generated.h MUST be the very last #include in this file.
// DECLARE_DYNAMIC_DELEGATE macros expand into UCLASS/USTRUCT machinery that
// requires the generated header to already be loaded by UHT. Any macro that
// touches UObject reflection (DECLARE_DYNAMIC_*, UPROPERTY, etc.) must live
// AFTER the generated.h line — or you get the C2143 "missing ';'" error you
// saw in the build log.
#include "GraphBridgeAutomationLibrary.generated.h"

// Delegate used when you want Blueprint/C++ code to push a message to a
// connected client without going through the full command pipeline.
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnSendMessage, FString, Message);

UCLASS()
class GRAPHBRIDGEV2_API UGraphBridgeAutomationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // -----------------------------------------------------------------------
    // Blueprint-callable API
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "GraphBridge")
    static void StartGraphBridgeServer(int32 Port = 8080);

    UFUNCTION(BlueprintCallable, Category = "GraphBridge")
    static void StopGraphBridgeServer();

    UFUNCTION(BlueprintCallable, Category = "GraphBridge")
    static bool IsServerRunning();

    UFUNCTION(BlueprintCallable, Category = "GraphBridge")
    static void SetSendMessageDelegate(FOnSendMessage InDelegate);

    // -----------------------------------------------------------------------
    // MCP (Model Context Protocol) transport — a second, independent entry
    // point into the exact same commands as the WebSocket bridge above (both
    // route through DispatchCommandSync). Runs on its own port so it can be
    // started/stopped independently of the WebSocket server.
    // -----------------------------------------------------------------------

    UFUNCTION(BlueprintCallable, Category = "GraphBridge|MCP")
    static bool StartMCPServer(int32 Port = 8090);

    UFUNCTION(BlueprintCallable, Category = "GraphBridge|MCP")
    static void StopMCPServer();

    UFUNCTION(BlueprintCallable, Category = "GraphBridge|MCP")
    static bool IsMCPServerRunning();

    /**
     * Synchronous in-process command dispatch — same routing as the WebSocket handler
     * but returns the JSON result string directly instead of sending it over the wire.
     * Called by FGraphBridgev2Module::HandleGraphCommand for the LLM agentic loop.
     * Must be called on the game thread.
     */
    static FString DispatchCommandSync(const FString& Command);

    // -----------------------------------------------------------------------
    // Static state — definitions live in the .cpp
    // -----------------------------------------------------------------------

    static FOnSendMessage SendMessageDelegate;

private:
    // Server owns the IXWebSocket server instance
    static std::unique_ptr<ix::WebSocketServer> Server;

    // MCPServer owns the MCP (HTTP) server instance — independent of Server above.
    static std::unique_ptr<FGraphBridgeMCPServer> MCPServer;

    // -----------------------------------------------------------------------
    // Internal command dispatch — takes the raw pipe-delimited string and the
    // socket to reply on. NOT exposed to Blueprint.
    // -----------------------------------------------------------------------
    static void ExecuteAtomicCommand(FString Command, ix::WebSocket* Sender);

    // ExecuteAtomicCommand's else-if dispatch chain hit MSVC's C1061 "blocks
    // nested too deeply" limit once it grew past ~116 branches (a single
    // else-if chain nests one compound-statement level per branch). Split at
    // roughly the midpoint (right after ADD_VARIABLE) into this second
    // function purely to keep each chain's nesting depth under the compiler
    // limit -- no command's logic changed, Op/P/Sender are just forwarded
    // from ExecuteAtomicCommand's final else. Not exposed to Blueprint.
    static void ExecuteAtomicCommandExtended(const FString& Command, const FString& Op,
                                             const TArray<FString>& P, ix::WebSocket* Sender);

    // -----------------------------------------------------------------------
    // Response helper — builds the JSON envelope and sends it back
    // -----------------------------------------------------------------------
    static void SendResponse(ix::WebSocket* Sender, bool bSuccess,
                             FString Command, FString Message, FString Payload);

    // -----------------------------------------------------------------------
    // Editor-only graph operations
    // -----------------------------------------------------------------------
#if WITH_EDITOR

    // Asset / node lookup helpers
    static UBlueprint*   GetBlueprintByPath(FString AssetPath);
    static UEdGraphNode* FindNodeByName(UBlueprint* Blueprint, FString NodeIdentifier);
    static UEdGraphNode* FindNodeById(UBlueprint* Blueprint, FString NodeId);
    static UEdGraphNode* FindNodeAnywhere(UBlueprint* Blueprint, const FString& NodeId);

    // Type resolution for SPAWN_VARIABLE, ADD_VARIABLE, and parameter opcodes
    static FEdGraphPinType ResolveTypeString(const FString& TypeString);

    // Graph lookup — resolves EventGraph (any UbergraphPage) or named Function/Macro graph
    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

    // Graph mutation verbs
    static FString SpawnEventNode(FString BlueprintPath, FString EventFuncName,
                                  FString Comment, int32 X, int32 Y);

    // GraphName: optional. Empty (default) preserves the original behavior
    // of always targeting UbergraphPages[0]. Non-empty resolves the target
    // graph via FindGraphByName instead (EventGraph, a Function graph, or a
    // Macro graph) — subsumes what SPAWN_NODE_IN_GRAPH already did.
    static FString SpawnNode(FString BlueprintPath, FString NodeClass,
                             FString Comment, int32 X, int32 Y, FString GraphName = TEXT(""));

    // Shared node-construction core used by both SpawnNode (always targets
    // UbergraphPages[0]) and SpawnNodeInGraph (targets any named graph).
    static FString SpawnNodeOnGraph(UBlueprint* Blueprint, UEdGraph* Graph,
                                    FString NodeClass, FString Comment, int32 X, int32 Y);

    // Finds a graph by name: "EventGraph" (or a UbergraphPages name match)
    // resolves to the main event graph; otherwise matched by GetName()
    // against Blueprint->FunctionGraphs, then Blueprint->MacroGraphs (in
    // that order). Returns nullptr if not found.

    // Finds a node by GUID, comment, or full title WITHIN a single graph
    // (not the whole Blueprint) — used by the GraphName-scoped verbs below
    // so a caller who explicitly names a Function/Macro graph only ever
    // matches nodes actually inside it. GUID matches are unambiguous and
    // returned immediately; comment/title matches follow FindNodeByName's
    // convention of returning nullptr (not a guess) when more than one node
    // matches within that graph.
    static UEdGraphNode* FindNodeInGraph(UEdGraph* Graph, const FString& NodeId);

    // LIST_GRAPHS — lists every graph on a Blueprint (UbergraphPages,
    // FunctionGraphs, MacroGraphs), tagged with its type. Lets the agent
    // discover what graphs exist before deciding where to operate.
    // Returns pipe-delimited "GraphName~GraphType" entries (GraphType:
    // EventGraph|Function|Macro), "ERR:..." on failure.
    static FString ListGraphs(FString BlueprintPath);

    // CREATE_FUNCTION_GRAPH — thin wrapper around CreateFunction (identical
    // graph-creation logic — reused rather than duplicated) that instead
    // returns the new graph's K2Node_FunctionEntry node's GUID, so a caller
    // can immediately act on the entry node. CreateFunction itself is left
    // untouched (its own callers rely on it returning the function name).
    // Returns the new K2Node_FunctionEntry node's GUID on success,
    // "ERR:..." on failure.
    static FString CreateFunctionGraph(FString BlueprintPath, FString FunctionName);

    // CREATE_MACRO_GRAPH — creates a new macro graph via
    // FBlueprintEditorUtils::AddMacroGraph. Macro graphs are inlined at the
    // call site (not a real UFunction), can have multiple exec pins, and
    // can contain latent nodes — modeled as a distinct graph type from
    // CreateFunctionGraph, not just "another function". Their entry point
    // is a UK2Node_Tunnel with bCanHaveOutputs=true (created by the schema's
    // CreateMacroGraphTerminators), NOT a K2Node_FunctionEntry.
    // Returns the new entry tunnel node's GUID on success, "ERR:..." on failure.
    static FString CreateMacroGraph(FString BlueprintPath, FString MacroName);

    // SPAWN_NODE_IN_GRAPH — like SpawnNode, but targets any named graph
    // (EventGraph or a custom function graph created via CREATE_FUNCTION).
    static FString SpawnNodeInGraph(FString BlueprintPath, FString GraphName,
                                    FString NodeClass, FString Comment, int32 X, int32 Y);

    // Like FindNodeById, but searches UbergraphPages AND FunctionGraphs —
    // used by SET_NODE_POSITION, which must be able to move nodes created
    // via SPAWN_NODE_IN_GRAPH inside a custom function graph.
    static UEdGraphNode* FindNodeByIdAllGraphs(UBlueprint* Blueprint, FString NodeId);

    // Locates a previously-spawned UAnimGraphNode_StateMachineBase by GUID.
    // State machine nodes live in a normal K2-schema AnimGraph, so
    // FindNodeByIdAllGraphs already reaches them.
    static class UAnimGraphNode_StateMachineBase* FindStateMachineNode(UBlueprint* Blueprint, const FString& NodeGUID);

    // Locates a UAnimStateNodeBase (state or transition node) by GUID inside
    // a specific state machine's EditorStateMachineGraph.
    static class UAnimStateNodeBase* FindAnimStateNode(class UAnimGraphNode_StateMachineBase* MachineNode, const FString& NodeGUID);

    // SET_NODE_POSITION — moves a node to specific graph coordinates.
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetNodePosition(FString BlueprintPath, FString NodeGUID, int32 X, int32 Y);

    // CREATE_INPUT_ACTION — creates a new UInputAction asset at AssetPath.
    // ValueType: bool, float, axis1d, axis2d, axis3d
    // (float is an alias for axis1d — EInputActionValueType::Axis1D is a float).
    // Returns the canonical asset path ("PackagePath.AssetName") on success.
    static FString CreateInputAction(FString AssetPath, FString ValueType);

    // SPAWN_ACTOR_IN_LEVEL — places a Blueprint actor instance in the current
    // editor level via UEditorActorSubsystem::SpawnActorFromClass (the
    // non-deprecated replacement for the Editor Scripting Utilities plugin's
    // UEditorLevelLibrary, which the UE 5.7 Python API itself warns is
    // deprecated in favor of the editor-subsystem equivalents).
    // Returns the spawned actor's label on success, "ERR:..." on failure.
    static FString SpawnActorInLevel(FString BlueprintPath, float X, float Y, float Z, float RotYaw);

    // LIST_LEVEL_ACTORS — lists actors in the current editor level via
    // UEditorActorSubsystem::GetAllLevelActors(). Filter is an optional
    // case-insensitive substring match on class name or actor label.
    // Returns pipe-delimited "Label~Class~X~Y~Z" entries.
    static FString ListLevelActors(FString Filter);

    // Shared level/world-context lookup used by the level-actor commands.
    static UEditorActorSubsystem* GetEditorActorSubsystem();

    // Finds a level actor by label (case-insensitive exact match) via
    // UEditorActorSubsystem::GetAllLevelActors(). Returns nullptr if not found.
    static AActor* FindLevelActorByLabel(const FString& ActorLabel);

    // SET_ACTOR_TRANSFORM — moves/rotates/scales a level actor by its label.
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetActorTransform(FString ActorLabel, FVector Location, FRotator Rotation, FVector Scale);

    // DELETE_LEVEL_ACTOR — removes an actor from the level by its label, via
    // UEditorActorSubsystem::DestroyActor (notifies the editor the actor was
    // destroyed, same as the "Delete" command in the World Outliner).
    // Returns empty string on success, "ERR:..." on failure.
    static FString DeleteLevelActor(FString ActorLabel);
    // GraphName: optional (see SpawnNode). Empty preserves the original
    // UbergraphPages[0]-only behavior; non-empty resolves via
    // FindGraphByName and scopes the node lookup to just that graph.
    static bool    DisconnectPins(FString BlueprintPath,
                                  FString NodeA, FString PinA,
                                  FString NodeB, FString PinB, FString GraphName = TEXT(""));
    // Returns empty string on success, error description on failure
    static FString ConnectPins(FString BlueprintPath,
                               FString NodeA, FString PinA,
                               FString NodeB, FString PinB, FString GraphName = TEXT(""));
    // GraphName: optional. Empty preserves the original FindNodeAnywhere
    // (broadest search — Ubergraph/Function/AnimGraph/state machines/bound
    // graphs) default; non-empty scopes the lookup to just that graph.
    static bool    DeleteNode(FString BlueprintPath, FString NodeId, FString GraphName = TEXT(""));
    // GraphName: optional. Empty preserves the original UbergraphPages-only
    // behavior; non-empty scopes the comment match to just that graph.
    static bool    ClearNodes(FString BlueprintPath, FString CommentMatch, FString GraphName = TEXT(""));
    static bool    SetPinDefault(FString BlueprintPath,
                                 FString NodeId, FString PinName, FString DefaultValue);
    // GraphName: optional. Empty preserves the original FindNodeByName/
    // FindNodeById (UbergraphPages-only) lookup; non-empty scopes it to
    // just that graph.
    static FString GetNodePins(FString BlueprintPath, FString NodeName, FString GraphName = TEXT(""));

    // GET_PIN_CONNECTIONS — returns comma-separated NODEGUID:PinName entries
    // for every pin linked to the named pin. Returns "ERR:..." on failure,
    // empty string (not an error) if the pin exists but has no connections.
    static FString GetPinConnections(FString BlueprintPath, FString NodeId, FString PinName);

    // GET_PIN_DEFAULT — returns the current DefaultValue on a pin, or the
    // asset path of DefaultObject for object/class pins. Returns "ERR:..."
    // on failure, empty string (not an error) if no default is set.
    static FString GetPinDefault(FString BlueprintPath, FString NodeId, FString PinName);

    static bool    CompileBlueprint(FString BlueprintPath);
    static bool    SaveBlueprint(FString BlueprintPath);
    static FString  SetAnimClass(FString BlueprintPath, FString ComponentName, FString AnimBPPath);

    // Variable operations
    static FString SpawnVariable(FString BlueprintPath,
                                 FString VarName, FString TypeString, FString Category);
    static bool    SetVariableDefault(FString BlueprintPath,
                                      FString VarName, FString DefaultValue);

    // Variable management (v1.5)
    static FString AddVariable(FString BlueprintPath, FString VarName,
                               FString VarType, FString Category);
    static FString SetVariableType(FString BlueprintPath, FString VarName,
                                   FString NewType);
    static FString ListVariables(FString BlueprintPath);

    // Discovery helpers
    // GraphName: optional. Empty preserves the original UbergraphPages-only
    // behavior; non-empty (resolved via FindGraphByName) lists just that graph.
    static FString ListNodes(FString BlueprintPath, FString GraphName = TEXT(""));
    static FString FindNodeClass(FString PartialName);
    static FString ListAssets(FString Filter);

    // Enhanced Input wiring
    static bool    SetInputAction(FString BlueprintPath,
                                  FString NodeId, FString InputActionPath);

    // Function reference — sets the UFunction on a K2Node_CallFunction and
    // rebuilds its pins. Call this immediately after SPAWN_NODE for call nodes.
    // Returns empty string on success, error description on failure
    static FString SetFunctionRef(FString BlueprintPath,
                                  FString NodeId, FString ClassName, FString FunctionName);

    // Event reference — binds a K2Node_Event to a named function on the parent
    // class chain and reconstructs its pins. Call immediately after SPAWN_NODE
    // for event nodes. e.g. FunctionName = "ReceiveBeginPlay"
    // Returns empty string on success, error description on failure
    static FString SetEventRef(FString BlueprintPath,
                               FString NodeId, FString FunctionName);

    // SET_CUSTOM_EVENT_NAME — names a K2Node_CustomEvent (whose
    // CustomFunctionName defaults to "None" and isn't reachable via
    // SetEventRef, which requires binding to an existing parent-class
    // function). Returns empty string on success, "ERR:..." on failure.
    static FString SetCustomEventName(FString BlueprintPath,
                                      FString NodeId, FString EventName);

    // ADD_ARRAY_PIN — adds one more wildcard input pin to a K2Node_MakeArray
    // (mirrors the "+" button in the editor). Returns empty string on
    // success, "ERR:..." on failure.
    static FString AddArrayPin(FString BlueprintPath, FString NodeId);

    // Component addition — adds a USCS_Node to the Blueprint's SCS and compiles.
    // ComponentClass: C++ name e.g. "ProjectileMovementComponent" or BP asset path.
    // Returns empty string on success, error description on failure.
    static FString AddComponent(FString BlueprintPath,
                                FString ComponentClass, FString ComponentName,
                                FString ParentComponentName = TEXT(""));

    // Variable reference — binds a K2Node_VariableGet/Set to a named variable
    // and reconstructs its pins so the output type is resolved before connection.
    static bool    SetVariableRef(FString BlueprintPath,
                                  FString NodeId, FString VarName,
                                  FString& OutError);

    // SET_EXTERNAL_VARIABLE_REF — like SetVariableRef, but binds to a property
    // on an arbitrary class (not just this Blueprint's own hierarchy), e.g.
    // TargetArmLength on SpringArmComponent. Produces a non-self-context node
    // with a "Target" pin. Returns empty string on success, "ERR:..." on failure.
    static FString SetExternalVariableRef(FString BlueprintPath, FString NodeId,
                                          FString OwnerClassName, FString VarName);



    // ------------------------------------------------------------------
    // Generic reflection — works on any UObject asset
    // ------------------------------------------------------------------
    static FString ListAssetProperties(FString AssetPath);
    static FString GetAssetProperty(FString AssetPath, FString PropertyName);
    static FString SetAssetProperty(FString AssetPath, FString PropertyName, FString Value);

    // ------------------------------------------------------------------
    // BlendSpace inspection & mutation
    // ------------------------------------------------------------------
    static FString ListBlendSpaces(FString Filter);

    // ------------------------------------------------------------------
    // AnimMontage structural commands (v1.1)
    // These cannot be handled via generic reflection because:
    //   - SlotAnimTracks is not EditAnywhere
    //   - There is no RemoveSection API on UAnimMontage
    //   - Notifies require Link() to be called for correct timeline placement
    // ------------------------------------------------------------------
    static FString GetMontageInfo(FString AssetPath);
    static FString AddMontageSection(FString AssetPath, FString SectionName, float StartTime);
    static FString RemoveMontageSection(FString AssetPath, FString SectionName);
    static FString SetMontageSlot(FString AssetPath, int32 SlotIndex, FString NewSlotName);
    static FString AddMontageNotify(FString AssetPath, FString NotifyClassName, float TimeSeconds);
    // Adds a UAnimNotifyState (a begin/end window) rather than a single-frame
    // UAnimNotify. Separate function because the two are sibling classes with
    // different FAnimNotifyEvent fields — see the .cpp for why this isn't a
    // duration flag on AddMontageNotify.
    static FString AddMontageNotifyState(FString AssetPath, FString NotifyClassName, float StartSeconds, float DurationSeconds);
    static FString RemoveMontageNotify(FString AssetPath, int32 NotifyIndex);

    // ------------------------------------------------------------------
    // DataTable row commands (v1.2)
    // ADD_DATATABLE_ROW creates a default-value row; use SET_ASSET_PROPERTY
    // to fill fields afterward via generic reflection.
    // RENAME_DATATABLE_ROW uses FDataTableEditorUtils::RenameRow which
    // handles undo/redo and cross-reference fixup.
    // ------------------------------------------------------------------
    static FString ListDataTableRows(FString AssetPath);
    static FString AddDataTableRow(FString AssetPath, FString RowName);
    static FString DeleteDataTableRow(FString AssetPath, FString RowName);
    static FString RenameDataTableRow(FString AssetPath, FString OldName, FString NewName);

    // ------------------------------------------------------------------
    // Skeleton socket commands (v1.3)
    // Sockets live on USkeleton (shared across all meshes on that skeleton).
    // There is no Remove API — the Sockets array is filtered directly.
    // MOVE_SKELETON_SOCKET takes loc (cm) and rot (degrees) in UE native units.
    // ------------------------------------------------------------------
    static FString ListSkeletonSockets(FString AssetPath);
    static FString MoveSkeletonSocket(FString AssetPath, FString SocketName,
                                      FVector Location, FRotator Rotation);
    static FString DeleteSkeletonSocket(FString AssetPath, FString SocketName);

    // ------------------------------------------------------------------
    // Character Pipeline commands (v1.4)
    //
    // Input Mapping Context asset operations:
    //   CREATE_IMC       — create a new UInputMappingContext asset on disk
    //   ADD_IMC_MAPPING  — add a key→action mapping (with optional modifiers)
    //   REMOVE_IMC_MAPPING — remove a key→action mapping
    //   LIST_IMC_MAPPINGS  — JSON list of all mappings in an IMC
    //
    // Generic save:
    //   SAVE_ASSET       — save any UObject asset (not just Blueprints)
    //
    // Character Blueprint component setup:
    //   SET_CHARACTER_MESH    — set SkeletalMesh on a SkeletalMeshComponent template
    //   SET_CHARACTER_CAPSULE — set HalfHeight/Radius on the CapsuleComponent template
    //   SET_CAMERA_BOOM       — set TargetArmLength/SocketOffset on a SpringArmComponent
    //
    // Enhanced Input wiring:
    //   ADD_IMC_TO_CHARACTER  — spawns the GetPlayerController→GetSubsystem→
    //                           AddMappingContext node chain in BeginPlay
    //
    // GameMode / world:
    //   SET_GAMEMODE_PAWN  — set DefaultPawnClass on a GameMode Blueprint CDO
    //   GET_CURRENT_GAMEMODE — JSON with current editor-world GameMode class
    //   GET_PLAYER_START     — JSON list of PlayerStart actors in the current level
    //   SET_LEVEL_GAMEMODE   — override DefaultGameMode in AWorldSettings
    // ------------------------------------------------------------------
    static FString CreateIMC(FString AssetPath);
    static FString AddIMCMapping(FString IMCPath, FString ActionPath,
                                 FString KeyName, FString ModifierClasses);
    static FString RemoveIMCMapping(FString IMCPath, FString ActionPath, FString KeyName);
    static FString ListIMCMappings(FString IMCPath);

    static bool    SaveAsset(FString AssetPath);

    static FString SetCharacterMesh(FString BlueprintPath, FString MeshPath,
                                    FString ComponentName);
    static FString SetCharacterCapsule(FString BlueprintPath, float HalfHeight,
                                       float Radius, FString ComponentName);
    static FString SetCameraBoom(FString BlueprintPath, float ArmLength,
                                 FVector SocketOffset, FString ComponentName);

    static FString AddIMCToCharacter(FString BlueprintPath, FString IMCPath, int32 Priority);

    // SET_CAST_TARGET — sets TargetType on an existing UK2Node_DynamicCast and
    // reconstructs its pins so the "As [ClassName]" output pin is correctly typed.
    static FString SetCastTarget(FString BlueprintPath, FString NodeGUID,
                                 FString TargetClassName);

    // SET_SUBSYSTEM_CLASS — sets CustomClass on an existing UK2Node_GetSubsystem
    // ("Get Local Player Subsystem" etc.) and reconstructs its pins so the
    // ReturnValue output pin is correctly typed.
    static FString SetSubsystemClass(FString BlueprintPath, FString NodeGUID,
                                      FString SubsystemClassName);

    // RUN_PYTHON — executes arbitrary Python inside UE via IPythonScriptPlugin
    // and returns captured stdout as the payload.
    static FString RunPython(FString Code);

    // ------------------------------------------------------------------
    // Agent reliability commands (v1.6)
    // ------------------------------------------------------------------

    // CREATE_BLUEPRINT — creates a new Blueprint asset at AssetPath inheriting
    // from ParentClassName (short C++ name, A/U prefix optional, e.g.
    // "Character" or "ACharacter"). Returns the canonical asset path
    // ("PackagePath.AssetName") on success, "ERR:..." on failure.
    static FString CreateBlueprint(FString AssetPath, FString ParentClassName);

    // GET_COMPILE_ERRORS — compiles the Blueprint capturing a
    // FCompilerResultsLog and returns "ERROR:msg|WARNING:msg|..." pipe-
    // delimited, or "CLEAN" if the compile produced no errors or warnings.
    // Distinct from COMPILE (simple pass/fail) — this is the diagnostic
    // version that lets the agent see exactly what to fix.
    static FString GetCompileErrors(FString BlueprintPath);

    static FString SetGameModePawn(FString GameModeBPPath, FString PawnClassPath);
    static FString GetCurrentGameMode();
    static FString GetPlayerStart();
    static FString SetLevelGameMode(FString GameModeBPPath);

    // ------------------------------------------------------------------
    // Function graphs, node positioning, level actor placement (v1.7)
    // ------------------------------------------------------------------

    // CREATE_FUNCTION — creates a new custom function graph in a Blueprint
    // with an auto-generated UK2Node_FunctionEntry. Returns the actual
    // function name used (may differ if uniquified) on success, "ERR:..."
    // on failure.
    static FString CreateFunction(FString BlueprintPath, FString FunctionName);

    // ------------------------------------------------------------------
    // UMG widget + Material graph commands (v1.8)
    // ------------------------------------------------------------------

    // CREATE_WIDGET_BLUEPRINT — creates a new UMG Widget Blueprint (UUserWidget
    // parent) at AssetPath with an empty UCanvasPanel root. Returns the
    // canonical asset path on success, "ERR:..." on failure.
    static FString CreateWidgetBlueprint(FString AssetPath);

    // ADD_WIDGET_ELEMENT — constructs a new widget of ElementType and adds it
    // as a child of the Widget Blueprint's root canvas panel at (X,Y) sized
    // (W,H). Returns the widget's name on success, "ERR:..." on failure.
    static FString AddWidgetElement(FString WidgetBPPath, FString ElementType,
                                    FString Name, int32 X, int32 Y, int32 W, int32 H);

    // SET_WIDGET_TEXT — finds a UTextBlock (or other text-bearing widget) by
    // name in the Widget Blueprint's WidgetTree and sets its default text.
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetWidgetText(FString WidgetBPPath, FString ElementName, FString Text);

    // CREATE_MATERIAL — creates a new Material asset at AssetPath with the
    // given BlendMode (Opaque/Translucent/Masked/Additive). Returns the
    // canonical asset path on success, "ERR:..." on failure.
    static FString CreateMaterial(FString AssetPath, FString BlendModeStr);

    // ADD_MATERIAL_NODE — adds a UMaterialExpression of NodeType to the
    // Material's expression graph at (X,Y). Returns the new node's index
    // (its position in the Material's expression array) on success,
    // "ERR:..." on failure.
    static FString AddMaterialNode(FString MaterialPath, FString NodeType, int32 X, int32 Y);

    // CONNECT_MATERIAL_PINS — connects an output pin of the expression at
    // NodeIndexA to an input pin of the expression at NodeIndexB.
    // OutputPin/InputPin: pin name, or "_" for the first pin (an actual empty
    // string cannot round-trip through the pipe-delimited command parser).
    // Returns empty string on success, "ERR:..." on failure.
    static FString ConnectMaterialPins(FString MaterialPath, int32 NodeIndexA, FString OutputPin,
                                       int32 NodeIndexB, FString InputPin);

    // SET_MATERIAL_RESULT — connects an output pin of the expression at
    // NodeIndex to a final material property (BaseColor, Metallic, Roughness,
    // Normal, Emissive, Opacity, OpacityMask, WorldPositionOffset).
    // OutputPin: pin name, or "_" for the first pin.
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetMaterialResult(FString MaterialPath, FString Channel,
                                     int32 NodeIndex, FString OutputPin);

    // COMPILE_MATERIAL — forces a material recompile via
    // UMaterialEditingLibrary::RecompileMaterial + a synchronous shader
    // compile flush, then reports any compile errors.
    // Returns "CLEAN" or "ERROR:msg|ERROR:msg|..." on success, "ERR:..." if
    // the material could not be found.
    static FString CompileMaterial(FString MaterialPath);

    // ------------------------------------------------------------------
    // Blueprint completeness — enums, structs, function libraries (v1.10)
    // ------------------------------------------------------------------

    // CREATE_ENUM — creates a new UUserDefinedEnum asset with the given
    // comma-separated enumerator display names, via
    // FEnumEditorUtils::CreateUserDefinedEnum + AddNewEnumeratorForUserDefinedEnum
    // (confirmed against Kismet2/EnumEditorUtils.h — the latter takes no name
    // argument; the new enumerator's index is NumEnums()-2 immediately after
    // the call, used with SetEnumeratorDisplayName to assign the real name).
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateEnum(FString AssetPath, FString CommaSeparatedNames);

    // CREATE_STRUCT — creates a new UUserDefinedStruct asset via
    // FStructureEditorUtils::CreateUserDefinedStruct, which already seeds one
    // default bool member internally (confirmed against
    // Kismet2/StructureEditorUtils.cpp) — this is intentional engine behavior,
    // not something this command needs to strip out.
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateStruct(FString AssetPath);

    // ADD_STRUCT_MEMBER — adds a member variable to an existing
    // UUserDefinedStruct via FStructureEditorUtils::AddVariable (which appends
    // to the end of GetVarDesc()), then renames it from its auto-generated
    // name to MemberName via the GUID-based RenameVariable overload.
    // MemberType uses the same names as ResolveTypeString (bool, float,
    // FString, FVector, object:ClassName, etc).
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddStructMember(FString StructAssetPath, FString MemberName, FString MemberType);

    // CREATE_FUNCTION_LIBRARY — creates a new Blueprint Function Library asset.
    // NOTE: this is NOT just CreateBlueprint(AssetPath, "BlueprintFunctionLibrary")
    // — that was tried first and fails live: UBlueprintFunctionLibrary has no
    // IsBlueprintBase metadata, so FKismetEditorUtilities::CanCreateBlueprintOfClass
    // (which CreateBlueprint calls) rejects it. Confirmed against the engine's
    // own UBlueprintFunctionLibraryFactory (Factories/BlueprintFunctionLibraryFactory.h,
    // EditorFactories.cpp): it calls FKismetEditorUtilities::CreateBlueprint
    // directly with BlueprintType = BPTYPE_FunctionLibrary (not BPTYPE_Normal),
    // bypassing CanCreateBlueprintOfClass entirely. This function mirrors that.
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateFunctionLibrary(FString AssetPath);

    // ------------------------------------------------------------------
    // Blueprint completeness Phase 2 — local variables, variable metadata,
    // event dispatchers (v1.11)
    // ------------------------------------------------------------------

    // ADD_LOCAL_VARIABLE — adds a variable scoped to a single function graph
    // via FBlueprintEditorUtils::AddLocalVariable. FunctionGraphName is
    // resolved the same way SPAWN_NODE_IN_GRAPH resolves graphs (see
    // FindGraphByName). VarType uses the same names as ResolveTypeString.
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddLocalVariable(FString BlueprintPath, FString FunctionGraphName,
        FString VarName, FString VarType, FString DefaultValue);

    // SET_VARIABLE_METADATA — sets a single metadata key/value pair on a
    // class (member) variable via FBlueprintEditorUtils::SetBlueprintVariableMetaData.
    // Class variables ONLY — local (function-scoped) variable metadata is
    // silently dropped at compile time by the engine itself (Epic bug
    // UE-239861, open as of this writing), so this deliberately does not
    // support local variables rather than pretending to.
    // SPECIAL CASE: MetaKey "Category" is NOT stored in the generic metadata
    // array — confirmed live it's a dedicated FText field with its own API
    // (FBlueprintEditorUtils::SetBlueprintVariableCategory), routed there
    // automatically rather than silently landing somewhere the Details panel
    // never reads it.
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetVariableMetadata(FString BlueprintPath, FString VarName,
        FString MetaKey, FString MetaValue);

    // CREATE_EVENT_DISPATCHER — creates a new multicast delegate ("Event
    // Dispatcher") member variable plus its signature graph, mirroring
    // FBlueprintEditor::OnAddNewDelegate() exactly (confirmed against
    // Editor/Kismet/Private/BlueprintEditor.cpp — the engine's own "Add New"
    // button in the My Blueprint panel calls this same sequence). Parameters
    // (ParamType:ParamName pairs, comma-separated) are added afterward via
    // the signature graph's entry node — CreateUserDefinedPin with
    // EGPD_Output, since dispatcher parameters flow out of the entry node to
    // whatever binds to the dispatcher.
    // Returns empty string on success, "ERR:..." on failure.
    static FString CreateEventDispatcher(FString BlueprintPath, FString DispatcherName,
        FString CommaSeparatedParams);

    // ------------------------------------------------------------------
    // Function / Custom Event parameters (v1.13)
    // ------------------------------------------------------------------
    // CREATE_FUNCTION_GRAPH produces a parameterless function; these add the
    // signature afterward, using the same CreateUserDefinedPin mechanism
    // CreateEventDispatcher already uses for its signature graph.
    //
    // Direction "in"  -> pins on the function ENTRY node as EGPD_Output
    //                    (inputs flow out of the entry node into the body).
    // Direction "out" -> pins on the function RESULT node as EGPD_Input.
    //                    A void function graph has no result node; one is
    //                    created on demand via
    //                    FBlueprintEditorUtils::FindOrCreateFunctionResultNode.
    //
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddFunctionParam(FString BlueprintPath, FString FunctionGraphName,
        FString Direction, FString CommaSeparatedParams);
    static FString RemoveFunctionParam(FString BlueprintPath, FString FunctionGraphName,
        FString Direction, FString ParamName);

    // A Custom Event is a single node, not a graph, so these take a node id.
    // UK2Node_CustomEvent derives from UK2Node_EditablePinBase, so pins are
    // created directly on the node (GetEntryAndResultNodes does NOT find custom
    // events — it only looks for FunctionEntry/Tunnel nodes within a graph).
    // The event must already be named via SET_CUSTOM_EVENT_NAME.
    static FString AddCustomEventParam(FString BlueprintPath, FString EventNodeId,
        FString CommaSeparatedParams);
    static FString RemoveCustomEventParam(FString BlueprintPath, FString EventNodeId,
        FString ParamName);

    // ------------------------------------------------------------------
    // Animation Blueprint state machines (v1.12)
    // ------------------------------------------------------------------
    // UAnimGraphNode_StateMachine derives from UAnimGraphNode_Base — the
    // same hierarchy the SPAWN_NODE crash-guard rejects outright (see
    // SpawnNodeOnGraph). That guard is correct for the general case
    // (most UAnimGraphNode_Base subclasses need a bespoke setup call the
    // generic template-duplication pipeline never performs), but confirmed
    // against engine source that UAnimGraphNode_StateMachineBase is NOT one
    // of those cases: its EditorStateMachineGraph is populated inside
    // PostPlacedNewNode() itself (Editor/AnimGraph/Private/
    // AnimGraphNode_StateMachineBase.cpp), which IS one of the standard
    // lifecycle hooks a node-construction path calls — unlike
    // UAnimGraphNode_CallFunction's InnerGraph, which only gets set by a
    // separate SetupFromFunction() the generic pipeline never calls. These
    // commands therefore use their OWN dedicated construction path (mirroring
    // the existing K2Node_SpawnActorFromClass manual-construction pattern in
    // SpawnNodeOnGraph) rather than reusing SpawnNode/SpawnNodeOnGraph, so the
    // blanket Anim Graph guard there is never even consulted.

    // CREATE_STATE_MACHINE — spawns a UAnimGraphNode_StateMachine into the
    // named graph (typically "AnimGraph", resolved via the existing
    // FindGraphByName) of an Animation Blueprint. Returns the new node's
    // GUID on success, "ERR:..." on failure.
    static FString CreateStateMachine(FString BlueprintPath, FString GraphName,
        FString StateMachineName, int32 X, int32 Y);

    // ADD_ANIM_STATE — adds a UAnimStateNode into a state machine's inner
    // EditorStateMachineGraph via the confirmed real
    // FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>
    // pathway (Editor/AnimGraph/Public/AnimationStateMachineSchema.h) — unlike
    // the K2 node path, this does NOT duplicate a template object at all
    // (the template is added to the graph directly), so there is no
    // StaticDuplicateObjectEx/PostLoad risk here.
    // Returns the new state node's GUID on success, "ERR:..." on failure.
    static FString AddAnimState(FString BlueprintPath, FString StateMachineNodeGUID,
        FString StateName, int32 X, int32 Y);

    // ADD_ANIM_TRANSITION — connects two existing states with a real
    // UAnimStateTransitionNode. NOTE: UAnimationStateMachineSchema::
    // TryCreateConnection alone (the API this task originally guessed at)
    // only makes a plain pin-to-pin link — it does NOT create the transition
    // node real state machines need. Confirmed against engine source
    // (AnimationStateMachineSchema.cpp, CreateAutomaticConversionNodeAndConnections)
    // that the real mechanism is: spawn a UAnimStateTransitionNode via
    // SpawnNodeFromTemplate, then call its CreateConnections(FromState, ToState)
    // — which takes the two state nodes directly, not pins.
    // Returns the new transition node's GUID on success, "ERR:..." on failure.
    static FString AddAnimTransition(FString BlueprintPath, FString StateMachineNodeGUID,
        FString FromStateGUID, FString ToStateGUID);

    // LIST_ANIM_STATES — lists all UAnimStateNodeBase-derived nodes inside a
    // state machine's EditorStateMachineGraph. Returns comma-separated
    // "GUID~StateName" entries, "ERR:..." on failure.
    static FString ListAnimStates(FString BlueprintPath, FString StateMachineNodeGUID);

    // GET_ANIM_STATE_TRANSITIONS — wraps UAnimStateNodeBase::GetTransitionList().
    // Returns comma-separated "GUID~FromStateName~ToStateName" entries (one
    // per transition connected to the given state), "ERR:..." on failure.
    static FString GetAnimStateTransitions(FString BlueprintPath, FString StateNodeGUID);

    // ------------------------------------------------------------------
    // Anim Graph pin wiring (v1.14) -- the generic GET_NODE_PINS/CONNECT_PINS
    // pair only ever searches UbergraphPages (GetNodePins/FindNodeById) or
    // just UbergraphPages[0] (ConnectPins), and ConnectPins hardcodes a cast
    // to UEdGraphSchema_K2. Neither reaches an Animation Blueprint's AnimGraph
    // (which lives in Blueprint->FunctionGraphs under UAnimationGraphSchema,
    // not UEdGraphSchema_K2) or works with that schema's TryCreateConnection.
    // These two commands fill that gap using the already-existing, broader
    // FindNodeByIdAllGraphs (which does walk FunctionGraphs) plus the
    // node's OWN graph schema (Node->GetGraph()->GetSchema()) instead of a
    // hardcoded K2 cast, so the exact same code path works for both regular
    // K2 pins and Anim Graph pins (e.g. wiring a state machine's Pose output
    // into AnimGraphNode_Root's Result input).
    // ------------------------------------------------------------------

    // GET_ANIM_NODE_PINS -- like GET_NODE_PINS, but finds the node via
    // FindNodeByIdAllGraphs so it also reaches AnimGraph/FunctionGraph nodes
    // (e.g. AnimGraphNode_Root, AnimGraphNode_StateMachine). Returns
    // comma-separated "IN:PinName"/"OUT:PinName" entries, "" if not found.
    static FString GetAnimNodePins(FString BlueprintPath, FString NodeGUID);

    // CONNECT_ANIM_PINS -- like CONNECT_PINS, but resolves both nodes via
    // FindNodeByIdAllGraphs and connects through each node's own graph
    // schema (Node->GetGraph()->GetSchema()->TryCreateConnection) rather
    // than a hardcoded UEdGraphSchema_K2 cast on UbergraphPages[0] -- this is
    // what makes it usable on Anim Graph pins. Returns empty string on
    // success, "ERR:..." on failure.
    static FString ConnectAnimPins(FString BlueprintPath,
                                   FString NodeA, FString PinA,
                                   FString NodeB, FString PinB);

    // LIST_ANIM_GRAPH_NODES -- like LIST_NODES, but for a named FunctionGraphs
    // graph (typically "AnimGraph"), reached via the existing FindGraphByName.
    // Needed because AnimGraph nodes (AnimGraphNode_Root, AnimGraphNode_StateMachine,
    // etc.) never show up in LIST_NODES (which only walks UbergraphPages) and
    // their NodeGuid can't be read from Python (protected property), so this is
    // the only way to discover e.g. AnimGraphNode_Root's GUID to wire something
    // into it. Returns pipe-delimited "GUID~ClassName~NodeTitle" entries,
    // "ERR:..." on failure.
    static FString ListAnimGraphNodes(FString BlueprintPath, FString GraphName);

    // FindNodeAnywhere -- broader than FindNodeByIdAllGraphs: also recurses
    // into every state machine's EditorStateMachineGraph, and from there
    // into each UAnimStateNodeBase's BoundGraph (a state's inner animation
    // graph, e.g. where a BlendSpacePlayer lives) and each
    // UAnimStateTransitionNode's BoundGraph (the transition rule graph).
    // This is what lets GET_ANIM_NODE_PINS/CONNECT_ANIM_PINS/SPAWN_NODE_ANCHORED
    // operate on nodes nested inside a state, not just top-level AnimGraph nodes.

    // LIST_STATE_GRAPH_NODES -- lists nodes (GUID~ClassName~NodeTitle,
    // pipe-delimited) inside a state's or transition's BoundGraph. GUID is
    // the state/transition node's own GUID (from LIST_ANIM_STATES /
    // GET_ANIM_STATE_TRANSITIONS) -- NOT a state machine GUID.
    static FString ListStateGraphNodes(FString BlueprintPath, FString StateOrTransitionGUID);

    // SPAWN_NODE_ANCHORED -- like SPAWN_NODE_IN_GRAPH, but resolves the
    // target graph as "whatever graph AnchorNodeGUID lives in" (via
    // FindNodeAnywhere) instead of by graph name -- this is what makes it
    // possible to spawn an ordinary K2Node (e.g. a VariableGet) inside a
    // state's BoundGraph or a transition's rule graph, neither of which has
    // a name FindGraphByName can resolve. Still runs through the same
    // SpawnNodeOnGraph (so the AnimGraphNode_Base crash-guard still applies
    // -- use CREATE_BLEND_SPACE_PLAYER_ANCHORED for that node type instead).
    static FString SpawnNodeAnchored(FString BlueprintPath, FString AnchorNodeGUID,
                                     FString NodeClass, FString Comment, int32 X, int32 Y);

    // CREATE_BLEND_SPACE_PLAYER_ANCHORED -- bespoke-safe construction of a
    // UAnimGraphNode_BlendSpacePlayer (mirrors CreateStateMachine's manual
    // NewObject+AddNode+AllocateDefaultPins+PostPlacedNewNode pattern --
    // confirmed safe for this class the same way: it's a plain leaf node
    // with no InnerGraph/bespoke-setup dependency the way
    // UAnimGraphNode_CallFunction has, so the generic crash risk the
    // SPAWN_NODE guard warns about does not apply here). Target graph is
    // resolved via FindNodeAnywhere(AnchorNodeGUID), same as SpawnNodeAnchored.
    // Sets the node's BlendSpace asset reference directly. Returns the new
    // node's GUID on success, "ERR:..." on failure.
    static FString CreateBlendSpacePlayerAnchored(FString BlueprintPath, FString AnchorNodeGUID,
                                                  FString BlendSpaceAssetPath, int32 X, int32 Y);

    // SET_TRANSITION_CONDITION -- wires a boolean variable straight into a
    // UAnimStateTransitionNode's rule graph result. The rule BoundGraph
    // already contains an AnimGraphNode_TransitionResult node (created by
    // AddAnimTransition's SpawnNodeFromTemplate, same as a state's
    // AnimGraphNode_StateResult) -- this spawns a K2Node_VariableGet for
    // VarName inside that same BoundGraph (via SpawnNodeAnchored, anchored
    // on the TransitionResult node itself) and connects its output straight
    // into the TransitionResult's boolean input pin.
    // bNegate: if true, inserts a "NOT Boolean" node between the variable
    // and the result (for the reverse-direction transition on the same bool).
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetTransitionCondition(FString BlueprintPath, FString TransitionNodeGUID,
                                          FString VarName, bool bNegate);

    // ADD_ANIM_SLOT_NODE -- inserts a UAnimGraphNode_Slot into an Animation
    // Blueprint's AnimGraph, spliced between whatever is currently feeding
    // AnimGraphNode_Root's "Result" pin and Output Pose itself. NOT built on
    // SpawnNodeOnGraph -- that path explicitly rejects UAnimGraphNode_Base
    // subclasses (see the rejection comment in SpawnNodeOnGraph). Instead
    // mirrors the manual NewObject+AddNode+AllocateDefaultPins+PostPlacedNewNode
    // sequence already proven safe in this file for other AnimGraphNode_Base
    // subclasses (CreateStateMachine, CreateBlendSpacePlayerAnchored).
    // GraphName resolved via the same FindGraphByName lookup ListAnimGraphNodes/
    // GetAnimNodePins already use for AnimGraph pages (Blueprint->FunctionGraphs
    // for an Animation Blueprint, not a bespoke GetAnimationGraphs() call).
    // Confirmed against installed engine source (AnimGraphNode_Base.cpp,
    // AnimNode_Root.h, AnimNode_Slot.h): the new node's output pose pin is
    // named "Pose", its pass-through input is named "Source" (FAnimNode_Slot's
    // FPoseLink member name), and Output Pose's input is named "Result"
    // (FAnimNode_Root's FPoseLink member name) -- pose-link pins are always
    // named after their reflected C++ property.
    // Returns the new Slot node's GUID on success, "ERR:..." on failure.
    static FString AddAnimSlotNode(FString BlueprintPath, FString GraphName,
                                   FString SlotName, int32 X, int32 Y);

    // ------------------------------------------------------------------
    // Niagara — scoped to what's confirmed real in this engine version
    // (v1.12). Does NOT support adding modules/renderers to an emitter or
    // building a VFX system from scratch — no confirmed API exists for
    // that; see GraphBridgeAutomationLibrary.cpp for the investigation.
    // ------------------------------------------------------------------

    // CREATE_NIAGARA_SYSTEM — creates an empty Niagara System asset via the
    // standard UNiagaraSystemFactoryNew (same shape as every other CREATE_
    // command in this file). Returns the canonical asset path on success,
    // "ERR:..." on failure.
    static FString CreateNiagaraSystem(FString AssetPath);

    // CREATE_NIAGARA_EMITTER — creates a Niagara Emitter asset via
    // UNiagaraEmitterFactoryNew, with bAddDefaultModulesAndRenderersToEmptyEmitter
    // = true so the result actually has real modules (EmitterState, SpawnRate,
    // a sprite renderer, etc.) — confirmed against engine source
    // (NiagaraEmitterFactoryNew.cpp) — this is what makes LIST_NIAGARA_MODULES/
    // SET_NIAGARA_MODULE_INPUT usable without a separately-authored template.
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateNiagaraEmitter(FString AssetPath);

    // LIST_NIAGARA_MODULES — lists module names in an emitter's Stack.
    // Confirmed real headless usage pattern for FNiagaraSystemViewModel
    // (constructed outside any open System Editor tab) against
    // Editor/.../Commandlets/NiagaraSystemAuditCommandlet.cpp and
    // Tests/NiagaraEditorTestUtilities.cpp — both construct one directly via
    // MakeShared<FNiagaraSystemViewModel>() + Initialize(), the same pattern
    // used here. Requires an emitter that already has modules (e.g. one
    // created via CREATE_NIAGARA_EMITTER, which adds default modules).
    // NOTE: only supports classic emitters (UNiagaraStackModuleItem) — the
    // newer "Stateless Emitter" system (e.g. the "...Lightweight" templates)
    // uses a parallel class hierarchy (UNiagaraStackStatelessModuleItem) this
    // does not walk. Confirmed live: this matches what CREATE_NIAGARA_EMITTER
    // actually produces (a classic emitter via UNiagaraEmitterFactoryNew), so
    // this isn't a gap for the intended create-then-inspect workflow.
    // Returns comma-separated module names, "ERR:..." on failure.
    static FString ListNiagaraModules(FString SystemAssetPath, FString EmitterName);

    // SET_NIAGARA_MODULE_INPUT — sets a module input's value. NOTE: this
    // task's originally-assumed API (UUpgradeNiagaraScriptResults's
    // SetFloatInput/SetIntInput/etc.) does NOT work outside its designed
    // purpose — confirmed against engine source that those setters only
    // mutate that class's own disconnected internal NewInputs array, never
    // touching a live UNiagaraStackModuleItem. The real, live-connected write
    // path used here instead: UNiagaraClipboardFunctionInput::CreateLocalValue
    // (the same byte-packing technique UUpgradeNiagaraScriptResults uses
    // internally) + UNiagaraStackModuleItem::SetInputValuesFromClipboardFunctionInputs
    // (which actually applies the value to the live module). Supports float,
    // int32, bool, and vec3 inputs (Value format for vec3: "X,Y,Z").
    // Returns empty string on success, "ERR:..." on failure.
    static FString SetNiagaraModuleInput(FString SystemAssetPath, FString EmitterName,
        FString ModuleName, FString InputName, FString Value);

    // ------------------------------------------------------------------
    // Character pipeline — Physics Assets, IK Rig, Montage/BlendSpace,
    // Skeleton sockets (v1.13)
    // ------------------------------------------------------------------

    // CREATE_PHYSICS_ASSET — auto-generates a Physics Asset from a
    // SkeletalMesh. NOTE: does NOT call UPhysicsAssetFactory::
    // CreatePhysicsAssetFromMesh — confirmed live-blocking: that function
    // unconditionally opens an interactive "New Body" options modal dialog
    // (IPhysicsAssetEditorModule::OpenNewBodyDlg) before doing anything,
    // which would hang a bridge command waiting for a user click. Confirmed
    // against its own source that the real body/constraint generation work
    // happens in a separate, non-interactive function it calls internally —
    // FPhysicsAssetUtils::CreateFromSkeletalMesh — which this command calls
    // directly instead, using the same FPhysAssetCreateParams defaults
    // (GetDefault<UPhysicsAssetGenerationSettings>()->CreateParams) the
    // dialog would have pre-populated, then replicating the rest of the
    // factory's post-creation steps (AssetCreated, MarkPackageDirty, and if
    // bSetToMesh: RefreshSkelMeshOnPhysicsAssetChange to update any already-
    // spawned components using this mesh).
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreatePhysicsAsset(FString SkeletalMeshPath, FString AssetPath, bool bSetToMesh);

    // CREATE_IK_RIG — creates a new IK Rig asset via the confirmed
    // real, purpose-built UIKRigDefinitionFactory::CreateNewIKRigAsset()
    // (a dedicated static scripting API, not hand-rolled factory
    // boilerplate). That function does not take a skeletal mesh parameter,
    // so the mesh is set immediately afterward via
    // UIKRigController::GetController(NewRig)->SetSkeletalMesh().
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateIKRig(FString AssetPath, FString SkeletalMeshPath);

    // IK_RIG_AUTO_SETUP — wraps UIKRigController::ApplyAutoFBIK(), which
    // analyzes the skeleton against known templates and auto-generates a
    // full solver/goal/chain setup in one call.
    // Returns empty string on success, "ERR:..." on failure (including when
    // ApplyAutoFBIK() itself returns false — the skeleton didn't match a
    // known template).
    static FString IKRigAutoSetup(FString IKRigAssetPath);

    // ADD_IK_GOAL — wraps UIKRigController::AddNewGoal() + SetGoalBone()
    // (AddNewGoal already takes the bone name directly, so SetGoalBone is
    // only needed if reassigning an existing goal — not required here, but
    // called anyway to explicitly confirm the bone stuck, matching the
    // task's specified shape).
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddIKGoal(FString IKRigAssetPath, FString GoalName, FString BoneName);

    // ADD_RETARGET_CHAIN — wraps UIKRigController::AddRetargetChain(), which
    // already takes ChainName/StartBone/EndBone/GoalName all in one call —
    // confirmed against the header that the separate SetRetargetChainStartBone/
    // EndBone/Goal setters are only needed to modify an EXISTING chain
    // afterward, not for initial creation, so this doesn't call them.
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddRetargetChain(FString IKRigAssetPath, FString ChainName,
        FString StartBone, FString EndBone, FString GoalName);

    // CREATE_IK_RETARGETER — creates a new IK Retargeter asset via
    // UIKRetargetFactory (confirmed real, same NewObject+AssetTools::CreateAsset
    // pattern as CreateNewIKRigAsset). The factory's own FactoryCreateNew
    // does NOT set the source/target IK Rigs despite the factory having a
    // SourceIKRig property (confirmed by reading its .cpp — that property is
    // never referenced) — both are set afterward via
    // UIKRetargeterController::SetIKRig(ERetargetSourceOrTarget::Source/Target, ...).
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateIKRetargeter(FString AssetPath, FString SourceIKRigPath, FString TargetIKRigPath);

    // CREATE_ANIM_MONTAGE — creates a Montage via UAnimMontageFactory.
    // Confirmed against the factory's .cpp that FactoryCreateNew does NOT
    // need ConfigureProperties() (the interactive skeleton-picker step) —
    // setting Factory->TargetSkeleton and/or Factory->SourceAnimation
    // directly before calling FactoryCreateNew is sufficient and
    // non-interactive. AnimSequencePath is optional (empty string skips it,
    // producing an empty Montage on the given skeleton).
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateAnimMontage(FString AssetPath, FString SkeletonPath, FString AnimSequencePath);

    // CREATE_BLEND_SPACE — creates a BlendSpace via UBlendSpaceFactoryNew.
    // Confirmed against EditorFactories.cpp that FactoryCreateNew only
    // requires Factory->TargetSkeleton to be set — non-interactive, same
    // pattern as CreateAnimMontage.
    // Returns the canonical asset path on success, "ERR:..." on failure.
    static FString CreateBlendSpace(FString AssetPath, FString SkeletonPath);

    // ADD_BLEND_SPACE_SAMPLE -- wraps UBlendSpace::AddSample(UAnimSequence*, FVector),
    // the real non-interactive sample-authoring API (confirmed against
    // Engine/Classes/Animation/BlendSpace.h). SampleValue Z is always 0 for a
    // standard 2D blend space. Calls ValidateSampleData() afterward so the
    // triangulation is rebuilt immediately, matching what the editor UI does
    // after adding a sample by hand.
    // Returns the new sample's index (as a string) on success, "ERR:..." on failure.
    static FString AddBlendSpaceSample(FString BlendSpaceAssetPath, FString AnimSequencePath,
                                       float X, float Y);

    // EDIT_BLEND_SPACE_SAMPLE -- wraps UBlendSpace::EditSampleValue(int32, FVector),
    // for repositioning an EXISTING sample (found by its current animation
    // asset reference, since that's the stable identifier a caller has --
    // sample index isn't guaranteed stable across edits) to precise
    // coordinates. Calls ValidateSampleData() afterward, same as AddBlendSpaceSample.
    // Returns empty string on success, "ERR:..." on failure.
    static FString EditBlendSpaceSample(FString BlendSpaceAssetPath, FString AnimSequencePath,
                                        float NewX, float NewY);

    // SET_BLEND_SPACE_PLAYER_ASSET -- retargets an EXISTING
    // UAnimGraphNode_BlendSpacePlayer (found via FindNodeAnywhere, so this
    // reaches state-bound-graph nodes) onto a different BlendSpace asset via
    // the same public SetBlendSpace() setter CreateBlendSpacePlayerAnchored
    // uses. Keeps all existing pin connections (X/Y/Pose) intact -- only the
    // asset reference changes. Returns empty string on success, "ERR:..." on
    // failure.
    static FString SetBlendSpacePlayerAsset(FString BlueprintPath, FString NodeGUID,
                                            FString BlendSpaceAssetPath);

    // GET_ANIM_PIN_CONNECTIONS -- like GET_PIN_CONNECTIONS, but resolves the
    // node via FindNodeAnywhere so it reaches AnimGraph/state/transition
    // nested nodes too. Returns comma-separated "NODEGUID:PinName" entries
    // for everything linked to the given pin (direction-agnostic), "" if
    // the pin exists but has no links, "ERR:..." on failure.
    static FString GetAnimPinConnections(FString BlueprintPath, FString NodeGUID, FString PinName);

    // ADD_SKELETON_SOCKET — NOTE: USkeleton::AddSocket() does not exist —
    // confirmed by searching the actual engine source that no such function
    // is declared anywhere. The real mechanism (confirmed by reading
    // USkeletalMesh::AddSocket()'s implementation, which optionally
    // replicates a socket onto its Skeleton when bAddToSkeleton=true) is a
    // direct, public TArray property: USkeleton::Sockets. This function
    // replicates that same logic directly against the Skeleton asset,
    // without requiring a SkeletalMesh at all: constructs a new
    // USkeletalMeshSocket owned by the Skeleton, validates BoneName exists
    // in the skeleton's reference skeleton, sets RelativeLocation from
    // X/Y/Z, and appends it to Skeleton->Sockets.
    // Returns empty string on success, "ERR:..." on failure.
    static FString AddSkeletonSocket(FString SkeletonPath, FString SocketName, FString BoneName,
        float X, float Y, float Z);

#endif // WITH_EDITOR
};
