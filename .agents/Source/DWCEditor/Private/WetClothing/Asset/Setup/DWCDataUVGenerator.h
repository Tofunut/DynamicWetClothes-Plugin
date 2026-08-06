#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"
#include "DWCDataUVGenerationTypes.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class USkeletalMesh;


/** Immutable output of the expensive chart analysis/packing stage. */
struct FDWCDataUVGenerationPlan
{
    int32 UVChannelIndex = INDEX_NONE;
    TArray<FDWCDataUVTriangle> Triangles;
    TArray<FDWCDataUVChart> SeamCharts;
    TMap<int32, FVector2f> PackedUVBySyntheticCorner;
    TSet<int32> ExcludedTriangleIndices;
    TSet<int32> ExcludedVertexInstanceIDs;
};

struct FDWCDataUVPlanApplyResult
{
    bool bSucceeded = false;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    double SeamSplitMilliseconds = 0.0;
    FString Message;
};

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
    /** Slots whose exclusion ratio crossed the safety limit and need explicit editor confirmation. */
    TSet<int32> ConfirmationRequiredMaterialSlotIndices;
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
        const EDWCDataUVResultSeverity Severity = DWCDataUVResultSeverity::Normalize(ResultSeverity);
        return Severity == EDWCDataUVResultSeverity::ReadyWithWarnings ||
               Severity == EDWCDataUVResultSeverity::Failed;
    }

    FString Message;
    TSharedPtr<const FDWCDataUVGenerationPlan, ESPMode::ThreadSafe> GenerationPlan;
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
        const TSet<int32>* TargetMaterialSlotIndices = nullptr,
        bool bDeferMeshCommit = false,
        FMeshDescription* MeshDescriptionOverride = nullptr,
        bool bAnalysisOnly = false,
        bool bClearNonTargetVertexInstances = true,
        const TMap<FName, int32>* MaterialSlotIndexByNameOverride = nullptr,
        bool bAllowVisibleExclusionAboveSafetyLimit = false);

    static FDWCDataUVPlanApplyResult ApplyGenerationPlan(
        USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        const FDWCDataUVGenerationPlan& Plan,
        bool bClearDestinationChannel = false,
        bool bDeferMeshCommit = false);

    static FDWCDataUVGenerationResult TransferFromSourceLOD(
        USkeletalMesh* SkeletalMesh,
        int32 SourceLODIndex,
        int32 TargetLODIndex,
        int32 DataUVChannelIndex,
        bool bAllowOverwriteExistingChannel = true,
        int32 TargetMaterialSlotIndex = INDEX_NONE);
};
