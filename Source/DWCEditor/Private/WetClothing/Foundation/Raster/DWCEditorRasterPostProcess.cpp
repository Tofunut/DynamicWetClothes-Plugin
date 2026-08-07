//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"

namespace
{
    FColor EncodeNormal(const FVector3f& Normal, const float Alpha)
    {
        const FVector3f Safe = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Safe.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Safe.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Safe.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f)));
    }

    FIntRect ClampRect(const FIntRect& Rect, const FIntPoint Size)
    {
        return FIntRect(
            FMath::Clamp(Rect.Min.X, 0, Size.X),
            FMath::Clamp(Rect.Min.Y, 0, Size.Y),
            FMath::Clamp(Rect.Max.X, 0, Size.X),
            FMath::Clamp(Rect.Max.Y, 0, Size.Y));
    }
}

FIntRect FDWCEditorRasterPostProcess::MapRect(
    const FIntRect& SourceRect,
    const FIntPoint SourceSize,
    const FIntPoint DestinationSize)
{
    if (SourceRect.IsEmpty() || SourceSize.X <= 0 || SourceSize.Y <= 0 ||
        DestinationSize.X <= 0 || DestinationSize.Y <= 0)
    {
        return FIntRect();
    }
    return ClampRect(
        FIntRect(
            FMath::FloorToInt(static_cast<double>(SourceRect.Min.X) * DestinationSize.X / SourceSize.X) - 1,
            FMath::FloorToInt(static_cast<double>(SourceRect.Min.Y) * DestinationSize.Y / SourceSize.Y) - 1,
            FMath::CeilToInt(static_cast<double>(SourceRect.Max.X) * DestinationSize.X / SourceSize.X) + 1,
            FMath::CeilToInt(static_cast<double>(SourceRect.Max.Y) * DestinationSize.Y / SourceSize.Y) + 1),
        DestinationSize);
}

FIntRect FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
    const FIntRect& DestinationRect,
    const FIntPoint SourceSize,
    const FIntPoint DestinationSize)
{
    if (DestinationRect.IsEmpty() || SourceSize.X <= 0 || SourceSize.Y <= 0 ||
        DestinationSize.X <= 0 || DestinationSize.Y <= 0)
    {
        return FIntRect();
    }
    return ClampRect(
        FIntRect(
            FMath::FloorToInt(static_cast<double>(DestinationRect.Min.X) * SourceSize.X / DestinationSize.X),
            FMath::FloorToInt(static_cast<double>(DestinationRect.Min.Y) * SourceSize.Y / DestinationSize.Y),
            FMath::CeilToInt(static_cast<double>(DestinationRect.Max.X) * SourceSize.X / DestinationSize.X),
            FMath::CeilToInt(static_cast<double>(DestinationRect.Max.Y) * SourceSize.Y / DestinationSize.Y)),
        SourceSize);
}

