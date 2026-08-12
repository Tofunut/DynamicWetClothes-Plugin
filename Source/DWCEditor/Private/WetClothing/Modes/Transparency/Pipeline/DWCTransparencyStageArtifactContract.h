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

/** Selects only the Stage 2 artifacts required by a downstream consumer. */
struct FDWCTransparencySourceArtifactSelection
{
    bool bRequireRevealSurface = false;
    bool bRequireOuterIslandID = true;
    bool bRequireHitSource = false;
    bool bRequireHitDistance = false;

    static FDWCTransparencySourceArtifactSelection Canonical(
        bool bRequiresRevealSurface);
    static FDWCTransparencySourceArtifactSelection Stage4(
        bool bRequiresRevealSurface,
        bool bRequiresOuterIslandID);
    static FDWCTransparencySourceArtifactSelection Diagnostics(
        bool bRequiresRevealSurface);
};

struct FDWCTransparencyStageArtifactSpec
{
    EDWCTransparencyTempArtifactKind Kind = EDWCTransparencyTempArtifactKind::BaseRevealColor;
    EDWCTransparencyStage Stage = EDWCTransparencyStage::Source;
    EDWCTransparencyArtifactDependency Dependency = EDWCTransparencyArtifactDependency::Source;
    const TCHAR* AssetToken = TEXT("Unknown");
    ETextureSourceFormat SourceFormat = TSF_Invalid;
    bool bSRGB = false;
};

/** Canonical schema, identity, and validation rules for editor-only stage artifacts. */
class FDWCTransparencyStageArtifactContract
{
  public:
    // v3 stores fractional target-surface coverage instead of a binary 0/1 mask.
    static constexpr int32 ContractVersion = 3;

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

    static void GetRequiredSourceArtifacts(
        const FDWCTransparencySourceArtifactSelection& Selection,
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

    static bool InspectSourceArtifactSet(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& SourceSignature,
        FIntPoint ExpectedResolution,
        const FDWCTransparencySourceArtifactSelection& Selection,
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
