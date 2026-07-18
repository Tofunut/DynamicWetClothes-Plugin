#include "RuntimeState/DWCRuntimeDataSubsystem.h"

#include "Async/DWCSkinningTasks.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/WetRuntimeDataBuilder.h"
#include "Utility/DWCLog.h"

TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::AcquireSharedRuntimeData(
    const UWetClothingAsset& WetClothingAsset,
    USkeletalMeshComponent& TargetSkeletalMesh,
    const int32 LODIndex,
    UObject* OwnerForLogs)
{
    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr ||
        WetClothingAsset.GetDWCSkeletalMesh() != SkeletalMesh ||
        !WetClothingAsset.IsPrecomputedSimulationDataMetadataValidForMesh(SkeletalMesh, LODIndex))
    {
        return nullptr;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData =
        WetClothingAsset.GetPrecomputedSimulationData(LODIndex);

    FDWCSharedRuntimeDataKey Key;
    Key.WetClothingAsset = FObjectKey(&WetClothingAsset);
    Key.SkeletalMesh = FObjectKey(SkeletalMesh);
    Key.LODIndex = LODIndex;
    Key.DataVersion = PrecomputedData.DataVersion;
    Key.MeshSignature = PrecomputedData.MeshSignature;
    Key.SourceDataSignature = PrecomputedData.SourceDataSignature;

    if (const TWeakPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe>* ExistingWeak =
            SharedRuntimeDataCache.Find(Key))
    {
        if (TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> Existing = ExistingWeak->Pin())
        {
            return Existing;
        }
        SharedRuntimeDataCache.Remove(Key);
    }

    // Full mesh/source signature validation is intentionally performed only on
    // cache miss. Additional receivers using the same immutable payload reuse
    // the validated shared object without repeating full mesh traversal.
    if (!WetClothingAsset.IsPrecomputedSimulationDataValidForMesh(SkeletalMesh, LODIndex))
    {
        return nullptr;
    }

    TSharedPtr<FWetClothingRuntimeData, ESPMode::ThreadSafe> MutableData =
        MakeShared<FWetClothingRuntimeData, ESPMode::ThreadSafe>();
    MutableData->LODIndex = LODIndex;
    MutableData->VertexCount = PrecomputedData.VertexCount;
    MutableData->DataVersion = PrecomputedData.DataVersion;
    MutableData->MeshSignature = PrecomputedData.MeshSignature;
    MutableData->SourceDataSignature = PrecomputedData.SourceDataSignature;

    FWetRuntimeDataBuilder Builder;
    FWetRuntimeDataBuildArgs Args;
    Args.OwnerForLogs = OwnerForLogs;
    Args.TargetSkeletalMesh = &TargetSkeletalMesh;
    Args.WetClothingAsset = &WetClothingAsset;
    Args.MutableRuntimeData = MutableData.Get();
    Args.RuntimeData = MutableData.Get();
    Args.LODIndex = LODIndex;
    Args.bUsePrecomputedSimulationData = true;
    Args.bUsePrecomputedBoneOptimizationCache = true;
    Args.bPrecomputedDataAlreadyValidated = true;

    if (!Builder.InitializeWetPartVertexData(Args))
    {
        return nullptr;
    }

    // The bone cache is a broad-phase optimization. Its absence is non-fatal;
    // contact resolution will use its existing full-vertex fallback.
    Builder.InitializeBoneOptimizationCacheFromPrecomputedData(Args, LODIndex);

    // Neighbor data is required only by the CPU spread simulation. Build it
    // once when available, but keep common data usable by GPU-only receivers.
    if (!Builder.InitializeNeighborGraphFromPrecomputedData(Args))
    {
        MutableData->ResetNeighborGraph();
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC shared runtime data: neighbor graph is unavailable for '%s' LOD %d. GPU receivers remain usable; CPU spread requires regenerated precomputed data."),
            *GetNameSafe(&WetClothingAsset),
            LODIndex);
    }

    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedData = MutableData;
    SharedRuntimeDataCache.Add(Key, SharedData);
    PruneExpiredEntries();
    return SharedData;
}

TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::AcquireSkinningStaticData(
    USkeletalMeshComponent& TargetSkeletalMesh,
    const FString& MeshSignature,
    const int32 LODIndex)
{
    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr || MeshSignature.IsEmpty())
    {
        return nullptr;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer = TargetSkeletalMesh.GetSkinWeightBuffer(LODIndex);
    if (SkinWeightBuffer == nullptr)
    {
        return nullptr;
    }

    FDWCSkinningStaticDataKey Key;
    Key.SkeletalMesh = FObjectKey(SkeletalMesh);
    Key.LODIndex = LODIndex;
    Key.SkinWeightBufferIdentity = reinterpret_cast<UPTRINT>(SkinWeightBuffer);
    Key.MeshSignature = MeshSignature;

    if (const TWeakPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>* ExistingWeak =
            SkinningStaticDataCache.Find(Key))
    {
        if (TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> Existing = ExistingWeak->Pin())
        {
            return Existing;
        }
        SkinningStaticDataCache.Remove(Key);
    }

    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> SharedData =
        BuildDWCSkinningStaticData(&TargetSkeletalMesh, LODIndex);
    if (SharedData.IsValid())
    {
        SkinningStaticDataCache.Add(Key, SharedData);
        PruneExpiredEntries();
    }
    return SharedData;
}

void UDWCRuntimeDataSubsystem::PruneExpiredEntries()
{
    for (auto It = SharedRuntimeDataCache.CreateIterator(); It; ++It)
    {
        if (!It.Value().Pin().IsValid())
        {
            It.RemoveCurrent();
        }
    }

    for (auto It = SkinningStaticDataCache.CreateIterator(); It; ++It)
    {
        if (!It.Value().Pin().IsValid())
        {
            It.RemoveCurrent();
        }
    }
}
