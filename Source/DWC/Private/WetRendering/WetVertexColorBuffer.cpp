// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetRendering/WetVertexColorBuffer.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Public/Rendering/ColorVertexBuffer.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Runtime/RenderCore/Public/RenderingThread.h"
#include "Runtime/RenderCore/Public/RenderResource.h"
#include "Runtime/RHI/Public/RHICommandList.h"
#include "Utility/DWCProfiling.h"

void FWetVertexColorBuffer::ApplyVertexColorOverride(
    USkeletalMeshComponent& TargetSkeletalMesh,
    const int32             LODIndex,
    const TArray<FColor>&   VertexColors)
{
    DWC_PROFILE_SCOPE(DWC_Render_SetVertexColorOverride);

    if (!ApplyVertexColorOverrideByDirectBufferSwap(TargetSkeletalMesh, LODIndex, VertexColors))
    {
        DWC_PROFILE_SCOPE(DWC_Render_SetVertexColorOverride_Fallback);

        TArray<FLinearColor> LinearColors;
        LinearColors.SetNumUninitialized(VertexColors.Num());
        for (int32 VertexIndex = 0; VertexIndex < VertexColors.Num(); ++VertexIndex)
        {
            LinearColors[VertexIndex] = FLinearColor(VertexColors[VertexIndex]);
        }

        TargetSkeletalMesh.SetVertexColorOverride_LinearColor(LODIndex, LinearColors);
        TargetSkeletalMesh.MarkRenderStateDirty();
    }
}

bool FWetVertexColorBuffer::ApplyVertexColorOverrideByDirectBufferSwap(
    USkeletalMeshComponent& TargetSkeletalMesh,
    const int32             LODIndex,
    const TArray<FColor>&   VertexColors)
{
    DWC_PROFILE_SCOPE(DWC_Render_SwapVertexColorOverrideBuffer);

    TargetSkeletalMesh.InitLODInfos();

    FSkeletalMeshRenderData* SkelMeshRenderData = TargetSkeletalMesh.GetSkeletalMeshRenderData();
    if (SkelMeshRenderData == nullptr ||
        !TargetSkeletalMesh.LODInfo.IsValidIndex(LODIndex) ||
        !SkelMeshRenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = SkelMeshRenderData->LODRenderData[LODIndex];
    const int32                       ExpectedNumVerts = LODData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
    if (ExpectedNumVerts <= 0 || VertexColors.Num() != ExpectedNumVerts)
    {
        return false;
    }

    FColorVertexBuffer* NewOverrideVertexColors = new FColorVertexBuffer;
    NewOverrideVertexColors->InitFromColorArray(VertexColors);

    FSkelMeshComponentLODInfo& LODInfo = TargetSkeletalMesh.LODInfo[LODIndex];
    FColorVertexBuffer*        OldOverrideVertexColors = LODInfo.OverrideVertexColors;
    LODInfo.OverrideVertexColors = NewOverrideVertexColors;

    BeginInitResource(NewOverrideVertexColors);
    if (OldOverrideVertexColors != nullptr)
    {
        BeginReleaseResource(OldOverrideVertexColors);
        ENQUEUE_RENDER_COMMAND(DWCDeleteOldOverrideVertexColors)(
            [OldOverrideVertexColors](FRHICommandListBase& RHICmdList)
            {
                delete OldOverrideVertexColors;
            });
    }

    TargetSkeletalMesh.MarkRenderStateDirty();
    return true;
}
