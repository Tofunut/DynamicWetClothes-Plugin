#pragma once

#include "CoreMinimal.h"
#include "Core/WetClothingSettings.h"
#include "WetInputSystem/WetContactTypes.h"
#include "Templates/Function.h"

class USkeletalMeshComponent;
class FWetClothingRuntimeData;
class FWetRuntimeDataBuilder;
class FWetClothingMeshSampler;
class FWetSimulationStage;
class FAbsorbedWetnessSimulationState;
class FSurfaceWaterSimulationState;
struct FSurfaceWaterSimulationSettings;

/*
WetInputStage 실행에 필요한 인자 묶음이다.

현재는 멀티스레드 작업 요청이 아니라, DynamicWetClothesComponent가 동기식으로
InputStage를 호출할 때 필요한 객체 참조를 모은 내부용 인자다.
역할:
- 외부 wet contact / area / water surface 입력 처리
- BoneOptimizationCache 기반 후보 vertex 탐색
- MeshSampler를 통한 현재 pose 위치/노멀 검사
- AbsorbedWetnessSimulationState의 pending wetness 누적
*/
struct DWC_API FWetInputStageArgs
{
    UObject*                    OwnerForLogs = nullptr;
    USkeletalMeshComponent*     TargetSkeletalMesh = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;

    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;
    TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>>* SurfaceWaterStatesByMaterialSlot = nullptr;
    const FSurfaceWaterSimulationSettings* SurfaceWaterSettings = nullptr;
    FRandomStream* SurfaceWaterRandomStream = nullptr;

    FWetRuntimeDataBuilder*  RuntimeDataBuilder = nullptr;
    FWetClothingMeshSampler* MeshSampler = nullptr;
    FWetSimulationStage*     SimulationStage = nullptr;

    // Set by callers that need complete per-vertex callback/output data.
    // Such requests intentionally bypass the bone cache.
    bool bRequireFullVertexTraversal = false;
    bool bAsyncSkinningRequested = false;

    TFunction<bool(bool bComputePositions, bool bComputeNormals)> RequestAsyncSkinning;

    float GetAbsorptionMultiplierForVertex(int32 VertexIndex) const;
};

class DWC_API FWetInputStage
{
  public:
    static float CalculateContactExposure(
        const FVector&              WorldNormal,
        const FVector&              Direction,
        const FVector&              Normal,
        const FWetClothingSettings& Settings);

    void ApplyWetAll(FWetInputStageArgs& Args, float Amount);
    bool ApplyWetSurface(
        FWetInputStageArgs&         Args,
        const FDWCWaterSurfaceData& WaterSurfaceData,
        float                       Amount,
        bool                        bApplyMaterial);
    bool ApplyWetArea(
        FWetInputStageArgs&    Args,
        const FDWCWetAreaData& AreaData,
        bool                   bApplyMaterial);
    bool ApplyWetContact(FWetInputStageArgs& Args, const FDWCWetContact& Contact, bool bApplyMaterial);
    bool ApplyWetContacts(
        FWetInputStageArgs&           Args,
        const TArray<FDWCWetContact>& Contacts,
        bool                          bApplyMaterial);
    bool        GetWetnessWorldBounds(const FWetInputStageArgs& Args, FBox& OutBounds);
    static bool QueryWaterSurfaceData(
        const FDWCWaterSurfaceData& WaterSurfaceData,
        const FVector&              WorldPosition,
        float&                      OutSurfaceZ);

    FRandomStream& GetSurfaceWaterRandomStream() { return SurfaceWaterRandomStream; }

  private:
    FRandomStream SurfaceWaterRandomStream = FRandomStream(0x445743);
};
