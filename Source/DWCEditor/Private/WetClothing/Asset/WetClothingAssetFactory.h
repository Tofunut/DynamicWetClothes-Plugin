//Copyright 2026 Team Tofunut. All Rights Reserved.
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

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Original UV Channel", ClampMin = "0", ClampMax = "7"))
    int32 OriginalUVChannelIndex = 0;

    /** INDEX_NONE is displayed as Same as Original in the custom creation UI. */
    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Surface Normal UV Channel"))
    int32 SurfaceWaterNormalUVChannelIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Preferred DWC UV Channel", ClampMin = "0", ClampMax = "3"))
    int32 PreferredDWCDataUVChannelIndex = 1;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "First Mapped LOD", ClampMin = "0"))
    int32 FirstGeneratedLODIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "Last Mapped LOD", ClampMin = "0"))
    int32 LastGeneratedLODIndex = MAX_int32;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "CPU Vertex Simulation Data"))
    bool bBuildCPUVertexSimulationData = false;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "GPU Wetness Map Simulation Data"))
    bool bBuildGPUWetnessMapSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "GPU Simulation"))
    EDWCMapResolution GPUSimulationMapResolution = EDWCMapResolution::Resolution512;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Surface Water"))
    EDWCMapResolution SurfaceWaterRTResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Wrinkle"))
    EDWCMapResolution WrinkleMapResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Transparency"))
    EDWCMapResolution TransparencyMapResolution = EDWCMapResolution::Resolution1024;

    FDWCWetClothingAssetSetupSettings BuildSettings() const
    {
        FDWCWetClothingAssetSetupSettings Result;
        Result.bBuildCPUVertexSimulationData = bBuildCPUVertexSimulationData;
        Result.bBuildGPUWetnessMapSimulationData = bBuildGPUWetnessMapSimulationData;
        Result.OriginalUVChannelIndex = OriginalUVChannelIndex;
        Result.SurfaceWaterNormalUVChannelIndex = SurfaceWaterNormalUVChannelIndex;
        Result.PreferredDWCDataUVChannelIndex = PreferredDWCDataUVChannelIndex;
        Result.FirstGeneratedLODIndex = FirstGeneratedLODIndex;
        Result.LastGeneratedLODIndex = LastGeneratedLODIndex;
        Result.GPUSimulationMapResolution = DWCMapResolution::ToInt(GPUSimulationMapResolution);
        Result.SurfaceWaterRTResolution = DWCMapResolution::ToInt(SurfaceWaterRTResolution);
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
    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Original UV Channel", ClampMin = "0", ClampMax = "7"))
    int32 OriginalUVChannelIndex = 0;

    /** INDEX_NONE is displayed as Same as Original in the custom setup UI. */
    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Surface Normal UV Channel"))
    int32 SurfaceWaterNormalUVChannelIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (DisplayName = "Preferred DWC UV Channel", ClampMin = "0", ClampMax = "3"))
    int32 PreferredDWCDataUVChannelIndex = 1;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "First Mapped LOD", ClampMin = "0"))
    int32 FirstGeneratedLODIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "Last Mapped LOD", ClampMin = "0"))
    int32 LastGeneratedLODIndex = MAX_int32;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "CPU Vertex Simulation Data"))
    bool bBuildCPUVertexSimulationData = false;

    UPROPERTY(EditAnywhere, Category = "Simulation Data", meta = (DisplayName = "GPU Wetness Map Simulation Data"))
    bool bBuildGPUWetnessMapSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "GPU Simulation"))
    EDWCMapResolution GPUSimulationMapResolution = EDWCMapResolution::Resolution512;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Surface Water"))
    EDWCMapResolution SurfaceWaterRTResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Wrinkle"))
    EDWCMapResolution WrinkleMapResolution = EDWCMapResolution::Resolution1024;

    UPROPERTY(EditAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Transparency"))
    EDWCMapResolution TransparencyMapResolution = EDWCMapResolution::Resolution1024;

    void InitializeFromSettings(const FDWCWetClothingAssetSetupSettings& InSettings)
    {
        bBuildCPUVertexSimulationData = InSettings.bBuildCPUVertexSimulationData;
        bBuildGPUWetnessMapSimulationData = InSettings.bBuildGPUWetnessMapSimulationData;
        OriginalUVChannelIndex = InSettings.OriginalUVChannelIndex;
        SurfaceWaterNormalUVChannelIndex = InSettings.SurfaceWaterNormalUVChannelIndex;
        PreferredDWCDataUVChannelIndex = InSettings.PreferredDWCDataUVChannelIndex;
        FirstGeneratedLODIndex = InSettings.FirstGeneratedLODIndex;
        LastGeneratedLODIndex = InSettings.LastGeneratedLODIndex;
        GPUSimulationMapResolution = DWCMapResolution::FromInt(InSettings.GetGPUSimulationMapResolution());
        SurfaceWaterRTResolution = DWCMapResolution::FromInt(InSettings.GetSurfaceWaterRTResolution());
        WrinkleMapResolution = DWCMapResolution::FromInt(InSettings.GetWrinkleMapResolution());
        TransparencyMapResolution = DWCMapResolution::FromInt(InSettings.GetTransparencyMapResolution());
    }

    FDWCWetClothingAssetSetupSettings BuildSettings() const
    {
        FDWCWetClothingAssetSetupSettings Result;
        Result.bBuildCPUVertexSimulationData = bBuildCPUVertexSimulationData;
        Result.bBuildGPUWetnessMapSimulationData = bBuildGPUWetnessMapSimulationData;
        Result.OriginalUVChannelIndex = OriginalUVChannelIndex;
        Result.SurfaceWaterNormalUVChannelIndex = SurfaceWaterNormalUVChannelIndex;
        Result.PreferredDWCDataUVChannelIndex = PreferredDWCDataUVChannelIndex;
        Result.FirstGeneratedLODIndex = FirstGeneratedLODIndex;
        Result.LastGeneratedLODIndex = LastGeneratedLODIndex;
        Result.GPUSimulationMapResolution = DWCMapResolution::ToInt(GPUSimulationMapResolution);
        Result.SurfaceWaterRTResolution = DWCMapResolution::ToInt(SurfaceWaterRTResolution);
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

    bool bConfirmedOverwriteExistingDataUVChannel = false;
};
