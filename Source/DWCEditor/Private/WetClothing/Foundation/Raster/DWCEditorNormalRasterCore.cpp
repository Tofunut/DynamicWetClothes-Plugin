// Copyright 2026 Team Tofunut. All Rights Reserved.

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

    void AddMergedRect(TArray<FIntRect>& Rects, FIntRect Rect)
    {
        if (Rect.IsEmpty())
        {
            return;
        }
        for (int32 Index = 0; Index < Rects.Num();)
        {
            const FIntRect& Existing = Rects[Index];
            const bool      bTouches = Rect.Min.X <= Existing.Max.X && Rect.Max.X >= Existing.Min.X &&
                                  Rect.Min.Y <= Existing.Max.Y && Rect.Max.Y >= Existing.Min.Y;
            if (!bTouches)
            {
                ++Index;
                continue;
            }
            Rect.Min.X = FMath::Min(Rect.Min.X, Existing.Min.X);
            Rect.Min.Y = FMath::Min(Rect.Min.Y, Existing.Min.Y);
            Rect.Max.X = FMath::Max(Rect.Max.X, Existing.Max.X);
            Rect.Max.Y = FMath::Max(Rect.Max.Y, Existing.Max.Y);
            Rects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            Index = 0;
        }
        Rects.Add(Rect);
    }

    template <typename GetNormalType, typename SetNormalType, typename GetCoverageType, typename SetCoverageType>
    FDWCEditorRasterResult RasterizeStampPixels(
        const FDWCEditorNormalStampCommand& Command,
        const FIntPoint                     CanvasSize,
        const FIntRect&                     EffectiveClip,
        const bool                          bHasCoverage,
        GetNormalType&&                     GetNormal,
        SetNormalType&&                     SetNormal,
        GetCoverageType&&                   GetCoverage,
        SetCoverageType&&                   SetCoverage,
        const FDWCEditorCancellationToken*  CancellationToken)
    {
        FDWCEditorRasterResult Result;
        const FVector2f        Center(
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
        bool        bHasDirtyRect = false;

        for (int32 TileY = MinTile; TileY <= MaxTile; ++TileY)
        {
            for (int32 TileX = MinTile; TileX <= MaxTile; ++TileX)
            {
                const FVector2f TileCenter = Center + FVector2f(static_cast<float>(TileX), static_cast<float>(TileY));
                const int32     MinX = FMath::Max(
                    FMath::FloorToInt((TileCenter.X - Command.Footprint.RadiusUV) * CanvasSize.X),
                    EffectiveClip.Min.X);
                const int32 MaxX = FMath::Min(
                    FMath::CeilToInt((TileCenter.X + Command.Footprint.RadiusUV) * CanvasSize.X),
                    EffectiveClip.Max.X - 1);
                const int32 MinY = FMath::Max(
                    FMath::FloorToInt((TileCenter.Y - Command.Footprint.RadiusUV) * CanvasSize.Y),
                    EffectiveClip.Min.Y);
                const int32 MaxY = FMath::Min(
                    FMath::CeilToInt((TileCenter.Y + Command.Footprint.RadiusUV) * CanvasSize.Y),
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
                            (static_cast<float>(X) + 0.5f) / CanvasSize.X,
                            (static_cast<float>(Y) + 0.5f) / CanvasSize.Y);
                        FVector2f Delta = PixelUV - TileCenter;
                        if (Command.Footprint.bWrap)
                        {
                            Delta.X = RasterWrappedDelta(Delta.X);
                            Delta.Y = RasterWrappedDelta(Delta.Y);
                        }
                        const FVector2f Local = Delta / FMath::Max(Command.Footprint.RadiusUV, UE_SMALL_NUMBER);
                        const float     Distance = Local.Size();
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
                        const float     Strength = FMath::Max(Command.Strength * EdgeFade, 0.0f);
                        const FVector3f Detail(Rotated.X * Strength, Rotated.Y * Strength, Rotated.Z);
                        SetNormal(X, Y, FDWCEditorNormalRasterCore::BlendAngleCorrected(GetNormal(X, Y), Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
                        if (bHasCoverage)
                        {
                            const float SourceCoverage = Command.CoverageSource.IsValid()
                                                             ? Command.CoverageSource.SampleBilinear(SourceUV)
                                                             : 1.0f;
                            SetCoverage(
                                X,
                                Y,
                                FMath::Max(GetCoverage(X, Y), FMath::Clamp(EdgeFade * SourceCoverage, 0.0f, 1.0f)));
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
} // namespace

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
    FDWCEditorNormalRasterSurface&      Surface,
    const FDWCEditorCancellationToken*  CancellationToken,
    const FIntRect*                     ClipRect)
{
    if (!Surface.IsValid() || !Command.NormalSource.IsValid() ||
        Command.Footprint.RadiusUV <= 0.0f || Command.Strength <= 0.0f)
    {
        return FDWCEditorRasterResult();
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
        return FDWCEditorRasterResult();
    }
    return RasterizeStampPixels(
        Command,
        Surface.Size,
        EffectiveClip,
        Surface.HasCoverage(),
        [&Surface](const int32 X, const int32 Y)
        { return Surface.GetNormal(Y * Surface.Size.X + X); },
        [&Surface](const int32 X, const int32 Y, const FVector3f& Normal)
        { Surface.SetNormal(Y * Surface.Size.X + X, Normal); },
        [&Surface](const int32 X, const int32 Y)
        { return Surface.Coverage[Y * Surface.Size.X + X]; },
        [&Surface](const int32 X, const int32 Y, const float Coverage)
        { Surface.Coverage[Y * Surface.Size.X + X] = Coverage; },
        CancellationToken);
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeStampRegion(
    const FDWCEditorNormalStampCommand& Command,
    FDWCEditorNormalRasterRegion&       Region,
    const FDWCEditorCancellationToken*  CancellationToken,
    const FIntRect*                     ClipRect)
{
    if (!Region.IsValid() || !Command.NormalSource.IsValid() ||
        Command.Footprint.RadiusUV <= 0.0f || Command.Strength <= 0.0f)
    {
        return FDWCEditorRasterResult();
    }
    FIntRect EffectiveClip = Region.Rect;
    if (ClipRect != nullptr)
    {
        EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, ClipRect->Min.X);
        EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, ClipRect->Min.Y);
        EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, ClipRect->Max.X);
        EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, ClipRect->Max.Y);
    }
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeStampPixels(
        Command,
        Region.CanvasSize,
        EffectiveClip,
        Region.Surface.HasCoverage(),
        [&Region](const int32 X, const int32 Y)
        { return Region.GetNormal(X, Y); },
        [&Region](const int32 X, const int32 Y, const FVector3f& Normal)
        { Region.SetNormal(X, Y, Normal); },
        [&Region](const int32 X, const int32 Y)
        { return Region.GetCoverage(X, Y); },
        [&Region](const int32 X, const int32 Y, const float Coverage)
        { Region.SetCoverage(X, Y, Coverage); },
        CancellationToken);
}

void FDWCEditorNormalRasterCore::ComputeStampBounds(
    const FDWCEditorNormalStampCommand& Command,
    const FIntPoint                     CanvasSize,
    TArray<FIntRect>&                   OutBounds)
{
    OutBounds.Reset();
    if (CanvasSize.X <= 0 || CanvasSize.Y <= 0 || Command.Footprint.RadiusUV <= 0.0f)
    {
        return;
    }
    const FVector2f Center(
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.X) : Command.Footprint.CenterUV.X,
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.Y) : Command.Footprint.CenterUV.Y);
    const int32 MinTile = Command.Footprint.bWrap ? -1 : 0;
    const int32 MaxTile = Command.Footprint.bWrap ? 1 : 0;
    for (int32 TileY = MinTile; TileY <= MaxTile; ++TileY)
    {
        for (int32 TileX = MinTile; TileX <= MaxTile; ++TileX)
        {
            const FVector2f TileCenter = Center + FVector2f(static_cast<float>(TileX), static_cast<float>(TileY));
            FIntRect        Bounds(
                FMath::FloorToInt((TileCenter.X - Command.Footprint.RadiusUV) * CanvasSize.X),
                FMath::FloorToInt((TileCenter.Y - Command.Footprint.RadiusUV) * CanvasSize.Y),
                FMath::CeilToInt((TileCenter.X + Command.Footprint.RadiusUV) * CanvasSize.X) + 1,
                FMath::CeilToInt((TileCenter.Y + Command.Footprint.RadiusUV) * CanvasSize.Y) + 1);
            Bounds.Min.X = FMath::Clamp(Bounds.Min.X, 0, CanvasSize.X);
            Bounds.Min.Y = FMath::Clamp(Bounds.Min.Y, 0, CanvasSize.Y);
            Bounds.Max.X = FMath::Clamp(Bounds.Max.X, 0, CanvasSize.X);
            Bounds.Max.Y = FMath::Clamp(Bounds.Max.Y, 0, CanvasSize.Y);
            AddMergedRect(OutBounds, Bounds);
        }
    }
}
