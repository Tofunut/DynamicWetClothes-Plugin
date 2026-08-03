#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class UWetClothingAsset;
class USkeletalMesh;

struct FDWCDataUVBuildResult
{
    bool bSucceeded = false;
    USkeletalMesh* PreparedMesh = nullptr;
    int32 OriginalUVIslandCount = 0;
    bool bGeneratedWithWarnings = false;
    int32 ExcludedTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 BudgetFallbackIslandCount = 0;
    TArray<FDWCDataUVSlotWarning> SlotWarnings;
    TSet<int32> FailedMaterialSlotIndices;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    FString Message;
};

class FDWCDataUVBuildService
{
public:
    /** Creates or rebuilds the packed layout and island topology for the current Wettable slot set. */
    static FDWCDataUVBuildResult Generate(
        UWetClothingAsset& Asset,
        bool bForceNewAsset = false,
        bool bAllowOverwriteExistingDataUVChannel = false,
        bool bUsePreferredDataUVChannel = false);

    /** Copies the sealed DWC UV Channel values to another channel without rebuilding charts or island topology. */
    static FDWCDataUVBuildResult RelocateChannel(
        UWetClothingAsset& Asset,
        int32 DestinationUVChannelIndex,
        bool bAllowOverwriteExistingDataUVChannel = false);
};
