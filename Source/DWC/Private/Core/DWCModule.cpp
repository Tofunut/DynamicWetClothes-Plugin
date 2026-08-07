//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "Profiling/DWCStats.h"
#include "ShaderCore.h"

class FDWCModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DynamicWetClothes"));
        if (Plugin.IsValid()) AddShaderSourceDirectoryMapping(TEXT("/DynamicWetClothes"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));

        RegisterDWCStatCommands();
        PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FDWCModule::RegisterDWCStatCommands);
    }

    virtual void ShutdownModule() override
    {
        if (PostEngineInitHandle.IsValid())
        {
            FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
            PostEngineInitHandle.Reset();
        }

        DWCStats::UnregisterStatCommands();
    }

private:
    void RegisterDWCStatCommands()
    {
        DWCStats::RegisterStatCommands();
    }

    FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FDWCModule, DWC)
