/*
 *  Skeletal Mesh, LOD, UV Channel, Material Slot 조합�?UV Island 분석 결과�?캐싱?�니??
 */

#include "WCAUVIslandViewCache.h"

#include "Engine/SkeletalMesh.h"
#include "DataAssets/WetClothingAsset.h"
#include "Misc/Crc.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace
{
    uint32 HashString(const FString& Value)
    {
        return FCrc::StrCrc32(*Value);
    }

    struct FWCAUVIslandViewCacheKey
    {
        FObjectKey MeshKey;
        FObjectKey AssetKey;
        int32 LODIndex = 0;
        int32 UVChannelIndex = 0;
        int32 MaterialSlotIndex = INDEX_NONE;
        uint32 DependencyHash = 0;

        FWCAUVIslandViewCacheKey(
            const USkeletalMesh* Mesh,
            const UWetClothingAsset* Asset,
            const int32 InLODIndex,
            const int32 InUVChannelIndex,
            const int32 InMaterialSlotIndex,
            const uint32 InDependencyHash)
            : MeshKey(Mesh)
            , AssetKey(Asset != nullptr ? FObjectKey(Asset) : FObjectKey())
            , LODIndex(InLODIndex)
            , UVChannelIndex(InUVChannelIndex)
            , MaterialSlotIndex(InMaterialSlotIndex)
            , DependencyHash(InDependencyHash)
        {
        }

        bool operator==(const FWCAUVIslandViewCacheKey& Other) const
        {
            return MeshKey == Other.MeshKey &&
                   AssetKey == Other.AssetKey &&
                   LODIndex == Other.LODIndex &&
                   UVChannelIndex == Other.UVChannelIndex &&
                   MaterialSlotIndex == Other.MaterialSlotIndex &&
                   DependencyHash == Other.DependencyHash;
        }
    };

    uint32 GetTypeHash(const FWCAUVIslandViewCacheKey& Key)
    {
        uint32 Hash = ::GetTypeHash(Key.MeshKey.GetWeakObjectPtr());
        Hash = HashCombine(Hash, ::GetTypeHash(Key.AssetKey.GetWeakObjectPtr()));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.UVChannelIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, Key.DependencyHash);
    }

    struct FWCAUVIslandViewCacheEntry
    {
        TArray<FWetClothingAssetUVIsland> Islands;
    };

    TMap<FWCAUVIslandViewCacheKey, FWCAUVIslandViewCacheEntry> GUVIslandCache;

    void CopyCachedIslands(
        const FWCAUVIslandViewCacheEntry& Entry,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands)
    {
        for (const FWetClothingAssetUVIsland& Island : Entry.Islands)
        {
            OutIslands.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
        }
    }
} // namespace

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

    uint32 DependencyHash = 0;
#if WITH_EDITOR
    DependencyHash = HashString(UWetClothingAsset::BuildMeshContentSignature(
        SkeletalMesh,
        LODIndex,
        UVChannelIndex));
#endif

    const FWCAUVIslandViewCacheKey Key(
        SkeletalMesh,
        nullptr,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        DependencyHash);

    if (const FWCAUVIslandViewCacheEntry* ExistingEntry = GUVIslandCache.Find(Key))
    {
        CopyCachedIslands(*ExistingEntry, OutIslands);
        return true;
    }

    FWCAUVIslandViewCacheEntry NewEntry;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            NewEntry.Islands,
            OutErrorMessage))
    {
        return false;
    }

    CopyCachedIslands(NewEntry, OutIslands);
    GUVIslandCache.Add(Key, MoveTemp(NewEntry));
    return true;
}

bool FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
    const UWetClothingAsset* WetClothingAsset,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString* OutErrorMessage)
{
    OutIslands.Reset();

    if (WetClothingAsset == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No Wet Clothing Asset is assigned."));
        return false;
    }

    const int32 LODIndex = WetClothingAsset->GetSimulationLODIndex();
    const int32 OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
    const USkeletalMesh* AnalysisMesh = WetClothingAsset->GetRuntimeSkeletalMesh();

    if (AnalysisMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No mesh is available for UV island analysis."));
        return false;
    }

    const FDWCEditorUVTopologyData* Topology = WetClothingAsset->FindOriginalUVTopologyForLOD(LODIndex);
    bool bUseStoredTopology = false;
    uint32 DependencyHash = 0;
#if WITH_EDITOR
    if (Topology != nullptr &&
        Topology->bIsValid &&
        Topology->UVChannelIndex == OriginalUVChannelIndex &&
        UVChannelIndex == OriginalUVChannelIndex &&
        Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology &&
        !Topology->BuildSignature.IsEmpty() &&
        !Topology->Islands.IsEmpty())
    {
        const FString CurrentPreparedMeshSignature = UWetClothingAsset::BuildMeshContentSignature(
            AnalysisMesh, LODIndex, OriginalUVChannelIndex);
        bUseStoredTopology = Topology->BuildSignature == CurrentPreparedMeshSignature;
        if (bUseStoredTopology)
        {
            DependencyHash = HashCombine(
                ::GetTypeHash(Topology->GeneratorVersion),
                HashString(Topology->BuildSignature));
        }
    }
#endif

    if (!bUseStoredTopology)
    {
#if WITH_EDITOR
        DependencyHash = HashString(UWetClothingAsset::BuildMeshContentSignature(
            AnalysisMesh, LODIndex, UVChannelIndex));
#endif
    }

    const FWCAUVIslandViewCacheKey Key(
        AnalysisMesh, WetClothingAsset, LODIndex, UVChannelIndex, MaterialSlotIndex, DependencyHash);
    if (const FWCAUVIslandViewCacheEntry* ExistingEntry = GUVIslandCache.Find(Key))
    {
        CopyCachedIslands(*ExistingEntry, OutIslands);
        return true;
    }

    FWCAUVIslandViewCacheEntry NewEntry;
    bool bBuilt = false;
    if (bUseStoredTopology)
    {
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Topology->Islands,
            NewEntry.Islands,
            OutErrorMessage);
    }

    if (!bBuilt)
    {
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            NewEntry.Islands,
            OutErrorMessage);
    }

    if (!bBuilt)
    {
        return false;
    }

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
    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
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
            const uint32 Indices[3] =
            {
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
    const int32 OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
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
}

void FWCAUVIslandViewCache::InvalidateMesh(const USkeletalMesh* SkeletalMesh)
{
    if (SkeletalMesh == nullptr)
    {
        return;
    }

    const FObjectKey MeshKey(SkeletalMesh);
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
    for (auto It = GUVIslandCache.CreateIterator(); It; ++It)
    {
        if (It.Key().AssetKey == AssetKey)
        {
            It.RemoveCurrent();
        }
    }
}
