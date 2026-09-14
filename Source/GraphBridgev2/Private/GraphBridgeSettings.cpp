// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeSettings.h"
#include "GraphBridgev2.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

UGraphBridgeSettings::UGraphBridgeSettings()
{
    CategoryName  = TEXT("Plugins");
    SectionName   = TEXT("GraphBridge");
    SelectedModel = TEXT("claude-sonnet-4-6");
    ServerPort    = 8080;
    bEnableMCPServer = true;
    MCPServerPort    = 8090;
    bAllowRunPython  = false;
}

#if WITH_EDITOR
void UGraphBridgeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    static const FName AllowRunPythonName = GET_MEMBER_NAME_CHECKED(UGraphBridgeSettings, bAllowRunPython);
    if (PropertyChangedEvent.GetPropertyName() == AllowRunPythonName && bAllowRunPython)
    {
        UE_LOG(LogGraphBridge, Warning,
            TEXT("GraphBridge: RUN_PYTHON is now ENABLED. Any process that can reach the ")
            TEXT("WebSocket bridge (any other program running as this OS user -- localhost is ")
            TEXT("not a trust boundary) can now execute arbitrary Python inside this editor ")
            TEXT("process. See README.md's Security section before leaving this on."));
    }
}
#endif
