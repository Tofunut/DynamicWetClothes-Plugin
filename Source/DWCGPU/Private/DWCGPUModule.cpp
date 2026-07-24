#include "DWCGPUBackend.h"
#include "GPU/DWCGPUBackend.h"
#include "GPU/DWCSurfaceWaterSimulationState.h"
#include "SurfaceWater/DWCSurfaceWaterSimulationState.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

class FDWCGPUModule final : public IDWCGPUModule
{
public:
    virtual void StartupModule() override
    {
        TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DynamicWetClothes"));
        if (Plugin.IsValid())
        {
            const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private"));
            AddShaderSourceDirectoryMapping(TEXT("/DWCGPU"), ShaderDirectory);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("DWCGPU: Could not find the DynamicWetClothes plugin for shader directory mapping."));
        }
    }

    virtual TUniquePtr<IDWCGPUBackend> CreateBackend() override
    {
        return MakeUnique<FDWCGPUBackend>();
    }

    virtual TUniquePtr<IDWCSurfaceWaterSimulationState> CreateSurfaceWaterSimulationState() override
    {
        return MakeUnique<FDWCSurfaceWaterSimulationState>();
    }
};

IMPLEMENT_MODULE(FDWCGPUModule, DWCGPU)
