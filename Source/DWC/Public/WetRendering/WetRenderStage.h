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
    const TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>>* SurfaceWaterStatesByMaterialSlot = nullptr;
    const TMap<int32, FSurfaceWaterProfileParameters>* SurfaceWaterProfilesByMaterialSlot = nullptr;

    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;

    FLinearColor UnassignedWetPartDebugColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
    bool         bEnableWetPartDebugVertexColors = false;
    bool         bWetPartDebugUseWetnessMask = true;

    FName WetPartDebugStrengthParameterName = TEXT("DWC_WetPartDebugStrength");
    FName WetPartDebugUseWetnessMaskParameterName = TEXT("DWC_WetPartDebugUseWetnessMask");
    FName WetnessProfileMap0ParameterName = DWCWetMaterialParameters::WetnessProfileMap0();
    FName UseWetnessProfileMap0ParameterName = DWCWetMaterialParameters::UseWetnessProfileMap0();
    FName WrinkleNormalMapParameterName = DWCWetMaterialParameters::WrinkleNormalMap();
    FName UseWrinkleNormalMapParameterName = DWCWetMaterialParameters::UseWrinkleNormalMap();
    FName WrinkleStrengthParameterName = DWCWetMaterialParameters::WrinkleStrength();
    FName WrinkleWetnessMinParameterName = DWCWetMaterialParameters::WrinkleWetnessMin();
    FName WrinkleWetnessMaxParameterName = DWCWetMaterialParameters::WrinkleWetnessMax();
    FName TransparencyMapParameterName = DWCWetMaterialParameters::TransparencyMap();
    FName UseTransparencyMapParameterName = DWCWetMaterialParameters::UseTransparencyMap();
    FName TransparencyStrengthParameterName = DWCWetMaterialParameters::TransparencyStrength();
    FName TransparencyWetnessMinParameterName = DWCWetMaterialParameters::TransparencyWetnessMin();
    FName TransparencyWetnessMaxParameterName = DWCWetMaterialParameters::TransparencyWetnessMax();
    FName TransparencyUVChannelParameterName = DWCWetMaterialParameters::TransparencyUVChannel();
    FName WrinkleSuppressionStrengthParameterName = DWCWetMaterialParameters::WrinkleSuppressionStrength();

    float WrinkleStrength = DWCWetMaterialParameters::DefaultWrinkleStrength();
    float WrinkleWetnessMin = DWCWetMaterialParameters::DefaultWrinkleWetnessMin();
    float WrinkleWetnessMax = DWCWetMaterialParameters::DefaultWrinkleWetnessMax();
    float TransparencyWetnessMin = DWCWetMaterialParameters::DefaultTransparencyWetnessMin();
    float TransparencyWetnessMax = DWCWetMaterialParameters::DefaultTransparencyWetnessMax();

    FName UnderColorParameterName = TEXT("DWC_UnderColor");
    FName UnderColorBlendStrengthParameterName = TEXT("DWC_UnderColorBlendStrength");
    FName SurfaceWaterRTParameterName = TEXT("DWC_SurfaceWaterRT");
    FName SurfaceDropletRTParameterName = TEXT("DWC_SurfaceDropletRT");
    FName SurfaceFlowRTParameterName = TEXT("DWC_SurfaceFlowRT");
    FName SurfaceWaterTimeParameterName = TEXT("DWC_SurfaceWaterTime");
    FName SurfaceWaterTexelSizeParameterName = TEXT("DWC_SurfaceWaterTexelSize");
    FName SurfaceWaterNormalStrengthParameterName = TEXT("DWC_SurfaceWaterNormalStrength");
    FName SurfaceWaterRoughnessParameterName = TEXT("DWC_SurfaceWaterRoughness");
    FName SurfaceDropletTilingParameterName = TEXT("DWC_SurfaceDropletTiling");
    FName SurfaceAmountThresholdMinParameterName = TEXT("DWC_SurfaceAmountThresholdMin");
    FName SurfaceAmountThresholdMaxParameterName = TEXT("DWC_SurfaceAmountThresholdMax");
    FName SurfaceDropletMaskMinParameterName = TEXT("DWC_SurfaceDropletMaskMin");
    FName SurfaceDropletMaskMaxParameterName = TEXT("DWC_SurfaceDropletMaskMax");
    FName SurfaceDropletMaskTextureParameterName = TEXT("DWC_SurfaceDropletMaskTexture");
    FName SurfaceDropletNormalTextureParameterName = TEXT("DWC_SurfaceDropletNormalTexture");
    FName SurfaceFlowTilingParameterName = TEXT("DWC_SurfaceFlowTiling");
    FName SurfaceFlowPanningXParameterName = TEXT("DWC_SurfaceFlowPanningX");
    FName SurfaceFlowPanningYParameterName = TEXT("DWC_SurfaceFlowPanningY");
    FName SurfaceFlowNormalStrengthParameterName = TEXT("DWC_SurfaceFlowNormalStrength");
    FName SurfaceFlowRoughnessParameterName = TEXT("DWC_SurfaceFlowRoughness");
    FName SurfaceFlowMaskMinParameterName = TEXT("DWC_SurfaceFlowMaskMin");
    FName SurfaceFlowMaskMaxParameterName = TEXT("DWC_SurfaceFlowMaskMax");
    FName SurfaceFlowMaskTextureParameterName = TEXT("DWC_SurfaceFlowMaskTexture");
    FName SurfaceFlowNormalTextureParameterName = TEXT("DWC_SurfaceFlowNormalTexture");
    FName SurfaceWaterDebugModeParameterName = TEXT("DWC_SurfaceWaterDebugMode");
    float SurfaceWaterTimeSeconds = 0.0f;
    ESurfaceWaterDebugView SurfaceWaterDebugView = ESurfaceWaterDebugView::None;

    FLinearColor UnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);
    float        UnderColorBlendStrength = 0.3f;
    bool         bLogWrinkleRuntimeBindings = false;
    bool         bLogTransparencyRuntimeBindings = false;

    int32 LODIndex = 0;
};

class DWC_API FWetRenderStage
{
  public:
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
};
