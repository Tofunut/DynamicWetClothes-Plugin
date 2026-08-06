using UnrealBuildTool;

public class DWCGPU : ModuleRules
{
    public DWCGPU(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "DWC",
                "Engine",
                "Niagara",
                "NiagaraCore",
                "RenderCore"
            });

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "RHI",
                "Renderer",
                "Projects"
            });
    }
}
