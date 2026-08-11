//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

class UMaterialInterface;
class USkeletalMesh;
class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;
enum class EDWCTransparencyTempArtifactKind : uint8;

struct FDWCTransparencyMaterialSurfaceBakeIdentity
{
    static constexpr int32 Version = 3;

    FString SourceMeshContentSignature;
    FString EffectiveMaterialSignature;
    FString PlacementSignature;
    FString Digest;

    bool IsValid() const
    {
        return !SourceMeshContentSignature.IsEmpty() &&
            !EffectiveMaterialSignature.IsEmpty() &&
            !PlacementSignature.IsEmpty() && !Digest.IsEmpty();
    }
};

struct FDWCTransparencyFinalSignatureInputs
{
    FString SourceSignature;
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
    /** Bump only when the runtime RG encoding contract changes. */
    static constexpr int32 RevealNormalEncodingVersion = 1;

    /** Bump when the surface frame used to produce Reveal Normal changes. */
    static constexpr int32 RevealSurfaceBasisVersion = 2;

    static FDWCTransparencyMaterialSurfaceBakeIdentity BuildMaterialSurfaceBakeIdentity(
        const USkeletalMesh* SourceMesh,
        const UMaterialInterface* EffectiveMaterial,
        const FTransform& BakeTransform,
        int32 MaterialSlotIndex,
        int32 SourceUVChannel,
        int32 Resolution);

    static FString BuildStageArtifactSignature(
        EDWCTransparencyTempArtifactKind Kind,
        int32 ContractVersion,
        const FString& DependencySignature);

    static bool BuildSourceSignature(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FString& OutSignature,
        FString& OutMaterialBakeSignature,
        FString& OutError);

    static FString BuildRevealSignature(
        const FString& SourceSignature,
        const FWetClothingTransparencyLayerData& Layer,
        float RevealMetallicDarkeningStrength);

    /** Identity for the editor-only packed surface produced by canonical Stage 2 data. */
    static FString BuildRevealSurfaceAuthoringSignature(const FString& SourceSignature);

    /** Identity for the coverage-weighted runtime Reveal Normal RG payload. */
    static FString BuildRevealNormalSignature(const FString& SourceSignature);

    static FString BuildAlphaAuthoringSignature(
        const FWetClothingTransparencyLayerData& Layer);

    static FString BuildSuppressionSettingsSignature(
        float CoverageThreshold,
        float MaskSoftness,
        float SuppressionStrength,
        float TransparencyStrength);

    /** Stage 4 alpha-domain identity. Deliberately excludes corrected reveal RGB. */
    static FString BuildFinalAlphaSignature(
        const FDWCTransparencyFinalSignatureInputs& Inputs);

    static FString BuildFinalSignature(
        const FDWCTransparencyFinalSignatureInputs& Inputs);

    static FString BuildFinalSignature(
        const FString& RevealSignature,
        const FWetClothingTransparencyLayerData& Layer,
        const FString& WrinkleMaskBuildSignature,
        const FString& SuppressionSettingsSignature,
        int32 PaddingPixels,
        float EdgeFeatherPixels,
        const FString& SourceSignature = FString());

    static FDWCTransparencyStageStatus EvaluateEditorStageCache(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& ExpectedSourceSignature,
        const FString& ExpectedRevealSignature);
};
