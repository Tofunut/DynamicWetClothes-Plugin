#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"

class UMaterial;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UMaterialFunctionInterface;
class UWetClothingAsset;

struct FWetClothingUnifiedMaterialSetupResult
{
    bool bSucceeded = false;
    bool bAlreadyConfigured = false;
    UMaterial* GeneratedMaterial = nullptr;
    UMaterialInstanceConstant* CPUMaterialInstance = nullptr;
    UMaterialInstanceConstant* GPUMaterialInstance = nullptr;
    UMaterialFunctionInterface* EvaluateSurfaceAppearanceFunction = nullptr;
    FString Message;
};

class FWCAMaterialGenerator
{
  public:
    struct FOptions
    {
        EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;
        int32 DWCDataUVChannelIndex = INDEX_NONE;
        int32 OriginalUVChannelIndex = 0;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 SurfaceWaterNormalUVChannelIndex = 0;
        float SurfaceWaterTargetRoughness = 0.02f;
        bool bUseSurfaceWater = false;
        bool bEnableDWCDataUVSampling = false;
        bool bConnectWetnessMapPath = false;

        /** Optional owner used to place generated assets in a WCA-specific deterministic folder. */
        const UWetClothingAsset* OwningWetClothingAsset = nullptr;
    };

    static FOptions MakeOptionsForAsset(
        const UWetClothingAsset* WetClothingAsset,
        EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU,
        int32 MaterialSlotIndex = INDEX_NONE);

    /** Creates one shared DWC material graph and two static CPU/GPU MIC permutations. */
    static FWetClothingUnifiedMaterialSetupResult CreateOrUpdateUnifiedMaterialSet(
        UMaterialInterface* SourceMaterial,
        const FOptions& Options = FOptions());

    /** Creates a transient GPU preview permutation from the source material without writing generated assets. */
    static FWetClothingUnifiedMaterialSetupResult CreateTransientUnifiedPreviewMaterial(
        UMaterialInterface* SourceMaterial,
        const FOptions& Options = FOptions());

    static bool IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface);
    static bool IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface, const FOptions& Options);
    /** Fast reference-only validation used by routine editor status refreshes. */
    static void ValidateGeneratedMaterialOverrideReferences(const UWetClothingAsset* WetClothingAsset, TArray<FString>& OutMessages);

    /** Deep graph and static-permutation validation used by explicit validation/generation workflows. */
    static void ValidateGeneratedMaterialOverrides(const UWetClothingAsset* WetClothingAsset, TArray<FString>& OutMessages);

    static bool ValidateSurfaceAppearanceFunctions(FString& OutErrorMessage);

};
