// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyTopologyCache.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    const FName TopologyCacheNamespace(TEXT("DWC.Transparency.Stage2Topology"));

    struct FDWCTransparencyTopologyCacheValue final : IDWCEditorCacheValue
    {
        FDWCRevealBakeSurface Surface;
        TMap<int32, TArray<int32>> TriangleIndicesByMaterialSlot;

        static FName StaticCacheTypeName()
        {
            static const FName Name(TEXT("TransparencyStage2Topology"));
            return Name;
        }

        virtual FName GetCacheTypeName() const override
        {
            return StaticCacheTypeName();
        }

        virtual uint64 GetAllocatedSizeBytes() const override
        {
            uint64 Bytes = sizeof(FDWCTransparencyTopologyCacheValue) +
                Surface.Triangles.GetAllocatedSize() +
                TriangleIndicesByMaterialSlot.GetAllocatedSize();
            for (const TPair<int32, TArray<int32>>& Pair : TriangleIndicesByMaterialSlot)
            {
                Bytes += Pair.Value.GetAllocatedSize();
            }
            return Bytes;
        }
    };

    FString BuildPlacementSignature(const FTransform& Transform)
    {
        const FVector Translation = Transform.GetTranslation();
        const FQuat Rotation = Transform.GetRotation();
        const FVector Scale = Transform.GetScale3D();
        return FString::Printf(
            TEXT("T=%.9g,%.9g,%.9g|R=%.9g,%.9g,%.9g,%.9g|S=%.9g,%.9g,%.9g"),
            Translation.X, Translation.Y, Translation.Z,
            Rotation.X, Rotation.Y, Rotation.Z, Rotation.W,
            Scale.X, Scale.Y, Scale.Z);
    }

    FDWCEditorCacheKey BuildCacheKey(
        const UWetClothingAsset& OwnerAsset,
        const FDWCBakeResolvedLayer& Layer,
        const int32 LODIndex,
        const int32 UVChannelIndex)
    {
        FDWCEditorCacheKey Key;
        Key.Namespace = TopologyCacheNamespace;
        Key.Owner = FObjectKey(&OwnerAsset);
        Key.ResourceIdentity = Layer.SkeletalMesh.Get();
        Key.LODIndex = LODIndex;
        Key.UVChannelIndex = UVChannelIndex;
        Key.MaterialSlotIndex = INDEX_NONE;
        Key.Signature = FString::Printf(
            TEXT("v1|Mesh=%s|Content=%s|%s"),
            *GetPathNameSafe(Layer.SkeletalMesh.Get()),
            *UWetClothingAsset::BuildMeshContentSignature(
                Layer.SkeletalMesh.Get(), LODIndex, UVChannelIndex),
            *BuildPlacementSignature(Layer.BakeTransform));
        return Key;
    }

    TSharedRef<const FDWCTransparencyTopologyCacheValue, ESPMode::ThreadSafe>
    BuildCacheValue(
        const FDWCBakeResolvedLayer& Layer,
        const int32 LODIndex,
        const int32 UVChannelIndex,
        FString& OutError)
    {
        TSharedRef<FDWCTransparencyTopologyCacheValue, ESPMode::ThreadSafe> Value =
            MakeShared<FDWCTransparencyTopologyCacheValue, ESPMode::ThreadSafe>();
        if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
                Layer, LODIndex, UVChannelIndex, Value->Surface, &OutError))
        {
            Value->Surface.Reset();
            return Value;
        }

        for (int32 TriangleIndex = 0;
             TriangleIndex < Value->Surface.Triangles.Num();
             ++TriangleIndex)
        {
            const int32 Slot = Value->Surface.Triangles[TriangleIndex].MaterialSlotIndex;
            Value->TriangleIndicesByMaterialSlot.FindOrAdd(Slot).Add(TriangleIndex);
        }
        OutError.Reset();
        return Value;
    }

    bool CopySlotSurface(
        const FDWCTransparencyTopologyCacheValue& Value,
        const FDWCBakeResolvedLayer& Layer,
        const int32 MaterialSlotIndex,
        FDWCRevealBakeSurface& OutSurface,
        FString& OutError)
    {
        const TArray<int32>* TriangleIndices =
            Value.TriangleIndicesByMaterialSlot.Find(MaterialSlotIndex);
        if (TriangleIndices == nullptr || TriangleIndices->IsEmpty())
        {
            OutError = FString::Printf(
                TEXT("Material slot %d has no LOD %d triangles."),
                MaterialSlotIndex,
                Value.Surface.LODIndex);
            return false;
        }

        OutSurface.Reset();
        OutSurface.LayerId = Layer.LayerId;
        OutSurface.LayerOrder = Layer.LayerOrder;
        OutSurface.LODIndex = Value.Surface.LODIndex;
        OutSurface.UVChannelIndex = Value.Surface.UVChannelIndex;
        OutSurface.SkeletalMesh = Layer.SkeletalMesh;
        OutSurface.bCanBeRevealSource = Layer.bCanBeRevealSource;
        OutSurface.bCanBeWetOuterLayer = Layer.bCanBeWetOuterLayer;
        OutSurface.bBlocksReveal = Layer.bBlocksReveal;
        OutSurface.MaxRevealDistance = Layer.MaxRevealDistance;
        OutSurface.Triangles.Reserve(TriangleIndices->Num());
        for (const int32 TriangleIndex : *TriangleIndices)
        {
            if (!Value.Surface.Triangles.IsValidIndex(TriangleIndex))
            {
                continue;
            }
            const FDWCRevealBakeSurfaceTriangle& Triangle =
                Value.Surface.Triangles[TriangleIndex];
            OutSurface.Bounds += Triangle.Bounds;
            OutSurface.Triangles.Add(Triangle);
        }
        OutError.Reset();
        return !OutSurface.Triangles.IsEmpty();
    }
}

