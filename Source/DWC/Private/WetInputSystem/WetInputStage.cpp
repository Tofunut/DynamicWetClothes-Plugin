// Fill out your copyright notice in the Description page of Project Settings.

#include "WetInputSystem/WetInputStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/WetContactTypes.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "Async/ParallelFor.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/WetRuntimeDataBuilder.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationSettings.h"
#include "DataAssets/WetClothingAsset.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"
#include "Utility/DWCLog.h"
#include "Utility/DWCProfiling.h"
#include "Misc/ScopeLock.h"

namespace
{
    void QueuePositiveWetness(FWetInputStageArgs& Receiver, int32 VertexIndex, float Amount)
    {
        if (Amount > 0.0f)
        {
            Receiver.SimulationStage->QueuePendingWetness(Receiver, VertexIndex, Amount);
        }
    }
    struct FPreparedWetContactData
    {
        FTransform ComponentTransform;
        bool       bHasNormals = false;
    };

    struct FDirectSkinnedWetContactData
    {
        FTransform                     ComponentTransform;
        FSkeletalMeshLODRenderData*    LODData = nullptr;
        const FSkinWeightVertexBuffer* SkinWeightBuffer = nullptr;
    };

    struct FWetSurfaceVertexHit
    {
        int32 VertexIndex = INDEX_NONE;
        float Amount = 0.0f;
    };

    bool RequestAsyncSkinning(FWetInputStageArgs& Receiver, const bool bComputePositions, const bool bComputeNormals)
    {
        if (!Receiver.RequestAsyncSkinning)
        {
            return false;
        }

        const bool bRequested = Receiver.RequestAsyncSkinning(bComputePositions, bComputeNormals);
        Receiver.bAsyncSkinningRequested |= bRequested;
        return bRequested;
    }

    bool HasCachedSkinnedPositions(const FWetInputStageArgs& Receiver)
    {
        return Receiver.MeshSampler != nullptr &&
               Receiver.SimulationState != nullptr &&
               Receiver.MeshSampler->CachedSkinnedPositions.Num() == Receiver.SimulationState->AbsorbedWetnessPerVertex.Num();
    }

    bool HasCachedSkinnedNormals(const FWetInputStageArgs& Receiver)
    {
        return Receiver.MeshSampler != nullptr &&
               Receiver.SimulationState != nullptr &&
               Receiver.MeshSampler->CachedSkinnedNormals.Num() == Receiver.SimulationState->AbsorbedWetnessPerVertex.Num();
    }

    bool EnsureCachedSkinnedPositions(FWetInputStageArgs& Receiver)
    {
        if (HasCachedSkinnedPositions(Receiver))
        {
            return true;
        }

        return false;
    }

    bool EnsureCachedSkinnedNormals(FWetInputStageArgs& Receiver)
    {
        if (HasCachedSkinnedNormals(Receiver))
        {
            return true;
        }

        return false;
    }

    struct FResolvedBoneCandidateContact
    {
        const FDWCWetContact* Contact = nullptr;
        TArray<int32>         CandidateVertexIndices;
        bool                  bUseFullVertexFallback = false;
        FString               FallbackReason;
    };

    struct FWetContactEvaluationData
    {
        explicit FWetContactEvaluationData(const FDWCWetContact& InContact)
            : Contact(InContact),
              EffectiveAmount(InContact.Amount),
              SafeDirection(InContact.Direction.IsNearlyZero() ? FVector::ZeroVector : InContact.Direction.GetSafeNormal()),
              SafeNormal(InContact.Normal.IsNearlyZero() ? FVector::ZeroVector : InContact.Normal.GetSafeNormal()),
              SafeRadius(FMath::Max(InContact.Radius, KINDA_SMALL_NUMBER)),
              SafeRadiusSquared(SafeRadius * SafeRadius)
        {
        }

        const FDWCWetContact& Contact;
        float                 EffectiveAmount = 0.0f;
        FVector               SafeDirection = FVector::ZeroVector;
        FVector               SafeNormal = FVector::ZeroVector;
        float                 SafeRadius = KINDA_SMALL_NUMBER;
        float                 SafeRadiusSquared = KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER;
    };

    struct FWaterInputVertexSample
    {
        int32 VertexIndex = INDEX_NONE;
        int32 MaterialSlotIndex = INDEX_NONE;
        FVector2f SurfaceUV = FVector2f::ZeroVector;
        float Influence = 0.0f;
        const FWetnessProfileParameters* Profile = nullptr;
    };

    struct FSurfaceWaterInputAccumulator
    {
        float TotalSurfaceAmount = 0.0f;
        float BestInfluence = -1.0f;
        FVector2f BestUV = FVector2f::ZeroVector;
        FSurfaceWaterProfileParameters Profile;
        bool bHasProfile = false;
    };

