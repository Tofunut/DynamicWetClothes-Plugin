#include "RuntimeState/Utils/WetApplicationStage.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GPU/DWCGPUBackend.h"
#include "Profiling/DWCStatsSubsystem.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Utility/DWCLog.h"
#include "GPU/DWCSurfaceWaterSimulationState.h"

namespace
{
    bool IsGPUWetnessMode(const EDWCSimulationMode Mode)
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU;
    }

    struct FGPUSurfaceWaterAccumulator
    {
        float TotalSurfaceAmount = 0.0f;
        float BestInfluence = -1.0f;
        FVector2f BestUV = FVector2f::ZeroVector;
        FSurfaceWaterProfileParameters Profile;
        bool bHasProfile = false;
    };

    int32 GetDominantTriangleVertexIndex(
        const FDWCGPUBakedTriangle& Triangle,
        const FVector3f&             Barycentric)
    {
        if (Barycentric.X >= Barycentric.Y && Barycentric.X >= Barycentric.Z)
        {
            return Triangle.VertexIndices.X;
        }
        if (Barycentric.Y >= Barycentric.Z)
        {
            return Triangle.VertexIndices.Y;
        }
        return Triangle.VertexIndices.Z;
    }

    bool QueueGPUSurfaceWaterStamps(
        FDWCWetMeshReceiverRuntime& Receiver,
        const TArray<FDWCResolvedSurfaceContact>& Contacts)
    {
        const UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
        if (Asset == nullptr ||
            !Receiver.GPUBackend.IsValid() ||
            !Receiver.SharedRuntimeData.IsValid() ||
            !Asset->Authored.SurfaceWaterSettings.bEnabled ||
            Contacts.IsEmpty())
        {
            return false;
        }

        const FDWCGPULODBakeData& GPUData =
            Asset->GetGPUWetMapRuntimeData(UWetClothingAsset::RuntimeSimulationLODIndex);
        TMap<int32, FGPUSurfaceWaterAccumulator> Accumulators;

        for (const FDWCResolvedSurfaceContact& Contact : Contacts)
        {
            if (Contact.Amount <= 0.0f || Contact.MaterialSlotIndex == INDEX_NONE ||
                !GPUData.Triangles.IsValidIndex(Contact.TriangleID) || Contact.ContactUV.ContainsNaN() ||
                !FMath::IsFinite(Contact.ContactUV.X) || !FMath::IsFinite(Contact.ContactUV.Y))
            {
                continue;
            }

            const FDWCGPUBakedTriangle& Triangle = GPUData.Triangles[Contact.TriangleID];
            if (Triangle.MaterialSlotIndex != Contact.MaterialSlotIndex)
            {
                continue;
            }

            const int32 ProfileVertexIndex = GetDominantTriangleVertexIndex(Triangle, Contact.Barycentric);
            const FWetnessProfileParameters* WetnessProfile =
                Receiver.SharedRuntimeData->GetWetnessProfileParameters(ProfileVertexIndex);
            if (WetnessProfile == nullptr ||
                !Receiver.SharedRuntimeData->SupportsSurfaceWater(ProfileVertexIndex))
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& SurfaceProfile = WetnessProfile->SurfaceWater;
            const float SurfaceAmount = Contact.Amount *
                FMath::Clamp(Contact.TriangleInfluence, 0.0f, 1.0f) *
                WetnessProfile->GetRejectedWaterFraction() *
                FMath::Clamp(SurfaceProfile.SurfaceRepresentationFraction, 0.0f, 1.0f);
            if (SurfaceAmount <= 0.0f)
            {
                continue;
            }

            FGPUSurfaceWaterAccumulator& Accumulator = Accumulators.FindOrAdd(Contact.MaterialSlotIndex);
            Accumulator.TotalSurfaceAmount += SurfaceAmount;
            if (Contact.TriangleInfluence > Accumulator.BestInfluence)
            {
                Accumulator.BestInfluence = Contact.TriangleInfluence;
                Accumulator.BestUV = Contact.ContactUV;
                Accumulator.Profile = SurfaceProfile;
                Accumulator.bHasProfile = true;
            }
        }

        FRandomStream& RandomStream = Receiver.SurfaceWaterRandomStream;
        TArray<FDWCSurfaceStampRequest> Requests;
        Requests.Reserve(Accumulators.Num() * 2);

        for (const TPair<int32, FGPUSurfaceWaterAccumulator>& Pair : Accumulators)
        {
            const FGPUSurfaceWaterAccumulator& Accumulator = Pair.Value;
            if (!Accumulator.bHasProfile)
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& Surface = Accumulator.Profile;
            if (Surface.bEnableDroplets &&
                RandomStream.FRand() < FMath::Clamp(Surface.DropletSpawnProbability, 0.0f, 1.0f))
            {
                FDWCSurfaceStampRequest& Request = Requests.AddDefaulted_GetRef();
                Request.Type = EDWCSurfaceStampType::Droplet;
                Request.MaterialSlotIndex = Pair.Key;
                Request.UV = Accumulator.BestUV;
                Request.HalfSizePixels = FVector2f(FMath::Max(0.5f, Surface.DropletRadiusPixels));
                Request.Amount = Accumulator.TotalSurfaceAmount *
                    FMath::Max(0.0f, Surface.DropletIntensityMultiplier);
                Request.LifetimeSeconds = FMath::Max(0.01f, Surface.DropletLifetimeSeconds);
            }

            if (Surface.bEnableRivulets &&
                Accumulator.TotalSurfaceAmount >= FMath::Max(0.0f, Surface.MinimumFlowSurfaceAmount) &&
                RandomStream.FRand() < FMath::Clamp(Surface.FlowSpawnProbability, 0.0f, 1.0f))
            {
                FDWCSurfaceStampRequest& Request = Requests.AddDefaulted_GetRef();
                Request.Type = EDWCSurfaceStampType::Rivulet;
                Request.MaterialSlotIndex = Pair.Key;
                Request.UV = Accumulator.BestUV;
                Request.HalfSizePixels = FVector2f(
                    FMath::Max(0.5f, Surface.FlowWidthPixels * 0.5f),
                    FMath::Max(0.5f, Surface.FlowLengthPixels * 0.5f));
                Request.Amount = Accumulator.TotalSurfaceAmount *
                    FMath::Max(0.0f, Surface.FlowIntensityMultiplier);
                Request.LifetimeSeconds = FMath::Max(0.01f, Surface.FlowLifetimeSeconds);
            }
        }

        return !Requests.IsEmpty() && Receiver.GPUBackend->EnqueueSurfaceStamps(Requests);
    }

}

