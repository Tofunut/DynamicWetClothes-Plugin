//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "SDWCPartViewport.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "DWCPartViewportClient.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetRendering/DWCSurfaceTextureSharedAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetViewport"

namespace
{
    constexpr int32 PartViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.
    const FName PreviewSurfaceWaterOverrideParameter(TEXT("DWC_PreviewSurfaceWaterOverride"));
    const FName PreviewSurfaceWaterAmountParameter(TEXT("DWC_PreviewSurfaceWaterAmount"));
    const FName PreviewDebugModeParameter(TEXT("DWCPreview_DebugMode"));
    const FName PartPreviewColorTextureParameter(TEXT("DWC_PartPreviewColorTexture"));
    const FName PartPreviewSelectionTextureParameter(TEXT("DWC_PartPreviewSelectionTexture"));
    const FName PartPreviewColorOpacityParameter(TEXT("DWC_PartPreviewColorOpacity"));
    const FName PartPreviewSelectionFillOpacityParameter(TEXT("DWC_PartPreviewSelectionFillOpacity"));
    const FName PartPreviewSelectionBoundaryOpacityParameter(TEXT("DWC_PartPreviewSelectionBoundaryOpacity"));
    const FName PartPreviewSelectionFillColorParameter(TEXT("DWC_PartPreviewSelectionFillColor"));
    const FName PartPreviewSelectionBoundaryColorParameter(TEXT("DWC_PartPreviewSelectionBoundaryColor"));
    constexpr int32 PartPreviewTextureResolution = 1024;

    constexpr float SurfacePreviewMinDetailSize = 0.0f;
    constexpr float SurfacePreviewMaxDetailSize = 4.0f;


    void ConfigureStaticPartPreviewPose(USkeletalMeshComponent* MeshComponent)
    {
        if (MeshComponent == nullptr)
        {
            return;
        }

        MeshComponent->SetForcedLOD(PartViewportForceRenderLOD0);
        MeshComponent->SetEnableAnimation(false);
        MeshComponent->SetUpdateAnimationInEditor(false);
        MeshComponent->SetDisablePostProcessBlueprint(true);
        MeshComponent->SetUpdateClothInEditor(false);
        MeshComponent->SetForceRefPose(true);
    }


    uint8 EncodeSurfacePreviewUNorm(const float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    uint8 EncodeSurfacePreviewDetailSize(const float Value)
    {
        const float Normalized = FMath::GetRangePct(
            SurfacePreviewMinDetailSize,
            SurfacePreviewMaxDetailSize,
            FMath::Clamp(Value, SurfacePreviewMinDetailSize, SurfacePreviewMaxDetailSize));
        return EncodeSurfacePreviewUNorm(Normalized);
    }

    bool IsSurfacePreviewUVPointInsideTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double SignedAreaTwice = Sign(A, B, C);
        if (!FMath::IsFinite(SignedAreaTwice) || FMath::Abs(SignedAreaTwice) <= UE_DOUBLE_SMALL_NUMBER)
        {
            return false;
        }

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }

    bool IsSurfacePreviewUVFinite(const FVector2D& UV)
    {
        return FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y);
    }

    bool IsSurfacePreviewPointInsideRect(
        const FVector2D& Point,
        const FVector2D& Min,
        const FVector2D& Max)
    {
        constexpr double Epsilon = 1.0e-12;
        return Point.X >= Min.X - Epsilon && Point.X <= Max.X + Epsilon &&
               Point.Y >= Min.Y - Epsilon && Point.Y <= Max.Y + Epsilon;
    }

    double SurfacePreviewOrientation(
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C)
    {
        return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
    }

    bool DoSurfacePreviewSegmentsIntersect(
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C,
        const FVector2D& D)
    {
        constexpr double Epsilon = 1.0e-12;
        const double O1 = SurfacePreviewOrientation(A, B, C);
        const double O2 = SurfacePreviewOrientation(A, B, D);
        const double O3 = SurfacePreviewOrientation(C, D, A);
        const double O4 = SurfacePreviewOrientation(C, D, B);

        const auto IsOnSegment = [Epsilon](
            const FVector2D& P,
            const FVector2D& Q,
            const FVector2D& R)
        {
            return Q.X >= FMath::Min(P.X, R.X) - Epsilon &&
                   Q.X <= FMath::Max(P.X, R.X) + Epsilon &&
                   Q.Y >= FMath::Min(P.Y, R.Y) - Epsilon &&
                   Q.Y <= FMath::Max(P.Y, R.Y) + Epsilon;
        };

        if (((O1 > Epsilon && O2 < -Epsilon) || (O1 < -Epsilon && O2 > Epsilon)) &&
            ((O3 > Epsilon && O4 < -Epsilon) || (O3 < -Epsilon && O4 > Epsilon)))
        {
            return true;
        }
        return (FMath::Abs(O1) <= Epsilon && IsOnSegment(A, C, B)) ||
               (FMath::Abs(O2) <= Epsilon && IsOnSegment(A, D, B)) ||
               (FMath::Abs(O3) <= Epsilon && IsOnSegment(C, A, D)) ||
               (FMath::Abs(O4) <= Epsilon && IsOnSegment(C, B, D));
    }

    bool DoesSurfacePreviewTriangleIntersectTexel(
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C,
        const int32 X,
        const int32 Y,
        const int32 Width,
        const int32 Height)
    {
        const FVector2D RectMin(
            static_cast<double>(X) / Width,
            static_cast<double>(Y) / Height);
        const FVector2D RectMax(
            static_cast<double>(X + 1) / Width,
            static_cast<double>(Y + 1) / Height);

        if (IsSurfacePreviewPointInsideRect(A, RectMin, RectMax) ||
            IsSurfacePreviewPointInsideRect(B, RectMin, RectMax) ||
            IsSurfacePreviewPointInsideRect(C, RectMin, RectMax))
        {
            return true;
        }

        const FVector2D RectCorners[4] = {
            RectMin,
            FVector2D(RectMax.X, RectMin.Y),
            RectMax,
            FVector2D(RectMin.X, RectMax.Y)
        };
        for (const FVector2D& Corner : RectCorners)
        {
            if (IsSurfacePreviewUVPointInsideTriangle(Corner, A, B, C))
            {
                return true;
            }
        }

        const FVector2D TriangleEdges[3][2] = {{A, B}, {B, C}, {C, A}};
        for (const auto& TriangleEdge : TriangleEdges)
        {
            for (int32 RectEdgeIndex = 0; RectEdgeIndex < 4; ++RectEdgeIndex)
            {
                if (DoSurfacePreviewSegmentsIntersect(
                        TriangleEdge[0],
                        TriangleEdge[1],
                        RectCorners[RectEdgeIndex],
                        RectCorners[(RectEdgeIndex + 1) % 4]))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void RasterizeSurfacePreviewTriangleMask(
        TArray<uint8>& Mask,
        const int32 Width,
        const int32 Height,
        const FWetClothingAssetUVTriangle& Triangle)
    {
        const FVector2D& A = Triangle.UVs[0];
        const FVector2D& B = Triangle.UVs[1];
        const FVector2D& C = Triangle.UVs[2];
        if (!IsSurfacePreviewUVFinite(A) || !IsSurfacePreviewUVFinite(B) || !IsSurfacePreviewUVFinite(C))
        {
            return;
        }

        const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MaxX = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);
        const int32 MaxY = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);

        bool bPainted = false;
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                if (!DoesSurfacePreviewTriangleIntersectTexel(A, B, C, X, Y, Width, Height))
                {
                    continue;
                }

                Mask[Y * Width + X] = 1;
                bPainted = true;
            }
        }

        if (!bPainted)
        {
            const FVector2D Center = (A + B + C) / 3.0;
            const int32 X = FMath::Clamp(FMath::FloorToInt(Center.X * Width), 0, Width - 1);
            const int32 Y = FMath::Clamp(FMath::FloorToInt(Center.Y * Height), 0, Height - 1);
            Mask[Y * Width + X] = 1;
        }
    }


    void RasterizePreviewTriangleColor(
        TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height,
        const FWetClothingAssetUVTriangle& Triangle,
        const FColor& Color)
    {
        if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
        {
            return;
        }

        const FVector2D& A = Triangle.UVs[0];
        const FVector2D& B = Triangle.UVs[1];
        const FVector2D& C = Triangle.UVs[2];
        if (!IsSurfacePreviewUVFinite(A) || !IsSurfacePreviewUVFinite(B) || !IsSurfacePreviewUVFinite(C))
        {
            return;
        }
        const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MaxX = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.X, B.X, C.X) * Width), 0, Width - 1);
        const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);
        const int32 MaxY = FMath::Clamp(FMath::FloorToInt(FMath::Max3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);

        bool bPainted = false;
        for (int32 Y = MinY; Y <= MaxY; ++Y)
        {
            for (int32 X = MinX; X <= MaxX; ++X)
            {
                if (!DoesSurfacePreviewTriangleIntersectTexel(A, B, C, X, Y, Width, Height))
                {
                    continue;
                }
                Pixels[Y * Width + X] = Color;
                bPainted = true;
            }
        }

        if (!bPainted)
        {
            const FVector2D Center = (A + B + C) / 3.0;
            const int32 X = FMath::Clamp(FMath::FloorToInt(Center.X * Width), 0, Width - 1);
            const int32 Y = FMath::Clamp(FMath::FloorToInt(Center.Y * Height), 0, Height - 1);
            Pixels[Y * Width + X] = Color;
        }
    }

    uint64 MakePartPreviewRenderEdgeKey(const int32 VertexA, const int32 VertexB)
    {
        const uint32 MinVertex = static_cast<uint32>(FMath::Min(VertexA, VertexB));
        const uint32 MaxVertex = static_cast<uint32>(FMath::Max(VertexA, VertexB));
        return (static_cast<uint64>(MinVertex) << 32) | static_cast<uint64>(MaxVertex);
    }

    void GetPartPreviewTriangleEdgeKeys(
        const FWetClothingAssetUVTriangle& Triangle,
        uint64 OutEdgeKeys[3])
    {
        OutEdgeKeys[0] = MakePartPreviewRenderEdgeKey(
            Triangle.RenderVertexIndices[0],
            Triangle.RenderVertexIndices[1]);
        OutEdgeKeys[1] = MakePartPreviewRenderEdgeKey(
            Triangle.RenderVertexIndices[1],
            Triangle.RenderVertexIndices[2]);
        OutEdgeKeys[2] = MakePartPreviewRenderEdgeKey(
            Triangle.RenderVertexIndices[2],
            Triangle.RenderVertexIndices[0]);
    }

    uint32 MakePartPreviewPositionKey(const FVector& Position)
    {
        // Prepared meshes may split render vertices at UV/chart boundaries. Quantized
        // local positions allow omitted UV-degenerate triangles to inherit from their
        // geometric neighbor even when the render-vertex indices differ.
        constexpr double QuantizationScale = 10000.0;
        const FIntVector Quantized(
            FMath::RoundToInt(Position.X * QuantizationScale),
            FMath::RoundToInt(Position.Y * QuantizationScale),
            FMath::RoundToInt(Position.Z * QuantizationScale));
        return HashCombine(
            HashCombine(GetTypeHash(Quantized.X), GetTypeHash(Quantized.Y)),
            GetTypeHash(Quantized.Z));
    }

    uint64 MakePartPreviewPositionEdgeKey(const FVector& PositionA, const FVector& PositionB)
    {
        const uint32 KeyA = MakePartPreviewPositionKey(PositionA);
        const uint32 KeyB = MakePartPreviewPositionKey(PositionB);
        const uint32 MinKey = FMath::Min(KeyA, KeyB);
        const uint32 MaxKey = FMath::Max(KeyA, KeyB);
        return (static_cast<uint64>(MinKey) << 32) | static_cast<uint64>(MaxKey);
    }

    void GetPartPreviewTrianglePositionEdgeKeys(
        const FWetClothingAssetUVTriangle& Triangle,
        uint64 OutEdgeKeys[3])
    {
        OutEdgeKeys[0] = MakePartPreviewPositionEdgeKey(
            Triangle.LocalPositions[0],
            Triangle.LocalPositions[1]);
        OutEdgeKeys[1] = MakePartPreviewPositionEdgeKey(
            Triangle.LocalPositions[1],
            Triangle.LocalPositions[2]);
        OutEdgeKeys[2] = MakePartPreviewPositionEdgeKey(
            Triangle.LocalPositions[2],
            Triangle.LocalPositions[0]);
    }

    bool ReadPartPreviewRenderTrianglesIncludingDegenerateUV(
        const USkeletalMesh* SkeletalMesh,
        const int32 UVChannelIndex,
        const int32 MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles)
    {
        OutTriangles.Reset();
        if (SkeletalMesh == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
        {
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
        if (UVChannelIndex < 0 || UVChannelIndex >= static_cast<int32>(LODData.GetNumTexCoords()))
        {
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
        if (IndexBuffer.IsEmpty())
        {
            return false;
        }

        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(
                FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
                IndexBuffer.Num());
            for (int32 Index = FirstIndex; Index + 2 < LastIndex; Index += 3)
            {
                const uint32 VertexIndices[3] =
                {
                    IndexBuffer[Index],
                    IndexBuffer[Index + 1],
                    IndexBuffer[Index + 2]
                };
                if (VertexIndices[0] >= static_cast<uint32>(VertexCount) ||
                    VertexIndices[1] >= static_cast<uint32>(VertexCount) ||
                    VertexIndices[2] >= static_cast<uint32>(VertexCount))
                {
                    continue;
                }

                FWetClothingAssetUVTriangle Triangle;
                Triangle.TriangleID = Index / 3;
                Triangle.MaterialSlotIndex = Section.MaterialIndex;
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    const int32 VertexIndex = static_cast<int32>(VertexIndices[CornerIndex]);
                    Triangle.RenderVertexIndices[CornerIndex] = VertexIndex;
                    Triangle.UVs[CornerIndex] = FVector2D(
                        LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(
                            VertexIndex,
                            UVChannelIndex));
                    Triangle.LocalPositions[CornerIndex] = FVector(
                        LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                    Triangle.LocalNormals[CornerIndex] = FVector(
                        LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)).GetSafeNormal();
                }

                if (!IsSurfacePreviewUVFinite(Triangle.UVs[0]) ||
                    !IsSurfacePreviewUVFinite(Triangle.UVs[1]) ||
                    !IsSurfacePreviewUVFinite(Triangle.UVs[2]))
                {
                    continue;
                }

                const FVector EdgeA = Triangle.LocalPositions[1] - Triangle.LocalPositions[0];
                const FVector EdgeB = Triangle.LocalPositions[2] - Triangle.LocalPositions[0];
                if (FVector::CrossProduct(EdgeA, EdgeB).SizeSquared() <= 1.0e-20)
                {
                    continue;
                }

                // Deliberately keep zero-area and near-zero-area UV triangles. They are
                // excluded from UV-island authoring, but the render mesh still draws them.
                // Their interpolated UV is a line/point, so painting that line/point is
                // sufficient to cover the full rendered triangle in the overlay pass.
                OutTriangles.Add(MoveTemp(Triangle));
            }
        }
        return !OutTriangles.IsEmpty();
    }

    void DilateSurfacePreviewMask(TArray<uint8>& Mask, const int32 Width, const int32 Height, const int32 PaddingPixels)
    {
        for (int32 Step = 0; Step < FMath::Clamp(PaddingPixels, 0, 32); ++Step)
        {
            const TArray<uint8> PreviousMask = Mask;
            bool bChanged = false;
            for (int32 Y = 0; Y < Height; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const int32 Index = Y * Width + X;
                    if (PreviousMask[Index] != 0)
                    {
                        continue;
                    }

                    for (int32 DY = -1; DY <= 1 && Mask[Index] == 0; ++DY)
                    {
                        for (int32 DX = -1; DX <= 1 && Mask[Index] == 0; ++DX)
                        {
                            if (DX == 0 && DY == 0)
                            {
                                continue;
                            }
                            const int32 NX = X + DX;
                            const int32 NY = Y + DY;
                            if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
                            {
                                continue;
                            }
                            if (PreviousMask[NY * Width + NX] != 0)
                            {
                                Mask[Index] = 1;
                                bChanged = true;
                            }
                        }
                    }
                }
            }
            if (!bChanged)
            {
                break;
            }
        }
    }

    void DilateSurfacePreviewColors(
        TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height,
        const int32 PaddingPixels)
    {
        for (int32 Step = 0; Step < FMath::Clamp(PaddingPixels, 0, 8); ++Step)
        {
            const TArray<FColor> PreviousPixels = Pixels;
            bool bChanged = false;
            for (int32 Y = 0; Y < Height; ++Y)
            {
                for (int32 X = 0; X < Width; ++X)
                {
                    const int32 Index = Y * Width + X;
                    if (PreviousPixels[Index].A != 0)
                    {
                        continue;
                    }

                    for (int32 DY = -1; DY <= 1 && Pixels[Index].A == 0; ++DY)
                    {
                        for (int32 DX = -1; DX <= 1 && Pixels[Index].A == 0; ++DX)
                        {
                            if (DX == 0 && DY == 0)
                            {
                                continue;
                            }
                            const int32 NX = X + DX;
                            const int32 NY = Y + DY;
                            if (NX < 0 || NY < 0 || NX >= Width || NY >= Height)
                            {
                                continue;
                            }
                            const FColor& Neighbor = PreviousPixels[NY * Width + NX];
                            if (Neighbor.A != 0)
                            {
                                Pixels[Index] = Neighbor;
                                bChanged = true;
                            }
                        }
                    }
                }
            }
            if (!bChanged)
            {
                break;
            }
        }
    }

    bool ReadSurfacePreviewSourcePixels(
        UTexture2D* Texture,
        TArray<FColor>& OutPixels,
        int32& OutWidth,
        int32& OutHeight,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        if (Texture == nullptr || !Texture->Source.IsValid())
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture does not contain readable editor source data.");
            return false;
        }
        if (Texture->Source.GetFormat() != TSF_BGRA8)
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture must use BGRA8 source data.");
            return false;
        }

        TArray64<uint8> RawData;
        if (!Texture->Source.GetMipData(RawData, 0))
        {
            OutErrorMessage = TEXT("Could not read the baked Wet Part Data Texture source mip.");
            return false;
        }

        OutWidth = Texture->Source.GetSizeX();
        OutHeight = Texture->Source.GetSizeY();
        const int64 PixelCount = static_cast<int64>(OutWidth) * static_cast<int64>(OutHeight);
        if (OutWidth <= 0 || OutHeight <= 0 || RawData.Num() < PixelCount * static_cast<int64>(sizeof(FColor)))
        {
            OutErrorMessage = TEXT("The baked Wet Part Data Texture has invalid source dimensions.");
            return false;
        }

        OutPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
        FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), PixelCount * static_cast<int64>(sizeof(FColor)));
        return true;
