#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UWetClothingAsset;
class FWetClothingRuntimeData;
class FAbsorbedWetnessSimulationState;
struct FWetClothingSettings;

/*
WetRenderStage §ÌñâÑÏöî∏Ïûê Î¨∂Ïùå¥Îã§.

ÑÏû¨Î©Ä∞Ïä§àÎìú ëÏóÖ îÏ≤≠ÑÎãà DynamicWetClothesComponentÍ∞Ä Game ThreadêÏÑú
åÎçî úÌòÑÍ∞±Ïã†ÑÏöîÏ∞∏Ï°∞Î•Î™®Ï ¥Î∏Ïûê
ï†:
- Wetness Í∞íÏùÑ MaterialInstance parameterÎ∞òÏòÅ
- WetnessProfileMap parameter §Ï†ï
- VertexColor Í∏∞Î∞ò debug / 1Ï∞wetness úÌòÑ Í∞±Ïã†
*/
struct DWC_API FWetRenderStageArgs
{
    USkeletalMeshComponent*     TargetSkeletalMesh = nullptr;
    const UWetClothingAsset*    WetClothingAsset = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;

    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;

    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;
    TArray<FLinearColor>*                         CachedWetVertexColors = nullptr;

    FLinearColor UnassignedWetPartDebugColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);
    bool         bEnableWetPartDebugVertexColors = false;
    bool         bWetPartDebugUseWetnessMask = true;

    FName WetPartDebugStrengthParameterName = TEXT("DWC_WetPartDebugStrength");
    FName WetPartDebugUseWetnessMaskParameterName = TEXT("DWC_WetPartDebugUseWetnessMask");
    FName WetnessProfileMap0ParameterName = TEXT("DWC_WetnessProfileMap0");
    FName UseWetnessProfileMap0ParameterName = TEXT("DWC_UseWetnessProfileMap0");
    FName WrinkleNormalMapParameterName = TEXT("DWC_WrinkleNormalMap");
    FName UseWrinkleNormalMapParameterName = TEXT("DWC_UseWrinkleNormalMap");
    FName WrinkleStrengthParameterName = TEXT("DWC_WrinkleStrength");
    FName WrinkleWetnessMinParameterName = TEXT("DWC_WrinkleWetnessMin");
    FName WrinkleWetnessMaxParameterName = TEXT("DWC_WrinkleWetnessMax");

    float WrinkleStrength = 1.0f;
    float WrinkleWetnessMin = 0.25f;
    float WrinkleWetnessMax = 1.0f;

    FName UnderColorParameterName = TEXT("DWC_UnderColor");
    FName UnderColorBlendStrengthParameterName = TEXT("DWC_UnderColorBlendStrength");

    FLinearColor UnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);
    float        UnderColorBlendStrength = 0.3f;

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
    void         ApplyWetnessToMaterial(FWetRenderStageArgs& Args);
    FLinearColor MakeWetVertexColor(const FWetRenderStageArgs& Args, int32 VertexIndex, float Wetness) const;

    TArray<FLinearColor> CachedWetVertexColors;
};
