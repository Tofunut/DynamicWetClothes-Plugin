#pragma once

#include "CoreMinimal.h"
#include "WCAUVPreviewTriangleReader.h"

struct FDWCOriginalUVIslandTopology;
struct FWetClothingAssetUVIsland;

/** Converts transient source triangles or persistent WCA topology into UV view data. */
class FWCAUVPreviewDataBuilder
{
public:
    static void BuildFromConnectivity(
        const TArray<FWCAUVPreviewSourceTriangle>& SourceTriangles,
        TArray<FWetClothingAssetUVIsland>& OutIslands);

    static bool BuildFromStoredTopology(
        const TArray<FWCAUVPreviewSourceTriangle>& SourceTriangles,
        int32 MaterialSlotIndex,
        const TArray<FDWCOriginalUVIslandTopology>& Topology,
        TArray<FWetClothingAssetUVIsland>& OutIslands,
        FString* OutErrorMessage = nullptr);
};
