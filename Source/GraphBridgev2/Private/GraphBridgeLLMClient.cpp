// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeLLMClient.h"
#include "GraphBridgeSettings.h"
#include "GraphBridgeCredentialStore.h"
#include "GraphBridgev2.h"
#include "GraphBridgeToolManifest.h"
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
    "8. After completing a task, give a short summary of what was done.\n"
    "9. A Blueprint can have more than just the default EventGraph — Function graphs and Macro graphs too. "
    "Call LIST_GRAPHS before spawning nodes into anything other than the default EventGraph, to see what graphs "
    "already exist and their type (EventGraph/Function/Macro). Use CREATE_FUNCTION_GRAPH or CREATE_MACRO_GRAPH "
    "to create a new one first if needed.\n"
    "10. SPAWN_NODE, CONNECT_PINS, DISCONNECT_PINS, DELETE_NODE, GET_NODE_PINS, and LIST_NODES all accept an "
    "optional graph_name argument. Pass it explicitly whenever you are operating on a Function or Macro graph — "
    "never assume the default EventGraph once other graphs exist."
);

// ── Tool schema helpers ───────────────────────────────────────────────────────

// OptionalParams: added to "properties" the same as Params, but NOT added to
// "required" — lets a tool call omit them (e.g. graph_name, defaulting to
// the main EventGraph on the C++ side) without the LLM provider rejecting
// the call for missing a required field.
static TSharedPtr<FJsonObject> MakeTool(const FString& Name, const FString& Description,
    TArray<TPair<FString,FString>> Params,
    TArray<TPair<FString,FString>> OptionalParams = {})
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

    for (auto& Pair : OptionalParams)
    {
        TSharedPtr<FJsonObject> Prop = MakeShareable(new FJsonObject);
        Prop->SetStringField(TEXT("type"),        TEXT("string"));
        Prop->SetStringField(TEXT("description"), Pair.Value);
        Properties->SetObjectField(Pair.Key, Prop);
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

    const TArray<FGraphBridgeToolDef>& Manifest = FGraphBridgeToolManifest::GetManifest();

    for (const FGraphBridgeToolDef& ToolDef : Manifest)
    {
        if (!ToolDef.bPublic) continue;

        TArray<TPair<FString, FString>> Params;
        for (const FGraphBridgeToolParam& Param : ToolDef.Parameters)
        {
            Params.Add({Param.Name, Param.Description});
        }

        TArray<TPair<FString, FString>> OptParams;
        for (const FGraphBridgeToolParam& Param : ToolDef.OptionalParameters)
        {
            OptParams.Add({Param.Name, Param.Description});
        }

        TSharedPtr<FJsonObject> Tool = MakeTool(
            *ToolDef.Command,
            *ToolDef.Description,
            Params,
            OptParams.Num() > 0 ? OptParams : TArray<TPair<FString,FString>>());

        Add(Tool);
    }

    // DEPRECATED: Original hardcoded tools removed (see git history for reference).
    // All tools now generated from FGraphBridgeToolManifest.
    // This ensures 83 public tools are always in sync across router/MCP/panel.

    // All tools generated from manifest (see above)
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
    const FString ApiKey = FGraphBridgeCredentialStore::GetApiKey();
    if (!Settings || ApiKey.IsEmpty())
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
    Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
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
    const FString ApiKey = FGraphBridgeCredentialStore::GetApiKey();
    if (!Settings || ApiKey.IsEmpty())
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
    Request->SetHeader(TEXT("x-api-key"),         ApiKey);
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

    // Build command from manifest (zero hardcoded tool names)
    const FGraphBridgeToolDef* ToolDef = FGraphBridgeToolManifest::FindTool(ToolName);
    if (!ToolDef)
    {
        UE_LOG(LogGraphBridge, Warning, TEXT("LLMClient: unknown tool '%s'"), *ToolName);
        return FString::Printf(TEXT("ERR: Unknown tool '%s'"), *ToolName);
    }

    // Build pipe-delimited command string from parameters
    // Format: COMMAND_NAME|param1|param2|param3|...
    FString Command = ToolDef->Command;

    // Add required parameters in order
    for (const FGraphBridgeToolParam& Param : ToolDef->Parameters)
    {
        Command += FString::Printf(TEXT("|%s"), *GetArg(Param.Name));
    }

    // Add optional parameters only if provided
    for (const FGraphBridgeToolParam& Param : ToolDef->OptionalParameters)
    {
        FString ArgVal = GetArg(Param.Name);
        if (!ArgVal.IsEmpty())
        {
            Command += FString::Printf(TEXT("|%s"), *ArgVal);
        }
    }

    FGraphBridgev2Module& Module = FModuleManager::GetModuleChecked<FGraphBridgev2Module>(TEXT("GraphBridgev2"));
    FString Result = Module.HandleGraphCommand(Command);

    // Verbose + truncated: a tool call result can be a RUN_PYTHON command's
    // full captured stdout -- same disclosure class as the command string
    // itself (see GraphBridgeTruncateForLog's comment in GraphBridgev2.h).
    UE_LOG(LogGraphBridge, Verbose, TEXT("LLMClient: %s → %s"), *ToolName,
        Result.IsEmpty() ? TEXT("(empty)") : *GraphBridgeTruncateForLog(Result));

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
