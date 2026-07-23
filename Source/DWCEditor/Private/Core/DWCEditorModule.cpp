#include "Core/DWCEditorStyle.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"

class FDWCEditorModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        FDWCEditorStyle::Initialize();

        FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyEditorModule.RegisterCustomClassLayout(
            TEXT("DynamicWetClothesComponent"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FDynamicWetClothesComponentCustomization::MakeInstance));
        PropertyEditorModule.NotifyCustomizationModuleChanged();

        ValidateApplyWetnessFunctionCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.ValidateApplyWetnessFunction"),
            TEXT("Validates the fixed MF_DWC_ApplyWetness_CPU/GPU assets without modifying them."),
            FConsoleCommandDelegate::CreateRaw(this, &FDWCEditorModule::ValidateApplyWetnessFunction),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (ValidateApplyWetnessFunctionCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ValidateApplyWetnessFunctionCommand);
            ValidateApplyWetnessFunctionCommand = nullptr;
        }

        if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
        {
            FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            PropertyEditorModule.UnregisterCustomClassLayout(TEXT("DynamicWetClothesComponent"));
            PropertyEditorModule.NotifyCustomizationModuleChanged();
        }

        FDWCEditorStyle::Shutdown();
    }

  private:
    void ValidateApplyWetnessFunction()
    {
        FString ErrorMessage;
        if (!FWCAMaterialGenerator::ValidateSharedApplyWetnessFunction(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_ApplyWetness_CPU/GPU fixed asset validation failed:\n%s"), *ErrorMessage);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_ApplyWetness_CPU/GPU satisfy the fixed DWC material-function contract."));
    }

    IConsoleObject*               ValidateApplyWetnessFunctionCommand = nullptr;
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
