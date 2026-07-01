#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"

class UTexture;

enum class EWetClothingAssetUVSelectionTool : uint8
{
    Select,
    BoxSelect,
    EllipseSelect,
    LassoSelect
};

enum class EWetClothingAssetUVSelectionOp : uint8
{
    Replace,
    Add
};

enum class EWetClothingAssetUVDisplayMode : uint8
{
    Normal,
    OutlineOnly
};

DECLARE_DELEGATE_TwoParams(FOnWetClothingUVIslandSelectionChanged, const TArray<int32>& /*IslandIDs*/, EWetClothingAssetUVSelectionOp /*SelectionOp*/);

class SWetClothingAssetUVView : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingAssetUVView) {}
    SLATE_EVENT(FOnWetClothingUVIslandSelectionChanged, OnIslandSelectionChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void                               SetIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands);
    void                               SetSelectedIslands(const TSet<int32>& InIslandIDs);
    void                               SetIslandColors(const TMap<int32, FLinearColor>& InIslandColors);
    void                               SetHiddenIslandIDs(const TSet<int32>& InIslandIDs);
    void                               SetBackgroundTexture(UTexture* InTexture);
    void                               SetDrawBackgroundTexture(bool bInDrawBackgroundTexture);
    void                               SetSelectionTool(EWetClothingAssetUVSelectionTool InSelectionTool);
    void                               SetDisplayMode(EWetClothingAssetUVDisplayMode InDisplayMode);
    EWetClothingAssetUVSelectionTool GetSelectionTool() const { return SelectionTool; }
    EWetClothingAssetUVDisplayMode   GetDisplayMode() const { return DisplayMode; }
    void                               Clear();

  protected:
    virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

    virtual int32 OnPaint(
        const FPaintArgs&        Args,
        const FGeometry&         AllottedGeometry,
        const FSlateRect&        MyCullingRect,
        FSlateWindowElementList& OutDrawElements,
        int32                    LayerId,
        const FWidgetStyle&      InWidgetStyle,
        bool                     bParentEnabled) const override;
    virtual FReply OnMouseButtonDown(
        const FGeometry&     MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseButtonUp(
        const FGeometry&     MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseMove(
        const FGeometry&     MyGeometry,
        const FPointerEvent& MouseEvent) override;
    virtual FReply OnMouseWheel(
        const FGeometry&     MyGeometry,
        const FPointerEvent& MouseEvent) override;

  private:
    FBox2D ComputeUVBounds() const;
    double GetTextureAspectRatio() const;

    FVector2D UVToLocal(
        const FVector2D& UV,
        const FGeometry& Geometry,
        const FBox2D&    UVBounds) const;

    FVector2D UVToLocal(
        const FVector2D& UV,
        const FGeometry& Geometry,
        const FBox2D&    UVBounds,
        double           InZoomAmount,
        const FVector2D& InViewOffset) const;

    FVector2D LocalToUV(
        const FVector2D& LocalPosition,
        const FGeometry& Geometry,
        const FBox2D&    UVBounds) const;

    FVector2D LocalToUV(
        const FVector2D& LocalPosition,
        const FGeometry& Geometry,
        const FBox2D&    UVBounds,
        double           InZoomAmount,
        const FVector2D& InViewOffset) const;

    FVector2D ClampViewOffset(
        const FGeometry& Geometry,
        const FBox2D&    UVBounds,
        double           InZoomAmount,
        const FVector2D& InViewOffset) const;

    bool  UsesDragSelectionTool() const;
    void  ResetView();
    void  ResetSelectionInteractionState();
    void  BroadcastSelection(const TArray<int32>& IslandIDs, EWetClothingAssetUVSelectionOp SelectionOp) const;
    int32 HitTestIslandAtUV(const FVector2D& UV) const;
    bool  IsIslandIntersectingRect(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV) const;
    bool  IsIslandIntersectingEllipse(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV) const;
    bool  IsIslandIntersectingPolygon(const FWetClothingAssetUVIsland& Island, const TArray<FVector2D>& PolygonUV) const;
    bool  IsTriangleIntersectingEllipse(const FWetClothingAssetUVTriangle& Triangle, const FVector2D& EllipseCenter, const FVector2D& EllipseRadii) const;
    bool  IsTriangleIntersectingPolygon(const FWetClothingAssetUVTriangle& Triangle, const TArray<FVector2D>& PolygonUV) const;
    bool  IsPointInTriangle(
         const FVector2D& Point,
         const FVector2D& A,
         const FVector2D& B,
         const FVector2D& C) const;
    bool IsPointInEllipse(const FVector2D& Point, const FVector2D& EllipseCenter, const FVector2D& EllipseRadii) const;
    bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon) const;
    bool DoesSegmentIntersectEllipse(const FVector2D& SegmentStart, const FVector2D& SegmentEnd, const FVector2D& EllipseCenter, const FVector2D& EllipseRadii) const;
    bool DoLineSegmentsIntersect(const FVector2D& AStart, const FVector2D& AEnd, const FVector2D& BStart, const FVector2D& BEnd) const;

  private:
    TArray<FWetClothingAssetUVIsland>    Islands;
    TSet<int32>                            SelectedIslandIDs;
    TMap<int32, FLinearColor>              IslandColors;
    TSet<int32>                            HiddenIslandIDs;
    FOnWetClothingUVIslandSelectionChanged OnIslandSelectionChanged;
    FSlateBrush                            BackgroundTextureBrush;
    TWeakObjectPtr<UTexture>               BackgroundTexture;
    bool                                   bDrawBackgroundTexture = true;
    float                                  Padding = 16.0f;
    double                                 ZoomAmount = 1.0;
    FVector2D                              ViewOffset = FVector2D::ZeroVector;
    bool                                   bIsPanning = false;
    FVector2D                              LastPanLocalPosition = FVector2D::ZeroVector;
    EWetClothingAssetUVSelectionTool     SelectionTool = EWetClothingAssetUVSelectionTool::Select;
    EWetClothingAssetUVDisplayMode       DisplayMode = EWetClothingAssetUVDisplayMode::Normal;
    bool                                   bIsDraggingSelectionShape = false;
    FVector2D                              SelectionDragStartLocal = FVector2D::ZeroVector;
    FVector2D                              SelectionDragCurrentLocal = FVector2D::ZeroVector;
    TArray<FVector2D>                      SelectionLassoPointsLocal;
};
