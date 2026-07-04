// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeLLMClient.h"
#include "GraphBridgeSettings.h"
#include "GraphBridgev2.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ── System prompt ─────────────────────────────────────────────────────────────

static const FString GSystemPrompt = TEXT(
    "You are GraphBridge, an AI assistant embedded in the Unreal Engine editor. "
    "You help users build and modify Blueprint graphs using natural language. "
    "\n\n"
    "STRICT RULES — follow these exactly:\n"
    "1. NEVER use RUN_PYTHON to inspect or query Blueprint nodes, pins, or connections. "
    "Use LIST_NODES, GET_NODE_PINS, and LIST_VARIABLES instead — they are faster and always work.\n"
    "2. NEVER use RUN_PYTHON to iterate over blueprint.graphs, blueprint.nodes, or any Blueprint graph property — these are not exposed to Python in UE5.\n"
    "3. Use RUN_PYTHON ONLY for tasks that have no dedicated GraphBridge tool, such as moving actors in the level or modifying non-Blueprint assets.\n"
    "4. To inspect a Blueprint: call LIST_NODES first to get all node GUIDs and titles. "
    "Then call GET_NODE_PINS with specific GUIDs to see pin names and directions.\n"
    "5. To wire nodes: call CONNECT_PINS with source_node_id, source_pin, target_node_id, target_pin. "
    "Pin names come from GET_NODE_PINS results.\n"
    "6. Node GUIDs are in LIST_NODES responses as: GUID~NodeTitle~Tooltip~NodeClass. Always use exact GUIDs.\n"
    "7. When you have all the information you need, act immediately — do not ask for confirmation unless the task is ambiguous.\n"
    "8. After completing a task, give a short summary of what was done."
);

// ── Tool schema helpers ───────────────────────────────────────────────────────

