//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"

#include "Algo/Sort.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationFieldBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationResolver.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVPreviewTriangleReader.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorSpatialQuery, Log, All);

namespace
{
    const FName SpatialCacheNamespace(TEXT("SpatialQuery"));
    constexpr int32 BVHLeafTriangleCount = 8;
    constexpr int64 FallbackTopologyVertexBase = static_cast<int64>(MAX_uint32) + 1;

    struct FQuantizedSurfaceVertexKey
    {
        FIntVector Position = FIntVector::ZeroValue;
        FIntVector Normal = FIntVector::ZeroValue;

        bool operator==(const FQuantizedSurfaceVertexKey& Other) const
        {
            return Position == Other.Position && Normal == Other.Normal;
        }
    };

    uint32 GetTypeHash(const FQuantizedSurfaceVertexKey& Key)
    {
        return HashCombine(GetTypeHash(Key.Position), GetTypeHash(Key.Normal));
    }

    struct FTopologyEdgeKey
    {
        int64 VertexA = INDEX_NONE;
        int64 VertexB = INDEX_NONE;

        bool operator==(const FTopologyEdgeKey& Other) const
        {
            return VertexA == Other.VertexA && VertexB == Other.VertexB;
        }
    };

    uint32 GetTypeHash(const FTopologyEdgeKey& Key)
    {
        const uint32 HashA = static_cast<uint32>(Key.VertexA) ^
            static_cast<uint32>(static_cast<uint64>(Key.VertexA) >> 32);
        const uint32 HashB = static_cast<uint32>(Key.VertexB) ^
            static_cast<uint32>(static_cast<uint64>(Key.VertexB) >> 32);
        return HashCombine(HashA, HashB);
    }

    struct FTopologyEdgeReference
    {
        int32 TriangleIndex = INDEX_NONE;
        int32 EdgeIndex = INDEX_NONE;
        FVector2f UVAtA = FVector2f::ZeroVector;
        FVector2f UVAtB = FVector2f::ZeroVector;
    };

    uint64 MakeTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    FVector3f AnyPerpendicular(const FVector3f& Normal)
    {
        FVector3f Result = FVector3f::CrossProduct(Normal, FVector3f(0.0f, 0.0f, 1.0f)).GetSafeNormal();
        if (Result.IsNearlyZero())
        {
            Result = FVector3f::CrossProduct(Normal, FVector3f(0.0f, 1.0f, 0.0f)).GetSafeNormal();
        }
        return Result.IsNearlyZero() ? FVector3f(1.0f, 0.0f, 0.0f) : Result;
    }

    FVector AnyPerpendicular(const FVector& Normal)
    {
        FVector Result = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
        if (Result.IsNearlyZero())
        {
            Result = FVector::CrossProduct(Normal, FVector::RightVector).GetSafeNormal();
        }
        return Result.IsNearlyZero() ? FVector::ForwardVector : Result;
    }

    bool DoesSegmentIntersectBox(
        const FBox3f& Box,
        const FVector3f& SegmentStart,
        const FVector3f& SegmentEnd)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        const FVector3f Delta = SegmentEnd - SegmentStart;
        float Entry = 0.0f;
        float Exit = 1.0f;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (FMath::IsNearlyZero(Delta[Axis]))
            {
                if (SegmentStart[Axis] < Box.Min[Axis] || SegmentStart[Axis] > Box.Max[Axis])
                {
                    return false;
                }
                continue;
            }

