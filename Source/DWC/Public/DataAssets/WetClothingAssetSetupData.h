#pragma once

#include "CoreMinimal.h"
#include "WetClothingAssetSetupData.generated.h"

UENUM(BlueprintType)
enum class EDWCBakeStatus : uint8
{
    Disabled UMETA(DisplayName = "Disabled"),
    Required UMETA(DisplayName = "Required"),
    Valid UMETA(DisplayName = "Valid"),
    ValidWithDiagnostics UMETA(DisplayName = "Valid With Diagnostics"),
    OutOfDate UMETA(DisplayName = "Out of Date"),
    Failed UMETA(DisplayName = "Failed")
};


namespace DWCBuildStatus
{
    FORCEINLINE bool IsUsable(const EDWCBakeStatus Status)
    {
        return Status == EDWCBakeStatus::Valid || Status == EDWCBakeStatus::ValidWithDiagnostics;
    }
}

namespace DWCGeneratedDataVersion
{
    // Version 7 makes the DWC Prepared Skeletal Mesh the sole source for generated UV metadata
    // and Original UV topology. Source meshes are used only to create the prepared mesh copy.
    static constexpr int32 DataUV = 7;
    static constexpr int32 OriginalUVTopology = 7;
}

namespace DWCBakeOutput
{
    static constexpr int32 GeneratedDataUV = 1 << 0;
    static constexpr int32 OriginalUVTopology = 1 << 1;
    static constexpr int32 CPURuntimeData = 1 << 2;
    static constexpr int32 GPURuntimeData = 1 << 3;
    static constexpr int32 GPUMaps = 1 << 4;
    static constexpr int32 WrinkleMaps = 1 << 5;
    static constexpr int32 TransparencyMaps = 1 << 6;

    FORCEINLINE bool Has(const int32 Mask, const int32 Output)
    {
        return (Mask & Output) != 0;
    }
}

UENUM(BlueprintType)
enum class EDWCMapResolution : uint8
{
    Resolution256 UMETA(DisplayName = "256"),
    Resolution512 UMETA(DisplayName = "512"),
    Resolution1024 UMETA(DisplayName = "1024"),
    Resolution2048 UMETA(DisplayName = "2048"),
    Resolution4096 UMETA(DisplayName = "4096")
};

namespace DWCMapResolution
{
    FORCEINLINE int32 ToInt(const EDWCMapResolution Resolution)
    {
        switch (Resolution)
        {
        case EDWCMapResolution::Resolution256: return 256;
        case EDWCMapResolution::Resolution512: return 512;
        case EDWCMapResolution::Resolution1024: return 1024;
        case EDWCMapResolution::Resolution2048: return 2048;
        case EDWCMapResolution::Resolution4096: return 4096;
        default: return 1024;
        }
    }

    FORCEINLINE EDWCMapResolution FromInt(const int32 Resolution)
    {
        if (Resolution < 384) return EDWCMapResolution::Resolution256;
        if (Resolution < 768) return EDWCMapResolution::Resolution512;
        if (Resolution < 1536) return EDWCMapResolution::Resolution1024;
        if (Resolution < 3072) return EDWCMapResolution::Resolution2048;
        return EDWCMapResolution::Resolution4096;
    }
}

