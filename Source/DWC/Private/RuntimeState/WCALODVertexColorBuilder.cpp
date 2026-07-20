#include "RuntimeState/WCALODVertexColorBuilder.h"

#include "DerivedData/DWCMeshContentSignature.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCError.h"

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

    int32 FindBestSourceVertex(
        const FLODVertexGeometry& Source,
        const FVector3f& TargetPosition,
        const FVector3f& TargetNormal)
    {
        int32 BestIndex = INDEX_NONE;
        float BestDistanceSq = TNumericLimits<float>::Max();
        float BestNormalDot = -1.0f;

        for (int32 SourceIndex = 0; SourceIndex < Source.Positions.Num(); ++SourceIndex)
        {
            const float NormalDot = Source.Normals.IsValidIndex(SourceIndex)
                                        ? FVector3f::DotProduct(Source.Normals[SourceIndex], TargetNormal)
                                        : 1.0f;
            const float DistanceSq = FVector3f::DistSquared(Source.Positions[SourceIndex], TargetPosition);
            if (BestIndex == INDEX_NONE ||
                DistanceSq < BestDistanceSq ||
                (FMath::IsNearlyEqual(DistanceSq, BestDistanceSq) && NormalDot > BestNormalDot))
            {
                BestIndex = SourceIndex;
                BestDistanceSq = DistanceSq;
                BestNormalDot = NormalDot;
            }
        }

        return BestIndex;
    }
}

bool FWCALODVertexColorBuilder::Build(
    const USkeletalMesh* Mesh,
    const int32 FirstMappedLODIndex,
    const int32 LastMappedLODIndex,
    TArray<FWCALODVertexColorRuntimeData>& OutRuntimeData,
    FString* OutErrorMessage)
{
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

    for (int32 TargetLODIndex = FirstTargetLODIndex; TargetLODIndex <= LastTargetLODIndex; ++TargetLODIndex)
    {

        FLODVertexGeometry TargetGeometry;
        if (!ReadLODGeometry(Mesh, TargetLODIndex, TargetGeometry))
        {
            continue;
        }

        FWCALODVertexColorRuntimeData& RuntimeData = OutRuntimeData.AddDefaulted_GetRef();
        RuntimeData.SourceLODIndex = SourceLODIndex;
        RuntimeData.TargetLODIndex = TargetLODIndex;
        RuntimeData.TargetVertexCount = TargetGeometry.Positions.Num();
        RuntimeData.MeshSignature = MeshSignature;
        RuntimeData.TargetToSourceVertex.SetNumUninitialized(TargetGeometry.Positions.Num());

        for (int32 TargetVertexIndex = 0; TargetVertexIndex < TargetGeometry.Positions.Num(); ++TargetVertexIndex)
        {
            const FVector3f TargetNormal = TargetGeometry.Normals.IsValidIndex(TargetVertexIndex)
                                               ? TargetGeometry.Normals[TargetVertexIndex]
                                               : FVector3f::UpVector;
            RuntimeData.TargetToSourceVertex[TargetVertexIndex] =
                FindBestSourceVertex(SourceGeometry, TargetGeometry.Positions[TargetVertexIndex], TargetNormal);
        }
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
