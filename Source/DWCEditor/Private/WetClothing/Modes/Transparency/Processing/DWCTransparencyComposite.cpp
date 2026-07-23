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

    TArray<uint8> RemainingCoverage = OuterCoverage;
    TArray<int32> BoundaryPixels;
    BoundaryPixels.Reserve(FMath::Max(Resolution.X, Resolution.Y) * 4);
    const FIntPoint Neighbors[] =
    {
        FIntPoint(-1, 0),
        FIntPoint(1, 0),
        FIntPoint(0, -1),
        FIntPoint(0, 1)
    };

    for (int32 Step = 0; Step < FeatherSteps; ++Step)
    {
        BoundaryPixels.Reset();
        for (int32 Y = 0; Y < Resolution.Y; ++Y)
        {
            for (int32 X = 0; X < Resolution.X; ++X)
            {
                const int32 PixelIndex = Y * Resolution.X + X;
                if (RemainingCoverage[PixelIndex] == 0)
                {
                    continue;
                }

                bool bBoundary = false;
                for (const FIntPoint& Offset : Neighbors)
                {
                    const int32 NeighborX = X + Offset.X;
                    const int32 NeighborY = Y + Offset.Y;
                    if (NeighborX < 0 || NeighborY < 0 ||
                        NeighborX >= Resolution.X || NeighborY >= Resolution.Y ||
                        RemainingCoverage[NeighborY * Resolution.X + NeighborX] == 0)
                    {
                        bBoundary = true;
                        break;
                    }
                }
                if (bBoundary)
                {
                    BoundaryPixels.Add(PixelIndex);
                }
            }
        }

        if (BoundaryPixels.IsEmpty())
        {
            break;
        }

        const uint8 EdgeWeight = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(
            (static_cast<float>(Step) + 1.0f) / (static_cast<float>(FeatherSteps) + 1.0f),
            0.0f,
            1.0f) * 255.0f));
        for (const int32 PixelIndex : BoundaryPixels)
        {
            OutBuffer[PixelIndex] = EdgeWeight;
            RemainingCoverage[PixelIndex] = 0;
        }
    }
    return true;
}
