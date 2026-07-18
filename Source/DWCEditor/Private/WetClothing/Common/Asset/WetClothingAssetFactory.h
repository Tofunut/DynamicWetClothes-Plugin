#pragma once

#include "Factories/Factory.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetClothingAssetFactory.generated.h"

class USkeletalMesh;
struct FPropertyChangedEvent;

/** Transient object rendered by the creation dialog. */
UCLASS(Transient)
class DWCEDITOR_API UWetClothingAssetCreationSettings : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Source Skeletal Mesh"))
    TObjectPtr<USkeletalMesh> SourceSkeletalMesh = nullptr;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Modify Source Mesh"))
    bool bModifySourceMeshForDWCDataUV = false;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Preferred DWC Data UV Channel", ClampMin = "0", ClampMax = "7"))
    int32 PreferredDWCDataUVChannelIndex = 1;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "CPU Vertex Simulation Data"))
    bool bBuildCPUVertexSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "GPU Wetness Map Simulation Data"))
    bool bBuildGPUWetnessMapSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "GPU Simulation"))
    EDWCMapResolution GPUSimulationMapResolution = EDWCMapResolution::Resolution512;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Wrinkle"))
    EDWCMapResolution WrinkleMapResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Transparency"))
    EDWCMapResolution TransparencyMapResolution = EDWCMapResolution::Resolution1024;

    FDWCWetClothingAssetSetupSettings BuildSettings() const
    {
        FDWCWetClothingAssetSetupSettings Result;
        Result.bBuildCPUVertexSimulationData = bBuildCPUVertexSimulationData;
        Result.bBuildGPUWetnessMapSimulationData = bBuildGPUWetnessMapSimulationData;
        Result.bModifySourceMeshForDWCDataUV = bModifySourceMeshForDWCDataUV;
        Result.PreferredDWCDataUVChannelIndex = PreferredDWCDataUVChannelIndex;
        Result.GPUSimulationMapResolution = DWCMapResolution::ToInt(GPUSimulationMapResolution);
        Result.WrinkleMapResolution = DWCMapResolution::ToInt(WrinkleMapResolution);
        Result.TransparencyMapResolution = DWCMapResolution::ToInt(TransparencyMapResolution);
        return Result;
    }
};

UCLASS(Transient)
class DWCEDITOR_API UWetClothingAssetSetupSettingsObject : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Modify Source Mesh"))
    bool bModifySourceMeshForDWCDataUV = false;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Preferred DWC Data UV Channel", ClampMin = "0", ClampMax = "7"))
    int32 PreferredDWCDataUVChannelIndex = 1;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "CPU Vertex Simulation Data"))
    bool bBuildCPUVertexSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "GPU Wetness Map Simulation Data"))
    bool bBuildGPUWetnessMapSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "GPU Simulation"))
    EDWCMapResolution GPUSimulationMapResolution = EDWCMapResolution::Resolution512;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Wrinkle"))
    EDWCMapResolution WrinkleMapResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Transparency"))
    EDWCMapResolution TransparencyMapResolution = EDWCMapResolution::Resolution1024;

    void InitializeFromSettings(const FDWCWetClothingAssetSetupSettings& InSettings)
    {
        bBuildCPUVertexSimulationData = InSettings.bBuildCPUVertexSimulationData;
        bBuildGPUWetnessMapSimulationData = InSettings.bBuildGPUWetnessMapSimulationData;
        bModifySourceMeshForDWCDataUV = InSettings.bModifySourceMeshForDWCDataUV;
        PreferredDWCDataUVChannelIndex = InSettings.PreferredDWCDataUVChannelIndex;
        GPUSimulationMapResolution = DWCMapResolution::FromInt(InSettings.GetGPUSimulationMapResolution());
        WrinkleMapResolution = DWCMapResolution::FromInt(InSettings.GetWrinkleMapResolution());
        TransparencyMapResolution = DWCMapResolution::FromInt(InSettings.GetTransparencyMapResolution());
    }

    FDWCWetClothingAssetSetupSettings BuildSettings() const
    {
        FDWCWetClothingAssetSetupSettings Result;
        Result.bBuildCPUVertexSimulationData = bBuildCPUVertexSimulationData;
        Result.bBuildGPUWetnessMapSimulationData = bBuildGPUWetnessMapSimulationData;
        Result.bModifySourceMeshForDWCDataUV = bModifySourceMeshForDWCDataUV;
        Result.PreferredDWCDataUVChannelIndex = PreferredDWCDataUVChannelIndex;
        Result.GPUSimulationMapResolution = DWCMapResolution::ToInt(GPUSimulationMapResolution);
        Result.WrinkleMapResolution = DWCMapResolution::ToInt(WrinkleMapResolution);
        Result.TransparencyMapResolution = DWCMapResolution::ToInt(TransparencyMapResolution);
        return Result;
    }
};

UCLASS()
class DWCEDITOR_API UWetClothingAssetFactory : public UFactory
{
    GENERATED_BODY()

public:
    UWetClothingAssetFactory();

    virtual bool ConfigureProperties() override;
    virtual UObject* FactoryCreateNew(
        UClass* Class,
        UObject* InParent,
        FName Name,
        EObjectFlags Flags,
        UObject* Context,
        FFeedbackContext* Warn) override;
    virtual bool ShouldShowInNewMenu() const override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UWetClothingAssetCreationSettings> PendingCreationSettings = nullptr;
};
