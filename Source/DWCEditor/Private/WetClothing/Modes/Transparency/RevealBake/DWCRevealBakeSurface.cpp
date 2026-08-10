// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyPaintIslandBuilder.h"

bool FDWCRevealBakeSurfaceFrame::IsValid() const
{
    return !Tangent.IsNearlyZero() && !Bitangent.IsNearlyZero() && !Normal.IsNearlyZero();
}

bool FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const FVector& Barycentric,
    FDWCRevealBakeSurfaceFrame& OutFrame)
{
    OutFrame = FDWCRevealBakeSurfaceFrame();
    const FVector InterpolatedNormal = (
        Triangle.Normals[0] * Barycentric.X +
        Triangle.Normals[1] * Barycentric.Y +
        Triangle.Normals[2] * Barycentric.Z).GetSafeNormal();
    if (InterpolatedNormal.IsNearlyZero())
    {
        return false;
    }

    const FVector EdgeU = Triangle.Positions[1] - Triangle.Positions[0];
    const FVector EdgeV = Triangle.Positions[2] - Triangle.Positions[0];
    const FVector2D UVU = Triangle.UVs[1] - Triangle.UVs[0];
    const FVector2D UVV = Triangle.UVs[2] - Triangle.UVs[0];
    const double Determinant = static_cast<double>(UVU.X) * UVV.Y -
        static_cast<double>(UVU.Y) * UVV.X;

    FVector RawTangent = FVector::ZeroVector;
    FVector RawBitangent = FVector::ZeroVector;
    if (!FMath::IsNearlyZero(Determinant, SMALL_NUMBER))
    {
        const double InverseDeterminant = 1.0 / Determinant;
        RawTangent = (EdgeU * UVV.Y - EdgeV * UVU.Y) * InverseDeterminant;
        RawBitangent = (EdgeV * UVU.X - EdgeU * UVV.X) * InverseDeterminant;
    }

    FVector Tangent = (RawTangent - InterpolatedNormal * FVector::DotProduct(RawTangent, InterpolatedNormal)).GetSafeNormal();
    if (Tangent.IsNearlyZero())
    {
        InterpolatedNormal.FindBestAxisVectors(Tangent, RawBitangent);
        Tangent.Normalize();
    }
    if (Tangent.IsNearlyZero())
    {
        return false;
    }

    FVector Bitangent = FVector::CrossProduct(InterpolatedNormal, Tangent).GetSafeNormal();
    if (Bitangent.IsNearlyZero())
    {
        return false;
    }
    if (!RawBitangent.IsNearlyZero() && FVector::DotProduct(Bitangent, RawBitangent) < 0.0f)
    {
        Bitangent *= -1.0f;
    }

    OutFrame.Tangent = Tangent;
    OutFrame.Bitangent = Bitangent;
    OutFrame.Normal = InterpolatedNormal;
    return OutFrame.IsValid();
}

FVector3f FDWCRevealBakeSurfaceFrameBuilder::ReorientTangentNormal(
    const FVector3f& SourceTangentNormal,
    const FDWCRevealBakeSurfaceFrame& SourceFrame,
    const FDWCRevealBakeSurfaceFrame& TargetFrame)
{
    const FVector SourceWorldNormal = (
        SourceFrame.Tangent * SourceTangentNormal.X +
        SourceFrame.Bitangent * SourceTangentNormal.Y +
        SourceFrame.Normal * SourceTangentNormal.Z).GetSafeNormal();
    if (SourceWorldNormal.IsNearlyZero() || !SourceFrame.IsValid() || !TargetFrame.IsValid())
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }

    FVector TargetTangentNormal(
        FVector::DotProduct(SourceWorldNormal, TargetFrame.Tangent),
        FVector::DotProduct(SourceWorldNormal, TargetFrame.Bitangent),
        FVector::DotProduct(SourceWorldNormal, TargetFrame.Normal));
    TargetTangentNormal = TargetTangentNormal.GetSafeNormal();
    if (TargetTangentNormal.IsNearlyZero())
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }
    // RG encoding reconstructs +Z in the material, so preserve the visible side
    // of the outer surface even if an imported source surface flips its normal.
    if (TargetTangentNormal.Z < 0.0f)
    {
        TargetTangentNormal *= -1.0f;
    }
    return FVector3f(TargetTangentNormal);
}

FColor FDWCRevealBakeSurfaceFrameBuilder::EncodeRevealSurface(
    const FVector3f& TargetTangentNormal,
    const float Metallic,
    const bool bHasValidSourceHit)
{
    FVector3f SafeNormal = TargetTangentNormal.GetSafeNormal();
    if (SafeNormal.IsNearlyZero())
    {
        SafeNormal = FVector3f(0.0f, 0.0f, 1.0f);
    }
    const auto EncodeNormalComponent = [](const float Component)
    {
        return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Component * 0.5f + 0.5f) * 255.0f), 0, 255));
    };
    return FColor(
        EncodeNormalComponent(SafeNormal.X),
        EncodeNormalComponent(SafeNormal.Y),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Metallic * 255.0f), 0, 255)),
        bHasValidSourceHit ? 255 : 0);
}

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
    FDWCRevealBakeSurface&       OutSurface,
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
                const uint32  VertexIndex = RawIndices[CornerIndex];
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
