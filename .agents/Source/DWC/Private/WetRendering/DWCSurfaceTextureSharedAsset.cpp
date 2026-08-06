#include "WetRendering/DWCSurfaceTextureSharedAsset.h"

#include "Engine/Texture2D.h"
#include "Misc/SecureHash.h"

namespace DWCSurfaceTextureSharedAsset
{
    namespace
    {
        constexpr const TCHAR* SharedFolder = TEXT("/Game/DWCGenerated/Shared/SurfaceTextures");

        FString MakeStableSourceKey(
            const FString& SourceTexturePath,
            const TCHAR* TextureRole,
            const bool bNormalMap)
        {
            return FString::Printf(
                TEXT("DWC.SurfaceTexture.v%d|Role=%s|Normal=%d|Texture=%s"),
                Version,
                TextureRole != nullptr ? TextureRole : TEXT("Unknown"),
                bNormalMap ? 1 : 0,
                *SourceTexturePath);
        }
    }

    const TCHAR* GetSharedFolder()
    {
        return SharedFolder;
    }

    FString MakeNormalizedTextureObjectName(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        if (SourceTexturePath.IsEmpty() || TextureRole == nullptr || TextureRole[0] == 0)
        {
            return FString();
        }

        const FString StableHash = FMD5::HashAnsiString(
            *MakeStableSourceKey(SourceTexturePath, TextureRole, bNormalMap));
        return FString::Printf(TEXT("T_DWC_%s_%s"), TextureRole, *StableHash.Left(16));
    }

    FString MakeNormalizedTextureObjectName(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        return SourceTexture != nullptr
            ? MakeNormalizedTextureObjectName(SourceTexture->GetPathName(), TextureRole, bNormalMap)
            : FString();
    }

    FString MakeNormalizedTexturePackageName(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        const FString ObjectName = MakeNormalizedTextureObjectName(
            SourceTexturePath,
            TextureRole,
            bNormalMap);
        return ObjectName.IsEmpty()
            ? FString()
            : FString(SharedFolder) / ObjectName;
    }

    FString MakeNormalizedTexturePackageName(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        return SourceTexture != nullptr
            ? MakeNormalizedTexturePackageName(SourceTexture->GetPathName(), TextureRole, bNormalMap)
            : FString();
    }

    FString MakeNormalizedTextureObjectPath(
        const FString& SourceTexturePath,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        const FString PackageName = MakeNormalizedTexturePackageName(
            SourceTexturePath,
            TextureRole,
            bNormalMap);
        if (PackageName.IsEmpty())
        {
            return FString();
        }

        const FString ObjectName = MakeNormalizedTextureObjectName(
            SourceTexturePath,
            TextureRole,
            bNormalMap);
        return FString::Printf(TEXT("%s.%s"), *PackageName, *ObjectName);
    }

    FString MakeNormalizedTextureObjectPath(
        const UTexture2D* SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        return SourceTexture != nullptr
            ? MakeNormalizedTextureObjectPath(SourceTexture->GetPathName(), TextureRole, bNormalMap)
            : FString();
    }
}
