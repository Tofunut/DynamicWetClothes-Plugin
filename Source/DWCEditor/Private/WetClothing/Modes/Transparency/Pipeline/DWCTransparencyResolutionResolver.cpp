// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"

namespace
{
    FIntPoint GetAuthoredTextureDimensions(const UTexture2D* Texture)
    {
        if (Texture == nullptr)
        {
            return FIntPoint::ZeroValue;
        }
        if (Texture->Source.IsValid())
        {
            return FIntPoint(
                IntCastChecked<int32>(Texture->Source.GetSizeX()),
                IntCastChecked<int32>(Texture->Source.GetSizeY()));
        }
        return FIntPoint(
            FMath::Max(0, Texture->GetSurfaceWidth()),
            FMath::Max(0, Texture->GetSurfaceHeight()));
    }

    FString HashResolutionCanonical(const FString& Canonical)
    {
        return FMD5::HashAnsiString(*Canonical);
    }
}

int32 FDWCTransparencyResolutionResolver::NormalizeResolution(const int32 RequestedResolution)
{
    constexpr int32 SupportedResolutions[] = {256, 512, 1024, 2048, 4096};
    const int32 Clamped = FMath::Clamp(
        RequestedResolution,
        MinimumResolution,
        MaximumResolution);
    for (const int32 SupportedResolution : SupportedResolutions)
    {
        if (Clamped <= SupportedResolution)
        {
            return SupportedResolution;
        }
    }
    return MaximumResolution;
}

int32 FDWCTransparencyResolutionResolver::ResolveAutomaticResolutionFromDimensions(
    const TConstArrayView<FIntPoint> CandidateDimensions)
{
    int32 LargestDimension = 0;
    for (const FIntPoint Dimensions : CandidateDimensions)
    {
        LargestDimension = FMath::Max(
            LargestDimension,
            FMath::Max(Dimensions.X, Dimensions.Y));
    }
    return LargestDimension > 0
        ? NormalizeResolution(LargestDimension)
        : DefaultResolution;
}

FDWCTransparencyResolvedOutputResolution FDWCTransparencyResolutionResolver::Resolve(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer)
{
    FDWCTransparencyResolvedOutputResolution Result;
    if (Layer.OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Override)
    {
        Result.Size = NormalizeResolution(Layer.OutputResolutionOverride);
        Result.Source = EDWCTransparencyResolutionSource::Override;
        Result.bUsedFallback = false;
        Result.SourceDescription = FString::Printf(TEXT("Override %d"), Result.Size);
        Result.Identity = HashResolutionCanonical(FString::Printf(
            TEXT("DWC.Transparency.Resolution.v1|Mode=Override|Resolution=%d"),
            Result.Size));
        return Result;
    }

    return ResolveAutomatic(Asset, Layer);
}

FDWCTransparencyResolvedOutputResolution
FDWCTransparencyResolutionResolver::ResolveAutomatic(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer)
{
    FDWCTransparencyResolvedOutputResolution Result;

    const USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    const int32 MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    UMaterialInterface* EffectiveMaterial =
        SourceMesh != nullptr && SourceMesh->GetMaterials().IsValidIndex(MaterialSlotIndex)
        ? SourceMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface
        : nullptr;
    if (EffectiveMaterial == nullptr)
    {
        Result.Source = EDWCTransparencyResolutionSource::MissingTargetFallback;
        Result.SourceDescription = FString::Printf(
            TEXT("Automatic fallback %d (missing original target material)"),
            DefaultResolution);
        Result.Identity = HashResolutionCanonical(FString::Printf(
            TEXT("DWC.Transparency.Resolution.v1|Mode=Auto|Fallback=Missing|Mesh=%s|Slot=%d|Resolution=%d"),
            *GetPathNameSafe(SourceMesh), MaterialSlotIndex, DefaultResolution));
        return Result;
    }

    TArray<UTexture*> PropertyTextures;
    EffectiveMaterial->GetTexturesInPropertyChain(
        MP_BaseColor,
        PropertyTextures,
        nullptr,
        nullptr);

    TArray<UTexture2D*> CandidateTextures;
    for (UTexture* PropertyTexture : PropertyTextures)
    {
        if (UTexture2D* Texture2D = Cast<UTexture2D>(PropertyTexture))
        {
            CandidateTextures.AddUnique(Texture2D);
        }
    }
    CandidateTextures.Sort(
        [](const UTexture2D& Left, const UTexture2D& Right)
        {
            return Left.GetPathName() < Right.GetPathName();
        });

    TArray<FIntPoint> CandidateDimensions;
    FString CandidateCanonical;
    int32 LargestCandidateIndex = INDEX_NONE;
    int32 LargestCandidateDimension = 0;
    UTexture2D* LargestCandidateTexture = nullptr;
    for (UTexture2D* Texture : CandidateTextures)
    {
        const FIntPoint Dimensions = GetAuthoredTextureDimensions(Texture);
        if (Dimensions.X <= 0 || Dimensions.Y <= 0)
        {
            continue;
        }
        const int32 CandidateIndex = CandidateDimensions.Add(Dimensions);
        const int32 LargestDimension = FMath::Max(Dimensions.X, Dimensions.Y);
        if (LargestDimension > LargestCandidateDimension)
        {
            LargestCandidateDimension = LargestDimension;
            LargestCandidateIndex = CandidateIndex;
            LargestCandidateTexture = Texture;
        }
        CandidateCanonical += FString::Printf(
            TEXT("|Texture=%s:%dx%d:%s"),
            *Texture->GetPathName(),
            Dimensions.X,
            Dimensions.Y,
            *Texture->Source.GetId().ToString(EGuidFormats::Digits));
    }

    Result.Size = ResolveAutomaticResolutionFromDimensions(CandidateDimensions);
    Result.bUsedFallback = CandidateDimensions.IsEmpty();
    Result.Source = Result.bUsedFallback
        ? EDWCTransparencyResolutionSource::ProceduralFallback
        : EDWCTransparencyResolutionSource::TargetBaseColorTexture;
    if (Result.bUsedFallback)
    {
        Result.SourceDescription = FString::Printf(
            TEXT("Automatic fallback %d (procedural or constant Base Color)"),
            Result.Size);
    }
    else
    {
        const FIntPoint LargestDimensions = CandidateDimensions[LargestCandidateIndex];
        Result.SourceDescription = FString::Printf(
            TEXT("Auto %d from %s (%dx%d)"),
            Result.Size,
            *GetNameSafe(LargestCandidateTexture),
            LargestDimensions.X,
            LargestDimensions.Y);
    }
    Result.Identity = HashResolutionCanonical(FString::Printf(
        TEXT("DWC.Transparency.Resolution.v1|Mode=Auto|Material=%s|Resolution=%d%s"),
        *EffectiveMaterial->GetPathName(),
        Result.Size,
        *CandidateCanonical));
    return Result;
}
