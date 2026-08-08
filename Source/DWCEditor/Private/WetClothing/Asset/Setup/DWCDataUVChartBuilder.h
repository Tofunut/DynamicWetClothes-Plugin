// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "DWCDataUVGenerationTypes.h"

/**
 * Builds non-overlapping DWC UV packing charts.
 *
 * The editor-facing Original UV island count still uses the shared UV-only policy,
 * but packing uses transient physical Source UV shells built from actual mesh-edge
 * adjacency plus Source UV continuity. Normal shells remain whole. Only a shell with
 * true internal positive-area UV overlap is split, and adjacent conflict-free pieces
 * are greedily merged back to minimize artificial chart boundaries.
 */
class FDWCDataUVChartBuilder
{
  public:
    static void BuildOriginalUVIslands(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TMap<int32, TArray<int32>>& TriangleIndicesByMaterialSlot,
        TArray<FDWCDataUVChart>&          OutOriginalUVIslands);

    static bool BuildNonOverlappingCharts(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<FDWCDataUVChart>&    OriginalUVIslands,
        TArray<FDWCDataUVChart>&          OutCharts,
        int32&                            OutSplitOriginalUVIslandCount,
        int32&                            OutOverlapPairCount,
        TArray<FDWCDataUVSlotWarning>&    InOutSlotWarnings,
        FDWCDataUVChartBuildFailure*      OutFailure = nullptr);

  private:
    /** Uses the shared editor Original-UV island policy for diagnostics only. */
    static void BuildOriginalUVIslandsForSlot(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<int32>&              SlotTriangleIndices,
        TArray<FDWCDataUVChart>&          OutOriginalUVIslands);

    /**
     * Builds physical Source UV shells using an actual MeshDescription edge shared
     * by exactly two triangles, with continuous Source UV endpoints across the edge.
     */
    static void BuildSourceUVShellsForSlot(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<int32>&              SlotTriangleIndices,
        TArray<FDWCDataUVChart>&          OutSourceShells);

    /** Builds an exact positive-area self-overlap graph inside one physical Source UV shell. */
    static bool BuildOverlapConflictGraph(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const FDWCDataUVChart&            SourceUVShell,
        TArray<TSet<int32>>&              OutConflicts,
        int32&                            OutOverlapPairCount,
        int64&                            OutTestedCandidatePairCount);
};
