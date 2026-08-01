#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
class USkeletalMesh;

struct FDWCDataUVBuildResult
{
    bool bSucceeded = false;
    USkeletalMesh* PreparedMesh = nullptr;
    int32 OriginalUVIslandCount = 0;
    bool bGeneratedWithWarnings = false;
    int32 ExcludedTriangleCount = 0;
    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    TSet<int32> WarningMaterialSlotIndices;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    FString Message;
};

class FDWCDataUVBuildService
{
public:
    /** Initial creation/recovery only. Once successful, the packed layout and island topology are sealed. */
    static FDWCDataUVBuildResult Generate(
        UWetClothingAsset& Asset,
        bool bForceNewAsset = false,
        bool bAllowOverwriteExistingDataUVChannel = false,
        bool bUsePreferredDataUVChannel = false);

    /** Copies the sealed Data UV values to another channel without rebuilding charts or island topology. */
    static FDWCDataUVBuildResult RelocateChannel(
        UWetClothingAsset& Asset,
        int32 DestinationUVChannelIndex,
        bool bAllowOverwriteExistingDataUVChannel = false);
};
