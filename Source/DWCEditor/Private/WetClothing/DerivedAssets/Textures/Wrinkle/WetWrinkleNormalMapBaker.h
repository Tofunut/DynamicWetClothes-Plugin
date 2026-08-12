//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;
class FDWCEditorCancellationToken;
class FDWCEditorCacheStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorSurfacePatchProjectionCacheService;

struct FWetWrinkleNormalMapBakeSettings
{
    int32 Resolution = 1024;
    int32 PaddingPixels = 8;
    bool bIncludeDisabledPatches = false;
};

struct FWetWrinkleInvalidatedTransparencyOutput
{
    int32 MaterialSlotIndex = INDEX_NONE;
    FString MaterialSlotName;
    FString TransparencyTextureName;
};

struct FWetWrinkleNormalMapBakeResult
{
    int32 BakedMapCount = 0;
    int32 BakedStampCount = 0;
    int32 BakedProceduralStrokeCount = 0;
    TArray<UTexture2D*> BakedNormalMaps;
    TArray<UTexture2D*> BakedMasks;
    TArray<FWetWrinkleInvalidatedTransparencyOutput> InvalidatedTransparencyOutputs;
};

/** Peak ownership estimates for the worker and game-thread commit phases. */
struct FWetWrinkleNormalMapBakeMemoryPlan
{
    uint64 SnapshotBytes = 0;
    uint64 RasterBytes = 0;
    uint64 PostProcessBytes = 0;
    uint64 OutputBytes = 0;
    uint64 CommitMetadataBytes = 0;

    uint64 GetWorkerBytes() const
    {
        return SnapshotBytes + RasterBytes + PostProcessBytes + OutputBytes;
    }

    uint64 GetCommitBytes() const
    {
        return CommitMetadataBytes + OutputBytes;
    }
};

enum class EWetWrinkleBakeCurrentnessIssue : uint8
{
    None,
    InvalidTarget,
    NoBakeableContent,
    NormalMissing,
    CoverageMissing,
    ResolutionMismatch,
    PaddingMismatch,
    SignatureMismatch
};

/** Structured exact-output state shared by validation and editor dependencies. */
struct FWetWrinkleMaterialSlotBakeState
{
    EWetWrinkleBakeCurrentnessIssue Issue = EWetWrinkleBakeCurrentnessIssue::InvalidTarget;
    bool bHasBakeableContent = false;
    bool bNormalExists = false;
    bool bCoverageMaskExists = false;
    bool bResolutionMatches = false;
    bool bPaddingMatches = false;
    bool bSignatureMatches = false;
    FString Detail;

    bool IsCurrent() const { return Issue == EWetWrinkleBakeCurrentnessIssue::None; }
};

/** Immutable, UObject-free input captured on the game thread for a wrinkle bake. */
class FWetWrinkleNormalMapBakeSnapshot
{
  public:
    struct FImpl;

    FWetWrinkleNormalMapBakeSnapshot();
    ~FWetWrinkleNormalMapBakeSnapshot();
    FWetWrinkleNormalMapBakeSnapshot(FWetWrinkleNormalMapBakeSnapshot&&);
    FWetWrinkleNormalMapBakeSnapshot& operator=(FWetWrinkleNormalMapBakeSnapshot&&);

    FWetWrinkleNormalMapBakeSnapshot(const FWetWrinkleNormalMapBakeSnapshot&) = delete;
    FWetWrinkleNormalMapBakeSnapshot& operator=(const FWetWrinkleNormalMapBakeSnapshot&) = delete;

    bool IsValid() const;
    int32 GetMaterialSlotIndex() const;
    uint64 GetEstimatedSnapshotBytes() const;
    uint64 GetEstimatedWorkingBytes() const;
    uint64 GetEstimatedResultBytes() const;
    uint64 GetEstimatedCommitBytes() const;
    FWetWrinkleNormalMapBakeMemoryPlan GetMemoryPlan() const;

    /** Drops worker-only topology, source, and raster inputs after ComputeSnapshot returns. */
    void ReleaseWorkerResources();

  private:
    TUniquePtr<FImpl> Impl;

    friend class FWetWrinkleNormalMapBaker;
};

/** Pure CPU result. It contains no UObject references and is safe to move back to the game thread. */
struct FWetWrinkleNormalMapComputedResult
{
    bool bSucceeded = false;
    bool bCanceled = false;
    FString Error;
    TArray<FColor> NormalPixels;
    TArray<uint8> MaskPixels;
    int32 BakedStampCount = 0;
    int32 BakedProceduralStrokeCount = 0;
    uint64 ResultBytes = 0;
};

class FWetWrinkleNormalMapBakeSession
{
  public:
    struct FImpl;

    explicit FWetWrinkleNormalMapBakeSession(
        TSharedRef<FDWCEditorSpatialQueryService> InSpatialQueryService,
        TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> InSurfacePatchProjectionCache,
        TSharedPtr<FDWCEditorCacheStore> InCacheStore = nullptr);
    ~FWetWrinkleNormalMapBakeSession();

    FWetWrinkleNormalMapBakeSession(FWetWrinkleNormalMapBakeSession&&);
    FWetWrinkleNormalMapBakeSession& operator=(FWetWrinkleNormalMapBakeSession&&);

    FWetWrinkleNormalMapBakeSession(const FWetWrinkleNormalMapBakeSession&) = delete;
    FWetWrinkleNormalMapBakeSession& operator=(const FWetWrinkleNormalMapBakeSession&) = delete;

  private:
    TUniquePtr<FImpl> Impl;

    friend class FWetWrinkleNormalMapBaker;
};

class FWetWrinkleNormalMapBaker
{
  public:
    static bool BuildMaterialSlotSnapshot(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession& Session,
        FWetWrinkleNormalMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static FWetWrinkleNormalMapComputedResult ComputeSnapshot(
        const FWetWrinkleNormalMapBakeSnapshot& Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool CommitComputedResult(
        UWetClothingAsset* WetClothingAsset,
        const FWetWrinkleNormalMapBakeSnapshot& Snapshot,
        FWetWrinkleNormalMapComputedResult&& ComputedResult,
        FWetWrinkleNormalMapBakeResult& OutResult,
        FString& OutErrorMessage);

    static bool BakeMaterialSlot(
        UWetClothingAsset*                       WetClothingAsset,
        int32                                    MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

    static bool IsMaterialSlotBakeCurrent(
        const UWetClothingAsset* WetClothingAsset,
        int32                    MaterialSlotIndex);

    static FWetWrinkleMaterialSlotBakeState EvaluateMaterialSlotBakeState(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        bool bExactSignature = true);

  private:
    struct FBakeGroup;

    static bool BuildGroupSnapshot(
        UWetClothingAsset&                       WetClothingAsset,
        const FBakeGroup&                        Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeSnapshot&       OutSnapshot,
        FString&                                OutErrorMessage);

    static FString MakeBuildSignature(
        const UWetClothingAsset& WetClothingAsset,
        const FBakeGroup&        Group,
        int32                    Width,
        int32                    Height,
        const FWetWrinkleNormalMapBakeSettings& Settings);

};
