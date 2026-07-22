#include "RuntimeState/Utils/WetSurfaceContactResolver.h"

#include "Components/SkeletalMeshComponent.h"
#include "Core/WetClothingSettings.h"
#include "DataAssets/WetClothingAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeState/Utils/WetInputStage.h"

namespace DWCWetSurfaceContactResolverPrivate
{
struct FNearestVertex
{
    int32 VertexIndex = INDEX_NONE;
    float DistanceSquared = TNumericLimits<float>::Max();
};

FVector3f ComputeTriangleBarycentric(
    const FVector& Point,
    const FVector& A,
    const FVector& B,
    const FVector& C)
{
    const FVector V0 = B - A;
    const FVector V1 = C - A;
    const FVector V2 = Point - A;
    const double D00 = FVector::DotProduct(V0, V0);
    const double D01 = FVector::DotProduct(V0, V1);
    const double D11 = FVector::DotProduct(V1, V1);
    const double D20 = FVector::DotProduct(V2, V0);
    const double D21 = FVector::DotProduct(V2, V1);
    const double Denominator = D00 * D11 - D01 * D01;
    if (FMath::IsNearlyZero(Denominator))
    {
        return FVector3f(1.0f, 0.0f, 0.0f);
    }

    const float WeightB = static_cast<float>((D11 * D20 - D01 * D21) / Denominator);
    const float WeightC = static_cast<float>((D00 * D21 - D01 * D20) / Denominator);
    return FVector3f(1.0f - WeightB - WeightC, WeightB, WeightC);
}

bool PassSurfaceFilter(
    const FDWCWetContact& Contact,
    const FWetClothingSettings& Settings,
    const FVector& ClosestPoint,
    const FVector& TriangleNormal)
{
    const FVector ContactNormal = Contact.Normal.GetSafeNormal();
    if (ContactNormal.IsNearlyZero())
    {
        return true;
    }

    const float NormalExposureMin = FMath::Min(Settings.RainExposureMin, Settings.RainExposureMax);
    if (!TriangleNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, ContactNormal) < NormalExposureMin)
    {
        return false;
    }

    const float BackfaceDepth = FVector::DotProduct(Contact.Location - ClosestPoint, ContactNormal);
    const float BackfaceDepthTolerance = FMath::Max(
        Settings.WetContactBackfaceDepthTolerance,
        FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER) * Settings.WetContactBackfaceDepthRadiusScale);
    return BackfaceDepth <= BackfaceDepthTolerance;
}

void AddNearestVertex(
    TArray<FNearestVertex>& InOutNearest,
    const int32 MaxCount,
    const int32 VertexIndex,
    const float DistanceSquared)
{
    if (MaxCount <= 0)
    {
        return;
    }

    if (InOutNearest.Num() < MaxCount)
    {
        InOutNearest.Add({VertexIndex, DistanceSquared});
        return;
    }

    int32 FarthestIndex = 0;
    for (int32 Index = 1; Index < InOutNearest.Num(); ++Index)
    {
        if (InOutNearest[Index].DistanceSquared > InOutNearest[FarthestIndex].DistanceSquared)
        {
            FarthestIndex = Index;
        }
    }

    if (DistanceSquared < InOutNearest[FarthestIndex].DistanceSquared)
    {
        InOutNearest[FarthestIndex] = {VertexIndex, DistanceSquared};
    }
}

float ComputeTriangleContactRadius(const FVector& ContactPoint, const FVector& P0, const FVector& P1, const FVector& P2)
{
    return FMath::Max(
        1.0f,
        FMath::Max3(
            static_cast<float>(FVector::Distance(ContactPoint, P0)),
            static_cast<float>(FVector::Distance(ContactPoint, P1)),
            static_cast<float>(FVector::Distance(ContactPoint, P2))));
}

