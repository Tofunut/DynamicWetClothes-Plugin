#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FDWCModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DynamicWetClothes"));
        if (Plugin.IsValid()) AddShaderSourceDirectoryMapping(TEXT("/DynamicWetClothes"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
    }
};

IMPLEMENT_MODULE(FDWCModule, DWC)
