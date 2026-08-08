// Copyright 2026 Team Tofunut. All Rights Reserved.

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
#include "WetClothing/WCAEditor/UI/UVView/WCAUVPreviewTriangleReader.h"

namespace
{
    const FName     SpatialCacheNamespace(TEXT("SpatialQuery"));
    constexpr int32 BVHLeafTriangleCount = 8;

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
        const FBox3f&    Box,
        const FVector3f& SegmentStart,
        const FVector3f& SegmentEnd)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        const FVector3f Delta = SegmentEnd - SegmentStart;
        float           Entry = 0.0f;
        float           Exit = 1.0f;
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
        float&           OutSegmentT,
        FVector3f&       OutBarycentric)
    {
        const FVector3f SegmentDelta = SegmentEnd - SegmentStart;
        const FVector3f EdgeAB = B - A;
        const FVector3f EdgeAC = C - A;
        const FVector3f P = FVector3f::CrossProduct(SegmentDelta, EdgeAC);
        const float     Determinant = FVector3f::DotProduct(EdgeAB, P);
        if (FMath::Abs(Determinant) <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const float     InverseDeterminant = 1.0f / Determinant;
        const FVector3f T = SegmentStart - A;
        const float     BarycentricB = FVector3f::DotProduct(T, P) * InverseDeterminant;
        if (BarycentricB < -UE_KINDA_SMALL_NUMBER || BarycentricB > 1.0f + UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }
        const FVector3f Q = FVector3f::CrossProduct(T, EdgeAB);
        const float     BarycentricC = FVector3f::DotProduct(SegmentDelta, Q) * InverseDeterminant;
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
        const float     Denominator = V0.X * V1.Y - V1.X * V0.Y;
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

    float WrapUV(const float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    void FillProjectedSurface(
        const FDWCEditorSpatialTriangle& Triangle,
        const FVector3f&                 Barycentric,
        const FTransform&                ComponentTransform,
        FDWCEditorProjectedSurface&      OutSurface)
    {
        const FVector3f LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        OutSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutSurface.TriangleID = Triangle.TriangleID;
        OutSurface.UVIslandID = Triangle.UVIslandID;
        OutSurface.Barycentric = FVector(Barycentric);
        OutSurface.WorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
        OutSurface.WorldNormal = ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalNormal))
                                     .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutSurface.WorldTangent = ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalTangent))
                                      .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutSurface.WorldBitangent = FVector::CrossProduct(
                                        OutSurface.WorldNormal,
                                        OutSurface.WorldTangent)
                                        .GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    }
} // namespace

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
                   static_cast<uint64>(UVTriangleGrid.GetAllocatedSize());
    for (const TArray<int32>& Cell : UVTriangleGrid)
    {
        Bytes += static_cast<uint64>(Cell.GetAllocatedSize());
    }
    return Bytes;
}

FDWCEditorSpatialQueryService::FDWCEditorSpatialQueryService(
    TSharedRef<FDWCEditorCacheStore> InCacheStore)
    : CacheStore(MoveTemp(InCacheStore))
{
}

