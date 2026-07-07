#include "WetClothing/RevealBake/DWCRevealBakeTextureWriter.h"

#include "Runtime/AssetRegistry/Public/AssetRegistry/AssetRegistryModule.h"
#include "Runtime/CoreUObject/Public/UObject/Package.h"
#include "Runtime/Engine/Classes/Engine/Texture2D.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"

float FDWCRevealBakeTextureWriter::ApplyTextureAddress(const float Coordinate, const TextureAddress AddressMode)
{
    switch (AddressMode)
    {
    case TA_Wrap:
        return FMath::Frac(Coordinate);

    case TA_Mirror:
    {
        const float Wrapped = FMath::Frac(Coordinate * 0.5f) * 2.0f;
        return Wrapped <= 1.0f ? Wrapped : 2.0f - Wrapped;
    }

    case TA_Clamp:
    default:
        return FMath::Clamp(Coordinate, 0.0f, 1.0f);
    }
}

FLinearColor FDWCRevealBakeTextureWriter::SampleTextureBilinear(
    const FWetClothingTextureReadback& TextureData,
    const FVector2D&                   UV)
{
    if (!TextureData.IsValid())
    {
        return FLinearColor::Black;
    }

    const float U = ApplyTextureAddress(static_cast<float>(UV.X), TextureData.AddressX);
    const float V = ApplyTextureAddress(static_cast<float>(UV.Y), TextureData.AddressY);
    const float X = U * static_cast<float>(TextureData.Width - 1);
    const float Y = V * static_cast<float>(TextureData.Height - 1);

    const int32 X0 = FMath::FloorToInt(X);
    const int32 Y0 = FMath::FloorToInt(Y);
    const int32 X1 = FMath::Min(X0 + 1, TextureData.Width - 1);
    const int32 Y1 = FMath::Min(Y0 + 1, TextureData.Height - 1);
    const float AlphaX = X - static_cast<float>(X0);
    const float AlphaY = Y - static_cast<float>(Y0);

    const FLinearColor C00 = TextureData.GetLinearColor(X0, Y0);
    const FLinearColor C10 = TextureData.GetLinearColor(X1, Y0);
    const FLinearColor C01 = TextureData.GetLinearColor(X0, Y1);
    const FLinearColor C11 = TextureData.GetLinearColor(X1, Y1);
    const FLinearColor C0 = FMath::Lerp(C00, C10, AlphaX);
    const FLinearColor C1 = FMath::Lerp(C01, C11, AlphaX);
    return FMath::Lerp(C0, C1, AlphaY);
}

void FDWCRevealBakeTextureWriter::BuildSourceTextureReadbacks(
    const FDWCRevealBakeTextureWriteSettings& Settings,
    TMap<FName, FWetClothingTextureReadback>& OutTextureDataByLayerId)
{
    OutTextureDataByLayerId.Reset();

    for (const TPair<FName, UTexture2D*>& Pair : Settings.SourceLayerTextures)
    {
        if (Pair.Key.IsNone() || Pair.Value == nullptr)
        {
            continue;
        }

        FWetClothingTextureReadback TextureData;
        FString                     ErrorMessage;
        if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(Pair.Value, TextureData, ErrorMessage))
        {
            OutTextureDataByLayerId.Add(Pair.Key, MoveTemp(TextureData));
        }
    }
}