            float AxisEntry = (Box.Min[Axis] - SegmentStart[Axis]) / Delta[Axis];
            float AxisExit = (Box.Max[Axis] - SegmentStart[Axis]) / Delta[Axis];
            if (AxisEntry > AxisExit)
            {
                Swap(AxisEntry, AxisExit);
            }
            Entry = FMath::Max(Entry, AxisEntry);
            Exit = FMath::Min(Exit, AxisExit);
            if (Entry > Exit)
            {
                return false;
            }
        }
        return Exit >= 0.0f && Entry <= 1.0f;
    }

    bool IntersectLocalTriangle(
        const FVector3f& SegmentStart,
        const FVector3f& SegmentEnd,
        const FVector3f& A,
        const FVector3f& B,
        const FVector3f& C,
        float& OutSegmentT,
        FVector3f& OutBarycentric)
    {
        const FVector3f SegmentDelta = SegmentEnd - SegmentStart;
        const FVector3f EdgeAB = B - A;
        const FVector3f EdgeAC = C - A;
        const FVector3f P = FVector3f::CrossProduct(SegmentDelta, EdgeAC);
        const float Determinant = FVector3f::DotProduct(EdgeAB, P);
        if (FMath::Abs(Determinant) <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const float InverseDeterminant = 1.0f / Determinant;
        const FVector3f T = SegmentStart - A;
        const float BarycentricB = FVector3f::DotProduct(T, P) * InverseDeterminant;
        if (BarycentricB < -UE_KINDA_SMALL_NUMBER || BarycentricB > 1.0f + UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }
        const FVector3f Q = FVector3f::CrossProduct(T, EdgeAB);
        const float BarycentricC = FVector3f::DotProduct(SegmentDelta, Q) * InverseDeterminant;
        if (BarycentricC < -UE_KINDA_SMALL_NUMBER ||
            BarycentricB + BarycentricC > 1.0f + UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }
        const float SegmentT = FVector3f::DotProduct(EdgeAC, Q) * InverseDeterminant;
        if (SegmentT < 0.0f || SegmentT > 1.0f)
        {
            return false;
        }

        OutSegmentT = SegmentT;
        OutBarycentric = FVector3f(1.0f - BarycentricB - BarycentricC, BarycentricB, BarycentricC);
        return true;
    }

    FVector3f ComputeBarycentric2D(
        const FVector2f& Point,
        const FVector2f& A,
        const FVector2f& B,
        const FVector2f& C)
    {
        const FVector2f V0 = B - A;
        const FVector2f V1 = C - A;
        const FVector2f V2 = Point - A;
        const float Denominator = V0.X * V1.Y - V1.X * V0.Y;
        if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
        {
            return FVector3f(-1.0f, -1.0f, -1.0f);
        }
        const float BarycentricB = (V2.X * V1.Y - V1.X * V2.Y) / Denominator;
        const float BarycentricC = (V0.X * V2.Y - V2.X * V0.Y) / Denominator;
        return FVector3f(1.0f - BarycentricB - BarycentricC, BarycentricB, BarycentricC);
    }

    bool IsBarycentricInside(const FVector3f& Barycentric)
    {
        constexpr float Tolerance = 0.0001f;
        return Barycentric.X >= -Tolerance &&
            Barycentric.Y >= -Tolerance &&
            Barycentric.Z >= -Tolerance;
    }

    void ComputeSurfaceUVFrame(FDWCEditorSpatialTriangle& Triangle)
    {
        const FVector3f Edge1 = Triangle.LocalPositions[1] - Triangle.LocalPositions[0];
        const FVector3f Edge2 = Triangle.LocalPositions[2] - Triangle.LocalPositions[0];
        const FVector2f UVEdge1 = Triangle.UVs[1] - Triangle.UVs[0];
        const FVector2f UVEdge2 = Triangle.UVs[2] - Triangle.UVs[0];
        const float Determinant = UVEdge1.X * UVEdge2.Y - UVEdge1.Y * UVEdge2.X;
        if (FMath::Abs(Determinant) <= UE_SMALL_NUMBER)
        {
            Triangle.LocalSurfaceAxisU = Triangle.LocalTangent;
            Triangle.LocalSurfaceAxisV = Triangle.LocalBitangent;
            Triangle.SurfaceUnitsPerUV = FVector2f::ZeroVector;
            return;
        }

        const float InverseDeterminant = 1.0f / Determinant;
        const FVector3f DPDU = (Edge1 * UVEdge2.Y - Edge2 * UVEdge1.Y) * InverseDeterminant;
        const FVector3f DPDV = (Edge2 * UVEdge1.X - Edge1 * UVEdge2.X) * InverseDeterminant;
        Triangle.SurfaceUnitsPerUV = FVector2f(DPDU.Size(), DPDV.Size());
        Triangle.LocalSurfaceAxisU = DPDU.GetSafeNormal();
        Triangle.LocalSurfaceAxisV = DPDV.GetSafeNormal();
        if (Triangle.LocalSurfaceAxisU.IsNearlyZero())
        {
            Triangle.LocalSurfaceAxisU = Triangle.LocalTangent;
        }
        if (Triangle.LocalSurfaceAxisV.IsNearlyZero())
        {
            Triangle.LocalSurfaceAxisV = Triangle.LocalBitangent;
        }
    }

    float WrapUV(const float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    struct FDWCEditorResolvedSpatialFrames
    {
        FVector3f LocalNormal = FVector3f::ZeroVector;
        FVector3f RenderFrameU = FVector3f::ZeroVector;
        FVector3f RenderFrameV = FVector3f::ZeroVector;
        FVector3f AuthoringFrameU = FVector3f::ZeroVector;
        FVector3f AuthoringFrameV = FVector3f::ZeroVector;
    };

    bool ResolveSpatialSurfaceFrames(
        const FDWCEditorSpatialData& SpatialData,
        const int32 TriangleIndex,
        const FVector3f& Barycentric,
        const FDWCEditorSurfaceOrientationPolicy& OrientationPolicy,
        FDWCEditorResolvedSpatialFrames& OutFrames)
    {
        if (!SpatialData.Triangles.IsValidIndex(TriangleIndex))
        {
            OutFrames = {};
            return false;
        }

        const FDWCEditorSpatialTriangle& Triangle = SpatialData.Triangles[TriangleIndex];
        OutFrames.LocalNormal = (
            Triangle.LocalNormals[0] * Barycentric.X +
            Triangle.LocalNormals[1] * Barycentric.Y +
            Triangle.LocalNormals[2] * Barycentric.Z).GetSafeNormal(
                UE_SMALL_NUMBER,
                Triangle.LocalNormal);
        const FVector3f InterpolatedTangent =
            Triangle.LocalTangents[0] * Barycentric.X +
            Triangle.LocalTangents[1] * Barycentric.Y +
            Triangle.LocalTangents[2] * Barycentric.Z;
        const FVector3f InterpolatedBitangent =
            Triangle.LocalBitangents[0] * Barycentric.X +
            Triangle.LocalBitangents[1] * Barycentric.Y +
            Triangle.LocalBitangents[2] * Barycentric.Z;
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            OutFrames.LocalNormal,
            InterpolatedTangent,
            InterpolatedBitangent,
            OutFrames.RenderFrameU,
            OutFrames.RenderFrameV);

        FDWCEditorResolvedSurfaceOrientation AuthoringOrientation;
        if (FDWCEditorSurfaceOrientationResolver::Resolve(
                SpatialData,
                TriangleIndex,
                Barycentric,
                OutFrames.LocalNormal,
                OrientationPolicy,
                AuthoringOrientation))
        {
            OutFrames.AuthoringFrameU = AuthoringOrientation.FrameU;
            OutFrames.AuthoringFrameV = AuthoringOrientation.FrameV;
        }
        else
        {
            OutFrames.AuthoringFrameU = OutFrames.RenderFrameU;
            OutFrames.AuthoringFrameV = OutFrames.RenderFrameV;
        }
        return true;
    }

    void FillProjectedSurface(
        const FDWCEditorSpatialData& SpatialData,
        const int32 TriangleIndex,
        const FVector3f& Barycentric,
        const FTransform& ComponentTransform,
        const FDWCEditorSurfaceOrientationPolicy& OrientationPolicy,
        FDWCEditorProjectedSurface& OutSurface)
    {
        FDWCEditorResolvedSpatialFrames Frames;
        if (!ResolveSpatialSurfaceFrames(
                SpatialData,
                TriangleIndex,
                Barycentric,
                OrientationPolicy,
                Frames))
        {
            OutSurface = {};
            return;
        }
        const FDWCEditorSpatialTriangle& Triangle = SpatialData.Triangles[TriangleIndex];
        const FVector3f LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        OutSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutSurface.TriangleID = Triangle.TriangleID;
        OutSurface.UVIslandID = Triangle.UVIslandID;
        OutSurface.Barycentric = FVector(Barycentric);
        OutSurface.UV = FVector2D(
            Triangle.UVs[0] * Barycentric.X +
            Triangle.UVs[1] * Barycentric.Y +
            Triangle.UVs[2] * Barycentric.Z);

        OutSurface.LocalPosition = FVector(LocalPosition);
        OutSurface.LocalNormal = FVector(Frames.LocalNormal);
        OutSurface.LocalTangent = FVector(Frames.RenderFrameU);
        OutSurface.LocalBitangent = FVector(Frames.RenderFrameV);
        OutSurface.LocalSurfaceAxisU = FVector(Triangle.LocalSurfaceAxisU);
        OutSurface.LocalSurfaceAxisV = FVector(Triangle.LocalSurfaceAxisV);
        OutSurface.LocalSurfaceFrameU = FVector(Frames.AuthoringFrameU);
        OutSurface.LocalSurfaceFrameV = FVector(Frames.AuthoringFrameV);
        OutSurface.SurfaceUnitsPerUV = Triangle.SurfaceUnitsPerUV;
        OutSurface.WorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
        OutSurface.WorldNormal = ComponentTransform.TransformVectorNoScale(FVector(Frames.LocalNormal))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutSurface.WorldTangent = ComponentTransform.TransformVectorNoScale(FVector(Frames.RenderFrameU))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutSurface.WorldBitangent = ComponentTransform.TransformVectorNoScale(FVector(Frames.RenderFrameV))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
        OutSurface.WorldSurfaceFrameU = ComponentTransform.TransformVectorNoScale(
            FVector(Frames.AuthoringFrameU)).GetSafeNormal(
                UE_SMALL_NUMBER,
                FVector::ForwardVector);
        OutSurface.WorldSurfaceFrameV = ComponentTransform.TransformVectorNoScale(
            FVector(Frames.AuthoringFrameV)).GetSafeNormal(
                UE_SMALL_NUMBER,
                FVector::RightVector);
    }
}

