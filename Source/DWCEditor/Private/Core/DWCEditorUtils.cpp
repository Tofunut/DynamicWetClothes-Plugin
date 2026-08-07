//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DWCEditorUtils.h"

#include "FileHelpers.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
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

    bool HasRuntimeDataIssueForSave(const UWetClothingAsset& WetClothingAsset)
    {
        const FDWCWetClothingAssetSetupSettings& Setup = WetClothingAsset.GetSetupSettings();
        const FDWCAssetBakeState& BakeState = WetClothingAsset.GetBakeState();
        return WetClothingAsset.HasAnyWettableMaterialSlot() &&
               ((Setup.bBuildCPUVertexSimulationData &&
                 (!DWCBuildStatus::IsUsable(BakeState.CPURuntimeData) ||
                  WetClothingAsset.IsBakeOutputSavePending(DWCBakeOutput::CPURuntimeData))) ||
                (Setup.bBuildGPUWetnessMapSimulationData &&
                 (!DWCBuildStatus::IsUsable(BakeState.GPURuntimeData) ||
                  WetClothingAsset.IsBakeOutputSavePending(DWCBakeOutput::GPURuntimeData))));
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

bool DWCEditorUtils::SaveAsset(UObject* Asset, const bool bPrepareRuntimeData)
{
    if (Asset == nullptr)
    {
        return false;
    }

    UWetClothingAsset* WetClothingAsset = Cast<UWetClothingAsset>(Asset);
    if (WetClothingAsset != nullptr)
    {
        WetClothingAsset->BeginRuntimeDataEditorSaveAttempt();

        if (bPrepareRuntimeData)
        {
            FString RuntimePreparationError;
            if (WetClothingAsset->CanPrepareRuntimeDataForEditorSave(&RuntimePreparationError))
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
            else if (HasRuntimeDataIssueForSave(*WetClothingAsset))
            {
                WetClothingAsset->CompleteRuntimeDataEditorSaveAttempt(false);
                ShowDWCEditorNotification(
                    FText::FromString(RuntimePreparationError.IsEmpty()
                        ? TEXT("DWC runtime data cannot be prepared for save.")
                        : RuntimePreparationError),
                    SNotificationItem::CS_Fail);
                GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, false);
                return false;
            }
        }
        else
        {
            // A targeted runtime-data command has already rebuilt the selected segment.
            // Preserve the other segment exactly as-is instead of running the aggregate
            // pre-save rebuild and unexpectedly completing unrelated work.
            WetClothingAsset->SkipNextRuntimeDataPreSaveRebuild();
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
             WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
        {
            AddDirtyGeneratedPackage(MaterialOverride.GeneratedMaterial.Get());
            AddDirtyGeneratedPackage(MaterialOverride.GeneratedMaterialInstance.Get());
        }

        for (const FWetClothingBakedWetPartDataSlotTexture& SlotTexture :
             WetClothingAsset->Derived.Inline.BakedWetPartData.SlotTextures)
        {
            AddDirtyGeneratedPackage(SlotTexture.WetPartDataTexture.Get());
        }
        // Local render profiles reference authored Surface Water textures directly.
        // Do not treat those source texture packages as generated DWC outputs.
        AddDirtyGeneratedPackage(WetClothingAsset->Derived.Inline.BakedWetPartData.NormalizedNeutralSurfaceNormal.Get());

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

#if WITH_EDITORONLY_DATA
        AddDirtyGeneratedPackage(WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction.Get());
#endif

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
        WetClothingAsset->RefreshBakeState(false);
        if (bSaved)
        {
            WetClothingAsset->ReleaseLoadedRuntimeBulkPayloadForEditor();
        }
    }
    if (bSaved)
    {
        if (WetClothingAsset != nullptr)
        {
            ShowDWCEditorNotification(
                FText::FromString(TEXT("Wet Clothing Asset saved.")),
                SNotificationItem::CS_Success);
        }
        GDWCEditorAssetSaved.Broadcast(Asset);
    }
    GDWCEditorAssetSaveAttemptFinished.Broadcast(Asset, bSaved);
    return bSaved;
}
