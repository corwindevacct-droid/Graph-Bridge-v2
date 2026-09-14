// Copyright 2026 Corwin Hicks. All Rights Reserved.

#include "GraphBridgeToolGenerators.h"

// ────────────────────────────────────────────────────────────────────────────
// MCP Generator Implementation
// ────────────────────────────────────────────────────────────────────────────

FString FGraphBridgeMCPGenerator::GenerateToolSpec(const FGraphBridgeToolDef& Tool)
{
	FString Result;
	Result += FString::Printf(TEXT("            { TEXT(\"%s\"), TEXT(\"%s\"),\n"),
		*Tool.Command, *Tool.Description);

	// Combine required and optional parameters
	TArray<FGraphBridgeToolParam> AllParams = Tool.Parameters;
	AllParams.Append(Tool.OptionalParameters);

	if (AllParams.Num() == 0)
	{
		Result += TEXT("              {} },\n");
	}
	else
	{
		Result += TEXT("              { ");
		for (int32 i = 0; i < AllParams.Num(); ++i)
		{
			const FGraphBridgeToolParam& Param = AllParams[i];
			Result += FString::Printf(TEXT("{TEXT(\"%s\"),TEXT(\"%s\")}"),
				*Param.Name, *Param.Type_Legacy);

			if (!Param.bRequired_Legacy)
			{
				Result += TEXT(",false");
			}

			if (i < AllParams.Num() - 1)
			{
				Result += TEXT(", ");
			}
		}
		Result += TEXT(" } },\n");
	}

	return Result;
}

FString FGraphBridgeMCPGenerator::GenerateToolSpecs(const TArray<FGraphBridgeToolDef>& Manifest)
{
	FString Result;
	Result += TEXT("    const TArray<FToolSpec>& GetToolSpecs()\n");
	Result += TEXT("    {\n");
	Result += TEXT("        static const TArray<FToolSpec> Specs = {\n");

	// Count and generate only public tools
	int32 PublicCount = 0;
	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		if (Tool.bPublic)
		{
			PublicCount++;
		}
	}

	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		if (Tool.bPublic)
		{
			Result += GenerateToolSpec(Tool);
		}
	}

	Result += FString::Printf(TEXT("        };\n"));
	Result += FString::Printf(TEXT("        return Specs;\n"));
	Result += FString::Printf(TEXT("    }\n\n"));
	Result += FString::Printf(TEXT("    // Generated: %d public tools\n"), PublicCount);

	return Result;
}

// ────────────────────────────────────────────────────────────────────────────
// Panel Generator Implementation
// ────────────────────────────────────────────────────────────────────────────

FString FGraphBridgePanelGenerator::GenerateToolSchema(const FGraphBridgeToolDef& Tool)
{
	FString Result;
	Result += FString::Printf(TEXT("    Add(MakeTool(TEXT(\"%s\"),\n"), *Tool.Command);
	Result += FString::Printf(TEXT("        TEXT(\"%s\"),\n"), *Tool.Description);

	// Required parameters
	if (Tool.Parameters.Num() > 0)
	{
		Result += TEXT("        {\n");
		for (int32 i = 0; i < Tool.Parameters.Num(); ++i)
		{
			const FGraphBridgeToolParam& Param = Tool.Parameters[i];
			Result += FString::Printf(TEXT("            {TEXT(\"%s\"), TEXT(\"%s\")}"),
				*Param.Name, *Param.Description);
			if (i < Tool.Parameters.Num() - 1 || Tool.OptionalParameters.Num() > 0)
			{
				Result += TEXT(",\n");
			}
			else
			{
				Result += TEXT("\n");
			}
		}

		// Optional parameters
		if (Tool.OptionalParameters.Num() > 0)
		{
			for (int32 i = 0; i < Tool.OptionalParameters.Num(); ++i)
			{
				const FGraphBridgeToolParam& Param = Tool.OptionalParameters[i];
				Result += FString::Printf(TEXT("            {TEXT(\"%s\"), TEXT(\"%s\")}"),
					*Param.Name, *Param.Description);
				if (i < Tool.OptionalParameters.Num() - 1)
				{
					Result += TEXT(",\n");
				}
				else
				{
					Result += TEXT("\n");
				}
			}
		}
		Result += TEXT("        }");
	}
	else if (Tool.OptionalParameters.Num() > 0)
	{
		Result += TEXT("        {\n");
		for (int32 i = 0; i < Tool.OptionalParameters.Num(); ++i)
		{
			const FGraphBridgeToolParam& Param = Tool.OptionalParameters[i];
			Result += FString::Printf(TEXT("            {TEXT(\"%s\"), TEXT(\"%s\")}"),
				*Param.Name, *Param.Description);
			if (i < Tool.OptionalParameters.Num() - 1)
			{
				Result += TEXT(",\n");
			}
			else
			{
				Result += TEXT("\n");
			}
		}
		Result += TEXT("        }");
	}
	else
	{
		Result += TEXT("        { }");
	}

	Result += TEXT("));\n\n");

	return Result;
}

