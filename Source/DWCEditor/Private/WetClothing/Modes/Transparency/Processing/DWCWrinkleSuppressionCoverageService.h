//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

class UTexture2D;
class UWetClothingAsset;

enum class EDWCWrinkleSuppressionDependencyStatus : uint8
{
    Missing,
    Stale,
    Ready,
    Unreadable
};

/** Immutable identity shared by preview, validation, and final bake. */
struct FDWCWrinkleSuppressionDependencySnapshot
{
    EDWCWrinkleSuppressionDependencyStatus Status =
        EDWCWrinkleSuppressionDependencyStatus::Missing;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 DataUVChannelIndex = INDEX_NONE;
    int32 LODIndex = 0;
    TWeakObjectPtr<UTexture2D> MaskTexture;
    FString MaskTexturePath;
    FString BuildSignature;
    FGuid BakeGuid;
    FGuid TextureSourceId;
    FIntPoint SourceResolution = FIntPoint::ZeroValue;
    FString Detail;

    bool IsAvailable() const;
    bool IsValid(FString* OutError = nullptr) const;
    UTexture2D* ResolveTexture() const;
    FString BuildCacheSignature() const;
};

/** Worker-safe source coverage retained through the shared editor cache. */
class FDWCWrinkleCoverageCacheValue final : public IDWCEditorCacheValue
{
  public:
    static FName StaticCacheTypeName();
    virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
    virtual uint64 GetAllocatedSizeBytes() const override;

    FDWCWrinkleSuppressionDependencySnapshot Dependency;
    FWetClothingTextureReadback Readback;

    bool IsValid() const;
    float SampleCoverage(const FVector2f& UV) const;
};

/** Resolves one exact wrinkle dependency and leases its CPU coverage payload. */
class FDWCWrinkleSuppressionCoverageService final
{
  public:
    explicit FDWCWrinkleSuppressionCoverageService(TSharedRef<FDWCEditorCacheStore> InCacheStore);

    static FDWCWrinkleSuppressionDependencySnapshot ResolveDependency(
        const UWetClothingAsset* Asset,
        int32 MaterialSlotIndex,
        bool bExactCurrentness = false);

    bool AcquireCoverage(
        const UWetClothingAsset& Asset,
        const FDWCWrinkleSuppressionDependencySnapshot& Dependency,
        FDWCEditorCacheLease& OutLease,
        FString& OutError);

    void InvalidateAssetSlot(const UWetClothingAsset* Asset, int32 MaterialSlotIndex);
    void InvalidateAsset(const UWetClothingAsset* Asset);

    static float EvaluateSuppression(
        float Coverage,
        float CoverageThreshold,
        float MaskSoftness);

  private:
    static FName CacheNamespace();
    static FDWCEditorCacheKey BuildCacheKey(
        const UWetClothingAsset& Asset,
        const FDWCWrinkleSuppressionDependencySnapshot& Dependency);

    TSharedRef<FDWCEditorCacheStore> CacheStore;
};
