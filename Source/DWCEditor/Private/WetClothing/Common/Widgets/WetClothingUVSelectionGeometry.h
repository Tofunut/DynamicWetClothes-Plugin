/*
 *  UV 선택 도구용 기하 판정 유틸리티 함수를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"

struct FWetClothingAssetUVIsland;
struct FWetClothingAssetUVTriangle;

class FWetClothingUVSelectionGeometry
{
  public:
    static bool IsIslandIntersectingRect(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV);
    static bool IsIslandIntersectingEllipse(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV);
    static bool IsIslandIntersectingPolygon(const FWetClothingAssetUVIsland& Island, const TArray<FVector2D>& PolygonUV);

    static bool IsTriangleIntersectingEllipse(
        const FWetClothingAssetUVTriangle& Triangle,
        const FVector2D&                   EllipseCenter,
        const FVector2D&                   EllipseRadii);

    static bool IsTriangleIntersectingPolygon(
        const FWetClothingAssetUVTriangle& Triangle,
        const TArray<FVector2D>&           PolygonUV);

    static bool IsPointInTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C);

    static bool IsPointInEllipse(
        const FVector2D& Point,
        const FVector2D& EllipseCenter,
        const FVector2D& EllipseRadii);

    static bool IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon);

    static bool DoesSegmentIntersectEllipse(
        const FVector2D& SegmentStart,
        const FVector2D& SegmentEnd,
        const FVector2D& EllipseCenter,
        const FVector2D& EllipseRadii);

    static bool DoLineSegmentsIntersect(
        const FVector2D& AStart,
        const FVector2D& AEnd,
        const FVector2D& BStart,
        const FVector2D& BEnd);
};