    void LogFullVertexFallback(
        const FWetInputStageArgs& Receiver,
        const FDWCWetContact&     Contact,
        const FString&            Reason)
    {
        FString EffectiveReason = Reason;
        if (EffectiveReason.IsEmpty())
        {
            EffectiveReason = TEXT("Unknown bone-cache failure.");
        }
        const FString WarningKey = FString::Printf(
            TEXT("%p|%p|%s|%s"),
            static_cast<const void*>(Receiver.OwnerForLogs),
            static_cast<const void*>(Receiver.TargetSkeletalMesh),
            *Contact.BoneName.ToString(),
            *EffectiveReason);

        // Niagara and batched contacts can hit the same fallback every frame.
        // Keep the required warning without flooding the log for the same cause.
        static FCriticalSection LoggedFallbackKeysGuard;
        static TSet<FString>    LoggedFallbackKeys;
        {
            FScopeLock Lock(&LoggedFallbackKeysGuard);
            if (LoggedFallbackKeys.Contains(WarningKey))
            {
                return;
            }
            LoggedFallbackKeys.Add(WarningKey);
        }

        UE_LOG(
            LogDWC,
            Warning,
            TEXT("WetInputStage: Bone-cache lookup fell back to a full LOD vertex traversal on %s (%s). HitBone=%s. Reason: %s"),
            *GetNameSafe(Receiver.OwnerForLogs),
            *GetNameSafe(Receiver.TargetSkeletalMesh),
            *Contact.BoneName.ToString(),
            *EffectiveReason);
    }

    bool ResolveBoneCandidateContact(
        FWetInputStageArgs&             Receiver,
        const FDWCWetContact&           Contact,
        FResolvedBoneCandidateContact&  OutResolvedContact)
    {
        OutResolvedContact = FResolvedBoneCandidateContact();
        OutResolvedContact.Contact = &Contact;

        if (!Receiver.RuntimeData || !Receiver.RuntimeDataBuilder)
        {
            OutResolvedContact.bUseFullVertexFallback = true;
            OutResolvedContact.FallbackReason = TEXT("RuntimeData or RuntimeDataBuilder is unavailable.");
            return true;
        }

        OutResolvedContact.bUseFullVertexFallback =
            !Receiver.RuntimeDataBuilder->GetBoneCandidateVertexIndices(
                *Receiver.RuntimeData,
                Receiver.TargetSkeletalMesh,
                Contact.BoneName,
                OutResolvedContact.CandidateVertexIndices,
                &OutResolvedContact.FallbackReason,
                Receiver.bRequireFullVertexTraversal);
        return true;
    }

    bool ResolveBoneCandidateContacts(
        FWetInputStageArgs&                    Receiver,
        const TArray<FDWCWetContact>&          Contacts,
        TArray<FResolvedBoneCandidateContact>& OutResolvedContacts,
        bool&                                  bOutAllContactsUseCache)
    {
        OutResolvedContacts.Reset();
        OutResolvedContacts.Reserve(Contacts.Num());
        bOutAllContactsUseCache = true;

        for (const FDWCWetContact& Contact : Contacts)
        {
            if (FMath::IsNearlyZero(Contact.Amount))
            {
                continue;
            }

            FResolvedBoneCandidateContact& ResolvedContact = OutResolvedContacts.AddDefaulted_GetRef();
            ResolveBoneCandidateContact(Receiver, Contact, ResolvedContact);
            bOutAllContactsUseCache &= !ResolvedContact.bUseFullVertexFallback;
        }

        return !OutResolvedContacts.IsEmpty();
    }

    bool CalculateWetContactBaseInfluence(
        const FWetContactEvaluationData& Evaluation,
        const FVector&                   WorldPosition,
        float&                           OutInfluence)
    {
        const float DistanceSquared = FVector::DistSquared(WorldPosition, Evaluation.Contact.Location);
        if (DistanceSquared > Evaluation.SafeRadiusSquared)
        {
            return false;
        }

        const float Distance = FMath::Sqrt(DistanceSquared);
        OutInfluence = 1.0f - (Distance / Evaluation.SafeRadius);
        return OutInfluence > KINDA_SMALL_NUMBER;
    }

    void ApplyWetContactNormalExposure(
        const FWetInputStageArgs&        Receiver,
        const FWetContactEvaluationData& Evaluation,
        const FVector*                   WorldNormal,
        float&                           InOutInfluence)
    {
        if (!WorldNormal || WorldNormal->IsNearlyZero())
        {
            return;
        }

        InOutInfluence *= FWetInputStage::CalculateContactExposure(
            *WorldNormal,
            Evaluation.SafeDirection,
            Evaluation.SafeNormal,
            *Receiver.WetnessSettings);
    }

    bool PassWetContactSurfaceFilter(
        const FWetInputStageArgs&        Receiver,
        const FWetContactEvaluationData& Evaluation,
        const FVector&                   WorldPosition,
        const FVector*                   WorldNormal)
    {
        if (Evaluation.SafeNormal.IsNearlyZero())
        {
            return true;
        }

        if (WorldNormal && !WorldNormal->IsNearlyZero())
        {
            const float NormalExposureMin =
                FMath::Min(Receiver.WetnessSettings->RainExposureMin, Receiver.WetnessSettings->RainExposureMax);
            if (FVector::DotProduct(*WorldNormal, Evaluation.SafeNormal) < NormalExposureMin)
            {
                return false;
            }
        }

        const float BackfaceDepth =
            FVector::DotProduct(Evaluation.Contact.Location - WorldPosition, Evaluation.SafeNormal);
        const float BackfaceDepthTolerance = FMath::Max(
            Receiver.WetnessSettings->WetContactBackfaceDepthTolerance,
            Evaluation.SafeRadius * Receiver.WetnessSettings->WetContactBackfaceDepthRadiusScale);

        return BackfaceDepth <= BackfaceDepthTolerance;
    }

