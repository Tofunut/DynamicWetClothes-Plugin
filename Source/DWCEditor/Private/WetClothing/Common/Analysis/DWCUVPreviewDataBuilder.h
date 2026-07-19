#pragma once

#include "CoreMinimal.h"
#include "DWCUVPreviewTriangleReader.h"

struct FDWCOriginalUVIslandTopology;
struct FWetClothingAssetUVIsland;

/** Converts transient source triangles or persistent WCA topology into UV view data. */
class FDWCUVPreviewDataBuilder
{
public:
    static void BuildFromConnectivity(
        const TArray<FDWCUVPreviewSourceTriangle>& SourceTriangles,
        TArray<FWetClothingAssetUVIsland>& OutIslands);

    static bool BuildFromStoredTopology(
        const TArray<FDWCUVPreviewSourceTriangle>& SourceTriangles,
        int32 MaterialSlotIndex,
        const TArray<FDWCOriginalUVIslandTopology>& Topology,
        TArray<FWetClothingAssetUVIsland>& OutIslands,
        FString* OutErrorMessage = nullptr);
};