#else
        OutErrorMessage = TEXT("Surface Water Tiling preview requires editor texture source data.");
        return false;
#endif
    }

    template <typename PixelType>
    bool UploadSurfacePreviewPixels(
        UTexture2D* Texture,
        const TArray<PixelType>& Pixels)
    {
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr ||
            Texture->GetPlatformData()->Mips.IsEmpty())
        {
            return false;
        }

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        const int64 ByteCount = static_cast<int64>(Pixels.Num()) * static_cast<int64>(sizeof(PixelType));
        void* Destination = Mip.BulkData.Lock(LOCK_READ_WRITE);
        if (Destination == nullptr || Mip.BulkData.GetBulkDataSize() < ByteCount)
        {
            Mip.BulkData.Unlock();
            return false;
        }

        FMemory::Memcpy(Destination, Pixels.GetData(), ByteCount);
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return true;
    }

    bool CreateOrUpdateSurfacePreviewByteTexture(
        TObjectPtr<UTexture2D>& Texture,
        const TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        if (Texture == nullptr || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height ||
            Texture->GetPixelFormat() != PF_B8G8R8A8)
        {
            Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None);
            if (Texture == nullptr)
            {
                return false;
            }
            Texture->SRGB = false;
            Texture->CompressionSettings = TC_VectorDisplacementmap;
            Texture->MipGenSettings = TMGS_NoMipmaps;
            Texture->Filter = TF_Nearest;
            Texture->AddressX = TA_Clamp;
            Texture->AddressY = TA_Clamp;
            Texture->NeverStream = true;
        }

        return UploadSurfacePreviewPixels(Texture, Pixels);
    }

    bool CreateOrUpdateSurfacePreviewWetnessTexture(
        TObjectPtr<UTexture2D>& Texture,
        const TArray<float>& Pixels,
        const int32 Width,
        const int32 Height,
        const TextureFilter Filter)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return false;
        }

        if (Texture == nullptr || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height ||
            Texture->GetPixelFormat() != PF_R32_FLOAT)
        {
            Texture = UTexture2D::CreateTransient(Width, Height, PF_R32_FLOAT, NAME_None);
            if (Texture == nullptr)
            {
                return false;
            }
            Texture->SRGB = false;
            Texture->CompressionSettings = TC_VectorDisplacementmap;
            Texture->MipGenSettings = TMGS_NoMipmaps;
            Texture->Filter = Filter;
            Texture->AddressX = TA_Clamp;
            Texture->AddressY = TA_Clamp;
            Texture->NeverStream = true;
        }
        else
        {
            Texture->Filter = Filter;
        }

        return UploadSurfacePreviewPixels(Texture, Pixels);
    }

    UTexture2D* ResolveTransientSurfacePreviewTexture(
        UTexture2D* SourceTexture,
        const bool bNormalMap,
        TObjectPtr<UTexture2D>& CachedTexture,
        TWeakObjectPtr<UTexture2D>& CachedSource,
        const TCHAR* DebugName)
    {
        if (SourceTexture == nullptr)
        {
            CachedTexture = nullptr;
            CachedSource = nullptr;
            return nullptr;
        }
        if (CachedTexture != nullptr && CachedSource.Get() == SourceTexture)
        {
            return CachedTexture;
        }

#if WITH_EDITORONLY_DATA
        const int32 SourceWidth = SourceTexture->Source.GetSizeX();
        const int32 SourceHeight = SourceTexture->Source.GetSizeY();
        const ETextureSourceFormat SourceFormat = SourceTexture->Source.GetFormat();
        if (SourceWidth > 0 && SourceHeight > 0 &&
            (SourceFormat == TSF_BGRA8 || SourceFormat == TSF_G8))
        {
            TArray64<uint8> SourceBytes;
            if (SourceTexture->Source.GetMipData(SourceBytes, 0))
            {
                constexpr int32 TargetSize = DWCSurfaceTextureSharedAsset::Resolution;
                TArray<FColor> Resampled;
                Resampled.SetNumUninitialized(TargetSize * TargetSize);
                for (int32 Y = 0; Y < TargetSize; ++Y)
                {
                    const int32 SourceY = FMath::Clamp(
                        FMath::FloorToInt((static_cast<double>(Y) + 0.5) * SourceHeight / TargetSize),
                        0,
                        SourceHeight - 1);
                    for (int32 X = 0; X < TargetSize; ++X)
                    {
                        const int32 SourceX = FMath::Clamp(
                            FMath::FloorToInt((static_cast<double>(X) + 0.5) * SourceWidth / TargetSize),
                            0,
                            SourceWidth - 1);
                        FColor Pixel;
                        if (SourceFormat == TSF_BGRA8)
                        {
                            const int64 ByteOffset =
                                (static_cast<int64>(SourceY) * SourceWidth + SourceX) * sizeof(FColor);
                            FMemory::Memcpy(&Pixel, SourceBytes.GetData() + ByteOffset, sizeof(FColor));
                        }
                        else
                        {
                            const uint8 Value = SourceBytes[static_cast<int64>(SourceY) * SourceWidth + SourceX];
                            Pixel = bNormalMap
                                ? FColor(Value, Value, 255, 255)
                                : FColor(Value, Value, Value, 255);
                        }
                        Resampled[Y * TargetSize + X] = Pixel;
                    }
                }

                CachedTexture = UTexture2D::CreateTransient(
                    TargetSize,
                    TargetSize,
                    PF_B8G8R8A8,
                    DebugName != nullptr ? FName(DebugName) : NAME_None);
                if (CachedTexture != nullptr)
                {
                    CachedTexture->SRGB = false;
                    CachedTexture->CompressionSettings = TC_VectorDisplacementmap;
                    CachedTexture->MipGenSettings = TMGS_NoMipmaps;
                    CachedTexture->Filter = TF_Bilinear;
                    CachedTexture->AddressX = TA_Wrap;
                    CachedTexture->AddressY = TA_Wrap;
                    CachedTexture->NeverStream = true;
                    if (UploadSurfacePreviewPixels(CachedTexture, Resampled))
                    {
                        CachedSource = SourceTexture;
                        return CachedTexture;
                    }
                }
            }
        }
#endif

        // Do not feed an arbitrary authored texture into the array registry. A direct
        // fallback is safe only when it already satisfies the fixed prepared-texture
        // contract and exposes uploadable mip-0 data. Otherwise slice 0 is preferable
        // to silently constructing a malformed Texture2DArray.
        const FTexturePlatformData* PlatformData = SourceTexture->GetPlatformData();
        const bool bDirectFallbackUsable =
            SourceTexture->GetSizeX() == DWCSurfaceTextureSharedAsset::Resolution &&
            SourceTexture->GetSizeY() == DWCSurfaceTextureSharedAsset::Resolution &&
            SourceTexture->GetResource() != nullptr &&
            PlatformData != nullptr &&
            !PlatformData->Mips.IsEmpty() &&
            SourceTexture->GetPixelFormat() != PF_Unknown;
        CachedTexture = bDirectFallbackUsable ? SourceTexture : nullptr;
        CachedSource = SourceTexture;
        return CachedTexture;
    }


} // namespace

void SDWCPartViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    bSurfaceWaterTilingPreview = InArgs._SurfaceWaterTilingPreview;
    OnIslandPicked = InArgs._OnIslandPicked;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ConfigureStaticPartPreviewPose(PreviewMeshComponent);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);



    RefreshPreviewMesh();
}

SDWCPartViewport::~SDWCPartViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SDWCPartViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(WetPartOverlayMaterial);
    Collector.AddReferencedObject(WetPartOverlayMID);
    Collector.AddReferencedObject(PartPreviewColorTexture);
    Collector.AddReferencedObject(PartPreviewSelectionTexture);
    Collector.AddReferencedObject(SurfaceWaterPreviewMaterialParent);
    Collector.AddReferencedObject(SurfaceWaterPreviewBaseMaterial);
    Collector.AddReferencedObject(SurfaceWaterPreviewStaticMaterial);
    Collector.AddReferencedObject(SurfaceWaterPreviewMaterial);
    Collector.AddReferencedObject(SurfacePreviewWetnessMap);
    Collector.AddReferencedObject(SurfacePreviewWetPartDataTexture);
    Collector.AddReferencedObject(SurfacePreviewDropletRT);
    Collector.AddReferencedObject(SurfacePreviewFlowDropletRT);
    Collector.AddReferencedObject(SurfacePreviewAuthoredDropletNormal);
    Collector.AddReferencedObject(SurfacePreviewAuthoredDropletMask);
    Collector.AddReferencedObject(SurfacePreviewAuthoredDroplet2Normal);
    Collector.AddReferencedObject(SurfacePreviewAuthoredDroplet2Mask);
    Collector.AddReferencedObjects(OriginalPreviewMaterials);
}

void SDWCPartViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = nullptr;
    if (UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        TargetMesh = WetClothingAssetPtr->GetRuntimeSkeletalMesh();
        if (TargetMesh == nullptr)
        {
            TargetMesh = WetClothingAssetPtr->GetSourceSkeletalMesh();
        }
    }

    if (PreviewMeshComponent->GetSkeletalMeshAsset() == TargetMesh && TargetMesh != nullptr)
    {
        ConfigureStaticPartPreviewPose(PreviewMeshComponent);
        RefreshPartPreviewOverlayMaterial();
        MarkWetPartOverlayDirty();
        MarkSelectionOverlayDirty();
        if (bSurfaceWaterTilingPreview)
        {
            RefreshSurfaceWaterPreviewMaterial();
        }
        RefreshMaterialSectionVisibility();

        RequestViewportRedraw();
        return;
    }

    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    ConfigureStaticPartPreviewPose(PreviewMeshComponent);
    PreviewMeshComponent->ShowAllMaterialSections(0);
    ClearPartPreviewOverlay();
    CacheOriginalMaterials();
    WetPartOverlayMID = nullptr;
    PartPreviewColorTexture = nullptr;
    PartPreviewSelectionTexture = nullptr;
    WetPartOverlayMaterial = nullptr;
    RefreshPartPreviewOverlayMaterial();
    SurfaceWaterPreviewMaterial = nullptr;
    SurfaceWaterPreviewBaseMaterial = nullptr;
    SurfaceWaterPreviewStaticMaterial = nullptr;
    SurfaceWaterPreviewMaterialParent = nullptr;
    SurfaceWaterPreviewMaterialSlotIndex = INDEX_NONE;
    SurfaceWaterPreviewDataUVChannel = INDEX_NONE;
    SurfaceWaterPreviewNormalUVChannel = INDEX_NONE;
    bSurfaceWaterPreviewFallbackProfileCacheValid = false;
    SurfaceWaterPreviewStatus.Reset();
    InvalidateSurfaceWaterPreviewLayoutCache();
    BeginPreviewUpdate();
    ClearHighlightedIsland();
    ClearWetPartIslandColors();
    EndPreviewUpdate();

    if (TargetMesh != nullptr)
    {
        const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(FTransform::Identity);
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
    else
    {
        PreviewScene->SetFloorOffset(0.0f);
    }

    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    RefreshMaterialSectionVisibility();

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SDWCPartViewport::BeginPreviewUpdate()
{
    ++PreviewUpdateDepth;
}

void SDWCPartViewport::EndPreviewUpdate()
{
    if (PreviewUpdateDepth > 0)
    {
        --PreviewUpdateDepth;
    }
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::MarkWetPartOverlayDirty()
{
    bWetPartOverlayDirty = true;
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::MarkSelectionOverlayDirty()
{
    bSelectionOverlayDirty = true;
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::MarkSurfacePreviewDirty()
{
    bSurfacePreviewDirty = true;
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::FlushPendingPreviewUpdates()
{
    if (PreviewUpdateDepth > 0)
    {
        return;
    }

    if (bPickableTopologyDirty && ViewportClient.IsValid())
    {
        ViewportClient->SetPickableIslands(CurrentSelectableIslands);
    }

    const bool bRefreshWetOverlay = bWetPartOverlayDirty;
    const bool bRefreshSelectionOverlay = bSelectionOverlayDirty;
    const bool bRefreshSurfacePreview = bSurfacePreviewDirty;
    bPickableTopologyDirty = false;
    bWetPartOverlayDirty = false;
    bSelectionOverlayDirty = false;
    bSurfacePreviewDirty = false;

    if (bRefreshWetOverlay || bRefreshSelectionOverlay)
    {
        RefreshPartPreviewOverlay();
    }
    if (bRefreshSurfacePreview && bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetHighlightedMaterialSlot(const int32 SlotIndex)
{
    const int32 MaterialCount = PreviewMeshComponent != nullptr ? PreviewMeshComponent->GetNumMaterials() : 0;
    CurrentHighlightedMaterialSlot = SlotIndex >= 0 && SlotIndex < MaterialCount ? SlotIndex : INDEX_NONE;
    RefreshMaterialSectionVisibility();
    MarkWetPartOverlayDirty();
    MarkSelectionOverlayDirty();

    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    RequestViewportRedraw();
}

void SDWCPartViewport::ClearMaterialSlotHighlight()
{
    CurrentHighlightedMaterialSlot = INDEX_NONE;
    RefreshMaterialSectionVisibility();
    MarkWetPartOverlayDirty();
    MarkSelectionOverlayDirty();
    RequestViewportRedraw();
}

void SDWCPartViewport::SetSelectableIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands)
{
    InvalidateSurfaceWaterPreviewLayoutCache();
    CurrentSelectableIslands.Reset();

    for (const TSharedPtr<FWetClothingAssetUVIsland>& Island : InIslands)
    {
        if (Island.IsValid())
        {
            CurrentSelectableIslands.Add(*Island);
        }
    }

    bPickableTopologyDirty = true;
    bWetPartOverlayDirty = true;
    bSelectionOverlayDirty = true;
    if (bSurfaceWaterTilingPreview)
    {
        bSurfacePreviewDirty = true;
    }
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::SetHighlightedUVIslandIDs(const TSet<int32>& InUVIslandIDs)
{
    CurrentHighlightedUVIslandIDs = InUVIslandIDs;
    bWetPartOverlayDirty = true;
    MarkSelectionOverlayDirty();
}

void SDWCPartViewport::SetSelectionOverlayThicknessScale(float InThicknessScale)
{
    const float NewThicknessScale = FMath::Clamp(InThicknessScale, 0.25f, 4.0f);
    if (!FMath::IsNearlyEqual(SelectionOverlayThicknessScale, NewThicknessScale))
    {
        SelectionOverlayThicknessScale = NewThicknessScale;
        MarkSelectionOverlayDirty();
    }
}

void SDWCPartViewport::ClearHighlightedIsland()
{
    CurrentHighlightedUVIslandIDs.Reset();
    bWetPartOverlayDirty = true;
    MarkSelectionOverlayDirty();
}

void SDWCPartViewport::SetWetPartIslandAssignments(const TMap<int32, int32>& InUVIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors)
{
    InvalidateSurfaceWaterPreviewLayoutCache();
    CurrentWetPartIslandAssignments = InUVIslandToWetPartID;
    CurrentWetPartIslandColors = InIslandColors;
    bWetPartOverlayDirty = true;
    if (bSurfaceWaterTilingPreview)
    {
        bSurfacePreviewDirty = true;
    }
    if (PreviewUpdateDepth == 0)
    {
        FlushPendingPreviewUpdates();
    }
}

void SDWCPartViewport::ClearWetPartIslandColors()
{
    InvalidateSurfaceWaterPreviewLayoutCache();
    CurrentWetPartIslandAssignments.Reset();
    CurrentWetPartIslandColors.Reset();

    MarkWetPartOverlayDirty();
}

void SDWCPartViewport::SetShowWetPartColors(const bool bInShowWetPartColors)
{
    if (bShowWetPartColors == bInShowWetPartColors)
    {
        return;
    }

    bShowWetPartColors = bInShowWetPartColors;
    MarkWetPartOverlayDirty();
}

void SDWCPartViewport::SetWetPartColorIntensity(const float InIntensity)
{
    const float NewIntensity = FMath::Clamp(InIntensity, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(WetPartColorIntensity, NewIntensity))
    {
        return;
    }

    WetPartColorIntensity = NewIntensity;
    MarkWetPartOverlayDirty();
}

void SDWCPartViewport::SetPreviewWetPart(const int32 MaterialSlotIndex, const int32 WetPartID)
{
    if (PreviewMaterialSlotIndex == MaterialSlotIndex && PreviewWetPartID == WetPartID)
    {
        return;
    }

    InvalidateSurfaceWaterPreviewLayoutCache();
    PreviewMaterialSlotIndex = MaterialSlotIndex;
    PreviewWetPartID = WetPartID;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
}

void SDWCPartViewport::SetPreviewWetness(const float AbsorbedWetness, const float SurfaceWater)
{
    const float NewAbsorbedWetness = FMath::Clamp(AbsorbedWetness, 0.0f, 1.0f);
    const float NewSurfaceWater = FMath::Clamp(SurfaceWater, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(PreviewAbsorbedWetness, NewAbsorbedWetness) &&
        FMath::IsNearlyEqual(PreviewSurfaceWater, NewSurfaceWater))
    {
        return;
    }

    PreviewAbsorbedWetness = NewAbsorbedWetness;
    PreviewSurfaceWater = NewSurfaceWater;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SDWCPartViewport::SetSurfaceWaterPreviewDropletsEnabled(const bool bInDropletsEnabled)
{
    if (bSurfaceWaterPreviewDropletsEnabled == bInDropletsEnabled)
    {
        return;
    }

    bSurfaceWaterPreviewDropletsEnabled = bInDropletsEnabled;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    else
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        RequestViewportRedraw();
    }
}

void SDWCPartViewport::SetSurfaceWaterPreviewNormalFlip(
    const bool bInFlipX,
    const bool bInFlipY)
{
    if (bSurfaceWaterPreviewFlipNormalX == bInFlipX &&
        bSurfaceWaterPreviewFlipNormalY == bInFlipY)
    {
        return;
    }

    bSurfaceWaterPreviewFlipNormalX = bInFlipX;
    bSurfaceWaterPreviewFlipNormalY = bInFlipY;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewMaterial();
    }
    else
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        RequestViewportRedraw();
    }
}

void SDWCPartViewport::SetSurfaceWaterTilingPreviewCoverageMode(
    const EDWCSurfaceWaterTilingPreviewCoverageMode InMode)
{
    if (SurfaceWaterPreviewCoverageMode == InMode)
    {
        return;
    }

    SurfaceWaterPreviewCoverageMode = InMode;
    if (bSurfaceWaterTilingPreview)
    {
        RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SDWCPartViewport::SetSurfaceWaterTilingPreviewDisplayMode(
    const EDWCSurfaceWaterTilingPreviewDisplayMode InMode)
{
    if (SurfaceWaterPreviewDisplayMode == InMode)
    {
        return;
    }

    SurfaceWaterPreviewDisplayMode = InMode;
    if (SurfaceWaterPreviewMaterial != nullptr)
    {
        ApplySurfaceWaterPreviewRenderOverrides();
        RequestViewportRedraw();
    }
}


void SDWCPartViewport::RefreshWetPartOverlayMesh()
{
    RefreshPartPreviewOverlay();
}

void SDWCPartViewport::RefreshSelectionOverlayMesh()
{
    RefreshPartPreviewOverlay();
}

void SDWCPartViewport::ClearPartPreviewOverlay()
{
    if (PreviewMeshComponent != nullptr)
    {
        PreviewMeshComponent->SetOverlayMaterial(nullptr);
        PreviewMeshComponent->MarkRenderStateDirty();
    }
}

bool SDWCPartViewport::BuildPartPreviewTextures()
{
    if (bSurfaceWaterTilingPreview || PreviewMeshComponent == nullptr ||
        CurrentHighlightedMaterialSlot == INDEX_NONE || CurrentSelectableIslands.IsEmpty())
    {
        return false;
    }

    const int32 Width = PartPreviewTextureResolution;
    const int32 Height = PartPreviewTextureResolution;
    TArray<FColor> PartPixels;
    PartPixels.Init(FColor(0, 0, 0, 0), Width * Height);
    TArray<uint8> SelectionMask;
    SelectionMask.Init(0, Width * Height);

    TSet<int32> KnownIslandTriangleIDs;
    TMap<int32, FColor> ResolvedColorByTriangleID;
    TSet<int32> ResolvedSelectedTriangleIDs;
    TMap<uint64, FColor> ColorByRenderEdge;
    TMap<uint64, FColor> ColorByPositionEdge;
    TSet<uint64> ConflictingRenderColorEdges;
    TSet<uint64> ConflictingPositionColorEdges;
    TSet<uint64> SelectedRenderEdges;
    TSet<uint64> SelectedPositionEdges;

    struct FPartPreviewOwnerSource
    {
        const FWetClothingAssetUVTriangle* Triangle = nullptr;
        bool bHasAssignedColor = false;
        FColor AssignedColor = FColor::Transparent;
        bool bSelected = false;
    };
    TArray<FPartPreviewOwnerSource> OwnerSources;

    const auto RegisterColorEdge = [](
        TMap<uint64, FColor>& ColorByEdge,
        TSet<uint64>& ConflictingEdges,
        const uint64 EdgeKey,
        const FColor& Color)
    {
        if (ConflictingEdges.Contains(EdgeKey))
        {
            return;
        }
        if (const FColor* ExistingColor = ColorByEdge.Find(EdgeKey))
        {
            if (*ExistingColor != Color)
            {
                ColorByEdge.Remove(EdgeKey);
                ConflictingEdges.Add(EdgeKey);
            }
            return;
        }
        ColorByEdge.Add(EdgeKey, Color);
    };

    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const bool bSelected = CurrentHighlightedUVIslandIDs.Contains(Island.UVIslandID);
        const int32* WetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        const FLinearColor* IslandColor = CurrentWetPartIslandColors.Find(Island.UVIslandID);
        const bool bHasAssignedColor =
            bShowWetPartColors &&
            WetPartColorIntensity > KINDA_SMALL_NUMBER &&
            WetPartID != nullptr &&
            *WetPartID > 0 &&
            IslandColor != nullptr;

        FColor EncodedColor = FColor::Transparent;
        if (bHasAssignedColor)
        {
            FLinearColor LinearColor = IslandColor->GetClamped(0.0f, 1.0f);
            LinearColor.A = 1.0f;
            EncodedColor = LinearColor.ToFColor(false);
        }

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            KnownIslandTriangleIDs.Add(Triangle.TriangleID);
            OwnerSources.Add(FPartPreviewOwnerSource{
                &Triangle,
                bHasAssignedColor,
                EncodedColor,
                bSelected});
            if (bSelected)
            {
                ResolvedSelectedTriangleIDs.Add(Triangle.TriangleID);
            }
            if (bHasAssignedColor)
            {
                ResolvedColorByTriangleID.Add(Triangle.TriangleID, EncodedColor);
            }
        }
    }

    TArray<FWetClothingAssetUVTriangle> RenderTriangles;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 OriginalUVChannelIndex = Asset != nullptr
        ? FMath::Clamp(Asset->GetOriginalUVChannelIndex(), 0, 7)
        : 0;
    ReadPartPreviewRenderTrianglesIncludingDegenerateUV(
        PreviewMeshComponent->GetSkeletalMeshAsset(),
        OriginalUVChannelIndex,
        CurrentHighlightedMaterialSlot,
        RenderTriangles);

    // Fallback to the island-owned triangles if render data is temporarily unavailable.
    if (RenderTriangles.IsEmpty())
    {
        for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
        {
            RenderTriangles.Append(Island.UVTriangles);
        }
    }

    // Seed adjacency from the actual preview render mesh. The prepared mesh may
    // have different render-vertex indices than the source island cache even though
    // triangle IDs are preserved.
    for (const FWetClothingAssetUVTriangle& Triangle : RenderTriangles)
    {
        uint64 RenderEdgeKeys[3];
        uint64 PositionEdgeKeys[3];
        GetPartPreviewTriangleEdgeKeys(Triangle, RenderEdgeKeys);
        GetPartPreviewTrianglePositionEdgeKeys(Triangle, PositionEdgeKeys);
        if (const FColor* DirectColor = ResolvedColorByTriangleID.Find(Triangle.TriangleID))
        {
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                RegisterColorEdge(
                    ColorByRenderEdge,
                    ConflictingRenderColorEdges,
                    RenderEdgeKeys[EdgeIndex],
                    *DirectColor);
                RegisterColorEdge(
                    ColorByPositionEdge,
                    ConflictingPositionColorEdges,
                    PositionEdgeKeys[EdgeIndex],
                    *DirectColor);
            }
        }
        if (ResolvedSelectedTriangleIDs.Contains(Triangle.TriangleID))
        {
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                SelectedRenderEdges.Add(RenderEdgeKeys[EdgeIndex]);
                SelectedPositionEdges.Add(PositionEdgeKeys[EdgeIndex]);
            }
        }
    }

    // UV-degenerate triangles are intentionally absent from the authoring island list.
    // Propagate their visual owner through shared render edges, without changing the
    // persisted island topology or making those triangles selectable authoring islands.
    for (int32 PassIndex = 0; PassIndex < RenderTriangles.Num(); ++PassIndex)
    {
        bool bChanged = false;
        for (const FWetClothingAssetUVTriangle& Triangle : RenderTriangles)
        {
            if (KnownIslandTriangleIDs.Contains(Triangle.TriangleID))
            {
                continue;
            }

            uint64 RenderEdgeKeys[3];
            uint64 PositionEdgeKeys[3];
            GetPartPreviewTriangleEdgeKeys(Triangle, RenderEdgeKeys);
            GetPartPreviewTrianglePositionEdgeKeys(Triangle, PositionEdgeKeys);

            if (!ResolvedColorByTriangleID.Contains(Triangle.TriangleID))
            {
                const FColor* InferredColor = nullptr;
                bool bConflictingInference = false;
                for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
                {
                    const FColor* EdgeColor = ColorByRenderEdge.Find(RenderEdgeKeys[EdgeIndex]);
                    if (EdgeColor == nullptr)
                    {
                        EdgeColor = ColorByPositionEdge.Find(PositionEdgeKeys[EdgeIndex]);
                    }
                    if (EdgeColor == nullptr)
                    {
                        continue;
                    }
                    if (InferredColor != nullptr && *InferredColor != *EdgeColor)
                    {
                        bConflictingInference = true;
                        break;
                    }
                    InferredColor = EdgeColor;
                }

                if (!bConflictingInference && InferredColor != nullptr)
                {
                    ResolvedColorByTriangleID.Add(Triangle.TriangleID, *InferredColor);
                    for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
                    {
                        RegisterColorEdge(
                            ColorByRenderEdge,
                            ConflictingRenderColorEdges,
                            RenderEdgeKeys[EdgeIndex],
                            *InferredColor);
                        RegisterColorEdge(
                            ColorByPositionEdge,
                            ConflictingPositionColorEdges,
                            PositionEdgeKeys[EdgeIndex],
                            *InferredColor);
                    }
                    bChanged = true;
                }
            }

            const bool bTouchesSelectedTriangle =
                SelectedRenderEdges.Contains(RenderEdgeKeys[0]) ||
                SelectedRenderEdges.Contains(RenderEdgeKeys[1]) ||
                SelectedRenderEdges.Contains(RenderEdgeKeys[2]) ||
                SelectedPositionEdges.Contains(PositionEdgeKeys[0]) ||
                SelectedPositionEdges.Contains(PositionEdgeKeys[1]) ||
                SelectedPositionEdges.Contains(PositionEdgeKeys[2]);
            if (!ResolvedSelectedTriangleIDs.Contains(Triangle.TriangleID) &&
                bTouchesSelectedTriangle)
            {
                ResolvedSelectedTriangleIDs.Add(Triangle.TriangleID);
                for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
                {
                    SelectedRenderEdges.Add(RenderEdgeKeys[EdgeIndex]);
                    SelectedPositionEdges.Add(PositionEdgeKeys[EdgeIndex]);
                }
                bChanged = true;
            }
        }

        if (!bChanged)
        {
            break;
        }
    }

    // Some authored seams, piping, and very thin decorative strips have zero-area
    // or near-zero-area UVs and are therefore intentionally absent from the editable
    // island topology. They are not always edge-connected to the garment panel, so the
    // shared-edge pass above cannot assign them a preview owner. For preview rendering
    // only, inherit the nearest editable island on the same material slot in local space.
    // Valid unassigned islands remain untouched because this only runs for triangles that
    // are absent from the editable island topology.
    if (!OwnerSources.IsEmpty())
    {
        FBox RenderBounds(ForceInit);
        for (const FWetClothingAssetUVTriangle& Triangle : RenderTriangles)
        {
            RenderBounds += Triangle.LocalPositions[0];
            RenderBounds += Triangle.LocalPositions[1];
            RenderBounds += Triangle.LocalPositions[2];
        }

        const double BoundsDiagonal = RenderBounds.IsValid
            ? RenderBounds.GetSize().Size()
            : 0.0;
        const double MaxOwnerDistance = FMath::Max(0.5, BoundsDiagonal * 0.01);
        const double MaxOwnerDistanceSquared = FMath::Square(MaxOwnerDistance);
        int32 NearestInheritedColorCount = 0;
        int32 NearestInheritedSelectionCount = 0;
        int32 RemainingOrphanCount = 0;

        for (const FWetClothingAssetUVTriangle& Triangle : RenderTriangles)
        {
            if (KnownIslandTriangleIDs.Contains(Triangle.TriangleID))
            {
                continue;
            }

            const bool bNeedsColor = !ResolvedColorByTriangleID.Contains(Triangle.TriangleID);
            const bool bNeedsSelection = !ResolvedSelectedTriangleIDs.Contains(Triangle.TriangleID);
            if (!bNeedsColor && !bNeedsSelection)
            {
                continue;
            }

            const FVector TargetCenter =
                (Triangle.LocalPositions[0] + Triangle.LocalPositions[1] + Triangle.LocalPositions[2]) / 3.0;
            const FVector TargetNormal = FVector::CrossProduct(
                Triangle.LocalPositions[1] - Triangle.LocalPositions[0],
                Triangle.LocalPositions[2] - Triangle.LocalPositions[0]).GetSafeNormal();

            const FPartPreviewOwnerSource* BestOwner = nullptr;
            double BestDistanceSquared = TNumericLimits<double>::Max();
            for (const FPartPreviewOwnerSource& Candidate : OwnerSources)
            {
                if (Candidate.Triangle == nullptr)
                {
                    continue;
                }

                const FWetClothingAssetUVTriangle& SourceTriangle = *Candidate.Triangle;
                const FVector SourceNormal = FVector::CrossProduct(
                    SourceTriangle.LocalPositions[1] - SourceTriangle.LocalPositions[0],
                    SourceTriangle.LocalPositions[2] - SourceTriangle.LocalPositions[0]).GetSafeNormal();
                if (!TargetNormal.IsNearlyZero() && !SourceNormal.IsNearlyZero() &&
                    FMath::Abs(FVector::DotProduct(TargetNormal, SourceNormal)) < 0.15)
                {
                    continue;
                }

                const FVector ClosestPoint = FMath::ClosestPointOnTriangleToPoint(
                    TargetCenter,
                    SourceTriangle.LocalPositions[0],
                    SourceTriangle.LocalPositions[1],
                    SourceTriangle.LocalPositions[2]);
                const double DistanceSquared = FVector::DistSquared(TargetCenter, ClosestPoint);
                if (DistanceSquared < BestDistanceSquared)
                {
                    BestDistanceSquared = DistanceSquared;
                    BestOwner = &Candidate;
                }
            }

            if (BestOwner == nullptr || BestDistanceSquared > MaxOwnerDistanceSquared)
            {
                ++RemainingOrphanCount;
                continue;
            }

            if (bNeedsColor && BestOwner->bHasAssignedColor)
            {
                ResolvedColorByTriangleID.Add(Triangle.TriangleID, BestOwner->AssignedColor);
                ++NearestInheritedColorCount;
            }
            if (bNeedsSelection && BestOwner->bSelected)
            {
                ResolvedSelectedTriangleIDs.Add(Triangle.TriangleID);
                ++NearestInheritedSelectionCount;
            }
        }

        if (NearestInheritedColorCount > 0 || NearestInheritedSelectionCount > 0 || RemainingOrphanCount > 0)
        {
            UE_LOG(
                LogTemp,
                Verbose,
                TEXT("DWC Part Preview orphan triangle ownership: color=%d selection=%d unresolved=%d maxDistance=%.3f."),
                NearestInheritedColorCount,
                NearestInheritedSelectionCount,
                RemainingOrphanCount,
                MaxOwnerDistance);
        }
    }

    for (const FWetClothingAssetUVTriangle& Triangle : RenderTriangles)
    {
        if (const FColor* Color = ResolvedColorByTriangleID.Find(Triangle.TriangleID))
        {
            RasterizePreviewTriangleColor(PartPixels, Width, Height, Triangle, *Color);
        }
        if (ResolvedSelectedTriangleIDs.Contains(Triangle.TriangleID))
        {
            RasterizeSurfacePreviewTriangleMask(SelectionMask, Width, Height, Triangle);
        }
    }

    const bool bHasPartColor = !ResolvedColorByTriangleID.IsEmpty();
    const bool bHasSelection = !ResolvedSelectedTriangleIDs.IsEmpty();

    // Original-UV preview textures are discontinuous data. Conservative rasterization
    // guarantees thin triangles at least one touched texel; nearest-value dilation keeps
    // point sampling stable at island edges without averaging adjacent Part IDs/colors.
    if (bHasPartColor)
    {
        DilateSurfacePreviewColors(PartPixels, Width, Height, 2);
    }
    if (bHasSelection)
    {
        DilateSurfacePreviewMask(SelectionMask, Width, Height, 1);
    }

    TArray<FColor> SelectionPixels;
    SelectionPixels.Init(FColor(0, 0, 0, 0), Width * Height);
    if (bHasSelection)
    {
        const int32 BoundaryRadius = FMath::Clamp(
            FMath::RoundToInt(1.5f * SelectionOverlayThicknessScale),
            1,
            6);
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 PixelIndex = Y * Width + X;
                if (SelectionMask[PixelIndex] == 0)
                {
                    continue;
                }

                bool bBoundary = false;
                for (int32 OffsetY = -BoundaryRadius; OffsetY <= BoundaryRadius && !bBoundary; ++OffsetY)
                {
                    for (int32 OffsetX = -BoundaryRadius; OffsetX <= BoundaryRadius; ++OffsetX)
                    {
                        const int32 NeighborX = X + OffsetX;
                        const int32 NeighborY = Y + OffsetY;
                        if (NeighborX < 0 || NeighborY < 0 || NeighborX >= Width || NeighborY >= Height ||
                            SelectionMask[NeighborY * Width + NeighborX] == 0)
                        {
                            bBoundary = true;
                            break;
                        }
                    }
                }

                // R = selection fill, G = selection boundary.
                SelectionPixels[PixelIndex] = FColor(255, bBoundary ? 255 : 0, 0, 255);
            }
        }
    }

    if (!bHasPartColor && !bHasSelection)
    {
        return false;
    }

    const bool bPartTextureReady = CreateOrUpdateSurfacePreviewByteTexture(
        PartPreviewColorTexture,
        PartPixels,
        Width,
        Height);
    const bool bSelectionTextureReady = CreateOrUpdateSurfacePreviewByteTexture(
        PartPreviewSelectionTexture,
        SelectionPixels,
        Width,
        Height);
    return bPartTextureReady && bSelectionTextureReady;
}

void SDWCPartViewport::RefreshPartPreviewOverlayMaterial()
{
    if (bSurfaceWaterTilingPreview || PreviewMeshComponent == nullptr)
    {
        ClearPartPreviewOverlay();
        return;
    }

    UMaterialInterface* OverlayMaterial = ResolveWetPartOverlayMaterial();
    if (OverlayMaterial == nullptr)
    {
        ClearPartPreviewOverlay();
        return;
    }

    if (WetPartOverlayMID == nullptr)
    {
        WetPartOverlayMID = UMaterialInstanceDynamic::Create(
            OverlayMaterial,
            GetTransientPackage(),
            TEXT("MID_DWC_PartPreviewOriginalUV"));
    }
    if (WetPartOverlayMID == nullptr)
    {
        ClearPartPreviewOverlay();
        return;
    }

    WetPartOverlayMID->SetTextureParameterValue(PartPreviewColorTextureParameter, PartPreviewColorTexture);
    WetPartOverlayMID->SetTextureParameterValue(PartPreviewSelectionTextureParameter, PartPreviewSelectionTexture);
    WetPartOverlayMID->SetScalarParameterValue(
        PartPreviewColorOpacityParameter,
        bShowWetPartColors ? FMath::Clamp(WetPartColorIntensity * 0.72f, 0.0f, 0.9f) : 0.0f);
    WetPartOverlayMID->SetScalarParameterValue(PartPreviewSelectionFillOpacityParameter, 0.72f);
    WetPartOverlayMID->SetScalarParameterValue(PartPreviewSelectionBoundaryOpacityParameter, 1.0f);
    WetPartOverlayMID->SetVectorParameterValue(
        PartPreviewSelectionFillColorParameter,
        FLinearColor(1.0f, 0.24f, 0.01f, 1.0f));
    WetPartOverlayMID->SetVectorParameterValue(
        PartPreviewSelectionBoundaryColorParameter,
        FLinearColor(1.0f, 0.72f, 0.02f, 1.0f));

    PreviewMeshComponent->SetOverlayMaterial(WetPartOverlayMID);
    PreviewMeshComponent->MarkRenderStateDirty();
}

void SDWCPartViewport::RefreshPartPreviewOverlay()
{
    ClearPartPreviewOverlay();
    if (!BuildPartPreviewTextures())
    {
        RequestViewportRedraw();
        return;
    }

    RefreshPartPreviewOverlayMaterial();
    RequestViewportRedraw();
}

void SDWCPartViewport::RefreshMaterialSectionVisibility()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    PreviewMeshComponent->ShowAllMaterialSections(0);
    const int32 IsolatedMaterialSlot = bSurfaceWaterTilingPreview
        ? PreviewMaterialSlotIndex
        : CurrentHighlightedMaterialSlot;
    const bool bIsolateSelectedSlot =
        IsolatedMaterialSlot != INDEX_NONE;
    if (!bIsolateSelectedSlot)
    {
        PreviewMeshComponent->MarkRenderStateDirty();
        return;
    }

    const USkeletalMesh* PreviewMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* RenderData = PreviewMesh != nullptr ? PreviewMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(0))
    {
        PreviewMeshComponent->MarkRenderStateDirty();
        return;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
    for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
        const bool bVisible = Section.MaterialIndex == IsolatedMaterialSlot;
        PreviewMeshComponent->ShowMaterialSection(
            Section.MaterialIndex,
            SectionIndex,
            bVisible,
            0);
    }
    PreviewMeshComponent->MarkRenderStateDirty();
}

