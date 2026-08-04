#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class USkeletalMesh;

struct FDWCDataUVGenerationResult
{
    bool bSucceeded = false;
    bool bTargetSlotNotPresent = false;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    /** Editor-facing Original UV island count retained for diagnostics. */
    int32 OriginalUVIslandCount = 0;

    /** Number of topology-connected physical Source UV shells packed as indivisible DWC UV charts. */
    int32 DataUVChartCount = 0;

    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 Degenerate3DTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
    int32 PackedDegenerateTriangleCount = 0;
    int32 ExcludedVisibleTriangleCount = 0;
    double ExcludedVisible3DSurfaceArea = 0.0;
    double ExcludedVisible3DSurfaceRatio = 0.0;
    double LargestConnectedExcluded3DSurfaceArea = 0.0;
    double LargestConnectedExcluded3DSurfaceRatio = 0.0;
    EDWCDataUVResultSeverity ResultSeverity = EDWCDataUVResultSeverity::Ready;
    TArray<FDWCDataUVSlotWarning> SlotWarnings;
    TSet<int32> FailedMaterialSlotIndices;
    FDWCDataUVValidationFailure ValidationFailure;

    /** VertexInstances duplicated to make non-overlapping DWC UV Channel chart boundaries real seams. */
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    int32 RenderVertexCount = 0;
    double TriangleReadMilliseconds = 0.0;
    double OriginalIslandBuildMilliseconds = 0.0;
    double ChartBuildMilliseconds = 0.0;
    double SeamSplitMilliseconds = 0.0;
    double PackAndValidateMilliseconds = 0.0;

    /** 3D degenerate triangles are excluded automatically and are informational only. */
    int32 GetWarningCount() const
    {
        int32 WarningCount = 0;
        for (const FDWCDataUVSlotWarning& SlotWarning : SlotWarnings)
        {
            if (SlotWarning.HasWarnings())
            {
                WarningCount += SlotWarning.DegenerateSourceUVTriangleCount +
                    SlotWarning.InvalidSourceUVTriangleCount +
                    SlotWarning.PackedDegenerateTriangleCount +
                    SlotWarning.SplitOriginalUVIslandCount +
                    (SlotWarning.SelfOverlapPairCount > 0 ? 1 : 0) +
                    SlotWarning.BudgetFallbackIslandCount;
            }
        }
        return WarningCount;
    }

    bool HasWarnings() const
    {
        return GetWarningCount() > 0;
    }

    FString Message;
};

/** Generates and writes a DWC UV Channel into an editable Skeletal Mesh LOD. */
class FDWCDataUVGenerator
{
public:
    static FDWCDataUVGenerationResult GenerateForSkeletalMesh(
        USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 SourceUVChannelIndex,
        int32 PreferredUVChannelIndex,
        bool bAllowOverwriteExistingChannel = false,
        int32 TargetMaterialSlotIndex = INDEX_NONE,
        const TSet<int32>* TargetMaterialSlotIndices = nullptr);

    static FDWCDataUVGenerationResult TransferFromSourceLOD(
        USkeletalMesh* SkeletalMesh,
        int32 SourceLODIndex,
        int32 TargetLODIndex,
        int32 DataUVChannelIndex,
        bool bAllowOverwriteExistingChannel = true,
        int32 TargetMaterialSlotIndex = INDEX_NONE);
};
