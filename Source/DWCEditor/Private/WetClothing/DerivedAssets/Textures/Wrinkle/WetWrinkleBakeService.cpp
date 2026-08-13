//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Modes/Wrinkle/Authoring/DWCEditorWrinkleTextureResolver.h"

namespace
{
    FWetWrinkleAuthoredSlotState& FindOrAddSlotState(
        TMap<int32, FWetWrinkleAuthoredSlotState>& States,
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex)
    {
        FWetWrinkleAuthoredSlotState& State = States.FindOrAdd(MaterialSlotIndex);
        State.MaterialSlotIndex = MaterialSlotIndex;
        State.bWettable = MaterialSlotIndex != INDEX_NONE &&
            Asset.IsMaterialSlotWettable(MaterialSlotIndex);
        State.bUsesCustomNormal = MaterialSlotIndex != INDEX_NONE &&
            Asset.Authored.WrinkleData.IsUsingCustomWrinkleNormalMap(MaterialSlotIndex);
        return State;
    }

    void CollectAuthoredWrinkleMaterialSlots(const UWetClothingAsset& Asset, TSet<int32>& OutMaterialSlots)
    {
        OutMaterialSlots.Reset();
        TArray<FWetWrinkleAuthoredSlotState> States;
        FWetWrinkleBakeService::CollectAuthoredSlotStates(Asset, States);
        for (const FWetWrinkleAuthoredSlotState& State : States)
        {
            if (State.MaterialSlotIndex != INDEX_NONE && State.bWettable &&
                !State.bUsesCustomNormal && State.HasBakeableContent())
            {
                OutMaterialSlots.Add(State.MaterialSlotIndex);
            }
        }
    }
}

void FWetWrinkleBakeService::CollectAuthoredSlotStates(
    const UWetClothingAsset& WetClothingAsset,
    TArray<FWetWrinkleAuthoredSlotState>& OutStates)
{
    TMap<int32, FWetWrinkleAuthoredSlotState> States;
    TMap<FSoftObjectPath, EDWCEditorWrinkleTextureResolveStatus> SourceStatusByPath;
    const FWetClothingWrinkleData& WrinkleData = WetClothingAsset.Authored.WrinkleData;

    for (const FWetWrinklePatchPlacement& Patch : WrinkleData.EditablePatches)
    {
        if (!Patch.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches)
        {
            continue;
        }
        FWetWrinkleAuthoredSlotState& State = FindOrAddSlotState(
            States, WetClothingAsset, Patch.MaterialSlotIndex);
        ++State.PatchCount;
        if (!Patch.HasWrinkleNormalTexture())
        {
            ++State.MissingPatchTextureCount;
            continue;
        }
        else
        {
            const FSoftObjectPath SourcePath = Patch.GetWrinkleNormalTexturePath();
            EDWCEditorWrinkleTextureResolveStatus* CachedStatus = SourceStatusByPath.Find(SourcePath);
            if (CachedStatus == nullptr)
            {
                const FDWCEditorWrinkleTextureReferenceSnapshot SourceReference =
                    FDWCEditorWrinkleTextureResolver::InspectSource(Patch);
                CachedStatus = &SourceStatusByPath.Add(SourcePath, SourceReference.Status);
            }
            if (*CachedStatus == EDWCEditorWrinkleTextureResolveStatus::Missing ||
                *CachedStatus == EDWCEditorWrinkleTextureResolveStatus::WrongType)
            {
                ++State.InvalidPatchTextureCount;
                continue;
            }
        }
        if (!Patch.HasValidSurfaceAnchor() || !Patch.HasValidSurfaceFrame() ||
            !Patch.HasValidSurfaceFootprint())
        {
            ++State.InvalidPatchPlacementCount;
        }
        else
        {
            ++State.ValidPatchCount;
        }
    }

    for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
    {
        if (!Stroke.bEnabled && !WrinkleData.BakeSettings.bIncludeDisabledPatches)
        {
            continue;
        }
        FWetWrinkleAuthoredSlotState& State = FindOrAddSlotState(
            States, WetClothingAsset, Stroke.MaterialSlotIndex);
        ++State.RidgeStrokeCount;
        if (Stroke.Points.Num() >= 2 && Stroke.WidthUV > 0.0f && Stroke.Strength > 0.0f)
        {
            ++State.ValidRidgeStrokeCount;
        }
        else
        {
            ++State.InvalidRidgeStrokeCount;
        }
    }

    OutStates.Reset(States.Num());
    States.GenerateValueArray(OutStates);
    OutStates.Sort([](const FWetWrinkleAuthoredSlotState& Left,
                      const FWetWrinkleAuthoredSlotState& Right)
    {
        return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
    });
}

