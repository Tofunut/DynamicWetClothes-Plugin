#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"

namespace
{
    FColor EncodeNormalTS(const FVector& Normal)
    {
        const FVector N = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
        return FColor(
            static_cast<uint8>(FMath::Clamp((N.X * 0.5 + 0.5) * 255.0, 0.0, 255.0)),
            static_cast<uint8>(FMath::Clamp((N.Y * 0.5 + 0.5) * 255.0, 0.0, 255.0)),
            static_cast<uint8>(FMath::Clamp((N.Z * 0.5 + 0.5) * 255.0, 0.0, 255.0)),
            255);
    }

    FVector DecodeNormalTS(const FColor& Color)
    {
        return FVector(
            static_cast<double>(Color.R) / 127.5 - 1.0,
            static_cast<double>(Color.G) / 127.5 - 1.0,
            static_cast<double>(Color.B) / 127.5 - 1.0).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
    }

    FIntRect ClampRect(const FIntRect& Rect, const FIntPoint TextureSize)
    {
        return FIntRect(
            FMath::Clamp(Rect.Min.X, 0, TextureSize.X),
            FMath::Clamp(Rect.Min.Y, 0, TextureSize.Y),
            FMath::Clamp(Rect.Max.X, 0, TextureSize.X),
            FMath::Clamp(Rect.Max.Y, 0, TextureSize.Y));
    }

    void IncludePoint(FIntRect& Rect, bool& bHasRect, const FIntPoint Point)
    {
        if (!bHasRect)
        {
            Rect = FIntRect(Point.X, Point.Y, Point.X + 1, Point.Y + 1);
            bHasRect = true;
            return;
        }

        Rect.Min.X = FMath::Min(Rect.Min.X, Point.X);
        Rect.Min.Y = FMath::Min(Rect.Min.Y, Point.Y);
        Rect.Max.X = FMath::Max(Rect.Max.X, Point.X + 1);
        Rect.Max.Y = FMath::Max(Rect.Max.Y, Point.Y + 1);
    }

    double SmoothNoise(const double Position, const double Frequency, const int32 Seed, const double PhaseOffset)
    {
        const double SeedPhase = FMath::Frac(FMath::Abs(static_cast<double>(Seed)) * 0.00061803398875 + PhaseOffset);
        const double Phase = (Position * Frequency + SeedPhase) * UE_TWO_PI;
        return FMath::Clamp(FMath::Sin(Phase) * 0.72 + FMath::Sin(Phase * 0.47 + 1.91) * 0.28, -1.0, 1.0);
    }

    double EndpointTaperScale(const FWetProceduralRidgeStroke& Stroke, const double StrokeT)
    {
        const bool bTaperStart = Stroke.StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Pointed;
        const bool bTaperEnd = Stroke.EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Pointed;
        const double StartScale = bTaperStart && Stroke.StartTaper > 0.0f
            ? FMath::Clamp(StrokeT / Stroke.StartTaper, 0.0, 1.0)
            : 1.0;
        const double EndScale = bTaperEnd && Stroke.EndTaper > 0.0f
            ? FMath::Clamp((1.0 - StrokeT) / Stroke.EndTaper, 0.0, 1.0)
            : 1.0;
        return FMath::Min(StartScale, EndScale);
    }

    double FlareBlend(const double DistanceFromEndpoint01, const float Softness)
    {
        const double Clamped = FMath::Clamp(DistanceFromEndpoint01, 0.0, 1.0);
        const double Exponent = FMath::Lerp(2.5, 0.5, FMath::Clamp(static_cast<double>(Softness), 0.0, 1.0));
        return FMath::Pow(1.0 - Clamped, Exponent);
    }

