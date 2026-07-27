#include "Core/DWCEditorStyle.h"
#include "Core/DWCSkeletalMeshMaterialSlotExtractor.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "Engine/SkeletalMesh.h"
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

        ExtractSkeletalMeshMaterialSlotCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.ExtractSkeletalMeshMaterialSlot"),
            TEXT("Creates a skeletal mesh asset containing only one material slot. Args: <SkeletalMeshPath> <MaterialSlotIndex> [OutputPackagePath]"),
            FConsoleCommandWithArgsDelegate::CreateRaw(this, &FDWCEditorModule::ExtractSkeletalMeshMaterialSlot),
            ECVF_Default);

        FDWCSkeletalMeshMaterialSlotExtractor::RegisterContentBrowserMenu(this);
    }

    virtual void ShutdownModule() override
    {
        FDWCSkeletalMeshMaterialSlotExtractor::UnregisterContentBrowserMenu(this);

        if (ExtractSkeletalMeshMaterialSlotCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ExtractSkeletalMeshMaterialSlotCommand);
            ExtractSkeletalMeshMaterialSlotCommand = nullptr;
        }

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

    void ExtractSkeletalMeshMaterialSlot(const TArray<FString>& Args)
    {
        if (Args.Num() < 2)
        {
            UE_LOG(LogTemp, Error, TEXT("Usage: DWC.ExtractSkeletalMeshMaterialSlot <SkeletalMeshPath> <MaterialSlotIndex> [OutputPackagePath]"));
            return;
        }

        USkeletalMesh* SourceMesh = LoadObject<USkeletalMesh>(nullptr, *Args[0]);
        if (SourceMesh == nullptr)
        {
            UE_LOG(LogTemp, Error, TEXT("Could not load skeletal mesh: %s"), *Args[0]);
            return;
        }

        const int32 MaterialSlotIndex = FCString::Atoi(*Args[1]);
        const FString OutputPackageName = Args.IsValidIndex(2) ? Args[2] : FString();

        const FDWCSkeletalMeshMaterialSlotExtractionResult Result =
            FDWCSkeletalMeshMaterialSlotExtractor::ExtractMaterialSlot(SourceMesh, MaterialSlotIndex, OutputPackageName);
        if (Result.bSucceeded)
        {
            UE_LOG(LogTemp, Display, TEXT("%s"), *Result.Message);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("%s"), *Result.Message);
        }
    }


    IConsoleObject* ValidateSurfaceAppearanceFunctionsCommand = nullptr;
    IConsoleObject* ExtractSkeletalMeshMaterialSlotCommand = nullptr;

};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
