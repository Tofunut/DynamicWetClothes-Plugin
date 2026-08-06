/*
 * Material Slot 미리보기 Slate 위젯을 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"

class UTexture;

class SWCAMaterialSlotPreview : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWCAMaterialSlotPreview) {}
    SLATE_ARGUMENT(TArray<FWetClothingAssetUVTriangle>, Triangles)
    SLATE_ARGUMENT(UTexture*, PreviewTexture)
    SLATE_ARGUMENT(bool, DrawWireframe)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    virtual int32 OnPaint(
        const FPaintArgs& Args,
        const FGeometry& AllottedGeometry,
        const FSlateRect& MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32 LayerId,
        const FWidgetStyle& InWidgetStyle,
        bool bParentEnabled) const override;

  private:
    struct FCachedTriangle
    {
        FVector2D NormalizedPositions[3];
        FVector2D UVs[3];
        FColor Colors[3];
    };

    struct FCachedEdge
    {
        FVector2D NormalizedStart = FVector2D::ZeroVector;
        FVector2D NormalizedEnd = FVector2D::ZeroVector;
    };

    void BuildPaintCache(const TArray<FWetClothingAssetUVTriangle>& InTriangles);
    void UpdateSlateGeometryCache(const FGeometry& AllottedGeometry) const;

    TWeakObjectPtr<UTexture> PreviewTexture;
    bool bDrawWireframe = true;
    bool bHasVisibleTextureVariation = false;
    FWetClothingTextureReadback PreviewTextureData;
    TArray<FCachedTriangle> CachedTriangles;
    TArray<FCachedEdge> CachedEdges;
    FVector2D CachedProjectedBoundsSize = FVector2D(1.0f, 1.0f);

    // Final Slate geometry is rebuilt only when the widget size or accumulated
    // transform changes, not on every paint.
    mutable FVector2D CachedSlateLocalSize = FVector2D::ZeroVector;
    mutable FVector2f CachedTransformOrigin = FVector2f::ZeroVector;
    mutable FVector2f CachedTransformUnitX = FVector2f::ZeroVector;
    mutable FVector2f CachedTransformUnitY = FVector2f::ZeroVector;
    mutable bool bSlateGeometryCacheValid = false;
    mutable TArray<FSlateVertex> CachedFillVertices;
    mutable TArray<SlateIndex> CachedFillIndices;
    mutable TArray<TArray<FVector2D>> CachedWireframeLines;
};
