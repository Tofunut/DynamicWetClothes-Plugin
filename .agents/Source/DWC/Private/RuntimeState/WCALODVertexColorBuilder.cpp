#include "RuntimeState/WCALODVertexColorBuilder.h"

#include "DerivedData/DWCMeshContentSignature.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"
#include "Utility/DWCError.h"
#include "Utility/DWCProfiling.h"

namespace
{
    struct FLODVertexGeometry
    {
        TArray<FVector3f> Positions;
        TArray<FVector3f> Normals;
    };

    bool ReadLODGeometry(const USkeletalMesh* Mesh, const int32 LODIndex, FLODVertexGeometry& OutGeometry)
    {
        OutGeometry = FLODVertexGeometry();

        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const int32 VertexCount = LODData.GetNumVertices();
        if (VertexCount <= 0)
        {
            return false;
        }

        OutGeometry.Positions.SetNumZeroed(VertexCount);
        OutGeometry.Normals.SetNumZeroed(VertexCount);

        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            int32 SectionIndex = INDEX_NONE;
            int32 SectionVertexIndex = INDEX_NONE;
            LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
            if (!LODData.RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
            {
                continue;
            }

            const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
            const int32 BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
            if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
            {
                continue;
            }

            OutGeometry.Positions[VertexIndex] =
                LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertexIndex);
            OutGeometry.Normals[VertexIndex] =
                LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();
        }

        return true;
    }
}

bool FWCALODVertexColorBuilder::IsCurrent(
    const USkeletalMesh* Mesh,
    const int32 FirstMappedLODIndex,
    const int32 LastMappedLODIndex,
    const TArray<FWCALODVertexColorRuntimeData>& RuntimeData)
{
    const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr)
    {
        return false;
    }

    constexpr int32 SourceLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (!RenderData->LODRenderData.IsValidIndex(SourceLODIndex))
    {
        return RuntimeData.IsEmpty();
    }

    const int32 LastAvailableLODIndex = RenderData->LODRenderData.Num() - 1;
    if (LastAvailableLODIndex <= SourceLODIndex ||
        LastMappedLODIndex < SourceLODIndex + 1 ||
        FirstMappedLODIndex > LastAvailableLODIndex)
    {
        return RuntimeData.IsEmpty();
    }

    const int32 FirstTargetLODIndex = FMath::Clamp(FirstMappedLODIndex, SourceLODIndex + 1, LastAvailableLODIndex);
    const int32 LastTargetLODIndex = FMath::Clamp(LastMappedLODIndex, FirstTargetLODIndex, LastAvailableLODIndex);
    if (RuntimeData.Num() != LastTargetLODIndex - FirstTargetLODIndex + 1)
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& SourceLODData = RenderData->LODRenderData[SourceLODIndex];
    const int32 SourceVertexCount = SourceLODData.GetNumVertices();
    const FString MeshSignature = FDWCMeshContentSignature::BuildStructure(Mesh, SourceLODData, SourceLODIndex);
    if (SourceVertexCount <= 0 || MeshSignature.IsEmpty())
    {
        return false;
    }

    for (int32 TargetLODIndex = FirstTargetLODIndex; TargetLODIndex <= LastTargetLODIndex; ++TargetLODIndex)
    {
        const FWCALODVertexColorRuntimeData* Entry = RuntimeData.FindByPredicate(
            [TargetLODIndex](const FWCALODVertexColorRuntimeData& Candidate)
            {
                return Candidate.TargetLODIndex == TargetLODIndex;
            });
        if (Entry == nullptr ||
            Entry->SourceLODIndex != SourceLODIndex ||
            Entry->TargetVertexCount != RenderData->LODRenderData[TargetLODIndex].GetNumVertices() ||
            Entry->MeshSignature != MeshSignature ||
            !Entry->IsValid())
        {
            return false;
        }

        if (Entry->TargetToSourceVertex.ContainsByPredicate(
                [SourceVertexCount](const int32 SourceVertexIndex)
                {
                    return SourceVertexIndex < 0 || SourceVertexIndex >= SourceVertexCount;
                }))
        {
            return false;
        }
    }

    return true;
}

