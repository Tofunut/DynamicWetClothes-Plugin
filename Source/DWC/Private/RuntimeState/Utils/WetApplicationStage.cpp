#include "RuntimeState/Utils/WetApplicationStage.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GPU/DWCGPUBackend.h"
#include "Profiling/DWCStatsSubsystem.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Utility/DWCLog.h"

namespace
{
    bool IsGPUWetnessMode(const EDWCSimulationMode Mode)
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU;
    }

    struct FGPUSurfaceWaterAccumulator
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        float TotalSurfaceAmount = 0.0f;
        float BestInfluence = -1.0f;
        FVector2f BestUV = FVector2f::ZeroVector;
        float FlowSelectionWeight = 0.0f;
        FVector2f FlowCandidateUV = FVector2f::ZeroVector;
        FVector3f FlowCandidateBarycentric = FVector3f::ZeroVector;
        int32 FlowCandidateTriangleID = INDEX_NONE;
        FSurfaceWaterProfileParameters Profile;
        float DropletRadiusScale = 1.0f;
        float Droplet2SizeScale = 1.0f;
        bool bHasProfile = false;
        bool bHasFlowCandidate = false;
    };

    struct FGPUSurfaceWaterAccumulatorKey
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 WetPartID = INDEX_NONE;
        uint16 ProfileIndex = FWetClothingRuntimeData::InvalidWetnessProfileIndex;

        bool operator==(const FGPUSurfaceWaterAccumulatorKey& Other) const
        {
            return MaterialSlotIndex == Other.MaterialSlotIndex &&
                   WetPartID == Other.WetPartID &&
                   ProfileIndex == Other.ProfileIndex;
        }

        friend uint32 GetTypeHash(const FGPUSurfaceWaterAccumulatorKey& Key)
        {
            return HashCombine(
                HashCombine(GetTypeHash(Key.MaterialSlotIndex), GetTypeHash(Key.WetPartID)),
                GetTypeHash(Key.ProfileIndex));
        }
    };

    struct FResolvedSurfaceWaterPart
    {
        const FWetClothingWetPartEntry* WetPart = nullptr;
        const FWetnessProfileParameters* WetnessProfile = nullptr;
        uint16 ProfileIndex = FWetClothingRuntimeData::InvalidWetnessProfileIndex;
    };

    FResolvedSurfaceWaterPart ResolveSurfaceWaterPartForTriangle(
        const FDWCWetMeshReceiverRuntime& Receiver,
        const FWetClothingAuthoredMaterialSlot& MaterialSlot,
        const FDWCGPUBakedTriangle& Triangle)
    {
        FResolvedSurfaceWaterPart Result;
        const UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
        if (Asset == nullptr || !Receiver.SharedRuntimeData.IsValid())
        {
            return Result;
        }

        const FWetClothingEditableWetPartData& WetPartData =
            Asset->Authored.PartData.EditableWetPartData;
        const FWetClothingWetPartEntry* WetPart =
            MaterialSlot.WetPartEntries.FindByPredicate(
                [&Triangle](const FWetClothingWetPartEntry& Candidate)
                {
                    return Candidate.WetPartID != 0 &&
                           Candidate.AssignedUVIslandIDs.Contains(Triangle.UVIslandID);
                });
        if (WetPart == nullptr)
        {
            return Result;
        }

        const int32 EffectiveProfileIndex =
            WetPartData.Profiles.IsValidIndex(WetPart->ProfileIndex)
                ? WetPart->ProfileIndex
                : 0;
        if (EffectiveProfileIndex < 0 ||
            EffectiveProfileIndex >= static_cast<int32>(FWetClothingRuntimeData::InvalidWetnessProfileIndex) ||
            !Receiver.SharedRuntimeData->WetnessProfileTable.IsValidIndex(EffectiveProfileIndex))
        {
            return Result;
        }

        Result.WetPart = WetPart;
        Result.WetnessProfile =
            &Receiver.SharedRuntimeData->WetnessProfileTable[EffectiveProfileIndex];
        Result.ProfileIndex = static_cast<uint16>(EffectiveProfileIndex);
        return Result;
    }

    FVector2f MakeIndependentFlowStampUV(
        const FDWCGPUBakedTriangle& Triangle,
        const FVector3f& ContactBarycentric,
        const float PositionSpread,
        FRandomStream& RandomStream)
    {
        FVector3f SafeContactBarycentric(
            FMath::Max(ContactBarycentric.X, 0.0f),
            FMath::Max(ContactBarycentric.Y, 0.0f),
            FMath::Max(ContactBarycentric.Z, 0.0f));
        const float ContactWeight = SafeContactBarycentric.X +
            SafeContactBarycentric.Y +
            SafeContactBarycentric.Z;
        SafeContactBarycentric = ContactWeight > KINDA_SMALL_NUMBER
            ? SafeContactBarycentric / ContactWeight
            : FVector3f(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);

        // Square-root sampling produces a uniform point inside the selected UV triangle.
        const float Root = FMath::Sqrt(RandomStream.FRand());
        const float Edge = RandomStream.FRand();
        const FVector3f RandomBarycentric(
            1.0f - Root,
            Root * (1.0f - Edge),
            Root * Edge);
        const FVector3f FlowBarycentric = FMath::Lerp(
            SafeContactBarycentric,
            RandomBarycentric,
            FMath::Clamp(PositionSpread, 0.0f, 1.0f));

        return FVector2f(
            static_cast<float>(
                Triangle.UV0.X * FlowBarycentric.X +
                Triangle.UV1.X * FlowBarycentric.Y +
                Triangle.UV2.X * FlowBarycentric.Z),
            static_cast<float>(
                Triangle.UV0.Y * FlowBarycentric.X +
                Triangle.UV1.Y * FlowBarycentric.Y +
                Triangle.UV2.Y * FlowBarycentric.Z));
    }

    bool QueueGPUSurfaceWaterStamps(
        FDWCWetMeshReceiverRuntime& Receiver,
        const TArray<FDWCResolvedSurfaceContact>& Contacts)
    {
        const UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
        if (Asset == nullptr ||
            !Receiver.GPUBackend.IsValid() ||
            !Receiver.SharedRuntimeData.IsValid() ||
            !Receiver.QualityLODState.ResolvedPolicy.bUpdateSurfaceWater ||
            !Asset->UsesSurfaceWater() ||
            Contacts.IsEmpty())
        {
            return false;
        }

        const FDWCGPULODBakeData& GPUData =
            Asset->GetGPUWetMapRuntimeData(UWetClothingAsset::RuntimeSimulationLODIndex);
        TMap<FGPUSurfaceWaterAccumulatorKey, FGPUSurfaceWaterAccumulator> Accumulators;
        FRandomStream& RandomStream = Receiver.GPUSurfaceWaterRandomStream;

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

            const FWetClothingAuthoredMaterialSlot* MaterialSlot =
                Asset->Authored.PartData.EditableWetPartData.FindMaterialSlot(Contact.MaterialSlotIndex);
            if (MaterialSlot == nullptr || !MaterialSlot->bIsWettableSlot)
            {
                continue;
            }

            const FResolvedSurfaceWaterPart ResolvedPart =
                ResolveSurfaceWaterPartForTriangle(Receiver, *MaterialSlot, Triangle);
            if (ResolvedPart.WetPart == nullptr ||
                ResolvedPart.WetnessProfile == nullptr ||
                ResolvedPart.ProfileIndex == FWetClothingRuntimeData::InvalidWetnessProfileIndex ||
                !ResolvedPart.WetnessProfile->SupportsSurfaceWater())
            {
                continue;
            }

            const FWetnessProfileParameters& WetnessProfile = *ResolvedPart.WetnessProfile;
            const FSurfaceWaterProfileParameters& SurfaceProfile = WetnessProfile.SurfaceWater;
            const float SurfaceAmount = Contact.Amount *
                FMath::Clamp(Contact.TriangleInfluence, 0.0f, 1.0f) *
                WetnessProfile.GetRejectedWaterFraction();
            if (SurfaceAmount <= 0.0f)
            {
                continue;
            }

            const FGPUSurfaceWaterAccumulatorKey AccumulatorKey{
                Contact.MaterialSlotIndex,
                ResolvedPart.WetPart->WetPartID,
                ResolvedPart.ProfileIndex};
            FGPUSurfaceWaterAccumulator& Accumulator = Accumulators.FindOrAdd(AccumulatorKey);
            Accumulator.MaterialSlotIndex = Contact.MaterialSlotIndex;
            Accumulator.TotalSurfaceAmount += SurfaceAmount;

            // Droplet2 uses a weighted reservoir sample instead of reusing Droplet1's strongest contact.
            if (SurfaceProfile.bUseSecondaryDroplets)
            {
                Accumulator.FlowSelectionWeight += SurfaceAmount;
                if (!Accumulator.bHasFlowCandidate ||
                    RandomStream.FRand() * Accumulator.FlowSelectionWeight < SurfaceAmount)
                {
                    Accumulator.FlowCandidateUV = Contact.ContactUV;
                    Accumulator.FlowCandidateBarycentric = Contact.Barycentric;
                    Accumulator.FlowCandidateTriangleID = Contact.TriangleID;
                    Accumulator.bHasFlowCandidate = true;
                }
            }

            if (Contact.TriangleInfluence > Accumulator.BestInfluence)
            {
                Accumulator.BestInfluence = Contact.TriangleInfluence;
                Accumulator.BestUV = Contact.ContactUV;
                Accumulator.Profile = SurfaceProfile;
                Accumulator.DropletRadiusScale =
                    ResolvedPart.WetPart->SurfaceWater.GetResolvedDropletStampSizeScale();
                Accumulator.Droplet2SizeScale =
                    ResolvedPart.WetPart->SurfaceWater.GetResolvedDropletFlowStampSizeScale();
                Accumulator.bHasProfile = true;
            }
        }

        TArray<FDWCSurfaceStampRequest> Requests;
        Requests.Reserve(Accumulators.Num() * 2);

        for (const TPair<FGPUSurfaceWaterAccumulatorKey, FGPUSurfaceWaterAccumulator>& Pair : Accumulators)
        {
            const FGPUSurfaceWaterAccumulator& Accumulator = Pair.Value;
            if (!Accumulator.bHasProfile)
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& Surface = Accumulator.Profile;
            if (Surface.DropletRadiusPixels > 0.0f &&
                RandomStream.FRand() < FMath::Clamp(Surface.DropletSpawnProbability, 0.0f, 1.0f))
            {
                FDWCSurfaceStampRequest& Request = Requests.AddDefaulted_GetRef();
                Request.MaterialSlotIndex = Accumulator.MaterialSlotIndex;
                Request.UV = Accumulator.BestUV;
                Request.HalfSizePixels = FVector2f(
                    FMath::Max(0.5f, Surface.DropletRadiusPixels * Accumulator.DropletRadiusScale),
                    FMath::Max(0.5f, Surface.DropletHeightPixels * Accumulator.DropletRadiusScale));
                Request.Amount = Accumulator.TotalSurfaceAmount;
                Request.bDroplet2 = false;
            }

            if (Surface.bUseSecondaryDroplets &&
                Surface.DropletFlowRadiusPixels > 0.0f &&
                Surface.DropletFlowHeightPixels > 0.0f &&
                Accumulator.bHasFlowCandidate &&
                RandomStream.FRand() < FMath::Clamp(Surface.DropletFlowSpawnProbability, 0.0f, 1.0f))
            {
                FDWCSurfaceStampRequest& Request = Requests.AddDefaulted_GetRef();
                Request.MaterialSlotIndex = Accumulator.MaterialSlotIndex;
                Request.UV = Accumulator.FlowCandidateUV;
                if (GPUData.Triangles.IsValidIndex(Accumulator.FlowCandidateTriangleID))
                {
                    Request.UV = MakeIndependentFlowStampUV(
                        GPUData.Triangles[Accumulator.FlowCandidateTriangleID],
                        Accumulator.FlowCandidateBarycentric,
                        Surface.DropletFlowSpawnPositionSpread,
                        RandomStream);
                }
                Request.HalfSizePixels = FVector2f(
                    FMath::Max(0.5f, Surface.DropletFlowRadiusPixels * Accumulator.Droplet2SizeScale),
                    FMath::Max(0.5f, Surface.DropletFlowHeightPixels * Accumulator.Droplet2SizeScale));
                Request.Amount = Accumulator.TotalSurfaceAmount;
                Request.bDroplet2 = true;
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
