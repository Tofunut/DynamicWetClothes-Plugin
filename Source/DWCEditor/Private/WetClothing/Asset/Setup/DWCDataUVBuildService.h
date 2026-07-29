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
    int32 TriangleFallbackChartCount = 0;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    FString Message;
};

class FDWCDataUVBuildService
{
public:
    static FDWCDataUVBuildResult Generate(
        UWetClothingAsset& Asset,
        bool bForceNewAsset = false,
        bool bAllowOverwriteExistingDataUVChannel = false,
        bool bUsePreferredDataUVChannel = false);
    static bool BuildOriginalUVTopology(UWetClothingAsset& Asset, FString* OutErrorMessage = nullptr);
};
