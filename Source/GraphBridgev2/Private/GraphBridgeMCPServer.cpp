// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeMCPServer.h"
#include "GraphBridgeAutomationLibrary.h"
#include "GraphBridgev2.h"
#include "GraphBridgeToolManifest.h"

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
        static TArray<FToolSpec> Specs;
        static bool bInitialized = false;

        if (!bInitialized)
        {
            bInitialized = true;
            const TArray<FGraphBridgeToolDef>& Manifest = FGraphBridgeToolManifest::GetManifest();

            // Build FToolSpec array from manifest (public tools only)
            for (const FGraphBridgeToolDef& Tool : Manifest)
            {
                if (!Tool.bPublic) continue;

                FToolSpec Spec;
                Spec.Command = *Tool.Command;
                Spec.Description = *Tool.Description;

                // Convert required + optional parameters
                for (const FGraphBridgeToolParam& Param : Tool.Parameters)
                {
                    Spec.Args.Add({*Param.Name, *Param.Type_Legacy, true});
                }
                for (const FGraphBridgeToolParam& Param : Tool.OptionalParameters)
                {
                    Spec.Args.Add({*Param.Name, *Param.Type_Legacy, false});
                }

                Specs.Add(Spec);
            }
        }

        return Specs;
    }

    // Generated from manifest: 83 public tools
    // All tool specs now built dynamically from FGraphBridgeToolManifest.

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

    auto MakeToolResult = [](const FString& Message, const FString& Payload, bool bIsError) -> TSharedPtr<FJsonObject>
    {
        TArray<TSharedPtr<FJsonValue>> ContentBlocks;

        // Add message text block if present
        if (!Message.IsEmpty())
        {
            TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
            TextBlock->SetStringField(TEXT("type"), TEXT("text"));
            TextBlock->SetStringField(TEXT("text"), Message);
            ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
        }

        // Detect and validate image payloads (PNG magic bytes in base64: "iVBORw0K")
        bool bIsImagePayload = false;
        TArray<FString> ImageBase64Array;
        int64 TotalImageSize = 0;
        const int64 MaxPayloadBytes = 1024 * 1024; // 1MB limit for MCP transport

        if (!Payload.IsEmpty() && !bIsError)
        {
            FString TrimmedPayload = Payload.TrimStartAndEnd();

            if (TrimmedPayload.StartsWith(TEXT("[")))
            {
                // Parse JSON array of base64 images (for multi-angle captures)
                TSharedRef<TJsonReader<TCHAR>> ArrayReader = TJsonReaderFactory<TCHAR>::Create(TrimmedPayload);
                TSharedPtr<FJsonValue> RootValue;
                if (FJsonSerializer::Deserialize(ArrayReader, RootValue) && RootValue.IsValid() && RootValue->Type == EJson::Array)
                {
                    const TArray<TSharedPtr<FJsonValue>>& Array = RootValue->AsArray();
                    for (const TSharedPtr<FJsonValue>& Item : Array)
                    {
                        if (Item.IsValid() && Item->Type == EJson::String)
                        {
                            FString Base64Str = Item->AsString();
                            if (Base64Str.StartsWith(TEXT("iVBORw0K")))
                            {
                                ImageBase64Array.Add(Base64Str);
                                TotalImageSize += Base64Str.Len();
                            }
                        }
                    }
                    bIsImagePayload = ImageBase64Array.Num() > 0;

                    // Size guard: MCP responses capped at ~1MB
                    if (bIsImagePayload && TotalImageSize > MaxPayloadBytes)
                    {
                        bIsImagePayload = false;
                        ImageBase64Array.Empty();
                        // Error will be added as text block below
                    }
                }
            }
            else if (TrimmedPayload.StartsWith(TEXT("iVBORw0K")))
            {
                // Single PNG image (PNG magic header in base64: 89 50 4E 47)
                TotalImageSize = TrimmedPayload.Len();
                if (TotalImageSize <= MaxPayloadBytes)
                {
                    ImageBase64Array.Add(TrimmedPayload);
                    bIsImagePayload = true;
                }
            }
        }

        // Add image blocks if payload contains validated images
        if (bIsImagePayload && ImageBase64Array.Num() > 0)
        {
            for (const FString& Base64Image : ImageBase64Array)
            {
                TSharedRef<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
                ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
                ImageBlock->SetStringField(TEXT("data"), Base64Image);
                ImageBlock->SetStringField(TEXT("mimeType"), TEXT("image/png"));
                ContentBlocks.Add(MakeShared<FJsonValueObject>(ImageBlock));
            }
        }
        else if (!Payload.IsEmpty())
        {
            // Non-image payload: add as text (includes size-limit errors)
            FString DisplayPayload = Payload;
            if (TotalImageSize > MaxPayloadBytes)
            {
                DisplayPayload = FString::Printf(TEXT("Image payload too large (%lld bytes). Reduce resolution or angle count."), TotalImageSize);
            }
            TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
            TextBlock->SetStringField(TEXT("type"), TEXT("text"));
            TextBlock->SetStringField(TEXT("text"), DisplayPayload);
            ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
        }

        // Ensure we have at least one content block
        if (ContentBlocks.Num() == 0)
        {
            TSharedRef<FJsonObject> TextBlock = MakeShared<FJsonObject>();
            TextBlock->SetStringField(TEXT("type"), TEXT("text"));
            TextBlock->SetStringField(TEXT("text"), bIsError ? TEXT("Error") : TEXT("Success"));
            ContentBlocks.Add(MakeShared<FJsonValueObject>(TextBlock));
        }

        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetArrayField(TEXT("content"), ContentBlocks);
        Result->SetBoolField(TEXT("isError"), bIsError);
        return Result;
    };

    if (!Spec)
    {
        return MakeToolResult(FString::Printf(TEXT("Unknown tool: %s"), *ToolName), TEXT(""), true);
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
                    TEXT(""), true);
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

    // DispatchCommandSync runs the exact same command the WebSocket bridge would.
    // Returns the JSON response safely (MCP and WebSocket now share the same
    // reentrancy-safe dispatch via FGraphBridgeCommandContext).
    FString ResponseJson = UGraphBridgeAutomationLibrary::DispatchCommandSync(Command);

    TSharedPtr<FJsonObject> ResponseObject;
    TSharedRef<TJsonReader<TCHAR>> ResponseReader = TJsonReaderFactory<TCHAR>::Create(ResponseJson);
    if (!FJsonSerializer::Deserialize(ResponseReader, ResponseObject) || !ResponseObject.IsValid())
    {
        return MakeToolResult(FString::Printf(TEXT("Malformed response from dispatcher: %s"), *ResponseJson), TEXT(""), true);
    }

    bool bSuccess = ResponseObject->GetBoolField(TEXT("success"));
    FString Message = ResponseObject->GetStringField(TEXT("message"));
    FString Payload = ResponseObject->GetStringField(TEXT("payload"));

    return MakeToolResult(Message, Payload, !bSuccess);
}