FName FDWCEditorSpatialData::StaticCacheTypeName()
{
    static const FName Name(TEXT("DWCEditorSpatialData"));
    return Name;
}

uint64 FDWCEditorSpatialData::GetAllocatedSizeBytes() const
{
    uint64 Bytes = static_cast<uint64>(Triangles.GetAllocatedSize()) +
        static_cast<uint64>(TriangleLookup.GetAllocatedSize()) +
        static_cast<uint64>(BVHTriangleIndices.GetAllocatedSize()) +
        static_cast<uint64>(BVHNodes.GetAllocatedSize()) +
        static_cast<uint64>(UVTriangleGrid.GetAllocatedSize()) +
        SurfaceOrientationField.GetAllocatedSizeBytes();
    for (const TArray<int32>& Cell : UVTriangleGrid)
    {
        Bytes += static_cast<uint64>(Cell.GetAllocatedSize());
    }
    return Bytes;
}

FDWCEditorSpatialQueryService::FDWCEditorSpatialQueryService(
    TSharedRef<FDWCEditorCacheStore> InCacheStore,
    FDWCEditorSurfaceOrientationPolicy InOrientationPolicy)
    : CacheStore(MoveTemp(InCacheStore)),
      OrientationPolicy(MoveTemp(InOrientationPolicy))
{
    OrientationPolicy.Normalize();
}

FDWCEditorSpatialHandle FDWCEditorSpatialQueryService::Acquire(
    const UWetClothingAsset* WetClothingAsset,
    USkeletalMesh* Mesh,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FString* OutError)
{
    FDWCEditorSpatialLease Lease = AcquireLease(
        WetClothingAsset,
        Mesh,
        UVChannelIndex,
        MaterialSlotIndex,
        OutError);
    return StaticCastSharedPtr<const FDWCEditorSpatialData>(Lease.GetSharedValue());
}

FDWCEditorSpatialLease FDWCEditorSpatialQueryService::AcquireLease(
    const UWetClothingAsset* WetClothingAsset,
    USkeletalMesh* Mesh,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FString* OutError)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorSpatialQueryService_Acquire);
    check(IsInGameThread());

    constexpr int32 LODIndex = 0;
    const TOptional<FDWCEditorCacheKey> CacheKey = MakeCacheKey(
        WetClothingAsset,
        Mesh,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex);
    if (!CacheKey.IsSet())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The selected mesh, Data UV channel, or material slot is invalid.");
        }
        return FDWCEditorSpatialLease();
    }

    if (FDWCEditorSpatialLease Existing =
            CacheStore->FindLease<FDWCEditorSpatialData>(CacheKey.GetValue()))
    {
        return Existing;
    }

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuiltData =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    if (!BuildSpatialData(
            WetClothingAsset,
            Mesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltData.Get(),
            OutError))
    {
        return FDWCEditorSpatialLease();
    }

    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = BuiltData;
    CacheStore->Put(CacheKey.GetValue(), CacheValue);
    return CacheStore->FindLease<FDWCEditorSpatialData>(CacheKey.GetValue());
}

