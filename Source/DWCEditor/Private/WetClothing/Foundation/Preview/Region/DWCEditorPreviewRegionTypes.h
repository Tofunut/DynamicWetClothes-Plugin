//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

enum class EDWCEditorPreviewRegionCommitResult : uint8
{
    Applied,
    WorkspaceEntryMissing,
    DataRevisionMismatch,
    ResourceGenerationMismatch,
    DescriptorMismatch,
    InvalidPayload,
    WorkspaceRejected
};

struct FDWCEditorPreviewRegionTarget
{
    FDWCEditorTextureKey Key;
    FDWCEditorTextureDescriptor Descriptor;
    uint64 ExpectedDataRevision = 0;
    uint64 ExpectedResourceGeneration = 0;
};

struct FDWCEditorBGRA8RegionPayload
{
    FIntRect Rect;
    TArray<FColor> Pixels;
};

struct FDWCEditorG8RegionPayload
{
    FIntRect Rect;
    TArray<uint8> Pixels;
};

struct FDWCEditorNormalRegionPayload
{
    FIntRect WorkingRect;
    TArray<uint32> PackedNormalXY;
    TArray<float> Coverage;
    FIntRect OutputRect;
    TArray<FColor> EncodedPixels;
};

struct FDWCEditorPreviewRegionCommitOutcome
{
    EDWCEditorPreviewRegionCommitResult Result = EDWCEditorPreviewRegionCommitResult::WorkspaceRejected;
    uint64 NewDataRevision = 0;
    uint64 NewContentRevision = 0;
    uint64 CommittedPixelCount = 0;
    uint64 CommittedBytes = 0;
    FDWCEditorTextureUploadTicket UploadTicket;

    bool WasApplied() const
    {
        return Result == EDWCEditorPreviewRegionCommitResult::Applied;
    }
};

struct FDWCEditorPreviewRegionMemoryEstimate
{
    uint64 SnapshotBytes = 0;
    uint64 ResultBytes = 0;

    uint64 GetTotalBytes() const
    {
        return SnapshotBytes <= MAX_uint64 - ResultBytes
            ? SnapshotBytes + ResultBytes
            : MAX_uint64;
    }
};

enum class EDWCEditorSparseUploadPlan : uint8
{
    Sparse,
    MergedSparse,
    Bounded
};

struct FDWCEditorSparseUploadPolicyConfig
{
    int32 MaxRegions = 8;
    uint64 RegionSubmissionPenaltyPixels = 4096;
};

struct FDWCEditorSparseUploadDecision
{
    EDWCEditorSparseUploadPlan Plan = EDWCEditorSparseUploadPlan::Bounded;
    TArray<FIntRect> Regions;
    int32 SourceRegionCount = 0;
    uint64 SourcePixelCount = 0;
    uint64 PlannedPixelCount = 0;
    uint64 BoundedPixelCount = 0;
};

/** Chooses the cheaper deterministic upload shape without changing pixel content. */
class FDWCEditorSparseUploadPolicy final
{
  public:
    static FDWCEditorSparseUploadDecision Choose(
        const TArray<FIntRect>& SparseRegions,
        const TArray<FIntRect>& BoundedRegions,
        FIntPoint TextureSize,
        const FDWCEditorSparseUploadPolicyConfig& Config = {});
};

class FDWCEditorPreviewRegionMemory final
{
  public:
    static bool TryEstimateBGRA8(
        const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
        FDWCEditorPreviewRegionMemoryEstimate& OutEstimate);
    static bool TryEstimateG8(
        const TArray<FDWCEditorG8RegionPayload>& Regions,
        FDWCEditorPreviewRegionMemoryEstimate& OutEstimate);
    static bool TryEstimateNormal(
        const TArray<FDWCEditorNormalRegionPayload>& Regions,
        FDWCEditorPreviewRegionMemoryEstimate& OutEstimate);
};

