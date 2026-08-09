//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
class FDWCEditorSpatialQueryService;
class FDWCEditorSurfacePatchProjectionCacheService;

class FWetWrinkleBakeService
{
public:
    static void CollectBakeMaterialSlots(const UWetClothingAsset& WetClothingAsset, TArray<int32>& OutMaterialSlots);
    static bool BakeAllWrinkleMaps(
        UWetClothingAsset* WetClothingAsset,
        TSharedRef<FDWCEditorSpatialQueryService> SpatialQueryService,
        TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache,
        FString& OutSummary,
        bool* OutHadWarnings = nullptr);
    static void RefreshBakeStatusFromCurrentOutputs(UWetClothingAsset* WetClothingAsset, const FString& Failure = FString());
};
