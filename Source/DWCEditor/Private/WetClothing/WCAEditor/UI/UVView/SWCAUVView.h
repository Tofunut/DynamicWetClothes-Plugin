#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

class UTexture;

enum class EWCAUVSelectionTool : uint8
{
    Select,
    BoxSelect,
    EllipseSelect,
    LassoSelect
};

enum class EWCAUVSelectionOp : uint8
{
    Replace,
    Add
};

enum class EWCAUVDisplayMode : uint8
{
    Normal,
    OutlineOnly
};

DECLARE_DELEGATE_TwoParams(FOnWetClothingUVIslandSelectionChanged, const TArray<int32>& /*UVIslandIDs*/, EWCAUVSelectionOp /*SelectionOp*/);

struct FWCAUVViewCircleMarker
{
    FVector2D CenterUV = FVector2D::ZeroVector;
    float RadiusUV = 0.025f;
    FLinearColor FillColor = FLinearColor::Transparent;
    FLinearColor OutlineColor = FLinearColor::Transparent;
    float OutlineThickness = 1.0f;
};

class SWCAUVView : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWCAUVView) {}
    SLATE_EVENT(FOnWetClothingUVIslandSelectionChanged, OnIslandSelectionChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void                             SetIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands);
    void                             SetSelectedIslands(const TSet<int32>& InUVIslandIDs);
    void                             SetIslandColors(const TMap<int32, FLinearColor>& InIslandColors);
    void                             SetHiddenUVIslandIDs(const TSet<int32>& InUVIslandIDs);
    void                             SetCircleMarkers(const TArray<FWCAUVViewCircleMarker>& InCircleMarkers);
    void                             SetBackgroundTexture(UTexture* InTexture);
    void                             SetDrawBackgroundTexture(bool bInDrawBackgroundTexture);
    void                             SetBackgroundTextureOpacity(float InOpacity);
    void                             SetUVIslandLineOpacity(float InOpacity);
    void                             SetUVIslandLineThicknessScale(float InThicknessScale);
    void                             SetNormalizeToContentBounds(bool bInNormalizeToContentBounds);
    void                             SetSelectionTool(EWCAUVSelectionTool InSelectionTool);
    void                             SetDisplayMode(EWCAUVDisplayMode InDisplayMode);
    EWCAUVSelectionTool GetSelectionTool() const { return SelectionTool; }
    EWCAUVDisplayMode   GetDisplayMode() const { return DisplayMode; }
    void                             Clear();

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
    void RebuildGeometryCache();
    FBox2D ComputeUVBounds() const;
    FBox2D ComputeContentUVBounds() const;
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
    void  BroadcastSelection(const TArray<int32>& UVIslandIDs, EWCAUVSelectionOp SelectionOp) const;
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
    struct FCachedOutlineEdge
    {
        FVector2D Start = FVector2D::ZeroVector;
        FVector2D End = FVector2D::ZeroVector;
    };

    TArray<FWetClothingAssetUVIsland>      Islands;
    TMap<int32, TArray<FCachedOutlineEdge>> CachedOutlineEdgesByIsland;
    FBox2D                                 CachedContentUVBounds = FBox2D(ForceInit);
    TSet<int32>                            SelectedUVIslandIDs;
    TMap<int32, FLinearColor>              IslandColors;
    TSet<int32>                            HiddenUVIslandIDs;
    TArray<FWCAUVViewCircleMarker> CircleMarkers;
    FOnWetClothingUVIslandSelectionChanged OnIslandSelectionChanged;
    FSlateBrush                            BackgroundTextureBrush;
    TWeakObjectPtr<UTexture>               BackgroundTexture;
    bool                                   bDrawBackgroundTexture = true;
    float                                  BackgroundTextureOpacity = 0.75f;
    float                                  UVIslandLineOpacity = 1.0f;
    float                                  UVIslandLineThicknessScale = 1.0f;
    bool                                   bNormalizeToContentBounds = false;
    float                                  Padding = 16.0f;
    double                                 ZoomAmount = 1.0;
    FVector2D                              ViewOffset = FVector2D::ZeroVector;
    bool                                   bIsPanning = false;
    FVector2D                              LastPanLocalPosition = FVector2D::ZeroVector;
    EWCAUVSelectionTool       SelectionTool = EWCAUVSelectionTool::Select;
    EWCAUVDisplayMode         DisplayMode = EWCAUVDisplayMode::Normal;
    bool                                   bIsDraggingSelectionShape = false;
    FVector2D                              SelectionDragStartLocal = FVector2D::ZeroVector;
    FVector2D                              SelectionDragCurrentLocal = FVector2D::ZeroVector;
    TArray<FVector2D>                      SelectionLassoPointsLocal;
};
