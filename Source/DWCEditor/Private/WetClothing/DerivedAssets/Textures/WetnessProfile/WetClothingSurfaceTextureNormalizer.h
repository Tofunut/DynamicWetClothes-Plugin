#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;
struct FWetClothingLocalRenderProfile;
struct FWetnessProfileParameters;

namespace DWCSurfaceTextureNormalization
{
    constexpr int32 Resolution = 256;
    constexpr int32 Version = 4;
}

/**
 * Converts authored profile textures into deterministic, array-compatible
 * Derived assets. All runtime Texture2DArray slices are sourced from these
 * textures rather than from arbitrary authored textures.
 */
class FWetClothingSurfaceTextureNormalizer
{
public:
    static UTexture2D* GetOrCreateNeutralNormalTexture(
        UWetClothingAsset& WetClothingAsset,
        FString& OutErrorMessage);

    static bool NormalizeProfileTextures(
        UWetClothingAsset& WetClothingAsset,
        const FWetnessProfileParameters& SourceParameters,
        FWetClothingLocalRenderProfile& InOutLocalProfile,
        FString& OutErrorMessage);

private:
    static bool NormalizeTexture(
        UWetClothingAsset& WetClothingAsset,
        UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap,
        UTexture2D*& OutNormalizedTexture,
        FString& OutErrorMessage);
};