    double ResolveHalfWidth(const FWetProceduralRidgeStroke& Stroke, const double StrokeT)
    {
        double WidthScale = 1.0;
        const double FlareLength = FMath::Clamp(static_cast<double>(Stroke.FlareSettings.Length), 0.01, 0.5);
        if (Stroke.StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared && StrokeT < FlareLength)
        {
            const double Blend = FlareBlend(StrokeT / FlareLength, Stroke.FlareSettings.Softness);
            WidthScale *= FMath::Lerp(1.0, static_cast<double>(Stroke.FlareSettings.WidthScale), Blend);
        }
        if (Stroke.EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared && 1.0 - StrokeT < FlareLength)
        {
            const double Blend = FlareBlend((1.0 - StrokeT) / FlareLength, Stroke.FlareSettings.Softness);
            WidthScale *= FMath::Lerp(1.0, static_cast<double>(Stroke.FlareSettings.WidthScale), Blend);
        }

        if (Stroke.NaturalVariation.bEnabled && Stroke.NaturalVariation.WidthVariation > 0.0f)
        {
            const double Noise = SmoothNoise(
                StrokeT,
                FMath::Max(static_cast<double>(Stroke.NaturalVariation.WidthFrequency), 0.25),
                Stroke.NaturalVariation.NoiseSeed,
                0.37);
            WidthScale *= FMath::Max(0.25, 1.0 + Noise * Stroke.NaturalVariation.WidthVariation);
        }
        return FMath::Max(static_cast<double>(Stroke.WidthUV) * 0.5 * WidthScale, 0.0001);
    }

    double ResolveEndpointStrengthScale(const FWetProceduralRidgeStroke& Stroke, const double StrokeT)
    {
        double StrengthScale = 1.0;
        const double FlareLength = FMath::Clamp(static_cast<double>(Stroke.FlareSettings.Length), 0.01, 0.5);
        if (Stroke.StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared && StrokeT < FlareLength)
        {
            const double Blend = FlareBlend(StrokeT / FlareLength, Stroke.FlareSettings.Softness);
            StrengthScale *= FMath::Lerp(1.0, static_cast<double>(Stroke.FlareSettings.EndStrength), Blend);
        }
        if (Stroke.EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared && 1.0 - StrokeT < FlareLength)
        {
            const double Blend = FlareBlend((1.0 - StrokeT) / FlareLength, Stroke.FlareSettings.Softness);
            StrengthScale *= FMath::Lerp(1.0, static_cast<double>(Stroke.FlareSettings.EndStrength), Blend);
        }
        return StrengthScale;
    }

    double ResolveCenterlineOffset(const FWetProceduralRidgeStroke& Stroke, const double StrokeT, const double HalfWidth)
    {
        if (!Stroke.NaturalVariation.bEnabled || Stroke.NaturalVariation.CenterlineAmount <= 0.0f)
        {
            return 0.0;
        }

        return SmoothNoise(
                   StrokeT,
                   FMath::Max(static_cast<double>(Stroke.NaturalVariation.CenterlineFrequency), 0.25),
                   Stroke.NaturalVariation.NoiseSeed,
                   0.0) *
            Stroke.NaturalVariation.CenterlineAmount * HalfWidth;
    }

    double ComputeCrossSectionCoverage(const FWetProceduralRidgeStroke& Stroke, const double Distance01, const double StrokeT)
    {
        const double Feather = FMath::Clamp(static_cast<double>(Stroke.Falloff), 0.01, 1.0);
        return FMath::Pow(FMath::Clamp(1.0 - Distance01, 0.0, 1.0), 1.0 / Feather) *
            EndpointTaperScale(Stroke, StrokeT);
    }

    FVector ComputeRidgeNormal(
        const FWetProceduralRidgeStroke& Stroke,
        const FVector2D& SegmentDir,
        const FVector2D& ProfileDirection,
        const double Distance01,
        const double StrokeT)
    {
        if (Distance01 <= UE_SMALL_NUMBER || Distance01 >= 1.0)
        {
            return FVector(0.0, 0.0, 1.0);
        }

        FVector2D NormalDirection = ProfileDirection.GetSafeNormal();
        if (Stroke.Shape == EWetProceduralRidgeShape::Concave)
        {
            NormalDirection *= -1.0;
        }
        else if (Stroke.Shape == EWetProceduralRidgeShape::Fold)
        {
            NormalDirection = FVector2D(-SegmentDir.Y, SegmentDir.X);
            if (Stroke.bFlipFoldSide)
            {
                NormalDirection *= -1.0;
            }
        }

        const double Feather = FMath::Clamp(static_cast<double>(Stroke.Falloff), 0.01, 1.0);
        const double SlopeExponent = FMath::Lerp(2.0, 0.65, Feather);
        const double SlopeProfile = FMath::Pow(FMath::Sin(PI * Distance01), SlopeExponent);
        const double Strength = FMath::Clamp(static_cast<double>(Stroke.Strength), 0.0, 4.0) *
            0.65 * SlopeProfile * ResolveEndpointStrengthScale(Stroke, StrokeT);
        return FVector(NormalDirection.X * Strength, NormalDirection.Y * Strength, 1.0)
            .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
    }
}