bool FDWCEditorSpatialQueryService::TraceSurface(
    const FDWCEditorSpatialHandle& Handle,
    const USkeletalMeshComponent* MeshComponent,
    const FVector& RayOrigin,
    const FVector& RayDirection,
    FDWCEditorSurfaceHit& OutHit) const
{
    OutHit = FDWCEditorSurfaceHit();
    if (!Handle.IsValid() || Handle->Triangles.IsEmpty() || MeshComponent == nullptr)
    {
        return false;
    }

    const FVector Direction = RayDirection.GetSafeNormal();
    if (Direction.IsNearlyZero())
    {
        return false;
    }

    const FTransform ComponentTransform = MeshComponent->GetComponentTransform();
    const FVector3f LocalRayOrigin(ComponentTransform.InverseTransformPosition(RayOrigin));
    const FVector3f LocalRayEnd(ComponentTransform.InverseTransformPosition(
        RayOrigin + Direction * 1000000.0f));
    float BestSegmentT = TNumericLimits<float>::Max();

    auto TestTriangle = [&](const int32 TriangleIndex)
    {
        if (!Handle->Triangles.IsValidIndex(TriangleIndex))
        {
            return;
        }
        const FDWCEditorSpatialTriangle& Triangle = Handle->Triangles[TriangleIndex];
        if (Triangle.LocalBounds.IsValid &&
            !DoesSegmentIntersectBox(Triangle.LocalBounds.ExpandBy(0.1f), LocalRayOrigin, LocalRayEnd))
        {
            return;
        }

        float SegmentT = 0.0f;
        FVector3f Barycentric = FVector3f::ZeroVector;
        if (!IntersectLocalTriangle(
                LocalRayOrigin,
                LocalRayEnd,
                Triangle.LocalPositions[0],
                Triangle.LocalPositions[1],
                Triangle.LocalPositions[2],
                SegmentT,
                Barycentric) ||
            SegmentT >= BestSegmentT)
        {
            return;
        }

        const FVector3f LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        FDWCEditorResolvedSpatialFrames Frames;
        if (!ResolveSpatialSurfaceFrames(
                *Handle,
                TriangleIndex,
                Barycentric,
                OrientationPolicy,
                Frames))
        {
            return;
        }

        const FVector WorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
        FVector WorldNormal = ComponentTransform.TransformVectorNoScale(FVector(Frames.LocalNormal)).GetSafeNormal();
        if (WorldNormal.IsNearlyZero())
        {
            WorldNormal = FVector::UpVector;
        }
        if (FVector::DotProduct(WorldNormal, Direction) > 0.0f)
        {
            WorldNormal *= -1.0f;
        }
        FVector WorldTangent = ComponentTransform.TransformVectorNoScale(FVector(Frames.RenderFrameU)).GetSafeNormal();
        WorldTangent = (WorldTangent - WorldNormal * FVector::DotProduct(WorldTangent, WorldNormal)).GetSafeNormal();
        if (WorldTangent.IsNearlyZero())
        {
            WorldTangent = AnyPerpendicular(WorldNormal);
        }
        FVector WorldBitangent = FVector::CrossProduct(WorldNormal, WorldTangent).GetSafeNormal();
        if (WorldBitangent.IsNearlyZero())
        {
            WorldBitangent = AnyPerpendicular(WorldNormal);
        }
        const FVector WorldSurfaceFrameU = ComponentTransform.TransformVectorNoScale(
            FVector(Frames.AuthoringFrameU)).GetSafeNormal(
                UE_SMALL_NUMBER,
                FVector::ForwardVector);
        const FVector WorldSurfaceFrameV = ComponentTransform.TransformVectorNoScale(
            FVector(Frames.AuthoringFrameV)).GetSafeNormal(
                UE_SMALL_NUMBER,
                FVector::RightVector);

        BestSegmentT = SegmentT;
        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVIslandID = Triangle.UVIslandID;
        OutHit.UVChannelIndex = Handle->UVChannelIndex;
        OutHit.WorldPosition = WorldPosition;
        OutHit.WorldNormal = WorldNormal;
        OutHit.WorldTangent = WorldTangent;
        OutHit.WorldBitangent = WorldBitangent;
        OutHit.WorldSurfaceFrameU = WorldSurfaceFrameU;
        OutHit.WorldSurfaceFrameV = WorldSurfaceFrameV;
        OutHit.LocalPosition = FVector(LocalPosition);
        OutHit.LocalNormal = FVector(Frames.LocalNormal);
        OutHit.LocalTangent = FVector(Frames.RenderFrameU);
        OutHit.LocalBitangent = FVector(Frames.RenderFrameV);
        OutHit.LocalSurfaceAxisU = FVector(Triangle.LocalSurfaceAxisU);
        OutHit.LocalSurfaceAxisV = FVector(Triangle.LocalSurfaceAxisV);
        OutHit.LocalSurfaceFrameU = FVector(Frames.AuthoringFrameU);
        OutHit.LocalSurfaceFrameV = FVector(Frames.AuthoringFrameV);
        OutHit.SurfaceUnitsPerUV = Triangle.SurfaceUnitsPerUV;
        OutHit.UV = FVector2D(
            Triangle.UVs[0] * Barycentric.X +
            Triangle.UVs[1] * Barycentric.Y +
            Triangle.UVs[2] * Barycentric.Z);
        OutHit.Barycentric = FVector(Barycentric);
        OutHit.DistanceSq = FVector::DistSquared(RayOrigin, WorldPosition);
    };

    TArray<int32, TInlineAllocator<64>> NodeStack;
    if (!Handle->BVHNodes.IsEmpty())
    {
        NodeStack.Add(0);
    }
    while (!NodeStack.IsEmpty())
    {
        const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
        if (!Handle->BVHNodes.IsValidIndex(NodeIndex))
        {
            continue;
        }
        const FDWCEditorSpatialBVHNode& Node = Handle->BVHNodes[NodeIndex];
        if (!Node.Bounds.IsValid ||
            !DoesSegmentIntersectBox(Node.Bounds.ExpandBy(0.1f), LocalRayOrigin, LocalRayEnd))
        {
            continue;
        }
        if (Node.IsLeaf())
        {
            for (int32 Offset = 0; Offset < Node.TriangleCount; ++Offset)
            {
                const int32 OrderedIndex = Node.FirstTriangleIndex + Offset;
                if (Handle->BVHTriangleIndices.IsValidIndex(OrderedIndex) &&
                    Handle->Triangles.IsValidIndex(Handle->BVHTriangleIndices[OrderedIndex]))
                {
                    TestTriangle(Handle->BVHTriangleIndices[OrderedIndex]);
                }
            }
        }
        else
        {
            if (Node.LeftChildIndex != INDEX_NONE) NodeStack.Add(Node.LeftChildIndex);
            if (Node.RightChildIndex != INDEX_NONE) NodeStack.Add(Node.RightChildIndex);
        }
    }

    if (Handle->BVHNodes.IsEmpty())
    {
        for (int32 TriangleIndex = 0; TriangleIndex < Handle->Triangles.Num(); ++TriangleIndex)
        {
            TestTriangle(TriangleIndex);
        }
    }
    return OutHit.bHit;
}

void FDWCEditorSpatialQueryService::FindSurfacesAtUV(
    const FDWCEditorSpatialHandle& Handle,
    const USkeletalMeshComponent* MeshComponent,
    const FVector2D& UV,
    TArray<FDWCEditorProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();
    if (!Handle.IsValid() || MeshComponent == nullptr)
    {
        return;
    }

    const FVector2D QueryUV(
        UV.X >= 0.0 && UV.X <= 1.0 ? UV.X : WrapUV(UV.X),
        UV.Y >= 0.0 && UV.Y <= 1.0 ? UV.Y : WrapUV(UV.Y));
    const int32 CellX = FMath::Clamp(
        FMath::FloorToInt(QueryUV.X * FDWCEditorSpatialData::UVGridResolution),
        0,
        FDWCEditorSpatialData::UVGridResolution - 1);
    const int32 CellY = FMath::Clamp(
        FMath::FloorToInt(QueryUV.Y * FDWCEditorSpatialData::UVGridResolution),
        0,
        FDWCEditorSpatialData::UVGridResolution - 1);
    const int32 CellIndex = CellY * FDWCEditorSpatialData::UVGridResolution + CellX;
    const TArray<int32>* Candidates = Handle->UVTriangleGrid.IsValidIndex(CellIndex)
        ? &Handle->UVTriangleGrid[CellIndex]
        : nullptr;
    const FVector2f QueryUV2f(QueryUV);
    const FTransform ComponentTransform = MeshComponent->GetComponentTransform();

    auto TestTriangle = [&](const int32 TriangleIndex)
    {
        if (!Handle->Triangles.IsValidIndex(TriangleIndex))
        {
            return;
        }
        const FDWCEditorSpatialTriangle& Triangle = Handle->Triangles[TriangleIndex];
        if (Handle->MaterialSlotIndex != INDEX_NONE &&
            Triangle.MaterialSlotIndex != Handle->MaterialSlotIndex)
        {
            return;
        }
        if (Triangle.UVBounds.bIsValid &&
            (QueryUV.X < Triangle.UVBounds.Min.X - 0.0001 || QueryUV.X > Triangle.UVBounds.Max.X + 0.0001 ||
             QueryUV.Y < Triangle.UVBounds.Min.Y - 0.0001 || QueryUV.Y > Triangle.UVBounds.Max.Y + 0.0001))
        {
            return;
        }

        const FVector3f Barycentric = ComputeBarycentric2D(
            QueryUV2f,
            Triangle.UVs[0],
            Triangle.UVs[1],
            Triangle.UVs[2]);
        if (!IsBarycentricInside(Barycentric))
        {
            return;
        }

        FDWCEditorProjectedSurface& Surface = OutSurfaces.AddDefaulted_GetRef();
        FillProjectedSurface(
            *Handle,
            TriangleIndex,
            Barycentric,
            ComponentTransform,
            OrientationPolicy,
            Surface);
    };

    if (Candidates != nullptr)
    {
        for (const int32 TriangleIndex : *Candidates)
        {
            if (Handle->Triangles.IsValidIndex(TriangleIndex))
            {
                TestTriangle(TriangleIndex);
            }
        }
    }
    else
    {
        for (int32 TriangleIndex = 0; TriangleIndex < Handle->Triangles.Num(); ++TriangleIndex)
        {
            TestTriangle(TriangleIndex);
        }
    }
}