    float RouteAbsorbedWater(
        FWetInputStageArgs& Receiver,
        const FWaterInputVertexSample& Sample,
        const float IncomingAmount)
    {
        if (!Sample.Profile || !Receiver.RuntimeData->SupportsAbsorbedWetness(Sample.VertexIndex))
        {
            return 0.0f;
        }

        // Preserve the server simulation contract: queue the complete absorbed-channel
        // request so the simulation stage can retain local capacity and propagate the
        // remainder as capillary/overflow wetness. Clamping here would silently discard
        // the water that is supposed to drive spreading from saturated vertices.
        const float RequestedAmount = IncomingAmount * Sample.Profile->GetAbsorptionMultiplier();
        if (RequestedAmount <= Receiver.WetnessSettings->MinPendingWetnessAmount)
        {
            return 0.0f;
        }

        QueuePositiveWetness(Receiver, Sample.VertexIndex, RequestedAmount);
        return RequestedAmount;
    }

    void InitializeWaterSample(
        const FWetInputStageArgs& Receiver,
        const int32 VertexIndex,
        const float Influence,
        FWaterInputVertexSample& OutSample)
    {
        OutSample.VertexIndex = VertexIndex;
        OutSample.Influence = Influence;
        OutSample.Profile = Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
            ? &Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex]
            : nullptr;
        if (Receiver.RuntimeData->SupportsSurfaceWater(VertexIndex))
        {
            Receiver.RuntimeData->TryGetSurfaceWaterBinding(
                VertexIndex, OutSample.MaterialSlotIndex, OutSample.SurfaceUV);
        }
    }

    bool ApplyWaterSamples(
        FWetInputStageArgs& Receiver,
        const float InputAmount,
        const TArray<FWaterInputVertexSample>& Samples,
        const bool bNormalizePositiveInfluence,
        bool& bDirty,
        bool& bQueuedWetness)
    {
        if (Samples.IsEmpty())
        {
            return false;
        }

        if (InputAmount < 0.0f)
        {
            bool bApplied = false;
            for (const FWaterInputVertexSample& Sample : Samples)
            {
                if (!Receiver.RuntimeData->SupportsAbsorbedWetness(Sample.VertexIndex))
                {
                    continue;
                }
                const float RemovedAmount = Receiver.SimulationStage->AbsorbWetnessAtVertex(
                    Receiver, Sample.VertexIndex, InputAmount * Sample.Influence, bDirty);
                bApplied |= !FMath::IsNearlyZero(RemovedAmount);
            }
            return bApplied;
        }

        float TotalInfluence = 0.0f;
        for (const FWaterInputVertexSample& Sample : Samples)
        {
            TotalInfluence += Sample.Influence;
        }
        if (TotalInfluence <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        TMap<int32, FSurfaceWaterInputAccumulator> SurfaceAccumulators;
        bool bApplied = false;
        for (const FWaterInputVertexSample& Sample : Samples)
        {
            //Calculate How Much Water will absorbed
            const float IncomingAmount = InputAmount *
                (bNormalizePositiveInfluence ? Sample.Influence / TotalInfluence : Sample.Influence);
            const float ActualAbsorbedAmount = RouteAbsorbedWater(Receiver, Sample, IncomingAmount);
            if (ActualAbsorbedAmount > 0.0f)
            {
                bQueuedWetness = true;
                bApplied = true;
            }

            if (!Sample.Profile || Sample.MaterialSlotIndex == INDEX_NONE ||
                !Receiver.RuntimeData->SupportsSurfaceWater(Sample.VertexIndex))
            {
                continue;
            }
            
            // Fraction alone routes water between absorbed and surface channels.
            // Absorption Rate remains the legacy absorbed-wetness response multiplier
            // and must not change the amount assigned to the surface channel.
            const FSurfaceWaterProfileParameters& Surface = Sample.Profile->SurfaceWater;
            const float RejectedAmount = IncomingAmount * Sample.Profile->GetRejectedWaterFraction();
            const float SurfaceAmount = RejectedAmount * FMath::Clamp(Surface.SurfaceRepresentationFraction, 0.0f, 1.0f);
            if (SurfaceAmount <= 0.0f)
            {
                continue;
            }
            //TODO : Maybe this will be bad for concise expression
            FSurfaceWaterInputAccumulator& Accumulator = SurfaceAccumulators.FindOrAdd(Sample.MaterialSlotIndex);
            Accumulator.TotalSurfaceAmount += SurfaceAmount;
            if (Sample.Influence > Accumulator.BestInfluence)
            {
                Accumulator.BestInfluence = Sample.Influence;
                Accumulator.BestUV = Sample.SurfaceUV;
                Accumulator.Profile = Surface;
                Accumulator.bHasProfile = true;
            }
        }

        if (!Receiver.SurfaceWaterSettings || !Receiver.SurfaceWaterSettings->bEnabled ||
            !Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            return bApplied;
        }

        static FRandomStream FallbackRandomStream(0x445743);
        FRandomStream& RandomStream = Receiver.SurfaceWaterRandomStream
            ? *Receiver.SurfaceWaterRandomStream
            : FallbackRandomStream;
        // For each material slot, use the highest-influence sample's UV and profile
        // as the representative stamp data, and use the accumulated surface amount
        // to queue droplet and flow stamps.
        for (const TPair<int32, FSurfaceWaterInputAccumulator>& Pair : SurfaceAccumulators)
        {
            const FSurfaceWaterInputAccumulator& Accumulator = Pair.Value;
            TUniquePtr<FSurfaceWaterSimulationState>* StatePtr = Receiver.SurfaceWaterStatesByMaterialSlot->Find(Pair.Key);
            if (!Accumulator.bHasProfile || !StatePtr || !StatePtr->IsValid())
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& Surface = Accumulator.Profile;
            if (RandomStream.FRand() < FMath::Clamp(Surface.DropletSpawnProbability, 0.0f, 1.0f))
            {
                (*StatePtr)->QueueDropletStamp(
                    Accumulator.BestUV,
                    Accumulator.TotalSurfaceAmount * FMath::Max(0.0f, Surface.DropletIntensityMultiplier),
                    Surface.DropletRadiusPixels,
                    Surface.DropletLifetimeSeconds);
                bQueuedWetness = true;
                bApplied = true;
            }

            if (Accumulator.TotalSurfaceAmount >= FMath::Max(0.0f, Surface.MinimumFlowSurfaceAmount) &&
                RandomStream.FRand() < FMath::Clamp(Surface.FlowSpawnProbability, 0.0f, 1.0f))
            {
                (*StatePtr)->QueueFlowStamp(
                    Accumulator.BestUV,
                    Accumulator.TotalSurfaceAmount * FMath::Max(0.0f, Surface.FlowIntensityMultiplier),
                    Surface.FlowWidthPixels,
                    Surface.FlowLengthPixels,
                    Surface.FlowLifetimeSeconds);
                bQueuedWetness = true;
                bApplied = true;
            }
        }
        return bApplied;
    }

    bool ApplyPreparedWetContact(
        FWetInputStageArgs&            Receiver,
        const FDWCWetContact&          Contact,
        const TArray<int32>*           CandidateVertexIndices,
        const FPreparedWetContactData& PreparedData,
        bool&                          bDirty,
        bool&                          bQueuedWetness)
    {
        if (FMath::IsNearlyZero(Contact.Amount))
        {
            return false;
        }

        const FWetContactEvaluationData Evaluation(Contact);

        TArray<FWaterInputVertexSample> Samples;
        auto ApplyVertex = [&](const int32 VertexIndex)
        {
            if (!Receiver.MeshSampler->CachedSkinnedPositions.IsValidIndex(VertexIndex) ||
                !Receiver.RuntimeData ||
                (Evaluation.EffectiveAmount > 0.0f
                     ? !Receiver.RuntimeData->SupportsWaterContact(VertexIndex)
                     : !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex)))
            {
                return;
            }

            const FVector WorldPosition =
                PreparedData.ComponentTransform.TransformPosition(
                    FVector(Receiver.MeshSampler->CachedSkinnedPositions[VertexIndex]));

            float Influence = 0.0f;
            if (!CalculateWetContactBaseInfluence(Evaluation, WorldPosition, Influence))
            {
                return;
            }

            FVector        WorldNormal = FVector::ZeroVector;
            const FVector* WorldNormalPtr = nullptr;
            if (PreparedData.bHasNormals && Receiver.MeshSampler->CachedSkinnedNormals.IsValidIndex(VertexIndex))
            {
                WorldNormal = PreparedData.ComponentTransform.TransformVectorNoScale(
                    FVector(Receiver.MeshSampler->CachedSkinnedNormals[VertexIndex])).GetSafeNormal();
                WorldNormalPtr = &WorldNormal;
            }

            if (!PassWetContactSurfaceFilter(Receiver, Evaluation, WorldPosition, WorldNormalPtr))
            {
                return;
            }

            ApplyWetContactNormalExposure(Receiver, Evaluation, WorldNormalPtr, Influence);
            if (Influence <= KINDA_SMALL_NUMBER ||
                !Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
            {
                return;
            }

            InitializeWaterSample(
                Receiver, VertexIndex, Influence, Samples.AddDefaulted_GetRef());
        };

        if (CandidateVertexIndices != nullptr)
        {
            for (const int32 VertexIndex : *CandidateVertexIndices)
            {
                ApplyVertex(VertexIndex);
            }
        }
        else
        {
            // This is the only full-vertex fallback pass. A cached candidate pass
            // that finds no matching spatial vertex never reaches this branch.
            for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler->CachedSkinnedPositions.Num(); ++VertexIndex)
            {
                ApplyVertex(VertexIndex);
            }
        }

        return ApplyWaterSamples(
            Receiver, Evaluation.EffectiveAmount, Samples, true, bDirty, bQueuedWetness);
    }

    bool ApplyDirectSkinnedWetContact(
        FWetInputStageArgs&                 Receiver,
        const FDWCWetContact&               Contact,
        const TArray<int32>&                CandidateVertexIndices,
        const FDirectSkinnedWetContactData& PreparedData,
        bool&                               bDirty,
        bool&                               bQueuedWetness)
    {
        if (FMath::IsNearlyZero(Contact.Amount) || CandidateVertexIndices.IsEmpty() ||
            !PreparedData.LODData || !PreparedData.SkinWeightBuffer)
        {
            return false;
        }

        const FWetContactEvaluationData Evaluation(Contact);
        TArray<FWaterInputVertexSample> Samples;
        for (const int32 VertexIndex : CandidateVertexIndices)
        {
            if (!Receiver.RuntimeData ||
                (Evaluation.EffectiveAmount > 0.0f
                     ? !Receiver.RuntimeData->SupportsWaterContact(VertexIndex)
                     : !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex)))
            {
                continue;
            }

            FVector3f SkinnedPosition = FVector3f::ZeroVector;
            if (!Receiver.MeshSampler->ComputeSkinnedPosition(
                    *PreparedData.LODData,
                    *PreparedData.SkinWeightBuffer,
                    VertexIndex,
                    SkinnedPosition))
            {
                continue;
            }

            const FVector WorldPosition = PreparedData.ComponentTransform.TransformPosition(FVector(SkinnedPosition));

            float Influence = 0.0f;
            if (!CalculateWetContactBaseInfluence(Evaluation, WorldPosition, Influence))
            {
                continue;
            }

            FVector        WorldNormal = FVector::ZeroVector;
            const FVector* WorldNormalPtr = nullptr;
            FVector3f      SkinnedNormal = FVector3f::ZeroVector;
            if (Receiver.MeshSampler->ComputeSkinnedNormal(
                    *PreparedData.LODData,
                    *PreparedData.SkinWeightBuffer,
                    VertexIndex,
                    SkinnedNormal))
            {
                WorldNormal = PreparedData.ComponentTransform.TransformVectorNoScale(FVector(SkinnedNormal)).GetSafeNormal();
                WorldNormalPtr = &WorldNormal;
            }

            if (!PassWetContactSurfaceFilter(Receiver, Evaluation, WorldPosition, WorldNormalPtr))
            {
                continue;
            }

            ApplyWetContactNormalExposure(Receiver, Evaluation, WorldNormalPtr, Influence);
            if (Influence <= KINDA_SMALL_NUMBER ||
                !Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
            {
                continue;
            }

            InitializeWaterSample(
                Receiver, VertexIndex, Influence, Samples.AddDefaulted_GetRef());
        }

        return ApplyWaterSamples(
            Receiver, Evaluation.EffectiveAmount, Samples, true, bDirty, bQueuedWetness);
    }
} // namespace

