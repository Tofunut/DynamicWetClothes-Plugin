//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

class UMaterialInterface;
class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyFinalSignatureInputs
{
    FString RevealSignature;
    FString AlphaAuthoringSignature;
    FString WrinkleMaskBuildSignature;
    FString SuppressionSettingsSignature;
    int32 PaddingPixels = 0;
    float EdgeFeatherPixels = 0.0f;
};

/** Single owner for deterministic transparency pipeline build identities. */
class FDWCTransparencySignatureService
{
  public:
    static FString BuildMaterialBakeSignature(
        const UMaterialInterface* Material,
        int32 SourceUVChannel,
        int32 Resolution);

    static bool BuildSourceSignature(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FString& OutSignature,
        FString& OutMaterialBakeSignature,
        FString& OutError);

    static FString BuildRevealSignature(
        const FString& SourceSignature,
        const FWetClothingTransparencyLayerData& Layer);

    /** Identity for the final linear Reveal Surface payload produced from canonical Stage 2 data. */
    static FString BuildRevealSurfaceSignature(const FString& SourceSignature);

    static FString BuildAlphaAuthoringSignature(
        const FWetClothingTransparencyLayerData& Layer);

    static FString BuildSuppressionSettingsSignature(
        float CoverageThreshold,
        float MaskSoftness,
        float SuppressionStrength,
        float TransparencyStrength);

    static FString BuildFinalSignature(
        const FDWCTransparencyFinalSignatureInputs& Inputs);

    static FString BuildFinalSignature(
        const FString& RevealSignature,
        const FWetClothingTransparencyLayerData& Layer,
        const FString& WrinkleMaskBuildSignature,
        const FString& SuppressionSettingsSignature,
        int32 PaddingPixels,
        float EdgeFeatherPixels);

    static FDWCTransparencyStageStatus EvaluateEditorStageCache(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& ExpectedSourceSignature,
        const FString& ExpectedRevealSignature);
};
