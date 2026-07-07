#include "WetWrinkle/Widgets/SWetWrinkleTexturePreview.h"

#include "DataAssets/WetWrinkleAsset.h"
#include "Engine/Texture.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"

namespace
{
    constexpr int32 WetWrinkleTextureCircleSegments = 64;

    FLinearColor MakeWetWrinkleTextureStrokeColor(const FWetWrinkleStroke& Stroke, bool bSelected)
    {
        if (bSelected)
        {
            return FLinearColor(1.0f, 0.78f, 0.1f, 1.0f);
        }

        return Stroke.bEnabled ? FLinearColor(0.12f, 0.82f, 1.0f, 1.0f)
                               : FLinearColor(0.25f, 0.25f, 0.25f, 0.85f);
    }

    float WrapWetWrinklePreviewUV(float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    FVector2D WrapWetWrinklePreviewUV(const FVector2D& UV)
    {
        return FVector2D(WrapWetWrinklePreviewUV(UV.X), WrapWetWrinklePreviewUV(UV.Y));
    }
}

void SWetWrinkleTexturePreview::Construct(const FArguments& InArgs)
{
    OnUVHovered = InArgs._OnUVHovered;
    OnUVHoverEnded = InArgs._OnUVHoverEnded;
    OnPaintStrokeStarted = InArgs._OnPaintStrokeStarted;
    OnPaintStampRequested = InArgs._OnPaintStampRequested;
    OnPaintStrokeEnded = InArgs._OnPaintStrokeEnded;
    TextureBrush.DrawAs = ESlateBrushDrawType::Image;
    TextureBrush.Tiling = ESlateBrushTileType::NoTile;
}

void SWetWrinkleTexturePreview::SetPreviewContext(
    UWetWrinkleAsset* InWetWrinkleAsset,
    UTexture* InSourceTexture,
    int32 InMaterialSlotIndex,
    int32 InUVChannelIndex,
    const FWetWrinkleBrushSettings& InBrushSettings,
    const FWetWrinkleSurfaceHit& InSurfaceHit)
{
    WetWrinkleAsset = InWetWrinkleAsset;
    SourceTexture = InSourceTexture;
    MaterialSlotIndex = InMaterialSlotIndex;
    UVChannelIndex = InUVChannelIndex;
    BrushSettings = InBrushSettings;
    SurfaceHit = InSurfaceHit;

    TextureBrush.SetResourceObject(InSourceTexture);
    if (InSourceTexture != nullptr)
    {
        TextureBrush.ImageSize = FVector2D(InSourceTexture->GetSurfaceWidth(), InSourceTexture->GetSurfaceHeight());
    }
    else
    {
        TextureBrush.ImageSize = FVector2D(256.0f, 256.0f);
    }

    Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SWetWrinkleTexturePreview::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
    return FVector2D(256.0f, 256.0f);
}

int32 SWetWrinkleTexturePreview::OnPaint(
    const FPaintArgs& Args,
    const FGeometry& AllottedGeometry,
    const FSlateRect& MyCullingRect,
    FSlateWindowElementList& OutDrawElements,
    int32 LayerId,
    const FWidgetStyle& InWidgetStyle,
    bool bParentEnabled) const
{
    const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
    const FSlateRect TextureRect = ComputeTextureRect(LocalSize);

    FSlateDrawElement::MakeBox(
        OutDrawElements,
        LayerId,
        AllottedGeometry.ToPaintGeometry(),
        FAppStyle::Get().GetBrush("Brushes.Recessed"),
        ESlateDrawEffect::None,
        FLinearColor(0.025f, 0.025f, 0.025f, 1.0f));

    const FVector2D TexturePosition(TextureRect.Left, TextureRect.Top);
    const FVector2D TextureSize(TextureRect.Right - TextureRect.Left, TextureRect.Bottom - TextureRect.Top);
    if (SourceTexture.IsValid())
    {
        FSlateDrawElement::MakeBox(
            OutDrawElements,
            LayerId + 1,
            AllottedGeometry.ToPaintGeometry(TextureSize, FSlateLayoutTransform(TexturePosition)),
            &TextureBrush,
            ESlateDrawEffect::None,
            InWidgetStyle.GetColorAndOpacityTint());
    }
    else
    {
        FSlateDrawElement::MakeText(
            OutDrawElements,
            LayerId + 1,
            AllottedGeometry.ToPaintGeometry(TextureSize, FSlateLayoutTransform(TexturePosition + FVector2D(8.0f, 8.0f))),
            FText::FromString(TEXT("No texture under cursor")),
            FAppStyle::Get().GetFontStyle("SmallFont"),
            ESlateDrawEffect::None,
            FLinearColor(0.65f, 0.65f, 0.65f, 1.0f));
    }

    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const UTexture* Texture = SourceTexture.Get();
    if (Asset != nullptr && Texture != nullptr)
    {
        for (const FWetWrinkleStroke& Stroke : Asset->Strokes)
        {
            const FLinearColor StrokeColor = MakeWetWrinkleTextureStrokeColor(Stroke, false);
            for (const FWetWrinkleStamp& Stamp : Stroke.Stamps)
            {
                if (Stamp.SourceTexture != Texture ||
                    Stamp.MaterialSlotIndex != MaterialSlotIndex ||
                    Stamp.UVChannelIndex != UVChannelIndex)
                {
                    continue;
                }

                TArray<FVector2D> CirclePoints;
                AppendCirclePoints(TextureRect, Stamp.PositionUV, Stamp.BrushRadiusUV, CirclePoints);
                FSlateDrawElement::MakeLines(
                    OutDrawElements,
                    LayerId + 2,
                    AllottedGeometry.ToPaintGeometry(),
                    CirclePoints,
                    ESlateDrawEffect::None,
                    StrokeColor,
                    true,
                    1.0f);
            }
        }
    }

    const bool bUseHoveredUV = bHasHoveredUV;
    const bool bUseSurfaceHitUV = !bUseHoveredUV &&
                                  SurfaceHit.bHit &&
                                  SurfaceHit.MaterialSlotIndex == MaterialSlotIndex &&
                                  SurfaceHit.UVChannelIndex == UVChannelIndex;
    if (bUseHoveredUV || bUseSurfaceHitUV)
    {
        TArray<FVector2D> BrushPoints;
        AppendCirclePoints(TextureRect, bUseHoveredUV ? HoveredUV : SurfaceHit.UV, BrushSettings.BrushRadiusUV, BrushPoints);
        FSlateDrawElement::MakeLines(
            OutDrawElements,
            LayerId + 3,
            AllottedGeometry.ToPaintGeometry(),
            BrushPoints,
            ESlateDrawEffect::None,
            FLinearColor(1.0f, 1.0f, 1.0f, 1.0f),
            true,
            2.0f);
    }

    return LayerId + 3;
}

FReply SWetWrinkleTexturePreview::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!OnUVHovered.IsBound() && !OnPaintStampRequested.IsBound())
    {
        return FReply::Unhandled();
    }

    FVector2D UV = FVector2D::ZeroVector;
    if (TryGetUVFromLocalPosition(MyGeometry, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), UV))
    {
        HoveredUV = UV;
        bHasHoveredUV = true;
        Invalidate(EInvalidateWidgetReason::Paint);
        if (OnUVHovered.IsBound())
        {
            OnUVHovered.Execute(UV);
        }
        if (bIsPainting && OnPaintStampRequested.IsBound())
        {
            OnPaintStampRequested.Execute(UV);
        }
        return FReply::Handled();
    }

    if (OnUVHoverEnded.IsBound())
    {
        OnUVHoverEnded.Execute();
    }
    bHasHoveredUV = false;
    Invalidate(EInvalidateWidgetReason::Paint);
    return FReply::Unhandled();
}

