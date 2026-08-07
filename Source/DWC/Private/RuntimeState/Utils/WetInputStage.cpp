//Copyright 2026 Team Tofunut. All Rights Reserved.
// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeState/Utils/WetInputStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/WetContactTypes.h"
#include "RuntimeState/Utils/WetSimulationStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "Async/ParallelFor.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
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
            FWetSimulationStage::QueuePendingWetness(Receiver, VertexIndex, Amount);
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

    constexpr int32 WetAreaNormalExposureCandidateMultiplier = 3;
    constexpr int32 WetAreaNormalExposureMinCandidateCount = 128;
    constexpr float WetAreaNormalExposurePickPower = 2.0f;

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
        float Influence = 0.0f;
        const FWetnessProfileParameters* Profile = nullptr;
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

        if (!Receiver.RuntimeData)
        {
            OutResolvedContact.bUseFullVertexFallback = true;
            OutResolvedContact.FallbackReason = TEXT("RuntimeData is unavailable.");
            return true;
        }

        OutResolvedContact.bUseFullVertexFallback =
            !FWetRuntimeDataBuilder::GetBoneCandidateVertexIndices(
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
        OutSample.Profile = Receiver.RuntimeData->GetWetnessProfileParameters(VertexIndex);
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
                const float RemovedAmount = FWetSimulationStage::AbsorbWetnessAtVertex(
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

        bool bApplied = false;
        for (const FWaterInputVertexSample& Sample : Samples)
        {
            const float IncomingAmount = InputAmount *
                (bNormalizePositiveInfluence ? Sample.Influence / TotalInfluence : Sample.Influence);
            if (RouteAbsorbedWater(Receiver, Sample, IncomingAmount) > 0.0f)
            {
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
                !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex))
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
                Receiver.RuntimeData->GetWetnessProfileParameters(VertexIndex) == nullptr)
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
            Receiver,
            Evaluation.EffectiveAmount,
            Samples,
            true,
            bDirty,
            bQueuedWetness);
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
                !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex))
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
                Receiver.RuntimeData->GetWetnessProfileParameters(VertexIndex) == nullptr)
            {
                continue;
            }

            InitializeWaterSample(
                Receiver, VertexIndex, Influence, Samples.AddDefaulted_GetRef());
        }

        return ApplyWaterSamples(
            Receiver,
            Evaluation.EffectiveAmount,
            Samples,
            true,
            bDirty,
            bQueuedWetness);
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

bool FWetInputStage::CanApplyWetAreaToVertex(
    const FWetInputStageArgs& Args,
    const FDWCWetAreaData& AreaData,
    const int32 VertexIndex)
{
    if (!Args.SimulationState ||
        !Args.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex) ||
        !Args.RuntimeData ||
        !Args.RuntimeData->SupportsAbsorbedWetness(VertexIndex))
    {
        return false;
    }

    if (AreaData.Amount < 0.0f &&
        Args.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f)
    {
        return false;
    }

    return true;
}

float FWetInputStage::CalculateWetAreaRawExposure(
    const FWetInputStageArgs& Args,
    const FSkeletalMeshLODRenderData& LODData,
    const FTransform& ComponentTransform,
    const FVector& SafeDirection,
    const FVector& SafeNormal,
    const bool bWantsNormalExposure,
    const bool bHasSkinnedNormals,
    const int32 VertexIndex)
{
    if (!bWantsNormalExposure)
    {
        return 1.0f;
    }

    FVector WorldNormal = FVector::ZeroVector;
    if (bHasSkinnedNormals &&
        Args.MeshSampler &&
        Args.MeshSampler->CachedSkinnedNormals.IsValidIndex(VertexIndex))
    {
        WorldNormal =
            ComponentTransform.TransformVectorNoScale(
                                  FVector(Args.MeshSampler->CachedSkinnedNormals[VertexIndex]))
                .GetSafeNormal();
    }
    else if (VertexIndex < static_cast<int32>(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices()))
    {
        WorldNormal =
            ComponentTransform.TransformVectorNoScale(
                                  FVector(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)))
                .GetSafeNormal();
    }

    if (WorldNormal.IsNearlyZero() || !Args.WetnessSettings)
    {
        return 0.0f;
    }

    return CalculateContactExposure(
        WorldNormal,
        SafeDirection,
        SafeNormal,
        *Args.WetnessSettings);
}

