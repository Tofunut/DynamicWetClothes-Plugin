#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyPaintIslandBuilder.h"

void FDWCRevealBakeSurface::Reset()
{
    LayerId = NAME_None;
    LayerOrder = 0;
    LODIndex = 0;
    UVChannelIndex = 0;
    SkeletalMesh = nullptr;
    bCanBeRevealSource = true;
    bCanBeWetOuterLayer = true;
    bBlocksReveal = false;
    MaxRevealDistance = 5.0f;
    Triangles.Reset();
    Bounds = FBox(ForceInit);
}

bool FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
    const FDWCBakeResolvedLayer& ResolvedLayer,
    const int32                  LODIndex,
    const int32                  UVChannelIndex,
    FDWCRevealBakeSurface&             OutSurface,
    FString*                     OutErrorMessage)
{
    OutSurface.Reset();

    //-------------- Error Check---------------------------- Start
    if (ResolvedLayer.SkeletalMesh == nullptr)
    {
        SetError(OutErrorMessage, TEXT("Resolved layer has no skeletal mesh."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = ResolvedLayer.SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        SetError(OutErrorMessage, TEXT("Skeletal mesh render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetError(OutErrorMessage, TEXT("Requested LOD render data is unavailable."));
        return false;
    }
    //-------------- Error Check---------------------------- End

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32                       NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
    if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
    {
        SetError(OutErrorMessage, TEXT("Requested UV channel is unavailable."));
        return false;
    }

    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("Skeletal mesh index buffer is empty."));
        return false;
    }

    const int32 VertexCount = LODData.GetNumVertices();
    if (VertexCount <= 0)
    {
        SetError(OutErrorMessage, TEXT("Skeletal mesh vertex buffer is empty."));
        return false;
    }

    OutSurface.LayerId = ResolvedLayer.LayerId;
    OutSurface.LayerOrder = ResolvedLayer.LayerOrder;
    OutSurface.LODIndex = LODIndex;
    OutSurface.UVChannelIndex = UVChannelIndex;
    OutSurface.SkeletalMesh = ResolvedLayer.SkeletalMesh;
    OutSurface.bCanBeRevealSource = ResolvedLayer.bCanBeRevealSource;
    OutSurface.bCanBeWetOuterLayer = ResolvedLayer.bCanBeWetOuterLayer;
    OutSurface.bBlocksReveal = ResolvedLayer.bBlocksReveal;
    OutSurface.MaxRevealDistance = ResolvedLayer.MaxRevealDistance;
    OutSurface.Triangles.Reserve(IndexBuffer.Num() / 3);

    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        if (!Section.IsValid())
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

        for (int32 Index = FirstIndex; Index + 2 < LastIndex; Index += 3)
        {
            const uint32 RawIndices[3] = {
                IndexBuffer[Index],
                IndexBuffer[Index + 1],
                IndexBuffer[Index + 2]
            };

            if (RawIndices[0] >= static_cast<uint32>(VertexCount) ||
                RawIndices[1] >= static_cast<uint32>(VertexCount) ||
                RawIndices[2] >= static_cast<uint32>(VertexCount))
            {
                continue;
            }

            FDWCRevealBakeSurfaceTriangle Triangle;
            // Keep this as the render-buffer triangle id. DWC UV Channel metadata,
            // island clip buffers, and editor hit tests all key by this id.
            Triangle.TriangleIndex = Index / 3;
            Triangle.MaterialSlotIndex = Section.MaterialIndex;
            Triangle.Bounds = FBox(ForceInit);

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const uint32 VertexIndex = RawIndices[CornerIndex];
                const FVector LocalPosition(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                const FVector LocalNormal(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex));

                Triangle.VertexIndices[CornerIndex] = static_cast<int32>(VertexIndex);
                Triangle.Positions[CornerIndex] = ResolvedLayer.BakeTransform.TransformPosition(LocalPosition);
                Triangle.Normals[CornerIndex] =
                    ResolvedLayer.BakeTransform.TransformVectorNoScale(LocalNormal).GetSafeNormal();
                Triangle.UVs[CornerIndex] =
                    FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, UVChannelIndex));
                Triangle.Bounds += Triangle.Positions[CornerIndex];
            }

            OutSurface.Bounds += Triangle.Bounds;
            OutSurface.Triangles.Add(Triangle);
        }
    }

    if (OutSurface.Triangles.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("No bake surface triangles were generated."));
        return false;
    }

    TArray<FDWCTransparencyPaintIslandTriangle> PaintIslandTriangles;
    PaintIslandTriangles.Reserve(OutSurface.Triangles.Num());
    for (const FDWCRevealBakeSurfaceTriangle& Triangle : OutSurface.Triangles)
    {
        FDWCTransparencyPaintIslandTriangle& PaintTriangle =
            PaintIslandTriangles.AddDefaulted_GetRef();
        PaintTriangle.TriangleID = Triangle.TriangleIndex;
        PaintTriangle.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            PaintTriangle.Positions[CornerIndex] = Triangle.Positions[CornerIndex];
            PaintTriangle.UVs[CornerIndex] = Triangle.UVs[CornerIndex];
        }
    }

    TMap<int32, int32> PaintIslandIDByTriangleID;
    FDWCTransparencyPaintIslandBuilder::Build(
        PaintIslandTriangles,
        PaintIslandIDByTriangleID);
    for (FDWCRevealBakeSurfaceTriangle& Triangle : OutSurface.Triangles)
    {
        const int32* PaintIslandID =
            PaintIslandIDByTriangleID.Find(Triangle.TriangleIndex);
        Triangle.UVIslandID =
            PaintIslandID != nullptr ? *PaintIslandID : INDEX_NONE;
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

void FDWCRevealBakeSurfaceBuilder::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    if (OutErrorMessage != nullptr)
    {
        *OutErrorMessage = InMessage;
    }
}