float ComputeWetAreaSampleRadius(const FVector& P0, const FVector& P1, const FVector& P2)
{
    const float Edge01 = static_cast<float>(FVector::Distance(P0, P1));
    const float Edge12 = static_cast<float>(FVector::Distance(P1, P2));
    const float Edge20 = static_cast<float>(FVector::Distance(P2, P0));
    const float MinEdge = FMath::Min3(Edge01, Edge12, Edge20);
    const float TriangleArea = static_cast<float>(0.5 * FVector::CrossProduct(P1 - P0, P2 - P0).Size());
    const float AreaRadius = TriangleArea > SMALL_NUMBER
        ? FMath::Sqrt(TriangleArea / UE_PI)
        : MinEdge * 0.5f;

    return FMath::Max(1.0f, FMath::Min(MinEdge * 0.35f, AreaRadius * 0.75f));
}

float ResolveGPUAbsorptionMultiplier(
    const FDWCGPULODBakeData& GPUData,
    const FDWCGPUBakedTriangle& Triangle,
    const float Amount)
{
    if (Amount < 0.0f)
    {
        return 1.0f;
    }

    const FDWCGPUProfileParameters* Profile =
        GPUData.Profiles.IsValidIndex(Triangle.ProfileIndex)
            ? &GPUData.Profiles[Triangle.ProfileIndex]
            : nullptr;
    return FMath::Max(0.0f, Profile ? Profile->AbsorptionMultiplier : 1.0f);
}

FVector OrientTriangleNormalForContact(const FVector& TriangleNormal, const FDWCWetContact& Contact)
{
    if (TriangleNormal.IsNearlyZero())
    {
        return TriangleNormal;
    }

    FVector ReferenceNormal = Contact.Normal.GetSafeNormal();
    if (ReferenceNormal.IsNearlyZero() && !Contact.Direction.IsNearlyZero())
    {
        ReferenceNormal = -Contact.Direction.GetSafeNormal();
    }

    if (!ReferenceNormal.IsNearlyZero() && FVector::DotProduct(TriangleNormal, ReferenceNormal) < 0.0)
    {
        return -TriangleNormal;
    }

    return TriangleNormal;
}

void KeepPrimaryContactSurface(
    const FDWCGPULODBakeData& GPUData,
    const FDWCWetContact& Contact,
    TArray<FDWCResolvedSurfaceContact>& InOutContacts)
{
    if (InOutContacts.Num() <= 1)
    {
        return;
    }

    InOutContacts.Sort([](const FDWCResolvedSurfaceContact& A, const FDWCResolvedSurfaceContact& B)
    {
        return A.DistanceToSurface < B.DistanceToSurface;
    });

    int32 PrimaryContactIndex = 0;
    if (Contact.RenderTriangleID != INDEX_NONE)
    {
        float BestRenderHitDistance = TNumericLimits<float>::Max();
        for (int32 ContactIndex = 0; ContactIndex < InOutContacts.Num(); ++ContactIndex)
        {
            const FDWCResolvedSurfaceContact& Resolved = InOutContacts[ContactIndex];
            const FDWCGPUBakedTriangle* Triangle = GPUData.Triangles.IsValidIndex(Resolved.TriangleID)
                                                       ? &GPUData.Triangles[Resolved.TriangleID]
                                                       : nullptr;
            if (Triangle != nullptr &&
                Triangle->RenderTriangleID == Contact.RenderTriangleID &&
                Resolved.DistanceToSurface < BestRenderHitDistance)
            {
                PrimaryContactIndex = ContactIndex;
                BestRenderHitDistance = Resolved.DistanceToSurface;
            }
        }
    }

    const FDWCResolvedSurfaceContact& PrimaryContact = InOutContacts[PrimaryContactIndex];
    const FDWCGPUBakedTriangle* PrimaryTriangle = GPUData.Triangles.IsValidIndex(PrimaryContact.TriangleID)
                                                     ? &GPUData.Triangles[PrimaryContact.TriangleID]
                                                     : nullptr;
    if (PrimaryTriangle == nullptr)
    {
        return;
    }

    const int32 PrimaryMaterialSlot = PrimaryTriangle->MaterialSlotIndex;
    const int32 PrimaryUVIsland = PrimaryTriangle->UVIslandID;
    const FVector SharedContactWorldPosition = PrimaryContact.ClosestWorldPosition;

    InOutContacts.RemoveAll(
        [&GPUData, PrimaryMaterialSlot, PrimaryUVIsland](const FDWCResolvedSurfaceContact& Candidate)
        {
            const FDWCGPUBakedTriangle* CandidateTriangle = GPUData.Triangles.IsValidIndex(Candidate.TriangleID)
                                                               ? &GPUData.Triangles[Candidate.TriangleID]
                                                               : nullptr;
            return CandidateTriangle == nullptr ||
                   CandidateTriangle->MaterialSlotIndex != PrimaryMaterialSlot ||
                   CandidateTriangle->UVIslandID != PrimaryUVIsland;
        });

    // One input event is one surface stamp. All triangles retained for that stamp
    // must evaluate their texels against the same center, otherwise each triangle
    // creates a separate falloff and triangle boundaries become visible.
    for (FDWCResolvedSurfaceContact& Candidate : InOutContacts)
    {
        Candidate.ContactWorldPosition = SharedContactWorldPosition;
        Candidate.DistanceToSurface = 0.0f;
    }
}
} // namespace DWCWetSurfaceContactResolverPrivate