FDWCEditorSpatialHandle FDWCEditorSpatialQueryService::Acquire(
    const UWetClothingAsset* WetClothingAsset,
    USkeletalMesh*           Mesh,
    const int32              UVChannelIndex,
    const int32              MaterialSlotIndex,
    FString*                 OutError)
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
    USkeletalMesh*           Mesh,
    const int32              UVChannelIndex,
    const int32              MaterialSlotIndex,
    FString*                 OutError)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorSpatialQueryService_Acquire);
    check(IsInGameThread());

    constexpr int32                     LODIndex = 0;
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
    const USkeletalMeshComponent*  MeshComponent,
    const FVector&                 RayOrigin,
    const FVector&                 RayDirection,
    FDWCEditorSurfaceHit&          OutHit) const
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
    const FVector3f  LocalRayOrigin(ComponentTransform.InverseTransformPosition(RayOrigin));
    const FVector3f  LocalRayEnd(ComponentTransform.InverseTransformPosition(
        RayOrigin + Direction * 1000000.0f));
    float            BestSegmentT = TNumericLimits<float>::Max();

    auto TestTriangle = [&](const FDWCEditorSpatialTriangle& Triangle)
    {
        if (Triangle.LocalBounds.IsValid &&
            !DoesSegmentIntersectBox(Triangle.LocalBounds.ExpandBy(0.1f), LocalRayOrigin, LocalRayEnd))
        {
            return;
        }

        float     SegmentT = 0.0f;
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
        const FVector WorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
        FVector       WorldNormal = ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalNormal)).GetSafeNormal();
        if (WorldNormal.IsNearlyZero())
        {
            WorldNormal = FVector::UpVector;
        }
        if (FVector::DotProduct(WorldNormal, Direction) > 0.0f)
        {
            WorldNormal *= -1.0f;
        }
        FVector WorldTangent = ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalTangent)).GetSafeNormal();
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
        OutHit.LocalPosition = FVector(LocalPosition);
        OutHit.LocalNormal = FVector(Triangle.LocalNormal);
        OutHit.LocalTangent = FVector(Triangle.LocalTangent);
        OutHit.LocalBitangent = FVector(Triangle.LocalBitangent);
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
                    TestTriangle(Handle->Triangles[Handle->BVHTriangleIndices[OrderedIndex]]);
                }
            }
        }
        else
        {
            if (Node.LeftChildIndex != INDEX_NONE)
                NodeStack.Add(Node.LeftChildIndex);
            if (Node.RightChildIndex != INDEX_NONE)
                NodeStack.Add(Node.RightChildIndex);
        }
    }

    if (Handle->BVHNodes.IsEmpty())
    {
        for (const FDWCEditorSpatialTriangle& Triangle : Handle->Triangles)
        {
            TestTriangle(Triangle);
        }
    }
    return OutHit.bHit;
}

void FDWCEditorSpatialQueryService::FindSurfacesAtUV(
    const FDWCEditorSpatialHandle&      Handle,
    const USkeletalMeshComponent*       MeshComponent,
    const FVector2D&                    UV,
    TArray<FDWCEditorProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();
    if (!Handle.IsValid() || MeshComponent == nullptr)
    {
        return;
    }

    const FVector2D QueryUV(
        UV.X >= 0.0 && UV.X <= 1.0 ? UV.X : WrapUV(static_cast<float>(UV.X)),
        UV.Y >= 0.0 && UV.Y <= 1.0 ? UV.Y : WrapUV(static_cast<float>(UV.Y)));
    const int32          CellX = IntCastChecked<int32>(FMath::Clamp(
        FMath::FloorToInt(QueryUV.X * FDWCEditorSpatialData::UVGridResolution),
        0,
        FDWCEditorSpatialData::UVGridResolution - 1));
    const int32          CellY = IntCastChecked<int32>(FMath::Clamp(
        FMath::FloorToInt(QueryUV.Y * FDWCEditorSpatialData::UVGridResolution),
        0,
        FDWCEditorSpatialData::UVGridResolution - 1));
    const int32          CellIndex = CellY * FDWCEditorSpatialData::UVGridResolution + CellX;
    const TArray<int32>* Candidates = Handle->UVTriangleGrid.IsValidIndex(CellIndex)
                                          ? &Handle->UVTriangleGrid[CellIndex]
                                          : nullptr;
    const FVector2f      QueryUV2f(QueryUV);
    const FTransform     ComponentTransform = MeshComponent->GetComponentTransform();

    auto TestTriangle = [&](const FDWCEditorSpatialTriangle& Triangle)
    {
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
        FillProjectedSurface(Triangle, Barycentric, ComponentTransform, Surface);
    };

    if (Candidates != nullptr)
    {
        for (const int32 TriangleIndex : *Candidates)
        {
            if (Handle->Triangles.IsValidIndex(TriangleIndex))
            {
                TestTriangle(Handle->Triangles[TriangleIndex]);
            }
        }
    }
    else
    {
        for (const FDWCEditorSpatialTriangle& Triangle : Handle->Triangles)
        {
            TestTriangle(Triangle);
        }
    }
}

