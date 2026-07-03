/*
 *  Skeletal Mesh, LOD, UV Channel, Material Slot 조합별 UV Island 분석 결과를 캐싱합니다.
 */

#include "WetClothing/Analysis/WetClothingAssetUVIslandCache.h"

#include "Engine/SkeletalMesh.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"

namespace
{
    struct FWetClothingAssetUVIslandCacheKey
    {
        FObjectKey MeshKey;
        int32      LODIndex = 0;
        int32      UVChannelIndex = 0;
        int32      MaterialSlotIndex = INDEX_NONE;

        bool operator==(const FWetClothingAssetUVIslandCacheKey& Other) const
        {
            return MeshKey == Other.MeshKey && LODIndex == Other.LODIndex && UVChannelIndex == Other.UVChannelIndex && MaterialSlotIndex == Other.MaterialSlotIndex;
        }
    };

    uint32 MakeWetClothingAssetUVIslandCacheKeyHash(const FWetClothingAssetUVIslandCacheKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.MeshKey);
        Hash = HashCombine(Hash, ::GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.UVChannelIndex));
        Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
        return Hash;
    }

    uint32 GetTypeHash(const FWetClothingAssetUVIslandCacheKey& Key)
    {
        return MakeWetClothingAssetUVIslandCacheKeyHash(Key);
    }

    struct FWetClothingAssetUVIslandCacheEntry
    {
        TArray<FWetClothingAssetUVIsland> Islands;
    };

    TMap<FWetClothingAssetUVIslandCacheKey, FWetClothingAssetUVIslandCacheEntry> GUVIslandCache;
} // namespace

bool FWetClothingAssetUVIslandCache::GetMaterialSlotUVIslands(
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
        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Assign a TargetMesh to see its UV islands."));
        return false;
    }

    const FWetClothingAssetUVIslandCacheKey Key{ FObjectKey(SkeletalMesh), LODIndex, UVChannelIndex, MaterialSlotIndex };

    if (const FWetClothingAssetUVIslandCacheEntry* ExistingEntry = GUVIslandCache.Find(Key))
    {
        for (const FWetClothingAssetUVIsland& Island : ExistingEntry->Islands)
        {
            OutIslands.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
        }
        return true;
    }

    FWetClothingAssetUVIslandCacheEntry NewEntry;
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

    for (const FWetClothingAssetUVIsland& Island : NewEntry.Islands)
    {
        OutIslands.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
    }

    GUVIslandCache.Add(Key, MoveTemp(NewEntry));
    return true;
}

bool FWetClothingAssetUVIslandCache::BuildMaterialSlotPreviewTriangles(
    const USkeletalMesh*                 SkeletalMesh,
    int32                                MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    OutTriangles.Reset();

    if (SkeletalMesh == nullptr || FWetClothingAssetMeshAnalyzer::GetNumUVChannels(SkeletalMesh, 0) <= 0)
    {
        return false;
    }

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> BuiltIslands;
    if (!GetMaterialSlotUVIslands(SkeletalMesh, 0, 0, MaterialSlotIndex, BuiltIslands, nullptr))
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

void FWetClothingAssetUVIslandCache::Clear()
{
    GUVIslandCache.Reset();
}
