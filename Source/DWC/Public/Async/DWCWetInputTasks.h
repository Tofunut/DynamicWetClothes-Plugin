#pragma once

#include "Async/DWCTask.h"
#include "DataAssets/WetnessProfile.h"
#include "WetInputSystem/WetContactTypes.h"

class UDynamicWetClothesComponent;

struct DWC_API FDWCWetnessDelta
{
    int32 VertexIndex = INDEX_NONE;
    float Amount = 0.0f;
};

struct DWC_API FDWCWetInputTaskResult
{
    TArray<FDWCWetnessDelta> PendingWetnessDeltas;
    TArray<FDWCWetnessDelta> ImmediateWetnessDeltas;

    bool HasAnyDelta() const
    {
        return PendingWetnessDeltas.Num() > 0 || ImmediateWetnessDeltas.Num() > 0;
    }
};

struct DWC_API FDWCWetSurfaceInputSnapshot
{
    FDWCVertexTaskSnapshot VertexTarget;

    FTransform ComponentTransform = FTransform::Identity;
    FDWCWaterSurfaceData WaterSurfaceData;
    float Amount = 0.0f;

    TArray<FVector3f> SkinnedPositions;
    TArray<float> AbsorbedWetness;
    TArray<bool> VertexWettableFlags;
    TArray<FWetnessProfileParameters> VertexProfileParameters;
};

class DWC_API FDWCWetSurfaceInputTask final : public IDWCTaskRequest
{
  public:
    FDWCWetSurfaceInputTask(
        TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
        FDWCWetSurfaceInputSnapshot&&               InSnapshot);

    virtual EDWCTaskKind GetKind() const override;
    virtual FName        GetDebugName() const override;
    virtual void         ExecuteWorker() override;
    virtual void         CommitGameThread() override;

    const FDWCWetInputTaskResult& GetResult() const
    {
        return Result;
    }

  private:
    bool QueryWaterSurfaceData(const FVector& WorldPosition, float& OutSurfaceZ) const;

    TWeakObjectPtr<UDynamicWetClothesComponent> Owner;
    FDWCWetSurfaceInputSnapshot Snapshot;
    FDWCWetInputTaskResult Result;
};
