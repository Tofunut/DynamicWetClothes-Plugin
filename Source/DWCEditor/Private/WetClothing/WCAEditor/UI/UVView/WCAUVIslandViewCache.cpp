// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Caches UV-island analysis by mesh/asset topology revision.
 */

#include "WCAUVIslandViewCache.h"

#include "Engine/SkeletalMesh.h"
#include "DataAssets/WetClothingAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace WCAUVIslandViewCachePrivate
{
    struct FWCAUVIslandViewCacheKey
    {
        FObjectKey MeshKey;
        FObjectKey AssetKey;
        int32      LODIndex = 0;
        int32      UVChannelIndex = 0;
        int32      MaterialSlotIndex = INDEX_NONE;
        uint64     GlobalRevision = 0;
        uint64     MeshRevision = 0;
        uint64     AssetRevision = 0;
        UPTRINT    RenderDataIdentity = 0;
        int32      RenderVertexCount = 0;
        int32      RenderUVChannelCount = 0;

        FWCAUVIslandViewCacheKey(
            const USkeletalMesh*     Mesh,
            const UWetClothingAsset* Asset,
            const int32              InLODIndex,
            const int32              InUVChannelIndex,
            const int32              InMaterialSlotIndex,
            const uint64             InGlobalRevision,
            const uint64             InMeshRevision,
            const uint64             InAssetRevision)
            : MeshKey(Mesh), AssetKey(Asset != nullptr ? FObjectKey(Asset) : FObjectKey()), LODIndex(InLODIndex), UVChannelIndex(InUVChannelIndex), MaterialSlotIndex(InMaterialSlotIndex), GlobalRevision(InGlobalRevision), MeshRevision(InMeshRevision), AssetRevision(InAssetRevision)
        {
            const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
            RenderDataIdentity = reinterpret_cast<UPTRINT>(RenderData);
            if (RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(LODIndex))
            {
                RenderVertexCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumVertices());
                RenderUVChannelCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
            }
        }

        bool operator==(const FWCAUVIslandViewCacheKey& Other) const
        {
            return MeshKey == Other.MeshKey &&
                   AssetKey == Other.AssetKey &&
                   LODIndex == Other.LODIndex &&
                   UVChannelIndex == Other.UVChannelIndex &&
                   MaterialSlotIndex == Other.MaterialSlotIndex &&
                   GlobalRevision == Other.GlobalRevision &&
                   MeshRevision == Other.MeshRevision &&
                   AssetRevision == Other.AssetRevision &&
                   RenderDataIdentity == Other.RenderDataIdentity &&
                   RenderVertexCount == Other.RenderVertexCount &&
                   RenderUVChannelCount == Other.RenderUVChannelCount;
        }
    };

    uint32 GetTypeHash(const FWCAUVIslandViewCacheKey& Key)
    {
        uint32 Hash = ::GetTypeHash(Key.MeshKey.GetWeakObjectPtr());
        Hash = HashCombine(Hash, ::GetTypeHash(Key.AssetKey.GetWeakObjectPtr()));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.UVChannelIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.GlobalRevision));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.MeshRevision));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.AssetRevision));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.RenderDataIdentity));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.RenderVertexCount));
        return HashCombine(Hash, ::GetTypeHash(Key.RenderUVChannelCount));
    }

    struct FWCAUVIslandViewCacheEntry
    {
        TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    };

    TMap<FWCAUVIslandViewCacheKey, FWCAUVIslandViewCacheEntry> GUVIslandCache;
    TMap<FObjectKey, uint64>                                   GMeshRevisions;
    TMap<FObjectKey, uint64>                                   GAssetRevisions;
    uint64                                                     GGlobalRevision = 1;

    void CollectUVIslandCacheMemory(TArray<FDWCEditorMemoryOwnerRecord>& OutOwners)
    {
        uint64 Bytes = GUVIslandCache.GetAllocatedSize() +
            GMeshRevisions.GetAllocatedSize() +
            GAssetRevisions.GetAllocatedSize();
        int32 IslandCount = 0;
        int32 TriangleCount = 0;
        for (const TPair<FWCAUVIslandViewCacheKey, FWCAUVIslandViewCacheEntry>& Pair : GUVIslandCache)
        {
            Bytes += Pair.Value.Islands.GetAllocatedSize();
            for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : Pair.Value.Islands)
            {
                if (!Island.IsValid())
                {
                    continue;
                }
                Bytes += sizeof(FWetClothingAssetUVIsland) +
                    Island->TriangleIDs.GetAllocatedSize() +
                    Island->UVTriangles.GetAllocatedSize();
                ++IslandCount;
                TriangleCount += Island->UVTriangles.Num();
            }
        }

        FDWCEditorMemoryOwnerRecord& Owner = OutOwners.AddDefaulted_GetRef();
        Owner.Identifier = TEXT("WetPart.UVIslandViewCache");
        Owner.Subsystem = TEXT("WetPart");
        Owner.Resource = TEXT("UVIslandViewCache");
        Owner.Category = EDWCEditorMemoryCategory::SharedCacheCPU;
        Owner.Accounting = EDWCEditorMemoryAccounting::Resident;
        Owner.CurrentBytes = Bytes;
        Owner.EntryCount = GUVIslandCache.Num();
        Owner.Context = FString::Printf(
            TEXT("islands=%d; triangles=%d; meshRevisions=%d; assetRevisions=%d"),
            IslandCount,
            TriangleCount,
            GMeshRevisions.Num(),
            GAssetRevisions.Num());
    }

    struct FUVIslandCacheMemoryDiagnosticRegistration
    {
        FUVIslandCacheMemoryDiagnosticRegistration()
        {
            FDWCEditorMemoryDiagnostics::RegisterCollector(
                TEXT("WetPartUVIslandViewCache"),
                &CollectUVIslandCacheMemory);
        }

        ~FUVIslandCacheMemoryDiagnosticRegistration()
        {
            FDWCEditorMemoryDiagnostics::UnregisterCollector(TEXT("WetPartUVIslandViewCache"));
        }
    } GUVIslandCacheMemoryDiagnosticRegistration;

    uint64 GetMeshRevision(const USkeletalMesh* Mesh)
    {
        const uint64* Revision = Mesh != nullptr ? GMeshRevisions.Find(FObjectKey(Mesh)) : nullptr;
        return Revision != nullptr ? *Revision : 0;
    }

    uint64 GetAssetRevision(const UWetClothingAsset* Asset)
    {
        const uint64  PersistentRevision = Asset != nullptr
                                               ? Asset->GetPreviewTopologyRevision()
                                               : 0;
        const uint64* TransientRevision = Asset != nullptr
                                              ? GAssetRevisions.Find(FObjectKey(Asset))
                                              : nullptr;
        return PersistentRevision + (TransientRevision != nullptr ? *TransientRevision : 0);
    }

    void MoveBuiltIslandsToEntry(
        TArray<FWetClothingAssetUVIsland>&& BuiltIslands,
        FWCAUVIslandViewCacheEntry&         OutEntry)
    {
        OutEntry.Islands.Reserve(BuiltIslands.Num());
        for (FWetClothingAssetUVIsland& Island : BuiltIslands)
        {
            OutEntry.Islands.Add(MakeShared<FWetClothingAssetUVIsland>(MoveTemp(Island)));
        }
    }

    void CopyCachedIslands(
        const FWCAUVIslandViewCacheEntry&              Entry,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands)
    {
        OutIslands.Append(Entry.Islands);
    }
} // namespace WCAUVIslandViewCachePrivate

