#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UWetClothingAsset;
class FWetClothingRuntimeData;
class FAbsorbedWetnessSimulationState;
struct FWetClothingSettings;

/*
WetRenderStage 실행에 필요한 인자 묶음이다.

현재는 멀티스레드 작업 요청이 아니라, DynamicWetClothesComponent가 Game Thread에서
렌더 표현을 갱신할 때 필요한 참조를 모은 내부용 인자다.
역할:
- Wetness 값을 MaterialInstance parameter에 반영
- WetnessProfileMap parameter 설정
- VertexColor 기반 debug / 1차 wetness 표현 갱신
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
    void         ApplyWetnessToMaterial(FWetRenderStageArgs& Args);
    FLinearColor MakeWetVertexColor(const FWetRenderStageArgs& Args, int32 VertexIndex, float Wetness) const;

    TArray<FLinearColor> CachedWetVertexColors;
};
