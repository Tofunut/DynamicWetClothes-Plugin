using UnrealBuildTool;

public class DynamicWetClothesEditor : ModuleRules
{
	public DynamicWetClothesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedPreviewScene",
				"AssetDefinition",
				"AssetRegistry",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DynamicWetClothes",
				"Engine",
				"InputCore",
				"ProceduralMeshComponent",
				"PropertyEditor",
				"Projects",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"UnrealEd"
			});
	}
}
