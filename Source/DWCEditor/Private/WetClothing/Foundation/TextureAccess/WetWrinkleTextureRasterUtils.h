#pragma once

#include "CoreMinimal.h"

namespace WetWrinkleTextureRaster
{
    inline constexpr int32 InternalBakeResolution = 2048;
    inline constexpr int32 MinTextureResolution = 16;
    inline constexpr int32 MaxTextureResolution = 8192;

    inline FIntPoint ResolveFinalTextureSize(const int32 Resolution)
    {
        const int32 ClampedResolution = FMath::Clamp(Resolution, MinTextureResolution, MaxTextureResolution);
        return FIntPoint(ClampedResolution, ClampedResolution);
    }

    inline FIntPoint ResolveWorkingTextureSize(const FIntPoint& FinalTextureSize)
    {
        const int32 WorkingResolution = FMath::Max(
            InternalBakeResolution,
            FMath::Max(FinalTextureSize.X, FinalTextureSize.Y));
        return FIntPoint(WorkingResolution, WorkingResolution);
    }

    inline FVector DecodeNormal(const FColor& Color)
    {
        const FVector Decoded(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
        return Decoded.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
    }

    inline FColor EncodeNormal(const FVector& Normal, const float Alpha = 1.0f)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Alpha, 0.0f, 1.0f) * 255.0f)));
    }

    inline FIntRect MapWorkingRectToFinal(
        const FIntRect& WorkingRect,
        const FIntPoint& WorkingSize,
        const FIntPoint& FinalSize)
    {
        if (WorkingRect.Width() <= 0 || WorkingRect.Height() <= 0 ||
            WorkingSize.X <= 0 || WorkingSize.Y <= 0 || FinalSize.X <= 0 || FinalSize.Y <= 0)
        {
            return FIntRect();
        }

        const int32 MinX = FMath::Clamp(
            FMath::FloorToInt(static_cast<double>(WorkingRect.Min.X) * FinalSize.X / WorkingSize.X) - 1,
            0,
            FinalSize.X);
        const int32 MinY = FMath::Clamp(
            FMath::FloorToInt(static_cast<double>(WorkingRect.Min.Y) * FinalSize.Y / WorkingSize.Y) - 1,
            0,
            FinalSize.Y);
        const int32 MaxX = FMath::Clamp(
            FMath::CeilToInt(static_cast<double>(WorkingRect.Max.X) * FinalSize.X / WorkingSize.X) + 1,
            0,
            FinalSize.X);
        const int32 MaxY = FMath::Clamp(
            FMath::CeilToInt(static_cast<double>(WorkingRect.Max.Y) * FinalSize.Y / WorkingSize.Y) + 1,
            0,
            FinalSize.Y);
        return FIntRect(MinX, MinY, MaxX, MaxY);
    }

    template <typename SampleNormalType, typename SampleAlphaType, typename StoreType>
    inline void DownsampleArea(
        const FIntPoint& SourceSize,
        const FIntPoint& DestinationSize,
        const FIntRect& DestinationRect,
        SampleNormalType&& SampleNormal,
        SampleAlphaType&& SampleAlpha,
        StoreType&& Store)
    {
        const FIntRect ClampedRect(
            FMath::Clamp(DestinationRect.Min.X, 0, DestinationSize.X),
            FMath::Clamp(DestinationRect.Min.Y, 0, DestinationSize.Y),
            FMath::Clamp(DestinationRect.Max.X, 0, DestinationSize.X),
            FMath::Clamp(DestinationRect.Max.Y, 0, DestinationSize.Y));

        for (int32 DestinationY = ClampedRect.Min.Y; DestinationY < ClampedRect.Max.Y; ++DestinationY)
        {
            const double SourceMinY = static_cast<double>(DestinationY) * SourceSize.Y / DestinationSize.Y;
            const double SourceMaxY = static_cast<double>(DestinationY + 1) * SourceSize.Y / DestinationSize.Y;
            const int32 FirstSourceY = FMath::FloorToInt(SourceMinY);
            const int32 LastSourceY = FMath::CeilToInt(SourceMaxY) - 1;

            for (int32 DestinationX = ClampedRect.Min.X; DestinationX < ClampedRect.Max.X; ++DestinationX)
            {
                const double SourceMinX = static_cast<double>(DestinationX) * SourceSize.X / DestinationSize.X;
                const double SourceMaxX = static_cast<double>(DestinationX + 1) * SourceSize.X / DestinationSize.X;
                const int32 FirstSourceX = FMath::FloorToInt(SourceMinX);
                const int32 LastSourceX = FMath::CeilToInt(SourceMaxX) - 1;

                FVector NormalSum = FVector::ZeroVector;
                double AlphaSum = 0.0;
                double WeightSum = 0.0;
                for (int32 SourceY = FirstSourceY; SourceY <= LastSourceY; ++SourceY)
                {
                    const int32 ClampedSourceY = FMath::Clamp(SourceY, 0, SourceSize.Y - 1);
                    const double WeightY = FMath::Max(
                        0.0,
                        FMath::Min(SourceMaxY, static_cast<double>(SourceY + 1)) -
                            FMath::Max(SourceMinY, static_cast<double>(SourceY)));
                    for (int32 SourceX = FirstSourceX; SourceX <= LastSourceX; ++SourceX)
                    {
                        const int32 ClampedSourceX = FMath::Clamp(SourceX, 0, SourceSize.X - 1);
                        const double WeightX = FMath::Max(
                            0.0,
                            FMath::Min(SourceMaxX, static_cast<double>(SourceX + 1)) -
                                FMath::Max(SourceMinX, static_cast<double>(SourceX)));
                        const double Weight = WeightX * WeightY;
                        const int32 SourceIndex = ClampedSourceY * SourceSize.X + ClampedSourceX;
                        NormalSum += SampleNormal(SourceIndex) * Weight;
                        AlphaSum += static_cast<double>(SampleAlpha(SourceIndex)) * Weight;
                        WeightSum += Weight;
                    }
                }

                const FVector FilteredNormal = NormalSum.GetSafeNormal(
                    UE_SMALL_NUMBER,
                    FVector(0.0f, 0.0f, 1.0f));
                const float FilteredAlpha = WeightSum > UE_DOUBLE_SMALL_NUMBER
                    ? static_cast<float>(AlphaSum / WeightSum)
                    : 0.0f;
                Store(DestinationY * DestinationSize.X + DestinationX, FilteredNormal, FilteredAlpha);
            }
        }
    }

    inline void DownsampleNormalCoverage(
        const TArray<FVector3f>& SourceNormals,
        const TArray<float>& SourceCoverage,
        const FIntPoint& SourceSize,
        const FIntPoint& DestinationSize,
        TArray<FVector3f>& OutNormals,
        TArray<float>& OutCoverage)
    {
        if (SourceSize.X <= 0 || SourceSize.Y <= 0 || DestinationSize.X <= 0 || DestinationSize.Y <= 0 ||
            SourceNormals.Num() != SourceSize.X * SourceSize.Y || SourceCoverage.Num() != SourceNormals.Num())
        {
            OutNormals.Reset();
            OutCoverage.Reset();
            return;
        }

        if (SourceSize == DestinationSize)
        {
            OutNormals = SourceNormals;
            OutCoverage = SourceCoverage;
            return;
        }

        OutNormals.Init(FVector3f(0.0f, 0.0f, 1.0f), DestinationSize.X * DestinationSize.Y);
        OutCoverage.Init(0.0f, DestinationSize.X * DestinationSize.Y);
        DownsampleArea(
            SourceSize,
            DestinationSize,
            FIntRect(FIntPoint::ZeroValue, DestinationSize),
            [&SourceNormals](const int32 Index) { return FVector(SourceNormals[Index]); },
            [&SourceCoverage](const int32 Index) { return SourceCoverage[Index]; },
            [&OutNormals, &OutCoverage](const int32 Index, const FVector& Normal, const float Coverage)
            {
                OutNormals[Index] = FVector3f(Normal);
                OutCoverage[Index] = FMath::Clamp(Coverage, 0.0f, 1.0f);
            });
    }

    inline void DownsampleNormalPixels(
        const TArray<FColor>& SourcePixels,
        const FIntPoint& SourceSize,
        const FIntPoint& DestinationSize,
        TArray<FColor>& InOutDestinationPixels,
        const FIntRect* DestinationRect = nullptr)
    {
        if (SourceSize.X <= 0 || SourceSize.Y <= 0 || DestinationSize.X <= 0 || DestinationSize.Y <= 0 ||
            SourcePixels.Num() != SourceSize.X * SourceSize.Y)
        {
            InOutDestinationPixels.Reset();
            return;
        }

        const int32 DestinationPixelCount = DestinationSize.X * DestinationSize.Y;
        if (InOutDestinationPixels.Num() != DestinationPixelCount)
        {
            InOutDestinationPixels.Init(EncodeNormal(FVector(0.0f, 0.0f, 1.0f)), DestinationPixelCount);
        }

        const FIntRect FullRect(FIntPoint::ZeroValue, DestinationSize);
        const FIntRect& Rect = DestinationRect != nullptr ? *DestinationRect : FullRect;
        if (SourceSize == DestinationSize)
        {
            const FIntRect ClampedRect(
                FMath::Clamp(Rect.Min.X, 0, DestinationSize.X),
                FMath::Clamp(Rect.Min.Y, 0, DestinationSize.Y),
                FMath::Clamp(Rect.Max.X, 0, DestinationSize.X),
                FMath::Clamp(Rect.Max.Y, 0, DestinationSize.Y));
            for (int32 Y = ClampedRect.Min.Y; Y < ClampedRect.Max.Y; ++Y)
            {
                FMemory::Memcpy(
                    InOutDestinationPixels.GetData() + Y * DestinationSize.X + ClampedRect.Min.X,
                    SourcePixels.GetData() + Y * SourceSize.X + ClampedRect.Min.X,
                    static_cast<SIZE_T>(ClampedRect.Width()) * sizeof(FColor));
            }
            return;
        }

        DownsampleArea(
            SourceSize,
            DestinationSize,
            Rect,
            [&SourcePixels](const int32 Index) { return DecodeNormal(SourcePixels[Index]); },
            [&SourcePixels](const int32 Index) { return static_cast<float>(SourcePixels[Index].A) / 255.0f; },
            [&InOutDestinationPixels](const int32 Index, const FVector& Normal, const float Alpha)
            {
                InOutDestinationPixels[Index] = EncodeNormal(Normal, Alpha);
            });
    }
}
