#pragma once

#include "CoreMinimal.h"

/** Shared UV geometry predicates used by Data UV chart construction and validation. */
class FDWCUVGeometry
{
public:
    static double ComputeTriangleArea2D(
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C);

    static double ComputeTriangleDoubleArea3D(
        const FVector& A,
        const FVector& B,
        const FVector& C);

    static double ComputeTriangleArea3D(
        const FVector& A,
        const FVector& B,
        const FVector& C);

    static bool IsFiniteReasonableUV(const FVector2D& UV);

    /** Returns true only when two triangles overlap by positive interior area. */
    static bool DoTrianglesOverlapByArea(
        const FVector2D& A0,
        const FVector2D& A1,
        const FVector2D& A2,
        const FVector2D& B0,
        const FVector2D& B1,
        const FVector2D& B2);

    static uint64 MakeTrianglePairKey(int32 A, int32 B);
};