bool FDWCRevealBakeTextureWriter::WriteTextures(
    const TArray<FDWCBakeRayHit>&              Hits,
    const FDWCRevealBakeTextureWriteSettings& Settings,
    FDWCRevealBakeTextureSet&                 OutTextures,
    FString*                                  OutErrorMessage)
{
    OutTextures = FDWCRevealBakeTextureSet();

    if (Settings.Resolution.X <= 0 || Settings.Resolution.Y <= 0)
    {
        SetError(OutErrorMessage, TEXT("Reveal bake texture resolution must be positive."));
        return false;
    }

    if (Settings.PackagePath.IsEmpty())
    {
        SetError(OutErrorMessage, TEXT("Reveal bake package path is empty."));
        return false;
    }

    if (Settings.AssetNamePrefix.IsEmpty())
    {
        SetError(OutErrorMessage, TEXT("Reveal bake asset name prefix is empty."));
        return false;
    }

    const int32 Width = Settings.Resolution.X;
    const int32 Height = Settings.Resolution.Y;
    const int32 PixelCount = Width * Height;

    TArray<FFloat16Color> LookupPixels;
    TArray<FColor> ColorPixels;
    TArray<FColor> MaskPixels;
    TArray<FColor> ConfidencePixels;
    TArray<uint8> BinaryMask;
    LookupPixels.Init(FFloat16Color(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f)), PixelCount);
    ColorPixels.Init(FColor(0, 0, 0, 255), PixelCount);
    MaskPixels.Init(FColor(0, 0, 0, 255), PixelCount);
    ConfidencePixels.Init(FColor(0, 0, 0, 255), PixelCount);
    BinaryMask.Init(0, PixelCount);

    TMap<FName, FWetClothingTextureReadback> SourceTextureDataByLayerId;
    BuildSourceTextureReadbacks(Settings, SourceTextureDataByLayerId);

    for (const FDWCBakeRayHit& Hit : Hits)
    {
        if (Hit.Pixel.X < 0 || Hit.Pixel.Y < 0 || Hit.Pixel.X >= Width || Hit.Pixel.Y >= Height)
        {
            continue;
        }

        const int32 PixelIndex = Hit.Pixel.Y * Width + Hit.Pixel.X;
        if (!LookupPixels.IsValidIndex(PixelIndex))
        {
            continue;
        }

        const uint8 Confidence = Hit.bHit ? EncodeUnitFloat(Hit.Confidence) : 0;
        if (Hit.bHit)
        {
            LookupPixels[PixelIndex] = FFloat16Color(FLinearColor(
                FMath::Clamp(static_cast<float>(Hit.SourceUV.X), 0.0f, 1.0f),
                FMath::Clamp(static_cast<float>(Hit.SourceUV.Y), 0.0f, 1.0f),
                EncodeLayerIndexFloat(Hit.SourceLayerId, Settings.SourceLayerIds),
                FMath::Clamp(Hit.Confidence, 0.0f, 1.0f)));
            if (const FWetClothingTextureReadback* SourceTextureData = SourceTextureDataByLayerId.Find(Hit.SourceLayerId))
            {
                ColorPixels[PixelIndex] = SampleTextureBilinear(*SourceTextureData, Hit.SourceUV).ToFColor(true);
            }
            BinaryMask[PixelIndex] = 1;
            ConfidencePixels[PixelIndex] = FColor(Confidence, Confidence, Confidence, 255);
        }
        else
        {
            LookupPixels[PixelIndex] = FFloat16Color(FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
            ColorPixels[PixelIndex] = FColor(0, 0, 0, 255);
            BinaryMask[PixelIndex] = 0;
            ConfidencePixels[PixelIndex] = FColor(0, 0, 0, 255);
        }
    }

    BuildFeatheredMaskPixels(BinaryMask, Width, Height, Settings.MaskFeatherRadiusPixels, MaskPixels);

    OutTextures.LookupMap = CreateOrUpdateFloatTextureAsset(
        Settings.PackagePath,
        Settings.AssetNamePrefix + TEXT("_Lookup"),
        Width,
        Height,
        LookupPixels);

    OutTextures.ColorMap = CreateOrUpdateTextureAsset(
        Settings.PackagePath,
        Settings.AssetNamePrefix + TEXT("_Color"),
        Width,
        Height,
        ColorPixels,
        true);

    OutTextures.MaskMap = CreateOrUpdateTextureAsset(
        Settings.PackagePath,
        Settings.AssetNamePrefix + TEXT("_Mask"),
        Width,
        Height,
        MaskPixels,
        false);

    OutTextures.ConfidenceMap = CreateOrUpdateTextureAsset(
        Settings.PackagePath,
        Settings.AssetNamePrefix + TEXT("_Confidence"),
        Width,
        Height,
        ConfidencePixels,
        false);

    if (OutTextures.LookupMap == nullptr || OutTextures.ColorMap == nullptr || OutTextures.MaskMap == nullptr || OutTextures.ConfidenceMap == nullptr)
    {
        SetError(OutErrorMessage, TEXT("Failed to create one or more reveal bake textures."));
        return false;
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

UTexture2D* FDWCRevealBakeTextureWriter::CreateOrUpdateTextureAsset(
    const FString&        PackagePath,
    const FString&        AssetName,
    const int32           Width,
    const int32           Height,
    const TArray<FColor>& Pixels,
    const bool            bSRGB)
{
    if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height || PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return nullptr;
    }

    const FString NormalizedPackagePath = PackagePath.EndsWith(TEXT("/")) ? PackagePath.LeftChop(1) : PackagePath;
    const FString PackageName = NormalizedPackagePath / AssetName;
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
    UPackage* Package = nullptr;

    if (Texture != nullptr)
    {
        Package = Texture->GetOutermost();
        Texture->Modify();
    }
    else
    {
        Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            return nullptr;
        }

        Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    }

    if (Texture == nullptr)
    {
        return nullptr;
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->CompressionSettings = bSRGB ? TC_Default : TC_VectorDisplacementmap;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->SRGB = bSRGB;
    Texture->LODGroup = TEXTUREGROUP_Pixels2D;
    Texture->UpdateResource();
    Texture->MarkPackageDirty();

    if (Package != nullptr)
    {
        Package->MarkPackageDirty();
    }

    FAssetRegistryModule::AssetCreated(Texture);
    return Texture;
}

UTexture2D* FDWCRevealBakeTextureWriter::CreateOrUpdateFloatTextureAsset(
    const FString&              PackagePath,
    const FString&              AssetName,
    const int32                 Width,
    const int32                 Height,
    const TArray<FFloat16Color>& Pixels)
{
    if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height || PackagePath.IsEmpty() || AssetName.IsEmpty())
    {
        return nullptr;
    }

    const FString NormalizedPackagePath = PackagePath.EndsWith(TEXT("/")) ? PackagePath.LeftChop(1) : PackagePath;
    const FString PackageName = NormalizedPackagePath / AssetName;
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
    UPackage* Package = nullptr;

    if (Texture != nullptr)
    {
        Package = Texture->GetOutermost();
        Texture->Modify();
    }
    else
    {
        Package = CreatePackage(*PackageName);
        if (Package == nullptr)
        {
            return nullptr;
        }

        Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    }

    if (Texture == nullptr)
    {
        return nullptr;
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_RGBA16F, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->CompressionSettings = TC_HDR;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->SRGB = false;
    Texture->LODGroup = TEXTUREGROUP_Pixels2D;
    Texture->UpdateResource();
    Texture->MarkPackageDirty();

    if (Package != nullptr)
    {
        Package->MarkPackageDirty();
    }

    FAssetRegistryModule::AssetCreated(Texture);
    return Texture;
}

void FDWCRevealBakeTextureWriter::BuildFeatheredMaskPixels(
    const TArray<uint8>& BinaryMask,
    const int32          Width,
    const int32          Height,
    const float          FeatherRadiusPixels,
    TArray<FColor>&      OutMaskPixels)
{
    OutMaskPixels.Init(FColor(0, 0, 0, 255), Width * Height);

    if (Width <= 0 || Height <= 0 || BinaryMask.Num() != Width * Height)
    {
        return;
    }

    if (FeatherRadiusPixels <= 0.0f)
    {
        for (int32 PixelIndex = 0; PixelIndex < BinaryMask.Num(); ++PixelIndex)
        {
            const uint8 Value = BinaryMask[PixelIndex] != 0 ? 255 : 0;
            OutMaskPixels[PixelIndex] = FColor(Value, Value, Value, 255);
        }
        return;
    }

    const int32 SearchRadius = FMath::CeilToInt(FeatherRadiusPixels);
    const float SafeFeatherRadius = FMath::Max(FeatherRadiusPixels, KINDA_SMALL_NUMBER);

    for (int32 Y = 0; Y < Height; ++Y)
    {
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 PixelIndex = Y * Width + X;
            const bool bInside = BinaryMask[PixelIndex] != 0;

            float NearestOppositeDistanceSq = TNumericLimits<float>::Max();
            for (int32 OffsetY = -SearchRadius; OffsetY <= SearchRadius; ++OffsetY)
            {
                const int32 SampleY = Y + OffsetY;
                if (SampleY < 0 || SampleY >= Height)
                {
                    continue;
                }

                for (int32 OffsetX = -SearchRadius; OffsetX <= SearchRadius; ++OffsetX)
                {
                    const int32 SampleX = X + OffsetX;
                    if (SampleX < 0 || SampleX >= Width)
                    {
                        continue;
                    }

                    const int32 SampleIndex = SampleY * Width + SampleX;
                    const bool bSampleInside = BinaryMask[SampleIndex] != 0;
                    if (bSampleInside == bInside)
                    {
                        continue;
                    }

                    const float DistanceSq = static_cast<float>(OffsetX * OffsetX + OffsetY * OffsetY);
                    NearestOppositeDistanceSq = FMath::Min(NearestOppositeDistanceSq, DistanceSq);
                }
            }

            float MaskValue = bInside ? 1.0f : 0.0f;
            if (NearestOppositeDistanceSq < TNumericLimits<float>::Max())
            {
                const float Distance = FMath::Sqrt(NearestOppositeDistanceSq);
                const float EdgeAlpha = FMath::Clamp(Distance / SafeFeatherRadius, 0.0f, 1.0f);
                MaskValue = bInside ? (0.5f + 0.5f * EdgeAlpha) : (0.5f - 0.5f * EdgeAlpha);
            }

            const uint8 EncodedMask = EncodeUnitFloat(MaskValue);
            OutMaskPixels[PixelIndex] = FColor(EncodedMask, EncodedMask, EncodedMask, 255);
        }
    }
}

