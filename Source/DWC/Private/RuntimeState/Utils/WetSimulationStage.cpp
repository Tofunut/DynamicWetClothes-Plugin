//Copyright 2026 Team Tofunut. All Rights Reserved.
// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeState/Utils/WetSimulationStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Utility/DWCProfiling.h"

namespace
{
    bool RequestAsyncSkinning(FWetSimulationStageArgs& Receiver, const bool bComputePositions, const bool bComputeNormals)
    {
        if (!Receiver.RequestAsyncSkinning)
        {
            return false;
        }

        const bool bRequested = Receiver.RequestAsyncSkinning(bComputePositions, bComputeNormals);
        Receiver.bAsyncSkinningRequested |= bRequested;
        return bRequested;
    }

    bool EnsureCachedSkinnedPositions(FWetSimulationStageArgs& Receiver)
    {
        if (Receiver.MeshSampler != nullptr &&
            Receiver.SimulationState != nullptr &&
            Receiver.MeshSampler->CachedSkinnedPositions.Num() == Receiver.SimulationState->AbsorbedWetnessPerVertex.Num())
        {
            return true;
        }

        return false;
    }

    const FWetnessProfileParameters* FindFirstVertexParameters(const FWetClothingRuntimeData* RuntimeData)
    {
        if (RuntimeData == nullptr)
        {
            return nullptr;
        }

        for (const FWetnessProfileParameters& Parameters : RuntimeData->WetnessProfileTable)
        {
            return &Parameters;
        }

        return nullptr;
    }
} // namespace

float FWetSimulationStageArgs::GetDryRatePerSecond() const
{
    const FWetnessProfileParameters* Parameters = FindFirstVertexParameters(RuntimeData);
    const float DryRateScale = WetnessSettings != nullptr ? FMath::Max(0.0f, WetnessSettings->DryRateScale) : 1.0f;
    return (Parameters ? Parameters->GetDryRatePerSecond() : 1.0f) * DryRateScale;
}

float FWetSimulationStageArgs::GetSpreadRatePerSecond() const
{
    const FWetnessProfileParameters* Parameters = FindFirstVertexParameters(RuntimeData);
    return Parameters ? Parameters->GetSpreadRatePerSecond() : 0.0f;
}

float FWetSimulationStageArgs::GetGravityFlowStrength() const
{
    const FWetnessProfileParameters* Parameters = FindFirstVertexParameters(RuntimeData);
    return Parameters ? Parameters->GetGravityFlowStrength() : 0.0f;
}

float FWetSimulationStageArgs::GetDryRatePerSecondForVertex(const int32 VertexIndex) const
{
    const FWetnessProfileParameters* Parameters = RuntimeData != nullptr
                                                     ? RuntimeData->GetWetnessProfileParameters(VertexIndex)
                                                     : nullptr;
    if (Parameters == nullptr)
    {
        return GetDryRatePerSecond();
    }

    const float DryRateScale = WetnessSettings != nullptr ? FMath::Max(0.0f, WetnessSettings->DryRateScale) : 1.0f;
    return Parameters->GetDryRatePerSecond() * DryRateScale;
}

float FWetSimulationStageArgs::GetSpreadRatePerSecondForVertex(const int32 VertexIndex) const
{
    const FWetnessProfileParameters* Parameters = RuntimeData != nullptr
                                                     ? RuntimeData->GetWetnessProfileParameters(VertexIndex)
                                                     : nullptr;
    return Parameters != nullptr
               ? Parameters->GetSpreadRatePerSecond()
               : GetSpreadRatePerSecond();
}

float FWetSimulationStageArgs::GetGravityFlowStrengthForVertex(const int32 VertexIndex) const
{
    const FWetnessProfileParameters* Parameters = RuntimeData != nullptr
                                                     ? RuntimeData->GetWetnessProfileParameters(VertexIndex)
                                                     : nullptr;
    return Parameters != nullptr
               ? Parameters->GetGravityFlowStrength()
               : GetGravityFlowStrength();
}

float FWetSimulationStage::ClampWetness(const float Wetness, const FWetClothingSettings& Settings)
{
    return FMath::Clamp(Wetness, 0.0f, FMath::Max(0.0f, Settings.MaxWetness));
}

