#include "Core/DWCEditorStyle.h"
#include "Modules/ModuleManager.h"

class FDWCEditorModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        FDWCEditorStyle::Initialize();
    }

    virtual void ShutdownModule() override
    {
        FDWCEditorStyle::Shutdown();
    }
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
