#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetnessProfile.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationSettings.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UWetClothingAsset;
class FWetClothingRuntimeData;
class FAbsorbedWetnessSimulationState;
class FSurfaceWaterSimulationState;
struct FWetClothingSettings;

/*
WetRenderStage �행�요�자 묶음�다.

�재멀�스�드 �업 �청�니 DynamicWetClothesComponent가 Game Thread�서
�더 �현갱신�요참조�모� ��자
��:
- Wetness 값을 MaterialInstance parameter반영
- WetnessProfileMap parameter �정
- VertexColor 기반 debug / 1�wetness �현 갱신
*/
struct DWC_API FWetRenderStageArgs
{
    USkeletalMeshComponent*     TargetSkeletalMesh = nullptr;
    const UWetClothingAsset*    WetClothingAsset = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;

    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;
    bool bShowWetPartDebugColors = false;
    bool bGPUWetnessMode = false;
    const TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>>* SurfaceWaterStatesByMaterialSlot = nullptr;
    const TMap<int32, FSurfaceWaterProfileParameters>* SurfaceWaterProfilesByMaterialSlot = nullptr;

    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;

    float WrinkleStrength = DWCWetMaterialParameters::DefaultWrinkleStrength();
    float WrinkleWetnessMin = DWCWetMaterialParameters::DefaultWrinkleWetnessMin();
    float WrinkleWetnessMax = DWCWetMaterialParameters::DefaultWrinkleWetnessMax();
    bool bEnableWrinkle = true;
    float TransparencyWetnessMin = DWCWetMaterialParameters::DefaultTransparencyWetnessMin();
    float TransparencyWetnessMax = DWCWetMaterialParameters::DefaultTransparencyWetnessMax();
    bool bEnableTransparency = true;

    float SurfaceWaterTimeSeconds = 0.0f;

    FLinearColor UnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);
    float        UnderColorBlendStrength = 0.3f;
    int32 LODIndex = 0;

};

class DWC_API FWetRenderStage
{
  public:
    uint64 GetAllocatedMemoryBytes() const;
    void ResetCachedVertexColors();
    void InitializeCachedVertexColors(int32 VertexCount);

    void         InitializeWetMaterialInstance(FWetRenderStageArgs& Args);
    void         ApplyWetMaterialParameters(FWetRenderStageArgs& Args);
    void         ApplyWetnessProfileMapParameters(FWetRenderStageArgs& Args);
    void         ApplyWetWrinkleNormalMapParameters(FWetRenderStageArgs& Args);
    void         ApplyWetTransparencyMapParameters(FWetRenderStageArgs& Args);
    void         ApplyWetnessToMaterial(FWetRenderStageArgs& Args);
    FLinearColor MakeWetVertexColor(const FWetRenderStageArgs& Args, int32 VertexIndex, float Wetness) const;

    TArray<FColor> CachedWetVertexColors;
    TMap<int32, FLinearColor> CachedWetPartDebugColorsByID;
    TWeakObjectPtr<UWetClothingAsset> CachedWetPartDebugColorAsset;
};