FString FGraphBridgePanelGenerator::GenerateToolSchemas(const TArray<FGraphBridgeToolDef>& Manifest)
{
	FString Result;
	Result += TEXT("TArray<TSharedPtr<FJsonValue>> FGraphBridgeLLMClient::BuildToolSchemas() const\n");
	Result += TEXT("{\n");
	Result += TEXT("    TArray<TSharedPtr<FJsonValue>> Tools;\n\n");
	Result += TEXT("    auto Add = [&](TSharedPtr<FJsonObject> Tool)\n");
	Result += TEXT("    {\n");
	Result += TEXT("        Tools.Add(MakeShareable(new FJsonValueObject(Tool)));\n");
	Result += TEXT("    };\n\n");

	int32 PublicCount = 0;
	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		if (Tool.bPublic)
		{
			PublicCount++;
			Result += GenerateToolSchema(Tool);
		}
	}

	Result += TEXT("    return Tools;\n");
	Result += TEXT("}\n\n");
	Result += FString::Printf(TEXT("// Generated: %d public tools\n"), PublicCount);

	return Result;
}

// ────────────────────────────────────────────────────────────────────────────
// Router Generator Implementation
// ────────────────────────────────────────────────────────────────────────────

FString FGraphBridgeRouterGenerator::GenerateParameterExtraction(const FGraphBridgeToolDef& Tool)
{
	FString Result;

	int32 TotalParams = Tool.Parameters.Num() + Tool.OptionalParameters.Num();
	if (TotalParams == 0)
	{
		return TEXT(""); // No parameter extraction needed
	}

	// Check minimum required parameters
	Result += FString::Printf(TEXT("        if (P.Num() >= %d)\n        {\n"),
		Tool.Parameters.Num() + 1); // +1 for the command itself

	// Extract each parameter
	for (int32 i = 0; i < Tool.Parameters.Num(); ++i)
	{
		const FGraphBridgeToolParam& Param = Tool.Parameters[i];
		int32 ParamIndex = i + 1; // +1 because P[0] is the command
		Result += FString::Printf(TEXT("            // P[%d] = %s (%s)\n"),
			ParamIndex, *Param.Name, *Param.Type_Legacy);
	}

	Result += TEXT("        }\n");

	return Result;
}

FString FGraphBridgeRouterGenerator::GenerateResponseCall(const FGraphBridgeToolDef& Tool)
{
	FString Result;
	Result += FString::Printf(TEXT("            SendResponse(Sender, bOk, TEXT(\"%s\"), Message, Result);\n"),
		*Tool.Command);
	return Result;
}

FString FGraphBridgeRouterGenerator::GenerateDispatchTable(const TArray<FGraphBridgeToolDef>& Manifest)
{
	FString Result;
	Result += TEXT("// ── Router dispatch table (generated from manifest) ──────────────────────\n");
	Result += TEXT("// This replaces the massive if-else chain in ExecuteAtomicCommand.\n");
	Result += TEXT("// Each tool definition includes parameter extraction and response handling.\n\n");

	for (const FGraphBridgeToolDef& Tool : Manifest)
	{
		Result += FString::Printf(TEXT("    if (Op == TEXT(\"%s\"))\n"), *Tool.Command);
		Result += TEXT("    {\n");
		Result += FString::Printf(TEXT("        // %s\n"), *Tool.Description);
		Result += TEXT("        bool bOk = false;\n");
		Result += TEXT("        FString Result;\n");
		Result += TEXT("        FString Message;\n");
		Result += TEXT("\n");

		if (Tool.Parameters.Num() + Tool.OptionalParameters.Num() > 0)
		{
			int32 MinParams = Tool.Parameters.Num() + 1; // +1 for command
			Result += FString::Printf(TEXT("        if (P.Num() >= %d)\n"), MinParams);
			Result += TEXT("        {\n");

			// TODO: Add actual handler call here
			Result += TEXT("            // TODO: Call handler for this command\n");
			Result += TEXT("            // bOk = Handle_*Command*(...parameters...);\n");

			Result += TEXT("        }\n");
			Result += TEXT("        else\n");
			Result += TEXT("        {\n");
			Result += FString::Printf(TEXT("            Message = TEXT(\"Insufficient parameters for %s\");\n"), *Tool.Command);
			Result += TEXT("        }\n");
		}
		else
		{
			Result += TEXT("        // TODO: Call handler (no parameters required)\n");
		}

		Result += TEXT("\n");
		Result += FString::Printf(TEXT("        SendResponse(Sender, bOk, TEXT(\"%s\"), Message, Result);\n"),
			*Tool.Command);
		Result += TEXT("    }\n");
		Result += TEXT("    else ");
	}

	// Final else clause
	Result += TEXT("{\n");
	Result += TEXT("        SendResponse(Sender, false, Op, TEXT(\"Unknown command\"), TEXT(\"\"));\n");
	Result += TEXT("    }\n\n");

	return Result;
}