bool FDWCEditorRasterPostProcess::DownsampleNormalSurface(
    const FDWCEditorNormalRasterSurface& Source,
    const FIntPoint DestinationSize,
    FDWCEditorNormalRasterSurface& OutDestination,
    const FIntRect* DestinationRect)
{
    if (!Source.IsValid() || DestinationSize.X <= 0 || DestinationSize.Y <= 0)
    {
        return false;
    }
    const bool bCoverage = Source.HasCoverage();
    if (!OutDestination.IsValid() || OutDestination.Size != DestinationSize ||
        OutDestination.HasCoverage() != bCoverage)
    {
        OutDestination.Initialize(DestinationSize, bCoverage);
    }
    const FIntRect FullRect(FIntPoint::ZeroValue, DestinationSize);
    const FIntRect Rect = ClampRect(DestinationRect != nullptr ? *DestinationRect : FullRect, DestinationSize);

    if (Source.Size == DestinationSize)
    {
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            const int32 Start = Y * DestinationSize.X + Rect.Min.X;
            const int32 Count = Rect.Width();
            FMemory::Memcpy(
                OutDestination.PackedNormalXY.GetData() + Start,
                Source.PackedNormalXY.GetData() + Start,
                Count * sizeof(uint32));
            if (bCoverage)
            {
                FMemory::Memcpy(OutDestination.Coverage.GetData() + Start, Source.Coverage.GetData() + Start, Count * sizeof(float));
            }
        }
        return true;
    }

    for (int32 DestinationY = Rect.Min.Y; DestinationY < Rect.Max.Y; ++DestinationY)
    {
        const double SourceMinY = static_cast<double>(DestinationY) * Source.Size.Y / DestinationSize.Y;
        const double SourceMaxY = static_cast<double>(DestinationY + 1) * Source.Size.Y / DestinationSize.Y;
        for (int32 DestinationX = Rect.Min.X; DestinationX < Rect.Max.X; ++DestinationX)
        {
            const double SourceMinX = static_cast<double>(DestinationX) * Source.Size.X / DestinationSize.X;
            const double SourceMaxX = static_cast<double>(DestinationX + 1) * Source.Size.X / DestinationSize.X;
            FVector3d NormalSum = FVector3d::ZeroVector;
            double CoverageSum = 0.0;
            double WeightSum = 0.0;
            for (int32 SourceY = FMath::FloorToInt(SourceMinY); SourceY < FMath::CeilToInt(SourceMaxY); ++SourceY)
            {
                const double WeightY = FMath::Max(0.0, FMath::Min(SourceMaxY, SourceY + 1.0) - FMath::Max(SourceMinY, static_cast<double>(SourceY)));
                const int32 Y = FMath::Clamp(SourceY, 0, Source.Size.Y - 1);
                for (int32 SourceX = FMath::FloorToInt(SourceMinX); SourceX < FMath::CeilToInt(SourceMaxX); ++SourceX)
                {
                    const double WeightX = FMath::Max(0.0, FMath::Min(SourceMaxX, SourceX + 1.0) - FMath::Max(SourceMinX, static_cast<double>(SourceX)));
                    const double Weight = WeightX * WeightY;
                    const int32 Index = Y * Source.Size.X + FMath::Clamp(SourceX, 0, Source.Size.X - 1);
                    NormalSum += FVector3d(Source.GetNormal(Index)) * Weight;
                    if (bCoverage)
                    {
                        CoverageSum += Source.Coverage[Index] * Weight;
                    }
                    WeightSum += Weight;
                }
            }
            const int32 DestinationIndex = DestinationY * DestinationSize.X + DestinationX;
            OutDestination.SetNormal(DestinationIndex, FVector3f(NormalSum.GetSafeNormal(
                UE_DOUBLE_SMALL_NUMBER,
                FVector3d(0.0, 0.0, 1.0))));
            if (bCoverage)
            {
                OutDestination.Coverage[DestinationIndex] = WeightSum > UE_DOUBLE_SMALL_NUMBER
                    ? FMath::Clamp(static_cast<float>(CoverageSum / WeightSum), 0.0f, 1.0f)
                    : 0.0f;
            }
        }
    }
    return true;
}

void FDWCEditorRasterPostProcess::EncodeNormalPixels(
    const FDWCEditorNormalRasterSurface& Surface,
    TArray<FColor>& InOutPixels,
    const FIntRect* Rect,
    const bool bEncodeCoverageInAlpha)
{
    if (!Surface.IsValid())
    {
        InOutPixels.Reset();
        return;
    }
    if (InOutPixels.Num() != Surface.GetPixelCount())
    {
        InOutPixels.Init(FColor(128, 128, 255, 255), Surface.GetPixelCount());
    }
    const FIntRect FullRect(FIntPoint::ZeroValue, Surface.Size);
    const FIntRect EffectiveRect = ClampRect(Rect != nullptr ? *Rect : FullRect, Surface.Size);
    for (int32 Y = EffectiveRect.Min.Y; Y < EffectiveRect.Max.Y; ++Y)
    {
        for (int32 X = EffectiveRect.Min.X; X < EffectiveRect.Max.X; ++X)
        {
            const int32 Index = Y * Surface.Size.X + X;
            InOutPixels[Index] = EncodeNormal(
                Surface.GetNormal(Index),
                bEncodeCoverageInAlpha && Surface.HasCoverage() ? Surface.Coverage[Index] : 1.0f);
        }
    }
}

