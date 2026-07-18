#include "DWCEditorUtils.h"

#include "FileHelpers.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
    FOnDWCEditorAssetSaved GDWCEditorAssetSaved;
    FOnDWCEditorAssetSaveAttemptFinished GDWCEditorAssetSaveAttemptFinished;

    void ShowDWCEditorNotification(const FText& Message, const SNotificationItem::ECompletionState CompletionState)
    {
        FNotificationInfo Info(Message);
        Info.ExpireDuration = 3.0f;
        Info.bUseSuccessFailIcons = true;

        if (TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info))
        {
            Notification->SetCompletionState(CompletionState);
        }
    }
}

FOnDWCEditorAssetSaved& DWCEditorUtils::OnAssetSaved()
{
    return GDWCEditorAssetSaved;
}

FOnDWCEditorAssetSaveAttemptFinished& DWCEditorUtils::OnAssetSaveAttemptFinished()
{
    return GDWCEditorAssetSaveAttemptFinished;
}

bool DWCEditorUtils::SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    UWetClothingAsset* WetClothingAsset = Cast<UWetClothingAsset>(Asset);
    if (WetClothingAsset != nullptr)
    {
        WetClothingAsset->BeginRuntimeDataEditorSaveAttempt();

        FString RuntimePreparationError;
        if (WetClothingAsset->CanPrepareRuntimeDataForEditorSave())
        {
            if (!WetClothingAsset->PrepareRuntimeDataForEditorSave(&RuntimePreparationError))
            {
                WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(false);
                ShowDWCEditorNotification(
                    FText::FromString(RuntimePreparationError.IsEmpty()
                        ? TEXT("Failed to prepare DWC precomputed simulation data for save.")
                        : RuntimePreparationError),
                    SNotificationItem::CS_Fail);
                GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, false);
                return false;
            }
        }
    }

    TUniquePtr<FScopedSlowTask> SaveSlowTask;
    if (WetClothingAsset != nullptr)
    {
        SaveSlowTask = MakeUnique<FScopedSlowTask>(
            2.0f,
            FText::FromString(FString::Printf(TEXT("Saving %s..."), *GetNameSafe(WetClothingAsset))));
        SaveSlowTask->MakeDialog(false);
        SaveSlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(TEXT("Collecting asset packages to save...")));
    }

    UPackage* Package = Asset->GetOutermost();
    if (Package == nullptr)
    {
        if (WetClothingAsset != nullptr)
        {
            WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(false);
        }
        GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, false);
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    if (WetClothingAsset != nullptr)
    {
        const auto AddDirtyGeneratedPackage = [&PackagesToSave, Package](UObject* GeneratedObject)
        {
            if (GeneratedObject == nullptr)
            {
                return;
            }

            UPackage* GeneratedPackage = GeneratedObject->GetOutermost();
            if (GeneratedPackage != nullptr && GeneratedPackage != Package && GeneratedPackage->IsDirty())
            {
                PackagesToSave.AddUnique(GeneratedPackage);
            }
        };

        AddDirtyGeneratedPackage(WetClothingAsset->GetRuntimeSkeletalMesh());
        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
             WetClothingAsset->PartData.GeneratedWetMaterialOverrides)
        {
            AddDirtyGeneratedPackage(MaterialOverride.GeneratedMaterial.Get());
            AddDirtyGeneratedPackage(MaterialOverride.CPUMaterialInstance.Get());
            AddDirtyGeneratedPackage(MaterialOverride.GPUMaterialInstance.Get());
        }

        // A Bake Maps action is a complete persistence operation. Save every dirty
        // generated package referenced by the WCA together with the WCA package so
        // a successful bake never leaves a misleading "not saved yet" state.
        for (const FWetClothingBakedWetnessProfileMap& ProfileMap :
             WetClothingAsset->PartData.BakedWetnessProfileMaps)
        {
            AddDirtyGeneratedPackage(ProfileMap.WetnessProfileMap0.Get());
        }

        for (const FWetWrinkleBakedMapSet& WrinkleMap :
             WetClothingAsset->WrinkleData.BakedWrinkleMaps)
        {
            AddDirtyGeneratedPackage(WrinkleMap.BakedWrinkleNormalMap.Get());
            AddDirtyGeneratedPackage(WrinkleMap.BakedWrinkleMask.Get());
        }

        for (const FWetClothingTransparencyLayerData& TransparencyLayer :
             WetClothingAsset->TransparencyData.TransparencyLayers)
        {
            for (const FWetClothingBakedTransparencyMap& TransparencyMap : TransparencyLayer.BakedMaps)
            {
                AddDirtyGeneratedPackage(TransparencyMap.TransparencyMap.Get());
            }
        }

        for (const FWetClothingBakedTransparencyRevealLayer& RevealLayer :
             WetClothingAsset->TransparencyData.BakedRevealLayers)
        {
            AddDirtyGeneratedPackage(RevealLayer.LookupMap.Get());
            AddDirtyGeneratedPackage(RevealLayer.ColorMap.Get());
            AddDirtyGeneratedPackage(RevealLayer.MaskMap.Get());
            AddDirtyGeneratedPackage(RevealLayer.ConfidenceMap.Get());
            AddDirtyGeneratedPackage(RevealLayer.RevealMaterial.Get());
        }
    }

    if (SaveSlowTask.IsValid())
    {
        SaveSlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(TEXT("Saving Wet Clothing Asset packages...")));
    }

    const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
    if (WetClothingAsset != nullptr)
    {
        WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(bSaved);
    }
    if (bSaved)
    {
        if (WetClothingAsset != nullptr)
        {
            WetClothingAsset->RefreshBakeState(false);
            ShowDWCEditorNotification(
                FText::FromString(TEXT("Wet Clothing Asset saved.")),
                SNotificationItem::CS_Success);
        }
        GDWCEditorAssetSaved.Broadcast(Asset);
    }
    GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, bSaved);
    return bSaved;
}
