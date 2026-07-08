/*
 *  Texture2D의 Source 데이터를 읽어 UV 좌표 기반 색상 샘플링용 픽셀 버퍼로 변환합니다.
 */

#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"

#include "Engine/Texture2D.h"

bool FWetClothingTextureReadback::IsValid() const
{
    return Width > 0 && Height > 0 && BytesPerPixel > 0 && RawData.Num() >= static_cast<int64>(Width) * Height * BytesPerPixel;
}

FLinearColor FWetClothingTextureReadback::GetLinearColor(int32 X, int32 Y) const
{
    if (!IsValid())
    {
        return FLinearColor::Black;
    }

    const int32  ClampedX = FMath::Clamp(X, 0, Width - 1);
    const int32  ClampedY = FMath::Clamp(Y, 0, Height - 1);
    const int64  PixelOffset = (static_cast<int64>(ClampedY) * Width + ClampedX) * BytesPerPixel;
    const uint8* PixelPtr = RawData.GetData() + PixelOffset;
    FColor       SRGBColor = FColor::Black;

    switch (Format)
    {
    case TSF_BGRA8:
        SRGBColor = *reinterpret_cast<const FColor*>(PixelPtr);
        break;

    case TSF_G8:
    {
        const uint8 Intensity = *PixelPtr;
        SRGBColor = FColor(Intensity, Intensity, Intensity, 255);
        break;
    }

    default:
        return FLinearColor::Black;
    }

    return bSRGB ? FLinearColor::FromSRGBColor(SRGBColor) : FLinearColor(SRGBColor);
}

bool FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
    UTexture2D*                  Texture,
    FWetClothingTextureReadback& OutTextureData,
    FString&                     OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    OutTextureData = FWetClothingTextureReadback();

    if (Texture == nullptr)
    {
        OutErrorMessage = TEXT("Turn on a texture image for the selected material slot before running Auto-Partitioning.");
        return false;
    }

    if (!Texture->Source.IsValid())
    {
        OutErrorMessage = FString::Printf(TEXT("Texture '%s' does not have readable source data."), *Texture->GetName());
        return false;
    }

    const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat();
    if (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_G8)
    {
        OutErrorMessage = FString::Printf(TEXT("Texture '%s' uses an unsupported source format for Auto-Partitioning."), *Texture->GetName());
        return false;
    }

    if (!Texture->Source.GetMipData(OutTextureData.RawData, 0))
    {
        OutErrorMessage = FString::Printf(TEXT("Failed to read source pixels from texture '%s'."), *Texture->GetName());
        return false;
    }

    OutTextureData.Width = Texture->Source.GetSizeX();
    OutTextureData.Height = Texture->Source.GetSizeY();
    OutTextureData.BytesPerPixel = Texture->Source.GetBytesPerPixel();
    OutTextureData.bSRGB = Texture->SRGB;
    OutTextureData.Format = SourceFormat;
    OutTextureData.AddressX = Texture->AddressX;
    OutTextureData.AddressY = Texture->AddressY;

    if (!OutTextureData.IsValid())
    {
        OutErrorMessage = FString::Printf(TEXT("Texture '%s' returned invalid source pixel data."), *Texture->GetName());
        return false;
    }

    OutErrorMessage.Reset();
    return true;
#else
    OutErrorMessage = TEXT("Auto-Partitioning requires editor-only texture source data.");
    return false;
#endif
}
