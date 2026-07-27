#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

namespace
{
    constexpr float MaskEdgeBlurRadiusTexels = 1.25f;
    constexpr float MaskEdgeBlendGain = 4.0f;

    int32 WrapTexelIndex(const int32 Index, const int32 Size)
    {
        return Size > 0 ? (Index % Size + Size) % Size : 0;
    }

    int32 MirrorTexelIndex(const int32 Index, const int32 Size)
    {
        if (Size <= 1)
        {
            return 0;
        }

        const int32 Period = Size * 2;
        const int32 Wrapped = WrapTexelIndex(Index, Period);
        return Wrapped < Size ? Wrapped : Period - Wrapped - 1;
    }

    int32 ResolveTexelIndex(
        const int32 Index,
        const int32 Size,
        const TextureAddress AddressMode)
    {
        switch (AddressMode)
        {
        case TA_Wrap:
            return WrapTexelIndex(Index, Size);

        case TA_Mirror:
            return MirrorTexelIndex(Index, Size);

        case TA_Clamp:
        default:
            return FMath::Clamp(Index, 0, Size - 1);
        }
    }

    FLinearColor SampleBilinear(
        const FWetClothingTextureReadback& Source,
        const float U,
        const float V)
    {
        const float SourceX = U * static_cast<float>(Source.Width) - 0.5f;
        const float SourceY = V * static_cast<float>(Source.Height) - 0.5f;
        const int32 X0 = FMath::FloorToInt(SourceX);
        const int32 Y0 = FMath::FloorToInt(SourceY);
        const int32 X1 = X0 + 1;
        const int32 Y1 = Y0 + 1;
        // Use floor-relative fractions. FMath::Frac(-0.5) is negative, which
        // would extrapolate at the first texel instead of clamping cleanly.
        const float TX = FMath::Clamp(SourceX - static_cast<float>(X0), 0.0f, 1.0f);
        const float TY = FMath::Clamp(SourceY - static_cast<float>(Y0), 0.0f, 1.0f);

        const int32 AddressedX0 = ResolveTexelIndex(X0, Source.Width, Source.AddressX);
        const int32 AddressedX1 = ResolveTexelIndex(X1, Source.Width, Source.AddressX);
        const int32 AddressedY0 = ResolveTexelIndex(Y0, Source.Height, Source.AddressY);
        const int32 AddressedY1 = ResolveTexelIndex(Y1, Source.Height, Source.AddressY);

        const FLinearColor C00 = Source.GetLinearColor(AddressedX0, AddressedY0);
        const FLinearColor C10 = Source.GetLinearColor(AddressedX1, AddressedY0);
        const FLinearColor C01 = Source.GetLinearColor(AddressedX0, AddressedY1);
        const FLinearColor C11 = Source.GetLinearColor(AddressedX1, AddressedY1);
        return FMath::Lerp(
            FMath::Lerp(C00, C10, TX),
            FMath::Lerp(C01, C11, TX),
            TY);
    }

    float SampleMask(
        const FWetClothingTextureReadback& Source,
        const float U,
        const float V)
    {
        return FMath::Clamp(SampleBilinear(Source, U, V).R, 0.0f, 1.0f);
    }

