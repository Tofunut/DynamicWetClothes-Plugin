#include "RuntimeState/DWCRuntimeDataSubsystem.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Async/DWCSkinningTasks.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/WetRuntimeDataBuilder.h"
#include "Utility/DWCLog.h"

#include "DataAssets/WetClothingAsset.h"

TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::AcquireSharedRuntimeData(
    const UWetClothingAsset& WetClothingAsset,
    USkeletalMeshComponent& TargetSkeletalMesh,
    UObject* OwnerForLogs)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr ||
        WetClothingAsset.GetDWCSkeletalMesh() != SkeletalMesh ||
        !WetClothingAsset.IsPrecomputedSimulationDataMetadataValidForMesh(SkeletalMesh))
    {
        return nullptr;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData =
        WetClothingAsset.GetPrecomputedSimulationData();

    FDWCSharedRuntimeDataKey Key;
    Key.WetClothingAsset = FObjectKey(&WetClothingAsset);
    Key.SkeletalMesh = FObjectKey(SkeletalMesh);
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
    if (!WetClothingAsset.IsPrecomputedSimulationDataValidForMesh(SkeletalMesh))
    {
        return nullptr;
    }

    TSharedPtr<FWetClothingRuntimeData, ESPMode::ThreadSafe> MutableData =
        MakeShared<FWetClothingRuntimeData, ESPMode::ThreadSafe>();
    MutableData->LODIndex = RuntimeLODIndex;
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
    Args.bUsePrecomputedSimulationData = true;
    Args.bUsePrecomputedBoneOptimizationCache = true;
    Args.bPrecomputedDataAlreadyValidated = true;

    if (!Builder.InitializeWetPartVertexData(Args))
    {
        return nullptr;
    }

    // The bone cache is a broad-phase optimization. Its absence is non-fatal;
    // contact resolution will use its existing full-vertex fallback.
    Builder.InitializeBoneOptimizationCacheFromPrecomputedData(Args);

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
            RuntimeLODIndex);
    }

    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedData = MutableData;
    SharedRuntimeDataCache.Add(Key, SharedData);
    PruneExpiredEntries();
    return SharedData;
}

FDWCLODVertexStaticDataKey UDWCRuntimeDataSubsystem::MakeLODVertexStaticDataKey(
    const USkeletalMeshComponent& TargetSkeletalMesh,
    const int32 LODIndex,
    const FString& MeshSignature) const
{
    FDWCLODVertexStaticDataKey Key;
    const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    Key.SkeletalMesh = FObjectKey(SkeletalMesh);
    Key.LODIndex = LODIndex;
    Key.MeshSignature = MeshSignature;

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        Key.LODRenderDataIdentity = reinterpret_cast<UPTRINT>(&RenderData->LODRenderData[LODIndex]);
    }

    return Key;
}

FDWCLODVertexColorTransferMapKey UDWCRuntimeDataSubsystem::MakeLODVertexColorTransferMapKey(
    const USkeletalMeshComponent& TargetSkeletalMesh,
    const FDWCLODVertexStaticData& SourceLODData,
    const FDWCLODVertexStaticData& TargetLODData,
    const FString& MeshSignature,
    const FDWCLODVertexColorTransferSettings& Settings) const
{
    FDWCLODVertexColorTransferMapKey Key;
    Key.SkeletalMesh = FObjectKey(TargetSkeletalMesh.GetSkeletalMeshAsset());
    Key.SourceLODRenderDataIdentity = SourceLODData.Geometry.VertexDataIdentity;
    Key.TargetLODRenderDataIdentity = TargetLODData.Geometry.VertexDataIdentity;
    Key.SourceLODIndex = SourceLODData.LODIndex;
    Key.TargetLODIndex = TargetLODData.LODIndex;
    Key.MeshSignature = MeshSignature;
    Key.Settings = Settings;
    return Key;
}

TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::AcquireSkinningStaticData(
    USkeletalMeshComponent& TargetSkeletalMesh,
    const FString& MeshSignature)
{
    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr || MeshSignature.IsEmpty())
    {
        return nullptr;
    }

    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    const FSkinWeightVertexBuffer* SkinWeightBuffer = TargetSkeletalMesh.GetSkinWeightBuffer(RuntimeLODIndex);
    if (SkinWeightBuffer == nullptr)
    {
        return nullptr;
    }

    FDWCSkinningStaticDataKey Key;
    Key.SkeletalMesh = FObjectKey(SkeletalMesh);
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
        BuildDWCSkinningStaticData(&TargetSkeletalMesh);
    if (SharedData.IsValid())
    {
        SkinningStaticDataCache.Add(Key, SharedData);
        PruneExpiredEntries();
    }
    return SharedData;
}

TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::AcquireLODVertexStaticData(
    USkeletalMeshComponent& TargetSkeletalMesh,
    const int32 LODIndex,
    const FString& MeshSignature)
{
    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr || MeshSignature.IsEmpty())
    {
        return nullptr;
    }

    FDWCLODVertexStaticDataKey Key = MakeLODVertexStaticDataKey(TargetSkeletalMesh, LODIndex, MeshSignature);
    if (Key.LODRenderDataIdentity == 0)
    {
        return nullptr;
    }

    if (const TWeakPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>* ExistingWeak =
            LODVertexStaticDataCache.Find(Key))
    {
        if (TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> Existing = ExistingWeak->Pin())
        {
            return Existing;
        }
        LODVertexStaticDataCache.Remove(Key);
    }

    TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SharedData =
        BuildDWCLODVertexStaticData(&TargetSkeletalMesh, LODIndex);
    if (SharedData.IsValid())
    {
        LODVertexStaticDataCache.Add(Key, SharedData);
        PruneExpiredEntries();
    }
    return SharedData;
}

TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::FindLODVertexColorTransferMap(
    const USkeletalMeshComponent& TargetSkeletalMesh,
    const FDWCLODVertexStaticData& SourceLODData,
    const FDWCLODVertexStaticData& TargetLODData,
    const FString& MeshSignature,
    const FDWCLODVertexColorTransferSettings& Settings)
{
    if (TargetSkeletalMesh.GetSkeletalMeshAsset() == nullptr ||
        MeshSignature.IsEmpty() ||
        !SourceLODData.IsValid() ||
        !TargetLODData.IsValid())
    {
        return nullptr;
    }

    FDWCLODVertexColorTransferMapKey Key =
        MakeLODVertexColorTransferMapKey(TargetSkeletalMesh, SourceLODData, TargetLODData, MeshSignature, Settings);
    if (Key.SourceLODRenderDataIdentity == 0 || Key.TargetLODRenderDataIdentity == 0)
    {
        return nullptr;
    }

    if (const TWeakPtr<const TArray<int32>, ESPMode::ThreadSafe>* ExistingWeak =
            LODVertexColorTransferMapCache.Find(Key))
    {
        if (TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> Existing = ExistingWeak->Pin())
        {
            return Existing;
        }
        LODVertexColorTransferMapCache.Remove(Key);
    }

    return nullptr;
}

TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>
UDWCRuntimeDataSubsystem::CacheLODVertexColorTransferMap(
    const USkeletalMeshComponent& TargetSkeletalMesh,
    const FDWCLODVertexStaticData& SourceLODData,
    const FDWCLODVertexStaticData& TargetLODData,
    const FString& MeshSignature,
    const FDWCLODVertexColorTransferSettings& Settings,
    TArray<int32>&& TargetToSourceVertex)
{
    if (TargetSkeletalMesh.GetSkeletalMeshAsset() == nullptr ||
        MeshSignature.IsEmpty() ||
        !SourceLODData.IsValid() ||
        !TargetLODData.IsValid() ||
        TargetToSourceVertex.Num() != TargetLODData.Geometry.VertexCount)
    {
        return nullptr;
    }

    FDWCLODVertexColorTransferMapKey Key =
        MakeLODVertexColorTransferMapKey(TargetSkeletalMesh, SourceLODData, TargetLODData, MeshSignature, Settings);
    if (Key.SourceLODRenderDataIdentity == 0 || Key.TargetLODRenderDataIdentity == 0)
    {
        return nullptr;
    }

    TSharedRef<TArray<int32>, ESPMode::ThreadSafe> SharedMap =
        MakeShared<TArray<int32>, ESPMode::ThreadSafe>(MoveTemp(TargetToSourceVertex));
    LODVertexColorTransferMapCache.Add(Key, SharedMap);
    PruneExpiredEntries();
    return SharedMap;
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

    for (auto It = LODVertexStaticDataCache.CreateIterator(); It; ++It)
    {
        if (!It.Value().Pin().IsValid())
        {
            It.RemoveCurrent();
        }
    }

    for (auto It = LODVertexColorTransferMapCache.CreateIterator(); It; ++It)
    {
        if (!It.Value().Pin().IsValid())
        {
            It.RemoveCurrent();
        }
    }
}
