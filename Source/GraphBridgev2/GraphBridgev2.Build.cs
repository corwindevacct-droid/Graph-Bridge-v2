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
                "UnrealEd",
                "UMG",
                "UMGEditor",
                "MaterialEditor",
                "RHI",
                // MCP (Model Context Protocol) transport — UE's built-in HTTP server
                // module, no third-party dependency needed alongside IXWebSocket.
                "HTTPServer",
                // UAnimGraphNode_Base — used only to reject Anim Graph node classes
                // in SpawnNodeOnGraph's safety guard (see GraphBridgeAutomationLibrary.cpp).
                "AnimGraph",
                // Niagara system/emitter creation and Stack Editor module
                // inspection (CREATE_NIAGARA_SYSTEM/EMITTER, LIST_NIAGARA_MODULES,
                // SET_NIAGARA_MODULE_INPUT). Runtime + Editor module both needed:
                // Niagara for UNiagaraSystem/UNiagaraEmitter themselves,
                // NiagaraEditor for the factories and Stack Editor ViewModel types.
                "Niagara",
                "NiagaraEditor",
                // Physics Asset auto-generation (CREATE_PHYSICS_ASSET) —
                // FPhysicsAssetUtils::CreateFromSkeletalMesh lives here, not in
                // UnrealEd (confirmed: UPhysicsAssetFactory::CreatePhysicsAssetFromMesh
                // itself is interactive-only, this is the real headless path it
                // calls internally).
                "PhysicsUtilities",
                // IK Rig + IK Retargeter creation and scripting API
                // (CREATE_IK_RIG, IK_RIG_AUTO_SETUP, ADD_IK_GOAL,
                // ADD_RETARGET_CHAIN, CREATE_IK_RETARGETER). IKRig for the
                // runtime asset types, IKRigEditor for UIKRigController /
                // UIKRigDefinitionFactory / UIKRetargetFactory / UIKRetargeterController.
                "IKRig",
                "IKRigEditor",
                // IAssetTools::CreateAsset — used for CREATE_IK_RETARGETER,
                // matching the same pattern IKRigDefinitionFactory's own
                // CreateNewIKRigAsset() uses internally.
                "AssetTools"
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
