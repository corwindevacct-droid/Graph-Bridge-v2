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

class FGraphBridgev2Module : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Dispatch a pipe-delimited graph command and return the JSON result string. */
    virtual FString HandleGraphCommand(const FString& Command);

    static TSharedRef<SDockTab> SpawnGraphBridgeTab(const FSpawnTabArgs& Args);

    TSharedPtr<FGraphBridgeLLMClient> GetOrCreateLLMClient();

private:
    TSharedPtr<FGraphBridgeLLMClient> LLMClient;
};
