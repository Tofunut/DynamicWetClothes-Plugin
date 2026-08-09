//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationResolver.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
    bool IsFiniteResolverVector(const FVector3f& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    FVector3f ProjectResolverDirection(
        const FVector3f& Direction,
        const FVector3f& Normal)
    {
        return (Direction - Normal * FVector3f::DotProduct(Direction, Normal)).GetSafeNormal();
    }

    FVector3f ChooseLeastAlignedResolverAxis(const FVector3f& Normal)
    {
        const FVector3f Axes[] = {
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f)
        };
        int32 BestAxisIndex = 0;
        float BestAlignment = FMath::Abs(FVector3f::DotProduct(Normal, Axes[0]));
        for (int32 AxisIndex = 1; AxisIndex < UE_ARRAY_COUNT(Axes); ++AxisIndex)
        {
            const float Alignment = FMath::Abs(FVector3f::DotProduct(Normal, Axes[AxisIndex]));
            if (Alignment < BestAlignment)
            {
                BestAlignment = Alignment;
                BestAxisIndex = AxisIndex;
            }
        }
        return Axes[BestAxisIndex];
    }

    FVector3f BuildDeterministicResolverFallback(
        const FVector3f& Normal,
        const FDWCEditorSurfaceOrientationPolicy& Policy)
    {
        FVector3f Direction = ProjectResolverDirection(Policy.SecondaryAxis, Normal);
        if (Direction.IsNearlyZero())
        {
            Direction = ProjectResolverDirection(ChooseLeastAlignedResolverAxis(Normal), Normal);
        }
        return Direction;
    }

    bool NormalizeResolverBarycentric(
        const FVector3f& Input,
        FVector3f& OutNormalized)
    {
        if (!IsFiniteResolverVector(Input) ||
            Input.X < -UE_KINDA_SMALL_NUMBER ||
            Input.Y < -UE_KINDA_SMALL_NUMBER ||
            Input.Z < -UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }
        OutNormalized = FVector3f(
            FMath::Max(Input.X, 0.0f),
            FMath::Max(Input.Y, 0.0f),
            FMath::Max(Input.Z, 0.0f));
        const float Sum = OutNormalized.X + OutNormalized.Y + OutNormalized.Z;
        if (!FMath::IsFinite(Sum) || Sum <= UE_SMALL_NUMBER)
        {
            return false;
        }
        OutNormalized /= Sum;
        return true;
    }

    bool InterpolateTopologyFallback(
        const FDWCEditorSurfaceOrientationFieldEntry& Entry,
        const FVector3f& Barycentric,
        const FVector3f& SurfaceNormal,
        FVector3f& OutDirection)
    {
        FVector3f Directions[3];
        int32 ReferenceCorner = 0;
        if (Barycentric.Y > Barycentric.X)
        {
            ReferenceCorner = 1;
        }
        if (Barycentric.Z > Barycentric[ReferenceCorner])
        {
            ReferenceCorner = 2;
        }

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Directions[CornerIndex] = Entry.CornerFallbackV[CornerIndex].ToFVector3f().GetSafeNormal();
            if (!IsFiniteResolverVector(Directions[CornerIndex]) || Directions[CornerIndex].IsNearlyZero())
            {
                return false;
            }
        }
        const FVector3f Reference = Directions[ReferenceCorner];
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            if (FVector3f::DotProduct(Directions[CornerIndex], Reference) < 0.0f)
            {
                Directions[CornerIndex] *= -1.0f;
            }
        }

        OutDirection = ProjectResolverDirection(
            Directions[0] * Barycentric.X +
            Directions[1] * Barycentric.Y +
            Directions[2] * Barycentric.Z,
            SurfaceNormal);
        return !OutDirection.IsNearlyZero();
    }

    float SmoothResolverStep(const float Value)
    {
        const float T = FMath::Clamp(Value, 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }
}

bool FDWCEditorResolvedSurfaceOrientation::IsValid() const
{
    return IsFiniteResolverVector(FrameU) && IsFiniteResolverVector(FrameV) &&
        FMath::IsNearlyEqual(FrameU.SizeSquared(), 1.0f, 0.002f) &&
        FMath::IsNearlyEqual(FrameV.SizeSquared(), 1.0f, 0.002f) &&
        FMath::IsNearlyZero(FVector3f::DotProduct(FrameU, FrameV), 0.002f) &&
        FMath::IsFinite(PrimaryProjectionQuality) &&
        FMath::IsFinite(FallbackWeight) &&
        PrimaryProjectionQuality >= 0.0f && PrimaryProjectionQuality <= 1.0f &&
        FallbackWeight >= 0.0f && FallbackWeight <= 1.0f;
}