using namespace DWCWetSurfaceContactResolverPrivate;

bool FWetSurfaceContactResolver::ResolveContact(
    FWetSurfaceContactResolverArgs& Args,
    const FDWCWetContact& Contact,
    TArray<FDWCResolvedSurfaceContact>& OutContacts)
{
    OutContacts.Reset();

    if (!Args.TargetSkeletalMesh || !Args.WetnessSettings || !Args.WetClothingAsset ||
        !Args.RuntimeData || !Args.MeshSampler ||
        FMath::IsNearlyZero(Contact.Amount))
    {
        return false;
    }

    const FDWCGPULODBakeData& GPUData = Args.WetClothingAsset->GetGPUWetMapRuntimeData(Args.LODIndex);
    if (!GPUData.bRuntimeDataValid || GPUData.Triangles.IsEmpty() || GPUData.VertexIncidentTriangles.IsEmpty())
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    const FSkinWeightVertexBuffer* SkinWeightBuffer = Args.TargetSkeletalMesh->GetSkinWeightBuffer(Args.LODIndex);
    if (!SkinWeightBuffer ||
        !FWetRuntimeDataBuilder::GetLODRenderData(Args.TargetSkeletalMesh, Args.LODIndex, LODData) ||
        !LODData || !Args.MeshSampler->UpdateSkinningMatrices(Args.TargetSkeletalMesh))
    {
        return false;
    }

    const FTransform ComponentTransform = Args.TargetSkeletalMesh->GetComponentTransform();
    TMap<int32, FVector> WorldPositionCache;
    auto GetWorldPosition = [&](const int32 VertexIndex, FVector& OutWorldPosition) -> bool
    {
        if (const FVector* Cached = WorldPositionCache.Find(VertexIndex))
        {
            OutWorldPosition = *Cached;
            return true;
        }

        FVector3f ComponentPosition;
        if (!Args.MeshSampler->ComputeSkinnedPosition(*LODData, *SkinWeightBuffer, VertexIndex, ComponentPosition))
        {
            return false;
        }

        OutWorldPosition = ComponentTransform.TransformPosition(FVector(ComponentPosition));
        WorldPositionCache.Add(VertexIndex, OutWorldPosition);
        return true;
    };

    TMap<int32, const FDWCGPUVertexIncidentTriangles*> IncidentByVertex;
    IncidentByVertex.Reserve(GPUData.VertexIncidentTriangles.Num());
    for (const FDWCGPUVertexIncidentTriangles& Incident : GPUData.VertexIncidentTriangles)
    {
        if (Incident.SourceVertexIndex != INDEX_NONE)
        {
            IncidentByVertex.Add(Incident.SourceVertexIndex, &Incident);
        }
    }

    TSet<int32> CandidateTriangleIDs;
    if (Contact.RenderTriangleID != INDEX_NONE)
    {
        if (const FDWCGPUBakedTriangle* RenderHitTriangle = GPUData.Triangles.FindByPredicate(
                [&Contact](const FDWCGPUBakedTriangle& Candidate)
                {
                    return Candidate.RenderTriangleID == Contact.RenderTriangleID;
                }))
        {
            CandidateTriangleIDs.Add(RenderHitTriangle->TriangleID);
        }
    }

    const float SafeRadius = FMath::Max(Contact.Radius, KINDA_SMALL_NUMBER);
    const float RadiusSquared = SafeRadius * SafeRadius;
    TSet<int32> SeedVertices;
    TArray<FNearestVertex> NearestVertices;
    TArray<int32> CandidateVertices;
    FString FallbackReason;
    const bool bCacheResolved = FWetRuntimeDataBuilder::GetBoneCandidateVertexIndices(
        *Args.RuntimeData,
        Args.TargetSkeletalMesh,
        Contact.BoneName,
        CandidateVertices,
        &FallbackReason,
        false);

    auto EvaluateVertex = [&](const int32 VertexIndex)
    {
        FVector WorldPosition;
        if (!GetWorldPosition(VertexIndex, WorldPosition))
        {
            return;
        }

        const float DistanceSquared = FVector::DistSquared(Contact.Location, WorldPosition);
        if (DistanceSquared <= RadiusSquared)
        {
            SeedVertices.Add(VertexIndex);
        }
        AddNearestVertex(NearestVertices, FMath::Max(3, Args.MaxNearestSeedVertices), VertexIndex, DistanceSquared);
    };

    if (bCacheResolved)
    {
        // An empty cache-resolved set is an intentional miss: do not perform a second full traversal.
        for (const int32 VertexIndex : CandidateVertices)
        {
            EvaluateVertex(VertexIndex);
        }
    }
    else
    {
        const int32 VertexCount = static_cast<int32>(LODData->GetNumVertices());
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            EvaluateVertex(VertexIndex);
        }
    }

    for (const FNearestVertex& Nearest : NearestVertices)
    {
        if (Nearest.VertexIndex != INDEX_NONE)
        {
            SeedVertices.Add(Nearest.VertexIndex);
        }
    }

    for (const int32 VertexIndex : SeedVertices)
    {
        const FDWCGPUVertexIncidentTriangles* const* Incident = IncidentByVertex.Find(VertexIndex);
        if (Incident == nullptr || *Incident == nullptr)
        {
            continue;
        }
        for (const int32 TriangleID : (*Incident)->TriangleIDs)
        {
            CandidateTriangleIDs.Add(TriangleID);
        }
    }

    for (const int32 TriangleID : CandidateTriangleIDs)
    {
        if (!GPUData.Triangles.IsValidIndex(TriangleID))
        {
            continue;
        }

        const FDWCGPUBakedTriangle& Triangle = GPUData.Triangles[TriangleID];
        if (!Triangle.IsValid() ||
            (Contact.MaterialSlotIndex != INDEX_NONE && Contact.MaterialSlotIndex != Triangle.MaterialSlotIndex))
        {
            continue;
        }

        FVector P0;
        FVector P1;
        FVector P2;
        if (!GetWorldPosition(Triangle.VertexIndices.X, P0) ||
            !GetWorldPosition(Triangle.VertexIndices.Y, P1) ||
            !GetWorldPosition(Triangle.VertexIndices.Z, P2))
        {
            continue;
        }

        const FVector ClosestPoint = FMath::ClosestPointOnTriangleToPoint(Contact.Location, P0, P1, P2);
        const float Distance = FVector::Distance(Contact.Location, ClosestPoint);
        if (Distance > SafeRadius)
        {
            continue;
        }

        const FVector TriangleNormal = OrientTriangleNormalForContact(
            FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal(),
            Contact);
        if (!PassSurfaceFilter(Contact, *Args.WetnessSettings, ClosestPoint, TriangleNormal))
        {
            continue;
        }

        const FVector3f Barycentric = ComputeTriangleBarycentric(ClosestPoint, P0, P1, P2);
        const float Exposure = FWetInputStage::CalculateContactExposure(
            TriangleNormal,
            Contact.Direction.IsNearlyZero() ? FVector::ZeroVector : Contact.Direction.GetSafeNormal(),
            Contact.Normal.IsNearlyZero() ? FVector::ZeroVector : Contact.Normal.GetSafeNormal(),
            *Args.WetnessSettings);
        if (Exposure <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        FDWCResolvedSurfaceContact& Resolved = OutContacts.AddDefaulted_GetRef();
        Resolved.TriangleID = TriangleID;
        Resolved.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        Resolved.Barycentric = Barycentric;
        Resolved.ContactUV = FVector2f(Triangle.UV0) * Barycentric.X +
                             FVector2f(Triangle.UV1) * Barycentric.Y +
                             FVector2f(Triangle.UV2) * Barycentric.Z;
        Resolved.ContactWorldPosition = ClosestPoint;
        Resolved.ClosestWorldPosition = ClosestPoint;
        Resolved.WorldTrianglePosition0 = P0;
        Resolved.WorldTrianglePosition1 = P1;
        Resolved.WorldTrianglePosition2 = P2;
        Resolved.WorldTriangleNormal = TriangleNormal;
        Resolved.DistanceToSurface = Distance;
        Resolved.TriangleInfluence = FMath::Clamp(1.0f - Distance / SafeRadius, 0.0f, 1.0f);
        Resolved.Amount = Contact.Amount * Exposure;
        Resolved.Radius = SafeRadius;
        Resolved.AbsorptionMultiplier = ResolveGPUAbsorptionMultiplier(GPUData, Triangle, Contact.Amount);
    }

    KeepPrimaryContactSurface(GPUData, Contact, OutContacts);
    return !OutContacts.IsEmpty();
}

