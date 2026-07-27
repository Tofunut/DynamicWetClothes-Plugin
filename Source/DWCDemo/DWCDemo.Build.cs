using UnrealBuildTool;

public class DWCDemo : ModuleRules
{
	public DWCDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DWC",
				"DWCGPU",
				"Niagara"
			});
	}
}
