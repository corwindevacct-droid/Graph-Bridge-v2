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

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge", meta=(Password, PasswordField))
    FString ApiKey;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge")
    FString SelectedModel;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge")
    int32 ServerPort;

    UPROPERTY(Config, EditAnywhere, Category="GraphBridge",
        meta=(DisplayName="API Endpoint (blank=OpenAI, use https://api.anthropic.com/v1/messages for Claude)"))
    FString ApiEndpoint;
};
