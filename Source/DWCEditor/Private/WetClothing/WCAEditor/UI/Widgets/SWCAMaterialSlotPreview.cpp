// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Implements the Slate thumbnail widget that draws a material slot's UV triangles and representative texture.
 */

#include "SWCAMaterialSlotPreview.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

namespace SWCAMaterialSlotPreviewPrivate
{
    constexpr int32 MaxMaterialSlotPreviewTriangles = 1000;

    uint64 MakeEdgeKey(const FVector2D& A, const FVector2D& B)
    {
        auto MakePointKey = [](const FVector2D& Point) -> uint32
        {
            constexpr float QuantizeScale = 4096.0f;
            const uint32    X = static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(Point.X * QuantizeScale), 0, 65535));
            const uint32    Y = static_cast<uint32>(FMath::Clamp(FMath::RoundToInt(Point.Y * QuantizeScale), 0, 65535));
            return (X << 16) | Y;
        };

        const uint32 PointA = MakePointKey(A);
        const uint32 PointB = MakePointKey(B);
        const uint32 MinPointKey = FMath::Min(PointA, PointB);
        const uint32 MaxPointKey = FMath::Max(PointA, PointB);
        return (static_cast<uint64>(MinPointKey) << 32) | static_cast<uint64>(MaxPointKey);
    }
} // namespace SWCAMaterialSlotPreviewPrivate

using namespace SWCAMaterialSlotPreviewPrivate;

void SWCAMaterialSlotPreview::Construct(const FArguments& InArgs)
{
    PreviewTexture = InArgs._PreviewTexture;
    bDrawWireframe = InArgs._DrawWireframe;

    if (UTexture2D* PreviewTexture2D = Cast<UTexture2D>(PreviewTexture.Get()))
    {
        FString ErrorMessage;
        FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            PreviewTexture2D,
            PreviewTextureData,
            ErrorMessage);
    }

    BuildPaintCache(InArgs._Triangles);
}

