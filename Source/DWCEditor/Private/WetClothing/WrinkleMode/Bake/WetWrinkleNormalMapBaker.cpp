#include "WetClothing/WrinkleMode/Bake/WetWrinkleNormalMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace
{
    struct FWetWrinkleNormalSource
    {
        explicit FWetWrinkleNormalSource(UTexture2D* InTexture)
            : Texture(InTexture)
        {
            if (Texture == nullptr || !Texture->Source.IsValid())
            {
                return;
            }

            SizeX = Texture->Source.GetSizeX();
            SizeY = Texture->Source.GetSizeY();
            SourceFormat = Texture->Source.GetFormat();
            if (SizeX <= 0 || SizeY <= 0 ||
                (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_BGRE8 && SourceFormat != TSF_G8 && SourceFormat != TSF_G16))
            {
                return;
            }

            bFlipGreenChannel = Texture->bFlipGreenChannel;
            MipData = Texture->Source.LockMipReadOnly(0);
        }

        ~FWetWrinkleNormalSource()
        {
            if (Texture != nullptr && MipData != nullptr)
            {
                Texture->Source.UnlockMip(0);
            }
        }

        bool IsValid() const
        {
            return Texture != nullptr && MipData != nullptr && SizeX > 0 && SizeY > 0;
        }

        FVector SampleNormalTS(const FVector2D& UV) const
        {
            if (!IsValid())
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            const float ClampedU = FMath::Clamp(static_cast<float>(UV.X), 0.0f, 1.0f);
            const float ClampedV = FMath::Clamp(static_cast<float>(UV.Y), 0.0f, 1.0f);
            const int32 PixelX = FMath::Clamp(FMath::FloorToInt(ClampedU * static_cast<float>(SizeX)), 0, SizeX - 1);
            const int32 PixelY = FMath::Clamp(FMath::FloorToInt(ClampedV * static_cast<float>(SizeY)), 0, SizeY - 1);

            if (SourceFormat == TSF_G8)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            if (SourceFormat == TSF_G16)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            const FColor* ColorData = reinterpret_cast<const FColor*>(MipData);
            const FColor Color = ColorData[PixelY * SizeX + PixelX];
            const float DecodedX = static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f;
            float DecodedY = -(static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f);
            if (bFlipGreenChannel)
            {
                DecodedY = -DecodedY;
            }

            FVector DecodedNormal(
                DecodedX,
                DecodedY,
                static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
            if (DecodedNormal.Z <= UE_SMALL_NUMBER)
            {
                const float XYLengthSq = FMath::Min(DecodedNormal.X * DecodedNormal.X + DecodedNormal.Y * DecodedNormal.Y, 1.0f);
                DecodedNormal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
            }

            return DecodedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        }

        UTexture2D* Texture = nullptr;
        const uint8* MipData = nullptr;
        int32 SizeX = 0;
        int32 SizeY = 0;
        ETextureSourceFormat SourceFormat = TSF_Invalid;
        bool bFlipGreenChannel = false;
    };

    float WrapUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    FVector2D WrapUV(const FVector2D& UV)
    {
        return FVector2D(WrapUnit(UV.X), WrapUnit(UV.Y));
    }

    float WrappedDelta(float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    float SmoothStep(float Edge0, float Edge1, float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }

        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    FVector DecodeNormal(const FColor& Color)
    {
        FVector Normal(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
        if (Normal.Z <= UE_SMALL_NUMBER)
        {
            const float XYLengthSq = FMath::Min(Normal.X * Normal.X + Normal.Y * Normal.Y, 1.0f);
            Normal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
        }

        return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
    }

    FColor EncodeNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    uint8 EncodeUnit(float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }
}

struct FWetWrinkleNormalMapBaker::FBakeGroup
{
    int32 LODIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    UTexture* SourceTexture = nullptr;
    TArray<const FWetWrinklePatchPlacement*> Stamps;
};

bool FWetWrinkleNormalMapBaker::BakeMaterialSlot(
    UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FWetWrinkleNormalMapBakeResult();

    if (WetClothingAsset == nullptr)
    {
        OutErrorMessage = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a material slot before baking a wrinkle normal map.");
        return false;
    }

    const int32 WrinkleUVChannelIndex = 0;

    TSet<int32> TargetSlots;
    TargetSlots.Add(MaterialSlotIndex);

    TArray<int32> LODIndices = WetClothingAsset->WrinkleData.BakeSettings.TargetLODIndices;
    if (LODIndices.Num() == 0)
    {
        LODIndices.Add(0);
    }

    for (const int32 LODIndex : LODIndices)
    {
        FBakeGroup Group;
        Group.LODIndex = FMath::Max(0, LODIndex);
        Group.MaterialSlotIndex = MaterialSlotIndex;
        Group.UVChannelIndex = WrinkleUVChannelIndex;

        for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset->WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes)
            {
                continue;
            }

            for (const FWetWrinklePatchPlacement& Stamp : Stroke.PatchPlacements)
            {
                if (Stamp.MaterialSlotIndex != MaterialSlotIndex ||
                    Stamp.UVChannelIndex != Group.UVChannelIndex ||
                    Stamp.NormalPatchTexture == nullptr ||
                    Stamp.SourceTexture == nullptr)
                {
                    continue;
                }

                if (Group.SourceTexture == nullptr)
                {
                    Group.SourceTexture = Stamp.SourceTexture;
                }

                if (Group.SourceTexture == Stamp.SourceTexture)
                {
                    Group.Stamps.Add(&Stamp);
                }
            }
        }

        if (Group.Stamps.Num() > 0 && !BakeGroup(*WetClothingAsset, Group, Settings, OutResult, OutErrorMessage))
        {
            return false;
        }
    }

    if (OutResult.BakedMapCount == 0)
    {
        OutErrorMessage = TEXT("No wrinkle stamps were found for the selected material slot.");
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

bool FWetWrinkleNormalMapBaker::BakeGroup(
    UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeResult& InOutResult,
    FString& OutErrorMessage)
{
    if (!Settings.bBakeNormalMap && !Settings.bBakeMask)
    {
        OutErrorMessage = TEXT("No wrinkle bake outputs are enabled.");
        return false;
    }

    if (Group.SourceTexture == nullptr || Group.Stamps.Num() == 0)
    {
        return true;
    }

    const int32 MaxResolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    const int32 SourceWidth = FMath::Max(Group.SourceTexture->GetSurfaceWidth(), 1);
    const int32 SourceHeight = FMath::Max(Group.SourceTexture->GetSurfaceHeight(), 1);
    const double ResolutionScale = static_cast<double>(MaxResolution) / FMath::Max(SourceWidth, SourceHeight);
    const int32 Width = FMath::Clamp(FMath::RoundToInt(SourceWidth * ResolutionScale), 1, 8192);
    const int32 Height = FMath::Clamp(FMath::RoundToInt(SourceHeight * ResolutionScale), 1, 8192);

    TArray<FColor> NormalPixels;
    TArray<FColor> MaskPixels;
    NormalPixels.Init(EncodeNormal(FVector(0.0f, 0.0f, 1.0f)), Width * Height);
    MaskPixels.Init(FColor(0, 0, 0, 255), Width * Height);

    int32 BakedStampCount = 0;
    for (const FWetWrinklePatchPlacement* StampPtr : Group.Stamps)
    {
        const FWetWrinklePatchPlacement& Stamp = *StampPtr;
        FWetWrinkleNormalSource NormalSource(Stamp.NormalPatchTexture);
        if (!NormalSource.IsValid() || Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            continue;
        }

        ++BakedStampCount;

        const FVector2D WrappedCenter = WrapUV(Stamp.PositionUV);
        const FVector2D SafeScale(
            FMath::Max(FMath::Abs(Stamp.Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Stamp.Scale.Y), UE_SMALL_NUMBER));
        const float EdgeFadeStart = FMath::Clamp(1.0f - Stamp.Falloff, 0.0f, 0.98f);
        const float CosRotation = FMath::Cos(Stamp.RotationRadians);
        const float SinRotation = FMath::Sin(Stamp.RotationRadians);

        for (int32 TileOffsetY = -1; TileOffsetY <= 1; ++TileOffsetY)
        {
            for (int32 TileOffsetX = -1; TileOffsetX <= 1; ++TileOffsetX)
            {
                const FVector2D TileCenter = WrappedCenter + FVector2D(static_cast<float>(TileOffsetX), static_cast<float>(TileOffsetY));
                const int32 MinX = FMath::Clamp(FMath::FloorToInt((TileCenter.X - Stamp.BrushRadiusUV) * Width), 0, Width - 1);
                const int32 MaxX = FMath::Clamp(FMath::CeilToInt((TileCenter.X + Stamp.BrushRadiusUV) * Width), 0, Width - 1);
                const int32 MinY = FMath::Clamp(FMath::FloorToInt((TileCenter.Y - Stamp.BrushRadiusUV) * Height), 0, Height - 1);
                const int32 MaxY = FMath::Clamp(FMath::CeilToInt((TileCenter.Y + Stamp.BrushRadiusUV) * Height), 0, Height - 1);
                if (MinX > MaxX || MinY > MaxY)
                {
                    continue;
                }

                for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
                {
                    for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                    {
                        const FVector2D PixelUV(
                            (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(Width),
                            (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(Height));
                        const FVector2D DeltaUV(
                            WrappedDelta(PixelUV.X - TileCenter.X),
                            WrappedDelta(PixelUV.Y - TileCenter.Y));
                        const FVector2D Local = DeltaUV / FMath::Max(Stamp.BrushRadiusUV, UE_SMALL_NUMBER);
                        const float DistanceFromCenter = Local.Size();
                        if (DistanceFromCenter > 1.0f)
                        {
                            continue;
                        }

                        const float EdgeFade = 1.0f - SmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
                        if (EdgeFade <= UE_SMALL_NUMBER)
                        {
                            continue;
                        }

                        const float BrushLocalX = (CosRotation * Local.X + SinRotation * Local.Y) / SafeScale.X;
                        const float BrushLocalY = (-SinRotation * Local.X + CosRotation * Local.Y) / SafeScale.Y;
                        if (FMath::Abs(BrushLocalX) > 1.0f || FMath::Abs(BrushLocalY) > 1.0f)
                        {
                            continue;
                        }

                        const FVector2D BrushTextureUV(BrushLocalX * 0.5f + 0.5f, BrushLocalY * 0.5f + 0.5f);
                        const FVector BrushNormalTS = NormalSource.SampleNormalTS(BrushTextureUV);
                        const FVector RotatedBrushNormalTS(
                            BrushNormalTS.X * CosRotation - BrushNormalTS.Y * SinRotation,
                            BrushNormalTS.X * SinRotation + BrushNormalTS.Y * CosRotation,
                            BrushNormalTS.Z);
                        const float StrengthScale = FMath::Max(Stamp.Strength * EdgeFade, 0.0f);
                        const FVector StampNormalTS =
                            FVector(
                                RotatedBrushNormalTS.X * StrengthScale,
                                RotatedBrushNormalTS.Y * StrengthScale,
                                RotatedBrushNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));

                        const int32 PixelIndex = PixelY * Width + PixelX;
                        const FVector ExistingNormalTS = DecodeNormal(NormalPixels[PixelIndex]);
                        const FVector BlendedNormalTS =
                            FVector(
                                ExistingNormalTS.X + StampNormalTS.X,
                                ExistingNormalTS.Y + StampNormalTS.Y,
                                ExistingNormalTS.Z * StampNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
                        NormalPixels[PixelIndex] = EncodeNormal(BlendedNormalTS);

                        const uint8 MaskValue = EncodeUnit(FMath::Max(static_cast<float>(MaskPixels[PixelIndex].R) / 255.0f, EdgeFade));
                        MaskPixels[PixelIndex] = FColor(MaskValue, MaskValue, MaskValue, 255);
                    }
                }
            }
        }
    }

    if (BakedStampCount == 0)
    {
        return true;
    }

    const FString BaseSuffix = FString::Printf(
        TEXT("Slot%d_UV%d_LOD%d"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex);

    UTexture2D* NormalTexture = nullptr;
    if (Settings.bBakeNormalMap)
    {
        NormalTexture = CreateOrUpdateTextureAsset(
            WetClothingAsset,
            BaseSuffix + TEXT("_WrinkleNormalMap"),
            Width,
            Height,
            NormalPixels,
            true,
            OutErrorMessage);
        if (NormalTexture == nullptr)
        {
            return false;
        }
    }

    UTexture2D* MaskTexture = nullptr;
    if (Settings.bBakeMask)
    {
        MaskTexture = CreateOrUpdateTextureAsset(
            WetClothingAsset,
            BaseSuffix + TEXT("_WrinkleMask"),
            Width,
            Height,
            MaskPixels,
            false,
            OutErrorMessage);
        if (MaskTexture == nullptr)
        {
            return false;
        }
    }

    WetClothingAsset.Modify();
    FWetWrinkleBakedMapSet* BakedMap = WetClothingAsset.WrinkleData.BakedWrinkleMaps.FindByPredicate(
        [&Group](const FWetWrinkleBakedMapSet& ExistingMap)
        {
            return ExistingMap.LODIndex == Group.LODIndex &&
                   ExistingMap.MaterialSlotIndex == Group.MaterialSlotIndex &&
                   ExistingMap.UVChannelIndex == Group.UVChannelIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &WetClothingAsset.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    }

    BakedMap->LODIndex = Group.LODIndex;
    BakedMap->MaterialSlotIndex = Group.MaterialSlotIndex;
    BakedMap->UVChannelIndex = Group.UVChannelIndex;
    if (Settings.bBakeNormalMap)
    {
        BakedMap->BakedWrinkleNormalMap = NormalTexture;
    }
    if (Settings.bBakeMask)
    {
        BakedMap->BakedWrinkleMask = MaskTexture;
    }
    BakedMap->Resolution = MaxResolution;
    BakedMap->PaddingPixels = FMath::Clamp(Settings.PaddingPixels, 0, 64);
    BakedMap->BuildSignature = MakeBuildSignature(WetClothingAsset, Group, Width, Height);
    BakedMap->BakeGuid = FGuid::NewGuid();
    WetClothingAsset.MarkPackageDirty();

    InOutResult.BakedMapCount++;
    InOutResult.BakedStampCount += BakedStampCount;
    if (NormalTexture != nullptr)
    {
        InOutResult.BakedNormalMaps.Add(NormalTexture);
    }
    if (MaskTexture != nullptr)
    {
        InOutResult.BakedMasks.Add(MaskTexture);
    }

    OutErrorMessage.Reset();
    return true;
}

FString FWetWrinkleNormalMapBaker::MakeBuildSignature(
    const UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const int32 Width,
    const int32 Height)
{
    FString Canonical;
    Canonical.Reserve(4096);
    Canonical += TEXT("DWC.WrinkleNormalMap.v1|");
    Canonical += WetClothingAsset.GetPathName();
    Canonical += FString::Printf(
        TEXT("|Slot=%d|UV=%d|LOD=%d|Size=%dx%d|Source=%s"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        Width,
        Height,
        *GetPathNameSafe(Group.SourceTexture));

    for (const FWetWrinklePatchPlacement* Stamp : Group.Stamps)
    {
        Canonical += FString::Printf(
            TEXT("|Stamp:%s;UV=%.9g,%.9g;Radius=%.9g;Rot=%.9g;Scale=%.9g,%.9g;Strength=%.9g;Falloff=%.9g;Normal=%s"),
            *Stamp->PatchGuid.ToString(EGuidFormats::Digits),
            Stamp->PositionUV.X,
            Stamp->PositionUV.Y,
            Stamp->BrushRadiusUV,
            Stamp->RotationRadians,
            Stamp->Scale.X,
            Stamp->Scale.Y,
            Stamp->Strength,
            Stamp->Falloff,
            *GetPathNameSafe(Stamp->NormalPatchTexture));
    }

    return FMD5::HashAnsiString(*Canonical);
}

UTexture2D* FWetWrinkleNormalMapBaker::CreateOrUpdateTextureAsset(
    UWetClothingAsset& WetClothingAsset,
    const FString& ObjectSuffix,
    const int32 Width,
    const int32 Height,
    const TArray<FColor>& Pixels,
    const bool bNormalMap,
    FString& OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    const FString AssetPackageName = WetClothingAsset.GetOutermost()->GetName();
    const FString PackagePath = FPackageName::GetLongPackagePath(AssetPackageName);
    if (PackagePath.IsEmpty())
    {
        OutErrorMessage = TEXT("Could not resolve a package path for the Wet Clothing Asset.");
        return nullptr;
    }

    const FString ObjectName = ObjectTools::SanitizeObjectName(
        FString::Printf(TEXT("T_%s_%s"), *WetClothingAsset.GetName(), *ObjectSuffix));
    const FString TexturePackageName = PackagePath / ObjectName;
    const FString TextureObjectPath = TexturePackageName + TEXT(".") + ObjectName;

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TextureObjectPath);
    if (Texture == nullptr)
    {
        UPackage* Package = CreatePackage(*TexturePackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create package '%s'."), *TexturePackageName);
            return nullptr;
        }

        Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Texture);
    }
    else
    {
        Texture->Modify();
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->SRGB = false;
    Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Grayscale;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Wrap;
    Texture->AddressY = TA_Wrap;
    Texture->PostEditChange();
    Texture->UpdateResource();
    Texture->MarkPackageDirty();

    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wrinkle normal map baking requires editor-only texture source data.");
    return nullptr;
#endif
}
