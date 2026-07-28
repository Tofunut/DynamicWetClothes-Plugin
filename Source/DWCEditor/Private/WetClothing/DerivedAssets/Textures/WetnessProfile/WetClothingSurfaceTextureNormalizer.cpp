#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureDefines.h"
#include "TextureCompiler.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "WetRendering/DWCSurfaceTextureSharedAsset.h"

namespace
{
    FString MakePreparedSourceKey(
        const UTexture2D& SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
#if WITH_EDITORONLY_DATA
        return FString::Printf(
            TEXT("%s|Role=%s|Normal=%d|SourceId=%s|Source=%dx%d:%d|SRGB=%d|Compression=%d|NoAlpha=%d|FlipG=%d"),
            *SourceTexture.GetPathName(),
            TextureRole != nullptr ? TextureRole : TEXT("Unknown"),
            bNormalMap ? 1 : 0,
            *SourceTexture.Source.GetId().ToString(),
            SourceTexture.Source.GetSizeX(),
            SourceTexture.Source.GetSizeY(),
            static_cast<int32>(SourceTexture.Source.GetFormat()),
            SourceTexture.SRGB ? 1 : 0,
            static_cast<int32>(SourceTexture.CompressionSettings),
            SourceTexture.CompressionNoAlpha ? 1 : 0,
            SourceTexture.bFlipGreenChannel ? 1 : 0);
#else
        return FString::Printf(
            TEXT("%s|Role=%s|Normal=%d"),
            *SourceTexture.GetPathName(),
            TextureRole != nullptr ? TextureRole : TEXT("Unknown"),
            bNormalMap ? 1 : 0);
#endif
    }

    FString MakePreparedTextureObjectPath(
        const UTexture2D& SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap)
    {
        return DWCSurfaceTextureSharedAsset::MakeNormalizedTextureObjectPath(
            MakePreparedSourceKey(SourceTexture, TextureRole, bNormalMap),
            TextureRole,
            bNormalMap);
    }

    bool HasUsableBuiltMip0(const UTexture2D& Texture)
    {
        const FTexturePlatformData* PlatformData = Texture.GetPlatformData();
        return PlatformData != nullptr &&
               !PlatformData->Mips.IsEmpty() &&
               Texture.GetPixelFormat() != PF_Unknown;
    }

    bool IsDirectlyUsableAtTargetResolution(const UTexture2D& Texture)
    {
        return Texture.GetSizeX() == DWCSurfaceTextureNormalization::Resolution &&
               Texture.GetSizeY() == DWCSurfaceTextureNormalization::Resolution &&
               HasUsableBuiltMip0(Texture);
    }

    bool ValidatePreparedTexture(
        UTexture2D* Texture,
        const TCHAR* TextureRole,
        const bool bNormalMap,
        FString& OutErrorMessage)
    {
        if (Texture == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated %s texture is null."),
                TextureRole != nullptr ? TextureRole : TEXT("Surface Water"));
            return false;
        }