void SWCAMaterialSlotPreview::BuildPaintCache(
    const TArray<FWetClothingAssetUVTriangle>& InTriangles)
{
    CachedTriangles.Reset();
    CachedEdges.Reset();
    bHasVisibleTextureVariation = false;
    CachedProjectedBoundsSize = FVector2D(1.0f, 1.0f);
    bSlateGeometryCacheValid = false;
    CachedFillVertices.Reset();
    CachedFillIndices.Reset();
    CachedWireframeLines.Reset();

    if (InTriangles.IsEmpty())
    {
        return;
    }

    const int32  SampleCount = FMath::Min(InTriangles.Num(), MaxMaterialSlotPreviewTriangles);
    const double SampleStride = static_cast<double>(InTriangles.Num()) / static_cast<double>(SampleCount);
    const FQuat  ViewRotation = FRotator(-18.0f, -32.0f, 0.0f).Quaternion();

    struct FProjectedTriangle
    {
        FVector2D Positions[3];
        FVector2D UVs[3];
    };

    TArray<FProjectedTriangle> ProjectedTriangles;
    ProjectedTriangles.Reserve(SampleCount);
    bool      bHasBounds = false;
    FVector2D MinPoint = FVector2D::ZeroVector;
    FVector2D MaxPoint = FVector2D::ZeroVector;

    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        const int32 TriangleIndex = FMath::Min(
            IntCastChecked<int32>(FMath::FloorToInt(static_cast<double>(SampleIndex) * SampleStride)),
            InTriangles.Num() - 1);
        const FWetClothingAssetUVTriangle& Triangle = InTriangles[TriangleIndex];
        FProjectedTriangle&                Projected = ProjectedTriangles.AddDefaulted_GetRef();
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVector   RotatedPosition = ViewRotation.RotateVector(Triangle.LocalPositions[CornerIndex]);
            const FVector2D Point(RotatedPosition.Y, -RotatedPosition.Z);
            Projected.Positions[CornerIndex] = Point;
            Projected.UVs[CornerIndex] = Triangle.UVs[CornerIndex];
            if (!bHasBounds)
            {
                MinPoint = Point;
                MaxPoint = Point;
                bHasBounds = true;
            }
            else
            {
                MinPoint.X = FMath::Min(MinPoint.X, Point.X);
                MinPoint.Y = FMath::Min(MinPoint.Y, Point.Y);
                MaxPoint.X = FMath::Max(MaxPoint.X, Point.X);
                MaxPoint.Y = FMath::Max(MaxPoint.Y, Point.Y);
            }
        }
    }

    if (!bHasBounds)
    {
        return;
    }

    CachedProjectedBoundsSize = MaxPoint - MinPoint;
    CachedProjectedBoundsSize.X = FMath::Max(CachedProjectedBoundsSize.X, 1.0f);
    CachedProjectedBoundsSize.Y = FMath::Max(CachedProjectedBoundsSize.Y, 1.0f);

    FLinearColor MinSampleColor(FLT_MAX, FLT_MAX, FLT_MAX, 1.0f);
    FLinearColor MaxSampleColor(-FLT_MAX, -FLT_MAX, -FLT_MAX, 1.0f);
    double       SampleSaturationSum = 0.0;
    int32        TextureSampleCount = 0;
    TSet<uint64> EdgeKeys;

    CachedTriangles.Reserve(ProjectedTriangles.Num());
    for (const FProjectedTriangle& Projected : ProjectedTriangles)
    {
        FCachedTriangle& Cached = CachedTriangles.AddDefaulted_GetRef();
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Cached.NormalizedPositions[CornerIndex] = FVector2D(
                (Projected.Positions[CornerIndex].X - MinPoint.X) / CachedProjectedBoundsSize.X,
                (Projected.Positions[CornerIndex].Y - MinPoint.Y) / CachedProjectedBoundsSize.Y);
            Cached.UVs[CornerIndex] = FVector2D(
                Projected.UVs[CornerIndex].X - FMath::FloorToDouble(Projected.UVs[CornerIndex].X),
                Projected.UVs[CornerIndex].Y - FMath::FloorToDouble(Projected.UVs[CornerIndex].Y));

            FLinearColor SampleColor(0.28f, 0.28f, 0.28f, 1.0f);
            if (PreviewTextureData.IsValid())
            {
                const int32 SampleX = IntCastChecked<int32>(FMath::RoundToInt(Cached.UVs[CornerIndex].X * (PreviewTextureData.Width - 1)));
                const int32 SampleY = IntCastChecked<int32>(FMath::RoundToInt((1.0f - Cached.UVs[CornerIndex].Y) * (PreviewTextureData.Height - 1)));
                SampleColor = PreviewTextureData.GetLinearColor(SampleX, SampleY);
                MinSampleColor.R = FMath::Min(MinSampleColor.R, SampleColor.R);
                MinSampleColor.G = FMath::Min(MinSampleColor.G, SampleColor.G);
                MinSampleColor.B = FMath::Min(MinSampleColor.B, SampleColor.B);
                MaxSampleColor.R = FMath::Max(MaxSampleColor.R, SampleColor.R);
                MaxSampleColor.G = FMath::Max(MaxSampleColor.G, SampleColor.G);
                MaxSampleColor.B = FMath::Max(MaxSampleColor.B, SampleColor.B);
                SampleSaturationSum += SampleColor.LinearRGBToHSV().G;
                ++TextureSampleCount;
            }
            Cached.Colors[CornerIndex] = SampleColor.ToFColor(true);
        }

        const int32 EdgeCorners[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
        for (const auto& EdgeCorner : EdgeCorners)
        {
            const FVector2D& A = Cached.NormalizedPositions[EdgeCorner[0]];
            const FVector2D& B = Cached.NormalizedPositions[EdgeCorner[1]];
            const uint64     EdgeKey = MakeEdgeKey(A, B);
            if (!EdgeKeys.Contains(EdgeKey))
            {
                EdgeKeys.Add(EdgeKey);
                FCachedEdge& Edge = CachedEdges.AddDefaulted_GetRef();
                Edge.NormalizedStart = A;
                Edge.NormalizedEnd = B;
            }
        }
    }

    if (TextureSampleCount > 0)
    {
        const float ChannelRange = FMath::Max3(
            MaxSampleColor.R - MinSampleColor.R,
            MaxSampleColor.G - MinSampleColor.G,
            MaxSampleColor.B - MinSampleColor.B);
        const double AverageSaturation = SampleSaturationSum / TextureSampleCount;
        bHasVisibleTextureVariation = ChannelRange > 0.08f || AverageSaturation > 0.08;
    }
}

