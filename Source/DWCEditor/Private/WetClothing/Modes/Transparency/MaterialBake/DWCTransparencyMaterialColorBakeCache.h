//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
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
    int32 SourceBakeResolution = 0;
    int32 IdentityVersion = 0;
    FString CacheIdentity;
    FString SourceMeshContentSignature;
    FString EffectiveMaterialSignature;
    FString PlacementSignature;

    bool IsValid() const;
    bool operator==(const FDWCTransparencyMaterialColorBakeKey& Other) const;
    friend uint32 GetTypeHash(const FDWCTransparencyMaterialColorBakeKey& Key);
};

/**
 * Immutable CPU view of one evaluated source material. Base Color is consumed
 * by the existing Stage 2 projection; tangent Normal and Metallic establish
 * the shared surface-property contract used by the later Reveal Surface bake.
 */
struct FDWCTransparencyMaterialColorBakeResult
{
    FDWCTransparencyMaterialColorBakeKey Key;
    EDWCTransparencyMaterialColorPayloadKind PayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;
    FIntPoint SourceBakeResolution = FIntPoint::ZeroValue;
    FIntPoint PhysicalResolution = FIntPoint::ZeroValue;
    FWetClothingTextureReadback TextureData;
    EDWCTransparencyMaterialColorPayloadKind NormalPayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;
    FIntPoint NormalPhysicalResolution = FIntPoint::ZeroValue;
    FWetClothingTextureReadback NormalTextureData;
    EDWCTransparencyMaterialColorPayloadKind MetallicPayloadKind =
        EDWCTransparencyMaterialColorPayloadKind::Texture;
    FIntPoint MetallicPhysicalResolution = FIntPoint::ZeroValue;
    FWetClothingTextureReadback MetallicTextureData;
    bool bHasBakedNormalProperty = false;
    bool bHasBakedMetallicProperty = false;
    /** Referenced immutable payload bytes. Ownership and accounting live in each readback payload. */
    uint64 AllocatedBytes = 0;
    bool bLoadedFromPersistentCache = false;

    bool InitializePayload(
        EDWCTransparencyMaterialColorPayloadKind InPayloadKind,
        FIntPoint InSourceBakeResolution,
        FIntPoint InPhysicalResolution,
        TArray<FColor>&& InPixels,
        bool bSRGB,
        FString& OutError);
    bool InitializePayloadFromReadback(
        EDWCTransparencyMaterialColorPayloadKind InPayloadKind,
        FIntPoint InSourceBakeResolution,
        FWetClothingTextureReadback&& InTextureData,
        FString& OutError);
    bool InitializeSurfacePayloadFromReadbacks(
        EDWCTransparencyMaterialColorPayloadKind InNormalPayloadKind,
        FWetClothingTextureReadback&& InNormalTextureData,
        bool bInHasBakedNormalProperty,
        EDWCTransparencyMaterialColorPayloadKind InMetallicPayloadKind,
        FWetClothingTextureReadback&& InMetallicTextureData,
        bool bInHasBakedMetallicProperty,
        FString& OutError);
    bool IsValid() const;
    bool HasCompleteSurfacePayload() const;
    FLinearColor Sample(const FVector2D& UV) const;
    FVector3f SampleTangentNormal(const FVector2D& UV) const;
    float SampleMetallic(const FVector2D& UV) const;
    uint64 GetImmediatelyReclaimableBytes() const;
};

/**
 * Process-local byte-budgeted source-material surface cache. UObject access and
 * MaterialBaking calls are confined to the game thread; worker snapshots retain
 * immutable shared Base Color, Normal, and Metallic results.
 */
class FDWCTransparencyMaterialColorBakeCache
{
  public:
    static void ConfigureCacheBudget(
        UWetClothingAsset& Asset,
        uint64 InCacheBudgetBytes);
    static TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> ResolveOrBake(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 SourceBakeResolution,
        FString& OutError);

    /** Uses the effective component material and placement captured by a source provider. */
    static TSharedPtr<const FDWCTransparencyMaterialColorBakeResult> ResolveOrBake(
        UWetClothingAsset& Asset,
        USkeletalMesh& SourceMesh,
        UMaterialInterface& EffectiveMaterial,
        const FTransform& BakeTransform,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 SourceBakeResolution,
        FString& OutError);

    static void InvalidateMesh(const USkeletalMesh* SourceMesh);
    static uint64 GetReclaimableBytes(const UWetClothingAsset* Asset);
    static uint64 ReclaimUnleasedBytes(const UWetClothingAsset* Asset, uint64 TargetBytes);
    static void Clear();
    static void Clear(const UWetClothingAsset* Asset);
};
