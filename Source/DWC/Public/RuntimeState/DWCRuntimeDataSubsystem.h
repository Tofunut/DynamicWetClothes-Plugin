#pragma once

#include "CoreMinimal.h"
#include "Misc/Crc.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DWCRuntimeDataSubsystem.generated.h"

class FWetClothingRuntimeData;
class UWetClothingAsset;
class USkeletalMeshComponent;
class USkeletalMesh;
struct FDWCSkinningStaticData;
struct FDWCLODVertexStaticData;

struct DWC_API FDWCSharedRuntimeDataKey
{
    FObjectKey WetClothingAsset;
    FObjectKey SkeletalMesh;
    int32 DataVersion = 0;
    FString MeshSignature;
    FString SourceDataSignature;

    bool operator==(const FDWCSharedRuntimeDataKey& Other) const
    {
        return WetClothingAsset == Other.WetClothingAsset &&
               SkeletalMesh == Other.SkeletalMesh &&
               DataVersion == Other.DataVersion &&
               MeshSignature == Other.MeshSignature &&
               SourceDataSignature == Other.SourceDataSignature;
    }

    friend uint32 GetTypeHash(const FDWCSharedRuntimeDataKey& Key)
    {
        uint32 Hash = HashCombine(GetTypeHash(Key.WetClothingAsset), GetTypeHash(Key.SkeletalMesh));
        Hash = HashCombine(Hash, GetTypeHash(Key.DataVersion));
        Hash = HashCombine(Hash, FCrc::StrCrc32(*Key.MeshSignature));
        return HashCombine(Hash, FCrc::StrCrc32(*Key.SourceDataSignature));
    }
};

struct DWC_API FDWCSkinningStaticDataKey
{
    FObjectKey SkeletalMesh;
    UPTRINT SkinWeightBufferIdentity = 0;
    FString MeshSignature;

    bool operator==(const FDWCSkinningStaticDataKey& Other) const
    {
        return SkeletalMesh == Other.SkeletalMesh &&
               SkinWeightBufferIdentity == Other.SkinWeightBufferIdentity &&
               MeshSignature == Other.MeshSignature;
    }

    friend uint32 GetTypeHash(const FDWCSkinningStaticDataKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.SkeletalMesh);
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint64>(Key.SkinWeightBufferIdentity)));
        return HashCombine(Hash, FCrc::StrCrc32(*Key.MeshSignature));
    }
};

struct DWC_API FDWCLODVertexStaticDataKey
{
    FObjectKey SkeletalMesh;
    UPTRINT LODRenderDataIdentity = 0;
    int32 LODIndex = INDEX_NONE;
    FString MeshSignature;

    bool operator==(const FDWCLODVertexStaticDataKey& Other) const
    {
        return SkeletalMesh == Other.SkeletalMesh &&
               LODRenderDataIdentity == Other.LODRenderDataIdentity &&
               LODIndex == Other.LODIndex &&
               MeshSignature == Other.MeshSignature;
    }

    friend uint32 GetTypeHash(const FDWCLODVertexStaticDataKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.SkeletalMesh);
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint64>(Key.LODRenderDataIdentity)));
        Hash = HashCombine(Hash, GetTypeHash(Key.LODIndex));
        return HashCombine(Hash, FCrc::StrCrc32(*Key.MeshSignature));
    }
};

struct DWC_API FDWCLODVertexColorTransferMapKey
{
    FObjectKey SkeletalMesh;
    UPTRINT SourceLODRenderDataIdentity = 0;
    UPTRINT TargetLODRenderDataIdentity = 0;
    int32 SourceLODIndex = INDEX_NONE;
    int32 TargetLODIndex = INDEX_NONE;
    FString MeshSignature;

    bool operator==(const FDWCLODVertexColorTransferMapKey& Other) const
    {
        return SkeletalMesh == Other.SkeletalMesh &&
               SourceLODRenderDataIdentity == Other.SourceLODRenderDataIdentity &&
               TargetLODRenderDataIdentity == Other.TargetLODRenderDataIdentity &&
               SourceLODIndex == Other.SourceLODIndex &&
               TargetLODIndex == Other.TargetLODIndex &&
               MeshSignature == Other.MeshSignature;
    }

    friend uint32 GetTypeHash(const FDWCLODVertexColorTransferMapKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.SkeletalMesh);
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint64>(Key.SourceLODRenderDataIdentity)));
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint64>(Key.TargetLODRenderDataIdentity)));
        Hash = HashCombine(Hash, GetTypeHash(Key.SourceLODIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.TargetLODIndex));
        return HashCombine(Hash, FCrc::StrCrc32(*Key.MeshSignature));
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
        UObject* OwnerForLogs = nullptr);

    void InvalidateSharedRuntimeData(const UWetClothingAsset* WetClothingAsset);

    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> AcquireSkinningStaticData(
        USkeletalMeshComponent& TargetSkeletalMesh,
        const FString& MeshSignature);

    TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> AcquireLODVertexStaticData(
        USkeletalMeshComponent& TargetSkeletalMesh,
        int32 LODIndex,
        const FString& MeshSignature);

    TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> FindLODVertexColorTransferMap(
        const USkeletalMeshComponent& TargetSkeletalMesh,
        const FDWCLODVertexStaticData& SourceLODData,
        const FDWCLODVertexStaticData& TargetLODData,
        const FString& MeshSignature);

    TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> CacheLODVertexColorTransferMap(
        const USkeletalMeshComponent& TargetSkeletalMesh,
        const FDWCLODVertexStaticData& SourceLODData,
        const FDWCLODVertexStaticData& TargetLODData,
        const FString& MeshSignature,
        TArray<int32>&& TargetToSourceVertex);

    void PruneExpiredEntries();

private:
    FDWCLODVertexStaticDataKey MakeLODVertexStaticDataKey(
        const USkeletalMeshComponent& TargetSkeletalMesh,
        int32 LODIndex,
        const FString& MeshSignature) const;

    FDWCLODVertexColorTransferMapKey MakeLODVertexColorTransferMapKey(
        const USkeletalMeshComponent& TargetSkeletalMesh,
        const FDWCLODVertexStaticData& SourceLODData,
        const FDWCLODVertexStaticData& TargetLODData,
        const FString& MeshSignature) const;

    TMap<FDWCSharedRuntimeDataKey, TWeakPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe>> SharedRuntimeDataCache;
    TMap<FDWCSkinningStaticDataKey, TWeakPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>> SkinningStaticDataCache;
    TMap<FDWCLODVertexStaticDataKey, TWeakPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> LODVertexStaticDataCache;
    TMap<FDWCLODVertexColorTransferMapKey, TWeakPtr<const TArray<int32>, ESPMode::ThreadSafe>> LODVertexColorTransferMapCache;
};
