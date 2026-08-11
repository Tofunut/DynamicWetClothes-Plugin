//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"

namespace
{
    constexpr FDWCTransparencyStageArtifactSpec Specs[] = {
        {EDWCTransparencyTempArtifactKind::BaseRevealColor,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("BaseRevealColor"), TSF_BGRA8, true, true, false},
        {EDWCTransparencyTempArtifactKind::ValidHit,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("ValidHit"), TSF_G8, false, true, false},
        {EDWCTransparencyTempArtifactKind::HitSource,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("HitSource"), TSF_G16, false, true, false},
        {EDWCTransparencyTempArtifactKind::HitDistance,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("HitDistance"), TSF_R16F, false, true, false},
        {EDWCTransparencyTempArtifactKind::OuterCoverage,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("OuterCoverage"), TSF_G8, false, true, false},
        {EDWCTransparencyTempArtifactKind::OuterIslandID,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::Source,
         TEXT("OuterIslandID"), TSF_G16, false, true, false},
        {EDWCTransparencyTempArtifactKind::BaseRevealSurface,
         EDWCTransparencyStage::Source, EDWCTransparencyArtifactDependency::RevealSurface,
         TEXT("BaseRevealSurface"), TSF_BGRA8, false, false, true},
        {EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
         EDWCTransparencyStage::Reveal, EDWCTransparencyArtifactDependency::Reveal,
         TEXT("CorrectedRevealColor"), TSF_BGRA8, true, false, false}
    };

    UTexture2D* ResolveTexture(
        const FDWCTransparencyTempArtifactReference& Reference,
        const bool bLoadTexture)
    {
        return bLoadTexture ? Reference.Texture.LoadSynchronous() : Reference.Texture.Get();
    }
}

const FDWCTransparencyStageArtifactSpec* FDWCTransparencyStageArtifactContract::FindSpec(
    const EDWCTransparencyTempArtifactKind Kind)
{
    for (const FDWCTransparencyStageArtifactSpec& Spec : Specs)
    {
        if (Spec.Kind == Kind)
        {
            return &Spec;
        }
    }
    return nullptr;
}

FString FDWCTransparencyStageArtifactContract::GetAssetToken(
    const EDWCTransparencyTempArtifactKind Kind)
{
    const FDWCTransparencyStageArtifactSpec* Spec = FindSpec(Kind);
    return Spec != nullptr ? FString(Spec->AssetToken) : FString();
}

FString FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
    const EDWCTransparencyTempArtifactKind Kind,
    const FString& SourceSignature,
    const FString& RevealSignature)
{
    const FDWCTransparencyStageArtifactSpec* Spec = FindSpec(Kind);
    if (Spec == nullptr)
    {
        return FString();
    }

    FString DependencySignature;
    switch (Spec->Dependency)
    {
    case EDWCTransparencyArtifactDependency::Source:
        DependencySignature = SourceSignature;
        break;
    case EDWCTransparencyArtifactDependency::RevealSurface:
        DependencySignature =
            FDWCTransparencySignatureService::BuildRevealSurfaceAuthoringSignature(SourceSignature);
        break;
    case EDWCTransparencyArtifactDependency::Reveal:
        DependencySignature = RevealSignature;
        break;
    }
    return FDWCTransparencySignatureService::BuildStageArtifactSignature(
        Kind, ContractVersion, DependencySignature);
}

void FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(
    const bool bRequiresRevealSurface,
    TArray<EDWCTransparencyTempArtifactKind>& OutKinds)
{
    OutKinds.Reset();
    for (const FDWCTransparencyStageArtifactSpec& Spec : Specs)
    {
        if (Spec.Stage == EDWCTransparencyStage::Source &&
            (Spec.bRequiredForAllSources ||
             (bRequiresRevealSurface && Spec.bRequiredForProjectedSources)))
        {
            OutKinds.Add(Spec.Kind);
        }
    }
}

const FDWCTransparencyTempArtifactReference*
FDWCTransparencyStageArtifactContract::FindReference(
    const FWetClothingTransparencyLayerData& Layer,
    const EDWCTransparencyTempArtifactKind Kind)
{
#if WITH_EDITORONLY_DATA
    return Layer.EditorStageCache.Artifacts.FindByPredicate(
        [Kind](const FDWCTransparencyTempArtifactReference& Candidate)
        {
            return Candidate.Kind == Kind;
        });
#else
    return nullptr;
#endif
}

