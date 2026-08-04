#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

namespace
{
    float BilinearChannel(
        const uint8 V00,
        const uint8 V10,
        const uint8 V01,
        const uint8 V11,
        const float FracX,
        const float FracY)
    {
        return FMath::Lerp(
            FMath::Lerp(static_cast<float>(V00), static_cast<float>(V10), FracX),
            FMath::Lerp(static_cast<float>(V01), static_cast<float>(V11), FracX),
            FracY) / 255.0f;
    }
}

bool FDWCEditorNormalSourceSnapshot::IsValid() const
{
    return Texture.IsValid() && Texture.Format == TSF_BGRA8;
}

FVector3f FDWCEditorNormalSourceSnapshot::SampleBilinear(const FVector2f& UV) const
{
    if (!IsValid())
    {
        return FVector3f(0.0f, 0.0f, 1.0f);
    }

    const float SampleX = FMath::Clamp(UV.X, 0.0f, 1.0f) * static_cast<float>(Texture.Width - 1);
    const float SampleY = FMath::Clamp(UV.Y, 0.0f, 1.0f) * static_cast<float>(Texture.Height - 1);
    const int32 X0 = FMath::FloorToInt(SampleX);
    const int32 Y0 = FMath::FloorToInt(SampleY);
    const int32 X1 = FMath::Min(X0 + 1, Texture.Width - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Texture.Height - 1);
    const float FracX = SampleX - static_cast<float>(X0);
    const float FracY = SampleY - static_cast<float>(Y0);
    const FColor* Pixels = reinterpret_cast<const FColor*>(Texture.RawData->GetData());
    const FColor& C00 = Pixels[Y0 * Texture.Width + X0];
    const FColor& C10 = Pixels[Y0 * Texture.Width + X1];
    const FColor& C01 = Pixels[Y1 * Texture.Width + X0];
    const FColor& C11 = Pixels[Y1 * Texture.Width + X1];

    const float X = BilinearChannel(C00.R, C10.R, C01.R, C11.R, FracX, FracY) * 2.0f - 1.0f;
    float Y = -(BilinearChannel(C00.G, C10.G, C01.G, C11.G, FracX, FracY) * 2.0f - 1.0f);
    if (bFlipGreenChannel)
    {
        Y = -Y;
    }
    float Z = BilinearChannel(C00.B, C10.B, C01.B, C11.B, FracX, FracY) * 2.0f - 1.0f;
    if (Z <= UE_SMALL_NUMBER)
    {
        const float XYLengthSquared = FMath::Min(X * X + Y * Y, 1.0f);
        Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSquared, 0.0f));
    }
    return FVector3f(X, Y, Z).GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
}

bool FDWCEditorScalarSourceSnapshot::IsValid() const
{
    return Size.X > 0 && Size.Y > 0 && Values.IsValid() && Values->Num() == Size.X * Size.Y;
}

float FDWCEditorScalarSourceSnapshot::SampleBilinear(const FVector2f& UV) const
{
    if (!IsValid())
    {
        return 1.0f;
    }

    const float SampleX = FMath::Clamp(UV.X, 0.0f, 1.0f) * static_cast<float>(Size.X - 1);
    const float SampleY = FMath::Clamp(UV.Y, 0.0f, 1.0f) * static_cast<float>(Size.Y - 1);
    const int32 X0 = FMath::FloorToInt(SampleX);
    const int32 Y0 = FMath::FloorToInt(SampleY);
    const int32 X1 = FMath::Min(X0 + 1, Size.X - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, Size.Y - 1);
    const float FracX = SampleX - static_cast<float>(X0);
    const float FracY = SampleY - static_cast<float>(Y0);
    return FMath::Lerp(
        FMath::Lerp((*Values)[Y0 * Size.X + X0], (*Values)[Y0 * Size.X + X1], FracX),
        FMath::Lerp((*Values)[Y1 * Size.X + X0], (*Values)[Y1 * Size.X + X1], FracX),
        FracY);
}

bool FDWCEditorNormalRasterSurface::Initialize(const FIntPoint& InSize, const bool bWithCoverage)
{
    if (InSize.X <= 0 || InSize.Y <= 0)
    {
        Size = FIntPoint::ZeroValue;
        PackedNormalXY.Reset();
        Coverage.Reset();
        return false;
    }

    Size = InSize;
    PackedNormalXY.Init(0u, Size.X * Size.Y);
    if (bWithCoverage)
    {
        Coverage.Init(0.0f, PackedNormalXY.Num());
    }
    else
    {
        Coverage.Reset();
    }
    return true;
}

bool FDWCEditorNormalRasterSurface::IsValid() const
{
    return Size.X > 0 && Size.Y > 0 && PackedNormalXY.Num() == Size.X * Size.Y &&
        (Coverage.IsEmpty() || Coverage.Num() == PackedNormalXY.Num());
}

bool FDWCEditorNormalRasterSurface::HasCoverage() const
{
    return IsValid() && Coverage.Num() == PackedNormalXY.Num();
}

uint64 FDWCEditorNormalRasterSurface::GetAllocatedSizeBytes() const
{
    return static_cast<uint64>(PackedNormalXY.GetAllocatedSize()) + Coverage.GetAllocatedSize();
}

int32 FDWCEditorNormalRasterSurface::GetPixelCount() const
{
    return PackedNormalXY.Num();
}

FVector3f FDWCEditorNormalRasterSurface::GetNormal(const int32 Index) const
{
    return PackedNormalXY.IsValidIndex(Index)
        ? UnpackNormalXY(PackedNormalXY[Index])
        : FVector3f(0.0f, 0.0f, 1.0f);
}

void FDWCEditorNormalRasterSurface::SetNormal(const int32 Index, const FVector3f& Normal)
{
    if (PackedNormalXY.IsValidIndex(Index))
    {
        PackedNormalXY[Index] = PackNormalXY(Normal);
    }
}

uint32 FDWCEditorNormalRasterSurface::PackNormalXY(const FVector3f& Normal)
{
    const FVector3f SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
    const int16 X = static_cast<int16>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X, -1.0f, 1.0f) * 32767.0f));
    const int16 Y = static_cast<int16>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y, -1.0f, 1.0f) * 32767.0f));
    return static_cast<uint16>(X) | (static_cast<uint32>(static_cast<uint16>(Y)) << 16);
}

FVector3f FDWCEditorNormalRasterSurface::UnpackNormalXY(const uint32 PackedNormal)
{
    const float X = static_cast<float>(static_cast<int16>(PackedNormal & 0xffffu)) / 32767.0f;
    const float Y = static_cast<float>(static_cast<int16>((PackedNormal >> 16) & 0xffffu)) / 32767.0f;
    const float Z = FMath::Sqrt(FMath::Max(1.0f - X * X - Y * Y, 0.0f));
    return FVector3f(X, Y, Z).GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
}
