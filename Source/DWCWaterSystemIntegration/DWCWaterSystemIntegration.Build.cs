// Copyright 2026 Team Tofunut. All Rights Reserved.

using UnrealBuildTool;

public class DWCWaterSystemIntegration : ModuleRules
{
	public DWCWaterSystemIntegration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
