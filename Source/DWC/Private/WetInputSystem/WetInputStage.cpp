// Fill out your copyright notice in the Description page of Project Settings.

#include "WetInputSystem/WetInputStage.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/WetContactTypes.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeData/WetClothingRuntimeData.h"
#include "RuntimeData/WetRuntimeDataBuilder.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"

float FWetInputStageArgs::GetAbsorptionMultiplierForVertex(const int32 VertexIndex) const
{
    return RuntimeData && RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex)
               ? RuntimeData->VertexWetnessProfileParameters[VertexIndex].GetAbsorptionMultiplier()
               : 1.0f;
}

namespace
{
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

    struct FBoneCandidateVertexRange
    {
        int32 StartOffset = INDEX_NONE;
        int32 EndOffset = INDEX_NONE;

        bool IsValid() const
        {
            return StartOffset >= 0 && EndOffset > StartOffset;
        }
    };

    struct FResolvedBoneCandidateContact
    {
        const FDWCWetContact*     Contact = nullptr;
        FBoneCandidateVertexRange Range;
    };

    struct FWetContactEvaluationData
    {
        explicit FWetContactEvaluationData(const FDWCWetContact& InContact)
            : Contact(InContact), EffectiveAmount(InContact.Amount), SafeDirection(InContact.Direction.IsNearlyZero() ? FVector::ZeroVector : InContact.Direction.GetSafeNormal()), SafeNormal(InContact.Normal.IsNearlyZero() ? FVector::ZeroVector : InContact.Normal.GetSafeNormal()), SafeRadius(FMath::Max(InContact.Radius, KINDA_SMALL_NUMBER)), SafeRadiusSquared(SafeRadius * SafeRadius)
        {
        }

        const FDWCWetContact& Contact;
        float                 EffectiveAmount = 0.0f;
        FVector               SafeDirection = FVector::ZeroVector;
        FVector               SafeNormal = FVector::ZeroVector;
        float                 SafeRadius = KINDA_SMALL_NUMBER;
        float                 SafeRadiusSquared = KINDA_SMALL_NUMBER * KINDA_SMALL_NUMBER;
    };

    bool TryGetBoneCandidateVertexRange(
        FWetInputStageArgs&        Receiver,
        const FDWCWetContact&      Contact,
        FBoneCandidateVertexRange& OutRange)
    {
        OutRange = FBoneCandidateVertexRange();
        return Receiver.RuntimeDataBuilder->GetBoneCandidateVertexRange(
                   *Receiver.RuntimeData,
                   Receiver.TargetSkeletalMesh,
                   Contact.BoneName,
                   OutRange.StartOffset,
                   OutRange.EndOffset) &&
               OutRange.IsValid();
    }

    bool TryResolveBoneCandidateContacts(
        FWetInputStageArgs&                    Receiver,
        const TArray<FDWCWetContact>&          Contacts,
        TArray<FResolvedBoneCandidateContact>& OutResolvedContacts)
    {
        OutResolvedContacts.Reset();
        OutResolvedContacts.Reserve(Contacts.Num());

        for (const FDWCWetContact& Contact : Contacts)
        {
            if (FMath::IsNearlyZero(Contact.Amount))
            {
                continue;
            }

            FBoneCandidateVertexRange Range;
            if (!TryGetBoneCandidateVertexRange(Receiver, Contact, Range))
            {
                OutResolvedContacts.Reset();
                return false;
            }

            FResolvedBoneCandidateContact& ResolvedContact = OutResolvedContacts.AddDefaulted_GetRef();
            ResolvedContact.Contact = &Contact;
            ResolvedContact.Range = Range;
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

    bool ApplyWetContactInfluence(
        FWetInputStageArgs&              Receiver,
        const FWetContactEvaluationData& Evaluation,
        const int32                      VertexIndex,
        const float                      Influence,
        bool&                            bDirty,
        bool&                            bQueuedWetness)
    {
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex))
        {
            return false;
        }

        if ((Evaluation.EffectiveAmount > 0.0f && Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] >= Receiver.WetnessSettings->MaxWetness) ||
            (Evaluation.EffectiveAmount < 0.0f && Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f))
        {
            return false;
        }

        if (Influence <= KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const float VertexAmount =
            (Evaluation.EffectiveAmount > 0.0f
                 ? Evaluation.EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex)
                 : Evaluation.EffectiveAmount) *
            Influence;
        if (VertexAmount > 0.0f)
        {
            Receiver.SimulationStage->QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            Receiver.SimulationStage->AbsorbWetnessAtVertex(Receiver, VertexIndex, VertexAmount, bDirty);
        }