bool FDWCEditorRasterPostProcess::ResampleAndEncodeNormalPixels(
    const FDWCEditorNormalRasterSurface& Source,
    const FIntPoint DestinationSize,
    TArray<FColor>& InOutPixels,
    const FIntRect* DestinationRect,
    const bool bEncodeCoverageInAlpha)
{
    if (!Source.IsValid() || DestinationSize.X <= 0 || DestinationSize.Y <= 0)
    {
        InOutPixels.Reset();
        return false;
    }

    const int64 DestinationPixelCount64 = static_cast<int64>(DestinationSize.X) * DestinationSize.Y;
    if (DestinationPixelCount64 > MAX_int32)
    {
        InOutPixels.Reset();
        return false;
    }

    const int32 DestinationPixelCount = static_cast<int32>(DestinationPixelCount64);
    if (InOutPixels.Num() != DestinationPixelCount)
    {
        InOutPixels.Init(FColor(128, 128, 255, 255), DestinationPixelCount);
    }

    const FIntRect FullRect(FIntPoint::ZeroValue, DestinationSize);
    const FIntRect Rect = ClampRect(DestinationRect != nullptr ? *DestinationRect : FullRect, DestinationSize);
    const bool bCoverage = bEncodeCoverageInAlpha && Source.HasCoverage();

    for (int32 DestinationY = Rect.Min.Y; DestinationY < Rect.Max.Y; ++DestinationY)
    {
        const double SourceMinY = static_cast<double>(DestinationY) * Source.Size.Y / DestinationSize.Y;
        const double SourceMaxY = static_cast<double>(DestinationY + 1) * Source.Size.Y / DestinationSize.Y;
        for (int32 DestinationX = Rect.Min.X; DestinationX < Rect.Max.X; ++DestinationX)
        {
            const double SourceMinX = static_cast<double>(DestinationX) * Source.Size.X / DestinationSize.X;
            const double SourceMaxX = static_cast<double>(DestinationX + 1) * Source.Size.X / DestinationSize.X;
            FVector3d NormalSum = FVector3d::ZeroVector;
            double CoverageSum = 0.0;
            double WeightSum = 0.0;

            for (int32 SourceY = FMath::FloorToInt(SourceMinY); SourceY < FMath::CeilToInt(SourceMaxY); ++SourceY)
            {
                const double WeightY = FMath::Max(0.0, FMath::Min(SourceMaxY, SourceY + 1.0) - FMath::Max(SourceMinY, static_cast<double>(SourceY)));
                const int32 ClampedY = FMath::Clamp(SourceY, 0, Source.Size.Y - 1);
                for (int32 SourceX = FMath::FloorToInt(SourceMinX); SourceX < FMath::CeilToInt(SourceMaxX); ++SourceX)
                {
                    const double WeightX = FMath::Max(0.0, FMath::Min(SourceMaxX, SourceX + 1.0) - FMath::Max(SourceMinX, static_cast<double>(SourceX)));
                    const double Weight = WeightX * WeightY;
                    const int32 SourceIndex = ClampedY * Source.Size.X + FMath::Clamp(SourceX, 0, Source.Size.X - 1);
                    NormalSum += FVector3d(Source.GetNormal(SourceIndex)) * Weight;
                    if (bCoverage)
                    {
                        CoverageSum += Source.Coverage[SourceIndex] * Weight;
                    }
                    WeightSum += Weight;
                }
            }

            const FVector3f Normal = FVector3f(NormalSum.GetSafeNormal(
                UE_DOUBLE_SMALL_NUMBER,
                FVector3d(0.0, 0.0, 1.0)));
            const float Alpha = bCoverage && WeightSum > UE_DOUBLE_SMALL_NUMBER
                ? FMath::Clamp(static_cast<float>(CoverageSum / WeightSum), 0.0f, 1.0f)
                : 1.0f;
            InOutPixels[DestinationY * DestinationSize.X + DestinationX] = EncodeNormal(Normal, Alpha);
        }
    }
    return true;
}

