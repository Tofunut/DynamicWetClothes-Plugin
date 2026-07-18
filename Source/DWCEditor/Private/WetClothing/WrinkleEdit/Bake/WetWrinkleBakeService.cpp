#include "WetClothing/WrinkleEdit/Bake/WetWrinkleBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/WrinkleEdit/Bake/WetWrinkleNormalMapBaker.h"

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
    for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset->WrinkleData.EditablePatchStrokes)
    {
        if (!Stroke.bEnabled && !WetClothingAsset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes)
        {
            continue;
        }

        for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
        {
            if (Patch.MaterialSlotIndex != INDEX_NONE && WetClothingAsset->IsMaterialSlotWettable(Patch.MaterialSlotIndex))
            {
                AuthoredMaterialSlots.Add(Patch.MaterialSlotIndex);
            }
        }
    }

    if (AuthoredMaterialSlots.IsEmpty())
    {
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Disabled);
        OutSummary = TEXT("No authored wrinkle patches require baking.");
        return true;
    }

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = WetClothingAsset->WrinkleData.BakeSettings.DefaultResolution;
    Settings.PaddingPixels = WetClothingAsset->WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatchStrokes = WetClothingAsset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes;
    Settings.bBakeNormalMap = WetClothingAsset->WrinkleData.BakeSettings.bBakeNormalMap;
    Settings.bBakeMask = WetClothingAsset->WrinkleData.BakeSettings.bBakeMask;

    int32 TotalMapCount = 0;
    int32 TotalStampCount = 0;
    TArray<FString> Failures;
    TArray<int32> SortedSlots = AuthoredMaterialSlots.Array();
    SortedSlots.Sort();

    for (const int32 MaterialSlotIndex : SortedSlots)
    {
        FWetWrinkleNormalMapBakeResult Result;
        FString ErrorMessage;
        if (!FWetWrinkleNormalMapBaker::BakeMaterialSlot(WetClothingAsset, MaterialSlotIndex, Settings, Result, ErrorMessage))
        {
            Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *ErrorMessage));
            continue;
        }
        TotalMapCount += Result.BakedMapCount;
        TotalStampCount += Result.BakedStampCount;
    }

    if (!Failures.IsEmpty())
    {
        OutSummary = FString::Printf(
            TEXT("Wrinkle bake completed with failures.\n\nBaked map sets: %d\nBaked patches: %d\n\n%s"),
            TotalMapCount,
            TotalStampCount,
            *FString::Join(Failures, TEXT("\n")));
        WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Failed, OutSummary);
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = true;
        }
        return false;
    }

    WetClothingAsset->Modify();
    WetClothingAsset->SetWrinkleBakeStatus(EDWCBakeStatus::Valid);
    OutSummary = FString::Printf(
        TEXT("Baked %d wrinkle map set(s) from %d patch(es) across %d material slot(s)."),
        TotalMapCount,
        TotalStampCount,
        SortedSlots.Num());
    return true;
}
