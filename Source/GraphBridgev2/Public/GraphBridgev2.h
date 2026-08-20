// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "GraphBridgeLLMClient.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGraphBridge, Log, All);

#if WITH_EDITOR
class SDockTab;
class FSpawnTabArgs;
#endif

class FGraphBridgeMCPServer;

class FGraphBridgev2Module : public IModuleInterface
{
public:
    // Declared out-of-line (defined in GraphBridgev2.cpp, where
    // GraphBridgeMCPServer.h is fully included) because TUniquePtr's
    // destructor requires a complete type for FGraphBridgeMCPServer, which is
    // only forward-declared here.
    virtual ~FGraphBridgev2Module() override;

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Dispatch a pipe-delimited graph command and return the JSON result string. */
    virtual FString HandleGraphCommand(const FString& Command);

    static TSharedRef<SDockTab> SpawnGraphBridgeTab(const FSpawnTabArgs& Args);

    TSharedPtr<FGraphBridgeLLMClient> GetOrCreateLLMClient();

private:
    TSharedPtr<FGraphBridgeLLMClient> LLMClient;

    // Owns the MCP (Model Context Protocol) HTTP transport — auto-started in
    // StartupModule alongside the WebSocket bridge above, independent of the
    // separate MCPServer instance UGraphBridgeAutomationLibrary owns for the
    // manual StartMCPServer/StopMCPServer Blueprint-callable functions.
    TUniquePtr<FGraphBridgeMCPServer> MCPServer;
};