bool FDWCEditorRasterPostProcess::ResampleAndEncodeNormalRegion(
    const FDWCEditorNormalRasterRegion& SourceRegion,
    const FIntPoint DestinationSize,
    const FIntRect& DestinationRect,
    TArray<FColor>& OutPixels,
    const bool bEncodeCoverageInAlpha)
{
    if (!SourceRegion.IsValid() || DestinationSize.X <= 0 || DestinationSize.Y <= 0)
    {
        OutPixels.Reset();
        return false;
    }
    const FIntRect Rect = ClampRect(DestinationRect, DestinationSize);
    if (Rect.IsEmpty())
    {
        OutPixels.Reset();
        return false;
    }
    const int64 PixelCount64 = static_cast<int64>(Rect.Width()) * Rect.Height();
    if (PixelCount64 <= 0 || PixelCount64 > MAX_int32)
    {
        OutPixels.Reset();
        return false;
    }
    OutPixels.SetNumUninitialized(static_cast<int32>(PixelCount64));
    const bool bCoverage = bEncodeCoverageInAlpha && SourceRegion.Surface.HasCoverage();

    for (int32 DestinationY = Rect.Min.Y; DestinationY < Rect.Max.Y; ++DestinationY)
    {
        const double SourceMinY = static_cast<double>(DestinationY) * SourceRegion.CanvasSize.Y / DestinationSize.Y;
        const double SourceMaxY = static_cast<double>(DestinationY + 1) * SourceRegion.CanvasSize.Y / DestinationSize.Y;
        for (int32 DestinationX = Rect.Min.X; DestinationX < Rect.Max.X; ++DestinationX)
        {
            const double SourceMinX = static_cast<double>(DestinationX) * SourceRegion.CanvasSize.X / DestinationSize.X;
            const double SourceMaxX = static_cast<double>(DestinationX + 1) * SourceRegion.CanvasSize.X / DestinationSize.X;
            FVector3d NormalSum = FVector3d::ZeroVector;
            double CoverageSum = 0.0;
            double WeightSum = 0.0;
            for (int32 SourceY = FMath::FloorToInt(SourceMinY); SourceY < FMath::CeilToInt(SourceMaxY); ++SourceY)
            {
                const double WeightY = FMath::Max(
                    0.0,
                    FMath::Min(SourceMaxY, SourceY + 1.0) - FMath::Max(SourceMinY, static_cast<double>(SourceY)));
                const int32 Y = FMath::Clamp(SourceY, 0, SourceRegion.CanvasSize.Y - 1);
                for (int32 SourceX = FMath::FloorToInt(SourceMinX); SourceX < FMath::CeilToInt(SourceMaxX); ++SourceX)
                {
                    const double WeightX = FMath::Max(
                        0.0,
                        FMath::Min(SourceMaxX, SourceX + 1.0) - FMath::Max(SourceMinX, static_cast<double>(SourceX)));
                    const int32 X = FMath::Clamp(SourceX, 0, SourceRegion.CanvasSize.X - 1);
                    if (!SourceRegion.Contains(X, Y))
                    {
                        OutPixels.Reset();
                        return false;
                    }
                    const double Weight = WeightX * WeightY;
                    NormalSum += FVector3d(SourceRegion.GetNormal(X, Y)) * Weight;
                    if (bCoverage)
                    {
                        CoverageSum += SourceRegion.GetCoverage(X, Y) * Weight;
                    }
                    WeightSum += Weight;
                }
            }
            const FVector3f Normal = FVector3f(NormalSum.GetSafeNormal(
                UE_DOUBLE_SMALL_NUMBER,
                FVector3d(0.0, 0.0, 1.0)));
            const float Alpha = bCoverage && WeightSum > UE_DOUBLE_SMALL_NUMBER
                ? FMath::Clamp(static_cast<float>(CoverageSum / WeightSum), 0.0f, 1.0f)
                : 1.0f;
            const int32 LocalIndex = (DestinationY - Rect.Min.Y) * Rect.Width() +
                (DestinationX - Rect.Min.X);
            OutPixels[LocalIndex] = EncodeNormal(Normal, Alpha);
        }
    }
    return true;
}

void FDWCEditorRasterPostProcess::EncodeCoveragePixels(
    const FDWCEditorNormalRasterSurface& Surface,
    TArray<uint8>& OutPixels)
{
    if (!Surface.HasCoverage())
    {
        OutPixels.Reset();
        return;
    }
    OutPixels.SetNumUninitialized(Surface.Coverage.Num());
    for (int32 Index = 0; Index < Surface.Coverage.Num(); ++Index)
    {
        OutPixels[Index] = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Surface.Coverage[Index], 0.0f, 1.0f) * 255.0f));
    }
}

