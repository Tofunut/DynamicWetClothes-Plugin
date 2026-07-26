#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

float FDWCTransparencyComposite::ResolveFinalAlpha(
    const float EditedAlpha,
    const float TransparencyStrength,
    const float WrinkleSuppression,
    const float WrinkleSuppressionStrength)
{
    const float Suppression = FMath::Clamp(
        WrinkleSuppression * FMath::Max(WrinkleSuppressionStrength, 0.0f),
        0.0f,
        1.0f);
    return FMath::Clamp(
        FMath::Clamp(EditedAlpha, 0.0f, 1.0f) *
        FMath::Max(TransparencyStrength, 0.0f) *
        (1.0f - Suppression),
        0.0f,
        1.0f);
}

uint8 FDWCTransparencyComposite::ResolveFinalAlpha8(
    const float EditedAlpha,
    const float TransparencyStrength,
    const uint8 WrinkleSuppression,
    const float WrinkleSuppressionStrength)
{
    return static_cast<uint8>(FMath::RoundToInt(
        ResolveFinalAlpha(
            EditedAlpha,
            TransparencyStrength,
            WrinkleSuppression / 255.0f,
            WrinkleSuppressionStrength) * 255.0f));
}

bool FDWCTransparencyComposite::BuildCoverageEdgeFeatherBuffer(
    const FIntPoint Resolution,
    const TArray<uint8>& OuterCoverage,
    const float FeatherPixels,
    TArray<uint8>& OutBuffer)
{
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || OuterCoverage.Num() != PixelCount)
    {
        OutBuffer.Reset();
        return false;
    }

    OutBuffer.Init(255, PixelCount);
    const int32 FeatherSteps = FMath::CeilToInt(FMath::Max(FeatherPixels, 0.0f));
    if (FeatherSteps <= 0)
    {
        return true;
    }

    // The old erosion loop rescanned the full image once for every feather pixel.
    // A two-pass Manhattan distance transform gives the same four-neighbor edge
    // distance in O(width * height), regardless of the configured feather radius.
    constexpr uint16 InfiniteDistance = MAX_uint16;
    TArray<uint16> DistanceToCoverageEdge;
    DistanceToCoverageEdge.Init(InfiniteDistance, PixelCount);

    for (int32 Y = 0; Y < Resolution.Y; ++Y)
    {
        for (int32 X = 0; X < Resolution.X; ++X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                DistanceToCoverageEdge[PixelIndex] = 0;
                continue;
            }

            // The virtual uncovered texels immediately outside the texture are
            // distance zero, so covered border texels begin at distance one.
            if (X == 0 || Y == 0 || X == Resolution.X - 1 || Y == Resolution.Y - 1)
            {
                DistanceToCoverageEdge[PixelIndex] = 1;
            }
        }
    }

    auto RelaxDistance = [&DistanceToCoverageEdge](const int32 PixelIndex, const int32 NeighborIndex)
    {
        const uint16 NeighborDistance = DistanceToCoverageEdge[NeighborIndex];
        if (NeighborDistance < InfiniteDistance)
        {
            DistanceToCoverageEdge[PixelIndex] = FMath::Min<uint16>(
                DistanceToCoverageEdge[PixelIndex],
                static_cast<uint16>(NeighborDistance + 1));
        }
    };

    for (int32 Y = 0; Y < Resolution.Y; ++Y)
    {
        for (int32 X = 0; X < Resolution.X; ++X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                continue;
            }
            if (X > 0)
            {
                RelaxDistance(PixelIndex, PixelIndex - 1);
            }
            if (Y > 0)
            {
                RelaxDistance(PixelIndex, PixelIndex - Resolution.X);
            }
        }
    }

    for (int32 Y = Resolution.Y - 1; Y >= 0; --Y)
    {
        for (int32 X = Resolution.X - 1; X >= 0; --X)
        {
            const int32 PixelIndex = Y * Resolution.X + X;
            if (OuterCoverage[PixelIndex] == 0)
            {
                continue;
            }
            if (X + 1 < Resolution.X)
            {
                RelaxDistance(PixelIndex, PixelIndex + 1);
            }
            if (Y + 1 < Resolution.Y)
            {
                RelaxDistance(PixelIndex, PixelIndex + Resolution.X);
            }
        }
    }

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const uint16 Distance = DistanceToCoverageEdge[PixelIndex];
        if (OuterCoverage[PixelIndex] == 0 || Distance == InfiniteDistance || Distance > FeatherSteps)
        {
            continue;
        }

        OutBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(
            static_cast<float>(Distance) / (static_cast<float>(FeatherSteps) + 1.0f),
            0.0f,
            1.0f) * 255.0f));
    }
    return true;
}