FReply SWetWrinkleTexturePreview::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!OnPaintStrokeStarted.IsBound())
    {
        return FReply::Unhandled();
    }

    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return FReply::Unhandled();
    }

    FVector2D UV = FVector2D::ZeroVector;
    if (!TryGetUVFromLocalPosition(MyGeometry, MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()), UV))
    {
        return FReply::Unhandled();
    }

    HoveredUV = UV;
    bHasHoveredUV = true;
    bIsPainting = true;
    Invalidate(EInvalidateWidgetReason::Paint);

    if (OnPaintStrokeStarted.IsBound())
    {
        OnPaintStrokeStarted.Execute(UV);
    }

    return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SWetWrinkleTexturePreview::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
    if (!OnPaintStrokeEnded.IsBound())
    {
        return FReply::Unhandled();
    }

    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !bIsPainting)
    {
        return FReply::Unhandled();
    }

    bIsPainting = false;
    if (OnPaintStrokeEnded.IsBound())
    {
        OnPaintStrokeEnded.Execute();
    }

    return FReply::Handled().ReleaseMouseCapture();
}

void SWetWrinkleTexturePreview::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    bHasHoveredUV = false;
    Invalidate(EInvalidateWidgetReason::Paint);
    if (OnUVHoverEnded.IsBound())
    {
        OnUVHoverEnded.Execute();
    }
}