bool FDWCEditorSpatialQueryService::ResolveTriangleAnchor(
    const FDWCEditorSpatialHandle& Handle,
    const USkeletalMeshComponent* MeshComponent,
    const int32 MaterialSlotIndex,
    const int32 TriangleID,
    const FVector3f& Barycentric,
    FDWCEditorProjectedSurface& OutSurface) const
{
    if (!Handle.IsValid() || MeshComponent == nullptr)
    {
        return false;
    }

    FVector3f NormalizedBarycentric;
    if (!NormalizeSurfaceAnchor(Barycentric, NormalizedBarycentric))
    {
        return false;
    }

    const int32* TriangleIndex = Handle->TriangleLookup.Find(
        MakeTriangleLookupKey(MaterialSlotIndex, TriangleID));
    if (TriangleIndex == nullptr || !Handle->Triangles.IsValidIndex(*TriangleIndex))
    {
        return false;
    }

    FillProjectedSurface(
        *Handle,
        *TriangleIndex,
        NormalizedBarycentric,
        MeshComponent->GetComponentTransform(),
        OrientationPolicy,
        OutSurface);
    return true;
}

bool FDWCEditorSpatialQueryService::ResolveUniqueSurfaceAtUV(
    const FDWCEditorSpatialHandle& Handle,
    const USkeletalMeshComponent* MeshComponent,
    const FVector2D& UV,
    FDWCEditorProjectedSurface& OutSurface) const
{
    TArray<FDWCEditorProjectedSurface> QueryResults;
    FindSurfacesAtUV(Handle, MeshComponent, UV, QueryResults);
    if (QueryResults.Num() != 1)
    {
        return false;
    }
    OutSurface = MoveTemp(QueryResults[0]);
    return true;
}

bool FDWCEditorSpatialQueryService::GetTriangleEdgeTopology(
    const FDWCEditorSpatialHandle& Handle,
    const int32 MaterialSlotIndex,
    const int32 TriangleID,
    const int32 EdgeIndex,
    FDWCEditorTriangleEdgeTopology& OutTopology) const
{
    OutTopology = FDWCEditorTriangleEdgeTopology();
    if (!Handle.IsValid() || EdgeIndex < 0 || EdgeIndex >= 3)
    {
        return false;
    }
    const int32* TriangleIndex = Handle->TriangleLookup.Find(
        MakeTriangleLookupKey(MaterialSlotIndex, TriangleID));
    if (TriangleIndex == nullptr || !Handle->Triangles.IsValidIndex(*TriangleIndex))
    {
        return false;
    }
    const FDWCEditorSpatialTriangle& Triangle = Handle->Triangles[*TriangleIndex];
    OutTopology.TriangleIndex = *TriangleIndex;
    OutTopology.EdgeIndex = EdgeIndex;
    OutTopology.AdjacentTriangleIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
    OutTopology.EdgeType = Triangle.EdgeTypes[EdgeIndex];
    return true;
}

bool FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
    const FVector3f& Barycentric,
    FVector3f& OutNormalizedBarycentric)
{
    constexpr float Tolerance = 0.001f;
    if (!FMath::IsFinite(Barycentric.X) ||
        !FMath::IsFinite(Barycentric.Y) ||
        !FMath::IsFinite(Barycentric.Z) ||
        Barycentric.X < -Tolerance ||
        Barycentric.Y < -Tolerance ||
        Barycentric.Z < -Tolerance)
    {
        return false;
    }

    OutNormalizedBarycentric = FVector3f(
        FMath::Max(0.0f, Barycentric.X),
        FMath::Max(0.0f, Barycentric.Y),
        FMath::Max(0.0f, Barycentric.Z));
    const float Sum = OutNormalizedBarycentric.X +
        OutNormalizedBarycentric.Y + OutNormalizedBarycentric.Z;
    if (!FMath::IsFinite(Sum) || Sum <= UE_SMALL_NUMBER)
    {
        return false;
    }
    OutNormalizedBarycentric /= Sum;
    return true;
}

bool FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
    const FVector3f& SurfaceNormal,
    const FVector3f& PreferredDirection,
    const FVector3f& PreferredBitangent,
    FVector3f& OutFrameU,
    FVector3f& OutFrameV)
{
    const FVector3f Normal = SurfaceNormal.GetSafeNormal();
    if (Normal.IsNearlyZero())
    {
        OutFrameU = FVector3f::ZeroVector;
        OutFrameV = FVector3f::ZeroVector;
        return false;
    }

    OutFrameU = PreferredDirection -
        Normal * FVector3f::DotProduct(PreferredDirection, Normal);
    OutFrameU.Normalize();
    if (OutFrameU.IsNearlyZero())
    {
        OutFrameU = AnyPerpendicular(Normal);
    }

    OutFrameV = FVector3f::CrossProduct(Normal, OutFrameU).GetSafeNormal();
    if (OutFrameV.IsNearlyZero())
    {
        OutFrameU = AnyPerpendicular(Normal);
        OutFrameV = FVector3f::CrossProduct(Normal, OutFrameU).GetSafeNormal();
    }
    if (!PreferredBitangent.IsNearlyZero() &&
        FVector3f::DotProduct(OutFrameV, PreferredBitangent) < 0.0f)
    {
        OutFrameU *= -1.0f;
        OutFrameV *= -1.0f;
    }
    return !OutFrameU.IsNearlyZero() && !OutFrameV.IsNearlyZero();
}

