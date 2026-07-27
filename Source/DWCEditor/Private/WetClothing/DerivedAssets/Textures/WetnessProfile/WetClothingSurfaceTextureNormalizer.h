#pragma once

#include "CoreMinimal.h"
#include "WetRendering/DWCSurfaceTextureSharedAsset.h"

class UTexture2D;
class UWetClothingAsset;
struct FWetClothingLocalRenderProfile;
struct FWetnessProfileParameters;

namespace DWCSurfaceTextureNormalization
{
    constexpr int32 Resolution = DWCSurfaceTextureSharedAsset::Resolution;
    constexpr int32 Version = DWCSurfaceTextureSharedAsset::Version;
}

/**
 * Converts authored profile textures into deterministic, array-compatible
 * project-wide shared assets. Every WCA referencing the same source texture
 * reuses the same normalized asset and Texture2DArray registry entry.
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
        UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap,
        UTexture2D*& OutNormalizedTexture,
        FString& OutErrorMessage);
};