void SWCAMaterialSlotPreview::UpdateSlateGeometryCache(const FGeometry& AllottedGeometry) const
{
    const FVector2D             LocalSize = AllottedGeometry.GetLocalSize();
    const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
    const FVector2f             TransformOrigin = TransformPoint(RenderTransform, FVector2f(0.0f, 0.0f));
    const FVector2f             TransformUnitX = TransformPoint(RenderTransform, FVector2f(1.0f, 0.0f));
    const FVector2f             TransformUnitY = TransformPoint(RenderTransform, FVector2f(0.0f, 1.0f));

    const bool bSameGeometry =
        bSlateGeometryCacheValid &&
        CachedSlateLocalSize.Equals(LocalSize, KINDA_SMALL_NUMBER) &&
        CachedTransformOrigin.Equals(TransformOrigin, KINDA_SMALL_NUMBER) &&
        CachedTransformUnitX.Equals(TransformUnitX, KINDA_SMALL_NUMBER) &&
        CachedTransformUnitY.Equals(TransformUnitY, KINDA_SMALL_NUMBER);
    if (bSameGeometry)
    {
        return;
    }

    bSlateGeometryCacheValid = true;
    CachedSlateLocalSize = LocalSize;
    CachedTransformOrigin = TransformOrigin;
    CachedTransformUnitX = TransformUnitX;
    CachedTransformUnitY = TransformUnitY;
    CachedFillVertices.Reset();
    CachedFillIndices.Reset();
    CachedWireframeLines.Reset();

    if (CachedTriangles.IsEmpty() || LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
    {
        return;
    }

    const float     Padding = 5.0f;
    const FVector2D Available(
        FMath::Max(1.0f, LocalSize.X - Padding * 2.0f),
        FMath::Max(1.0f, LocalSize.Y - Padding * 2.0f));
    const float UniformScale = FMath::Max(
        0.01f,
        static_cast<float>(FMath::Min(
            Available.X / CachedProjectedBoundsSize.X,
            Available.Y / CachedProjectedBoundsSize.Y)));
    const FVector2D ScaledSize = CachedProjectedBoundsSize * UniformScale;
    const FVector2D Offset(
        (LocalSize.X - ScaledSize.X) * 0.5f,
        (LocalSize.Y - ScaledSize.Y) * 0.5f);

    auto PaintPoint = [&ScaledSize, &Offset](const FVector2D& Normalized)
    {
        return FVector2D(
            Normalized.X * ScaledSize.X + Offset.X,
            Normalized.Y * ScaledSize.Y + Offset.Y);
    };

    CachedFillVertices.Reserve(CachedTriangles.Num() * 3);
    CachedFillIndices.Reserve(CachedTriangles.Num() * 3);
    for (const FCachedTriangle& Triangle : CachedTriangles)
    {
        const SlateIndex StartVertexIndex = static_cast<SlateIndex>(CachedFillVertices.Num());
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            CachedFillVertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                RenderTransform,
                FVector2f(PaintPoint(Triangle.NormalizedPositions[CornerIndex])),
                FVector2f(Triangle.UVs[CornerIndex]),
                Triangle.Colors[CornerIndex]));
        }
        CachedFillIndices.Add(StartVertexIndex);
        CachedFillIndices.Add(StartVertexIndex + 1);
        CachedFillIndices.Add(StartVertexIndex + 2);
    }

    CachedWireframeLines.Reserve(CachedEdges.Num());
    for (const FCachedEdge& Edge : CachedEdges)
    {
        TArray<FVector2D>& Line = CachedWireframeLines.AddDefaulted_GetRef();
        Line.Reserve(2);
        Line.Add(PaintPoint(Edge.NormalizedStart));
        Line.Add(PaintPoint(Edge.NormalizedEnd));
    }
}

FVector2D SWCAMaterialSlotPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(48.0f, 48.0f);
}

int32 SWCAMaterialSlotPreview::OnPaint(
    const FPaintArgs&        Args,
    const FGeometry&         AllottedGeometry,
    const FSlateRect&        MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32                    LayerId,
    const FWidgetStyle&      InWidgetStyle,
    bool                     bParentEnabled) const
{
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        FLinearColor(0.03f, 0.03f, 0.03f, 1.0f));

    UpdateSlateGeometryCache(AllottedGeometry);
    if (CachedFillVertices.IsEmpty())
    {
        return LayerId + 1;
    }

    const FSlateResourceHandle ResourceHandle =
        FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
    if (ResourceHandle.IsValid())
    {
        FSlateDrawElement::MakeCustomVerts(
            OutDrawElements,
            LayerId + 1,
            ResourceHandle,
            CachedFillVertices,
            CachedFillIndices,
            nullptr,
            0,
            0,
            ESlateDrawEffect::None);
    }

    if (bDrawWireframe || !bHasVisibleTextureVariation)
    {
        const FLinearColor LineColor(0.96f, 0.96f, 0.96f, 1.0f);
        for (const TArray<FVector2D>& LinePoints : CachedWireframeLines)
        {
            FSlateDrawElement::MakeLines(
                OutDrawElements,
                LayerId + 2,
                AllottedGeometry.ToPaintGeometry(),
                LinePoints,
                ESlateDrawEffect::None,
                LineColor,
                true,
                0.55f);
        }
    }

    return LayerId + 2;
}