using namespace WCAUVIslandViewCachePrivate;

bool FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
    const USkeletalMesh*                           SkeletalMesh,
    int32                                          LODIndex,
    int32                                          UVChannelIndex,
    int32                                          MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString*                                       OutErrorMessage)
{
    OutIslands.Reset();

    if (SkeletalMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Generate the DWC UV Channel to inspect its UV islands."));
        return false;
    }

    const FWCAUVIslandViewCacheKey Key(
        SkeletalMesh,
        nullptr,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        GGlobalRevision,
        GetMeshRevision(SkeletalMesh),
        0);

    if (const FWCAUVIslandViewCacheEntry* ExistingEntry = GUVIslandCache.Find(Key))
    {
        CopyCachedIslands(*ExistingEntry, OutIslands);
        return true;
    }

    TArray<FWetClothingAssetUVIsland> BuiltIslands;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            OutErrorMessage))
    {
        return false;
    }

    FWCAUVIslandViewCacheEntry NewEntry;
    MoveBuiltIslandsToEntry(MoveTemp(BuiltIslands), NewEntry);
    CopyCachedIslands(NewEntry, OutIslands);
    GUVIslandCache.Add(Key, MoveTemp(NewEntry));
    return true;
}

bool FWCAUVIslandViewCache::BuildMaterialSlotUVIslandsUncached(
    const UWetClothingAsset*                       WetClothingAsset,
    const int32                                    LODIndex,
    const int32                                    UVChannelIndex,
    const int32                                    MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString*                                       OutErrorMessage)
{
    OutIslands.Reset();
    if (WetClothingAsset == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No Wet Clothing Asset is assigned."));
        return false;
    }

    const USkeletalMesh* AnalysisMesh = WetClothingAsset->GetRuntimeSkeletalMesh();
    if (AnalysisMesh == nullptr)
    {
        AnalysisMesh = WetClothingAsset->GetSourceSkeletalMesh();
    }
    if (AnalysisMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No mesh is available for UV island analysis."));
        return false;
    }

    const int32 OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
    const FDWCEditorUVTopologyData* Topology = WetClothingAsset->FindOriginalUVTopologyForLOD(LODIndex);
    const bool bUseStoredTopology =
        Topology != nullptr && Topology->bIsValid &&
        Topology->UVChannelIndex == OriginalUVChannelIndex &&
        UVChannelIndex == OriginalUVChannelIndex &&
        Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology &&
        !Topology->BuildSignature.IsEmpty() && !Topology->Islands.IsEmpty();

    TArray<FWetClothingAssetUVIsland> BuiltIslands;
    bool bBuilt = false;
    if (bUseStoredTopology)
    {
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Topology->Islands,
            BuiltIslands,
            OutErrorMessage);
    }
    if (!bBuilt)
    {
        BuiltIslands.Reset();
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            OutErrorMessage);
    }
    if (!bBuilt)
    {
        return false;
    }

    FWCAUVIslandViewCacheEntry Entry;
    MoveBuiltIslandsToEntry(MoveTemp(BuiltIslands), Entry);
    OutIslands = MoveTemp(Entry.Islands);
    return true;
}

