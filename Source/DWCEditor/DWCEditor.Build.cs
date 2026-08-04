using UnrealBuildTool;

public class DWCEditor : ModuleRules
{
	public DWCEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AdvancedPreviewScene",
				"AppFramework",
				"AssetDefinition",
				"AssetRegistry",
				"AssetTools",
				"Core",
				"CoreUObject",
				"ContentBrowser",
				"DWC",
				"Engine",
				"InputCore",
				"InteractiveToolsFramework",
				"MaterialEditor",
				"MessageLog",
				"MeshDescription",
				"ProceduralMeshComponent",
				"PropertyEditor",
				"Projects",
				"RHI",
				"RenderCore",
				"Slate",
				"SkeletalMeshDescription",
				"SlateCore",
				"StaticMeshDescription",
				"ToolMenus",
				"EditorInteractiveToolsFramework",
				"UnrealEd"
			});
	}
}
