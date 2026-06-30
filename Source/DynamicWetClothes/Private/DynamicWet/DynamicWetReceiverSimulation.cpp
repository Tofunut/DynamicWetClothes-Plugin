// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/DynamicWetReceiverSimulationSolver.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverContext.h"
#include "DynamicWet/DynamicWetReceiverRenderApplier.h"
#include "DynamicWet/DynamicWetReceiverMeshSampler.h"
#include "DynamicWet/DynamicWetReceiverRuntimeData.h"
#include "DynamicWet/DynamicWetReceiverSimulationState.h"

float FDynamicWetReceiverSimulationSolver::ClampWetness(const float Wetness, const FDynamicWetReceiverSettings& Settings)
{
    return FMath::Clamp(Wetness, 0.0f, FMath::Max(0.0f, Settings.MaxStoredWetness));
}

float FDynamicWetReceiverSimulationSolver::CalculateDryMultiplier(const float DryRatePerSecond, const float DeltaSeconds)
{
    return FMath::Exp(-FMath::Max(0.0f, DryRatePerSecond) * DeltaSeconds);
}


float FDynamicWetReceiverSimulationSolver::AbsorbWetnessAtVertex(FDynamicWetReceiverContext& Receiver, const int32 VertexIndex, const float Amount, bool& bDirty)
{
    if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex) || FMath::IsNearlyZero(Amount))
    {
        return 0.0f;
    }

    if (Amount > Receiver.WetnessSettings.MinPendingWetnessAmount)
    {
        RefreshWetnessDryHold(Receiver, VertexIndex);
    }

    float&      Wetness = Receiver.SimulationState.WetnessPerVertex[VertexIndex];
    const float OldWetness = Wetness;
    const float NewWetness = FMath::Clamp(
        Wetness + Amount,
        0.0f,
        FMath::Max(0.0f, Receiver.WetnessSettings.MaxStoredWetness));
    const float AbsorbedAmount = NewWetness - OldWetness;

    if (!FMath::IsNearlyEqual(OldWetness, NewWetness))
    {
        Wetness = NewWetness;
        Receiver.SimulationState.DirtyWetVertexIndices.Add(VertexIndex);
        bDirty = true;
    }

    return AbsorbedAmount;
}

void FDynamicWetReceiverSimulationSolver::QueuePendingWetness(FDynamicWetReceiverContext& Receiver, const int32 VertexIndex, const float Amount)
{
    if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Receiver.SimulationState.Updating_Pending_Wetness_Amounts.IsValidIndex(VertexIndex) ||
        !Receiver.SimulationState.bPendingWetnessQueued.IsValidIndex(VertexIndex) ||
        Amount <= Receiver.WetnessSettings.MinPendingWetnessAmount)
    {
        return;
    }

    Receiver.SimulationState.Updating_Pending_Wetness_Amounts[VertexIndex] += Amount;
    RefreshWetnessDryHold(Receiver, VertexIndex);

    if (!Receiver.SimulationState.bPendingWetnessQueued[VertexIndex])
    {
        Receiver.SimulationState.bPendingWetnessQueued[VertexIndex] = true;
        Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Add(VertexIndex);
    }
}

void FDynamicWetReceiverSimulationSolver::RefreshWetnessDryHold(FDynamicWetReceiverContext& Receiver, const int32 VertexIndex)
{
    if (!Receiver.SimulationState.WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) ||
        Receiver.WetnessSettings.WetnessDryHoldDuration <= 0.0f)
    {
        return;
    }

    Receiver.SimulationState.WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
        Receiver.SimulationState.WetnessDryHoldTimePerVertex[VertexIndex],
        Receiver.WetnessSettings.WetnessDryHoldDuration);
}

void FDynamicWetReceiverSimulationSolver::ClearPendingWetness(FDynamicWetReceiverContext& Receiver)
{
    for (float& PendingWetness : Receiver.SimulationState.Updating_Pending_Wetness_Amounts)
    {
        PendingWetness = 0.0f;
    }

    Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reset();

    for (bool& bQueued : Receiver.SimulationState.bPendingWetnessQueued)
    {
        bQueued = false;
    }
}

