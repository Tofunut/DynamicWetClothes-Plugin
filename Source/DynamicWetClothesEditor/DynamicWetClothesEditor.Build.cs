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
				"AppFramework",
				"AssetDefinition",
				"AssetRegistry",
				"AssetTools",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DynamicWetClothes",
				"Engine",
				"InputCore",
				"MaterialEditor",
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
