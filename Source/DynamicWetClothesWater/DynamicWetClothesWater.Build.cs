using UnrealBuildTool;

public class DynamicWetClothesWater : ModuleRules
{
	public DynamicWetClothesWater(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"DynamicWetClothes",
				"Water"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			});
	}
}