FIntRect FWetProceduralRidgeRasterizer::ComputeBounds(
    const FWetProceduralRidgeStroke& Stroke,
    const FIntPoint TextureSize,
    const int32 FirstPointIndex)
{
    if (TextureSize.X <= 0 || TextureSize.Y <= 0 || Stroke.Points.Num() < 2)
    {
        return FIntRect(0, 0, 0, 0);
    }

    FIntRect Bounds;
    bool bHasBounds = false;
    const int32 StartIndex = FMath::Clamp(FirstPointIndex, 0, Stroke.Points.Num() - 1);
    const int32 PaddingPixels = FMath::CeilToInt(
        FMath::Max(Stroke.WidthUV, 0.001f) *
        FMath::Max(1.0f, Stroke.FlareSettings.WidthScale) *
        FMath::Max(TextureSize.X, TextureSize.Y) * 0.75f) + 4;
    for (int32 PointIndex = StartIndex; PointIndex < Stroke.Points.Num(); ++PointIndex)
    {
        const FVector2D UV = Stroke.Points[PointIndex].PositionUV;
        const FIntPoint Pixel(
            FMath::RoundToInt(UV.X * TextureSize.X),
            FMath::RoundToInt(UV.Y * TextureSize.Y));
        IncludePoint(Bounds, bHasBounds, FIntPoint(Pixel.X - PaddingPixels, Pixel.Y - PaddingPixels));
        IncludePoint(Bounds, bHasBounds, FIntPoint(Pixel.X + PaddingPixels, Pixel.Y + PaddingPixels));
    }

    return bHasBounds ? ClampRect(Bounds, TextureSize) : FIntRect(0, 0, 0, 0);
}

namespace
{
    FIntRect ResolveRasterBounds(
        const FWetProceduralRidgeStroke& Stroke,
        const FIntPoint TextureSize,
        const FIntRect* ClipRect)
    {
        FIntRect Bounds = FWetProceduralRidgeRasterizer::ComputeBounds(Stroke, TextureSize);
        if (ClipRect != nullptr)
        {
            Bounds.Min.X = FMath::Max(Bounds.Min.X, ClipRect->Min.X);
            Bounds.Min.Y = FMath::Max(Bounds.Min.Y, ClipRect->Min.Y);
            Bounds.Max.X = FMath::Min(Bounds.Max.X, ClipRect->Max.X);
            Bounds.Max.Y = FMath::Min(Bounds.Max.Y, ClipRect->Max.Y);
        }
        return ClampRect(Bounds, TextureSize);
    }

