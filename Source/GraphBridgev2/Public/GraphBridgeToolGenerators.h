// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GraphBridgeToolManifest.h"

/**
 * Tool code generators — transform the unified manifest into surface-specific code.
 * Each generator reads the manifest and produces code for one of the three surfaces:
 * - Router dispatch table (GraphBridgeAutomationLibrary)
 * - MCP tool specs (GraphBridgeMCPServer)
 * - Panel tool schemas (GraphBridgeLLMClient)
 */

class FGraphBridgeRouterGenerator
{
public:
	/**
	 * Generate the if-else dispatch branches for ExecuteAtomicCommand.
	 * Output: C++ code that can be inserted into ExecuteAtomicCommand's body.
	 */
	static FString GenerateDispatchTable(const TArray<FGraphBridgeToolDef>& Manifest);

	/**
	 * Generate parameter extraction boilerplate for a single command.
	 * Example: Converts "SPAWN_NODE|bp|class|comment|x|y" into named variables.
	 */
	static FString GenerateParameterExtraction(const FGraphBridgeToolDef& Tool);

	/**
	 * Generate SendResponse call with proper success/error handling.
	 */
	static FString GenerateResponseCall(const FGraphBridgeToolDef& Tool);
};

class FGraphBridgeMCPGenerator
{
public:
	/**
	 * Generate the complete FToolSpec array for GetToolSpecs().
	 * Only includes tools marked as bPublic=true.
	 */
	static FString GenerateToolSpecs(const TArray<FGraphBridgeToolDef>& Manifest);

	/**
	 * Generate a single FToolSpec for a command.
	 */
	static FString GenerateToolSpec(const FGraphBridgeToolDef& Tool);
};

class FGraphBridgePanelGenerator
{
public:
	/**
	 * Generate all MakeTool() calls for BuildToolSchemas().
	 * Only includes tools marked as bPublic=true.
	 */
	static FString GenerateToolSchemas(const TArray<FGraphBridgeToolDef>& Manifest);

	/**
	 * Generate a single MakeTool() call with parameter descriptors.
	 */
	static FString GenerateToolSchema(const FGraphBridgeToolDef& Tool);
};