bool FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
    const UWetClothingAsset*                       WetClothingAsset,
    const int32                                    UVChannelIndex,
    const int32                                    MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString*                                       OutErrorMessage)
{
    OutIslands.Reset();

    if (WetClothingAsset == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No Wet Clothing Asset is assigned."));
        return false;
    }

    const int32          LODIndex = WetClothingAsset->GetSimulationLODIndex();
    const int32          OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
    const USkeletalMesh* AnalysisMesh = WetClothingAsset->GetRuntimeSkeletalMesh();

    if (AnalysisMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No mesh is available for UV island analysis."));
        return false;
    }

    const FDWCEditorUVTopologyData* Topology = WetClothingAsset->FindOriginalUVTopologyForLOD(LODIndex);
    const bool                      bUseStoredTopology =
        Topology != nullptr &&
        Topology->bIsValid &&
        Topology->UVChannelIndex == OriginalUVChannelIndex &&
        UVChannelIndex == OriginalUVChannelIndex &&
        Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology &&
        !Topology->BuildSignature.IsEmpty() &&
        !Topology->Islands.IsEmpty();

    const FWCAUVIslandViewCacheKey Key(
        AnalysisMesh,
        WetClothingAsset,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        GGlobalRevision,
        GetMeshRevision(AnalysisMesh),
        GetAssetRevision(WetClothingAsset));
    if (const FWCAUVIslandViewCacheEntry* ExistingEntry = GUVIslandCache.Find(Key))
    {
        CopyCachedIslands(*ExistingEntry, OutIslands);
        return true;
    }

    TArray<FWetClothingAssetUVIsland> BuiltIslands;
    bool                              bBuilt = false;
    if (bUseStoredTopology)
    {
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Topology->Islands,
            BuiltIslands,
            OutErrorMessage);
    }

    if (!bBuilt)
    {
        BuiltIslands.Reset();
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            OutErrorMessage);
    }

    if (!bBuilt)
    {
        return false;
    }

    FWCAUVIslandViewCacheEntry NewEntry;
    MoveBuiltIslandsToEntry(MoveTemp(BuiltIslands), NewEntry);
    CopyCachedIslands(NewEntry, OutIslands);
    GUVIslandCache.Add(Key, MoveTemp(NewEntry));
    return true;
}

bool FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
    const USkeletalMesh*                 SkeletalMesh,
    int32                                MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    return BuildMaterialSlotPreviewTriangles(
        SkeletalMesh,
        0,
        0,
        MaterialSlotIndex,
        OutTriangles);
}

bool FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
    const USkeletalMesh*                 SkeletalMesh,
    const int32                          LODIndex,
    const int32                          UVChannelIndex,
    const int32                          MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    OutTriangles.Reset();

    if (SkeletalMesh == nullptr || FWetClothingAssetMeshAnalyzer::GetNumUVChannels(SkeletalMesh, LODIndex) <= 0)
    {
        return false;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> BuiltIslands;
    if (!GetMaterialSlotUVIslands(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            nullptr))
    {
        return false;
    }

    for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : BuiltIslands)
    {
        if (Island.IsValid())
        {
            OutTriangles.Append(Island->UVTriangles);
        }
    }

    return OutTriangles.Num() > 0;
}

