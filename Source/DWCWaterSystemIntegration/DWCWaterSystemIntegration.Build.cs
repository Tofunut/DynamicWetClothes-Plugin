// Copyright 2026 Team Tofunut. All Rights Reserved.

using UnrealBuildTool;

public class DWCWaterSystemIntegration : ModuleRules
{
	public DWCWaterSystemIntegration(ReadOnlyTargetRules Target) : base(Target)
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


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DWC",
				"Water"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			});
	}
}