void FDWCEditorSpatialQueryService::InvalidateMesh(const USkeletalMesh* Mesh)
{
    CacheStore->InvalidateOwner(Mesh);
}

void FDWCEditorSpatialQueryService::Reset()
{
    CacheStore->InvalidateNamespace(SpatialCacheNamespace);
}

void FDWCEditorSpatialQueryService::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    CacheStore->AppendDiagnosticMemoryBucket(OutBuckets);
}

void FDWCEditorSpatialQueryService::ResetDiagnosticCounters()
{
    CacheStore->ResetDiagnosticCounters();
}

TOptional<FDWCEditorCacheKey> FDWCEditorSpatialQueryService::MakeCacheKey(
    const UWetClothingAsset* WetClothingAsset,
    const USkeletalMesh* Mesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex) const
{
    if (Mesh == nullptr || UVChannelIndex == INDEX_NONE || MaterialSlotIndex == INDEX_NONE)
    {
        return {};
    }

    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return {};
    }

    FDWCEditorCacheKey Key;
    Key.Namespace = SpatialCacheNamespace;
    Key.Owner = FObjectKey(Mesh);
    Key.ResourceIdentity = &RenderData->LODRenderData[LODIndex];
    Key.LODIndex = LODIndex;
    Key.UVChannelIndex = UVChannelIndex;
    Key.MaterialSlotIndex = MaterialSlotIndex;

    if (WetClothingAsset != nullptr)
    {
        if (const FDWCDataUVLODMetadata* DataUVMetadata =
                WetClothingAsset->FindDataUVMetadataForLOD(LODIndex);
            DataUVMetadata != nullptr && DataUVMetadata->UVChannelIndex == UVChannelIndex)
        {
            Key.Signature = DataUVMetadata->DataUVOutputSignature;
        }
#if WITH_EDITORONLY_DATA
        else if (const FDWCEditorUVTopologyDescriptor* OriginalUVTopology =
                     WetClothingAsset->FindOriginalUVTopologyDescriptorForLOD(LODIndex);
                 OriginalUVTopology != nullptr && OriginalUVTopology->UVChannelIndex == UVChannelIndex)
        {
            Key.Signature = OriginalUVTopology->BuildSignature;
        }
#endif
        else
        {
            Key.Signature = WetClothingAsset->GetSourceMeshSignature();
        }
    }
    Key.Signature += FString::Printf(
        TEXT("|SurfaceOrientation=P%u-F%u-R%u"),
        OrientationPolicy.BuildSignature(),
        DWCEditorSurfaceOrientationVersion::FieldLayout,
        DWCEditorSurfaceOrientationVersion::Resolver);
    return Key;
}