int32 FWetInputStage::SelectWetAreaCandidateIndex(
    const TArray<FWetAreaCandidate>& Candidates,
    FRandomStream& RandomStream)
{
    float TotalPickWeight = 0.0f;
    for (const FWetAreaCandidate& Candidate : Candidates)
    {
        TotalPickWeight += Candidate.PickWeight;
    }

    if (TotalPickWeight <= KINDA_SMALL_NUMBER)
    {
        return Candidates.IsEmpty() ? INDEX_NONE : RandomStream.RandRange(0, Candidates.Num() - 1);
    }

    float PickValue = RandomStream.FRandRange(0.0f, TotalPickWeight);
    for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
    {
        PickValue -= Candidates[CandidateIndex].PickWeight;
        if (PickValue <= 0.0f)
        {
            return CandidateIndex;
        }
    }

    return Candidates.Num() - 1;
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
            !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex))
        {
            continue;
        }

        InitializeWaterSample(Receiver, VertexIndex, 1.0f, Samples.AddDefaulted_GetRef());
    }

    ApplyWaterSamples(
        Receiver,
        EffectiveAmount,
        Samples,
        false,
        bDirty,
        bQueuedWetness);
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
        FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
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
                !Receiver.RuntimeData->SupportsAbsorbedWetness(VertexIndex))
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
        Receiver,
        EffectiveAmount,
        Samples,
        false,
        bDirty,
        bQueuedWetness);
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
    if (!FWetRuntimeDataBuilder::GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) || !LODData)
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
        FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, VertexCount);
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

    if (!bWantsNormalExposure)
    {
        if (SamplesToProcess == VertexCount)
        {
            for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                if (CanApplyWetAreaToVertex(Receiver, AreaData, VertexIndex))
                {
                    InitializeWaterSample(Receiver, VertexIndex, 1.0f, Samples.AddDefaulted_GetRef());
                }
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
                if (CanApplyWetAreaToVertex(Receiver, AreaData, VertexIndex))
                {
                    InitializeWaterSample(Receiver, VertexIndex, 1.0f, Samples.AddDefaulted_GetRef());
                }
            }
        }
    }
    else
    {
        const int32 CandidateCount =
            FMath::Min(
                VertexCount,
                FMath::Max(
                    SamplesToProcess * WetAreaNormalExposureCandidateMultiplier,
                    WetAreaNormalExposureMinCandidateCount));

        TSet<int32> CandidateVertexIndices;
        CandidateVertexIndices.Reserve(CandidateCount);

        if (CandidateCount == VertexCount)
        {
            for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
            {
                CandidateVertexIndices.Add(VertexIndex);
            }
        }
        else
        {
            int32       Attempts = 0;
            const int32 MaxAttempts = CandidateCount * 8;
            while (CandidateVertexIndices.Num() < CandidateCount && Attempts < MaxAttempts)
            {
                ++Attempts;
                CandidateVertexIndices.Add(RandomStream.RandRange(0, VertexCount - 1));
            }
        }

        TArray<FWetAreaCandidate> Candidates;
        Candidates.Reserve(CandidateVertexIndices.Num());
        for (const int32 VertexIndex : CandidateVertexIndices)
        {
            if (!CanApplyWetAreaToVertex(Receiver, AreaData, VertexIndex))
            {
                continue;
            }

            const float RawExposure = CalculateWetAreaRawExposure(
                Receiver,
                *LODData,
                ComponentTransform,
                SafeDirection,
                SafeNormal,
                bWantsNormalExposure,
                bHasSkinnedNormals,
                VertexIndex);
            const float MinInfluence = Receiver.WetnessSettings
                                           ? FMath::Clamp(Receiver.WetnessSettings->RainExposureMinInfluence, 0.0f, 1.0f)
                                           : 0.05f;
            const float EffectiveExposure = FMath::Clamp(RawExposure, MinInfluence, 1.0f);
            Candidates.Add({
                VertexIndex,
                EffectiveExposure,
                FMath::Pow(EffectiveExposure, WetAreaNormalExposurePickPower)});
        }

        const int32 PickCount = FMath::Min(SamplesToProcess, Candidates.Num());
        for (int32 PickIndex = 0; PickIndex < PickCount; ++PickIndex)
        {
            const int32 SelectedCandidateIndex = SelectWetAreaCandidateIndex(Candidates, RandomStream);
            if (SelectedCandidateIndex == INDEX_NONE)
            {
                break;
            }

            const FWetAreaCandidate SelectedCandidate = Candidates[SelectedCandidateIndex];
            InitializeWaterSample(
                Receiver,
                SelectedCandidate.VertexIndex,
                SelectedCandidate.Exposure,
                Samples.AddDefaulted_GetRef());
            Candidates.RemoveAtSwap(SelectedCandidateIndex, 1, EAllowShrinking::No);
        }
    }

    return ApplyWaterSamples(
        Receiver,
        AreaData.Amount,
        Samples,
        false,
        bDirty,
        bQueuedWetness);
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
            FWetRuntimeDataBuilder::GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) &&
            LODData &&
            Receiver.MeshSampler->UpdateSkinningMatrices(Receiver.TargetSkeletalMesh))
        {
            const int32 VertexCount = LODData->GetNumVertices();
            if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
            {
                FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, VertexCount);
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
        FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
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
            FWetRuntimeDataBuilder::GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData) &&
            LODData &&
            Receiver.MeshSampler->UpdateSkinningMatrices(Receiver.TargetSkeletalMesh))
        {
            const int32 VertexCount = LODData->GetNumVertices();
            if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
            {
                FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, VertexCount);
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
        FWetRuntimeDataBuilder::EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
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
