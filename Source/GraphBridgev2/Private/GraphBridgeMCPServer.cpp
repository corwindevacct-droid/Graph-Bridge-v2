// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeMCPServer.h"
#include "GraphBridgeAutomationLibrary.h"
#include "GraphBridgev2.h"

#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"

namespace
{
    const TCHAR* MCP_PROTOCOL_VERSION = TEXT("2025-06-18");

    // ---------------------------------------------------------------------
    // Tool schema table — the single source of truth for both tools/list's
    // JSON schemas and tools/call's argument-to-pipe-string conversion.
    //
    // This is metadata ABOUT the existing dispatch table in
    // GraphBridgeAutomationLibrary.cpp (ExecuteAtomicCommand), not a second
    // copy of it — every entry here maps 1:1 to one "else if (Op ==
    // TEXT(...))" branch there, in the same argument order. Adding a new
    // bridge command means adding one line here too; nothing here changes
    // how the WebSocket bridge behaves.
    // ---------------------------------------------------------------------

    struct FToolArg
    {
        const TCHAR* Name;
        const TCHAR* JsonType; // "string" or "number"
        bool bRequired = true;
    };

    struct FToolSpec
    {
        const TCHAR* Command;
        const TCHAR* Description;
        TArray<FToolArg> Args;
    };