bool FDWCEditorSpatialQueryService::BuildSpatialData(
    const UWetClothingAsset* WetClothingAsset,
    USkeletalMesh* Mesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FDWCEditorSpatialData& OutData,
    FString* OutError) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorSpatialQueryService_BuildSpatialData);

    TArray<FWCAUVPreviewSourceTriangle> SourceTriangles;
    if (!FWCAUVPreviewTriangleReader::ReadFromSkeletalMesh(
            Mesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            SourceTriangles,
            OutError) ||
        SourceTriangles.IsEmpty())
    {
        if (OutError != nullptr && OutError->IsEmpty())
        {
            *OutError = TEXT("No LOD0 triangles were found for the selected Data UV and material slot.");
        }
        return false;
    }

    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset != nullptr
        ? WetClothingAsset->FindDataUVMetadataForLOD(LODIndex)
        : nullptr;
    bool bUseStoredIslandIDs = DataUVMetadata != nullptr &&
        DataUVMetadata->bIsValid &&
        DataUVMetadata->UVChannelIndex == UVChannelIndex &&
        !DataUVMetadata->DataUVIslandIDByTriangleID.IsEmpty();
    if (bUseStoredIslandIDs)
    {
        for (const FWCAUVPreviewSourceTriangle& Triangle : SourceTriangles)
        {
            if (!DataUVMetadata->DataUVIslandIDByTriangleID.IsValidIndex(Triangle.TriangleID) ||
                DataUVMetadata->DataUVIslandIDByTriangleID[Triangle.TriangleID] == INDEX_NONE)
            {
                bUseStoredIslandIDs = false;
                break;
            }
        }
    }

    TMap<int32, int32> FallbackIslandIDByTriangleID;
    if (!bUseStoredIslandIDs)
    {
        TArray<FWetClothingAssetUVIsland> Islands;
        if (FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                Mesh,
                LODIndex,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                nullptr))
        {
            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
                {
                    FallbackIslandIDByTriangleID.Add(Triangle.TriangleID, Triangle.UVIslandID);
                }
            }
        }
    }

    OutData.LODIndex = LODIndex;
    OutData.UVChannelIndex = UVChannelIndex;
    OutData.MaterialSlotIndex = MaterialSlotIndex;
    OutData.Triangles.Reserve(SourceTriangles.Num());
    TMap<FQuantizedSurfaceVertexKey, int64> WeldedTopologyVertexIDs;
    int64 NextFallbackTopologyVertexID = FallbackTopologyVertexBase;
    for (const FWCAUVPreviewSourceTriangle& Source : SourceTriangles)
    {
        FDWCEditorSpatialTriangle& Triangle = OutData.Triangles.AddDefaulted_GetRef();
        Triangle.MaterialSlotIndex = Source.MaterialSlotIndex;
        Triangle.TriangleID = Source.TriangleID;
        const int32* FallbackIslandID = FallbackIslandIDByTriangleID.Find(Source.TriangleID);
        Triangle.UVIslandID = bUseStoredIslandIDs
            ? DataUVMetadata->DataUVIslandIDByTriangleID[Source.TriangleID]
            : (FallbackIslandID != nullptr ? *FallbackIslandID : INDEX_NONE);
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.LocalPositions[CornerIndex] = Source.LocalPositions[CornerIndex];
            Triangle.UVs[CornerIndex] = Source.UVs[CornerIndex];
            Triangle.LocalNormals[CornerIndex] = Source.LocalNormals[CornerIndex].GetSafeNormal();
            Triangle.LocalTangents[CornerIndex] = Source.LocalTangents[CornerIndex];
            Triangle.LocalBitangents[CornerIndex] = Source.LocalBitangents[CornerIndex];
            if (Source.ImportedVertexIndices[CornerIndex] != INDEX_NONE)
            {
                Triangle.TopologyVertexIDs[CornerIndex] = Source.ImportedVertexIndices[CornerIndex];
            }
            else
            {
                const FVector3f Position = Source.LocalPositions[CornerIndex];
                const FVector3f Normal = Source.LocalNormals[CornerIndex].GetSafeNormal();
                FQuantizedSurfaceVertexKey VertexKey;
                VertexKey.Position = FIntVector(
                    FMath::RoundToInt(Position.X * 1000.0f),
                    FMath::RoundToInt(Position.Y * 1000.0f),
                    FMath::RoundToInt(Position.Z * 1000.0f));
                VertexKey.Normal = FIntVector(
                    FMath::RoundToInt(Normal.X * 1000.0f),
                    FMath::RoundToInt(Normal.Y * 1000.0f),
                    FMath::RoundToInt(Normal.Z * 1000.0f));
                int64& TopologyVertexID = WeldedTopologyVertexIDs.FindOrAdd(VertexKey, INDEX_NONE);
                if (TopologyVertexID == INDEX_NONE)
                {
                    TopologyVertexID = NextFallbackTopologyVertexID++;
                }
                Triangle.TopologyVertexIDs[CornerIndex] = TopologyVertexID;
            }
            Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
            Triangle.UVBounds += Triangle.UVs[CornerIndex];
        }

        Triangle.LocalNormal = FVector3f::CrossProduct(
            Triangle.LocalPositions[1] - Triangle.LocalPositions[0],
            Triangle.LocalPositions[2] - Triangle.LocalPositions[0]).GetSafeNormal();
        if (Triangle.LocalNormal.IsNearlyZero())
        {
            Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
        }
        FVector3f TangentSum = FVector3f::ZeroVector;
        FVector3f BitangentSum = FVector3f::ZeroVector;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVector3f CornerNormal = Source.LocalNormals[CornerIndex].GetSafeNormal(
                UE_SMALL_NUMBER,
                Triangle.LocalNormal);
            FVector3f& CornerTangent = Triangle.LocalTangents[CornerIndex];
            CornerTangent = (CornerTangent - CornerNormal *
                FVector3f::DotProduct(CornerTangent, CornerNormal)).GetSafeNormal();
            if (CornerTangent.IsNearlyZero())
            {
                CornerTangent = AnyPerpendicular(CornerNormal);
            }

            FVector3f& CornerBitangent = Triangle.LocalBitangents[CornerIndex];
            const float Handedness = FVector3f::DotProduct(
                FVector3f::CrossProduct(CornerNormal, CornerTangent),
                CornerBitangent) < 0.0f ? -1.0f : 1.0f;
            CornerBitangent = FVector3f::CrossProduct(
                CornerNormal,
                CornerTangent).GetSafeNormal() * Handedness;
            TangentSum += CornerTangent;
            BitangentSum += CornerBitangent;
        }

        Triangle.LocalTangent = TangentSum.GetSafeNormal();
        if (Triangle.LocalTangent.IsNearlyZero())
        {
            Triangle.LocalTangent = Triangle.LocalTangents[0];
        }
        Triangle.LocalBitangent = BitangentSum.GetSafeNormal();
        if (Triangle.LocalBitangent.IsNearlyZero())
        {
            Triangle.LocalBitangent = Triangle.LocalBitangents[0];
        }
        ComputeSurfaceUVFrame(Triangle);
    }

    BuildTriangleTopology(OutData);

    FString OrientationWarning;
    if (!FDWCEditorSurfaceOrientationFieldBuilder::Build(
            OutData.Triangles,
            OrientationPolicy,
            OutData.SurfaceOrientationField,
            &OrientationWarning))
    {
        OutData.SurfaceOrientationField.Reset();
        OutData.SurfaceOrientationField.BuildStatus =
            EDWCEditorSurfaceOrientationFieldBuildStatus::Degraded;
        OutData.SurfaceOrientationField.PolicySignature = OrientationPolicy.BuildSignature();
        OutData.SurfaceOrientationField.FieldLayoutVersion =
            DWCEditorSurfaceOrientationVersion::FieldLayout;
    }
    if (!OrientationWarning.IsEmpty())
    {
        UE_LOG(
            LogDWCEditorSpatialQuery,
            Warning,
            TEXT("Surface orientation field for slot %d was degraded: %s"),
            MaterialSlotIndex,
            *OrientationWarning);
    }

    OutData.TriangleLookup.Reserve(OutData.Triangles.Num());
    OutData.BVHTriangleIndices.Reserve(OutData.Triangles.Num());
    OutData.UVTriangleGrid.SetNum(
        FDWCEditorSpatialData::UVGridResolution * FDWCEditorSpatialData::UVGridResolution);
    for (int32 TriangleIndex = 0; TriangleIndex < OutData.Triangles.Num(); ++TriangleIndex)
    {
        const FDWCEditorSpatialTriangle& Triangle = OutData.Triangles[TriangleIndex];
        OutData.TriangleLookup.Add(
            MakeTriangleLookupKey(Triangle.MaterialSlotIndex, Triangle.TriangleID),
            TriangleIndex);
        OutData.BVHTriangleIndices.Add(TriangleIndex);
        if (!Triangle.UVBounds.bIsValid)
        {
            continue;
        }

        const int32 MinCellX = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Min.X * FDWCEditorSpatialData::UVGridResolution),
            0,
            FDWCEditorSpatialData::UVGridResolution - 1);
        const int32 MinCellY = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Min.Y * FDWCEditorSpatialData::UVGridResolution),
            0,
            FDWCEditorSpatialData::UVGridResolution - 1);
        const int32 MaxCellX = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Max.X * FDWCEditorSpatialData::UVGridResolution),
            0,
            FDWCEditorSpatialData::UVGridResolution - 1);
        const int32 MaxCellY = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Max.Y * FDWCEditorSpatialData::UVGridResolution),
            0,
            FDWCEditorSpatialData::UVGridResolution - 1);
        for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
        {
            for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
            {
                OutData.UVTriangleGrid[
                    CellY * FDWCEditorSpatialData::UVGridResolution + CellX].Add(TriangleIndex);
            }
        }
    }

    TFunction<int32(int32, int32)> BuildNode;
    BuildNode = [&OutData, &BuildNode](const int32 FirstIndex, const int32 TriangleCount)
    {
        const int32 NodeIndex = OutData.BVHNodes.AddDefaulted();
        FBox3f Bounds(ForceInit);
        FBox3f CenterBounds(ForceInit);
        for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
        {
            const FDWCEditorSpatialTriangle& Triangle =
                OutData.Triangles[OutData.BVHTriangleIndices[FirstIndex + Offset]];
            Bounds += Triangle.LocalBounds;
            CenterBounds += Triangle.LocalBounds.GetCenter();
        }
        OutData.BVHNodes[NodeIndex].Bounds = Bounds;
        OutData.BVHNodes[NodeIndex].FirstTriangleIndex = FirstIndex;
        OutData.BVHNodes[NodeIndex].TriangleCount = TriangleCount;
        if (TriangleCount <= BVHLeafTriangleCount)
        {
            return NodeIndex;
        }

        const FVector3f Extent = CenterBounds.GetExtent();
        const int32 SplitAxis = Extent.Y > Extent.X
            ? (Extent.Z > Extent.Y ? 2 : 1)
            : (Extent.Z > Extent.X ? 2 : 0);
        TArrayView<int32> Range(
            OutData.BVHTriangleIndices.GetData() + FirstIndex,
            TriangleCount);
        Algo::Sort(Range, [&OutData, SplitAxis](const int32 A, const int32 B)
        {
            return OutData.Triangles[A].LocalBounds.GetCenter()[SplitAxis] <
                OutData.Triangles[B].LocalBounds.GetCenter()[SplitAxis];
        });

        const int32 LeftCount = TriangleCount / 2;
        const int32 LeftChild = BuildNode(FirstIndex, LeftCount);
        const int32 RightChild = BuildNode(FirstIndex + LeftCount, TriangleCount - LeftCount);
        OutData.BVHNodes[NodeIndex].LeftChildIndex = LeftChild;
        OutData.BVHNodes[NodeIndex].RightChildIndex = RightChild;
        OutData.BVHNodes[NodeIndex].TriangleCount = 0;
        return NodeIndex;
    };
    BuildNode(0, OutData.BVHTriangleIndices.Num());
    return true;
}

