//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"

namespace
{
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
        TFunctionRef<void(int32, int32, const FVector&, double)> ApplyPixel,
        const FDWCEditorCancellationToken* CancellationToken)
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

        FIntRect DirtyRect;
        bool bHasDirtyRect = false;
        // Keep candidate scratch bounded. Large strokes are evaluated tile by
        // tile, so a full-resolution stroke does not allocate full-resolution
        // int/float helper arrays on the game or worker thread.
        for (int32 TileMinY = Bounds.Min.Y; TileMinY < Bounds.Max.Y; TileMinY += FWetProceduralRidgeRasterizer::ScratchTileSize)
        {
            for (int32 TileMinX = Bounds.Min.X; TileMinX < Bounds.Max.X; TileMinX += FWetProceduralRidgeRasterizer::ScratchTileSize)
            {
                if (CancellationToken != nullptr && CancellationToken->IsCanceled())
                {
                    Result.bCanceled = true;
                    return Result;
                }
                const FIntRect TileBounds(
                    TileMinX,
                    TileMinY,
                    FMath::Min(TileMinX + FWetProceduralRidgeRasterizer::ScratchTileSize, Bounds.Max.X),
                    FMath::Min(TileMinY + FWetProceduralRidgeRasterizer::ScratchTileSize, Bounds.Max.Y));
                const int32 TileWidth = TileBounds.Width();
                TArray<int32, TInlineAllocator<FWetProceduralRidgeRasterizer::ScratchTileSize * FWetProceduralRidgeRasterizer::ScratchTileSize>> BestSegmentIndices;
                TArray<float, TInlineAllocator<FWetProceduralRidgeRasterizer::ScratchTileSize * FWetProceduralRidgeRasterizer::ScratchTileSize>> BestDistanceMetrics;
                BestSegmentIndices.Init(INDEX_NONE, TileBounds.Width() * TileBounds.Height());
                BestDistanceMetrics.Init(MAX_flt, TileBounds.Width() * TileBounds.Height());

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
                    SegmentBounds.Min.X = FMath::Max(SegmentBounds.Min.X, TileBounds.Min.X);
                    SegmentBounds.Min.Y = FMath::Max(SegmentBounds.Min.Y, TileBounds.Min.Y);
                    SegmentBounds.Max.X = FMath::Min(SegmentBounds.Max.X, TileBounds.Max.X);
                    SegmentBounds.Max.Y = FMath::Min(SegmentBounds.Max.Y, TileBounds.Max.Y);
                    for (int32 Y = SegmentBounds.Min.Y; Y < SegmentBounds.Max.Y; ++Y)
                    {
                        for (int32 X = SegmentBounds.Min.X; X < SegmentBounds.Max.X; ++X)
                        {
                            const FVector2D UV((static_cast<double>(X) + 0.5) / TextureSize.X, (static_cast<double>(Y) + 0.5) / TextureSize.Y);
                            const double SegmentT = FMath::Clamp(FVector2D::DotProduct(UV - A, AB) / SegmentLengthSquared, 0.0, 1.0);
                            const double StrokeT = FMath::Clamp((CumulativeLengths[SegmentIndex] + SegmentT * SegmentLengths[SegmentIndex]) / TotalLength, 0.0, 1.0);
                            FVector2D StrokeDirection = FMath::Lerp(PointTangents[SegmentIndex], PointTangents[SegmentIndex + 1], SegmentT).GetSafeNormal();
                            if (StrokeDirection.IsNearlyZero()) StrokeDirection = SegmentDir;
                            const double HalfWidth = ResolveHalfWidth(Stroke, StrokeT);
                            const FVector2D Perp(-StrokeDirection.Y, StrokeDirection.X);
                            const FVector2D AdjustedDelta = UV - (A + AB * SegmentT) - Perp * ResolveCenterlineOffset(Stroke, StrokeT, HalfWidth);
                            const double DistanceMetric = AdjustedDelta.SizeSquared() / FMath::Square(HalfWidth);
                            const int32 LocalIndex = (Y - TileBounds.Min.Y) * TileWidth + (X - TileBounds.Min.X);
                            if (DistanceMetric <= 1.0 && DistanceMetric < BestDistanceMetrics[LocalIndex])
                            {
                                BestDistanceMetrics[LocalIndex] = static_cast<float>(DistanceMetric);
                                BestSegmentIndices[LocalIndex] = SegmentIndex;
                            }
                        }
                    }
                }

                for (int32 Y = TileBounds.Min.Y; Y < TileBounds.Max.Y; ++Y)
                {
                    for (int32 X = TileBounds.Min.X; X < TileBounds.Max.X; ++X)
                    {
                        const int32 LocalIndex = (Y - TileBounds.Min.Y) * TileWidth + (X - TileBounds.Min.X);
                        const int32 SegmentIndex = BestSegmentIndices[LocalIndex];
                        if (SegmentIndex == INDEX_NONE) continue;

                        const FVector2D A = Stroke.Points[SegmentIndex].PositionUV;
                        const FVector2D B = Stroke.Points[SegmentIndex + 1].PositionUV;
                        const FVector2D AB = B - A;
                        const double SegmentT = FMath::Clamp(FVector2D::DotProduct(
                            FVector2D((static_cast<double>(X) + 0.5) / TextureSize.X, (static_cast<double>(Y) + 0.5) / TextureSize.Y) - A, AB) / AB.SizeSquared(), 0.0, 1.0);
                        const double StrokeT = FMath::Clamp((CumulativeLengths[SegmentIndex] + SegmentT * SegmentLengths[SegmentIndex]) / TotalLength, 0.0, 1.0);
                        FVector2D StrokeDirection = FMath::Lerp(PointTangents[SegmentIndex], PointTangents[SegmentIndex + 1], SegmentT).GetSafeNormal();
                        if (StrokeDirection.IsNearlyZero()) StrokeDirection = SegmentDirections[SegmentIndex];
                        const FVector2D Perp(-StrokeDirection.Y, StrokeDirection.X);
                        const double HalfWidth = ResolveHalfWidth(Stroke, StrokeT);
                        const FVector2D UV((static_cast<double>(X) + 0.5) / TextureSize.X, (static_cast<double>(Y) + 0.5) / TextureSize.Y);
                        const FVector2D AdjustedDelta = UV - (A + AB * SegmentT) - Perp * ResolveCenterlineOffset(Stroke, StrokeT, HalfWidth);
                        const double Distance01 = FMath::Clamp(FMath::Sqrt(static_cast<double>(BestDistanceMetrics[LocalIndex])), 0.0, 1.0);
                        const double Coverage = ComputeCrossSectionCoverage(Stroke, Distance01, StrokeT);
                        if (Coverage <= UE_SMALL_NUMBER) continue;

                        ApplyPixel(X, Y, ComputeRidgeNormal(Stroke, StrokeDirection, AdjustedDelta, Distance01, StrokeT), Coverage);
                        IncludePoint(DirtyRect, bHasDirtyRect, FIntPoint(X, Y));
                    }
                }
            }
        }

        Result.bAffectedPixels = bHasDirtyRect;
        Result.DirtyRect = bHasDirtyRect ? ClampRect(DirtyRect, TextureSize) : FIntRect(0, 0, 0, 0);
        return Result;
    }

}

