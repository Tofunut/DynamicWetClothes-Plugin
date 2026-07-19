#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"

/** Packs transient Data UV charts independently per material slot into the unit square. */
class FDWCDataUVPacker
{
public:
    static void Pack(
        const TArray<FDWCDataUVTriangle>& Triangles,
        TArray<FDWCDataUVChart>& Charts,
        int32 Resolution,
        int32 PaddingPixels,
        TMap<int32, FVector2f>& OutPackedUVByVertexInstance);

private:
    static void BuildRawChartUVs(
        const TArray<FDWCDataUVTriangle>& Triangles,
        FDWCDataUVChart& Chart);
};