    const TArray<FToolSpec>& GetToolSpecs()
    {
        static const TArray<FToolSpec> Specs = {
            { TEXT("SPAWN_NODE"), TEXT("Spawn a graph node in a Blueprint's EventGraph."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_class"),TEXT("string")}, {TEXT("comment"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")} } },
            { TEXT("SPAWN_EVENT_NODE"), TEXT("Spawn a class-level override event node (K2Node_Event)."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("event_func_name"),TEXT("string")}, {TEXT("comment"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")} } },
            { TEXT("CONNECT_PINS"), TEXT("Connect an output pin on node_a to an input pin on node_b."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_a"),TEXT("string")}, {TEXT("pin_a"),TEXT("string")}, {TEXT("node_b"),TEXT("string")}, {TEXT("pin_b"),TEXT("string")} } },
            { TEXT("DISCONNECT_PINS"), TEXT("Break all connections on pin_a of node_a."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_a"),TEXT("string")}, {TEXT("pin_a"),TEXT("string")}, {TEXT("node_b"),TEXT("string")}, {TEXT("pin_b"),TEXT("string")} } },
            { TEXT("DELETE_NODE"), TEXT("Delete a node by GUID or comment string."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")} } },
            { TEXT("CLEAR_NODES"), TEXT("Delete all EventGraph nodes whose comment contains comment_match."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("comment_match"),TEXT("string")} } },
            { TEXT("SET_PIN_DEFAULT"), TEXT("Set the literal default value on an unconnected input pin."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("pin_name"),TEXT("string")}, {TEXT("default_value"),TEXT("string")} } },
            { TEXT("GET_NODE_PINS"), TEXT("List pin descriptors (IN:Name / OUT:Name) for a node."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_name"),TEXT("string")} } },
            { TEXT("GET_PIN_CONNECTIONS"), TEXT("List what a pin is connected to."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("pin_name"),TEXT("string")} } },
            { TEXT("GET_PIN_DEFAULT"), TEXT("Read the current default value on a pin."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("pin_name"),TEXT("string")} } },
            { TEXT("SET_ANIM_CLASS"), TEXT("Set the AnimClass on a SkeletalMeshComponent and recompile."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("component_name"),TEXT("string")}, {TEXT("anim_bp_path"),TEXT("string")} } },
            { TEXT("COMPILE"), TEXT("Compile the Blueprint."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("SAVE_BLUEPRINT"), TEXT("Save the Blueprint package to disk."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("SPAWN_VARIABLE"), TEXT("Add a new member variable to a Blueprint."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("var_name"),TEXT("string")}, {TEXT("type_string"),TEXT("string")}, {TEXT("category"),TEXT("string"),false} } },
            { TEXT("ADD_COMPONENT"), TEXT("Add a component to a Blueprint's SimpleConstructionScript."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("component_class"),TEXT("string")}, {TEXT("component_name"),TEXT("string"),false}, {TEXT("parent_component_name"),TEXT("string"),false} } },
            { TEXT("SET_VARIABLE_DEFAULT"), TEXT("Set the CDO default value of a Blueprint member variable."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("var_name"),TEXT("string")}, {TEXT("default_value"),TEXT("string")} } },
            { TEXT("LIST_NODES"), TEXT("List all nodes in a Blueprint's EventGraph."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("FIND_NODE_CLASS"), TEXT("Search loaded UEdGraphNode subclasses by partial name."),
              { {TEXT("partial_name"),TEXT("string")} } },
            { TEXT("LIST_ASSETS"), TEXT("List Blueprint/animation/material assets, optionally filtered."),
              { {TEXT("filter"),TEXT("string"),false} } },
            { TEXT("SET_INPUT_ACTION"), TEXT("Wire a UInputAction asset to an input action node."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("input_action_path"),TEXT("string")} } },
            { TEXT("SET_FUNCTION_REF"), TEXT("Set the function reference on a K2Node_CallFunction and rebuild its pins."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("class_name"),TEXT("string")}, {TEXT("function_name"),TEXT("string")} } },
            { TEXT("SET_EVENT_REF"), TEXT("Bind a K2Node_Event to a named function on the parent class chain."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("function_name"),TEXT("string")} } },
            { TEXT("SET_VARIABLE_REF"), TEXT("Bind a K2Node_VariableGet/Set to a named Blueprint variable."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_id"),TEXT("string")}, {TEXT("var_name"),TEXT("string")} } },
            { TEXT("CLOSE_BLUEPRINT"), TEXT("Close the Blueprint Editor for this asset. Call before any SPAWN_NODE sequence."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("OPEN_BLUEPRINT"), TEXT("Reopen the Blueprint Editor after node spawning is complete."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("LIST_BLENDSPACES"), TEXT("List BlendSpace/BlendSpace1D assets, optionally filtered."),
              { {TEXT("filter"),TEXT("string"),false} } },
            { TEXT("LIST_ASSET_PROPERTIES"), TEXT("List editable UPROPERTYs on any UObject asset."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("GET_ASSET_PROPERTY"), TEXT("Get a property value on any UObject asset via reflection."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("property_name"),TEXT("string")} } },
            { TEXT("SET_ASSET_PROPERTY"), TEXT("Set a property value on any UObject asset via reflection."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("property_name"),TEXT("string")}, {TEXT("value"),TEXT("string")} } },
            { TEXT("GET_MONTAGE_INFO"), TEXT("Get JSON info on an AnimMontage's sections, slots and notifies."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("ADD_MONTAGE_SECTION"), TEXT("Add a named section to an AnimMontage."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("section_name"),TEXT("string")}, {TEXT("start_time"),TEXT("number")} } },
            { TEXT("REMOVE_MONTAGE_SECTION"), TEXT("Remove a named section from an AnimMontage."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("section_name"),TEXT("string")} } },
            { TEXT("SET_MONTAGE_SLOT"), TEXT("Rename an AnimMontage slot (GroupName.SlotName)."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("slot_index"),TEXT("number")}, {TEXT("new_slot_name"),TEXT("string")} } },
            { TEXT("ADD_MONTAGE_NOTIFY"), TEXT("Add a notify to an AnimMontage timeline."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("notify_class_name"),TEXT("string")}, {TEXT("time_seconds"),TEXT("number")} } },
            { TEXT("REMOVE_MONTAGE_NOTIFY"), TEXT("Remove a notify from an AnimMontage by index."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("notify_index"),TEXT("number")} } },
            { TEXT("LIST_DATATABLE_ROWS"), TEXT("List rows and fields of a DataTable."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("ADD_DATATABLE_ROW"), TEXT("Add a default-value row to a DataTable."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("row_name"),TEXT("string")} } },
            { TEXT("DELETE_DATATABLE_ROW"), TEXT("Delete a row from a DataTable."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("row_name"),TEXT("string")} } },
            { TEXT("RENAME_DATATABLE_ROW"), TEXT("Rename a DataTable row (handles undo/redo and cross-reference fixup)."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("old_name"),TEXT("string")}, {TEXT("new_name"),TEXT("string")} } },
            { TEXT("LIST_SKELETON_SOCKETS"), TEXT("List sockets on a Skeleton."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("ADD_SKELETON_SOCKET"), TEXT("Add a socket to a Skeleton at a bone's origin."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("socket_name"),TEXT("string")}, {TEXT("bone_name"),TEXT("string")} } },
            { TEXT("MOVE_SKELETON_SOCKET"), TEXT("Move/rotate a Skeleton socket (cm / degrees)."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("socket_name"),TEXT("string")}, {TEXT("loc_x"),TEXT("number")}, {TEXT("loc_y"),TEXT("number")}, {TEXT("loc_z"),TEXT("number")}, {TEXT("pitch"),TEXT("number")}, {TEXT("yaw"),TEXT("number")}, {TEXT("roll"),TEXT("number")} } },
            { TEXT("DELETE_SKELETON_SOCKET"), TEXT("Delete a socket from a Skeleton."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("socket_name"),TEXT("string")} } },
            { TEXT("CREATE_IMC"), TEXT("Create a new UInputMappingContext asset on disk."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("ADD_IMC_MAPPING"), TEXT("Add a key-to-action mapping to an Input Mapping Context."),
              { {TEXT("imc_path"),TEXT("string")}, {TEXT("action_path"),TEXT("string")}, {TEXT("key_name"),TEXT("string")}, {TEXT("modifier_classes"),TEXT("string"),false} } },
            { TEXT("REMOVE_IMC_MAPPING"), TEXT("Remove a key-to-action mapping from an Input Mapping Context."),
              { {TEXT("imc_path"),TEXT("string")}, {TEXT("action_path"),TEXT("string")}, {TEXT("key_name"),TEXT("string")} } },
            { TEXT("LIST_IMC_MAPPINGS"), TEXT("List all mappings in an Input Mapping Context."),
              { {TEXT("imc_path"),TEXT("string")} } },
            { TEXT("SAVE_ASSET"), TEXT("Save any UObject asset to disk (not just Blueprints)."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("SET_CHARACTER_MESH"), TEXT("Set the SkeletalMesh on a character's mesh component."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("mesh_path"),TEXT("string")}, {TEXT("component_name"),TEXT("string"),false} } },
            { TEXT("SET_CHARACTER_CAPSULE"), TEXT("Set HalfHeight/Radius on a character's capsule component."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("half_height"),TEXT("number")}, {TEXT("radius"),TEXT("number")}, {TEXT("component_name"),TEXT("string"),false} } },
            { TEXT("SET_CAMERA_BOOM"), TEXT("Set TargetArmLength/SocketOffset on a SpringArmComponent."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("arm_length"),TEXT("number")}, {TEXT("off_x"),TEXT("number")}, {TEXT("off_y"),TEXT("number")}, {TEXT("off_z"),TEXT("number")}, {TEXT("component_name"),TEXT("string"),false} } },
            { TEXT("ADD_IMC_TO_CHARACTER"), TEXT("Wire GetPlayerController->GetSubsystem->AddMappingContext into BeginPlay."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("imc_path"),TEXT("string")}, {TEXT("priority"),TEXT("number"),false} } },
            { TEXT("SET_GAMEMODE_PAWN"), TEXT("Set DefaultPawnClass on a GameMode Blueprint's CDO."),
              { {TEXT("game_mode_bp_path"),TEXT("string")}, {TEXT("pawn_class_path"),TEXT("string")} } },
            { TEXT("GET_CURRENT_GAMEMODE"), TEXT("Get the current editor-world GameMode class as JSON."), {} },
            { TEXT("GET_PLAYER_START"), TEXT("List PlayerStart actors in the current editor level as JSON."), {} },
            { TEXT("SET_LEVEL_GAMEMODE"), TEXT("Override DefaultGameMode for the current editor level. Pass 'None' to clear."),
              { {TEXT("game_mode_bp_path"),TEXT("string")} } },
            { TEXT("SET_CAST_TARGET"), TEXT("Set TargetType on a UK2Node_DynamicCast and rebuild its pins."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_guid"),TEXT("string")}, {TEXT("target_class_name"),TEXT("string")} } },
            { TEXT("SET_SUBSYSTEM_CLASS"), TEXT("Set CustomClass on a UK2Node_GetSubsystem and rebuild its pins."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_guid"),TEXT("string")}, {TEXT("subsystem_class_name"),TEXT("string")} } },
            { TEXT("ADD_VARIABLE"), TEXT("Add a Blueprint member variable (alias of SPAWN_VARIABLE)."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("var_name"),TEXT("string")}, {TEXT("var_type"),TEXT("string")}, {TEXT("category"),TEXT("string"),false} } },
            { TEXT("SET_VARIABLE_TYPE"), TEXT("Retype an existing Blueprint member variable."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("var_name"),TEXT("string")}, {TEXT("new_type"),TEXT("string")} } },
            { TEXT("LIST_VARIABLES"), TEXT("List a Blueprint's member variables (name/category/type)."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("RUN_PYTHON"), TEXT("Execute arbitrary Python inside UE and return captured stdout. Do NOT use for Blueprint graph inspection — use LIST_NODES/GET_NODE_PINS/LIST_VARIABLES instead, they are faster and always work."),
              { {TEXT("code"),TEXT("string")} } },
            { TEXT("GET_COMPILE_ERRORS"), TEXT("Compile a Blueprint and return detailed ERROR/WARNING diagnostics, or CLEAN."),
              { {TEXT("bp_path"),TEXT("string")} } },
            { TEXT("CREATE_BLUEPRINT"), TEXT("Create a new Blueprint asset inheriting from a given parent class."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("parent_class"),TEXT("string")} } },
            { TEXT("CREATE_FUNCTION"), TEXT("Create a new custom function graph in a Blueprint."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("function_name"),TEXT("string")} } },
            { TEXT("SPAWN_NODE_IN_GRAPH"), TEXT("Spawn a node in a named graph — EventGraph or any function graph."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("graph_name"),TEXT("string")}, {TEXT("node_class"),TEXT("string")}, {TEXT("comment"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")} } },
            { TEXT("SET_NODE_POSITION"), TEXT("Move a node to specific graph coordinates (searches all graphs)."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("node_guid"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")} } },
            { TEXT("CREATE_INPUT_ACTION"), TEXT("Create a new UInputAction asset with a given value type."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("value_type"),TEXT("string")} } },
            { TEXT("SPAWN_ACTOR_IN_LEVEL"), TEXT("Place a Blueprint actor instance in the current editor level."),
              { {TEXT("bp_path"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")}, {TEXT("z"),TEXT("number")}, {TEXT("rot_yaw"),TEXT("number")} } },
            { TEXT("LIST_LEVEL_ACTORS"), TEXT("List actors in the current editor level, optionally filtered."),
              { {TEXT("filter"),TEXT("string"),false} } },
            { TEXT("SET_ACTOR_TRANSFORM"), TEXT("Move/rotate/scale a level actor by its label."),
              { {TEXT("actor_label"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")}, {TEXT("z"),TEXT("number")}, {TEXT("pitch"),TEXT("number")}, {TEXT("yaw"),TEXT("number")}, {TEXT("roll"),TEXT("number")}, {TEXT("sx"),TEXT("number"),false}, {TEXT("sy"),TEXT("number"),false}, {TEXT("sz"),TEXT("number"),false} } },
            { TEXT("DELETE_LEVEL_ACTOR"), TEXT("Remove an actor from the level by its label."),
              { {TEXT("actor_label"),TEXT("string")} } },
            { TEXT("CREATE_WIDGET_BLUEPRINT"), TEXT("Create a new UMG Widget Blueprint with an empty canvas root."),
              { {TEXT("asset_path"),TEXT("string")} } },
            { TEXT("ADD_WIDGET_ELEMENT"), TEXT("Add a widget element as a child of the root canvas panel."),
              { {TEXT("widget_bp_path"),TEXT("string")}, {TEXT("element_type"),TEXT("string")}, {TEXT("name"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")}, {TEXT("w"),TEXT("number")}, {TEXT("h"),TEXT("number")} } },
            { TEXT("SET_WIDGET_TEXT"), TEXT("Set the default text of a UTextBlock (or other text widget) by name."),
              { {TEXT("widget_bp_path"),TEXT("string")}, {TEXT("element_name"),TEXT("string")}, {TEXT("text"),TEXT("string")} } },
            { TEXT("CREATE_MATERIAL"), TEXT("Create a new Material asset with a given blend mode."),
              { {TEXT("asset_path"),TEXT("string")}, {TEXT("blend_mode"),TEXT("string")} } },
            { TEXT("ADD_MATERIAL_NODE"), TEXT("Add a material expression node to a Material's graph."),
              { {TEXT("material_path"),TEXT("string")}, {TEXT("node_type"),TEXT("string")}, {TEXT("x"),TEXT("number")}, {TEXT("y"),TEXT("number")} } },
            { TEXT("CONNECT_MATERIAL_PINS"), TEXT("Connect an output pin of one material expression to an input pin of another. Use '_' for a pin name to mean 'the first pin'."),
              { {TEXT("material_path"),TEXT("string")}, {TEXT("node_index_a"),TEXT("number")}, {TEXT("output_pin"),TEXT("string")}, {TEXT("node_index_b"),TEXT("number")}, {TEXT("input_pin"),TEXT("string")} } },
            { TEXT("SET_MATERIAL_RESULT"), TEXT("Connect a material expression's output to a final material property (BaseColor, Metallic, etc)."),
              { {TEXT("material_path"),TEXT("string")}, {TEXT("channel"),TEXT("string")}, {TEXT("node_index"),TEXT("number")}, {TEXT("output_pin"),TEXT("string")} } },
            { TEXT("COMPILE_MATERIAL"), TEXT("Recompile a Material and report ERROR diagnostics, or CLEAN."),
              { {TEXT("material_path"),TEXT("string")} } },
            { TEXT("CLOSE_MATERIAL"), TEXT("Close the Material Editor for this asset before graph mutation."),
              { {TEXT("material_path"),TEXT("string")} } },
        };
        return Specs;
    }

    const FToolSpec* FindToolSpec(const FString& Name)
    {
        for (const FToolSpec& Spec : GetToolSpecs())
            if (Name.Equals(Spec.Command, ESearchCase::CaseSensitive))
                return &Spec;
        return nullptr;
    }

    // Formats a JSON number without a spurious ".0" for whole numbers, so
    // e.g. node coordinates round-trip as "200" rather than "200.0".
    FString FormatJsonNumber(double Value)
    {
        if (FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value)))
            return FString::Printf(TEXT("%lld"), (int64)FMath::RoundToDouble(Value));
        return FString::SanitizeFloat(Value);
    }

    TSharedRef<FJsonObject> MakeJsonRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
    {
        TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
        Error->SetNumberField(TEXT("code"), Code);
        Error->SetStringField(TEXT("message"), Message);

        TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
        Envelope->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        if (Id.IsValid()) Envelope->SetField(TEXT("id"), Id);
        Envelope->SetObjectField(TEXT("error"), Error);
        return Envelope;
    }

    TSharedRef<FJsonObject> MakeJsonRpcResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result)
    {
        TSharedRef<FJsonObject> Envelope = MakeShared<FJsonObject>();
        Envelope->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
        if (Id.IsValid()) Envelope->SetField(TEXT("id"), Id);
        Envelope->SetObjectField(TEXT("result"), Result);
        return Envelope;
    }

    FString JsonObjectToString(const TSharedRef<FJsonObject>& Object)
    {
        FString Output;
        TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
            TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
        // The FJsonObject-element overload of Serialize takes the writer by
        // raw reference (not TSharedRef) and requires bCloseWriter explicitly.
        FJsonSerializer::Serialize(Object, Writer.Get(), true);
        return Output;
    }

    TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Body, EHttpServerResponseCodes Code)
    {
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonObjectToString(Body), TEXT("application/json"));
        Response->Code = Code;
        return Response;
    }
}

FGraphBridgeMCPServer::FGraphBridgeMCPServer() = default;
FGraphBridgeMCPServer::~FGraphBridgeMCPServer() { Stop(); }

bool FGraphBridgeMCPServer::Start(int32 Port)
{
    if (RouteHandle.IsValid()) return true; // already running

    Router = FHttpServerModule::Get().GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
    if (!Router)
    {
        UE_LOG(LogGraphBridge, Error, TEXT("GraphBridge MCP: failed to bind HTTP router on port %d"), Port);
        return false;
    }

    RouteHandle = Router->BindRoute(
        FHttpPath(TEXT("/mcp")),
        EHttpServerRequestVerbs::VERB_POST,
        FHttpRequestHandler::CreateRaw(this, &FGraphBridgeMCPServer::HandleMCPRequest));

    if (!RouteHandle.IsValid())
    {
        UE_LOG(LogGraphBridge, Error, TEXT("GraphBridge MCP: BindRoute failed for /mcp on port %d"), Port);
        Router.Reset();
        return false;
    }

    FHttpServerModule::Get().StartAllListeners();
    BoundPort = Port;
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge MCP: listening on http://127.0.0.1:%d/mcp"), Port);
    return true;
}

void FGraphBridgeMCPServer::Stop()
{
    if (RouteHandle.IsValid() && Router.IsValid())
    {
        Router->UnbindRoute(RouteHandle);
    }
    RouteHandle.Reset();
    Router.Reset();
    BoundPort = 0;
}

bool FGraphBridgeMCPServer::HandleMCPRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    // Security: the Streamable HTTP transport spec requires validating Origin
    // to prevent DNS rebinding (a malicious page resolving to 127.0.0.1 and
    // issuing same-origin-looking requests from a real browser). Standalone
    // MCP clients (Claude Code, Cursor, curl, SDKs) never send an Origin
    // header at all — only browsers do — so rejecting any request that DOES
    // carry one blocks the browser-based attack without affecting real
    // clients. Combined with FHttpServerConfig::BindAddress defaulting to
    // "localhost" (not 0.0.0.0), this satisfies both of the transport spec's
    // security requirements.
    if (const TArray<FString>* OriginHeader = Request.Headers.Find(TEXT("Origin")))
    {
        if (OriginHeader->Num() > 0 && !(*OriginHeader)[0].IsEmpty())
        {
            TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(
                TEXT("Requests with an Origin header are not accepted (DNS-rebinding protection)."), TEXT("text/plain"));
            Response->Code = EHttpServerResponseCodes::Forbidden;
            OnComplete(MoveTemp(Response));
            return true;
        }
    }

    // Every JSON-RPC message the client sends is a fresh HTTP POST (per the
    // Streamable HTTP transport spec) — parse it fresh each time, no session
    // state carried between requests.
    // Request.Body is not null-terminated, so convert with an explicit
    // length via StringCast rather than treating it as a C string.
    auto BodyConversion = StringCast<TCHAR>(
        reinterpret_cast<const UTF8CHAR*>(Request.Body.GetData()), Request.Body.Num());
    FString BodyStr(BodyConversion.Length(), BodyConversion.Get());

    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(BodyStr);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        OnComplete(MakeJsonResponse(MakeJsonRpcError(nullptr, -32700, TEXT("Parse error")), EHttpServerResponseCodes::BadRequest));
        return true;
    }

    TSharedPtr<FJsonValue> Id = RootObject->TryGetField(TEXT("id"));
    FString Method = RootObject->GetStringField(TEXT("method"));
    const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
    RootObject->TryGetObjectField(TEXT("params"), ParamsPtr);
    TSharedPtr<FJsonObject> Params = ParamsPtr ? *ParamsPtr : nullptr;

    // Notifications (no "id") never get a JSON-RPC response body — just 202
    // Accepted, per the Streamable HTTP transport spec.
    const bool bIsNotification = !Id.IsValid();

    if (Method == TEXT("notifications/initialized") || Method == TEXT("notifications/cancelled"))
    {
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Accepted;
        OnComplete(MoveTemp(Response));
        return true;
    }

    TSharedPtr<FJsonObject> Result;
    int32 ErrorCode = 0;
    FString ErrorMessage;

    if (Method == TEXT("initialize"))
    {
        Result = HandleInitialize(Params);
    }
    else if (Method == TEXT("tools/list"))
    {
        Result = HandleToolsList();
    }
    else if (Method == TEXT("tools/call"))
    {
        Result = HandleToolsCall(Params);
        if (!Result.IsValid())
        {
            ErrorCode = -32602;
            ErrorMessage = TEXT("Invalid params: missing 'name' or a required argument");
        }
    }
    else if (Method == TEXT("ping"))
    {
        Result = MakeShared<FJsonObject>();
    }
    else
    {
        ErrorCode = -32601;
        ErrorMessage = FString::Printf(TEXT("Method not found: %s"), *Method);
    }

    if (bIsNotification)
    {
        TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(FString(), TEXT("application/json"));
        Response->Code = EHttpServerResponseCodes::Accepted;
        OnComplete(MoveTemp(Response));
        return true;
    }

    if (ErrorCode != 0)
    {
        OnComplete(MakeJsonResponse(MakeJsonRpcError(Id, ErrorCode, ErrorMessage), EHttpServerResponseCodes::Ok));
    }
    else
    {
        OnComplete(MakeJsonResponse(MakeJsonRpcResult(Id, Result), EHttpServerResponseCodes::Ok));
    }
    return true;
}

TSharedPtr<FJsonObject> FGraphBridgeMCPServer::HandleInitialize(const TSharedPtr<FJsonObject>& Params)
{
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("protocolVersion"), MCP_PROTOCOL_VERSION);

    TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> ToolsCapability = MakeShared<FJsonObject>();
    ToolsCapability->SetBoolField(TEXT("listChanged"), false);
    Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);
    Result->SetObjectField(TEXT("capabilities"), Capabilities);

    TSharedRef<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
    ServerInfo->SetStringField(TEXT("name"), TEXT("GraphBridge"));
    ServerInfo->SetStringField(TEXT("title"), TEXT("GraphBridge AI"));
    ServerInfo->SetStringField(TEXT("version"), TEXT("1.0.10"));
    Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

    Result->SetStringField(TEXT("instructions"),
        TEXT("GraphBridge exposes Unreal Blueprint/level automation as MCP tools. ")
        TEXT("Always call LIST_NODES before modifying a Blueprint graph — never guess GUIDs or pin names. ")
        TEXT("Always call CLOSE_BLUEPRINT before SPAWN_NODE / SPAWN_NODE_IN_GRAPH."));

    return Result;
}

TSharedPtr<FJsonObject> FGraphBridgeMCPServer::HandleToolsList()
{
    TArray<TSharedPtr<FJsonValue>> Tools;

    for (const FToolSpec& Spec : GetToolSpecs())
    {
        TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Required;

        for (const FToolArg& Arg : Spec.Args)
        {
            TSharedRef<FJsonObject> Prop = MakeShared<FJsonObject>();
            Prop->SetStringField(TEXT("type"), Arg.JsonType);
            Properties->SetObjectField(Arg.Name, Prop);
            if (Arg.bRequired)
                Required.Add(MakeShared<FJsonValueString>(Arg.Name));
        }

        TSharedRef<FJsonObject> InputSchema = MakeShared<FJsonObject>();
        InputSchema->SetStringField(TEXT("type"), TEXT("object"));
        InputSchema->SetObjectField(TEXT("properties"), Properties);
        InputSchema->SetArrayField(TEXT("required"), Required);

        TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
        Tool->SetStringField(TEXT("name"), Spec.Command);
        Tool->SetStringField(TEXT("description"), Spec.Description);
        Tool->SetObjectField(TEXT("inputSchema"), InputSchema);

        Tools.Add(MakeShared<FJsonValueObject>(Tool));
    }

    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tools"), Tools);
    return Result;
}

TSharedPtr<FJsonObject> FGraphBridgeMCPServer::HandleToolsCall(const TSharedPtr<FJsonObject>& Params)
{
    if (!Params.IsValid()) return nullptr;

    FString ToolName;
    if (!Params->TryGetStringField(TEXT("name"), ToolName)) return nullptr;

    const FToolSpec* Spec = FindToolSpec(ToolName);

    auto MakeToolResult = [](const FString& Text, bool bIsError) -> TSharedPtr<FJsonObject>
    {
        TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
        TextBlock->SetStringField(TEXT("type"), TEXT("text"));
        TextBlock->SetStringField(TEXT("text"), Text);

        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(TextBlock) });
        Result->SetBoolField(TEXT("isError"), bIsError);
        return Result;
    };

    if (!Spec)
    {
        return MakeToolResult(FString::Printf(TEXT("Unknown tool: %s"), *ToolName), true);
    }

    const TSharedPtr<FJsonObject>* ArgumentsPtr = nullptr;
    Params->TryGetObjectField(TEXT("arguments"), ArgumentsPtr);
    TSharedPtr<FJsonObject> Arguments = ArgumentsPtr ? *ArgumentsPtr : MakeShared<FJsonObject>();

    // Build the pipe-delimited command string in the spec's declared order.
    // Optional args are always trailing (matches the real wire protocol —
    // see e.g. SPAWN_VARIABLE's Category or ADD_COMPONENT's ParentComponentName),
    // so the first missing arg — required or not — ends the command.
    FString Command = Spec->Command;
    for (const FToolArg& Arg : Spec->Args)
    {
        TSharedPtr<FJsonValue> Value = Arguments->TryGetField(Arg.Name);
        if (!Value.IsValid())
        {
            if (Arg.bRequired)
            {
                return MakeToolResult(
                    FString::Printf(TEXT("Missing required argument '%s' for tool '%s'"), Arg.Name, Spec->Command),
                    true);
            }
            break;
        }

        FString ValueStr;
        if (Value->Type == EJson::Number)
            ValueStr = FormatJsonNumber(Value->AsNumber());
        else
            ValueStr = Value->AsString();

        Command += TEXT("|") + ValueStr;
    }

    // DispatchCommandSync runs the exact same command the WebSocket bridge
    // would — same FScopedTransaction/undo support, same validation, same
    // error messages. MCP is purely a different way to reach it.
    FString ResponseJson = UGraphBridgeAutomationLibrary::DispatchCommandSync(Command);

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<TCHAR>> ResponseReader = TJsonReaderFactory<TCHAR>::Create(ResponseJson);
    if (!FJsonSerializer::Deserialize(ResponseReader, ResponseObject) || !ResponseObject.IsValid())
    {
        return MakeToolResult(FString::Printf(TEXT("Malformed response from dispatcher: %s"), *ResponseJson), true);
    }

    bool bSuccess = ResponseObject->GetBoolField(TEXT("success"));
    FString Message = ResponseObject->GetStringField(TEXT("message"));
    FString Payload = ResponseObject->GetStringField(TEXT("payload"));

    FString Text = Message;
    if (!Payload.IsEmpty())
        Text += TEXT("\n") + Payload;

    return MakeToolResult(Text, !bSuccess);
}
