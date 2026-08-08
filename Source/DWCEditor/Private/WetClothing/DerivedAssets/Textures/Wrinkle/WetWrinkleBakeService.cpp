// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

namespace
{
    void CollectAuthoredWrinkleMaterialSlots(const UWetClothingAsset& Asset, TSet<int32>& OutMaterialSlots)
    {
        OutMaterialSlots.Reset();

        const FWetClothingWrinkleData& WrinkleData = Asset.Authored.WrinkleData;
        for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
        {
            if ((!Patch.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Patch.MaterialSlotIndex == INDEX_NONE ||
                !Asset.IsMaterialSlotWettable(Patch.MaterialSlotIndex))
            {
                continue;
            }

            if (WrinkleData.IsUsingCustomWrinkleNormalMap(Patch.MaterialSlotIndex))
            {
                continue;
            }

            OutMaterialSlots.Add(Patch.MaterialSlotIndex);
        }

        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches) ||
                Stroke.MaterialSlotIndex == INDEX_NONE ||
                Stroke.Points.Num() < 2 ||
                !Asset.IsMaterialSlotWettable(Stroke.MaterialSlotIndex))
            {
                continue;
            }

            if (WrinkleData.IsUsingCustomWrinkleNormalMap(Stroke.MaterialSlotIndex))
            {
                continue;
            }

            OutMaterialSlots.Add(Stroke.MaterialSlotIndex);
        }
    }

    bool HasExactBakedWrinkleMap(const UWetClothingAsset& Asset, const int32 MaterialSlotIndex)
    {
        return FWetWrinkleNormalMapBaker::IsMaterialSlotBakeCurrent(
            &Asset,
            MaterialSlotIndex);
    }
} // namespace

void FWetWrinkleBakeService::CollectBakeMaterialSlots(
    const UWetClothingAsset& WetClothingAsset,
    TArray<int32>&           OutMaterialSlots)
{
    TSet<int32> MaterialSlots;
    CollectAuthoredWrinkleMaterialSlots(WetClothingAsset, MaterialSlots);
    OutMaterialSlots = MaterialSlots.Array();
    OutMaterialSlots.Sort();
}

void FWetWrinkleBakeService::RefreshBakeStatusFromCurrentOutputs(
    UWetClothingAsset* WetClothingAsset,
    const FString&     Failure)
{
    if (WetClothingAsset == nullptr)
    {
        return;
    }

    if (!Failure.IsEmpty())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Failed, Failure);
        return;
    }

    TSet<int32> AuthoredMaterialSlots;
    CollectAuthoredWrinkleMaterialSlots(*WetClothingAsset, AuthoredMaterialSlots);
    if (AuthoredMaterialSlots.IsEmpty())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Disabled);
        return;
    }

    int32 CompletedSlotCount = 0;
    for (const int32 MaterialSlotIndex : AuthoredMaterialSlots)
    {
        CompletedSlotCount += HasExactBakedWrinkleMap(*WetClothingAsset, MaterialSlotIndex) ? 1 : 0;
    }

    if (CompletedSlotCount == AuthoredMaterialSlots.Num())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Valid);
        return;
    }

    if (CompletedSlotCount > 0 || !WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.IsEmpty())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::OutOfDate);
        WetClothingAsset->MarkBakeOutputGenerated(DWCBakeOutput::WrinkleMaps);
        return;
    }

    WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Required);
}

bool FWetWrinkleBakeService::BakeAllWrinkleMaps(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    if (WetClothingAsset == nullptr)
    {
        OutSummary = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    TSet<int32> AuthoredMaterialSlots;
    CollectAuthoredWrinkleMaterialSlots(*WetClothingAsset, AuthoredMaterialSlots);

    if (AuthoredMaterialSlots.IsEmpty())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Disabled);
        OutSummary = TEXT("No authored wrinkle patches or procedural ridge strokes require baking.");
        return true;
    }

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = WetClothingAsset->Authored.WrinkleData.BakeSettings.DefaultResolution;
    Settings.PaddingPixels = WetClothingAsset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatches = WetClothingAsset->Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches;

    int32           TotalMapCount = 0;
    int32           TotalStampCount = 0;
    int32           TotalProceduralStrokeCount = 0;
    TArray<FString> Failures;
    TArray<int32>   SortedSlots = AuthoredMaterialSlots.Array();
    SortedSlots.Sort();
    FWetWrinkleNormalMapBakeSession BakeSession;

    for (const int32 MaterialSlotIndex : SortedSlots)
    {
        FWetWrinkleNormalMapBakeResult Result;
        FString                        ErrorMessage;
        if (!FWetWrinkleNormalMapBaker::BakeMaterialSlot(
                WetClothingAsset,
                MaterialSlotIndex,
                Settings,
                BakeSession,
                Result,
                ErrorMessage))
        {
            Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *ErrorMessage));
            continue;
        }
        TotalMapCount += Result.BakedMapCount;
        TotalStampCount += Result.BakedStampCount;
        TotalProceduralStrokeCount += Result.BakedProceduralStrokeCount;
    }

    if (!Failures.IsEmpty())
    {
        OutSummary = FString::Printf(
            TEXT("Wrinkle bake completed with failures.\n\nBaked map sets: %d\nBaked patches: %d\n\n%s"),
            TotalMapCount,
            TotalStampCount,
            *FString::Join(Failures, TEXT("\n")));
        RefreshBakeStatusFromCurrentOutputs(WetClothingAsset, OutSummary);
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = true;
        }
        return false;
    }

    WetClothingAsset->Modify();
    RefreshBakeStatusFromCurrentOutputs(WetClothingAsset);
    OutSummary = FString::Printf(
        TEXT("Baked %d wrinkle map set(s) from %d patch(es) and %d procedural ridge stroke(s) across %d material slot(s).\n"
             "Each set contains a tangent-space normal texture and a separate grayscale separation mask."),
        TotalMapCount,
        TotalStampCount,
        TotalProceduralStrokeCount,
        SortedSlots.Num());
    return true;
}