void FDWCEditorRasterPostProcess::ClipToMask(
    FDWCEditorNormalRasterSurface& Surface,
    const TConstArrayView<uint8> Mask)
{
    if (!Surface.IsValid() || Mask.Num() != Surface.GetPixelCount())
    {
        return;
    }
    for (int32 Index = 0; Index < Mask.Num(); ++Index)
    {
        if (Mask[Index] == 0)
        {
            Surface.SetNormal(Index, FVector3f(0.0f, 0.0f, 1.0f));
            if (Surface.HasCoverage())
            {
                Surface.Coverage[Index] = 0.0f;
            }
        }
    }
}

void FDWCEditorRasterPostProcess::DilateIntoPadding(
    FDWCEditorNormalRasterSurface& Surface,
    const TConstArrayView<uint8> IslandMask,
    const int32 PaddingPixels)
{
    const int32 Padding = FMath::Clamp(PaddingPixels, 0, 64);
    if (!Surface.IsValid() || Padding <= 0 || IslandMask.Num() != Surface.GetPixelCount())
    {
        return;
    }

    TArray<uint8> Distance;
    Distance.Init(MAX_uint8, Surface.GetPixelCount());
    TArray<int32> Frontier;
    for (int32 Y = 0; Y < Surface.Size.Y; ++Y)
    {
        for (int32 X = 0; X < Surface.Size.X; ++X)
        {
            const int32 Index = Y * Surface.Size.X + X;
            if (IslandMask[Index] == 0)
            {
                continue;
            }
            Distance[Index] = 0;
            bool bBoundary = false;
            for (int32 OffsetY = -1; OffsetY <= 1 && !bBoundary; ++OffsetY)
            {
                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    if (OffsetX == 0 && OffsetY == 0)
                    {
                        continue;
                    }
                    const int32 NX = X + OffsetX;
                    const int32 NY = Y + OffsetY;
                    bBoundary = NX >= 0 && NX < Surface.Size.X && NY >= 0 && NY < Surface.Size.Y &&
                        IslandMask[NY * Surface.Size.X + NX] == 0;
                    if (bBoundary)
                    {
                        break;
                    }
                }
            }
            if (bBoundary)
            {
                Frontier.Add(Index);
            }
        }
    }

    TArray<int32> NextFrontier;
    for (int32 Step = 1; Step <= Padding && !Frontier.IsEmpty(); ++Step)
    {
        NextFrontier.Reset();
        for (const int32 SourceIndex : Frontier)
        {
            const int32 SourceX = SourceIndex % Surface.Size.X;
            const int32 SourceY = SourceIndex / Surface.Size.X;
            for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
            {
                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    if (OffsetX == 0 && OffsetY == 0)
                    {
                        continue;
                    }
                    const int32 X = SourceX + OffsetX;
                    const int32 Y = SourceY + OffsetY;
                    if (X < 0 || X >= Surface.Size.X || Y < 0 || Y >= Surface.Size.Y)
                    {
                        continue;
                    }
                    const int32 Index = Y * Surface.Size.X + X;
                    if (IslandMask[Index] != 0 || Distance[Index] < Step)
                    {
                        continue;
                    }
                    if (Distance[Index] == MAX_uint8)
                    {
                        Distance[Index] = static_cast<uint8>(Step);
                        Surface.PackedNormalXY[Index] = Surface.PackedNormalXY[SourceIndex];
                        if (Surface.HasCoverage())
                        {
                            Surface.Coverage[Index] = Surface.Coverage[SourceIndex];
                        }
                        NextFrontier.Add(Index);
                    }
                    else if (Surface.HasCoverage() && Surface.Coverage[SourceIndex] > Surface.Coverage[Index])
                    {
                        Surface.PackedNormalXY[Index] = Surface.PackedNormalXY[SourceIndex];
                        Surface.Coverage[Index] = Surface.Coverage[SourceIndex];
                    }
                }
            }
        }
        Swap(Frontier, NextFrontier);
    }
}
