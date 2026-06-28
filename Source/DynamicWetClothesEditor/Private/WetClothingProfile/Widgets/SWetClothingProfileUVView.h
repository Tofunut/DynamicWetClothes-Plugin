#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

#include "WetClothingProfile/Analysis/WetClothingProfileMeshAnalyzer.h"

class UTexture;

struct FQuantizedUVPoint
{
    int64 X = 0;
    int64 Y = 0;

    bool operator==(const FQuantizedUVPoint& Other) const
    {
        return X == Other.X && Y == Other.Y;
    }
};

FORCEINLINE uint32 GetTypeHash(const FQuantizedUVPoint& Point)
{
    return HashCombine(::GetTypeHash(Point.X), ::GetTypeHash(Point.Y));
}

struct FQuantizedUVEdge
{
    FQuantizedUVPoint A;
    FQuantizedUVPoint B;

    bool operator==(const FQuantizedUVEdge& Other) const
    {
        return A == Other.A && B == Other.B;
    }
};

FORCEINLINE uint32 GetTypeHash(const FQuantizedUVEdge& Edge)
{
    return HashCombine(::GetTypeHash(Edge.A), ::GetTypeHash(Edge.B));
}

enum class EWetClothingProfileUVSelectionTool : uint8
{
    Select,
    BoxSelect,
    EllipseSelect,
    LassoSelect
};

enum class EWetClothingProfileUVSelectionOp : uint8
{
    Replace,
    Add
};

enum class EWetClothingProfileUVDisplayMode : uint8
{
    Normal,
    OutlineOnly
};

DECLARE_DELEGATE_TwoParams(FOnWetClothingUVIslandSelectionChanged, const TArray<int32>& /*IslandIDs*/, EWetClothingProfileUVSelectionOp /*SelectionOp*/);

class SWetClothingProfileUVView : public SLeafWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingProfileUVView) {}
    SLATE_EVENT(FOnWetClothingUVIslandSelectionChanged, OnIslandSelectionChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void                               SetIslands(const TArray<TSharedPtr<FWetClothingProfileUVIsland>>& InIslands);
    void                               SetSelectedIslands(const TSet<int32>& InIslandIDs);
    void                               SetIslandColors(const TMap<int32, FLinearColor>& InIslandColors);
    void                               SetBackgroundTexture(UTexture* InTexture);
    void                               SetSelectionTool(EWetClothingProfileUVSelectionTool InSelectionTool);
    void                               SetDisplayMode(EWetClothingProfileUVDisplayMode InDisplayMode);
    EWetClothingProfileUVSelectionTool GetSelectionTool() const { return SelectionTool; }
    EWetClothingProfileUVDisplayMode   GetDisplayMode() const { return DisplayMode; }
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
    void  BroadcastSelection(const TArray<int32>& IslandIDs, EWetClothingProfileUVSelectionOp SelectionOp) const;
    int32 HitTestIslandAtUV(const FVector2D& UV) const;
    bool  IsIslandIntersectingRect(const FWetClothingProfileUVIsland& Island, const FBox2D& RectUV) const;
    bool  IsIslandIntersectingEllipse(const FWetClothingProfileUVIsland& Island, const FBox2D& RectUV) const;
    bool  IsIslandIntersectingPolygon(const FWetClothingProfileUVIsland& Island, const TArray<FVector2D>& PolygonUV) const;
    bool  IsTriangleIntersectingEllipse(const FWetClothingProfileUVTriangle& Triangle, const FVector2D& EllipseCenter, const FVector2D& EllipseRadii) const;
    bool  IsTriangleIntersectingPolygon(const FWetClothingProfileUVTriangle& Triangle, const TArray<FVector2D>& PolygonUV) const;
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
    TArray<FWetClothingProfileUVIsland>    Islands;
    TSet<int32>                            SelectedIslandIDs;
    TMap<int32, FLinearColor>              IslandColors;
    FOnWetClothingUVIslandSelectionChanged OnIslandSelectionChanged;
    FSlateBrush                            BackgroundTextureBrush;
    TWeakObjectPtr<UTexture>               BackgroundTexture;
    float                                  Padding = 16.0f;
    double                                 ZoomAmount = 1.0;
    FVector2D                              ViewOffset = FVector2D::ZeroVector;
    bool                                   bIsPanning = false;
    FVector2D                              LastPanLocalPosition = FVector2D::ZeroVector;
    EWetClothingProfileUVSelectionTool     SelectionTool = EWetClothingProfileUVSelectionTool::Select;
    EWetClothingProfileUVDisplayMode       DisplayMode = EWetClothingProfileUVDisplayMode::Normal;
    bool                                   bIsDraggingSelectionShape = false;
    FVector2D                              SelectionDragStartLocal = FVector2D::ZeroVector;
    FVector2D                              SelectionDragCurrentLocal = FVector2D::ZeroVector;
    TArray<FVector2D>                      SelectionLassoPointsLocal;
};