    FLinearColor SampleEdgeSoftenedMask(
        const FWetClothingTextureReadback& Source,
        const float U,
        const float V)
    {
        const float Center = SampleMask(Source, U, V);
        const float BlurStep = MaskEdgeBlurRadiusTexels /
            static_cast<float>(DWCSurfaceTextureNormalization::Resolution);

        float WeightedSum = 0.0f;
        float WeightSum = 0.0f;
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            const float WeightY = OffsetY == 0 ? 2.0f : 1.0f;
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                const float WeightX = OffsetX == 0 ? 2.0f : 1.0f;
                const float Weight = WeightX * WeightY;
                WeightedSum += SampleMask(
                    Source,
                    U + static_cast<float>(OffsetX) * BlurStep,
                    V + static_cast<float>(OffsetY) * BlurStep) * Weight;
                WeightSum += Weight;
            }
        }

        const float Blurred = WeightSum > 0.0f ? WeightedSum / WeightSum : Center;
        const float EdgeBlend = FMath::Clamp(
            FMath::Abs(Blurred - Center) * MaskEdgeBlendGain,
            0.0f,
            1.0f);
        const float Softened = FMath::Lerp(Center, Blurred, EdgeBlend);
        return FLinearColor(Softened, Softened, Softened, 1.0f);
    }

    FColor EncodeMask(const FLinearColor& Sample)
    {
        const float Mask = FMath::Clamp(Sample.R, 0.0f, 1.0f);
        return FLinearColor(Mask, Mask, Mask, 1.0f).ToFColor(false);
    }

    FColor EncodeNormal(const FLinearColor& Sample, const bool bFlipGreenChannel)
    {
        FVector3f Normal(
            Sample.R * 2.0f - 1.0f,
            Sample.G * 2.0f - 1.0f,
            Sample.B * 2.0f - 1.0f);
        if (bFlipGreenChannel)
        {
            Normal.Y *= -1.0f;
        }
        if (!Normal.Normalize())
        {
            Normal = FVector3f(0.0f, 0.0f, 1.0f);
        }
        return FLinearColor(
            Normal.X * 0.5f + 0.5f,
            Normal.Y * 0.5f + 0.5f,
            Normal.Z * 0.5f + 0.5f,
            1.0f).ToFColor(false);
    }

    UTexture2D* LoadOrCreateDefaultSurfaceTexture(
        const bool bNormalMap,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        const TCHAR* AssetName = bNormalMap
            ? TEXT("T_DWC_DefaultSurfaceNormal")
            : TEXT("T_DWC_DefaultSurfaceMask");
        const FString PackageName = FString(TEXT("/Game/DWCGenerated/SharedDefaults")) / AssetName;
        const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, AssetName);

        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
        if (Texture != nullptr)
        {
            OutErrorMessage.Reset();
            return Texture;
        }

        UPackage* Package = CreatePackage(*PackageName);
        Texture = Package != nullptr
            ? NewObject<UTexture2D>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional)
            : nullptr;
        if (Texture == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create default surface texture '%s'."), *ObjectPath);
            return nullptr;
        }

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
        Texture->SRGB = false;
        Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Masks;
        Texture->CompressionNoAlpha = true;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->NeverStream = true;
        Texture->bFlipGreenChannel = false;
        Texture->PostEditChange();
        Texture->UpdateResource();
        Texture->MarkPackageDirty();
        if (UPackage* OuterPackage = Texture->GetOutermost())
        {
            OuterPackage->MarkPackageDirty();
        }
        FAssetRegistryModule::AssetCreated(Texture);

        OutErrorMessage.Reset();
        return Texture;
#else
        OutErrorMessage = TEXT("Default surface texture creation requires editor-only texture source data.");
        return nullptr;
