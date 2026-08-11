// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyPaintIslandBuilder.h"

namespace
{
    bool IsUsableDirection(const FVector& Direction)
    {
        return !Direction.ContainsNaN() &&
            FMath::IsFinite(Direction.X) &&
            FMath::IsFinite(Direction.Y) &&
            FMath::IsFinite(Direction.Z) &&
            !Direction.IsNearlyZero();
    }
}

bool FDWCRevealBakeSurfaceFrameBuilder::TransformImportedBasis(
    const FTransform& BakeTransform,
    const FVector3f& LocalTangent,
    const FVector3f& LocalBitangent,
    const FVector3f& LocalNormal,
    FVector3f& OutTangent,
    FVector3f& OutNormal,
    int8& OutBitangentSign)
{
    OutTangent = FVector3f(1.0f, 0.0f, 0.0f);
    OutNormal = FVector3f(0.0f, 0.0f, 1.0f);
    OutBitangentSign = 1;

    const FVector Scale = BakeTransform.GetScale3D();
    if (BakeTransform.ContainsNaN() ||
        FMath::Abs(Scale.X) <= UE_SMALL_NUMBER ||
        FMath::Abs(Scale.Y) <= UE_SMALL_NUMBER ||
        FMath::Abs(Scale.Z) <= UE_SMALL_NUMBER)
    {
        return false;
    }

    const FMatrix NormalTransform = BakeTransform.ToInverseMatrixWithScale().GetTransposed();
    FVector TransformedNormal = NormalTransform.TransformVector(FVector(LocalNormal)).GetSafeNormal();
    if (!IsUsableDirection(TransformedNormal))
    {
        return false;
    }
    OutNormal = FVector3f(TransformedNormal);

    FVector TransformedTangent = BakeTransform.TransformVector(FVector(LocalTangent));
    const FVector TransformedBitangent = BakeTransform.TransformVector(FVector(LocalBitangent));
    if (!IsUsableDirection(TransformedTangent) ||
        !IsUsableDirection(TransformedBitangent))
    {
        return false;
    }

    TransformedTangent = (
        TransformedTangent -
        TransformedNormal * FVector::DotProduct(TransformedTangent, TransformedNormal)).GetSafeNormal();
    if (!IsUsableDirection(TransformedTangent))
    {
        return false;
    }

    const FVector ReconstructedBitangent =
        FVector::CrossProduct(TransformedNormal, TransformedTangent).GetSafeNormal();
    if (!IsUsableDirection(ReconstructedBitangent))
    {
        return false;
    }

    OutTangent = FVector3f(TransformedTangent);
    OutBitangentSign =
        FVector::DotProduct(ReconstructedBitangent, TransformedBitangent) < 0.0 ? -1 : 1;
    return true;
}

bool FDWCRevealBakeSurfaceFrame::IsValid() const
{
    return IsUsableDirection(Tangent) &&
        IsUsableDirection(Bitangent) &&
        IsUsableDirection(Normal);
}

bool FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const FVector& Barycentric,
    FDWCRevealBakeSurfaceFrame& OutFrame)
{
    OutFrame = FDWCRevealBakeSurfaceFrame();
    if (!Triangle.bHasValidImportedTangentBasis)
    {
        return false;
    }

    const FVector InterpolatedNormal = (
        FVector(Triangle.Normals[0]) * Barycentric.X +
        FVector(Triangle.Normals[1]) * Barycentric.Y +
        FVector(Triangle.Normals[2]) * Barycentric.Z).GetSafeNormal();
    if (!IsUsableDirection(InterpolatedNormal))
    {
        return false;
    }

    FVector InterpolatedTangent = FVector::ZeroVector;
    FVector InterpolatedBitangent = FVector::ZeroVector;
    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
    {
        const double Weight = Barycentric[CornerIndex];
        const FVector CornerNormal(Triangle.Normals[CornerIndex]);
        const FVector CornerTangent(Triangle.Tangents[CornerIndex]);
        const FVector CornerBitangent =
            FVector::CrossProduct(CornerNormal, CornerTangent) *
            static_cast<double>(Triangle.BitangentSigns[CornerIndex]);
        InterpolatedTangent += CornerTangent * Weight;
        InterpolatedBitangent += CornerBitangent * Weight;
    }

    const FVector Tangent = (
        InterpolatedTangent -
        InterpolatedNormal * FVector::DotProduct(InterpolatedTangent, InterpolatedNormal)).GetSafeNormal();
    if (!IsUsableDirection(Tangent))
    {
        return false;
    }

    FVector Bitangent = FVector::CrossProduct(InterpolatedNormal, Tangent).GetSafeNormal();
    if (!IsUsableDirection(Bitangent))
    {
        return false;
    }
    if (IsUsableDirection(InterpolatedBitangent) &&
        FVector::DotProduct(Bitangent, InterpolatedBitangent) < 0.0)
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
    if (!SourceFrame.IsValid() || !TargetFrame.IsValid())
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }

    const FVector SourceWorldNormal = (
        SourceFrame.Tangent * SourceTangentNormal.X +
        SourceFrame.Bitangent * SourceTangentNormal.Y +
        SourceFrame.Normal * SourceTangentNormal.Z).GetSafeNormal();
    if (!IsUsableDirection(SourceWorldNormal))
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
            bool bHasValidImportedTangentBasis = true;

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const uint32 VertexIndex = RawIndices[CornerIndex];
                const FVector LocalPosition(
                    LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                const FVector4f ImportedTangentX =
                    LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
                const FVector4f ImportedTangentZ =
                    LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
                const FVector3f LocalTangent(
                    ImportedTangentX.X,
                    ImportedTangentX.Y,
                    ImportedTangentX.Z);
                const FVector3f LocalBitangent =
                    LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex);
                const FVector3f LocalNormal(
                    ImportedTangentZ.X,
                    ImportedTangentZ.Y,
                    ImportedTangentZ.Z);

                Triangle.VertexIndices[CornerIndex] = static_cast<int32>(VertexIndex);
                Triangle.Positions[CornerIndex] = ResolvedLayer.BakeTransform.TransformPosition(LocalPosition);
                bHasValidImportedTangentBasis &= FDWCRevealBakeSurfaceFrameBuilder::TransformImportedBasis(
                    ResolvedLayer.BakeTransform,
                    LocalTangent,
                    LocalBitangent,
                    LocalNormal,
                    Triangle.Tangents[CornerIndex],
                    Triangle.Normals[CornerIndex],
                    Triangle.BitangentSigns[CornerIndex]);
                Triangle.UVs[CornerIndex] =
                    FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, UVChannelIndex));
                Triangle.Bounds += Triangle.Positions[CornerIndex];
            }
            Triangle.bHasValidImportedTangentBasis = bHasValidImportedTangentBasis;

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
