#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SLeafWidget.h"
#include "WetWrinkle/Viewport/WetWrinkleHitData.h"

class UTexture;
class UWetClothingAsset;

DECLARE_DELEGATE_OneParam(FOnWetWrinkleTextureUVHovered, const FVector2D& /*UV*/);
DECLARE_DELEGATE(FOnWetWrinkleTextureUVHoverEnded);
DECLARE_DELEGATE_OneParam(FOnWetWrinkleTexturePaintStrokeStarted, const FVector2D& /*UV*/);
DECLARE_DELEGATE_OneParam(FOnWetWrinkleTexturePaintStampRequested, const FVector2D& /*UV*/);
DECLARE_DELEGATE(FOnWetWrinkleTexturePaintStrokeEnded);

class SWetWrinkleTexturePreview : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleTexturePreview) {}
    SLATE_EVENT(FOnWetWrinkleTextureUVHovered, OnUVHovered)
    SLATE_EVENT(FOnWetWrinkleTextureUVHoverEnded, OnUVHoverEnded)
    SLATE_EVENT(FOnWetWrinkleTexturePaintStrokeStarted, OnPaintStrokeStarted)
    SLATE_EVENT(FOnWetWrinkleTexturePaintStampRequested, OnPaintStampRequested)
    SLATE_EVENT(FOnWetWrinkleTexturePaintStrokeEnded, OnPaintStrokeEnded)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void SetPreviewContext(
        UWetClothingAsset* InWetClothingAsset,
        UTexture* InSourceTexture,
        int32 InMaterialSlotIndex,
        int32 InUVChannelIndex,
        const FWetWrinkleBrushSettings& InBrushSettings,
        const FWetWrinkleSurfaceHit& InSurfaceHit);

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
    virtual int32 OnPaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

  private:
    FSlateRect ComputeTextureRect(const FVector2D& LocalSize) const;
    bool TryGetUVFromLocalPosition(const FGeometry& Geometry, const FVector2D& LocalPosition, FVector2D& OutUV) const;
    FVector2D UVToLocalPosition(const FSlateRect& TextureRect, const FVector2D& UV) const;
    void AppendCirclePoints(const FSlateRect& TextureRect, const FVector2D& UV, float RadiusUV, TArray<FVector2D>& OutPoints) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TWeakObjectPtr<UTexture> SourceTexture;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit SurfaceHit;
    FVector2D HoveredUV = FVector2D::ZeroVector;
    bool bHasHoveredUV = false;
    bool bIsPainting = false;
    FOnWetWrinkleTextureUVHovered OnUVHovered;
    FOnWetWrinkleTextureUVHoverEnded OnUVHoverEnded;
    FOnWetWrinkleTexturePaintStrokeStarted OnPaintStrokeStarted;
    FOnWetWrinkleTexturePaintStampRequested OnPaintStampRequested;
    FOnWetWrinkleTexturePaintStrokeEnded OnPaintStrokeEnded;
    mutable FSlateBrush TextureBrush;
};