#endif
    }

    UTexture2D* CreateOrUpdateNormalizedAsset(
        UWetClothingAsset& WetClothingAsset,
        UTexture2D& SourceTexture,
        const TCHAR* TextureRole,
        const bool bNormalMap,
        const TArray<FColor>& Pixels,
        FString& OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        const FString WcaPackageName = WetClothingAsset.GetOutermost()->GetName();
        const FString WcaFolder = FPackageName::GetLongPackagePath(WcaPackageName);
        if (WcaFolder.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not resolve the WCA package path while normalizing a surface texture.");
            return nullptr;
        }

        const FString SourceBuildKey = FString::Printf(
            TEXT("DWC.SurfaceTexture.v%d|Role=%s|Normal=%d|Texture=%s"),
            DWCSurfaceTextureNormalization::Version,
            TextureRole,
            bNormalMap ? 1 : 0,
            *SourceTexture.GetPathName());
        const FString StableHash = FMD5::HashAnsiString(*SourceBuildKey);
        const FString ObjectName = ObjectTools::SanitizeObjectName(FString::Printf(
            TEXT("T_%s_%s_%s"),
            *WetClothingAsset.GetName(),
            *StableHash.Left(12),
            TextureRole));
        const FString GeneratedFolder =
            WcaFolder / TEXT("Generated") / WetClothingAsset.GetName() / TEXT("Textures") / TEXT("Profiles");
        const FString PackageName = GeneratedFolder / ObjectName;
        const FString ObjectPath = PackageName + TEXT(".") + ObjectName;

        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
        if (Texture == nullptr)
        {
            UPackage* Package = CreatePackage(*PackageName);
            if (Package == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to create normalized texture package '%s'."), *PackageName);
                return nullptr;
            }
            Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
            if (Texture == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Failed to create normalized texture '%s'."), *ObjectPath);
                return nullptr;
            }
            FAssetRegistryModule::AssetCreated(Texture);
        }
        else
        {
            Texture->Modify();
        }

        Texture->Source.Init(
            DWCSurfaceTextureNormalization::Resolution,
            DWCSurfaceTextureNormalization::Resolution,
            1,
            1,
            TSF_BGRA8,
            reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->SRGB = false;
        Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Masks;
        Texture->CompressionNoAlpha = true;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->NeverStream = true;
        Texture->bFlipGreenChannel = false;
        Texture->PostEditChange();
        Texture->UpdateResource();
        Texture->MarkPackageDirty();
        if (UPackage* Package = Texture->GetOutermost())
        {
            Package->MarkPackageDirty();
        }
        OutErrorMessage.Reset();
        return Texture;
#else
        OutErrorMessage = TEXT("Surface texture normalization requires editor-only texture source data.");
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

bool FWetClothingSurfaceTextureNormalizer::NormalizeTexture(
    UWetClothingAsset& WetClothingAsset,
    UTexture2D* SourceTexture,
    const TCHAR* TextureRole,
    const bool bNormalMap,
    UTexture2D*& OutNormalizedTexture,
    FString& OutErrorMessage)
{
    OutNormalizedTexture = nullptr;
    if (SourceTexture == nullptr)
    {
        // Null is the authored OFF state. Runtime maps it to Texture2DArray
        // slice 0, which contains the shared flat normal.
        OutErrorMessage.Reset();
        return true;
    }

    FWetClothingTextureReadback SourceData;
    FString ReadError;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(SourceTexture, SourceData, ReadError))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not normalize %s texture '%s': %s"),
            TextureRole,
            *SourceTexture->GetPathName(),
            *ReadError);
        return false;
    }

    // Tangent-space normal components are encoded data, never gamma-space color.
    // Decode their raw bytes even when an imported source accidentally has sRGB enabled.
    if (bNormalMap)
    {
        SourceData.bSRGB = false;
    }

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(
        DWCSurfaceTextureNormalization::Resolution * DWCSurfaceTextureNormalization::Resolution);
    for (int32 Y = 0; Y < DWCSurfaceTextureNormalization::Resolution; ++Y)
    {
        const float V = (static_cast<float>(Y) + 0.5f) /
            static_cast<float>(DWCSurfaceTextureNormalization::Resolution);
        for (int32 X = 0; X < DWCSurfaceTextureNormalization::Resolution; ++X)
        {
            const float U = (static_cast<float>(X) + 0.5f) /
                static_cast<float>(DWCSurfaceTextureNormalization::Resolution);
            const FLinearColor Sample = SampleBilinear(SourceData, U, V);
            Pixels[Y * DWCSurfaceTextureNormalization::Resolution + X] = bNormalMap
                ? EncodeNormal(Sample, SourceTexture->bFlipGreenChannel)
                : EncodeMask(SampleEdgeSoftenedMask(SourceData, U, V));
        }
    }

    OutNormalizedTexture = CreateOrUpdateNormalizedAsset(
        WetClothingAsset,
        *SourceTexture,
        TextureRole,
        bNormalMap,
        Pixels,
        OutErrorMessage);
    return OutNormalizedTexture != nullptr;
}

bool FWetClothingSurfaceTextureNormalizer::NormalizeProfileTextures(
    UWetClothingAsset& WetClothingAsset,
    const FWetnessProfileParameters& SourceParameters,
    FWetClothingLocalRenderProfile& InOutLocalProfile,
    FString& OutErrorMessage)
{
    const FSurfaceWaterProfileParameters& Surface = SourceParameters.SurfaceWater;
    UTexture2D* DropletNormal = nullptr;
    UTexture2D* DropletMask = nullptr;
    UTexture2D* RivuletNormal = nullptr;
    UTexture2D* RivuletMask = nullptr;

    if (!NormalizeTexture(
            WetClothingAsset,
            Surface.DropletNormalTexture,
            TEXT("DropletNormal"),
            true,
            DropletNormal,
            OutErrorMessage) ||
        !NormalizeTexture(
            WetClothingAsset,
            Surface.DropletMaskTexture,
            TEXT("DropletMask"),
            false,
            DropletMask,
            OutErrorMessage) ||
        !NormalizeTexture(
            WetClothingAsset,
            Surface.RivuletNormalTexture,
            TEXT("RivuletNormal"),
            true,
            RivuletNormal,
            OutErrorMessage) ||
        !NormalizeTexture(
            WetClothingAsset,
            Surface.RivuletMaskTexture,
            TEXT("RivuletMask"),
            false,
            RivuletMask,
            OutErrorMessage))
    {
        return false;
    }

    InOutLocalProfile.NormalizedDropletNormal = DropletNormal;
    InOutLocalProfile.NormalizedDropletMask = DropletMask;
    InOutLocalProfile.NormalizedRivuletNormal = RivuletNormal;
    InOutLocalProfile.NormalizedRivuletMask = RivuletMask;
    OutErrorMessage.Reset();
    return true;
}
