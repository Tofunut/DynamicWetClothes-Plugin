//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
struct FWetWrinkleTexturePixelBuffer;

class FWetWrinkleNormalCorrectionService
{
  public:
    static FString MakeCorrectedTextureName(const UTexture2D& SourceTexture);
    static FString MakeCorrectedTextureObjectPath(const UTexture2D& SourceTexture);
    static UTexture2D* FindExistingCorrectedTexture(const UTexture2D& SourceTexture);

    static bool CreateOrUpdateCorrectedTexture(
        UTexture2D& SourceTexture,
        const FWetWrinkleTexturePixelBuffer& CorrectedPixels,
        UTexture2D*& OutTexture,
        FString& OutError);
};