/** Settings selected when a Wet Clothing Asset is created or reconfigured. */
USTRUCT(BlueprintType)
struct DWC_API FDWCWetClothingAssetSetupSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Simulation Data")
    bool bBuildCPUVertexSimulationData = true;

    UPROPERTY(EditAnywhere, Category = "Simulation Data")
    bool bBuildGPUWetnessMapSimulationData = true;

    /** Serialized as int32 for compatibility with existing WCA assets. Creation/Setup dialogs expose enum proxies. */
    UPROPERTY(VisibleAnywhere, Category = "Map Resolutions", meta = (DisplayName = "GPU Simulation"))
    int32 GPUSimulationMapResolution = 512;

    /** Runtime Surface Water render-target resolution. This is independent from the GPU wetness-map bake resolution. */
    UPROPERTY(VisibleAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Surface Water RT"))
    int32 SurfaceWaterRTResolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Wrinkle"))
    int32 WrinkleMapResolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Map Resolutions", meta = (DisplayName = "Transparency"))
    int32 TransparencyMapResolution = 1024;

    /** Original UV channel on the DWC Prepared Skeletal Mesh used by Part Edit and Data UV generation. */
    UPROPERTY(VisibleAnywhere, Category = "Mesh")
    int32 OriginalUVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (ClampMin = "0", ClampMax = "7"))
    int32 PreferredDWCDataUVChannelIndex = 1;

    UPROPERTY(EditAnywhere, Category = "Mesh|LOD Mapping Range", meta = (DisplayName = "First Mapped LOD", ClampMin = "0"))
    int32 FirstGeneratedLODIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh|LOD Mapping Range", meta = (DisplayName = "Last Mapped LOD", ClampMin = "0"))
    int32 LastGeneratedLODIndex = 0;

    /** Render LOD used by CPU/GPU simulation runtime data. */
    UPROPERTY(VisibleAnywhere, Category = "Mesh")
    int32 SimulationLODIndex = 0;

    int32 GetGPUSimulationMapResolution() const { return GPUSimulationMapResolution; }
    int32 GetSurfaceWaterRTResolution() const { return SurfaceWaterRTResolution; }
    int32 GetWrinkleMapResolution() const { return WrinkleMapResolution; }
    int32 GetTransparencyMapResolution() const { return TransparencyMapResolution; }

    void NormalizeMapResolutions()
    {
        GPUSimulationMapResolution = DWCMapResolution::ToInt(DWCMapResolution::FromInt(GPUSimulationMapResolution));
        SurfaceWaterRTResolution = DWCMapResolution::ToInt(DWCMapResolution::FromInt(SurfaceWaterRTResolution));
        WrinkleMapResolution = DWCMapResolution::ToInt(DWCMapResolution::FromInt(WrinkleMapResolution));
        TransparencyMapResolution = DWCMapResolution::ToInt(DWCMapResolution::FromInt(TransparencyMapResolution));
        OriginalUVChannelIndex = FMath::Clamp(OriginalUVChannelIndex, 0, 7);
        PreferredDWCDataUVChannelIndex = FMath::Clamp(PreferredDWCDataUVChannelIndex, 0, 7);
        FirstGeneratedLODIndex = FMath::Max(0, FirstGeneratedLODIndex);
        LastGeneratedLODIndex = FMath::Max(FirstGeneratedLODIndex, LastGeneratedLODIndex);
        SimulationLODIndex = 0;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FDWCOriginalUVIslandTopology
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    int32 IslandID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    TArray<int32> TriangleIndices;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    FBox2D UVBounds = FBox2D(ForceInit);

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    double UVArea = 0.0;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCEditorUVTopologyData
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    int32 GeneratorVersion = 1;

    UPROPERTY(VisibleAnywhere, Category = "Editor UV Topology")
    TArray<FDWCOriginalUVIslandTopology> Islands;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCDataUVLODMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    int32 RenderVertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    int32 MaterialSlotCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    int32 UVChannelIndex = INDEX_NONE;

    /** Signature of the mesh inputs used to generate this output. */
    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    FString MeshInputSignature;

    /** Signature of the actual DWC Data UV channel stored on DWCSkeletalMesh. */
    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    FString DataUVOutputSignature;

    UPROPERTY(VisibleAnywhere, Category = "DWC Data UV")
    int32 GeneratorVersion = 1;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCTriangleValidationSummary
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 TotalWettableTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 CPUUsableTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 GPUUsableTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 Degenerate3DTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 DegenerateOriginalUVTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 DegenerateDWCDataUVTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    int32 InvalidUVTriangles = 0;

    UPROPERTY(VisibleAnywhere, Category = "Validation")
    TArray<int32> ExampleTriangleIndices;

    /** Mesh/UV diagnostics. These do not imply user-actionable validation failure. */
    int32 GetDiagnosticTriangleCount() const
    {
        return DegenerateOriginalUVTriangles + DegenerateDWCDataUVTriangles + InvalidUVTriangles;
    }

    bool HasDiagnostics() const
    {
        return GetDiagnosticTriangleCount() > 0;
    }

    /** Total triangles excluded for any reason, including informational 3D degenerates. */
    int32 GetExcludedTriangleCount() const
    {
        return Degenerate3DTriangles + GetDiagnosticTriangleCount();
    }
};

USTRUCT(BlueprintType)
struct DWC_API FDWCAssetBakeState
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Bake Status")
    EDWCBakeStatus GeneratedDataUV = EDWCBakeStatus::Required;

    UPROPERTY(VisibleAnywhere, Category = "Bake Status")
    EDWCBakeStatus OriginalUVTopology = EDWCBakeStatus::Required;

    /** Non-texture CPU runtime data. Rebuilt automatically when the WCA is saved. */
    UPROPERTY(VisibleAnywhere, Category = "Runtime Data Status")
    EDWCBakeStatus CPURuntimeData = EDWCBakeStatus::Required;

    /** Non-texture GPU triangle/profile/incident data. Rebuilt automatically when the WCA is saved. */
    UPROPERTY(VisibleAnywhere, Category = "Runtime Data Status")
    EDWCBakeStatus GPURuntimeData = EDWCBakeStatus::Required;

    /** Resolution-dependent GPU texel lookup and seam maps. Rebuilt explicitly from Bake Maps. */
    UPROPERTY(VisibleAnywhere, Category = "Map Bake Status")
    EDWCBakeStatus GPUMaps = EDWCBakeStatus::Required;

    UPROPERTY(VisibleAnywhere, Category = "Map Bake Status")
    EDWCBakeStatus WrinkleMaps = EDWCBakeStatus::Required;

    UPROPERTY(VisibleAnywhere, Category = "Map Bake Status")
    EDWCBakeStatus TransparencyMaps = EDWCBakeStatus::Required;

    /** Outputs that have completed at least once. Used to distinguish first-time guidance from rebuild guidance. */
    UPROPERTY(VisibleAnywhere, Category = "Bake Status")
    int32 GeneratedOutputMask = 0;

    /** Outputs that have been included in a Wet Clothing Asset save at least once. */
    UPROPERTY(VisibleAnywhere, Category = "Bake Status")
    int32 SavedOutputMask = 0;

    UPROPERTY(VisibleAnywhere, Category = "Bake Status")
    FString LastFailure;
};
