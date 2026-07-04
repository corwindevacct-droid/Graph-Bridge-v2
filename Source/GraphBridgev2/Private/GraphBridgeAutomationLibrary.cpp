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
#include "K2Node_Event.h"
#include "K2Node_InputAction.h"
#include "EnhancedInputActionDelegateBinding.h"
#include "K2Node_VariableGet.h"
#include "K2Node_SpawnActorFromClass.h"
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
        FString Result = SpawnNode(P[1], P[2], P[3],
            FCString::Atoi(*P[4]), FCString::Atoi(*P[5]));
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
        FString Err = ConnectPins(P[1], P[2], P[3], P[4], P[5]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pins connected") : Err, TEXT(""));
    }
    else if (Op == TEXT("DISCONNECT_PINS") && P.Num() >= 6)
    {
        bool bOk = DisconnectPins(P[1], P[2], P[3], P[4], P[5]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Pins disconnected") : TEXT("Disconnect failed"), TEXT(""));
    }
    else if (Op == TEXT("DELETE_NODE") && P.Num() >= 3)
    {
        bool bOk = DeleteNode(P[1], P[2]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Node deleted") : TEXT("Delete failed"), TEXT(""));
    }
    else if (Op == TEXT("CLEAR_NODES") && P.Num() >= 3)
    {
        bool bOk = ClearNodes(P[1], P[2]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Nodes cleared") : TEXT("Clear failed"), TEXT(""));
    }
    else if (Op == TEXT("SET_PIN_DEFAULT") && P.Num() >= 5)
    {
        bool bOk = SetPinDefault(P[1], P[2], P[3], P[4]);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Default set") : TEXT("Set default failed"), TEXT(""));
    }
    else if (Op == TEXT("GET_NODE_PINS") && P.Num() >= 3)
    {
        FString Pins = GetNodePins(P[1], P[2]);
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
        // LIST_NODES|BPPath
        // Returns: GUID:Title:Comment|GUID:Title:Comment|...
        FString Results = ListNodes(P[1]);
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

    else if (Op == TEXT("SET_VARIABLE_REF") && P.Num() >= 4)
    {
        // SET_VARIABLE_REF|BPPath|NodeId|VarName
        // Returns the available variable list in the message on failure
        FString VarRefErr;
        bool bOk = SetVariableRef(P[1], P[2], P[3], VarRefErr);
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Variable reference set") : VarRefErr, TEXT(""));
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
    else if (Op == TEXT("REMOVE_MONTAGE_NOTIFY") && P.Num() >= 3)
    {
        // REMOVE_MONTAGE_NOTIFY|AssetPath|NotifyIndex
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
    else if (Op == TEXT("ADD_SKELETON_SOCKET") && P.Num() >= 4)
    {
        // ADD_SKELETON_SOCKET|SkeletonAssetPath|SocketName|BoneName
        FString Err = AddSkeletonSocket(P[1], P[2], P[3]);
        bool bOk = Err.IsEmpty();
        SendResponse(Sender, bOk, Op,
            bOk ? TEXT("Socket added") : Err, TEXT(""));
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
    else if (Op == TEXT("SET_VARIABLE_TYPE") && P.Num() >= 4)
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
    FString Comment, int32 X, int32 Y)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("ERR:Blueprint not found at '%s'"), *BlueprintPath);
    if (!Blueprint->UbergraphPages.Num())
        return TEXT("ERR:Blueprint has no EventGraph pages");

    return SpawnNodeOnGraph(Blueprint, Blueprint->UbergraphPages[0], NodeClass, Comment, X, Y);
}

// ---------------------------------------------------------------------------
// FindGraphByName
// "EventGraph" (or any UbergraphPages name match) resolves to the main event
// graph. Anything else is matched by name against Blueprint->FunctionGraphs
// (created via CREATE_FUNCTION, or any other user function graph).
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
    FString NodeA, FString PinA, FString NodeB, FString PinB)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint)
        return FString::Printf(TEXT("Blueprint not found at '%s'"), *BlueprintPath);
    if (!Blueprint->UbergraphPages.Num())
        return TEXT("Blueprint has no EventGraph pages");

    UEdGraph* Graph = Blueprint->UbergraphPages[0];
    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return TEXT("Could not get K2 schema");

    UEdGraphNode* SourceNode = FindNodeById(Blueprint, NodeA);
    if (!SourceNode) SourceNode = FindNodeByName(Blueprint, NodeA);
    if (!SourceNode)
        return FString::Printf(TEXT("Source node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeA);

    UEdGraphNode* TargetNode = FindNodeById(Blueprint, NodeB);
    if (!TargetNode) TargetNode = FindNodeByName(Blueprint, NodeB);
    if (!TargetNode)
        return FString::Printf(TEXT("Target node not found: '%s' — run LIST_NODES to get valid GUIDs"), *NodeB);

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
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return TEXT(""); // empty = success
}

bool UGraphBridgeAutomationLibrary::DisconnectPins(FString BlueprintPath,
    FString NodeA, FString PinA, FString NodeB, FString PinB)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;
    if (!Blueprint->UbergraphPages.Num()) return false;

    UEdGraph* Graph = Blueprint->UbergraphPages[0];
    const UEdGraphSchema_K2* Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
    if (!Schema) return false;

    UEdGraphNode* SourceNode = FindNodeById(Blueprint, NodeA);
    if (!SourceNode) SourceNode = FindNodeByName(Blueprint, NodeA);
    UEdGraphNode* TargetNode = FindNodeById(Blueprint, NodeB);
    if (!TargetNode) TargetNode = FindNodeByName(Blueprint, NodeB);
    if (!SourceNode || !TargetNode) return false;

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

bool UGraphBridgeAutomationLibrary::DeleteNode(FString BlueprintPath, FString NodeId)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
    if (!Node) Node = FindNodeByName(Blueprint, NodeId);
    if (!Node) return false;

    UEdGraph* Graph = Node->GetGraph();
    if (!Graph) return false;

    const FScopedTransaction Transaction(NSLOCTEXT("GraphBridge", "DeleteNode", "GraphBridge: Delete Node"));
    Graph->Modify();
    Node->Modify();
    Graph->RemoveNode(Node);
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    return true;
}

bool UGraphBridgeAutomationLibrary::ClearNodes(FString BlueprintPath, FString CommentMatch)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return false;

    TArray<UEdGraphNode*> ToRemove;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
        for (UEdGraphNode* Node : Graph->Nodes)
            if (Node && Node->NodeComment.Contains(CommentMatch))
                ToRemove.Add(Node);

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

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
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

FString UGraphBridgeAutomationLibrary::GetNodePins(FString BlueprintPath, FString NodeName)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    UEdGraphNode* Node = FindNodeByName(Blueprint, NodeName);
    if (!Node) Node = FindNodeById(Blueprint, NodeName);
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

FEdGraphPinType UGraphBridgeAutomationLibrary::ResolveTypeString(const FString& TypeString)
{
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

FString UGraphBridgeAutomationLibrary::ListNodes(FString BlueprintPath)
{
    UBlueprint* Blueprint = GetBlueprintByPath(BlueprintPath);
    if (!Blueprint) return TEXT("");

    TArray<FString> Entries;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
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

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
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

    UEdGraphNode* Node = FindNodeById(Blueprint, NodeId);
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
// RemoveMontageNotify
// Command: REMOVE_MONTAGE_NOTIFY|AssetPath|NotifyIndex
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
// AddSkeletonSocket
// Command: ADD_SKELETON_SOCKET|SkeletonAssetPath|SocketName|BoneName
// Position defaults to bone origin — use MOVE_SKELETON_SOCKET to place it.
// ---------------------------------------------------------------------------
FString UGraphBridgeAutomationLibrary::AddSkeletonSocket(
    FString AssetPath, FString SocketName, FString BoneName)
{
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *AssetPath);
    if (!Skeleton)
        return FString::Printf(TEXT("ERR:Skeleton not found at '%s'"), *AssetPath);
    if (SocketName.IsEmpty())
        return TEXT("ERR:SocketName cannot be empty");
    if (BoneName.IsEmpty())
        return TEXT("ERR:BoneName cannot be empty");

    // Guard: duplicate name
    for (const USkeletalMeshSocket* S : Skeleton->Sockets)
        if (S && S->SocketName == FName(*SocketName))
            return FString::Printf(TEXT("ERR:Socket '%s' already exists"), *SocketName);

    // Guard: bone must exist on the skeleton
    if (Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
        return FString::Printf(TEXT("ERR:Bone '%s' not found on skeleton"), *BoneName);

    const FScopedTransaction Transaction(
        NSLOCTEXT("GraphBridge", "AddSkeletonSocket", "GraphBridge: Add Skeleton Socket"));
    Skeleton->Modify();

    USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(
        Skeleton, NAME_None, RF_Transactional);
    NewSocket->SocketName       = FName(*SocketName);
    NewSocket->BoneName         = FName(*BoneName);
    NewSocket->RelativeLocation = FVector::ZeroVector;
    NewSocket->RelativeRotation = FRotator::ZeroRotator;
    NewSocket->RelativeScale    = FVector::OneVector;

    Skeleton->Sockets.Add(NewSocket);
    Skeleton->MarkPackageDirty();

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge AddSkeletonSocket: '%s' on bone '%s'"),
        *SocketName, *BoneName);
    return TEXT("");
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
// Property resolution uses FObjectProperty reflection to handle UE 5.1+ rename:
//   SkeletalMeshAsset (preferred, UE 5.1+) with SkeletalMesh as fallback.
// // VERIFY: property name in UE 5.7 CDO template context
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

    // Writes the mesh to a SkeletalMeshComponent template via property reflection.
    // Tries SkeletalMeshAsset (UE 5.1+) first, then SkeletalMesh for backwards compat.
    auto ApplyMesh = [&](USkeletalMeshComponent* SkelComp) -> bool
    {
        for (const TCHAR* PropName : { TEXT("SkeletalMeshAsset"), TEXT("SkeletalMesh") })
        {
            FObjectProperty* Prop = FindFProperty<FObjectProperty>(SkelComp->GetClass(), PropName);
            if (Prop && Prop->PropertyClass && Prop->PropertyClass->IsChildOf(USkeletalMesh::StaticClass()))
            {
                Prop->SetObjectPropertyValue_InContainer(SkelComp, Mesh);
                return true;
            }
        }
        return false;
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
// Pin name notes (// VERIFY against live Blueprint in UE 5.7):
//   GetLocalPlayerSubsystem params: "PlayerController", "Class"
//   AddMappingContext target pin: "self" (UEdGraphSchema_K2::PN_Self)
//   AddMappingContext params: "MappingContext", "Priority"
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

    // --- Resolve the three functions ---
    auto FindUFunction = [](const TCHAR* ShortClass, const TCHAR* FuncName) -> UFunction*
    {
        UClass* C = FindFirstObject<UClass>(ShortClass, EFindFirstObjectOptions::None);
        if (!C) C = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), ShortClass),
                                             EFindFirstObjectOptions::None);
        return C ? C->FindFunctionByName(FuncName) : nullptr;
    };

    UFunction* FnGetPC  = FindUFunction(TEXT("GameplayStatics"),        TEXT("GetPlayerController"));
    UFunction* FnGetSub = FindUFunction(TEXT("SubsystemBlueprintLibrary"), TEXT("GetLocalPlayerSubsystem")); // VERIFY
    UFunction* FnAddIMC = FindUFunction(TEXT("EnhancedInputLocalPlayerSubsystem"), TEXT("AddMappingContext"));

    if (!FnGetPC)
        return TEXT("ERR:UGameplayStatics::GetPlayerController not found — is the Engine module loaded?");
    if (!FnGetSub)
        return TEXT("ERR:USubsystemBlueprintLibrary::GetLocalPlayerSubsystem not found. " // VERIFY
                    "Ensure the Engine module is loaded.");
    if (!FnAddIMC)
        return TEXT("ERR:UEnhancedInputLocalPlayerSubsystem::AddMappingContext not found — "
                    "is the EnhancedInput plugin enabled for this project?");

    // Resolve UEnhancedInputLocalPlayerSubsystem class for the Class pin
    UClass* EISubClass = FindFirstObject<UClass>(
        TEXT("EnhancedInputLocalPlayerSubsystem"), EFindFirstObjectOptions::None);
    if (!EISubClass)
        EISubClass = FindFirstObject<UClass>(
            TEXT("UEnhancedInputLocalPlayerSubsystem"), EFindFirstObjectOptions::None);

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
    UK2Node_CallFunction* GetSubNode = SpawnCallNode(FnGetSub, BaseX + 380, BaseY);
    UK2Node_CallFunction* AddIMCNode = SpawnCallNode(FnAddIMC, BaseX + 760, BaseY);

    if (!GetPCNode || !GetSubNode || !AddIMCNode)
        return TEXT("ERR:Failed to spawn one or more Blueprint nodes. "
                    "Try CLOSE_BLUEPRINT first to release the editor viewport.");

    // Wire exec chain: BeginPlay.Then → GetPC → GetSub → AddIMC
    auto TryWire = [&](UEdGraphPin* Output, UEdGraphPin* Input)
    {
        if (Output && Input)
            Schema->TryCreateConnection(Output, Input);
    };

    TryWire(BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then),
            GetPCNode->FindPin(UEdGraphSchema_K2::PN_Execute));
    TryWire(GetPCNode->FindPin(UEdGraphSchema_K2::PN_Then),
            GetSubNode->FindPin(UEdGraphSchema_K2::PN_Execute));
    TryWire(GetSubNode->FindPin(UEdGraphSchema_K2::PN_Then),
            AddIMCNode->FindPin(UEdGraphSchema_K2::PN_Execute));

    // GetPC.ReturnValue → GetSub.PlayerController // VERIFY: pin name "PlayerController"
    TryWire(GetPCNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue),
            GetSubNode->FindPin(TEXT("PlayerController")));

    // Set GetSub.Class pin to UEnhancedInputLocalPlayerSubsystem // VERIFY: pin name "Class"
    if (UEdGraphPin* ClassPin = GetSubNode->FindPin(TEXT("Class")))
    {
        if (EISubClass)
            ClassPin->DefaultObject = EISubClass;
    }

    // GetSub.ReturnValue → AddIMC.self (Target)  // VERIFY: "self" vs "Target" in UE 5.7
    UEdGraphPin* AddIMCTarget = AddIMCNode->FindPin(UEdGraphSchema_K2::PN_Self);
    if (!AddIMCTarget)
        AddIMCTarget = AddIMCNode->FindPin(TEXT("Target"));
    TryWire(GetSubNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue), AddIMCTarget);

    // Set AddIMC.MappingContext pin to IMC asset object reference  // VERIFY: pin name "MappingContext"
    if (UEdGraphPin* IMCPin = AddIMCNode->FindPin(TEXT("MappingContext")))
        IMCPin->DefaultObject = IMC;

    // Set AddIMC.Priority pin  // VERIFY: pin name "Priority"
    if (UEdGraphPin* PriorityPin = AddIMCNode->FindPin(TEXT("Priority")))
        PriorityPin->DefaultValue = FString::FromInt(Priority);

    // Default PlayerIndex to 0 on GetPC
    if (UEdGraphPin* IndexPin = GetPCNode->FindPin(TEXT("PlayerIndex")))
        IndexPin->DefaultValue = TEXT("0");

    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

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
// // VERIFY: whether recompile alone is sufficient or if MarkModified is needed
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
// // VERIFY: whether WorldSettings.Modify() + MarkPackageDirty is sufficient
//             or whether a World->CommitMapChange() call is needed
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

#endif // WITH_EDITOR
