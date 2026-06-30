// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/DynamicWetReceiverInputApplicator.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverContext.h"
#include "DynamicWet/DynamicWetContactTypes.h"
#include "DynamicWet/DynamicWetReceiverRenderApplier.h"
#include "DynamicWet/DynamicWetReceiverSimulationSolver.h"
#include "DynamicWet/DynamicWetReceiverMeshSampler.h"
#include "DynamicWet/DynamicWetReceiverRuntimeData.h"
#include "DynamicWet/DynamicWetReceiverSimulationState.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"

float FDynamicWetReceiverInputApplicator::CalculateContactExposure(
    const FVector& WorldNormal,
    const FVector& Direction,
    const FVector& Normal,
    const FDynamicWetReceiverSettings& Settings)
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

void FDynamicWetReceiverInputApplicator::ApplyWetnessGlobal(FDynamicWetReceiverContext& Receiver, float Amount)
{
    if (Receiver.SimulationState.WetnessPerVertex.Num() == 0 || FMath::IsNearlyZero(Amount))
    {
        return;
    }

    const float EffectiveAmount = Amount;
    if (FMath::IsNearlyZero(EffectiveAmount))
    {
        return;
    }

    bool bDirty = false;

    for (int32 VertexIndex = 0; VertexIndex < Receiver.SimulationState.WetnessPerVertex.Num(); ++VertexIndex)
    {
        if (EffectiveAmount > 0.0f)
        {
            Receiver.SimulationSolver.QueuePendingWetness(Receiver, VertexIndex, EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex));
        }
        else
        {
            Receiver.SimulationSolver.AbsorbWetnessAtVertex(Receiver, VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }
}

void FDynamicWetReceiverInputApplicator::ApplyWetnessBelowHeight(FDynamicWetReceiverContext& Receiver, float WaterSurfaceZ, float Amount)
{
    if (!Receiver.TargetSkeletalMesh || FMath::IsNearlyZero(Amount))
    {
        return;
    }

    const float EffectiveAmount = Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !Receiver.MeshSampler.UpdateSkinnedPositions(Receiver))
    {
        return;
    }

    if (Receiver.SimulationState.WetnessPerVertex.Num() != Receiver.MeshSampler.CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder.EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler.CachedSkinnedPositions.Num());
    }

    bool bDirty = false;
    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();

    for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler.CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(Receiver.MeshSampler.CachedSkinnedPositions[VertexIndex]));

        if (WorldPosition.Z > WaterSurfaceZ)
        {
            continue;
        }

        if (EffectiveAmount > 0.0f)
        {
            Receiver.SimulationSolver.QueuePendingWetness(Receiver, VertexIndex, EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex));
        }
        else
        {
            if (Receiver.SimulationState.WetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            Receiver.SimulationSolver.AbsorbWetnessAtVertex(Receiver, VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }
}

bool FDynamicWetReceiverInputApplicator::ApplyWetSurface(FDynamicWetReceiverContext& Receiver, const FDWCWetSurfaceData& SurfaceData, const float Amount, const bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh ||
        FMath::IsNearlyZero(Amount) ||
        SurfaceData.SizeX < 2 ||
        SurfaceData.SizeY < 2 ||
        !SurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const int32 ExpectedSampleCount = SurfaceData.SizeX * SurfaceData.SizeY;
    if (SurfaceData.SurfaceZ.Num() != ExpectedSampleCount ||
        SurfaceData.Valid.Num() != ExpectedSampleCount)
    {
        return false;
    }

    const float EffectiveAmount = Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !Receiver.MeshSampler.UpdateSkinnedPositions(Receiver))
    {
        return false;
    }

    if (Receiver.SimulationState.WetnessPerVertex.Num() != Receiver.MeshSampler.CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder.EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler.CachedSkinnedPositions.Num());
    }

    bool             bDirty = false;
    bool             bQueuedWetness = false;
    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();

    for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler.CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex))
        {
            continue;
        }

        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(Receiver.MeshSampler.CachedSkinnedPositions[VertexIndex]));

        float SurfaceZ = 0.0f;
        if (!QueryWetSurfaceData(SurfaceData, WorldPosition, SurfaceZ) ||
            WorldPosition.Z > SurfaceZ)
        {
            continue;
        }

        if (EffectiveAmount > 0.0f)
        {
            const float VertexAmount = EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex);
            Receiver.SimulationSolver.QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            if (Receiver.SimulationState.WetnessPerVertex[VertexIndex] <= 0.0f)
            {
                continue;
            }

            Receiver.SimulationSolver.AbsorbWetnessAtVertex(Receiver, VertexIndex, EffectiveAmount, bDirty);
        }
    }

    if (bDirty && bApplyMaterial)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }

    return bDirty || bQueuedWetness;
}

