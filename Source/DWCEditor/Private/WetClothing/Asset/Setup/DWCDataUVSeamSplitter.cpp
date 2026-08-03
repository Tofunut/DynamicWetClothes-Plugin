#include "DWCDataUVSeamSplitter.h"

#include "SkeletalMeshAttributes.h"

namespace DWCDataUVSeamSplitterPrivate
{
    static uint64 MakeChartVertexInstanceKey(const int32 ChartIndex, const int32 VertexInstanceIndex)
    {
        return (static_cast<uint64>(static_cast<uint32>(ChartIndex)) << 32) |
               static_cast<uint32>(VertexInstanceIndex);
    }

    static void SetFailure(FDWCDataUVSeamSplitResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }
}

FDWCDataUVSeamSplitResult FDWCDataUVSeamSplitter::SplitChartBoundaries(
    FMeshDescription& MeshDescription,
    TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& Charts)
{
    using namespace DWCDataUVSeamSplitterPrivate;

    FDWCDataUVSeamSplitResult Result;
    if (Triangles.IsEmpty() || Charts.IsEmpty())
    {
        Result.bSucceeded = true;
        return Result;
    }

    TMap<int32, int32> ChartIndexByTriangle;
    TMap<int32, TArray<int32>> ChartIndicesByVertexInstance;
    TMap<int32, int32> TriangleIndexByPolygon;
    for (int32 ChartIndex = 0; ChartIndex < Charts.Num(); ++ChartIndex)
    {
        for (const int32 TriangleIndex : Charts[ChartIndex].TriangleIndices)
        {
            if (!Triangles.IsValidIndex(TriangleIndex))
            {
                SetFailure(Result, TEXT("A DWC UV Channel chart references an invalid triangle."));
                return Result;
            }

            if (const int32* ExistingChartIndex = ChartIndexByTriangle.Find(TriangleIndex))
            {
                SetFailure(Result, FString::Printf(
                    TEXT("Triangle %d belongs to more than one DWC UV Channel chart (%d and %d)."),
                    TriangleIndex,
                    *ExistingChartIndex,
                    ChartIndex));
                return Result;
            }
            ChartIndexByTriangle.Add(TriangleIndex, ChartIndex);

            const FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(Triangles[TriangleIndex].TriangleID);
            if (PolygonID.GetValue() == INDEX_NONE ||
                MeshDescription.GetPolygonTriangles(PolygonID).Num() != 1 ||
                MeshDescription.GetPolygonVertexInstances(PolygonID).Num() != 3 ||
                TriangleIndexByPolygon.Contains(PolygonID.GetValue()))
            {
                SetFailure(Result, TEXT("DWC UV Channel seam splitting requires triangulated mesh polygons."));
                return Result;
            }
            TriangleIndexByPolygon.Add(PolygonID.GetValue(), TriangleIndex);

            for (const FVertexInstanceID VertexInstanceID : Triangles[TriangleIndex].VertexInstances)
            {
                if (VertexInstanceID.GetValue() == INDEX_NONE)
                {
                    SetFailure(Result, TEXT("A DWC UV Channel triangle references an invalid VertexInstance."));
                    return Result;
                }

                TArray<int32>& VertexInstanceCharts = ChartIndicesByVertexInstance.FindOrAdd(VertexInstanceID.GetValue());
                VertexInstanceCharts.AddUnique(ChartIndex);
            }
        }
    }

    TMap<uint64, FVertexInstanceID> VertexInstanceByChart;
    for (TPair<int32, TArray<int32>>& Pair : ChartIndicesByVertexInstance)
    {
        TArray<int32>& VertexInstanceCharts = Pair.Value;
        VertexInstanceCharts.Sort();
        const FVertexInstanceID OriginalVertexInstanceID(Pair.Key);
        VertexInstanceByChart.Add(
            MakeChartVertexInstanceKey(VertexInstanceCharts[0], Pair.Key),
            OriginalVertexInstanceID);
    }

    FSkeletalMeshAttributes Attributes(MeshDescription);
    Attributes.Register(true);
    auto VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    auto VertexInstanceTangents = Attributes.GetVertexInstanceTangents();
    auto VertexInstanceBinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    auto VertexInstanceColors = Attributes.GetVertexInstanceColors();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    const int32 UVChannelCount = VertexInstanceUVs.GetNumChannels();
    const TArray<FName> MorphTargetNames = Attributes.GetMorphTargetNames();

    for (const TPair<int32, TArray<int32>>& Pair : ChartIndicesByVertexInstance)
    {
        const int32 OriginalVertexInstanceIndex = Pair.Key;
        const TArray<int32>& VertexInstanceCharts = Pair.Value;
        const FVertexInstanceID OriginalVertexInstanceID(OriginalVertexInstanceIndex);

        for (int32 ChartListIndex = 1; ChartListIndex < VertexInstanceCharts.Num(); ++ChartListIndex)
        {
            const int32 ChartIndex = VertexInstanceCharts[ChartListIndex];
            const FVertexID ParentVertexID = MeshDescription.GetVertexInstanceVertex(OriginalVertexInstanceID);
            const FVertexInstanceID SplitVertexInstanceID = MeshDescription.CreateVertexInstance(ParentVertexID);
            VertexInstanceNormals[SplitVertexInstanceID] = VertexInstanceNormals[OriginalVertexInstanceID];
            VertexInstanceTangents[SplitVertexInstanceID] = VertexInstanceTangents[OriginalVertexInstanceID];
            VertexInstanceBinormalSigns[SplitVertexInstanceID] = VertexInstanceBinormalSigns[OriginalVertexInstanceID];
            VertexInstanceColors[SplitVertexInstanceID] = VertexInstanceColors[OriginalVertexInstanceID];
            for (int32 UVChannelIndex = 0; UVChannelIndex < UVChannelCount; ++UVChannelIndex)
            {
                VertexInstanceUVs.Set(
                    SplitVertexInstanceID,
                    UVChannelIndex,
                    VertexInstanceUVs.Get(OriginalVertexInstanceID, UVChannelIndex));
            }
            for (const FName MorphTargetName : MorphTargetNames)
            {
                auto MorphNormalDeltas = Attributes.GetVertexInstanceMorphNormalDelta(MorphTargetName);
                if (MorphNormalDeltas.IsValid())
                {
                    MorphNormalDeltas[SplitVertexInstanceID] = MorphNormalDeltas[OriginalVertexInstanceID];
                }
            }

            VertexInstanceByChart.Add(
                MakeChartVertexInstanceKey(ChartIndex, OriginalVertexInstanceIndex),
                SplitVertexInstanceID);
            ++Result.SplitVertexInstanceCount;
        }
    }

    for (const TPair<int32, int32>& Pair : TriangleIndexByPolygon)
    {
        const FPolygonID PolygonID(Pair.Key);
        FDWCDataUVTriangle& Triangle = Triangles[Pair.Value];
        const int32 ChartIndex = ChartIndexByTriangle.FindChecked(Pair.Value);
        TMap<int32, FVertexInstanceID> ReplacementByOriginalVertexInstance;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVertexInstanceID OriginalVertexInstanceID = Triangle.VertexInstances[CornerIndex];
            const FVertexInstanceID* ReplacementVertexInstanceID = VertexInstanceByChart.Find(
                MakeChartVertexInstanceKey(ChartIndex, OriginalVertexInstanceID.GetValue()));
            if (ReplacementVertexInstanceID == nullptr)
            {
                SetFailure(Result, TEXT("Failed to resolve a chart-specific DWC UV Channel VertexInstance."));
                return Result;
            }

            ReplacementByOriginalVertexInstance.Add(OriginalVertexInstanceID.GetValue(), *ReplacementVertexInstanceID);
            Triangle.VertexInstances[CornerIndex] = *ReplacementVertexInstanceID;
        }

        TArray<FVertexInstanceID> PolygonVertexInstances = MeshDescription.GetPolygonVertexInstances(PolygonID);
        for (FVertexInstanceID& VertexInstanceID : PolygonVertexInstances)
        {
            if (const FVertexInstanceID* ReplacementVertexInstanceID = ReplacementByOriginalVertexInstance.Find(VertexInstanceID.GetValue()))
            {
                VertexInstanceID = *ReplacementVertexInstanceID;
            }
        }
        MeshDescription.SetPolygonVertexInstances(PolygonID, PolygonVertexInstances);

        // SetPolygonVertexInstances rebuilds the polygon triangles. Keep the transient
        // generation record aligned with the new MeshDescription IDs for later stages.
        const TArrayView<const FTriangleID> RebuiltTriangleIDs = MeshDescription.GetPolygonTriangles(PolygonID);
        if (RebuiltTriangleIDs.Num() != 1)
        {
            SetFailure(Result, TEXT("DWC UV Channel seam splitting could not rebuild a triangulated polygon."));
            return Result;
        }
        Triangle.TriangleID = RebuiltTriangleIDs[0];
        ++Result.AffectedPolygonCount;
    }

    Result.bSucceeded = true;
    Result.Message = FString::Printf(
        TEXT("Created %d chart-boundary VertexInstance seam(s) across %d polygon(s)."),
        Result.SplitVertexInstanceCount,
        Result.AffectedPolygonCount);
    return Result;
}
