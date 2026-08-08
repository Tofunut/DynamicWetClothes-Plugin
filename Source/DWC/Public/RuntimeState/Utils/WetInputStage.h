// Copyright 2026 Team Tofunut. All Rights Reserved.

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
Arguments required to execute WetInputStage.

This is currently an internal synchronous argument bundle used by DynamicWetClothesComponent. It handles:
- External wet-contact, area, and water-surface input.
- Candidate vertex lookup through the BoneOptimizationCache.
- Current-pose position and normal tests through the mesh sampler.
- Accumulation of pending wetness in AbsorbedWetnessSimulationState.
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

    static float CalculateAreaExposure(
        const FVector&              WorldNormal,
        const FVector&              Direction,
        const FVector&              Normal,
        const FWetClothingSettings& Settings);

    static void ApplyWetAll(FWetInputStageArgs& Args, float Amount);
    static bool ApplyWetSurface(
        FWetInputStageArgs&         Args,
        const FDWCWaterSurfaceData& WaterSurfaceData,
        float                       Amount);
    static bool ApplyWetArea(
        FWetInputStageArgs&    Args,
        const FDWCWetAreaData& AreaData);
    static bool ApplyWetContact(FWetInputStageArgs& Args, const FDWCWetContact& Contact);
    static bool ApplyWetContacts(
        FWetInputStageArgs&           Args,
        const TArray<FDWCWetContact>& Contacts);
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