bool FDynamicWetReceiverInputApplicator::ApplyRainWetness(FDynamicWetReceiverContext& Receiver, const FVector& RainDirection, float Amount, bool bApplyMaterial)
{
    FDWCWetContact Contact;
    Contact.Amount = Amount;
    Contact.Location = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->Bounds.Origin : FVector::ZeroVector;
    Contact.Radius = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->Bounds.SphereRadius : 0.0f;
    Contact.Direction = RainDirection;
    Contact.Normal = -RainDirection.GetSafeNormal();

    return ApplyWetContact(Receiver, Contact, bApplyMaterial);
}

bool FDynamicWetReceiverInputApplicator::ApplyWetRain(
    FDynamicWetReceiverContext& Receiver,
    const FDWCWetRainData& RainData,
    const bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh ||
        FMath::IsNearlyZero(RainData.Amount) ||
        RainData.SampleCount <= 0)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!Receiver.RuntimeDataBuilder.GetLODRenderData(Receiver, 0, LODData) || !LODData)
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        return false;
    }

    if (Receiver.SimulationState.WetnessPerVertex.Num() != VertexCount)
    {
        Receiver.RuntimeDataBuilder.EnsureWetnessBufferSize(Receiver, VertexCount);
    }

    const bool bWantsNormalExposure = RainData.bUseNormalExposure && !RainData.Direction.IsNearlyZero();
    const bool bHasSkinnedNormals =
        bWantsNormalExposure &&
        RainData.bUseSkinnedNormalsForExposure &&
        Receiver.MeshSampler.UpdateSkinnedNormals(Receiver);

    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    const FVector SafeDirection =
        RainData.Direction.IsNearlyZero()
            ? FVector::DownVector
            : RainData.Direction.GetSafeNormal();
    const FVector SafeNormal = -SafeDirection;
    const int32 SamplesToProcess = FMath::Min(RainData.SampleCount, VertexCount);

    FRandomStream RandomStream;
    if (RainData.bOverrideRandomSeed)
    {
        RandomStream.Initialize(RainData.RandomSeed);
    }
    else
    {
        RandomStream.GenerateNewSeed();
    }

    bool bDirty = false;
    bool bQueuedWetness = false;

    auto ApplyRainToVertex = [&](const int32 VertexIndex)
    {
        if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex))
        {
            return;
        }

        if ((RainData.Amount > 0.0f && Receiver.SimulationState.WetnessPerVertex[VertexIndex] >= Receiver.WetnessSettings.MaxStoredWetness) ||
            (RainData.Amount < 0.0f && Receiver.SimulationState.WetnessPerVertex[VertexIndex] <= 0.0f))
        {
            return;
        }

        float Exposure = 1.0f;
        if (bWantsNormalExposure)
        {
            FVector WorldNormal = FVector::ZeroVector;
            if (bHasSkinnedNormals && Receiver.MeshSampler.CachedSkinnedNormals.IsValidIndex(VertexIndex))
            {
                WorldNormal =
                    ComponentTransform.TransformVectorNoScale(
                                          FVector(Receiver.MeshSampler.CachedSkinnedNormals[VertexIndex]))
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

            Exposure = FDynamicWetReceiverInputApplicator::CalculateContactExposure(
                WorldNormal,
                SafeDirection,
                SafeNormal,
                Receiver.WetnessSettings);
            if (Exposure <= KINDA_SMALL_NUMBER)
            {
                return;
            }
        }

        const float VertexAmount =
            (RainData.Amount > 0.0f
                 ? RainData.Amount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex)
                 : RainData.Amount) *
            Exposure;

        if (VertexAmount > 0.0f)
        {
            Receiver.SimulationSolver.QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            Receiver.SimulationSolver.AbsorbWetnessAtVertex(Receiver, VertexIndex, VertexAmount, bDirty);
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

        int32 Attempts = 0;
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

    if (bDirty && bApplyMaterial)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }

    return bDirty || bQueuedWetness;
}