float FWetInputStage::CalculateContactExposure(
    const FVector&              WorldNormal,
    const FVector&              Direction,
    const FVector&              Normal,
    const FWetClothingSettings& Settings)
{
    float Exposure = 1.0f;

    if (!Direction.IsNearlyZero())
    {
        const float Facing = FVector::DotProduct(WorldNormal, -Direction.GetSafeNormal());
        Exposure *= FMath::SmoothStep(
            FMath::Min(Settings.RainExposureMin, Settings.RainExposureMax - KINDA_SMALL_NUMBER),
            FMath::Max(Settings.RainExposureMax, Settings.RainExposureMin + KINDA_SMALL_NUMBER),
            Facing);
    }

    if (!Normal.IsNearlyZero())
    {
        Exposure *= FMath::Clamp(FVector::DotProduct(WorldNormal, Normal.GetSafeNormal()), 0.0f, 1.0f);
    }

    return Exposure;
}

void FWetInputStage::ApplyWetAll(FWetInputStageArgs& Receiver, float Amount)
{
    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() == 0 || FMath::IsNearlyZero(Amount))
    {
        return;
    }

    const float EffectiveAmount = Amount;
    if (FMath::IsNearlyZero(EffectiveAmount))
    {
        return;
    }

    bool bDirty = false;
    bool bQueuedWetness = false;
    TArray<FWaterInputVertexSample> Samples;
    Samples.Reserve(Receiver.SimulationState->AbsorbedWetnessPerVertex.Num());

    for (int32 VertexIndex = 0; VertexIndex < Receiver.SimulationState->AbsorbedWetnessPerVertex.Num(); ++VertexIndex)
    {
        if (!Receiver.RuntimeData ||
            (EffectiveAmount > 0.0f
                 ? !Receiver.RuntimeData->SupportsWaterContact(VertexIndex)
                 : !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex)))
        {
            continue;
        }

        InitializeWaterSample(Receiver, VertexIndex, 1.0f, Samples.AddDefaulted_GetRef());
    }

    ApplyWaterSamples(Receiver, EffectiveAmount, Samples, false, bDirty, bQueuedWetness);
}