bool SDWCPartViewport::BuildSurfaceWaterPreviewTextures(FString& OutErrorMessage)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID < 0)
    {
        OutErrorMessage = TEXT("Select a wettable Material Slot and Wet Part.");
        return false;
    }

    const FWetClothingEditableWetPartData& Editable = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = Editable.FindMaterialSlot(PreviewMaterialSlotIndex);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(PreviewWetPartID) : nullptr;
    if (Part == nullptr)
    {
        OutErrorMessage = TEXT("The selected Wet Part could not be resolved.");
        return false;
    }
    if (!Slot->bIsWettableSlot)
    {
        OutErrorMessage = TEXT("Mark the selected Material Slot as Wettable before using the Surface Water Tiling preview.");
        return false;
    }

    TSet<int32> SelectedTriangleIDs;
    for (const FWetClothingAssetUVIsland& Island : CurrentSelectableIslands)
    {
        const int32* AssignedWetPartID = CurrentWetPartIslandAssignments.Find(Island.UVIslandID);
        const int32 EffectiveWetPartID = AssignedWetPartID != nullptr ? *AssignedWetPartID : 0;
        if (EffectiveWetPartID != PreviewWetPartID)
        {
            continue;
        }
        for (const int32 TriangleID : Island.TriangleIDs)
        {
            SelectedTriangleIDs.Add(TriangleID);
        }
    }
    if (SelectedTriangleIDs.IsEmpty())
    {
        OutErrorMessage = TEXT("The selected Wet Part does not contain any UV-island triangles.");
        return false;
    }

    const bool bUseCachedLayout =
        bSurfacePreviewLayoutCacheValid &&
        SurfacePreviewCachedMaterialSlotIndex == PreviewMaterialSlotIndex &&
        SurfacePreviewCachedWetPartID == PreviewWetPartID &&
        SurfacePreviewCachedWidth > 0 &&
        SurfacePreviewCachedHeight > 0 &&
        SurfacePreviewCachedSourcePartDataPixels.Num() ==
            SurfacePreviewCachedWidth * SurfacePreviewCachedHeight &&
        SurfacePreviewCachedSelectedMask.Num() ==
            SurfacePreviewCachedWidth * SurfacePreviewCachedHeight;

    if (!bUseCachedLayout)
    {
        const int32 Width = DWCWetPartDataTextureBake::Resolution;
        const int32 Height = DWCWetPartDataTextureBake::Resolution;
        TArray<FColor> SourcePartDataPixels;
        SourcePartDataPixels.Init(
            FColor(
                DWCWetPartDataTextureBake::NeutralProfileID,
                EncodeSurfacePreviewDetailSize(1.0f),
                EncodeSurfacePreviewDetailSize(1.0f),
                0),
            Width * Height);

        TArray<FWetClothingAssetUVIsland> DataUVIslands;
        FString DataUVError;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
                *Asset,
                0,
                PreviewMaterialSlotIndex,
                DataUVIslands,
                &DataUVError))
        {
            OutErrorMessage = DataUVError.IsEmpty()
                ? TEXT("Could not rebuild the selected slot's DWC UV Channel triangles.")
                : DataUVError;
            return false;
        }

        TArray<uint8> SelectedMask;
        SelectedMask.Init(0, Width * Height);
        for (const FWetClothingAssetUVIsland& Island : DataUVIslands)
        {
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                if (SelectedTriangleIDs.Contains(Triangle.TriangleID))
                {
                    RasterizeSurfacePreviewTriangleMask(SelectedMask, Width, Height, Triangle);
                }
            }
        }

        const FWetPartProfileAssignment* PartProfile = Editable.FindProfile(*Part);
        FWetnessProfileParameters PartProfileParameters;
        if (!FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(PartProfile, PartProfileParameters))
        {
            OutErrorMessage = TEXT("Could not resolve the selected Wet Part's authored profile parameters.");
            return false;
        }

        constexpr uint8 LocalProfileID = 1;
        int32 MinSelectedX = Width;
        int32 MinSelectedY = Height;
        int32 MaxSelectedX = 0;
        int32 MaxSelectedY = 0;
        bool bHasSelectedPixel = false;
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                const int32 PixelIndex = Y * Width + X;
                if (SelectedMask[PixelIndex] == 0)
                {
                    continue;
                }
                bHasSelectedPixel = true;
                MinSelectedX = FMath::Min(MinSelectedX, X);
                MinSelectedY = FMath::Min(MinSelectedY, Y);
                MaxSelectedX = FMath::Max(MaxSelectedX, X);
                MaxSelectedY = FMath::Max(MaxSelectedY, Y);
            }
        }
        if (!bHasSelectedPixel)
        {
            OutErrorMessage = TEXT("The selected Wet Part did not produce any DWC UV preview texels.");
            return false;
        }

        SurfacePreviewCachedSingleCircleCenter = FVector2D(
            (static_cast<float>(MinSelectedX) + static_cast<float>(MaxSelectedX)) * 0.5f,
            (static_cast<float>(MinSelectedY) + static_cast<float>(MaxSelectedY)) * 0.5f);

        // Preview-only padding. This does not read or mutate the runtime-baked Wet Part texture.
        DilateSurfacePreviewMask(
            SelectedMask,
            Width,
            Height,
            DWCWetPartDataTextureBake::PaddingPixels);

        SurfacePreviewCachedSourcePartDataPixels = MoveTemp(SourcePartDataPixels);
        SurfacePreviewCachedSelectedMask = MoveTemp(SelectedMask);
        SurfacePreviewCachedWidth = Width;
        SurfacePreviewCachedHeight = Height;
        SurfacePreviewCachedLocalProfileID = static_cast<int32>(LocalProfileID);
        SurfacePreviewCachedMaterialSlotIndex = PreviewMaterialSlotIndex;
        SurfacePreviewCachedWetPartID = PreviewWetPartID;
        bSurfacePreviewLayoutCacheValid = true;
    }

    const int32 Width = SurfacePreviewCachedWidth;
    const int32 Height = SurfacePreviewCachedHeight;
    const int32 LocalProfileID = SurfacePreviewCachedLocalProfileID;
    SurfacePreviewLocalProfileID = LocalProfileID;

    const FWetPartProfileAssignment* PreviewPartProfile = Editable.FindProfile(*Part);
    FWetnessProfileParameters PreviewProfileParameters;
    if (!FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(
            PreviewPartProfile,
            PreviewProfileParameters))
    {
        OutErrorMessage = TEXT("Could not resolve the selected Wet Part's profile parameters.");
        return false;
    }
    const FSurfaceWaterProfileParameters& Surface = PreviewProfileParameters.SurfaceWater;
    const FVector2D SingleCircleCenter = SurfacePreviewCachedSingleCircleCenter;
    const float Droplet1StampSizeScale = Part->SurfaceWater.GetResolvedDropletStampSizeScale();
    const float Droplet1HalfWidthPixels = Surface.DropletRadiusPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletRadiusPixels * Droplet1StampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float Droplet1HalfHeightPixels = Surface.DropletHeightPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletHeightPixels * Droplet1StampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float FlowStampSizeScale = Part->SurfaceWater.GetResolvedDropletFlowStampSizeScale();
    const float FlowHalfWidthPixels = Surface.DropletFlowRadiusPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletFlowRadiusPixels * FlowStampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    const float FlowHalfHeightPixels = Surface.DropletFlowHeightPixels > UE_KINDA_SMALL_NUMBER
        ? FMath::Clamp(Surface.DropletFlowHeightPixels * FlowStampSizeScale, 1.0f, 256.0f)
        : 0.0f;
    TArray<FColor> PreviewPartDataPixels = SurfacePreviewCachedSourcePartDataPixels;
    TArray<float> WetnessPixels;
    TArray<float> DropletPixels;
    TArray<float> FlowDropletPixels;
    WetnessPixels.Init(0.0f, Width * Height);
    DropletPixels.Init(0.0f, Width * Height);
    FlowDropletPixels.Init(0.0f, Width * Height);

    const float SurfaceAmount = FMath::Clamp(PreviewSurfaceWater, 0.0f, 1.0f);
    const uint8 DropletDetailSize = EncodeSurfacePreviewDetailSize(Part->SurfaceWater.DropletDetailSize);
    const uint8 DropletFlowDetailSize =
        EncodeSurfacePreviewDetailSize(Part->SurfaceWater.DropletFlowDetailSize);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            if (SurfacePreviewCachedSelectedMask[PixelIndex] == 0)
            {
                continue;
            }

            PreviewPartDataPixels[PixelIndex].R = LocalProfileID;
            PreviewPartDataPixels[PixelIndex].G = DropletDetailSize;
            PreviewPartDataPixels[PixelIndex].B = DropletFlowDetailSize;
            PreviewPartDataPixels[PixelIndex].A = 0;
            WetnessPixels[PixelIndex] = 0.0f;

            if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
            {
                const FVector2D PixelPosition(static_cast<float>(X), static_cast<float>(Y));
                const FVector2D CenterDelta = PixelPosition - SingleCircleCenter;
                if (Droplet1HalfWidthPixels > UE_KINDA_SMALL_NUMBER &&
                    Droplet1HalfHeightPixels > UE_KINDA_SMALL_NUMBER)
                {
                    const float EllipseDistance = FMath::Sqrt(
                        FMath::Square(CenterDelta.X / Droplet1HalfWidthPixels) +
                        FMath::Square(CenterDelta.Y / Droplet1HalfHeightPixels));
                    const float EdgeWidth = FMath::Clamp(
                        1.5f / FMath::Max(Droplet1HalfWidthPixels, Droplet1HalfHeightPixels),
                        0.02f,
                        0.25f);
                    const float RegionAlpha = 1.0f - FMath::SmoothStep(
                        1.0f - EdgeWidth,
                        1.0f,
                        EllipseDistance);
                    if (RegionAlpha > UE_KINDA_SMALL_NUMBER)
                    {
                        DropletPixels[PixelIndex] = SurfaceAmount * RegionAlpha;
                    }
                }

                if (FlowHalfWidthPixels > UE_KINDA_SMALL_NUMBER &&
                    FlowHalfHeightPixels > UE_KINDA_SMALL_NUMBER)
                {
                    const float EllipseDistance = FMath::Sqrt(
                        FMath::Square(CenterDelta.X / FlowHalfWidthPixels) +
                        FMath::Square(CenterDelta.Y / FlowHalfHeightPixels));
                    const float EdgeWidth = FMath::Clamp(
                        1.5f / FMath::Max(FlowHalfWidthPixels, FlowHalfHeightPixels),
                        0.02f,
                        0.25f);
                    const float RegionAlpha = 1.0f - FMath::SmoothStep(
                        1.0f - EdgeWidth,
                        1.0f,
                        EllipseDistance);
                    if (RegionAlpha > UE_KINDA_SMALL_NUMBER)
                    {
                        FlowDropletPixels[PixelIndex] = SurfaceAmount * RegionAlpha;
                    }
                }
            }
            else
            {
                DropletPixels[PixelIndex] = SurfaceAmount;
                FlowDropletPixels[PixelIndex] = SurfaceAmount;
            }
        }
    }

    if (!CreateOrUpdateSurfacePreviewByteTexture(SurfacePreviewWetPartDataTexture, PreviewPartDataPixels, Width, Height) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewWetnessMap, WetnessPixels, Width, Height, TF_Nearest) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewDropletRT, DropletPixels, Width, Height, TF_Bilinear) ||
        !CreateOrUpdateSurfacePreviewWetnessTexture(SurfacePreviewFlowDropletRT, FlowDropletPixels, Width, Height, TF_Bilinear))
    {
        OutErrorMessage = TEXT("Could not create the transient Surface Water preview textures.");
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

void SDWCPartViewport::InvalidateSurfaceWaterPreviewLayoutCache()
{
    bSurfacePreviewLayoutCacheValid = false;
    SurfacePreviewCachedSourcePartDataPixels.Reset();
    SurfacePreviewCachedSelectedMask.Reset();
    SurfacePreviewCachedSingleCircleCenter = FVector2D::ZeroVector;
    SurfacePreviewCachedWidth = 0;
    SurfacePreviewCachedHeight = 0;
    SurfacePreviewCachedLocalProfileID = 0;
    SurfacePreviewCachedMaterialSlotIndex = INDEX_NONE;
    SurfacePreviewCachedWetPartID = INDEX_NONE;
}

void SDWCPartViewport::ApplySurfaceWaterPreviewTextureParameters()
{
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        return;
    }

    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetnessMap(),
        SurfacePreviewWetnessMap);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::WetPartDataTexture(),
        SurfacePreviewWetPartDataTexture);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet1RT(),
        SurfacePreviewDropletRT);
    SurfaceWaterPreviewMaterial->SetTextureParameterValue(
        DWCWetMaterialParameters::SurfaceDroplet2RT(),
        SurfacePreviewFlowDropletRT);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterTexelSize(),
        SurfacePreviewWetnessMap != nullptr && SurfacePreviewWetnessMap->GetSizeX() > 0
            ? 1.0f / static_cast<float>(SurfacePreviewWetnessMap->GetSizeX())
            : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter, 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, 0.0f);
}