uint8 FDWCRevealBakeTextureWriter::EncodeUnitFloat(const float Value)
{
    return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255));
}

uint8 FDWCRevealBakeTextureWriter::EncodeLayerIndex(const FName LayerId, const TArray<FName>& SourceLayerIds)
{
    const int32 LayerIndex = SourceLayerIds.IndexOfByKey(LayerId);
    if (LayerIndex == INDEX_NONE || SourceLayerIds.Num() <= 1)
    {
        return 0;
    }

    return EncodeUnitFloat(static_cast<float>(LayerIndex) / static_cast<float>(SourceLayerIds.Num() - 1));
}

float FDWCRevealBakeTextureWriter::EncodeLayerIndexFloat(const FName LayerId, const TArray<FName>& SourceLayerIds)
{
    const int32 LayerIndex = SourceLayerIds.IndexOfByKey(LayerId);
    if (LayerIndex == INDEX_NONE || SourceLayerIds.Num() <= 1)
    {
        return 0.0f;
    }

    return FMath::Clamp(static_cast<float>(LayerIndex) / static_cast<float>(SourceLayerIds.Num() - 1), 0.0f, 1.0f);
}

void FDWCRevealBakeTextureWriter::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    if (OutErrorMessage != nullptr)
    {
        *OutErrorMessage = InMessage;
    }
}
