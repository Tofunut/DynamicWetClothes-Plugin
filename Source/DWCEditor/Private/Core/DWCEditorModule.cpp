#include "Core/DWCEditorStyle.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
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

        RepairApplyWetnessFunctionCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.RepairApplyWetnessFunction"),
            TEXT("Explicitly rebuilds and saves MF_DWC_ApplyWetness for the current DWCEditor contract."),
            FConsoleCommandDelegate::CreateRaw(this, &FDWCEditorModule::RepairApplyWetnessFunction),
            ECVF_Default);
        ValidateApplyWetnessFunctionCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.ValidateApplyWetnessFunction"),
            TEXT("Validates the fixed MF_DWC_ApplyWetness asset without modifying it."),
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

        FDWCEditorStyle::Shutdown();
    }

  private:
    void ValidateApplyWetnessFunction()
    {
        FString ErrorMessage;
        if (!FWetClothingMaterialSetup::ValidateSharedApplyWetnessFunction(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_ApplyWetness fixed asset validation failed:\n%s"), *ErrorMessage);
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_ApplyWetness satisfies the fixed DWC material-function contract."));
    }

    void RepairApplyWetnessFunction()
    {
        static const FString ApplyWetnessAssetPath = TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_ApplyWetness");

        FString ErrorMessage;
        if (!FWetClothingMaterialSetup::RepairOrUpgradeSharedApplyWetnessFunction(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_ApplyWetness repair/upgrade failed:\n%s"), *ErrorMessage);
            if (!FApp::IsUnattended())
            {
                FMessageDialog::Open(
                    EAppMsgCategory::Error,
                    EAppMsgType::Ok,
                    FText::FromString(TEXT("MF_DWC_ApplyWetness repair/upgrade failed.\n\n") + ErrorMessage));
            }
            return;
        }

        UEditorAssetSubsystem* AssetSubsystem = GEditor != nullptr
                                                    ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()
                                                    : nullptr;
        if (AssetSubsystem == nullptr || !AssetSubsystem->SaveAsset(ApplyWetnessAssetPath, false))
        {
            ErrorMessage = FString::Printf(
                TEXT("MF_DWC_ApplyWetness was rebuilt but '%s' could not be saved. Check source-control checkout and file permissions."),
                *ApplyWetnessAssetPath);
            UE_LOG(LogTemp, Error, TEXT("%s"), *ErrorMessage);
            if (!FApp::IsUnattended())
            {
                FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, FText::FromString(ErrorMessage));
            }
            return;
        }

        UE_LOG(LogTemp, Display, TEXT("MF_DWC_ApplyWetness was repaired/upgraded, validated, and saved as the fixed shared material function."));
        if (!FApp::IsUnattended())
        {
            FMessageDialog::Open(
                EAppMsgCategory::Success,
                EAppMsgType::Ok,
                FText::FromString(TEXT("MF_DWC_ApplyWetness was repaired/upgraded, validated, and saved.")));
        }
    }

    TSharedPtr<IAssetTypeActions> WetWrinklePresetAssetTypeActions;
    IConsoleObject*               RepairApplyWetnessFunctionCommand = nullptr;
    IConsoleObject*               ValidateApplyWetnessFunctionCommand = nullptr;
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