        if (Texture->GetSizeX() != DWCSurfaceTextureNormalization::Resolution ||
            Texture->GetSizeY() != DWCSurfaceTextureNormalization::Resolution)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated %s texture '%s' built as %dx%d instead of %dx%d."),
                TextureRole,
                *Texture->GetPathName(),
                Texture->GetSizeX(),
                Texture->GetSizeY(),
                DWCSurfaceTextureNormalization::Resolution,
                DWCSurfaceTextureNormalization::Resolution);
            return false;
        }

        const TextureCompressionSettings ExpectedCompression = bNormalMap
            ? TC_Normalmap
            : TC_Masks;
        if (Texture->CompressionSettings != ExpectedCompression)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated %s texture '%s' has compression setting %d instead of %d."),
                TextureRole,
                *Texture->GetPathName(),
                static_cast<int32>(Texture->CompressionSettings),
                static_cast<int32>(ExpectedCompression));
            return false;
        }

        if (Texture->SRGB)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated %s texture '%s' unexpectedly has sRGB enabled."),
                TextureRole,
                *Texture->GetPathName());
            return false;
        }

        if (!HasUsableBuiltMip0(*Texture))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated %s texture '%s' has no usable built mip-0 platform data."),
                TextureRole,
                *Texture->GetPathName());
            return false;
        }

        OutErrorMessage.Reset();
        return true;
    }

    UTexture2D* LoadOrCreateDefaultSurfaceTexture(
        const bool bNormalMap,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        const TCHAR* AssetName = bNormalMap
            ? TEXT("T_DWC_DefaultSurfaceNormal")
            : TEXT("T_DWC_DefaultSurfaceMask");
        const FString PackageName = FString(DWCSurfaceTextureSharedAsset::GetSharedFolder()) / AssetName;
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, AssetName);

        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
        const bool bCreated = Texture == nullptr;
        if (bCreated)
        {
            UPackage* Package = CreatePackage(*PackageName);
            Texture = Package != nullptr
                ? NewObject<UTexture2D>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional)
                : nullptr;
            if (Texture == nullptr)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Failed to create default surface texture '%s'."),
                    *ObjectPath);
                return nullptr;
            }
        }
        else
        {
            Texture->Modify();
        }

        const bool bNeedsSourceRebuild =
            Texture->Source.GetSizeX() != DWCSurfaceTextureNormalization::Resolution ||
            Texture->Source.GetSizeY() != DWCSurfaceTextureNormalization::Resolution ||
            Texture->Source.GetFormat() != TSF_BGRA8;
        if (bNeedsSourceRebuild)
        {
            TArray<FColor> Pixels;
            Pixels.Init(
                bNormalMap ? FColor(128, 128, 255, 255) : FColor(0, 0, 0, 255),
                DWCSurfaceTextureNormalization::Resolution * DWCSurfaceTextureNormalization::Resolution);
            Texture->Source.Init(
                DWCSurfaceTextureNormalization::Resolution,
                DWCSurfaceTextureNormalization::Resolution,
                1,
                1,
                TSF_BGRA8,
                reinterpret_cast<const uint8*>(Pixels.GetData()));
        }

        Texture->SRGB = false;
        Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Masks;
        Texture->CompressionNoAlpha = true;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->NeverStream = true;
        Texture->VirtualTextureStreaming = false;
        Texture->bFlipGreenChannel = false;
        Texture->PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
        Texture->ResizeDuringBuildX = 0;
        Texture->ResizeDuringBuildY = 0;
        Texture->MaxTextureSize = DWCSurfaceTextureNormalization::Resolution;
        Texture->PostEditChange();
        Texture->UpdateResource();

        TArray<UTexture*> TexturesToFinish;
        TexturesToFinish.Add(Texture);
        FTextureCompilingManager::Get().FinishCompilation(TexturesToFinish);

        Texture->MarkPackageDirty();
        if (UPackage* OuterPackage = Texture->GetOutermost())
        {
            OuterPackage->MarkPackageDirty();
        }
        if (bCreated)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }

        if (!ValidatePreparedTexture(
                Texture,
                bNormalMap ? TEXT("neutral normal") : TEXT("neutral mask"),
                bNormalMap,
                OutErrorMessage))
        {
            return nullptr;
        }

        OutErrorMessage.Reset();
        return Texture;
#else
        OutErrorMessage = TEXT("Default surface texture creation requires editor-only texture source data.");
        return nullptr;