bool FWCALODVertexColorBuilder::Build(
    const USkeletalMesh* Mesh,
    const int32 FirstMappedLODIndex,
    const int32 LastMappedLODIndex,
    TArray<FWCALODVertexColorRuntimeData>& OutRuntimeData,
    FString* OutErrorMessage)
{
    DWC_PROFILE_SCOPE(DWC_WCA_BuildLODVertexColorRuntimeData);

    OutRuntimeData.Reset();
    if (Mesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No prepared mesh is available for LOD vertex color runtime data."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
    if (RenderData == nullptr || RenderData->LODRenderData.Num() <= 1)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    constexpr int32 SourceLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    FLODVertexGeometry SourceGeometry;
    if (!ReadLODGeometry(Mesh, SourceLODIndex, SourceGeometry))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Failed to read prepared mesh LOD0 geometry for LOD vertex color runtime data."));
        return false;
    }

    const FString MeshSignature =
        FDWCMeshContentSignature::BuildStructure(Mesh, RenderData->LODRenderData[SourceLODIndex], SourceLODIndex);

    const int32 LastAvailableLODIndex = RenderData->LODRenderData.Num() - 1;
    const int32 FirstTargetLODIndex = FMath::Clamp(FirstMappedLODIndex, SourceLODIndex + 1, LastAvailableLODIndex);
    const int32 LastTargetLODIndex = FMath::Clamp(LastMappedLODIndex, FirstTargetLODIndex, LastAvailableLODIndex);

    if (LastMappedLODIndex < SourceLODIndex + 1 || FirstMappedLODIndex > LastAvailableLODIndex)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
        return true;
    }

    TArray<FLODVertexGeometry> TargetGeometries;
    TArray<FDWCLODVertexColorTransferTargetGeometryView> TargetGeometryViews;
    TargetGeometries.Reserve(LastTargetLODIndex - FirstTargetLODIndex + 1);
    TargetGeometryViews.Reserve(LastTargetLODIndex - FirstTargetLODIndex + 1);

    for (int32 TargetLODIndex = FirstTargetLODIndex; TargetLODIndex <= LastTargetLODIndex; ++TargetLODIndex)
    {
        FLODVertexGeometry& TargetGeometry = TargetGeometries.AddDefaulted_GetRef();
        if (!ReadLODGeometry(Mesh, TargetLODIndex, TargetGeometry))
        {
            TargetGeometries.Pop(EAllowShrinking::No);
            continue;
        }

        TargetGeometryViews.Add({
            TargetLODIndex,
            FDWCLODVertexColorTransferGeometryView{TargetGeometry.Positions, TargetGeometry.Normals}
        });
    }

    TArray<FDWCLODVertexColorTransferMapBuildResult> TransferMapResults;
    if (BuildDWCLODVertexColorTransferMaps(
            FDWCLODVertexColorTransferGeometryView{SourceGeometry.Positions, SourceGeometry.Normals},
            TargetGeometryViews,
            TransferMapResults))
    {
        OutRuntimeData.Reserve(TransferMapResults.Num());
        for (FDWCLODVertexColorTransferMapBuildResult& TransferMapResult : TransferMapResults)
        {
            FWCALODVertexColorRuntimeData RuntimeData;
            RuntimeData.SourceLODIndex = SourceLODIndex;
            RuntimeData.TargetLODIndex = TransferMapResult.LODIndex;
            RuntimeData.TargetVertexCount = TransferMapResult.TargetToSourceVertex.Num();
            RuntimeData.MeshSignature = MeshSignature;
            RuntimeData.TargetToSourceVertex = MoveTemp(TransferMapResult.TargetToSourceVertex);
            OutRuntimeData.Add(MoveTemp(RuntimeData));
        }
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
