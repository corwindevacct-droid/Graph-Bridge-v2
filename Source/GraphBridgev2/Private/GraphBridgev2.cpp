// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgev2.h"
#include "GraphBridgeAutomationLibrary.h"
#include "GraphBridgeSettings.h"
#include "GraphBridgeMCPServer.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "SGraphBridgePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Containers/Ticker.h"
#endif

IMPLEMENT_MODULE(FGraphBridgev2Module, GraphBridgev2)
DEFINE_LOG_CATEGORY(LogGraphBridge);

FGraphBridgev2Module::~FGraphBridgev2Module() = default;

void FGraphBridgev2Module::StartupModule()
{
    UGraphBridgeAutomationLibrary::StartGraphBridgeServer(
        UGraphBridgeSettings::Get()->ServerPort);

    if (UGraphBridgeSettings::Get()->bEnableMCPServer)
    {
        MCPServer = MakeUnique<FGraphBridgeMCPServer>();
        const int32 MCPPort = UGraphBridgeSettings::Get()->MCPServerPort;
        if (MCPServer->Start(MCPPort))
        {
            UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: MCP server auto-started on port %d"), MCPPort);
        }
        else
        {
            UE_LOG(LogGraphBridge, Error, TEXT("GraphBridge: MCP server failed to auto-start on port %d"), MCPPort);
            MCPServer.Reset();
        }
    }

    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: Registering tab spawner"));
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        FName(TEXT("GraphBridgePanel")),
        FOnSpawnTab::CreateStatic(&FGraphBridgev2Module::SpawnGraphBridgeTab))
        .SetDisplayName(NSLOCTEXT("GraphBridge", "TabTitle", "GraphBridge AI"))
        .SetMenuType(ETabSpawnerMenuType::Enabled);
    UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: Tab spawner registered"));
}

FString FGraphBridgev2Module::HandleGraphCommand(const FString& Command)
{
    return UGraphBridgeAutomationLibrary::DispatchCommandSync(Command);
}

void FGraphBridgev2Module::ShutdownModule()
{
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FName(TEXT("GraphBridgePanel")));
    LLMClient.Reset();

    if (MCPServer)
    {
        MCPServer->Stop();
        MCPServer.Reset();
        UE_LOG(LogGraphBridge, Log, TEXT("GraphBridge: MCP server stopped (module shutdown)"));
    }
}

TSharedPtr<FGraphBridgeLLMClient> FGraphBridgev2Module::GetOrCreateLLMClient()
{
    if (!LLMClient.IsValid())
    {
        LLMClient = MakeShareable(new FGraphBridgeLLMClient());
    }
    return LLMClient;
}

TSharedRef<SDockTab> FGraphBridgev2Module::SpawnGraphBridgeTab(const FSpawnTabArgs& Args)
{
    return SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        [
            SNew(SGraphBridgePanel)
        ];
}