#endif
    }

    UTexture2D* CreateOrLoadAutoResizedTexture(
        UTexture2D& SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        const FString PreparedSourceKey = MakePreparedSourceKey(
            SourceTexture,
            TextureRole,
            bNormalMap);
        const FString ObjectName = DWCSurfaceTextureSharedAsset::MakeNormalizedTextureObjectName(
            PreparedSourceKey,
            TextureRole,
            bNormalMap);
        const FString PackageName = DWCSurfaceTextureSharedAsset::MakeNormalizedTexturePackageName(
            PreparedSourceKey,
            TextureRole,
            bNormalMap);
        const FString ObjectPath = DWCSurfaceTextureSharedAsset::MakeNormalizedTextureObjectPath(
            PreparedSourceKey,
            TextureRole,
            bNormalMap);
        if (ObjectName.IsEmpty() || PackageName.IsEmpty() || ObjectPath.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not resolve a generated 512 texture path for '%s'."),
                *SourceTexture.GetPathName());
            return nullptr;
        }

        if (UTexture2D* ExistingTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath))
        {
            TArray<UTexture*> TexturesToFinish;
            TexturesToFinish.Add(ExistingTexture);
            FTextureCompilingManager::Get().FinishCompilation(TexturesToFinish);
            if (ValidatePreparedTexture(
                    ExistingTexture,
                    TextureRole,
                    bNormalMap,
                    OutErrorMessage))
            {
                return ExistingTexture;
            }

            // The object name includes the source-data signature and DWC surface texture
            // version. A mismatching existing object is therefore an interrupted or stale
            // build; rebuild it in-place from the authored texture below.
            ExistingTexture->Modify();
        }

        UPackage* Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Failed to create generated surface texture package '%s'."),
                *PackageName);
            return nullptr;
        }

        UTexture2D* Texture = FindObject<UTexture2D>(Package, *ObjectName);
        const bool bCreated = Texture == nullptr;
        if (bCreated)
        {
            Texture = DuplicateObject<UTexture2D>(&SourceTexture, Package, FName(*ObjectName));
            if (Texture == nullptr)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Failed to duplicate '%s' as generated 512 texture '%s'."),
                    *SourceTexture.GetPathName(),
                    *ObjectPath);
                return nullptr;
            }
            Texture->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
            Texture->ClearFlags(RF_Transient);
        }
        else
        {
            // The generated object name includes the authored source-data ID and all
            // build settings that affect the normal interpretation. If this object
            // exists, its Source already belongs to the same immutable source key;
            // only force its interrupted/stale platform build to run again.
            Texture->Modify();
        }

        // Resize in Unreal's texture build pipeline instead of resampling normal RG in
        // DWC code. This keeps the authored source and normal-map compression contract,
        // while producing an exact 512x512 built mip for Texture2DArray upload.
        Texture->PowerOfTwoMode = ETexturePowerOfTwoSetting::ResizeToSpecificResolution;
        Texture->ResizeDuringBuildX = DWCSurfaceTextureNormalization::Resolution;
        Texture->ResizeDuringBuildY = DWCSurfaceTextureNormalization::Resolution;
        Texture->MaxTextureSize = DWCSurfaceTextureNormalization::Resolution;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->NeverStream = true;
        Texture->VirtualTextureStreaming = false;

        // These are validated on the source and intentionally preserved on the duplicate.
        Texture->SRGB = false;
        Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Masks;

        Texture->PostEditChange();
        Texture->UpdateResource();

        TArray<UTexture*> TexturesToFinish;
        TexturesToFinish.Add(Texture);
        FTextureCompilingManager::Get().FinishCompilation(TexturesToFinish);

        if (!ValidatePreparedTexture(
                Texture,
                TextureRole,
                bNormalMap,
                OutErrorMessage))
        {
            return nullptr;
        }

        Texture->MarkPackageDirty();
        Package->MarkPackageDirty();
        if (bCreated)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }

        OutErrorMessage.Reset();
        return Texture;
#else
        OutErrorMessage = TEXT("Automatic Surface Water texture resizing requires editor-only texture data.");
        return nullptr;
#endif
    }
}

UTexture2D* FWetClothingSurfaceTextureNormalizer::GetOrCreateNeutralNormalTexture(
    UWetClothingAsset& /*WetClothingAsset*/,
    FString& OutErrorMessage)
{
    return LoadOrCreateDefaultSurfaceTexture(true, OutErrorMessage);
}

bool FWetClothingSurfaceTextureNormalizer::ValidateTexture(
    UTexture2D* SourceTexture,
    const TCHAR* TextureRole,
    const bool bNormalMap,
    FString& OutErrorMessage)
{
    if (SourceTexture == nullptr)
    {
        OutErrorMessage.Reset();
        return true;
    }

    if (SourceTexture->SRGB)
    {
        OutErrorMessage = FString::Printf(
            TEXT("%s texture '%s' must have sRGB disabled."),
            TextureRole,
            *SourceTexture->GetPathName());
        return false;
    }

    const TextureCompressionSettings ExpectedCompression = bNormalMap
        ? TC_Normalmap
        : TC_Masks;
    if (SourceTexture->CompressionSettings != ExpectedCompression)
    {
        OutErrorMessage = FString::Printf(
            TEXT("%s texture '%s' has compression setting %d, but Surface Water requires %s (%d)."),
            TextureRole,
            *SourceTexture->GetPathName(),
            static_cast<int32>(SourceTexture->CompressionSettings),
            bNormalMap ? TEXT("TC_Normalmap") : TEXT("TC_Masks"),
            static_cast<int32>(ExpectedCompression));
        return false;
    }

    if (IsDirectlyUsableAtTargetResolution(*SourceTexture))
    {
        OutErrorMessage.Reset();
        return true;
    }

#if WITH_EDITORONLY_DATA
    if (!SourceTexture->Source.IsValid() ||
        SourceTexture->Source.GetSizeX() <= 0 ||
        SourceTexture->Source.GetSizeY() <= 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("%s texture '%s' is %dx%d at runtime and has no readable source data from which DWC can generate a %dx%d copy."),
            TextureRole,
            *SourceTexture->GetPathName(),
            SourceTexture->GetSizeX(),
            SourceTexture->GetSizeY(),
            DWCSurfaceTextureNormalization::Resolution,
            DWCSurfaceTextureNormalization::Resolution);
        return false;
    }
#else
    OutErrorMessage = FString::Printf(
        TEXT("%s texture '%s' is not %dx%d. Automatic resizing is editor-only, so bake the generated Render Profile Data before cooking."),
        TextureRole,
        *SourceTexture->GetPathName(),
        DWCSurfaceTextureNormalization::Resolution,
        DWCSurfaceTextureNormalization::Resolution);
    return false;
