#pragma once

#include "Async/DWCTask.h"

class UDynamicWetClothesComponent;
class USkeletalMeshComponent;

struct DWC_API FDWCLODVertexStaticData
{
    FDWCVertexGeometryStaticData Geometry;
    int32 LODIndex = INDEX_NONE;

    uint64 GetAllocatedMemoryBytes() const
    {
        return sizeof(*this) +
               Geometry.GetAllocatedArrayMemoryBytes();
    }

    bool IsValid() const
    {
        return LODIndex != INDEX_NONE &&
               Geometry.IsValid();
    }
};

struct DWC_API FDWCLODVertexColorTransferSnapshot
{
    FName ReceiverId = NAME_None;
    int32 Generation = 0;

    TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData;
    TArray<TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> TargetLODData;
    TMap<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>> CachedTargetToSourceVertexByLOD;
    TMap<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>> CachedTargetColorsByLOD;
    TArray<FColor> SourceColors;
    TArray<int32> DirtySourceVertices;
};

struct DWC_API FDWCLODVertexColorTransferResult
{
    FName ReceiverId = NAME_None;
    int32 Generation = 0;
    int32 DirtySourceVertexCount = 0;

    struct FLODColors
    {
        int32 LODIndex = INDEX_NONE;
        TArray<FColor> Colors;
        TArray<int32> TargetToSourceVertex;
    };

    TArray<FLODColors> LODResults;
};

class DWC_API FDWCLODVertexColorTransferTask final : public IDWCTaskRequest
{
  public:
    FDWCLODVertexColorTransferTask(
        TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
        FDWCLODVertexColorTransferSnapshot&&        InSnapshot);

    virtual void ExecuteWorker() override;
    virtual void CommitGameThread() override;

  private:
    TWeakObjectPtr<UDynamicWetClothesComponent> Owner;
    FDWCLODVertexColorTransferSnapshot          Snapshot;
    FDWCLODVertexColorTransferResult            Result;
};

DWC_API TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> BuildDWCLODVertexStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh,
    int32 LODIndex);
