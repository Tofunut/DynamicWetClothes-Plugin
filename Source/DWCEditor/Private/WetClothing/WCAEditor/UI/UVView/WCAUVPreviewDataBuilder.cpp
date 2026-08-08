// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WCAUVPreviewDataBuilder.h"

#include "DataAssets/WetClothingAssetSetupData.h"
#include "Utility/DWCError.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/UV/DWCUVIslandBuilder.h"

void FWCAUVPreviewDataBuilder::BuildFromConnectivity(
    const TArray<FWCAUVPreviewSourceTriangle>& SourceTriangles,
    TArray<FWetClothingAssetUVIsland>&         OutIslands)
{
    OutIslands.Reset();

    TArray<FDWCUVIslandBuildTriangle> BuildTriangles;
    BuildTriangles.Reserve(SourceTriangles.Num());
    for (const FWCAUVPreviewSourceTriangle& SourceTriangle : SourceTriangles)
    {
        FDWCUVIslandBuildTriangle& BuildTriangle = BuildTriangles.AddDefaulted_GetRef();
        BuildTriangle.TriangleID = SourceTriangle.TriangleID;
        BuildTriangle.MaterialSlotIndex = SourceTriangle.MaterialSlotIndex;
        BuildTriangle.UVs[0] = FVector2D(SourceTriangle.UVs[0]);
        BuildTriangle.UVs[1] = FVector2D(SourceTriangle.UVs[1]);
        BuildTriangle.UVs[2] = FVector2D(SourceTriangle.UVs[2]);
    }

    TArray<FDWCOriginalUVIslandBuildResult> BuiltIslands;
    FDWCUVIslandBuilder::Build(BuildTriangles, BuiltIslands);

    OutIslands.Reserve(BuiltIslands.Num());
    for (const FDWCOriginalUVIslandBuildResult& BuiltIsland : BuiltIslands)
    {
        FWetClothingAssetUVIsland& Island = OutIslands.AddDefaulted_GetRef();
        Island.MaterialSlotIndex = BuiltIsland.MaterialSlotIndex;
        Island.UVIslandID = BuiltIsland.IslandID;
        Island.TriangleCount = BuiltIsland.TriangleInputIndices.Num();
        Island.UVBounds = BuiltIsland.UVBounds;
        Island.UVArea = BuiltIsland.UVArea;
        Island.TriangleIDs = BuiltIsland.TriangleIDs;
        Island.UVTriangles.Reserve(BuiltIsland.TriangleInputIndices.Num());

        for (const int32 TriangleInputIndex : BuiltIsland.TriangleInputIndices)
        {
            if (!SourceTriangles.IsValidIndex(TriangleInputIndex))
            {
                continue;
            }

            const FWCAUVPreviewSourceTriangle& SourceTriangle = SourceTriangles[TriangleInputIndex];
            FWetClothingAssetUVTriangle&       Triangle = Island.UVTriangles.AddDefaulted_GetRef();
            Triangle.TriangleID = SourceTriangle.TriangleID;
            Triangle.MaterialSlotIndex = SourceTriangle.MaterialSlotIndex;
            Triangle.UVIslandID = Island.UVIslandID;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                Triangle.UVs[CornerIndex] = FVector2D(SourceTriangle.UVs[CornerIndex]);
                Triangle.LocalPositions[CornerIndex] = FVector(SourceTriangle.LocalPositions[CornerIndex]);
                Triangle.RenderVertexIndices[CornerIndex] = SourceTriangle.RenderVertexIndices[CornerIndex];
                Triangle.LocalNormals[CornerIndex] = FVector(SourceTriangle.LocalNormals[CornerIndex]).GetSafeNormal();
                Island.LocalBounds += Triangle.LocalPositions[CornerIndex];
            }
        }
    }
}

bool FWCAUVPreviewDataBuilder::BuildFromStoredTopology(
    const TArray<FWCAUVPreviewSourceTriangle>&  SourceTriangles,
    const int32                                 MaterialSlotIndex,
    const TArray<FDWCOriginalUVIslandTopology>& Topology,
    TArray<FWetClothingAssetUVIsland>&          OutIslands,
    FString*                                    OutErrorMessage)
{
    OutIslands.Reset();

    TMap<int32, int32> SourceIndexByTriangleID;
    SourceIndexByTriangleID.Reserve(SourceTriangles.Num());
    for (int32 SourceIndex = 0; SourceIndex < SourceTriangles.Num(); ++SourceIndex)
    {
        SourceIndexByTriangleID.Add(SourceTriangles[SourceIndex].TriangleID, SourceIndex);
    }

    for (const FDWCOriginalUVIslandTopology& TopologyIsland : Topology)
    {
        if (TopologyIsland.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        FWetClothingAssetUVIsland Island;
        Island.MaterialSlotIndex = MaterialSlotIndex;
        Island.UVIslandID = TopologyIsland.IslandID;
        Island.UVBounds = TopologyIsland.UVBounds;
        Island.UVArea = TopologyIsland.UVArea;

        for (const int32 TriangleID : TopologyIsland.TriangleIndices)
        {
            const int32* SourceIndex = SourceIndexByTriangleID.Find(TriangleID);
            if (SourceIndex == nullptr || !SourceTriangles.IsValidIndex(*SourceIndex))
            {
                continue;
            }

            const FWCAUVPreviewSourceTriangle& SourceTriangle = SourceTriangles[*SourceIndex];
            FWetClothingAssetUVTriangle&       Triangle = Island.UVTriangles.AddDefaulted_GetRef();
            Triangle.TriangleID = SourceTriangle.TriangleID;
            Triangle.MaterialSlotIndex = SourceTriangle.MaterialSlotIndex;
            Triangle.UVIslandID = Island.UVIslandID;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                Triangle.UVs[CornerIndex] = FVector2D(SourceTriangle.UVs[CornerIndex]);
                Triangle.LocalPositions[CornerIndex] = FVector(SourceTriangle.LocalPositions[CornerIndex]);
                Triangle.RenderVertexIndices[CornerIndex] = SourceTriangle.RenderVertexIndices[CornerIndex];
                Triangle.LocalNormals[CornerIndex] = FVector(SourceTriangle.LocalNormals[CornerIndex]).GetSafeNormal();
                Island.LocalBounds += Triangle.LocalPositions[CornerIndex];
            }
            Island.TriangleIDs.Add(Triangle.TriangleID);
        }

        Island.TriangleCount = Island.UVTriangles.Num();
        if (Island.TriangleCount > 0)
        {
            OutIslands.Add(MoveTemp(Island));
        }
    }

    OutIslands.Sort([](const FWetClothingAssetUVIsland& A, const FWetClothingAssetUVIsland& B)
                    { return A.UVIslandID < B.UVIslandID; });

    if (OutIslands.IsEmpty() && !SourceTriangles.IsEmpty())
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            TEXT("Stored WCA Original UV topology does not match the Source Skeletal Mesh."));
        return false;
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