FWetInputStageArgs FWetApplicationStage::MakeWetInputStageArgs(
    const FWetApplicationStageContext& Context,
    FDWCWetMeshReceiverRuntime&        Receiver)
{
    check(Context.WetnessSettings != nullptr);
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetInputStageArgs Args;
    Args.OwnerForLogs = Context.OwnerForLogs;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = Context.WetnessSettings;
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.SurfaceWaterStatesByMaterialSlot = &Receiver.SurfaceWaterStatesByMaterialSlot;
    Args.SurfaceWaterSettings = Receiver.WetClothingAsset.IsValid()
        ? &Receiver.WetClothingAsset->Authored.SurfaceWaterSettings
        : nullptr;
    Args.SurfaceWaterRandomStream = &Receiver.SurfaceWaterRandomStream;
    Args.MeshSampler = Receiver.MeshSampler.Get();
    return Args;
}

FWetSurfaceContactResolverArgs FWetApplicationStage::MakeWetSurfaceContactResolverArgs(
    const FWetApplicationStageContext& Context,
    FDWCWetMeshReceiverRuntime&        Receiver)
{
    check(Context.WetnessSettings != nullptr);
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetSurfaceContactResolverArgs Args;
    Args.OwnerForLogs = Context.OwnerForLogs;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = Context.WetnessSettings;
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    Args.MaxNearestSeedVertices = Context.MaxNearestSeedVertices;
    return Args;
}

void FWetApplicationStage::ApplyWetAll(FWetApplicationStageContext& Context, const float Amount)
{
    if (Context.Receivers == nullptr)
    {
        return;
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (Receiver.IsValid() && Receiver->GPUBackend.IsValid())
            {
                Receiver->GPUBackend->ApplyWetAll(Amount);
            }
        }
        return;
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        FWetInputStage::ApplyWetAll(InputArgs, Amount);
        if (Context.RequestWetRenderingUpdate)
        {
            Context.RequestWetRenderingUpdate(*Receiver);
        }
    }
}

