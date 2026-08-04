#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;
class FDWCEditorCancellationToken;
struct FDWCTransparencyAutoBakeResult;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyEditedMapBakeResult
{
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    FString WarningMessage;
};

class FDWCTransparencyEditedMapBakeSnapshot
{
  public:
    struct FImpl;

    FDWCTransparencyEditedMapBakeSnapshot();
    ~FDWCTransparencyEditedMapBakeSnapshot();
    FDWCTransparencyEditedMapBakeSnapshot(FDWCTransparencyEditedMapBakeSnapshot&&);
    FDWCTransparencyEditedMapBakeSnapshot& operator=(FDWCTransparencyEditedMapBakeSnapshot&&);

    FDWCTransparencyEditedMapBakeSnapshot(const FDWCTransparencyEditedMapBakeSnapshot&) = delete;
    FDWCTransparencyEditedMapBakeSnapshot& operator=(const FDWCTransparencyEditedMapBakeSnapshot&) = delete;

    bool IsValid() const;
    int32 GetMaterialSlotIndex() const;
    FGuid GetLayerGuid() const;
    uint64 GetEstimatedBytes() const;

  private:
    TUniquePtr<FImpl> Impl;
    friend class FDWCTransparencyEditedMapBaker;
};

struct FDWCTransparencyEditedMapComputedResult
{
    bool bSucceeded = false;
    bool bCanceled = false;
    FString Error;
    TArray<FColor> FinalPixels;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    FString WarningMessage;
    uint64 ResultBytes = 0;
};

class FDWCTransparencyEditedMapBaker
{
  public:
    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResult,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static FDWCTransparencyEditedMapComputedResult ComputeSnapshot(
        const FDWCTransparencyEditedMapBakeSnapshot& Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool CommitComputedResult(
        UWetClothingAsset& WetClothingAsset,
        const FDWCTransparencyEditedMapBakeSnapshot& Snapshot,
        FDWCTransparencyEditedMapComputedResult&& ComputedResult,
        FDWCTransparencyEditedMapBakeResult& OutResult,
        FString& OutErrorMessage);

    static bool IsAutoResultCompatible(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        FString& OutReason);

    static bool IsLayerBakeCurrent(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FString* OutReason = nullptr);

    static bool Bake(
        UWetClothingAsset& WetClothingAsset,
        FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        FDWCTransparencyEditedMapBakeResult& OutResult,
        FString& OutErrorMessage);
};