bool FDWCEditorSurfaceOrientationResolver::Resolve(
    const FDWCEditorSpatialData& SpatialData,
    const int32 TriangleIndex,
    const FVector3f& Barycentric,
    const FVector3f& SurfaceNormal,
    const FDWCEditorSurfaceOrientationPolicy& Policy,
    FDWCEditorResolvedSurfaceOrientation& OutResult)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorSurfaceOrientationResolver_Resolve);
    OutResult = {};
    if (!Policy.IsValid() || !SpatialData.Triangles.IsValidIndex(TriangleIndex))
    {
        return false;
    }

    FVector3f NormalizedBarycentric;
    if (!NormalizeResolverBarycentric(Barycentric, NormalizedBarycentric))
    {
        return false;
    }
    const FVector3f Normal = SurfaceNormal.GetSafeNormal();
    if (!IsFiniteResolverVector(Normal) || Normal.IsNearlyZero())
    {
        return false;
    }

    const FVector3f PrimaryProjection =
        Policy.PrimaryAxis - Normal * FVector3f::DotProduct(Policy.PrimaryAxis, Normal);
    OutResult.PrimaryProjectionQuality = FMath::Clamp(PrimaryProjection.Size(), 0.0f, 1.0f);
    FVector3f PrimaryDirection = PrimaryProjection.GetSafeNormal();
    if (OutResult.PrimaryProjectionQuality >= Policy.FallbackBeginQuality &&
        !PrimaryDirection.IsNearlyZero())
    {
        OutResult.FrameV = PrimaryDirection;
        OutResult.FallbackWeight = 0.0f;
        OutResult.Source = EDWCEditorSurfaceOrientationSource::PrimaryAxis;
    }
    else
    {
        FVector3f FallbackDirection = FVector3f::ZeroVector;
        const FDWCEditorSurfaceOrientationField& Field = SpatialData.SurfaceOrientationField;
        const FDWCEditorSurfaceOrientationFieldEntry* Entry =
            Field.IsCompatible(Policy.BuildSignature())
                ? Field.FindByTriangleIndex(TriangleIndex)
                : nullptr;
        const bool bHasTopologyFallback = Entry != nullptr &&
            InterpolateTopologyFallback(
                *Entry,
                NormalizedBarycentric,
                Normal,
                FallbackDirection);
        if (!bHasTopologyFallback)
        {
            FallbackDirection = BuildDeterministicResolverFallback(Normal, Policy);
        }
        if (FallbackDirection.IsNearlyZero())
        {
            return false;
        }

        if (OutResult.PrimaryProjectionQuality <= Policy.FallbackFullQuality ||
            PrimaryDirection.IsNearlyZero())
        {
            OutResult.FrameV = FallbackDirection;
            OutResult.FallbackWeight = 1.0f;
            OutResult.Source = bHasTopologyFallback
                ? EDWCEditorSurfaceOrientationSource::TopologyFallback
                : EDWCEditorSurfaceOrientationSource::DeterministicSecondaryFallback;
        }
        else
        {
            if (FVector3f::DotProduct(FallbackDirection, PrimaryDirection) < 0.0f)
            {
                FallbackDirection *= -1.0f;
            }
            const float PrimaryWeight = SmoothResolverStep(
                (OutResult.PrimaryProjectionQuality - Policy.FallbackFullQuality) /
                (Policy.FallbackBeginQuality - Policy.FallbackFullQuality));
            OutResult.FallbackWeight = 1.0f - PrimaryWeight;
            OutResult.FrameV = ProjectResolverDirection(
                FallbackDirection * OutResult.FallbackWeight +
                PrimaryDirection * PrimaryWeight,
                Normal);
            OutResult.Source = bHasTopologyFallback
                ? EDWCEditorSurfaceOrientationSource::BlendedTopologyFallback
                : EDWCEditorSurfaceOrientationSource::DeterministicSecondaryFallback;
        }
    }

    OutResult.FrameV = ProjectResolverDirection(OutResult.FrameV, Normal);
    OutResult.FrameU = FVector3f::CrossProduct(OutResult.FrameV, Normal).GetSafeNormal();
    if (OutResult.FrameU.IsNearlyZero())
    {
        return false;
    }
    OutResult.FrameV = FVector3f::CrossProduct(Normal, OutResult.FrameU).GetSafeNormal();
    return OutResult.IsValid();
}
