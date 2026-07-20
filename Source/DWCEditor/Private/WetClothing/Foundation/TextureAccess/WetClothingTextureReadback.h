/*
 *  Texture readback 결과 구조체와 Texture2D Source 읽기 유틸리티를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"

class UTexture2D;

struct FWetClothingTextureReadback
{
    int32                Width = 0;
    int32                Height = 0;
    int32                BytesPerPixel = 0;
    bool                 bSRGB = true;
    ETextureSourceFormat Format = TSF_Invalid;
    TextureAddress       AddressX = TA_Clamp;
    TextureAddress       AddressY = TA_Clamp;
    /** Shared by session-cache hits to avoid copying full source mips on every preview request. */
    TSharedPtr<TArray64<uint8>> RawData;

    bool         IsValid() const;
    FLinearColor GetLinearColor(int32 X, int32 Y) const;
};

class FWetClothingTextureReadbackUtils
{
  public:
    static bool TryReadTextureSourceData(
        UTexture2D*                  Texture,
        FWetClothingTextureReadback& OutTextureData,
        FString&                     OutErrorMessage);
    static void ClearCache();
};
