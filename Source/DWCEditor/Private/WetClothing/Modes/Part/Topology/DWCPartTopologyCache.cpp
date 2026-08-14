// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCPartTopologyCache.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"

FDWCEditorCacheKey FDWCPartTopologyCache::BuildKey(
    const UWetClothingAsset& WetClothingAsset,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex)
{
    const USkeletalMesh* Mesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (Mesh == nullptr)
    {
        Mesh = WetClothingAsset.GetSourceSkeletalMesh();
    }
    if (Mesh == nullptr)
    {
        FDWCEditorCacheKey Key;
        Key.Namespace = FDWCEditorUVTopologyCache::CacheNamespace();
        Key.Owner = FObjectKey(&WetClothingAsset);
        Key.LODIndex = AuthoringLODIndex;
        Key.UVChannelIndex = UVChannelIndex;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        Key.Signature = FString::Printf(
            TEXT("rev=%llu;mesh=none"),
            WetClothingAsset.GetPreviewTopologyRevision());
        return Key;
    }

    return FDWCEditorUVTopologyCache::BuildKey(
        WetClothingAsset,
        *Mesh,
        AuthoringLODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        WetClothingAsset.GetPreviewTopologyRevision());
}

bool FDWCPartTopologyCache::Acquire(
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
    const UWetClothingAsset* WetClothingAsset,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FDWCEditorCacheKey& OutKey,
    FDWCEditorCacheLease& OutLease,
    FString* OutErrorMessage)
{
    return FDWCEditorUVTopologyCache::AcquireForAsset(
        CacheStore,
        WetClothingAsset,
        UVChannelIndex,
        MaterialSlotIndex,
        OutKey,
        OutLease,
        OutErrorMessage);
}