void FDynamicWetReceiverSimulationSolver::DryOutWetness(FDynamicWetReceiverContext& Receiver, bool& bDirty, const float EffectiveDryRatePerSecond)
{
    for (int32 VertexIndex = 0; VertexIndex < Receiver.SimulationState.WetnessPerVertex.Num(); ++VertexIndex)
    {
        if (Receiver.SimulationState.WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) &&
            Receiver.SimulationState.WetnessDryHoldTimePerVertex[VertexIndex] > 0.0f)
        {
            Receiver.SimulationState.WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
                0.0f,
                Receiver.SimulationState.WetnessDryHoldTimePerVertex[VertexIndex] - Receiver.WetnessSettings.WetnessUpdateInterval);
            continue;
        }

        float& Wetness = Receiver.SimulationState.WetnessPerVertex[VertexIndex];
        if (Wetness > 0.0f)
        {
            const float VertexDryRate = Receiver.RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
                                            ? Receiver.GetDryRatePerSecondForVertex(VertexIndex)
                                            : EffectiveDryRatePerSecond;
            const float DryMultiplier = FMath::Exp(
                -FMath::Max(0.0f, VertexDryRate) * Receiver.WetnessSettings.WetnessUpdateInterval);
            const float OldWetness = Wetness;
            Wetness *= DryMultiplier;
            if (Wetness <= Receiver.WetnessSettings.MinPendingWetnessAmount)
            {
                Wetness = 0.0f;
            }

            if (!FMath::IsNearlyEqual(OldWetness, Wetness))
            {
                Receiver.SimulationState.DirtyWetVertexIndices.Add(VertexIndex);
                bDirty = true;
            }
        }
    }
}

bool FDynamicWetReceiverSimulationSolver::PreparePendingWetnessProcessing(FDynamicWetReceiverContext& Receiver, const float EffectiveSpreadRatePerSecond, float& OutSpreadAlpha, float& OutGravityFlowStrength, bool& bOutUseGravityBias, bool& bOutCanSpread)
{
    if (Receiver.SimulationState.WetnessPerVertex.Num() == 0)
    {
        return false;
    }

    if (Receiver.SimulationState.Updating_Pending_Wetness_Amounts.Num() != Receiver.SimulationState.WetnessPerVertex.Num() ||
        Receiver.SimulationState.bPendingWetnessQueued.Num() != Receiver.SimulationState.WetnessPerVertex.Num())
    {
        Receiver.RuntimeDataBuilder.EnsureWetnessBufferSize(Receiver, Receiver.SimulationState.WetnessPerVertex.Num());
    }

    if (Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Num() == 0)
    {
        return false;
    }

    OutGravityFlowStrength = Receiver.GetGravityFlowStrength();
    bOutUseGravityBias = OutGravityFlowStrength > 0.0f;
    if (!bOutUseGravityBias)
    {
        for (const int32 VertexIndex : Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue)
        {
            if (Receiver.GetGravityFlowStrengthForVertex(VertexIndex) > 0.0f)
            {
                bOutUseGravityBias = true;
                break;
            }
        }
    }

    bOutCanSpread =
        Receiver.RuntimeData.NeighborGraph.Num() == Receiver.SimulationState.WetnessPerVertex.Num();

    if (bOutUseGravityBias)
    {
        bOutUseGravityBias =
            Receiver.MeshSampler.UpdateSkinnedPositions(Receiver) &&
            Receiver.MeshSampler.CachedSkinnedPositions.Num() == Receiver.SimulationState.WetnessPerVertex.Num();
    }

    OutSpreadAlpha = FMath::Clamp(
        EffectiveSpreadRatePerSecond * Receiver.WetnessSettings.WetnessUpdateInterval,
        0.0f,
        1.0f);

    return true;
}

void FDynamicWetReceiverSimulationSolver::SnapshotPendingWetnessForCurrentUpdate(FDynamicWetReceiverContext& Receiver)
{
    Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reset();
    Swap(Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue, Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue);
    Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reserve(Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Num());

    for (const int32 VertexIndex : Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue)
    {
        float PendingWater = 0.0f;
        if (Receiver.SimulationState.Updating_Pending_Wetness_Amounts.IsValidIndex(VertexIndex))
        {
            PendingWater = Receiver.SimulationState.Updating_Pending_Wetness_Amounts[VertexIndex];
            Receiver.SimulationState.Updating_Pending_Wetness_Amounts[VertexIndex] = 0.0f;
        }

        Receiver.SimulationState.Current_Pending_Wetness_Amounts.Add(PendingWater);

        if (Receiver.SimulationState.bPendingWetnessQueued.IsValidIndex(VertexIndex))
        {
            Receiver.SimulationState.bPendingWetnessQueued[VertexIndex] = false;
        }
    }
}

