/*
 *  UV Island 선택 도구에서 사용하는 사각형, 타원, 라쏘 교차 판정 기하 함수를 구현합니다.
 */

#include "WetClothingUVSelectionGeometry.h"

#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"

bool FWetClothingUVSelectionGeometry::IsIslandIntersectingRect(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV)
{
    if (!Island.UVBounds.bIsValid || !RectUV.bIsValid)
    {
        return false;
    }

    if (!Island.UVBounds.Intersect(RectUV))
    {
        return false;
    }

    for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
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

bool FWetClothingUVSelectionGeometry::IsIslandIntersectingEllipse(const FWetClothingAssetUVIsland& Island, const FBox2D& RectUV)
{
    if (!Island.UVBounds.bIsValid || !RectUV.bIsValid || !Island.UVBounds.Intersect(RectUV))
    {
        return false;
    }

    const FVector2D EllipseCenter = (RectUV.Min + RectUV.Max) * 0.5f;
    const FVector2D EllipseRadii = (RectUV.Max - RectUV.Min) * 0.5f;

    for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
    {
        if (IsTriangleIntersectingEllipse(Triangle, EllipseCenter, EllipseRadii))
        {
            return true;
        }
    }

    return false;
}

bool FWetClothingUVSelectionGeometry::IsIslandIntersectingPolygon(const FWetClothingAssetUVIsland& Island, const TArray<FVector2D>& PolygonUV)
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

    for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
    {
        if (IsTriangleIntersectingPolygon(Triangle, PolygonUV))
        {
            return true;
        }
    }

    return false;
}

bool FWetClothingUVSelectionGeometry::IsTriangleIntersectingEllipse(
    const FWetClothingAssetUVTriangle& Triangle,
    const FVector2D&                   EllipseCenter,
    const FVector2D&                   EllipseRadii)
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

bool FWetClothingUVSelectionGeometry::IsTriangleIntersectingPolygon(
    const FWetClothingAssetUVTriangle& Triangle,
    const TArray<FVector2D>&           PolygonUV)
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

bool FWetClothingUVSelectionGeometry::IsPointInTriangle(
    const FVector2D& Point,
    const FVector2D& A,
    const FVector2D& B,
    const FVector2D& C)
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

bool FWetClothingUVSelectionGeometry::IsPointInEllipse(
    const FVector2D& Point,
    const FVector2D& EllipseCenter,
    const FVector2D& EllipseRadii)
{
    const double RadiusX = FMath::Max(FMath::Abs(EllipseRadii.X), static_cast<double>(KINDA_SMALL_NUMBER));
    const double RadiusY = FMath::Max(FMath::Abs(EllipseRadii.Y), static_cast<double>(KINDA_SMALL_NUMBER));
    const double NormalizedX = (Point.X - EllipseCenter.X) / RadiusX;
    const double NormalizedY = (Point.Y - EllipseCenter.Y) / RadiusY;
    return (NormalizedX * NormalizedX + NormalizedY * NormalizedY) <= 1.0;
}

bool FWetClothingUVSelectionGeometry::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
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

bool FWetClothingUVSelectionGeometry::DoesSegmentIntersectEllipse(
    const FVector2D& SegmentStart,
    const FVector2D& SegmentEnd,
    const FVector2D& EllipseCenter,
    const FVector2D& EllipseRadii)
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

bool FWetClothingUVSelectionGeometry::DoLineSegmentsIntersect(
    const FVector2D& AStart,
    const FVector2D& AEnd,
    const FVector2D& BStart,
    const FVector2D& BEnd)
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
