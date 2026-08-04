#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"

#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

namespace
{
    int32 WrapIndex(const int32 Value, const int32 Size)
    {
        return (Value % Size + Size) % Size;
    }

    float ResolveEditedAlphaInternal(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        const int32 PixelIndex)
    {
        const float AutoAlpha = AutoResult.AutoAlphaBuffer.IsValidIndex(PixelIndex)
            ? AutoResult.AutoAlphaBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualPremultiplied = ManualPremultipliedBuffer.IsValidIndex(PixelIndex)
            ? ManualPremultipliedBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualWeight = ManualWeightBuffer.IsValidIndex(PixelIndex)
            ? ManualWeightBuffer[PixelIndex] / 255.0f
            : 0.0f;
        return FMath::Clamp(AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied, 0.0f, 1.0f);
    }

    bool PassesIslandClip(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const int32 PixelIndex,
        const int32 UVIslandID)
    {
        if (UVIslandID == INDEX_NONE)
        {
            return true;
        }
        return AutoResult.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencyAutoBakeResult::MatchesOuterIslandID(
                AutoResult.OuterIslandIDBuffer[PixelIndex],
                UVIslandID);
    }

    int32 ResolveSampleIslandID(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyBrushSample& Sample,
        const int32 Width,
        const int32 Height,
        const bool bWrap)
    {
        if (Sample.UVIslandID != INDEX_NONE)
        {
            return Sample.UVIslandID;
        }
        if (AutoResult.OuterIslandIDBuffer.Num() != Width * Height)
        {
            return INDEX_NONE;
        }

        int32 X = FMath::FloorToInt(Sample.PositionUV.X * Width);
        int32 Y = FMath::FloorToInt(Sample.PositionUV.Y * Height);
        if (bWrap)
        {
            X = WrapIndex(X, Width);
            Y = WrapIndex(Y, Height);
        }
        else if (X < 0 || X >= Width || Y < 0 || Y >= Height)
        {
            return INDEX_NONE;
        }
        else
        {
            X = FMath::Clamp(X, 0, Width - 1);
            Y = FMath::Clamp(Y, 0, Height - 1);
        }
        return FDWCTransparencyAutoBakeResult::DecodeOuterIslandID(
            AutoResult.OuterIslandIDBuffer[Y * Width + X]);
    }