        return true;
    }

    bool ApplyPreparedWetContact(
        FWetInputStageArgs&            Receiver,
        const FDWCWetContact&          Contact,
        const FPreparedWetContactData& PreparedData,
        bool&                          bDirty,
        bool&                          bQueuedWetness)
    {
        if (FMath::IsNearlyZero(Contact.Amount))
        {
            return false;
        }

        const FWetContactEvaluationData Evaluation(Contact);

        bool bApplied = false;
        auto ApplyVertex = [&](const int32 VertexIndex, const bool bCheckBoneName)
        {
            if (!Receiver.MeshSampler->CachedSkinnedPositions.IsValidIndex(VertexIndex))
            {
                return;
            }

            if (bCheckBoneName && !Receiver.RuntimeDataBuilder->DoesVertexMatchBoneName(Receiver.TargetSkeletalMesh, VertexIndex, Contact.BoneName))
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
                WorldNormal = PreparedData.ComponentTransform.TransformVectorNoScale(FVector(Receiver.MeshSampler->CachedSkinnedNormals[VertexIndex])).GetSafeNormal();
                WorldNormalPtr = &WorldNormal;
            }

            if (!PassWetContactSurfaceFilter(Receiver, Evaluation, WorldPosition, WorldNormalPtr))
            {
                return;
            }

            ApplyWetContactNormalExposure(Receiver, Evaluation, WorldNormalPtr, Influence);

            if (ApplyWetContactInfluence(Receiver, Evaluation, VertexIndex, Influence, bDirty, bQueuedWetness))
            {
                bApplied = true;
            }
        };

        int32      CandidateStartOffset = INDEX_NONE;
        int32      CandidateEndOffset = INDEX_NONE;
        const bool bUseBoneCandidates =
            Receiver.RuntimeDataBuilder->GetBoneCandidateVertexRange(*Receiver.RuntimeData, Receiver.TargetSkeletalMesh, Contact.BoneName, CandidateStartOffset, CandidateEndOffset) && CandidateStartOffset < CandidateEndOffset;

        if (bUseBoneCandidates)
        {
            const TArray<int32>& FlatVertexIndices =
                Receiver.RuntimeData->BoneOptimizationCache.PrimaryVertexCache.FlatVertexIndices;
            for (int32 CandidateOffset = CandidateStartOffset; CandidateOffset < CandidateEndOffset; ++CandidateOffset)
            {
                if (!FlatVertexIndices.IsValidIndex(CandidateOffset))
                {
                    continue;
                }

                ApplyVertex(FlatVertexIndices[CandidateOffset], false);
            }
        }
        else
        {
            for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler->CachedSkinnedPositions.Num(); ++VertexIndex)
            {
                ApplyVertex(VertexIndex, true);
            }
        }

        return bApplied;
    }

    bool ApplyDirectSkinnedWetContact(
        FWetInputStageArgs&                 Receiver,
        const FDWCWetContact&               Contact,
        const FBoneCandidateVertexRange&    CandidateRange,
        const FDirectSkinnedWetContactData& PreparedData,
        bool&                               bDirty,
        bool&                               bQueuedWetness)
    {
        if (FMath::IsNearlyZero(Contact.Amount) || !CandidateRange.IsValid() ||
            !PreparedData.LODData || !PreparedData.SkinWeightBuffer)
        {
            return false;
        }

        const FWetContactEvaluationData Evaluation(Contact);

        bool                 bApplied = false;
        const TArray<int32>& FlatVertexIndices =
            Receiver.RuntimeData->BoneOptimizationCache.PrimaryVertexCache.FlatVertexIndices;

        for (int32 CandidateOffset = CandidateRange.StartOffset; CandidateOffset < CandidateRange.EndOffset; ++CandidateOffset)
        {
            if (!FlatVertexIndices.IsValidIndex(CandidateOffset))
            {
                continue;
            }

            const int32 VertexIndex = FlatVertexIndices[CandidateOffset];
            FVector3f   SkinnedPosition = FVector3f::ZeroVector;
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

            if (ApplyWetContactInfluence(Receiver, Evaluation, VertexIndex, Influence, bDirty, bQueuedWetness))
            {
                bApplied = true;
            }
        }

        return bApplied;
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

    for (int32 VertexIndex = 0; VertexIndex < Receiver.SimulationState->AbsorbedWetnessPerVertex.Num(); ++VertexIndex)
    {
        if (EffectiveAmount > 0.0f)
        {
            Receiver.SimulationStage->QueuePendingWetness(Receiver, VertexIndex, EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex));
        }
        else
        {
            Receiver.SimulationStage->AbsorbWetnessAtVertex(Receiver, VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty)
    {
    }
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

    if (FMath::IsNearlyZero(EffectiveAmount) || !Receiver.MeshSampler->UpdateSkinnedPositions(Receiver.TargetSkeletalMesh, Receiver.LODIndex))
    {
        return false;
    }

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != Receiver.MeshSampler->CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
    }

    bool             bDirty = false;
    bool             bQueuedWetness = false;
    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();

    for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler->CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex))
        {
            continue;
        }

        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(Receiver.MeshSampler->CachedSkinnedPositions[VertexIndex]));

        float SurfaceZ = 0.0f;
        if (!QueryWaterSurfaceData(WaterSurfaceData, WorldPosition, SurfaceZ) ||
            WorldPosition.Z > SurfaceZ)
        {
            continue;
        }

        if (EffectiveAmount > 0.0f)
        {
            const float VertexAmount = EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex);
            Receiver.SimulationStage->QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            if (Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            Receiver.SimulationStage->AbsorbWetnessAtVertex(Receiver, VertexIndex, EffectiveAmount, bDirty);
        }
    }

    return bDirty || bQueuedWetness;
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
    if (!Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData) || !LODData)
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
        Receiver.MeshSampler->UpdateSkinnedNormals(Receiver.TargetSkeletalMesh, Receiver.LODIndex);

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

    auto ApplyRainToVertex = [&](const int32 VertexIndex)
    {
        if (!Receiver.SimulationState->AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex))
        {
            return;
        }

        if ((AreaData.Amount > 0.0f && Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] >= Receiver.WetnessSettings->MaxWetness) ||
            (AreaData.Amount < 0.0f && Receiver.SimulationState->AbsorbedWetnessPerVertex[VertexIndex] <= 0.0f))
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

        const float VertexAmount =
            (AreaData.Amount > 0.0f
                 ? AreaData.Amount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex)
                 : AreaData.Amount) *
            Exposure;

        if (VertexAmount > 0.0f)
        {
            Receiver.SimulationStage->QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            Receiver.SimulationStage->AbsorbWetnessAtVertex(Receiver, VertexIndex, VertexAmount, bDirty);
        }
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

    return bDirty || bQueuedWetness;
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

    const float               EffectiveAmount = Contact.Amount;
    FBoneCandidateVertexRange CandidateRange;
    if (!FMath::IsNearlyZero(EffectiveAmount) &&
        TryGetBoneCandidateVertexRange(Receiver, Contact, CandidateRange))
    {
        FSkeletalMeshLODRenderData*    LODData = nullptr;
        const FSkinWeightVertexBuffer* SkinWeightBuffer = Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(0);
        if (SkinWeightBuffer &&
            Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData) &&
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
                CandidateRange,
                PreparedData,
                bDirty,
                bQueuedWetness);

            if (bDirty && bApplyMaterial)
            {
            }

            return bDirty || bQueuedWetness;
        }
    }

    if (FMath::IsNearlyZero(EffectiveAmount) || !Receiver.MeshSampler->UpdateSkinnedPositions(Receiver.TargetSkeletalMesh, Receiver.LODIndex))
    {
        return false;
    }

    const bool bHasNormals = Receiver.MeshSampler->UpdateSkinnedNormals(Receiver.TargetSkeletalMesh, Receiver.LODIndex);

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

    TArray<FResolvedBoneCandidateContact> ResolvedCandidateContacts;
    if (TryResolveBoneCandidateContacts(Receiver, Contacts, ResolvedCandidateContacts))
    {
        FSkeletalMeshLODRenderData*    LODData = nullptr;
        const FSkinWeightVertexBuffer* SkinWeightBuffer = Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(0);
        if (SkinWeightBuffer &&
            Receiver.RuntimeDataBuilder->GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData) &&
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
            for (const FResolvedBoneCandidateContact& ResolvedContact : ResolvedCandidateContacts)
            {
                if (!ResolvedContact.Contact)
                {
                    continue;
                }

                ApplyDirectSkinnedWetContact(
                    Receiver,
                    *ResolvedContact.Contact,
                    ResolvedContact.Range,
                    PreparedData,
                    bDirty,
                    bQueuedWetness);
            }

            if ((bDirty || bQueuedWetness) && bApplyMaterial)
            {
            }

            return bDirty || bQueuedWetness;
        }
    }

    if (!Receiver.MeshSampler->UpdateSkinnedPositions(Receiver.TargetSkeletalMesh, Receiver.LODIndex))
    {
        return false;
    }

    const bool bHasNormals = Receiver.MeshSampler->UpdateSkinnedNormals(Receiver.TargetSkeletalMesh, Receiver.LODIndex);

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != Receiver.MeshSampler->CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler->CachedSkinnedPositions.Num());
    }

    FPreparedWetContactData PreparedData;
    PreparedData.ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    PreparedData.bHasNormals = bHasNormals;

    bool bDirty = false;
    bool bQueuedWetness = false;
    for (const FDWCWetContact& Contact : Contacts)
    {
        ApplyPreparedWetContact(
            Receiver,
            Contact,
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
