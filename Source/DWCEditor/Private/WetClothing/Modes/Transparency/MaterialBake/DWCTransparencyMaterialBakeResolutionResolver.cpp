// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialBakeResolutionResolver.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"

namespace
{
    struct FPropertyTextureCandidate
    {
        EMaterialProperty Property = MP_BaseColor;
        UTexture2D* Texture = nullptr;
        FIntPoint Dimensions = FIntPoint::ZeroValue;
    };

    FIntPoint GetMaterialBakeAuthoredTextureDimensions(const UTexture2D* Texture)
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

    FString HashMaterialBakeResolutionCanonical(const FString& Canonical)
    {
        return FMD5::HashAnsiString(*Canonical);
    }
}

int32 FDWCTransparencyMaterialBakeResolutionResolver::ResolveAutomaticResolutionFromDimensions(
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
        ? FDWCTransparencyResolutionResolver::NormalizeResolution(LargestDimension)
        : DefaultResolution;
}

FDWCTransparencyResolvedMaterialBakeResolution
FDWCTransparencyMaterialBakeResolutionResolver::Resolve(
    UMaterialInterface* EffectiveMaterial)
{
    FDWCTransparencyResolvedMaterialBakeResolution Result;
    if (EffectiveMaterial == nullptr)
    {
        Result.Source = EDWCTransparencyMaterialBakeResolutionSource::MissingMaterialFallback;
        Result.SourceDescription = FString::Printf(
            TEXT("Automatic fallback %d (missing source material)"),
            DefaultResolution);
        Result.Identity = HashMaterialBakeResolutionCanonical(FString::Printf(
            TEXT("DWC.Transparency.MaterialBakeResolution.v1|Fallback=Missing|Resolution=%d"),
            DefaultResolution));
        return Result;
    }

    constexpr EMaterialProperty Properties[] = {MP_BaseColor, MP_Normal, MP_Metallic};
    TArray<FPropertyTextureCandidate> Candidates;
    TSet<FString> SeenPropertyTextures;
    for (const EMaterialProperty Property : Properties)
    {
        TArray<UTexture*> PropertyTextures;
        EffectiveMaterial->GetTexturesInPropertyChain(Property, PropertyTextures, nullptr, nullptr);
        for (UTexture* PropertyTexture : PropertyTextures)
        {
            UTexture2D* Texture = Cast<UTexture2D>(PropertyTexture);
            const FString CandidateKey = FString::Printf(
                TEXT("%d:%s"), static_cast<int32>(Property), *GetPathNameSafe(Texture));
            if (Texture == nullptr || SeenPropertyTextures.Contains(CandidateKey))
            {
                continue;
            }
            const FIntPoint Dimensions = GetMaterialBakeAuthoredTextureDimensions(Texture);
            if (Dimensions.X <= 0 || Dimensions.Y <= 0)
            {
                continue;
            }
            SeenPropertyTextures.Add(CandidateKey);
            FPropertyTextureCandidate& Candidate = Candidates.AddDefaulted_GetRef();
            Candidate.Property = Property;
            Candidate.Texture = Texture;
            Candidate.Dimensions = Dimensions;
        }
    }

    Candidates.Sort([](const FPropertyTextureCandidate& Left, const FPropertyTextureCandidate& Right)
    {
        if (Left.Property != Right.Property)
        {
            return static_cast<int32>(Left.Property) < static_cast<int32>(Right.Property);
        }
        return Left.Texture->GetPathName() < Right.Texture->GetPathName();
    });

    TArray<FIntPoint> CandidateDimensions;
    FString CandidateCanonical;
    const FPropertyTextureCandidate* LargestCandidate = nullptr;
    int32 LargestCandidateDimension = 0;
    for (const FPropertyTextureCandidate& Candidate : Candidates)
    {
        CandidateDimensions.Add(Candidate.Dimensions);
        const int32 LargestDimension = FMath::Max(
            Candidate.Dimensions.X,
            Candidate.Dimensions.Y);
        if (LargestDimension > LargestCandidateDimension)
        {
            LargestCandidateDimension = LargestDimension;
            LargestCandidate = &Candidate;
        }
        CandidateCanonical += FString::Printf(
            TEXT("|Property=%d:Texture=%s:%dx%d:%s"),
            static_cast<int32>(Candidate.Property),
            *Candidate.Texture->GetPathName(),
            Candidate.Dimensions.X,
            Candidate.Dimensions.Y,
            *Candidate.Texture->Source.GetId().ToString(EGuidFormats::Digits));
    }

    Result.Resolution = ResolveAutomaticResolutionFromDimensions(CandidateDimensions);
    Result.bUsedFallback = CandidateDimensions.IsEmpty();
    Result.Source = Result.bUsedFallback
        ? EDWCTransparencyMaterialBakeResolutionSource::ProceduralFallback
        : EDWCTransparencyMaterialBakeResolutionSource::MaterialPropertyTexture;
    if (LargestCandidate != nullptr)
    {
        Result.SourceDescription = FString::Printf(
            TEXT("Auto %d from %s (%dx%d)"),
            Result.Resolution,
            *GetNameSafe(LargestCandidate->Texture),
            LargestCandidate->Dimensions.X,
            LargestCandidate->Dimensions.Y);
    }
    else
    {
        Result.SourceDescription = FString::Printf(
            TEXT("Automatic fallback %d (procedural or constant source material)"),
            Result.Resolution);
    }
    Result.Identity = HashMaterialBakeResolutionCanonical(FString::Printf(
        TEXT("DWC.Transparency.MaterialBakeResolution.v1|Material=%s|Resolution=%d%s"),
        *EffectiveMaterial->GetPathName(),
        Result.Resolution,
        *CandidateCanonical));
    return Result;
}