bool FWetInputStage::ApplyWetSurface(FWetInputStageArgs& Receiver, const FDWCWaterSurfaceData& WaterSurfaceData, const float Amount, const bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh ||
        FMath::IsNearlyZero(Amount) ||
        WaterSurfaceData.SizeX < 2 ||
        WaterSurfaceData.SizeY < 2 ||
        !WaterSurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const int32 ExpectedSampleCount = WaterSurfaceData.SizeX * WaterSurfaceData.SizeY;
    if (WaterSurfaceData.SurfaceZ.Num() != ExpectedSampleCount ||
        WaterSurfaceData.Valid.Num() != ExpectedSampleCount)
    {
        return false;
    }

    const float EffectiveAmount = Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !EnsureCachedSkinnedPositions(Receiver))
    {
        return false;
    }

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != Receiver.MeshSampler->CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
    }

    bool             bDirty = false;
    bool             bQueuedWetness = false;
    TArray<FWaterInputVertexSample> Samples;
    Samples.Reserve(Receiver.MeshSampler->CachedSkinnedPositions.Num());
    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    const int32      VertexCount = Receiver.MeshSampler->CachedSkinnedPositions.Num();

    constexpr int32 ChunkVertexCount = 512;
    const int32 ChunkCount = FMath::DivideAndRoundUp(VertexCount, ChunkVertexCount);
    TArray<TArray<FWetSurfaceVertexHit>> ChunkHits;
    ChunkHits.SetNum(ChunkCount);

    ParallelFor(ChunkCount, [&Receiver, &WaterSurfaceData, EffectiveAmount, ComponentTransform, &ChunkHits, VertexCount](const int32 ChunkIndex)
    {
        const int32 BeginVertexIndex = ChunkIndex * ChunkVertexCount;
        const int32 EndVertexIndex = FMath::Min(BeginVertexIndex + ChunkVertexCount, VertexCount);
        TArray<FWetSurfaceVertexHit>& LocalHits = ChunkHits[ChunkIndex];

        for (int32 VertexIndex = BeginVertexIndex; VertexIndex < EndVertexIndex; ++VertexIndex)
        {
            if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
                !Receiver.RuntimeData ||
                (EffectiveAmount > 0.0f
                     ? !Receiver.RuntimeData->SupportsWaterContact(VertexIndex)
                     : !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex)))
            {
                continue;
            }

            const FVector WorldPosition =
                ComponentTransform.TransformPosition(
                    FVector(Receiver.MeshSampler->CachedSkinnedPositions[VertexIndex]));

            float SurfaceZ = 0.0f;
            if (!FWetInputStage::QueryWaterSurfaceData(WaterSurfaceData, WorldPosition, SurfaceZ) ||
                WorldPosition.Z > SurfaceZ)
            {
                continue;
            }

            if (EffectiveAmount < 0.0f &&
                Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            LocalHits.Add({VertexIndex, EffectiveAmount});
        }
    });

    for (const TArray<FWetSurfaceVertexHit>& LocalHits : ChunkHits)
    {
        for (const FWetSurfaceVertexHit& Hit : LocalHits)
        {
            InitializeWaterSample(Receiver, Hit.VertexIndex, 1.0f, Samples.AddDefaulted_GetRef());
        }
    }

    return ApplyWaterSamples(
        Receiver, EffectiveAmount, Samples, false, bDirty, bQueuedWetness);
}