    FWetProceduralRidgeRasterResult RasterizeStrokePixels(
        const FWetProceduralRidgeStroke& Stroke,
        const FIntPoint TextureSize,
        const FIntRect& Bounds,
        TFunctionRef<void(int32, int32, const FVector&, double)> ApplyPixel)
    {
        FWetProceduralRidgeRasterResult Result;
        if (Bounds.IsEmpty())
        {
            return Result;
        }

        const int32 RasterPaddingPixels = FMath::CeilToInt(
            FMath::Max(Stroke.WidthUV, 0.001f) *
            FMath::Max(1.0f, Stroke.FlareSettings.WidthScale) *
            FMath::Max(TextureSize.X, TextureSize.Y) * 0.75f) + 4;

        TArray<double, TInlineAllocator<64>> SegmentLengths;
        TArray<double, TInlineAllocator<64>> CumulativeLengths;
        TArray<FVector2D, TInlineAllocator<64>> SegmentDirections;
        TArray<FVector2D, TInlineAllocator<64>> PointTangents;
        SegmentLengths.SetNumZeroed(Stroke.Points.Num() - 1);
        CumulativeLengths.SetNumZeroed(Stroke.Points.Num());
        SegmentDirections.SetNumZeroed(Stroke.Points.Num() - 1);
        PointTangents.SetNumZeroed(Stroke.Points.Num());
        for (int32 SegmentIndex = 0; SegmentIndex < SegmentLengths.Num(); ++SegmentIndex)
        {
            const FVector2D SegmentDelta =
                Stroke.Points[SegmentIndex + 1].PositionUV - Stroke.Points[SegmentIndex].PositionUV;
            SegmentLengths[SegmentIndex] = SegmentDelta.Size();
            SegmentDirections[SegmentIndex] = SegmentDelta.GetSafeNormal();
            CumulativeLengths[SegmentIndex + 1] = CumulativeLengths[SegmentIndex] + SegmentLengths[SegmentIndex];
        }
        const double TotalLength = CumulativeLengths.Last();
        if (TotalLength <= UE_SMALL_NUMBER)
        {
            return Result;
        }

        PointTangents[0] = SegmentDirections[0];
        PointTangents.Last() = SegmentDirections.Last();
        for (int32 PointIndex = 1; PointIndex + 1 < PointTangents.Num(); ++PointIndex)
        {
            PointTangents[PointIndex] =
                (SegmentDirections[PointIndex - 1] + SegmentDirections[PointIndex]).GetSafeNormal();
            if (PointTangents[PointIndex].IsNearlyZero())
            {
                PointTangents[PointIndex] = SegmentDirections[PointIndex];
            }
        }

        const int32 BoundsWidth = Bounds.Width();
        const int32 BoundsHeight = Bounds.Height();
        TArray<int32> BestSegmentIndices;
        TArray<float> BestDistanceMetrics;
        BestSegmentIndices.Init(INDEX_NONE, BoundsWidth * BoundsHeight);
        BestDistanceMetrics.Init(MAX_flt, BoundsWidth * BoundsHeight);

        // Segment bounds are used only to collect the best polyline candidate. A pixel is
        // evaluated once below, so adjacent segment caps cannot build a bead at each point.
        for (int32 SegmentIndex = 0; SegmentIndex + 1 < Stroke.Points.Num(); ++SegmentIndex)
        {
            const FVector2D A = Stroke.Points[SegmentIndex].PositionUV;
            const FVector2D B = Stroke.Points[SegmentIndex + 1].PositionUV;
            const FVector2D AB = B - A;
            const double SegmentLengthSquared = AB.SizeSquared();
            if (SegmentLengthSquared <= UE_SMALL_NUMBER)
            {
                continue;
            }

            const FVector2D SegmentDir = SegmentDirections[SegmentIndex];
            const FIntPoint PixelA(FMath::RoundToInt(A.X * TextureSize.X), FMath::RoundToInt(A.Y * TextureSize.Y));
            const FIntPoint PixelB(FMath::RoundToInt(B.X * TextureSize.X), FMath::RoundToInt(B.Y * TextureSize.Y));
            FIntRect SegmentBounds(
                FMath::Min(PixelA.X, PixelB.X) - RasterPaddingPixels,
                FMath::Min(PixelA.Y, PixelB.Y) - RasterPaddingPixels,
                FMath::Max(PixelA.X, PixelB.X) + RasterPaddingPixels + 1,
                FMath::Max(PixelA.Y, PixelB.Y) + RasterPaddingPixels + 1);
            SegmentBounds.Min.X = FMath::Max(SegmentBounds.Min.X, Bounds.Min.X);
            SegmentBounds.Min.Y = FMath::Max(SegmentBounds.Min.Y, Bounds.Min.Y);
            SegmentBounds.Max.X = FMath::Min(SegmentBounds.Max.X, Bounds.Max.X);
            SegmentBounds.Max.Y = FMath::Min(SegmentBounds.Max.Y, Bounds.Max.Y);

            for (int32 Y = SegmentBounds.Min.Y; Y < SegmentBounds.Max.Y; ++Y)
            {
                for (int32 X = SegmentBounds.Min.X; X < SegmentBounds.Max.X; ++X)
                {
                    const FVector2D UV(
                        (static_cast<double>(X) + 0.5) / TextureSize.X,
                        (static_cast<double>(Y) + 0.5) / TextureSize.Y);
                    const double SegmentT = FMath::Clamp(
                        FVector2D::DotProduct(UV - A, AB) / SegmentLengthSquared,
                        0.0,
                        1.0);
                    const double StrokeT = FMath::Clamp(
                        (CumulativeLengths[SegmentIndex] + SegmentT * SegmentLengths[SegmentIndex]) / TotalLength,
                        0.0,
                        1.0);
                    const FVector2D Closest = A + AB * SegmentT;
                    const FVector2D Delta = UV - Closest;
                    const double HalfWidth = ResolveHalfWidth(Stroke, StrokeT);
                    FVector2D StrokeDirection = FMath::Lerp(
                        PointTangents[SegmentIndex],
                        PointTangents[SegmentIndex + 1],
                        SegmentT).GetSafeNormal();
                    if (StrokeDirection.IsNearlyZero())
                    {
                        StrokeDirection = SegmentDir;
                    }
                    const FVector2D Perp(-StrokeDirection.Y, StrokeDirection.X);
                    const double CenterlineOffset = ResolveCenterlineOffset(Stroke, StrokeT, HalfWidth);
                    const FVector2D AdjustedDelta = Delta - Perp * CenterlineOffset;
                    const double DistanceMetric = AdjustedDelta.SizeSquared() / FMath::Square(HalfWidth);
                    if (DistanceMetric > 1.0)
                    {
                        continue;
                    }

                    const int32 LocalIndex = (Y - Bounds.Min.Y) * BoundsWidth + (X - Bounds.Min.X);
                    if (DistanceMetric < BestDistanceMetrics[LocalIndex])
                    {
                        BestDistanceMetrics[LocalIndex] = static_cast<float>(DistanceMetric);
                        BestSegmentIndices[LocalIndex] = SegmentIndex;
                    }
                }
            }
        }

        FIntRect DirtyRect;
        bool bHasDirtyRect = false;
        for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
        {
            for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
            {
                const int32 LocalIndex = (Y - Bounds.Min.Y) * BoundsWidth + (X - Bounds.Min.X);
                const int32 SegmentIndex = BestSegmentIndices[LocalIndex];
                if (SegmentIndex == INDEX_NONE)
                {
                    continue;
                }

                const FVector2D A = Stroke.Points[SegmentIndex].PositionUV;
                const FVector2D B = Stroke.Points[SegmentIndex + 1].PositionUV;
                const FVector2D AB = B - A;
                const double SegmentLengthSquared = AB.SizeSquared();
                const FVector2D UV(
                    (static_cast<double>(X) + 0.5) / TextureSize.X,
                    (static_cast<double>(Y) + 0.5) / TextureSize.Y);
                const double SegmentT = FMath::Clamp(
                    FVector2D::DotProduct(UV - A, AB) / SegmentLengthSquared,
                    0.0,
                    1.0);
                const double StrokeT = FMath::Clamp(
                    (CumulativeLengths[SegmentIndex] + SegmentT * SegmentLengths[SegmentIndex]) / TotalLength,
                    0.0,
                    1.0);
                FVector2D StrokeDirection = FMath::Lerp(
                    PointTangents[SegmentIndex],
                    PointTangents[SegmentIndex + 1],
                    SegmentT).GetSafeNormal();
                if (StrokeDirection.IsNearlyZero())
                {
                    StrokeDirection = SegmentDirections[SegmentIndex];
                }
                const FVector2D Perp(-StrokeDirection.Y, StrokeDirection.X);
                const double HalfWidth = ResolveHalfWidth(Stroke, StrokeT);
                const FVector2D Delta = UV - (A + AB * SegmentT);
                const FVector2D AdjustedDelta =
                    Delta - Perp * ResolveCenterlineOffset(Stroke, StrokeT, HalfWidth);
                const double Distance01 = FMath::Clamp(
                    FMath::Sqrt(static_cast<double>(BestDistanceMetrics[LocalIndex])),
                    0.0,
                    1.0);
                const double Coverage = ComputeCrossSectionCoverage(Stroke, Distance01, StrokeT);
                if (Coverage <= UE_SMALL_NUMBER)
                {
                    continue;
                }

                ApplyPixel(
                    X,
                    Y,
                    ComputeRidgeNormal(Stroke, StrokeDirection, AdjustedDelta, Distance01, StrokeT),
                    Coverage);
                IncludePoint(DirtyRect, bHasDirtyRect, FIntPoint(X, Y));
            }
        }

        Result.bAffectedPixels = bHasDirtyRect;
        Result.DirtyRect = bHasDirtyRect ? ClampRect(DirtyRect, TextureSize) : FIntRect(0, 0, 0, 0);
        return Result;
    }

