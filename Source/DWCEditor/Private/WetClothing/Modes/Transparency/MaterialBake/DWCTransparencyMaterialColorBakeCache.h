//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

class UMaterialInterface;
class USkeletalMesh;
class UWetClothingAsset;

struct FDWCTransparencyMaterialColorBakeKey
{
    FSoftObjectPath OwnerAssetPath;
    FSoftObjectPath SourceMeshPath;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 SourceUVChannel = 0;
    int32 Resolution = 0;
    FString MaterialBakeSignature;

    bool IsValid() const;
    bool operator==(const FDWCTransparencyMaterialColorBakeKey& Other) const;
    friend uint32 GetTypeHash(const FDWCTransparencyMaterialColorBakeKey& Key);
};

/** Immutable CPU view of a GPU material-property bake. */
struct FDWCTransparencyMaterialColorBakeResult
{
    FDWCTransparencyMaterialColorBakeKey Key;
    FWetClothingTextureReadback TextureData;
    uint64 AllocatedBytes = 0;
    bool bLoadedFromPersistentCache = false;
    TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> MemoryLease;

    bool IsValid() const { return Key.IsValid() && TextureData.IsValid(); }
};

/**
 * Process-local byte-budgeted cache. UObject access and MaterialBaking calls are
 * confined to the game thread; worker snapshots retain immutable shared results.
 */
class FDWCTransparencyMaterialColorBakeCache
{
  public:
    static void ConfigureResourceGovernor(
        UWetClothingAsset& Asset,
        TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor,
        const FGuid& SessionEpoch,
        uint64 InCacheBudgetBytes);
    static TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> ResolveOrBake(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 Resolution,
        FString& OutError);

    /** Uses the effective component material and placement captured by a source provider. */
    static TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> ResolveOrBake(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        UMaterialInterface& EffectiveMaterial,
        const FTransform& BakeTransform,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 Resolution,
        FString& OutError);

    static void InvalidateMesh(const USkeletalMesh* SourceMesh);
    static void Clear();
    static void Clear(const UWetClothingAsset* Asset);
};