int32 FDynamicWetReceiverSimulationSolver::ProcessCurrentPendingWetness(FDynamicWetReceiverContext& Receiver, bool& bDirty, const float SpreadAlpha, const float GravityFlowStrength, const bool bUseGravityBias, const bool bCanSpread)
{
    (void)SpreadAlpha;
    (void)GravityFlowStrength;

    int32 QueueReadIndex = 0;
    int32 ProcessedVertices = 0;

    while (QueueReadIndex < Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Num() &&
           ProcessedVertices < Receiver.WetnessSettings.MaxPendingWetnessVerticesPerUpdate)
    {
        const int32 VertexIndex = Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue[QueueReadIndex++];
        const int32 CurrentAmountIndex = QueueReadIndex - 1;
        ++ProcessedVertices;

        if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex) ||
            !Receiver.SimulationState.Current_Pending_Wetness_Amounts.IsValidIndex(CurrentAmountIndex))
        {
            continue;
        }

        const float PendingWater = Receiver.SimulationState.Current_Pending_Wetness_Amounts[CurrentAmountIndex];

        if (PendingWater <= Receiver.WetnessSettings.MinPendingWetnessAmount)
        {
            continue;
        }

        const float SafeImmediateAbsorptionFraction = FMath::Clamp(
            Receiver.WetnessSettings.CapillaryImmediateAbsorptionFraction,
            0.0f,
            Receiver.WetnessSettings.MaxStoredWetness);

        const float DesiredAbsorption = PendingWater * SafeImmediateAbsorptionFraction;
        const float AbsorbedWetness = AbsorbWetnessAtVertex(Receiver, VertexIndex, DesiredAbsorption, bDirty);
        const float OverflowWetness = FMath::Max(0.0f, DesiredAbsorption - AbsorbedWetness); // MaxStored�??�어???�수?��? 못하�??�온 ??
        const float CapillaryWetness = FMath::Max(0.0f, PendingWater - DesiredAbsorption);   // MaxStored�??��????�았지�??�수?�이 ?�려???�수?��? 못한 ??
        const float SpreadableWetness = CapillaryWetness + OverflowWetness;
        const float VertexSpreadAlpha = FMath::Clamp(
            Receiver.GetSpreadRatePerSecondForVertex(VertexIndex) * Receiver.WetnessSettings.WetnessUpdateInterval,
            0.0f,
            1.0f);
        const float VertexGravityFlowStrength = Receiver.GetGravityFlowStrengthForVertex(VertexIndex);

        if (!bCanSpread ||
            VertexSpreadAlpha <= 0.0f ||
            SpreadableWetness <= Receiver.WetnessSettings.MinPendingWetnessAmount)
        {
            continue;
        }

        SpreadPendingWetnessToNeighbors(Receiver, 
            VertexIndex,
            SpreadableWetness,
            VertexSpreadAlpha,
            VertexGravityFlowStrength,
            bUseGravityBias && VertexGravityFlowStrength > 0.0f);
    }

    return QueueReadIndex;
}

