#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
class USkeletalMesh;

struct FDWCDataUVBuildResult
{
    bool bSucceeded = false;
    USkeletalMesh* GeneratedDataUV = nullptr;
    int32 OriginalIslandCount = 0;
    bool bGeneratedWithWarnings = false;
    int32 ExcludedTriangleCount = 0;
    int32 SplitSourceIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 TriangleFallbackChartCount = 0;
    FString Message;
};

class FDWCDataUVBuildService
{
public:
    static FDWCDataUVBuildResult Generate(UWetClothingAsset& Asset, bool bForceNewAsset = false);
    static bool BuildOriginalUVTopology(UWetClothingAsset& Asset, FString* OutErrorMessage = nullptr);
};
