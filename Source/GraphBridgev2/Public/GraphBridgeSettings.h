// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GraphBridgeSettings.generated.h"

UCLASS(Config=EditorPerProjectUserSettings, defaultconfig, meta=(DisplayName="GraphBridge AI"))
class GRAPHBRIDGEV2_API UGraphBridgeSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UGraphBridgeSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    static UGraphBridgeSettings* Get() { return GetMutableDefault<UGraphBridgeSettings>(); }

    // NOTE: the LLM provider API key is intentionally NOT a UPROPERTY here
    // any more -- it used to be, which meant `defaultconfig` persisted it in
    // plaintext to <Project>/Config/DefaultEditorPerProjectUserSettings.ini,
    // a file inside the user's repository. It now lives in OS credential
    // storage (Windows Credential Manager / macOS Keychain), falling back to
    // the GRAPHBRIDGE_API_KEY environment variable -- see
    // FGraphBridgeCredentialStore. Read/write it through that class, not a
    // field here.

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge")
    FString SelectedModel;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge")
    int32 ServerPort;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge",
        meta=(DisplayName="API Endpoint (blank=OpenAI, use https://api.anthropic.com/v1/messages for Claude)"))
    FString ApiEndpoint;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge", meta=(DisplayName="Enable MCP Server"))
    bool bEnableMCPServer;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge", meta=(DisplayName="MCP Server Port"))
    int32 MCPServerPort;

    // Off by default. RUN_PYTHON executes arbitrary Python inside the editor
    // process on behalf of ANY bridge client -- enabling this is equivalent
    // to granting arbitrary code execution to anything that can reach the
    // WebSocket port (see README.md's Security section). Enforced at the
    // ExecuteAtomicCommand dispatch boundary, not just by hiding a control.
    UPROPERTY(Config, EditAnywhere, Category="GraphBridge",
        meta=(DisplayName="Allow RUN_PYTHON (arbitrary code execution -- see README Security section)"))
    bool bAllowRunPython;

#if WITH_EDITOR
    // Logs a prominent warning the moment bAllowRunPython is switched on,
    // regardless of whether it happened via this panel, Project Settings, or
    // editing the ini directly.
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