bool FDWCTransparencyTopologyCache::BuildSlotSurface(
    const UWetClothingAsset& OwnerAsset,
    const FDWCBakeResolvedLayer& Layer,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
    FDWCRevealBakeSurface& OutSurface,
    FString& OutError,
    bool* bOutCacheHit)
{
    check(IsInGameThread());
    if (bOutCacheHit != nullptr)
    {
        *bOutCacheHit = false;
    }
    if (Layer.SkeletalMesh == nullptr)
    {
        OutError = TEXT("Transparency topology requires a valid Skeletal Mesh.");
        return false;
    }

    const FDWCEditorCacheKey Key = BuildCacheKey(
        OwnerAsset, Layer, LODIndex, UVChannelIndex);
    if (CacheStore.IsValid())
    {
        FDWCEditorCacheLease Lease =
            CacheStore->FindLease<FDWCTransparencyTopologyCacheValue>(Key);
        if (const FDWCTransparencyTopologyCacheValue* Cached =
                Lease.GetAs<FDWCTransparencyTopologyCacheValue>())
        {
            if (bOutCacheHit != nullptr)
            {
                *bOutCacheHit = true;
            }
            return CopySlotSurface(
                *Cached, Layer, MaterialSlotIndex, OutSurface, OutError);
        }
    }

    FString BuildError;
    const TSharedRef<const FDWCTransparencyTopologyCacheValue, ESPMode::ThreadSafe> Built =
        BuildCacheValue(Layer, LODIndex, UVChannelIndex, BuildError);
    if (Built->Surface.Triangles.IsEmpty())
    {
        OutError = MoveTemp(BuildError);
        return false;
    }

    // Cache admission is an optimization. Oversized topology still completes
    // through this operation-local immutable value.
    if (CacheStore.IsValid())
    {
        CacheStore->Put(Key, Built);
    }
    return CopySlotSurface(
        *Built, Layer, MaterialSlotIndex, OutSurface, OutError);
}