bool FDWCTransparencyStageArtifactContract::ValidateReference(
    const FDWCTransparencyTempArtifactReference& Reference,
    const FDWCTransparencyStageArtifactSpec& Spec,
    const FString& ExpectedSignature,
    const FIntPoint ExpectedResolution,
    const FGuid* ExpectedGeneration,
    const bool bLoadTexture,
    FString& OutError)
{
    OutError.Reset();
    if (Reference.bObsolete || Reference.ContractVersion != ContractVersion ||
        Reference.BuildSignature != ExpectedSignature ||
        Reference.Resolution != ExpectedResolution || Reference.Texture.IsNull() ||
        !Reference.CommitGeneration.IsValid() || !Reference.TextureSourceId.IsValid() ||
        (ExpectedGeneration != nullptr && Reference.CommitGeneration != *ExpectedGeneration))
    {
        OutError = FString::Printf(
            TEXT("Transparency Stage artifact '%s' has stale or incomplete metadata."),
            Spec.AssetToken);
        return false;
    }

    if (!bLoadTexture)
    {
        return true;
    }

    UTexture2D* Texture = ResolveTexture(Reference, true);
    if (Texture == nullptr || !Texture->Source.IsValid() ||
        Texture->Source.GetFormat() != Spec.SourceFormat ||
        Texture->Source.GetSizeX() != ExpectedResolution.X ||
        Texture->Source.GetSizeY() != ExpectedResolution.Y ||
        Texture->SRGB != Spec.bSRGB ||
        Texture->Source.GetId() != Reference.TextureSourceId)
    {
        OutError = FString::Printf(
            TEXT("Transparency Stage artifact '%s' does not match its payload contract."),
            Spec.AssetToken);
        return false;
    }
    return true;
}

bool FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
    const FWetClothingTransparencyLayerData& Layer,
    const FString& SourceSignature,
    const FIntPoint ExpectedResolution,
    const bool bLoadTextures,
    FString& OutError)
{
    OutError.Reset();
    if (SourceSignature.IsEmpty() || ExpectedResolution.X <= 0 || ExpectedResolution.Y <= 0)
    {
        OutError = TEXT("The Stage 2 source artifact identity is incomplete.");
        return false;
    }

    TArray<EDWCTransparencyTempArtifactKind> RequiredKinds;
    GetRequiredSourceArtifacts(Layer.RequiresRevealSurface(), RequiredKinds);
    FGuid CommitGeneration;
    for (const EDWCTransparencyTempArtifactKind Kind : RequiredKinds)
    {
        const FDWCTransparencyStageArtifactSpec* Spec = FindSpec(Kind);
        const FDWCTransparencyTempArtifactReference* Reference = FindReference(Layer, Kind);
        if (Spec == nullptr || Reference == nullptr)
        {
            OutError = FString::Printf(
                TEXT("A required Stage 2 artifact '%s' is missing."),
                Spec != nullptr ? Spec->AssetToken : TEXT("Unknown"));
            return false;
        }
        if (!CommitGeneration.IsValid())
        {
            CommitGeneration = Reference->CommitGeneration;
        }
        const FString ExpectedSignature = BuildExpectedSignature(Kind, SourceSignature);
        if (!ValidateReference(
                *Reference, *Spec, ExpectedSignature, ExpectedResolution,
                &CommitGeneration, bLoadTextures, OutError))
        {
            return false;
        }
    }
    return CommitGeneration.IsValid();
}

bool FDWCTransparencyStageArtifactContract::InspectRevealArtifact(
    const FWetClothingTransparencyLayerData& Layer,
    const FString& SourceSignature,
    const FString& RevealSignature,
    const FIntPoint ExpectedResolution,
    const bool bLoadTexture,
    FString& OutError)
{
    const FDWCTransparencyStageArtifactSpec* Spec =
        FindSpec(EDWCTransparencyTempArtifactKind::CorrectedRevealColor);
    const FDWCTransparencyTempArtifactReference* Reference =
        FindReference(Layer, EDWCTransparencyTempArtifactKind::CorrectedRevealColor);
    if (Spec == nullptr || Reference == nullptr)
    {
        OutError = TEXT("The Stage 3 Corrected Reveal Color artifact is missing.");
        return false;
    }
    const FString ExpectedSignature = BuildExpectedSignature(
        Spec->Kind, SourceSignature, RevealSignature);
    return ValidateReference(
        *Reference, *Spec, ExpectedSignature, ExpectedResolution,
        nullptr, bLoadTexture, OutError);
}