void SDWCPartViewport::RefreshSurfaceWaterPreviewDynamicTextures()
{
    if (!bSurfaceWaterTilingPreview || SurfaceWaterPreviewMaterial == nullptr)
    {
        RefreshSurfaceWaterPreviewMaterial();
        return;
    }

    FString TextureError;
    if (!BuildSurfaceWaterPreviewTextures(TextureError))
    {
        SurfaceWaterPreviewStatus = TextureError;
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    ApplySurfaceWaterPreviewTextureParameters();
    PreviewMeshComponent->MarkRenderStateDirty();
    RequestViewportRedraw();
}

void SDWCPartViewport::RefreshSurfaceWaterPreviewMaterial()
{
    if (!bSurfaceWaterTilingPreview || PreviewMeshComponent == nullptr)
    {
        return;
    }

    RestoreOriginalMaterials();
    SurfaceWaterPreviewStatus.Reset();
    bSurfaceWaterPreviewStatusIsError = false;
    bSurfaceWaterPreviewFallbackProfileCacheValid = false;
    FString SurfaceWaterResourceDiagnostics;
    bool bSurfaceWaterResourceDiagnosticError = false;

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMaterialSlotIndex == INDEX_NONE || PreviewWetPartID < 0)
    {
        SurfaceWaterPreviewStatus = TEXT("Select a Wet Part to preview Surface Water.");
        RequestViewportRedraw();
        return;
    }

    FString TextureError;
    if (!BuildSurfaceWaterPreviewTextures(TextureError))
    {
        SurfaceWaterPreviewStatus = TextureError;
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    const FWetClothingEditableWetPartData& Editable = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* Slot = Editable.FindMaterialSlot(PreviewMaterialSlotIndex);
    const FWetClothingWetPartEntry* Part = Slot != nullptr ? Slot->FindPart(PreviewWetPartID) : nullptr;
    if (Slot == nullptr || Part == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("The selected Wet Part could not be resolved.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    USkeletalMesh* SourceMesh = Asset->GetSourceSkeletalMesh();
    USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
    UMaterialInterface* SourceMaterial =
        SourceMesh != nullptr && SourceMesh->GetMaterials().IsValidIndex(PreviewMaterialSlotIndex)
            ? SourceMesh->GetMaterials()[PreviewMaterialSlotIndex].MaterialInterface
            : (RuntimeMesh != nullptr && RuntimeMesh->GetMaterials().IsValidIndex(PreviewMaterialSlotIndex)
                ? RuntimeMesh->GetMaterials()[PreviewMaterialSlotIndex].MaterialInterface
                : nullptr);
    if (SourceMaterial == nullptr)
    {
        SourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    }

    FWCAMaterialGenerator::FOptions PreviewOptions =
        FWCAMaterialGenerator::MakeOptionsForAsset(
            Asset,
            EDWCSimulationMode::WetnessMapGPU,
            PreviewMaterialSlotIndex);
    PreviewOptions.bUseSurfaceWater = true;
    PreviewOptions.bEnableDWCDataUVSampling = true;
    PreviewOptions.bConnectWetnessMapPath = true;

    if (SurfaceWaterPreviewMaterial == nullptr ||
        SurfaceWaterPreviewMaterialParent != SourceMaterial ||
        SurfaceWaterPreviewMaterialSlotIndex != PreviewMaterialSlotIndex ||
        SurfaceWaterPreviewDataUVChannel != PreviewOptions.DWCDataUVChannelIndex ||
        SurfaceWaterPreviewNormalUVChannel != PreviewOptions.SurfaceWaterNormalUVChannelIndex)
    {
        SurfaceWaterPreviewMaterialParent = SourceMaterial;
        SurfaceWaterPreviewMaterialSlotIndex = PreviewMaterialSlotIndex;
        SurfaceWaterPreviewDataUVChannel = PreviewOptions.DWCDataUVChannelIndex;
        SurfaceWaterPreviewNormalUVChannel = PreviewOptions.SurfaceWaterNormalUVChannelIndex;
        SurfaceWaterPreviewMaterial = nullptr;
        SurfaceWaterPreviewBaseMaterial = nullptr;
        SurfaceWaterPreviewStaticMaterial = nullptr;
        bSurfaceWaterPreviewFallbackProfileCacheValid = false;

        const FWetClothingUnifiedMaterialSetupResult PreviewMaterialSet =
            FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(
                SourceMaterial,
                PreviewOptions);
        if (!PreviewMaterialSet.bSucceeded ||
            PreviewMaterialSet.GeneratedMaterial == nullptr ||
            PreviewMaterialSet.GeneratedMaterialInstance == nullptr)
        {
            SurfaceWaterPreviewStatus = FString::Printf(
                TEXT("Could not create the transient DWC Surface Water tiling preview material for slot %d. %s"),
                PreviewMaterialSlotIndex,
                *PreviewMaterialSet.Message);
            bSurfaceWaterPreviewStatusIsError = true;
            RequestViewportRedraw();
            return;
        }

        SurfaceWaterPreviewBaseMaterial = PreviewMaterialSet.GeneratedMaterial;
        SurfaceWaterPreviewStaticMaterial = PreviewMaterialSet.GeneratedMaterialInstance;
        SurfaceWaterPreviewMaterial = UMaterialInstanceDynamic::Create(
            SurfaceWaterPreviewStaticMaterial,
            GetTransientPackage());
    }
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        SurfaceWaterPreviewStatus = TEXT("Could not create the transient generated-material Surface Water preview instance.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    const FWetPartProfileAssignment* PreviewPartProfile = Editable.FindProfile(*Part);
    FWetnessProfileParameters AuthoredPreviewParameters;
    if (!FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(
            PreviewPartProfile,
            AuthoredPreviewParameters))
    {
        SurfaceWaterPreviewStatus = TEXT("Could not resolve the selected Wet Part's authored profile parameters.");
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    FWetClothingLocalRenderProfile PreviewLocalProfile;
    PreviewLocalProfile.Parameters = AuthoredPreviewParameters;
    // Leave StableKey empty so the preview registry key is derived from the current
    // authored parameters and texture identities. Slider edits then resolve a fresh
    // preview profile instead of reusing stale packed values.
    PreviewLocalProfile.StableKey.Reset();
    if (PreviewPartProfile != nullptr)
    {
        PreviewLocalProfile.SetSourceProfilePath(PreviewPartProfile->GetSourceProfilePath());
    }
    // Use the same Unreal texture-build path as Render Profile baking. The old
    // ad-hoc source-byte resampler only supported a subset of TextureSource formats;
    // unsupported imported textures silently produced null prepared references and
    // then fell through to Texture2DArray slice 0 (flat normal).
    FString PreparedSurfaceTextureError;
    if (!FWetClothingSurfaceTextureNormalizer::PrepareProfileTextures(
            AuthoredPreviewParameters,
            PreviewLocalProfile,
            PreparedSurfaceTextureError))
    {
        SurfaceWaterPreviewStatus = FString::Printf(
            TEXT("Could not prepare the selected profile's 512x512 Surface Water textures: %s"),
            *PreparedSurfaceTextureError);
        bSurfaceWaterPreviewStatusIsError = true;
        RequestViewportRedraw();
        return;
    }

    const FWetClothingLocalRenderProfile& LocalProfile = PreviewLocalProfile;
    const FSurfaceWaterProfileParameters& Surface = LocalProfile.Parameters.SurfaceWater;

    UDWCGPUResourceSubsystem* ResourceSubsystem = nullptr;
    if (PreviewScene.IsValid())
    {
        if (UWorld* PreviewWorld = PreviewScene->GetWorld())
        {
            ResourceSubsystem = PreviewWorld->GetSubsystem<UDWCGPUResourceSubsystem>();
        }
    }
    if (ResourceSubsystem != nullptr)
    {
        if (!ResourceSubsystem->ApplyPreviewRenderProfileFallbackProfile(
            nullptr,
            PreviewMaterialSlotIndex,
            LocalProfile,
            *SurfaceWaterPreviewMaterial))
        {
            SurfaceWaterPreviewStatus = TEXT("Could not apply the selected Surface Water render profile to the tiling preview material.");
            bSurfaceWaterPreviewStatusIsError = true;
        }

        if (ResourceSubsystem->GetDropletNormalArray() == nullptr ||
            ResourceSubsystem->GetDropletMaskArray() == nullptr)
        {
            SurfaceWaterPreviewStatus = TEXT("Surface Water preview could not build complete transient droplet texture-array resources from the authored textures. Coverage still renders, but detail may be missing.");
            bSurfaceWaterPreviewStatusIsError = true;
        }
    }
    else
    {
        SurfaceWaterPreviewStatus = TEXT("Could not initialize DWC GPU render resources for the Surface Water preview world. Coverage still renders with material fallback profile values.");
        bSurfaceWaterPreviewStatusIsError = true;
    }

    ApplySurfaceWaterPreviewTextureParameters();
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::GlobalRenderProfileTexelSize(),
        1.0f / static_cast<float>(UDWCGPUResourceSubsystem::GlobalLUTWidth));
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::WetPartDebugStrength(), 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterDebugStrength(), 0.0f);
    bSurfaceWaterPreviewFallbackProfileCacheValid =
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(0))),
            SurfaceWaterPreviewBaseFallbackProfile0) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(1))),
            SurfaceWaterPreviewBaseFallbackProfile1) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(2))),
            SurfaceWaterPreviewBaseFallbackProfile2) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(3))),
            SurfaceWaterPreviewBaseFallbackProfile3) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(4))),
            SurfaceWaterPreviewBaseFallbackProfile4) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(5))),
            SurfaceWaterPreviewBaseFallbackProfile5) &&
        SurfaceWaterPreviewMaterial->GetVectorParameterValue(
            FHashedMaterialParameterInfo(FMaterialParameterInfo(DWCWetMaterialParameters::FallbackRenderProfileTexel(6))),
            SurfaceWaterPreviewBaseFallbackProfile6);

    const int32 ResolvedSurfaceNormalUV = Asset->GetSurfaceWaterNormalUVChannelIndex();
    const int32 DropletNormalSlice = bSurfaceWaterPreviewFallbackProfileCacheValid
        ? FMath::Max(0, FMath::RoundToInt(SurfaceWaterPreviewBaseFallbackProfile0.B))
        : INDEX_NONE;
    const int32 DropletMaskSlice = bSurfaceWaterPreviewFallbackProfileCacheValid
        ? FMath::Max(0, FMath::RoundToInt(SurfaceWaterPreviewBaseFallbackProfile2.R))
        : INDEX_NONE;
    UTexture2DArray* NormalArray = ResourceSubsystem != nullptr
        ? ResourceSubsystem->GetDropletNormalArray()
        : nullptr;
    UTexture2DArray* MaskArray = ResourceSubsystem != nullptr
        ? ResourceSubsystem->GetDropletMaskArray()
        : nullptr;
    const int32 NormalArrayWidth = NormalArray != nullptr ? NormalArray->GetSizeX() : 0;
    const int32 NormalArrayHeight = NormalArray != nullptr ? NormalArray->GetSizeY() : 0;
    const int32 NormalArrayFormat = NormalArray != nullptr
        ? static_cast<int32>(NormalArray->GetPixelFormat())
        : static_cast<int32>(PF_Unknown);
    const int32 MaskArrayWidth = MaskArray != nullptr ? MaskArray->GetSizeX() : 0;
    const int32 MaskArrayHeight = MaskArray != nullptr ? MaskArray->GetSizeY() : 0;
    const int32 MaskArrayFormat = MaskArray != nullptr
        ? static_cast<int32>(MaskArray->GetPixelFormat())
        : static_cast<int32>(PF_Unknown);

    SurfaceWaterResourceDiagnostics = FString::Printf(
        TEXT("\nUV channels: Original=%d DWCData=%d SurfaceNormal=%d."),
        Asset->GetOriginalUVChannelIndex(),
        Asset->GetDWCDataUVChannelIndex(),
        ResolvedSurfaceNormalUV);
    SurfaceWaterResourceDiagnostics += FString::Printf(
        TEXT("\nTexture arrays: DropletNormalSlice=%d DropletMaskSlice=%d NormalArray=%dx%d Format=%d MaskArray=%dx%d Format=%d."),
        DropletNormalSlice,
        DropletMaskSlice,
        NormalArrayWidth,
        NormalArrayHeight,
        NormalArrayFormat,
        MaskArrayWidth,
        MaskArrayHeight,
        MaskArrayFormat);
    UTexture2D* PreparedNormal = PreviewLocalProfile.NormalizedDropletNormal;
    UTexture2D* PreparedMask = PreviewLocalProfile.NormalizedDropletMask;
    SurfaceWaterResourceDiagnostics += FString::Printf(
        TEXT("\nPrepared preview textures: Normal=%dx%d Format=%d Mask=%dx%d Format=%d."),
        PreparedNormal != nullptr ? PreparedNormal->GetSizeX() : 0,
        PreparedNormal != nullptr ? PreparedNormal->GetSizeY() : 0,
        PreparedNormal != nullptr ? static_cast<int32>(PreparedNormal->GetPixelFormat()) : static_cast<int32>(PF_Unknown),
        PreparedMask != nullptr ? PreparedMask->GetSizeX() : 0,
        PreparedMask != nullptr ? PreparedMask->GetSizeY() : 0,
        PreparedMask != nullptr ? static_cast<int32>(PreparedMask->GetPixelFormat()) : static_cast<int32>(PF_Unknown));

    const bool bDropletDetailRequested =
        Surface.bEnabled &&
        bSurfaceWaterPreviewDropletsEnabled &&
        Surface.SurfaceWaterNormalStrength > UE_KINDA_SMALL_NUMBER;
    if (!bSurfaceWaterPreviewFallbackProfileCacheValid)
    {
        SurfaceWaterResourceDiagnostics +=
            TEXT("\nError: Fallback render-profile texels could not be read from the preview MID.");
        bSurfaceWaterResourceDiagnosticError = true;
    }
    if (ResolvedSurfaceNormalUV == Asset->GetDWCDataUVChannelIndex())
    {
        SurfaceWaterResourceDiagnostics +=
            TEXT("\nError: Surface Normal UV points at the generated DWC Data UV channel. Choose Same as Original or the material normal map's source UV.");
        bSurfaceWaterResourceDiagnosticError = true;
    }
    const bool bHasResolvedDropletNormal =
        PreviewLocalProfile.NormalizedDropletNormal != nullptr ||
        Surface.DropletNormalTexture != nullptr;
    const bool bHasResolvedDropletMask =
        PreviewLocalProfile.NormalizedDropletMask != nullptr ||
        Surface.DropletMaskTexture != nullptr;
    if (bDropletDetailRequested && bHasResolvedDropletNormal && DropletNormalSlice <= 0)
    {
        SurfaceWaterResourceDiagnostics +=
            TEXT("\nError: the authored droplet normal resolved to Texture2DArray slice 0 (flat-normal fallback).");
        bSurfaceWaterResourceDiagnosticError = true;
    }
    if (bDropletDetailRequested &&
        (NormalArray == nullptr ||
         NormalArrayWidth != DWCSurfaceTextureSharedAsset::Resolution ||
         NormalArrayHeight != DWCSurfaceTextureSharedAsset::Resolution ||
         NormalArrayFormat == static_cast<int32>(PF_Unknown)))
    {
        SurfaceWaterResourceDiagnostics +=
            TEXT("\nError: the droplet normal Texture2DArray is missing or does not satisfy the prepared 512x512 texture contract.");
        bSurfaceWaterResourceDiagnosticError = true;
    }
    if (Surface.bEnabled && bSurfaceWaterPreviewDropletsEnabled && bHasResolvedDropletMask &&
        (DropletMaskSlice <= 0 || MaskArray == nullptr ||
         MaskArrayWidth != DWCSurfaceTextureSharedAsset::Resolution ||
         MaskArrayHeight != DWCSurfaceTextureSharedAsset::Resolution ||
         MaskArrayFormat == static_cast<int32>(PF_Unknown)))
    {
        SurfaceWaterResourceDiagnostics +=
            TEXT("\nError: the authored droplet mask did not resolve to a valid prepared Texture2DArray slice.");
        bSurfaceWaterResourceDiagnosticError = true;
    }
    if (bSurfaceWaterResourceDiagnosticError)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC Surface Water preview resource diagnostic for asset '%s' slot %d:%s"),
            *GetPathNameSafe(Asset),
            PreviewMaterialSlotIndex,
            *SurfaceWaterResourceDiagnostics);
    }

    ApplySurfaceWaterPreviewRenderOverrides();

    if (PreviewMaterialSlotIndex >= 0 && PreviewMaterialSlotIndex < PreviewMeshComponent->GetNumMaterials())
    {
        PreviewMeshComponent->SetMaterial(PreviewMaterialSlotIndex, SurfaceWaterPreviewMaterial);
    }
    RefreshMaterialSectionVisibility();
    PreviewMeshComponent->MarkRenderStateDirty();

    if (SurfaceWaterPreviewStatus.IsEmpty())
    {
        SurfaceWaterPreviewStatus = TEXT("Using a preview-only transient material, authored Wetness Profile parameters, and transient preview textures. No Build for Runtime output is required.");
        bSurfaceWaterPreviewStatusIsError = false;
    }

    if (SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)
    {
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d NormalStrength=%.3g RoughnessBlend=%.3g TargetRoughness=%.3g TotalStrength=%.3g Specular=%.3g."),
            SurfacePreviewLocalProfileID,
            Surface.bEnabled ? 1 : 0,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterSpecular);
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nSingleCircleSurface=%g Droplet1SpawnChance=%.3g Droplet1StampPx=(%.3g,%.3g) SizeScale=%.3g Droplet1DetailSize=%.3g Droplet2DetailSize=%.3g AbsorbedWetness=0 NormalFlipXY=%d/%d."),
            PreviewSurfaceWater,
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Surface.DropletHeightPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.DropletDetailSize,
            Part->SurfaceWater.DropletFlowDetailSize,
            bSurfaceWaterPreviewFlipNormalX ? 1 : 0,
            bSurfaceWaterPreviewFlipNormalY ? 1 : 0);
    }
    else
    {
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nPreview LocalProfileID %d: SurfaceEnabled=%d NormalStrength=%.3g RoughnessBlend=%.3g TargetRoughness=%.3g TotalStrength=%.3g Specular=%.3g."),
            SurfacePreviewLocalProfileID,
            Surface.bEnabled ? 1 : 0,
            Surface.SurfaceWaterNormalStrength,
            Surface.SurfaceWaterRoughnessBlend,
            Surface.SurfaceWaterTargetRoughness,
            Surface.SurfaceWaterTotalStrength,
            Surface.SurfaceWaterSpecular);
        SurfaceWaterPreviewStatus += FString::Printf(
            TEXT("\nFullPartSurface=%g Droplet1SpawnChance=%.3g Droplet1StampPx=(%.3g,%.3g) SizeScale=%.3g Droplet1DetailSize=%.3g Droplet2DetailSize=%.3g AbsorbedWetness=0 NormalFlipXY=%d/%d."),
            PreviewSurfaceWater,
            Surface.DropletSpawnProbability,
            Surface.DropletRadiusPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Surface.DropletHeightPixels * Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.GetResolvedDropletStampSizeScale(),
            Part->SurfaceWater.DropletDetailSize,
            Part->SurfaceWater.DropletFlowDetailSize,
            bSurfaceWaterPreviewFlipNormalX ? 1 : 0,
            bSurfaceWaterPreviewFlipNormalY ? 1 : 0);
    }
    SurfaceWaterPreviewStatus += FString::Printf(
        TEXT("\nDisplayMode=%s."),
        SurfaceWaterPreviewDisplayMode == EDWCSurfaceWaterTilingPreviewDisplayMode::DropletNormal
            ? TEXT("DropletNormal")
            : TEXT("Lit"));
    if (!Surface.bEnabled)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has Surface Water disabled, so the preview keeps the source material appearance.");
    }
    else if (Surface.SurfaceWaterNormalStrength <= UE_KINDA_SMALL_NUMBER)
    {
        SurfaceWaterPreviewStatus += TEXT("\nSelected preview profile has zero Surface Water Normal Strength, so World Normal remains unchanged.");
    }
    else if (!bSurfaceWaterPreviewDropletsEnabled)
    {
        SurfaceWaterPreviewStatus += TEXT("\nThe droplet normal layer is disabled, so World Normal remains unchanged.");
    }

    const bool bMissingDropletNormal =
        Surface.bEnabled &&
        bSurfaceWaterPreviewDropletsEnabled &&
        Surface.DropletNormalTexture == nullptr;
    const bool bMissingDropletMask =
        Surface.bEnabled &&
        bSurfaceWaterPreviewDropletsEnabled &&
        Surface.DropletMaskTexture == nullptr;
    SurfaceWaterPreviewStatus += FString::Printf(
        TEXT("\nPreview source data: DropletNormal=%d DropletMask=%d Droplet2Normal=%d Droplet2Mask=%d."),
        Surface.DropletNormalTexture != nullptr ? 1 : 0,
        Surface.DropletMaskTexture != nullptr ? 1 : 0,
        Surface.DropletFlowNormalTexture != nullptr ? 1 : 0,
        Surface.DropletFlowMaskTexture != nullptr ? 1 : 0);
    if (bMissingDropletNormal)
    {
        SurfaceWaterPreviewStatus += TEXT("\nThe authored profile has no Primary Droplet normal texture, so the preview shows coverage without detail normals.");
        bSurfaceWaterPreviewStatusIsError = true;
    }
    if (bMissingDropletMask)
    {
        SurfaceWaterPreviewStatus += TEXT("\nThe authored profile has no Primary Droplet mask. Surface Water is mask-gated, so preview coverage resolves to zero.");
        bSurfaceWaterPreviewStatusIsError = true;
    }

    SurfaceWaterPreviewStatus += SurfaceWaterResourceDiagnostics;
    bSurfaceWaterPreviewStatusIsError |= bSurfaceWaterResourceDiagnosticError;

    RequestViewportRedraw();
}