#endif

    OutErrorMessage.Reset();
    return true;
}

bool FWetClothingSurfaceTextureNormalizer::ValidateProfileTextures(
    const FWetnessProfileParameters& SourceParameters,
    FString& OutErrorMessage)
{
    const FSurfaceWaterProfileParameters& Surface = SourceParameters.SurfaceWater;
    if (!ValidateTexture(
            Surface.DropletNormalTexture,
            TEXT("Droplet normal"),
            true,
            OutErrorMessage))
    {
        return false;
    }

    if (!ValidateTexture(
            Surface.DropletMaskTexture,
            TEXT("Droplet mask"),
            false,
            OutErrorMessage))
    {
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

bool FWetClothingSurfaceTextureNormalizer::NormalizeTexture(
    UTexture2D* SourceTexture,
    const TCHAR* TextureRole,
    const bool bNormalMap,
    UTexture2D*& OutNormalizedTexture,
    FString& OutErrorMessage)
{
    OutNormalizedTexture = nullptr;
    if (!ValidateTexture(SourceTexture, TextureRole, bNormalMap, OutErrorMessage))
    {
        return false;
    }

    if (SourceTexture == nullptr)
    {
        OutErrorMessage.Reset();
        return true;
    }

    if (IsDirectlyUsableAtTargetResolution(*SourceTexture))
    {
        OutNormalizedTexture = SourceTexture;
        OutErrorMessage.Reset();
        return true;
    }

    OutNormalizedTexture = CreateOrLoadAutoResizedTexture(
        *SourceTexture,
        TextureRole,
        bNormalMap,
        OutErrorMessage);
    return OutNormalizedTexture != nullptr;
}

bool FWetClothingSurfaceTextureNormalizer::PrepareProfileTextures(
    const FWetnessProfileParameters& SourceParameters,
    FWetClothingLocalRenderProfile& InOutLocalProfile,
    FString& OutErrorMessage)
{
    const FSurfaceWaterProfileParameters& Surface = SourceParameters.SurfaceWater;
    InOutLocalProfile.SourceDropletNormal = Surface.DropletNormalTexture != nullptr
        ? FSoftObjectPath(Surface.DropletNormalTexture.Get())
        : FSoftObjectPath();
    InOutLocalProfile.SourceDropletMask = Surface.DropletMaskTexture != nullptr
        ? FSoftObjectPath(Surface.DropletMaskTexture.Get())
        : FSoftObjectPath();

    UTexture2D* PreparedNormal = nullptr;
    UTexture2D* PreparedMask = nullptr;
    if (!NormalizeTexture(
            Surface.DropletNormalTexture,
            TEXT("DropletNormal"),
            true,
            PreparedNormal,
            OutErrorMessage) ||
        !NormalizeTexture(
            Surface.DropletMaskTexture,
            TEXT("DropletMask"),
            false,
            PreparedMask,
            OutErrorMessage))
    {
        return false;
    }

    InOutLocalProfile.NormalizedDropletNormal = PreparedNormal;
    InOutLocalProfile.NormalizedDropletMask = PreparedMask;
    OutErrorMessage.Reset();
    return true;
}

bool FWetClothingSurfaceTextureNormalizer::NormalizeProfileTextures(
    UWetClothingAsset& WetClothingAsset,
    const FWetnessProfileParameters& SourceParameters,
    FWetClothingLocalRenderProfile& InOutLocalProfile,
    FString& OutErrorMessage)
{
    (void)WetClothingAsset;
    return PrepareProfileTextures(SourceParameters, InOutLocalProfile, OutErrorMessage);
}

bool FWetClothingSurfaceTextureNormalizer::IsPreparedTextureReferenceCurrent(
    const UTexture2D* PreparedTexture,
    const UTexture2D* SourceTexture,
    const TCHAR* TextureRole,
    const bool bNormalMap)
{
    if (SourceTexture == nullptr)
    {
        return PreparedTexture == nullptr;
    }

    if (IsDirectlyUsableAtTargetResolution(*SourceTexture))
    {
        return PreparedTexture == SourceTexture;
    }

    if (PreparedTexture == nullptr ||
        PreparedTexture->GetPathName() != MakePreparedTextureObjectPath(
            *SourceTexture,
            TextureRole,
            bNormalMap))
    {
        return false;
    }

    FString ValidationError;
    return ValidatePreparedTexture(
        const_cast<UTexture2D*>(PreparedTexture),
        TextureRole,
        bNormalMap,
        ValidationError);
}
