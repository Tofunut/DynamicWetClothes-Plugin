// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FWetClothingAssetUVIsland;
struct FWetClothingAssetUVTriangle;

/** Stateless UV topology construction. Cache ownership belongs to FDWCEditorUVTopologyCache. */
class FDWCEditorUVTopologyBuilder
{
  public:
    static bool BuildMaterialSlotUVIslands(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString* OutErrorMessage = nullptr);

    /** Uses persistent WCA topology when valid, then falls back to direct analysis. */
    static bool BuildMaterialSlotUVIslands(
        const UWetClothingAsset* WetClothingAsset,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString* OutErrorMessage = nullptr);

    /** Geometry-only fallback for compact material-slot thumbnails. */
    static bool BuildMaterialSlotGeometryPreviewTriangles(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 PreferredUVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);
};
