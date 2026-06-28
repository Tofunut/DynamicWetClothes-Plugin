#include "SWetClothingProfileUVView.h"

#include "Engine/Texture.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr double UVEdgeQuantizeScale = 100000000.0;

    FQuantizedUVPoint QuantizeUVPoint(const FVector2D& UV)
    {
        return FQuantizedUVPoint{
            static_cast<int64>(FMath::RoundToDouble(UV.X * UVEdgeQuantizeScale)),
            static_cast<int64>(FMath::RoundToDouble(UV.Y * UVEdgeQuantizeScale))
        };
    }

    FQuantizedUVEdge MakeCanonicalUVEdge(const FVector2D& A, const FVector2D& B)
    {
        const FQuantizedUVPoint QuantizedA = QuantizeUVPoint(A);
        const FQuantizedUVPoint QuantizedB = QuantizeUVPoint(B);
        const bool              bSwap = QuantizedB.X < QuantizedA.X || (QuantizedB.X == QuantizedA.X && QuantizedB.Y < QuantizedA.Y);
        return bSwap
                   ? FQuantizedUVEdge{ QuantizedB, QuantizedA }
                   : FQuantizedUVEdge{ QuantizedA, QuantizedB };
    }
} // namespace

void SWetClothingProfileUVView::Construct(const FArguments& InArgs)
{
    OnIslandSelectionChanged = InArgs._OnIslandSelectionChanged;
    BackgroundTextureBrush.DrawAs = ESlateBrushDrawType::Image;
    BackgroundTextureBrush.Mirroring = ESlateBrushMirrorType::Vertical;
    ResetView();
}