void SDWCPartViewport::ApplySurfaceWaterPreviewRenderOverrides()
{
    if (SurfaceWaterPreviewMaterial == nullptr)
    {
        return;
    }

    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterNormalFlipX(),
        bSurfaceWaterPreviewFlipNormalX ? 1.0f : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        DWCWetMaterialParameters::SurfaceWaterNormalFlipY(),
        bSurfaceWaterPreviewFlipNormalY ? 1.0f : 0.0f);
    SurfaceWaterPreviewMaterial->SetScalarParameterValue(
        PreviewDebugModeParameter,
        SurfaceWaterPreviewDisplayMode == EDWCSurfaceWaterTilingPreviewDisplayMode::DropletNormal ? 4.0f : 0.0f);

    if (!bSurfaceWaterPreviewFallbackProfileCacheValid)
    {
        return;
    }

    FLinearColor Texel0 = SurfaceWaterPreviewBaseFallbackProfile0;
    const FLinearColor Texel1 = SurfaceWaterPreviewBaseFallbackProfile1;
    FLinearColor Texel2 = SurfaceWaterPreviewBaseFallbackProfile2;
    const FLinearColor Texel3 = SurfaceWaterPreviewBaseFallbackProfile3;
    FLinearColor Texel4 = SurfaceWaterPreviewBaseFallbackProfile4;
    const FLinearColor Texel5 = SurfaceWaterPreviewBaseFallbackProfile5;
    const FLinearColor Texel6 = SurfaceWaterPreviewBaseFallbackProfile6;

    if (!bSurfaceWaterPreviewDropletsEnabled)
    {
        Texel0.B = 0.0f;
        Texel0.A = 0.0f;
        Texel2.R = 0.0f;
        Texel2.G = 0.0f;
        Texel4.R = 0.0f;
        Texel4.G = 0.0f;
    }

    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(0), Texel0);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(1), Texel1);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(2), Texel2);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(3), Texel3);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(4), Texel4);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(5), Texel5);
    SurfaceWaterPreviewMaterial->SetVectorParameterValue(
        DWCWetMaterialParameters::FallbackRenderProfileTexel(6), Texel6);
}

