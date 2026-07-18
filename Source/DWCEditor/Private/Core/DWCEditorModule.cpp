#include "Core/DWCEditorStyle.h"
#include "AssetToolsModule.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"
#include "WetClothing/WrinkleEdit/Preset/WetWrinklePresetAssetTypeActions.h"

class FDWCEditorModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        FDWCEditorStyle::Initialize();

        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        WetWrinklePresetAssetTypeActions = MakeShared<FWetWrinklePresetAssetTypeActions>();
        AssetTools.RegisterAssetTypeActions(WetWrinklePresetAssetTypeActions.ToSharedRef());

        FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyEditorModule.RegisterCustomClassLayout(
            TEXT("DynamicWetClothesComponent"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FDynamicWetClothesComponentCustomization::MakeInstance));
        PropertyEditorModule.NotifyCustomizationModuleChanged();

        RepairApplyWetnessFunctionCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.RepairApplyWetnessFunction"),
            TEXT("Validates and saves MF_DWC_ApplyWetness_CPU/GPU for the current DWCEditor contract."),
            FConsoleCommandDelegate::CreateRaw(this, &FDWCEditorModule::RepairApplyWetnessFunction),
            ECVF_Default);
        ValidateApplyWetnessFunctionCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.ValidateApplyWetnessFunction"),
            TEXT("Validates the fixed MF_DWC_ApplyWetness_CPU/GPU assets without modifying them."),
            FConsoleCommandDelegate::CreateRaw(this, &FDWCEditorModule::ValidateApplyWetnessFunction),
            ECVF_Default);
    }

    virtual void ShutdownModule() override
    {
        if (RepairApplyWetnessFunctionCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(RepairApplyWetnessFunctionCommand);
            RepairApplyWetnessFunctionCommand = nullptr;
        }
        if (ValidateApplyWetnessFunctionCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ValidateApplyWetnessFunctionCommand);
            ValidateApplyWetnessFunctionCommand = nullptr;
        }

        if (WetWrinklePresetAssetTypeActions.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
        {
            IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
            AssetTools.UnregisterAssetTypeActions(WetWrinklePresetAssetTypeActions.ToSharedRef());
            WetWrinklePresetAssetTypeActions.Reset();
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
        if (!FWetClothingMaterialSetup::ValidateSharedApplyWetnessFunction(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_ApplyWetness_CPU/GPU fixed asset validation failed:\n%s"), *ErrorMessage);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_ApplyWetness_CPU/GPU satisfy the fixed DWC material-function contract."));
    }

    void RepairApplyWetnessFunction()
    {
        static const FString ApplyWetnessCPUAssetPath = TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_ApplyWetness_CPU");
        static const FString ApplyWetnessGPUAssetPath = TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_ApplyWetness_GPU");

        UE_LOG(LogTemp, Display, TEXT("DWC material function repair started."));

        FString ErrorMessage;
        if (!FWetClothingMaterialSetup::RepairOrUpgradeSharedApplyWetnessFunction(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_ApplyWetness_CPU/GPU repair/upgrade failed:\n%s"), *ErrorMessage);
            if (!FApp::IsUnattended())
            {
                FMessageDialog::Open(
                    EAppMsgCategory::Error,
                    EAppMsgType::Ok,
                    FText::FromString(TEXT("MF_DWC_ApplyWetness_CPU/GPU repair/upgrade failed.\n\n") + ErrorMessage));
            }
            return;
        }

        UEditorAssetSubsystem* AssetSubsystem = GEditor != nullptr
                                                    ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                                                    : nullptr;
        if (AssetSubsystem == nullptr ||
            !AssetSubsystem->SaveAsset(ApplyWetnessCPUAssetPath, false) ||
            !AssetSubsystem->SaveAsset(ApplyWetnessGPUAssetPath, false))
        {
            ErrorMessage = FString::Printf(
                TEXT("DWC material functions validated but one or more assets could not be saved. Check source-control checkout and file permissions. CPU='%s' GPU='%s'."),
                *ApplyWetnessCPUAssetPath,
                *ApplyWetnessGPUAssetPath);
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
            if (!FApp::IsUnattended())
            {
                FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, FText::FromString(ErrorMessage));
            }
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_ApplyWetness_CPU/GPU were validated and saved as the fixed material functions."));
        if (!FApp::IsUnattended())
        {
            FMessageDialog::Open(
                EAppMsgCategory::Success,
                EAppMsgType::Ok,
                FText::FromString(TEXT("MF_DWC_ApplyWetness_CPU/GPU were validated and saved.")));
        }
    }

    TSharedPtr<IAssetTypeActions> WetWrinklePresetAssetTypeActions;
    IConsoleObject*               RepairApplyWetnessFunctionCommand = nullptr;
    IConsoleObject*               ValidateApplyWetnessFunctionCommand = nullptr;
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
