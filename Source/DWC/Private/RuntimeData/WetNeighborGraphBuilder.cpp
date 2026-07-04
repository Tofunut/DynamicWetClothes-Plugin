#include "RuntimeData/WetNeighborGraphBuilder.h"

#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RuntimeData/WetClothingRuntimeData.h"
#include "Utility/DWCError.h"

namespace
{
    template <typename NeighborEntryType>
    void AddNeighbor(TArray<NeighborEntryType>& NeighborGraph, const int32 VertexIndex, const int32 NeighborIndex)
    {
        if (!NeighborGraph.IsValidIndex(VertexIndex) || !NeighborGraph.IsValidIndex(NeighborIndex) || VertexIndex == NeighborIndex)
        {
            return;
        }

        NeighborGraph[VertexIndex].Neighbors.AddUnique(NeighborIndex);
    }

    template <typename NeighborEntryType>
    void ConnectCoincidentPositionNeighbors(
        const FSkeletalMeshLODRenderData& LODData,
        const float                       Tolerance,
        TArray<NeighborEntryType>&        NeighborGraph)
    {
        if (Tolerance <= 0.0f)
        {
            return;
        }

        TMap<FIntVector, TArray<int32>> VerticesByPosition;
        const int32                     VertexCount = LODData.GetNumVertices();
        VerticesByPosition.Reserve(VertexCount);

        const float QuantizeScale = 1.0f / Tolerance;
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector3f Position = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
            const FIntVector PositionKey(
                FMath::RoundToInt(Position.X * QuantizeScale),
                FMath::RoundToInt(Position.Y * QuantizeScale),
                FMath::RoundToInt(Position.Z * QuantizeScale));
            VerticesByPosition.FindOrAdd(PositionKey).Add(VertexIndex);
        }

        const float ToleranceSquared = FMath::Square(Tolerance);
        for (const TPair<FIntVector, TArray<int32>>& Pair : VerticesByPosition)
        {
            const TArray<int32>& Vertices = Pair.Value;
            for (int32 IndexA = 0; IndexA < Vertices.Num(); ++IndexA)
            {
                const int32     VertexA = Vertices[IndexA];
                const FVector3f PositionA = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexA);

                for (int32 IndexB = IndexA + 1; IndexB < Vertices.Num(); ++IndexB)
                {
                    const int32     VertexB = Vertices[IndexB];
                    const FVector3f PositionB = LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexB);
                    if ((PositionA - PositionB).SizeSquared() > ToleranceSquared)
                    {
                        continue;
                    }

                    AddNeighbor(NeighborGraph, VertexA, VertexB);
                    AddNeighbor(NeighborGraph, VertexB, VertexA);
                }
            }
        }
    }

    template <typename NeighborEntryType>
    bool BuildGraphFromLODData(
        const FSkeletalMeshLODRenderData& LODData,
        const float                       CoincidentVertexNeighborTolerance,
        TArray<NeighborEntryType>&        OutNeighborGraph,
        FString*                          OutErrorMessage)
    {
        const int32 VertexCount = LODData.GetNumVertices();
        if (VertexCount <= 0)
        {
            OutNeighborGraph.Reset();
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh LOD has no render vertices."));
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
        if (IndexBuffer.Num() == 0)
        {
            OutNeighborGraph.Reset();
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh LOD index buffer is empty."));
            return false;
        }

        OutNeighborGraph.Reset();
        OutNeighborGraph.SetNum(VertexCount);

        for (int32 Index = 0; Index + 2 < IndexBuffer.Num(); Index += 3)
        {
            const int32 V0 = static_cast<int32>(IndexBuffer[Index]);
            const int32 V1 = static_cast<int32>(IndexBuffer[Index + 1]);
            const int32 V2 = static_cast<int32>(IndexBuffer[Index + 2]);

            AddNeighbor(OutNeighborGraph, V0, V1);
            AddNeighbor(OutNeighborGraph, V1, V0);
            AddNeighbor(OutNeighborGraph, V1, V2);
            AddNeighbor(OutNeighborGraph, V2, V1);
            AddNeighbor(OutNeighborGraph, V2, V0);
            AddNeighbor(OutNeighborGraph, V0, V2);
        }

        ConnectCoincidentPositionNeighbors(LODData, CoincidentVertexNeighborTolerance, OutNeighborGraph);
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }
} // namespace

bool FWetNeighborGraphBuilder::BuildRuntimeGraph(
    const FSkeletalMeshLODRenderData& LODData,
    const float                       InCoincidentVertexNeighborTolerance,
    TArray<FWetVertexNeighbors>&      OutNeighborGraph,
    FString*                          OutErrorMessage)
{
    return BuildGraphFromLODData(LODData, InCoincidentVertexNeighborTolerance, OutNeighborGraph, OutErrorMessage);
}

bool FWetNeighborGraphBuilder::BuildBakedGraph(
    const FSkeletalMeshLODRenderData&              LODData,
    const float                                    InCoincidentVertexNeighborTolerance,
    TArray<FWetClothingAssetBakedVertexNeighbors>& OutNeighborGraph,
    FString*                                       OutErrorMessage)
{
    return BuildGraphFromLODData(LODData, InCoincidentVertexNeighborTolerance, OutNeighborGraph, OutErrorMessage);
}