FText SDWCPartViewport::GetSurfaceWaterPreviewStatusText() const
{
    return FText::FromString(SurfaceWaterPreviewStatus);
}

FSlateColor SDWCPartViewport::GetSurfaceWaterPreviewStatusColor() const
{
    return bSurfaceWaterPreviewStatusIsError
        ? FSlateColor(FStyleColors::Error)
        : FSlateColor(FStyleColors::ForegroundHover);
}

void SDWCPartViewport::RequestViewportRedraw()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }

    Invalidate();
}

void SDWCPartViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SDWCPartViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FDWCPartViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDWCPartViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WCAEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::DWCEditor::CreateDWCViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SDWCPartViewport::HandleIslandPickedFromClient(int32 UVIslandID, bool bAppendSelection)
{
    if (OnIslandPicked.IsBound())
    {
        OnIslandPicked.Execute(UVIslandID, bAppendSelection);
    }
}

void SDWCPartViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

}

void SDWCPartViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

void SDWCPartViewport::CacheOriginalMaterials()
{
    OriginalPreviewMaterials.Reset();

    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    OriginalPreviewMaterials.Reserve(MaterialCount);

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        OriginalPreviewMaterials.Add(PreviewMeshComponent->GetMaterial(MaterialIndex));
    }
}

void SDWCPartViewport::RestoreOriginalMaterials()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    for (int32 MaterialIndex = 0; MaterialIndex < OriginalPreviewMaterials.Num(); ++MaterialIndex)
    {
        PreviewMeshComponent->SetMaterial(MaterialIndex, OriginalPreviewMaterials[MaterialIndex]);
    }
}

UMaterialInterface* SDWCPartViewport::ResolveWetPartOverlayMaterial()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 OriginalUVChannelIndex = Asset != nullptr
        ? FMath::Clamp(Asset->GetOriginalUVChannelIndex(), 0, 7)
        : 0;

    const FString MaterialName = FString::Printf(
        TEXT("M_DWC_PartPreviewOverlay_UV%d"),
        OriginalUVChannelIndex);
    const FString MaterialObjectPath = FString::Printf(
        TEXT("/DynamicWetClothes/Editor/Materials/%s.%s"),
        *MaterialName,
        *MaterialName);

    if (WetPartOverlayMaterial != nullptr &&
        WetPartOverlayMaterial->GetPathName() == MaterialObjectPath)
    {
        return WetPartOverlayMaterial;
    }

    WetPartOverlayMID = nullptr;
    WetPartOverlayMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        *MaterialObjectPath);
    if (WetPartOverlayMaterial == nullptr)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DWC Original-UV Part Preview material is missing: %s. ")
            TEXT("Run Scripts/Python/GenerateDWCPartOverlayMaterial.py and save all generated UV variants."),
            *MaterialObjectPath);
        return nullptr;
    }

    return WetPartOverlayMaterial;
}

#undef LOCTEXT_NAMESPACE
