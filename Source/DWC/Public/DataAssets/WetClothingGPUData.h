// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetnessProfile.h"
#include "WetClothingGPUData.generated.h"

/**
 * Serialized compatibility snapshot used by existing GPU runtime bulk data.
 * New runtime code should use FResolvedAbsorbedWaterSimulationParameters.
 */
USTRUCT()
struct DWC_API FDWCGPUProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    float AbsorptionMultiplier = 1.0f;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    float SpreadRatePerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    float DryRatePerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    float GravityFlowStrength = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    float DropletDryRatePerSecond = 0.0f;

    FDWCGPUProfileParameters() = default;

    explicit FDWCGPUProfileParameters(
        const FResolvedAbsorbedWaterSimulationParameters& Parameters)
        : AbsorptionMultiplier(Parameters.AbsorptionMultiplier), SpreadRatePerSecond(Parameters.SpreadRatePerSecond), DryRatePerSecond(Parameters.DryRatePerSecond), GravityFlowStrength(Parameters.GravityFlowStrength)
    {
    }

    FResolvedAbsorbedWaterSimulationParameters ToResolvedParameters() const
    {
        FResolvedAbsorbedWaterSimulationParameters Result;
        Result.AbsorptionMultiplier = AbsorptionMultiplier;
        Result.SpreadRatePerSecond = SpreadRatePerSecond;
        Result.DryRatePerSecond = DryRatePerSecond;
        Result.GravityFlowStrength = GravityFlowStrength;
        return Result;
    }

    bool Equals(const FDWCGPUProfileParameters& Other, float Tolerance = KINDA_SMALL_NUMBER) const
    {
        return FMath::IsNearlyEqual(AbsorptionMultiplier, Other.AbsorptionMultiplier, Tolerance) &&
               FMath::IsNearlyEqual(SpreadRatePerSecond, Other.SpreadRatePerSecond, Tolerance) &&
               FMath::IsNearlyEqual(DryRatePerSecond, Other.DryRatePerSecond, Tolerance) &&
               FMath::IsNearlyEqual(GravityFlowStrength, Other.GravityFlowStrength, Tolerance) &&
               FMath::IsNearlyEqual(DropletDryRatePerSecond, Other.DropletDryRatePerSecond, Tolerance);
    }
};

/** Static simulation-LOD triangle data shared by contact resolution and the GPU solver. */
USTRUCT()
struct DWC_API FDWCGPUBakedTriangle
{
    GENERATED_BODY()

    /** Compact wettable-triangle index used by GPU buffers and texel lookup. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 TriangleID = INDEX_NONE;

    /** Original simulation-LOD index-buffer triangle ID used to resolve render hits. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 RenderTriangleID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 RenderSectionIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 UVIslandID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    FIntVector VertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    FVector2D UV0 = FVector2D::ZeroVector;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    FVector2D UV1 = FVector2D::ZeroVector;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    FVector2D UV2 = FVector2D::ZeroVector;

    /**
     * Row-major 2x2 transform from DWCDataUV direction to
     * SurfaceWaterNormalUV direction:
     *
     * | X Y |
     * | Z W |
     */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    FVector4 DataToSurfaceWaterNormalUV = FVector4(1.0, 0.0, 0.0, 1.0);

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    float RestSurfaceArea = 0.0f;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 ProfileIndex = INDEX_NONE;

    bool IsValid() const
    {
        return TriangleID != INDEX_NONE && MaterialSlotIndex != INDEX_NONE &&
               VertexIndices.X != INDEX_NONE && VertexIndices.Y != INDEX_NONE && VertexIndices.Z != INDEX_NONE;
    }
};

USTRUCT()
struct DWC_API FDWCGPUVertexIncidentTriangles
{
    GENERATED_BODY()

    /** Original LOD render-vertex index. Only vertices touching wettable triangles are serialized. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 SourceVertexIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<int32> TriangleIDs;
};

/** One source contribution in a destination-oriented seam gather table. */
USTRUCT()
struct DWC_API FDWCGPUSeamIncoming
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 SourceTexelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    float Weight = 1.0f;
};