FSlateRect SWetWrinkleTexturePreview::ComputeTextureRect(const FVector2D& LocalSize) const
{
    const UTexture* Texture = SourceTexture.Get();
    const float TextureWidth = Texture != nullptr ? FMath::Max(1.0f, Texture->GetSurfaceWidth()) : 1.0f;
    const float TextureHeight = Texture != nullptr ? FMath::Max(1.0f, Texture->GetSurfaceHeight()) : 1.0f;
    const float TextureAspect = TextureWidth / TextureHeight;
    const float WidgetAspect = LocalSize.X / FMath::Max(1.0f, LocalSize.Y);

    FVector2D DrawSize = LocalSize;
    if (WidgetAspect > TextureAspect)
    {
        DrawSize.X = LocalSize.Y * TextureAspect;
    }
    else
    {
        DrawSize.Y = LocalSize.X / TextureAspect;
    }

    const FVector2D DrawPosition = (LocalSize - DrawSize) * 0.5f;
    return FSlateRect(DrawPosition.X, DrawPosition.Y, DrawPosition.X + DrawSize.X, DrawPosition.Y + DrawSize.Y);
}

bool SWetWrinkleTexturePreview::TryGetUVFromLocalPosition(const FGeometry& Geometry, const FVector2D& LocalPosition, FVector2D& OutUV) const
{
    if (!SourceTexture.IsValid())
    {
        return false;
    }

    const FSlateRect TextureRect = ComputeTextureRect(Geometry.GetLocalSize());
    if (LocalPosition.X < TextureRect.Left ||
        LocalPosition.X > TextureRect.Right ||
        LocalPosition.Y < TextureRect.Top ||
        LocalPosition.Y > TextureRect.Bottom)
    {
        return false;
    }

    const FVector2D TextureSize(TextureRect.Right - TextureRect.Left, TextureRect.Bottom - TextureRect.Top);
    OutUV.X = (LocalPosition.X - TextureRect.Left) / FMath::Max(1.0f, TextureSize.X);
    OutUV.Y = (LocalPosition.Y - TextureRect.Top) / FMath::Max(1.0f, TextureSize.Y);
    return true;
}

FVector2D SWetWrinkleTexturePreview::UVToLocalPosition(const FSlateRect& TextureRect, const FVector2D& UV) const
{
    const FVector2D WrappedUV = WrapWetWrinklePreviewUV(UV);
    const FVector2D TextureSize(TextureRect.Right - TextureRect.Left, TextureRect.Bottom - TextureRect.Top);
    return FVector2D(
        TextureRect.Left + WrappedUV.X * TextureSize.X,
        TextureRect.Top + WrappedUV.Y * TextureSize.Y);
}

void SWetWrinkleTexturePreview::AppendCirclePoints(const FSlateRect& TextureRect, const FVector2D& UV, float RadiusUV, TArray<FVector2D>& OutPoints) const
{
    const FVector2D Center = UVToLocalPosition(TextureRect, UV);
    const FVector2D TextureSize(TextureRect.Right - TextureRect.Left, TextureRect.Bottom - TextureRect.Top);
    const FVector2D Radius(TextureSize.X * RadiusUV, TextureSize.Y * RadiusUV);

    OutPoints.Reset(WetWrinkleTextureCircleSegments + 1);
    for (int32 SegmentIndex = 0; SegmentIndex <= WetWrinkleTextureCircleSegments; ++SegmentIndex)
    {
        const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(WetWrinkleTextureCircleSegments)) * UE_TWO_PI;
        OutPoints.Add(Center + FVector2D(FMath::Cos(Angle) * Radius.X, FMath::Sin(Angle) * Radius.Y));
    }
}