bool FWetInputStage::ApplyWetArea(FWetInputStageArgs&    Receiver,
                                  const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh ||
        FMath::IsNearlyZero(AreaData.Amount) ||
        AreaData.SampleCount <= 0)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (!Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) || !LODData)
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        return false;
    }

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, VertexCount);
    }

    const bool bWantsNormalExposure = AreaData.bUseNormalExposure && !AreaData.Direction.IsNearlyZero();
    const bool bHasSkinnedNormals =
        bWantsNormalExposure &&
        AreaData.bUseSkinnedNormalsForExposure &&
        EnsureCachedSkinnedNormals(Receiver);

    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    const FVector    SafeDirection =
        AreaData.Direction.IsNearlyZero()
               ? FVector::DownVector
               : AreaData.Direction.GetSafeNormal();
    const FVector SafeNormal = -SafeDirection;
    const int32   SamplesToProcess = FMath::Min(AreaData.SampleCount, VertexCount);

    FRandomStream RandomStream;
    if (AreaData.bOverrideRandomSeed)
    {
        RandomStream.Initialize(AreaData.RandomSeed);
    }
    else
    {
        RandomStream.GenerateNewSeed();
    }

    bool bDirty = false;
    bool bQueuedWetness = false;
    TArray<FWaterInputVertexSample> Samples;
    Samples.Reserve(SamplesToProcess);

    auto ApplyRainToVertex = [&](const int32 VertexIndex)
    {
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData ||
            (AreaData.Amount > 0.0f
                 ? !Receiver.RuntimeData->SupportsWaterContact(VertexIndex)
                 : !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex)))
        {
            return;
        }

        if (AreaData.Amount < 0.0f &&
            Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f)
        {
            return;
        }

        float Exposure = 1.0f;
        if (bWantsNormalExposure)
        {
            FVector WorldNormal = FVector::ZeroVector;
            if (bHasSkinnedNormals && Receiver.MeshSampler->CachedSkinnedNormals.IsValidIndex(VertexIndex))
            {
                WorldNormal =
                    ComponentTransform.TransformVectorNoScale(
                                          FVector(Receiver.MeshSampler->CachedSkinnedNormals[VertexIndex]))
                        .GetSafeNormal();
            }
            else if (VertexIndex < static_cast<int32>(LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices()))
            {
                WorldNormal =
                    ComponentTransform.TransformVectorNoScale(
                                          FVector(LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)))
                        .GetSafeNormal();
            }

            if (WorldNormal.IsNearlyZero())
            {
                return;
            }

            Exposure = FWetInputStage::CalculateContactExposure(
                WorldNormal,
                SafeDirection,
                SafeNormal,
                *Receiver.WetnessSettings);
            if (Exposure <= KINDA_SMALL_NUMBER)
            {
                return;
            }
        }

        InitializeWaterSample(Receiver, VertexIndex, Exposure, Samples.AddDefaulted_GetRef());
    };

    if (SamplesToProcess == VertexCount)
    {
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            ApplyRainToVertex(VertexIndex);
        }
    }
    else
    {
        TSet<int32> SelectedVertexIndices;
        SelectedVertexIndices.Reserve(SamplesToProcess);

        int32       Attempts = 0;
        const int32 MaxAttempts = SamplesToProcess * 8;
        while (SelectedVertexIndices.Num() < SamplesToProcess && Attempts < MaxAttempts)
        {
            ++Attempts;
            SelectedVertexIndices.Add(RandomStream.RandRange(0, VertexCount - 1));
        }

        for (const int32 VertexIndex : SelectedVertexIndices)
        {
            ApplyRainToVertex(VertexIndex);
        }
    }

    return ApplyWaterSamples(
        Receiver, AreaData.Amount, Samples, false, bDirty, bQueuedWetness);
}