float FWetSimulationStage::CalculateDryMultiplier(const float DryRatePerSecond, const float DeltaSeconds)
{
    return FMath::Exp(-FMath::Max(0.0f, DryRatePerSecond) * DeltaSeconds);
}

float FWetSimulationStage::AbsorbWetnessAtVertex(FWetInputStageArgs& Receiver, const int32 VertexIndex, const float Amount, bool& bDirty)
{
    if (!Receiver.SimulationState || !Receiver.WetnessSettings ||
        !Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Receiver.RuntimeData ||
        !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
        FMath::IsNearlyZero(Amount))
    {
        return 0.0f;
    }

    if (Amount > Receiver.WetnessSettings->MinPendingWetnessAmount &&
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) &&
        Receiver.WetnessSettings->WetnessDryHoldDuration > 0.0f)
    {
        Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
            Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex],
            Receiver.WetnessSettings->WetnessDryHoldDuration);
    }

    float&      Wetness = Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex];
    const float OldWetness = Wetness;
    const float NewWetness = FMath::Clamp(
        Wetness + Amount,
        0.0f,
        FMath::Max(0.0f, Receiver.WetnessSettings->MaxWetness));
    const float AbsorbedAmount = NewWetness - OldWetness;

    if (!FMath::IsNearlyEqual(OldWetness, NewWetness))
    {
        Wetness = NewWetness;
        Receiver.SimulationState->MarkWetVertexDirty(VertexIndex);
        bDirty = true;
    }

    return AbsorbedAmount;
}

void FWetSimulationStage::QueuePendingWetness(FWetInputStageArgs& Receiver, const int32 VertexIndex, const float Amount)
{
    if (!Receiver.SimulationState || !Receiver.WetnessSettings ||
        !Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Receiver.RuntimeData ||
        !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
        !Receiver.SimulationState->UpdatingPendingWetnessAmounts.IsValidIndex(VertexIndex) ||
        !Receiver.SimulationState->bPendingWetnessQueued.IsValidIndex(VertexIndex) ||
        Amount <= Receiver.WetnessSettings->MinPendingWetnessAmount)
    {
        return;
    }

    Receiver.SimulationState->UpdatingPendingWetnessAmounts[VertexIndex] += Amount;

    if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) &&
        Receiver.WetnessSettings->WetnessDryHoldDuration > 0.0f)
    {
        Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
            Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex],
            Receiver.WetnessSettings->WetnessDryHoldDuration);
    }

    if (!Receiver.SimulationState->bPendingWetnessQueued[VertexIndex])
    {
        Receiver.SimulationState->bPendingWetnessQueued[VertexIndex] = true;
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Add(VertexIndex);
    }
}

float FWetSimulationStage::AbsorbWetnessAtVertex(FWetSimulationStageArgs& Receiver, const int32 VertexIndex, const float Amount, bool& bDirty)
{
    if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Receiver.RuntimeData ||
        !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
        FMath::IsNearlyZero(Amount))
    {
        return 0.0f;
    }

    if (Amount > Receiver.WetnessSettings->MinPendingWetnessAmount)
    {
        RefreshWetnessDryHold(Receiver, VertexIndex);
    }

    float&      Wetness = Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex];
    const float OldWetness = Wetness;
    const float NewWetness = FMath::Clamp(
        Wetness + Amount,
        0.0f,
        FMath::Max(0.0f, Receiver.WetnessSettings->MaxWetness));
    const float AbsorbedAmount = NewWetness - OldWetness;

    if (!FMath::IsNearlyEqual(OldWetness, NewWetness))
    {
        Wetness = NewWetness;
        Receiver.SimulationState->MarkWetVertexDirty(VertexIndex);
        bDirty = true;
    }

    return AbsorbedAmount;
}

void FWetSimulationStage::QueuePendingWetness(FWetSimulationStageArgs& Receiver, const int32 VertexIndex, const float Amount)
{
    if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Receiver.RuntimeData ||
        !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
        !Receiver.SimulationState->UpdatingPendingWetnessAmounts.IsValidIndex(VertexIndex) ||
        !Receiver.SimulationState->bPendingWetnessQueued.IsValidIndex(VertexIndex) ||
        Amount <= Receiver.WetnessSettings->MinPendingWetnessAmount)
    {
        return;
    }

    Receiver.SimulationState->UpdatingPendingWetnessAmounts[VertexIndex] += Amount;
    RefreshWetnessDryHold(Receiver, VertexIndex);

    if (!Receiver.SimulationState->bPendingWetnessQueued[VertexIndex])
    {
        Receiver.SimulationState->bPendingWetnessQueued[VertexIndex] = true;
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Add(VertexIndex);
    }
}

