#include "WetClothing/TransparencyBake/Brush/DWCTransparencyBrushRasterizer.h"

#include "WetClothing/TransparencyBake/AutoMap/DWCTransparencyAutoMapGenerator.h"

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
            ? FMath::Clamp(AutoResult.AutoAlphaBuffer[PixelIndex], 0.0f, 1.0f)
            : 0.0f;
        const float ManualPremultiplied = ManualPremultipliedBuffer.IsValidIndex(PixelIndex)
            ? ManualPremultipliedBuffer[PixelIndex] / 255.0f
            : 0.0f;
        const float ManualWeight = ManualWeightBuffer.IsValidIndex(PixelIndex)
            ? ManualWeightBuffer[PixelIndex] / 255.0f
            : 0.0f;
        return FMath::Clamp(AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied, 0.0f, 1.0f);
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
                                Target += ResolveEditedAlphaInternal(AutoResult, ManualPremultipliedBuffer, ManualWeightBuffer, SampleY * Width + SampleX);
                            }
                        }
                        Target /= 9.0f;
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
    const FWetClothingTransparencyLayerData& Layer,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    TArray<uint8>& OutManualPremultipliedBuffer,
    TArray<uint8>& OutManualWeightBuffer)
{
    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    OutManualPremultipliedBuffer.Init(0, PixelCount);
    OutManualWeightBuffer.Init(0, PixelCount);
    if (PixelCount <= 0)
    {
        return;
    }

    for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
    {
        if (!Stroke.bEnabled ||
            Stroke.MaterialSlotIndex != MaterialSlotIndex ||
            Stroke.UVChannelIndex != UVChannelIndex)
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