bool FDynamicWetReceiverInputApplicator::ApplyWetContact(
    FDynamicWetReceiverContext& Receiver,
    const FDWCWetContact& Contact,
    bool bApplyMaterial)
{
    if (!Receiver.TargetSkeletalMesh || FMath::IsNearlyZero(Contact.Amount))
    {
        return false;
    }

    const float EffectiveAmount = Contact.Amount;

    if (FMath::IsNearlyZero(EffectiveAmount) || !Receiver.MeshSampler.UpdateSkinnedPositions(Receiver))
    {
        return false;
    }

    const bool bHasNormals = Receiver.MeshSampler.UpdateSkinnedNormals(Receiver);

    if (Receiver.SimulationState.WetnessPerVertex.Num() != Receiver.MeshSampler.CachedSkinnedPositions.Num())
    {
        Receiver.RuntimeDataBuilder.EnsureWetnessBufferSize(Receiver, Receiver.MeshSampler.CachedSkinnedPositions.Num());
    }

    const FTransform ComponentTransform = Receiver.TargetSkeletalMesh->GetComponentTransform();
    const FVector SafeDirection =
        Contact.Direction.IsNearlyZero()
            ? FVector::ZeroVector
            : Contact.Direction.GetSafeNormal();
    const FVector SafeNormal =
        Contact.Normal.IsNearlyZero()
            ? FVector::ZeroVector
            : Contact.Normal.GetSafeNormal();
    const float SafeRadius = FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER);
    const float SafeRadiusSquared = SafeRadius * SafeRadius;

    bool bDirty = false;
    bool bQueuedWetness = false;

    for (int32 VertexIndex = 0; VertexIndex < Receiver.MeshSampler.CachedSkinnedPositions.Num(); ++VertexIndex)
    {
        if (!Receiver.SimulationState.WetnessPerVertex.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeDataBuilder.DoesVertexMatchBoneName(Receiver, VertexIndex, Contact.BoneName))
        {
            continue;
        }

        if ((EffectiveAmount > 0.0f && Receiver.SimulationState.WetnessPerVertex[VertexIndex] >= Receiver.WetnessSettings.MaxStoredWetness) ||
            (EffectiveAmount < 0.0f && Receiver.SimulationState.WetnessPerVertex[VertexIndex] <= 0.0f))
        {
            continue;
        }

        const FVector WorldPosition =
            ComponentTransform.TransformPosition(
                FVector(Receiver.MeshSampler.CachedSkinnedPositions[VertexIndex]));
        const float DistanceSquared = FVector::DistSquared(WorldPosition, Contact.Location);
        if (DistanceSquared > SafeRadiusSquared)
        {
            continue;
        }

        const float Distance = FMath::Sqrt(DistanceSquared);
        float Influence = 1.0f - (Distance / SafeRadius);

        if (bHasNormals && Receiver.MeshSampler.CachedSkinnedNormals.IsValidIndex(VertexIndex))
        {
            const FVector WorldNormal =
                ComponentTransform.TransformVectorNoScale(
                                      FVector(Receiver.MeshSampler.CachedSkinnedNormals[VertexIndex]))
                    .GetSafeNormal();

            if (!WorldNormal.IsNearlyZero())
            {
                Influence *= FDynamicWetReceiverInputApplicator::CalculateContactExposure(
                    WorldNormal,
                    SafeDirection,
                    SafeNormal,
                    Receiver.WetnessSettings);
            }
        }

        if (Influence <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        const float VertexAmount =
            (EffectiveAmount > 0.0f
                 ? EffectiveAmount * Receiver.GetAbsorptionMultiplierForVertex(VertexIndex)
                 : EffectiveAmount) *
            Influence;
        if (VertexAmount > 0.0f)
        {
            Receiver.SimulationSolver.QueuePendingWetness(Receiver, VertexIndex, VertexAmount);
            bQueuedWetness = true;
        }
        else
        {
            Receiver.SimulationSolver.AbsorbWetnessAtVertex(Receiver, VertexIndex, VertexAmount, bDirty);
        }
    }

    if (bDirty && bApplyMaterial)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }

    return bDirty || bQueuedWetness;
}