bool FWetSurfaceContactResolver::ResolveContacts(
    FWetSurfaceContactResolverArgs& Args,
    const TArray<FDWCWetContact>& Contacts,
    TArray<FDWCResolvedSurfaceContact>& OutContacts)
{
    OutContacts.Reset();
    TArray<FDWCResolvedSurfaceContact> Resolved;
    for (const FDWCWetContact& Contact : Contacts)
    {
        if (ResolveContact(Args, Contact, Resolved))
        {
            OutContacts.Append(Resolved);
        }
    }
    return !OutContacts.IsEmpty();
}

bool FWetSurfaceContactResolver::ResolveWetArea(
    FWetSurfaceContactResolverArgs& Args,
    const FDWCWetAreaData& AreaData,
    TArray<FDWCResolvedSurfaceContact>& OutContacts)
{
    OutContacts.Reset();

    if (!Args.TargetSkeletalMesh || !Args.WetnessSettings || !Args.WetClothingAsset ||
        !Args.MeshSampler ||
        FMath::IsNearlyZero(AreaData.Amount) || AreaData.SampleCount <= 0)
    {
        return false;
    }

    const FDWCGPULODBakeData& GPUData = Args.WetClothingAsset->GetGPUWetMapRuntimeData(Args.LODIndex);
    if (!GPUData.bRuntimeDataValid || GPUData.Triangles.IsEmpty() || GPUData.VertexIncidentTriangles.IsEmpty())
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    const FSkinWeightVertexBuffer* SkinWeightBuffer = Args.TargetSkeletalMesh->GetSkinWeightBuffer(Args.LODIndex);
    if (!SkinWeightBuffer ||
        !FWetRuntimeDataBuilder::GetLODRenderData(Args.TargetSkeletalMesh, Args.LODIndex, LODData) ||
        !LODData || !Args.MeshSampler->UpdateSkinningMatrices(Args.TargetSkeletalMesh))
    {
        return false;
    }

    const int32 VertexCount = static_cast<int32>(LODData->GetNumVertices());
    if (VertexCount <= 0)
    {
        return false;
    }

    TMap<int32, const FDWCGPUVertexIncidentTriangles*> IncidentByVertex;
    IncidentByVertex.Reserve(GPUData.VertexIncidentTriangles.Num());
    TArray<int32> WettableVertices;
    WettableVertices.Reserve(GPUData.VertexIncidentTriangles.Num());
    for (const FDWCGPUVertexIncidentTriangles& Incident : GPUData.VertexIncidentTriangles)
    {
        if (Incident.SourceVertexIndex >= 0 && Incident.SourceVertexIndex < VertexCount && !Incident.TriangleIDs.IsEmpty())
        {
            IncidentByVertex.Add(Incident.SourceVertexIndex, &Incident);
            WettableVertices.Add(Incident.SourceVertexIndex);
        }
    }

    if (WettableVertices.IsEmpty())
    {
        return false;
    }

    const bool bWantsNormalExposure = AreaData.bUseNormalExposure && !AreaData.Direction.IsNearlyZero();
    const FVector SafeDirection =
        AreaData.Direction.IsNearlyZero()
            ? FVector::DownVector
            : AreaData.Direction.GetSafeNormal();
    const FVector SafeNormal = -SafeDirection;
    const FTransform ComponentTransform = Args.TargetSkeletalMesh->GetComponentTransform();

    TArray<int32> SelectedVertices;
    const int32 SamplesToProcess = FMath::Min(AreaData.SampleCount, WettableVertices.Num());
    SelectedVertices.Reserve(SamplesToProcess);
    if (SamplesToProcess == WettableVertices.Num())
    {
        SelectedVertices = MoveTemp(WettableVertices);
    }
    else
    {
        FRandomStream RandomStream;
        if (AreaData.bOverrideRandomSeed)
        {
            RandomStream.Initialize(AreaData.RandomSeed);
        }
        else
        {
            RandomStream.GenerateNewSeed();
        }

        TSet<int32> SelectedSet;
        SelectedSet.Reserve(SamplesToProcess);
        int32 Attempts = 0;
        const int32 MaxAttempts = SamplesToProcess * 8;
        while (SelectedSet.Num() < SamplesToProcess && Attempts < MaxAttempts)
        {
            ++Attempts;
            SelectedSet.Add(WettableVertices[RandomStream.RandRange(0, WettableVertices.Num() - 1)]);
        }
        for (const int32 VertexIndex : SelectedSet)
        {
            SelectedVertices.Add(VertexIndex);
        }
    }

    TMap<int32, FVector> WorldPositionCache;
    auto GetWorldPosition = [&](const int32 VertexIndex, FVector& OutWorldPosition) -> bool
    {
        if (const FVector* Cached = WorldPositionCache.Find(VertexIndex))
        {
            OutWorldPosition = *Cached;
            return true;
        }

        FVector3f ComponentPosition;
        if (!Args.MeshSampler->ComputeSkinnedPosition(*LODData, *SkinWeightBuffer, VertexIndex, ComponentPosition))
        {
            return false;
        }

        OutWorldPosition = ComponentTransform.TransformPosition(FVector(ComponentPosition));
        WorldPositionCache.Add(VertexIndex, OutWorldPosition);
        return true;
    };

    auto GetWorldNormal = [&](const int32 VertexIndex, FVector& OutWorldNormal) -> bool
    {
        FVector3f ComponentNormal;
        if (AreaData.bUseSkinnedNormalsForExposure &&
            Args.MeshSampler->ComputeSkinnedNormal(*LODData, *SkinWeightBuffer, VertexIndex, ComponentNormal))
        {
            OutWorldNormal = ComponentTransform.TransformVectorNoScale(FVector(ComponentNormal)).GetSafeNormal();
            return !OutWorldNormal.IsNearlyZero();
        }

        if (VertexIndex >= 0 &&
            VertexIndex < static_cast<int32>(LODData->StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices()))
        {
            OutWorldNormal =
                ComponentTransform.TransformVectorNoScale(
                    FVector(LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)))
                .GetSafeNormal();
            return !OutWorldNormal.IsNearlyZero();
        }

        return false;
    };

    for (const int32 VertexIndex : SelectedVertices)
    {
        const FDWCGPUVertexIncidentTriangles* const* Incident = IncidentByVertex.Find(VertexIndex);
        if (Incident == nullptr || *Incident == nullptr)
        {
            continue;
        }

        FVector VertexWorldPosition;
        if (!GetWorldPosition(VertexIndex, VertexWorldPosition))
        {
            continue;
        }

        float Exposure = 1.0f;
        if (bWantsNormalExposure)
        {
            FVector WorldNormal = FVector::ZeroVector;
            if (!GetWorldNormal(VertexIndex, WorldNormal))
            {
                continue;
            }

            Exposure = FWetInputStage::CalculateContactExposure(
                WorldNormal,
                SafeDirection,
                SafeNormal,
                *Args.WetnessSettings);
            if (Exposure <= KINDA_SMALL_NUMBER)
            {
                continue;
            }
        }

        const float EffectiveAmount = AreaData.Amount * Exposure;
        TSet<int32> AddedTriangleIDs;
        for (const int32 TriangleID : (*Incident)->TriangleIDs)
        {
            if (AddedTriangleIDs.Contains(TriangleID))
            {
                continue;
            }
            AddedTriangleIDs.Add(TriangleID);

            if (!GPUData.Triangles.IsValidIndex(TriangleID))
            {
                continue;
            }

            const FDWCGPUBakedTriangle& Triangle = GPUData.Triangles[TriangleID];
            if (!Triangle.IsValid())
            {
                continue;
            }

            FVector P0;
            FVector P1;
            FVector P2;
            if (!GetWorldPosition(Triangle.VertexIndices.X, P0) ||
                !GetWorldPosition(Triangle.VertexIndices.Y, P1) ||
                !GetWorldPosition(Triangle.VertexIndices.Z, P2))
            {
                continue;
            }

            const FVector3f Barycentric = ComputeTriangleBarycentric(VertexWorldPosition, P0, P1, P2);
            FDWCResolvedSurfaceContact& Resolved = OutContacts.AddDefaulted_GetRef();
            Resolved.TriangleID = TriangleID;
            Resolved.MaterialSlotIndex = Triangle.MaterialSlotIndex;
            Resolved.Barycentric = Barycentric;
            Resolved.ContactUV = FVector2f(Triangle.UV0) * Barycentric.X +
                                 FVector2f(Triangle.UV1) * Barycentric.Y +
                                 FVector2f(Triangle.UV2) * Barycentric.Z;
            Resolved.ContactWorldPosition = VertexWorldPosition;
            Resolved.ClosestWorldPosition = VertexWorldPosition;
            Resolved.WorldTrianglePosition0 = P0;
            Resolved.WorldTrianglePosition1 = P1;
            Resolved.WorldTrianglePosition2 = P2;
            Resolved.WorldTriangleNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
            Resolved.DistanceToSurface = 0.0f;
            Resolved.TriangleInfluence = 1.0f;
            Resolved.Amount = EffectiveAmount;
            Resolved.Radius = ComputeWetAreaSampleRadius(P0, P1, P2);
            Resolved.AbsorptionMultiplier = ResolveGPUAbsorptionMultiplier(GPUData, Triangle, EffectiveAmount);
        }
    }

    return !OutContacts.IsEmpty();
}

