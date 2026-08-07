//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Async/DWCLODVertexColorTasks.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"
#include "Utility/DWCProfiling.h"

namespace
{
    bool GetLODVertexColorLODRenderData(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData)
    {
        OutLODData = nullptr;
        if (!TargetSkeletalMesh)
        {
            return false;
        }

        const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
        if (!SkeletalMesh)
        {
            return false;
        }

        FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return false;
        }

        OutLODData = &RenderData->LODRenderData[LODIndex];
        return true;
    }

}

TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> BuildDWCLODVertexStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh,
    const int32 LODIndex)
{
    DWC_PROFILE_SCOPE(DWC_BuildLODVertexStaticData);

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODVertexColorLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return nullptr;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        return nullptr;
    }

    TSharedRef<FDWCLODVertexStaticData, ESPMode::ThreadSafe> StaticData =
        MakeShared<FDWCLODVertexStaticData, ESPMode::ThreadSafe>();
    StaticData->Geometry.SkeletalMeshIdentity = reinterpret_cast<UPTRINT>(TargetSkeletalMesh->GetSkeletalMeshAsset());
    StaticData->Geometry.VertexDataIdentity = reinterpret_cast<UPTRINT>(LODData);
    StaticData->Geometry.VertexCount = VertexCount;
    StaticData->LODIndex = LODIndex;
    StaticData->Geometry.LocalPositions.SetNumZeroed(VertexCount);
    StaticData->Geometry.LocalNormals.SetNumZeroed(VertexCount);

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        int32 SectionIndex = INDEX_NONE;
        int32 SectionVertexIndex = INDEX_NONE;
        LODData->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
        if (!LODData->RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
        {
            continue;
        }

        const FSkelMeshRenderSection& Section = LODData->RenderSections[SectionIndex];
        const int32 BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
        if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
        {
            continue;
        }

        StaticData->Geometry.LocalPositions[VertexIndex] =
            LODData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertexIndex);
        StaticData->Geometry.LocalNormals[VertexIndex] =
            LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();
    }

    if (!StaticData->IsValid())
    {
        return nullptr;
    }

    return StaticData;
}

static void TransferLODVertexColors(
    const TArray<FColor>& SourceColors,
    const TArray<int32>&  TargetToSourceVertex,
    TArray<FColor>&       OutTargetColors)
{
    OutTargetColors.SetNumUninitialized(TargetToSourceVertex.Num());

    for (int32 TargetVertexIndex = 0; TargetVertexIndex < TargetToSourceVertex.Num(); ++TargetVertexIndex)
    {
        const int32 SourceVertexIndex = TargetToSourceVertex[TargetVertexIndex];
        OutTargetColors[TargetVertexIndex] = SourceColors.IsValidIndex(SourceVertexIndex)
                                                 ? SourceColors[SourceVertexIndex]
                                                 : FColor::Black;
    }
}

static void TransferDirtyLODVertexColors(
    const TArray<FColor>& SourceColors,
    const TArray<int32>&  DirtySourceVertices,
    const TArray<int32>&  TargetToSourceVertex,
    const TArray<FColor>& CachedTargetColors,
    TArray<FColor>&       OutTargetColors)
{
    OutTargetColors = CachedTargetColors;

    TSet<int32> DirtySourceSet;
    DirtySourceSet.Reserve(DirtySourceVertices.Num());
    for (const int32 SourceVertexIndex : DirtySourceVertices)
    {
        if (SourceColors.IsValidIndex(SourceVertexIndex))
        {
            DirtySourceSet.Add(SourceVertexIndex);
        }
    }

    if (DirtySourceSet.IsEmpty())
    {
        return;
    }

    for (int32 TargetVertexIndex = 0; TargetVertexIndex < TargetToSourceVertex.Num(); ++TargetVertexIndex)
    {
        const int32 SourceVertexIndex = TargetToSourceVertex[TargetVertexIndex];
        if (DirtySourceSet.Contains(SourceVertexIndex) &&
            SourceColors.IsValidIndex(SourceVertexIndex) &&
            OutTargetColors.IsValidIndex(TargetVertexIndex))
        {
            OutTargetColors[TargetVertexIndex] = SourceColors[SourceVertexIndex];
        }
    }
}

FDWCLODVertexColorTransferTask::FDWCLODVertexColorTransferTask(
    TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
    FDWCLODVertexColorTransferSnapshot&&        InSnapshot)
    : Owner(InOwner), Snapshot(MoveTemp(InSnapshot))
{
}


