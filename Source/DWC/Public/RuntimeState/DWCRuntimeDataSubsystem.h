#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DWCRuntimeDataSubsystem.generated.h"

class FWetClothingRuntimeData;
class UWetClothingAsset;
class USkeletalMeshComponent;
class USkeletalMesh;
struct FDWCSkinningStaticData;

struct DWC_API FDWCSharedRuntimeDataKey
{
    FObjectKey WetClothingAsset;
    FObjectKey SkeletalMesh;
    int32 LODIndex = INDEX_NONE;
    int32 DataVersion = 0;
    FString MeshSignature;
    FString SourceDataSignature;

    bool operator==(const FDWCSharedRuntimeDataKey& Other) const
    {
        return WetClothingAsset == Other.WetClothingAsset &&
               SkeletalMesh == Other.SkeletalMesh &&
               LODIndex == Other.LODIndex &&
               DataVersion == Other.DataVersion &&
               MeshSignature == Other.MeshSignature &&
               SourceDataSignature == Other.SourceDataSignature;
    }

    friend uint32 GetTypeHash(const FDWCSharedRuntimeDataKey& Key)
    {
        uint32 Hash = HashCombine(GetTypeHash(Key.WetClothingAsset), GetTypeHash(Key.SkeletalMesh));
        Hash = HashCombine(Hash, GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.DataVersion));
        Hash = HashCombine(Hash, GetTypeHash(Key.MeshSignature));
        return HashCombine(Hash, GetTypeHash(Key.SourceDataSignature));
    }
};

struct DWC_API FDWCSkinningStaticDataKey
{
    FObjectKey SkeletalMesh;
    int32 LODIndex = INDEX_NONE;
    UPTRINT SkinWeightBufferIdentity = 0;
    FString MeshSignature;

    bool operator==(const FDWCSkinningStaticDataKey& Other) const
    {
        return SkeletalMesh == Other.SkeletalMesh &&
               LODIndex == Other.LODIndex &&
               SkinWeightBufferIdentity == Other.SkinWeightBufferIdentity &&
               MeshSignature == Other.MeshSignature;
    }

    friend uint32 GetTypeHash(const FDWCSkinningStaticDataKey& Key)
    {
        uint32 Hash = HashCombine(GetTypeHash(Key.SkeletalMesh), GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint64>(Key.SkinWeightBufferIdentity)));
        return HashCombine(Hash, GetTypeHash(Key.MeshSignature));
    }
};

/**
 * World-local cache for immutable WCA/mesh runtime data.
 * Receivers own strong references; the subsystem only keeps weak references so
 * unused payloads can be released without waiting for the World to shut down.
 */
UCLASS()
class DWC_API UDWCRuntimeDataSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> AcquireSharedRuntimeData(
        const UWetClothingAsset& WetClothingAsset,
        USkeletalMeshComponent& TargetSkeletalMesh,
        int32 LODIndex,
        UObject* OwnerForLogs = nullptr);

    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> AcquireSkinningStaticData(
        USkeletalMeshComponent& TargetSkeletalMesh,
        const FString& MeshSignature,
        int32 LODIndex);

    void PruneExpiredEntries();

private:
    TMap<FDWCSharedRuntimeDataKey, TWeakPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe>> SharedRuntimeDataCache;
    TMap<FDWCSkinningStaticDataKey, TWeakPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>> SkinningStaticDataCache;
};