uint64 FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound()
{
    return static_cast<uint64>(ScratchTileSize) * ScratchTileSize * (sizeof(int32) + sizeof(float));
}

FWetProceduralRidgeRasterResult FWetProceduralRidgeRasterizer::RasterizeToSurface(
    const FWetProceduralRidgeStroke& Stroke,
    FDWCEditorNormalRasterSurface& Surface,
    const FIntRect* ClipRect,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FWetProceduralRidgeRasterResult Result;
    if (!Surface.IsValid() || !Stroke.bEnabled || Stroke.Points.Num() < 2)
    {
        return Result;
    }

    const FIntRect Bounds = ResolveRasterBounds(Stroke, Surface.Size, ClipRect);
    return RasterizeStrokePixels(
        Stroke,
        Surface.Size,
        Bounds,
        [&Surface](const int32 X, const int32 Y, const FVector& RidgeNormal, const double Coverage)
        {
            const int32 Index = Y * Surface.Size.X + X;
            const FVector3f Detail = FVector3f(
                RidgeNormal.X * Coverage,
                RidgeNormal.Y * Coverage,
                RidgeNormal.Z);
            Surface.SetNormal(Index, FDWCEditorNormalRasterCore::BlendAngleCorrected(
                Surface.GetNormal(Index),
                Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
            if (Surface.HasCoverage())
            {
                Surface.Coverage[Index] = FMath::Max(
                    Surface.Coverage[Index],
                    static_cast<float>(Coverage));
            }
        },
        CancellationToken);
}

FWetProceduralRidgeRasterResult FWetProceduralRidgeRasterizer::RasterizeToRegion(
    const FWetProceduralRidgeStroke& Stroke,
    FDWCEditorNormalRasterRegion& Region,
    const FIntRect* ClipRect,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FWetProceduralRidgeRasterResult Result;
    if (!Region.IsValid() || !Stroke.bEnabled || Stroke.Points.Num() < 2)
    {
        return Result;
    }

    FIntRect EffectiveClip = Region.Rect;
    if (ClipRect != nullptr)
    {
        EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, ClipRect->Min.X);
        EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, ClipRect->Min.Y);
        EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, ClipRect->Max.X);
        EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, ClipRect->Max.Y);
    }
    const FIntRect Bounds = ResolveRasterBounds(Stroke, Region.CanvasSize, &EffectiveClip);
    return RasterizeStrokePixels(
        Stroke,
        Region.CanvasSize,
        Bounds,
        [&Region](const int32 X, const int32 Y, const FVector& RidgeNormal, const double Coverage)
        {
            const FVector3f Detail = FVector3f(
                RidgeNormal.X * Coverage,
                RidgeNormal.Y * Coverage,
                RidgeNormal.Z);
            Region.SetNormal(X, Y, FDWCEditorNormalRasterCore::BlendAngleCorrected(
                Region.GetNormal(X, Y),
                Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
            if (Region.Surface.HasCoverage())
            {
                Region.SetCoverage(X, Y, FMath::Max(
                    Region.GetCoverage(X, Y),
                    static_cast<float>(Coverage)));
            }
        },
        CancellationToken);
}