    FVector BlendRidgeNormal(const FVector& BaseNormal, const FVector& RidgeNormal, const double Coverage)
    {
        return FVector(
                   BaseNormal.X + RidgeNormal.X * Coverage,
                   BaseNormal.Y + RidgeNormal.Y * Coverage,
                   BaseNormal.Z * RidgeNormal.Z)
            .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
    }
}

FWetProceduralRidgeRasterResult FWetProceduralRidgeRasterizer::Rasterize(
    const FWetProceduralRidgeStroke& Stroke,
    const FIntPoint TextureSize,
    TArray<FColor>& InOutPixels,
    const FIntRect* ClipRect,
    const bool bBlendWithExisting)
{
    FWetProceduralRidgeRasterResult Result;
    const int32 PixelCount = TextureSize.X * TextureSize.Y;
    if (!Stroke.bEnabled || Stroke.Points.Num() < 2 || PixelCount <= 0 || InOutPixels.Num() != PixelCount)
    {
        return Result;
    }

    const FIntRect Bounds = ResolveRasterBounds(Stroke, TextureSize, ClipRect);
    if (Bounds.IsEmpty())
    {
        return Result;
    }

    return RasterizeStrokePixels(
        Stroke,
        TextureSize,
        Bounds,
        [&InOutPixels, TextureSize, bBlendWithExisting](
            const int32 X,
            const int32 Y,
            const FVector& RidgeNormal,
            const double Coverage)
        {
            const int32 PixelIndex = Y * TextureSize.X + X;
            const FVector BaseNormal = bBlendWithExisting
                ? DecodeNormalTS(InOutPixels[PixelIndex])
                : FVector(0.0, 0.0, 1.0);
            InOutPixels[PixelIndex] = EncodeNormalTS(BlendRidgeNormal(BaseNormal, RidgeNormal, Coverage));
        });
}