void FWetSimulationStage::RefreshWetnessDryHold(FWetSimulationStageArgs& Receiver, const int32 VertexIndex)
{
    if (!Receiver.SimulationState->WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) ||
        Receiver.WetnessSettings->WetnessDryHoldDuration <= 0.0f)
    {
        return;
    }

    Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
        Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex],
        Receiver.WetnessSettings->WetnessDryHoldDuration);
}

void FWetSimulationStage::ClearPendingWetness(FWetSimulationStageArgs& Receiver)
{
    for (float& PendingWetness : Receiver.SimulationState->UpdatingPendingWetnessAmounts)
    {
        PendingWetness = 0.0f;
    }

    Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
    Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
    Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
    Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;

    for (bool& bQueued : Receiver.SimulationState->bPendingWetnessQueued)
    {
        bQueued = false;
    }
}

void FWetSimulationStage::DryOutWetness(FWetSimulationStageArgs& Receiver, bool& bDirty, const float EffectiveDryRatePerSecond)
{
    DWC_PROFILE_SCOPE(DWC_Simulation_DryOutWetness);

    for (int32 VertexIndex = 0; VertexIndex < Receiver.SimulationState->AbsorbedWetnessPerVertex.Num(); ++VertexIndex)
    {
        if (!Receiver.RuntimeData || !Receiver.RuntimeData->IsVertexWettable(VertexIndex))
        {
            continue;
        }

        if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.IsValidIndex(VertexIndex) &&
            Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] > 0.0f)
        {
            Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] = FMath::Max(
                0.0f,
                Receiver.SimulationState->WetnessDryHoldTimePerVertex[VertexIndex] - Receiver.WetnessSettings->WetnessUpdateInterval);
            continue;
        }

        float& Wetness = Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex];
        if (Wetness > 0.0f)
        {
            const float VertexDryRate = Receiver.RuntimeData->GetWetnessProfileParameters(VertexIndex) != nullptr
                                            ? Receiver.GetDryRatePerSecondForVertex(VertexIndex)
                                            : EffectiveDryRatePerSecond;
            const float DryMultiplier = FMath::Exp(
                -FMath::Max(0.0f, VertexDryRate) * Receiver.WetnessSettings->WetnessUpdateInterval);
            const float OldWetness = Wetness;
            Wetness *= DryMultiplier;
            if (Wetness <= Receiver.WetnessSettings->MinPendingWetnessAmount)
            {
                Wetness = 0.0f;
            }

            if (!FMath::IsNearlyEqual(OldWetness, Wetness))
            {
                Receiver.SimulationState->MarkWetVertexDirty(VertexIndex);
                bDirty = true;
            }
        }
    }
}

