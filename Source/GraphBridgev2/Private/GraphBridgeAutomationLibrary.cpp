// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeAutomationLibrary.h"
#include "GraphBridgev2.h"
#include "GraphBridgeMCPServer.h"

// All UE5 headers MUST come before any third-party Windows headers.
// IXWebSocket pulls in raw Windows atomics (winsock2.h etc.) which clash
// with UE5's FWindowsPlatformAtomics if included too early — causing the
// C2039 '_InterlockedIncrement' errors in IoBuffer.h.
#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"

#if WITH_EDITOR
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_MakeArray.h"
#include "K2Node_InputAction.h"
#include "EnhancedInputActionDelegateBinding.h"
#include "K2Node_VariableGet.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimStateNode.h"
#include "AnimStateNodeBase.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_Root.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraClipboard.h"
#include "NiagaraNodeFunctionCall.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "Factories/PhysicsAssetFactory.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsAssetGenerationSettings.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RigEditor/IKRigController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Animation/AnimSequence.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Variable.h"
#include "Editor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "InputAction.h"
#include "ScopedTransaction.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/BlendSpace.h"
#include "UObject/SavePackage.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/DataTable.h"
#include "DataTableEditorUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
// Character Pipeline (v1.4) — Dynamic cast node
#include "K2Node_DynamicCast.h"
#include "IPythonScriptPlugin.h"
// Get Subsystem node — SET_SUBSYSTEM_CLASS
#include "K2Node_GetSubsystem.h"
#include "Subsystems/Subsystem.h"
// Character Pipeline (v1.4) — Enhanced Input
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "EnhancedInputSubsystems.h"
// Function graphs / node positioning / level actors (v1.7)
#include "InputActionValue.h"
#include "Subsystems/EditorActorSubsystem.h"
// Character Pipeline (v1.4) — Component types
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerStart.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
// Character Pipeline (v1.4) — World iteration and statics
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/SubsystemBlueprintLibrary.h"
// UMG + Material graph commands (v1.8)
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/Border.h"
#include "Components/Slider.h"
#include "Components/EditableText.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionTime.h"
#include "SceneTypes.h"
#include "ShaderCompiler.h"
// Blueprint completeness (v1.10) — enums, structs, function libraries
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Engine/UserDefinedEnum.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#endif // WITH_EDITOR

// ── IXWebSocket (third-party) ────────────────────────────────────────────────
// Wrapped with UE5 platform guards so that Windows atomic macros are restored
// to UE5's versions after the third-party headers are done with them.
// AllowWindowsPlatformAtomics re-enables the raw _Interlocked* macros that
// IXWebSocket headers expect; Hide restores UE5's overrides afterward.
// THIRD_PARTY_INCLUDES_START suppresses warnings-as-errors for non-UE code.
// #undef check prevents UE5's check() assertion macro from mangling any
// internal 'check' identifiers inside IXWebSocket headers.
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
THIRD_PARTY_INCLUDES_START
#pragma push_macro("check")
#undef check
#include "ixwebsocket/IXWebSocket.h"
#include "ixwebsocket/IXWebSocketServer.h"
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
// ─────────────────────────────────────────────────────────────────────────────

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

std::unique_ptr<ix::WebSocketServer> UGraphBridgeAutomationLibrary::Server;
std::unique_ptr<FGraphBridgeMCPServer> UGraphBridgeAutomationLibrary::MCPServer;
FOnSendMessage UGraphBridgeAutomationLibrary::SendMessageDelegate;

// Non-null only during a DispatchCommandSync call — captures the JSON result
// string instead of sending it over the WebSocket wire. Game thread only.
static FString* GSyncResultCapture = nullptr;

// ---------------------------------------------------------------------------
// Server startup
// ---------------------------------------------------------------------------

void UGraphBridgeAutomationLibrary::StartGraphBridgeServer(int32 Port)
{
    if (Server) return;

    // Allow port override via DefaultEditor.ini:
    //   [GraphBridge]
    //   Port=8080
    int32 ConfigPort = 0;
    if (GConfig && GConfig->GetInt(TEXT("GraphBridge"), TEXT("Port"), ConfigPort, GEditorIni) && ConfigPort > 0)
    {
        Port = ConfigPort;
    }

    Server = std::make_unique<ix::WebSocketServer>(Port);

    Server->setOnClientMessageCallback(
        [](std::shared_ptr<ix::ConnectionState> ConnectionState,
           ix::WebSocket& WebSocket,
           const ix::WebSocketMessagePtr& Msg)
        {
            if (Msg->type == ix::WebSocketMessageType::Message)
            {
                FString Received = FString(UTF8_TO_TCHAR(Msg->str.c_str()));

                // IXWebSocket delivers on its own thread — marshal to GameThread
                // before touching any UObjects.
                ix::WebSocket* SenderPtr = &WebSocket;
                AsyncTask(ENamedThreads::GameThread, [Received, SenderPtr]()
                {
                    ExecuteAtomicCommand(Received, SenderPtr);
                });
            }
        });

    auto Result = Server->listen();
    if (!Result.first)
    {
        UE_LOG(LogGraphBridge, Error, TEXT("GraphBridge: Failed to bind on port %d: %s"),
               Port, UTF8_TO_TCHAR(Result.second.c_str()));
        Server.reset();
        return;
    }

    Server->start();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: WebSocket server started on port %d"), Port);
}

// ---------------------------------------------------------------------------
// Server stop
// ---------------------------------------------------------------------------

void UGraphBridgeAutomationLibrary::StopGraphBridgeServer()
{
    if (!Server) return;
    Server->stop();
    Server.reset();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: WebSocket server stopped"));
}

bool UGraphBridgeAutomationLibrary::IsServerRunning()
{
    return Server != nullptr;
}

// ---------------------------------------------------------------------------
// MCP server startup / shutdown — independent of the WebSocket server above.
// ---------------------------------------------------------------------------

bool UGraphBridgeAutomationLibrary::StartMCPServer(int32 Port)
{
    if (MCPServer && MCPServer->IsRunning()) return true;

    // Allow port override via DefaultEditor.ini:
    //   [GraphBridge]
    //   MCPPort=8090
    int32 ConfigPort = 0;
    if (GConfig && GConfig->GetInt(TEXT("GraphBridge"), TEXT("MCPPort"), ConfigPort, GEditorIni) && ConfigPort > 0)
    {
        Port = ConfigPort;
    }

    if (!MCPServer)
        MCPServer = std::make_unique<FGraphBridgeMCPServer>();

    if (!MCPServer->Start(Port))
    {
        MCPServer.reset();
        return false;
    }
    return true;
}

void UGraphBridgeAutomationLibrary::StopMCPServer()
{
    if (!MCPServer) return;
    MCPServer->Stop();
    MCPServer.reset();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: MCP server stopped"));
}

bool UGraphBridgeAutomationLibrary::IsMCPServerRunning()
{
    return MCPServer && MCPServer->IsRunning();
}

// ---------------------------------------------------------------------------
// Response helper
// ---------------------------------------------------------------------------

void UGraphBridgeAutomationLibrary::SendResponse(ix::WebSocket* Sender, bool bSuccess,
    FString Command, FString Message, FString Payload)
{
    auto Escape = [](FString& S)
    {
        S.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
        S.ReplaceInline(TEXT("\""), TEXT("\\\""));
        S.ReplaceInline(TEXT("\n"), TEXT("\\n"));
        S.ReplaceInline(TEXT("\r"), TEXT("\\r"));
        S.ReplaceInline(TEXT("\t"), TEXT("\\t"));
    };
    Escape(Command);
    Escape(Message);
    Escape(Payload);

    FString Json = FString::Printf(
        TEXT("{\"success\":%s,\"command\":\"%s\",\"message\":\"%s\",\"payload\":\"%s\"}"),
        bSuccess ? TEXT("true") : TEXT("false"),
        *Command, *Message, *Payload);

    // Sync capture path — used by DispatchCommandSync / the LLM agentic loop
    if (GSyncResultCapture)
    {
        *GSyncResultCapture = Json;
        return;
    }

    if (!Sender) return;
    std::string JsonStr(TCHAR_TO_UTF8(*Json));
    Sender->send(JsonStr);
}

// ---------------------------------------------------------------------------
// Synchronous in-process dispatch (for LLM tool calls)
// ---------------------------------------------------------------------------

FString UGraphBridgeAutomationLibrary::DispatchCommandSync(const FString& Command)
{
    // GSyncResultCapture is a non-reentrant global — this function must only
    // ever be called on the game thread, never concurrently.
    check(IsInGameThread());
    FString Result;
    GSyncResultCapture = &Result;
    ExecuteAtomicCommand(Command, nullptr);
    GSyncResultCapture = nullptr;
    return Result;
}

// ---------------------------------------------------------------------------
// Command router
// ---------------------------------------------------------------------------

void UGraphBridgeAutomationLibrary::ExecuteAtomicCommand(FString Command, ix::WebSocket* Sender)
{
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge received: %s"), *Command);

    TArray<FString> P;
    Command.ParseIntoArray(P, TEXT("|"));
    if (P.Num() == 0)
    {
        SendResponse(Sender, false, TEXT(""), TEXT("Empty command"), TEXT(""));
        return;
    }

    const FString Op = P[0].TrimStartAndEnd();

#if WITH_EDITOR

    if (Op == TEXT("SPAWN_NODE") && P.Num() >= 6)
    {
        // Trailing GraphName is optional — SPAWN_NODE|BPPath|NodeClass|Comment|X|Y|GraphName
        FString GraphName = P.Num() >= 7 ? P[6] : TEXT("");
        FString Result = SpawnNode(P[1], P[2], P[3],
            FCString::Atoi(*P[4]), FCString::Atoi(*P[5]), GraphName);
        bool bOk = !Result.IsEmpty() && !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Node spawned: %s"), *Result)
                : (Result.IsEmpty() ? TEXT("Spawn failed") : Result.RightChop(4)),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SPAWN_EVENT_NODE") && P.Num() >= 6)
    {
        // SPAWN_EVENT_NODE|BPPath|EventFuncName|Comment|X|Y
        // EventFuncName: internal UFunction name e.g.
        //   ReceiveAnyDamage, ReceiveBeginPlay, ReceiveHit,
        //   ReceivePointDamage, ReceiveRadialDamage, ReceiveTick
        FString Result = SpawnEventNode(P[1], P[2], P[3],
            FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
        bool bOk = !Result.IsEmpty() && !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Event node spawned: %s"), *Result)
                : (Result.IsEmpty() ? TEXT("Spawn failed") : Result.RightChop(4)),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CONNECT_PINS") && P.Num() >= 6)
    {
        // Trailing GraphName is optional — CONNECT_PINS|...|SourcePin|TargetNode|TargetPin|GraphName
        FString GraphName = P.Num() >= 7 ? P[6] : TEXT("");
        FString Err = ConnectPins(P[1], P[2], P[3], P[4], P[5], GraphName);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pins connected") : Err, TEXT(""));
    }
    else if (Op == TEXT("DISCONNECT_PINS") && P.Num() >= 6)
    {
        FString GraphName = P.Num() >= 7 ? P[6] : TEXT("");
        bool bOk = DisconnectPins(P[1], P[2], P[3], P[4], P[5], GraphName);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pins disconnected") : TEXT("Disconnect failed"), TEXT(""));
    }
    else if (Op == TEXT("DELETE_NODE") && P.Num() >= 3)
    {
        // Trailing GraphName is optional — DELETE_NODE|BPPath|NodeId|GraphName
        FString GraphName = P.Num() >= 4 ? P[3] : TEXT("");
        bool bOk = DeleteNode(P[1], P[2], GraphName);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Node deleted") : TEXT("Delete failed"), TEXT(""));
    }
    else if (Op == TEXT("CLEAR_NODES") && P.Num() >= 3)
    {
        // Trailing GraphName is optional — CLEAR_NODES|BPPath|CommentMatch|GraphName
        FString GraphName = P.Num() >= 4 ? P[3] : TEXT("");
        bool bOk = ClearNodes(P[1], P[2], GraphName);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Nodes cleared") : TEXT("Clear failed"), TEXT(""));
    }
    else if (Op == TEXT("SET_PIN_DEFAULT") && P.Num() >= 5)
    {
        bool bOk = SetPinDefault(P[1], P[2], P[3], P[4]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Default set") : TEXT("Set default failed"), TEXT(""));
    }
    else if (Op == TEXT("ADD_ARRAY_PIN") && P.Num() >= 3)
    {
        // ADD_ARRAY_PIN|BPPath|NodeGUID
        // K2Node_MakeArray only ever spawns with a single wildcard input pin
        // [0] — the "+" button that adds more in the real editor calls
        // AddInputPin(), which no existing opcode exposed. Needed because
        // Array_Add's dual-wildcard (TargetArray + NewItem, mutually type-
        // dependent) pins do not reliably retype even when both are wired
        // to concrete sources — confirmed live: building a 2+ element
        // literal array via MakeArray's own pins is the robust path.
        FString Err = AddArrayPin(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Array pin added") : Err, TEXT(""));
    }
    else if (Op == TEXT("GET_NODE_PINS") && P.Num() >= 3)
    {
        // Trailing GraphName is optional — GET_NODE_PINS|BPPath|NodeId|GraphName
        FString GraphName = P.Num() >= 4 ? P[3] : TEXT("");
        FString Pins = GetNodePins(P[1], P[2], GraphName);
        bool bOk = !Pins.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pins found") : TEXT("Node not found"), Pins);
    }
    else if (Op == TEXT("GET_PIN_CONNECTIONS") && P.Num() >= 4)
    {
        // GET_PIN_CONNECTIONS|BPPath|NodeGUID|PinName
        // Returns comma-separated NODEGUID:PinName entries for everything this
        // pin is linked to. Empty payload (still success) = no connections.
        FString Result = GetPinConnections(P[1], P[2], P[3]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? (Result.IsEmpty() ? TEXT("No connections") : TEXT("Connections found")) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_PIN_DEFAULT") && P.Num() >= 4)
    {
        // GET_PIN_DEFAULT|BPPath|NodeGUID|PinName
        // Empty payload (still success) = pin has no default value set.
        FString Result = GetPinDefault(P[1], P[2], P[3]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? (Result.IsEmpty() ? TEXT("No default set") : TEXT("Default retrieved")) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_ANIM_CLASS") && P.Num() >= 4)
    {
        // SET_ANIM_CLASS|BPPath|ComponentName|AnimBPPath
        FString Err = SetAnimClass(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("AnimClass set and blueprint recompiled") : Err.RightChop(4), TEXT(""));
    }

    else if (Op == TEXT("COMPILE") && P.Num() >= 2)
    {
        bool bOk = CompileBlueprint(P[1]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Compiled") : TEXT("Compile failed"), TEXT(""));
    }
    else if (Op == TEXT("SAVE_BLUEPRINT") && P.Num() >= 2)
    {
        bool bOk = SaveBlueprint(P[1]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Saved") : TEXT("Save failed"), TEXT(""));
    }
    else if (Op == TEXT("SPAWN_VARIABLE") && P.Num() >= 4)
    {
        FString Category = P.Num() >= 5 ? P[4] : TEXT("Default");
        FString Guid = SpawnVariable(P[1], P[2], P[3], Category);
        bool bOk = !Guid.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Variable '%s' created"), *P[2]) : TEXT("Variable spawn failed"),
            Guid);
    }
    else if (Op == TEXT("ADD_COMPONENT") && P.Num() >= 3)
    {
        // ADD_COMPONENT|BPPath|ComponentClass|ComponentName|ParentComponentName(optional)
        // ComponentClass: C++ class name e.g. "ProjectileMovementComponent"
        //                 or full Blueprint path e.g. "/Game/BP_MyComp.BP_MyComp"
        FString CompName   = P.Num() > 3 ? P[3] : P[2];
        FString ParentName = P.Num() > 4 ? P[4] : TEXT("");
        FString Err = AddComponent(P[1], P[2], CompName, ParentName);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Component added: %s"), *CompName) : Err,
            bOk ? CompName : TEXT(""));
    }

    else if (Op == TEXT("SET_VARIABLE_DEFAULT") && P.Num() >= 4)
    {
        bool bOk = SetVariableDefault(P[1], P[2], P[3]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Default set — remember to COMPILE") : TEXT("Set default failed"), TEXT(""));
    }
    else if (Op == TEXT("LIST_NODES") && P.Num() >= 2)
    {
        // LIST_NODES|BPPath|GraphName(optional)
        // Returns: GUID:Title:Comment|GUID:Title:Comment|...
        FString GraphName = P.Num() >= 3 ? P[2] : TEXT("");
        FString Results = ListNodes(P[1], GraphName);
        bool bOk = !Results.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Nodes listed") : TEXT("No nodes or BP not found"), Results);
    }

    else if (Op == TEXT("FIND_NODE_CLASS") && P.Num() >= 2)
    {
        FString Results = FindNodeClass(P[1]);
        bool bOk = !Results.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Matches found") : TEXT("No matches"), Results);
    }
    else if (Op == TEXT("LIST_ASSETS"))
    {
        // Filter is optional — LIST_ASSETS with no arg returns all assets
        FString Filter = P.Num() >= 2 ? P[1] : TEXT("");
        FString Results = ListAssets(Filter);
        bool bOk = !Results.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Assets found") : TEXT("No assets"), Results);
    }
    else if (Op == TEXT("SET_INPUT_ACTION") && P.Num() >= 4)
    {
        bool bOk = SetInputAction(P[1], P[2], P[3]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Input action set") : TEXT("Set input action failed"), TEXT(""));
    }

    else if (Op == TEXT("SET_FUNCTION_REF") && P.Num() >= 5)
    {
        // SET_FUNCTION_REF|BPPath|NodeId|ClassName|FunctionName
        FString Err = SetFunctionRef(P[1], P[2], P[3], P[4]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Function reference set") : Err, TEXT(""));
    }

    else if (Op == TEXT("SET_EVENT_REF") && P.Num() >= 4)
    {
        // SET_EVENT_REF|BPPath|NodeId|FunctionName
        // Binds a K2Node_Event to a named function on the parent class chain.
        // e.g. SET_EVENT_REF|/Game/BP_X.BP_X|<guid>|ReceiveBeginPlay
        FString Err = SetEventRef(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Event reference set") : Err, TEXT(""));
    }

    else if (Op == TEXT("SET_CUSTOM_EVENT_NAME") && P.Num() >= 4)
    {
        // SET_CUSTOM_EVENT_NAME|BPPath|NodeId|EventName
        // K2Node_CustomEvent nodes spawn with CustomFunctionName "None" and
        // no opcode could name them — SET_EVENT_REF requires binding to an
        // EXISTING function on the parent class chain, which a user-defined
        // custom event by definition isn't. Needed for anything that must
        // call a Latent node (e.g. K2Node_PlayMontage) from outside this
        // Blueprint's own graph: latent nodes are illegal inside plain
        // CREATE_FUNCTION function graphs (confirmed live: "Event node ...
        // registers net ... in a non-event graph"), so the callable entry
        // point has to be a named Custom Event living in the main EventGraph
        // instead — this command is what makes that name callable.
        FString Err = SetCustomEventName(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Custom event named") : Err, TEXT(""));
    }

    else if (Op == TEXT("SET_VARIABLE_REF") && P.Num() >= 4)
    {
        // SET_VARIABLE_REF|BPPath|NodeId|VarName
        // Returns the available variable list in the message on failure
        FString VarRefErr;
        bool bOk = SetVariableRef(P[1], P[2], P[3], VarRefErr);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Variable reference set") : VarRefErr, TEXT(""));
    }

    else if (Op == TEXT("SET_EXTERNAL_VARIABLE_REF") && P.Num() >= 5)
    {
        // SET_EXTERNAL_VARIABLE_REF|BPPath|NodeId|OwnerClassName|VarName
        // Like SET_VARIABLE_REF, but binds a K2Node_Variable to a property on
        // an ARBITRARY class (e.g. TargetArmLength on SpringArmComponent),
        // not just this Blueprint's own class hierarchy. SET_VARIABLE_REF
        // always creates a self-context reference, which cannot express the
        // common "get a component reference, then set/get a property on it"
        // pattern (e.g. wiring a Tick-driven camera zoom into
        // SpringArmComponent::TargetArmLength) since that property does not
        // live on the Character's own class. The resulting node exposes a
        // "Target" self pin that must be wired (via CONNECT_PINS) to the
        // component reference (e.g. a Get CameraBoom node's output).
        FString Err = SetExternalVariableRef(P[1], P[2], P[3], P[4]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("External variable reference set") : Err, TEXT(""));
    }

    else if (Op == TEXT("CLOSE_BLUEPRINT") && P.Num() >= 2)
    {
        // CLOSE_BLUEPRINT|BPPath
        // Close the Blueprint Editor for this asset so the SCS preview viewport
        // is not ticking when we spawn nodes. Always call this before SPAWN_NODE.
        UBlueprint* BP = GetBlueprintByPath(P[1]);
        bool bOk = false;
        if (BP)
        {
            if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            {
                Sub->CloseAllEditorsForAsset(BP);
                bOk = true;
            }
        }
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Blueprint editor closed") : TEXT("Close failed"), TEXT(""));
    }

    else if (Op == TEXT("OPEN_BLUEPRINT") && P.Num() >= 2)
    {
        // OPEN_BLUEPRINT|BPPath
        // Reopen the Blueprint Editor after node spawning is complete.
        UBlueprint* BP = GetBlueprintByPath(P[1]);
        bool bOk = false;
        if (BP)
        {
            if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            {
                Sub->OpenEditorForAsset(BP);
                bOk = true;
            }
        }
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Blueprint editor opened") : TEXT("Open failed"), TEXT(""));
    }
    else if (Op == TEXT("LIST_BLENDSPACES"))
    {
        // LIST_BLENDSPACES           — all blendspaces in the project
        // LIST_BLENDSPACES|/Game/Foo — filtered by path
        FString BSFilter = P.Num() >= 2 ? P[1] : TEXT("");
        FString Results  = ListBlendSpaces(BSFilter);
        bool bOk = !Results.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("BlendSpaces found") : TEXT("No BlendSpaces found"), Results);
    }
    else if (Op == TEXT("LIST_ASSET_PROPERTIES") && P.Num() >= 2)
    {
        // LIST_ASSET_PROPERTIES|AssetPath
        // Returns all editable UPROPERTY names, types and current values.
        FString Result = ListAssetProperties(P[1]);
        bool bOk = !Result.IsEmpty() && !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Properties listed") : (Result.IsEmpty() ? TEXT("Asset not found") : Result.RightChop(4)),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_ASSET_PROPERTY") && P.Num() >= 3)
    {
        // GET_ASSET_PROPERTY|AssetPath|PropertyName
        FString Result = GetAssetProperty(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Property retrieved") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_ASSET_PROPERTY") && P.Num() >= 4)
    {
        // SET_ASSET_PROPERTY|AssetPath|PropertyName|Value
        // Value may itself contain pipes — rejoin everything from index 3 onward
        FString Value = P[3];
        for (int32 i = 4; i < P.Num(); ++i)
            Value += TEXT("|") + P[i];
        FString Err = SetAssetProperty(P[1], P[2], Value);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Property set") : Err, TEXT(""));
    }
    // ------------------------------------------------------------------
    // AnimMontage commands  (v1.1)
    // ------------------------------------------------------------------
    else if (Op == TEXT("GET_MONTAGE_INFO") && P.Num() >= 2)
    {
        // GET_MONTAGE_INFO|AssetPath
        // Returns JSON with sections, slots and notifies.
        FString Result = GetMontageInfo(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Montage info retrieved") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_MONTAGE_SECTION") && P.Num() >= 4)
    {
        // ADD_MONTAGE_SECTION|AssetPath|SectionName|StartTimeSeconds
        FString Err = AddMontageSection(P[1], P[2], FCString::Atof(*P[3]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Section added") : Err, TEXT(""));
    }
    else if (Op == TEXT("REMOVE_MONTAGE_SECTION") && P.Num() >= 3)
    {
        // REMOVE_MONTAGE_SECTION|AssetPath|SectionName
        FString Err = RemoveMontageSection(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Section removed") : Err, TEXT(""));
    }
    else if (Op == TEXT("SET_MONTAGE_SLOT") && P.Num() >= 4)
    {
        // SET_MONTAGE_SLOT|AssetPath|SlotIndex|NewSlotName
        // SlotName format:  GroupName.SlotName  e.g. DefaultGroup.UpperBody
        FString Err = SetMontageSlot(P[1], FCString::Atoi(*P[2]), P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Slot renamed") : Err, TEXT(""));
    }
    else if (Op == TEXT("ADD_MONTAGE_NOTIFY") && P.Num() >= 4)
    {
        // ADD_MONTAGE_NOTIFY|AssetPath|NotifyClass|TimeSeconds
        // NotifyClass: short name e.g. "AnimNotify_PlaySound", or full path
        FString Err = AddMontageNotify(P[1], P[2], FCString::Atof(*P[3]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Notify added") : Err, TEXT(""));
    }
    else if (Op == TEXT("ADD_MONTAGE_NOTIFY_STATE") && P.Num() >= 5)
    {
        // ADD_MONTAGE_NOTIFY_STATE|AssetPath|NotifyStateClass|StartSeconds|DurationSeconds
        // For begin/end windows (e.g. weapon hitbox active frames). A plain
        // single-frame notify goes through ADD_MONTAGE_NOTIFY instead.
        FString Err = AddMontageNotifyState(
            P[1], P[2], FCString::Atof(*P[3]), FCString::Atof(*P[4]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Notify state added") : Err, TEXT(""));
    }
    else if (Op == TEXT("REMOVE_MONTAGE_NOTIFY") && P.Num() >= 3)
    {
        // REMOVE_MONTAGE_NOTIFY|AssetPath|NotifyIndex (works for both kinds)
        FString Err = RemoveMontageNotify(P[1], FCString::Atoi(*P[2]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Notify removed") : Err, TEXT(""));
    }
    // ------------------------------------------------------------------
    // DataTable commands (v1.2)
    // ------------------------------------------------------------------
    else if (Op == TEXT("LIST_DATATABLE_ROWS") && P.Num() >= 2)
    {
        // LIST_DATATABLE_ROWS|AssetPath
        FString Result = ListDataTableRows(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Rows listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_DATATABLE_ROW") && P.Num() >= 3)
    {
        // ADD_DATATABLE_ROW|AssetPath|RowName
        // Adds an empty (default-value) row. Use SET_ASSET_PROPERTY afterward
        // to fill individual fields via generic reflection.
        FString Err = AddDataTableRow(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Row added") : Err, TEXT(""));
    }
    else if (Op == TEXT("DELETE_DATATABLE_ROW") && P.Num() >= 3)
    {
        // DELETE_DATATABLE_ROW|AssetPath|RowName
        FString Err = DeleteDataTableRow(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Row deleted") : Err, TEXT(""));
    }
    else if (Op == TEXT("RENAME_DATATABLE_ROW") && P.Num() >= 4)
    {
        // RENAME_DATATABLE_ROW|AssetPath|OldRowName|NewRowName
        FString Err = RenameDataTableRow(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Row renamed") : Err, TEXT(""));
    }
    // ------------------------------------------------------------------
    // Skeleton socket commands (v1.3)
    // ------------------------------------------------------------------
    else if (Op == TEXT("LIST_SKELETON_SOCKETS") && P.Num() >= 2)
    {
        // LIST_SKELETON_SOCKETS|SkeletonAssetPath
        FString Result = ListSkeletonSockets(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Sockets listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("MOVE_SKELETON_SOCKET") && P.Num() >= 9)
    {
        // MOVE_SKELETON_SOCKET|AssetPath|SocketName|LocX|LocY|LocZ|RotP|RotY|RotR
        // All transform values in centimetres / degrees (UE native units)
        FVector Loc(FCString::Atof(*P[3]), FCString::Atof(*P[4]), FCString::Atof(*P[5]));
        FRotator Rot(FCString::Atof(*P[6]), FCString::Atof(*P[7]), FCString::Atof(*P[8]));
        FString Err = MoveSkeletonSocket(P[1], P[2], Loc, Rot);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Socket moved") : Err, TEXT(""));
    }
    else if (Op == TEXT("DELETE_SKELETON_SOCKET") && P.Num() >= 3)
    {
        // DELETE_SKELETON_SOCKET|SkeletonAssetPath|SocketName
        FString Err = DeleteSkeletonSocket(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Socket deleted") : Err, TEXT(""));
    }
    // ------------------------------------------------------------------
    // Character Pipeline commands (v1.4)
    // ------------------------------------------------------------------
    else if (Op == TEXT("CREATE_IMC") && P.Num() >= 2)
    {
        // CREATE_IMC|AssetPath
        // Creates a new UInputMappingContext asset and saves it to disk.
        // Example: CREATE_IMC|/Game/Input/IMC_Default
        FString Err = CreateIMC(P[1]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("IMC created at '%s'"), *P[1]) : Err, TEXT(""));
    }
    else if (Op == TEXT("ADD_IMC_MAPPING") && P.Num() >= 4)
    {
        // ADD_IMC_MAPPING|IMCPath|ActionPath|KeyName|ModifierClasses(optional)
        // ModifierClasses: comma-separated short class names e.g. "InputModifierNegate"
        // KeyName: exact FKey name e.g. W, SpaceBar, Gamepad_LeftX
        FString Mods = P.Num() >= 5 ? P[4] : TEXT("");
        FString Err = AddIMCMapping(P[1], P[2], P[3], Mods);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Mapping added: %s -> %s"), *P[3], *P[2]) : Err, TEXT(""));
    }
    else if (Op == TEXT("REMOVE_IMC_MAPPING") && P.Num() >= 4)
    {
        // REMOVE_IMC_MAPPING|IMCPath|ActionPath|KeyName
        FString Err = RemoveIMCMapping(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Mapping removed: %s -> %s"), *P[3], *P[2]) : Err, TEXT(""));
    }
    else if (Op == TEXT("LIST_IMC_MAPPINGS") && P.Num() >= 2)
    {
        // LIST_IMC_MAPPINGS|IMCPath
        // Returns JSON array of all key-to-action mappings in the IMC.
        FString Result = ListIMCMappings(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Mappings listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SAVE_ASSET") && P.Num() >= 2)
    {
        // SAVE_ASSET|AssetPath
        // Saves any UObject asset to disk. Works on IMC, DataTable, Skeleton, etc.
        bool bOk = SaveAsset(P[1]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Asset saved") : TEXT("Save failed — asset may not exist or path is wrong"), TEXT(""));
    }
    else if (Op == TEXT("SET_CHARACTER_MESH") && P.Num() >= 3)
    {
        // SET_CHARACTER_MESH|BPPath|MeshPath|ComponentName(optional)
        // ComponentName defaults to "CharacterMesh0" (ACharacter inherited mesh).
        FString CompName = P.Num() >= 4 ? P[3] : TEXT("");
        FString Err = SetCharacterMesh(P[1], P[2], CompName);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Skeletal mesh set and Blueprint recompiled") : Err, TEXT(""));
    }
    else if (Op == TEXT("SET_CHARACTER_CAPSULE") && P.Num() >= 4)
    {
        // SET_CHARACTER_CAPSULE|BPPath|HalfHeight|Radius|ComponentName(optional)
        // HalfHeight and Radius in centimetres (UE native units).
        FString CompName = P.Num() >= 5 ? P[4] : TEXT("");
        FString Err = SetCharacterCapsule(
            P[1], FCString::Atof(*P[2]), FCString::Atof(*P[3]), CompName);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Capsule dimensions set and Blueprint recompiled") : Err, TEXT(""));
    }
    else if (Op == TEXT("SET_CAMERA_BOOM") && P.Num() >= 6)
    {
        // SET_CAMERA_BOOM|BPPath|ArmLength|OffX|OffY|OffZ|ComponentName(optional)
        // ArmLength in cm; SocketOffset in cm (X, Y, Z).
        FVector Offset(FCString::Atof(*P[3]), FCString::Atof(*P[4]), FCString::Atof(*P[5]));
        FString CompName = P.Num() >= 7 ? P[6] : TEXT("");
        FString Err = SetCameraBoom(P[1], FCString::Atof(*P[2]), Offset, CompName);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Camera boom configured and Blueprint recompiled") : Err, TEXT(""));
    }
    else if (Op == TEXT("ADD_IMC_TO_CHARACTER") && P.Num() >= 3)
    {
        // ADD_IMC_TO_CHARACTER|BPPath|IMCPath|Priority(optional, default 0)
        // Spawns GetPlayerController→GetLocalPlayerSubsystem→AddMappingContext
        // in BeginPlay and wires the IMC asset to the MappingContext pin.
        // Returns GUID of the AddMappingContext node on success.
        int32 Priority = P.Num() >= 4 ? FCString::Atoi(*P[3]) : 0;
        FString Result = AddIMCToCharacter(P[1], P[2], Priority);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(
                TEXT("IMC wired in BeginPlay — AddMappingContext GUID: %s"), *Result)
                : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_GAMEMODE_PAWN") && P.Num() >= 3)
    {
        // SET_GAMEMODE_PAWN|GameModeBPPath|PawnClassPath
        // Sets DefaultPawnClass on the GameMode Blueprint's CDO and recompiles.
        FString Err = SetGameModePawn(P[1], P[2]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("DefaultPawnClass set — Blueprint recompiled") : Err, TEXT(""));
    }
    else if (Op == TEXT("GET_CURRENT_GAMEMODE"))
    {
        // GET_CURRENT_GAMEMODE
        // Returns JSON: {"world":"<LevelName>","gameMode":"<ClassPath>"}
        FString Result = GetCurrentGameMode();
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("GameMode retrieved") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_PLAYER_START"))
    {
        // GET_PLAYER_START
        // Returns JSON array of PlayerStart actors in the current editor level.
        FString Result = GetPlayerStart();
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Player starts listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_LEVEL_GAMEMODE") && P.Num() >= 2)
    {
        // SET_LEVEL_GAMEMODE|GameModeBPPath
        // Sets AWorldSettings::DefaultGameMode for the current editor level.
        // Save the level afterward to persist (File > Save Current Level).
        FString Err = SetLevelGameMode(P[1]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Level GameMode set — save the level to persist the change") : Err, TEXT(""));
    }
    else if (Op == TEXT("SET_CAST_TARGET") && P.Num() >= 4)
    {
        // SET_CAST_TARGET|BlueprintPath|NodeGUID|TargetClassName
        // Sets TargetType on a UK2Node_DynamicCast and rebuilds its pins.
        // TargetClassName: short name e.g. "ACharacter" or full Blueprint path
        // e.g. "/Game/Characters/BP_Hero.BP_Hero_C"
        FString Err = SetCastTarget(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Cast target set to '%s' — pins rebuilt"), *P[3]) : Err,
            TEXT(""));
    }
    else if (Op == TEXT("SET_SUBSYSTEM_CLASS") && P.Num() >= 4)
    {
        // SET_SUBSYSTEM_CLASS|BlueprintPath|NodeGUID|SubsystemClassName
        // Sets CustomClass on a UK2Node_GetSubsystem ("Get Local Player/World/
        // GameInstance/Engine/Editor Subsystem" nodes) and rebuilds its pins so
        // ReturnValue is typed as the requested subsystem class.
        FString Err = SetSubsystemClass(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Subsystem class set to '%s' — pins rebuilt"), *P[3]) : Err,
            TEXT(""));
    }
    else if (Op == TEXT("ADD_VARIABLE") && P.Num() >= 4)
    {
        // ADD_VARIABLE|BPPath|VarName|VarType|Category(optional)
        // VarType uses same names as SPAWN_VARIABLE e.g. "bool", "int32", "object:ACharacter"
        // Returns the actual variable name used (may differ if name was uniquified).
        FString Category = P.Num() > 4 ? P[4] : TEXT("");
        FString Result = AddVariable(P[1], P[2], P[3], Category);
        bool bOk = !Result.IsEmpty() && !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Variable '%s' created"), *Result)
                : (Result.IsEmpty() ? TEXT("Add variable failed") : Result.RightChop(4)),
            bOk ? Result : TEXT(""));
    }
    else
    {
        // See ExecuteAtomicCommandExtended's declaration comment: this chain
        // hit MSVC's C1061 nesting-depth limit, split here purely to shorten
        // it -- no logic moved, just relocated.
        ExecuteAtomicCommandExtended(Command, Op, P, Sender);
    }

#else
    SendResponse(Sender, false, Op, TEXT("GraphBridge commands require an editor build"), TEXT(""));
#endif
}

// ---------------------------------------------------------------------------
// Command router, part 2 — see ExecuteAtomicCommandExtended's header comment.
// ---------------------------------------------------------------------------
void UGraphBridgeAutomationLibrary::ExecuteAtomicCommandExtended(const FString& Command, const FString& Op,
    const TArray<FString>& P, ix::WebSocket* Sender)
{
#if WITH_EDITOR
    if (Op == TEXT("SET_VARIABLE_TYPE") && P.Num() >= 4)
    {
        // SET_VARIABLE_TYPE|BPPath|VarName|NewType
        // Retypes an existing Blueprint member variable.
        FString Err = SetVariableType(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Variable '%s' retyped to '%s'"), *P[2], *P[3]) : Err,
            TEXT(""));
    }
    else if (Op == TEXT("LIST_VARIABLES") && P.Num() >= 2)
    {
        // LIST_VARIABLES|BPPath
        // Returns pipe-delimited: VarName~PinCategory~PinSubCategory
        FString Result = ListVariables(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Variables listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("RUN_PYTHON") && P.Num() >= 2)
    {
        // RUN_PYTHON|PythonCode
        // Joins remaining pipe segments back (code may contain pipes)
        FString Code = P[1];
        for (int32 i = 2; i < P.Num(); i++)
            Code += TEXT("|") + P[i];
        FString Result = RunPython(Code);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Python executed") : Result,
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_COMPILE_ERRORS") && P.Num() >= 2)
    {
        // GET_COMPILE_ERRORS|BPPath
        // Compiles the Blueprint and returns "ERROR:msg|WARNING:msg|..." or
        // "CLEAN". Distinct from COMPILE — this is the diagnostic version.
        FString Result = GetCompileErrors(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? (Result == TEXT("CLEAN") ? TEXT("Compiled clean") : TEXT("Compile issues found"))
                : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_BLUEPRINT") && P.Num() >= 3)
    {
        // CREATE_BLUEPRINT|AssetPath|ParentClass
        // ParentClass: short C++ name, A/U prefix optional, e.g. "Character",
        // "Actor", "Pawn", "ActorComponent", "GameModeBase", "PlayerController"
        FString Result = CreateBlueprint(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Blueprint created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    // ------------------------------------------------------------------
    // Function graphs, node positioning, level actor placement (v1.7)
    // ------------------------------------------------------------------
    else if (Op == TEXT("CREATE_FUNCTION") && P.Num() >= 3)
    {
        // CREATE_FUNCTION|BPPath|FunctionName
        FString Result = CreateFunction(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Function '%s' created"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SPAWN_NODE_IN_GRAPH") && P.Num() >= 7)
    {
        // SPAWN_NODE_IN_GRAPH|BPPath|GraphName|NodeClass|Comment|X|Y
        FString Result = SpawnNodeInGraph(P[1], P[2], P[3], P[4],
            FCString::Atoi(*P[5]), FCString::Atoi(*P[6]));
        bool bOk = !Result.IsEmpty() && !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Node spawned: %s"), *Result)
                : (Result.IsEmpty() ? TEXT("Spawn failed") : Result.RightChop(4)),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_NODE_POSITION") && P.Num() >= 5)
    {
        // SET_NODE_POSITION|BPPath|NodeGUID|X|Y
        FString Err = SetNodePosition(P[1], P[2], FCString::Atoi(*P[3]), FCString::Atoi(*P[4]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Node position set") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_INPUT_ACTION") && P.Num() >= 3)
    {
        // CREATE_INPUT_ACTION|AssetPath|ValueType
        FString Result = CreateInputAction(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("InputAction created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SPAWN_ACTOR_IN_LEVEL") && P.Num() >= 6)
    {
        // SPAWN_ACTOR_IN_LEVEL|BlueprintPath|X|Y|Z|RotYaw
        FString Result = SpawnActorInLevel(P[1],
            FCString::Atof(*P[2]), FCString::Atof(*P[3]), FCString::Atof(*P[4]), FCString::Atof(*P[5]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Actor spawned: '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("LIST_LEVEL_ACTORS"))
    {
        // LIST_LEVEL_ACTORS|Filter — filter is optional
        FString Filter = P.Num() >= 2 ? P[1] : TEXT("");
        FString Results = ListLevelActors(Filter);
        bool bOk = !Results.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Actors listed") : Results.RightChop(4),
            bOk ? Results : TEXT(""));
    }
    else if (Op == TEXT("SET_ACTOR_TRANSFORM") && P.Num() >= 8)
    {
        // SET_ACTOR_TRANSFORM|ActorLabel|X|Y|Z|Pitch|Yaw|Roll|SX|SY|SZ(optional, default 1.0)
        FVector Loc(FCString::Atof(*P[2]), FCString::Atof(*P[3]), FCString::Atof(*P[4]));
        FRotator Rot(FCString::Atof(*P[5]), FCString::Atof(*P[6]), FCString::Atof(*P[7]));
        FVector Scale(
            P.Num() >= 9  ? FCString::Atof(*P[8])  : 1.0f,
            P.Num() >= 10 ? FCString::Atof(*P[9])  : 1.0f,
            P.Num() >= 11 ? FCString::Atof(*P[10]) : 1.0f);
        FString Err = SetActorTransform(P[1], Loc, Rot, Scale);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Actor transform set") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("DELETE_LEVEL_ACTOR") && P.Num() >= 2)
    {
        // DELETE_LEVEL_ACTOR|ActorLabel
        FString Err = DeleteLevelActor(P[1]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Actor deleted") : Err.RightChop(4), TEXT(""));
    }
    // ------------------------------------------------------------------
    // UMG widget + Material graph commands (v1.8)
    // ------------------------------------------------------------------
    else if (Op == TEXT("CREATE_WIDGET_BLUEPRINT") && P.Num() >= 2)
    {
        // CREATE_WIDGET_BLUEPRINT|AssetPath
        FString Result = CreateWidgetBlueprint(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Widget Blueprint created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_WIDGET_ELEMENT") && P.Num() >= 8)
    {
        // ADD_WIDGET_ELEMENT|WidgetBPPath|ElementType|Name|X|Y|W|H
        FString Result = AddWidgetElement(P[1], P[2], P[3],
            FCString::Atoi(*P[4]), FCString::Atoi(*P[5]), FCString::Atoi(*P[6]), FCString::Atoi(*P[7]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Widget element '%s' added"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_WIDGET_TEXT") && P.Num() >= 4)
    {
        // SET_WIDGET_TEXT|WidgetBPPath|ElementName|Text
        // Text may itself contain pipes — rejoin everything from index 3 onward
        FString Text = P[3];
        for (int32 i = 4; i < P.Num(); ++i)
            Text += TEXT("|") + P[i];
        FString Err = SetWidgetText(P[1], P[2], Text);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Widget text set") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_MATERIAL") && P.Num() >= 3)
    {
        // CREATE_MATERIAL|AssetPath|BlendMode
        FString Result = CreateMaterial(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Material created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_MATERIAL_NODE") && P.Num() >= 4)
    {
        // ADD_MATERIAL_NODE|MaterialPath|NodeType|X|Y
        FString Result = AddMaterialNode(P[1], P[2], FCString::Atoi(*P[3]), FCString::Atoi(*P[4]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Material node added at index %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CONNECT_MATERIAL_PINS") && P.Num() >= 6)
    {
        // CONNECT_MATERIAL_PINS|MaterialPath|NodeIndexA|OutputPin|NodeIndexB|InputPin
        FString Err = ConnectMaterialPins(P[1], FCString::Atoi(*P[2]), P[3], FCString::Atoi(*P[4]), P[5]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Material pins connected") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("SET_MATERIAL_RESULT") && P.Num() >= 5)
    {
        // SET_MATERIAL_RESULT|MaterialPath|Channel|NodeIndex|OutputPin
        FString Err = SetMaterialResult(P[1], P[2], FCString::Atoi(*P[3]), P[4]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Material result connected") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("COMPILE_MATERIAL") && P.Num() >= 2)
    {
        // COMPILE_MATERIAL|MaterialPath
        FString Result = CompileMaterial(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? (Result == TEXT("CLEAN") ? TEXT("Compiled clean") : TEXT("Compile errors found"))
                : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CLOSE_MATERIAL") && P.Num() >= 2)
    {
        // CLOSE_MATERIAL|MaterialPath
        // Close the Material Editor for this asset before graph mutation —
        // material expression edits while the editor has the asset open can
        // leave the open editor's graph view out of sync with the underlying
        // expression array.
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *P[1]);
        bool bOk = false;
        if (Material)
        {
            if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
            {
                Sub->CloseAllEditorsForAsset(Material);
                bOk = true;
            }
        }
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Material editor closed") : TEXT("Close failed"), TEXT(""));
    }
    else if (Op == TEXT("CREATE_ENUM") && P.Num() >= 3)
    {
        // CREATE_ENUM|AssetPath|Name1,Name2,Name3,...
        FString Result = CreateEnum(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Enum created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_STRUCT") && P.Num() >= 2)
    {
        // CREATE_STRUCT|AssetPath
        FString Result = CreateStruct(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Struct created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_STRUCT_MEMBER") && P.Num() >= 4)
    {
        // ADD_STRUCT_MEMBER|StructAssetPath|MemberName|MemberType
        FString Err = AddStructMember(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Struct member added") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_FUNCTION_LIBRARY") && P.Num() >= 2)
    {
        // CREATE_FUNCTION_LIBRARY|AssetPath
        FString Result = CreateFunctionLibrary(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Function Library created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_LOCAL_VARIABLE") && P.Num() >= 6)
    {
        // ADD_LOCAL_VARIABLE|BPPath|FunctionGraphName|VarName|VarType|DefaultValue
        FString Err = AddLocalVariable(P[1], P[2], P[3], P[4], P[5]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Local variable added") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("SET_VARIABLE_METADATA") && P.Num() >= 5)
    {
        // SET_VARIABLE_METADATA|BPPath|VarName|MetaKey|MetaValue
        FString Err = SetVariableMetadata(P[1], P[2], P[3], P[4]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Variable metadata set") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_EVENT_DISPATCHER") && P.Num() >= 3)
    {
        // CREATE_EVENT_DISPATCHER|BPPath|DispatcherName|ParamType1:ParamName1,...
        FString Params = P.Num() >= 4 ? P[3] : TEXT("");
        FString Err = CreateEventDispatcher(P[1], P[2], Params);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Event dispatcher created") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_STATE_MACHINE") && P.Num() >= 6)
    {
        // CREATE_STATE_MACHINE|BPPath|GraphName|StateMachineName|X|Y
        FString Result = CreateStateMachine(P[1], P[2], P[3], FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("State machine node spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_ANIM_STATE") && P.Num() >= 6)
    {
        // ADD_ANIM_STATE|BPPath|StateMachineNodeGUID|StateName|X|Y
        FString Result = AddAnimState(P[1], P[2], P[3], FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("State node spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_ANIM_TRANSITION") && P.Num() >= 5)
    {
        // ADD_ANIM_TRANSITION|BPPath|StateMachineNodeGUID|FromStateGUID|ToStateGUID
        FString Result = AddAnimTransition(P[1], P[2], P[3], P[4]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Transition node spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("LIST_ANIM_STATES") && P.Num() >= 3)
    {
        // LIST_ANIM_STATES|BPPath|StateMachineNodeGUID
        FString Result = ListAnimStates(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("States listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_ANIM_STATE_TRANSITIONS") && P.Num() >= 3)
    {
        // GET_ANIM_STATE_TRANSITIONS|BPPath|StateNodeGUID
        FString Result = GetAnimStateTransitions(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Transitions listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_ANIM_NODE_PINS") && P.Num() >= 3)
    {
        // GET_ANIM_NODE_PINS|BPPath|NodeGUID
        FString Result = GetAnimNodePins(P[1], P[2]);
        SendResponse(Sender, true, Op, TEXT("Pins found"), Result);
    }
    else if (Op == TEXT("CONNECT_ANIM_PINS") && P.Num() >= 6)
    {
        // CONNECT_ANIM_PINS|BPPath|NodeGUIDA|PinNameA|NodeGUIDB|PinNameB
        FString Result = ConnectAnimPins(P[1], P[2], P[3], P[4], P[5]);
        bool bOk = Result.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Anim pins connected") : Result.RightChop(4),
            TEXT(""));
    }
    else if (Op == TEXT("LIST_ANIM_GRAPH_NODES") && P.Num() >= 3)
    {
        // LIST_ANIM_GRAPH_NODES|BPPath|GraphName
        FString Result = ListAnimGraphNodes(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Anim graph nodes listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("LIST_STATE_GRAPH_NODES") && P.Num() >= 3)
    {
        // LIST_STATE_GRAPH_NODES|BPPath|StateOrTransitionGUID
        FString Result = ListStateGraphNodes(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("State graph nodes listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SPAWN_NODE_ANCHORED") && P.Num() >= 6)
    {
        // SPAWN_NODE_ANCHORED|BPPath|AnchorNodeGUID|NodeClass|Comment|X|Y
        FString Result = SpawnNodeAnchored(P[1], P[2], P[3], P[4], FCString::Atoi(*P[5]), P.Num() >= 7 ? FCString::Atoi(*P[6]) : 0);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Node spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_BLEND_SPACE_PLAYER_ANCHORED") && P.Num() >= 6)
    {
        // CREATE_BLEND_SPACE_PLAYER_ANCHORED|BPPath|AnchorNodeGUID|BlendSpaceAssetPath|X|Y
        FString Result = CreateBlendSpacePlayerAnchored(P[1], P[2], P[3], FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("BlendSpacePlayer spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_TRANSITION_CONDITION") && P.Num() >= 5)
    {
        // SET_TRANSITION_CONDITION|BPPath|TransitionNodeGUID|VarName|bNegate(0/1)
        FString Result = SetTransitionCondition(P[1], P[2], P[3], FCString::Atoi(*P[4]) != 0);
        bool bOk = Result.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Transition condition set") : Result.RightChop(4),
            TEXT(""));
    }
    else if (Op == TEXT("ADD_ANIM_SLOT_NODE") && P.Num() >= 6)
    {
        // ADD_ANIM_SLOT_NODE|BPPath|GraphName|SlotName|X|Y
        FString Result = AddAnimSlotNode(P[1], P[2], P[3], FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Slot node spawned: %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_NIAGARA_SYSTEM") && P.Num() >= 2)
    {
        // CREATE_NIAGARA_SYSTEM|AssetPath
        FString Result = CreateNiagaraSystem(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Niagara System created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_NIAGARA_EMITTER") && P.Num() >= 2)
    {
        // CREATE_NIAGARA_EMITTER|AssetPath
        FString Result = CreateNiagaraEmitter(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Niagara Emitter created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("LIST_NIAGARA_MODULES") && P.Num() >= 3)
    {
        // LIST_NIAGARA_MODULES|SystemAssetPath|EmitterName
        FString Result = ListNiagaraModules(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Modules listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("SET_NIAGARA_MODULE_INPUT") && P.Num() >= 6)
    {
        // SET_NIAGARA_MODULE_INPUT|SystemAssetPath|EmitterName|ModuleName|InputName|Value
        FString Err = SetNiagaraModuleInput(P[1], P[2], P[3], P[4], P[5]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Module input set") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_PHYSICS_ASSET") && P.Num() >= 4)
    {
        // CREATE_PHYSICS_ASSET|SkeletalMeshPath|AssetPath|bSetToMesh
        bool bSetToMesh = P[3].ToBool();
        FString Result = CreatePhysicsAsset(P[1], P[2], bSetToMesh);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Physics Asset created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_IK_RIG") && P.Num() >= 3)
    {
        // CREATE_IK_RIG|AssetPath|SkeletalMeshPath
        FString Result = CreateIKRig(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("IK Rig created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("IK_RIG_AUTO_SETUP") && P.Num() >= 2)
    {
        // IK_RIG_AUTO_SETUP|IKRigAssetPath
        FString Err = IKRigAutoSetup(P[1]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("IK Rig auto setup applied") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("ADD_IK_GOAL") && P.Num() >= 4)
    {
        // ADD_IK_GOAL|IKRigAssetPath|GoalName|BoneName
        FString Err = AddIKGoal(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("IK Goal added") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("ADD_RETARGET_CHAIN") && P.Num() >= 6)
    {
        // ADD_RETARGET_CHAIN|IKRigAssetPath|ChainName|StartBone|EndBone|GoalName
        FString Err = AddRetargetChain(P[1], P[2], P[3], P[4], P[5]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Retarget chain added") : Err.RightChop(4), TEXT(""));
    }
    else if (Op == TEXT("CREATE_IK_RETARGETER") && P.Num() >= 4)
    {
        // CREATE_IK_RETARGETER|AssetPath|SourceIKRigPath|TargetIKRigPath
        FString Result = CreateIKRetargeter(P[1], P[2], P[3]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("IK Retargeter created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_ANIM_MONTAGE") && P.Num() >= 3)
    {
        // CREATE_ANIM_MONTAGE|AssetPath|SkeletonPath|AnimSequencePath
        FString AnimSeq = P.Num() >= 4 ? P[3] : TEXT("");
        FString Result = CreateAnimMontage(P[1], P[2], AnimSeq);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Anim Montage created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("GET_ANIM_PIN_CONNECTIONS") && P.Num() >= 4)
    {
        // GET_ANIM_PIN_CONNECTIONS|BPPath|NodeGUID|PinName
        FString Result = GetAnimPinConnections(P[1], P[2], P[3]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pin connections listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("EDIT_BLEND_SPACE_SAMPLE") && P.Num() >= 5)
    {
        // EDIT_BLEND_SPACE_SAMPLE|BlendSpaceAssetPath|AnimSequencePath|NewX|NewY
        FString Result = EditBlendSpaceSample(P[1], P[2], FCString::Atof(*P[3]), FCString::Atof(*P[4]));
        bool bOk = Result.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Sample repositioned") : Result.RightChop(4),
            TEXT(""));
    }
    else if (Op == TEXT("SET_BLEND_SPACE_PLAYER_ASSET") && P.Num() >= 4)
    {
        // SET_BLEND_SPACE_PLAYER_ASSET|BPPath|NodeGUID|BlendSpaceAssetPath
        FString Result = SetBlendSpacePlayerAsset(P[1], P[2], P[3]);
        bool bOk = Result.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("BlendSpacePlayer asset set") : Result.RightChop(4),
            TEXT(""));
    }
    else if (Op == TEXT("ADD_BLEND_SPACE_SAMPLE") && P.Num() >= 5)
    {
        // ADD_BLEND_SPACE_SAMPLE|BlendSpaceAssetPath|AnimSequencePath|X|Y
        FString Result = AddBlendSpaceSample(P[1], P[2], FCString::Atof(*P[3]), FCString::Atof(*P[4]));
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Sample added at index %s"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_BLEND_SPACE") && P.Num() >= 3)
    {
        // CREATE_BLEND_SPACE|AssetPath|SkeletonPath
        FString Result = CreateBlendSpace(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("BlendSpace created at '%s'"), *Result) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("ADD_SKELETON_SOCKET") && P.Num() >= 7)
    {
        // ADD_SKELETON_SOCKET|SkeletonPath|SocketName|BoneName|X|Y|Z
        FString Err = AddSkeletonSocket(P[1], P[2], P[3],
            FCString::Atof(*P[4]), FCString::Atof(*P[5]), FCString::Atof(*P[6]));
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Skeleton socket added") : Err.RightChop(4), TEXT(""));
    }
    // ------------------------------------------------------------------
    // Multi-graph support — Function & Macro graphs (v1.15)
    // ------------------------------------------------------------------
    else if (Op == TEXT("LIST_GRAPHS") && P.Num() >= 2)
    {
        // LIST_GRAPHS|BPPath
        // Returns pipe-delimited "GraphName~GraphType" entries.
        FString Result = ListGraphs(P[1]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Graphs listed") : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_FUNCTION_GRAPH") && P.Num() >= 3)
    {
        // CREATE_FUNCTION_GRAPH|BPPath|FunctionName
        FString Result = CreateFunctionGraph(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Function graph '%s' created"), *P[2]) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else if (Op == TEXT("CREATE_MACRO_GRAPH") && P.Num() >= 3)
    {
        // CREATE_MACRO_GRAPH|BPPath|MacroName
        FString Result = CreateMacroGraph(P[1], P[2]);
        bool bOk = !Result.StartsWith(TEXT("ERR:"));
        SendResponse(Sender, bOk, Op,
            bOk ? FString::Printf(TEXT("Macro graph '%s' created"), *P[2]) : Result.RightChop(4),
            bOk ? Result : TEXT(""));
    }
    else
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge: Unknown or malformed command: %s"), *Command);
        SendResponse(Sender, false, Op,
            FString::Printf(TEXT("Unknown command or wrong arg count: %s"), *Command), TEXT(""));
    }

#else
    SendResponse(Sender, false, Op, TEXT("GraphBridge commands require an editor build"), TEXT(""));
#endif
}

// ---------------------------------------------------------------------------
// Delegate setter
// ---------------------------------------------------------------------------

void UGraphBridgeAutomationLibrary::SetSendMessageDelegate(FOnSendMessage InDelegate)
{
    SendMessageDelegate = InDelegate;
}

// ---------------------------------------------------------------------------
// Asset helper (outside WITH_EDITOR — LoadObject works everywhere)
// ---------------------------------------------------------------------------

UBlueprint* UGraphBridgeAutomationLibrary::GetBlueprintByPath(FString AssetPath)
{
    AssetPath.RemoveFromEnd(TEXT("_C"));
    return LoadObject<UBlueprint>(nullptr, *AssetPath);
}

// ---------------------------------------------------------------------------
// Editor-only implementations
// ---------------------------------------------------------------------------

#if WITH_EDITOR

UEdGraphNode* UGraphBridgeAutomationLibrary::FindNodeByName(UBlueprint* Blueprint, FString NodeIdentifier)
{
    if (!Blueprint) return nullptr;

    TArray<UEdGraphNode*> Matches;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == NodeIdentifier ||
                Node->NodeComment == NodeIdentifier)
            {
                Matches.Add(Node);
            }
        }
    }

    if (Matches.Num() == 1) return Matches[0];

    if (Matches.Num() > 1)
    {
        // Ambiguous — log all matches so the caller can surface a useful error
        // rather than silently wiring the wrong node. Return null to force
        // the caller to use a GUID instead.
        TArray<FString> Descriptions;
        for (UEdGraphNode* M : Matches)
            Descriptions.Add(FString::Printf(TEXT("%s (GUID: %s)"),
                *M->GetNodeTitle(ENodeTitleType::FullTitle).ToString(),
                *M->NodeGuid.ToString()));
        UE_LOG(LogGraphBridge, Warning,
            TEXT("GraphBridge FindNodeByName: '%s' matched %d nodes — use GUID instead: %s"),
            *NodeIdentifier, Matches.Num(), *FString::Join(Descriptions, TEXT(" | ")));
        return nullptr;
    }

    return nullptr;
}

UEdGraphNode* UGraphBridgeAutomationLibrary::FindNodeById(UBlueprint* Blueprint, FString NodeId)
{
    if (!Blueprint) return nullptr;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            if (Node->NodeGuid.ToString() == NodeId) return Node;
            if (Node->NodeComment == NodeId) return Node;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// SpawnEventNode — create a class-level override event node (K2Node_Event).
// Command: SPAWN_EVENT_NODE|BPPath|EventFuncName|Comment|X|Y
// EventFuncName is the internal UFunction name on the parent class, e.g.:
//   ReceiveAnyDamage, ReceiveBeginPlay, ReceiveHit, ReceivePointDamage,
//   ReceiveRadialDamage, ReceiveTick, ReceiveDestroyed, ReceiveEndPlay
// Returns the new node's GUID on success, or "ERR:..." on failure.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SpawnEventNode(
    FString BlueprintPath, FString EventFuncName,
    FString Comment, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);
    if (!Blueprint->UbergraphPages.Num())
        return TEXT("ERR:Blueprint has no EventGraph pages");

    UEdGraph* Graph = Blueprint->UbergraphPages[0];

    // Walk the parent class chain for the named overridable function.
    UFunction* OverrideFunc = nullptr;
    UClass* SearchClass = Blueprint->ParentClass;
    while (SearchClass && !OverrideFunc)
    {
        OverrideFunc = SearchClass->FindFunctionByName(
            *EventFuncName, EIncludeSuperFlag::ExcludeSuper);
        SearchClass = SearchClass->GetSuperClass();
    }

    if (!OverrideFunc)
        return FString::Printf(
            TEXT("ERR:No overridable function '%s' found in class hierarchy. ")
            TEXT("Valid names include: ReceiveAnyDamage, ReceiveBeginPlay, ")
            TEXT("ReceiveHit, ReceivePointDamage, ReceiveRadialDamage, ReceiveTick"),
            *EventFuncName);

    // Reject if an override node already exists for this function.
    TArray<UK2Node_Event*> ExistingEvents;
    FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_Event>(Blueprint, ExistingEvents);
    for (UK2Node_Event* Existing : ExistingEvents)
    {
        if (Existing->EventReference.GetMemberName() == OverrideFunc->GetFName())
            return FString::Printf(
                TEXT("ERR:Event node for '%s' already exists in graph"),
                *EventFuncName);
    }

    const FScopedTransaction Transaction(
        FText::Format(
            NSLOCTEXT("GraphBridge", "SpawnEventNode", "GraphBridge: Spawn Event Node ({0})"),
            FText::FromString(EventFuncName)));

    Graph->Modify();

    UK2Node_Event* EventNode = NewObject<UK2Node_Event>(
        Graph, UK2Node_Event::StaticClass(), NAME_None, RF_Transactional);

    EventNode->EventReference.SetExternalMember(
        OverrideFunc->GetFName(),
        OverrideFunc->GetOuterUClass());
    EventNode->bOverrideFunction = true;
    EventNode->NodePosX = X;
    EventNode->NodePosY = Y;
    EventNode->NodeComment = Comment;
    EventNode->CreateNewGuid();

    Graph->AddNode(EventNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
    EventNode->AllocateDefaultPins();
    EventNode->PostPlacedNewNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    return EventNode->NodeGuid.ToString();
}

FString UGraphBridgeAutomationLibrary::SpawnNode(FString BlueprintPath, FString NodeClass,
    FString Comment, int32 X, int32 Y, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (!GraphName.IsEmpty())
    {
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph)
            return FString::Printf(
                TEXT("ERR:Graph '%s' not found — run LIST_GRAPHS to see available graphs"), *GraphName);
        return SpawnNodeOnGraph(Blueprint, Graph, NodeClass, Comment, X, Y);
    }

    if (!Blueprint->UbergraphPages.Num())
        return TEXT("ERR:Blueprint has no EventGraph pages");

    return SpawnNodeOnGraph(Blueprint, Blueprint->UbergraphPages[0], NodeClass, Comment, X, Y);
}

// ---------------------------------------------------------------------------
// FindGraphByName
// "EventGraph" (or any UbergraphPages name match) resolves to the main event
// graph. Anything else is matched by name against Blueprint->FunctionGraphs
// (created via CREATE_FUNCTION/CREATE_FUNCTION_GRAPH, or any other user
// function graph), then Blueprint->MacroGraphs (created via
// CREATE_MACRO_GRAPH) — searched in that order.
// ---------------------------------------------------------------------------
UEdGraph* UGraphBridgeAutomationLibrary::FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
{
    if (!Blueprint) return nullptr;

    if (GraphName.Equals(TEXT("EventGraph"), ESearchCase::IgnoreCase))
        return Blueprint->UbergraphPages.Num() ? Blueprint->UbergraphPages[0] : nullptr;

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
        if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            return Graph;

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            return Graph;

    for (UEdGraph* Graph : Blueprint->MacroGraphs)
        if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
            return Graph;

    return nullptr;
}

// ---------------------------------------------------------------------------
// FindNodeInGraph — see header comment. Combines FindNodeById's GUID lookup
// and FindNodeByName's comment/title lookup (with the same
// return-null-on-ambiguity convention), but scoped to a single graph instead
// of Blueprint->UbergraphPages.
// ---------------------------------------------------------------------------
UEdGraphNode* UGraphBridgeAutomationLibrary::FindNodeInGraph(UEdGraph* Graph, const FString& NodeId)
{
    if (!Graph) return nullptr;

    TArray<UEdGraphNode*> Matches;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node) continue;
        if (Node->NodeGuid.ToString() == NodeId) return Node;
        if (Node->NodeComment == NodeId ||
            Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString() == NodeId)
        {
            Matches.Add(Node);
        }
    }

    if (Matches.Num() == 1) return Matches[0];
    return nullptr;
}

// ---------------------------------------------------------------------------
// SpawnNodeInGraph
// Command: SPAWN_NODE_IN_GRAPH|BPPath|GraphName|NodeClass|Comment|X|Y
// Same node-construction pipeline as SpawnNode, but targets any named graph
// resolved via FindGraphByName instead of always UbergraphPages[0].
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SpawnNodeInGraph(FString BlueprintPath, FString GraphName,
    FString NodeClass, FString Comment, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        TArray<FString> Available;
        Available.Add(TEXT("EventGraph"));
        for (UEdGraph* G : Blueprint->FunctionGraphs)
            if (G) Available.Add(G->GetName());
        return FString::Printf(
            TEXT("ERR:Graph '%s' not found. Available graphs: %s"),
            *GraphName, *FString::Join(Available, TEXT(", ")));
    }

    return SpawnNodeOnGraph(Blueprint, Graph, NodeClass, Comment, X, Y);
}

// ---------------------------------------------------------------------------
// SpawnNodeOnGraph — shared node-construction core for SpawnNode and
// SpawnNodeInGraph. See SpawnNode's original comments for the rationale
// behind the PerformAction / SpawnActorFromClass special-case handling.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SpawnNodeOnGraph(UBlueprint* Blueprint, UEdGraph* Graph,
    FString NodeClass, FString Comment, int32 X, int32 Y)
{
    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema)
        return TEXT("ERR:Could not get K2 schema");

    // Strip full path prefix if provided (e.g. /Script/BlueprintGraph.K2Node_Event -> K2Node_Event)
    FString ShortNodeClass = NodeClass;
    if (NodeClass.Contains(TEXT(".")))
        NodeClass.Split(TEXT("."), nullptr, &ShortNodeClass);

    // Prefer exact name match; fall back to contains. Skip abstract classes.
    // Sort partial matches for determinism — first alphabetically is used.
    UClass* FoundClass = nullptr;
    TArray<FString> PartialMatches;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (!It->IsChildOf(UK2Node::StaticClass())) continue;
        if (It->HasAnyClassFlags(CLASS_Abstract)) continue;
        if (It->GetName() == ShortNodeClass) { FoundClass = *It; break; }
        if (It->GetName().Contains(ShortNodeClass))
        {
            // Skip ActorBoundEvent unless explicitly requested —
            // it requires a bound delegate target and spawns as
            // "None (None)" when used as a generic event node.
            if (It->GetName().Contains(TEXT("ActorBoundEvent")) &&
                !ShortNodeClass.Contains(TEXT("ActorBound")))
                continue;
            PartialMatches.Add(It->GetName());
        }
    }
    if (!FoundClass && PartialMatches.Num() > 0)
    {
        PartialMatches.Sort();
        // Find best match: prefer exact substring at word boundary
        for (const FString& M : PartialMatches)
        {
            UClass* C = FindFirstObject<UClass>(*M, EFindFirstObjectOptions::None);
            if (C && !C->HasAnyClassFlags(CLASS_Abstract)) { FoundClass = C; break; }
        }
    }
    if (!FoundClass)
        return FString::Printf(TEXT("ERR:No UClass found for '%s' — use FIND_NODE_CLASS to discover valid class names"), *NodeClass);

    if (!FoundClass->IsChildOf(UK2Node::StaticClass()))
        return FString::Printf(TEXT("ERR:Resolved class '%s' is not a UK2Node"), *FoundClass->GetName());

    // UAnimGraphNode_Base and its subclasses (e.g. UAnimGraphNode_CallFunction)
    // rely on bespoke setup methods (SetupFromFunction, etc.) that populate
    // members like InnerGraph *before* the node is ever duplicated/PostLoad'd.
    // This generic pipeline only does NewObject + PerformAction's template
    // duplication — it never calls that bespoke setup — so those classes'
    // PostLoad()/BindDelegates() dereference a null InnerGraph and crash with
    // an access violation in UEdGraph::AddOnGraphChangedHandler(), even when
    // spawned on a legitimate AnimGraph (confirmed live: crashes identically
    // on both EventGraph and a real AnimBlueprint's AnimGraph). Reject the
    // whole family outright rather than guess which subclasses happen to be
    // safe — SPAWN_NODE/SPAWN_NODE_IN_GRAPH do not support Anim Graph nodes.
    if (FoundClass->IsChildOf(UAnimGraphNode_Base::StaticClass()))
        return FString::Printf(
            TEXT("ERR:Resolved class '%s' is an Anim Graph node — these require bespoke setup this command doesn't perform and can crash the editor if spawned via the generic template-duplication path. Not supported by SPAWN_NODE/SPAWN_NODE_IN_GRAPH."),
            *FoundClass->GetName());

    // Some UK2Node subclasses are only valid on a specific graph schema
    // (e.g. Material/Sound/Niagara graph node classes that happen to derive
    // from UK2Node) and could hit similar issues outside their intended
    // schema. CanCreateUnderSpecifiedSchema is the same check the Blueprint
    // editor's own node palette uses to filter actions per graph — ask the
    // resolved class's CDO before ever constructing/duplicating it.
    const UK2Node* FoundClassCDO = Cast<UK2Node>(FoundClass->GetDefaultObject());
    if (!FoundClassCDO || !FoundClassCDO->CanCreateUnderSpecifiedSchema(Schema))
        return FString::Printf(
            TEXT("ERR:Resolved class '%s' is not valid on this graph (schema '%s') — it likely belongs to a different graph type. Use FIND_NODE_CLASS or a more specific NodeClass string to avoid ambiguous matches."),
            *FoundClass->GetName(), *Schema->GetClass()->GetName());

    if (PartialMatches.Num() > 1)
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SpawnNode: '%s' matched %d classes, using first: %s. Matches: %s"),
            *NodeClass, PartialMatches.Num(), *PartialMatches[0], *FString::Join(PartialMatches, TEXT(", ")));

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "SpawnNode", "GraphBridge: Spawn Node ({0})"),
                      FText::FromString(NodeClass)));

    // Create the node template on the transient package first (not on the graph)
    // then hand it to PerformAction which runs the full K2 initialisation pipeline:
    // PostPlacedNewNode -> AllocateDefaultPins -> AddNode, all in correct order.
    // This is exactly what the Blueprint Editor does when you drag from the palette.
    // Raw NewObject on the graph skips critical virtual dispatch setup and causes
    // the LowLevelFatalError at EngineBaseTypes.h:481 on the next tick.
    UK2Node* Template = NewObject<UK2Node>(
        GetTransientPackage(), FoundClass, NAME_None, RF_Transactional);
    if (!Template)
        return FString::Printf(TEXT("ERR:Failed to create node template for class '%s'"), *NodeClass);

    FEdGraphSchemaAction_K2NewNode Action;
    Action.NodeTemplate = Template;

    // K2Node_SpawnActorFromClass::PostPlacedNewNode() calls FindPinChecked before
    // AllocateDefaultPins has run when spawned via PerformAction on UE5.7, causing
    // an assertion crash. Work around it by manually adding the node to the graph,
    // calling AllocateDefaultPins first, then PostPlacedNewNode in the correct order.
    UEdGraphNode* NewNode = nullptr;
    if (FoundClass->IsChildOf(UK2Node_SpawnActorFromClass::StaticClass()))
    {
        const FScopedTransaction SpawnTx(
            FText::Format(NSLOCTEXT("GraphBridge", "SpawnNodeSafe", "GraphBridge: Spawn Node ({0})"),
                          FText::FromString(NodeClass)));
        Graph->Modify();
        UK2Node* SafeNode = NewObject<UK2Node>(Graph, FoundClass, NAME_None, RF_Transactional);
        SafeNode->CreateNewGuid();
        SafeNode->NodePosX = X;
        SafeNode->NodePosY = Y;
        Graph->AddNode(SafeNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
        SafeNode->AllocateDefaultPins();
        SafeNode->PostPlacedNewNode();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        NewNode = SafeNode;
    }
    else
    {
    // PerformAction uses FVector2f in UE5.7 (double->float API migration)
    NewNode = Action.PerformAction(
        Graph, /*FromPin=*/nullptr, FVector2f((float)X, (float)Y), /*bSelectNewNode=*/false);
    }

    if (!NewNode)
        return FString::Printf(TEXT("ERR:PerformAction returned null for class '%s' — Blueprint editor may still be open, call CLOSE_BLUEPRINT first"), *NodeClass);

    NewNode->NodeComment = Comment;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return NewNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// FindNodeByIdAllGraphs — like FindNodeById, but also searches FunctionGraphs.
// FindNodeById/FindNodeByName only look at UbergraphPages because every
// existing command targets EventGraph; SET_NODE_POSITION is the first
// command that must reach nodes inside a custom function graph too.
// ---------------------------------------------------------------------------
UEdGraphNode* UGraphBridgeAutomationLibrary::FindNodeByIdAllGraphs(UBlueprint* Blueprint, FString NodeId)
{
    if (!Blueprint) return nullptr;

    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(Blueprint->UbergraphPages);
    AllGraphs.Append(Blueprint->FunctionGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            if (Node->NodeGuid.ToString() == NodeId) return Node;
            if (Node->NodeComment == NodeId) return Node;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// FindStateMachineNode — see header comment.
// ---------------------------------------------------------------------------
UAnimGraphNode_StateMachineBase* UGraphBridgeAutomationLibrary::FindStateMachineNode(UBlueprint* Blueprint, const FString& NodeGUID)
{
    UEdGraphNode* Node = FindNodeByIdAllGraphs(Blueprint, NodeGUID);
    return Cast<UAnimGraphNode_StateMachineBase>(Node);
}

// ---------------------------------------------------------------------------
// FindAnimStateNode — see header comment.
// ---------------------------------------------------------------------------
UAnimStateNodeBase* UGraphBridgeAutomationLibrary::FindAnimStateNode(UAnimGraphNode_StateMachineBase* MachineNode, const FString& NodeGUID)
{
    if (!MachineNode || !MachineNode->EditorStateMachineGraph) return nullptr;
    for (UEdGraphNode* Node : MachineNode->EditorStateMachineGraph->Nodes)
    {
        if (!Node) continue;
        if (Node->NodeGuid.ToString() == NodeGUID) return Cast<UAnimStateNodeBase>(Node);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// SetNodePosition
// Command: SET_NODE_POSITION|BPPath|NodeGUID|X|Y
// Moves a node to specific coordinates. Searches EventGraph + FunctionGraphs.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetNodePosition(FString BlueprintPath, FString NodeGUID, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeByIdAllGraphs(Blueprint, NodeGUID);
    if (!Node)
        return FString::Printf(
            TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeGUID);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetNodePosition", "GraphBridge: Set Node Position"));
    Node->Modify();
    Node->NodePosX = X;
    Node->NodePosY = Y;

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetNodePosition: '%s' -> (%d, %d)"),
        *NodeGUID, X, Y);
    return TEXT("");
}

// Returns empty string on success, or a human-readable error reason on failure.
// The caller (ExecuteAtomicCommand) uses IsEmpty() to determine success and
// forwards the error string to SendResponse so the AI can self-correct.
FString UGraphBridgeAutomationLibrary::ConnectPins(FString BlueprintPath,
    FString NodeA, FString PinA, FString NodeB, FString PinB, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = nullptr;
    UEdGraphNode* SourceNode = nullptr;
    UEdGraphNode* TargetNode = nullptr;

    if (!GraphName.IsEmpty())
    {
        Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph)
            return FString::Printf(TEXT("Graph '%s' not found — run LIST_GRAPHS to see available graphs"), *GraphName);

        SourceNode = FindNodeInGraph(Graph, NodeA);
        if (!SourceNode)
            return FString::Printf(TEXT("Source node not found: '%s' in graph '%s'"), *NodeA, *GraphName);

        TargetNode = FindNodeInGraph(Graph, NodeB);
        if (!TargetNode)
            return FString::Printf(TEXT("Target node not found: '%s' in graph '%s'"), *NodeB, *GraphName);
    }
    else
    {
        if (!Blueprint->UbergraphPages.Num())
            return TEXT("Blueprint has no EventGraph pages");
        Graph = Blueprint->UbergraphPages[0];

        SourceNode = FindNodeById(Blueprint, NodeA);
        if (!SourceNode) SourceNode = FindNodeByName(Blueprint, NodeA);
        if (!SourceNode)
            return FString::Printf(TEXT("Source node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeA);

        TargetNode = FindNodeById(Blueprint, NodeB);
        if (!TargetNode) TargetNode = FindNodeByName(Blueprint, NodeB);
        if (!TargetNode)
            return FString::Printf(TEXT("Target node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeB);
    }

    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return TEXT("Could not get K2 schema");

    // Build available pin list for helpful error messages
    auto PinList = [](UEdGraphNode* Node) -> FString {
        TArray<FString> Names;
        for (UEdGraphPin* P : Node->Pins)
            if (P) Names.Add(FString::Printf(TEXT("%s(%s)"),
                *P->PinName.ToString(),
                P->Direction == EGPD_Output ? TEXT("OUT") : TEXT("IN")));
        return FString::Join(Names, TEXT(", "));
    };

    UEdGraphPin* SourcePin = SourceNode->FindPin(*PinA);
    if (!SourcePin)
        return FString::Printf(TEXT("Pin '%s' not found on source node '%s'. Available pins: %s"),
            *PinA, *NodeA, *PinList(SourceNode));

    UEdGraphPin* TargetPin = TargetNode->FindPin(*PinB);
    if (!TargetPin)
        return FString::Printf(TEXT("Pin '%s' not found on target node '%s'. Available pins: %s"),
            *PinB, *NodeB, *PinList(TargetNode));

    // Explicit direction check before hitting the schema — gives a clear error
    // rather than a cryptic schema rejection when the AI has the pins backwards.
    if (SourcePin->Direction != EGPD_Output)
        return FString::Printf(
            TEXT("Pin '%s' on '%s' is an INPUT pin and cannot be a connection source. "
                 "Swap your node/pin arguments so the OUTPUT pin is first."),
            *PinA, *NodeA);

    if (TargetPin->Direction != EGPD_Input)
        return FString::Printf(
            TEXT("Pin '%s' on '%s' is an OUTPUT pin and cannot be a connection target. "
                 "Swap your node/pin arguments so the INPUT pin is second."),
            *PinB, *NodeB);

    const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);
    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
        return FString::Printf(TEXT("Schema rejected connection: %s"), *Response.Message.ToString());

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "ConnectPins", "GraphBridge: Connect Pins"));
    SourceNode->Modify();
    TargetNode->Modify();
    Schema->TryCreateConnection(SourcePin, TargetPin);

    // Explicitly notify both nodes of the new connection. Wildcard/array-
    // dependent pins (e.g. Array_Length's TargetArray, meta=(ArrayParm=...))
    // only retype themselves in response to this callback. In the real
    // Blueprint editor this fires as a side effect of the drag-and-drop
    // connection UI; TryCreateConnection alone does not reliably trigger it
    // for nodes spawned via this bridge's NewObject+template-duplication
    // pipeline — confirmed live: connecting a concretely-typed Actor array
    // into Array_Length's TargetArray pin left it at "undetermined type"
    // after TryCreateConnection alone, with the compiler asking to "connect
    // something to Length to imply a specific type" (the retyping consumer
    // path also never fired). Calling this explicitly on both endpoints
    // mirrors what the editor does and is a no-op for ordinary non-wildcard
    // pins.
    // NotifyPinConnectionListChanged is declared on UK2Node, NOT the generic
    // UEdGraphNode base (confirmed by a real compile error: "is not a member
    // of 'UEdGraphNode'") — every node this bridge spawns in a K2 graph is a
    // UK2Node in practice, so this cast is safe; guarded with an if in case
    // a future caller ever passes a non-K2 graph node.
    if (UK2Node* SourceK2 = Cast<UK2Node>(SourceNode))
        SourceK2->NotifyPinConnectionListChanged(SourcePin);
    if (UK2Node* TargetK2 = Cast<UK2Node>(TargetNode))
        TargetK2->NotifyPinConnectionListChanged(TargetPin);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

bool UGraphBridgeAutomationLibrary::DisconnectPins(FString BlueprintPath,
    FString NodeA, FString PinA, FString NodeB, FString PinB, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    UEdGraph* Graph = nullptr;
    UEdGraphNode* SourceNode = nullptr;
    UEdGraphNode* TargetNode = nullptr;

    if (!GraphName.IsEmpty())
    {
        Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph) return false;
        SourceNode = FindNodeInGraph(Graph, NodeA);
        TargetNode = FindNodeInGraph(Graph, NodeB);
    }
    else
    {
        if (!Blueprint->UbergraphPages.Num()) return false;
        Graph = Blueprint->UbergraphPages[0];

        SourceNode = FindNodeById(Blueprint, NodeA);
        if (!SourceNode) SourceNode = FindNodeByName(Blueprint, NodeA);
        TargetNode = FindNodeById(Blueprint, NodeB);
        if (!TargetNode) TargetNode = FindNodeByName(Blueprint, NodeB);
    }
    if (!SourceNode || !TargetNode) return false;

    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return false;

    UEdGraphPin* SourcePin = SourceNode->FindPin(PinA);
    UEdGraphPin* TargetPin = TargetNode->FindPin(PinB);
    if (!SourcePin || !TargetPin) return false;

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "DisconnectPins", "GraphBridge: Disconnect Pins"));
    SourceNode->Modify();
    TargetNode->Modify();
    Schema->BreakPinLinks(*SourcePin, true);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

// ---------------------------------------------------------------------------
// DeleteNode
// Command: DELETE_NODE|BPPath|NodeGUID
//
// Uses FindNodeByIdAllGraphs (UbergraphPages + FunctionGraphs) rather than
// the narrower FindNodeById/FindNodeByName pair (EventGraph only) — the
// original bug this fixes: a node in AnimGraph (which lives in
// FunctionGraphs, same as CREATE_FUNCTION graphs) could never be found here.
//
// Uses FBlueprintEditorUtils::RemoveNode(Blueprint, Node) rather than a raw
// Graph->RemoveNode(Node) call — confirmed against engine source that
// UEdGraph::RemoveNode() ONLY removes the node from the graph's Nodes array;
// it never calls Node->DestroyNode(). FBlueprintEditorUtils::RemoveNode DOES
// call DestroyNode() (plus breakpoint/pin-watch cleanup, BreakNodeLinks via
// schema). This matters a lot for state machine nodes specifically:
// UAnimGraphNode_StateMachineBase::DestroyNode() is what actually removes
// its EditorStateMachineGraph sub-graph — skipping it (the original bug)
// would silently orphan that sub-graph in the package instead of deleting it.
// ---------------------------------------------------------------------------
bool UGraphBridgeAutomationLibrary::DeleteNode(FString BlueprintPath, FString NodeId, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    UEdGraphNode* Node = nullptr;
    if (!GraphName.IsEmpty())
    {
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph) return false;
        Node = FindNodeInGraph(Graph, NodeId);
    }
    else
    {
        Node = FindNodeAnywhere(Blueprint, NodeId);
    }
    if (!Node) return false;

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "DeleteNode", "GraphBridge: Delete Node"));
    FBlueprintEditorUtils::RemoveNode(Blueprint, Node);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool UGraphBridgeAutomationLibrary::ClearNodes(FString BlueprintPath, FString CommentMatch, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    TArray<UEdGraphNode*> ToRemove;
    if (!GraphName.IsEmpty())
    {
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph) return false;
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node && Node->NodeComment.Contains(CommentMatch))
                ToRemove.Add(Node);
    }
    else
    {
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
            for (UEdGraphNode* Node : Graph->Nodes)
                if (Node && Node->NodeComment.Contains(CommentMatch))
                    ToRemove.Add(Node);
    }

    if (ToRemove.Num() == 0) return false;

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "ClearNodes", "GraphBridge: Clear Nodes ({0})"),
                      FText::FromString(CommentMatch)));

    for (UEdGraphNode* Node : ToRemove)
    {
        UEdGraph* Graph = Node->GetGraph();
        if (!Graph) continue;
        Graph->Modify();
        Node->Modify();
        Graph->RemoveNode(Node);
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

bool UGraphBridgeAutomationLibrary::SetPinDefault(FString BlueprintPath,
    FString NodeId, FString PinName, FString DefaultValue)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    // FindNodeById only walks UbergraphPages — confirmed live this silently
    // fails for any node in a custom function graph (spawned via
    // SPAWN_NODE_IN_GRAPH), AnimGraph, or state machine, even though those
    // are all reachable via GET_ANIM_NODE_PINS/CONNECT_ANIM_PINS already.
    // FindNodeAnywhere is the same broad search SetVariableRef already uses.
    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node) return false;

    UEdGraphPin* Pin = Node->FindPin(PinName);
    if (!Pin) return false;

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "SetPinDefault", "GraphBridge: Set Pin Default"));
    Node->Modify();

    // Object/class pins store their value in DefaultObject, not DefaultValue.
    // Attempting to set DefaultValue on a PC_Class or PC_Object pin is silently
    // ignored by the compiler, leaving the pin as NONE. Detect these pin categories
    // and resolve the path to a live UObject* instead.
    const FName PinCat = Pin->PinType.PinCategory;
    if (PinCat == UEdGraphSchema_K2::PC_Class || PinCat == UEdGraphSchema_K2::PC_SoftClass)
    {
        // DefaultValue for class pins is expected to be a class path such as
        // /Script/VisualTutorial.UltimateProjectile_C â€” strip the _C suffix to
        // get the UClass asset path, then resolve via StaticLoadClass.
        FString ClassPath = DefaultValue;
        if (ClassPath.EndsWith(TEXT("_C")))
            ClassPath = ClassPath.LeftChop(2); // e.g. /Script/Foo.Bar_C -> /Script/Foo.Bar

        UClass* ResolvedClass = LoadClass<UObject>(nullptr, *DefaultValue);
        if (!ResolvedClass)
            ResolvedClass = LoadClass<UObject>(nullptr, *ClassPath);
        if (!ResolvedClass)
            ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *DefaultValue);
        if (!ResolvedClass)
            ResolvedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
        if (ResolvedClass)
        {
            Pin->DefaultObject = ResolvedClass;
            Pin->DefaultValue.Empty();
        }
        else
        {
            // Fall back to string â€” will likely fail compile but at least records intent
            Pin->DefaultValue = DefaultValue;
        }
    }
    else if (PinCat == UEdGraphSchema_K2::PC_Object || PinCat == UEdGraphSchema_K2::PC_SoftObject)
    {
        UObject* ResolvedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *DefaultValue);
        if (ResolvedObj)
        {
            Pin->DefaultObject = ResolvedObj;
            Pin->DefaultValue.Empty();
        }
        else
        {
            Pin->DefaultValue = DefaultValue;
        }
    }
    else
    {
        Pin->DefaultValue = DefaultValue;
    }

    // Reconstruct the node so downstream pins reflect the new class (e.g.
    // SpawnActorFromClass grows exposed-on-spawn variable pins after class is set).
    Node->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

FString UGraphBridgeAutomationLibrary::GetNodePins(FString BlueprintPath, FString NodeName, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    UEdGraphNode* Node = nullptr;
    if (!GraphName.IsEmpty())
    {
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph) return TEXT("");
        Node = FindNodeInGraph(Graph, NodeName);
    }
    else
    {
        Node = FindNodeByName(Blueprint, NodeName);
        if (!Node) Node = FindNodeById(Blueprint, NodeName);
    }
    if (!Node) return TEXT("");

    TArray<FString> PinDescs;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        FString Dir = Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT");
        PinDescs.Add(FString::Printf(TEXT("%s:%s"), *Dir, *Pin->PinName.ToString()));
    }
    return FString::Join(PinDescs, TEXT(","));
}

// ---------------------------------------------------------------------------
// GetPinConnections
// Command: GET_PIN_CONNECTIONS|BPPath|NodeGUID|PinName
//
// Returns comma-separated "NODEGUID:PinName" entries, one for each pin
// linked to the requested pin (UEdGraphPin::LinkedTo). Direction doesn't
// matter — output pins list their input targets and vice versa.
// Returns "ERR:..." on failure (bad Blueprint/node/pin). An empty string
// (not prefixed with ERR:) means the pin was found but has no connections.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetPinConnections(
    FString BlueprintPath, FString NodeId, FString PinName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(
            TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UEdGraphPin* Pin = Node->FindPin(PinName);
    if (!Pin)
    {
        TArray<FString> Names;
        for (UEdGraphPin* P : Node->Pins)
            if (P) Names.Add(FString::Printf(TEXT("%s(%s)"),
                *P->PinName.ToString(), P->Direction == EGPD_Output ? TEXT("OUT") : TEXT("IN")));
        return FString::Printf(TEXT("ERR:Pin '%s' not found on node '%s'. Available pins: %s"),
            *PinName, *NodeId, *FString::Join(Names, TEXT(", ")));
    }

    TArray<FString> Entries;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
    {
        if (!Linked) continue;
        UEdGraphNode* OwningNode = Linked->GetOwningNode();
        Entries.Add(FString::Printf(TEXT("%s:%s"),
            *OwningNode->NodeGuid.ToString(), *Linked->PinName.ToString()));
    }
    return FString::Join(Entries, TEXT(","));
}

// ---------------------------------------------------------------------------
// GetPinDefault
// Command: GET_PIN_DEFAULT|BPPath|NodeGUID|PinName
//
// Mirrors SetPinDefault's storage rules in reverse: object/class pins store
// their value in DefaultObject (not DefaultValue), so those categories return
// DefaultObject's asset path instead. Returns "ERR:..." on failure, empty
// string (not an error) if the pin has no default set.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetPinDefault(
    FString BlueprintPath, FString NodeId, FString PinName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    // See SetPinDefault — same FindNodeById→FindNodeAnywhere fix, for the
    // same reason (function graphs / AnimGraph / state machines).
    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(
            TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UEdGraphPin* Pin = Node->FindPin(PinName);
    if (!Pin)
    {
        TArray<FString> Names;
        for (UEdGraphPin* P : Node->Pins)
            if (P) Names.Add(FString::Printf(TEXT("%s(%s)"),
                *P->PinName.ToString(), P->Direction == EGPD_Output ? TEXT("OUT") : TEXT("IN")));
        return FString::Printf(TEXT("ERR:Pin '%s' not found on node '%s'. Available pins: %s"),
            *PinName, *NodeId, *FString::Join(Names, TEXT(", ")));
    }

    const FName PinCat = Pin->PinType.PinCategory;
    if (PinCat == UEdGraphSchema_K2::PC_Class  || PinCat == UEdGraphSchema_K2::PC_SoftClass ||
        PinCat == UEdGraphSchema_K2::PC_Object || PinCat == UEdGraphSchema_K2::PC_SoftObject)
    {
        if (Pin->DefaultObject)
            return Pin->DefaultObject->GetPathName();
        // Soft references (and unresolved literals) fall back to the string value.
        return Pin->DefaultValue;
    }

    return Pin->DefaultValue;
}

bool UGraphBridgeAutomationLibrary::CompileBlueprint(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    // Attach a results log so failed compiles are actually reported as
    // failures — CompileBlueprint() alone doesn't return a status, so
    // without this every call reported success regardless of outcome.
    FCompilerResultsLog ResultsLog;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);
    return ResultsLog.NumErrors == 0;
}

// ---------------------------------------------------------------------------
// GetCompileErrors
// Command: GET_COMPILE_ERRORS|BPPath
//
// Compiles the Blueprint with a local FCompilerResultsLog attached so we can
// read back exactly what the compiler reported, instead of the simple
// pass/fail that COMPILE gives. Messages carry severity via GetSeverity();
// PerformanceWarning is folded into WARNING since callers only distinguish
// two buckets. Info-level notes are skipped — they're not actionable.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetCompileErrors(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    FCompilerResultsLog ResultsLog;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &ResultsLog);

    TArray<FString> Entries;
    for (const TSharedRef<FTokenizedMessage>& Msg : ResultsLog.Messages)
    {
        const EMessageSeverity::Type Sev = Msg->GetSeverity();
        if (Sev != EMessageSeverity::Error &&
            Sev != EMessageSeverity::Warning &&
            Sev != EMessageSeverity::PerformanceWarning)
            continue; // skip Info notes — not actionable

        FString Text = Msg->ToText().ToString();
        // Sanitise so the pipe-delimited response doesn't break on embedded
        // pipes/newlines from the compiler's tokenized message text.
        Text.ReplaceInline(TEXT("|"), TEXT(" "));
        Text.ReplaceInline(TEXT("\n"), TEXT(" "));
        Text.ReplaceInline(TEXT("\r"), TEXT(""));

        const TCHAR* Prefix = (Sev == EMessageSeverity::Error) ? TEXT("ERROR") : TEXT("WARNING");
        Entries.Add(FString::Printf(TEXT("%s:%s"), Prefix, *Text));
    }

    return Entries.Num() > 0 ? FString::Join(Entries, TEXT("|")) : TEXT("CLEAN");
}

// ---------------------------------------------------------------------------
// SetAnimClass â€” sets the AnimClass (AnimBlueprint) on the named SkeletalMesh
// component template inside a Blueprint's SCS, then recompiles.
// Command: SET_ANIM_CLASS|BPPath|ComponentName|AnimBPPath
// ComponentName defaults to "CharacterMesh0" if empty/omitted.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetAnimClass(
    FString BlueprintPath, FString ComponentName, FString AnimBPPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    // Resolve the AnimBP generated class
    FString AnimClassPath = AnimBPPath;
    if (!AnimClassPath.EndsWith(TEXT("_C")))
        AnimClassPath += TEXT("_C");
    UClass* AnimClass = LoadClass<UAnimInstance>(nullptr, *AnimClassPath);
    if (!AnimClass)
        return FString::Printf(TEXT("ERR:Could not resolve AnimBP class from '%s'"), *AnimBPPath);

    // Walk the SCS to find the named SkeletalMeshComponent template
    if (ComponentName.IsEmpty())
        ComponentName = TEXT("CharacterMesh0");

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
        return TEXT("ERR:Blueprint has no SimpleConstructionScript");

    // First pass: search the SCS (Blueprint-added components)
    for (USCS_Node* Node : SCS->GetAllNodes())
    {
        if (!Node) continue;
        UActorComponent* Template = Node->ComponentTemplate;
        if (!Template) continue;
        FString NodeName = Node->GetVariableName().ToString();
        if (!NodeName.Equals(ComponentName, ESearchCase::IgnoreCase))
            continue;
        USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Template);
        if (!SkelComp)
            return FString::Printf(TEXT("ERR:Component '%s' is not a SkeletalMeshComponent"), *ComponentName);
        const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "SetAnimClass", "GraphBridge: Set Anim Class"));
        SkelComp->Modify();
        SkelComp->AnimClass = AnimClass;
        Blueprint->Modify();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        return TEXT("");
    }

    // Second pass: search inherited C++ component templates on the CDO
    // (e.g. ACharacter::Mesh which lives on the parent class, not in the SCS)
    UClass* GenClass = Blueprint->GeneratedClass;
    if (GenClass)
    {
        UObject* CDO = GenClass->GetDefaultObject();
        if (CDO)
        {
            for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            {
                FObjectProperty* Prop = *PropIt;
                if (!Prop->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
                    continue;
                UObject* PropVal = Prop->GetObjectPropertyValue_InContainer(CDO);
                USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(PropVal);
                if (!SkelComp)
                    continue;
                const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "SetAnimClass", "GraphBridge: Set Anim Class"));
                SkelComp->Modify();
                SkelComp->AnimClass = AnimClass;
                Blueprint->Modify();
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
                return TEXT("");
            }
        }
    }

    // List available names from both SCS and CDO properties
    TArray<FString> Names;
    for (USCS_Node* Node : SCS->GetAllNodes())
        if (Node) Names.Add(Node->GetVariableName().ToString());
    if (GenClass)
        for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            if (Cast<USkeletalMeshComponent>((*PropIt)->GetObjectPropertyValue_InContainer(GenClass->GetDefaultObject())))
                Names.Add((*PropIt)->GetName());
    return FString::Printf(TEXT("ERR:Component '%s' not found. Available SkeletalMesh components: %s"),
        *ComponentName, *FString::Join(Names, TEXT(", ")));
}

bool UGraphBridgeAutomationLibrary::SaveBlueprint(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    UPackage* Package = Blueprint->GetOutermost();
    if (!Package) return false;

    Package->MarkPackageDirty();

    const FString PackageName     = Package->GetName();
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    const bool bSuccess = UPackage::SavePackage(Package, Blueprint, *PackageFilename, SaveArgs);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SaveBlueprint: %s -> %s"),
        *BlueprintPath, bSuccess ? TEXT("saved") : TEXT("FAILED"));

    return bSuccess;
}

FEdGraphPinType UGraphBridgeAutomationLibrary::ResolveTypeString(const FString& InTypeString)
{
    // Array container support: a trailing "[]" (e.g. "class:Character[]")
    // requests an array of the base type. No existing caller used a type
    // string ending in "[]" before this was added, so this is purely
    // additive — added because ADD_VARIABLE/SPAWN_VARIABLE had no way to
    // create an array-typed member variable at all (confirmed live: every
    // branch below only ever sets PinCategory, never ContainerType, so
    // container was always Array's default None/single-value).
    FString TypeString = InTypeString;
    bool bIsArray = false;
    if (TypeString.EndsWith(TEXT("[]")))
    {
        TypeString = TypeString.LeftChop(2);
        bIsArray = true;
    }

    FEdGraphPinType PinType;
    PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;

    if      (TypeString == TEXT("bool"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean; }
    else if (TypeString == TEXT("int32"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Int; }
    else if (TypeString == TEXT("int64"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Int64; }
    else if (TypeString == TEXT("float"))
    {
        PinType.PinCategory    = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    }
    else if (TypeString == TEXT("double"))
    {
        PinType.PinCategory    = UEdGraphSchema_K2::PC_Real;
        PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
    }
    else if (TypeString == TEXT("byte"))          { PinType.PinCategory = UEdGraphSchema_K2::PC_Byte; }
    else if (TypeString == TEXT("FString"))       { PinType.PinCategory = UEdGraphSchema_K2::PC_String; }
    else if (TypeString == TEXT("FName"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Name; }
    else if (TypeString == TEXT("FText"))         { PinType.PinCategory = UEdGraphSchema_K2::PC_Text; }
    else if (TypeString == TEXT("FVector"))
    {
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
    }
    else if (TypeString == TEXT("FVector2D"))
    {
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FVector2D>::Get();
    }
    else if (TypeString == TEXT("FRotator"))
    {
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
    }
    else if (TypeString == TEXT("FTransform"))
    {
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
    }
    else if (TypeString == TEXT("FLinearColor"))
    {
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Struct;
        PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
    }
    else if (TypeString.StartsWith(TEXT("object:")))
    {
        FString ClassName = TypeString.Mid(7);
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Object;
        PinType.PinSubCategoryObject = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
    }
    else if (TypeString.StartsWith(TEXT("class:")))
    {
        FString ClassName = TypeString.Mid(6);
        PinType.PinCategory          = UEdGraphSchema_K2::PC_Class;
        PinType.PinSubCategoryObject = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
    }

    if (bIsArray && PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
        PinType.ContainerType = EPinContainerType::Array;

    return PinType;
}

FString UGraphBridgeAutomationLibrary::SpawnVariable(FString BlueprintPath,
    FString VarName, FString TypeString, FString Category)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    FEdGraphPinType PinType = ResolveTypeString(TypeString);
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SpawnVariable: unrecognised type '%s'"), *TypeString);
        return TEXT("");
    }

    FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, VarName);

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "SpawnVariable", "GraphBridge: Add Variable ({0})"),
                      FText::FromString(VarName)));
    Blueprint->Modify();

    FBlueprintEditorUtils::AddMemberVariable(Blueprint, UniqueName, PinType);

    int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, UniqueName);
    if (VarIdx != INDEX_NONE && !Category.IsEmpty())
        Blueprint->NewVariables[VarIdx].Category = FText::FromString(Category);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return UniqueName.ToString();
}

bool UGraphBridgeAutomationLibrary::SetVariableDefault(FString BlueprintPath,
    FString VarName, FString DefaultValue)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VarName));
    if (VarIdx == INDEX_NONE) return false;

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "SetVarDefault", "GraphBridge: Set Variable Default"));
    Blueprint->Modify();
    Blueprint->NewVariables[VarIdx].DefaultValue = DefaultValue;
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

FString UGraphBridgeAutomationLibrary::ListNodes(FString BlueprintPath, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    TArray<UEdGraph*> GraphsToList;
    if (!GraphName.IsEmpty())
    {
        UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
        if (!Graph) return TEXT("");
        GraphsToList.Add(Graph);
    }
    else
    {
        GraphsToList = Blueprint->UbergraphPages;
    }

    TArray<FString> Entries;
    for (UEdGraph* Graph : GraphsToList)
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            FString Guid    = Node->NodeGuid.ToString();
            FString Title   = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
            FString Comment = Node->NodeComment;
            FString Class   = Node->GetClass()->GetName();
            // Sanitise title/comment — strip newlines so the JSON response
            // stays on one line and the Python json.loads() doesn't choke.
            Title.ReplaceInline(TEXT("\n"), TEXT(" "));
            Title.ReplaceInline(TEXT("\r"), TEXT(""));
            Comment.ReplaceInline(TEXT("\n"), TEXT(" "));
            Comment.ReplaceInline(TEXT("\r"), TEXT(""));
            // Format: GUID~Title~Comment~ClassName
            Entries.Add(FString::Printf(TEXT("%s~%s~%s~%s"),
                *Guid, *Title, *Comment, *Class));
        }
    }
    return FString::Join(Entries, TEXT("|"));
}

FString UGraphBridgeAutomationLibrary::FindNodeClass(FString PartialName)
{
    TArray<FString> Matches;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->IsChildOf(UK2Node::StaticClass()) &&
            It->GetName().Contains(PartialName, ESearchCase::IgnoreCase))
        {
            Matches.Add(It->GetPathName());
        }
    }
    return FString::Join(Matches, TEXT(","));
}

FString UGraphBridgeAutomationLibrary::ListAssets(FString Filter)
{
    // Filter can be:
    //   empty           — return all supported asset types
    //   a path fragment — e.g. "/Game/Characters"
    //   a class name    — e.g. "BlendSpace" or "AnimSequence"
    //
    // Supported asset classes (covers the most common AI automation targets):
    //   Blueprint, AnimBlueprint, BlendSpace, BlendSpace1D,
    //   AnimSequence, AnimMontage, AnimComposite, PoseAsset,
    //   SkeletalMesh, StaticMesh, Material, MaterialInstance,
    //   Texture2D, SoundCue, SoundWave, DataAsset, DataTable,
    //   PhysicsAsset, Skeleton

    static const TArray<FTopLevelAssetPath> AssetClasses = {
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("Blueprint")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("AnimBlueprint")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("BlendSpace")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("BlendSpace1D")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("AnimSequence")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("AnimMontage")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("AnimComposite")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("PoseAsset")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("SkeletalMesh")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("StaticMesh")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("Material")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("MaterialInstanceConstant")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("Texture2D")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("SoundCue")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("SoundWave")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("DataAsset")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("DataTable")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("PhysicsAsset")),
        FTopLevelAssetPath(TEXT("/Script/Engine"),         TEXT("Skeleton")),
    };

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    TArray<FAssetData> Assets;
    for (const FTopLevelAssetPath& ClassPath : AssetClasses)
    {
        TArray<FAssetData> ClassAssets;
        AssetRegistry.Get().GetAssetsByClass(ClassPath, ClassAssets, /*bSearchSubClasses=*/true);
        Assets.Append(ClassAssets);
    }

    TArray<FString> Paths;
    for (const FAssetData& Asset : Assets)
    {
        FString Path = Asset.GetObjectPathString();
        FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
        // Match against path OR class name so callers can do LIST_ASSETS|BlendSpace
        if (Filter.IsEmpty() ||
            Path.Contains(Filter, ESearchCase::IgnoreCase) ||
            ClassName.Contains(Filter, ESearchCase::IgnoreCase))
        {
            // Format: Path|ClassName so the AI knows what type each asset is
            Paths.Add(FString::Printf(TEXT("%s|%s"), *Path, *ClassName));
        }
    }
    return FString::Join(Paths, TEXT(","));
}

bool UGraphBridgeAutomationLibrary::SetInputAction(FString BlueprintPath,
    FString NodeId, FString InputActionPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node) return false;

    UInputAction* Action = LoadObject<UInputAction>(nullptr, *InputActionPath);
    if (!Action)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SetInputAction: could not load asset '%s'"), *InputActionPath);
        return false;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "SetInputAction", "GraphBridge: Set Input Action"));
    Node->Modify();

    // Handle K2Node_InputAction (legacy)
    if (UK2Node_InputAction* LegacyNode = Cast<UK2Node_InputAction>(Node))
    {
        LegacyNode->InputActionName = Action->GetFName();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }

    // Handle K2Node_EnhancedInputAction — stores a TObjectPtr<UInputAction>
    // in a property called "InputAction". Use reflection so we don't need
    // a private header include.
    FObjectProperty* ActionProp = FindFProperty<FObjectProperty>(
        Node->GetClass(), TEXT("InputAction"));
    if (ActionProp)
    {
        ActionProp->SetObjectPropertyValue_InContainer(Node, Action);
        // Reconstruct the node so its pins update to match the new action
        Node->ReconstructNode();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }

    // Last resort — try InputActionName FName property (some engine versions)
    FNameProperty* NameProp = FindFProperty<FNameProperty>(
        Node->GetClass(), TEXT("InputActionName"));
    if (NameProp)
    {
        NameProp->SetPropertyValue_InContainer(Node, Action->GetFName());
        Node->ReconstructNode();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }

    UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SetInputAction: node class '%s' has no recognised InputAction property"),
           *Node->GetClass()->GetName());
    return false;
}

// ---------------------------------------------------------------------------
// Returns empty string on success, or a human-readable error reason on failure.
FString UGraphBridgeAutomationLibrary::SetFunctionRef(FString BlueprintPath,
    FString NodeId, FString ClassName, FString FunctionName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(TEXT("Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
    if (!CallNode)
        return FString::Printf(TEXT("Node '%s' is not a K2Node_CallFunction (it is %s)"),
            *NodeId, *Node->GetClass()->GetName());

    // Try exact class name, then with U prefix
    UClass* TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None);
    if (!TargetClass)
        TargetClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ClassName), EFindFirstObjectOptions::None);
    if (!TargetClass)
        return FString::Printf(TEXT("Class not found: '%s'. Try FIND_NODE_CLASS to discover the correct name"), *ClassName);

    UFunction* Function = TargetClass->FindFunctionByName(*FunctionName);
    if (!Function)
    {
        // List available UFUNCTIONs to help the AI self-correct
        TArray<FString> Available;
        for (TFieldIterator<UFunction> It(TargetClass); It; ++It)
            Available.Add(It->GetName());
        Available.Sort();
        FString AvailableStr = Available.Num() > 20
            ? FString::Join(TArrayView<FString>(Available.GetData(), 20), TEXT(", ")) + TEXT("...")
            : FString::Join(Available, TEXT(", "));
        return FString::Printf(TEXT("Function '%s' not found on '%s'. Available: %s"),
            *FunctionName, *ClassName, *AvailableStr);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetFunctionRef", "GraphBridge: Set Function Reference"));
    CallNode->Modify();
    CallNode->SetFromFunction(Function);
    CallNode->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// SetVariableRef — binds a K2Node_VariableGet/Set to a named Blueprint variable
// and reconstructs its pins so the output type is resolved before connection.
// Command: SET_VARIABLE_REF|BPPath|NodeId|VarName
// ---------------------------------------------------------------------------
bool UGraphBridgeAutomationLibrary::SetVariableRef(FString BlueprintPath,
    FString NodeId, FString VarName, FString& OutError)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) { OutError = FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath); return false; }

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node) { OutError = FString::Printf(TEXT("Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId); return false; }

    UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
    if (!VarNode)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SetVariableRef: node is not a K2Node_Variable"));
        return false;
    }

    UClass* GeneratedClass = Blueprint->GeneratedClass;

    // --- Strategy 1: Blueprint member variable (in NewVariables list) ---
    FProperty* VarProperty = nullptr;
    if (GeneratedClass)
        VarProperty = FindFProperty<FProperty>(GeneratedClass, *VarName);

    // --- Strategy 2: SCS component variable (AddComponent in construction script) ---
    // SCS components live on the parent class or are registered as SCS nodes.
    // AnimBlueprints do not have a SimpleConstructionScript — guard against null.
    if (!VarProperty && Blueprint->SimpleConstructionScript)
    {
        for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (SCSNode && SCSNode->GetVariableName().ToString() == VarName)
            {
                const FScopedTransaction Transaction(
                    NSLOCTEXT("GraphBridge", "SetVariableRef", "GraphBridge: Set Variable Reference"));
                VarNode->Modify();

                FMemberReference MemberRef;
                MemberRef.SetSelfMember(SCSNode->GetVariableName());
                VarNode->VariableReference = MemberRef;
                VarNode->ReconstructNode();
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                return true;
            }
        }
    }

    // --- Strategy 3: C++ property on parent class chain ---
    if (!VarProperty)
    {
        for (UClass* C = Blueprint->ParentClass; C && !VarProperty; C = C->GetSuperClass())
            VarProperty = FindFProperty<FProperty>(C, *VarName);
    }

    if (!VarProperty)
    {
        // Build a list of available variables to help AI self-correct
        TArray<FString> Available;
        // Blueprint member variables
        for (const FBPVariableDescription& Var : Blueprint->NewVariables)
            Available.Add(Var.VarName.ToString());
        // SCS component variables
        if (Blueprint->SimpleConstructionScript)
            for (USCS_Node* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
                if (SCSNode) Available.Add(SCSNode->GetVariableName().ToString());
        // C++ parent class properties (one level only to keep list short)
        if (Blueprint->ParentClass)
            for (TFieldIterator<FProperty> It(Blueprint->ParentClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
                Available.Add(It->GetName());
        Available.Sort();
        FString AvailableStr = Available.Num() > 20
            ? FString::Join(TArrayView<FString>(Available.GetData(), 20), TEXT(", ")) + TEXT("...")
            : FString::Join(Available, TEXT(", "));
        OutError = FString::Printf(TEXT("Variable '%s' not found. Available: %s"), *VarName, *AvailableStr);
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SetVariableRef: %s"), *OutError);
        return false;
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetVariableRef", "GraphBridge: Set Variable Reference"));
    VarNode->Modify();

    // For self-context variables (accessed from within this Blueprint),
    // bIsConsideredSelfContext must be true so the compiler resolves the
    // reference correctly. Using false produces the "invalid target" warning.
    FMemberReference MemberRef;
    MemberRef.SetFromField<FProperty>(VarProperty, /*bIsConsideredSelfContext=*/true,
        GeneratedClass ? GeneratedClass : static_cast<UClass*>(Blueprint->ParentClass));
    VarNode->VariableReference = MemberRef;
    VarNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

// ---------------------------------------------------------------------------
// SetExternalVariableRef — binds a K2Node_Variable to a property on an
// ARBITRARY class, not just this Blueprint's own class hierarchy (see
// SET_EXTERNAL_VARIABLE_REF comment at the dispatch site for why this is
// needed alongside SetVariableRef, which is self-context only).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetExternalVariableRef(FString BlueprintPath,
    FString NodeId, FString OwnerClassName, FString VarName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
    if (!VarNode)
        return TEXT("ERR:node is not a K2Node_Variable — spawn a K2Node_VariableGet or K2Node_VariableSet first");

    UClass* OwnerClass = FindFirstObject<UClass>(*OwnerClassName, EFindFirstObjectOptions::None);
    if (!OwnerClass)
        OwnerClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *OwnerClassName),
            EFindFirstObjectOptions::None);
    if (!OwnerClass)
        return FString::Printf(TEXT("ERR:Class '%s' not found"), *OwnerClassName);

    FProperty* VarProperty = FindFProperty<FProperty>(OwnerClass, *VarName);
    if (!VarProperty)
        return FString::Printf(TEXT("ERR:Property '%s' not found on class '%s'"), *VarName, *OwnerClassName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetExternalVariableRef", "GraphBridge: Set External Variable Reference"));
    VarNode->Modify();

    // bIsConsideredSelfContext=false is the whole point here — it produces a
    // node with a "Target" input pin (wire it via CONNECT_PINS to whatever
    // object reference owns the property, e.g. a Get CameraBoom node),
    // exactly matching what dragging off a component pin and choosing
    // "Set <Property>" does in the Blueprint editor.
    FMemberReference MemberRef;
    MemberRef.SetFromField<FProperty>(VarProperty, /*bIsConsideredSelfContext=*/false, OwnerClass);
    VarNode->VariableReference = MemberRef;
    VarNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// AddComponent — adds a component USCS_Node to a Blueprint's SCS and compiles.
// Command: ADD_COMPONENT|BPPath|ComponentClass|ComponentName
//
// ComponentClass resolution order:
//   1. Exact C++ UClass name       e.g. "ProjectileMovementComponent"
//   2. With U prefix               e.g. "UProjectileMovementComponent"
//   3. Partial C++ name match      e.g. "Projectile" matches UProjectileMovementComponent
//   4. Blueprint asset path        e.g. "/Game/Components/BP_MyComp.BP_MyComp"
//
// Pattern confirmed from Epic community:
//   USCS_Node* Node = SCS->CreateNode(Class, Name);
//   SCS->AddNode(Node);
//   FKismetEditorUtilities::CompileBlueprint(BP);
//   Cast<UMyComp>(Node->ComponentTemplate) to set properties.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddComponent(FString BlueprintPath,
    FString ComponentClass, FString ComponentName, FString ParentComponentName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath);

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
        return TEXT("Blueprint has no SimpleConstructionScript — AnimBlueprints do not support components");

    // Check if a component with this name already exists
    for (USCS_Node* Existing : SCS->GetAllNodes())
    {
        if (Existing && Existing->GetVariableName().ToString() == ComponentName)
            return FString::Printf(TEXT("Component '%s' already exists — use SET_VARIABLE_REF to reference it"), *ComponentName);
    }

    // --- Resolve component class ---
    UClass* CompClass = nullptr;

    // 1. Try as Blueprint asset path first (contains '/')
    if (ComponentClass.Contains(TEXT("/")))
    {
        UBlueprint* CompBP = LoadObject<UBlueprint>(nullptr, *ComponentClass);
        if (CompBP && CompBP->GeneratedClass)
            CompClass = CompBP->GeneratedClass;
        else
            return FString::Printf(TEXT("Blueprint component asset not found at '%s'"), *ComponentClass);
    }
    else
    {
        // 2. Exact C++ class name
        CompClass = FindFirstObject<UClass>(*ComponentClass, EFindFirstObjectOptions::None);

        // 3. With U prefix
        if (!CompClass)
            CompClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *ComponentClass),
                EFindFirstObjectOptions::None);

        // 4. Partial match — iterate all UActorComponent subclasses
        if (!CompClass)
        {
            TArray<FString> Matches;
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (!It->IsChildOf(UActorComponent::StaticClass())) continue;
                if (It->HasAnyClassFlags(CLASS_Abstract)) continue;
                if (It->GetName().Contains(ComponentClass, ESearchCase::IgnoreCase))
                {
                    Matches.Add(It->GetName());
                    if (!CompClass) CompClass = *It;
                }
            }
            if (Matches.Num() > 1)
            {
                Matches.Sort();
                CompClass = FindFirstObject<UClass>(*Matches[0], EFindFirstObjectOptions::None);
                UE_LOG(LogGraphBridge, Warning,
                    TEXT("GraphBridge AddComponent: '%s' matched %d classes, using '%s'. All: %s"),
                    *ComponentClass, Matches.Num(), *Matches[0], *FString::Join(Matches, TEXT(", ")));
            }
        }

        if (!CompClass)
        {
            // Build helpful list of common component names
            TArray<FString> Available;
            for (TObjectIterator<UClass> It; It; ++It)
                if (It->IsChildOf(UActorComponent::StaticClass()) &&
                    !It->HasAnyClassFlags(CLASS_Abstract) &&
                    It->GetName().StartsWith(TEXT("U")))
                    Available.Add(It->GetName().Mid(1)); // strip U prefix for readability
            Available.Sort();
            FString AvailableStr = Available.Num() > 20
                ? FString::Join(TArrayView<FString>(Available.GetData(), 20), TEXT(", ")) + TEXT("...")
                : FString::Join(Available, TEXT(", "));
            return FString::Printf(
                TEXT("Component class '%s' not found. Common components: %s"),
                *ComponentClass, *AvailableStr);
        }
    }

    // Verify it's actually a component class
    if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
        return FString::Printf(TEXT("Class '%s' is not a UActorComponent subclass"),
            *CompClass->GetName());

    // --- Create and add the SCS node ---
    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddComponent", "GraphBridge: Add Component"));
    Blueprint->Modify();

    USCS_Node* NewNode = SCS->CreateNode(CompClass, *ComponentName);
    if (!NewNode)
        return FString::Printf(TEXT("SCS->CreateNode failed for class '%s'"), *CompClass->GetName());

    // If a parent component name is specified, attach to it; otherwise add at root
    if (!ParentComponentName.IsEmpty())
    {
        USCS_Node* ParentNode = nullptr;
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (Node->GetVariableName().ToString() == ParentComponentName)
            {
                ParentNode = Node;
                break;
            }
        }
        if (ParentNode)
            ParentNode->AddChildNode(NewNode);
        else
            SCS->AddNode(NewNode); // fallback to root if parent not found
    }
    else
    {
        SCS->AddNode(NewNode);
    }

    // Compile so the node becomes a real SCS variable accessible via SET_VARIABLE_REF
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddComponent: added '%s' (%s) to %s"),
        *ComponentName, *CompClass->GetName(), *BlueprintPath);

    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// SetEventRef — binds a K2Node_Event to a named function on the parent class
// chain and reconstructs its pins so exec wiring works correctly.
// Command: SET_EVENT_REF|BPPath|NodeId|FunctionName
// e.g.    SET_EVENT_REF|/Game/BP_X.BP_X|<guid>|ReceiveBeginPlay
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetEventRef(FString BlueprintPath,
    FString NodeId, FString FunctionName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(TEXT("Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
    if (!EventNode)
        return FString::Printf(TEXT("Node is not a K2Node_Event (it is %s)"),
            *Node->GetClass()->GetName());

    // Walk the parent class chain to find the named function
    UFunction* Function = nullptr;
    for (UClass* C = Blueprint->ParentClass; C && !Function; C = C->GetSuperClass())
        Function = C->FindFunctionByName(*FunctionName);

    if (!Function)
    {
        // List available events on parent class to aid self-correction
        TArray<FString> Available;
        for (UClass* C = Blueprint->ParentClass; C; C = C->GetSuperClass())
            for (TFieldIterator<UFunction> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
                if (It->HasAnyFunctionFlags(FUNC_BlueprintEvent))
                    Available.AddUnique(It->GetName());
        Available.Sort();
        FString AvailableStr = Available.Num() > 20
            ? FString::Join(TArrayView<FString>(Available.GetData(), 20), TEXT(", ")) + TEXT("...")
            : FString::Join(Available, TEXT(", "));
        return FString::Printf(TEXT("Function '%s' not found on parent class chain. Available events: %s"),
            *FunctionName, *AvailableStr);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetEventRef", "GraphBridge: Set Event Reference"));
    EventNode->Modify();
    EventNode->EventReference.SetFromField<UFunction>(Function, false);
    EventNode->bOverrideFunction = true;
    EventNode->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// SetCustomEventName — names a K2Node_CustomEvent so it becomes a real,
// externally-callable UFunction after compile. See SET_CUSTOM_EVENT_NAME
// dispatch comment for why this is needed alongside SetEventRef.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetCustomEventName(FString BlueprintPath,
    FString NodeId, FString EventName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(Node);
    if (!EventNode)
        return FString::Printf(TEXT("ERR:Node is not a K2Node_CustomEvent (it is %s)"),
            *Node->GetClass()->GetName());

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetCustomEventName", "GraphBridge: Set Custom Event Name"));
    EventNode->Modify();
    EventNode->CustomFunctionName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, EventName);
    EventNode->ReconstructNode();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// AddArrayPin — appends one more wildcard input pin to a K2Node_MakeArray,
// mirroring the "+" affordance in the real Blueprint editor. See
// ADD_ARRAY_PIN dispatch comment for why this is needed.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddArrayPin(FString BlueprintPath, FString NodeId)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node)
        return FString::Printf(TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeId);

    UK2Node_MakeArray* MakeArrayNode = Cast<UK2Node_MakeArray>(Node);
    if (!MakeArrayNode)
        return FString::Printf(TEXT("ERR:Node is not a K2Node_MakeArray (it is %s)"),
            *Node->GetClass()->GetName());

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddArrayPin", "GraphBridge: Add Array Pin"));
    MakeArrayNode->Modify();
    MakeArrayNode->AddInputPin();
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// ListBlendSpaces
// Command: LIST_BLENDSPACES|OptionalPathFilter
// Returns comma-separated list of "AssetPath|ClassName" for every BlendSpace
// and BlendSpace1D in the project (or matching the filter).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListBlendSpaces(FString Filter)
{
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    TArray<FAssetData> Assets;
    AssetRegistry.Get().GetAssetsByClass(
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("BlendSpace")), Assets, true);

    TArray<FAssetData> Assets1D;
    AssetRegistry.Get().GetAssetsByClass(
        FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("BlendSpace1D")), Assets1D, true);
    Assets.Append(Assets1D);

    TArray<FString> Paths;
    for (const FAssetData& Asset : Assets)
    {
        FString Path = Asset.GetObjectPathString();
        if (Filter.IsEmpty() || Path.Contains(Filter, ESearchCase::IgnoreCase))
            Paths.Add(FString::Printf(TEXT("%s|%s"),
                *Path, *Asset.AssetClassPath.GetAssetName().ToString()));
    }
    return FString::Join(Paths, TEXT(","));
}


// ===========================================================================
// Generic reflection commands — work on ANY UObject asset
// ===========================================================================

// ---------------------------------------------------------------------------
// ListAssetProperties
// Command: LIST_ASSET_PROPERTIES|AssetPath
//
// Returns pipe-delimited entries, one per editable UPROPERTY:
//   PropertyName~TypeName~CurrentValue|PropertyName~TypeName~CurrentValue|...
//
// Only properties with at least one Edit* or Visible* specifier are included
// so the list stays focused on things the AI should actually touch.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListAssetProperties(FString AssetPath)
{
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
        return FString::Printf(TEXT("ERR:Asset not found at '%s'"), *AssetPath);

    UClass* Class = Asset->GetClass();
    TArray<FString> Entries;

    for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;

        // Only expose properties that are visible/editable in the editor
        const uint64 EditFlags = CPF_Edit | CPF_EditConst;
        if (!(Prop->PropertyFlags & EditFlags))
            continue;

        // Export current value to string via UE's built-in text serializer
        FString ValueStr;
        const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Asset);
        Prop->ExportTextItem_InContainer(ValueStr, Asset, nullptr, nullptr, PPF_None);

        // Sanitise: strip newlines so the JSON envelope stays on one line
        ValueStr.ReplaceInline(TEXT("\n"), TEXT(" "));
        ValueStr.ReplaceInline(TEXT("\r"), TEXT(""));

        FString TypeName = Prop->GetCPPType();
        Entries.Add(FString::Printf(TEXT("%s~%s~%s"),
            *Prop->GetName(), *TypeName, *ValueStr));
    }

    if (Entries.IsEmpty())
        return FString::Printf(
            TEXT("ERR:No editable properties found on '%s' (class: %s)"),
            *AssetPath, *Class->GetName());

    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
// GetAssetProperty
// Command: GET_ASSET_PROPERTY|AssetPath|PropertyName
//
// Returns the property value as a string (same format ExportText produces).
// Returns ERR:... on failure.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetAssetProperty(FString AssetPath, FString PropertyName)
{
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
        return FString::Printf(TEXT("ERR:Asset not found at '%s'"), *AssetPath);

    FProperty* Prop = FindFProperty<FProperty>(Asset->GetClass(), *PropertyName);
    if (!Prop)
    {
        // Help the AI self-correct with a list of available property names
        TArray<FString> Available;
        for (TFieldIterator<FProperty> It(Asset->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
            if ((*It)->PropertyFlags & (CPF_Edit | CPF_EditConst))
                Available.Add((*It)->GetName());
        Available.Sort();
        return FString::Printf(TEXT("ERR:Property '%s' not found. Available: %s"),
            *PropertyName, *FString::Join(Available, TEXT(", ")));
    }

    FString ValueStr;
    Prop->ExportTextItem_InContainer(ValueStr, Asset, nullptr, nullptr, PPF_None);
    return ValueStr;
}

// ---------------------------------------------------------------------------
// SetAssetProperty
// Command: SET_ASSET_PROPERTY|AssetPath|PropertyName|Value
//
// Imports a string value into the property using UE's text import pipeline,
// which handles every reflected type: bool, int, float, FString, FName,
// FVector, FRotator, FLinearColor, enums, object references, etc.
//
// For object references pass the full asset path e.g.
//   SET_ASSET_PROPERTY|/Game/Foo.Foo|MyMesh|/Game/Meshes/SM_Rock.SM_Rock
//
// Returns empty string on success, ERR:... on failure.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetAssetProperty(FString AssetPath,
    FString PropertyName, FString Value)
{
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
        return FString::Printf(TEXT("ERR:Asset not found at '%s'"), *AssetPath);

    FProperty* Prop = FindFProperty<FProperty>(Asset->GetClass(), *PropertyName);
    if (!Prop)
    {
        TArray<FString> Available;
        for (TFieldIterator<FProperty> It(Asset->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
            if ((*It)->PropertyFlags & CPF_Edit)
                Available.Add((*It)->GetName());
        Available.Sort();
        return FString::Printf(TEXT("ERR:Property '%s' not found. Editable properties: %s"),
            *PropertyName, *FString::Join(Available, TEXT(", ")));
    }

    // Guard against read-only properties
    if (Prop->PropertyFlags & CPF_EditConst)
        return FString::Printf(
            TEXT("ERR:Property '%s' is read-only (EditConst) and cannot be set"),
            *PropertyName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetAssetProp", "GraphBridge: Set Asset Property"));
    Asset->Modify();

    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Asset);

    // ImportText parses the string and writes directly into the property memory.
    // It handles every FProperty subtype natively — no type switch needed.
    const TCHAR* ImportResult = Prop->ImportText_InContainer(
        *Value, Asset, Asset, PPF_None);

    if (!ImportResult)
        return FString::Printf(
            TEXT("ERR:ImportText failed for property '%s' with value '%s'. ")
            TEXT("Check that the value format matches the property type (%s)."),
            *PropertyName, *Value, *Prop->GetCPPType());

    // Notify the editor that this object has changed so details panels refresh
    FPropertyChangedEvent ChangeEvent(Prop, EPropertyChangeType::ValueSet);
    Asset->PostEditChangeProperty(ChangeEvent);
    Asset->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetAssetProperty: %s.%s = %s"),
        *AssetPath, *PropertyName, *Value);

    return TEXT(""); // empty = success
}

// ---------------------------------------------------------------------------
// AnimMontage commands (v1.1)
// ---------------------------------------------------------------------------
//
// Key UE5 types (all in Animation/AnimMontage.h):
//
//   UAnimMontage::CompositeSections   TArray<FCompositeSection>
//     FCompositeSection::SectionName  FName
//     FCompositeSection::GetTime()    float (seconds)
//
//   UAnimMontage::SlotAnimTracks      TArray<FSlotAnimationTrack>
//     FSlotAnimationTrack::SlotName   FName  ("GroupName.SlotName")
//     NOT EditAnywhere — generic reflection can't touch it; dedicated command needed.
//
//   UAnimSequenceBase::Notifies       TArray<FAnimNotifyEvent>
//     FAnimNotifyEvent::Link(asset, time)  positions notify on the timeline
//     FAnimNotifyEvent::Notify             UAnimNotify* (nullptr = marker only)
//
// There is no RemoveAnimCompositeSection API — array is spliced directly.

// ---------------------------------------------------------------------------
// GetMontageInfo
// Command: GET_MONTAGE_INFO|AssetPath
// Returns JSON listing sections, slots, and notifies.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetMontageInfo(FString AssetPath)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    // ---- Sections ----
    FString SectionsJson = TEXT("[");
    for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
    {
        const FCompositeSection& S = Montage->CompositeSections[i];
        if (i > 0) SectionsJson += TEXT(",");
        SectionsJson += FString::Printf(
            TEXT("{\"index\":%d,\"name\":\"%s\",\"startTime\":%.4f}"),
            i, *S.SectionName.ToString(), S.GetTime());
    }
    SectionsJson += TEXT("]");

    // ---- Slots ----
    FString SlotsJson = TEXT("[");
    for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
    {
        if (i > 0) SlotsJson += TEXT(",");
        SlotsJson += FString::Printf(
            TEXT("{\"index\":%d,\"slotName\":\"%s\"}"),
            i, *Montage->SlotAnimTracks[i].SlotName.ToString());
    }
    SlotsJson += TEXT("]");

    // ---- Notifies ----
    FString NotifiesJson = TEXT("[");
    for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
    {
        const FAnimNotifyEvent& N = Montage->Notifies[i];
        FString ClassName = N.Notify ? N.Notify->GetClass()->GetName() : TEXT("None");
        if (i > 0) NotifiesJson += TEXT(",");
        NotifiesJson += FString::Printf(
            TEXT("{\"index\":%d,\"time\":%.4f,\"class\":\"%s\",\"trackIndex\":%d}"),
            i, N.GetTime(), *ClassName, N.TrackIndex);
    }
    NotifiesJson += TEXT("]");

    return FString::Printf(
        TEXT("{\"sections\":%s,\"slots\":%s,\"notifies\":%s}"),
        *SectionsJson, *SlotsJson, *NotifiesJson);
}

// ---------------------------------------------------------------------------
// AddMontageSection
// Command: ADD_MONTAGE_SECTION|AssetPath|SectionName|StartTimeSeconds
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddMontageSection(
    FString AssetPath, FString SectionName, float StartTime)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    if (SectionName.IsEmpty())
        return TEXT("ERR:SectionName cannot be empty");

    const FName SectionFName(*SectionName);

    // Guard: duplicate name
    for (const FCompositeSection& S : Montage->CompositeSections)
        if (S.SectionName == SectionFName)
            return FString::Printf(TEXT("ERR:Section '%s' already exists"), *SectionName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddMontageSection", "GraphBridge: Add Montage Section"));
    Montage->Modify();

    int32 Idx = Montage->AddAnimCompositeSection(SectionFName, StartTime);
    if (Idx == INDEX_NONE)
        return TEXT("ERR:AddAnimCompositeSection returned INDEX_NONE");

    Montage->MarkPackageDirty();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddMontageSection: '%s' at %.4fs (index %d)"),
        *SectionName, StartTime, Idx);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// RemoveMontageSection
// Command: REMOVE_MONTAGE_SECTION|AssetPath|SectionName
// No RemoveAnimCompositeSection API exists — splice the array directly.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::RemoveMontageSection(
    FString AssetPath, FString SectionName)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    const FName SectionFName(*SectionName);
    int32 FoundIdx = INDEX_NONE;
    for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
    {
        if (Montage->CompositeSections[i].SectionName == SectionFName)
        {
            FoundIdx = i;
            break;
        }
    }
    if (FoundIdx == INDEX_NONE)
        return FString::Printf(TEXT("ERR:Section '%s' not found"), *SectionName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "RemoveMontageSection", "GraphBridge: Remove Montage Section"));
    Montage->Modify();
    Montage->CompositeSections.RemoveAt(FoundIdx);
    Montage->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge RemoveMontageSection: removed '%s'"), *SectionName);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetMontageSlot
// Command: SET_MONTAGE_SLOT|AssetPath|SlotIndex|NewSlotName
// SlotName: "GroupName.SlotName" e.g. "DefaultGroup.UpperBody"
// SlotAnimTracks is not EditAnywhere — must be written directly.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetMontageSlot(
    FString AssetPath, int32 SlotIndex, FString NewSlotName)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    if (!Montage->SlotAnimTracks.IsValidIndex(SlotIndex))
        return FString::Printf(
            TEXT("ERR:SlotIndex %d out of range (montage has %d slot(s))"),
            SlotIndex, Montage->SlotAnimTracks.Num());

    if (NewSlotName.IsEmpty())
        return TEXT("ERR:NewSlotName cannot be empty");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetMontageSlot", "GraphBridge: Set Montage Slot"));
    Montage->Modify();
    Montage->SlotAnimTracks[SlotIndex].SlotName = FName(*NewSlotName);
    Montage->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetMontageSlot: slot[%d] = '%s'"),
        SlotIndex, *NewSlotName);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// AddMontageNotify
// Command: ADD_MONTAGE_NOTIFY|AssetPath|NotifyClass|TimeSeconds
//
// NotifyClass: short class name e.g. "AnimNotify_PlaySound"
//              or "None" to add an untriggered marker notify
//
// Link() positions the notify on the timeline. TrackIndex=0 puts it on the
// first notify track, which is the standard default.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddMontageNotify(
    FString AssetPath, FString NotifyClassName, float TimeSeconds)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    if (TimeSeconds < 0.f)
        return TEXT("ERR:TimeSeconds must be >= 0");

    UAnimNotify* NotifyInstance = nullptr;
    if (!NotifyClassName.IsEmpty() && NotifyClassName != TEXT("None"))
    {
        UClass* NotifyClass = nullptr;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->IsChildOf(UAnimNotify::StaticClass()) &&
                It->GetName() == NotifyClassName)
            {
                NotifyClass = *It;
                break;
            }
        }
        if (!NotifyClass)
            return FString::Printf(
                TEXT("ERR:AnimNotify class '%s' not found. ")
                TEXT("Pass 'None' to add an untriggered marker notify."),
                *NotifyClassName);

        NotifyInstance = NewObject<UAnimNotify>(
            Montage, NotifyClass, NAME_None, RF_Transactional);
        if (!NotifyInstance)
            return FString::Printf(
                TEXT("ERR:Failed to instantiate notify class '%s'"), *NotifyClassName);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddMontageNotify", "GraphBridge: Add Montage Notify"));
    Montage->Modify();

    int32 NewIdx = Montage->Notifies.Add(FAnimNotifyEvent());
    FAnimNotifyEvent& NewEvent  = Montage->Notifies[NewIdx];
    NewEvent.Notify             = NotifyInstance;
    NewEvent.TrackIndex         = 0;
    NewEvent.NotifyName         = NotifyInstance
        ? FName(*NotifyClassName)
        : FName(*FString::Printf(TEXT("Notify_%d"), NewIdx));

    // Link() places the notify at the requested time on the montage timeline.
    // Minimum 0.01f avoids a known UE edge case at exactly t=0.
    NewEvent.Link(Montage, FMath::Max(TimeSeconds, 0.01f));

    Montage->MarkPackageDirty();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddMontageNotify: '%s' at %.4fs (index %d)"),
        *NotifyClassName, TimeSeconds, NewIdx);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// AddMontageNotifyState
// Command: ADD_MONTAGE_NOTIFY_STATE|AssetPath|NotifyStateClass|StartSeconds|DurationSeconds
//
// NotifyStateClass: short class name of a UAnimNotifyState subclass,
//                   e.g. "MeleeHitboxNotifyState"
//
// This is deliberately a SEPARATE opcode from ADD_MONTAGE_NOTIFY rather than a
// duration parameter on it. UAnimNotify and UAnimNotifyState are sibling
// classes, not parent/child, and they populate different FAnimNotifyEvent
// fields (Notify vs NotifyStateClass + Duration). Overloading one opcode would
// mean an agent silently gets window semantics just by passing a nonzero
// duration — a failure mode that is hard to notice. Two names, two meanings.
//
// Placement follows UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent
// (Engine/Source/Editor/AnimationBlueprintLibrary). Link() alone is NOT enough
// for a state notify: the end of the window is a second linkable element, so
// SetDuration() and an explicit EndLink.Link() are both required, followed by
// RefreshCacheData() to rebuild the editor's notify-track layout.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddMontageNotifyState(
    FString AssetPath, FString NotifyClassName, float StartSeconds, float DurationSeconds)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    if (StartSeconds < 0.f)
        return TEXT("ERR:StartSeconds must be >= 0");

    // A zero-length window is almost always a caller mistake (they wanted
    // ADD_MONTAGE_NOTIFY), so reject it loudly instead of creating a state
    // notify that begins and ends on the same frame.
    if (DurationSeconds <= 0.f)
        return FString::Printf(
            TEXT("ERR:DurationSeconds must be > 0 for a notify state (got %.4f). ")
            TEXT("Use ADD_MONTAGE_NOTIFY for a single-frame notify."),
            DurationSeconds);

    const float MontageLength = Montage->GetPlayLength();
    if (StartSeconds > MontageLength)
        return FString::Printf(
            TEXT("ERR:StartSeconds %.4f is beyond montage length %.4f"),
            StartSeconds, MontageLength);

    if (NotifyClassName.IsEmpty() || NotifyClassName == TEXT("None"))
        return TEXT("ERR:NotifyStateClass is required (a marker-only notify state is meaningless)");

    // Search UAnimNotifyState, not UAnimNotify — they are siblings, so a state
    // class never satisfies IsChildOf(UAnimNotify::StaticClass()).
    UClass* NotifyClass = nullptr;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        if (It->IsChildOf(UAnimNotifyState::StaticClass()) &&
            It->GetName() == NotifyClassName)
        {
            NotifyClass = *It;
            break;
        }
    }
    if (!NotifyClass)
    {
        // Distinguish "wrong opcode" from "class doesn't exist" — this is the
        // single most likely caller error.
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->IsChildOf(UAnimNotify::StaticClass()) &&
                It->GetName() == NotifyClassName)
            {
                return FString::Printf(
                    TEXT("ERR:'%s' is a UAnimNotify, not a UAnimNotifyState. ")
                    TEXT("Use ADD_MONTAGE_NOTIFY instead."), *NotifyClassName);
            }
        }
        return FString::Printf(
            TEXT("ERR:AnimNotifyState class '%s' not found"), *NotifyClassName);
    }

    if (NotifyClass->HasAnyClassFlags(CLASS_Abstract))
        return FString::Printf(
            TEXT("ERR:AnimNotifyState class '%s' is abstract"), *NotifyClassName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddMontageNotifyState", "GraphBridge: Add Montage Notify State"));
    Montage->Modify();

    UAnimNotifyState* StateInstance = NewObject<UAnimNotifyState>(
        Montage, NotifyClass, NAME_None, RF_Transactional);
    if (!StateInstance)
        return FString::Printf(
            TEXT("ERR:Failed to instantiate notify state class '%s'"), *NotifyClassName);

    const int32 NewIdx = Montage->Notifies.Add(FAnimNotifyEvent());
    FAnimNotifyEvent& NewEvent = Montage->Notifies[NewIdx];

    NewEvent.NotifyStateClass = StateInstance;
    NewEvent.Notify           = nullptr;
    NewEvent.TrackIndex       = 0;
    NewEvent.Guid             = FGuid::NewGuid();
    NewEvent.NotifyName       = FName(*StateInstance->GetNotifyName());

    // Order matters: Link() sets the start, SetDuration() derives the end time,
    // then EndLink.Link() anchors that end to the montage timeline.
    NewEvent.Link(Montage, FMath::Max(StartSeconds, 0.01f));
    NewEvent.TriggerTimeOffset =
        GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(StartSeconds));
    NewEvent.SetDuration(DurationSeconds);
    NewEvent.EndLink.Link(Montage, NewEvent.EndLink.GetTime());

    // Rebuilds AnimNotifyTracks so the window is drawn on the right row in the
    // Montage editor. Also creates a track if none exists and shuffles the
    // notify to a free row if track 0 already has something overlapping.
    Montage->RefreshCacheData();
    Montage->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log,
        TEXT("GraphBridge AddMontageNotifyState: '%s' at %.4fs for %.4fs (index %d)"),
        *NotifyClassName, StartSeconds, DurationSeconds, NewIdx);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// RemoveMontageNotify
// Command: REMOVE_MONTAGE_NOTIFY|AssetPath|NotifyIndex
//
// Works for both plain notifies and notify states: it removes by index into
// Montage->Notifies, which holds both kinds in the same array.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::RemoveMontageNotify(
    FString AssetPath, int32 NotifyIndex)
{
    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (!Montage)
        return FString::Printf(TEXT("ERR:AnimMontage not found at '%s'"), *AssetPath);

    if (!Montage->Notifies.IsValidIndex(NotifyIndex))
        return FString::Printf(
            TEXT("ERR:NotifyIndex %d out of range (montage has %d notif(ies))"),
            NotifyIndex, Montage->Notifies.Num());

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "RemoveMontageNotify", "GraphBridge: Remove Montage Notify"));
    Montage->Modify();
    Montage->Notifies.RemoveAt(NotifyIndex);

    // Rebuild the editor track layout so a removed notify (especially a state
    // window, which occupies a span) doesn't leave a stale row behind.
    Montage->RefreshCacheData();
    Montage->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge RemoveMontageNotify: removed index %d"), NotifyIndex);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// DataTable commands (v1.2)
// ---------------------------------------------------------------------------
//
// Key API:
//   UDataTable::AddRow(FName, FTableRowBase&)   — adds a default row
//   UDataTable::RemoveRow(FName)                — deletes by name
//   FDataTableEditorUtils::RenameRow(UDataTable*, FName old, FName new)
//     — handles undo/redo and fixup of cross-references; prefer over direct edit
//   UDataTable::GetRowMap()                     — const TMap<FName, uint8*>
//   UDataTable::RowStruct                       — UScriptStruct* for field names
//
// ADD_DATATABLE_ROW adds an empty (default-value) row.
// Use SET_ASSET_PROPERTY afterward to fill individual fields.
// Note: DataTable rows are raw structs (uint8*); we can't construct a typed
// FTableRowBase here without knowing the row struct type at compile time.
// Instead we allocate zero-initialised memory of the right size, which gives
// UE's default values for all fields exactly as the editor does.

// ---------------------------------------------------------------------------
// ListDataTableRows
// Command: LIST_DATATABLE_ROWS|AssetPath
// Returns JSON array: [{index, name, fields:[{name,type}]}]
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListDataTableRows(FString AssetPath)
{
    UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
    if (!DT)
        return FString::Printf(TEXT("ERR:DataTable not found at '%s'"), *AssetPath);

    // Collect field names from the row struct once
    TArray<FString> FieldNames;
    if (DT->RowStruct)
    {
        for (TFieldIterator<FProperty> It(DT->RowStruct); It; ++It)
            FieldNames.Add((*It)->GetName());
    }

    FString FieldsJson = TEXT("[");
    for (int32 i = 0; i < FieldNames.Num(); ++i)
    {
        if (i > 0) FieldsJson += TEXT(",");
        FieldsJson += FString::Printf(TEXT("\"%s\""), *FieldNames[i]);
    }
    FieldsJson += TEXT("]");

    FString RowsJson = TEXT("[");
    int32 Idx = 0;
    for (const auto& Pair : DT->GetRowMap())
    {
        if (Idx > 0) RowsJson += TEXT(",");
        RowsJson += FString::Printf(
            TEXT("{\"index\":%d,\"name\":\"%s\"}"),
            Idx, *Pair.Key.ToString());
        ++Idx;
    }
    RowsJson += TEXT("]");

    return FString::Printf(
        TEXT("{\"rowCount\":%d,\"fields\":%s,\"rows\":%s}"),
        DT->GetRowMap().Num(), *FieldsJson, *RowsJson);
}

// ---------------------------------------------------------------------------
// AddDataTableRow
// Command: ADD_DATATABLE_ROW|AssetPath|RowName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddDataTableRow(
    FString AssetPath, FString RowName)
{
    UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
    if (!DT)
        return FString::Printf(TEXT("ERR:DataTable not found at '%s'"), *AssetPath);
    if (!DT->RowStruct)
        return TEXT("ERR:DataTable has no RowStruct — cannot add rows");
    if (RowName.IsEmpty())
        return TEXT("ERR:RowName cannot be empty");

    const FName RowFName(*RowName);
    if (DT->GetRowMap().Contains(RowFName))
        return FString::Printf(TEXT("ERR:Row '%s' already exists"), *RowName);

    // Allocate zero-initialised memory for one row — same as editor default
    const int32 RowSize = DT->RowStruct->GetStructureSize();
    uint8* RowData = (uint8*)FMemory::Malloc(RowSize);
    DT->RowStruct->InitializeStruct(RowData);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddDTRow", "GraphBridge: Add DataTable Row"));
    DT->Modify();
    // AddRow takes an FTableRowBase& — we reinterpret our zeroed memory
    DT->AddRow(RowFName, *reinterpret_cast<FTableRowBase*>(RowData));
    DT->MarkPackageDirty();

    DT->RowStruct->DestroyStruct(RowData);
    FMemory::Free(RowData);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddDataTableRow: '%s' in %s"), *RowName, *AssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// DeleteDataTableRow
// Command: DELETE_DATATABLE_ROW|AssetPath|RowName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::DeleteDataTableRow(
    FString AssetPath, FString RowName)
{
    UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
    if (!DT)
        return FString::Printf(TEXT("ERR:DataTable not found at '%s'"), *AssetPath);

    const FName RowFName(*RowName);
    if (!DT->GetRowMap().Contains(RowFName))
        return FString::Printf(TEXT("ERR:Row '%s' not found"), *RowName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "DeleteDTRow", "GraphBridge: Delete DataTable Row"));
    DT->Modify();
    DT->RemoveRow(RowFName);
    DT->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge DeleteDataTableRow: '%s' from %s"), *RowName, *AssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// RenameDataTableRow
// Command: RENAME_DATATABLE_ROW|AssetPath|OldName|NewName
// Uses FDataTableEditorUtils::RenameRow which handles undo/redo and
// cross-reference fixup — same path the editor takes internally.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::RenameDataTableRow(
    FString AssetPath, FString OldName, FString NewName)
{
    UDataTable* DT = LoadObject<UDataTable>(nullptr, *AssetPath);
    if (!DT)
        return FString::Printf(TEXT("ERR:DataTable not found at '%s'"), *AssetPath);

    const FName OldFName(*OldName);
    const FName NewFName(*NewName);

    if (!DT->GetRowMap().Contains(OldFName))
        return FString::Printf(TEXT("ERR:Row '%s' not found"), *OldName);
    if (DT->GetRowMap().Contains(NewFName))
        return FString::Printf(TEXT("ERR:Row '%s' already exists"), *NewName);
    if (NewName.IsEmpty())
        return TEXT("ERR:NewName cannot be empty");

    // FDataTableEditorUtils::RenameRow wraps its own transaction
    bool bOk = FDataTableEditorUtils::RenameRow(DT, OldFName, NewFName);
    if (!bOk)
        return FString::Printf(TEXT("ERR:FDataTableEditorUtils::RenameRow failed for '%s'"), *OldName);

    DT->MarkPackageDirty();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge RenameDataTableRow: '%s' -> '%s' in %s"),
        *OldName, *NewName, *AssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// Skeleton socket commands (v1.3)
// ---------------------------------------------------------------------------
//
// Key API (Animation/Skeleton.h):
//   USkeleton::Sockets  — TArray<USkeletalMeshSocket*>
//   USkeletalMeshSocket — SocketName, BoneName, RelativeLocation,
//                         RelativeRotation, RelativeScale (all public)
//
// Sockets live on USkeleton (shared across all meshes using that skeleton).
// The correct outer for NewObject is the Skeleton, not the mesh.
// There is no dedicated Remove API — filter the Sockets array directly.

// ---------------------------------------------------------------------------
// ListSkeletonSockets
// Command: LIST_SKELETON_SOCKETS|SkeletonAssetPath
// Returns JSON array of sockets with name, bone, and transform.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListSkeletonSockets(FString AssetPath)
{
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *AssetPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *AssetPath);

    FString Json = TEXT("[");
    for (int32 i = 0; i < Skeleton->Sockets.Num(); ++i)
    {
        const USkeletalMeshSocket* S = Skeleton->Sockets[i];
        if (!S) continue;
        if (i > 0) Json += TEXT(",");
        Json += FString::Printf(
            TEXT("{\"index\":%d,\"name\":\"%s\",\"bone\":\"%s\","
                 "\"loc\":[%.3f,%.3f,%.3f],\"rot\":[%.3f,%.3f,%.3f]}"),
            i,
            *S->SocketName.ToString(),
            *S->BoneName.ToString(),
            S->RelativeLocation.X, S->RelativeLocation.Y, S->RelativeLocation.Z,
            S->RelativeRotation.Pitch, S->RelativeRotation.Yaw, S->RelativeRotation.Roll);
    }
    Json += TEXT("]");
    return Json;
}

// ---------------------------------------------------------------------------
// MoveSkeletonSocket
// Command: MOVE_SKELETON_SOCKET|AssetPath|SocketName|LocX|LocY|LocZ|Pitch|Yaw|Roll
// All values in UE native units (cm / degrees).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::MoveSkeletonSocket(
    FString AssetPath, FString SocketName, FVector Location, FRotator Rotation)
{
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *AssetPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *AssetPath);

    USkeletalMeshSocket* Found = nullptr;
    for (USkeletalMeshSocket* S : Skeleton->Sockets)
        if (S && S->SocketName == FName(*SocketName))
        {
            Found = S;
            break;
        }
    if (!Found)
        return FString::Printf(TEXT("ERR:Socket '%s' not found"), *SocketName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "MoveSkeletonSocket", "GraphBridge: Move Skeleton Socket"));
    Skeleton->Modify();
    Found->Modify();

    Found->RelativeLocation = Location;
    Found->RelativeRotation = Rotation;
    Skeleton->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log,
        TEXT("GraphBridge MoveSkeletonSocket: '%s' loc=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f)"),
        *SocketName,
        Location.X, Location.Y, Location.Z,
        Rotation.Pitch, Rotation.Yaw, Rotation.Roll);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// DeleteSkeletonSocket
// Command: DELETE_SKELETON_SOCKET|SkeletonAssetPath|SocketName
// No API — filter the Sockets array directly.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::DeleteSkeletonSocket(
    FString AssetPath, FString SocketName)
{
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *AssetPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *AssetPath);

    const FName SocketFName(*SocketName);
    int32 Removed = 0;

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "DeleteSkeletonSocket", "GraphBridge: Delete Skeleton Socket"));
    Skeleton->Modify();
    Removed = Skeleton->Sockets.RemoveAll([&SocketFName](const USkeletalMeshSocket* S)
    {
        return S && S->SocketName == SocketFName;
    });

    if (Removed == 0)
        return FString::Printf(TEXT("ERR:Socket '%s' not found"), *SocketName);

    Skeleton->MarkPackageDirty();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge DeleteSkeletonSocket: '%s' removed"), *SocketName);
    return TEXT("");
}

// ===========================================================================
// Character Pipeline commands (v1.4)
// ===========================================================================

// ---------------------------------------------------------------------------
// CreateIMC
// Command: CREATE_IMC|AssetPath
//
// Creates a new UInputMappingContext asset at the given content-browser path
// and saves it to disk immediately so it appears in the asset registry.
//
// AssetPath format: /Game/Input/IMC_Default  (no .uasset extension, no _C suffix)
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateIMC(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UInputMappingContext>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An IMC already exists at '%s'"), *AssetPath);

    // Derive package name and asset name.
    // Accept both /Game/Foo/Bar  and  /Game/Foo/Bar.Bar  forms.
    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }

    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);

    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateIMC", "GraphBridge: Create IMC"));

    UInputMappingContext* NewIMC = NewObject<UInputMappingContext>(
        NewPackage, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!NewIMC)
        return TEXT("ERR:NewObject<UInputMappingContext> returned null");

    FAssetRegistryModule::AssetCreated(NewIMC);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewIMC, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:IMC object created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateIMC: '%s'"), *AssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateBlueprint
// Command: CREATE_BLUEPRINT|AssetPath|ParentClass
//
// Creates a new Blueprint asset inheriting from ParentClass. Mirrors CreateIMC's
// package-creation pattern (CreatePackage -> NewObject-equivalent -> AssetCreated
// -> SavePackage), but the "NewObject" step is FKismetEditorUtilities::CreateBlueprint,
// which is the same call UBlueprintFactory::FactoryCreateNew uses internally
// (see Editor/Blutility/Private/EditorUtilityBlueprintFactory.cpp) — it builds a
// valid skeleton graph (EventGraph, SCS, etc.), not just a bare UObject.
//
// ParentClass resolution: exact name match first, then retried with an "A" or
// "U" prefix (so "Character" resolves to ACharacter, "ActorComponent" to
// UActorComponent), then a case-insensitive scan as a last resort.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateBlueprint(FString AssetPath, FString ParentClassName)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UBlueprint>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Blueprint already exists at '%s'"), *AssetPath);

    // Derive package name and asset name.
    // Accept both /Game/Foo/Bar  and  /Game/Foo/Bar.Bar  forms.
    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    // --- Resolve the parent class ---
    UClass* ParentClass = FindFirstObject<UClass>(*ParentClassName, EFindFirstObjectOptions::None);
    if (!ParentClass)
        ParentClass = FindFirstObject<UClass>(*(TEXT("A") + ParentClassName), EFindFirstObjectOptions::None);
    if (!ParentClass)
        ParentClass = FindFirstObject<UClass>(*(TEXT("U") + ParentClassName), EFindFirstObjectOptions::None);

    if (!ParentClass)
    {
        // Case-insensitive scan as a last resort (also tries A/U prefixed forms)
        for (TObjectIterator<UClass> It; It; ++It)
        {
            const FString Name = It->GetName();
            if (Name.Equals(ParentClassName, ESearchCase::IgnoreCase) ||
                Name.Equals(TEXT("A") + ParentClassName, ESearchCase::IgnoreCase) ||
                Name.Equals(TEXT("U") + ParentClassName, ESearchCase::IgnoreCase))
            {
                ParentClass = *It;
                break;
            }
        }
    }

    if (!ParentClass)
        return FString::Printf(
            TEXT("ERR:Parent class '%s' not found. Common values: Actor, Character, Pawn, ")
            TEXT("ActorComponent, SceneComponent, GameModeBase, PlayerController, HUD, ")
            TEXT("GameInstance, GameState, PlayerState"),
            *ParentClassName);

    if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
        return FString::Printf(
            TEXT("ERR:Cannot create a Blueprint based on class '%s'"), *ParentClass->GetName());

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateBlueprint", "GraphBridge: Create Blueprint"));

    UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, NewPackage, FName(*AssetName), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
        FName("GraphBridge"));
    if (!NewBP)
        return FString::Printf(
            TEXT("ERR:FKismetEditorUtilities::CreateBlueprint returned null for parent '%s'"),
            *ParentClass->GetName());

    // FKismetEditorUtilities::CreateBlueprint does NOT always add a default
    // EventGraph — confirmed live it's skipped for non-Actor parent classes
    // (e.g. AnimNotify/AnimNotifyState), unlike the Content Browser's "New
    // Blueprint Class" UI action (UBlueprintFactory::FactoryCreateNew), which
    // explicitly adds one after this same CreateBlueprint call. Without an
    // Ubergraph page, SPAWN_NODE ("Blueprint has no EventGraph pages") and
    // ADD_IMC_TO_CHARACTER-style event lookups have nowhere to put nodes, so
    // mirror the factory's own follow-up step here for parity. Guarded by
    // UbergraphPages.Num()==0 so this is a no-op for parent types (Actor,
    // Character, ...) that already get one — matches CanCreateBlueprintOfClass
    // having already passed above, so this parent class does support graphs.
    if (NewBP->UbergraphPages.Num() == 0)
    {
        UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
            NewBP, UEdGraphSchema_K2::GN_EventGraph, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(NewBP, NewGraph);
        NewBP->LastEditedDocuments.Add(NewGraph);
    }

    FAssetRegistryModule::AssetCreated(NewBP);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewBP, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Blueprint created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateBlueprint: '%s' (parent: %s)"),
        *AssetPath, *ParentClass->GetName());
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddIMCMapping
// Command: ADD_IMC_MAPPING|IMCPath|ActionPath|KeyName|ModifierClasses(optional)
//
// Adds a key→action mapping to an existing IMC.
// ModifierClasses: comma-separated short class names e.g. "InputModifierNegate"
// Partial class name matching is supported for modifier resolution.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddIMCMapping(
    FString IMCPath, FString ActionPath, FString KeyName, FString ModifierClasses)
{
    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
    if (!IMC)
        return FString::Printf(TEXT("ERR:IMC not found at '%s'"), *IMCPath);

    UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (!Action)
        return FString::Printf(TEXT("ERR:InputAction not found at '%s'"), *ActionPath);

    FKey Key(*KeyName);
    if (!Key.IsValid())
        return FString::Printf(
            TEXT("ERR:Key '%s' is not a recognised FKey name. "
                 "Examples: W, SpaceBar, Gamepad_LeftX, MouseX"),
            *KeyName);

    // Guard: duplicate mapping
    for (const FEnhancedActionKeyMapping& Existing : IMC->GetMappings())
    {
        if (Existing.Action == Action && Existing.Key == Key)
            return FString::Printf(
                TEXT("ERR:A mapping for Action '%s' + Key '%s' already exists. "
                     "Use REMOVE_IMC_MAPPING first."),
                *Action->GetName(), *KeyName);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddIMCMapping", "GraphBridge: Add IMC Mapping"));
    IMC->Modify();

    FEnhancedActionKeyMapping& NewMapping = IMC->MapKey(Action, Key);

    // Attach modifier objects if requested
    if (!ModifierClasses.IsEmpty())
    {
        TArray<FString> ModNames;
        ModifierClasses.ParseIntoArray(ModNames, TEXT(","), /*bCullEmpty=*/true);

        for (FString ModName : ModNames)
        {
            ModName.TrimStartAndEndInline();
            if (ModName.IsEmpty()) continue;

            // Resolution order: exact name, with U prefix, partial match in UInputModifier hierarchy
            UClass* ModClass = FindFirstObject<UClass>(*ModName, EFindFirstObjectOptions::None);
            if (!ModClass)
                ModClass = FindFirstObject<UClass>(
                    *FString::Printf(TEXT("U%s"), *ModName), EFindFirstObjectOptions::None);
            if (!ModClass)
            {
                for (TObjectIterator<UClass> It; It; ++It)
                {
                    if (It->IsChildOf(UInputModifier::StaticClass()) &&
                        !It->HasAnyClassFlags(CLASS_Abstract) &&
                        It->GetName().Contains(ModName, ESearchCase::IgnoreCase))
                    {
                        ModClass = *It;
                        break;
                    }
                }
            }
            if (!ModClass || !ModClass->IsChildOf(UInputModifier::StaticClass()))
            {
                UE_LOG(LogGraphBridge, Warning,
                    TEXT("GraphBridge AddIMCMapping: modifier '%s' not found or not a UInputModifier — skipping"),
                    *ModName);
                continue;
            }

            UInputModifier* Mod = NewObject<UInputModifier>(IMC, ModClass, NAME_None, RF_Transactional);
            if (Mod)
                NewMapping.Modifiers.Add(Mod);
        }
    }

    IMC->MarkPackageDirty();
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddIMCMapping: %s + key '%s' -> %s"),
        *ActionPath, *KeyName, *IMCPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// RemoveIMCMapping
// Command: REMOVE_IMC_MAPPING|IMCPath|ActionPath|KeyName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::RemoveIMCMapping(
    FString IMCPath, FString ActionPath, FString KeyName)
{
    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
    if (!IMC)
        return FString::Printf(TEXT("ERR:IMC not found at '%s'"), *IMCPath);

    UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
    if (!Action)
        return FString::Printf(TEXT("ERR:InputAction not found at '%s'"), *ActionPath);

    FKey Key(*KeyName);
    if (!Key.IsValid())
        return FString::Printf(TEXT("ERR:Key '%s' is not a recognised FKey name"), *KeyName);

    // Confirm the mapping exists before transacting
    bool bFound = false;
    for (const FEnhancedActionKeyMapping& M : IMC->GetMappings())
    {
        if (M.Action == Action && M.Key == Key)
        {
            bFound = true;
            break;
        }
    }
    if (!bFound)
    {
        TArray<FString> ExistingKeys;
        for (const FEnhancedActionKeyMapping& M : IMC->GetMappings())
            if (M.Action == Action)
                ExistingKeys.Add(M.Key.GetDisplayName().ToString());
        return FString::Printf(
            TEXT("ERR:No mapping found for Action '%s' + Key '%s'. "
                 "Keys bound to this action: %s"),
            *Action->GetName(), *KeyName,
            ExistingKeys.IsEmpty() ? TEXT("(none)") : *FString::Join(ExistingKeys, TEXT(", ")));
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "RemoveIMCMapping", "GraphBridge: Remove IMC Mapping"));
    IMC->Modify();
    IMC->UnmapKey(Action, Key);
    IMC->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge RemoveIMCMapping: removed key '%s' from action '%s' in '%s'"),
        *KeyName, *ActionPath, *IMCPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// ListIMCMappings
// Command: LIST_IMC_MAPPINGS|IMCPath
// Returns JSON array: [{"index":0,"action":"...","key":"...","modifiers":"..."}]
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListIMCMappings(FString IMCPath)
{
    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
    if (!IMC)
        return FString::Printf(TEXT("ERR:IMC not found at '%s'"), *IMCPath);

    const TArray<FEnhancedActionKeyMapping>& Mappings = IMC->GetMappings();
    FString Json = TEXT("[");
    for (int32 i = 0; i < Mappings.Num(); ++i)
    {
        const FEnhancedActionKeyMapping& M = Mappings[i];
        if (i > 0) Json += TEXT(",");

        FString ActionPath = M.Action ? M.Action->GetPathName() : TEXT("None");
        FString KeyStr     = M.Key.GetDisplayName().ToString();

        TArray<FString> ModNames;
        for (const TObjectPtr<UInputModifier>& Mod : M.Modifiers)
            if (Mod) ModNames.Add(Mod->GetClass()->GetName());

        Json += FString::Printf(
            TEXT("{\"index\":%d,\"action\":\"%s\",\"key\":\"%s\",\"modifiers\":\"%s\"}"),
            i, *ActionPath, *KeyStr, *FString::Join(ModNames, TEXT(",")));
    }
    Json += TEXT("]");
    return Json;
}

// ---------------------------------------------------------------------------
// SaveAsset
// Command: SAVE_ASSET|AssetPath
//
// Generic save for any UObject asset — IMC, DataTable, Skeleton, etc.
// Same save pipeline as SaveBlueprint but works on the base UObject type.
// ---------------------------------------------------------------------------
bool UGraphBridgeAutomationLibrary::SaveAsset(FString AssetPath)
{
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("GraphBridge SaveAsset: asset not found at '%s'"), *AssetPath);
        return false;
    }

    UPackage* Package = Asset->GetOutermost();
    if (!Package) return false;

    Package->MarkPackageDirty();

    const FString PackageName     = Package->GetName();
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    const bool bSuccess = UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SaveAsset: %s -> %s"),
        *AssetPath, bSuccess ? TEXT("saved") : TEXT("FAILED"));
    return bSuccess;
}

// ---------------------------------------------------------------------------
// SetCharacterMesh
// Command: SET_CHARACTER_MESH|BPPath|MeshPath|ComponentName(optional)
//
// Sets the SkeletalMesh on a SkeletalMeshComponent template inside the Blueprint
// SCS, or on an inherited C++ component via CDO (e.g. ACharacter::Mesh).
// ComponentName defaults to "CharacterMesh0" (ACharacter's inherited mesh).
//
// Mesh assignment uses the canonical USkeletalMeshComponent::SetSkeletalMeshAsset()
// setter. Confirmed against UE 5.8 SkeletalMeshComponent.h: the SkeletalMeshAsset
// UPROPERTY is EditAnywhere BUT Transient, with Setter=SetSkeletalMeshAsset, and
// its deprecation note states "getter and setter must be used at all times to
// preserve correct operations." So the previous raw-reflection write was wrong on
// both 5.7 and 5.8 — it bypassed the required setter and wrote a Transient field
// that never serialized. The setter drives SetSkeletalMesh() which updates the
// real serialized backing store (SkinnedAsset). SetSkeletalMeshAsset exists since
// UE 5.1, so this is correct for 5.1 through 5.8. (Resolved // VERIFY: mesh property.)
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetCharacterMesh(
    FString BlueprintPath, FString MeshPath, FString ComponentName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
    if (!Mesh)
        return FString::Printf(TEXT("ERR:SkeletalMesh not found at '%s'"), *MeshPath);

    if (ComponentName.IsEmpty())
        ComponentName = TEXT("CharacterMesh0");

    // Assigns the mesh to a SkeletalMeshComponent template via the canonical
    // setter (see the function-header note). Always succeeds for a valid
    // USkeletalMeshComponent; kept as a bool-returning lambda so the call sites'
    // error-reporting structure is unchanged.
    auto ApplyMesh = [&](USkeletalMeshComponent* SkelComp) -> bool
    {
        SkelComp->SetSkeletalMeshAsset(Mesh);
        return true;
    };

    // First pass: SCS (Blueprint-added components)
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (SCS)
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node) continue;
            if (!Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
                continue;
            USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(Node->ComponentTemplate);
            if (!SkelComp)
                return FString::Printf(
                    TEXT("ERR:Component '%s' is not a SkeletalMeshComponent"), *ComponentName);
            const FScopedTransaction Transaction(
                NSLOCTEXT("GraphBridge", "SetCharacterMesh", "GraphBridge: Set Character Mesh"));
            SkelComp->Modify();
            if (!ApplyMesh(SkelComp))
                return FString::Printf(
                    TEXT("ERR:Could not write mesh property on '%s' — neither SkeletalMeshAsset "
                         "nor SkeletalMesh found on class '%s'"),
                    *ComponentName, *SkelComp->GetClass()->GetName());
            Blueprint->Modify();
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
            UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetCharacterMesh: '%s' on SCS component '%s'"),
                *MeshPath, *ComponentName);
            return TEXT("");
        }
    }

    // Second pass: inherited C++ component on CDO (e.g. ACharacter::Mesh)
    UClass* GenClass = Blueprint->GeneratedClass;
    if (GenClass)
    {
        UObject* CDO = GenClass->GetDefaultObject();
        if (CDO)
        {
            for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            {
                FObjectProperty* Prop = *PropIt;
                if (!Prop->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) continue;
                USkeletalMeshComponent* SkelComp = Cast<USkeletalMeshComponent>(
                    Prop->GetObjectPropertyValue_InContainer(CDO));
                if (!SkelComp) continue;
                const FScopedTransaction Transaction(
                    NSLOCTEXT("GraphBridge", "SetCharacterMesh", "GraphBridge: Set Character Mesh"));
                SkelComp->Modify();
                if (!ApplyMesh(SkelComp))
                    return FString::Printf(
                        TEXT("ERR:Could not write mesh property on inherited component '%s'"),
                        *ComponentName);
                Blueprint->Modify();
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
                UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetCharacterMesh: '%s' on CDO component '%s'"),
                    *MeshPath, *ComponentName);
                return TEXT("");
            }
        }
    }

    // Build a helpful available-names list
    TArray<FString> Names;
    if (SCS)
        for (USCS_Node* N : SCS->GetAllNodes())
            if (N && Cast<USkeletalMeshComponent>(N->ComponentTemplate))
                Names.Add(N->GetVariableName().ToString());
    if (GenClass)
        for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            if (Cast<USkeletalMeshComponent>((*PropIt)->GetObjectPropertyValue_InContainer(
                    GenClass->GetDefaultObject())))
                Names.Add((*PropIt)->GetName());

    return FString::Printf(
        TEXT("ERR:SkeletalMeshComponent '%s' not found. Available: %s"),
        *ComponentName, Names.IsEmpty() ? TEXT("(none)") : *FString::Join(Names, TEXT(", ")));
}

// ---------------------------------------------------------------------------
// SetCharacterCapsule
// Command: SET_CHARACTER_CAPSULE|BPPath|HalfHeight|Radius|ComponentName(optional)
//
// Sets CapsuleHalfHeight and CapsuleRadius on the capsule template.
// ComponentName defaults to "CapsuleComponent" (ACharacter's root capsule).
// Both values are in centimetres (UE native units).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetCharacterCapsule(
    FString BlueprintPath, float HalfHeight, float Radius, FString ComponentName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (HalfHeight <= 0.f)
        return TEXT("ERR:HalfHeight must be > 0");
    if (Radius <= 0.f)
        return TEXT("ERR:Radius must be > 0");

    if (ComponentName.IsEmpty())
        ComponentName = TEXT("CapsuleComponent");

    auto ApplyCapsule = [&](UCapsuleComponent* Cap) -> FString
    {
        const FScopedTransaction Transaction(
            NSLOCTEXT("GraphBridge", "SetCapsule", "GraphBridge: Set Character Capsule"));
        Cap->Modify();
        // SetCapsuleSize is the public API and clamps values correctly.
        // Safe to call on SCS templates — UpdateBodySetup only touches the
        // abstract body setup asset, not runtime physics.
        Cap->SetCapsuleSize(Radius, HalfHeight);
        Blueprint->Modify();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UE_LOG(LogGraphBridge, Log,
            TEXT("GraphBridge SetCharacterCapsule: HalfHeight=%.1f Radius=%.1f on '%s'"),
            HalfHeight, Radius, *ComponentName);
        return TEXT("");
    };

    // First pass: SCS
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (SCS)
    {
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node) continue;
            if (!Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
                continue;
            UCapsuleComponent* Cap = Cast<UCapsuleComponent>(Node->ComponentTemplate);
            if (!Cap)
                return FString::Printf(
                    TEXT("ERR:Component '%s' is not a UCapsuleComponent"), *ComponentName);
            return ApplyCapsule(Cap);
        }
    }

    // Second pass: inherited C++ component on CDO
    UClass* GenClass = Blueprint->GeneratedClass;
    if (GenClass)
    {
        UObject* CDO = GenClass->GetDefaultObject();
        if (CDO)
        {
            for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            {
                if (!(*PropIt)->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) continue;
                UCapsuleComponent* Cap = Cast<UCapsuleComponent>(
                    (*PropIt)->GetObjectPropertyValue_InContainer(CDO));
                if (!Cap) continue;
                return ApplyCapsule(Cap);
            }
        }
    }

    TArray<FString> Names;
    if (SCS)
        for (USCS_Node* N : SCS->GetAllNodes())
            if (N && Cast<UCapsuleComponent>(N->ComponentTemplate))
                Names.Add(N->GetVariableName().ToString());
    if (GenClass)
        for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            if (Cast<UCapsuleComponent>((*PropIt)->GetObjectPropertyValue_InContainer(
                    GenClass->GetDefaultObject())))
                Names.Add((*PropIt)->GetName());

    return FString::Printf(
        TEXT("ERR:CapsuleComponent '%s' not found. Available capsule components: %s"),
        *ComponentName, Names.IsEmpty() ? TEXT("(none)") : *FString::Join(Names, TEXT(", ")));
}

// ---------------------------------------------------------------------------
// SetCameraBoom
// Command: SET_CAMERA_BOOM|BPPath|ArmLength|OffX|OffY|OffZ|ComponentName(optional)
//
// Sets TargetArmLength (cm) and SocketOffset (cm) on the SpringArmComponent.
// When ComponentName is omitted the first USpringArmComponent in the SCS is used.
// TargetArmLength and SocketOffset are confirmed EditAnywhere on UE 5.7 (dev.epicgames.com).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetCameraBoom(
    FString BlueprintPath, float ArmLength, FVector SocketOffset, FString ComponentName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (ArmLength < 0.f)
        return TEXT("ERR:ArmLength must be >= 0");

    auto ApplyBoom = [&](USpringArmComponent* Boom, const FString& ActualName) -> FString
    {
        const FScopedTransaction Transaction(
            NSLOCTEXT("GraphBridge", "SetCameraBoom", "GraphBridge: Set Camera Boom"));
        Boom->Modify();
        Boom->TargetArmLength = ArmLength;
        Boom->SocketOffset    = SocketOffset;
        Blueprint->Modify();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        UE_LOG(LogGraphBridge, Log,
            TEXT("GraphBridge SetCameraBoom: '%s' ArmLength=%.1f Offset=(%.1f,%.1f,%.1f)"),
            *ActualName, ArmLength, SocketOffset.X, SocketOffset.Y, SocketOffset.Z);
        return TEXT("");
    };

    // First pass: SCS — match by name if given, otherwise accept first SpringArm
    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (SCS)
    {
        USCS_Node* FirstSpringArm = nullptr;
        for (USCS_Node* Node : SCS->GetAllNodes())
        {
            if (!Node || !Cast<USpringArmComponent>(Node->ComponentTemplate)) continue;
            if (ComponentName.IsEmpty() ||
                Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
            {
                return ApplyBoom(
                    Cast<USpringArmComponent>(Node->ComponentTemplate),
                    Node->GetVariableName().ToString());
            }
            if (!FirstSpringArm) FirstSpringArm = Node;
        }
        // If a name was given but not found, fall through to CDO pass; if no name was given
        // and no SpringArm was found in SCS, fall through as well.
    }

    // Second pass: inherited C++ component on CDO
    UClass* GenClass = Blueprint->GeneratedClass;
    if (GenClass)
    {
        UObject* CDO = GenClass->GetDefaultObject();
        if (CDO)
        {
            for (TFieldIterator<FObjectProperty> PropIt(GenClass, EFieldIteratorFlags::IncludeSuper); PropIt; ++PropIt)
            {
                USpringArmComponent* Boom = Cast<USpringArmComponent>(
                    (*PropIt)->GetObjectPropertyValue_InContainer(CDO));
                if (!Boom) continue;
                if (!ComponentName.IsEmpty() &&
                    !(*PropIt)->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
                    continue;
                return ApplyBoom(Boom, (*PropIt)->GetName());
            }
        }
    }

    return FString::Printf(
        TEXT("ERR:No USpringArmComponent%s found. "
             "Add one via ADD_COMPONENT|%s|SpringArmComponent|CameraBoom first."),
        ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName),
        *BlueprintPath);
}

// ---------------------------------------------------------------------------
// AddIMCToCharacter
// Command: ADD_IMC_TO_CHARACTER|BPPath|IMCPath|Priority(default 0)
//
// Spawns a node chain inside BeginPlay that calls:
//   UGameplayStatics::GetPlayerController(0)
//     → USubsystemBlueprintLibrary::GetLocalPlayerSubsystem(class=UEnhancedInputLocalPlayerSubsystem)
//       → UEnhancedInputLocalPlayerSubsystem::AddMappingContext(IMC, Priority)
//
// Returns the NodeGuid string of the AddMappingContext node on success.
//
// Pin names (confirmed against UE 5.8 source — unchanged from 5.7):
//   GetLocalPlayerSubsystem params: "PlayerController", "Class"
//   AddMappingContext target pin: "self" (UEdGraphSchema_K2::PN_Self)
//   AddMappingContext params: "MappingContext", "Priority"
//   (EnhancedInputSubsystemInterface.h: AddMappingContext(const UInputMappingContext*
//    MappingContext, int32 Priority, ...) — a member UFUNCTION, so param-derived pins
//    are MappingContext/Priority and the target pin is self. The self→Target and
//    FindPin null-guards below remain as defensive fallbacks.)
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddIMCToCharacter(
    FString BlueprintPath, FString IMCPath, int32 Priority)
{
    UBlueprint* BP = GetBlueprintByPath(BlueprintPath);
    if (!BP)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);
    if (!BP->UbergraphPages.Num())
        return TEXT("ERR:Blueprint has no EventGraph");

    UEdGraph* Graph = BP->UbergraphPages[0];

    UInputMappingContext* IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
    if (!IMC)
        return FString::Printf(TEXT("ERR:InputMappingContext not found at '%s'"), *IMCPath);

    // Find the ReceiveBeginPlay override event node
    UK2Node_Event* BeginPlayNode = nullptr;
    for (UEdGraphNode* N : Graph->Nodes)
    {
        UK2Node_Event* Ev = Cast<UK2Node_Event>(N);
        if (Ev && Ev->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
        {
            BeginPlayNode = Ev;
            break;
        }
    }
    if (!BeginPlayNode)
        return TEXT("ERR:ReceiveBeginPlay event not found in EventGraph. "
                    "Add it first: SPAWN_NODE|BPPath|K2Node_Event|BeginPlay|0|0 "
                    "then SET_EVENT_REF|BPPath|<guid>|ReceiveBeginPlay");

    // --- Resolve the two plain functions (GetSub is handled separately below) ---
    auto FindUFunction = [](const TCHAR* ShortClass, const TCHAR* FuncName) -> UFunction*
    {
        UClass* C = FindFirstObject<UClass>(ShortClass, EFindFirstObjectOptions::None);
        if (!C) C = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), ShortClass),
                                             EFindFirstObjectOptions::None);
        return C ? C->FindFunctionByName(FuncName) : nullptr;
    };

    UFunction* FnGetPC  = FindUFunction(TEXT("GameplayStatics"),        TEXT("GetPlayerController"));
    UFunction* FnAddIMC = FindUFunction(TEXT("EnhancedInputLocalPlayerSubsystem"), TEXT("AddMappingContext"));

    if (!FnGetPC)
        return TEXT("ERR:UGameplayStatics::GetPlayerController not found — is the Engine module loaded?");
    if (!FnAddIMC)
        return TEXT("ERR:UEnhancedInputLocalPlayerSubsystem::AddMappingContext not found — "
                    "is the EnhancedInput plugin enabled for this project?");

    // Resolve UEnhancedInputLocalPlayerSubsystem class up front — required
    // (not optional) since K2Node_GetSubsystemFromPC::Initialize() needs it
    // to produce a correctly-typed ReturnValue pin.
    UClass* EISubClass = FindFirstObject<UClass>(
        TEXT("EnhancedInputLocalPlayerSubsystem"), EFindFirstObjectOptions::None);
    if (!EISubClass)
        EISubClass = FindFirstObject<UClass>(
            TEXT("UEnhancedInputLocalPlayerSubsystem"), EFindFirstObjectOptions::None);
    if (!EISubClass)
        return TEXT("ERR:UEnhancedInputLocalPlayerSubsystem class not found — "
                    "is the EnhancedInput plugin enabled for this project?");

    UClass* GetSubFromPCClass = FindFirstObject<UClass>(
        TEXT("K2Node_GetSubsystemFromPC"), EFindFirstObjectOptions::None);
    if (!GetSubFromPCClass)
        return TEXT("ERR:K2Node_GetSubsystemFromPC class not found — is the BlueprintGraph module loaded?");

    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return TEXT("ERR:Could not get K2 schema");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddIMCToChar", "GraphBridge: Add IMC To Character"));
    Graph->Modify();
    BeginPlayNode->Modify();

    int32 BaseX = BeginPlayNode->NodePosX + 400;
    int32 BaseY = BeginPlayNode->NodePosY;

    // Spawn a K2Node_CallFunction pre-bound to the given UFunction
    auto SpawnCallNode = [&](UFunction* Func, int32 X, int32 Y) -> UK2Node_CallFunction*
    {
        UK2Node_CallFunction* Template = NewObject<UK2Node_CallFunction>(
            GetTransientPackage(), NAME_None, RF_Transactional);
        Template->SetFromFunction(Func);
        FEdGraphSchemaAction_K2NewNode Action;
        Action.NodeTemplate = Template;
        return Cast<UK2Node_CallFunction>(
            Action.PerformAction(Graph, nullptr, FVector2f((float)X, (float)Y), false));
    };

    UK2Node_CallFunction* GetPCNode  = SpawnCallNode(FnGetPC,  BaseX,       BaseY);

    // GetSub uses K2Node_GetSubsystemFromPC, NOT a generic K2Node_CallFunction
    // targeting USubsystemBlueprintLibrary::GetLocalPlayerSubsystem. Confirmed
    // live: that generic function's ReturnValue pin is statically typed to
    // the base ULocalPlayerSubsystem regardless of what the Class input pin's
    // default is set to (it is a plain TSubclassOf param, NOT a
    // DeterminesOutputType wildcard) — so a downstream connection expecting
    // IEnhancedInputSubsystemInterface always fails schema validation
    // ("...is not compatible with Enhanced Input Subsystem Interface
    // Interface"), no matter what default value or ReconstructNode() call is
    // made. K2Node_GetSubsystemFromPC is the dedicated node this bridge's own
    // SET_SUBSYSTEM_CLASS command already supports (Initialize(Class) +
    // ReconstructNode() genuinely re-types ReturnValue) — mirrored here
    // directly rather than going through a second WebSocket round-trip.
    UK2Node_GetSubsystem* GetSubTemplate = NewObject<UK2Node_GetSubsystem>(
        GetTransientPackage(), GetSubFromPCClass, NAME_None, RF_Transactional);
    GetSubTemplate->Initialize(EISubClass);
    FEdGraphSchemaAction_K2NewNode SubAction;
    SubAction.NodeTemplate = GetSubTemplate;
    UK2Node_GetSubsystem* GetSubNode = Cast<UK2Node_GetSubsystem>(
        SubAction.PerformAction(Graph, nullptr, FVector2f((float)(BaseX + 380), (float)BaseY), false));

    UK2Node_CallFunction* AddIMCNode = SpawnCallNode(FnAddIMC, BaseX + 760, BaseY);

    if (!GetPCNode || !GetSubNode || !AddIMCNode)
        return TEXT("ERR:Failed to spawn one or more Blueprint nodes. "
                    "Try CLOSE_BLUEPRINT first to release the editor viewport.");

    // Wire exec chain: BeginPlay.Then → AddIMC directly. GetPlayerController
    // and GetLocalPlayerSubsystem are BOTH BlueprintPure in this engine
    // version (confirmed live via GET_NODE_PINS — neither has an execute/then
    // pin at all), so they never participate in the exec chain; a pure node
    // is evaluated automatically whenever its output feeds a wired input pin.
    // The original code assumed all three were exec-having nodes and tried
    // to wire BeginPlay→GetPC→GetSub→AddIMC, which silently no-op'd on every
    // link (TryWire's Output&&Input guard skipped connection whenever either
    // pure node's nonexistent execute/then pin came back null) — the net
    // effect was AddMappingContext's execute pin was NEVER connected to
    // anything, so this command silently failed to make Enhanced Input work
    // at runtime despite reporting success.
    auto TryWire = [&](UEdGraphPin* Output, UEdGraphPin* Input)
    {
        if (Output && Input)
            Schema->TryCreateConnection(Output, Input);
    };

    TryWire(BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then),
            AddIMCNode->FindPin(UEdGraphSchema_K2::PN_Execute));

    // GetPC.ReturnValue → GetSub.PlayerController
    // Confirmed live via GET_NODE_PINS that K2Node_GetSubsystemFromPC's input
    // pin is named "PlayerController" (not "ContextObject" — that name
    // belongs to the generic CallFunction node this was replaced with).
    // GetSubNode's Class is already resolved by Initialize(EISubClass) above,
    // so ReturnValue is genuinely typed to EnhancedInputLocalPlayerSubsystem
    // from the moment the node is created — no separate Class-pin/
    // ReconstructNode step needed here.
    TryWire(GetPCNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue),
            GetSubNode->FindPin(TEXT("PlayerController")));

    // GetSub.ReturnValue → AddIMC.self (Target). "self" confirmed for UE 5.8
    // (AddMappingContext is a member UFUNCTION); Target kept as a fallback.
    UEdGraphPin* AddIMCTarget = AddIMCNode->FindPin(UEdGraphSchema_K2::PN_Self);
    if (!AddIMCTarget)
        AddIMCTarget = AddIMCNode->FindPin(TEXT("Target"));
    TryWire(GetSubNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue), AddIMCTarget);

    // Set AddIMC.MappingContext pin (param name confirmed for UE 5.8).
    if (UEdGraphPin* IMCPin = AddIMCNode->FindPin(TEXT("MappingContext")))
        IMCPin->DefaultObject = IMC;

    // Set AddIMC.Priority pin (param name confirmed for UE 5.8).
    if (UEdGraphPin* PriorityPin = AddIMCNode->FindPin(TEXT("Priority")))
        PriorityPin->DefaultValue = FString::FromInt(Priority);

    // Default PlayerIndex to 0 on GetPC
    if (UEdGraphPin* IndexPin = GetPCNode->FindPin(TEXT("PlayerIndex")))
        IndexPin->DefaultValue = TEXT("0");

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

    // Verify the wiring actually took, rather than trusting TryWire's silent
    // no-op-on-null-pin behavior (exactly the class of bug that made this
    // command previously report success while leaving AddMappingContext's
    // execute pin completely disconnected — see comments above).
    UEdGraphPin* ExecPin = AddIMCNode->FindPin(UEdGraphSchema_K2::PN_Execute);
    if (!ExecPin || ExecPin->LinkedTo.Num() == 0)
        return TEXT("ERR:Failed to wire BeginPlay exec into AddMappingContext — "
                    "pin names may have changed in this engine version. "
                    "Nodes were spawned; inspect them with LIST_NODES/GET_NODE_PINS.");
    if (!AddIMCTarget || AddIMCTarget->LinkedTo.Num() == 0)
        return TEXT("ERR:Failed to wire GetLocalPlayerSubsystem result into "
                    "AddMappingContext's target pin — pin names may have changed "
                    "in this engine version.");

    UE_LOG(LogGraphBridge, Log,
        TEXT("GraphBridge AddIMCToCharacter: wired '%s' (priority %d) in BeginPlay of '%s'"),
        *IMCPath, Priority, *BlueprintPath);

    return AddIMCNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// SetGameModePawn
// Command: SET_GAMEMODE_PAWN|GameModeBPPath|PawnClassPath
//
// Sets DefaultPawnClass on the AGameModeBase CDO obtained from the Blueprint's
// GeneratedClass, then recompiles the Blueprint to bake the value in.
//
// PawnClassPath may be a Blueprint asset path (/Game/BP_Hero.BP_Hero) or
// a C++ class name (e.g. ACharacter).
// (UE 5.8: setting a CDO default + recompile is the standard persist path here;
//  UBlueprint / FKismetEditorUtilities recompile behavior is unchanged in 5.8 —
//  no signature or behavior change, and the plugin compiles clean on 5.8.)
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetGameModePawn(
    FString GameModeBPPath, FString PawnClassPath)
{
    UBlueprint* BP = GetBlueprintByPath(GameModeBPPath);
    if (!BP)
        return FString::Printf(TEXT("ERR:GameMode Blueprint not found at '%s'"), *GameModeBPPath);

    // Validate that this is actually a GameMode Blueprint
    if (!BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(AGameModeBase::StaticClass()))
        return FString::Printf(
            TEXT("ERR:Blueprint '%s' is not derived from AGameModeBase"),
            *GameModeBPPath);

    // Resolve the Pawn class — Blueprint asset path or C++ class name
    UClass* PawnClass = nullptr;
    if (PawnClassPath.Contains(TEXT("/")))
    {
        // Blueprint asset: try _C suffix first, then load as UBlueprint and get GeneratedClass
        FString ClassPath = PawnClassPath;
        if (!ClassPath.EndsWith(TEXT("_C")))
            ClassPath += TEXT("_C");
        PawnClass = LoadClass<APawn>(nullptr, *ClassPath);
        if (!PawnClass)
        {
            UBlueprint* PawnBP = LoadObject<UBlueprint>(nullptr, *PawnClassPath);
            if (PawnBP && PawnBP->GeneratedClass &&
                PawnBP->GeneratedClass->IsChildOf(APawn::StaticClass()))
                PawnClass = PawnBP->GeneratedClass;
        }
    }
    else
    {
        // C++ class: try with A prefix if bare name given
        PawnClass = FindFirstObject<UClass>(*PawnClassPath, EFindFirstObjectOptions::None);
        if (!PawnClass)
            PawnClass = FindFirstObject<UClass>(
                *FString::Printf(TEXT("A%s"), *PawnClassPath), EFindFirstObjectOptions::None);
    }

    if (!PawnClass)
        return FString::Printf(
            TEXT("ERR:Pawn class not found: '%s'. "
                 "Pass a Blueprint path (/Game/BP_Hero.BP_Hero) or C++ class name (ACharacter)."),
            *PawnClassPath);

    if (!PawnClass->IsChildOf(APawn::StaticClass()))
        return FString::Printf(
            TEXT("ERR:Class '%s' is not derived from APawn"), *PawnClass->GetName());

    AGameModeBase* CDO = Cast<AGameModeBase>(BP->GeneratedClass->GetDefaultObject());
    if (!CDO)
        return TEXT("ERR:Failed to get AGameModeBase CDO from GeneratedClass");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetGMPawn", "GraphBridge: Set GameMode DefaultPawnClass"));
    CDO->Modify();
    BP->Modify();

    CDO->DefaultPawnClass = PawnClass;

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetGameModePawn: '%s' DefaultPawnClass = '%s'"),
        *GameModeBPPath, *PawnClass->GetName());
    return TEXT("");
}

// ---------------------------------------------------------------------------
// GetCurrentGameMode
// Command: GET_CURRENT_GAMEMODE
// Returns JSON: {"world":"<LevelName>","gameMode":"<ClassPath>"}
// GameMode is read from AWorldSettings::DefaultGameMode on the editor world.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetCurrentGameMode()
{
    if (!GEditor)
        return TEXT("ERR:GEditor not available (not running in editor)");

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
        return TEXT("ERR:No editor world — open a level first");

    AWorldSettings* WS = World->GetWorldSettings();
    if (!WS)
        return TEXT("ERR:World has no WorldSettings actor");

    UClass* GMClass = WS->DefaultGameMode;
    FString GameModeStr = GMClass
        ? GMClass->GetPathName()
        : TEXT("(using project default from Project Settings)");

    return FString::Printf(
        TEXT("{\"world\":\"%s\",\"gameMode\":\"%s\"}"),
        *World->GetName(), *GameModeStr);
}

// ---------------------------------------------------------------------------
// GetPlayerStart
// Command: GET_PLAYER_START
// Returns JSON array of all APlayerStart actors in the current editor level.
// Includes actor name, world location (cm) and rotation (degrees).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetPlayerStart()
{
    if (!GEditor)
        return TEXT("ERR:GEditor not available");

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
        return TEXT("ERR:No editor world — open a level first");

    FString Json = TEXT("[");
    int32 Count  = 0;
    for (TActorIterator<APlayerStart> It(World); It; ++It)
    {
        APlayerStart* PS = *It;
        if (!PS) continue;
        FVector  Loc = PS->GetActorLocation();
        FRotator Rot = PS->GetActorRotation();
        if (Count > 0) Json += TEXT(",");
        Json += FString::Printf(
            TEXT("{\"index\":%d,\"name\":\"%s\","
                 "\"loc\":[%.1f,%.1f,%.1f],"
                 "\"rot\":[%.1f,%.1f,%.1f]}"),
            Count, *PS->GetName(),
            Loc.X, Loc.Y, Loc.Z,
            Rot.Pitch, Rot.Yaw, Rot.Roll);
        ++Count;
    }
    Json += TEXT("]");

    if (Count == 0)
        return TEXT("ERR:No APlayerStart actors found in the current level. "
                    "Drag one in from the Place Actors panel (Basic > Player Start).");

    return Json;
}

// ---------------------------------------------------------------------------
// SetLevelGameMode
// Command: SET_LEVEL_GAMEMODE|GameModeBPPath
//
// Sets AWorldSettings::DefaultGameMode for the currently open editor level.
// This overrides the project-level default for this level only.
// IMPORTANT: Save the level (File > Save Current Level) to persist the change.
// Pass "None" as GameModeBPPath to clear the override and use the project default.
// (UE 5.8: AWorldSettings.Modify() + MarkPackageDirty is the standard persist path
//  and is unchanged in 5.8 — no CommitMapChange() needed; the user still saves the
//  level to write it to disk. Stable AWorldSettings/UPackage API, compiles clean on 5.8.)
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetLevelGameMode(FString GameModeBPPath)
{
    if (!GEditor)
        return TEXT("ERR:GEditor not available");

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
        return TEXT("ERR:No editor world — open a level first");

    AWorldSettings* WS = World->GetWorldSettings();
    if (!WS)
        return TEXT("ERR:World has no WorldSettings actor");

    // Resolve the game mode class; allow "None" to clear the override
    UClass* GMClass = nullptr;
    if (!GameModeBPPath.IsEmpty() && GameModeBPPath != TEXT("None"))
    {
        FString ClassPath = GameModeBPPath;
        if (ClassPath.Contains(TEXT("/")) && !ClassPath.EndsWith(TEXT("_C")))
            ClassPath += TEXT("_C");
        GMClass = LoadClass<AGameModeBase>(nullptr, *ClassPath);
        if (!GMClass)
        {
            UBlueprint* GMBP = LoadObject<UBlueprint>(nullptr, *GameModeBPPath);
            if (GMBP && GMBP->GeneratedClass &&
                GMBP->GeneratedClass->IsChildOf(AGameModeBase::StaticClass()))
                GMClass = GMBP->GeneratedClass;
        }
        if (!GMClass)
            return FString::Printf(
                TEXT("ERR:GameMode class not found at '%s'. "
                     "Pass the Blueprint asset path e.g. /Game/BP_GameMode.BP_GameMode, "
                     "or 'None' to clear the level override."),
                *GameModeBPPath);
        if (!GMClass->IsChildOf(AGameModeBase::StaticClass()))
            return FString::Printf(
                TEXT("ERR:Class '%s' is not derived from AGameModeBase"),
                *GMClass->GetName());
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetLevelGM", "GraphBridge: Set Level GameMode"));
    WS->Modify();
    WS->DefaultGameMode = GMClass;
    World->MarkPackageDirty();

    FString SetTo = GMClass ? GMClass->GetPathName() : TEXT("(project default)");
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetLevelGameMode: '%s' -> %s. Save the level to persist."),
        *World->GetName(), *SetTo);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetCastTarget
// Command: SET_CAST_TARGET|BlueprintPath|NodeGUID|TargetClassName
//
// Sets TargetType on an existing UK2Node_DynamicCast and calls ReconstructNode
// so the typed output pin ("As [ClassName]") is correctly rebuilt.
//
// TargetClassName resolution order:
//   1. FindFirstObject<UClass> — handles short C++ names ("ACharacter") and
//      already-loaded Blueprint generated classes ("BP_Hero_C")
//   2. LoadClass<UObject> — handles full Blueprint paths ending in _C
//      e.g. "/Game/Characters/BP_Hero.BP_Hero_C"
//   3. Retry with _C suffix appended — handles paths without the generated-
//      class suffix e.g. "/Game/Characters/BP_Hero.BP_Hero"
//
// TargetType is TSubclassOf<UObject> on UK2Node_DynamicCast (confirmed UE 5.7 API docs).
// ReconstructNode rebuilds the "As [Type]" output pin; AllocateDefaultPins
// is for fresh-spawned nodes only and must NOT be called here.
// Header: K2Node_DynamicCast.h (BlueprintGraph module — already in Build.cs).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RunPython
// Command: RUN_PYTHON|PythonCode
//
// Executes Python inside UE via IPythonScriptPlugin and captures print()
// output by redirecting GLog temporarily. Returns captured output as payload.
// Pipes in the code are supported — the dispatcher rejoins split segments.
// ---------------------------------------------------------------------------

// Helper output device to capture UE log output during Python execution
class FGraphBridgePythonOutputDevice : public FOutputDevice
{
public:
    FString Output;
    virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        if (Category == FName("LogPython") || Category == FName("Python"))
            Output += FString(V) + TEXT("\n");
    }
    virtual bool CanBeUsedOnAnyThread() const override { return false; }
};

FString UGraphBridgeAutomationLibrary::RunPython(FString Code)
{
    IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
    if (!PythonPlugin || !PythonPlugin->IsPythonAvailable())
        return TEXT("ERR:Python plugin not available");

    FGraphBridgePythonOutputDevice Capture;
    GLog->AddOutputDevice(&Capture);

    PythonPlugin->ExecPythonCommand(*Code);

    GLog->RemoveOutputDevice(&Capture);
    Capture.Output.TrimEndInline();

    return Capture.Output.IsEmpty() ? TEXT("(no output)") : Capture.Output;
}

// ---------------------------------------------------------------------------

// ===========================================================================
// Variable management commands (v1.5)
// ===========================================================================

// ---------------------------------------------------------------------------
// AddVariable
// Command: ADD_VARIABLE|BPPath|VarName|VarType|Category(optional)
//
// Creates a new Blueprint member variable. VarType uses the same names as
// SPAWN_VARIABLE via ResolveTypeString(): bool, int32, float, FString, FName,
// FVector, FRotator, object:ClassName, class:ClassName, etc.
// If the variable name is already taken, returns ERR: and suggests SET_VARIABLE_TYPE.
// Returns the actual name used on success (FindUniqueKismetName may alter it).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddVariable(FString BlueprintPath,
    FString VarName, FString VarType, FString Category)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VarName)) != INDEX_NONE)
        return FString::Printf(
            TEXT("ERR:Variable '%s' already exists — use SET_VARIABLE_TYPE to retype it"), *VarName);

    FEdGraphPinType PinType = ResolveTypeString(VarType);
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        return FString::Printf(TEXT("ERR:Unknown type '%s'"), *VarType);

    FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, VarName);

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "AddVariable", "GraphBridge: Add Variable ({0})"),
                      FText::FromString(VarName)));
    Blueprint->Modify();

    FBlueprintEditorUtils::AddMemberVariable(Blueprint, UniqueName, PinType);

    int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, UniqueName);
    if (VarIdx != INDEX_NONE && !Category.IsEmpty())
        Blueprint->NewVariables[VarIdx].Category = FText::FromString(Category);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddVariable: '%s' (%s) in '%s'"),
        *UniqueName.ToString(), *VarType, *BlueprintPath);
    return UniqueName.ToString();
}

// ---------------------------------------------------------------------------
// SetVariableType
// Command: SET_VARIABLE_TYPE|BPPath|VarName|NewType
//
// Retypes an existing Blueprint member variable using
// FBlueprintEditorUtils::ChangeMemberVariableType, which handles disconnecting
// incompatible existing pin connections and updating dependent nodes.
// NewType uses ResolveTypeString() — same names as ADD_VARIABLE.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetVariableType(FString BlueprintPath,
    FString VarName, FString NewType)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    int32 VarIdx = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(*VarName));
    if (VarIdx == INDEX_NONE)
        return FString::Printf(
            TEXT("ERR:Variable '%s' not found — use LIST_VARIABLES to see available"), *VarName);

    FEdGraphPinType NewPinType = ResolveTypeString(NewType);
    if (NewPinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        return FString::Printf(TEXT("ERR:Unknown type '%s'"), *NewType);

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "SetVariableType", "GraphBridge: Set Variable Type ({0})"),
                      FText::FromString(VarName)));
    Blueprint->Modify();

    FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, FName(*VarName), NewPinType);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetVariableType: '%s' -> '%s' in '%s'"),
        *VarName, *NewType, *BlueprintPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// ListVariables
// Command: LIST_VARIABLES|BPPath
//
// Returns pipe-delimited entries from Blueprint->NewVariables:
//   VarName~PinCategory~PinSubCategory|VarName~...
// Only lists user-defined Blueprint member variables, not C++ properties or
// SCS component variables. Use GET_NODE_PINS after SET_VARIABLE_REF for those.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListVariables(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    TArray<FString> Entries;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        FString Name    = Var.VarName.ToString();
        FString Cat     = Var.VarType.PinCategory.ToString();
        FString SubCat  = Var.VarType.PinSubCategory.ToString();
        Entries.Add(FString::Printf(TEXT("%s~%s~%s"), *Name, *Cat, *SubCat));
    }
    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetCastTarget(
    FString BlueprintPath, FString NodeGUID, FString TargetClassName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeGUID);
    if (!Node)
        return FString::Printf(
            TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeGUID);

    UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node);
    if (!CastNode)
        return FString::Printf(
            TEXT("ERR:Node '%s' is not a UK2Node_DynamicCast (it is %s). "
                 "Spawn a cast node with SPAWN_NODE|%s|K2Node_DynamicCast first."),
            *NodeGUID, *Node->GetClass()->GetName(), *BlueprintPath);

    // --- Resolve the target class ---
    UClass* TargetClass = nullptr;

    // 1. Short name / already-loaded class (works for ACharacter, BP_Hero_C, etc.)
    TargetClass = FindFirstObject<UClass>(*TargetClassName, EFindFirstObjectOptions::None);

    // 2. Full path — LoadClass handles Blueprint generated class paths ending in _C
    if (!TargetClass)
        TargetClass = LoadClass<UObject>(nullptr, *TargetClassName);

    // 3. Retry with _C suffix for paths like /Game/Foo/BP_Hero.BP_Hero
    if (!TargetClass && !TargetClassName.EndsWith(TEXT("_C")))
        TargetClass = LoadClass<UObject>(nullptr, *(TargetClassName + TEXT("_C")));

    if (!TargetClass)
    {
        // Build a partial-match suggestion list to help self-correction
        TArray<FString> Suggestions;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->GetName().Contains(TargetClassName, ESearchCase::IgnoreCase))
                Suggestions.Add(It->GetPathName());
        }
        Suggestions.Sort();
        FString SuggestStr = Suggestions.Num() > 0
            ? FString::Join(TArrayView<FString>(Suggestions.GetData(), FMath::Min(5, Suggestions.Num())), TEXT(", "))
            : TEXT("(none found)");
        return FString::Printf(
            TEXT("ERR:Class '%s' not found. Partial matches: %s"),
            *TargetClassName, *SuggestStr);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetCastTarget", "GraphBridge: Set Cast Target"));
    CastNode->Modify();

    CastNode->TargetType = TargetClass;

    // ReconstructNode rebuilds the typed "As [ClassName]" output pin and
    // updates the node title. Do NOT call AllocateDefaultPins — that is for
    // fresh node construction only and will reset existing connections.
    CastNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetCastTarget: node '%s' TargetType = '%s'"),
        *NodeGUID, *TargetClass->GetName());
    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetSubsystemClass
// Command: SET_SUBSYSTEM_CLASS|BlueprintPath|NodeGUID|SubsystemClassName
//
// Sets CustomClass on an existing UK2Node_GetSubsystem (the node behind
// "Get Local Player Subsystem" / "Get World Subsystem" / etc. in the palette)
// and reconstructs its pins so the ReturnValue output is correctly typed.
//
// CustomClass is a protected UPROPERTY with no public setter except
// Initialize() — the same call the node's own palette customization uses
// (see UK2Node_GetSubsystem::GetMenuActions in K2Node_GetSubsystem.cpp) — so
// we call that instead of poking the field directly. ReconstructNode() then
// re-runs AllocateDefaultPins: since CustomClass is now set, the "Class"
// input pin is dropped and the ReturnValue pin's PinSubCategoryObject is set
// to CustomClass (see UK2Node_GetSubsystem::ReallocatePinsDuringReconstruction).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetSubsystemClass(
    FString BlueprintPath, FString NodeGUID, FString SubsystemClassName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeGUID);
    if (!Node)
        return FString::Printf(
            TEXT("ERR:Node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeGUID);

    UK2Node_GetSubsystem* SubsystemNode = Cast<UK2Node_GetSubsystem>(Node);
    if (!SubsystemNode)
        return FString::Printf(
            TEXT("ERR:Node '%s' is not a UK2Node_GetSubsystem (it is %s). "
                 "Spawn a 'Get Local Player Subsystem' node with SPAWN_NODE|%s|K2Node_GetSubsystem first."),
            *NodeGUID, *Node->GetClass()->GetName(), *BlueprintPath);

    // --- Resolve the subsystem class (same resolution order as SetCastTarget) ---
    UClass* FoundClass = nullptr;

    FoundClass = FindFirstObject<UClass>(*SubsystemClassName, EFindFirstObjectOptions::None);
    if (!FoundClass)
        FoundClass = LoadClass<UObject>(nullptr, *SubsystemClassName);
    if (!FoundClass && !SubsystemClassName.EndsWith(TEXT("_C")))
        FoundClass = LoadClass<UObject>(nullptr, *(SubsystemClassName + TEXT("_C")));

    if (!FoundClass || !FoundClass->IsChildOf(USubsystem::StaticClass()))
    {
        TArray<FString> Suggestions;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            if (It->IsChildOf(USubsystem::StaticClass()) &&
                It->GetName().Contains(SubsystemClassName, ESearchCase::IgnoreCase))
                Suggestions.Add(It->GetPathName());
        }
        Suggestions.Sort();
        FString SuggestStr = Suggestions.Num() > 0
            ? FString::Join(TArrayView<FString>(Suggestions.GetData(), FMath::Min(5, Suggestions.Num())), TEXT(", "))
            : TEXT("(none found)");
        return FString::Printf(
            TEXT("ERR:Subsystem class '%s' not found or is not a USubsystem. Partial matches: %s"),
            *SubsystemClassName, *SuggestStr);
    }

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetSubsystemClass", "GraphBridge: Set Subsystem Class"));
    SubsystemNode->Modify();

    SubsystemNode->Initialize(FoundClass);
    SubsystemNode->ReconstructNode();

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetSubsystemClass: node '%s' CustomClass = '%s'"),
        *NodeGUID, *FoundClass->GetName());
    return TEXT("");
}

// ===========================================================================
// Function graphs, node positioning, level actor placement (v1.7)
// ===========================================================================

// ---------------------------------------------------------------------------
// CreateFunction
// Command: CREATE_FUNCTION|BPPath|FunctionName
//
// Creates a new custom function graph in a Blueprint. Mirrors what the "My
// Blueprint" panel's "+Function" button does internally — confirmed against
// engine source (Editor/Kismet/Private/BlueprintEditor.cpp,
// FBlueprintEditor::CollapseSelectionToFunction, and
// Editor/BlueprintEditorLibrary/Private/BlueprintEditorLibrary.cpp,
// UBlueprintEditorLibrary::AddFunctionGraph): both call the same two-step
// CreateNewGraph + AddFunctionGraph<UClass> sequence used here.
// AddFunctionGraph internally calls the K2 schema's CreateDefaultNodesForGraph
// / CreateFunctionGraphTerminators, which spawns the UK2Node_FunctionEntry
// (and UK2Node_FunctionResult if the signature has return values) automatically
// — no manual entry-node construction needed.
// Returns the actual function name used (may differ if uniquified) on success.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateFunction(FString BlueprintPath, FString FunctionName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (FunctionName.IsEmpty())
        return TEXT("ERR:FunctionName cannot be empty");

    // Guard: duplicate function graph name
    for (UEdGraph* Existing : Blueprint->FunctionGraphs)
        if (Existing && Existing->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
            return FString::Printf(
                TEXT("ERR:Function '%s' already exists — use SPAWN_NODE_IN_GRAPH to add nodes to it"),
                *FunctionName);

    FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, FunctionName);

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "CreateFunction", "GraphBridge: Create Function ({0})"),
                      FText::FromString(FunctionName)));
    Blueprint->Modify();

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, UniqueName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
        return TEXT("ERR:FBlueprintEditorUtils::CreateNewGraph returned null");

    FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, /*bIsUserCreated=*/true, nullptr);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateFunction: '%s' in '%s'"),
        *UniqueName.ToString(), *BlueprintPath);
    return UniqueName.ToString();
}

// ---------------------------------------------------------------------------
// ListGraphs
// Command: LIST_GRAPHS|BPPath
// Returns pipe-delimited "GraphName~GraphType" entries for every graph on
// the Blueprint (GraphType: EventGraph|Function|Macro).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListGraphs(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    TArray<FString> Entries;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
        if (Graph)
            Entries.Add(FString::Printf(TEXT("%s~EventGraph"), *Graph->GetName()));
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph)
            Entries.Add(FString::Printf(TEXT("%s~Function"), *Graph->GetName()));
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
        if (Graph)
            Entries.Add(FString::Printf(TEXT("%s~Macro"), *Graph->GetName()));

    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
// CreateFunctionGraph
// Command: CREATE_FUNCTION_GRAPH|BPPath|FunctionName
// Reuses CreateFunction's graph-creation logic (not duplicated) but returns
// the new graph's K2Node_FunctionEntry GUID instead of the function name —
// CreateFunction itself is untouched since its existing callers rely on the
// function-name return value.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateFunctionGraph(FString BlueprintPath, FString FunctionName)
{
    FString NameResult = CreateFunction(BlueprintPath, FunctionName);
    if (NameResult.StartsWith(TEXT("ERR:")))
        return NameResult;

    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = FindGraphByName(Blueprint, NameResult);
    if (!Graph)
        return TEXT("ERR:Function graph created but could not be re-located");

    for (UEdGraphNode* Node : Graph->Nodes)
        if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node))
            return Entry->NodeGuid.ToString();

    return TEXT("ERR:Function graph created but no FunctionEntry node found");
}

// ---------------------------------------------------------------------------
// CreateMacroGraph
// Command: CREATE_MACRO_GRAPH|BPPath|MacroName
// Mirrors CreateFunction's CreateNewGraph pattern, but registers via
// FBlueprintEditorUtils::AddMacroGraph instead of AddFunctionGraph. Macro
// graphs are inlined at the call site rather than compiling to a real
// UFunction — confirmed against engine source (EdGraphSchema_K2.cpp,
// CreateMacroGraphTerminators) that their entry point is a UK2Node_Tunnel
// with bCanHaveOutputs=true (labeled "Inputs" in the editor), NOT a
// K2Node_FunctionEntry, so this returns that tunnel node's GUID.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateMacroGraph(FString BlueprintPath, FString MacroName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (MacroName.IsEmpty())
        return TEXT("ERR:MacroName cannot be empty");

    // Guard: duplicate macro graph name
    for (UEdGraph* Existing : Blueprint->MacroGraphs)
        if (Existing && Existing->GetName().Equals(MacroName, ESearchCase::IgnoreCase))
            return FString::Printf(
                TEXT("ERR:Macro '%s' already exists — use SPAWN_NODE with graph_name to add nodes to it"),
                *MacroName);

    FName UniqueName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, MacroName);

    const FScopedTransaction Transaction(
        FText::Format(NSLOCTEXT("GraphBridge", "CreateMacroGraph", "GraphBridge: Create Macro Graph ({0})"),
                      FText::FromString(MacroName)));
    Blueprint->Modify();

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, UniqueName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
        return TEXT("ERR:FBlueprintEditorUtils::CreateNewGraph returned null");

    FBlueprintEditorUtils::AddMacroGraph(Blueprint, NewGraph, /*bIsUserCreated=*/true, nullptr);

    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

    for (UEdGraphNode* Node : NewGraph->Nodes)
        if (UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node))
            if (Tunnel->bCanHaveOutputs)
                return Tunnel->NodeGuid.ToString();

    return TEXT("ERR:Macro graph created but no entry tunnel node found");
}

// ---------------------------------------------------------------------------
// CreateInputAction
// Command: CREATE_INPUT_ACTION|AssetPath|ValueType
// ValueType: bool, float, axis1d, axis2d, axis3d ("float" is an alias for
// axis1d — EInputActionValueType::Axis1D is backed by a single float).
//
// Mirrors CreateIMC's package-creation pattern exactly (CreatePackage ->
// NewObject -> AssetCreated -> SavePackage) — UInputAction and
// UInputMappingContext are both plain UObject assets with no factory-driven
// construction requirements, unlike Blueprints.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateInputAction(FString AssetPath, FString ValueType)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UInputAction>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An InputAction already exists at '%s'"), *AssetPath);

    EInputActionValueType ResolvedType;
    if (ValueType.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
        ResolvedType = EInputActionValueType::Boolean;
    else if (ValueType.Equals(TEXT("float"), ESearchCase::IgnoreCase) ||
             ValueType.Equals(TEXT("axis1d"), ESearchCase::IgnoreCase))
        ResolvedType = EInputActionValueType::Axis1D;
    else if (ValueType.Equals(TEXT("axis2d"), ESearchCase::IgnoreCase))
        ResolvedType = EInputActionValueType::Axis2D;
    else if (ValueType.Equals(TEXT("axis3d"), ESearchCase::IgnoreCase))
        ResolvedType = EInputActionValueType::Axis3D;
    else
        return FString::Printf(
            TEXT("ERR:Unknown ValueType '%s'. Use: bool, float, axis1d, axis2d, axis3d"), *ValueType);

    // Derive package name and asset name.
    // Accept both /Game/Foo/Bar  and  /Game/Foo/Bar.Bar  forms.
    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateInputAction", "GraphBridge: Create Input Action"));

    UInputAction* NewAction = NewObject<UInputAction>(
        NewPackage, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!NewAction)
        return TEXT("ERR:NewObject<UInputAction> returned null");

    NewAction->ValueType = ResolvedType;

    FAssetRegistryModule::AssetCreated(NewAction);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewAction, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:InputAction created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateInputAction: '%s' (ValueType: %s)"),
        *AssetPath, *ValueType);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// SpawnActorInLevel
// Command: SPAWN_ACTOR_IN_LEVEL|BlueprintPath|X|Y|Z|RotYaw
//
// Places a Blueprint actor instance in the current editor level (not the PIE
// game world) via UEditorActorSubsystem — the modern, non-deprecated
// replacement for UEditorLevelLibrary::SpawnActorFromClass. Confirmed against
// engine source (Editor/UnrealEd/Public/Subsystems/EditorActorSubsystem.h)
// and the UE 5.7 Python API's own runtime deprecation warning on
// EditorLevelLibrary in favor of the editor-subsystem equivalents.
// Returns the spawned actor's auto-assigned label on success.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SpawnActorInLevel(FString BlueprintPath, float X, float Y, float Z, float RotYaw)
{
    if (!GEditor)
        return TEXT("ERR:GEditor not available (not running in editor)");

    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UClass* GenClass = Blueprint->GeneratedClass;
    if (!GenClass || !GenClass->IsChildOf(AActor::StaticClass()))
        return FString::Printf(
            TEXT("ERR:Blueprint '%s' does not generate an AActor subclass"), *BlueprintPath);

    UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
    if (!ActorSubsystem)
        return TEXT("ERR:UEditorActorSubsystem not available");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SpawnActorInLevel", "GraphBridge: Spawn Actor In Level"));

    AActor* NewActor = ActorSubsystem->SpawnActorFromClass(
        TSubclassOf<AActor>(GenClass), FVector(X, Y, Z), FRotator(0.f, RotYaw, 0.f));
    if (!NewActor)
        return FString::Printf(
            TEXT("ERR:SpawnActorFromClass returned null for '%s' — is a level open in the editor?"),
            *BlueprintPath);

    GEditor->RedrawLevelEditingViewports();

    UE_LOG(LogGraphBridge, Log,
        TEXT("GraphBridge SpawnActorInLevel: '%s' at (%.1f,%.1f,%.1f) yaw=%.1f -> label '%s'"),
        *BlueprintPath, X, Y, Z, RotYaw, *NewActor->GetActorLabel());
    return NewActor->GetActorLabel();
}

// ---------------------------------------------------------------------------
// GetEditorActorSubsystem — shared lookup used by the level-actor commands.
// ---------------------------------------------------------------------------
UEditorActorSubsystem* UGraphBridgeAutomationLibrary::GetEditorActorSubsystem()
{
    return GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------
// ListLevelActors
// Command: LIST_LEVEL_ACTORS|Filter
// Filter is optional — empty returns every actor in the current level.
// Returns pipe-delimited "Label~Class~X~Y~Z" entries.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListLevelActors(FString Filter)
{
    UEditorActorSubsystem* ActorSubsystem = GetEditorActorSubsystem();
    if (!ActorSubsystem)
        return TEXT("ERR:UEditorActorSubsystem not available");

    TArray<FString> Entries;
    for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
    {
        if (!Actor) continue;

        FString Label = Actor->GetActorLabel();
        FString ClassName = Actor->GetClass()->GetName();

        if (!Filter.IsEmpty() &&
            !Label.Contains(Filter, ESearchCase::IgnoreCase) &&
            !ClassName.Contains(Filter, ESearchCase::IgnoreCase))
            continue;

        FVector Loc = Actor->GetActorLocation();
        Entries.Add(FString::Printf(TEXT("%s~%s~%.2f~%.2f~%.2f"),
            *Label, *ClassName, Loc.X, Loc.Y, Loc.Z));
    }
    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
// FindLevelActorByLabel — case-insensitive exact match on GetActorLabel().
// ---------------------------------------------------------------------------
AActor* UGraphBridgeAutomationLibrary::FindLevelActorByLabel(const FString& ActorLabel)
{
    UEditorActorSubsystem* ActorSubsystem = GetEditorActorSubsystem();
    if (!ActorSubsystem) return nullptr;

    for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
        if (Actor && Actor->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
            return Actor;

    return nullptr;
}

// ---------------------------------------------------------------------------
// SetActorTransform
// Command: SET_ACTOR_TRANSFORM|ActorLabel|X|Y|Z|Pitch|Yaw|Roll|SX|SY|SZ
// SX/SY/SZ default to 1.0 if omitted.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetActorTransform(
    FString ActorLabel, FVector Location, FRotator Rotation, FVector Scale)
{
    UEditorActorSubsystem* ActorSubsystem = GetEditorActorSubsystem();
    if (!ActorSubsystem)
        return TEXT("ERR:UEditorActorSubsystem not available");

    AActor* Actor = FindLevelActorByLabel(ActorLabel);
    if (!Actor)
        return FString::Printf(
            TEXT("ERR:Actor with label '%s' not found — run LIST_LEVEL_ACTORS to see available actors"),
            *ActorLabel);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetActorTransform", "GraphBridge: Set Actor Transform"));

    const FTransform NewTransform(Rotation, Location, Scale);
    if (!ActorSubsystem->SetActorTransform(Actor, NewTransform))
        return FString::Printf(TEXT("ERR:SetActorTransform failed for '%s'"), *ActorLabel);

    GEditor->RedrawLevelEditingViewports();

    UE_LOG(LogGraphBridge, Log,
        TEXT("GraphBridge SetActorTransform: '%s' -> loc=(%.1f,%.1f,%.1f) rot=(%.1f,%.1f,%.1f) scale=(%.2f,%.2f,%.2f)"),
        *ActorLabel, Location.X, Location.Y, Location.Z,
        Rotation.Pitch, Rotation.Yaw, Rotation.Roll, Scale.X, Scale.Y, Scale.Z);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// DeleteLevelActor
// Command: DELETE_LEVEL_ACTOR|ActorLabel
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::DeleteLevelActor(FString ActorLabel)
{
    UEditorActorSubsystem* ActorSubsystem = GetEditorActorSubsystem();
    if (!ActorSubsystem)
        return TEXT("ERR:UEditorActorSubsystem not available");

    AActor* Actor = FindLevelActorByLabel(ActorLabel);
    if (!Actor)
        return FString::Printf(
            TEXT("ERR:Actor with label '%s' not found — run LIST_LEVEL_ACTORS to see available actors"),
            *ActorLabel);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "DeleteLevelActor", "GraphBridge: Delete Level Actor"));

    if (!ActorSubsystem->DestroyActor(Actor))
        return FString::Printf(TEXT("ERR:DestroyActor failed for '%s'"), *ActorLabel);

    GEditor->RedrawLevelEditingViewports();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge DeleteLevelActor: '%s' removed"), *ActorLabel);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateWidgetBlueprint
// Command: CREATE_WIDGET_BLUEPRINT|AssetPath
//
// Creates a new UMG Widget Blueprint (UUserWidget parent) via
// UWidgetBlueprintFactory — the same factory the Content Browser's
// "Widget Blueprint" menu item uses. FactoryCreateNew is called directly
// (bypassing ConfigureProperties, which would otherwise pop up the parent-
// class picker dialog UI) with ParentClass pre-set to UUserWidget.
//
// UUMGEditorProjectSettings::DefaultRootWidget may be unset in a fresh
// project, in which case the factory leaves WidgetTree->RootWidget null —
// so we explicitly construct a UCanvasPanel root if one wasn't created,
// guaranteeing ADD_WIDGET_ELEMENT always has a canvas to add children to.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateWidgetBlueprint(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UWidgetBlueprint>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Widget Blueprint already exists at '%s'"), *AssetPath);

    // Derive package name and asset name.
    // Accept both /Game/Foo/Bar  and  /Game/Foo/Bar.Bar  forms.
    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateWidgetBlueprint", "GraphBridge: Create Widget Blueprint"));

    UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
    Factory->ParentClass   = UUserWidget::StaticClass();
    Factory->BlueprintType = BPTYPE_Normal;

    UWidgetBlueprint* NewBP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
        UWidgetBlueprint::StaticClass(), NewPackage, FName(*AssetName),
        RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!NewBP)
        return TEXT("ERR:UWidgetBlueprintFactory::FactoryCreateNew returned null");

    if (!NewBP->WidgetTree->RootWidget)
    {
        UWidget* RootCanvas = NewBP->WidgetTree->ConstructWidget<UWidget>(UCanvasPanel::StaticClass());
        NewBP->WidgetTree->RootWidget = RootCanvas;
        // Matches UWidgetBlueprintFactory::FactoryCreateNew's own handling of
        // RootWidgetClass — without this the compiler's variable-GUID
        // validation fires an ensure the first time this Blueprint compiles.
        NewBP->OnVariableAdded(RootCanvas->GetFName());
    }

    FAssetRegistryModule::AssetCreated(NewBP);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewBP, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Widget Blueprint created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateWidgetBlueprint: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddWidgetElement
// Command: ADD_WIDGET_ELEMENT|WidgetBPPath|ElementType|Name|X|Y|W|H
//
// Constructs a new UWidget of ElementType via WidgetTree->ConstructWidget
// (giving it Name so SET_WIDGET_TEXT / FindWidget can locate it later) and
// adds it as a child of the Widget Blueprint's root canvas panel, positioned
// via the returned UCanvasPanelSlot.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddWidgetElement(FString WidgetBPPath, FString ElementType,
    FString Name, int32 X, int32 Y, int32 W, int32 H)
{
    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBPPath);
    if (!WidgetBP)
        return FString::Printf(TEXT("ERR:Widget Blueprint not found at '%s'"), *WidgetBPPath);

    if (!WidgetBP->WidgetTree)
        return TEXT("ERR:Widget Blueprint has no WidgetTree");

    UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBP->WidgetTree->RootWidget);
    if (!RootCanvas)
        return TEXT("ERR:Widget Blueprint's root widget is not a Canvas Panel");

    UClass* WidgetClass = nullptr;
    if (ElementType == TEXT("Button"))            WidgetClass = UButton::StaticClass();
    else if (ElementType == TEXT("Text"))         WidgetClass = UTextBlock::StaticClass();
    else if (ElementType == TEXT("Image"))        WidgetClass = UImage::StaticClass();
    else if (ElementType == TEXT("ProgressBar"))  WidgetClass = UProgressBar::StaticClass();
    else if (ElementType == TEXT("VerticalBox"))  WidgetClass = UVerticalBox::StaticClass();
    else if (ElementType == TEXT("HorizontalBox"))WidgetClass = UHorizontalBox::StaticClass();
    else if (ElementType == TEXT("Canvas"))       WidgetClass = UCanvasPanel::StaticClass();
    else if (ElementType == TEXT("Overlay"))      WidgetClass = UOverlay::StaticClass();
    else if (ElementType == TEXT("Border"))       WidgetClass = UBorder::StaticClass();
    else if (ElementType == TEXT("Slider"))       WidgetClass = USlider::StaticClass();
    else if (ElementType == TEXT("EditableText")) WidgetClass = UEditableText::StaticClass();
    else
        return FString::Printf(
            TEXT("ERR:Unknown ElementType '%s'. Valid: Button, Text, Image, ProgressBar, ")
            TEXT("VerticalBox, HorizontalBox, Canvas, Overlay, Border, Slider, EditableText"),
            *ElementType);

    if (WidgetBP->WidgetTree->FindWidget(FName(*Name)))
        return FString::Printf(TEXT("ERR:A widget named '%s' already exists"), *Name);

    // Close the Widget Blueprint editor (if open) before mutating the tree —
    // the Designer/preview viewport ticks against the WidgetTree and can
    // crash the editor if it renders a widget mid-construction, exactly like
    // the SCS preview viewport issue SPAWN_NODE's CLOSE_BLUEPRINT works around.
    if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        Sub->CloseAllEditorsForAsset(WidgetBP);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddWidgetElement", "GraphBridge: Add Widget Element"));
    WidgetBP->Modify();

    UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*Name));
    if (!NewWidget)
        return TEXT("ERR:WidgetTree->ConstructWidget returned null");

    UPanelSlot* NewSlot = RootCanvas->AddChild(NewWidget);
    if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(NewSlot))
    {
        CanvasSlot->SetPosition(FVector2D((float)X, (float)Y));
        CanvasSlot->SetSize(FVector2D((float)W, (float)H));
    }

    // The compiler's ValidateAndFixUpVariableGuids step asserts every named
    // widget in the tree has an entry in WidgetVariableNameToGuidMap — normally
    // populated by the Designer UI when you drag a widget in. We must register
    // it ourselves here, exactly like UWidgetBlueprintFactory does for the
    // root widget it creates, or CompileBlueprint below fires an ensure.
    WidgetBP->OnVariableAdded(NewWidget->GetFName());

    FKismetEditorUtilities::CompileBlueprint(WidgetBP);
    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddWidgetElement: '%s' (%s) added to '%s'"),
        *Name, *ElementType, *WidgetBPPath);
    return Name;
}

// ---------------------------------------------------------------------------
// SetWidgetText
// Command: SET_WIDGET_TEXT|WidgetBPPath|ElementName|Text
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetWidgetText(FString WidgetBPPath, FString ElementName, FString Text)
{
    UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBPPath);
    if (!WidgetBP)
        return FString::Printf(TEXT("ERR:Widget Blueprint not found at '%s'"), *WidgetBPPath);

    if (!WidgetBP->WidgetTree)
        return TEXT("ERR:Widget Blueprint has no WidgetTree");

    UWidget* Found = WidgetBP->WidgetTree->FindWidget(FName(*ElementName));
    if (!Found)
        return FString::Printf(TEXT("ERR:Widget '%s' not found"), *ElementName);

    if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        Sub->CloseAllEditorsForAsset(WidgetBP);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetWidgetText", "GraphBridge: Set Widget Text"));
    Found->Modify();

    const FText NewText = FText::FromString(Text);
    if (UTextBlock* TextBlock = Cast<UTextBlock>(Found))
        TextBlock->SetText(NewText);
    else if (UEditableText* EditableText = Cast<UEditableText>(Found))
        EditableText->SetText(NewText);
    else if (UButton* Button = Cast<UButton>(Found))
        return FString::Printf(TEXT("ERR:'%s' is a Button — add a Text child element and set its text instead"), *ElementName);
    else
        return FString::Printf(TEXT("ERR:'%s' (%s) has no settable text"), *ElementName, *Found->GetClass()->GetName());

    FKismetEditorUtilities::CompileBlueprint(WidgetBP);
    FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetWidgetText: '%s' -> '%s'"), *ElementName, *Text);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateMaterial
// Command: CREATE_MATERIAL|AssetPath|BlendMode
// BlendMode: Opaque, Translucent, Masked, Additive
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateMaterial(FString AssetPath, FString BlendModeStr)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UMaterial>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Material already exists at '%s'"), *AssetPath);

    EBlendMode Blend;
    if (BlendModeStr == TEXT("Opaque"))            Blend = BLEND_Opaque;
    else if (BlendModeStr == TEXT("Translucent"))  Blend = BLEND_Translucent;
    else if (BlendModeStr == TEXT("Masked"))       Blend = BLEND_Masked;
    else if (BlendModeStr == TEXT("Additive"))     Blend = BLEND_Additive;
    else
        return FString::Printf(
            TEXT("ERR:Unknown BlendMode '%s'. Valid: Opaque, Translucent, Masked, Additive"), *BlendModeStr);

    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateMaterial", "GraphBridge: Create Material"));

    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    UMaterial* NewMaterial = Cast<UMaterial>(Factory->FactoryCreateNew(
        UMaterial::StaticClass(), NewPackage, FName(*AssetName),
        RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!NewMaterial)
        return TEXT("ERR:UMaterialFactoryNew::FactoryCreateNew returned null");

    NewMaterial->BlendMode = Blend;

    FAssetRegistryModule::AssetCreated(NewMaterial);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewMaterial, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Material created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateMaterial: '%s' (BlendMode=%s)"),
        *AssetPath, *BlendModeStr);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddMaterialNode
// Command: ADD_MATERIAL_NODE|MaterialPath|NodeType|X|Y
//
// Uses UMaterialEditingLibrary::CreateMaterialExpression, the sanctioned
// scripting entry point for material graph editing — it handles NewObject,
// adding to the material's FMaterialExpressionCollection, and editor
// position in one call. Returns the node's index in the expression array
// (stable for the lifetime of this editing session — used by
// CONNECT_MATERIAL_PINS / SET_MATERIAL_RESULT to address the node).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddMaterialNode(FString MaterialPath, FString NodeType, int32 X, int32 Y)
{
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Material)
        return FString::Printf(TEXT("ERR:Material not found at '%s'"), *MaterialPath);

    TSubclassOf<UMaterialExpression> ExpressionClass;
    if (NodeType == TEXT("Multiply"))       ExpressionClass = UMaterialExpressionMultiply::StaticClass();
    else if (NodeType == TEXT("Add"))       ExpressionClass = UMaterialExpressionAdd::StaticClass();
    else if (NodeType == TEXT("Lerp"))      ExpressionClass = UMaterialExpressionLinearInterpolate::StaticClass();
    else if (NodeType == TEXT("Texture"))   ExpressionClass = UMaterialExpressionTextureSample::StaticClass();
    else if (NodeType == TEXT("Constant"))  ExpressionClass = UMaterialExpressionConstant::StaticClass();
    else if (NodeType == TEXT("Constant3")) ExpressionClass = UMaterialExpressionConstant3Vector::StaticClass();
    else if (NodeType == TEXT("Param"))     ExpressionClass = UMaterialExpressionScalarParameter::StaticClass();
    else if (NodeType == TEXT("Param3"))    ExpressionClass = UMaterialExpressionVectorParameter::StaticClass();
    else if (NodeType == TEXT("Fresnel"))   ExpressionClass = UMaterialExpressionFresnel::StaticClass();
    else if (NodeType == TEXT("Time"))      ExpressionClass = UMaterialExpressionTime::StaticClass();
    else
        return FString::Printf(
            TEXT("ERR:Unknown NodeType '%s'. Valid: Multiply, Add, Lerp, Texture, Constant, ")
            TEXT("Constant3, Param, Param3, Fresnel, Time"), *NodeType);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddMaterialNode", "GraphBridge: Add Material Node"));

    UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(
        Material, ExpressionClass, X, Y);
    if (!NewExpr)
        return TEXT("ERR:UMaterialEditingLibrary::CreateMaterialExpression returned null");

    const int32 NodeIndex = Material->GetExpressionCollection().Expressions.Find(NewExpr);
    if (NodeIndex == INDEX_NONE)
        return TEXT("ERR:Expression created but not found in the material's expression collection");

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddMaterialNode: '%s' node %d in '%s'"),
        *NodeType, NodeIndex, *MaterialPath);
    return FString::FromInt(NodeIndex);
}

// ---------------------------------------------------------------------------
// ConnectMaterialPins
// Command: CONNECT_MATERIAL_PINS|MaterialPath|NodeIndexA|OutputPin|NodeIndexB|InputPin
// OutputPin/InputPin: pin name, or "_" to use the first pin.
//
// "_" (rather than an empty string) is required here because the shared
// command parser (ExecuteAtomicCommand) splits on '|' with CullEmpty=true,
// which silently drops empty segments and misaligns every argument after
// them — so an empty pin name in the middle of this pipe-delimited command
// can never survive parsing.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ConnectMaterialPins(FString MaterialPath, int32 NodeIndexA,
    FString OutputPin, int32 NodeIndexB, FString InputPin)
{
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Material)
        return FString::Printf(TEXT("ERR:Material not found at '%s'"), *MaterialPath);

    if (OutputPin == TEXT("_")) OutputPin.Empty();
    if (InputPin == TEXT("_")) InputPin.Empty();

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
    if (!Expressions.IsValidIndex(NodeIndexA))
        return FString::Printf(TEXT("ERR:NodeIndexA %d out of range (0..%d)"), NodeIndexA, Expressions.Num() - 1);
    if (!Expressions.IsValidIndex(NodeIndexB))
        return FString::Printf(TEXT("ERR:NodeIndexB %d out of range (0..%d)"), NodeIndexB, Expressions.Num() - 1);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "ConnectMaterialPins", "GraphBridge: Connect Material Pins"));

    const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(
        Expressions[NodeIndexA], OutputPin, Expressions[NodeIndexB], InputPin);
    if (!bOk)
        return FString::Printf(
            TEXT("ERR:Connect failed — check OutputPin '%s' exists on node %d and InputPin '%s' exists on node %d"),
            *OutputPin, NodeIndexA, *InputPin, NodeIndexB);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge ConnectMaterialPins: %d.%s -> %d.%s in '%s'"),
        NodeIndexA, *OutputPin, NodeIndexB, *InputPin, *MaterialPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetMaterialResult
// Command: SET_MATERIAL_RESULT|MaterialPath|Channel|NodeIndex|OutputPin
// Channel: BaseColor, Metallic, Roughness, Normal, Emissive, Opacity,
//          OpacityMask, WorldPositionOffset
// OutputPin: pin name, or "_" to use the first pin (see ConnectMaterialPins
// for why "_" is required instead of an empty string).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetMaterialResult(FString MaterialPath, FString Channel,
    int32 NodeIndex, FString OutputPin)
{
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Material)
        return FString::Printf(TEXT("ERR:Material not found at '%s'"), *MaterialPath);

    if (OutputPin == TEXT("_")) OutputPin.Empty();

    EMaterialProperty Property;
    if (Channel == TEXT("BaseColor"))              Property = MP_BaseColor;
    else if (Channel == TEXT("Metallic"))          Property = MP_Metallic;
    else if (Channel == TEXT("Roughness"))         Property = MP_Roughness;
    else if (Channel == TEXT("Normal"))            Property = MP_Normal;
    else if (Channel == TEXT("Emissive"))          Property = MP_EmissiveColor;
    else if (Channel == TEXT("Opacity"))           Property = MP_Opacity;
    else if (Channel == TEXT("OpacityMask"))       Property = MP_OpacityMask;
    else if (Channel == TEXT("WorldPositionOffset"))Property = MP_WorldPositionOffset;
    else
        return FString::Printf(
            TEXT("ERR:Unknown Channel '%s'. Valid: BaseColor, Metallic, Roughness, Normal, ")
            TEXT("Emissive, Opacity, OpacityMask, WorldPositionOffset"), *Channel);

    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressionCollection().Expressions;
    if (!Expressions.IsValidIndex(NodeIndex))
        return FString::Printf(TEXT("ERR:NodeIndex %d out of range (0..%d)"), NodeIndex, Expressions.Num() - 1);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetMaterialResult", "GraphBridge: Set Material Result"));

    const bool bOk = UMaterialEditingLibrary::ConnectMaterialProperty(
        Expressions[NodeIndex], OutputPin, Property);
    if (!bOk)
        return FString::Printf(
            TEXT("ERR:Connect failed — check OutputPin '%s' exists on node %d"), *OutputPin, NodeIndex);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetMaterialResult: node %d.%s -> %s in '%s'"),
        NodeIndex, *OutputPin, *Channel, *MaterialPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CompileMaterial
// Command: COMPILE_MATERIAL|MaterialPath
//
// RecompileMaterial() queues shader compile jobs asynchronously; we force
// them to finish synchronously via GShaderCompilingManager so the compile
// errors are available immediately for the response, matching the
// synchronous CLEAN/ERROR:msg convention GET_COMPILE_ERRORS uses for
// Blueprints.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CompileMaterial(FString MaterialPath)
{
    UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
    if (!Material)
        return FString::Printf(TEXT("ERR:Material not found at '%s'"), *MaterialPath);

    UMaterialEditingLibrary::RecompileMaterial(Material);

    if (GShaderCompilingManager)
        GShaderCompilingManager->FinishAllCompilation();

    TArray<FString> Entries;
    if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
    {
        for (const FString& Err : Resource->GetCompileErrors())
        {
            FString Sanitised = Err;
            Sanitised.ReplaceInline(TEXT("|"), TEXT(" "));
            Sanitised.ReplaceInline(TEXT("\n"), TEXT(" "));
            Sanitised.ReplaceInline(TEXT("\r"), TEXT(""));
            Entries.Add(FString::Printf(TEXT("ERROR:%s"), *Sanitised));
        }
    }

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CompileMaterial: '%s' -> %s"),
        *MaterialPath, Entries.Num() > 0 ? TEXT("errors") : TEXT("clean"));
    return Entries.Num() > 0 ? FString::Join(Entries, TEXT("|")) : TEXT("CLEAN");
}

// ===========================================================================
// Blueprint completeness — enums, structs, function libraries (v1.10)
// ===========================================================================

// ---------------------------------------------------------------------------
// CreateEnum
// Command: CREATE_ENUM|AssetPath|Name1,Name2,Name3,...
//
// FEnumEditorUtils::CreateUserDefinedEnum mirrors CreateIMC/CreateBlueprint's
// package-creation pattern. AddNewEnumeratorForUserDefinedEnum takes no name
// argument — confirmed against Kismet2/EnumEditorUtils.cpp, it appends a new
// enumerator with an auto-generated short name right before the implicit
// _MAX sentinel, so immediately after the call the new enumerator's index is
// Enum->NumEnums() - 2 (-1 for _MAX, -1 for 0-based). SetEnumeratorDisplayName
// is what the Blueprint enum editor UI itself calls when you type a new name
// into an enumerator row — it only touches the user-facing DisplayNameMap,
// not the internal auto-generated short name, which is expected.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateEnum(FString AssetPath, FString CommaSeparatedNames)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    TArray<FString> Names;
    CommaSeparatedNames.ParseIntoArray(Names, TEXT(","), /*bCullEmpty=*/true);
    for (FString& Name : Names)
        Name.TrimStartAndEndInline();
    Names.RemoveAll([](const FString& N) { return N.IsEmpty(); });

    if (Names.IsEmpty())
        return TEXT("ERR:At least one enumerator name is required");

    if (LoadObject<UUserDefinedEnum>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An Enum already exists at '%s'"), *AssetPath);

    // Derive package name and asset name.
    // Accept both /Game/Foo/Bar  and  /Game/Foo/Bar.Bar  forms.
    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateEnum", "GraphBridge: Create Enum"));

    UEnum* NewEnumBase = FEnumEditorUtils::CreateUserDefinedEnum(NewPackage, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(NewEnumBase);
    if (!NewEnum)
        return TEXT("ERR:FEnumEditorUtils::CreateUserDefinedEnum returned null");

    for (const FString& Name : Names)
    {
        FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);
        const int32 NewIndex = NewEnum->NumEnums() - 2;
        FEnumEditorUtils::SetEnumeratorDisplayName(NewEnum, NewIndex, FText::FromString(Name));
    }

    FAssetRegistryModule::AssetCreated(NewEnum);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewEnum, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Enum created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateEnum: '%s' with %d enumerator(s)"),
        *AssetPath, Names.Num());
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// CreateStruct
// Command: CREATE_STRUCT|AssetPath
//
// FStructureEditorUtils::CreateUserDefinedStruct already seeds one default
// bool member internally (confirmed against Kismet2/StructureEditorUtils.cpp,
// CreateUserDefinedStruct() itself calls AddVariable() once) — this is
// intentional engine behavior (UUserDefinedStruct assumes at least one
// variable exists in various editor code paths), not stripped out here.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateStruct(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UUserDefinedStruct>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Struct already exists at '%s'"), *AssetPath);

    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateStruct", "GraphBridge: Create Struct"));

    UUserDefinedStruct* NewStruct = FStructureEditorUtils::CreateUserDefinedStruct(
        NewPackage, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!NewStruct)
        return TEXT("ERR:FStructureEditorUtils::CreateUserDefinedStruct returned null");

    FAssetRegistryModule::AssetCreated(NewStruct);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewStruct, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Struct created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateStruct: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddStructMember
// Command: ADD_STRUCT_MEMBER|StructAssetPath|MemberName|MemberType
//
// FStructureEditorUtils::AddVariable appends a new FStructVariableDescription
// to the end of GetVarDesc(Struct) with a fresh, freshly-generated Guid and
// auto-generated name (confirmed against StructureEditorUtils.cpp) — so the
// new variable's Guid is always GetVarDesc(Struct).Last().VarGuid immediately
// after a successful AddVariable call. RenameVariable(Struct, Guid, NewName)
// then renames it to the caller's requested MemberName. OnStructureChanged
// (called inside AddVariable) already triggers CompileStructure internally,
// so no separate compile step is needed here.
// MemberType uses the same names as ResolveTypeString (SPAWN_VARIABLE/
// ADD_VARIABLE) — reused rather than re-implementing the type mapping.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddStructMember(FString StructAssetPath, FString MemberName, FString MemberType)
{
    UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, *StructAssetPath);
    if (!Struct)
        return FString::Printf(TEXT("ERR:Struct not found at '%s'"), *StructAssetPath);

    if (MemberName.IsEmpty())
        return TEXT("ERR:MemberName cannot be empty");

    FEdGraphPinType PinType = ResolveTypeString(MemberType);
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        return FString::Printf(TEXT("ERR:Unknown MemberType '%s'"), *MemberType);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddStructMember", "GraphBridge: Add Struct Member"));

    if (!FStructureEditorUtils::AddVariable(Struct, PinType))
        return TEXT("ERR:FStructureEditorUtils::AddVariable failed — check the type is supported for struct members");

    const TArray<FStructVariableDescription>& VarDesc = FStructureEditorUtils::GetVarDesc(Struct);
    if (VarDesc.Num() == 0)
        return TEXT("ERR:AddVariable succeeded but no variable was found afterward");

    const FGuid NewGuid = VarDesc.Last().VarGuid;
    if (!FStructureEditorUtils::RenameVariable(Struct, NewGuid, MemberName))
        return FString::Printf(
            TEXT("ERR:Variable added but rename to '%s' failed (name may already be in use)"), *MemberName);

    Struct->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddStructMember: '%s' (%s) added to '%s'"),
        *MemberName, *MemberType, *StructAssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateFunctionLibrary
// Command: CREATE_FUNCTION_LIBRARY|AssetPath
//
// NOT a reuse of CreateBlueprint — that was tried first and rejected live
// ("Cannot create a Blueprint based on class 'BlueprintFunctionLibrary'")
// because UBlueprintFunctionLibrary has no IsBlueprintBase metadata, which
// FKismetEditorUtilities::CanCreateBlueprintOfClass requires. Confirmed
// against the engine's own UBlueprintFunctionLibraryFactory::FactoryCreateNew
// (Editor/UnrealEd/Private/Factories/EditorFactories.cpp): it calls
// FKismetEditorUtilities::CreateBlueprint directly with
// BlueprintType = BPTYPE_FunctionLibrary, bypassing CanCreateBlueprintOfClass
// entirely — this function does exactly the same thing.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateFunctionLibrary(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");

    if (LoadObject<UBlueprint>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Blueprint already exists at '%s'"), *AssetPath);

    FString PackageName = AssetPath;
    FString AssetName;
    int32 DotIdx;
    if (PackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        AssetName   = PackageName.Mid(DotIdx + 1);
        PackageName = PackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        AssetName = PackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? PackageName.Mid(SlashIdx + 1)
            : PackageName;
    }
    if (AssetName.IsEmpty())
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateFunctionLibrary", "GraphBridge: Create Function Library"));

    UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
        UBlueprintFunctionLibrary::StaticClass(), NewPackage, FName(*AssetName), BPTYPE_FunctionLibrary,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName("GraphBridge"));
    if (!NewBP)
        return TEXT("ERR:FKismetEditorUtilities::CreateBlueprint returned null for BlueprintFunctionLibrary");

    FAssetRegistryModule::AssetCreated(NewBP);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewBP, *PackageFilename, SaveArgs))
        return FString::Printf(
            TEXT("ERR:Function Library created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateFunctionLibrary: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddLocalVariable
// Command: ADD_LOCAL_VARIABLE|BPPath|FunctionGraphName|VarName|VarType|DefaultValue
//
// Adds a variable scoped to a single function graph via
// FBlueprintEditorUtils::AddLocalVariable. FunctionGraphName is resolved via
// FindGraphByName — the same lookup SPAWN_NODE_IN_GRAPH already uses.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddLocalVariable(FString BlueprintPath, FString FunctionGraphName,
    FString VarName, FString VarType, FString DefaultValue)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = FindGraphByName(Blueprint, FunctionGraphName);
    if (!Graph)
    {
        TArray<FString> Available;
        Available.Add(TEXT("EventGraph"));
        for (UEdGraph* G : Blueprint->FunctionGraphs)
            if (G) Available.Add(G->GetName());
        return FString::Printf(
            TEXT("ERR:Graph '%s' not found. Available graphs: %s"),
            *FunctionGraphName, *FString::Join(Available, TEXT(", ")));
    }

    if (VarName.IsEmpty())
        return TEXT("ERR:VarName cannot be empty");

    FEdGraphPinType PinType = ResolveTypeString(VarType);
    if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
        return FString::Printf(TEXT("ERR:Unknown VarType '%s'"), *VarType);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddLocalVariable", "GraphBridge: Add Local Variable"));

    const bool bOk = FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, FName(*VarName), PinType, DefaultValue);
    if (!bOk)
        return FString::Printf(TEXT("ERR:AddLocalVariable failed for '%s' in graph '%s' — name may already be in use"),
            *VarName, *FunctionGraphName);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddLocalVariable: '%s' in '%s'"), *VarName, *FunctionGraphName);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetVariableMetadata
// Command: SET_VARIABLE_METADATA|BPPath|VarName|MetaKey|MetaValue
//
// Class (member) variables only. FBlueprintEditorUtils::SetBlueprintVariableMetaData
// takes an InLocalVarScope parameter for local-variable metadata, but the
// engine (as of this writing) silently drops metadata set that way at
// compile time (Epic bug UE-239861, open) — always pass nullptr here and
// do not extend this to local variables without first re-checking that bug.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetVariableMetadata(FString BlueprintPath, FString VarName,
    FString MetaKey, FString MetaValue)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (VarName.IsEmpty())
        return TEXT("ERR:VarName cannot be empty");
    if (MetaKey.IsEmpty())
        return TEXT("ERR:MetaKey cannot be empty");

    const FName VarFName(*VarName);
    const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VarFName);
    if (VarIndex == INDEX_NONE)
        return FString::Printf(
            TEXT("ERR:Class variable '%s' not found — use LIST_VARIABLES to see available (local/function variables are not supported by this command)"),
            *VarName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetVariableMetadata", "GraphBridge: Set Variable Metadata"));

    // "Category" is NOT stored in FBPVariableDescription::MetaDataArray like
    // other metadata — it's a dedicated FText field on FBPVariableDescription
    // with its own engine API. Confirmed live: the generic
    // SetBlueprintVariableMetaData path silently writes it to the wrong
    // place (round-trips fine through GetBlueprintVariableMetaData, but the
    // Details panel's category widget never reads that — it calls
    // FBlueprintEditorUtils::GetBlueprintVariableCategory(), per
    // BlueprintDetailsCustomization.cpp). ToolTip and other generic keys do
    // use the metadata-array path correctly.
    if (MetaKey.Equals(TEXT("Category"), ESearchCase::IgnoreCase))
        FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, VarFName, nullptr, FText::FromString(MetaValue));
    else
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VarFName, nullptr, FName(*MetaKey), MetaValue);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetVariableMetadata: '%s' %s='%s'"), *VarName, *MetaKey, *MetaValue);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateEventDispatcher
// Command: CREATE_EVENT_DISPATCHER|BPPath|DispatcherName|ParamType1:ParamName1,ParamType2:ParamName2,...
//
// Mirrors FBlueprintEditor::OnAddNewDelegate() exactly (confirmed against
// Editor/Kismet/Private/BlueprintEditor.cpp — the engine's own "Add New"
// button in the My Blueprint panel's Event Dispatchers section calls this
// same sequence). Parameters are new — the engine's own default action adds
// none — added afterward via the signature graph's entry node using
// CreateUserDefinedPin(..., EGPD_Output, ...). EGPD_Output here is this
// command's own addition, not verbatim engine source like the rest of this
// function — confirmed live (not just by compiling) via a real
// K2Node_CallDelegate referencing the dispatcher: the parameter correctly
// appears as an IN pin on the caller (a caller must supply it), matching
// real Event Dispatcher semantics, and the FunctionEntry node in the
// dispatcher's own signature graph correctly shows it as an output pin.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateEventDispatcher(FString BlueprintPath, FString DispatcherName,
    FString CommaSeparatedParams)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (DispatcherName.IsEmpty())
        return TEXT("ERR:DispatcherName cannot be empty");

    const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
    if (!K2Schema)
        return TEXT("ERR:Could not get K2 schema");

    // Parse "Type1:Name1,Type2:Name2,..." up front so a malformed parameter
    // list fails cleanly before any state is mutated.
    struct FParamSpec { FString Name; FEdGraphPinType PinType; };
    TArray<FParamSpec> Params;
    if (!CommaSeparatedParams.IsEmpty())
    {
        TArray<FString> Pairs;
        CommaSeparatedParams.ParseIntoArray(Pairs, TEXT(","), true);
        for (FString& Pair : Pairs)
        {
            Pair.TrimStartAndEndInline();
            FString TypeStr, NameStr;
            if (!Pair.Split(TEXT(":"), &TypeStr, &NameStr))
                return FString::Printf(TEXT("ERR:Malformed parameter '%s' — expected Type:Name"), *Pair);
            TypeStr.TrimStartAndEndInline();
            NameStr.TrimStartAndEndInline();
            if (NameStr.IsEmpty())
                return FString::Printf(TEXT("ERR:Malformed parameter '%s' — empty parameter name"), *Pair);

            FEdGraphPinType PinType = ResolveTypeString(TypeStr);
            if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
                return FString::Printf(TEXT("ERR:Unknown parameter type '%s' in '%s'"), *TypeStr, *Pair);

            Params.Add(FParamSpec{ NameStr, PinType });
        }
    }

    FName Name = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, DispatcherName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateEventDispatcher", "GraphBridge: Create Event Dispatcher"));
    Blueprint->Modify();

    FEdGraphPinType DelegateType;
    DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
    const bool bVarCreatedSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, DelegateType);
    if (!bVarCreatedSuccess)
        return TEXT("ERR:AddMemberVariable failed for the dispatcher's delegate variable");

    UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (!NewGraph)
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
        return TEXT("ERR:CreateNewGraph failed for the dispatcher's signature graph");
    }

    NewGraph->bEditable = false;
    K2Schema->CreateDefaultNodesForGraph(*NewGraph);
    K2Schema->CreateFunctionGraphTerminators(*NewGraph, (UClass*)nullptr);
    K2Schema->AddExtraFunctionFlags(NewGraph, (FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public));
    K2Schema->MarkFunctionEntryAsEditable(NewGraph, true);

    Blueprint->DelegateSignatureGraphs.Add(NewGraph);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    if (Params.Num() > 0)
    {
        TWeakObjectPtr<UK2Node_EditablePinBase> EntryNode, ResultNode;
        FBlueprintEditorUtils::GetEntryAndResultNodes(NewGraph, EntryNode, ResultNode);
        if (!EntryNode.IsValid())
            return FString::Printf(TEXT("ERR:Dispatcher '%s' created but its entry node could not be found to add parameters"), *Name.ToString());

        for (const FParamSpec& P : Params)
        {
            UEdGraphPin* NewPin = EntryNode->CreateUserDefinedPin(FName(*P.Name), P.PinType, EGPD_Output, true);
            if (!NewPin)
                return FString::Printf(TEXT("ERR:Dispatcher '%s' created but parameter '%s' could not be added"), *Name.ToString(), *P.Name);
        }
    }

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateEventDispatcher: '%s' (%d params) in '%s'"),
        *Name.ToString(), Params.Num(), *BlueprintPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// Animation Blueprint state machines
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CreateStateMachine
// Command: CREATE_STATE_MACHINE|BPPath|GraphName|StateMachineName|X|Y
//
// Dedicated construction path — does NOT reuse SpawnNode/SpawnNodeOnGraph,
// since that function's crash-guard blanket-rejects all UAnimGraphNode_Base
// subclasses. Confirmed via engine source that UAnimGraphNode_StateMachine
// doesn't have the bespoke-setup problem that guard protects against (see
// header comment for the full explanation) — its EditorStateMachineGraph is
// populated inside the standard PostPlacedNewNode() lifecycle hook, which
// this manual construction sequence already calls, mirroring the existing
// K2Node_SpawnActorFromClass special case in SpawnNodeOnGraph.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateStateMachine(FString BlueprintPath, FString GraphName,
    FString StateMachineName, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        TArray<FString> Available;
        Available.Add(TEXT("EventGraph"));
        for (UEdGraph* G : Blueprint->FunctionGraphs)
            if (G) Available.Add(G->GetName());
        return FString::Printf(
            TEXT("ERR:Graph '%s' not found. Available graphs: %s"),
            *GraphName, *FString::Join(Available, TEXT(", ")));
    }

    if (StateMachineName.IsEmpty())
        return TEXT("ERR:StateMachineName cannot be empty");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateStateMachine", "GraphBridge: Create State Machine"));
    Graph->Modify();

    UAnimGraphNode_StateMachine* NewNode = NewObject<UAnimGraphNode_StateMachine>(
        Graph, UAnimGraphNode_StateMachine::StaticClass(), NAME_None, RF_Transactional);
    NewNode->CreateNewGuid();
    NewNode->NodePosX = X;
    NewNode->NodePosY = Y;
    Graph->AddNode(NewNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
    NewNode->AllocateDefaultPins();
    NewNode->PostPlacedNewNode();

    if (!NewNode->EditorStateMachineGraph)
        return TEXT("ERR:State machine node created but EditorStateMachineGraph was not populated — PostPlacedNewNode may not have run correctly");

    FBlueprintEditorUtils::RenameGraph(NewNode->EditorStateMachineGraph, StateMachineName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateStateMachine: '%s' in '%s'"), *StateMachineName, *GraphName);
    return NewNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// AddAnimState
// Command: ADD_ANIM_STATE|BPPath|StateMachineNodeGUID|StateName|X|Y
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddAnimState(FString BlueprintPath, FString StateMachineNodeGUID,
    FString StateName, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UAnimGraphNode_StateMachineBase* MachineNode = FindStateMachineNode(Blueprint, StateMachineNodeGUID);
    if (!MachineNode)
        return FString::Printf(TEXT("ERR:State machine node not found: '%s' — use LIST_NODES to get valid GUIDs"), *StateMachineNodeGUID);
    if (!MachineNode->EditorStateMachineGraph)
        return TEXT("ERR:State machine node has no EditorStateMachineGraph");

    if (StateName.IsEmpty())
        return TEXT("ERR:StateName cannot be empty");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddAnimState", "GraphBridge: Add Anim State"));

    UAnimStateNode* NewStateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
        MachineNode->EditorStateMachineGraph, NewObject<UAnimStateNode>(), FVector2f((float)X, (float)Y), false);
    if (!NewStateNode)
        return TEXT("ERR:SpawnNodeFromTemplate returned null for UAnimStateNode");
    if (!NewStateNode->BoundGraph)
        return TEXT("ERR:State node created but its BoundGraph was not populated");

    FBlueprintEditorUtils::RenameGraph(NewStateNode->BoundGraph, StateName);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddAnimState: '%s' in state machine '%s'"), *StateName, *StateMachineNodeGUID);
    return NewStateNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// AddAnimTransition
// Command: ADD_ANIM_TRANSITION|BPPath|StateMachineNodeGUID|FromStateGUID|ToStateGUID
// See header comment — TryCreateConnection alone is NOT sufficient; the real
// transition-node creation is SpawnNodeFromTemplate<UAnimStateTransitionNode>
// + CreateConnections(FromState, ToState).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddAnimTransition(FString BlueprintPath, FString StateMachineNodeGUID,
    FString FromStateGUID, FString ToStateGUID)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UAnimGraphNode_StateMachineBase* MachineNode = FindStateMachineNode(Blueprint, StateMachineNodeGUID);
    if (!MachineNode)
        return FString::Printf(TEXT("ERR:State machine node not found: '%s'"), *StateMachineNodeGUID);
    if (!MachineNode->EditorStateMachineGraph)
        return TEXT("ERR:State machine node has no EditorStateMachineGraph");

    UAnimStateNodeBase* FromState = FindAnimStateNode(MachineNode, FromStateGUID);
    if (!FromState)
        return FString::Printf(TEXT("ERR:FromState node not found: '%s' — use LIST_ANIM_STATES to get valid GUIDs"), *FromStateGUID);

    UAnimStateNodeBase* ToState = FindAnimStateNode(MachineNode, ToStateGUID);
    if (!ToState)
        return FString::Printf(TEXT("ERR:ToState node not found: '%s' — use LIST_ANIM_STATES to get valid GUIDs"), *ToStateGUID);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddAnimTransition", "GraphBridge: Add Anim Transition"));

    UAnimStateTransitionNode* TransitionNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(
        MachineNode->EditorStateMachineGraph, NewObject<UAnimStateTransitionNode>(), FVector2f(0.0f, 0.0f), false);
    if (!TransitionNode)
        return TEXT("ERR:SpawnNodeFromTemplate returned null for UAnimStateTransitionNode");

    TransitionNode->CreateConnections(FromState, ToState);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddAnimTransition: '%s' -> '%s' in state machine '%s'"),
        *FromStateGUID, *ToStateGUID, *StateMachineNodeGUID);
    return TransitionNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// ListAnimStates
// Command: LIST_ANIM_STATES|BPPath|StateMachineNodeGUID
// Returns comma-separated "GUID~StateName" entries for every
// UAnimStateNodeBase-derived node in the machine (this includes plain
// states, conduits, and entry nodes — all derive from UAnimStateNodeBase).
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListAnimStates(FString BlueprintPath, FString StateMachineNodeGUID)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UAnimGraphNode_StateMachineBase* MachineNode = FindStateMachineNode(Blueprint, StateMachineNodeGUID);
    if (!MachineNode)
        return FString::Printf(TEXT("ERR:State machine node not found: '%s'"), *StateMachineNodeGUID);
    if (!MachineNode->EditorStateMachineGraph)
        return TEXT("ERR:State machine node has no EditorStateMachineGraph");

    TArray<FString> Entries;
    for (UEdGraphNode* Node : MachineNode->EditorStateMachineGraph->Nodes)
    {
        UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(Node);
        if (!StateNode) continue;
        // Transition nodes are also UAnimStateNodeBase-derived but represent
        // edges, not states — exclude them from the state list.
        if (Cast<UAnimStateTransitionNode>(StateNode)) continue;
        Entries.Add(FString::Printf(TEXT("%s~%s"), *StateNode->NodeGuid.ToString(), *StateNode->GetStateName()));
    }

    return FString::Join(Entries, TEXT(","));
}

// ---------------------------------------------------------------------------
// GetAnimStateTransitions
// Command: GET_ANIM_STATE_TRANSITIONS|BPPath|StateNodeGUID
// Wraps UAnimStateNodeBase::GetTransitionList(). Returns comma-separated
// "GUID~FromStateName~ToStateName" entries. Note StateNodeGUID must be
// searched across ALL state machines in the Blueprint's AnimGraph(s), since
// the command doesn't take a StateMachineNodeGUID — this mirrors LIST_NODES'
// GUID-only lookup convention elsewhere in this file.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetAnimStateTransitions(FString BlueprintPath, FString StateNodeGUID)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(Blueprint->UbergraphPages);
    AllGraphs.Append(Blueprint->FunctionGraphs);

    UAnimStateNodeBase* TargetState = nullptr;
    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachineBase* MachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
            if (!MachineNode || !MachineNode->EditorStateMachineGraph) continue;
            TargetState = FindAnimStateNode(MachineNode, StateNodeGUID);
            if (TargetState) break;
        }
        if (TargetState) break;
    }

    if (!TargetState)
        return FString::Printf(TEXT("ERR:State node not found: '%s' — use LIST_ANIM_STATES to get valid GUIDs"), *StateNodeGUID);

    TArray<UAnimStateTransitionNode*> Transitions;
    TargetState->GetTransitionList(Transitions);

    TArray<FString> Entries;
    for (UAnimStateTransitionNode* Transition : Transitions)
    {
        if (!Transition) continue;
        UAnimStateNodeBase* PrevState = Transition->GetPreviousState();
        UAnimStateNodeBase* NextState = Transition->GetNextState();
        Entries.Add(FString::Printf(TEXT("%s~%s~%s"),
            *Transition->NodeGuid.ToString(),
            PrevState ? *PrevState->GetStateName() : TEXT("?"),
            NextState ? *NextState->GetStateName() : TEXT("?")));
    }

    return FString::Join(Entries, TEXT(","));
}

// ---------------------------------------------------------------------------
// GetAnimNodePins
// Command: GET_ANIM_NODE_PINS|BPPath|NodeGUID
// See header comment -- uses FindNodeByIdAllGraphs so it also reaches
// AnimGraph/FunctionGraph nodes that GetNodePins (FindNodeById, Ubergraph
// only) cannot see.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetAnimNodePins(FString BlueprintPath, FString NodeGUID)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeGUID);
    if (!Node) return TEXT("");

    TArray<FString> PinDescs;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin) continue;
        FString Dir = Pin->Direction == EGPD_Input ? TEXT("IN") : TEXT("OUT");
        PinDescs.Add(FString::Printf(TEXT("%s:%s"), *Dir, *Pin->PinName.ToString()));
    }
    return FString::Join(PinDescs, TEXT(","));
}

// ---------------------------------------------------------------------------
// ConnectAnimPins
// Command: CONNECT_ANIM_PINS|BPPath|NodeGUIDA|PinNameA|NodeGUIDB|PinNameB
// See header comment -- resolves nodes via FindNodeByIdAllGraphs (reaches
// FunctionGraphs, where an Animation Blueprint's AnimGraph pages live) and
// connects via each node's OWN graph schema instead of a hardcoded
// UEdGraphSchema_K2 cast, so this works for AnimGraph pins (UAnimationGraphSchema)
// as well as ordinary K2 pins.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ConnectAnimPins(FString BlueprintPath,
    FString NodeA, FString PinA, FString NodeB, FString PinB)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* SourceNode = FindNodeAnywhere(Blueprint, NodeA);
    if (!SourceNode)
        return FString::Printf(TEXT("ERR:Source node not found: '%s' â€” use CREATE_STATE_MACHINE/GET_ANIM_NODE_PINS output for valid GUIDs"), *NodeA);

    UEdGraphNode* TargetNode = FindNodeAnywhere(Blueprint, NodeB);
    if (!TargetNode)
        return FString::Printf(TEXT("ERR:Target node not found: '%s' â€” use CREATE_STATE_MACHINE/GET_ANIM_NODE_PINS output for valid GUIDs"), *NodeB);

    auto PinList = [](UEdGraphNode* Node) -> FString {
        TArray<FString> Names;
        for (UEdGraphPin* P : Node->Pins)
            if (P) Names.Add(FString::Printf(TEXT("%s(%s)"),
                *P->PinName.ToString(),
                P->Direction == EGPD_Output ? TEXT("OUT") : TEXT("IN")));
        return FString::Join(Names, TEXT(", "));
    };

    UEdGraphPin* SourcePin = SourceNode->FindPin(*PinA);
    if (!SourcePin)
        return FString::Printf(TEXT("ERR:Pin '%s' not found on source node '%s'. Available pins: %s"),
            *PinA, *NodeA, *PinList(SourceNode));

    UEdGraphPin* TargetPin = TargetNode->FindPin(*PinB);
    if (!TargetPin)
        return FString::Printf(TEXT("ERR:Pin '%s' not found on target node '%s'. Available pins: %s"),
            *PinB, *NodeB, *PinList(TargetNode));

    if (SourcePin->Direction != EGPD_Output)
        return FString::Printf(
            TEXT("ERR:Pin '%s' on '%s' is an INPUT pin and cannot be a connection source. "
                 "Swap your node/pin arguments so the OUTPUT pin is first."),
            *PinA, *NodeA);

    if (TargetPin->Direction != EGPD_Input)
        return FString::Printf(
            TEXT("ERR:Pin '%s' on '%s' is an OUTPUT pin and cannot be a connection target. "
                 "Swap your node/pin arguments so the INPUT pin is second."),
            *PinB, *NodeB);

    UEdGraph* Graph = SourceNode->GetGraph();
    if (!Graph)
        return TEXT("ERR:Source node has no owning graph");

    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
        return TEXT("ERR:Could not get graph schema");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "ConnectAnimPins", "GraphBridge: Connect Anim Pins"));
    Graph->Modify();
    SourceNode->Modify();
    TargetNode->Modify();

    if (!Schema->TryCreateConnection(SourcePin, TargetPin))
        return FString::Printf(TEXT("ERR:Schema rejected connection between '%s.%s' and '%s.%s' â€” types are likely incompatible"),
            *NodeA, *PinA, *NodeB, *PinB);

    // See ConnectPins for why this is needed — wildcard/array-dependent pins
    // only retype in response to this callback, which TryCreateConnection
    // alone does not reliably trigger for bridge-spawned nodes.
    // NotifyPinConnectionListChanged lives on UK2Node, not UEdGraphNode.
    if (UK2Node* SourceK2 = Cast<UK2Node>(SourceNode))
        SourceK2->NotifyPinConnectionListChanged(SourcePin);
    if (UK2Node* TargetK2 = Cast<UK2Node>(TargetNode))
        TargetK2->NotifyPinConnectionListChanged(TargetPin);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge ConnectAnimPins: %s.%s -> %s.%s"),
        *NodeA, *PinA, *NodeB, *PinB);

    return TEXT("");
}

// ---------------------------------------------------------------------------
// ListAnimGraphNodes
// Command: LIST_ANIM_GRAPH_NODES|BPPath|GraphName
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListAnimGraphNodes(FString BlueprintPath, FString GraphName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        TArray<FString> Available;
        Available.Add(TEXT("EventGraph"));
        for (UEdGraph* G : Blueprint->FunctionGraphs)
            if (G) Available.Add(G->GetName());
        return FString::Printf(
            TEXT("ERR:Graph '%s' not found. Available graphs: %s"),
            *GraphName, *FString::Join(Available, TEXT(", ")));
    }

    TArray<FString> Entries;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node) continue;
        Entries.Add(FString::Printf(TEXT("%s~%s~%s"),
            *Node->NodeGuid.ToString(),
            *Node->GetClass()->GetName(),
            *Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
    }
    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
// FindNodeAnywhere -- see header comment.
// ---------------------------------------------------------------------------
UEdGraphNode* UGraphBridgeAutomationLibrary::FindNodeAnywhere(UBlueprint* Blueprint, const FString& NodeId)
{
    if (!Blueprint) return nullptr;

    if (UEdGraphNode* Found = FindNodeByIdAllGraphs(Blueprint, NodeId))
        return Found;

    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(Blueprint->UbergraphPages);
    AllGraphs.Append(Blueprint->FunctionGraphs);

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            UAnimGraphNode_StateMachineBase* MachineNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
            if (!MachineNode || !MachineNode->EditorStateMachineGraph) continue;

            for (UEdGraphNode* InnerNode : MachineNode->EditorStateMachineGraph->Nodes)
            {
                if (!InnerNode) continue;
                if (InnerNode->NodeGuid.ToString() == NodeId) return InnerNode;

                UAnimStateNodeBase* AnimNode = Cast<UAnimStateNodeBase>(InnerNode);
                if (AnimNode && AnimNode->GetBoundGraph())
                {
                    for (UEdGraphNode* Sub : AnimNode->GetBoundGraph()->Nodes)
                        if (Sub && Sub->NodeGuid.ToString() == NodeId) return Sub;
                }
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ListStateGraphNodes
// Command: LIST_STATE_GRAPH_NODES|BPPath|StateOrTransitionGUID
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListStateGraphNodes(FString BlueprintPath, FString StateOrTransitionGUID)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, StateOrTransitionGUID);
    if (!Node)
        return FString::Printf(TEXT("ERR:State/transition node not found: '%s'"), *StateOrTransitionGUID);

    UAnimStateNodeBase* AnimNode = Cast<UAnimStateNodeBase>(Node);
    if (!AnimNode)
        return FString::Printf(TEXT("ERR:Node '%s' is not a state or transition node (found %s)"),
            *StateOrTransitionGUID, *Node->GetClass()->GetName());

    if (!AnimNode->GetBoundGraph())
        return TEXT("ERR:Node has no BoundGraph");

    TArray<FString> Entries;
    for (UEdGraphNode* N : AnimNode->GetBoundGraph()->Nodes)
    {
        if (!N) continue;
        Entries.Add(FString::Printf(TEXT("%s~%s~%s"),
            *N->NodeGuid.ToString(), *N->GetClass()->GetName(),
            *N->GetNodeTitle(ENodeTitleType::ListView).ToString()));
    }
    return FString::Join(Entries, TEXT("|"));
}

// ---------------------------------------------------------------------------
// SpawnNodeAnchored
// Command: SPAWN_NODE_ANCHORED|BPPath|AnchorNodeGUID|NodeClass|Comment|X|Y
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SpawnNodeAnchored(FString BlueprintPath, FString AnchorNodeGUID,
    FString NodeClass, FString Comment, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Anchor = FindNodeAnywhere(Blueprint, AnchorNodeGUID);
    if (!Anchor)
        return FString::Printf(TEXT("ERR:Anchor node not found: '%s'"), *AnchorNodeGUID);

    UEdGraph* Graph = Anchor->GetGraph();
    if (!Graph)
        return TEXT("ERR:Anchor node has no owning graph");

    return SpawnNodeOnGraph(Blueprint, Graph, NodeClass, Comment, X, Y);
}

// ---------------------------------------------------------------------------
// CreateBlendSpacePlayerAnchored
// Command: CREATE_BLEND_SPACE_PLAYER_ANCHORED|BPPath|AnchorNodeGUID|BlendSpaceAssetPath|X|Y
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateBlendSpacePlayerAnchored(FString BlueprintPath, FString AnchorNodeGUID,
    FString BlendSpaceAssetPath, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Anchor = FindNodeAnywhere(Blueprint, AnchorNodeGUID);
    if (!Anchor)
        return FString::Printf(TEXT("ERR:Anchor node not found: '%s'"), *AnchorNodeGUID);

    UEdGraph* Graph = Anchor->GetGraph();
    if (!Graph)
        return TEXT("ERR:Anchor node has no owning graph");

    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpaceAssetPath);
    if (!BlendSpace)
        return FString::Printf(TEXT("ERR:Could not load BlendSpace at '%s'"), *BlendSpaceAssetPath);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateBlendSpacePlayerAnchored", "GraphBridge: Create Blend Space Player"));
    Graph->Modify();

    UAnimGraphNode_BlendSpacePlayer* NewNode = NewObject<UAnimGraphNode_BlendSpacePlayer>(
        Graph, UAnimGraphNode_BlendSpacePlayer::StaticClass(), NAME_None, RF_Transactional);
    // FAnimNode_BlendSpacePlayer::BlendSpace is private (friended only to
    // specific UAnimGraphNode_* editor classes, which this automation
    // library is not) -- use the public setter instead.
    NewNode->Node.SetBlendSpace(BlendSpace);
    NewNode->CreateNewGuid();
    NewNode->NodePosX = X;
    NewNode->NodePosY = Y;
    Graph->AddNode(NewNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
    NewNode->AllocateDefaultPins();
    NewNode->PostPlacedNewNode();

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateBlendSpacePlayerAnchored: '%s' anchored on '%s'"),
        *BlendSpaceAssetPath, *AnchorNodeGUID);

    return NewNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// SetTransitionCondition
// Command: SET_TRANSITION_CONDITION|BPPath|TransitionNodeGUID|VarName|bNegate
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetTransitionCondition(FString BlueprintPath, FString TransitionNodeGUID,
    FString VarName, bool bNegate)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, TransitionNodeGUID);
    UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
    if (!TransNode)
        return FString::Printf(TEXT("ERR:Transition node not found: '%s'"), *TransitionNodeGUID);
    if (!TransNode->BoundGraph)
        return TEXT("ERR:Transition node has no BoundGraph");

    UAnimGraphNode_TransitionResult* ResultNode = nullptr;
    for (UEdGraphNode* N : TransNode->BoundGraph->Nodes)
    {
        ResultNode = Cast<UAnimGraphNode_TransitionResult>(N);
        if (ResultNode) break;
    }
    if (!ResultNode)
        return TEXT("ERR:No AnimGraphNode_TransitionResult found in transition's BoundGraph");

    UEdGraphPin* ResultInputPin = nullptr;
    for (UEdGraphPin* P : ResultNode->Pins)
    {
        if (P && P->Direction == EGPD_Input) { ResultInputPin = P; break; }
    }
    if (!ResultInputPin)
        return TEXT("ERR:TransitionResult node has no input pin");

    // Spawn the variable getter anchored in the same BoundGraph.
    FString VarGetResult = SpawnNodeAnchored(BlueprintPath, ResultNode->NodeGuid.ToString(),
        TEXT("K2Node_VariableGet"), TEXT(""), ResultNode->NodePosX - 300, ResultNode->NodePosY);
    if (VarGetResult.StartsWith(TEXT("ERR:")))
        return VarGetResult;

    FString VarGetError;
    bool bRefOk = SetVariableRef(BlueprintPath, VarGetResult, VarName, VarGetError);
    if (!bRefOk)
        return FString::Printf(TEXT("ERR:SetVariableRef failed for '%s': %s"), *VarName, *VarGetError);

    UEdGraphNode* VarGetNode = FindNodeAnywhere(Blueprint, VarGetResult);
    if (!VarGetNode)
        return TEXT("ERR:Could not re-find spawned VariableGet node after SetVariableRef");

    UEdGraphPin* VarGetOutPin = nullptr;
    for (UEdGraphPin* P : VarGetNode->Pins)
    {
        if (P && P->Direction == EGPD_Output) { VarGetOutPin = P; break; }
    }
    if (!VarGetOutPin)
        return TEXT("ERR:VariableGet node has no output pin");

    if (!bNegate)
    {
        FString ConnectErr = ConnectAnimPins(BlueprintPath,
            VarGetNode->NodeGuid.ToString(), VarGetOutPin->PinName.ToString(),
            ResultNode->NodeGuid.ToString(), ResultInputPin->PinName.ToString());
        return ConnectErr;
    }

    // bNegate: insert a "Not Boolean" call node between the getter and the result.
    FString NotResult = SpawnNodeAnchored(BlueprintPath, ResultNode->NodeGuid.ToString(),
        TEXT("K2Node_CallFunction"), TEXT(""), ResultNode->NodePosX - 150, ResultNode->NodePosY);
    if (NotResult.StartsWith(TEXT("ERR:")))
        return NotResult;

    FString FuncRefErr = SetFunctionRef(BlueprintPath, NotResult, TEXT("KismetMathLibrary"), TEXT("Not_PreBool"));
    if (!FuncRefErr.IsEmpty())
        return FString::Printf(TEXT("ERR:SetFunctionRef failed: %s"), *FuncRefErr);

    UEdGraphNode* NotNode = FindNodeAnywhere(Blueprint, NotResult);
    if (!NotNode)
        return TEXT("ERR:Could not re-find spawned Not node after SetFunctionRef");

    UEdGraphPin* NotInPin = nullptr;
    UEdGraphPin* NotOutPin = nullptr;
    for (UEdGraphPin* P : NotNode->Pins)
    {
        if (!P) continue;
        if (P->Direction == EGPD_Input && !NotInPin && P->PinName != TEXT("self")) NotInPin = P;
        if (P->Direction == EGPD_Output && !NotOutPin) NotOutPin = P;
    }
    if (!NotInPin || !NotOutPin)
        return TEXT("ERR:Not_PreBool node missing expected pins");

    FString Err1 = ConnectAnimPins(BlueprintPath,
        VarGetNode->NodeGuid.ToString(), VarGetOutPin->PinName.ToString(),
        NotNode->NodeGuid.ToString(), NotInPin->PinName.ToString());
    if (!Err1.IsEmpty()) return Err1;

    FString Err2 = ConnectAnimPins(BlueprintPath,
        NotNode->NodeGuid.ToString(), NotOutPin->PinName.ToString(),
        ResultNode->NodeGuid.ToString(), ResultInputPin->PinName.ToString());
    return Err2;
}

// ---------------------------------------------------------------------------
// AddAnimSlotNode
// Command: ADD_ANIM_SLOT_NODE|BPPath|GraphName|SlotName|X|Y
// See header comment. Splices a UAnimGraphNode_Slot between whatever
// currently feeds AnimGraphNode_Root's "Result" pin and Output Pose itself.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddAnimSlotNode(FString BlueprintPath, FString GraphName,
    FString SlotName, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    if (SlotName.IsEmpty())
        return TEXT("ERR:SlotName cannot be empty");

    UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
    if (!Graph)
    {
        TArray<FString> Available;
        Available.Add(TEXT("EventGraph"));
        for (UEdGraph* G : Blueprint->FunctionGraphs)
            if (G) Available.Add(G->GetName());
        return FString::Printf(
            TEXT("ERR:Graph '%s' not found. Available graphs: %s"),
            *GraphName, *FString::Join(Available, TEXT(", ")));
    }

    UAnimGraphNode_Root* RootNode = nullptr;
    for (UEdGraphNode* N : Graph->Nodes)
    {
        RootNode = Cast<UAnimGraphNode_Root>(N);
        if (RootNode) break;
    }
    if (!RootNode)
        return FString::Printf(TEXT("ERR:No AnimGraphNode_Root (Output Pose) found in graph '%s' — is this really an AnimGraph?"), *GraphName);

    UEdGraphPin* ResultPin = RootNode->FindPin(TEXT("Result"));
    if (!ResultPin)
        return TEXT("ERR:Output Pose node has no 'Result' pin");

    if (ResultPin->LinkedTo.Num() == 0)
        return TEXT("ERR:Output Pose's Result pin has nothing connected to splice a Slot node into — connect a pose source first");

    UEdGraphPin* UpstreamPin = ResultPin->LinkedTo[0];
    UEdGraphNode* UpstreamNode = UpstreamPin ? UpstreamPin->GetOwningNode() : nullptr;
    if (!UpstreamPin || !UpstreamNode)
        return TEXT("ERR:Could not resolve the node currently feeding Output Pose's Result pin");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddAnimSlotNode", "GraphBridge: Add Anim Slot Node"));
    Graph->Modify();
    RootNode->Modify();
    UpstreamNode->Modify();

    // Same manual construction order already proven safe in this file for
    // other UAnimGraphNode_Base subclasses (CreateStateMachine,
    // CreateBlendSpacePlayerAnchored) -- NOT SpawnNodeOnGraph's generic
    // template-duplication path, which explicitly rejects this class family.
    UAnimGraphNode_Slot* NewNode = NewObject<UAnimGraphNode_Slot>(
        Graph, UAnimGraphNode_Slot::StaticClass(), NAME_None, RF_Transactional);
    NewNode->Node.SlotName = FName(*SlotName);
    NewNode->CreateNewGuid();
    NewNode->NodePosX = X;
    NewNode->NodePosY = Y;
    Graph->AddNode(NewNode, /*bFromUI=*/false, /*bSelectNewNode=*/false);
    NewNode->AllocateDefaultPins();
    NewNode->PostPlacedNewNode();

    UEdGraphPin* SlotSourcePin = NewNode->FindPin(TEXT("Source"));
    UEdGraphPin* SlotPosePin = NewNode->FindPin(TEXT("Pose"));
    if (!SlotSourcePin || !SlotPosePin)
        return TEXT("ERR:Slot node created but is missing expected 'Source'/'Pose' pins");

    const UEdGraphSchema* Schema = Graph->GetSchema();
    if (!Schema)
        return TEXT("ERR:Could not get graph schema");

    // Break the old Upstream -> Result link, then route Upstream -> Slot.Source
    // and Slot.Pose -> Result instead.
    ResultPin->BreakAllPinLinks();

    if (!Schema->TryCreateConnection(UpstreamPin, SlotSourcePin))
        return FString::Printf(TEXT("ERR:Failed to connect '%s' to the new Slot node's Source pin"), *UpstreamNode->GetClass()->GetName());

    if (!Schema->TryCreateConnection(SlotPosePin, ResultPin))
        return TEXT("ERR:Failed to connect the new Slot node's Pose pin to Output Pose's Result pin");

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddAnimSlotNode: '%s' spliced into '%s' in '%s'"),
        *SlotName, *GraphName, *BlueprintPath);

    return NewNode->NodeGuid.ToString();
}

// ---------------------------------------------------------------------------
// Niagara
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// DeriveAssetPathParts — shared by CreateNiagaraSystem/CreateNiagaraEmitter,
// same derivation logic already used by CreateEnum/CreateStruct/
// CreateFunctionLibrary (accepts "/Game/Foo/Bar" or "/Game/Foo/Bar.Bar").
// ---------------------------------------------------------------------------
static bool DeriveAssetPathParts(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName)
{
    OutPackageName = AssetPath;
    int32 DotIdx;
    if (OutPackageName.FindLastChar(TEXT('.'), DotIdx))
    {
        OutAssetName = OutPackageName.Mid(DotIdx + 1);
        OutPackageName = OutPackageName.Left(DotIdx);
    }
    else
    {
        int32 SlashIdx;
        OutAssetName = OutPackageName.FindLastChar(TEXT('/'), SlashIdx)
            ? OutPackageName.Mid(SlashIdx + 1)
            : OutPackageName;
    }
    return !OutAssetName.IsEmpty();
}

// ---------------------------------------------------------------------------
// SaveExistingAssetPackage — persists an already-loaded asset's package to
// disk immediately after an in-place mutation (MarkPackageDirty() alone only
// lives in memory — a crash or ungraceful editor exit before the next
// autosave/manual save silently discards the change, as happened to
// IKRigAutoSetup's ApplyAutoFBIK() results here). Mirrors the SavePackage
// block every CreateXxx command in this file already uses.
// ---------------------------------------------------------------------------
static bool SaveExistingAssetPackage(UObject* Asset, FString& OutError)
{
    UPackage* Package = Asset->GetOutermost();
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs))
    {
        OutError = FString::Printf(TEXT("ERR:Change applied but failed to save to disk at '%s'"), *PackageFilename);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// CreateNiagaraSystem
// Command: CREATE_NIAGARA_SYSTEM|AssetPath
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateNiagaraSystem(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");
    if (LoadObject<UNiagaraSystem>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Niagara System already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateNiagaraSystem", "GraphBridge: Create Niagara System"));

    // UE 5.8: UNiagaraSystemFactoryNew::FactoryCreateNew is now private (it was
    // callable directly in 5.7). Route through IAssetTools::CreateAsset, which
    // invokes the factory via the public path and handles package creation +
    // asset-registry notification. Same pattern this file already uses for the
    // IK Retargeter. (Confirmed against UE 5.8 NiagaraSystemFactoryNew.h.)
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
    const FString FolderPath = FPackageName::GetLongPackagePath(PackageName);
    UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(
        AssetToolsModule.Get().CreateAsset(AssetName, FolderPath, UNiagaraSystem::StaticClass(), Factory));
    if (!NewSystem)
        return TEXT("ERR:AssetTools::CreateAsset returned null for Niagara System");

    UPackage* NewPackage = NewSystem->GetOutermost();
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        NewPackage->GetName(), FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewSystem, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:Niagara System created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateNiagaraSystem: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// CreateNiagaraEmitter
// Command: CREATE_NIAGARA_EMITTER|AssetPath
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateNiagaraEmitter(FString AssetPath)
{
    if (AssetPath.IsEmpty())
        return TEXT("ERR:AssetPath cannot be empty");
    if (LoadObject<UNiagaraEmitter>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Niagara Emitter already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateNiagaraEmitter", "GraphBridge: Create Niagara Emitter"));

    // UE 5.8: UNiagaraEmitterFactoryNew::FactoryCreateNew is now private (was
    // callable directly in 5.7). Route through IAssetTools::CreateAsset. The
    // bAddDefaultModulesAndRenderersToEmptyEmitter flag is set on the same factory
    // instance before CreateAsset uses it. (Confirmed against UE 5.8
    // NiagaraEmitterFactoryNew.h.)
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UNiagaraEmitterFactoryNew* Factory = NewObject<UNiagaraEmitterFactoryNew>();
    Factory->bAddDefaultModulesAndRenderersToEmptyEmitter = true;
    const FString FolderPath = FPackageName::GetLongPackagePath(PackageName);
    UNiagaraEmitter* NewEmitter = Cast<UNiagaraEmitter>(
        AssetToolsModule.Get().CreateAsset(AssetName, FolderPath, UNiagaraEmitter::StaticClass(), Factory));
    if (!NewEmitter)
        return TEXT("ERR:AssetTools::CreateAsset returned null for Niagara Emitter");

    UPackage* NewPackage = NewEmitter->GetOutermost();
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        NewPackage->GetName(), FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewEmitter, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:Niagara Emitter created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateNiagaraEmitter: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// BuildEmitterStackViewModel — shared by ListNiagaraModules/SetNiagaraModuleInput.
// Constructs a headless FNiagaraSystemViewModel (no open System Editor tab
// required) — confirmed real pattern against
// Commandlets/NiagaraSystemAuditCommandlet.cpp and
// Tests/NiagaraEditorTestUtilities.cpp, both of which build one exactly this
// way for non-interactive use. The caller must keep OutSystemViewModelKeepAlive
// alive for as long as the returned UNiagaraStackViewModel is used.
// ---------------------------------------------------------------------------
static UNiagaraStackViewModel* BuildEmitterStackViewModel(const FString& SystemAssetPath, const FString& EmitterName,
    TSharedPtr<FNiagaraSystemViewModel>& OutSystemViewModelKeepAlive, FString& OutError)
{
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemAssetPath);
    if (!System)
    {
        OutError = FString::Printf(TEXT("ERR:Niagara System not found at '%s'"), *SystemAssetPath);
        return nullptr;
    }

    FNiagaraSystemViewModelOptions Options;
    Options.bCanSimulate = false;
    Options.bCanAutoCompile = false;
    // bIsForDataProcessingOnly deliberately left false (its default) — unlike
    // the commandlet, this needs real edit-mode Stack population (module
    // listing came back empty with it true) and, for SET_NIAGARA_MODULE_INPUT,
    // an actually-editable input. NiagaraEditorTestUtilities.cpp's headless
    // test pattern (also confirmed real, non-interactive) leaves this false.
    // Confirmed-crashing without this: FNiagaraMessageManager asserts
    // "MessageAssetKey != FGuid()" inside Initialize()->RefreshAll() if
    // MessageLogGuid isn't set — NiagaraSystemAuditCommandlet.cpp sets this
    // to the system's own asset GUID, which is what's replicated here.
    Options.MessageLogGuid = System->GetAssetGuid();

    OutSystemViewModelKeepAlive = MakeShared<FNiagaraSystemViewModel>();
    OutSystemViewModelKeepAlive->Initialize(*System, Options);

    for (const TSharedRef<FNiagaraEmitterHandleViewModel>& HandleVM : OutSystemViewModelKeepAlive->GetEmitterHandleViewModels())
    {
        if (HandleVM->GetName().ToString() == EmitterName)
        {
            UNiagaraStackViewModel* StackViewModel = HandleVM->GetEmitterStackViewModel();
            if (!StackViewModel || !StackViewModel->GetRootEntry())
            {
                OutError = TEXT("ERR:Could not get emitter's Stack ViewModel");
                return nullptr;
            }
            StackViewModel->GetRootEntry()->RefreshChildren();
            return StackViewModel;
        }
    }

    TArray<FString> Available;
    for (const TSharedRef<FNiagaraEmitterHandleViewModel>& HandleVM : OutSystemViewModelKeepAlive->GetEmitterHandleViewModels())
        Available.Add(HandleVM->GetName().ToString());
    OutError = FString::Printf(TEXT("ERR:Emitter '%s' not found in system '%s'. Available emitters: %s"),
        *EmitterName, *SystemAssetPath, *FString::Join(Available, TEXT(", ")));
    return nullptr;
}

// ---------------------------------------------------------------------------
// ListNiagaraModules
// Command: LIST_NIAGARA_MODULES|SystemAssetPath|EmitterName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::ListNiagaraModules(FString SystemAssetPath, FString EmitterName)
{
    if (SystemAssetPath.IsEmpty()) return TEXT("ERR:SystemAssetPath cannot be empty");
    if (EmitterName.IsEmpty()) return TEXT("ERR:EmitterName cannot be empty");

    TSharedPtr<FNiagaraSystemViewModel> SystemViewModelKeepAlive;
    FString Error;
    UNiagaraStackViewModel* StackViewModel = BuildEmitterStackViewModel(SystemAssetPath, EmitterName, SystemViewModelKeepAlive, Error);
    if (!StackViewModel) return Error;

    TArray<UNiagaraStackModuleItem*> ModuleItems;
    StackViewModel->GetRootEntry()->GetUnfilteredChildrenOfType<UNiagaraStackModuleItem>(ModuleItems, /*bRecursive=*/true);

    TArray<FString> Names;
    for (UNiagaraStackModuleItem* ModuleItem : ModuleItems)
    {
        if (!ModuleItem) continue;
        Names.Add(ModuleItem->GetModuleNode().GetFunctionName());
    }

    return FString::Join(Names, TEXT(","));
}

// ---------------------------------------------------------------------------
// SetNiagaraModuleInput
// Command: SET_NIAGARA_MODULE_INPUT|SystemAssetPath|EmitterName|ModuleName|InputName|Value
// See header comment for why this does NOT use UUpgradeNiagaraScriptResults.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetNiagaraModuleInput(FString SystemAssetPath, FString EmitterName,
    FString ModuleName, FString InputName, FString Value)
{
    if (SystemAssetPath.IsEmpty()) return TEXT("ERR:SystemAssetPath cannot be empty");
    if (EmitterName.IsEmpty()) return TEXT("ERR:EmitterName cannot be empty");
    if (ModuleName.IsEmpty()) return TEXT("ERR:ModuleName cannot be empty");
    if (InputName.IsEmpty()) return TEXT("ERR:InputName cannot be empty");

    TSharedPtr<FNiagaraSystemViewModel> SystemViewModelKeepAlive;
    FString Error;
    UNiagaraStackViewModel* StackViewModel = BuildEmitterStackViewModel(SystemAssetPath, EmitterName, SystemViewModelKeepAlive, Error);
    if (!StackViewModel) return Error;

    TArray<UNiagaraStackModuleItem*> ModuleItems;
    StackViewModel->GetRootEntry()->GetUnfilteredChildrenOfType<UNiagaraStackModuleItem>(ModuleItems, /*bRecursive=*/true);

    UNiagaraStackModuleItem* TargetModule = nullptr;
    TArray<FString> AvailableModules;
    for (UNiagaraStackModuleItem* ModuleItem : ModuleItems)
    {
        if (!ModuleItem) continue;
        FString Name = ModuleItem->GetModuleNode().GetFunctionName();
        AvailableModules.Add(Name);
        if (Name == ModuleName) { TargetModule = ModuleItem; break; }
    }
    if (!TargetModule)
        return FString::Printf(TEXT("ERR:Module '%s' not found — use LIST_NIAGARA_MODULES to see available (%s)"),
            *ModuleName, *FString::Join(AvailableModules, TEXT(", ")));

    TArray<UNiagaraStackFunctionInput*> Inputs;
    TargetModule->GetParameterInputs(Inputs);

    UNiagaraStackFunctionInput* TargetInput = nullptr;
    for (UNiagaraStackFunctionInput* Input : Inputs)
    {
        if (Input && Input->GetInputParameterHandle().GetName().ToString() == InputName)
        {
            TargetInput = Input;
            break;
        }
    }
    if (!TargetInput)
        return FString::Printf(TEXT("ERR:Input '%s' not found on module '%s'"), *InputName, *ModuleName);

    const FNiagaraTypeDefinition& Type = TargetInput->GetInputType();
    TArray<uint8> LocalData;

    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        float V = FCString::Atof(*Value);
        LocalData.SetNumZeroed(sizeof(float));
        FMemory::Memcpy(LocalData.GetData(), &V, sizeof(float));
    }
    else if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        int32 V = FCString::Atoi(*Value);
        LocalData.SetNumZeroed(sizeof(int32));
        FMemory::Memcpy(LocalData.GetData(), &V, sizeof(int32));
    }
    else if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        FNiagaraBool V;
        V.SetValue(Value.ToBool());
        LocalData.SetNumZeroed(sizeof(FNiagaraBool));
        FMemory::Memcpy(LocalData.GetData(), &V, sizeof(FNiagaraBool));
    }
    else if (Type == FNiagaraTypeDefinition::GetVec3Def())
    {
        TArray<FString> Parts;
        Value.ParseIntoArray(Parts, TEXT(","), true);
        if (Parts.Num() < 3)
            return FString::Printf(TEXT("ERR:vec3 Value must be 'X,Y,Z' — got '%s'"), *Value);
        FVector3f V(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]));
        LocalData.SetNumZeroed(sizeof(FVector3f));
        FMemory::Memcpy(LocalData.GetData(), &V, sizeof(FVector3f));
    }
    else
    {
        return FString::Printf(TEXT("ERR:Unsupported input type for '%s' — SET_NIAGARA_MODULE_INPUT currently supports float, int32, bool, vec3 ('X,Y,Z')"), *InputName);
    }

    const UNiagaraClipboardFunctionInput* ClipboardInput = UNiagaraClipboardFunctionInput::CreateLocalValue(
        TargetModule, TargetInput->GetInputParameterHandle().GetName(), Type, TOptional<bool>(), LocalData);
    if (!ClipboardInput)
        return TEXT("ERR:UNiagaraClipboardFunctionInput::CreateLocalValue failed");

    TargetModule->SetInputValuesFromClipboardFunctionInputs({ ClipboardInput });

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetNiagaraModuleInput: '%s'.'%s' = '%s' on emitter '%s'"),
        *ModuleName, *InputName, *Value, *EmitterName);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// Character pipeline — Physics Assets, IK Rig, Montage/BlendSpace, sockets
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CreatePhysicsAsset
// Command: CREATE_PHYSICS_ASSET|SkeletalMeshPath|AssetPath|bSetToMesh
// See header comment — bypasses UPhysicsAssetFactory::CreatePhysicsAssetFromMesh
// entirely (interactive-only) in favor of the non-interactive
// FPhysicsAssetUtils::CreateFromSkeletalMesh it calls internally.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreatePhysicsAsset(FString SkeletalMeshPath, FString AssetPath, bool bSetToMesh)
{
    if (SkeletalMeshPath.IsEmpty()) return TEXT("ERR:SkeletalMeshPath cannot be empty");
    if (AssetPath.IsEmpty()) return TEXT("ERR:AssetPath cannot be empty");

    USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    if (!SkelMesh)
        return FString::Printf(TEXT("ERR:Skeletal Mesh not found at '%s'"), *SkeletalMeshPath);

    if (LoadObject<UPhysicsAsset>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A Physics Asset already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreatePhysicsAsset", "GraphBridge: Create Physics Asset"));

    UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>(NewPackage, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!NewAsset)
        return TEXT("ERR:Failed to construct UPhysicsAsset object");

    FText ErrorMessage;
    const FPhysAssetCreateParams& CreateParams = GetDefault<UPhysicsAssetGenerationSettings>()->CreateParams;
    const bool bSuccess = FPhysicsAssetUtils::CreateFromSkeletalMesh(NewAsset, SkelMesh, CreateParams, ErrorMessage, bSetToMesh);
    if (!bSuccess)
        return FString::Printf(TEXT("ERR:FPhysicsAssetUtils::CreateFromSkeletalMesh failed: %s"), *ErrorMessage.ToString());

    NewAsset->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(NewAsset);

    if (bSetToMesh)
    {
        RefreshSkelMeshOnPhysicsAssetChange(SkelMesh);
        SkelMesh->MarkPackageDirty();
    }

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewAsset, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:Physics Asset created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreatePhysicsAsset: '%s' from '%s'"), *AssetPath, *SkeletalMeshPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// CreateIKRig
// Command: CREATE_IK_RIG|AssetPath|SkeletalMeshPath
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateIKRig(FString AssetPath, FString SkeletalMeshPath)
{
    if (AssetPath.IsEmpty()) return TEXT("ERR:AssetPath cannot be empty");
    if (SkeletalMeshPath.IsEmpty()) return TEXT("ERR:SkeletalMeshPath cannot be empty");

    USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    if (!SkelMesh)
        return FString::Printf(TEXT("ERR:Skeletal Mesh not found at '%s'"), *SkeletalMeshPath);

    if (LoadObject<UIKRigDefinition>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An IK Rig already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    // CreateNewIKRigAsset takes a plain FOLDER path (its own doc comment:
    // "ie /Game/MyIKRigs/") and appends AssetName itself — unlike this
    // file's usual CreatePackage(*PackageName) convention where PackageName
    // already includes the asset-name-like final segment. Passing PackageName
    // directly here double-nests a folder matching the asset name (confirmed
    // live) — strip it back down to the containing folder first.
    const FString FolderPath = PackageName.LeftChop(AssetName.Len() + 1);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateIKRig", "GraphBridge: Create IK Rig"));

    UIKRigDefinition* NewRig = UIKRigDefinitionFactory::CreateNewIKRigAsset(FolderPath, AssetName);
    if (!NewRig)
        return TEXT("ERR:UIKRigDefinitionFactory::CreateNewIKRigAsset returned null");

    UIKRigController* Controller = UIKRigController::GetController(NewRig);
    if (!Controller)
        return TEXT("ERR:Could not get IK Rig controller for the new asset");

    if (!Controller->SetSkeletalMesh(SkelMesh))
        return FString::Printf(TEXT("ERR:SetSkeletalMesh failed — '%s' may be incompatible"), *SkeletalMeshPath);

    NewRig->MarkPackageDirty();

    UPackage* Package = NewRig->GetOutermost();
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(Package, NewRig, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:IK Rig created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateIKRig: '%s' for mesh '%s'"), *AssetPath, *SkeletalMeshPath);
    return Package->GetName() + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// IKRigAutoSetup
// Command: IK_RIG_AUTO_SETUP|IKRigAssetPath
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::IKRigAutoSetup(FString IKRigAssetPath)
{
    if (IKRigAssetPath.IsEmpty()) return TEXT("ERR:IKRigAssetPath cannot be empty");

    UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigAssetPath);
    if (!IKRig)
        return FString::Printf(TEXT("ERR:IK Rig not found at '%s'"), *IKRigAssetPath);

    UIKRigController* Controller = UIKRigController::GetController(IKRig);
    if (!Controller)
        return TEXT("ERR:Could not get IK Rig controller");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "IKRigAutoSetup", "GraphBridge: IK Rig Auto Setup"));

    const bool bSuccess = Controller->ApplyAutoFBIK();
    if (!bSuccess)
        return TEXT("ERR:ApplyAutoFBIK failed — skeleton didn't match a known template");

    IKRig->MarkPackageDirty();

    FString SaveError;
    if (!SaveExistingAssetPackage(IKRig, SaveError))
        return SaveError;

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge IKRigAutoSetup: '%s'"), *IKRigAssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// AddIKGoal
// Command: ADD_IK_GOAL|IKRigAssetPath|GoalName|BoneName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddIKGoal(FString IKRigAssetPath, FString GoalName, FString BoneName)
{
    if (IKRigAssetPath.IsEmpty()) return TEXT("ERR:IKRigAssetPath cannot be empty");
    if (GoalName.IsEmpty()) return TEXT("ERR:GoalName cannot be empty");
    if (BoneName.IsEmpty()) return TEXT("ERR:BoneName cannot be empty");

    UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigAssetPath);
    if (!IKRig)
        return FString::Printf(TEXT("ERR:IK Rig not found at '%s'"), *IKRigAssetPath);

    UIKRigController* Controller = UIKRigController::GetController(IKRig);
    if (!Controller)
        return TEXT("ERR:Could not get IK Rig controller");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddIKGoal", "GraphBridge: Add IK Goal"));

    // AddNewGoal(GoalName, BoneName) already assigns BoneName to the new goal
    // internally (confirmed in IKRigController.cpp: NewGoal->BoneName = BoneName).
    // A follow-up SetGoalBone() call with the same bone is not just redundant —
    // SetGoalBone() explicitly returns false as a no-op when the goal already
    // uses that bone (IKRigController.cpp: "goal is already using this bone"),
    // so treating that as an error would misreport every successful call.
    const FName NewGoalName = Controller->AddNewGoal(FName(*GoalName), FName(*BoneName));
    if (NewGoalName.IsNone())
        return FString::Printf(TEXT("ERR:AddNewGoal failed for '%s' on bone '%s' — check the goal name isn't taken and the bone exists"), *GoalName, *BoneName);

    IKRig->MarkPackageDirty();

    FString SaveError;
    if (!SaveExistingAssetPackage(IKRig, SaveError))
        return SaveError;

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddIKGoal: '%s' on bone '%s'"), *NewGoalName.ToString(), *BoneName);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// AddRetargetChain
// Command: ADD_RETARGET_CHAIN|IKRigAssetPath|ChainName|StartBone|EndBone|GoalName
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddRetargetChain(FString IKRigAssetPath, FString ChainName,
    FString StartBone, FString EndBone, FString GoalName)
{
    if (IKRigAssetPath.IsEmpty()) return TEXT("ERR:IKRigAssetPath cannot be empty");
    if (ChainName.IsEmpty()) return TEXT("ERR:ChainName cannot be empty");
    if (StartBone.IsEmpty()) return TEXT("ERR:StartBone cannot be empty");
    if (EndBone.IsEmpty()) return TEXT("ERR:EndBone cannot be empty");

    UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigAssetPath);
    if (!IKRig)
        return FString::Printf(TEXT("ERR:IK Rig not found at '%s'"), *IKRigAssetPath);

    UIKRigController* Controller = UIKRigController::GetController(IKRig);
    if (!Controller)
        return TEXT("ERR:Could not get IK Rig controller");

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddRetargetChain", "GraphBridge: Add Retarget Chain"));

    const FName NewChainName = Controller->AddRetargetChain(
        FName(*ChainName), FName(*StartBone), FName(*EndBone), FName(*GoalName));
    if (NewChainName.IsNone())
        return FString::Printf(TEXT("ERR:AddRetargetChain failed for '%s'"), *ChainName);

    IKRig->MarkPackageDirty();

    FString SaveError;
    if (!SaveExistingAssetPackage(IKRig, SaveError))
        return SaveError;

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddRetargetChain: '%s' (%s -> %s) in '%s'"),
        *NewChainName.ToString(), *StartBone, *EndBone, *IKRigAssetPath);
    return TEXT("");
}

// ---------------------------------------------------------------------------
// CreateIKRetargeter
// Command: CREATE_IK_RETARGETER|AssetPath|SourceIKRigPath|TargetIKRigPath
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateIKRetargeter(FString AssetPath, FString SourceIKRigPath, FString TargetIKRigPath)
{
    if (AssetPath.IsEmpty()) return TEXT("ERR:AssetPath cannot be empty");
    if (SourceIKRigPath.IsEmpty()) return TEXT("ERR:SourceIKRigPath cannot be empty");
    if (TargetIKRigPath.IsEmpty()) return TEXT("ERR:TargetIKRigPath cannot be empty");

    UIKRigDefinition* SourceRig = LoadObject<UIKRigDefinition>(nullptr, *SourceIKRigPath);
    if (!SourceRig)
        return FString::Printf(TEXT("ERR:Source IK Rig not found at '%s'"), *SourceIKRigPath);

    UIKRigDefinition* TargetRig = LoadObject<UIKRigDefinition>(nullptr, *TargetIKRigPath);
    if (!TargetRig)
        return FString::Printf(TEXT("ERR:Target IK Rig not found at '%s'"), *TargetIKRigPath);

    if (LoadObject<UIKRetargeter>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An IK Retargeter already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    // IAssetTools::CreateAsset's PackagePath parameter is a plain containing
    // FOLDER, not the full package name — it internally does
    // PackagePath + "/" + AssetName (confirmed in AssetTools.cpp,
    // UAssetToolsImpl::CreateAsset). Same double-nesting trap as
    // UIKRigDefinitionFactory::CreateNewIKRigAsset in CreateIKRig above —
    // strip the trailing /AssetName segment before calling.
    const FString FolderPath = PackageName.LeftChop(AssetName.Len() + 1);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateIKRetargeter", "GraphBridge: Create IK Retargeter"));

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
    UObject* NewAssetObj = AssetToolsModule.Get().CreateAsset(AssetName, FolderPath, UIKRetargeter::StaticClass(), Factory);
    UIKRetargeter* NewRetargeter = Cast<UIKRetargeter>(NewAssetObj);
    if (!NewRetargeter)
        return TEXT("ERR:Failed to create IK Retargeter asset");

    UIKRetargeterController* Controller = UIKRetargeterController::GetController(NewRetargeter);
    if (!Controller)
        return TEXT("ERR:Could not get IK Retargeter controller");

    Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
    Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);

    NewRetargeter->MarkPackageDirty();

    FString SaveError;
    if (!SaveExistingAssetPackage(NewRetargeter, SaveError))
        return SaveError;

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateIKRetargeter: '%s' (%s -> %s)"),
        *AssetPath, *SourceIKRigPath, *TargetIKRigPath);
    return NewRetargeter->GetOutermost()->GetName() + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// CreateAnimMontage
// Command: CREATE_ANIM_MONTAGE|AssetPath|SkeletonPath|AnimSequencePath
// AnimSequencePath may be empty — produces an empty Montage on the given
// Skeleton. Confirmed against AnimMontageFactory.cpp that FactoryCreateNew
// doesn't need ConfigureProperties() (the interactive skeleton picker) when
// TargetSkeleton/SourceAnimation are pre-set.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateAnimMontage(FString AssetPath, FString SkeletonPath, FString AnimSequencePath)
{
    if (AssetPath.IsEmpty()) return TEXT("ERR:AssetPath cannot be empty");
    if (SkeletonPath.IsEmpty()) return TEXT("ERR:SkeletonPath cannot be empty");

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *SkeletonPath);

    UAnimSequence* SourceAnim = nullptr;
    if (!AnimSequencePath.IsEmpty())
    {
        SourceAnim = LoadObject<UAnimSequence>(nullptr, *AnimSequencePath);
        if (!SourceAnim)
            return FString::Printf(TEXT("ERR:AnimSequence not found at '%s'"), *AnimSequencePath);
    }

    if (LoadObject<UAnimMontage>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:An Anim Montage already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateAnimMontage", "GraphBridge: Create Anim Montage"));

    UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
    Factory->TargetSkeleton = Skeleton;
    Factory->SourceAnimation = SourceAnim;

    UAnimMontage* NewMontage = Cast<UAnimMontage>(Factory->FactoryCreateNew(
        UAnimMontage::StaticClass(), NewPackage, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!NewMontage)
        return TEXT("ERR:UAnimMontageFactory::FactoryCreateNew returned null");

    FAssetRegistryModule::AssetCreated(NewMontage);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewMontage, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:Anim Montage created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateAnimMontage: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// CreateBlendSpace
// Command: CREATE_BLEND_SPACE|AssetPath|SkeletonPath
// Confirmed against EditorFactories.cpp that FactoryCreateNew only needs
// TargetSkeleton pre-set — non-interactive.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::CreateBlendSpace(FString AssetPath, FString SkeletonPath)
{
    if (AssetPath.IsEmpty()) return TEXT("ERR:AssetPath cannot be empty");
    if (SkeletonPath.IsEmpty()) return TEXT("ERR:SkeletonPath cannot be empty");

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *SkeletonPath);

    if (LoadObject<UBlendSpace>(nullptr, *AssetPath))
        return FString::Printf(TEXT("ERR:A BlendSpace already exists at '%s'"), *AssetPath);

    FString PackageName, AssetName;
    if (!DeriveAssetPathParts(AssetPath, PackageName, AssetName))
        return TEXT("ERR:Could not derive an asset name from the given path");

    UPackage* NewPackage = CreatePackage(*PackageName);
    if (!NewPackage)
        return FString::Printf(TEXT("ERR:Failed to create package '%s'"), *PackageName);
    NewPackage->FullyLoad();

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "CreateBlendSpace", "GraphBridge: Create Blend Space"));

    UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
    Factory->TargetSkeleton = Skeleton;

    UBlendSpace* NewBlendSpace = Cast<UBlendSpace>(Factory->FactoryCreateNew(
        UBlendSpace::StaticClass(), NewPackage, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
    if (!NewBlendSpace)
        return TEXT("ERR:UBlendSpaceFactoryNew::FactoryCreateNew returned null");

    FAssetRegistryModule::AssetCreated(NewBlendSpace);
    NewPackage->MarkPackageDirty();

    const FString PackageFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags     = SAVE_NoError;

    if (!UPackage::SavePackage(NewPackage, NewBlendSpace, *PackageFilename, SaveArgs))
        return FString::Printf(TEXT("ERR:BlendSpace created but failed to save to disk at '%s'"), *PackageFilename);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge CreateBlendSpace: '%s'"), *AssetPath);
    return PackageName + TEXT(".") + AssetName;
}

// ---------------------------------------------------------------------------
// AddBlendSpaceSample
// Command: ADD_BLEND_SPACE_SAMPLE|BlendSpaceAssetPath|AnimSequencePath|X|Y
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddBlendSpaceSample(FString BlendSpaceAssetPath, FString AnimSequencePath,
    float X, float Y)
{
    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpaceAssetPath);
    if (!BlendSpace)
        return FString::Printf(TEXT("ERR:BlendSpace not found at '%s'"), *BlendSpaceAssetPath);

    UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *AnimSequencePath);
    if (!Sequence)
        return FString::Printf(TEXT("ERR:AnimSequence not found at '%s'"), *AnimSequencePath);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddBlendSpaceSample", "GraphBridge: Add Blend Space Sample"));
    BlendSpace->Modify();

    int32 SampleIndex = BlendSpace->AddSample(Sequence, FVector(X, Y, 0.0f));
    if (SampleIndex == INDEX_NONE)
        return FString::Printf(TEXT("ERR:AddSample rejected '%s' at (%f, %f) â€” out of range or duplicate coordinate"),
            *AnimSequencePath, X, Y);

    BlendSpace->ValidateSampleData();
    BlendSpace->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddBlendSpaceSample: '%s' at (%f, %f) -> index %d"),
        *AnimSequencePath, X, Y, SampleIndex);

    return FString::FromInt(SampleIndex);
}

// ---------------------------------------------------------------------------
// EditBlendSpaceSample
// Command: EDIT_BLEND_SPACE_SAMPLE|BlendSpaceAssetPath|AnimSequencePath|NewX|NewY
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::EditBlendSpaceSample(FString BlendSpaceAssetPath, FString AnimSequencePath,
    float NewX, float NewY)
{
    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpaceAssetPath);
    if (!BlendSpace)
        return FString::Printf(TEXT("ERR:BlendSpace not found at '%s'"), *BlendSpaceAssetPath);

    UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *AnimSequencePath);
    if (!Sequence)
        return FString::Printf(TEXT("ERR:AnimSequence not found at '%s'"), *AnimSequencePath);

    const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
    int32 FoundIndex = INDEX_NONE;
    for (int32 i = 0; i < Samples.Num(); ++i)
    {
        if (Samples[i].Animation == Sequence) { FoundIndex = i; break; }
    }
    if (FoundIndex == INDEX_NONE)
        return FString::Printf(TEXT("ERR:No existing sample found using animation '%s'"), *AnimSequencePath);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "EditBlendSpaceSample", "GraphBridge: Edit Blend Space Sample"));
    BlendSpace->Modify();

    bool bOk = BlendSpace->EditSampleValue(FoundIndex, FVector(NewX, NewY, 0.0f));
    if (!bOk)
        return FString::Printf(TEXT("ERR:EditSampleValue rejected (%f, %f) for sample %d â€” out of range or duplicate coordinate"),
            NewX, NewY, FoundIndex);

    BlendSpace->ValidateSampleData();
    BlendSpace->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge EditBlendSpaceSample: '%s' -> (%f, %f)"),
        *AnimSequencePath, NewX, NewY);

    return TEXT("");
}

// ---------------------------------------------------------------------------
// SetBlendSpacePlayerAsset
// Command: SET_BLEND_SPACE_PLAYER_ASSET|BPPath|NodeGUID|BlendSpaceAssetPath
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::SetBlendSpacePlayerAsset(FString BlueprintPath, FString NodeGUID,
    FString BlendSpaceAssetPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeGUID);
    UAnimGraphNode_BlendSpacePlayer* BSPNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node);
    if (!BSPNode)
        return FString::Printf(TEXT("ERR:Node not found or not a BlendSpacePlayer: '%s'"), *NodeGUID);

    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *BlendSpaceAssetPath);
    if (!BlendSpace)
        return FString::Printf(TEXT("ERR:Could not load BlendSpace at '%s'"), *BlendSpaceAssetPath);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "SetBlendSpacePlayerAsset", "GraphBridge: Set Blend Space Player Asset"));
    BSPNode->Modify();
    BSPNode->Node.SetBlendSpace(BlendSpace);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge SetBlendSpacePlayerAsset: '%s' -> '%s'"),
        *NodeGUID, *BlendSpaceAssetPath);

    return TEXT("");
}

// ---------------------------------------------------------------------------
// GetAnimPinConnections
// Command: GET_ANIM_PIN_CONNECTIONS|BPPath|NodeGUID|PinName
// See header comment.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::GetAnimPinConnections(FString BlueprintPath, FString NodeGUID, FString PinName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);

    UEdGraphNode* Node = FindNodeAnywhere(Blueprint, NodeGUID);
    if (!Node)
        return FString::Printf(TEXT("ERR:Node not found: '%s'"), *NodeGUID);

    UEdGraphPin* Pin = Node->FindPin(*PinName);
    if (!Pin)
        return FString::Printf(TEXT("ERR:Pin '%s' not found on node '%s'"), *PinName, *NodeGUID);

    TArray<FString> Entries;
    for (UEdGraphPin* Linked : Pin->LinkedTo)
    {
        if (!Linked || !Linked->GetOwningNode()) continue;
        Entries.Add(FString::Printf(TEXT("%s:%s"),
            *Linked->GetOwningNode()->NodeGuid.ToString(), *Linked->PinName.ToString()));
    }
    return FString::Join(Entries, TEXT(","));
}

// ---------------------------------------------------------------------------
// AddSkeletonSocket
// Command: ADD_SKELETON_SOCKET|SkeletonPath|SocketName|BoneName|X|Y|Z
// See header comment — USkeleton::AddSocket() does not exist; this
// replicates the same logic USkeletalMesh::AddSocket()'s bAddToSkeleton=true
// path uses internally, directly against Skeleton->Sockets.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddSkeletonSocket(FString SkeletonPath, FString SocketName, FString BoneName,
    float X, float Y, float Z)
{
    if (SkeletonPath.IsEmpty()) return TEXT("ERR:SkeletonPath cannot be empty");
    if (SocketName.IsEmpty()) return TEXT("ERR:SocketName cannot be empty");
    if (BoneName.IsEmpty()) return TEXT("ERR:BoneName cannot be empty");

    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *SkeletonPath);

    const FName BoneFName(*BoneName);
    if (Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneFName) == INDEX_NONE)
        return FString::Printf(TEXT("ERR:Bone '%s' not found in skeleton"), *BoneName);

    const FName SocketFName(*SocketName);
    if (Skeleton->Sockets.ContainsByPredicate([SocketFName](const TObjectPtr<USkeletalMeshSocket>& S) { return S->SocketName == SocketFName; }))
        return FString::Printf(TEXT("ERR:Socket '%s' already exists on this skeleton"), *SocketName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddSkeletonSocket", "GraphBridge: Add Skeleton Socket"));

    Skeleton->Modify();
    USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Skeleton);
    NewSocket->SocketName = SocketFName;
    NewSocket->BoneName = BoneFName;
    NewSocket->RelativeLocation = FVector(X, Y, Z);
    Skeleton->Sockets.Add(NewSocket);
    Skeleton->MarkPackageDirty();

    FString SaveError;
    if (!SaveExistingAssetPackage(Skeleton, SaveError))
        return SaveError;

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddSkeletonSocket: '%s' on bone '%s' in '%s'"),
        *SocketName, *BoneName, *SkeletonPath);
    return TEXT("");
}

#endif // WITH_EDITOR
