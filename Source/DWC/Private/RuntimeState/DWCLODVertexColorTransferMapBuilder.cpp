//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"

#include "Math/GenericOctree.h"
#include "Utility/DWCProfiling.h"

namespace
{
    constexpr float DistanceTieToleranceSquared = KINDA_SMALL_NUMBER;

    struct FDWCLODVertexOctreeElement
    {
        int32 VertexIndex = INDEX_NONE;
        FBoxCenterAndExtent Bounds;
    };

    struct FDWCLODVertexOctreeSemantics
    {
        enum { MaxElementsPerLeaf = 16 };
        enum { MinInclusiveElementsPerNode = 7 };
        enum { MaxNodeDepth = 12 };

        typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

        static const FBoxCenterAndExtent& GetBoundingBox(const FDWCLODVertexOctreeElement& Element)
        {
            return Element.Bounds;
        }

        static bool AreElementsEqual(const FDWCLODVertexOctreeElement& A, const FDWCLODVertexOctreeElement& B)
        {
            return A.VertexIndex == B.VertexIndex;
        }

        static void SetElementId(const FDWCLODVertexOctreeElement& Element, FOctreeElementId2 Id)
        {
        }

        static void ApplyOffset(FDWCLODVertexOctreeElement& Element, FVector Offset)
        {
            Element.Bounds.Center += Offset;
        }
    };

    using FDWCLODVertexOctree = TOctree2<FDWCLODVertexOctreeElement, FDWCLODVertexOctreeSemantics>;

    int32 FindBestSourceVertex(
        const FDWCLODVertexColorTransferGeometryView& Source,
        const FDWCLODVertexOctree&                    SourceOctree,
        const FVector3f&                              TargetPos,
        const FVector3f&                              TargetNormal)
    {
        int32 BestIndex = INDEX_NONE;
        float BestDistSq = TNumericLimits<float>::Max();
        float BestNormalDot = -1.0f;

        auto ConsiderSourceVertex = [&Source, &TargetPos, &TargetNormal, &BestIndex, &BestDistSq, &BestNormalDot](const int32 SourceIndex)
        {
            if (!Source.Positions.IsValidIndex(SourceIndex))
            {
                return;
            }

            float NormalDot = 1.0f;
            if (Source.Normals.IsValidIndex(SourceIndex))
            {
                NormalDot = FVector3f::DotProduct(Source.Normals[SourceIndex], TargetNormal);
            }

            const float DistSq = FVector3f::DistSquared(Source.Positions[SourceIndex], TargetPos);
            if (BestIndex == INDEX_NONE ||
                DistSq < BestDistSq - DistanceTieToleranceSquared ||
                (FMath::Abs(DistSq - BestDistSq) <= DistanceTieToleranceSquared && NormalDot > BestNormalDot))
            {
                BestIndex = SourceIndex;
                BestDistSq = DistSq;
                BestNormalDot = NormalDot;
            }
        };

        SourceOctree.FindNearbyElements(
            FVector(TargetPos),
            [&ConsiderSourceVertex](const FDWCLODVertexOctreeElement& Element)
            {
                ConsiderSourceVertex(Element.VertexIndex);
            });

        return BestIndex;
    }
}

bool BuildDWCLODVertexColorTransferMaps(
    const FDWCLODVertexColorTransferGeometryView& SourceGeometry,
    TConstArrayView<FDWCLODVertexColorTransferTargetGeometryView> TargetGeometries,
    TArray<FDWCLODVertexColorTransferMapBuildResult>& OutResults)
{
    DWC_PROFILE_SCOPE(DWC_BuildLODVertexColorTransferMap);

    OutResults.Reset();
    if (!SourceGeometry.IsValid() || TargetGeometries.IsEmpty())
    {
        return false;
    }

    FBox CompleteBounds(ForceInit);
    for (const FVector3f& SourcePosition : SourceGeometry.Positions)
    {
        CompleteBounds += FVector(SourcePosition);
    }
    for (const FDWCLODVertexColorTransferTargetGeometryView& TargetGeometry : TargetGeometries)
    {
        if (!TargetGeometry.Geometry.IsValid())
        {
            continue;
        }

        for (const FVector3f& TargetPosition : TargetGeometry.Geometry.Positions)
        {
            CompleteBounds += FVector(TargetPosition);
        }
    }

    if (!CompleteBounds.IsValid)
    {
        return false;
    }

    const double OctreeExtent = CompleteBounds.GetExtent().GetMax();
    FDWCLODVertexOctree SourceOctree(CompleteBounds.GetCenter(), OctreeExtent);

    {
        DWC_PROFILE_SCOPE(DWC_BuildLODVertexColorTransferMap_Octree);

        for (int32 SourceIndex = 0; SourceIndex < SourceGeometry.Positions.Num(); ++SourceIndex)
        {
            const FVector SourcePosition(SourceGeometry.Positions[SourceIndex]);
            SourceOctree.AddElement({SourceIndex, FBoxCenterAndExtent(SourcePosition, FVector::ZeroVector)});
        }
    }

    for (const FDWCLODVertexColorTransferTargetGeometryView& TargetGeometry : TargetGeometries)
    {
        if (!TargetGeometry.Geometry.IsValid())
        {
            continue;
        }

        FDWCLODVertexColorTransferMapBuildResult& Result = OutResults.AddDefaulted_GetRef();
        Result.LODIndex = TargetGeometry.LODIndex;
        Result.TargetToSourceVertex.SetNumUninitialized(TargetGeometry.Geometry.Positions.Num());
        for (int32 VertexIndex = 0; VertexIndex < TargetGeometry.Geometry.Positions.Num(); ++VertexIndex)
        {
            const FVector3f Normal = TargetGeometry.Geometry.Normals.IsValidIndex(VertexIndex)
                                         ? TargetGeometry.Geometry.Normals[VertexIndex]
                                         : FVector3f::UpVector;

            Result.TargetToSourceVertex[VertexIndex] = FindBestSourceVertex(
                SourceGeometry,
                SourceOctree,
                TargetGeometry.Geometry.Positions[VertexIndex],
                Normal);
        }
    }

    return !OutResults.IsEmpty();
}