bool FWetSimulationStage::PreparePendingWetnessProcessing(FWetSimulationStageArgs& Receiver, const float EffectiveSpreadRatePerSecond, float& OutSpreadAlpha, float& OutGravityFlowStrength, FVector& OutLocalGravityDirection, bool& bOutUseGravityBias, bool& bOutCanSpread)
{
    DWC_PROFILE_SCOPE(DWC_Simulation_PreparePendingWetnessProcessing);

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() == 0)
    {
        return false;
    }

    if (Receiver.SimulationState->UpdatingPendingWetnessAmounts.Num() != Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() ||
        Receiver.SimulationState->bPendingWetnessQueued.Num() != Receiver.SimulationState->AbsorbedWetnessPerVertex.Num())
    {
        return false;
    }

    const bool bHasCurrentPendingWetness =
        Receiver.SimulationState->CurrentPendingWetnessReadIndex <
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num();

    if (Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Num() == 0 &&
        !bHasCurrentPendingWetness)
    {
        return false;
    }

    OutGravityFlowStrength = Receiver.GetGravityFlowStrength();
    OutLocalGravityDirection = FVector::DownVector;
    bOutUseGravityBias = OutGravityFlowStrength > 0.0f;
    if (!bOutUseGravityBias)
    {
        for (const int32 VertexIndex : Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue)
        {
            if (Receiver.GetGravityFlowStrengthForVertex(VertexIndex) > 0.0f)
            {
                bOutUseGravityBias = true;
                break;
            }
        }

        for (int32 QueueIndex = Receiver.SimulationState->CurrentPendingWetnessReadIndex;
             !bOutUseGravityBias &&
             QueueIndex < Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num();
             ++QueueIndex)
        {
            const int32 VertexIndex = Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue[QueueIndex];
            if (Receiver.GetGravityFlowStrengthForVertex(VertexIndex) > 0.0f)
            {
                bOutUseGravityBias = true;
                break;
            }
        }
    }

    bOutCanSpread =
        Receiver.RuntimeData->bHasNeighborGraph &&
        Receiver.RuntimeData->NeighborRanges.Num() == Receiver.SimulationState->AbsorbedWetnessPerVertex.Num();

    if (bOutUseGravityBias)
    {
        bOutUseGravityBias = EnsureCachedSkinnedPositions(Receiver);
        if (bOutUseGravityBias && Receiver.TargetSkeletalMesh)
        {
            OutLocalGravityDirection = Receiver.TargetSkeletalMesh
                                           ->GetComponentTransform()
                                           .InverseTransformVectorNoScale(FVector::DownVector)
                                           .GetSafeNormal();
        }
    }

    OutSpreadAlpha = FMath::Clamp(
        EffectiveSpreadRatePerSecond * Receiver.WetnessSettings->WetnessUpdateInterval,
        0.0f,
        1.0f);

    return true;
}

void FWetSimulationStage::SnapshotPendingWetnessForCurrentUpdate(FWetSimulationStageArgs& Receiver)
{
    if (Receiver.SimulationState->CurrentPendingWetnessReadIndex <
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num())
    {
        return;
    }

    Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
    Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
    Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
    Swap(Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue, Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue);
    Receiver.SimulationState->CurrentPendingWetnessAmounts.Reserve(Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num());

    for (const int32 VertexIndex : Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue)
    {
        float PendingWater = 0.0f;
        if (Receiver.SimulationState->UpdatingPendingWetnessAmounts.IsValidIndex(VertexIndex))
        {
            PendingWater = Receiver.SimulationState->UpdatingPendingWetnessAmounts[VertexIndex];
            Receiver.SimulationState->UpdatingPendingWetnessAmounts[VertexIndex] = 0.0f;
        }

        Receiver.SimulationState->CurrentPendingWetnessAmounts.Add(PendingWater);

        if (Receiver.SimulationState->bPendingWetnessQueued.IsValidIndex(VertexIndex))
        {
            Receiver.SimulationState->bPendingWetnessQueued[VertexIndex] = false;
        }
    }
}

int32 FWetSimulationStage::ProcessCurrentPendingWetness(FWetSimulationStageArgs& Receiver, bool& bDirty, const float SpreadAlpha, const float GravityFlowStrength, const FVector& LocalGravityDirection, const bool bUseGravityBias, const bool bCanSpread)
{
    DWC_PROFILE_SCOPE(DWC_Simulation_ProcessCurrentPendingWetness);

    (void)SpreadAlpha;
    (void)GravityFlowStrength;

    int32 QueueReadIndex = Receiver.SimulationState->CurrentPendingWetnessReadIndex;
    int32 ProcessedVertices = 0;

    while (QueueReadIndex < Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num() &&
           ProcessedVertices < Receiver.WetnessSettings->MaxPendingWetnessVerticesPerUpdate)
    {
        const int32 VertexIndex = Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue[QueueReadIndex++];
        const int32 CurrentAmountIndex = QueueReadIndex - 1;
        ++ProcessedVertices;

        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData ||
            !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
            !Receiver.SimulationState->CurrentPendingWetnessAmounts.IsValidIndex(CurrentAmountIndex))
        {
            continue;
        }

        const float PendingWater = Receiver.SimulationState->CurrentPendingWetnessAmounts[CurrentAmountIndex];

        if (PendingWater <= Receiver.WetnessSettings->MinPendingWetnessAmount)
        {
            continue;
        }

        const float SafeImmediateAbsorptionFraction = FMath::Clamp(
            Receiver.WetnessSettings->CapillaryImmediateAbsorptionFraction,
            0.0f,
            Receiver.WetnessSettings->MaxWetness);

        const float DesiredAbsorption = PendingWater * SafeImmediateAbsorptionFraction;
        const float AbsorbedWetness = AbsorbWetnessAtVertex(Receiver, VertexIndex, DesiredAbsorption, bDirty);
        const float OverflowWetness = FMath::Max(0.0f, DesiredAbsorption - AbsorbedWetness); // MaxStored�??�어???�수?��? 못하�??�온 ??
        const float CapillaryWetness = FMath::Max(0.0f, PendingWater - DesiredAbsorption);   // MaxStored�??��????�았지�??�수?�이 ?�려???�수?��? 못한 ??
        const float SpreadableWetness = CapillaryWetness + OverflowWetness;
        const float VertexSpreadAlpha = FMath::Clamp(
            Receiver.GetSpreadRatePerSecondForVertex(VertexIndex) * Receiver.WetnessSettings->WetnessUpdateInterval,
            0.0f,
            1.0f);
        const float VertexGravityFlowStrength = Receiver.GetGravityFlowStrengthForVertex(VertexIndex);

        if (!bCanSpread ||
            VertexSpreadAlpha <= 0.0f ||
            SpreadableWetness <= Receiver.WetnessSettings->MinPendingWetnessAmount)
        {
            continue;
        }

        SpreadPendingWetnessToNeighbors(Receiver,
                                        VertexIndex,
                                        SpreadableWetness,
                                        VertexSpreadAlpha,
                                        VertexGravityFlowStrength,
                                        LocalGravityDirection,
                                        bUseGravityBias && VertexGravityFlowStrength > 0.0f);
    }

    return QueueReadIndex;
}

