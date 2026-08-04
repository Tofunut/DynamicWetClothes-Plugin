#include "WetWrinkleNormalCorrectionService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetWrinkleNormalTextureData.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

FString FWetWrinkleNormalCorrectionService::MakeCorrectedTextureName(const UTexture2D& SourceTexture)
{
    FString BaseName = SourceTexture.GetName();
    BaseName.RemoveFromStart(TEXT("T_"));
    return ObjectTools::SanitizeObjectName(FString::Printf(TEXT("T_%s_CorrectedNormal"), *BaseName));
}

FString FWetWrinkleNormalCorrectionService::MakeCorrectedTextureObjectPath(const UTexture2D& SourceTexture)
{
    const FString Folder = FPackageName::GetLongPackagePath(SourceTexture.GetOutermost()->GetName());
    const FString TextureName = MakeCorrectedTextureName(SourceTexture);
    const FString PackageName = Folder / TextureName;
    return PackageName + TEXT(".") + TextureName;
}

UTexture2D* FWetWrinkleNormalCorrectionService::FindExistingCorrectedTexture(const UTexture2D& SourceTexture)
{
    return LoadObject<UTexture2D>(nullptr, *MakeCorrectedTextureObjectPath(SourceTexture));
}

bool FWetWrinkleNormalCorrectionService::CreateOrUpdateCorrectedTexture(
    UTexture2D& SourceTexture,
    const FWetWrinkleTexturePixelBuffer& CorrectedPixels,
    UTexture2D*& OutTexture,
    FString& OutError)
{
    OutTexture = nullptr;
#if WITH_EDITORONLY_DATA
    if (!CorrectedPixels.IsValid())
    {
        OutError = TEXT("Corrected normal output is empty.");
        return false;
    }

    const FString Folder = FPackageName::GetLongPackagePath(SourceTexture.GetOutermost()->GetName());
    if (Folder.IsEmpty())
    {
        OutError = TEXT("Could not resolve the source texture package folder.");
        return false;
    }

    const FString TextureName = MakeCorrectedTextureName(SourceTexture);
    const FString PackageName = Folder / TextureName;
    const FString ObjectPath = PackageName + TEXT(".") + TextureName;
    if (ObjectPath == SourceTexture.GetPathName())
    {
        OutError = TEXT("The corrected texture path would overwrite the source texture.");
        return false;
    }

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
    if (Texture == nullptr)
    {
        UPackage* Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to create package '%s'."), *PackageName);
            return false;
        }

        Texture = NewObject<UTexture2D>(Package, *TextureName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Texture);
    }
    else
    {
        Texture->Modify();
    }

    Texture->Source.Init(
        CorrectedPixels.Size.X,
        CorrectedPixels.Size.Y,
        1,
        1,
        TSF_BGRA8,
        reinterpret_cast<const uint8*>(CorrectedPixels.Pixels.GetData()));
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_Normalmap;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->bFlipGreenChannel = false;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();

    OutTexture = Texture;
    OutError.Reset();
    return true;
#else
    OutError = TEXT("Wrinkle normal correction requires editor-only texture source data.");
    return false;
#endif
}