void FDWCEditorSpatialQueryService::BuildTriangleTopology(FDWCEditorSpatialData& OutData)
{
    TMap<FTopologyEdgeKey, TArray<FTopologyEdgeReference, TInlineAllocator<2>>> EdgeReferences;
    EdgeReferences.Reserve(OutData.Triangles.Num() * 3);

    for (int32 TriangleIndex = 0; TriangleIndex < OutData.Triangles.Num(); ++TriangleIndex)
    {
        FDWCEditorSpatialTriangle& Triangle = OutData.Triangles[TriangleIndex];
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            Triangle.AdjacentTriangleIndices[EdgeIndex] = INDEX_NONE;
            Triangle.EdgeTypes[EdgeIndex] = EDWCEditorSpatialEdgeType::Boundary;
            const int32 StartCorner = EdgeIndex;
            const int32 EndCorner = (EdgeIndex + 1) % 3;
            const int64 StartVertexID = Triangle.TopologyVertexIDs[StartCorner];
            const int64 EndVertexID = Triangle.TopologyVertexIDs[EndCorner];
            if (StartVertexID == INDEX_NONE || EndVertexID == INDEX_NONE || StartVertexID == EndVertexID)
            {
                Triangle.EdgeTypes[EdgeIndex] = EDWCEditorSpatialEdgeType::Blocked;
                continue;
            }

            FTopologyEdgeKey EdgeKey;
            EdgeKey.VertexA = FMath::Min(StartVertexID, EndVertexID);
            EdgeKey.VertexB = FMath::Max(StartVertexID, EndVertexID);
            FTopologyEdgeReference& Reference = EdgeReferences.FindOrAdd(EdgeKey).AddDefaulted_GetRef();
            Reference.TriangleIndex = TriangleIndex;
            Reference.EdgeIndex = EdgeIndex;
            const bool bStartIsA = StartVertexID == EdgeKey.VertexA;
            Reference.UVAtA = Triangle.UVs[bStartIsA ? StartCorner : EndCorner];
            Reference.UVAtB = Triangle.UVs[bStartIsA ? EndCorner : StartCorner];
        }
    }

    constexpr float UVTolerance = 1.0e-5f;
    for (const TPair<FTopologyEdgeKey, TArray<FTopologyEdgeReference, TInlineAllocator<2>>>& Pair : EdgeReferences)
    {
        const TArray<FTopologyEdgeReference, TInlineAllocator<2>>& References = Pair.Value;
        if (References.Num() == 1)
        {
            continue;
        }
        if (References.Num() != 2 || References[0].TriangleIndex == References[1].TriangleIndex)
        {
            for (const FTopologyEdgeReference& Reference : References)
            {
                if (OutData.Triangles.IsValidIndex(Reference.TriangleIndex))
                {
                    OutData.Triangles[Reference.TriangleIndex].EdgeTypes[Reference.EdgeIndex] =
                        EDWCEditorSpatialEdgeType::Blocked;
                }
            }
            continue;
        }

        const FTopologyEdgeReference& ReferenceA = References[0];
        const FTopologyEdgeReference& ReferenceB = References[1];
        FDWCEditorSpatialTriangle& TriangleA = OutData.Triangles[ReferenceA.TriangleIndex];
        FDWCEditorSpatialTriangle& TriangleB = OutData.Triangles[ReferenceB.TriangleIndex];
        const bool bDifferentIslands =
            TriangleA.UVIslandID != INDEX_NONE && TriangleB.UVIslandID != INDEX_NONE &&
            TriangleA.UVIslandID != TriangleB.UVIslandID;
        const bool bDifferentUVs =
            !ReferenceA.UVAtA.Equals(ReferenceB.UVAtA, UVTolerance) ||
            !ReferenceA.UVAtB.Equals(ReferenceB.UVAtB, UVTolerance);
        const EDWCEditorSpatialEdgeType EdgeType = bDifferentIslands || bDifferentUVs
            ? EDWCEditorSpatialEdgeType::UVSeam
            : EDWCEditorSpatialEdgeType::Regular;

        TriangleA.AdjacentTriangleIndices[ReferenceA.EdgeIndex] = ReferenceB.TriangleIndex;
        TriangleA.EdgeTypes[ReferenceA.EdgeIndex] = EdgeType;
        TriangleB.AdjacentTriangleIndices[ReferenceB.EdgeIndex] = ReferenceA.TriangleIndex;
        TriangleB.EdgeTypes[ReferenceB.EdgeIndex] = EdgeType;
    }
}
