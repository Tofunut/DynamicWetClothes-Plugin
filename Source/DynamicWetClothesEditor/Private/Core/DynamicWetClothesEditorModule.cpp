#include "Core/DynamicWetClothesEditorStyle.h"
#include "Modules/ModuleManager.h"

class FDynamicWetClothesEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FDynamicWetClothesEditorStyle::Initialize();
	}

	virtual void ShutdownModule() override
	{
		FDynamicWetClothesEditorStyle::Shutdown();
	}
};

IMPLEMENT_MODULE(FDynamicWetClothesEditorModule, DynamicWetClothesEditor)
