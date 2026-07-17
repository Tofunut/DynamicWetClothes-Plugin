#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"

class UMaterialInterface;
class UWetClothingAsset;

struct FWetClothingMaterialSetupResult
{
    bool                bSucceeded = false;
    bool                bAlreadyConfigured = false;
    UMaterialInterface* ConfiguredMaterial = nullptr;
    FString             Message;
};

class FWetClothingMaterialSetup
{
  public:
    struct FOptions
    {
        EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;
        int32 DWCDataUVChannelIndex = INDEX_NONE;
        bool bEnableDWCDataUVSampling = false;
        bool bConnectWetnessMapPath = false;
    };

    static FOptions MakeOptionsForAsset(
        const UWetClothingAsset* WetClothingAsset,
        EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU);

    static FWetClothingMaterialSetupResult DuplicateAndApplyToMaterialInterface(
        UMaterialInterface* MaterialInterface,
        const FOptions& Options = FOptions());
    static FWetClothingMaterialSetupResult DuplicateAndApplyToMaterialInterface(
        UMaterialInterface* MaterialInterface,
        int32 WrinkleUVChannelIndex,
        int32 FallbackDWCDataUVChannelIndex = 1);
    static bool IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface);
    static bool IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface, const FOptions& Options);
    static void ValidateGeneratedMaterialOverrides(const UWetClothingAsset* WetClothingAsset, TArray<FString>& OutMessages);

    // Routine material setup treats the shared functions as fixed, read-only assets.
    static bool ValidateSharedApplyWetnessFunction(FString& OutErrorMessage);

    // CPU/GPU split functions are plugin assets in this branch, so repair currently validates availability.
    static bool RepairOrUpgradeSharedApplyWetnessFunction(FString& OutErrorMessage);
};
