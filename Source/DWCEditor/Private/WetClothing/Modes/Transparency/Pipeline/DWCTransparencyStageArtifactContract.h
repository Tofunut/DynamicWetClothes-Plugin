//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/TextureDefines.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

class UTexture2D;

enum class EDWCTransparencyArtifactDependency : uint8
{
    Source,
    RevealSurface,
    Reveal
};

struct FDWCTransparencyStageArtifactSpec
{
    EDWCTransparencyTempArtifactKind Kind = EDWCTransparencyTempArtifactKind::BaseRevealColor;
    EDWCTransparencyStage Stage = EDWCTransparencyStage::Source;
    EDWCTransparencyArtifactDependency Dependency = EDWCTransparencyArtifactDependency::Source;
    const TCHAR* AssetToken = TEXT("Unknown");
    ETextureSourceFormat SourceFormat = TSF_Invalid;
    bool bSRGB = false;
    bool bRequiredForAllSources = false;
    bool bRequiredForProjectedSources = false;
};

/** Canonical schema, identity, and validation rules for editor-only stage artifacts. */
class FDWCTransparencyStageArtifactContract
{
  public:
    static constexpr int32 ContractVersion = 1;

    static const FDWCTransparencyStageArtifactSpec* FindSpec(
        EDWCTransparencyTempArtifactKind Kind);

    static FString GetAssetToken(EDWCTransparencyTempArtifactKind Kind);

    static FString BuildExpectedSignature(
        EDWCTransparencyTempArtifactKind Kind,
        const FString& SourceSignature,
        const FString& RevealSignature = FString());

    static void GetRequiredSourceArtifacts(
        bool bRequiresRevealSurface,
        TArray<EDWCTransparencyTempArtifactKind>& OutKinds);

    static const FDWCTransparencyTempArtifactReference* FindReference(
        const FWetClothingTransparencyLayerData& Layer,
        EDWCTransparencyTempArtifactKind Kind);

    static bool ValidateReference(
        const FDWCTransparencyTempArtifactReference& Reference,
        const FDWCTransparencyStageArtifactSpec& Spec,
        const FString& ExpectedSignature,
        FIntPoint ExpectedResolution,
        const FGuid* ExpectedGeneration,
        bool bLoadTexture,
        FString& OutError);

    static bool InspectSourceArtifactSet(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& SourceSignature,
        FIntPoint ExpectedResolution,
        bool bLoadTextures,
        FString& OutError);

    static bool InspectRevealArtifact(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& SourceSignature,
        const FString& RevealSignature,
        FIntPoint ExpectedResolution,
        bool bLoadTexture,
        FString& OutError);
};
