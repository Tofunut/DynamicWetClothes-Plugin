//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/WetClothingSettings.h"
#include "WetInputSystem/WetContactTypes.h"
#include "Templates/Function.h"

class USkeletalMeshComponent;
class FSkeletalMeshLODRenderData;
class FWetClothingRuntimeData;
class FWetClothingMeshSampler;
class FAbsorbedWetnessSimulationState;

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

    FWetClothingMeshSampler* MeshSampler = nullptr;

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
    FWetInputStage() = delete;

    static float CalculateContactExposure(
        const FVector&              WorldNormal,
        const FVector&              Direction,
        const FVector&              Normal,
        const FWetClothingSettings& Settings);

    static void ApplyWetAll(FWetInputStageArgs& Args, float Amount);
    static bool ApplyWetSurface(
        FWetInputStageArgs&         Args,
        const FDWCWaterSurfaceData& WaterSurfaceData,
        float                       Amount,
        bool                        bApplyMaterial);
    static bool ApplyWetArea(
        FWetInputStageArgs&    Args,
        const FDWCWetAreaData& AreaData,
        bool                   bApplyMaterial);
    static bool ApplyWetContact(FWetInputStageArgs& Args, const FDWCWetContact& Contact, bool bApplyMaterial);
    static bool ApplyWetContacts(
        FWetInputStageArgs&           Args,
        const TArray<FDWCWetContact>& Contacts,
        bool                          bApplyMaterial);
    static bool GetWetnessWorldBounds(const FWetInputStageArgs& Args, FBox& OutBounds);
    static bool QueryWaterSurfaceData(
        const FDWCWaterSurfaceData& WaterSurfaceData,
        const FVector&              WorldPosition,
        float&                      OutSurfaceZ);

  private:
    struct FWetAreaCandidate
    {
        int32 VertexIndex = INDEX_NONE;
        float Exposure = 1.0f;
        float PickWeight = 1.0f;
    };

    static bool CanApplyWetAreaToVertex(const FWetInputStageArgs& Args, const FDWCWetAreaData& AreaData, int32 VertexIndex);

    static float CalculateWetAreaRawExposure(
        const FWetInputStageArgs&         Args,
        const FSkeletalMeshLODRenderData& LODData,
        const FTransform&                 ComponentTransform,
        const FVector&                    SafeDirection,
        const FVector&                    SafeNormal,
        bool                              bWantsNormalExposure,
        bool                              bHasSkinnedNormals,
        int32                             VertexIndex);

    static int32 SelectWetAreaCandidateIndex(const TArray<FWetAreaCandidate>& Candidates, FRandomStream& RandomStream);
};