bool FWetInputStage::ApplyWetContact(
    FWetInputStageArgs&   Receiver,
    const FDWCWetContact& Contact,
    bool                  bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh || FMath::IsNearlyZero(Contact.Amount))
    {
        return false;
    }

    FResolvedBoneCandidateContact ResolvedContact;
    ResolveBoneCandidateContact(Receiver, Contact, ResolvedContact);

    if (!ResolvedContact.bUseFullVertexFallback)
    {
        FSkeletalMeshLODRenderData*    LODData = nullptr;
        constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
        const FSkinWeightVertexBuffer* SkinWeightBuffer = Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(RuntimeLODIndex);
        if (SkinWeightBuffer &&
            Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) &&
            LODData &&
            Receiver.MeshSampler->UpdateSkinningMatrices(Receiver.TargetSkeletalMesh))
        {
            const int32 VertexCount = LODData->GetNumVertices();
            if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
            {
                Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, VertexCount);
            }

            FDirectSkinnedWetContactData PreparedData;
            PreparedData.ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
            PreparedData.LODData = LODData;
            PreparedData.SkinWeightBuffer = SkinWeightBuffer;

            bool bDirty = false;
            bool bQueuedWetness = false;
            ApplyDirectSkinnedWetContact(
                Receiver,
                Contact,
                ResolvedContact.CandidateVertexIndices,
                PreparedData,
                bDirty,
                bQueuedWetness);

            // Deliberately return the cached-search result. A spatial miss inside
            // valid bone candidates does not trigger a full-vertex second pass.
            return bDirty || bQueuedWetness;
        }
    }
    else
    {
        LogFullVertexFallback(Receiver, Contact, ResolvedContact.FallbackReason);
    }

    if (!EnsureCachedSkinnedPositions(Receiver))
    {
        return false;
    }

    const bool bHasNormals = EnsureCachedSkinnedNormals(Receiver);

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != Receiver.MeshSampler->CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
    }

    FPreparedWetContactData PreparedData;
    PreparedData.ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    PreparedData.bHasNormals = bHasNormals;

    bool bDirty = false;
    bool bQueuedWetness = false;
    ApplyPreparedWetContact(
        Receiver,
        Contact,
        ResolvedContact.bUseFullVertexFallback ? nullptr : &ResolvedContact.CandidateVertexIndices,
        PreparedData,
        bDirty,
        bQueuedWetness);

    return bDirty || bQueuedWetness;
}

