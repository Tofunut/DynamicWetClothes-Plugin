#include "Core/DWCEditorStyle.h"
#include "Core/DWCSkeletalMeshMaterialSlotExtractor.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "DataAssets/WetnessProfile.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"
#include "UObject/UnrealType.h"

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

        DumpEditorPreviewStatsCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.EditorPreview.DumpStats"),
            TEXT("Dumps memory and cache statistics for active DWC editor preview sessions."),
            FConsoleCommandDelegate::CreateStatic(&FDWCEditorPreviewDiagnostics::DumpAllSessions),
            ECVF_Default);

        ResetEditorPreviewStatsCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("DWC.EditorPreview.ResetStats"),
            TEXT("Resets counters for active DWC editor preview sessions without clearing their caches."),
            FConsoleCommandDelegate::CreateStatic(&FDWCEditorPreviewDiagnostics::ResetAllCounters),
            ECVF_Default);

        FDWCSkeletalMeshMaterialSlotExtractor::RegisterContentBrowserMenu(this);
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDWCEditorModule::HandleObjectPropertyChanged);
    }

    virtual void ShutdownModule() override
    {
        if (ObjectPropertyChangedHandle.IsValid())
        {
            FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
            ObjectPropertyChangedHandle.Reset();
        }

        FDWCSkeletalMeshMaterialSlotExtractor::UnregisterContentBrowserMenu(this);

        if (ResetEditorPreviewStatsCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(ResetEditorPreviewStatsCommand);
            ResetEditorPreviewStatsCommand = nullptr;
        }

        if (DumpEditorPreviewStatsCommand != nullptr)
        {
            IConsoleManager::Get().UnregisterConsoleObject(DumpEditorPreviewStatsCommand);
            DumpEditorPreviewStatsCommand = nullptr;
        }

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
    void PrepareWetnessProfileTextures(UWetnessProfile& WetnessProfile)
    {
        FWetClothingLocalRenderProfile PreparedProfile;
        PreparedProfile.SourceProfile = FSoftObjectPath(&WetnessProfile);
        PreparedProfile.Parameters = WetnessProfile.GetParameters();
        FString ErrorMessage;
        if (!FWetClothingSurfaceTextureNormalizer::PrepareProfileTextures(
                WetnessProfile.GetParameters(),
                PreparedProfile,
                ErrorMessage))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC could not prepare changed WP textures: profile='%s', error='%s'."),
                *WetnessProfile.GetPathName(),
                *ErrorMessage);
            return;
        }

        WetnessProfile.SetPreparedSurfaceTextures(
            PreparedProfile.NormalizedDropletNormal,
            PreparedProfile.NormalizedDropletMask,
            PreparedProfile.NormalizedDropletFlowNormal,
            PreparedProfile.NormalizedDropletFlowMask);
        WetnessProfile.MarkPackageDirty();
    }

    void HandleObjectPropertyChanged(
        UObject* ObjectBeingModified,
        FPropertyChangedEvent& PropertyChangedEvent)
    {
        UWetnessProfile* WetnessProfile = Cast<UWetnessProfile>(ObjectBeingModified);
        if (WetnessProfile == nullptr || WetnessProfile->HasAnyFlags(RF_ClassDefaultObject) ||
            (GEditor != nullptr && GEditor->PlayWorld != nullptr))
        {
            return;
        }

        const FName PropertyName = PropertyChangedEvent.GetPropertyName();
        const bool bTextureReferenceChanged =
            PropertyName.IsNone() ||
            PropertyName == GET_MEMBER_NAME_CHECKED(FSurfaceWaterProfileParameters, DropletNormalTexture) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(FSurfaceWaterProfileParameters, DropletMaskTexture) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(FSurfaceWaterProfileParameters, DropletFlowNormalTexture) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(FSurfaceWaterProfileParameters, DropletFlowMaskTexture);
        if (!bTextureReferenceChanged)
        {
            return;
        }

        PrepareWetnessProfileTextures(*WetnessProfile);
    }

    void ValidateSurfaceAppearanceFunctions()
    {
        FString ErrorMessage;
        if (!FWCAMaterialGenerator::ValidateSurfaceAppearanceFunctions(ErrorMessage))
        {
            UE_LOG(LogTemp, Error, TEXT("MF_DWC_EvaluateSurfaceAppearance validation failed:\n%s"), *ErrorMessage);
            return;
        }

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
    IConsoleObject* DumpEditorPreviewStatsCommand = nullptr;
    IConsoleObject* ResetEditorPreviewStatsCommand = nullptr;
    FDelegateHandle ObjectPropertyChangedHandle;

};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
