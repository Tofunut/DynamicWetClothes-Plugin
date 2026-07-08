/*
 *  Material Slot의 UV 삼각형과 대표 텍스처를 작은 썸네일로 그리는 Slate 위젯을 구현합니다.
 */

#include "WetClothing/Common/Widgets/SWetClothingMaterialSlotPreview.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"

void SWetClothingMaterialSlotPreview::Construct(const FArguments& InArgs)
{
    Triangles = InArgs._Triangles;
    PreviewTexture = InArgs._PreviewTexture;

    if (UTexture2D* PreviewTexture2D = Cast<UTexture2D>(PreviewTexture.Get()))
    {
        FString ErrorMessage;
        FWetClothingTextureReadbackUtils::TryReadTextureSourceData(PreviewTexture2D, PreviewTextureData, ErrorMessage);
    }
}

FVector2D SWetClothingMaterialSlotPreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(48.0f, 48.0f);
}

int32 SWetClothingMaterialSlotPreview::OnPaint(
    const FPaintArgs&        Args,
    const FGeometry&         AllottedGeometry,
    const FSlateRect&        MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32                    LayerId,
    const FWidgetStyle&      InWidgetStyle,
    bool                     bParentEnabled) const
{
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FVector2D    LocalSize = AllottedGeometry.GetLocalSize();

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        FLinearColor(0.03f, 0.03f, 0.03f, 1.0f));

    if (Triangles.Num() == 0 || LocalSize.X <= 1.0f || LocalSize.Y <= 1.0f)
    {
        return LayerId + 1;
    }

    const FQuat ViewRotation = FRotator(-18.0f, -32.0f, 0.0f).Quaternion();
    struct FProjectedTriangle
    {
        FVector2D Positions[3];
        FVector2D UVs[3];
    };

    TArray<FProjectedTriangle> ProjectedTriangles;
    ProjectedTriangles.Reserve(Triangles.Num());

    bool      bHasBounds = false;
    FVector2D MinPoint = FVector2D::ZeroVector;
    FVector2D MaxPoint = FVector2D::ZeroVector;

    for (const FWetClothingAssetUVTriangle& Triangle : Triangles)
    {
        FProjectedTriangle ProjectedTriangle;

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVector   RotatedPosition = ViewRotation.RotateVector(Triangle.LocalPositions[CornerIndex]);
            const FVector2D ProjectedPoint(RotatedPosition.Y, -RotatedPosition.Z);
            ProjectedTriangle.Positions[CornerIndex] = ProjectedPoint;
            ProjectedTriangle.UVs[CornerIndex] = Triangle.UVs[CornerIndex];

            if (!bHasBounds)
            {
                MinPoint = ProjectedPoint;
                MaxPoint = ProjectedPoint;
                bHasBounds = true;
            }
            else
            {
                MinPoint.X = FMath::Min(MinPoint.X, ProjectedPoint.X);
                MinPoint.Y = FMath::Min(MinPoint.Y, ProjectedPoint.Y);
                MaxPoint.X = FMath::Max(MaxPoint.X, ProjectedPoint.X);
                MaxPoint.Y = FMath::Max(MaxPoint.Y, ProjectedPoint.Y);
            }
        }

        ProjectedTriangles.Add(ProjectedTriangle);
    }

    if (!bHasBounds)
    {
        return LayerId + 1;
    }

    const FVector2D BoundsSize = MaxPoint - MinPoint;
    const float     Padding = 5.0f;
    const float     AvailableWidth = FMath::Max(1.0f, LocalSize.X - Padding * 2.0f);
    const float     AvailableHeight = FMath::Max(1.0f, LocalSize.Y - Padding * 2.0f);
    const float     ScaleX = AvailableWidth / FMath::Max(BoundsSize.X, 1.0f);
    const float     ScaleY = AvailableHeight / FMath::Max(BoundsSize.Y, 1.0f);
    const float     UniformScale = FMath::Max(0.01f, FMath::Min(ScaleX, ScaleY));
    const FVector2D ScaledSize = BoundsSize * UniformScale;
    const FVector2D Offset(
        (LocalSize.X - ScaledSize.X) * 0.5f,
        (LocalSize.Y - ScaledSize.Y) * 0.5f);

    if (PreviewTextureData.IsValid())
    {
        const FSlateRenderTransform RenderTransform = AllottedGeometry.GetAccumulatedRenderTransform();
        const FSlateResourceHandle  ResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
        if (ResourceHandle.IsValid())
        {
            TArray<FSlateVertex> FillVerts;
            TArray<SlateIndex>   FillIndices;
            FillVerts.Reserve(ProjectedTriangles.Num() * 3);
            FillIndices.Reserve(ProjectedTriangles.Num() * 3);

            for (const FProjectedTriangle& ProjectedTriangle : ProjectedTriangles)
            {
                const SlateIndex StartVertexIndex = static_cast<SlateIndex>(FillVerts.Num());

                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    const FVector2D PaintedPosition = (ProjectedTriangle.Positions[CornerIndex] - MinPoint) * UniformScale + Offset;
                    const FVector2D UV(
                        ProjectedTriangle.UVs[CornerIndex].X - FMath::FloorToDouble(ProjectedTriangle.UVs[CornerIndex].X),
                        ProjectedTriangle.UVs[CornerIndex].Y - FMath::FloorToDouble(ProjectedTriangle.UVs[CornerIndex].Y));
                    const int32  SampleX = FMath::RoundToInt(UV.X * (PreviewTextureData.Width - 1));
                    const int32  SampleY = FMath::RoundToInt((1.0f - UV.Y) * (PreviewTextureData.Height - 1));
                    const FColor VertexColor = PreviewTextureData.GetLinearColor(SampleX, SampleY).ToFColor(true);
                    FillVerts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                        RenderTransform,
                        FVector2f(PaintedPosition),
                        FVector2f::ZeroVector,
                        VertexColor));
                }

                FillIndices.Add(StartVertexIndex);
                FillIndices.Add(StartVertexIndex + 1);
                FillIndices.Add(StartVertexIndex + 2);
            }

            FSlateDrawElement::MakeCustomVerts(
                OutDrawElements,
                LayerId + 1,
                ResourceHandle,
                FillVerts,
                FillIndices,
                nullptr,
                0,
                0,
                ESlateDrawEffect::None);
        }
    }

    const FLinearColor LineColor(0.92f, 0.92f, 0.92f, 1.0f);
    for (const FProjectedTriangle& ProjectedTriangle : ProjectedTriangles)
    {
        TArray<FVector2D> PaintedLinePoints;
        PaintedLinePoints.Reserve(4);

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            PaintedLinePoints.Add((ProjectedTriangle.Positions[CornerIndex] - MinPoint) * UniformScale + Offset);
        }
        const FVector2D FirstPoint = PaintedLinePoints[0];
        PaintedLinePoints.Add(FirstPoint);

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId + 2,
            AllottedGeometry.ToPaintGeometry(),
            PaintedLinePoints,
            ESlateDrawEffect::None,
            LineColor,
            true,
            0.1f);
    }

    return LayerId + 2;
}