bool FWetApplicationStage::ApplyWetContact(
    FWetApplicationStageContext& Context,
    const FDWCWetContact&        Contact,
    const bool                   bApplyMaterial)
{
    FDWCWorkloadStats::RecordWetContactsReceived(1);
    const auto RecordContactResult = [](const bool bApplied)
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(1, bApplied);
        return bApplied;
    };

    if (Context.bBatchWetContactsPerFrame)
    {
        if (Context.PendingWetContacts == nullptr)
        {
            return RecordContactResult(false);
        }

        const int32 MaxQueuedContacts = FMath::Max(1, Context.MaxBatchedWetContactsPerFrame);
        if (FMath::IsNearlyZero(Contact.Amount) || Context.PendingWetContacts->Num() >= MaxQueuedContacts)
        {
            return RecordContactResult(false);
        }

        Context.PendingWetContacts->Add(Contact);
        if (Context.bPendingWetContactsApplyMaterial != nullptr)
        {
            *Context.bPendingWetContactsApplyMaterial |= bApplyMaterial;
        }
        if (Context.SetComponentTickEnabled)
        {
            Context.SetComponentTickEnabled(true);
        }
        return true;
    }

    if (Context.Receivers == nullptr)
    {
        return RecordContactResult(false);
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid() ||
                !ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(Context, *Receiver);
            if (FWetSurfaceContactResolver::ResolveContact(ResolverArgs, Contact, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return RecordContactResult(bAnyQueued);
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid() || !ShouldReceiverConsiderContact(*Receiver, Contact))
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        const bool bChanged = FWetInputStage::ApplyWetContact(InputArgs, Contact, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial && Context.RequestWetRenderingUpdate)
            {
                Context.RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return RecordContactResult(bAnyChanged);
}

bool FWetApplicationStage::ApplyWetContacts(
    FWetApplicationStageContext& Context,
    const TArray<FDWCWetContact>& Contacts,
    const bool                  bApplyMaterial)
{
    const uint32 ContactCount = static_cast<uint32>(Contacts.Num());
    FDWCWorkloadStats::RecordWetContactsReceived(ContactCount);
    const auto RecordContactResults = [ContactCount](const bool bApplied)
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bApplied);
        return bApplied;
    };

    FlushPendingWetContacts(Context);

    if (Context.Receivers == nullptr)
    {
        return RecordContactResults(false);
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid())
            {
                continue;
            }

            TArray<FDWCWetContact> ReceiverContacts;
            ReceiverContacts.Reserve(Contacts.Num());
            for (const FDWCWetContact& Contact : Contacts)
            {
                if (ShouldReceiverConsiderContact(*Receiver, Contact))
                {
                    ReceiverContacts.Add(Contact);
                }
            }

            if (ReceiverContacts.IsEmpty())
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(Context, *Receiver);
            if (FWetSurfaceContactResolver::ResolveContacts(ResolverArgs, ReceiverContacts, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return RecordContactResults(bAnyQueued);
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        TArray<FDWCWetContact> ReceiverContacts;
        ReceiverContacts.Reserve(Contacts.Num());
        for (const FDWCWetContact& Contact : Contacts)
        {
            if (ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                ReceiverContacts.Add(Contact);
            }
        }

        if (ReceiverContacts.IsEmpty())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        const bool bChanged = FWetInputStage::ApplyWetContacts(InputArgs, ReceiverContacts, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial && Context.RequestWetRenderingUpdate)
            {
                Context.RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return RecordContactResults(bAnyChanged);
}

bool FWetApplicationStage::ApplyWetArea(
    FWetApplicationStageContext& Context,
    const FDWCWetAreaData&        AreaData,
    const bool                    bApplyMaterial)
{
    if (Context.Receivers == nullptr)
    {
        return false;
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid())
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(Context, *Receiver);
            if (FWetSurfaceContactResolver::ResolveWetArea(ResolverArgs, AreaData, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return bAnyQueued;
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        const bool bChanged = FWetInputStage::ApplyWetArea(InputArgs, AreaData, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial && Context.RequestWetRenderingUpdate)
            {
                Context.RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

bool FWetApplicationStage::ApplyWetSurface(
    FWetApplicationStageContext& Context,
    const FDWCWaterSurfaceData&   WaterSurfaceData,
    const float                   Amount,
    const bool                    bApplyMaterial)
{
    if (Context.Receivers == nullptr)
    {
        return false;
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid() ||
                !ShouldReceiverConsiderSurface(*Receiver, WaterSurfaceData))
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(Context, *Receiver);
            if (FWetSurfaceContactResolver::ResolveWaterSurface(ResolverArgs, WaterSurfaceData, Amount, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return bAnyQueued;
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid() || !ShouldReceiverConsiderSurface(*Receiver, WaterSurfaceData))
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        const bool bChanged = FWetInputStage::ApplyWetSurface(InputArgs, WaterSurfaceData, Amount, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial && Context.RequestWetRenderingUpdate)
            {
                Context.RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

bool FWetApplicationStage::FlushPendingWetContacts(FWetApplicationStageContext& Context)
{
    if (Context.PendingWetContacts == nullptr || Context.bPendingWetContactsApplyMaterial == nullptr ||
        Context.Receivers == nullptr)
    {
        return false;
    }

    if (Context.PendingWetContacts->IsEmpty())
    {
        *Context.bPendingWetContactsApplyMaterial = false;
        return false;
    }

    TArray<FDWCWetContact> ContactsToApply;
    ContactsToApply.Reserve(Context.PendingWetContacts->Num());
    Swap(ContactsToApply, *Context.PendingWetContacts);
    const uint32 ContactCount = static_cast<uint32>(ContactsToApply.Num());

    const bool bApplyMaterial = *Context.bPendingWetContactsApplyMaterial;
    *Context.bPendingWetContactsApplyMaterial = false;

    if (Context.Receivers->IsEmpty() &&
        (!Context.EnsureWetRuntimeInitialized || !Context.EnsureWetRuntimeInitialized()))
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, false);
        return false;
    }

    if (IsGPUWetnessMode(Context.SimulationMode))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid())
            {
                continue;
            }

            TArray<FDWCWetContact> ReceiverContacts;
            ReceiverContacts.Reserve(ContactsToApply.Num());
            for (const FDWCWetContact& Contact : ContactsToApply)
            {
                if (ShouldReceiverConsiderContact(*Receiver, Contact))
                {
                    ReceiverContacts.Add(Contact);
                }
            }

            if (ReceiverContacts.IsEmpty())
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(Context, *Receiver);
            if (FWetSurfaceContactResolver::ResolveContacts(ResolverArgs, ReceiverContacts, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bAnyQueued);
        return bAnyQueued;
    }

    if (Context.RequestContinuousCpuSkinningTasks)
    {
        Context.RequestContinuousCpuSkinningTasks();
    }

    bool bAnyChanged = false;
    bool bWaitingForSkinningCache = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        TArray<FDWCWetContact> ReceiverContacts;
        ReceiverContacts.Reserve(ContactsToApply.Num());
        for (const FDWCWetContact& Contact : ContactsToApply)
        {
            if (ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                ReceiverContacts.Add(Contact);
            }
        }

        if (ReceiverContacts.IsEmpty())
        {
            continue;
        }

        if (!Receiver->MeshSampler.IsValid() || !Receiver->SimulationState.IsValid() ||
            Receiver->MeshSampler->CachedSkinnedPositions.Num() != Receiver->SimulationState->AbsorbedWetnessPerVertex.Num())
        {
            bWaitingForSkinningCache = true;
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(Context, *Receiver);
        const bool bChanged = FWetInputStage::ApplyWetContacts(InputArgs, ReceiverContacts, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial && Context.RequestWetRenderingUpdate)
            {
                Context.RequestWetRenderingUpdate(*Receiver);
            }
        }
    }

    if (!bAnyChanged && bWaitingForSkinningCache)
    {
        *Context.PendingWetContacts = MoveTemp(ContactsToApply);
        *Context.bPendingWetContactsApplyMaterial |= bApplyMaterial;
        if (Context.SetComponentTickEnabled)
        {
            Context.SetComponentTickEnabled(true);
        }
        return false;
    }

    FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bAnyChanged);
    return bAnyChanged;
}

bool FWetApplicationStage::ShouldReceiverConsiderContact(
    const FDWCWetMeshReceiverRuntime& Receiver,
    const FDWCWetContact&             Contact)
{
    const USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    const FBox ReceiverBounds = Mesh->Bounds.GetBox().ExpandBy(FMath::Max(0.0f, Contact.Radius));
    return ReceiverBounds.IsValid && ReceiverBounds.IsInsideOrOn(Contact.Location);
}

bool FWetApplicationStage::ShouldReceiverConsiderSurface(
    const FDWCWetMeshReceiverRuntime& Receiver,
    const FDWCWaterSurfaceData&       WaterSurfaceData)
{
    const USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr || !WaterSurfaceData.Bounds.IsValid)
    {
        return false;
    }

    return Mesh->Bounds.GetBox().Intersect(WaterSurfaceData.Bounds);
}
