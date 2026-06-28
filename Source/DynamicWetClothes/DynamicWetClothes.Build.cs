using UnrealBuildTool;

public class DynamicWetClothes : ModuleRules
{
	public DynamicWetClothes(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Niagara",
				"RHI",
				"RenderCore",
				"Water"
			});
	}
}
