// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Core/DWCEditorStyle.h"
#include "Utility/DWCLog.h"
#include "Core/DWCGeneratedAssetRelocator.h"
#include "Components/DynamicWetClothesComponentCustomization.h"
#include "DataAssets/WetnessProfile.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"
#include "UObject/UnrealType.h"

class FDWCEditorModule : public IModuleInterface
{
  public:
    virtual void StartupModule() override
    {
        FDWCEditorStyle::Initialize();
        FDWCEditorAuthoringPayloadDiagnostics::Initialize();
        FWetClothingTextureReadbackUtils::InitializeResourceBroker();

        FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        PropertyEditorModule.RegisterCustomClassLayout(
            TEXT("DynamicWetClothesComponent"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FDynamicWetClothesComponentCustomization::MakeInstance));
        PropertyEditorModule.RegisterCustomClassLayout(
            TEXT("WetnessProfile"),
            FOnGetDetailCustomizationInstance::CreateStatic(&FWetnessProfileDetailsCustomization::MakeInstance));
        PropertyEditorModule.NotifyCustomizationModuleChanged();

        FDWCGeneratedAssetRelocator::RegisterContentBrowserMenu(this);
        ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
            this,
            &FDWCEditorModule::HandleObjectPropertyChanged);
    }

    virtual void ShutdownModule() override
    {
        FDWCEditorAuthoringPayloadDiagnostics::Shutdown();
        FWetClothingTextureReadbackUtils::ShutdownResourceBroker();

        if (ObjectPropertyChangedHandle.IsValid())
        {
            FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
            ObjectPropertyChangedHandle.Reset();
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
        PreparedProfile.SetSourceProfilePath(FSoftObjectPath(&WetnessProfile));
        PreparedProfile.Parameters = WetnessProfile.GetParameters();
        FString ErrorMessage;
        if (!FWetClothingSurfaceTextureNormalizer::PrepareProfileTextures(
                WetnessProfile.GetParameters(),
                PreparedProfile,
                ErrorMessage))
        {
            UE_LOG(
                LogDWC,
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
        UObject*               ObjectBeingModified,
        FPropertyChangedEvent& PropertyChangedEvent)
    {
        if (ObjectBeingModified == nullptr ||
            ObjectBeingModified->HasAnyFlags(RF_ClassDefaultObject) ||
            (GEditor != nullptr && GEditor->PlayWorld != nullptr))
        {
            return;
        }

        if (USkeletalMesh* ChangedMesh = Cast<USkeletalMesh>(ObjectBeingModified))
        {
            // Covers direct editor changes and most reimport paths so a cached UV view
            // can never survive a mesh topology change outside the WCA editor.
            FWCAGeneratedDataInvalidator::InvalidateMesh(ChangedMesh);
        }
        else if (UWetClothingAsset* ChangedAsset = Cast<UWetClothingAsset>(ObjectBeingModified))
        {
            FWCAGeneratedDataInvalidator::NotifyAssetChanged(*ChangedAsset);
        }

        UWetnessProfile* WetnessProfile = Cast<UWetnessProfile>(ObjectBeingModified);
        if (WetnessProfile == nullptr)
        {
            return;
        }

        const FName PropertyName = PropertyChangedEvent.GetPropertyName();
        const bool  bTextureReferenceChanged =
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

    FDelegateHandle ObjectPropertyChangedHandle;
};

IMPLEMENT_MODULE(FDWCEditorModule, DWCEditor)
