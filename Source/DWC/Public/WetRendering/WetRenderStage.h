// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "WetRendering/WetMaterialParameters.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UWetClothingAsset;
class FWetClothingRuntimeData;
class FAbsorbedWetnessSimulationState;
struct FWetClothingSettings;

/*
Arguments required to execute WetRenderStage.

DynamicWetClothesComponent currently invokes this stage on the game thread with the references required to
update visual wetness state, including material parameters, render-profile lookup data, and vertex-color-based
debug or CPU wetness rendering.
*/
struct DWC_API FWetRenderStageArgs
{
    USkeletalMeshComponent*     TargetSkeletalMesh = nullptr;
    const UWetClothingAsset*    WetClothingAsset = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;

    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;
    bool                             bShowWetPartDebugColors = false;
    bool                             bShowSurfaceWaterDebugColors = false;
    bool                             bDroplet1RenderingEnabled = true;
    bool                             bDroplet2RenderingEnabled = true;
    bool                             bGPUWetnessMode = false;

    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;

    float WrinkleStrength = DWCWetMaterialParameters::DefaultWrinkleStrength();
    float WrinkleWetnessMin = DWCWetMaterialParameters::DefaultWrinkleWetnessMin();
    float WrinkleWetnessMax = DWCWetMaterialParameters::DefaultWrinkleWetnessMax();
    bool  bEnableWrinkle = true;
    float TransparencyWetnessMin = DWCWetMaterialParameters::DefaultTransparencyWetnessMin();
    float TransparencyWetnessMax = DWCWetMaterialParameters::DefaultTransparencyWetnessMax();
    bool  bEnableTransparency = true;

    FLinearColor UnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);
    float        UnderColorBlendStrength = 0.3f;
    int32        LODIndex = 0;
};

class DWC_API FWetRenderStage
{
  public:
    uint64 GetAllocatedMemoryBytes() const;
    void   ResetCachedVertexColors();
    void   InitializeCachedVertexColors(int32 VertexCount);

    void         InitializeWetMaterialInstance(FWetRenderStageArgs& Args);
    void         ApplyWetMaterialParameters(FWetRenderStageArgs& Args);
    void         ApplyWetWrinkleNormalMapParameters(FWetRenderStageArgs& Args);
    void         ApplyWetTransparencyMapParameters(FWetRenderStageArgs& Args);
    void         ApplyWetnessToMaterial(FWetRenderStageArgs& Args);
    FLinearColor MakeWetVertexColor(const FWetRenderStageArgs& Args, int32 VertexIndex, float Wetness) const;

    // Per-receiver dynamic material instances. RenderStage owns the handles
    // because all material parameter updates are performed through this stage.
    TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;

    TArray<FColor>                    CachedWetVertexColors;        // VertexColor
    TMap<int32, FLinearColor>         CachedWetPartDebugColorsByID; // ID is at FWetClothingRuntimeData.
    TWeakObjectPtr<UWetClothingAsset> CachedWetPartDebugColorAsset; // What Asset is Debug Color based on
};
