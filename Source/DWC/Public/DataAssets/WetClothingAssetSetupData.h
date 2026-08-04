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

UENUM(BlueprintType)
enum class EDWCDataUVResultSeverity : uint8
{
    Ready UMETA(DisplayName = "Ready"),
    ReadyWithNotes UMETA(DisplayName = "Ready With Notes"),
    ReadyWithWarnings UMETA(DisplayName = "Ready With Warnings"),
    Failed UMETA(DisplayName = "Failed")
};

namespace DWCDataUVResultSeverity
{
    FORCEINLINE int32 Rank(const EDWCDataUVResultSeverity Severity)
    {
        switch (Severity)
        {
        case EDWCDataUVResultSeverity::Ready: return 0;
        case EDWCDataUVResultSeverity::ReadyWithNotes: return 1;
        case EDWCDataUVResultSeverity::ReadyWithWarnings: return 2;
        case EDWCDataUVResultSeverity::Failed: return 3;
        default: return 3;
        }
    }

    FORCEINLINE EDWCDataUVResultSeverity Max(
        const EDWCDataUVResultSeverity A,
        const EDWCDataUVResultSeverity B)
    {
        return Rank(A) >= Rank(B) ? A : B;
    }
}

UENUM()
enum class EDWCDataUVSlotLODResultState : uint8
{
    Ready,
    NotPresent,
    NotCommitted,
    NotGenerated,
    Failed
};

USTRUCT()
struct DWC_API FDWCDataUVSlotLODResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 LODIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    EDWCDataUVSlotLODResultState State = EDWCDataUVSlotLODResultState::NotGenerated;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    FString Message;
};

namespace DWCGeneratedDataVersion
{
    // Version 10 preserves the version-9 topology/packing policy and adds persistent
    // per-slot result severity plus visible-surface exclusion diagnostics. Packed
    // degenerate triangles within the configured coverage limits are excluded from
    // DWC-derived data instead of failing the complete LOD.
    static constexpr int32 DataUV = 10;
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
    bool bBuildCPUVertexSimulationData = false;

    UPROPERTY(EditAnywhere, Category = "Simulation Data")
    bool bBuildGPUWetnessMapSimulationData = true;

    /** Serialized as int32 for compatibility with existing WCA assets. Creation/Setup dialogs expose enum proxies. */
    UPROPERTY(VisibleAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "GPU Simulation"))
    int32 GPUSimulationMapResolution = 512;

    /** Runtime Surface Water render-target resolution. This is independent from the GPU wetness-map bake resolution. */
    UPROPERTY(VisibleAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Surface Water"))
    int32 SurfaceWaterRTResolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Wrinkle"))
    int32 WrinkleMapResolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Texture Resolutions", meta = (DisplayName = "Transparency"))
    int32 TransparencyMapResolution = 1024;

    /** Original UV channel on the DWC Prepared Skeletal Mesh used by Part Edit and DWC UV Channel generation. */
    UPROPERTY(VisibleAnywhere, Category = "Mesh")
    int32 OriginalUVChannelIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh", meta = (ClampMin = "0", ClampMax = "3"))
    int32 PreferredDWCDataUVChannelIndex = 1;

    /** Creation/setup confirmation for replacing an occupied preferred channel on the DWC Prepared Mesh only. */
    UPROPERTY()
    bool bAllowOverwritePreferredDWCDataUVChannel = false;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "First Mapped LOD", ClampMin = "0"))
    int32 FirstGeneratedLODIndex = 0;

    UPROPERTY(EditAnywhere, Category = "Mesh|Active LOD Mapping Range", meta = (DisplayName = "Last Mapped LOD", ClampMin = "0"))
    int32 LastGeneratedLODIndex = MAX_int32;

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
        PreferredDWCDataUVChannelIndex = FMath::Clamp(PreferredDWCDataUVChannelIndex, 0, 3);
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
struct DWC_API FDWCDataUVSlotWarning
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 Degenerate3DTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 DegenerateSourceUVTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 InvalidSourceUVTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 PackedDegenerateTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 ExcludedVisibleTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    double TotalValid3DSurfaceArea = 0.0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    double ExcludedVisible3DSurfaceArea = 0.0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    double ExcludedVisible3DSurfaceRatio = 0.0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    double LargestConnectedExcluded3DSurfaceArea = 0.0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    double LargestConnectedExcluded3DSurfaceRatio = 0.0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 SplitOriginalUVIslandCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 SelfOverlapPairCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 BudgetFallbackIslandCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    EDWCDataUVResultSeverity ResultSeverity = EDWCDataUVResultSeverity::Ready;

    bool HasWarnings() const
    {
        return ResultSeverity != EDWCDataUVResultSeverity::Ready ||
            Degenerate3DTriangleCount > 0 ||
            DegenerateSourceUVTriangleCount > 0 ||
            InvalidSourceUVTriangleCount > 0 ||
            PackedDegenerateTriangleCount > 0 ||
            ExcludedVisibleTriangleCount > 0 ||
            SplitOriginalUVIslandCount > 0 ||
            SelfOverlapPairCount > 0 ||
            BudgetFallbackIslandCount > 0;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FDWCDataUVLODMetadata
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 RenderVertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 MaterialSlotCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    int32 UVChannelIndex = INDEX_NONE;

    /** Material slots included in this immutable DWC UV Channel layout. Empty means all slots for legacy assets. */
    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    TArray<int32> GeneratedMaterialSlotIndices;

    /** Per-slot details for non-fatal DWC UV Channel warnings. Empty on legacy assets. */
    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    TArray<FDWCDataUVSlotWarning> SlotWarnings;

    /** Signature of the mesh inputs used to generate this output. */
    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    FString MeshInputSignature;

    /** Signature of the actual DWC UV Channel stored on DWCSkeletalMesh. */
    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    FString DataUVOutputSignature;

    /**
     * Generated Data-UV island membership indexed by render-buffer TriangleID.
     * Editor surface hit tests use this compact lookup without rebuilding UV connectivity.
     */
#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
    TArray<int32> DataUVIslandIDByTriangleID;
#endif

    UPROPERTY(VisibleAnywhere, Category = "DWC UV Channel")
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

    /** Resolution-dependent GPU texel lookup and seam maps. Built as part of Build for Runtime > Build GPU Runtime Data. */
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
