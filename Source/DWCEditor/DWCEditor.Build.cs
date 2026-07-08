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
				"DWC",
				"Engine",
				"InputCore",
				"MaterialEditor",
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
				"UnrealEd"
			});
	}
}
