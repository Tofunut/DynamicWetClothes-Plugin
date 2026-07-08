#pragma once

#include "CoreMinimal.h"
#include "RuntimeData/WetClothingRuntimeData.h"

class FSkeletalMeshLODRenderData;
class USkeletalMeshComponent;
class UWetClothingAsset;
class UWetnessProfile;
class FAbsorbedWetnessSimulationState;
struct FWetnessProfileParameters;
struct FWetInputStageArgs;

/*
RuntimeData를 생성할 때 필요한 인자 묶음이다.

현재 단계에서는 멀티스레드용 Request가 아니라, DynamicWetClothesComponent가
동기식 초기화 과정에서 RuntimeDataBuilder에 넘기는 단순 인자 구조다.
나중에 TaskQueue를 도입할 경우 이 Args를 기반으로 별도의 TaskRequest를 설계할 수 있다.
*/
struct DWC_API FWetRuntimeDataBuildArgs
{
    UObject*                        OwnerForLogs = nullptr;
    USkeletalMeshComponent*         TargetSkeletalMesh = nullptr;
    const UWetClothingAsset*        WetClothingAsset = nullptr;
    const TArray<UWetnessProfile*>* WetnessProfiles = nullptr;

    FWetClothingRuntimeData*         RuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;
    TArray<FLinearColor>*            CachedWetVertexColors = nullptr;

    FLinearColor UnassignedWetPartDebugColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);

    int32 LODIndex = 0;
    bool  bUsePrecomputedSimulationData = true;
    bool  bUsePrecomputedBoneOptimizationCache = true;
    bool  bAllowRuntimeFallbackBuild = true;
    float CoincidentVertexNeighborTolerance = 0.001f;

    const UWetnessProfile* GetActiveWetnessProfile() const
    {
        if (WetnessProfiles == nullptr)
        {
            return nullptr;
        }

        for (const UWetnessProfile* WetnessProfile : *WetnessProfiles)
        {
            if (WetnessProfile)
            {
                return WetnessProfile;
            }
        }
        return nullptr;
    }
};

class DWC_API FWetRuntimeDataBuilder
{
  public:
    void InitializeAbsorbedWetnessData(FWetRuntimeDataBuildArgs& Args);
    void InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Args);
    bool InitializeWetPartVertexDataFromBakedProfile(
        FWetRuntimeDataBuildArgs&        Args,
        int32                            VertexCount,
        const FWetnessProfileParameters& DefaultParameters);
    void BuildNeighborGraph(FWetRuntimeDataBuildArgs& Args);
    bool BuildNeighborGraphFromBakedProfile(FWetRuntimeDataBuildArgs& Args, int32 VertexCount);
    void AddNeighbor(FWetClothingRuntimeData& RuntimeData, int32 VertexIndex, int32 NeighborIndex);
    void EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Args, int32 VertexCount);
    void EnsureWetnessBufferSize(FWetInputStageArgs& Args, int32 VertexCount);
    bool GetLODRenderData(
        const USkeletalMeshComponent* TargetSkeletalMesh,
        int32                         LODIndex,
        FSkeletalMeshLODRenderData*&  OutLODData) const;
    bool BuildBoneOptimizationCache(FWetRuntimeDataBuildArgs& Args, int32 LODIndex = 0);
    bool GetBoneCandidateVertexRange(
        const FWetClothingRuntimeData& RuntimeData,
        const USkeletalMeshComponent*  TargetSkeletalMesh,
        FName                          BoneName,
        int32&                         OutStartOffset,
        int32&                         OutEndOffset) const;
    bool DoesVertexMatchBoneName(const USkeletalMeshComponent* TargetSkeletalMesh, int32 VertexIndex, FName BoneName) const;
};
