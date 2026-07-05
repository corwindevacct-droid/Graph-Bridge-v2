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

    // Type resolution for SPAWN_VARIABLE
    static FEdGraphPinType ResolveTypeString(const FString& TypeString);

    // Graph mutation verbs
    static FString SpawnEventNode(FString BlueprintPath, FString EventFuncName,
                                  FString Comment, int32 X, int32 Y);
    static FString SpawnNode(FString BlueprintPath, FString NodeClass,
                             FString Comment, int32 X, int32 Y);

    // Shared node-construction core used by both SpawnNode (always targets
    // UbergraphPages[0]) and SpawnNodeInGraph (targets any named graph).
    static FString SpawnNodeOnGraph(UBlueprint* Blueprint, UEdGraph* Graph,
                                    FString NodeClass, FString Comment, int32 X, int32 Y);

    // Finds a graph by name: "EventGraph" (or a UbergraphPages name match)
    // resolves to the main event graph; anything else is matched against
    // Blueprint->FunctionGraphs by GetName(). Returns nullptr if not found.
    static UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName);

    // SPAWN_NODE_IN_GRAPH — like SpawnNode, but targets any named graph
    // (EventGraph or a custom function graph created via CREATE_FUNCTION).
    static FString SpawnNodeInGraph(FString BlueprintPath, FString GraphName,
                                    FString NodeClass, FString Comment, int32 X, int32 Y);

    // Like FindNodeById, but searches UbergraphPages AND FunctionGraphs —
    // used by SET_NODE_POSITION, which must be able to move nodes created
    // via SPAWN_NODE_IN_GRAPH inside a custom function graph.
    static UEdGraphNode* FindNodeByIdAllGraphs(UBlueprint* Blueprint, FString NodeId);

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
    static bool    DisconnectPins(FString BlueprintPath,
                                  FString NodeA, FString PinA,
                                  FString NodeB, FString PinB);
    // Returns empty string on success, error description on failure
    static FString ConnectPins(FString BlueprintPath,
                               FString NodeA, FString PinA,
                               FString NodeB, FString PinB);
    static bool    DeleteNode(FString BlueprintPath, FString NodeId);
    static bool    ClearNodes(FString BlueprintPath, FString CommentMatch);
    static bool    SetPinDefault(FString BlueprintPath,
                                 FString NodeId, FString PinName, FString DefaultValue);
    static FString GetNodePins(FString BlueprintPath, FString NodeName);

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
    static FString ListNodes(FString BlueprintPath);
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
    static FString AddSkeletonSocket(FString AssetPath, FString SocketName, FString BoneName);
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

#endif // WITH_EDITOR
};
