// Copyright 2026 Team Tofunut. All Rights Reserved.

using UnrealBuildTool;

public class DWCEditor : ModuleRules
{
	public DWCEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// STRICT-CHECK ONLY — remove/revert before Fab submission.
		// UE 5.8 ModuleRules diagnostics used for pre-submission verification.
		bWarningsAsErrors = false;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Error;
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;
		CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Error;
		CppCompileWarningSettings.ModuleIncludePathWarningLevel = WarningLevel.Error;
		CppCompileWarningSettings.ModuleIncludePrivateWarningLevel = WarningLevel.Error;
		CppCompileWarningSettings.ModuleIncludeSubdirectoryWarningLevel = WarningLevel.Error;
		CppCompileWarningSettings.NonInlinedGenCppWarningLevel = WarningLevel.Warning;
		bValidateFormatStrings = true;
		bValidateInternalApi = true;
		bUseUnity = false;


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AdvancedPreviewScene",
				"AppFramework",
				"AssetDefinition",
				"AssetRegistry",
				"AssetTools",
				"Core",
				"CoreUObject",
				"ContentBrowser",
				"DWC",
				"Engine",
				"InputCore",
				"InteractiveToolsFramework",
				"MaterialEditor",
				"MessageLog",
				"MeshDescription",
				"ProceduralMeshComponent",
				"PropertyEditor",
				"Projects",
				"RHI",
				"RenderCore",
				"Slate",
				"SkeletalMeshDescription",
				"SlateCore",
				"StaticMeshDescription",
				"ToolMenus",
				"EditorInteractiveToolsFramework",
				"UnrealEd"
			});
	}
}
