using UnrealBuildTool;

public class DWC : ModuleRules
{
	public DWC(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
                "RenderCore",
                "RHI"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PhysicsCore",
				"Renderer",
				"Projects"
			});
	}
}