bool FWetInputStage::ApplyWetContacts(FWetInputStageArgs& Receiver, const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh || Contacts.IsEmpty())
    {
        return false;
    }

    TArray<FResolvedBoneCandidateContact> ResolvedContacts;
    bool                                  bAllContactsUseCache = false;
    if (!ResolveBoneCandidateContacts(Receiver, Contacts, ResolvedContacts, bAllContactsUseCache))
    {
        return false;
    }

    if (bAllContactsUseCache)
    {
        FSkeletalMeshLODRenderData*    LODData = nullptr;
        constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
        const FSkinWeightVertexBuffer* SkinWeightBuffer = Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(RuntimeLODIndex);
        if (SkinWeightBuffer &&
            Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) &&
            LODData &&
            Receiver.MeshSampler->UpdateSkinningMatrices(Receiver.TargetSkeletalMesh))
        {
            const int32 VertexCount = LODData->GetNumVertices();
            if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
            {
                Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, VertexCount);
            }

            FDirectSkinnedWetContactData PreparedData;
            PreparedData.ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
            PreparedData.LODData = LODData;
            PreparedData.SkinWeightBuffer = SkinWeightBuffer;

            bool bDirty = false;
            bool bQueuedWetness = false;
            for (const FResolvedBoneCandidateContact& ResolvedContact : ResolvedContacts)
            {
                if (!ResolvedContact.Contact)
                {
                    continue;
                }

                ApplyDirectSkinnedWetContact(
                    Receiver,
                    *ResolvedContact.Contact,
                    ResolvedContact.CandidateVertexIndices,
                    PreparedData,
                    bDirty,
                    bQueuedWetness);
            }

            // No full retry is performed when every contact had a valid cache,
            // even if none of the candidate vertices passed the spatial filters.
            return bDirty || bQueuedWetness;
        }
    }

    if (!EnsureCachedSkinnedPositions(Receiver))
    {
        return false;
    }

    const bool bHasNormals = EnsureCachedSkinnedNormals(Receiver);

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != Receiver.MeshSampler->CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
    }

    FPreparedWetContactData PreparedData;
    PreparedData.ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    PreparedData.bHasNormals = bHasNormals;

    bool bDirty = false;
    bool bQueuedWetness = false;
    for (const FResolvedBoneCandidateContact& ResolvedContact : ResolvedContacts)
    {
        if (!ResolvedContact.Contact)
        {
            continue;
        }

        if (ResolvedContact.bUseFullVertexFallback)
        {
            LogFullVertexFallback(Receiver, *ResolvedContact.Contact, ResolvedContact.FallbackReason);
        }

        ApplyPreparedWetContact(
            Receiver,
            *ResolvedContact.Contact,
            ResolvedContact.bUseFullVertexFallback ? nullptr : &ResolvedContact.CandidateVertexIndices,
            PreparedData,
            bDirty,
            bQueuedWetness);
    }

    if ((bDirty || bQueuedWetness) && bApplyMaterial)
    {
    }

    return bDirty || bQueuedWetness;
}

bool FWetInputStage::GetWetnessWorldBounds(const FWetInputStageArgs& Receiver, FBox& OutBounds)
{
    OutBounds = FBox(ForceInit);

    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    OutBounds = Receiver.TargetSkeletalMesh->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

bool FWetInputStage::QueryWaterSurfaceData(const FDWCWaterSurfaceData& WaterSurfaceData, const FVector& WorldPosition, float& OutSurfaceZ)
{
    OutSurfaceZ = 0.0f;

    if (WaterSurfaceData.SizeX < 2 ||
        WaterSurfaceData.SizeY < 2 ||
        !WaterSurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const FVector BoundsMin = WaterSurfaceData.Bounds.Min;
    const FVector BoundsMax = WaterSurfaceData.Bounds.Max;
    const float   BoundsSizeX = BoundsMax.X - BoundsMin.X;
    const float   BoundsSizeY = BoundsMax.Y - BoundsMin.Y;

    if (BoundsSizeX <= KINDA_SMALL_NUMBER ||
        BoundsSizeY <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float NormalizedX = FMath::Clamp((WorldPosition.X - BoundsMin.X) / BoundsSizeX, 0.0f, 1.0f);
    const float NormalizedY = FMath::Clamp((WorldPosition.Y - BoundsMin.Y) / BoundsSizeY, 0.0f, 1.0f);

    const float GridX = NormalizedX * static_cast<float>(WaterSurfaceData.SizeX - 1);
    const float GridY = NormalizedY * static_cast<float>(WaterSurfaceData.SizeY - 1);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, WaterSurfaceData.SizeX - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, WaterSurfaceData.SizeY - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, WaterSurfaceData.SizeX - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, WaterSurfaceData.SizeY - 1);

    if (!WaterSurfaceData.IsValidSampleIndex(X0, Y0) ||
        !WaterSurfaceData.IsValidSampleIndex(X1, Y0) ||
        !WaterSurfaceData.IsValidSampleIndex(X0, Y1) ||
        !WaterSurfaceData.IsValidSampleIndex(X1, Y1))
    {
        return false;
    }

    const float AlphaX = GridX - static_cast<float>(X0);
    const float AlphaY = GridY - static_cast<float>(Y0);

    const float Z00 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X0, Y0)];
    const float Z10 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X1, Y0)];
    const float Z01 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X0, Y1)];
    const float Z11 = WaterSurfaceData.SurfaceZ[WaterSurfaceData.GetSampleIndex(X1, Y1)];

    const float Z0 = FMath::Lerp(Z00, Z10, AlphaX);
    const float Z1 = FMath::Lerp(Z01, Z11, AlphaX);

    OutSurfaceZ = FMath::Lerp(Z0, Z1, AlphaY);
    return true;
}
