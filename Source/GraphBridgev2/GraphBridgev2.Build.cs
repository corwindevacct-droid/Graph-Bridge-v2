// Copyright 2026 Corwin Hicks. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class GraphBridgev2 : ModuleRules
{
    public GraphBridgev2(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        bUseUnity = false;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "DeveloperSettings",
            "HTTP",
            "Json",
            "JsonUtilities"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "BlueprintGraph",
                "PythonScriptPlugin",
                "Slate",
                "SlateCore",
                "ToolMenus",
                "EditorFramework",
                "UnrealEd"
            });
        }

        // Plugin public headers
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

        // IXWebSocket third-party
        // Folder: Source/ThirdParty/ixwebsocket/
        // Includes use: #include "ixwebsocket/IXWebSocket.h"
        // Add ThirdParty/ as parent so the ixwebsocket/ subfolder resolves
        string ThirdPartyPath = Path.Combine(PluginDirectory, "Source", "ThirdParty");
        PublicIncludePaths.Add(ThirdPartyPath);

        // ix .cpp files include their siblings as "IXWebSocket.h" (no prefix),
        // so the ixwebsocket source directory itself must be on the include path
        // for the thin wrappers in Private/ix/ to resolve those internal includes.
        PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "ixwebsocket"));

        bEnableExceptions = true;
        CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;
        CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;

        // Suppress MSVC deprecation warnings from vendored IXWebSocket
        PublicDefinitions.Add("_CRT_SECURE_NO_WARNINGS=1");
    }
}