bool FWCAUVIslandViewCache::BuildMaterialSlotGeometryPreviewTriangles(
    const USkeletalMesh*                 SkeletalMesh,
    const int32                          LODIndex,
    const int32                          PreferredUVChannelIndex,
    const int32                          MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    OutTriangles.Reset();

    if (SkeletalMesh == nullptr || !SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32                       VertexCount = static_cast<int32>(LODData.GetNumVertices());
    if (VertexCount <= 0)
    {
        return false;
    }

    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        return false;
    }

    const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
    const int32 PreviewUVChannelIndex = NumUVChannels > 0
                                            ? FMath::Clamp(PreferredUVChannelIndex, 0, NumUVChannels - 1)
                                            : INDEX_NONE;

    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(
            FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
            IndexBuffer.Num());

        for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
        {
            const uint32 Indices[3] = {
                IndexBuffer[TriangleIndex],
                IndexBuffer[TriangleIndex + 1],
                IndexBuffer[TriangleIndex + 2]
            };
            if (Indices[0] >= static_cast<uint32>(VertexCount) ||
                Indices[1] >= static_cast<uint32>(VertexCount) ||
                Indices[2] >= static_cast<uint32>(VertexCount))
            {
                continue;
            }

            FWetClothingAssetUVTriangle Triangle;
            Triangle.TriangleID = TriangleIndex / 3;
            Triangle.MaterialSlotIndex = MaterialSlotIndex;
            Triangle.UVIslandID = INDEX_NONE;

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const uint32 VertexIndex = Indices[CornerIndex];
                Triangle.LocalPositions[CornerIndex] = FVector(
                    LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                Triangle.RenderVertexIndices[CornerIndex] = static_cast<int32>(VertexIndex);
                Triangle.LocalNormals[CornerIndex] = FVector(
                                                         LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex))
                                                         .GetSafeNormal();

                FVector2D PreviewUV = FVector2D::ZeroVector;
                if (PreviewUVChannelIndex != INDEX_NONE)
                {
                    PreviewUV = FVector2D(
                        LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(
                            VertexIndex,
                            PreviewUVChannelIndex));
                    if (!FDWCUVGeometry::IsFiniteReasonableUV(PreviewUV))
                    {
                        PreviewUV = FVector2D::ZeroVector;
                    }
                }
                Triangle.UVs[CornerIndex] = PreviewUV;
            }

            if (FDWCUVGeometry::ComputeTriangleDoubleArea3D(
                    Triangle.LocalPositions[0],
                    Triangle.LocalPositions[1],
                    Triangle.LocalPositions[2]) <= 1.0e-10)
            {
                continue;
            }

            OutTriangles.Add(MoveTemp(Triangle));
        }
    }

    return !OutTriangles.IsEmpty();
}

bool FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
    const UWetClothingAsset*             WetClothingAsset,
    int32                                MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    OutTriangles.Reset();

    if (WetClothingAsset == nullptr)
    {
        return false;
    }

    const USkeletalMesh* PreparedMesh = WetClothingAsset->GetRuntimeSkeletalMesh();
    const int32          OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
    if (PreparedMesh == nullptr ||
        OriginalUVChannelIndex < 0 ||
        OriginalUVChannelIndex >= FWetClothingAssetMeshAnalyzer::GetNumUVChannels(PreparedMesh, WetClothingAsset->GetSimulationLODIndex()))
    {
        return false;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> BuiltIslands;
    if (!GetMaterialSlotUVIslands(
            WetClothingAsset,
            OriginalUVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            nullptr))
    {
        return false;
    }

    for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : BuiltIslands)
    {
        if (Island.IsValid())
        {
            OutTriangles.Append(Island->UVTriangles);
        }
    }

    return !OutTriangles.IsEmpty();
}

void FWCAUVIslandViewCache::InvalidateAll()
{
    GUVIslandCache.Reset();
    ++GGlobalRevision;
}

void FWCAUVIslandViewCache::InvalidateMesh(const USkeletalMesh* SkeletalMesh)
{
    if (SkeletalMesh == nullptr)
    {
        return;
    }

    const FObjectKey MeshKey(SkeletalMesh);
    ++GMeshRevisions.FindOrAdd(MeshKey);
    for (auto It = GUVIslandCache.CreateIterator(); It; ++It)
    {
        if (It.Key().MeshKey == MeshKey)
        {
            It.RemoveCurrent();
        }
    }
}

void FWCAUVIslandViewCache::InvalidateAsset(const UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr)
    {
        return;
    }

    const FObjectKey AssetKey(WetClothingAsset);
    ++GAssetRevisions.FindOrAdd(AssetKey);
    for (auto It = GUVIslandCache.CreateIterator(); It; ++It)
    {
        if (It.Key().AssetKey == AssetKey)
        {
            It.RemoveCurrent();
        }
    }
}