bool FDWCEditorSpatialQueryService::ResolveTriangleAnchor(
    const FDWCEditorSpatialHandle& Handle,
    const USkeletalMeshComponent*  MeshComponent,
    const int32                    MaterialSlotIndex,
    const int32                    TriangleID,
    const FVector3f&               Barycentric,
    FDWCEditorProjectedSurface&    OutSurface) const
{
    if (!Handle.IsValid() || MeshComponent == nullptr)
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
        Handle->Triangles[*TriangleIndex],
        Barycentric,
        MeshComponent->GetComponentTransform(),
        OutSurface);
    return true;
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
    const USkeletalMesh*     Mesh,
    const int32              LODIndex,
    const int32              UVChannelIndex,
    const int32              MaterialSlotIndex)
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
        else if (const FDWCEditorUVTopologyData* OriginalUVTopology =
                     WetClothingAsset->FindOriginalUVTopologyForLOD(LODIndex);
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
    return Key;
}

bool FDWCEditorSpatialQueryService::BuildSpatialData(
    const UWetClothingAsset* WetClothingAsset,
    USkeletalMesh*           Mesh,
    const int32              LODIndex,
    const int32              UVChannelIndex,
    const int32              MaterialSlotIndex,
    FDWCEditorSpatialData&   OutData,
    FString*                 OutError)
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
    bool                         bUseStoredIslandIDs = DataUVMetadata != nullptr &&
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
            Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
            Triangle.UVBounds += Triangle.UVs[CornerIndex];
        }

        Triangle.LocalNormal = FVector3f::CrossProduct(
                                   Triangle.LocalPositions[1] - Triangle.LocalPositions[0],
                                   Triangle.LocalPositions[2] - Triangle.LocalPositions[0])
                                   .GetSafeNormal();
        if (Triangle.LocalNormal.IsNearlyZero())
        {
            Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
        }
        Triangle.LocalTangent =
            (Triangle.LocalPositions[1] - Triangle.LocalPositions[0]).GetSafeNormal();
        Triangle.LocalTangent = (Triangle.LocalTangent - Triangle.LocalNormal *
                                                             FVector3f::DotProduct(Triangle.LocalTangent, Triangle.LocalNormal))
                                    .GetSafeNormal();
        if (Triangle.LocalTangent.IsNearlyZero())
        {
            Triangle.LocalTangent = AnyPerpendicular(Triangle.LocalNormal);
        }
        Triangle.LocalBitangent = FVector3f::CrossProduct(
                                      Triangle.LocalNormal,
                                      Triangle.LocalTangent)
                                      .GetSafeNormal();
        if (Triangle.LocalBitangent.IsNearlyZero())
        {
            Triangle.LocalBitangent = AnyPerpendicular(Triangle.LocalNormal);
        }
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
                OutData.UVTriangleGrid[CellY * FDWCEditorSpatialData::UVGridResolution + CellX].Add(TriangleIndex);
            }
        }
    }

    TFunction<int32(int32, int32)> BuildNode;
    BuildNode = [&OutData, &BuildNode](const int32 FirstIndex, const int32 TriangleCount)
    {
        const int32 NodeIndex = OutData.BVHNodes.AddDefaulted();
        FBox3f      Bounds(ForceInit);
        FBox3f      CenterBounds(ForceInit);
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

        const FVector3f   Extent = CenterBounds.GetExtent();
        const int32       SplitAxis = Extent.Y > Extent.X
                                          ? (Extent.Z > Extent.Y ? 2 : 1)
                                          : (Extent.Z > Extent.X ? 2 : 0);
        TArrayView<int32> Range(
            OutData.BVHTriangleIndices.GetData() + FirstIndex,
            TriangleCount);
        Algo::Sort(Range, [&OutData, SplitAxis](const int32 A, const int32 B)
                   { return OutData.Triangles[A].LocalBounds.GetCenter()[SplitAxis] <
                            OutData.Triangles[B].LocalBounds.GetCenter()[SplitAxis]; });

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