    void ApplySample(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample,
        TArray<uint8>& ManualPremultipliedBuffer,
        TArray<uint8>& ManualWeightBuffer)
    {
        const int32 Width = AutoResult.Resolution.X;
        const int32 Height = AutoResult.Resolution.Y;
        if (Width <= 0 || Height <= 0)
        {
            return;
        }

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
        const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
        const FVector2D CenterPixels(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
        const int32 MinX = FMath::FloorToInt(CenterPixels.X - RadiusPixelsX - 1.0f);
        const int32 MaxX = FMath::CeilToInt(CenterPixels.X + RadiusPixelsX + 1.0f);
        const int32 MinY = FMath::FloorToInt(CenterPixels.Y - RadiusPixelsY - 1.0f);
        const int32 MaxY = FMath::CeilToInt(CenterPixels.Y + RadiusPixelsY + 1.0f);
        const int32 ClipUVIslandID = ResolveSampleIslandID(
            AutoResult,
            Sample,
            Width,
            Height,
            bWrap);

        for (int32 UnwrappedY = MinY; UnwrappedY <= MaxY; ++UnwrappedY)
        {
            for (int32 UnwrappedX = MinX; UnwrappedX <= MaxX; ++UnwrappedX)
            {
                if (!bWrap && (UnwrappedX < 0 || UnwrappedX >= Width || UnwrappedY < 0 || UnwrappedY >= Height))
                {
                    continue;
                }

                const float DX = (UnwrappedX + 0.5f - CenterPixels.X) / RadiusPixelsX;
                const float DY = (UnwrappedY + 0.5f - CenterPixels.Y) / RadiusPixelsY;
                const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                if (Distance > 1.0f)
                {
                    continue;
                }

                const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
                const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                    ? 1.0f
                    : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                const float BrushWeight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                if (BrushWeight <= 0.0f)
                {
                    continue;
                }

                const int32 X = bWrap ? WrapIndex(UnwrappedX, Width) : UnwrappedX;
                const int32 Y = bWrap ? WrapIndex(UnwrappedY, Height) : UnwrappedY;
                const int32 PixelIndex = Y * Width + X;
                if (!PassesIslandClip(AutoResult, PixelIndex, ClipUVIslandID))
                {
                    continue;
                }

                const float OldPremultiplied = ManualPremultipliedBuffer[PixelIndex] / 255.0f;
                const float OldWeight = ManualWeightBuffer[PixelIndex] / 255.0f;
                float NewPremultiplied = OldPremultiplied;
                float NewWeight = OldWeight;

                if (Stroke.BrushMode == EDWCTransparencyBrushMode::ResetToAuto)
                {
                    NewPremultiplied *= 1.0f - BrushWeight;
                    NewWeight *= 1.0f - BrushWeight;
                }
                else
                {
                    float Target = Stroke.TargetAlpha;
                    if (Stroke.BrushMode == EDWCTransparencyBrushMode::Apply)
                    {
                        Target = 1.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Erase)
                    {
                        Target = 0.0f;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
                    {
                        Target = 0.0f;
                        int32 SmoothSampleCount = 0;
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                int32 SampleX = X + OffsetX;
                                int32 SampleY = Y + OffsetY;
                                if (bWrap)
                                {
                                    SampleX = WrapIndex(SampleX, Width);
                                    SampleY = WrapIndex(SampleY, Height);
                                }
                                else
                                {
                                    SampleX = FMath::Clamp(SampleX, 0, Width - 1);
                                    SampleY = FMath::Clamp(SampleY, 0, Height - 1);
                                }
                                const int32 NeighborIndex = SampleY * Width + SampleX;
                                if (PassesIslandClip(AutoResult, NeighborIndex, ClipUVIslandID))
                                {
                                    Target += ResolveEditedAlphaInternal(AutoResult, ManualPremultipliedBuffer, ManualWeightBuffer, NeighborIndex);
                                    ++SmoothSampleCount;
                                }
                            }
                        }
                        Target = SmoothSampleCount > 0
                            ? Target / static_cast<float>(SmoothSampleCount)
                            : ResolveEditedAlphaInternal(AutoResult, ManualPremultipliedBuffer, ManualWeightBuffer, PixelIndex);
                    }

                    NewPremultiplied = Target * BrushWeight + OldPremultiplied * (1.0f - BrushWeight);
                    NewWeight = BrushWeight + OldWeight * (1.0f - BrushWeight);
                }

                ManualPremultipliedBuffer[PixelIndex] =
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewPremultiplied, 0.0f, 1.0f) * 255.0f));
                ManualWeightBuffer[PixelIndex] =
                    static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewWeight, 0.0f, 1.0f) * 255.0f));
            }
        }
    }
}

void FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const int32 BaselineStrokeCount,
    const int32 MaterialSlotIndex,
    const int32 /*UVChannelIndex*/,
    TArray<uint8>& OutManualPremultipliedBuffer,
    TArray<uint8>& OutManualWeightBuffer)
{
    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    if (PixelCount <= 0)
    {
        return;
    }

    const int32 FirstStrokeIndex = FMath::Clamp(
        BaselineStrokeCount,
        0,
        Strokes.Num());

    bool bHasRelevantStrokeSamples = false;
    for (int32 StrokeIndex = FirstStrokeIndex; StrokeIndex < Strokes.Num(); ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Strokes[StrokeIndex];
        if (Stroke.bEnabled &&
            Stroke.MaterialSlotIndex == MaterialSlotIndex &&
            !Stroke.Samples.IsEmpty())
        {
            bHasRelevantStrokeSamples = true;
            break;
        }
    }

    if (!bHasRelevantStrokeSamples)
    {
        return;
    }

    OutManualPremultipliedBuffer.Init(0, PixelCount);
    OutManualWeightBuffer.Init(0, PixelCount);
    for (int32 StrokeIndex = FirstStrokeIndex; StrokeIndex < Strokes.Num(); ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Strokes[StrokeIndex];
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            ApplySample(AutoResult, Stroke, Sample, OutManualPremultipliedBuffer, OutManualWeightBuffer);
        }
    }
}

float FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const TArray<uint8>& ManualPremultipliedBuffer,
    const TArray<uint8>& ManualWeightBuffer,
    const int32 PixelIndex)
{
    return ResolveEditedAlphaInternal(AutoResult, ManualPremultipliedBuffer, ManualWeightBuffer, PixelIndex);
}