void FWetSimulationStage::SpreadPendingWetnessToNeighbors(FWetSimulationStageArgs& Receiver, const int32 VertexIndex, const float SpreadableWetness, const float SpreadAlpha, const float GravityFlowStrength, const FVector& LocalGravityDirection, const bool bUseGravityBias)
{
    if (!Receiver.RuntimeData ||
        !Receiver.RuntimeData->IsVertexWettable(VertexIndex) ||
        !Receiver.RuntimeData->NeighborRanges.IsValidIndex(VertexIndex))
    {
        return;
    }

    const FWetVertexNeighborRange& NeighborRange = Receiver.RuntimeData->NeighborRanges[VertexIndex];
    if (NeighborRange.Count == 0 ||
        !NeighborRange.IsValidFor(Receiver.RuntimeData->FlatNeighborIndices))
    {
        return;
    }

    const float TotalFlowAmount = SpreadableWetness * SpreadAlpha;
    if (TotalFlowAmount <= Receiver.WetnessSettings->MinPendingWetnessAmount)
    {
        return;
    }

    const bool bCanUseGravityBias = bUseGravityBias &&
                                    Receiver.MeshSampler &&
                                    Receiver.MeshSampler->CachedSkinnedPositions.IsValidIndex(VertexIndex) &&
                                    !LocalGravityDirection.IsNearlyZero();
    const FVector SourceLocalPosition = bCanUseGravityBias
                                            ? FVector(Receiver.MeshSampler->CachedSkinnedPositions[VertexIndex])
                                            : FVector::ZeroVector;

    struct FNeighborFlow
    {
        int32 NeighborIndex = INDEX_NONE;
        float Weight = 0.0f;
    };

    TArray<FNeighborFlow, TInlineAllocator<16>> ValidNeighborFlows;
    float TotalWeight = 0.0f;

    const int32 NeighborEndOffset = NeighborRange.StartOffset + NeighborRange.Count;
    for (int32 NeighborOffset = NeighborRange.StartOffset; NeighborOffset < NeighborEndOffset; ++NeighborOffset)
    {
        const int32 NeighborIndex = Receiver.RuntimeData->FlatNeighborIndices[NeighborOffset];
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(NeighborIndex) ||
            !Receiver.RuntimeData->IsVertexWettable(NeighborIndex))
        {
            continue;
        }

        const float TargetCapacity = Receiver.WetnessSettings->MaxWetness - Receiver.SimulationState->AbsorbedWetnessPerVertex[NeighborIndex];
        if (TargetCapacity <= Receiver.WetnessSettings->MinPendingWetnessAmount)
        {
            // RefreshWetnessDryHold(Receiver, NeighborIndex);
            continue;
        }

        const float GravityBias = bCanUseGravityBias
                                      ? CalculateNeighborGravityBias(Receiver,
                                                                     SourceLocalPosition,
                                                                     NeighborIndex,
                                                                     GravityFlowStrength,
                                                                     LocalGravityDirection)
                                      : 1.0f;

        float PartBoundaryScale = 1.0f;
        if (Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex) &&
            Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(NeighborIndex) &&
            Receiver.RuntimeData->VertexWetPartIDs[VertexIndex] != Receiver.RuntimeData->VertexWetPartIDs[NeighborIndex])
        {
            PartBoundaryScale = FMath::Clamp(Receiver.WetnessSettings->CrossWetPartSpreadScale, 0.0f, 1.0f);
        }

        const float Weight = TargetCapacity * GravityBias * PartBoundaryScale;
        if (Weight <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        ValidNeighborFlows.Add({NeighborIndex, Weight});
        TotalWeight += Weight;
    }

    if (TotalWeight <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    for (const FNeighborFlow& Flow : ValidNeighborFlows)
    {
        const float FlowAmount = TotalFlowAmount * (Flow.Weight / TotalWeight);

        if (FlowAmount > Receiver.WetnessSettings->MinPendingWetnessAmount)
        {
            QueuePendingWetness(Receiver, Flow.NeighborIndex, FlowAmount);
        }
    }
}

