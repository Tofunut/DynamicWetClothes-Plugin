using UnrealBuildTool;

public class DWCWater : ModuleRules
{
	public DWCWater(ReadOnlyTargetRules Target) : base(Target)
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
