//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

void FDWCTransparencyRevealSurfaceAuthoringPayload::Init(
    const FIntPoint InResolution,
    const FColor InitialValue)
{
    Resolution = InResolution;
    const int64 PixelCount = static_cast<int64>(Resolution.X) * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || PixelCount > MAX_int32)
    {
        Reset();
        return;
    }
    PackedPixels.Init(InitialValue, static_cast<int32>(PixelCount));
}

void FDWCTransparencyRevealSurfaceAuthoringPayload::SetNumUninitialized(
    const FIntPoint InResolution)
{
    Resolution = InResolution;
    const int64 PixelCount = static_cast<int64>(Resolution.X) * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || PixelCount > MAX_int32)
    {
        Reset();
        return;
    }
    PackedPixels.SetNumUninitialized(static_cast<int32>(PixelCount));
}

void FDWCTransparencyRevealSurfaceAuthoringPayload::Reset()
{
    Resolution = FIntPoint::ZeroValue;
    PackedPixels.Reset();
}

bool FDWCTransparencyRevealSurfaceAuthoringPayload::IsValid() const
{
    return IsValidForResolution(Resolution);
}

bool FDWCTransparencyRevealSurfaceAuthoringPayload::IsValidForResolution(
    const FIntPoint ExpectedResolution) const
{
    const int64 PixelCount = static_cast<int64>(ExpectedResolution.X) * ExpectedResolution.Y;
    return ExpectedResolution.X > 0 && ExpectedResolution.Y > 0 &&
        ExpectedResolution == Resolution && PixelCount <= MAX_int32 &&
        PackedPixels.Num() == static_cast<int32>(PixelCount);
}

FVector3f FDWCTransparencyRevealSurfaceAuthoringPayload::DecodeRevealNormal(
    const int32 PixelIndex) const
{
    if (!PackedPixels.IsValidIndex(PixelIndex))
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }

    const FColor Packed = PackedPixels[PixelIndex];
    const float X = static_cast<float>(Packed.R) / 127.5f - 1.0f;
    const float Y = static_cast<float>(Packed.G) / 127.5f - 1.0f;
    const float Z = FMath::Sqrt(FMath::Max(1.0f - X * X - Y * Y, 0.0f));
    const FVector3f Normal(X, Y, Z);
    return Normal.IsNearlyZero()
        ? FVector3f(0.0f, 0.0f, 1.0f)
        : Normal.GetSafeNormal();
}

FColor FDWCTransparencyRevealSurfaceAuthoringPayload::EncodeRuntimeRevealNormal(
    const FColor PackedAuthoringPixel)
{
    const float X = static_cast<float>(PackedAuthoringPixel.R) / 127.5f - 1.0f;
    const float Y = static_cast<float>(PackedAuthoringPixel.G) / 127.5f - 1.0f;
    const float Coverage = static_cast<float>(PackedAuthoringPixel.A) / 255.0f;
    const FVector2f CoveredXY = FVector2f(X, Y) * Coverage;
    const FVector2f ClampedXY = CoveredXY.GetClampedToMaxSize(1.0f);
    return FColor(
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(ClampedXY.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
        static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(ClampedXY.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
        255,
        255);
}

float FDWCTransparencyRevealSurfaceAuthoringPayload::GetInnerMetallic(
    const int32 PixelIndex) const
{
    return PackedPixels.IsValidIndex(PixelIndex)
        ? static_cast<float>(PackedPixels[PixelIndex].B) / 255.0f
        : 0.0f;
}

float FDWCTransparencyRevealSurfaceAuthoringPayload::GetSourceCoverage(
    const int32 PixelIndex) const
{
    return PackedPixels.IsValidIndex(PixelIndex)
        ? static_cast<float>(PackedPixels[PixelIndex].A) / 255.0f
        : 0.0f;
}

bool FDWCTransparencyRevealSurfaceAuthoringPayload::HasValidSource(
    const int32 PixelIndex) const
{
    return PackedPixels.IsValidIndex(PixelIndex) && PackedPixels[PixelIndex].A != 0;
}

uint64 FDWCTransparencyRevealSurfaceAuthoringPayload::GetAllocatedBytes() const
{
    return static_cast<uint64>(PackedPixels.GetAllocatedSize());
}

int32 FDWCTransparencySourcePayload::ResolveOuterIslandIDAtUV(
    const FVector2D& PositionUV,
    const int32 FallbackUVIslandID,
    const bool bWrap) const
{
    const int32 Width = Resolution.X;
    const int32 Height = Resolution.Y;
    if (Width <= 0 || Height <= 0 || OuterIslandIDBuffer.Num() != Width * Height)
    {
        return FallbackUVIslandID;
    }

    int32 X = FMath::FloorToInt(PositionUV.X * Width);
    int32 Y = FMath::FloorToInt(PositionUV.Y * Height);
    if (bWrap)
    {
        X = (X % Width + Width) % Width;
        Y = (Y % Height + Height) % Height;
    }
    else if (X < 0 || X >= Width || Y < 0 || Y >= Height)
    {
        return INDEX_NONE;
    }

    const int32 RasterIslandID = DecodeOuterIslandID(OuterIslandIDBuffer[Y * Width + X]);
    // Spatial hit IDs are local to the query cache and can differ from the
    // raster workspace. Only use the hit ID when the sampled texel is uncovered.
    return RasterIslandID != INDEX_NONE ? RasterIslandID : FallbackUVIslandID;
}