float FWetSimulationStage::CalculateNeighborGravityBias(const FWetSimulationStageArgs& Receiver, const FVector& SourceLocalPosition, const int32 NeighborIndex, const float GravityFlowStrength, const FVector& LocalGravityDirection)
{
    if (!Receiver.MeshSampler ||
        !Receiver.MeshSampler->CachedSkinnedPositions.IsValidIndex(NeighborIndex))
    {
        return 1.0f;
    }

    const FVector TargetLocalPosition(Receiver.MeshSampler->CachedSkinnedPositions[NeighborIndex]);
    const FVector FlowDelta = TargetLocalPosition - SourceLocalPosition;
    const float   FlowLengthSquared = FlowDelta.SizeSquared();
    if (FlowLengthSquared <= SMALL_NUMBER)
    {
        return 1.0f;
    }

    const float GravityAlignment =
        FVector::DotProduct(FlowDelta, LocalGravityDirection) * FMath::InvSqrt(FlowLengthSquared);

    return FMath::Clamp(
        1.0f + GravityAlignment * GravityFlowStrength,
        0.0f,
        2.0f);
}

void FWetSimulationStage::ProcessPendingWetness(FWetSimulationStageArgs& Receiver, bool& bDirty, const float EffectiveSpreadRatePerSecond)
{
    DWC_PROFILE_SCOPE(DWC_Simulation_ProcessPendingWetness);

    float SpreadAlpha = 0.0f;
    float GravityFlowStrength = 0.0f;
    FVector LocalGravityDirection = FVector::DownVector;
    bool  bUseGravityBias = false;
    bool  bCanSpread = false;

    if (!PreparePendingWetnessProcessing(Receiver,
                                         EffectiveSpreadRatePerSecond,
                                         SpreadAlpha,
                                         GravityFlowStrength,
                                         LocalGravityDirection,
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
                                                              LocalGravityDirection,
                                                              bUseGravityBias,
                                                              bCanSpread);

    Receiver.SimulationState->CurrentPendingWetnessReadIndex = QueueReadIndex;
    if (Receiver.SimulationState->CurrentPendingWetnessReadIndex >=
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num())
    {
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
    }
}

bool FWetSimulationStage::UpdateWetness(FWetSimulationStageArgs& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Simulation_UpdateWetness);

    bool        bDirty = false;
    const float EffectiveDryRatePerSecond = Receiver.GetDryRatePerSecond();
    const float EffectiveSpreadRatePerSecond = Receiver.GetSpreadRatePerSecond();

    const bool bHasCurrentPendingWetness =
        Receiver.SimulationState->CurrentPendingWetnessReadIndex <
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Num();

    if (Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Num() > 0 ||
        bHasCurrentPendingWetness)
    {
        ProcessPendingWetness(Receiver, bDirty, EffectiveSpreadRatePerSecond);
    }
    else
    {
        ClearPendingWetness(Receiver);
    }

    DryOutWetness(Receiver, bDirty, EffectiveDryRatePerSecond);

    return bDirty;
}
