#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"

namespace
{
    float RasterWrapUnit(const float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    float RasterWrappedDelta(const float Value)
    {
        return Value - FMath::RoundToFloat(Value);
    }

    float RasterSmoothStep(const float Edge0, const float Edge1, const float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }
        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    void RasterIncludePixel(FIntRect& Rect, bool& bHasRect, const int32 X, const int32 Y)
    {
        if (!bHasRect)
        {
            Rect = FIntRect(X, Y, X + 1, Y + 1);
            bHasRect = true;
            return;
        }
        Rect.Min.X = FMath::Min(Rect.Min.X, X);
        Rect.Min.Y = FMath::Min(Rect.Min.Y, Y);
        Rect.Max.X = FMath::Max(Rect.Max.X, X + 1);
        Rect.Max.Y = FMath::Max(Rect.Max.Y, Y + 1);
    }
}

FVector3f FDWCEditorNormalRasterCore::BlendAngleCorrected(
    const FVector3f& BaseNormal,
    const FVector3f& DetailNormal)
{
    const FVector3f Blended(
        BaseNormal.X + DetailNormal.X,
        BaseNormal.Y + DetailNormal.Y,
        BaseNormal.Z * DetailNormal.Z);
    return Blended.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeStamp(
    const FDWCEditorNormalStampCommand& Command,
    FDWCEditorNormalRasterSurface& Surface,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect)
{
    FDWCEditorRasterResult Result;
    if (!Surface.IsValid() || !Command.NormalSource.IsValid() ||
        Command.Footprint.RadiusUV <= 0.0f || Command.Strength <= 0.0f)
    {
        return Result;
    }

    const FIntRect SurfaceRect(FIntPoint::ZeroValue, Surface.Size);
    const FIntRect EffectiveClip = ClipRect != nullptr
        ? FIntRect(
            FIntPoint(
                FMath::Max(ClipRect->Min.X, SurfaceRect.Min.X),
                FMath::Max(ClipRect->Min.Y, SurfaceRect.Min.Y)),
            FIntPoint(
                FMath::Min(ClipRect->Max.X, SurfaceRect.Max.X),
                FMath::Min(ClipRect->Max.Y, SurfaceRect.Max.Y)))
        : SurfaceRect;
    if (EffectiveClip.IsEmpty())
    {
        return Result;
    }

    const FVector2f Center(
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.X) : Command.Footprint.CenterUV.X,
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.Y) : Command.Footprint.CenterUV.Y);
    const FVector2f SafeScale(
        FMath::Max(FMath::Abs(Command.Footprint.Scale.X), UE_SMALL_NUMBER),
        FMath::Max(FMath::Abs(Command.Footprint.Scale.Y), UE_SMALL_NUMBER));
    const float EdgeFadeStart = FMath::Clamp(1.0f - Command.Footprint.Falloff, 0.0f, 0.98f);
    const float CosRotation = FMath::Cos(Command.Footprint.RotationRadians);
    const float SinRotation = FMath::Sin(Command.Footprint.RotationRadians);
    const int32 MinTile = Command.Footprint.bWrap ? -1 : 0;
    const int32 MaxTile = Command.Footprint.bWrap ? 1 : 0;
    bool bHasDirtyRect = false;

    for (int32 TileY = MinTile; TileY <= MaxTile; ++TileY)
    {
        for (int32 TileX = MinTile; TileX <= MaxTile; ++TileX)
        {
            const FVector2f TileCenter = Center + FVector2f(static_cast<float>(TileX), static_cast<float>(TileY));
            const int32 MinX = FMath::Max(
                FMath::FloorToInt((TileCenter.X - Command.Footprint.RadiusUV) * Surface.Size.X),
                EffectiveClip.Min.X);
            const int32 MaxX = FMath::Min(
                FMath::CeilToInt((TileCenter.X + Command.Footprint.RadiusUV) * Surface.Size.X),
                EffectiveClip.Max.X - 1);
            const int32 MinY = FMath::Max(
                FMath::FloorToInt((TileCenter.Y - Command.Footprint.RadiusUV) * Surface.Size.Y),
                EffectiveClip.Min.Y);
            const int32 MaxY = FMath::Min(
                FMath::CeilToInt((TileCenter.Y + Command.Footprint.RadiusUV) * Surface.Size.Y),
                EffectiveClip.Max.Y - 1);

            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                if (CancellationToken != nullptr && CancellationToken->IsCanceled())
                {
                    Result.bSucceeded = false;
                    Result.bCanceled = true;
                    return Result;
                }
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    const FVector2f PixelUV(
                        (static_cast<float>(X) + 0.5f) / Surface.Size.X,
                        (static_cast<float>(Y) + 0.5f) / Surface.Size.Y);
                    FVector2f Delta = PixelUV - TileCenter;
                    if (Command.Footprint.bWrap)
                    {
                        Delta.X = RasterWrappedDelta(Delta.X);
                        Delta.Y = RasterWrappedDelta(Delta.Y);
                    }
                    const FVector2f Local = Delta / FMath::Max(Command.Footprint.RadiusUV, UE_SMALL_NUMBER);
                    const float Distance = Local.Size();
                    if (Distance > 1.0f)
                    {
                        continue;
                    }

                    const float EdgeFade = 1.0f - RasterSmoothStep(EdgeFadeStart, 1.0f, Distance);
                    if (EdgeFade <= UE_SMALL_NUMBER)
                    {
                        continue;
                    }
                    const float LocalX = (CosRotation * Local.X + SinRotation * Local.Y) / SafeScale.X;
                    const float LocalY = (-SinRotation * Local.X + CosRotation * Local.Y) / SafeScale.Y;
                    if (FMath::Abs(LocalX) > 1.0f || FMath::Abs(LocalY) > 1.0f)
                    {
                        continue;
                    }

                    const FVector2f SourceUV(LocalX * 0.5f + 0.5f, LocalY * 0.5f + 0.5f);
                    const FVector3f Sampled = Command.NormalSource.SampleBilinear(SourceUV);
                    const FVector3f Rotated(
                        Sampled.X * CosRotation - Sampled.Y * SinRotation,
                        Sampled.X * SinRotation + Sampled.Y * CosRotation,
                        Sampled.Z);
                    const float Strength = FMath::Max(Command.Strength * EdgeFade, 0.0f);
                    const FVector3f Detail(Rotated.X * Strength, Rotated.Y * Strength, Rotated.Z);
                    const int32 Index = Y * Surface.Size.X + X;
                    Surface.SetNormal(Index, BlendAngleCorrected(
                        Surface.GetNormal(Index),
                        Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
                    if (Surface.HasCoverage())
                    {
                        const float SourceCoverage = Command.CoverageSource.IsValid()
                            ? Command.CoverageSource.SampleBilinear(SourceUV)
                            : 1.0f;
                        Surface.Coverage[Index] = FMath::Max(
                            Surface.Coverage[Index],
                            FMath::Clamp(EdgeFade * SourceCoverage, 0.0f, 1.0f));
                    }
                    RasterIncludePixel(Result.DirtyRect, bHasDirtyRect, X, Y);
                    ++Result.AffectedPixelCount;
                }
            }
        }
    }

    Result.bAffectedPixels = bHasDirtyRect;
    return Result;
}
