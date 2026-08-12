//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
class FDWCEditorSpatialQueryService;
class FDWCEditorSurfacePatchProjectionCacheService;

/** Read-only authored state shared by bake admission, validation, and build status. */
struct FWetWrinkleAuthoredSlotState
{
    int32 MaterialSlotIndex = INDEX_NONE;
    bool bWettable = false;
    bool bUsesCustomNormal = false;
    int32 PatchCount = 0;
    int32 ValidPatchCount = 0;
    int32 MissingPatchTextureCount = 0;
    int32 InvalidPatchPlacementCount = 0;
    int32 RidgeStrokeCount = 0;
    int32 ValidRidgeStrokeCount = 0;
    int32 InvalidRidgeStrokeCount = 0;

    bool HasAuthoredContent() const { return PatchCount > 0 || RidgeStrokeCount > 0; }
    bool HasBakeableContent() const
    {
        return ValidPatchCount > 0 || ValidRidgeStrokeCount > 0;
    }
    bool HasInvalidInput() const
    {
        return MissingPatchTextureCount > 0 || InvalidPatchPlacementCount > 0 ||
               InvalidRidgeStrokeCount > 0;
    }
};

class FWetWrinkleBakeService
{
public:
    static void CollectAuthoredSlotStates(
        const UWetClothingAsset& WetClothingAsset,
        TArray<FWetWrinkleAuthoredSlotState>& OutStates);
    static void CollectBakeMaterialSlots(const UWetClothingAsset& WetClothingAsset, TArray<int32>& OutMaterialSlots);
    static bool BakeAllWrinkleMaps(
        UWetClothingAsset* WetClothingAsset,
        TSharedRef<FDWCEditorSpatialQueryService> SpatialQueryService,
        TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache,
        FString& OutSummary,
        bool* OutHadWarnings = nullptr);
    static void RefreshBakeStatusFromCurrentOutputs(UWetClothingAsset* WetClothingAsset, const FString& Failure = FString());
};