/** Range into SeamIncoming for one destination texel. */
USTRUCT()
struct DWC_API FDWCGPUSeamDestination
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 DestinationTexelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 IncomingStartIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 IncomingCount = 0;
};

/** CPU-baked lookup payload for one material slot's absorbed-wetness map and optional GPU Surface Water maps. */
USTRUCT()
struct DWC_API FDWCGPUMaterialSlotBakeData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    int32 Resolution = 0;

    /** Global simulation-LOD TriangleID per texel. INDEX_NONE means invalid/background. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<int32> TexelTriangleIDs;

    /** Two UNorm16 barycentric weights packed into uint32 (low=V0, high=V1); V2 = 1-X-Y. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<uint32> PackedTexelBarycentricXY;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<float> RestTexelAreas;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<uint8> ValidMask;

    /** Independent lookup resolution used by the GPU-only Droplet surface RT. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Surface Water")
    int32 SurfaceWaterResolution = 0;

    /** Surface-water lookup payload. Empty when no Part in this slot enables Surface Water. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Surface Water")
    TArray<int32> SurfaceTexelTriangleIDs;

    UPROPERTY(VisibleAnywhere, Category = "GPU Surface Water")
    TArray<uint32> SurfacePackedTexelBarycentricXY;

    UPROPERTY(VisibleAnywhere, Category = "GPU Surface Water")
    TArray<float> SurfaceRestTexelAreas;

    UPROPERTY(VisibleAnywhere, Category = "GPU Surface Water")
    TArray<uint8> SurfaceValidMask;

    /** Destination-oriented inverse mapping used by the separate seam gather pass. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<FDWCGPUSeamDestination> SeamDestinations;

    UPROPERTY(VisibleAnywhere, Category = "GPU Wet Map")
    TArray<FDWCGPUSeamIncoming> SeamIncoming;
};

USTRUCT()
struct DWC_API FDWCGPULODBakeData
{
    GENERATED_BODY()

    static constexpr int32 CurrentRuntimeDataVersion = 7;
    static constexpr int32 CurrentMapBakeVersion = 8;
    static constexpr int32 CurrentBulkDataVersion = 6;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 RuntimeDataVersion = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 BulkDataVersion = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    bool bRuntimeDataValid = false;

    UPROPERTY(VisibleAnywhere, Category = "GPU Map Bake")
    int32 MapBakeVersion = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Map Bake")
    bool bMapDataValid = false;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    FString MeshSignature;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    FString SourceDataSignature;

    /** Excludes map resolution. Used to validate Save-generated GPU runtime structures. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    FString RuntimeSignature;

    /** Includes RuntimeSignature, absorbed/surface map resolutions and map-bake version. */
    UPROPERTY(VisibleAnywhere, Category = "GPU Map Bake")
    FString MapSignature;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 ProfileCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 TriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    int32 VertexIncidentRecordCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "GPU Map Bake")
    int32 MaterialSlotMapCount = 0;

    /** Legacy serialized snapshot retained for runtime-bulk compatibility.
     *  The GPU backend rebuilds current simulation profiles from the resolved
     *  Wetness Profile and uploads them through its Structured Buffer.
     */
    UPROPERTY(Transient, VisibleAnywhere, Category = "GPU Runtime Data")
    TArray<FDWCGPUProfileParameters> Profiles;

    UPROPERTY(Transient, VisibleAnywhere, Category = "GPU Runtime Data")
    TArray<FDWCGPUBakedTriangle> Triangles;

    UPROPERTY(Transient, VisibleAnywhere, Category = "GPU Runtime Data")
    TArray<FDWCGPUVertexIncidentTriangles> VertexIncidentTriangles;

    /** Resolution-dependent texel lookup and seam-map payload. */
    UPROPERTY(Transient, VisibleAnywhere, Category = "GPU Map Bake")
    TArray<FDWCGPUMaterialSlotBakeData> MaterialSlots;

    UPROPERTY(VisibleAnywhere, Category = "GPU Runtime Data")
    FGuid RuntimeBuildGuid;

    UPROPERTY(VisibleAnywhere, Category = "GPU Map Bake")
    FGuid MapBakeGuid;
};
