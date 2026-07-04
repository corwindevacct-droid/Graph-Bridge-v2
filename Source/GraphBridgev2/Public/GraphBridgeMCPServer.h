// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"

class IHttpRouter;
struct FHttpServerRequest;
class FJsonObject;
class FJsonValue;

// Minimal MCP (Model Context Protocol) server — JSON-RPC 2.0 over Streamable
// HTTP (spec version 2025-06-18), exposing every GraphBridge bridge command
// as an MCP tool.
//
// This is a NEW transport alongside the existing IXWebSocket bridge in
// GraphBridgeAutomationLibrary — it does not replace it, and both can run
// at once on separate ports. Every tool call is re-shaped into the same
// pipe-delimited command string the WebSocket bridge understands and routed
// through UGraphBridgeAutomationLibrary::DispatchCommandSync, so there is
// exactly one command implementation to maintain; MCP is just another way
// to reach it. This also means every mutating command's existing
// FScopedTransaction (undo/redo) support applies unchanged.
//
// Deliberately minimal: no SSE, no session IDs, no pagination — all
// optional per spec for a single-client, localhost-only tool server like
// this one. Every JSON-RPC request gets a single `Content-Type:
// application/json` response, which the spec explicitly allows as an
// alternative to opening an SSE stream (see "Streamable HTTP" transport,
// "Sending Messages to the Server", point 5).
class FGraphBridgeMCPServer
{
public:
    FGraphBridgeMCPServer();
    ~FGraphBridgeMCPServer();

    FGraphBridgeMCPServer(const FGraphBridgeMCPServer&) = delete;
    FGraphBridgeMCPServer& operator=(const FGraphBridgeMCPServer&) = delete;

    // Binds the /mcp route on Port and starts the HTTP listener.
    // Returns false if the port is already in use or binding otherwise fails.
    bool Start(int32 Port);

    // Unbinds the route. Safe to call even if Start() was never called or failed.
    void Stop();

    bool IsRunning() const { return RouteHandle.IsValid(); }
    int32 GetPort() const { return BoundPort; }

private:
    // Single entry point for the /mcp route (POST). Parses the JSON-RPC
    // envelope, dispatches to the matching handler below, and writes the
    // JSON-RPC response (or a bare 202 Accepted for notifications).
    bool HandleMCPRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

    // JSON-RPC method handlers. Each returns the "result" object to embed
    // in the response envelope.
    TSharedPtr<FJsonObject> HandleInitialize(const TSharedPtr<FJsonObject>& Params);
    TSharedPtr<FJsonObject> HandleToolsList();
    TSharedPtr<FJsonObject> HandleToolsCall(const TSharedPtr<FJsonObject>& Params);

    TSharedPtr<IHttpRouter> Router;
    FHttpRouteHandle RouteHandle;
    int32 BoundPort = 0;
};
