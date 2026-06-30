// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeSettings.h"

UGraphBridgeSettings::UGraphBridgeSettings()
{
    CategoryName  = TEXT("Plugins");
    SectionName   = TEXT("GraphBridge");
    SelectedModel = TEXT("claude-sonnet-4-6");
    ServerPort    = 8080;
}
