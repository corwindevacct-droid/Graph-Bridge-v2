// Copyright 2026 Corwin Hicks. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * GraphBridge Tool Manifest
 *
 * Single source of truth for all 120 tools. Defines:
 * - Command name and description
 * - Parameters (required + optional)
 * - Return type
 * - Category (for grouping in panel/MCP)
 * - Flags (mutating, public, etc.)
 *
 * From this manifest, we generate:
 * 1. Router command table (GraphBridgeAutomationLibrary)
 * 2. MCP tools/list schema (GraphBridgeMCPServer)
 * 3. Panel tool schemas (GraphBridgeLLMClient)
 */

enum class EGraphBridgeParamType : uint8
{
	String,        // FString
	Integer,       // int32
	Float,         // float
	Bool,          // bool
	Enum,          // Enumeration (values: comma-separated list)
};

struct FGraphBridgeToolParam
{
	FString Name;                     // Parameter identifier (bp_path, node_id, etc.)
	EGraphBridgeParamType ParamType = EGraphBridgeParamType::String;  // Type: String, Integer, Float, Bool, Enum
	FString Description;              // Human-readable description
	bool bOptional = false;           // Is this parameter optional?

	// Type-specific metadata
	double MinValue = 0.0;            // For Integer/Float: minimum allowed value
	double MaxValue = 0.0;            // For Integer/Float: maximum allowed value
	FString EnumValues;               // For Enum: comma-separated list of valid values
	FString DefaultValue;             // Default value for optional parameters (e.g., "0", "Red", "")

	// Legacy compatibility: kept for manifest parsing
	FString Type_Legacy;              // Old-style type string ("string", "number", etc.)
	bool bRequired_Legacy = true;     // Old bRequired flag (maps to !bOptional)

	// Backward-compatible constructor from old string-based system
	FGraphBridgeToolParam(const FString& InName, const FString& InType, const FString& InDesc, bool bInRequired = true)
		: Name(InName), Description(InDesc), bOptional(!bInRequired), Type_Legacy(InType), bRequired_Legacy(bInRequired)
	{
		// Auto-detect type from legacy string
		if (InType == TEXT("number")) {
			ParamType = EGraphBridgeParamType::Integer;
		} else if (InType == TEXT("float")) {
			ParamType = EGraphBridgeParamType::Float;
		} else if (InType == TEXT("bool")) {
			ParamType = EGraphBridgeParamType::Bool;
		} else {
			ParamType = EGraphBridgeParamType::String;  // Default to string
		}
	}

	FGraphBridgeToolParam() = default;
};

struct FGraphBridgeToolDef
{
	FString Command;           // Unique command name (e.g., "SPAWN_NODE")
	FString Description;       // One-line description for UI
	FString Category;          // "Blueprint", "Variable", "Animation", etc.
	FString ReturnType;        // "string", "json", "void", etc.
	bool bMutating = true;     // Does it modify the editor state?
	bool bPublic = true;       // Expose on all three surfaces (MCP/panel/router)?
	bool bDeprecated = false;  // Mark as deprecated in descriptions
	FString DeprecationHint;   // "Use X instead" if deprecated

	TArray<FGraphBridgeToolParam> Parameters;
	TArray<FGraphBridgeToolParam> OptionalParameters;

	FGraphBridgeToolDef() = default;
	FGraphBridgeToolDef(
		const FString& InCommand,
		const FString& InDescription,
		const FString& InCategory
	)
		: Command(InCommand), Description(InDescription), Category(InCategory)
	{
	}
};

/**
 * Manifest builder — registers all 120 tools.
 * This is the ONLY place tools are defined.
 */
class GRAPHBRIDGEV2_API FGraphBridgeToolManifest
{
public:
	/**
	 * Get or build the complete manifest.
	 * Lazy-initialized on first call.
	 */
	static const TArray<FGraphBridgeToolDef>& GetManifest();

	/**
	 * Find a tool by command name.
	 */
	static const FGraphBridgeToolDef* FindTool(const FString& CommandName);

	/**
	 * Count tools matching a predicate.
	 */
	static int32 CountTools(TFunction<bool(const FGraphBridgeToolDef&)> Predicate);

	/**
	 * Export manifest as JSON for documentation.
	 */
	static TSharedPtr<FJsonObject> ExportAsJson();

private:
	static TArray<FGraphBridgeToolDef> BuildManifest();
	static TArray<FGraphBridgeToolDef> CachedManifest;
	static bool bManifestBuilt;
};
