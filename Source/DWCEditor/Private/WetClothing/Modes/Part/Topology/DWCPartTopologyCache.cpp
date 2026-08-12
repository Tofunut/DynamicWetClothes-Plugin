// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCPartTopologyCache.h"

#include "Algo/Sort.h"
#include "DataAssets/WetClothingAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"

namespace DWCPartTopologyCachePrivate
{
    static const FName CacheNamespace(TEXT("WetPart.Topology"));

    int32 BuildBVHNode(FDWCPartTopologyCacheValue& Value, const int32 FirstTriangle, const int32 TriangleCount)
    {
        const int32 NodeIndex = Value.PickBVHNodes.AddDefaulted();
        FDWCPartPickBVHNode& Node = Value.PickBVHNodes[NodeIndex];
        Node.FirstTriangle = FirstTriangle;
        Node.TriangleCount = TriangleCount;

        FBox3f Bounds(ForceInit);
        FBox3f CentroidBounds(ForceInit);
        for (int32 OrderedIndex = FirstTriangle; OrderedIndex < FirstTriangle + TriangleCount; ++OrderedIndex)
        {
            const FDWCPartPickTriangle& Triangle =
                Value.PickTriangles[Value.PickTriangleIndices[OrderedIndex]];
            Bounds += Triangle.Bounds;
            CentroidBounds += Triangle.Centroid;
        }
        Node.Bounds = Bounds;

        constexpr int32 MaxTrianglesPerLeaf = 12;
        if (TriangleCount <= MaxTrianglesPerLeaf || !CentroidBounds.IsValid)
        {
            return NodeIndex;
        }

        const FVector3f Extent = CentroidBounds.GetExtent();
        int32 SplitAxis = 0;
        if (Extent.Y > Extent.X && Extent.Y >= Extent.Z)
        {
            SplitAxis = 1;
        }
        else if (Extent.Z > Extent.X && Extent.Z > Extent.Y)
        {
            SplitAxis = 2;
        }

        TArrayView<int32> OrderedView(
            Value.PickTriangleIndices.GetData() + FirstTriangle,
            TriangleCount);
        Algo::Sort(
            OrderedView,
            [&Value, SplitAxis](const int32 A, const int32 B)
            {
                return Value.PickTriangles[A].Centroid[SplitAxis] <
                    Value.PickTriangles[B].Centroid[SplitAxis];
            });

        const int32 LeftCount = TriangleCount / 2;
        const int32 RightCount = TriangleCount - LeftCount;
        if (LeftCount <= 0 || RightCount <= 0)
        {
            return NodeIndex;
        }

        const int32 LeftChild = BuildBVHNode(Value, FirstTriangle, LeftCount);
        const int32 RightChild = BuildBVHNode(Value, FirstTriangle + LeftCount, RightCount);
        FDWCPartPickBVHNode& FinalNode = Value.PickBVHNodes[NodeIndex];
        FinalNode.LeftChild = LeftChild;
        FinalNode.RightChild = RightChild;
        FinalNode.TriangleCount = 0;
        return NodeIndex;
    }

    void BuildPickData(FDWCPartTopologyCacheValue& Value)
    {
        int32 TriangleCount = 0;
        for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : Value.Islands)
        {
            if (Island.IsValid())
            {
                TriangleCount += Island->UVTriangles.Num();
            }
        }

        Value.PickTriangles.Reserve(TriangleCount);
        for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : Value.Islands)
        {
            if (!Island.IsValid())
            {
                continue;
            }
            for (const FWetClothingAssetUVTriangle& SourceTriangle : Island->UVTriangles)
            {
                FDWCPartPickTriangle& Triangle = Value.PickTriangles.AddDefaulted_GetRef();
                Triangle.UVIslandID = Island->UVIslandID;
                Triangle.Bounds = FBox3f(ForceInit);
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    Triangle.Positions[CornerIndex] = FVector3f(SourceTriangle.LocalPositions[CornerIndex]);
                    Triangle.Bounds += Triangle.Positions[CornerIndex];
                }
                Triangle.Centroid =
                    (Triangle.Positions[0] + Triangle.Positions[1] + Triangle.Positions[2]) / 3.0f;
            }
        }

        Value.PickTriangleIndices.Reserve(Value.PickTriangles.Num());
        for (int32 TriangleIndex = 0; TriangleIndex < Value.PickTriangles.Num(); ++TriangleIndex)
        {
            Value.PickTriangleIndices.Add(TriangleIndex);
        }
        if (!Value.PickTriangles.IsEmpty())
        {
            BuildBVHNode(Value, 0, Value.PickTriangles.Num());
        }
    }
}

