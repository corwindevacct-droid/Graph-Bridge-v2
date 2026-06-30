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

// Editor-only forward declarations — placed here (before generated.h) so that
// the private #if WITH_EDITOR methods below compile in editor builds.
// UEdGraphNode* and UBlueprint* only need forward declarations (pointer use only);
// FEdGraphPinType is returned by value and requires its full definition.
#if WITH_EDITOR
#include "EdGraph/EdGraphPin.h"
class UBlueprint;
class UEdGraphNode;
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
    static FString SpawnNode(FString BlueprintPath, FString NodeClass,
                             FString Comment, int32 X, int32 Y);
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

    // RUN_PYTHON — executes arbitrary Python inside UE via IPythonScriptPlugin
    // and returns captured stdout as the payload.
    static FString RunPython(FString Code);

    static FString SetGameModePawn(FString GameModeBPPath, FString PawnClassPath);
    static FString GetCurrentGameMode();
    static FString GetPlayerStart();
    static FString SetLevelGameMode(FString GameModeBPPath);

#endif // WITH_EDITOR
};
