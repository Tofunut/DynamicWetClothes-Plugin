#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"

/** Packs transient DWC UV Channel charts independently per material slot into the unit square. */
class FDWCDataUVPacker
{
public:
    static bool Pack(
        const TArray<FDWCDataUVTriangle>& Triangles,
        TArray<FDWCDataUVChart>& Charts,
        int32 Resolution,
        int32 PaddingPixels,
        TMap<int32, FVector2f>& OutPackedUVByVertexInstance,
        int32& OutFailedMaterialSlotIndex);

private:
    static void BuildRawChartUVs(
        const TArray<FDWCDataUVTriangle>& Triangles,
        FDWCDataUVChart& Chart);
};