static TSharedPtr<FJsonObject> MakeTool(const FString& Name, const FString& Description,
    TArray<TPair<FString,FString>> Params)
{
    TSharedPtr<FJsonObject> Properties = MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> Required;

    for (auto& Pair : Params)
    {
        TSharedPtr<FJsonObject> Prop = MakeShareable(new FJsonObject);
        Prop->SetStringField(TEXT("type"),        TEXT("string"));
        Prop->SetStringField(TEXT("description"), Pair.Value);
        Properties->SetObjectField(Pair.Key, Prop);
        Required.Add(MakeShareable(new FJsonValueString(Pair.Key)));
    }

    TSharedPtr<FJsonObject> Parameters = MakeShareable(new FJsonObject);
    Parameters->SetStringField(TEXT("type"),       TEXT("object"));
    Parameters->SetObjectField(TEXT("properties"), Properties);
    Parameters->SetArrayField(TEXT("required"),    Required);

    TSharedPtr<FJsonObject> Function = MakeShareable(new FJsonObject);
    Function->SetStringField(TEXT("name"),        Name);
    Function->SetStringField(TEXT("description"), Description);
    Function->SetObjectField(TEXT("parameters"),  Parameters);

    TSharedPtr<FJsonObject> Tool = MakeShareable(new FJsonObject);
    Tool->SetStringField(TEXT("type"),     TEXT("function"));
    Tool->SetObjectField(TEXT("function"), Function);

    return Tool;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

FGraphBridgeLLMClient::FGraphBridgeLLMClient()
{
}

FGraphBridgeLLMClient::~FGraphBridgeLLMClient()
{
}

void FGraphBridgeLLMClient::RequestCancel()
{
    bCancelRequested = true;
    if (ActiveHttpRequest.IsValid())
    {
        ActiveHttpRequest->OnProcessRequestComplete().Unbind();
        ActiveHttpRequest->CancelRequest();
        ActiveHttpRequest.Reset();
    }
    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: cancel requested"));
}

bool FGraphBridgeLLMClient::IsAnthropicProvider() const
{
    const UGraphBridgeSettings* Settings = GetDefault<UGraphBridgeSettings>();
    if (!Settings) return false;
    return Settings->ApiEndpoint.Contains(TEXT("anthropic.com"))
        || Settings->SelectedModel.StartsWith(TEXT("claude-"));
}

// ── Public API ────────────────────────────────────────────────────────────────

void FGraphBridgeLLMClient::ResetConversation()
{
    ConversationHistory.Empty();
    CurrentIteration = 0;
    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: conversation reset"));
}

void FGraphBridgeLLMClient::SendMessage(const FString& UserMessage)
{
    bCancelRequested = false;
    CurrentIteration = 0;
    AppendMessage(TEXT("user"), UserMessage);
    PostToOpenAI();
}

// ── Tool schemas ──────────────────────────────────────────────────────────────

TArray<TSharedPtr<FJsonValue>> FGraphBridgeLLMClient::BuildToolSchemas() const
{
    TArray<TSharedPtr<FJsonValue>> Tools;

    auto Add = [&](TSharedPtr<FJsonObject> Tool)
    {
        Tools.Add(MakeShareable(new FJsonValueObject(Tool)));
    };

    Add(MakeTool(TEXT("OPEN_BLUEPRINT"),
        TEXT("Open a Blueprint asset for editing."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint, e.g. /Game/BP_MyActor")} }));

    Add(MakeTool(TEXT("CLOSE_BLUEPRINT"),
        TEXT("Close the currently open Blueprint editor."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")} }));

    Add(MakeTool(TEXT("SPAWN_NODE"),
        TEXT("Spawn a new node in the Blueprint graph."),
        {
            {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_class"), TEXT("The node class to spawn, e.g. K2Node_CallFunction")},
            {TEXT("comment"),    TEXT("Optional comment label for the node")},
            {TEXT("pos_x"),      TEXT("X position in the graph")},
            {TEXT("pos_y"),      TEXT("Y position in the graph")}
        }));

    Add(MakeTool(TEXT("CONNECT_PINS"),
        TEXT("Connect an output pin on one node to an input pin on another."),
        {
            {TEXT("asset_path"),     TEXT("Content-browser path to the Blueprint")},
            {TEXT("source_node_id"), TEXT("GUID of the source node")},
            {TEXT("source_pin"),     TEXT("Name of the output pin")},
            {TEXT("target_node_id"), TEXT("GUID of the target node")},
            {TEXT("target_pin"),     TEXT("Name of the input pin")}
        }));

    Add(MakeTool(TEXT("DISCONNECT_PINS"),
        TEXT("Disconnect a pin connection."),
        {
            {TEXT("asset_path"),     TEXT("Content-browser path to the Blueprint")},
            {TEXT("source_node_id"), TEXT("GUID of the source node")},
            {TEXT("source_pin"),     TEXT("Name of the output pin")},
            {TEXT("target_node_id"), TEXT("GUID of the target node")},
            {TEXT("target_pin"),     TEXT("Name of the input pin")}
        }));

    Add(MakeTool(TEXT("DELETE_NODE"),
        TEXT("Delete a node from the graph by its GUID."),
        {
            {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),    TEXT("GUID of the node to delete")}
        }));

    Add(MakeTool(TEXT("CLEAR_NODES"),
        TEXT("Delete nodes matching a comment tag in a Blueprint graph."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("comment_match"), TEXT("Comment tag to match for deletion")}
        }));

    Add(MakeTool(TEXT("SET_PIN_DEFAULT"),
        TEXT("Set the default value of a pin on a node."),
        {
            {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),    TEXT("GUID of the node")},
            {TEXT("pin_name"),   TEXT("Name of the pin")},
            {TEXT("value"),      TEXT("The default value to set")}
        }));

    Add(MakeTool(TEXT("GET_NODE_PINS"),
        TEXT("Get all pins on a node."),
        {
            {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),    TEXT("GUID or title of the node")}
        }));

    Add(MakeTool(TEXT("LIST_NODES"),
        TEXT("List all nodes in a Blueprint graph."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")} }));

    Add(MakeTool(TEXT("COMPILE"),
        TEXT("Compile a Blueprint."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")} }));

    Add(MakeTool(TEXT("SAVE_BLUEPRINT"),
        TEXT("Save a Blueprint."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")} }));

    Add(MakeTool(TEXT("SPAWN_VARIABLE"),
        TEXT("Spawn a Get or Set variable node for a Blueprint variable."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("variable_name"), TEXT("Name of the variable")},
            {TEXT("variable_type"), TEXT("Type string, e.g. bool, int32, FString, object:ACharacter")},
            {TEXT("category"),      TEXT("Variable category")}
        }));

    Add(MakeTool(TEXT("ADD_VARIABLE"),
        TEXT("Add a new variable to a Blueprint."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("variable_name"), TEXT("Name for the new variable")},
            {TEXT("variable_type"), TEXT("Type, e.g. bool, int32, float, FString, FVector")},
            {TEXT("category"),      TEXT("Optional category")}
        }));

    Add(MakeTool(TEXT("LIST_VARIABLES"),
        TEXT("List all variables in a Blueprint."),
        { {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")} }));

    Add(MakeTool(TEXT("SET_VARIABLE_DEFAULT"),
        TEXT("Set the default value of a Blueprint variable."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("variable_name"), TEXT("Name of the variable")},
            {TEXT("value"),         TEXT("Default value to set")}
        }));

    Add(MakeTool(TEXT("LIST_ASSETS"),
        TEXT("List assets in the content browser, optionally filtered by path or class."),
        { {TEXT("filter"), TEXT("Path or class name filter, e.g. /Game/Characters or Blueprint")} }));

    Add(MakeTool(TEXT("FIND_NODE_CLASS"),
        TEXT("Search for a node class by keyword."),
        { {TEXT("keyword"), TEXT("Search keyword")} }));

    Add(MakeTool(TEXT("SET_FUNCTION_REF"),
        TEXT("Set the function reference on a function call node."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),       TEXT("GUID of the node")},
            {TEXT("class_name"),    TEXT("Class that owns the function")},
            {TEXT("function_name"), TEXT("Name of the function")}
        }));

    Add(MakeTool(TEXT("SET_EVENT_REF"),
        TEXT("Set the event reference on an event node."),
        {
            {TEXT("asset_path"), TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),    TEXT("GUID of the node")},
            {TEXT("event_name"), TEXT("Name of the event, e.g. ReceiveBeginPlay")}
        }));

    Add(MakeTool(TEXT("SET_VARIABLE_REF"),
        TEXT("Set the variable reference on a variable node."),
        {
            {TEXT("asset_path"),    TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),       TEXT("GUID of the node")},
            {TEXT("variable_name"), TEXT("Name of the variable")}
        }));

    Add(MakeTool(TEXT("ADD_COMPONENT"),
        TEXT("Add a component to a Blueprint."),
        {
            {TEXT("asset_path"),      TEXT("Content-browser path to the Blueprint")},
            {TEXT("component_class"), TEXT("Class of the component, e.g. StaticMeshComponent")},
            {TEXT("component_name"),  TEXT("Name for the new component")}
        }));

    Add(MakeTool(TEXT("SET_ANIM_CLASS"),
        TEXT("Set the animation class on a skeletal mesh component in a Blueprint."),
        {
            {TEXT("asset_path"),     TEXT("Content-browser path to the Blueprint")},
            {TEXT("component_name"), TEXT("Name of the SkeletalMeshComponent")},
            {TEXT("anim_bp_path"),   TEXT("Content path to the AnimBlueprint")}
        }));

    Add(MakeTool(TEXT("SET_INPUT_ACTION"),
        TEXT("Set the input action on an input action node."),
        {
            {TEXT("asset_path"),        TEXT("Content-browser path to the Blueprint")},
            {TEXT("node_id"),           TEXT("GUID of the node")},
            {TEXT("input_action_path"), TEXT("Content path to the input action asset")}
        }));

    Add(MakeTool(TEXT("LIST_BLENDSPACES"),
        TEXT("List all BlendSpace assets in the project."),
        { {TEXT("filter"), TEXT("Optional path filter")} }));

    Add(MakeTool(TEXT("LIST_ASSET_PROPERTIES"),
        TEXT("List all editable properties on an asset."),
        { {TEXT("asset_path"), TEXT("Content path to the asset")} }));

    Add(MakeTool(TEXT("GET_ASSET_PROPERTY"),
        TEXT("Get the value of a property on an asset."),
        {
            {TEXT("asset_path"),    TEXT("Content path to the asset")},
            {TEXT("property_name"), TEXT("Name of the property")}
        }));

    Add(MakeTool(TEXT("SET_ASSET_PROPERTY"),
        TEXT("Set the value of a property on an asset."),
        {
            {TEXT("asset_path"),    TEXT("Content path to the asset")},
            {TEXT("property_name"), TEXT("Name of the property")},
            {TEXT("value"),         TEXT("Value to set")}
        }));

    Add(MakeTool(TEXT("SAVE_ASSET"),
        TEXT("Save an asset to disk."),
        { {TEXT("asset_path"), TEXT("Content path to the asset")} }));

    Add(MakeTool(TEXT("GET_MONTAGE_INFO"),
        TEXT("Get info about an Animation Montage."),
        { {TEXT("asset_path"), TEXT("Content path to the montage")} }));

    Add(MakeTool(TEXT("ADD_MONTAGE_SECTION"),
        TEXT("Add a section to an Animation Montage."),
        {
            {TEXT("asset_path"),   TEXT("Content path to the montage")},
            {TEXT("section_name"), TEXT("Name for the new section")},
            {TEXT("start_time"),   TEXT("Start time in seconds")}
        }));

    Add(MakeTool(TEXT("REMOVE_MONTAGE_SECTION"),
        TEXT("Remove a section from an Animation Montage."),
        {
            {TEXT("asset_path"),  TEXT("Content path to the montage")},
            {TEXT("section_name"), TEXT("Name of the section to remove")}
        }));

    Add(MakeTool(TEXT("SET_MONTAGE_SLOT"),
        TEXT("Set the slot on an Animation Montage track."),
        {
            {TEXT("asset_path"),  TEXT("Content path to the montage")},
            {TEXT("slot_index"),  TEXT("Index of the slot track")},
            {TEXT("slot_name"),   TEXT("New slot name, e.g. DefaultGroup.UpperBody")}
        }));

    Add(MakeTool(TEXT("ADD_MONTAGE_NOTIFY"),
        TEXT("Add a notify to an Animation Montage."),
        {
            {TEXT("asset_path"),   TEXT("Content path to the montage")},
            {TEXT("notify_class"), TEXT("Class of the notify")},
            {TEXT("trigger_time"), TEXT("Trigger time in seconds")}
        }));

    Add(MakeTool(TEXT("REMOVE_MONTAGE_NOTIFY"),
        TEXT("Remove a notify from an Animation Montage by index."),
        {
            {TEXT("asset_path"),   TEXT("Content path to the montage")},
            {TEXT("notify_index"), TEXT("Index of the notify to remove")}
        }));

    Add(MakeTool(TEXT("LIST_DATATABLE_ROWS"),
        TEXT("List all rows in a DataTable."),
        { {TEXT("asset_path"), TEXT("Content path to the DataTable")} }));

    Add(MakeTool(TEXT("ADD_DATATABLE_ROW"),
        TEXT("Add a row to a DataTable."),
        {
            {TEXT("asset_path"), TEXT("Content path to the DataTable")},
            {TEXT("row_name"),   TEXT("Name for the new row")}
        }));

    Add(MakeTool(TEXT("DELETE_DATATABLE_ROW"),
        TEXT("Delete a row from a DataTable."),
        {
            {TEXT("asset_path"), TEXT("Content path to the DataTable")},
            {TEXT("row_name"),   TEXT("Name of the row to delete")}
        }));

    Add(MakeTool(TEXT("RENAME_DATATABLE_ROW"),
        TEXT("Rename a row in a DataTable."),
        {
            {TEXT("asset_path"), TEXT("Content path to the DataTable")},
            {TEXT("old_name"),   TEXT("Current row name")},
            {TEXT("new_name"),   TEXT("New row name")}
        }));

    Add(MakeTool(TEXT("LIST_SKELETON_SOCKETS"),
        TEXT("List all sockets on a Skeleton."),
        { {TEXT("asset_path"), TEXT("Content path to the Skeleton")} }));

    Add(MakeTool(TEXT("ADD_SKELETON_SOCKET"),
        TEXT("Add a socket to a Skeleton."),
        {
            {TEXT("asset_path"),  TEXT("Content path to the Skeleton")},
            {TEXT("socket_name"), TEXT("Name for the new socket")},
            {TEXT("bone_name"),   TEXT("Parent bone name")}
        }));

    Add(MakeTool(TEXT("MOVE_SKELETON_SOCKET"),
        TEXT("Move a socket on a Skeleton."),
        {
            {TEXT("asset_path"),        TEXT("Content path to the Skeleton")},
            {TEXT("socket_name"),       TEXT("Name of the socket")},
            {TEXT("loc_x"),             TEXT("Relative location X (cm)")},
            {TEXT("loc_y"),             TEXT("Relative location Y (cm)")},
            {TEXT("loc_z"),             TEXT("Relative location Z (cm)")},
            {TEXT("rot_pitch"),         TEXT("Relative rotation pitch (degrees)")},
            {TEXT("rot_yaw"),           TEXT("Relative rotation yaw (degrees)")},
            {TEXT("rot_roll"),          TEXT("Relative rotation roll (degrees)")}
        }));

    Add(MakeTool(TEXT("DELETE_SKELETON_SOCKET"),
        TEXT("Delete a socket from a Skeleton."),
        {
            {TEXT("asset_path"),  TEXT("Content path to the Skeleton")},
            {TEXT("socket_name"), TEXT("Name of the socket to delete")}
        }));

    Add(MakeTool(TEXT("CREATE_IMC"),
        TEXT("Create a new Input Mapping Context asset."),
        { {TEXT("asset_path"), TEXT("Content path for the new IMC")} }));

    Add(MakeTool(TEXT("ADD_IMC_MAPPING"),
        TEXT("Add an input action mapping to an Input Mapping Context."),
        {
            {TEXT("imc_path"),    TEXT("Content path to the IMC")},
            {TEXT("action_path"), TEXT("Content path to the Input Action")},
            {TEXT("key"),         TEXT("Key name, e.g. W, SpaceBar, Gamepad_LeftX")}
        }));

    Add(MakeTool(TEXT("REMOVE_IMC_MAPPING"),
        TEXT("Remove an input action mapping from an Input Mapping Context."),
        {
            {TEXT("imc_path"),    TEXT("Content path to the IMC")},
            {TEXT("action_path"), TEXT("Content path to the Input Action")},
            {TEXT("key"),         TEXT("Key name to remove")}
        }));

    Add(MakeTool(TEXT("LIST_IMC_MAPPINGS"),
        TEXT("List all mappings in an Input Mapping Context."),
        { {TEXT("imc_path"), TEXT("Content path to the IMC")} }));

    Add(MakeTool(TEXT("ADD_IMC_TO_CHARACTER"),
        TEXT("Add an Input Mapping Context to a character Blueprint's BeginPlay."),
        {
            {TEXT("asset_path"), TEXT("Content path to the character Blueprint")},
            {TEXT("imc_path"),   TEXT("Content path to the IMC")},
            {TEXT("priority"),   TEXT("Priority (default 0)")}
        }));

    Add(MakeTool(TEXT("SET_CHARACTER_MESH"),
        TEXT("Set the skeletal mesh on a character Blueprint."),
        {
            {TEXT("asset_path"),     TEXT("Content path to the character Blueprint")},
            {TEXT("mesh_path"),      TEXT("Content path to the SkeletalMesh")},
            {TEXT("component_name"), TEXT("Component name (default CharacterMesh0)")}
        }));

    Add(MakeTool(TEXT("SET_CHARACTER_CAPSULE"),
        TEXT("Set the capsule size on a character Blueprint."),
        {
            {TEXT("asset_path"),   TEXT("Content path to the character Blueprint")},
            {TEXT("half_height"),  TEXT("Half height of the capsule in cm")},
            {TEXT("radius"),       TEXT("Radius of the capsule in cm")}
        }));

    Add(MakeTool(TEXT("SET_CAMERA_BOOM"),
        TEXT("Set the camera boom (spring arm) on a character Blueprint."),
        {
            {TEXT("asset_path"),        TEXT("Content path to the character Blueprint")},
            {TEXT("arm_length"),        TEXT("Target arm length in cm")},
            {TEXT("offset_x"),          TEXT("Socket offset X")},
            {TEXT("offset_y"),          TEXT("Socket offset Y")},
            {TEXT("offset_z"),          TEXT("Socket offset Z")}
        }));

    Add(MakeTool(TEXT("SET_GAMEMODE_PAWN"),
        TEXT("Set the default pawn class on a GameMode Blueprint."),
        {
            {TEXT("gamemode_path"), TEXT("Content path to the GameMode Blueprint")},
            {TEXT("pawn_path"),     TEXT("Content path to the Pawn Blueprint")}
        }));

    Add(MakeTool(TEXT("GET_CURRENT_GAMEMODE"),
        TEXT("Get the current GameMode for the active level."),
        {}));

    Add(MakeTool(TEXT("GET_PLAYER_START"),
        TEXT("Get the location of the Player Start actor in the current level."),
        {}));

    Add(MakeTool(TEXT("SET_LEVEL_GAMEMODE"),
        TEXT("Set the GameMode override for the current level."),
        { {TEXT("gamemode_path"), TEXT("Content path to the GameMode Blueprint")} }));

    Add(MakeTool(TEXT("SET_CAST_TARGET"),
        TEXT("Set the cast target class on a Cast node."),
        {
            {TEXT("asset_path"),  TEXT("Content path to the Blueprint")},
            {TEXT("node_id"),     TEXT("GUID of the Cast node")},
            {TEXT("class_name"),  TEXT("Target class name")}
        }));

    Add(MakeTool(TEXT("SET_VARIABLE_TYPE"),
        TEXT("Change the type of an existing Blueprint variable."),
        {
            {TEXT("asset_path"),    TEXT("Content path to the Blueprint")},
            {TEXT("variable_name"), TEXT("Name of the variable")},
            {TEXT("new_type"),      TEXT("New type, e.g. bool, int32, float, FString, FVector")}
        }));

    Add(MakeTool(TEXT("RUN_PYTHON"),
        TEXT("Execute a Python script string in the Unreal Editor Python environment."),
        { {TEXT("script"), TEXT("Python code to execute")} }));

    return Tools;
}

// ── HTTP ──────────────────────────────────────────────────────────────────────

void FGraphBridgeLLMClient::PostToOpenAI()
{
    if (IsAnthropicProvider())
    {
        PostToAnthropic();
        return;
    }

    if (bCancelRequested)
    {
        UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
        return;
    }

    OnLLMIteration.ExecuteIfBound(CurrentIteration);

    if (CurrentIteration >= MaxToolIterations)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("LLMClient: max tool iterations reached"));
        OnError.ExecuteIfBound(TEXT("Max tool call iterations reached. Please try a simpler request."));
        return;
    }

    const UGraphBridgeSettings* Settings = GetDefault<UGraphBridgeSettings>();
    if (!Settings || Settings->ApiKey.IsEmpty())
    {
        OnError.ExecuteIfBound(TEXT("No API key set. Please add your OpenAI API key in Project Settings → Plugins → GraphBridge AI."));
        return;
    }

    // Build messages array
    TArray<TSharedPtr<FJsonValue>> Messages;

    TSharedPtr<FJsonObject> SystemMsg = MakeShareable(new FJsonObject);
    SystemMsg->SetStringField(TEXT("role"),    TEXT("system"));
    SystemMsg->SetStringField(TEXT("content"), GSystemPrompt);
    Messages.Add(MakeShareable(new FJsonValueObject(SystemMsg)));

    for (auto& Msg : ConversationHistory)
        Messages.Add(MakeShareable(new FJsonValueObject(Msg)));

    // Build payload
    TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
    Payload->SetStringField(TEXT("model"),       Settings->SelectedModel.IsEmpty() ? TEXT("gpt-4o") : Settings->SelectedModel);
    Payload->SetNumberField(TEXT("max_tokens"),  2048);
    Payload->SetArrayField(TEXT("messages"),     Messages);
    Payload->SetArrayField(TEXT("tools"),        BuildToolSchemas());
    Payload->SetStringField(TEXT("tool_choice"), TEXT("auto"));

    FString PayloadString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    const FString LLMUrl = Settings->ApiEndpoint.IsEmpty()
        ? TEXT("https://api.openai.com/v1/chat/completions")
        : Settings->ApiEndpoint;
    Request->SetURL(LLMUrl);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"),  TEXT("application/json"));
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Settings->ApiKey);
    Request->SetContentAsString(PayloadString);
    Request->OnProcessRequestComplete().BindRaw(this, &FGraphBridgeLLMClient::OnHttpResponse);
    ActiveHttpRequest = Request;
    Request->ProcessRequest();

    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: POST to OpenAI (iteration %d)"), CurrentIteration);
}

void FGraphBridgeLLMClient::OnHttpResponse(
    FHttpRequestPtr /*Request*/,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    ActiveHttpRequest.Reset();
    if (bCancelRequested)
    {
        UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
        return;
    }

    if (!bWasSuccessful || !Response.IsValid())
    {
        UE_LOG(LogGraphBridge, Error, TEXT("LLMClient: HTTP request failed"));
        OnError.ExecuteIfBound(TEXT("Network error — could not reach OpenAI."));
        return;
    }

    const int32 Code = Response->GetResponseCode();
    if (Code != 200)
    {
        UE_LOG(LogGraphBridge, Error, TEXT("LLMClient: OpenAI returned HTTP %d: %s"),
            Code, *Response->GetContentAsString());
        OnError.ExecuteIfBound(FString::Printf(
            TEXT("OpenAI error %d. Check your API key and model name."), Code));
        return;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OnError.ExecuteIfBound(TEXT("Failed to parse OpenAI response."));
        return;
    }

    const TArray<TSharedPtr<FJsonValue>>* Choices;
    if (!Root->TryGetArrayField(TEXT("choices"), Choices) || Choices->Num() == 0)
    {
        OnError.ExecuteIfBound(TEXT("OpenAI response contained no choices."));
        return;
    }

    TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
    FString FinishReason;
    Choice->TryGetStringField(TEXT("finish_reason"), FinishReason);

    TSharedPtr<FJsonObject> AssistantMessage = Choice->GetObjectField(TEXT("message"));
    ConversationHistory.Add(AssistantMessage);

    // ── Tool calls ────────────────────────────────────────────────────────────
    if (FinishReason == TEXT("tool_calls"))
    {
        const TArray<TSharedPtr<FJsonValue>>* ToolCalls;
        if (!AssistantMessage->TryGetArrayField(TEXT("tool_calls"), ToolCalls))
        {
            OnError.ExecuteIfBound(TEXT("Model returned tool_calls finish reason but no tool_calls array."));
            return;
        }

        CurrentIteration++;

        for (auto& ToolCallVal : *ToolCalls)
        {
            TSharedPtr<FJsonObject> ToolCall   = ToolCallVal->AsObject();
            FString                 ToolCallId = ToolCall->GetStringField(TEXT("id"));
            TSharedPtr<FJsonObject> Func       = ToolCall->GetObjectField(TEXT("function"));
            FString                 FuncName   = Func->GetStringField(TEXT("name"));
            FString                 ArgsString = Func->GetStringField(TEXT("arguments"));

            TSharedPtr<FJsonObject> Args;
            TSharedRef<TJsonReader<>> ArgReader = TJsonReaderFactory<>::Create(ArgsString);
            FJsonSerializer::Deserialize(ArgReader, Args);

            UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: tool call → %s"), *FuncName);

            FString Result = DispatchToolCall(FuncName, Args);
            AppendToolResult(ToolCallId, Result);
        }

        if (bCancelRequested)
        {
            UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
            return;
        }
        PostToOpenAI();
        return;
    }

    // ── Final text response ───────────────────────────────────────────────────
    FString Content;
    AssistantMessage->TryGetStringField(TEXT("content"), Content);

    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: final response received"));
    OnResponse.ExecuteIfBound(Content);
}

void FGraphBridgeLLMClient::PostToAnthropic()
{
    if (bCancelRequested)
    {
        UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
        return;
    }

    OnLLMIteration.ExecuteIfBound(CurrentIteration);

    if (CurrentIteration >= MaxToolIterations)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("LLMClient: max tool iterations reached"));
        OnError.ExecuteIfBound(TEXT("Max tool call iterations reached."));
        return;
    }

    const UGraphBridgeSettings* Settings = GetDefault<UGraphBridgeSettings>();
    if (!Settings || Settings->ApiKey.IsEmpty())
    {
        OnError.ExecuteIfBound(TEXT("No API key. Add your Anthropic API key in Editor Preferences → Plugins → GraphBridge AI."));
        return;
    }

    // Build messages array
    TArray<TSharedPtr<FJsonValue>> Messages;
    for (auto& Msg : ConversationHistory)
    {
        Messages.Add(MakeShareable(new FJsonValueObject(Msg)));
    }

    // Convert OpenAI tool schemas to Anthropic format (input_schema instead of parameters)
    TArray<TSharedPtr<FJsonValue>> AnthropicTools;
    for (auto& ToolVal : BuildToolSchemas())
    {
        TSharedPtr<FJsonObject> OAITool = ToolVal->AsObject();
        TSharedPtr<FJsonObject> Func = OAITool->GetObjectField(TEXT("function"));
        TSharedPtr<FJsonObject> ATool = MakeShareable(new FJsonObject);
        ATool->SetStringField(TEXT("name"),        Func->GetStringField(TEXT("name")));
        ATool->SetStringField(TEXT("description"), Func->GetStringField(TEXT("description")));
        ATool->SetObjectField(TEXT("input_schema"), Func->GetObjectField(TEXT("parameters")));
        AnthropicTools.Add(MakeShareable(new FJsonValueObject(ATool)));
    }

    TSharedPtr<FJsonObject> Payload = MakeShareable(new FJsonObject);
    Payload->SetStringField(TEXT("model"),      Settings->SelectedModel.IsEmpty() ? TEXT("claude-sonnet-4-6") : Settings->SelectedModel);
    Payload->SetNumberField(TEXT("max_tokens"), 4096);
    Payload->SetStringField(TEXT("system"),     GSystemPrompt);
    Payload->SetArrayField(TEXT("messages"),    Messages);
    Payload->SetArrayField(TEXT("tools"),       AnthropicTools);

    FString PayloadString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadString);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

    FString Endpoint = Settings->ApiEndpoint.IsEmpty()
        ? TEXT("https://api.anthropic.com/v1/messages")
        : Settings->ApiEndpoint;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Content-Type"),      TEXT("application/json"));
    Request->SetHeader(TEXT("x-api-key"),         Settings->ApiKey);
    Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
    Request->SetContentAsString(PayloadString);
    Request->OnProcessRequestComplete().BindRaw(this, &FGraphBridgeLLMClient::OnAnthropicHttpResponse);
    ActiveHttpRequest = Request;
    Request->ProcessRequest();

    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: POST to Anthropic (iteration %d)"), CurrentIteration);
}

void FGraphBridgeLLMClient::OnAnthropicHttpResponse(
    FHttpRequestPtr /*Request*/,
    FHttpResponsePtr Response,
    bool bWasSuccessful)
{
    ActiveHttpRequest.Reset();
    if (bCancelRequested)
    {
        UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
        return;
    }

    if (!bWasSuccessful || !Response.IsValid())
    {
        OnError.ExecuteIfBound(TEXT("Network error — could not reach Anthropic."));
        return;
    }

    const int32 Code = Response->GetResponseCode();
    if (Code != 200)
    {
        UE_LOG(LogGraphBridge, Error, TEXT("LLMClient: Anthropic returned HTTP %d: %s"), Code, *Response->GetContentAsString());
        OnError.ExecuteIfBound(FString::Printf(TEXT("Anthropic error %d. Check your API key and model name."), Code));
        return;
    }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OnError.ExecuteIfBound(TEXT("Failed to parse Anthropic response."));
        return;
    }

    FString StopReason;
    Root->TryGetStringField(TEXT("stop_reason"), StopReason);

    const TArray<TSharedPtr<FJsonValue>>* ContentBlocks;
    if (!Root->TryGetArrayField(TEXT("content"), ContentBlocks) || ContentBlocks->Num() == 0)
    {
        OnError.ExecuteIfBound(TEXT("Anthropic response contained no content."));
        return;
    }

    // Add assistant message with full content array (required for tool_result turns)
    TSharedPtr<FJsonObject> AssistantMsg = MakeShareable(new FJsonObject);
    AssistantMsg->SetStringField(TEXT("role"), TEXT("assistant"));
    AssistantMsg->SetArrayField(TEXT("content"), *ContentBlocks);
    ConversationHistory.Add(AssistantMsg);

    if (StopReason == TEXT("tool_use"))
    {
        CurrentIteration++;
        TArray<TSharedPtr<FJsonValue>> ToolResults;

        for (auto& BlockVal : *ContentBlocks)
        {
            TSharedPtr<FJsonObject> Block = BlockVal->AsObject();
            FString BlockType;
            Block->TryGetStringField(TEXT("type"), BlockType);
            if (BlockType != TEXT("tool_use")) continue;

            FString ToolId   = Block->GetStringField(TEXT("id"));
            FString FuncName = Block->GetStringField(TEXT("name"));
            TSharedPtr<FJsonObject> Input = Block->GetObjectField(TEXT("input"));

            UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: tool call %s"), *FuncName);
            FString Result = DispatchToolCall(FuncName, Input);

            TSharedPtr<FJsonObject> ToolResult = MakeShareable(new FJsonObject);
            ToolResult->SetStringField(TEXT("type"),        TEXT("tool_result"));
            ToolResult->SetStringField(TEXT("tool_use_id"), ToolId);
            ToolResult->SetStringField(TEXT("content"),     Result);
            ToolResults.Add(MakeShareable(new FJsonValueObject(ToolResult)));
        }

        TSharedPtr<FJsonObject> UserMsg = MakeShareable(new FJsonObject);
        UserMsg->SetStringField(TEXT("role"),   TEXT("user"));
        UserMsg->SetArrayField(TEXT("content"), ToolResults);
        ConversationHistory.Add(UserMsg);

        if (bCancelRequested)
        {
            UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: request cancelled"));
            return;
        }
        PostToAnthropic();
        return;
    }

    // Final text response
    FString FinalText;
    for (auto& BlockVal : *ContentBlocks)
    {
        TSharedPtr<FJsonObject> Block = BlockVal->AsObject();
        FString BlockType;
        Block->TryGetStringField(TEXT("type"), BlockType);
        if (BlockType == TEXT("text"))
        {
            FString Text;
            Block->TryGetStringField(TEXT("text"), Text);
            FinalText += Text;
        }
    }

    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: Anthropic final response received"));
    OnResponse.ExecuteIfBound(FinalText);
}

// ── Tool dispatch ─────────────────────────────────────────────────────────────

FString FGraphBridgeLLMClient::DispatchToolCall(const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
{
    auto GetArg = [&](const FString& Key) -> FString
    {
        FString Val;
        if (Args.IsValid()) Args->TryGetStringField(Key, Val);
        return Val;
    };

    FString Command;

    if      (ToolName == TEXT("OPEN_BLUEPRINT"))        Command = FString::Printf(TEXT("OPEN_BLUEPRINT|%s"),              *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("CLOSE_BLUEPRINT"))       Command = FString::Printf(TEXT("CLOSE_BLUEPRINT|%s"),             *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("SPAWN_NODE"))            Command = FString::Printf(TEXT("SPAWN_NODE|%s|%s|%s|%s|%s"),      *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_class")), *GetArg(TEXT("comment")), *GetArg(TEXT("pos_x")), *GetArg(TEXT("pos_y")));
    else if (ToolName == TEXT("CONNECT_PINS"))          Command = FString::Printf(TEXT("CONNECT_PINS|%s|%s|%s|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("source_node_id")), *GetArg(TEXT("source_pin")), *GetArg(TEXT("target_node_id")), *GetArg(TEXT("target_pin")));
    else if (ToolName == TEXT("DISCONNECT_PINS"))       Command = FString::Printf(TEXT("DISCONNECT_PINS|%s|%s|%s|%s|%s"), *GetArg(TEXT("asset_path")), *GetArg(TEXT("source_node_id")), *GetArg(TEXT("source_pin")), *GetArg(TEXT("target_node_id")), *GetArg(TEXT("target_pin")));
    else if (ToolName == TEXT("DELETE_NODE"))           Command = FString::Printf(TEXT("DELETE_NODE|%s|%s"),               *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")));
    else if (ToolName == TEXT("CLEAR_NODES"))           Command = FString::Printf(TEXT("CLEAR_NODES|%s|%s"),               *GetArg(TEXT("asset_path")), *GetArg(TEXT("comment_match")));
    else if (ToolName == TEXT("SET_PIN_DEFAULT"))       Command = FString::Printf(TEXT("SET_PIN_DEFAULT|%s|%s|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("pin_name")), *GetArg(TEXT("value")));
    else if (ToolName == TEXT("GET_NODE_PINS"))         Command = FString::Printf(TEXT("GET_NODE_PINS|%s|%s"),             *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")));
    else if (ToolName == TEXT("LIST_NODES"))            Command = FString::Printf(TEXT("LIST_NODES|%s"),                   *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("COMPILE"))               Command = FString::Printf(TEXT("COMPILE|%s"),                      *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("SAVE_BLUEPRINT"))        Command = FString::Printf(TEXT("SAVE_BLUEPRINT|%s"),               *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("SPAWN_VARIABLE"))        Command = FString::Printf(TEXT("SPAWN_VARIABLE|%s|%s|%s|%s"),     *GetArg(TEXT("asset_path")), *GetArg(TEXT("variable_name")), *GetArg(TEXT("variable_type")), *GetArg(TEXT("category")));
    else if (ToolName == TEXT("ADD_VARIABLE"))          Command = FString::Printf(TEXT("ADD_VARIABLE|%s|%s|%s|%s"),       *GetArg(TEXT("asset_path")), *GetArg(TEXT("variable_name")), *GetArg(TEXT("variable_type")), *GetArg(TEXT("category")));
    else if (ToolName == TEXT("LIST_VARIABLES"))        Command = FString::Printf(TEXT("LIST_VARIABLES|%s"),               *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("SET_VARIABLE_DEFAULT"))  Command = FString::Printf(TEXT("SET_VARIABLE_DEFAULT|%s|%s|%s"),   *GetArg(TEXT("asset_path")), *GetArg(TEXT("variable_name")), *GetArg(TEXT("value")));
    else if (ToolName == TEXT("LIST_ASSETS"))           Command = FString::Printf(TEXT("LIST_ASSETS|%s"),                  *GetArg(TEXT("filter")));
    else if (ToolName == TEXT("FIND_NODE_CLASS"))       Command = FString::Printf(TEXT("FIND_NODE_CLASS|%s"),              *GetArg(TEXT("keyword")));
    else if (ToolName == TEXT("SET_FUNCTION_REF"))      Command = FString::Printf(TEXT("SET_FUNCTION_REF|%s|%s|%s|%s"),   *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("class_name")), *GetArg(TEXT("function_name")));
    else if (ToolName == TEXT("SET_EVENT_REF"))         Command = FString::Printf(TEXT("SET_EVENT_REF|%s|%s|%s"),          *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("event_name")));
    else if (ToolName == TEXT("SET_VARIABLE_REF"))      Command = FString::Printf(TEXT("SET_VARIABLE_REF|%s|%s|%s"),       *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("variable_name")));
    else if (ToolName == TEXT("ADD_COMPONENT"))         Command = FString::Printf(TEXT("ADD_COMPONENT|%s|%s|%s"),           *GetArg(TEXT("asset_path")), *GetArg(TEXT("component_class")), *GetArg(TEXT("component_name")));
    else if (ToolName == TEXT("SET_ANIM_CLASS"))        Command = FString::Printf(TEXT("SET_ANIM_CLASS|%s|%s|%s"),          *GetArg(TEXT("asset_path")), *GetArg(TEXT("component_name")), *GetArg(TEXT("anim_bp_path")));
    else if (ToolName == TEXT("SET_INPUT_ACTION"))      Command = FString::Printf(TEXT("SET_INPUT_ACTION|%s|%s|%s"),        *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("input_action_path")));
    else if (ToolName == TEXT("LIST_BLENDSPACES"))      Command = FString::Printf(TEXT("LIST_BLENDSPACES|%s"),              *GetArg(TEXT("filter")));
    else if (ToolName == TEXT("LIST_ASSET_PROPERTIES")) Command = FString::Printf(TEXT("LIST_ASSET_PROPERTIES|%s"),         *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("GET_ASSET_PROPERTY"))    Command = FString::Printf(TEXT("GET_ASSET_PROPERTY|%s|%s"),         *GetArg(TEXT("asset_path")), *GetArg(TEXT("property_name")));
    else if (ToolName == TEXT("SET_ASSET_PROPERTY"))    Command = FString::Printf(TEXT("SET_ASSET_PROPERTY|%s|%s|%s"),      *GetArg(TEXT("asset_path")), *GetArg(TEXT("property_name")), *GetArg(TEXT("value")));
    else if (ToolName == TEXT("SAVE_ASSET"))            Command = FString::Printf(TEXT("SAVE_ASSET|%s"),                    *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("GET_MONTAGE_INFO"))      Command = FString::Printf(TEXT("GET_MONTAGE_INFO|%s"),              *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("ADD_MONTAGE_SECTION"))   Command = FString::Printf(TEXT("ADD_MONTAGE_SECTION|%s|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("section_name")), *GetArg(TEXT("start_time")));
    else if (ToolName == TEXT("REMOVE_MONTAGE_SECTION"))Command = FString::Printf(TEXT("REMOVE_MONTAGE_SECTION|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("section_name")));
    else if (ToolName == TEXT("SET_MONTAGE_SLOT"))      Command = FString::Printf(TEXT("SET_MONTAGE_SLOT|%s|%s|%s"),       *GetArg(TEXT("asset_path")), *GetArg(TEXT("slot_index")), *GetArg(TEXT("slot_name")));
    else if (ToolName == TEXT("ADD_MONTAGE_NOTIFY"))    Command = FString::Printf(TEXT("ADD_MONTAGE_NOTIFY|%s|%s|%s"),     *GetArg(TEXT("asset_path")), *GetArg(TEXT("notify_class")), *GetArg(TEXT("trigger_time")));
    else if (ToolName == TEXT("REMOVE_MONTAGE_NOTIFY")) Command = FString::Printf(TEXT("REMOVE_MONTAGE_NOTIFY|%s|%s"),     *GetArg(TEXT("asset_path")), *GetArg(TEXT("notify_index")));
    else if (ToolName == TEXT("LIST_DATATABLE_ROWS"))   Command = FString::Printf(TEXT("LIST_DATATABLE_ROWS|%s"),           *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("ADD_DATATABLE_ROW"))     Command = FString::Printf(TEXT("ADD_DATATABLE_ROW|%s|%s"),          *GetArg(TEXT("asset_path")), *GetArg(TEXT("row_name")));
    else if (ToolName == TEXT("DELETE_DATATABLE_ROW"))  Command = FString::Printf(TEXT("DELETE_DATATABLE_ROW|%s|%s"),       *GetArg(TEXT("asset_path")), *GetArg(TEXT("row_name")));
    else if (ToolName == TEXT("RENAME_DATATABLE_ROW"))  Command = FString::Printf(TEXT("RENAME_DATATABLE_ROW|%s|%s|%s"),   *GetArg(TEXT("asset_path")), *GetArg(TEXT("old_name")), *GetArg(TEXT("new_name")));
    else if (ToolName == TEXT("LIST_SKELETON_SOCKETS")) Command = FString::Printf(TEXT("LIST_SKELETON_SOCKETS|%s"),         *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("ADD_SKELETON_SOCKET"))   Command = FString::Printf(TEXT("ADD_SKELETON_SOCKET|%s|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("socket_name")), *GetArg(TEXT("bone_name")));
    else if (ToolName == TEXT("MOVE_SKELETON_SOCKET"))  Command = FString::Printf(TEXT("MOVE_SKELETON_SOCKET|%s|%s|%s|%s|%s|%s|%s|%s"), *GetArg(TEXT("asset_path")), *GetArg(TEXT("socket_name")), *GetArg(TEXT("loc_x")), *GetArg(TEXT("loc_y")), *GetArg(TEXT("loc_z")), *GetArg(TEXT("rot_pitch")), *GetArg(TEXT("rot_yaw")), *GetArg(TEXT("rot_roll")));
    else if (ToolName == TEXT("DELETE_SKELETON_SOCKET"))Command = FString::Printf(TEXT("DELETE_SKELETON_SOCKET|%s|%s"),     *GetArg(TEXT("asset_path")), *GetArg(TEXT("socket_name")));
    else if (ToolName == TEXT("CREATE_IMC"))            Command = FString::Printf(TEXT("CREATE_IMC|%s"),                    *GetArg(TEXT("asset_path")));
    else if (ToolName == TEXT("ADD_IMC_MAPPING"))       Command = FString::Printf(TEXT("ADD_IMC_MAPPING|%s|%s|%s"),         *GetArg(TEXT("imc_path")), *GetArg(TEXT("action_path")), *GetArg(TEXT("key")));
    else if (ToolName == TEXT("REMOVE_IMC_MAPPING"))    Command = FString::Printf(TEXT("REMOVE_IMC_MAPPING|%s|%s|%s"),      *GetArg(TEXT("imc_path")), *GetArg(TEXT("action_path")), *GetArg(TEXT("key")));
    else if (ToolName == TEXT("LIST_IMC_MAPPINGS"))     Command = FString::Printf(TEXT("LIST_IMC_MAPPINGS|%s"),              *GetArg(TEXT("imc_path")));
    else if (ToolName == TEXT("ADD_IMC_TO_CHARACTER"))  Command = FString::Printf(TEXT("ADD_IMC_TO_CHARACTER|%s|%s|%s"),    *GetArg(TEXT("asset_path")), *GetArg(TEXT("imc_path")), *GetArg(TEXT("priority")));
    else if (ToolName == TEXT("SET_CHARACTER_MESH"))    Command = FString::Printf(TEXT("SET_CHARACTER_MESH|%s|%s|%s"),       *GetArg(TEXT("asset_path")), *GetArg(TEXT("mesh_path")), *GetArg(TEXT("component_name")));
    else if (ToolName == TEXT("SET_CHARACTER_CAPSULE")) Command = FString::Printf(TEXT("SET_CHARACTER_CAPSULE|%s|%s|%s"),   *GetArg(TEXT("asset_path")), *GetArg(TEXT("half_height")), *GetArg(TEXT("radius")));
    else if (ToolName == TEXT("SET_CAMERA_BOOM"))       Command = FString::Printf(TEXT("SET_CAMERA_BOOM|%s|%s|%s|%s|%s"),   *GetArg(TEXT("asset_path")), *GetArg(TEXT("arm_length")), *GetArg(TEXT("offset_x")), *GetArg(TEXT("offset_y")), *GetArg(TEXT("offset_z")));
    else if (ToolName == TEXT("SET_GAMEMODE_PAWN"))     Command = FString::Printf(TEXT("SET_GAMEMODE_PAWN|%s|%s"),           *GetArg(TEXT("gamemode_path")), *GetArg(TEXT("pawn_path")));
    else if (ToolName == TEXT("GET_CURRENT_GAMEMODE"))  Command = TEXT("GET_CURRENT_GAMEMODE");
    else if (ToolName == TEXT("GET_PLAYER_START"))      Command = TEXT("GET_PLAYER_START");
    else if (ToolName == TEXT("SET_LEVEL_GAMEMODE"))    Command = FString::Printf(TEXT("SET_LEVEL_GAMEMODE|%s"),             *GetArg(TEXT("gamemode_path")));
    else if (ToolName == TEXT("SET_CAST_TARGET"))       Command = FString::Printf(TEXT("SET_CAST_TARGET|%s|%s|%s"),          *GetArg(TEXT("asset_path")), *GetArg(TEXT("node_id")), *GetArg(TEXT("class_name")));
    else if (ToolName == TEXT("SET_VARIABLE_TYPE"))     Command = FString::Printf(TEXT("SET_VARIABLE_TYPE|%s|%s|%s"),        *GetArg(TEXT("asset_path")), *GetArg(TEXT("variable_name")), *GetArg(TEXT("new_type")));
    else if (ToolName == TEXT("RUN_PYTHON"))            Command = FString::Printf(TEXT("RUN_PYTHON|%s"),                     *GetArg(TEXT("script")));
    else
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("LLMClient: unknown tool '%s'"), *ToolName);
        return FString::Printf(TEXT("ERR: Unknown tool '%s'"), *ToolName);
    }

    FGraphBridgev2Module& Module = FModuleManager::GetModuleChecked<FGraphBridgev2Module>(TEXT("GraphBridgev2"));
    FString Result = Module.HandleGraphCommand(Command);

    UE_LOG(LogGraphBridge, Log, TEXT("LLMClient: %s → %s"), *ToolName,
        Result.IsEmpty() ? TEXT("(empty)") : *Result);

    return Result.IsEmpty() ? TEXT("OK") : Result;
}

// ── History helpers ───────────────────────────────────────────────────────────

void FGraphBridgeLLMClient::AppendMessage(const FString& Role, const FString& Content)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("role"),    Role);
    Msg->SetStringField(TEXT("content"), Content);
    ConversationHistory.Add(Msg);
}

void FGraphBridgeLLMClient::AppendToolResult(const FString& ToolCallId, const FString& Result)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("role"),         TEXT("tool"));
    Msg->SetStringField(TEXT("tool_call_id"), ToolCallId);
    Msg->SetStringField(TEXT("content"),      Result);
    ConversationHistory.Add(Msg);
}
