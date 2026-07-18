#include "Utility/DWCDataUVBufferView.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool FDWCDataUVBufferView::Initialize(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    FString* OutErrorMessage)
{
    Reset();

    auto SetError = [OutErrorMessage](const FString& Message)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = Message;
        }
    };

    if (SkeletalMesh == nullptr)
    {
        SetError(TEXT("No DWC Skeletal Mesh is available."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetError(FString::Printf(TEXT("LOD%d render data is unavailable on '%s'."), LODIndex, *GetNameSafe(SkeletalMesh)));
        return false;
    }

    const FSkeletalMeshLODRenderData& ResolvedLODData = RenderData->LODRenderData[LODIndex];
    const FStaticMeshVertexBuffer& ResolvedVertexBuffer = ResolvedLODData.StaticVertexBuffers.StaticMeshVertexBuffer;
    const int32 NumTexCoords = static_cast<int32>(ResolvedVertexBuffer.GetNumTexCoords());
    if (UVChannelIndex < 0 || UVChannelIndex >= NumTexCoords)
    {
        SetError(FString::Printf(
            TEXT("LOD%d on '%s' does not contain DWC Data UV channel %d (available channels: %d)."),
            LODIndex,
            *GetNameSafe(SkeletalMesh),
            UVChannelIndex,
            NumTexCoords));
        return false;
    }

    const int32 NumVertices = static_cast<int32>(ResolvedLODData.GetNumVertices());
    if (NumVertices <= 0)
    {
        SetError(FString::Printf(TEXT("LOD%d on '%s' has no render vertices."), LODIndex, *GetNameSafe(SkeletalMesh)));
        return false;
    }

    LODData = &ResolvedLODData;
    VertexBuffer = &ResolvedVertexBuffer;
    VertexCount = NumVertices;
    ResolvedLODIndex = LODIndex;
    ResolvedUVChannelIndex = UVChannelIndex;
    SetError(FString());
    return true;
}

void FDWCDataUVBufferView::Reset()
{
    LODData = nullptr;
    VertexBuffer = nullptr;
    VertexCount = 0;
    ResolvedLODIndex = INDEX_NONE;
    ResolvedUVChannelIndex = INDEX_NONE;
}

bool FDWCDataUVBufferView::IsValidVertexIndex(const int32 RenderVertexIndex) const
{
    return IsValid() && RenderVertexIndex >= 0 && RenderVertexIndex < VertexCount;
}

FVector2f FDWCDataUVBufferView::GetUV(const int32 RenderVertexIndex) const
{
    check(IsValidVertexIndex(RenderVertexIndex));
    return VertexBuffer->GetVertexUV(RenderVertexIndex, ResolvedUVChannelIndex);
}
