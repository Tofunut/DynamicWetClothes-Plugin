/*
 *  Material Slot 미리보기 Slate 위젯을 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"
#include "Widgets/SLeafWidget.h"

class UTexture;

class SWetClothingMaterialSlotPreview : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingMaterialSlotPreview) {}
    SLATE_ARGUMENT(TArray<FWetClothingAssetUVTriangle>, Triangles)
    SLATE_ARGUMENT(UTexture*, PreviewTexture)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    virtual int32 OnPaint(
        const FPaintArgs&        Args,
        const FGeometry&         AllottedGeometry,
        const FSlateRect&        MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32                    LayerId,
        const FWidgetStyle&      InWidgetStyle,
        bool                     bParentEnabled) const override;

  private:
    TArray<FWetClothingAssetUVTriangle> Triangles;
    TWeakObjectPtr<UTexture>            PreviewTexture;
    FWetClothingTextureReadback         PreviewTextureData;
};