bool FDynamicWetReceiverInputApplicator::ApplyWetContacts(FDynamicWetReceiverContext& Receiver, const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial)
{
    bool bAppliedAny = false;

    for (const FDWCWetContact& Contact : Contacts)
    {
        bAppliedAny |= ApplyWetContact(Receiver, Contact, false);
    }

    if (bAppliedAny && bApplyMaterial)
    {
        Receiver.RenderApplier.ApplyWetnessToMaterial(Receiver);
    }

    return bAppliedAny;
}

bool FDynamicWetReceiverInputApplicator::GetWetnessWorldBounds(const FDynamicWetReceiverContext& Receiver, FBox& OutBounds)
{
    OutBounds = FBox(ForceInit);

    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    OutBounds = Receiver.TargetSkeletalMesh->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

bool FDynamicWetReceiverInputApplicator::QueryWetSurfaceData(const FDWCWetSurfaceData& SurfaceData, const FVector& WorldPosition, float& OutSurfaceZ)
{
    OutSurfaceZ = 0.0f;

    if (SurfaceData.SizeX < 2 ||
        SurfaceData.SizeY < 2 ||
        !SurfaceData.Bounds.IsValid)
    {
        return false;
    }

    const FVector BoundsMin = SurfaceData.Bounds.Min;
    const FVector BoundsMax = SurfaceData.Bounds.Max;
    const float   BoundsSizeX = BoundsMax.X - BoundsMin.X;
    const float   BoundsSizeY = BoundsMax.Y - BoundsMin.Y;

    if (BoundsSizeX <= KINDA_SMALL_NUMBER ||
        BoundsSizeY <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const float NormalizedX = FMath::Clamp((WorldPosition.X - BoundsMin.X) / BoundsSizeX, 0.0f, 1.0f);
    const float NormalizedY = FMath::Clamp((WorldPosition.Y - BoundsMin.Y) / BoundsSizeY, 0.0f, 1.0f);

    const float GridX = NormalizedX * static_cast<float>(SurfaceData.SizeX - 1);
    const float GridY = NormalizedY * static_cast<float>(SurfaceData.SizeY - 1);

    const int32 X0 = FMath::Clamp(FMath::FloorToInt(GridX), 0, SurfaceData.SizeX - 1);
    const int32 Y0 = FMath::Clamp(FMath::FloorToInt(GridY), 0, SurfaceData.SizeY - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, SurfaceData.SizeX - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, SurfaceData.SizeY - 1);

    if (!SurfaceData.IsValidSampleIndex(X0, Y0) ||
        !SurfaceData.IsValidSampleIndex(X1, Y0) ||
        !SurfaceData.IsValidSampleIndex(X0, Y1) ||
        !SurfaceData.IsValidSampleIndex(X1, Y1))
    {
        return false;
    }

    const float AlphaX = GridX - static_cast<float>(X0);
    const float AlphaY = GridY - static_cast<float>(Y0);

    const float Z00 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X0, Y0)];
    const float Z10 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X1, Y0)];
    const float Z01 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X0, Y1)];
    const float Z11 = SurfaceData.SurfaceZ[SurfaceData.GetSampleIndex(X1, Y1)];

    const float Z0 = FMath::Lerp(Z00, Z10, AlphaX);
    const float Z1 = FMath::Lerp(Z01, Z11, AlphaX);

    OutSurfaceZ = FMath::Lerp(Z0, Z1, AlphaY);
    return true;
}
