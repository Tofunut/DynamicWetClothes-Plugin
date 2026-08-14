// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorUVTopologyCache.h"

#include "Algo/Sort.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Foundation/UV/DWCEditorUVTopologyBuilder.h"

namespace DWCEditorUVTopologyCachePrivate
{
    int32 BuildBVHNode(
        FDWCEditorUVTopologyCacheValue& Value,
        const int32 FirstTriangle,
        const int32 TriangleCount)
    {
        const int32 NodeIndex = Value.PickBVHNodes.AddDefaulted();
        FDWCEditorUVPickBVHNode& Node = Value.PickBVHNodes[NodeIndex];
        Node.FirstTriangle = FirstTriangle;
        Node.TriangleCount = TriangleCount;

        FBox3f Bounds(ForceInit);
        FBox3f CentroidBounds(ForceInit);
        for (int32 OrderedIndex = FirstTriangle;
             OrderedIndex < FirstTriangle + TriangleCount;
             ++OrderedIndex)
        {
            const FDWCEditorUVPickTriangle& Triangle =
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
        FDWCEditorUVPickBVHNode& FinalNode = Value.PickBVHNodes[NodeIndex];
        FinalNode.LeftChild = LeftChild;
        FinalNode.RightChild = RightChild;
        FinalNode.TriangleCount = 0;
        return NodeIndex;
    }

    void BuildPickData(FDWCEditorUVTopologyCacheValue& Value)
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
                FDWCEditorUVPickTriangle& Triangle = Value.PickTriangles.AddDefaulted_GetRef();
                Triangle.UVIslandID = Island->UVIslandID;
                Triangle.Bounds = FBox3f(ForceInit);
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    Triangle.Positions[CornerIndex] =
                        FVector3f(SourceTriangle.LocalPositions[CornerIndex]);
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

    bool CommitValue(
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        const FDWCEditorCacheKey& Key,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>&& Islands,
        FDWCEditorCacheLease& OutLease,
        FString* OutErrorMessage)
    {
        TSharedRef<FDWCEditorUVTopologyCacheValue, ESPMode::ThreadSafe> Value =
            MakeShared<FDWCEditorUVTopologyCacheValue, ESPMode::ThreadSafe>();
        Value->Islands = MoveTemp(Islands);
        BuildPickData(*Value);
        if (!CacheStore->Put(Key, Value))
        {
            if (OutErrorMessage != nullptr)
            {
                *OutErrorMessage = TEXT("The shared editor cache budget could not retain UV topology.");
            }
            return false;
        }

        OutLease = CacheStore->FindLease<FDWCEditorUVTopologyCacheValue>(Key);
        return OutLease.IsValid();
    }
}

FName FDWCEditorUVTopologyCacheValue::StaticCacheTypeName()
{
    static const FName Name(TEXT("DWCEditorUVTopology"));
    return Name;
}

uint64 FDWCEditorUVTopologyCacheValue::GetAllocatedSizeBytes() const
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

FName FDWCEditorUVTopologyCache::CacheNamespace()
{
    static const FName Name(TEXT("DWC.UVTopology"));
    return Name;
}

FDWCEditorCacheKey FDWCEditorUVTopologyCache::BuildKey(
    const UObject& Owner,
    const USkeletalMesh& Mesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    const uint64 TopologyRevision)
{
    const FSkeletalMeshRenderData* RenderData = Mesh.GetResourceForRendering();
    int32 VertexCount = 0;
    int32 UVChannelCount = 0;
    if (RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        VertexCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumVertices());
        UVChannelCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
    }

    FDWCEditorCacheKey Key;
    Key.Namespace = CacheNamespace();
    Key.Owner = FObjectKey(&Owner);
    Key.ResourceIdentity = &Mesh;
    Key.LODIndex = LODIndex;
    Key.UVChannelIndex = UVChannelIndex;
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.Signature = FString::Printf(
        TEXT("rev=%llu;vertices=%d;uvs=%d;render=%p"),
        TopologyRevision,
        VertexCount,
        UVChannelCount,
        RenderData);
    return Key;
}

bool FDWCEditorUVTopologyCache::AcquireForAsset(
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
    const UWetClothingAsset* OwnerAsset,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FDWCEditorCacheKey& OutKey,
    FDWCEditorCacheLease& OutLease,
    FString* OutErrorMessage)
{
    check(IsInGameThread());
    OutLease.Reset();
    const USkeletalMesh* Mesh = OwnerAsset != nullptr ? OwnerAsset->GetRuntimeSkeletalMesh() : nullptr;
    if (Mesh == nullptr && OwnerAsset != nullptr)
    {
        Mesh = OwnerAsset->GetSourceSkeletalMesh();
    }
    if (!CacheStore.IsValid() || OwnerAsset == nullptr || Mesh == nullptr ||
        MaterialSlotIndex == INDEX_NONE)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = TEXT("UV topology cache is unavailable.");
        }
        return false;
    }

    OutKey = BuildKey(
        *OwnerAsset,
        *Mesh,
        AuthoringLODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        OwnerAsset->GetPreviewTopologyRevision());
    OutLease = CacheStore->FindLease<FDWCEditorUVTopologyCacheValue>(OutKey);
    if (OutLease.IsValid())
    {
        return true;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    if (!FDWCEditorUVTopologyBuilder::BuildMaterialSlotUVIslands(
            OwnerAsset,
            AuthoringLODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Islands,
            OutErrorMessage))
    {
        return false;
    }
    return DWCEditorUVTopologyCachePrivate::CommitValue(
        CacheStore,
        OutKey,
        MoveTemp(Islands),
        OutLease,
        OutErrorMessage);
}

bool FDWCEditorUVTopologyCache::AcquireForMesh(
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
    const UWetClothingAsset* OwnerAsset,
    const USkeletalMesh* Mesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    FDWCEditorCacheKey& OutKey,
    FDWCEditorCacheLease& OutLease,
    FString* OutErrorMessage)
{
    check(IsInGameThread());
    OutLease.Reset();
    if (!CacheStore.IsValid() || OwnerAsset == nullptr || Mesh == nullptr ||
        MaterialSlotIndex == INDEX_NONE)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = TEXT("UV topology cache is unavailable.");
        }
        return false;
    }

    OutKey = BuildKey(
        *OwnerAsset,
        *Mesh,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        OwnerAsset->GetPreviewTopologyRevision());
    OutLease = CacheStore->FindLease<FDWCEditorUVTopologyCacheValue>(OutKey);
    if (OutLease.IsValid())
    {
        return true;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    if (!FDWCEditorUVTopologyBuilder::BuildMaterialSlotUVIslands(
            Mesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Islands,
            OutErrorMessage))
    {
        return false;
    }
    return DWCEditorUVTopologyCachePrivate::CommitValue(
        CacheStore,
        OutKey,
        MoveTemp(Islands),
        OutLease,
        OutErrorMessage);
}
