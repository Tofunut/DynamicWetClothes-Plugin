#pragma once

#include "CoreMinimal.h"
#include "Core/WetClothingSettings.h"
#include "Templates/Function.h"

class USkeletalMeshComponent;
class FWetClothingRuntimeData;
class FWetClothingMeshSampler;
class FAbsorbedWetnessSimulationState;
struct FWetInputStageArgs;

/*
WetSimulationStage 실행에 필요한 인자 묶음이다.

현재는 멀티스레드 작업 요청이 아니라, DynamicWetClothesComponent가 동기식으로
시뮬레이션 갱신을 호출할 때 필요한 상태와 helper 참조를 모은 내부용 인자다.
CPU Simulation의 Absorbed Wetness 확산과 건조만 갱신한다. Surface Water는 GPU Backend가 전담한다.
*/
struct DWC_API FWetSimulationStageArgs
{
    USkeletalMeshComponent*     TargetSkeletalMesh = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;

    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;

    FWetClothingMeshSampler* MeshSampler = nullptr;

    bool  bAsyncSkinningRequested = false;

    TFunction<bool(bool bComputePositions, bool bComputeNormals)> RequestAsyncSkinning;

    float GetDryRatePerSecond() const;
    float GetSpreadRatePerSecond() const;
    float GetGravityFlowStrength() const;
    float GetDryRatePerSecondForVertex(int32 VertexIndex) const;
    float GetSpreadRatePerSecondForVertex(int32 VertexIndex) const;
    float GetGravityFlowStrengthForVertex(int32 VertexIndex) const;
};

class DWC_API FWetSimulationStage
{
  public:
    FWetSimulationStage() = delete;

    static float AbsorbWetnessAtVertex(FWetInputStageArgs& Args, int32 VertexIndex, float Amount, bool& bDirty);
    static void  QueuePendingWetness(FWetInputStageArgs& Args, int32 VertexIndex, float Amount);

    static float AbsorbWetnessAtVertex(FWetSimulationStageArgs& Args, int32 VertexIndex, float Amount, bool& bDirty);
    static void  QueuePendingWetness(FWetSimulationStageArgs& Args, int32 VertexIndex, float Amount);
    static void  RefreshWetnessDryHold(FWetSimulationStageArgs& Args, int32 VertexIndex);
    static void  ClearPendingWetness(FWetSimulationStageArgs& Args);
    static void  DryOutWetness(FWetSimulationStageArgs& Args, bool& bDirty, float EffectiveDryRatePerSecond);
    static bool  PreparePendingWetnessProcessing(
         FWetSimulationStageArgs& Args,
         float                    EffectiveSpreadRatePerSecond,
         float&                   OutSpreadAlpha,
         float&                   OutGravityFlowStrength,
         FVector&                 OutLocalGravityDirection,
         bool&                    bOutUseGravityBias,
         bool&                    bOutCanSpread);
    static void  SnapshotPendingWetnessForCurrentUpdate(FWetSimulationStageArgs& Args);
    static int32 ProcessCurrentPendingWetness(
        FWetSimulationStageArgs& Args,
        bool&                    bDirty,
        float                    SpreadAlpha,
        float                    GravityFlowStrength,
        const FVector&           LocalGravityDirection,
        bool                     bUseGravityBias,
        bool                     bCanSpread);
    static void SpreadPendingWetnessToNeighbors(
        FWetSimulationStageArgs& Args,
        int32                    VertexIndex,
        float                    SpreadableWetness,
        float                    SpreadAlpha,
        float                    GravityFlowStrength,
        const FVector&           LocalGravityDirection,
        bool                     bUseGravityBias);
    static float CalculateNeighborGravityBias(
        const FWetSimulationStageArgs& Args,
        const FVector&                 SourceLocalPosition,
        int32                          NeighborIndex,
        float                          GravityFlowStrength,
        const FVector&                 LocalGravityDirection);
    static void ProcessPendingWetness(
        FWetSimulationStageArgs& Args,
        bool&                    bDirty,
        float                    EffectiveSpreadRatePerSecond);
    static bool UpdateWetness(FWetSimulationStageArgs& Args);

    static float ClampWetness(float Wetness, const FWetClothingSettings& Settings);
    static float CalculateDryMultiplier(float DryRatePerSecond, float DeltaSeconds);
};
