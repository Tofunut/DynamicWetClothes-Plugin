#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetClothingRuntimeData.h"

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
    bool InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Args);
    bool InitializeWetPartVertexDataFromPrecomputedData(
        FWetRuntimeDataBuildArgs&        Args,
        int32                            VertexCount,
        const FWetnessProfileParameters& DefaultParameters);
    bool InitializeNeighborGraphFromPrecomputedData(FWetRuntimeDataBuildArgs& Args);
    void EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Args, int32 VertexCount);
    void EnsureWetnessBufferSize(FWetInputStageArgs& Args, int32 VertexCount);
    bool GetLODRenderData(
        const USkeletalMeshComponent* TargetSkeletalMesh,
        int32                         LODIndex,
        FSkeletalMeshLODRenderData*&  OutLODData) const;

    /*
    Failure does not disable the receiver. It records why the runtime must use
    full-vertex traversal when a wet contact is processed.
    */
    bool InitializeBoneOptimizationCacheFromPrecomputedData(FWetRuntimeDataBuildArgs& Args, int32 LODIndex = 0);

    /*
    Resolves HitResult::BoneName to the target bone plus the pre-flattened
    collisionless parent/child include bones. No runtime recursion is performed.
    */
    bool ResolveSpecificBonesToLoopThrough(
        const FWetClothingRuntimeData& RuntimeData,
        const USkeletalMeshComponent*  TargetSkeletalMesh,
        FName                          HitBoneName,
        TArray<int32>&                 OutBoneIndices,
        FString*                       OutFallbackReason = nullptr,
        bool                           bRequireFullVertexTraversal = false) const;

    /*
    Returns cached candidate vertices for the resolved bones. False means the
    caller must perform one full LOD vertex traversal and emit a fallback warning.
    Once this returns true, a geometric search miss must not trigger a second
    full-vertex pass.
    */
    bool GetBoneCandidateVertexIndices(
        const FWetClothingRuntimeData& RuntimeData,
        const USkeletalMeshComponent*  TargetSkeletalMesh,
        FName                          HitBoneName,
        TArray<int32>&                 OutVertexIndices,
        FString*                       OutFallbackReason = nullptr,
        bool                           bRequireFullVertexTraversal = false) const;
};
