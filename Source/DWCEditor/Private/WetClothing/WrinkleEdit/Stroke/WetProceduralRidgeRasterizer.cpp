#include "WetClothing/WrinkleEdit/Stroke/WetProceduralRidgeRasterizer.h"

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

    double EndpointTaperScale(const FWetProceduralRidgeStroke& Stroke, const double SegmentT)
    {
        const double StartScale = Stroke.StartTaper > 0.0f ? FMath::Clamp(SegmentT / Stroke.StartTaper, 0.0, 1.0) : 1.0;
        const double EndScale = Stroke.EndTaper > 0.0f ? FMath::Clamp((1.0 - SegmentT) / Stroke.EndTaper, 0.0, 1.0) : 1.0;
        return FMath::Min(StartScale, EndScale);
    }

    FVector ComputeRidgeNormal(const FWetProceduralRidgeStroke& Stroke, const FVector2D& SegmentDir, const double SignedDistance, const double HalfWidth, const double SegmentT)
    {
        const double Distance01 = HalfWidth > UE_SMALL_NUMBER ? FMath::Clamp(FMath::Abs(SignedDistance) / HalfWidth, 0.0, 1.0) : 1.0;
        const double Feather = FMath::Clamp(static_cast<double>(Stroke.Falloff), 0.01, 1.0);
        const double Coverage = FMath::Pow(1.0 - Distance01, 1.0 / Feather) * EndpointTaperScale(Stroke, SegmentT);
        if (Coverage <= UE_SMALL_NUMBER)
        {
            return FVector(0.0, 0.0, 1.0);
        }

        FVector2D Perp(-SegmentDir.Y, SegmentDir.X);
        double DirectionSign = 1.0;
        if (Stroke.Shape == EWetProceduralRidgeShape::Concave)
        {
            DirectionSign = -1.0;
        }
        else if (Stroke.Shape == EWetProceduralRidgeShape::Fold)
        {
            DirectionSign = SignedDistance < 0.0 ? -1.0 : 1.0;
            if (Stroke.bFlipFoldSide)
            {
                DirectionSign *= -1.0;
            }
        }

        const double Strength = FMath::Clamp(static_cast<double>(Stroke.Strength), 0.0, 4.0) * 0.65 * Coverage * DirectionSign;
        return FVector(Perp.X * Strength, Perp.Y * Strength, 1.0).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
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
    const int32 PaddingPixels = FMath::CeilToInt(FMath::Max(Stroke.WidthUV, 0.001f) * FMath::Max(TextureSize.X, TextureSize.Y) * 0.75f) + 4;
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

    TArray<FVector> NormalBuffer;
    TArray<float> CoverageBuffer;
    NormalBuffer.SetNum(PixelCount);
    CoverageBuffer.Init(0.0f, PixelCount);
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        NormalBuffer[Index] = bBlendWithExisting ? DecodeNormalTS(InOutPixels[Index]) : FVector(0.0, 0.0, 1.0);
    }

    Result = RasterizeToNormalCoverageBuffers(Stroke, TextureSize, NormalBuffer, CoverageBuffer, ClipRect);
    if (!Result.bAffectedPixels)
    {
        return Result;
    }

    for (int32 Y = Result.DirtyRect.Min.Y; Y < Result.DirtyRect.Max.Y; ++Y)
    {
        for (int32 X = Result.DirtyRect.Min.X; X < Result.DirtyRect.Max.X; ++X)
        {
            const int32 Index = Y * TextureSize.X + X;
            if (CoverageBuffer[Index] > 0.0f)
            {
                InOutPixels[Index] = EncodeNormalTS(NormalBuffer[Index]);
            }
        }
    }
    return Result;
}

FWetProceduralRidgeRasterResult FWetProceduralRidgeRasterizer::RasterizeToNormalCoverageBuffers(
    const FWetProceduralRidgeStroke& Stroke,
    const FIntPoint TextureSize,
    TArray<FVector>& InOutNormalBuffer,
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

    FIntRect Bounds = ComputeBounds(Stroke, TextureSize);
    if (ClipRect != nullptr)
    {
        Bounds.Min.X = FMath::Max(Bounds.Min.X, ClipRect->Min.X);
        Bounds.Min.Y = FMath::Max(Bounds.Min.Y, ClipRect->Min.Y);
        Bounds.Max.X = FMath::Min(Bounds.Max.X, ClipRect->Max.X);
        Bounds.Max.Y = FMath::Min(Bounds.Max.Y, ClipRect->Max.Y);
    }
    Bounds = ClampRect(Bounds, TextureSize);
    if (Bounds.IsEmpty())
    {
        return Result;
    }

    const double HalfWidth = FMath::Max(Stroke.WidthUV * 0.5, 0.0001f);
    FIntRect DirtyRect;
    bool bHasDirtyRect = false;

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

        const FVector2D SegmentDir = AB.GetSafeNormal();
        for (int32 Y = Bounds.Min.Y; Y < Bounds.Max.Y; ++Y)
        {
            for (int32 X = Bounds.Min.X; X < Bounds.Max.X; ++X)
            {
                const FVector2D UV((static_cast<double>(X) + 0.5) / TextureSize.X, (static_cast<double>(Y) + 0.5) / TextureSize.Y);
                const double SegmentT = FMath::Clamp(FVector2D::DotProduct(UV - A, AB) / SegmentLengthSquared, 0.0, 1.0);
                const FVector2D Closest = A + AB * SegmentT;
                const FVector2D Delta = UV - Closest;
                const double SignedDistance = FVector2D::CrossProduct(SegmentDir, Delta);
                if (FMath::Abs(SignedDistance) > HalfWidth)
                {
                    continue;
                }

                const FVector RidgeNormal = ComputeRidgeNormal(Stroke, SegmentDir, SignedDistance, HalfWidth, SegmentT);
                const double Coverage = FMath::Clamp(1.0 - FMath::Abs(SignedDistance) / HalfWidth, 0.0, 1.0) * EndpointTaperScale(Stroke, SegmentT);
                if (Coverage <= UE_SMALL_NUMBER)
                {
                    continue;
                }

                const int32 Index = Y * TextureSize.X + X;
                InOutNormalBuffer[Index] = FVector(
                    InOutNormalBuffer[Index].X + RidgeNormal.X * Coverage,
                    InOutNormalBuffer[Index].Y + RidgeNormal.Y * Coverage,
                    InOutNormalBuffer[Index].Z * RidgeNormal.Z).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0, 0.0, 1.0));
                InOutCoverageBuffer[Index] = FMath::Max(InOutCoverageBuffer[Index], static_cast<float>(Coverage));
                IncludePoint(DirtyRect, bHasDirtyRect, FIntPoint(X, Y));
            }
        }
    }

    Result.bAffectedPixels = bHasDirtyRect;
    Result.DirtyRect = bHasDirtyRect ? ClampRect(DirtyRect, TextureSize) : FIntRect(0, 0, 0, 0);
    return Result;
}