FName FDWCPartTopologyCacheValue::StaticCacheTypeName()
{
    static const FName Name(TEXT("WetPartTopology"));
    return Name;
}

uint64 FDWCPartTopologyCacheValue::GetAllocatedSizeBytes() const
{
    uint64 Bytes = static_cast<uint64>(Islands.GetAllocatedSize()) +
        static_cast<uint64>(PickTriangles.GetAllocatedSize()) +
        static_cast<uint64>(PickTriangleIndices.GetAllocatedSize()) +
        static_cast<uint64>(PickBVHNodes.GetAllocatedSize());
    for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : Islands)
    {
        if (Island.IsValid())
        {
            Bytes += sizeof(FWetClothingAssetUVIsland) +
                static_cast<uint64>(Island->TriangleIDs.GetAllocatedSize()) +
                static_cast<uint64>(Island->UVTriangles.GetAllocatedSize());
        }
    }
    return Bytes;
}

FDWCEditorCacheKey FDWCPartTopologyCache::BuildKey(
    const UWetClothingAsset& WetClothingAsset,
    const int32              UVChannelIndex,
    const int32              MaterialSlotIndex)
{
    const USkeletalMesh* Mesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (Mesh == nullptr)
    {
        Mesh = WetClothingAsset.GetSourceSkeletalMesh();
    }
    const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
    int32 VertexCount = 0;
    int32 UVChannelCount = 0;
    if (RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(AuthoringLODIndex))
    {
        VertexCount = static_cast<int32>(RenderData->LODRenderData[AuthoringLODIndex].GetNumVertices());
        UVChannelCount = static_cast<int32>(RenderData->LODRenderData[AuthoringLODIndex].GetNumTexCoords());
    }

    FDWCEditorCacheKey Key;
    Key.Namespace = DWCPartTopologyCachePrivate::CacheNamespace;
    Key.Owner = FObjectKey(&WetClothingAsset);
    Key.ResourceIdentity = Mesh;
    Key.LODIndex = AuthoringLODIndex;
    Key.UVChannelIndex = UVChannelIndex;
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.Signature = FString::Printf(
        TEXT("rev=%llu;dataUV=%d;vertices=%d;uvs=%d;render=%p"),
        WetClothingAsset.GetPreviewTopologyRevision(),
        WetClothingAsset.GetDWCDataUVChannelIndex(),
        VertexCount,
        UVChannelCount,
        RenderData);
    return Key;
}

bool FDWCPartTopologyCache::Acquire(
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
    const UWetClothingAsset*                WetClothingAsset,
    const int32                             UVChannelIndex,
    const int32                             MaterialSlotIndex,
    FDWCEditorCacheKey&                      OutKey,
    FDWCEditorCacheLease&                    OutLease,
    FString*                                 OutErrorMessage)
{
    check(IsInGameThread());
    OutLease.Reset();
    if (!CacheStore.IsValid() || WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = TEXT("Wet Part topology cache is unavailable.");
        }
        return false;
    }

    OutKey = BuildKey(*WetClothingAsset, UVChannelIndex, MaterialSlotIndex);
    OutLease = CacheStore->FindLease<FDWCPartTopologyCacheValue>(OutKey);
    if (OutLease.IsValid())
    {
        return true;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    if (!FWCAUVIslandViewCache::BuildMaterialSlotUVIslandsUncached(
            WetClothingAsset,
            AuthoringLODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Islands,
            OutErrorMessage))
    {
        return false;
    }

    TSharedRef<FDWCPartTopologyCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FDWCPartTopologyCacheValue, ESPMode::ThreadSafe>();
    Value->Islands = MoveTemp(Islands);
    DWCPartTopologyCachePrivate::BuildPickData(*Value);
    if (!CacheStore->Put(OutKey, Value))
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = TEXT("The shared editor cache budget could not retain Wet Part topology.");
        }
        return false;
    }

    OutLease = CacheStore->FindLease<FDWCPartTopologyCacheValue>(OutKey);
    return OutLease.IsValid();
}