void FDWCLODVertexColorTransferTask::ExecuteWorker()
{
    DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_ExecuteWorker);

    SetStatus(EDWCTaskStatus::Running);

    Result.ReceiverId = Snapshot.ReceiverId;
    Result.Generation = Snapshot.Generation;
    Result.DirtySourceVertexCount = Snapshot.DirtySourceVertices.Num();

    if (!Snapshot.SourceLODData.IsValid() ||
        Snapshot.SourceLODData->Geometry.LocalPositions.Num() == 0 ||
        Snapshot.SourceLODData->Geometry.LocalPositions.Num() != Snapshot.SourceColors.Num())
    {
        SetStatus(EDWCTaskStatus::Failed);
        return;
    }

    TArray<FDWCLODVertexColorTransferTargetGeometryView> MissingTargetGeometries;
    for (const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>& TargetLODData : Snapshot.TargetLODData)
    {
        if (!TargetLODData.IsValid())
        {
            continue;
        }

        const TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>* CachedMap =
            Snapshot.CachedTargetToSourceVertexByLOD.Find(TargetLODData->LODIndex);
        if (CachedMap != nullptr &&
            CachedMap->IsValid() &&
            (*CachedMap)->Num() == TargetLODData->Geometry.LocalPositions.Num())
        {
            continue;
        }

        MissingTargetGeometries.Add({
            TargetLODData->LODIndex,
            FDWCLODVertexColorTransferGeometryView{
                TargetLODData->Geometry.LocalPositions,
                TargetLODData->Geometry.LocalNormals
            }
        });
    }

    TMap<int32, TArray<int32>> BuiltTargetToSourceByLOD;
    TArray<FDWCLODVertexColorTransferMapBuildResult> BuiltTransferMaps;
    if (BuildDWCLODVertexColorTransferMaps(
            FDWCLODVertexColorTransferGeometryView{
                Snapshot.SourceLODData->Geometry.LocalPositions,
                Snapshot.SourceLODData->Geometry.LocalNormals
            },
            MissingTargetGeometries,
            BuiltTransferMaps))
    {
        for (FDWCLODVertexColorTransferMapBuildResult& BuiltTransferMap : BuiltTransferMaps)
        {
            BuiltTargetToSourceByLOD.Add(
                BuiltTransferMap.LODIndex,
                MoveTemp(BuiltTransferMap.TargetToSourceVertex));
        }
    }

    for (const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>& TargetLODData : Snapshot.TargetLODData)
    {
        DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_TransferTargetLOD);

        if (!TargetLODData.IsValid())
        {
            continue;
        }

        FDWCLODVertexColorTransferResult::FLODColors LODResult;
        LODResult.LODIndex = TargetLODData->LODIndex;

        const TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>* CachedMap =
            Snapshot.CachedTargetToSourceVertexByLOD.Find(LODResult.LODIndex);
        const TArray<int32>* TransferMap = nullptr;
        const bool bHasCachedMap =
            CachedMap != nullptr &&
            CachedMap->IsValid() &&
            (*CachedMap)->Num() == TargetLODData->Geometry.LocalPositions.Num();
        if (bHasCachedMap)
        {
            TransferMap = CachedMap->Get();
        }
        else
        {
            TArray<int32>* BuiltTransferMap = BuiltTargetToSourceByLOD.Find(LODResult.LODIndex);
            if (BuiltTransferMap == nullptr)
            {
                continue;
            }

            LODResult.TargetToSourceVertex = MoveTemp(*BuiltTransferMap);
            TransferMap = &LODResult.TargetToSourceVertex;
        }

        if (TransferMap == nullptr)
        {
            continue;
        }

        const TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>* CachedColors =
            Snapshot.CachedTargetColorsByLOD.Find(LODResult.LODIndex);
        if (bHasCachedMap &&
            CachedColors != nullptr &&
            CachedColors->IsValid() &&
            (*CachedColors)->Num() == TargetLODData->Geometry.LocalPositions.Num() &&
            !Snapshot.DirtySourceVertices.IsEmpty())
        {
            TransferDirtyLODVertexColors(
                Snapshot.SourceColors,
                Snapshot.DirtySourceVertices,
                *TransferMap,
                **CachedColors,
                LODResult.Colors);
        }
        else
        {
            TransferLODVertexColors(Snapshot.SourceColors, *TransferMap, LODResult.Colors);
        }

        Result.LODResults.Add(MoveTemp(LODResult));
    }

    SetStatus(EDWCTaskStatus::Completed);
}

void FDWCLODVertexColorTransferTask::CommitGameThread()
{
    DWC_PROFILE_SCOPE(DWC_LODVertexColorTransferTask_CommitGameThread);

    if (!Owner.IsValid() || GetStatus() != EDWCTaskStatus::Completed)
    {
        return;
    }

    Owner->CommitLODVertexColorTransferResult(MoveTemp(Result));
}

