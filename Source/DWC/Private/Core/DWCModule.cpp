// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/CoreDelegates.h"
#include "Profiling/DWCStats.h"
#include "Utility/DWCLog.h"
#include "ShaderCore.h"

class FDWCModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DynamicWetClothes"));
        if (Plugin.IsValid())
            AddShaderSourceDirectoryMapping(TEXT("/DynamicWetClothes"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));

        RegisterDWCStatCommands();
        PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FDWCModule::HandlePostEngineInit);
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
    void HandlePostEngineInit()
    {
        RegisterDWCStatCommands();
        LoadOptionalWaterIntegration();
    }

    void RegisterDWCStatCommands()
    {
        DWCStats::RegisterStatCommands();
    }

    void LoadOptionalWaterIntegration()
    {
        const TSharedPtr<IPlugin> WaterPlugin = IPluginManager::Get().FindEnabledPlugin(TEXT("Water"));
        if (!WaterPlugin.IsValid())
        {
            return;
        }

        if (!FModuleManager::Get().IsModuleLoaded(TEXT("DWCWaterSystemIntegration")) &&
            FModuleManager::Get().LoadModule(TEXT("DWCWaterSystemIntegration")) == nullptr)
        {
            UE_LOG(LogDWC, Warning, TEXT("DWC: Water is enabled, but the optional DWCWaterSystemIntegration module could not be loaded."));
        }
    }

    FDelegateHandle PostEngineInitHandle;
};

IMPLEMENT_MODULE(FDWCModule, DWC)