void SWetClothingProfileUVView::SetIslands(const TArray<TSharedPtr<FWetClothingProfileUVIsland>>& InIslands)
{
    Islands.Reset();

    for (const TSharedPtr<FWetClothingProfileUVIsland>& Island : InIslands)
    {
        if (Island.IsValid())
        {
            Islands.Add(*Island);
        }
    }

    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::SetSelectedIslands(const TSet<int32>& InIslandIDs)
{
    SelectedIslandIDs = InIslandIDs;
    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::SetIslandColors(const TMap<int32, FLinearColor>& InIslandColors)
{
    IslandColors = InIslandColors;
    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::SetBackgroundTexture(UTexture* InTexture)
{
    BackgroundTexture = InTexture;
    BackgroundTextureBrush.SetResourceObject(InTexture);

    if (InTexture != nullptr)
    {
        BackgroundTextureBrush.ImageSize = FVector2D(
            FMath::Max(1, InTexture->GetSurfaceWidth()),
            FMath::Max(1, InTexture->GetSurfaceHeight()));
    }
    else
    {
        BackgroundTextureBrush.ImageSize = FVector2D(1.0f, 1.0f);
    }

    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::SetSelectionTool(EWetClothingProfileUVSelectionTool InSelectionTool)
{
    SelectionTool = InSelectionTool;
    ResetSelectionInteractionState();
    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::SetDisplayMode(EWetClothingProfileUVDisplayMode InDisplayMode)
{
    DisplayMode = InDisplayMode;
    Invalidate(EInvalidateWidget::Paint);
}

void SWetClothingProfileUVView::Clear()
{
    Islands.Reset();
    SelectedIslandIDs.Reset();
    IslandColors.Reset();
    SetBackgroundTexture(nullptr);
    ResetView();
    Invalidate(EInvalidateWidget::Paint);
}

FVector2D SWetClothingProfileUVView::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(512.0f, 512.0f);
}

int32 SWetClothingProfileUVView::OnPaint(
    const FPaintArgs&        Args,
    const FGeometry&         AllottedGeometry,
    const FSlateRect&        MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32                    LayerId,
    const FWidgetStyle&      InWidgetStyle,
    bool                     bParentEnabled) const
{
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const FLinearColor SelectColor = FAppStyle::GetSlateColor(TEXT("Colors.SelectHover")).GetSpecifiedColor();
    const FLinearColor SelectedIslandLineColor(1.0f, 0.45f, 0.08f, 1.0f);
    const bool         bOutlineOnly = DisplayMode == EWetClothingProfileUVDisplayMode::OutlineOnly;

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        WhiteBrush,
        ESlateDrawEffect::None,
        bOutlineOnly ? FLinearColor(0.01f, 0.01f, 0.01f, 1.0f) : FLinearColor(0.015f, 0.015f, 0.015f, 1.0f));

    const FBox2D UVBounds = ComputeUVBounds();
    if (!UVBounds.bIsValid)
    {
        return LayerId + 1;
    }

    const int32     TextureLayer = LayerId + 1;
    const int32     GridLayer = LayerId + 2;
    const int32     WireLayer = LayerId + 3;
    const int32     SelectedLayer = LayerId + 4;
    const int32     SelectionRectLayer = LayerId + 5;
    const FVector2D ClampedViewOffset = ClampViewOffset(AllottedGeometry, UVBounds, ZoomAmount, ViewOffset);
    OutDrawElements.PushClip(FSlateClippingZone(AllottedGeometry));

    if (BackgroundTextureBrush.GetResourceObject() != nullptr)
    {
        const FVector2D TopLeft = UVToLocal(FVector2D(0.0, 1.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset);
        const FVector2D BottomRight = UVToLocal(FVector2D(1.0, 0.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset);
        const FVector2D TexturePosition(
            FMath::Min(TopLeft.X, BottomRight.X),
            FMath::Min(TopLeft.Y, BottomRight.Y));
        const FVector2D TextureSize(
            FMath::Abs(BottomRight.X - TopLeft.X),
            FMath::Abs(BottomRight.Y - TopLeft.Y));

        FSlateDrawElement::MakeBox(
            OutDrawElements,
            TextureLayer,
            AllottedGeometry.ToPaintGeometry(TextureSize, FSlateLayoutTransform(TexturePosition)),
            &BackgroundTextureBrush,
            ESlateDrawEffect::None,
            FLinearColor(1.0f, 1.0f, 1.0f, 0.75f));
    }

    for (int32 GridLineIndex = 0; GridLineIndex <= 4; ++GridLineIndex)
    {
        const double T = GridLineIndex / 4.0;
        const double U = FMath::Lerp(UVBounds.Min.X, UVBounds.Max.X, T);
        const double V = FMath::Lerp(UVBounds.Min.Y, UVBounds.Max.Y, T);

        const TArray<FVector2D> VerticalLine = {
            UVToLocal(FVector2D(U, UVBounds.Min.Y), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
            UVToLocal(FVector2D(U, UVBounds.Max.Y), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset)
        };

        const TArray<FVector2D> HorizontalLine = {
            UVToLocal(FVector2D(UVBounds.Min.X, V), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
            UVToLocal(FVector2D(UVBounds.Max.X, V), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset)
        };

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            GridLayer,
            AllottedGeometry.ToPaintGeometry(),
            VerticalLine,
            ESlateDrawEffect::None,
            FLinearColor(0.12f, 0.12f, 0.12f, 0.85f),
            true,
            1.0f);

        FSlateDrawElement::MakeLines(
            OutDrawElements,
            GridLayer,
            AllottedGeometry.ToPaintGeometry(),
            HorizontalLine,
            ESlateDrawEffect::None,
            FLinearColor(0.12f, 0.12f, 0.12f, 0.85f),
            true,
            1.0f);
    }

    const TArray<FVector2D> BorderPoints = {
        UVToLocal(FVector2D(0.0, 0.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
        UVToLocal(FVector2D(1.0, 0.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
        UVToLocal(FVector2D(1.0, 1.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
        UVToLocal(FVector2D(0.0, 1.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
        UVToLocal(FVector2D(0.0, 0.0), AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset)
    };

    FSlateDrawElement::MakeLines(
        OutDrawElements,
        GridLayer,
        AllottedGeometry.ToPaintGeometry(),
        BorderPoints,
        ESlateDrawEffect::None,
        FLinearColor(0.25f, 0.25f, 0.25f, 1.0f),
        true,
        1.0f);

    for (const FWetClothingProfileUVIsland& Island : Islands)
    {
        const bool          bSelected = SelectedIslandIDs.Contains(Island.IslandID);
        const FLinearColor* AssignedColor = IslandColors.Find(Island.IslandID);
        const bool          bHasAssignedColor = AssignedColor != nullptr;
        const bool          bIsDefaultGrayOverlay = bHasAssignedColor && AssignedColor->A < 0.75f;
        const FLinearColor  LineColor = bSelected
                                            ? SelectedIslandLineColor
                                            : (bHasAssignedColor ? *AssignedColor : (bOutlineOnly ? FLinearColor(0.72f, 0.72f, 0.72f, 0.95f) : FLinearColor(0.45f, 0.45f, 0.45f, 0.25f)));
        const float         Thickness = bOutlineOnly
                                            ? (bSelected ? 2.3f : (bHasAssignedColor ? 1.7f : 1.45f))
                                            : (bSelected ? 1.15f : (bIsDefaultGrayOverlay ? 0.35f : (bHasAssignedColor ? 0.75f : 0.35f)));
        const int32         DrawLayer = bSelected ? SelectedLayer : WireLayer;

        if (bOutlineOnly)
        {
            const FSlateResourceHandle FillResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);
            if (FillResourceHandle.IsValid())
            {
                TArray<FSlateVertex> FillVerts;
                TArray<SlateIndex>   FillIndices;
                FillVerts.Reserve(Island.UVTriangles.Num() * 3);
                FillIndices.Reserve(Island.UVTriangles.Num() * 3);

                const FLinearColor FillLinearColor(LineColor.R, LineColor.G, LineColor.B, LineColor.A * 0.7f);
                const FColor       FillColor = FillLinearColor.ToFColor(true);

                for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
                {
                    const SlateIndex StartIndex = static_cast<SlateIndex>(FillVerts.Num());
                    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                    {
                        const FVector2D PaintedPoint = UVToLocal(Triangle.UVs[CornerIndex], AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset);
                        FillVerts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                            AllottedGeometry.GetAccumulatedRenderTransform(),
                            FVector2f(PaintedPoint),
                            FVector2f::ZeroVector,
                            FillColor));
                    }

                    FillIndices.Add(StartIndex);
                    FillIndices.Add(StartIndex + 1);
                    FillIndices.Add(StartIndex + 2);
                }

                FSlateDrawElement::MakeCustomVerts(
                    OutDrawElements,
                    DrawLayer,
                    FillResourceHandle,
                    FillVerts,
                    FillIndices,
                    nullptr,
                    0,
                    0,
                    ESlateDrawEffect::None);
            }

            TMap<FQuantizedUVEdge, int32>                       EdgeCounts;
            TMap<FQuantizedUVEdge, TPair<FVector2D, FVector2D>> EdgePoints;
            EdgeCounts.Reserve(Island.UVTriangles.Num() * 3);
            EdgePoints.Reserve(Island.UVTriangles.Num() * 3);

            for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
            {
                for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
                {
                    const int32            NextIndex = (EdgeIndex + 1) % 3;
                    const FVector2D        StartUV = Triangle.UVs[EdgeIndex];
                    const FVector2D        EndUV = Triangle.UVs[NextIndex];
                    const FQuantizedUVEdge EdgeKey = MakeCanonicalUVEdge(StartUV, EndUV);
                    EdgeCounts.FindOrAdd(EdgeKey)++;
                    if (!EdgePoints.Contains(EdgeKey))
                    {
                        EdgePoints.Add(EdgeKey, TPair<FVector2D, FVector2D>(StartUV, EndUV));
                    }
                }
            }

            for (const TPair<FQuantizedUVEdge, int32>& EdgeCountPair : EdgeCounts)
            {
                if (EdgeCountPair.Value != 1)
                {
                    continue;
                }

                const TPair<FVector2D, FVector2D>* UVEdge = EdgePoints.Find(EdgeCountPair.Key);
                if (UVEdge == nullptr)
                {
                    continue;
                }

                const TArray<FVector2D> EdgeLine = {
                    UVToLocal(UVEdge->Key, AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
                    UVToLocal(UVEdge->Value, AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset)
                };

                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    DrawLayer,
                    AllottedGeometry.ToPaintGeometry(),
                    EdgeLine,
                    ESlateDrawEffect::None,
                    FLinearColor(0.02f, 0.02f, 0.02f, 1.0f),
                    true,
                    Thickness + 1.2f);

                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    DrawLayer + 1,
                    AllottedGeometry.ToPaintGeometry(),
                    EdgeLine,
                    ESlateDrawEffect::None,
                    LineColor,
                    true,
                    Thickness);
            }
        }
        else
        {
            for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
            {
                const TArray<FVector2D> TrianglePoints = {
                    UVToLocal(Triangle.UVs[0], AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
                    UVToLocal(Triangle.UVs[1], AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
                    UVToLocal(Triangle.UVs[2], AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset),
                    UVToLocal(Triangle.UVs[0], AllottedGeometry, UVBounds, ZoomAmount, ClampedViewOffset)
                };

                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    DrawLayer,
                    AllottedGeometry.ToPaintGeometry(),
                    TrianglePoints,
                    ESlateDrawEffect::None,
                    LineColor,
                    true,
                    Thickness);
            }
        }
    }

    if (bIsDraggingSelectionShape)
    {
        const FVector2D    RectMin(FMath::Min(SelectionDragStartLocal.X, SelectionDragCurrentLocal.X), FMath::Min(SelectionDragStartLocal.Y, SelectionDragCurrentLocal.Y));
        const FVector2D    RectMax(FMath::Max(SelectionDragStartLocal.X, SelectionDragCurrentLocal.X), FMath::Max(SelectionDragStartLocal.Y, SelectionDragCurrentLocal.Y));
        const FLinearColor SelectionFillColor(SelectColor.R, SelectColor.G, SelectColor.B, 0.12f);
        const FLinearColor SelectionLineColor(SelectColor.R, SelectColor.G, SelectColor.B, 0.9f);

        if (SelectionTool == EWetClothingProfileUVSelectionTool::BoxSelect)
        {
            const FVector2D RectPos = RectMin;
            const FVector2D RectSize = RectMax - RectMin;

            FSlateDrawElement::MakeBox(
                OutDrawElements,
                SelectionRectLayer,
                AllottedGeometry.ToPaintGeometry(RectSize, FSlateLayoutTransform(RectPos)),
                WhiteBrush,
                ESlateDrawEffect::None,
                SelectionFillColor);

            const TArray<FVector2D> RectPoints = {
                RectMin,
                FVector2D(RectMax.X, RectMin.Y),
                RectMax,
                FVector2D(RectMin.X, RectMax.Y),
                RectMin
            };

            FSlateDrawElement::MakeLines(
                OutDrawElements,
                SelectionRectLayer + 1,
                AllottedGeometry.ToPaintGeometry(),
                RectPoints,
                ESlateDrawEffect::None,
                SelectionLineColor,
                true,
                1.5f);
        }
        else if (SelectionTool == EWetClothingProfileUVSelectionTool::EllipseSelect)
        {
            TArray<FVector2D> EllipsePoints;
            const FVector2D   Center = (RectMin + RectMax) * 0.5f;
            const FVector2D   Radii = (RectMax - RectMin) * 0.5f;
            const int32       SampleCount = 48;
            EllipsePoints.Reserve(SampleCount + 1);

            for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
            {
                const float Angle = static_cast<float>(SampleIndex) / static_cast<float>(SampleCount) * 2.0f * PI;
                EllipsePoints.Add(FVector2D(
                    Center.X + FMath::Cos(Angle) * Radii.X,
                    Center.Y + FMath::Sin(Angle) * Radii.Y));
            }

            FSlateDrawElement::MakeLines(
                OutDrawElements,
                SelectionRectLayer + 1,
                AllottedGeometry.ToPaintGeometry(),
                EllipsePoints,
                ESlateDrawEffect::None,
                SelectionLineColor,
                true,
                1.5f);
        }
        else if (SelectionTool == EWetClothingProfileUVSelectionTool::LassoSelect)
        {
            TArray<FVector2D> LassoPoints = SelectionLassoPointsLocal;
            if (LassoPoints.Num() == 0 || !LassoPoints.Last().Equals(SelectionDragCurrentLocal, 0.5f))
            {
                LassoPoints.Add(SelectionDragCurrentLocal);
            }

            if (LassoPoints.Num() > 1)
            {
                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    SelectionRectLayer + 1,
                    AllottedGeometry.ToPaintGeometry(),
                    LassoPoints,
                    ESlateDrawEffect::None,
                    SelectionLineColor,
                    true,
                    1.5f);
            }
        }
    }

    OutDrawElements.PopClip();
    return SelectionRectLayer + 2;
}

FReply SWetClothingProfileUVView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
    {
        bIsPanning = true;
        LastPanLocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }

    const FBox2D UVBounds = ComputeUVBounds();
    if (!UVBounds.bIsValid)
    {
        return FReply::Unhandled();
    }

    const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

    if (UsesDragSelectionTool())
    {
        bIsDraggingSelectionShape = true;
        SelectionDragStartLocal = LocalPosition;
        SelectionDragCurrentLocal = LocalPosition;
        SelectionLassoPointsLocal.Reset();
        SelectionLassoPointsLocal.Add(LocalPosition);
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }

    const FVector2D ClickedUV = LocalToUV(
        LocalPosition,
        MyGeometry,
        UVBounds,
        ZoomAmount,
        ClampViewOffset(MyGeometry, UVBounds, ZoomAmount, ViewOffset));

    const int32                            HitIslandID = HitTestIslandAtUV(ClickedUV);
    const EWetClothingProfileUVSelectionOp SelectionOp = MouseEvent.IsShiftDown()
                                                             ? EWetClothingProfileUVSelectionOp::Add
                                                             : EWetClothingProfileUVSelectionOp::Replace;

    if (HitIslandID != INDEX_NONE)
    {
        BroadcastSelection({ HitIslandID }, SelectionOp);
    }
    else if (SelectionOp == EWetClothingProfileUVSelectionOp::Replace)
    {
        BroadcastSelection({}, EWetClothingProfileUVSelectionOp::Replace);
    }

    return FReply::Handled();
}

FReply SWetClothingProfileUVView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (bIsPanning && (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton))
    {
        bIsPanning = false;
        return FReply::Handled().ReleaseMouseCapture();
    }

    if (bIsDraggingSelectionShape && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
    {
        SelectionDragCurrentLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

        const FBox2D UVBounds = ComputeUVBounds();
        if (UVBounds.bIsValid)
        {
            const FVector2D ClampedOffset = ClampViewOffset(MyGeometry, UVBounds, ZoomAmount, ViewOffset);
            TArray<int32>   HitIslandIDs;
            if (SelectionTool == EWetClothingProfileUVSelectionTool::BoxSelect)
            {
                const FVector2D StartUV = LocalToUV(SelectionDragStartLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset);
                const FVector2D EndUV = LocalToUV(SelectionDragCurrentLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset);
                FBox2D          RectUV(ForceInit);
                RectUV += StartUV;
                RectUV += EndUV;

                for (const FWetClothingProfileUVIsland& Island : Islands)
                {
                    if (IsIslandIntersectingRect(Island, RectUV))
                    {
                        HitIslandIDs.Add(Island.IslandID);
                    }
                }
            }
            else if (SelectionTool == EWetClothingProfileUVSelectionTool::EllipseSelect)
            {
                const FVector2D StartUV = LocalToUV(SelectionDragStartLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset);
                const FVector2D EndUV = LocalToUV(SelectionDragCurrentLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset);
                FBox2D          RectUV(ForceInit);
                RectUV += StartUV;
                RectUV += EndUV;

                for (const FWetClothingProfileUVIsland& Island : Islands)
                {
                    if (IsIslandIntersectingEllipse(Island, RectUV))
                    {
                        HitIslandIDs.Add(Island.IslandID);
                    }
                }
            }
            else if (SelectionTool == EWetClothingProfileUVSelectionTool::LassoSelect)
            {
                TArray<FVector2D> PolygonUV;
                PolygonUV.Reserve(SelectionLassoPointsLocal.Num() + 1);
                const FVector2D FinalCurrentUV = LocalToUV(SelectionDragCurrentLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset);

                for (const FVector2D& PointLocal : SelectionLassoPointsLocal)
                {
                    PolygonUV.Add(LocalToUV(PointLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOffset));
                }

                if (PolygonUV.Num() == 0 || !PolygonUV.Last().Equals(FinalCurrentUV, KINDA_SMALL_NUMBER))
                {
                    PolygonUV.Add(FinalCurrentUV);
                }

                if (PolygonUV.Num() >= 3)
                {
                    for (const FWetClothingProfileUVIsland& Island : Islands)
                    {
                        if (IsIslandIntersectingPolygon(Island, PolygonUV))
                        {
                            HitIslandIDs.Add(Island.IslandID);
                        }
                    }
                }
            }

            BroadcastSelection(HitIslandIDs, MouseEvent.IsShiftDown() ? EWetClothingProfileUVSelectionOp::Add : EWetClothingProfileUVSelectionOp::Replace);
        }

        ResetSelectionInteractionState();
        Invalidate(EInvalidateWidget::Paint);
        return FReply::Handled().ReleaseMouseCapture();
    }

    return FReply::Unhandled();
}

FReply SWetClothingProfileUVView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (bIsDraggingSelectionShape)
    {
        SelectionDragCurrentLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

        if (SelectionTool == EWetClothingProfileUVSelectionTool::LassoSelect)
        {
            if (SelectionLassoPointsLocal.Num() == 0 || !SelectionLassoPointsLocal.Last().Equals(SelectionDragCurrentLocal, 1.5f))
            {
                SelectionLassoPointsLocal.Add(SelectionDragCurrentLocal);
            }
        }

        Invalidate(EInvalidateWidget::Paint);
        return FReply::Handled();
    }

    if (!bIsPanning)
    {
        return FReply::Unhandled();
    }

    const FVector2D LocalPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    ViewOffset += LocalPosition - LastPanLocalPosition;
    ViewOffset = ClampViewOffset(MyGeometry, ComputeUVBounds(), ZoomAmount, ViewOffset);
    LastPanLocalPosition = LocalPosition;
    Invalidate(EInvalidateWidget::Paint);
    return FReply::Handled();
}

FReply SWetClothingProfileUVView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    const FBox2D UVBounds = ComputeUVBounds();
    if (!UVBounds.bIsValid)
    {
        return FReply::Unhandled();
    }

    const FVector2D CursorLocal = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    const FVector2D ClampedOldOffset = ClampViewOffset(MyGeometry, UVBounds, ZoomAmount, ViewOffset);
    const FVector2D CursorUV = LocalToUV(CursorLocal, MyGeometry, UVBounds, ZoomAmount, ClampedOldOffset);

    const double    OldZoom = ZoomAmount;
    const FVector2D OldOffset = ClampedOldOffset;
    ViewOffset = ClampedOldOffset;

    const double ZoomStep = MouseEvent.GetWheelDelta() > 0.0f ? 1.15 : 1.0 / 1.15;
    ZoomAmount = FMath::Clamp(ZoomAmount * ZoomStep, 0.25, 16.0);

    const FVector2D CursorLocalAfterZoom = UVToLocal(CursorUV, MyGeometry, UVBounds, ZoomAmount, ViewOffset);
    ViewOffset += CursorLocal - CursorLocalAfterZoom;
    ViewOffset = ClampViewOffset(MyGeometry, UVBounds, ZoomAmount, ViewOffset);

    if (FMath::IsNearlyEqual(ZoomAmount, OldZoom) && ViewOffset.Equals(OldOffset))
    {
        return FReply::Unhandled();
    }

    Invalidate(EInvalidateWidget::Paint);
    return FReply::Handled();
}

FBox2D SWetClothingProfileUVView::ComputeUVBounds() const
{
    FBox2D Bounds(ForceInit);

    if (!BackgroundTexture.IsValid())
    {
        Bounds += FVector2D(0.0f, 0.0f);
        Bounds += FVector2D(1.0f, 1.0f);
    }

    for (const FWetClothingProfileUVIsland& Island : Islands)
    {
        for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
        {
            Bounds += Triangle.UVs[0];
            Bounds += Triangle.UVs[1];
            Bounds += Triangle.UVs[2];
        }
    }

    if (!Bounds.bIsValid)
    {
        return Bounds;
    }

    if (FMath::IsNearlyEqual(Bounds.Min.X, Bounds.Max.X))
    {
        Bounds.Min.X -= 0.5;
        Bounds.Max.X += 0.5;
    }

    if (FMath::IsNearlyEqual(Bounds.Min.Y, Bounds.Max.Y))
    {
        Bounds.Min.Y -= 0.5;
        Bounds.Max.Y += 0.5;
    }

    const FVector2D BoundsSize = Bounds.GetSize();
    const FVector2D Margin(
        FMath::Max(BoundsSize.X * 0.05, 0.01),
        FMath::Max(BoundsSize.Y * 0.05, 0.01));

    Bounds.Min -= Margin;
    Bounds.Max += Margin;

    return Bounds;
}

FVector2D SWetClothingProfileUVView::UVToLocal(
    const FVector2D& UV,
    const FGeometry& Geometry,
    const FBox2D&    UVBounds) const
{
    return UVToLocal(UV, Geometry, UVBounds, ZoomAmount, ViewOffset);
}

FVector2D SWetClothingProfileUVView::UVToLocal(
    const FVector2D& UV,
    const FGeometry& Geometry,
    const FBox2D&    UVBounds,
    double           InZoomAmount,
    const FVector2D& InViewOffset) const
{
    const FVector2D LocalSize = Geometry.GetLocalSize();
    const double    Width = FMath::Max(0.0001, UVBounds.Max.X - UVBounds.Min.X);
    const double    Height = FMath::Max(0.0001, UVBounds.Max.Y - UVBounds.Min.Y);
    const double    AvailableWidth = FMath::Max(1.0, LocalSize.X - Padding * 2.0);
    const double    AvailableHeight = FMath::Max(1.0, LocalSize.Y - Padding * 2.0);
    const double    Scale = FMath::Min(AvailableWidth / Width, AvailableHeight / Height) * InZoomAmount;
    const double    DrawWidth = Width * Scale;
    const double    DrawHeight = Height * Scale;
    const double    OffsetX = (LocalSize.X - DrawWidth) * 0.5 + InViewOffset.X;
    const double    OffsetY = (LocalSize.Y - DrawHeight) * 0.5 + InViewOffset.Y;

    return FVector2D(
        OffsetX + (UV.X - UVBounds.Min.X) * Scale,
        OffsetY + (UVBounds.Max.Y - UV.Y) * Scale);
}

FVector2D SWetClothingProfileUVView::LocalToUV(
    const FVector2D& LocalPosition,
    const FGeometry& Geometry,
    const FBox2D&    UVBounds) const
{
    return LocalToUV(LocalPosition, Geometry, UVBounds, ZoomAmount, ViewOffset);
}

FVector2D SWetClothingProfileUVView::LocalToUV(
    const FVector2D& LocalPosition,
    const FGeometry& Geometry,
    const FBox2D&    UVBounds,
    double           InZoomAmount,
    const FVector2D& InViewOffset) const
{
    const FVector2D LocalSize = Geometry.GetLocalSize();
    const double    Width = FMath::Max(0.0001, UVBounds.Max.X - UVBounds.Min.X);
    const double    Height = FMath::Max(0.0001, UVBounds.Max.Y - UVBounds.Min.Y);
    const double    AvailableWidth = FMath::Max(1.0, LocalSize.X - Padding * 2.0);
    const double    AvailableHeight = FMath::Max(1.0, LocalSize.Y - Padding * 2.0);
    const double    Scale = FMath::Min(AvailableWidth / Width, AvailableHeight / Height) * InZoomAmount;
    const double    DrawWidth = Width * Scale;
    const double    DrawHeight = Height * Scale;
    const double    OffsetX = (LocalSize.X - DrawWidth) * 0.5 + InViewOffset.X;
    const double    OffsetY = (LocalSize.Y - DrawHeight) * 0.5 + InViewOffset.Y;

    return FVector2D(
        UVBounds.Min.X + (LocalPosition.X - OffsetX) / Scale,
        UVBounds.Max.Y - (LocalPosition.Y - OffsetY) / Scale);
}

FVector2D SWetClothingProfileUVView::ClampViewOffset(
    const FGeometry& Geometry,
    const FBox2D&    UVBounds,
    double           InZoomAmount,
    const FVector2D& InViewOffset) const
{
    if (!UVBounds.bIsValid)
    {
        return FVector2D::ZeroVector;
    }

    const FVector2D LocalSize = Geometry.GetLocalSize();
    const double    Width = FMath::Max(0.0001, UVBounds.Max.X - UVBounds.Min.X);
    const double    Height = FMath::Max(0.0001, UVBounds.Max.Y - UVBounds.Min.Y);
    const double    AvailableWidth = FMath::Max(1.0, LocalSize.X - Padding * 2.0);
    const double    AvailableHeight = FMath::Max(1.0, LocalSize.Y - Padding * 2.0);
    const double    Scale = FMath::Min(AvailableWidth / Width, AvailableHeight / Height) * InZoomAmount;
    const double    DrawWidth = Width * Scale;
    const double    DrawHeight = Height * Scale;
    const double    CenteredOffsetX = (LocalSize.X - DrawWidth) * 0.5;
    const double    CenteredOffsetY = (LocalSize.Y - DrawHeight) * 0.5;

    FVector2D Result = InViewOffset;

    if (DrawWidth <= AvailableWidth)
    {
        Result.X = 0.0f;
    }
    else
    {
        const double MinOffsetX = (LocalSize.X - Padding) - (CenteredOffsetX + DrawWidth);
        const double MaxOffsetX = Padding - CenteredOffsetX;
        Result.X = FMath::Clamp(Result.X, MinOffsetX, MaxOffsetX);
    }

    if (DrawHeight <= AvailableHeight)
    {
        Result.Y = 0.0f;
    }
    else
    {
        const double MinOffsetY = (LocalSize.Y - Padding) - (CenteredOffsetY + DrawHeight);
        const double MaxOffsetY = Padding - CenteredOffsetY;
        Result.Y = FMath::Clamp(Result.Y, MinOffsetY, MaxOffsetY);
    }

    return Result;
}

bool SWetClothingProfileUVView::UsesDragSelectionTool() const
{
    return SelectionTool == EWetClothingProfileUVSelectionTool::BoxSelect || SelectionTool == EWetClothingProfileUVSelectionTool::EllipseSelect || SelectionTool == EWetClothingProfileUVSelectionTool::LassoSelect;
}

void SWetClothingProfileUVView::ResetView()
{
    ZoomAmount = 1.0;
    ViewOffset = FVector2D::ZeroVector;
    bIsPanning = false;
    LastPanLocalPosition = FVector2D::ZeroVector;
    ResetSelectionInteractionState();
}

void SWetClothingProfileUVView::ResetSelectionInteractionState()
{
    bIsDraggingSelectionShape = false;
    SelectionDragStartLocal = FVector2D::ZeroVector;
    SelectionDragCurrentLocal = FVector2D::ZeroVector;
    SelectionLassoPointsLocal.Reset();
}

void SWetClothingProfileUVView::BroadcastSelection(const TArray<int32>& IslandIDs, EWetClothingProfileUVSelectionOp SelectionOp) const
{
    if (OnIslandSelectionChanged.IsBound())
    {
        OnIslandSelectionChanged.Execute(IslandIDs, SelectionOp);
    }
}

int32 SWetClothingProfileUVView::HitTestIslandAtUV(const FVector2D& UV) const
{
    for (const FWetClothingProfileUVIsland& Island : Islands)
    {
        for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
        {
            if (IsPointInTriangle(UV, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]))
            {
                return Island.IslandID;
            }
        }
    }

    return INDEX_NONE;
}

bool SWetClothingProfileUVView::IsIslandIntersectingRect(const FWetClothingProfileUVIsland& Island, const FBox2D& RectUV) const
{
    if (!Island.UVBounds.bIsValid || !RectUV.bIsValid)
    {
        return false;
    }

    if (!Island.UVBounds.Intersect(RectUV))
    {
        return false;
    }

    for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
    {
        if (RectUV.IsInsideOrOn(Triangle.UVs[0]) || RectUV.IsInsideOrOn(Triangle.UVs[1]) || RectUV.IsInsideOrOn(Triangle.UVs[2]))
        {
            return true;
        }

        const FVector2D Center = (Triangle.UVs[0] + Triangle.UVs[1] + Triangle.UVs[2]) / 3.0f;
        if (RectUV.IsInsideOrOn(Center))
        {
            return true;
        }
    }

    return true;
}

bool SWetClothingProfileUVView::IsIslandIntersectingEllipse(const FWetClothingProfileUVIsland& Island, const FBox2D& RectUV) const
{
    if (!Island.UVBounds.bIsValid || !RectUV.bIsValid || !Island.UVBounds.Intersect(RectUV))
    {
        return false;
    }

    const FVector2D EllipseCenter = (RectUV.Min + RectUV.Max) * 0.5f;
    const FVector2D EllipseRadii = (RectUV.Max - RectUV.Min) * 0.5f;

    for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
    {
        if (IsTriangleIntersectingEllipse(Triangle, EllipseCenter, EllipseRadii))
        {
            return true;
        }
    }

    return false;
}

bool SWetClothingProfileUVView::IsIslandIntersectingPolygon(const FWetClothingProfileUVIsland& Island, const TArray<FVector2D>& PolygonUV) const
{
    if (!Island.UVBounds.bIsValid || PolygonUV.Num() < 3)
    {
        return false;
    }

    FBox2D PolygonBounds(ForceInit);
    for (const FVector2D& Point : PolygonUV)
    {
        PolygonBounds += Point;
    }

    if (!PolygonBounds.bIsValid || !Island.UVBounds.Intersect(PolygonBounds))
    {
        return false;
    }

    for (const FWetClothingProfileUVTriangle& Triangle : Island.UVTriangles)
    {
        if (IsTriangleIntersectingPolygon(Triangle, PolygonUV))
        {
            return true;
        }
    }

    return false;
}

bool SWetClothingProfileUVView::IsTriangleIntersectingEllipse(
    const FWetClothingProfileUVTriangle& Triangle,
    const FVector2D&                     EllipseCenter,
    const FVector2D&                     EllipseRadii) const
{
    for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
    {
        if (IsPointInEllipse(Triangle.UVs[VertexIndex], EllipseCenter, EllipseRadii))
        {
            return true;
        }
    }

    if (IsPointInTriangle(EllipseCenter, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]))
    {
        return true;
    }

    return DoesSegmentIntersectEllipse(Triangle.UVs[0], Triangle.UVs[1], EllipseCenter, EllipseRadii) || DoesSegmentIntersectEllipse(Triangle.UVs[1], Triangle.UVs[2], EllipseCenter, EllipseRadii) || DoesSegmentIntersectEllipse(Triangle.UVs[2], Triangle.UVs[0], EllipseCenter, EllipseRadii);
}

bool SWetClothingProfileUVView::IsTriangleIntersectingPolygon(
    const FWetClothingProfileUVTriangle& Triangle,
    const TArray<FVector2D>&             PolygonUV) const
{
    for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
    {
        if (IsPointInPolygon(Triangle.UVs[VertexIndex], PolygonUV))
        {
            return true;
        }
    }

    for (const FVector2D& PolygonPoint : PolygonUV)
    {
        if (IsPointInTriangle(PolygonPoint, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]))
        {
            return true;
        }
    }

    const FVector2D TriangleEdges[3][2] = {
        { Triangle.UVs[0], Triangle.UVs[1] },
        { Triangle.UVs[1], Triangle.UVs[2] },
        { Triangle.UVs[2], Triangle.UVs[0] }
    };

    for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
    {
        for (int32 PolygonIndex = 0; PolygonIndex < PolygonUV.Num(); ++PolygonIndex)
        {
            const FVector2D& PolygonStart = PolygonUV[PolygonIndex];
            const FVector2D& PolygonEnd = PolygonUV[(PolygonIndex + 1) % PolygonUV.Num()];
            if (DoLineSegmentsIntersect(TriangleEdges[EdgeIndex][0], TriangleEdges[EdgeIndex][1], PolygonStart, PolygonEnd))
            {
                return true;
            }
        }
    }

    return false;
}

bool SWetClothingProfileUVView::IsPointInTriangle(
    const FVector2D& Point,
    const FVector2D& A,
    const FVector2D& B,
    const FVector2D& C) const
{
    const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
    {
        return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
    };

    const double D1 = Sign(Point, A, B);
    const double D2 = Sign(Point, B, C);
    const double D3 = Sign(Point, C, A);
    const bool   bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
    const bool   bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
    return !(bHasNegative && bHasPositive);
}

bool SWetClothingProfileUVView::IsPointInEllipse(
    const FVector2D& Point,
    const FVector2D& EllipseCenter,
    const FVector2D& EllipseRadii) const
{
    const double RadiusX = FMath::Max(FMath::Abs(EllipseRadii.X), static_cast<double>(KINDA_SMALL_NUMBER));
    const double RadiusY = FMath::Max(FMath::Abs(EllipseRadii.Y), static_cast<double>(KINDA_SMALL_NUMBER));
    const double NormalizedX = (Point.X - EllipseCenter.X) / RadiusX;
    const double NormalizedY = (Point.Y - EllipseCenter.Y) / RadiusY;
    return (NormalizedX * NormalizedX + NormalizedY * NormalizedY) <= 1.0;
}

bool SWetClothingProfileUVView::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon) const
{
    if (Polygon.Num() < 3)
    {
        return false;
    }

    bool bInside = false;
    for (int32 CurrentIndex = 0, PreviousIndex = Polygon.Num() - 1; CurrentIndex < Polygon.Num(); PreviousIndex = CurrentIndex++)
    {
        const FVector2D& Current = Polygon[CurrentIndex];
        const FVector2D& Previous = Polygon[PreviousIndex];

        const bool bCrossesHorizontalRay = ((Current.Y > Point.Y) != (Previous.Y > Point.Y)) && (Point.X <= ((Previous.X - Current.X) * (Point.Y - Current.Y) / (Previous.Y - Current.Y)) + Current.X);

        if (bCrossesHorizontalRay)
        {
            bInside = !bInside;
        }
    }

    return bInside;
}

bool SWetClothingProfileUVView::DoesSegmentIntersectEllipse(
    const FVector2D& SegmentStart,
    const FVector2D& SegmentEnd,
    const FVector2D& EllipseCenter,
    const FVector2D& EllipseRadii) const
{
    const FVector2D SafeRadii(
        FMath::Max(FMath::Abs(EllipseRadii.X), KINDA_SMALL_NUMBER),
        FMath::Max(FMath::Abs(EllipseRadii.Y), KINDA_SMALL_NUMBER));

    const FVector2D Start((SegmentStart.X - EllipseCenter.X) / SafeRadii.X, (SegmentStart.Y - EllipseCenter.Y) / SafeRadii.Y);
    const FVector2D End((SegmentEnd.X - EllipseCenter.X) / SafeRadii.X, (SegmentEnd.Y - EllipseCenter.Y) / SafeRadii.Y);
    const FVector2D Direction = End - Start;

    const double A = Direction.SizeSquared();
    const double B = 2.0 * FVector2D::DotProduct(Start, Direction);
    const double C = Start.SizeSquared() - 1.0;

    if (C <= 0.0)
    {
        return true;
    }

    if (A <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const double Discriminant = B * B - 4.0 * A * C;
    if (Discriminant < 0.0)
    {
        return false;
    }

    const double SqrtDiscriminant = FMath::Sqrt(Discriminant);
    const double InverseDenominator = 0.5 / A;
    const double T0 = (-B - SqrtDiscriminant) * InverseDenominator;
    const double T1 = (-B + SqrtDiscriminant) * InverseDenominator;
    return (T0 >= 0.0 && T0 <= 1.0) || (T1 >= 0.0 && T1 <= 1.0);
}

bool SWetClothingProfileUVView::DoLineSegmentsIntersect(
    const FVector2D& AStart,
    const FVector2D& AEnd,
    const FVector2D& BStart,
    const FVector2D& BEnd) const
{
    const auto Cross = [](const FVector2D& P0, const FVector2D& P1, const FVector2D& P2)
    {
        return (P1.X - P0.X) * (P2.Y - P0.Y) - (P1.Y - P0.Y) * (P2.X - P0.X);
    };

    const auto IsOnSegment = [](const FVector2D& Point, const FVector2D& SegmentStart, const FVector2D& SegmentEnd)
    {
        return Point.X >= FMath::Min(SegmentStart.X, SegmentEnd.X) - KINDA_SMALL_NUMBER && Point.X <= FMath::Max(SegmentStart.X, SegmentEnd.X) + KINDA_SMALL_NUMBER && Point.Y >= FMath::Min(SegmentStart.Y, SegmentEnd.Y) - KINDA_SMALL_NUMBER && Point.Y <= FMath::Max(SegmentStart.Y, SegmentEnd.Y) + KINDA_SMALL_NUMBER;
    };

    const double D1 = Cross(AStart, AEnd, BStart);
    const double D2 = Cross(AStart, AEnd, BEnd);
    const double D3 = Cross(BStart, BEnd, AStart);
    const double D4 = Cross(BStart, BEnd, AEnd);

    if (((D1 > 0.0 && D2 < 0.0) || (D1 < 0.0 && D2 > 0.0)) && ((D3 > 0.0 && D4 < 0.0) || (D3 < 0.0 && D4 > 0.0)))
    {
        return true;
    }

    if (FMath::IsNearlyZero(D1) && IsOnSegment(BStart, AStart, AEnd))
    {
        return true;
    }

    if (FMath::IsNearlyZero(D2) && IsOnSegment(BEnd, AStart, AEnd))
    {
        return true;
    }

    if (FMath::IsNearlyZero(D3) && IsOnSegment(AStart, BStart, BEnd))
    {
        return true;
    }

    if (FMath::IsNearlyZero(D4) && IsOnSegment(AEnd, BStart, BEnd))
    {
        return true;
    }

    return false;
}