void FWetWrinkleBakeService::CollectBakeMaterialSlots(
    const UWetClothingAsset& WetClothingAsset,
    TArray<int32>& OutMaterialSlots)
{
    TSet<int32> MaterialSlots;
    CollectAuthoredWrinkleMaterialSlots(WetClothingAsset, MaterialSlots);
    OutMaterialSlots = MaterialSlots.Array();
    OutMaterialSlots.Sort();
}

void FWetWrinkleBakeService::RefreshBakeStatusFromCurrentOutputs(
    UWetClothingAsset* WetClothingAsset,
    const FString& Failure)
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
        CompletedSlotCount += FWetWrinkleNormalMapBaker::IsMaterialSlotBakeCurrent(
            WetClothingAsset, MaterialSlotIndex) ? 1 : 0;
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

bool FWetWrinkleBakeService::BakeAllWrinkleMaps(
    UWetClothingAsset* WetClothingAsset,
    TSharedRef<FDWCEditorSpatialQueryService> SpatialQueryService,
    TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache,
    FString& OutSummary,
    bool* OutHadWarnings)
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
    Settings.Resolution = WetClothingAsset->GetWrinkleMapResolution();
    Settings.PaddingPixels = WetClothingAsset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatches = WetClothingAsset->Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches;

    int32 TotalMapCount = 0;
    int32 TotalStampCount = 0;
    int32 TotalProceduralStrokeCount = 0;
    TArray<FWetWrinkleInvalidatedTransparencyOutput> InvalidatedTransparencyOutputs;
    TArray<FString> Failures;
    TArray<int32> SortedSlots = AuthoredMaterialSlots.Array();
    SortedSlots.Sort();
    FWetWrinkleNormalMapBakeSession BakeSession(
        SpatialQueryService,
        SurfacePatchProjectionCache);

    for (const int32 MaterialSlotIndex : SortedSlots)
    {
        FWetWrinkleNormalMapBakeResult Result;
        FString ErrorMessage;
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
        for (FWetWrinkleInvalidatedTransparencyOutput& Invalidated :
             Result.InvalidatedTransparencyOutputs)
        {
            if (!InvalidatedTransparencyOutputs.ContainsByPredicate(
                    [&Invalidated](const FWetWrinkleInvalidatedTransparencyOutput& Existing)
                    {
                        return Existing.MaterialSlotIndex == Invalidated.MaterialSlotIndex;
                    }))
            {
                InvalidatedTransparencyOutputs.Add(MoveTemp(Invalidated));
            }
        }
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
    if (!InvalidatedTransparencyOutputs.IsEmpty())
    {
        InvalidatedTransparencyOutputs.Sort(
            [](const FWetWrinkleInvalidatedTransparencyOutput& Left,
               const FWetWrinkleInvalidatedTransparencyOutput& Right)
            {
                return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
            });
        TArray<FString> SlotDescriptions;
        SlotDescriptions.Reserve(InvalidatedTransparencyOutputs.Num());
        for (const FWetWrinkleInvalidatedTransparencyOutput& Invalidated : InvalidatedTransparencyOutputs)
        {
            SlotDescriptions.Add(Invalidated.MaterialSlotName.IsEmpty()
                ? FString::Printf(TEXT("Slot %d"), Invalidated.MaterialSlotIndex)
                : FString::Printf(
                    TEXT("%s (Slot %d)"),
                    *Invalidated.MaterialSlotName,
                    Invalidated.MaterialSlotIndex));
        }
        OutSummary += FString::Printf(
            TEXT("\n\nTransparency Maps now out of date:\n- %s")
            TEXT("\n\nThe previous maps remain visible in editor preview. ")
            TEXT("Rebake them in Transparency Editor before runtime use."),
            *FString::Join(SlotDescriptions, TEXT("\n- ")));
    }
    return true;
}
