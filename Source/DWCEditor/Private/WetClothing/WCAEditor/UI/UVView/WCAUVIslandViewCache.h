//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 * UV island view-data cache. Entries are transient and must never be treated as persistent topology.
 */

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FWetClothingAssetUVIsland;
struct FWetClothingAssetUVTriangle;

class FWCAUVIslandViewCache
{
public:
    static bool GetMaterialSlotUVIslands(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString* OutErrorMessage = nullptr);

    /** Uses current persistent WCA topology when valid, then falls back to direct analysis. */
    static bool GetMaterialSlotUVIslands(
        const UWetClothingAsset* WetClothingAsset,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString* OutErrorMessage = nullptr);

    static bool BuildMaterialSlotPreviewTriangles(
        const USkeletalMesh* SkeletalMesh,
        int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    static bool BuildMaterialSlotPreviewTriangles(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    /**
     * Geometry-only fallback for the compact material-slot thumbnail.
     * Keeps a slot visible even when its UVs are missing or degenerate.
     */
    static bool BuildMaterialSlotGeometryPreviewTriangles(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 PreferredUVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    static bool BuildMaterialSlotPreviewTriangles(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    static void InvalidateAll();
    static void InvalidateMesh(const USkeletalMesh* SkeletalMesh);
    static void InvalidateAsset(const UWetClothingAsset* WetClothingAsset);

};
