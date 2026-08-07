//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DWCGPUBackend.h"
#include "DWCGPUPreviewSimulator.h"
#include "GPU/DWCGPUBackend.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCGPUModule, Log, All);

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
            UE_LOG(LogDWCGPUModule, Warning, TEXT("DWCGPU: Could not find the DynamicWetClothes plugin for shader directory mapping."));
        }
    }

    virtual TUniquePtr<IDWCGPUBackend> CreateBackend() override
    {
        return MakeUnique<FDWCGPUBackend>();
    }

    virtual TUniquePtr<IDWCGPUPreviewSimulator> CreatePreviewSimulator() override
    {
        return MakeUnique<FDWCGPUPreviewSimulator>();
    }

};

IMPLEMENT_MODULE(FDWCGPUModule, DWCGPU)
