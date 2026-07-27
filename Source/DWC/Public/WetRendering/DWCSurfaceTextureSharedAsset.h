#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Deterministic project-wide asset contract for normalized Surface Water textures.
 *
 * The generated assets are shared by every WCA. A source texture therefore maps
 * to one normalized texture asset and one world-local Texture2DArray slice,
 * instead of producing a duplicate generated asset for each WCA.
 */
namespace DWCSurfaceTextureSharedAsset
{
    constexpr int32 Resolution = 256;
    constexpr int32 Version = 7;

    DWC_API const TCHAR* GetSharedFolder();

    DWC_API FString MakeNormalizedTextureObjectName(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        bool bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectName(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap);

    DWC_API FString MakeNormalizedTexturePackageName(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        bool bNormalMap);

    DWC_API FString MakeNormalizedTexturePackageName(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectPath(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        bool bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectPath(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        bool bNormalMap);
}