bool FWetSurfaceContactResolver::ResolveWaterSurface(
    FWetSurfaceContactResolverArgs& Args,
    const FDWCWaterSurfaceData& WaterSurfaceData,
    const float Amount,
    TArray<FDWCResolvedSurfaceContact>& OutContacts)
{
    OutContacts.Reset();

    if (!Args.TargetSkeletalMesh || !Args.WetClothingAsset ||
        !Args.MeshSampler ||
        FMath::IsNearlyZero(Amount) ||
        WaterSurfaceData.SizeX < 2 || WaterSurfaceData.SizeY < 2 ||
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

    const FDWCGPULODBakeData& GPUData = Args.WetClothingAsset->GetGPUWetMapRuntimeData(Args.LODIndex);
    if (!GPUData.bRuntimeDataValid || GPUData.Triangles.IsEmpty())
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    const FSkinWeightVertexBuffer* SkinWeightBuffer = Args.TargetSkeletalMesh->GetSkinWeightBuffer(Args.LODIndex);
    if (!SkinWeightBuffer ||
        !FWetRuntimeDataBuilder::GetLODRenderData(Args.TargetSkeletalMesh, Args.LODIndex, LODData) ||
        !LODData || !Args.MeshSampler->UpdateSkinningMatrices(Args.TargetSkeletalMesh))
    {
        return false;
    }

    const FTransform ComponentTransform = Args.TargetSkeletalMesh->GetComponentTransform();
    TMap<int32, FVector> WorldPositionCache;
    auto GetWorldPosition = [&](const int32 VertexIndex, FVector& OutWorldPosition) -> bool
    {
        if (const FVector* Cached = WorldPositionCache.Find(VertexIndex))
        {
            OutWorldPosition = *Cached;
            return true;
        }

        FVector3f ComponentPosition;
        if (!Args.MeshSampler->ComputeSkinnedPosition(*LODData, *SkinWeightBuffer, VertexIndex, ComponentPosition))
        {
            return false;
        }

        OutWorldPosition = ComponentTransform.TransformPosition(FVector(ComponentPosition));
        WorldPositionCache.Add(VertexIndex, OutWorldPosition);
        return true;
    };

    auto IsSubmerged = [&WaterSurfaceData](const FVector& WorldPosition) -> bool
    {
        float SurfaceZ = 0.0f;
        return FWetInputStage::QueryWaterSurfaceData(WaterSurfaceData, WorldPosition, SurfaceZ) &&
               WorldPosition.Z <= SurfaceZ;
    };

    for (int32 TriangleID = 0; TriangleID < GPUData.Triangles.Num(); ++TriangleID)
    {
        const FDWCGPUBakedTriangle& Triangle = GPUData.Triangles[TriangleID];
        if (!Triangle.IsValid())
        {
            continue;
        }

        FVector P0;
        FVector P1;
        FVector P2;
        if (!GetWorldPosition(Triangle.VertexIndices.X, P0) ||
            !GetWorldPosition(Triangle.VertexIndices.Y, P1) ||
            !GetWorldPosition(Triangle.VertexIndices.Z, P2))
        {
            continue;
        }

        const FVector Centroid = (P0 + P1 + P2) / 3.0;
        FVector ContactPoint = FVector::ZeroVector;
        int32 SubmergedSamples = 0;

        if (IsSubmerged(Centroid))
        {
            ContactPoint = Centroid;
            SubmergedSamples = 1;
        }
        else
        {
            if (IsSubmerged(P0))
            {
                ContactPoint += P0;
                ++SubmergedSamples;
            }
            if (IsSubmerged(P1))
            {
                ContactPoint += P1;
                ++SubmergedSamples;
            }
            if (IsSubmerged(P2))
            {
                ContactPoint += P2;
                ++SubmergedSamples;
            }

            if (SubmergedSamples > 0)
            {
                ContactPoint /= static_cast<double>(SubmergedSamples);
            }
        }

        if (SubmergedSamples == 0)
        {
            continue;
        }

        const FVector3f Barycentric = ComputeTriangleBarycentric(ContactPoint, P0, P1, P2);
        FDWCResolvedSurfaceContact& Resolved = OutContacts.AddDefaulted_GetRef();
        Resolved.TriangleID = TriangleID;
        Resolved.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        Resolved.Barycentric = Barycentric;
        Resolved.ContactUV = FVector2f(Triangle.UV0) * Barycentric.X +
                             FVector2f(Triangle.UV1) * Barycentric.Y +
                             FVector2f(Triangle.UV2) * Barycentric.Z;
        Resolved.ContactWorldPosition = ContactPoint;
        Resolved.ClosestWorldPosition = ContactPoint;
        Resolved.WorldTrianglePosition0 = P0;
        Resolved.WorldTrianglePosition1 = P1;
        Resolved.WorldTrianglePosition2 = P2;
        Resolved.WorldTriangleNormal = FVector::CrossProduct(P1 - P0, P2 - P0).GetSafeNormal();
        Resolved.DistanceToSurface = 0.0f;
        Resolved.TriangleInfluence = 1.0f;
        Resolved.Amount = Amount;
        Resolved.Radius = ComputeTriangleContactRadius(ContactPoint, P0, P1, P2);
        Resolved.AbsorptionMultiplier = ResolveGPUAbsorptionMultiplier(GPUData, Triangle, Amount);
    }

    return !OutContacts.IsEmpty();
}
