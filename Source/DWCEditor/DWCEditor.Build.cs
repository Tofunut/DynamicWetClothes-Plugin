using UnrealBuildTool;

public class DWCEditor : ModuleRules
{
	public DWCEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"DWC",
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
				"ToolMenus",
				"UnrealEd"
			});
	}
}
