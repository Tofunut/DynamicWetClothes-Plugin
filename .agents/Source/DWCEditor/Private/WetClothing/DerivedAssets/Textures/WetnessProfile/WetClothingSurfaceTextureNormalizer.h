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
 * Prepares fixed-resolution Surface Water textures for Texture2DArray upload.
 *
 * Authored 512x512 textures are referenced directly. Other valid authored
 * textures are duplicated into the shared generated folder and resized to
 * 512x512 by Unreal's texture build pipeline. DWC never CPU-decodes,
 * normalizes, reconstructs, or re-encodes the authored normal RG values.
 */
class FWetClothingSurfaceTextureNormalizer
{
public:
    static UTexture2D* GetOrCreateNeutralNormalTexture(
        UWetClothingAsset& WetClothingAsset,
        FString& OutErrorMessage);

    static bool ValidateProfileTextures(
        const FWetnessProfileParameters& SourceParameters,
        FString& OutErrorMessage);

    static bool PrepareProfileTextures(
        const FWetnessProfileParameters& SourceParameters,
        FWetClothingLocalRenderProfile& InOutLocalProfile,
        FString& OutErrorMessage);

    static bool NormalizeProfileTextures(
        UWetClothingAsset& WetClothingAsset,
        const FWetnessProfileParameters& SourceParameters,
        FWetClothingLocalRenderProfile& InOutLocalProfile,
        FString& OutErrorMessage);

    static bool IsPreparedTextureReferenceCurrent(
        const UTexture2D* PreparedTexture,
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap);

private:
    static bool ValidateTexture(
        UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap,
        bool bAllowSourceConversion,
        FString& OutErrorMessage);

    static bool NormalizeTexture(
        UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap,
        bool bAllowSourceConversion,
        UTexture2D*& OutNormalizedTexture,
        FString& OutErrorMessage);
};
