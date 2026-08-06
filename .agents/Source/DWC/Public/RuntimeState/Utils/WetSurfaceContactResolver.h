#pragma once

#include "CoreMinimal.h"
#include "WetInputSystem/WetContactTypes.h"

class USkeletalMeshComponent;
class FWetClothingRuntimeData;
class FWetClothingMeshSampler;
class UWetClothingAsset;
struct FWetClothingSettings;

/** Current-pose triangle-space contact consumed by the GPU wetness-map backend. */
struct DWC_API FDWCResolvedSurfaceContact
{
    int32 TriangleID = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;

    FVector3f Barycentric = FVector3f::ZeroVector;
    FVector2f ContactUV = FVector2f::ZeroVector;

    FVector ContactWorldPosition = FVector::ZeroVector;
    FVector ClosestWorldPosition = FVector::ZeroVector;
    FVector WorldTrianglePosition0 = FVector::ZeroVector;
    FVector WorldTrianglePosition1 = FVector::ZeroVector;
    FVector WorldTrianglePosition2 = FVector::ZeroVector;
    FVector WorldTriangleNormal = FVector::UpVector;

    float DistanceToSurface = 0.0f;
    float TriangleInfluence = 0.0f;
    float Amount = 0.0f;
    float Radius = 0.0f;
    float AbsorptionMultiplier = 1.0f;
};

struct DWC_API FWetSurfaceContactResolverArgs
{
    UObject* OwnerForLogs = nullptr;
    USkeletalMeshComponent* TargetSkeletalMesh = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;
    const UWetClothingAsset* WetClothingAsset = nullptr;
    const FWetClothingRuntimeData* RuntimeData = nullptr;
    FWetClothingMeshSampler* MeshSampler = nullptr;
    int32 LODIndex = 0;
    int32 MaxNearestSeedVertices = 12;
};

/**
 * Reuses the CPU backend's bone candidate cache as broad phase, then resolves
 * the actual current-pose triangle closest point and barycentric coordinates.
 */
class DWC_API FWetSurfaceContactResolver
{
public:
    FWetSurfaceContactResolver() = delete;

    static bool ResolveContact(
        FWetSurfaceContactResolverArgs& Args,
        const FDWCWetContact& Contact,
        TArray<FDWCResolvedSurfaceContact>& OutContacts);

    static bool ResolveContacts(
        FWetSurfaceContactResolverArgs& Args,
        const TArray<FDWCWetContact>& Contacts,
        TArray<FDWCResolvedSurfaceContact>& OutContacts);

    static bool ResolveWetArea(
        FWetSurfaceContactResolverArgs& Args,
        const FDWCWetAreaData& AreaData,
        TArray<FDWCResolvedSurfaceContact>& OutContacts);

    static bool ResolveWaterSurface(
        FWetSurfaceContactResolverArgs& Args,
        const FDWCWaterSurfaceData& WaterSurfaceData,
        float Amount,
        TArray<FDWCResolvedSurfaceContact>& OutContacts);
};
