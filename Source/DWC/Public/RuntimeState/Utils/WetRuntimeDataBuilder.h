//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetClothingRuntimeData.h"

class FSkeletalMeshLODRenderData;
class USkeletalMeshComponent;
class UWetClothingAsset;
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
    const FWetClothingRuntimeData*   RuntimeData = nullptr;
    FWetClothingRuntimeData*         MutableRuntimeData = nullptr;
    FAbsorbedWetnessSimulationState* SimulationState = nullptr;
    TArray<FColor>*                  CachedWetVertexColors = nullptr;

    bool  bUsePrecomputedSimulationData = true;
    bool  bUsePrecomputedBoneOptimizationCache = true;
    bool  bPrecomputedDataAlreadyValidated = false;

};

class DWC_API FWetRuntimeDataBuilder
{
  public:
    FWetRuntimeDataBuilder() = delete;

    static void InitializeAbsorbedWetnessData(FWetRuntimeDataBuildArgs& Args);
    static bool InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Args);
    static bool InitializeWetPartVertexDataFromPrecomputedData(
        FWetRuntimeDataBuildArgs& Args,
        int32 VertexCount);
    /** Builds the common per-vertex Part/Profile lookup from GPU runtime triangles.
     *  This path intentionally does not require CPU neighbor/precomputed data. */
    static bool InitializeWetPartVertexDataFromGPUData(
        FWetRuntimeDataBuildArgs& Args,
        int32 VertexCount);
    static bool InitializeNeighborGraphFromPrecomputedData(FWetRuntimeDataBuildArgs& Args);
    static void EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Args, int32 VertexCount);
    static void EnsureWetnessBufferSize(FWetInputStageArgs& Args, int32 VertexCount);
    static bool GetLODRenderData(
        const USkeletalMeshComponent* TargetSkeletalMesh,
        int32                         LODIndex,
        FSkeletalMeshLODRenderData*&  OutLODData);

    /*
    Failure does not disable the receiver. It records why the runtime must use
    full-vertex traversal when a wet contact is processed.
    */
    static bool InitializeBoneOptimizationCacheFromPrecomputedData(FWetRuntimeDataBuildArgs& Args);

    /*
    Resolves HitResult::BoneName to the target bone plus the pre-flattened
    collisionless parent/child include bones. No runtime recursion is performed.
    */
    static bool ResolveSpecificBonesToLoopThrough(
        const FWetClothingRuntimeData& RuntimeData,
        const USkeletalMeshComponent*  TargetSkeletalMesh,
        FName                          HitBoneName,
        TArray<int32>&                 OutBoneIndices,
        FString*                       OutFallbackReason = nullptr,
        bool                           bRequireFullVertexTraversal = false);

    /*
    Returns cached candidate vertices for the resolved bones. False means the
    caller must perform one full LOD vertex traversal and emit a fallback warning.
    Once this returns true, a geometric search miss must not trigger a second
    full-vertex pass.
    */
    static bool GetBoneCandidateVertexIndices(
        const FWetClothingRuntimeData& RuntimeData,
        const USkeletalMeshComponent*  TargetSkeletalMesh,
        FName                          HitBoneName,
        TArray<int32>&                 OutVertexIndices,
        FString*                       OutFallbackReason = nullptr,
        bool                           bRequireFullVertexTraversal = false);
};
