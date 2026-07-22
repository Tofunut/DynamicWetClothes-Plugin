#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Math/GenericOctree.h"
#include "Utility/DWCProfiling.h"

namespace
{
    struct FDWCTransferMapOctreeElement
    {
        int32 VertexIndex = INDEX_NONE;
        FBoxCenterAndExtent Bounds;
    };

    struct FDWCTransferMapOctreeSemantics
    {
        enum { MaxElementsPerLeaf = 16 };
        enum { MinInclusiveElementsPerNode = 7 };
        enum { MaxNodeDepth = 12 };

        typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

        static const FBoxCenterAndExtent& GetBoundingBox(const FDWCTransferMapOctreeElement& Element)
        {
            return Element.Bounds;
        }

        static bool AreElementsEqual(const FDWCTransferMapOctreeElement& A, const FDWCTransferMapOctreeElement& B)
        {
            return A.VertexIndex == B.VertexIndex;
        }

        static void SetElementId(const FDWCTransferMapOctreeElement& Element, FOctreeElementId2 Id)
        {
        }

        static void ApplyOffset(FDWCTransferMapOctreeElement& Element, FVector Offset)
        {
            Element.Bounds.Center += Offset;
        }
    };

    using FDWCTransferMapOctree = TOctree2<FDWCTransferMapOctreeElement, FDWCTransferMapOctreeSemantics>;

    int32 FindBestSourceVertex(
        const FDWCLODVertexColorTransferGeometryView& Source,
        const FDWCTransferMapOctree&                  SourceOctree,
        const FVector3f&                              TargetPos,
        const FVector3f&                              TargetNormal,
        const FDWCLODVertexColorTransferSettings&     Settings)
    {
        int32 BestIndex = INDEX_NONE;
        float BestDistSq = TNumericLimits<float>::Max();
        float BestNormalDot = -1.0f;

        auto ConsiderSourceVertex = [&Source, &TargetPos, &TargetNormal, &Settings, &BestIndex, &BestDistSq, &BestNormalDot](const int32 SourceIndex)
        {
            if (!Source.Positions.IsValidIndex(SourceIndex))
            {
                return;
            }

            float NormalDot = 1.0f;
            if (Source.Normals.IsValidIndex(SourceIndex))
            {
                NormalDot = FVector3f::DotProduct(Source.Normals[SourceIndex], TargetNormal);
                if (NormalDot < Settings.MaxNormalAngleDot)
                {
                    return;
                }
            }

            const float DistSq = FVector3f::DistSquared(Source.Positions[SourceIndex], TargetPos);
            const float TieToleranceSq = FMath::Square(FMath::Max(Settings.DistanceTieTolerance, 0.0f));

            if (BestIndex == INDEX_NONE ||
                DistSq < BestDistSq - TieToleranceSq ||
                (FMath::Abs(DistSq - BestDistSq) <= TieToleranceSq && NormalDot > BestNormalDot))
            {
                BestIndex = SourceIndex;
                BestDistSq = DistSq;
                BestNormalDot = NormalDot;
            }
        };

        const float InitialRadius = FMath::Max(Settings.InitialSearchRadius, Settings.DistanceTieTolerance);
        const float MaxRadius = FMath::Max(Settings.MaxSearchRadius, InitialRadius);

        for (float SearchRadius = InitialRadius; SearchRadius <= MaxRadius && BestIndex == INDEX_NONE; SearchRadius *= 2.0f)
        {
            const FVector QueryCenter(TargetPos);
            const FVector QueryExtent(SearchRadius, SearchRadius, SearchRadius);
            SourceOctree.FindElementsWithBoundsTest(
                FBoxCenterAndExtent(QueryCenter, QueryExtent),
                [&ConsiderSourceVertex](const FDWCTransferMapOctreeElement& Element)
                {
                    ConsiderSourceVertex(Element.VertexIndex);
                });
        }

        return BestIndex;
    }
}

bool BuildDWCLODVertexColorTransferMap(
    const FDWCLODVertexColorTransferGeometryView& SourceGeometry,
    const FDWCLODVertexColorTransferGeometryView& TargetGeometry,
    const FDWCLODVertexColorTransferSettings&     Settings,
    TArray<int32>&                                OutTargetToSourceVertex)
{
    DWC_PROFILE_SCOPE(DWC_BuildLODVertexColorTransferMap);

    OutTargetToSourceVertex.Reset();
    if (!SourceGeometry.IsValid() || !TargetGeometry.IsValid())
    {
        return false;
    }

    FBox CompleteBounds(ForceInit);
    for (const FVector3f& SourcePosition : SourceGeometry.Positions)
    {
        CompleteBounds += FVector(SourcePosition);
    }
    for (const FVector3f& TargetPosition : TargetGeometry.Positions)
    {
        CompleteBounds += FVector(TargetPosition);
    }

    if (!CompleteBounds.IsValid)
    {
        return false;
    }

    const FVector CompleteExtent = CompleteBounds.GetExtent();
    const double OctreeExtent = CompleteExtent.GetMax() + FMath::Max(1.0f, Settings.MaxSearchRadius);
    FDWCTransferMapOctree SourceOctree(CompleteBounds.GetCenter(), OctreeExtent);

    {
        DWC_PROFILE_SCOPE(DWC_BuildLODVertexColorTransferMap_Octree);

        for (int32 SourceIndex = 0; SourceIndex < SourceGeometry.Positions.Num(); ++SourceIndex)
        {
            const FVector SourcePosition(SourceGeometry.Positions[SourceIndex]);
            SourceOctree.AddElement({SourceIndex, FBoxCenterAndExtent(SourcePosition, FVector::ZeroVector)});
        }
    }

    OutTargetToSourceVertex.SetNumUninitialized(TargetGeometry.Positions.Num());
    for (int32 VertexIndex = 0; VertexIndex < TargetGeometry.Positions.Num(); ++VertexIndex)
    {
        const FVector3f Normal = TargetGeometry.Normals.IsValidIndex(VertexIndex)
                                     ? TargetGeometry.Normals[VertexIndex]
                                     : FVector3f::UpVector;

        OutTargetToSourceVertex[VertexIndex] = FindBestSourceVertex(
            SourceGeometry,
            SourceOctree,
            TargetGeometry.Positions[VertexIndex],
            Normal,
            Settings);
    }

    return true;
}

bool BuildDWCLODVertexColorTransferMap(
    const FDWCLODVertexStaticData&            SourceLODData,
    const FDWCLODVertexStaticData&            TargetLODData,
    const FDWCLODVertexColorTransferSettings& Settings,
    TArray<int32>&                            OutTargetToSourceVertex)
{
    if (!SourceLODData.IsValid() || !TargetLODData.IsValid())
    {
        OutTargetToSourceVertex.Reset();
        return false;
    }

    const FDWCLODVertexColorTransferGeometryView SourceGeometry{
        SourceLODData.Geometry.LocalPositions,
        SourceLODData.Geometry.LocalNormals
    };
    const FDWCLODVertexColorTransferGeometryView TargetGeometry{
        TargetLODData.Geometry.LocalPositions,
        TargetLODData.Geometry.LocalNormals
    };
    return BuildDWCLODVertexColorTransferMap(SourceGeometry, TargetGeometry, Settings, OutTargetToSourceVertex);
}
