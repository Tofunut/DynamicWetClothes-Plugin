// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Surface Water Texture2DArray contract.
 *
 * Authored 512 textures are uploaded directly. Other authored textures are duplicated
 * and resized to 512 by Unreal's texture build pipeline in the shared folder.
 */
namespace DWCSurfaceTextureSharedAsset
{
    constexpr int32 Resolution = 512;
    constexpr int32 Version = 12;

    DWC_API const TCHAR* GetSharedFolder();

    DWC_API FString MakeNormalizedTextureObjectName(
        const FString& SourceTexturePath,
        const TCHAR*   TextureRole,
        bool           bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectName(
        const UTexture2D* SourceTexture,
        const TCHAR*      TextureRole,
        bool              bNormalMap);

    DWC_API FString MakeNormalizedTexturePackageName(
        const FString& SourceTexturePath,
        const TCHAR*   TextureRole,
        bool           bNormalMap);

    DWC_API FString MakeNormalizedTexturePackageName(
        const UTexture2D* SourceTexture,
        const TCHAR*      TextureRole,
        bool              bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectPath(
        const FString& SourceTexturePath,
        const TCHAR*   TextureRole,
        bool           bNormalMap);

    DWC_API FString MakeNormalizedTextureObjectPath(
        const UTexture2D* SourceTexture,
        const TCHAR*      TextureRole,
        bool              bNormalMap);
} // namespace DWCSurfaceTextureSharedAsset
