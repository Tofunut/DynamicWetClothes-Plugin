#pragma once

#include "CoreMinimal.h"
#include "DynamicWet/DynamicWetReceiverSettings.h"

struct FDynamicWetReceiverContext;

class FDynamicWetReceiverSimulationSolver
{
public:
    float AbsorbWetnessAtVertex(FDynamicWetReceiverContext& Receiver, int32 VertexIndex, float Amount, bool& bDirty);
    void QueuePendingWetness(FDynamicWetReceiverContext& Receiver, int32 VertexIndex, float Amount);
    void RefreshWetnessDryHold(FDynamicWetReceiverContext& Receiver, int32 VertexIndex);
    void ClearPendingWetness(FDynamicWetReceiverContext& Receiver);
    void DryOutWetness(FDynamicWetReceiverContext& Receiver, bool& bDirty, float EffectiveDryRatePerSecond);
    bool PreparePendingWetnessProcessing(
        FDynamicWetReceiverContext& Receiver,
        float EffectiveSpreadRatePerSecond,
        float& OutSpreadAlpha,
        float& OutGravityFlowStrength,
        bool& bOutUseGravityBias,
        bool& bOutCanSpread);
    void SnapshotPendingWetnessForCurrentUpdate(FDynamicWetReceiverContext& Receiver);
    int32 ProcessCurrentPendingWetness(
        FDynamicWetReceiverContext& Receiver,
        bool& bDirty,
        float SpreadAlpha,
        float GravityFlowStrength,
        bool bUseGravityBias,
        bool bCanSpread);
    void SpreadPendingWetnessToNeighbors(
        FDynamicWetReceiverContext& Receiver,
        int32 VertexIndex,
        float SpreadableWetness,
        float SpreadAlpha,
        float GravityFlowStrength,
        bool bUseGravityBias);
    float CalculateNeighborGravityBias(
        const FDynamicWetReceiverContext& Receiver,
        int32 SourceVertexIndex,
        int32 NeighborIndex,
        float GravityFlowStrength,
        const FTransform& ComponentTransform,
        const FVector& GravityDirection);
    void RequeueUnprocessedPendingWetness(FDynamicWetReceiverContext& Receiver, int32 QueueReadIndex);
    void ProcessPendingWetness(
        FDynamicWetReceiverContext& Receiver,
        bool& bDirty,
        float EffectiveSpreadRatePerSecond);
    void UpdateWetness(FDynamicWetReceiverContext& Receiver);

    static float ClampWetness(float Wetness, const FDynamicWetReceiverSettings& Settings);
    static float CalculateDryMultiplier(float DryRatePerSecond, float DeltaSeconds);
};
