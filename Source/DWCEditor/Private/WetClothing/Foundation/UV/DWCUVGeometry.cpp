// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCUVGeometryPrivate
{
    double Cross2D(const FVector2D& A, const FVector2D& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    double SignedDistanceToEdge(
        const FVector2D& Point,
        const FVector2D& EdgeA,
        const FVector2D& EdgeB,
        const double     OrientationSign)
    {
        return OrientationSign * Cross2D(EdgeB - EdgeA, Point - EdgeA);
    }

    FVector2D IntersectSegmentWithClipEdge(
        const FVector2D& SegmentStart,
        const FVector2D& SegmentEnd,
        const double     StartDistance,
        const double     EndDistance)
    {
        const double Denominator = StartDistance - EndDistance;
        if (FMath::Abs(Denominator) <= 1.0e-12)
        {
            return (SegmentStart + SegmentEnd) * 0.5;
        }

        const double T = FMath::Clamp(StartDistance / Denominator, 0.0, 1.0);
        return SegmentStart + (SegmentEnd - SegmentStart) * T;
    }

    double ComputeTriangleIntersectionArea(
        const FVector2D& A0,
        const FVector2D& A1,
        const FVector2D& A2,
        const FVector2D& B0,
        const FVector2D& B1,
        const FVector2D& B2)
    {
        TArray<FVector2D, TInlineAllocator<8>> Polygon;
        Polygon.Add(A0);
        Polygon.Add(A1);
        Polygon.Add(A2);

        const FVector2D ClipVertices[3] = { B0, B1, B2 };
        const double    ClipSignedDoubleArea = Cross2D(B1 - B0, B2 - B0);
        if (FMath::Abs(ClipSignedDoubleArea) <= 1.0e-12)
        {
            return 0.0;
        }

        const double     OrientationSign = ClipSignedDoubleArea >= 0.0 ? 1.0 : -1.0;
        constexpr double InsideTolerance = 1.0e-12;

        for (int32 ClipEdgeIndex = 0; ClipEdgeIndex < 3 && !Polygon.IsEmpty(); ++ClipEdgeIndex)
        {
            const FVector2D                              EdgeA = ClipVertices[ClipEdgeIndex];
            const FVector2D                              EdgeB = ClipVertices[(ClipEdgeIndex + 1) % 3];
            const TArray<FVector2D, TInlineAllocator<8>> InputPolygon = Polygon;
            Polygon.Reset();

            FVector2D Previous = InputPolygon.Last();
            double    PreviousDistance = SignedDistanceToEdge(Previous, EdgeA, EdgeB, OrientationSign);
            bool      bPreviousInside = PreviousDistance >= -InsideTolerance;

            for (const FVector2D& Current : InputPolygon)
            {
                const double CurrentDistance = SignedDistanceToEdge(Current, EdgeA, EdgeB, OrientationSign);
                const bool   bCurrentInside = CurrentDistance >= -InsideTolerance;

                if (bCurrentInside != bPreviousInside)
                {
                    Polygon.Add(IntersectSegmentWithClipEdge(
                        Previous,
                        Current,
                        PreviousDistance,
                        CurrentDistance));
                }

                if (bCurrentInside)
                {
                    Polygon.Add(Current);
                }

                Previous = Current;
                PreviousDistance = CurrentDistance;
                bPreviousInside = bCurrentInside;
            }
        }

        if (Polygon.Num() < 3)
        {
            return 0.0;
        }

        double SignedDoubleArea = 0.0;
        for (int32 Index = 0; Index < Polygon.Num(); ++Index)
        {
            SignedDoubleArea += Cross2D(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]);
        }
        return FMath::Abs(SignedDoubleArea) * 0.5;
    }
} // namespace DWCUVGeometryPrivate

double FDWCUVGeometry::ComputeTriangleArea2D(
    const FVector2D& A,
    const FVector2D& B,
    const FVector2D& C)
{
    return FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
}

double FDWCUVGeometry::ComputeTriangleDoubleArea3D(
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    return FVector::CrossProduct(B - A, C - A).Size();
}

double FDWCUVGeometry::ComputeTriangleArea3D(
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    return ComputeTriangleDoubleArea3D(A, B, C) * 0.5;
}

bool FDWCUVGeometry::IsFiniteReasonableUV(const FVector2D& UV)
{
    constexpr double MaximumAbsoluteCoordinate = 1.0e6;
    return FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y) &&
           FMath::Abs(UV.X) <= MaximumAbsoluteCoordinate &&
           FMath::Abs(UV.Y) <= MaximumAbsoluteCoordinate;
}

bool FDWCUVGeometry::DoTrianglesOverlapByArea(
    const FVector2D& A0,
    const FVector2D& A1,
    const FVector2D& A2,
    const FVector2D& B0,
    const FVector2D& B1,
    const FVector2D& B2)
{
    const double AreaA = ComputeTriangleArea2D(A0, A1, A2);
    const double AreaB = ComputeTriangleArea2D(B0, B1, B2);
    if (AreaA <= 1.0e-12 || AreaB <= 1.0e-12)
    {
        return false;
    }

    const double IntersectionArea = DWCUVGeometryPrivate::ComputeTriangleIntersectionArea(
        A0,
        A1,
        A2,
        B0,
        B1,
        B2);
    const double RelativeTolerance = FMath::Min(AreaA, AreaB) * 1.0e-8;
    return IntersectionArea > FMath::Max(1.0e-12, RelativeTolerance);
}

uint64 FDWCUVGeometry::MakeTrianglePairKey(const int32 A, const int32 B)
{
    const uint32 MinIndex = static_cast<uint32>(FMath::Min(A, B));
    const uint32 MaxIndex = static_cast<uint32>(FMath::Max(A, B));
    return (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
}
