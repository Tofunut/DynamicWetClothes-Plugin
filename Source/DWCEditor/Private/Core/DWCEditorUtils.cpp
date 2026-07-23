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

    const double SaveStartTime = FPlatformTime::Seconds();

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
    const double RuntimePreparationEndTime = FPlatformTime::Seconds();

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
             WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
        {
            AddDirtyGeneratedPackage(MaterialOverride.GeneratedMaterial.Get());
            AddDirtyGeneratedPackage(MaterialOverride.CPUMaterialInstance.Get());
            AddDirtyGeneratedPackage(MaterialOverride.GPUMaterialInstance.Get());
        }

        for (const FWetClothingBakedWetnessProfileMap& ProfileMap :
             WetClothingAsset->Derived.Inline.BakedWetnessProfileMaps)
        {
            AddDirtyGeneratedPackage(ProfileMap.WetnessProfileMap0.Get());
        }

        for (const FWetWrinkleBakedMapSet& WrinkleMap :
             WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps)
        {
            AddDirtyGeneratedPackage(WrinkleMap.BakedWrinkleNormalMap.Get());
            AddDirtyGeneratedPackage(WrinkleMap.BakedWrinkleMask.Get());
        }

        for (const FWetClothingTransparencyLayerData& TransparencyLayer :
             WetClothingAsset->Authored.TransparencyData.TransparencyLayers)
        {
            for (const FWetClothingBakedTransparencyMap& TransparencyMap : TransparencyLayer.BakedMaps)
            {
                AddDirtyGeneratedPackage(TransparencyMap.TransparencyMap.Get());
            }
        }

    }
    const double PackageCollectionEndTime = FPlatformTime::Seconds();

    if (SaveSlowTask.IsValid())
    {
        SaveSlowTask->EnterProgressFrame(
            1.0f,
            FText::FromString(TEXT("Saving Wet Clothing Asset packages...")));
    }

    const bool bSaved = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
    const double PackageSaveEndTime = FPlatformTime::Seconds();
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
    const double SaveEndTime = FPlatformTime::Seconds();
    UE_LOG(
        LogTemp,
        Display,
        TEXT("DWC editor save for '%s' %s in %.1f ms across %d package(s) "
             "(runtime preparation %.1f, package collection %.1f, package save %.1f, completion callbacks %.1f)."),
        *GetNameSafe(Asset),
        bSaved ? TEXT("completed") : TEXT("failed"),
        (SaveEndTime - SaveStartTime) * 1000.0,
        PackagesToSave.Num(),
        (RuntimePreparationEndTime - SaveStartTime) * 1000.0,
        (PackageCollectionEndTime - RuntimePreparationEndTime) * 1000.0,
        (PackageSaveEndTime - PackageCollectionEndTime) * 1000.0,
        (SaveEndTime - PackageSaveEndTime) * 1000.0);
    return bSaved;
}
