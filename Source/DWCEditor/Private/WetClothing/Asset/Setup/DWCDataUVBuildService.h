#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class UWetClothingAsset;
class USkeletalMesh;

struct FDWCDataUVLODWarning
{
    int32 LODIndex = INDEX_NONE;
    FString Summary;
    FString TechnicalDetails;
};


struct FDWCDataUVBuildOptions
{
    /** When non-empty, build only these LODs instead of resolving the full active range. */
    TArray<int32> TargetLODIndices;

    /** Preserve metadata/topology for LODs outside TargetLODIndices and merge the new payload. */
    bool bMergeWithExistingLayout = false;

    /** Treat any material-slot failure as a complete build failure and commit nothing. */
    bool bRequireAllMaterialSlots = false;
};

struct FDWCDataUVBuildResult
{
    bool bSucceeded = false;
    USkeletalMesh* PreparedMesh = nullptr;
    int32 DataUVChannelIndex = INDEX_NONE;
    int32 WettableMaterialSlotCount = 0;
    TArray<int32> TargetLODIndices;
    TArray<int32> GeneratedLODIndices;
    TArray<FDWCDataUVLODWarning> LODWarnings;
    int32 OriginalUVIslandCount = 0;
    bool bGeneratedWithWarnings = false;
    EDWCDataUVResultSeverity ResultSeverity = EDWCDataUVResultSeverity::Ready;
    int32 ExcludedTriangleCount = 0;
    int32 Degenerate3DTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
    int32 PackedDegenerateTriangleCount = 0;
    int32 ExcludedVisibleTriangleCount = 0;
    double ExcludedVisible3DSurfaceArea = 0.0;
    double ExcludedVisible3DSurfaceRatio = 0.0;
    double LargestConnectedExcluded3DSurfaceArea = 0.0;
    double LargestConnectedExcluded3DSurfaceRatio = 0.0;
    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 BudgetFallbackIslandCount = 0;
    TArray<FDWCDataUVSlotWarning> SlotWarnings;
    TSet<int32> GeneratedMaterialSlotIndices;
    TSet<int32> FailedMaterialSlotIndices;
    TArray<FDWCDataUVSlotLODResult> SlotLODResults;
    int32 FailureLODIndex = INDEX_NONE;
    FDWCDataUVValidationFailure ValidationFailure;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    FString TimingSummary;
    FString Message;
};

class FDWCDataUVBuildService
{
public:
    /** Creates or rebuilds per-slot packed layouts; successful slots commit even when another slot fails. */
    static FDWCDataUVBuildResult Generate(
        UWetClothingAsset& Asset,
        bool bForceNewAsset = false,
        bool bAllowOverwriteExistingDataUVChannel = false,
        bool bUsePreferredDataUVChannel = false,
        const FDWCDataUVBuildOptions* Options = nullptr);

    /** Copies the sealed DWC UV Channel values to another channel without rebuilding charts or island topology. */
    static FDWCDataUVBuildResult RelocateChannel(
        UWetClothingAsset& Asset,
        int32 DestinationUVChannelIndex,
        bool bAllowOverwriteExistingDataUVChannel = false);
};
