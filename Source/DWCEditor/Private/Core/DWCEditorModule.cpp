#include "Core/DWCEditorStyle.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"

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
        PropertyEditorModule.RegisterCustomClassLayout(
            TEXT("WetnessProfile"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FWetnessProfileDetailsCustomization::MakeInstance));
        PropertyEditorModule.NotifyCustomizationModuleChanged();

        ValidateSurfaceAppearanceFunctionsCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.ValidateSurfaceAppearanceFunctions"),
            TEXT("Validates the manually authored DWC material-function set without modifying it."),
            FConsoleCommandDelegate::CreateRaw(this, &FDWCEditorModule::ValidateSurfaceAppearanceFunctions),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (ValidateSurfaceAppearanceFunctionsCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ValidateSurfaceAppearanceFunctionsCommand);
            ValidateSurfaceAppearanceFunctionsCommand = nullptr;
        }

        if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
        {
            FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            PropertyEditorModule.UnregisterCustomClassLayout(TEXT("DynamicWetClothesComponent"));
            PropertyEditorModule.UnregisterCustomClassLayout(TEXT("WetnessProfile"));
            PropertyEditorModule.NotifyCustomizationModuleChanged();
        }

        FDWCEditorStyle::Shutdown();
    }

  private:
    void ValidateSurfaceAppearanceFunctions()
    {
        FString ErrorMessage;
        if (!FWCAMaterialGenerator::ValidateSurfaceAppearanceFunctions(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_EvaluateSurfaceAppearance validation failed:\n%s"), *ErrorMessage);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_EvaluateSurfaceAppearance satisfies the DWC runtime material-function contract."));
    }


    IConsoleObject* ValidateSurfaceAppearanceFunctionsCommand = nullptr;

};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