void FDynamicWetReceiverSimulationSolver::SpreadPendingWetnessToNeighbors(FDynamicWetReceiverContext& Receiver, const int32 VertexIndex, const float SpreadableWetness, const float SpreadAlpha, const float GravityFlowStrength, const bool bUseGravityBias)
{
    if (!Receiver.RuntimeData.NeighborGraph.IsValidIndex(VertexIndex))
    {
        return;
    }

    const TArray<int32>& Neighbors = Receiver.RuntimeData.NeighborGraph[VertexIndex].Neighbors;
    if (Neighbors.Num() == 0)
    {
        return;
    }

    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    const FVector    GravityDirection = FVector::DownVector;

    TArray<float, TInlineAllocator<16>> NeighborWeights;
    NeighborWeights.SetNumZeroed(Neighbors.Num());
    float TotalWeight = 0.0f;

    for (int32 NeighborArrayIndex = 0; NeighborArrayIndex < Neighbors.Num(); ++NeighborArrayIndex)
    {
        const int32 NeighborIndex = Neighbors[NeighborArrayIndex];
        if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(NeighborIndex))
        {
            continue;
        }

        const float TargetCapacity = Receiver.WetnessSettings.MaxStoredWetness - Receiver.SimulationState.WetnessPerVertex[NeighborIndex];
        // if (TargetCapacity <= Receiver.WetnessSettings.MinPendingWetnessAmount)
        // {
        //     continue;
        // }

        const float GravityBias =
            bUseGravityBias
                ? CalculateNeighborGravityBias(Receiver, 
                      VertexIndex,
                      NeighborIndex,
                      GravityFlowStrength,
                      ComponentTransform,
                      GravityDirection)
                : 1.0f;

        float PartBoundaryScale = 1.0f;
        if (Receiver.RuntimeData.VertexWetPartIDs.IsValidIndex(VertexIndex) &&
            Receiver.RuntimeData.VertexWetPartIDs.IsValidIndex(NeighborIndex) &&
            Receiver.RuntimeData.VertexWetPartIDs[VertexIndex] != Receiver.RuntimeData.VertexWetPartIDs[NeighborIndex])
        {
            PartBoundaryScale = FMath::Clamp(Receiver.WetnessSettings.CrossWetPartSpreadScale, 0.0f, 1.0f);
        }

        const float Weight = TargetCapacity * GravityBias * PartBoundaryScale;
        if (Weight <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        NeighborWeights[NeighborArrayIndex] = Weight;
        TotalWeight += Weight;
    }

    if (TotalWeight <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float TotalFlowAmount = SpreadableWetness * SpreadAlpha;

    for (int32 NeighborArrayIndex = 0; NeighborArrayIndex < Neighbors.Num(); ++NeighborArrayIndex)
    {
        const float Weight = NeighborWeights[NeighborArrayIndex];
        if (Weight <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const int32 NeighborIndex = Neighbors[NeighborArrayIndex];
        const float FlowAmount = TotalFlowAmount * (Weight / TotalWeight);

        if (FlowAmount > Receiver.WetnessSettings.MinPendingWetnessAmount)
        {
            QueuePendingWetness(Receiver, NeighborIndex, FlowAmount);
        }
    }
}

float FDynamicWetReceiverSimulationSolver::CalculateNeighborGravityBias(const FDynamicWetReceiverContext& Receiver, const int32 SourceVertexIndex, const int32 NeighborIndex, const float GravityFlowStrength, const FTransform& ComponentTransform, const FVector& GravityDirection)
{
    if (!Receiver.MeshSampler.CachedSkinnedPositions.IsValidIndex(SourceVertexIndex) ||
        !Receiver.MeshSampler.CachedSkinnedPositions.IsValidIndex(NeighborIndex))
    {
        return 1.0f;
    }

    const FVector SourceWorldPosition = ComponentTransform.TransformPosition(
        FVector(Receiver.MeshSampler.CachedSkinnedPositions[SourceVertexIndex]));

    const FVector TargetWorldPosition = ComponentTransform.TransformPosition(
        FVector(Receiver.MeshSampler.CachedSkinnedPositions[NeighborIndex]));

    const FVector FlowDirection =
        (TargetWorldPosition - SourceWorldPosition).GetSafeNormal();

    const float GravityAlignment =
        FVector::DotProduct(FlowDirection, GravityDirection);

    return FMath::Clamp(
        1.0f + GravityAlignment * GravityFlowStrength,
        0.0f,
        2.0f);
}

void FDynamicWetReceiverSimulationSolver::RequeueUnprocessedPendingWetness(FDynamicWetReceiverContext& Receiver, const int32 QueueReadIndex)
{
    for (int32 RemainingQueueIndex = QueueReadIndex;
         RemainingQueueIndex < Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Num();
         ++RemainingQueueIndex)
    {
        const int32 VertexIndex = Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue[RemainingQueueIndex];
        if (Receiver.SimulationState.Current_Pending_Wetness_Amounts.IsValidIndex(RemainingQueueIndex))
        {
            QueuePendingWetness(Receiver, VertexIndex, Receiver.SimulationState.Current_Pending_Wetness_Amounts[RemainingQueueIndex]);
        }
    }
}

void FDynamicWetReceiverSimulationSolver::ProcessPendingWetness(FDynamicWetReceiverContext& Receiver, bool& bDirty, const float EffectiveSpreadRatePerSecond)
{
    float SpreadAlpha = 0.0f;
    float GravityFlowStrength = 0.0f;
    bool  bUseGravityBias = false;
    bool  bCanSpread = false;

    if (!PreparePendingWetnessProcessing(Receiver, 
            EffectiveSpreadRatePerSecond,
            SpreadAlpha,
            GravityFlowStrength,
            bUseGravityBias,
            bCanSpread))
    {
        return;
    }

    SnapshotPendingWetnessForCurrentUpdate(Receiver);

    const int32 QueueReadIndex = ProcessCurrentPendingWetness(Receiver, 
        bDirty,
        SpreadAlpha,
        GravityFlowStrength,
        bUseGravityBias,
        bCanSpread);

    RequeueUnprocessedPendingWetness(Receiver, QueueReadIndex);

    Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reset();
}

void FDynamicWetReceiverSimulationSolver::UpdateWetness(FDynamicWetReceiverContext& Receiver)
{
    bool        bDirty = false;
    const float EffectiveDryRatePerSecond = Receiver.GetDryRatePerSecond();
    const float EffectiveSpreadRatePerSecond = Receiver.GetSpreadRatePerSecond();

    if (Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Num() > 0)
    {
        ProcessPendingWetness(Receiver, bDirty, EffectiveSpreadRatePerSecond);
    }
    else
    {
        ClearPendingWetness(Receiver);
    }

    DryOutWetness(Receiver, bDirty, EffectiveDryRatePerSecond);

    if (bDirty)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }
}
