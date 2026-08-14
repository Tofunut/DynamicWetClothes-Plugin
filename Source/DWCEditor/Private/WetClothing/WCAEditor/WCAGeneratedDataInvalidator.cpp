// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

namespace
{
    FOnWCAGeneratedDataInvalidated GOnGeneratedDataInvalidated;
}

FOnWCAGeneratedDataInvalidated& FWCAGeneratedDataInvalidator::OnInvalidated()
{
    return GOnGeneratedDataInvalidated;
}

void FWCAGeneratedDataInvalidator::InvalidateAll()
{
    FWCAGeneratedDataInvalidation Invalidation;
    Invalidation.Scope = EWCAGeneratedDataInvalidationScope::All;
    GOnGeneratedDataInvalidated.Broadcast(Invalidation);
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FWCAGeneratedDataInvalidator::InvalidateAsset(UWetClothingAsset& Asset)
{
    Asset.BumpPreviewTopologyRevision();
    NotifyAssetChanged(Asset);
    InvalidateMesh(Asset.GetRuntimeSkeletalMesh());
    InvalidateMesh(Asset.GetSourceSkeletalMesh());
    // Texture readback currently has no asset-scoped API, so explicit rebuild/setup changes
    // conservatively invalidate it globally. It remains transient and fully regenerable.
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FWCAGeneratedDataInvalidator::NotifyAssetChanged(const UWetClothingAsset& Asset)
{
    FWCAGeneratedDataInvalidation Invalidation;
    Invalidation.Scope = EWCAGeneratedDataInvalidationScope::Asset;
    Invalidation.Asset = &Asset;
    GOnGeneratedDataInvalidated.Broadcast(Invalidation);
}

void FWCAGeneratedDataInvalidator::InvalidateMesh(const USkeletalMesh* Mesh)
{
    if (Mesh == nullptr)
    {
        return;
    }
    FWCAGeneratedDataInvalidation Invalidation;
    Invalidation.Scope = EWCAGeneratedDataInvalidationScope::Mesh;
    Invalidation.Mesh = Mesh;
    GOnGeneratedDataInvalidated.Broadcast(Invalidation);
}

void FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(
    UWetClothingAsset&   Asset,
    const USkeletalMesh* TouchedMesh)
{
    InvalidateAsset(Asset);
    InvalidateMesh(TouchedMesh);
}
