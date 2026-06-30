// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

DECLARE_DELEGATE_OneParam(FOnLLMResponse, const FString& /*Message*/);
DECLARE_DELEGATE_OneParam(FOnLLMError,     const FString& /*ErrorMessage*/);
DECLARE_DELEGATE_OneParam(FOnLLMIteration, int32          /*IterationNumber*/);

/**
 * Owns the OpenAI agentic loop for GraphBridge.
 * Sends user messages to the chat/completions endpoint,
 * dispatches tool calls through the local WebSocket bridge,
 * and fires FOnLLMResponse when the model produces a final text reply.
 */
class GRAPHBRIDGEV2_API FGraphBridgeLLMClient
{
public:
    FGraphBridgeLLMClient();
    ~FGraphBridgeLLMClient();

    /** Delegates — bind these before calling SendMessage */
    FOnLLMResponse  OnResponse;
    FOnLLMError     OnError;
    FOnLLMIteration OnLLMIteration;

    /**
     * Send a user message. Starts or continues the agentic loop.
     * Non-blocking — response arrives via OnResponse delegate on game thread.
     */
    void SendMessage(const FString& UserMessage);

    /** Clear conversation history (start a new session) */
    void ResetConversation();

    /** Abort an in-flight agentic loop */
    void RequestCancel();

private:
    /** Full conversation history as JSON objects (role/content pairs) */
    TArray<TSharedPtr<FJsonObject>> ConversationHistory;

    /** Recursion guard — max tool call iterations per user turn */
    static constexpr int32 MaxToolIterations = 40;
    int32 CurrentIteration = 0;

    TAtomic<bool>                                bCancelRequested { false };
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveHttpRequest;

    /** Build the tools array for the API payload */
    TArray<TSharedPtr<FJsonValue>> BuildToolSchemas() const;

    /** Build and fire the HTTP request to OpenAI */
    void PostToOpenAI();

    /** HTTP response handler */
    void OnHttpResponse(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful);

    bool IsAnthropicProvider() const;
    void PostToAnthropic();
    void OnAnthropicHttpResponse(
        FHttpRequestPtr Request,
        FHttpResponsePtr Response,
        bool bWasSuccessful);

    /** Dispatch a single tool call through the WebSocket bridge and return result */
    FString DispatchToolCall(const FString& ToolName, const TSharedPtr<FJsonObject>& Args);

    /** Append a message object to ConversationHistory */
    void AppendMessage(const FString& Role, const FString& Content);

    /** Append a tool result message to ConversationHistory */
    void AppendToolResult(const FString& ToolCallId, const FString& Result);
};
