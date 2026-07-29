#pragma once

#include "CoreMinimal.h"
#include "DWCDataUVGenerationTypes.h"

/** Converts persistent Original UV connectivity into transient non-overlapping Data UV charts. */
class FDWCDataUVChartBuilder
{
public:
    static void BuildOriginalUVIslands(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TMap<int32, TArray<int32>>& TriangleIndicesByMaterialSlot,
        TArray<FDWCDataUVChart>& OutOriginalUVIslands);

    static void BuildNonOverlappingCharts(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<FDWCDataUVChart>& OriginalUVIslands,
        TArray<FDWCDataUVChart>& OutCharts,
        int32& OutSplitOriginalUVIslandCount,
        int32& OutOverlapPairCount,
        int32& OutCandidateBudgetFallbackChartCount);

    static void BuildTriangleFallbackCharts(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<FDWCDataUVChart>& ExistingCharts,
        const TSet<int32>& MaterialSlotsToReplace,
        TArray<FDWCDataUVChart>& OutCharts,
        int32& OutFallbackChartCount);

private:
    static void BuildOriginalUVIslandsForSlot(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<int32>& SlotTriangleIndices,
        TArray<FDWCDataUVChart>& OutOriginalUVIslands);

    static bool BuildOverlapConflictGraph(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const FDWCDataUVChart& OriginalUVIsland,
        TArray<TSet<int32>>& OutConflicts,
        int32& OutOverlapPairCount);
};