FWetProceduralRidgeRasterResult FWetProceduralRidgeRasterizer::RasterizeToNormalCoverageBuffers(
    const FWetProceduralRidgeStroke& Stroke,
    const FIntPoint TextureSize,
    TArray<FVector3f>& InOutNormalBuffer,
    TArray<float>& InOutCoverageBuffer,
    const FIntRect* ClipRect)
{
    FWetProceduralRidgeRasterResult Result;
    const int32 PixelCount = TextureSize.X * TextureSize.Y;
    if (!Stroke.bEnabled || Stroke.Points.Num() < 2 || PixelCount <= 0 ||
        InOutNormalBuffer.Num() != PixelCount || InOutCoverageBuffer.Num() != PixelCount)
    {
        return Result;
    }

    const FIntRect Bounds = ResolveRasterBounds(Stroke, TextureSize, ClipRect);
    return RasterizeStrokePixels(
        Stroke,
        TextureSize,
        Bounds,
        [&InOutNormalBuffer, &InOutCoverageBuffer, TextureSize](
            const int32 X,
            const int32 Y,
            const FVector& RidgeNormal,
            const double Coverage)
        {
            const int32 Index = Y * TextureSize.X + X;
            InOutNormalBuffer[Index] = FVector3f(
                BlendRidgeNormal(FVector(InOutNormalBuffer[Index]), RidgeNormal, Coverage));
            InOutCoverageBuffer[Index] = FMath::Max(InOutCoverageBuffer[Index], static_cast<float>(Coverage));
        });
}
