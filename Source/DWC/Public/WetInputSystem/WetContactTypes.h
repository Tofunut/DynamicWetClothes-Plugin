//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetContactTypes.generated.h"

USTRUCT(BlueprintType, meta = (DisplayName = "DWC Wet Contact"))
struct DWC_API FDWCWetContact
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ToolTip = "Wetness amount to add at the contact. Negative values remove wetness."))
    float Amount = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ToolTip = "World-space contact center."))
    FVector Location = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ToolTip = "Optional world-space surface normal for contact filtering. Leave zero to skip normal filtering."))
    FVector Normal = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ClampMin = "0.0", ToolTip = "World-space radius around Location affected by this contact."))
    float Radius = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ToolTip = "Optional world-space incoming wetness direction, such as rain or splash travel direction. Leave zero for direction-independent contact wetness."))
    FVector Direction = FVector::DownVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (AdvancedDisplay, ToolTip = "Optional hit bone used to narrow the vertex search. Leave None for automatic/full search."))
    FName BoneName = NAME_None;

    /** Optional exact simulation-LOD index-buffer triangle ID from a trace hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (AdvancedDisplay, ToolTip = "Optional exact simulation-LOD index-buffer triangle ID from a trace hit. Leave INDEX_NONE when unavailable."))
    int32 RenderTriangleID = INDEX_NONE;

    /** Optional material-slot restriction for the contact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (AdvancedDisplay, ToolTip = "Optional material-slot restriction for this contact. Leave INDEX_NONE to allow every material slot."))
    int32 MaterialSlotIndex = INDEX_NONE;
};

USTRUCT(BlueprintType, meta = (DisplayName = "DWC Wet Area"))
struct DWC_API FDWCWetAreaData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (ToolTip = "Wetness amount distributed across sampled vertices. Negative values remove wetness."))
    float Amount = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (ToolTip = "World-space incoming wetness direction. Used for normal exposure when enabled."))
    FVector Direction = FVector::DownVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (ClampMin = "1", ToolTip = "Number of receiver vertices to sample for this area input. Higher values cover more cloth but cost more."))
    int32 SampleCount = 300;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (ToolTip = "When enabled, surfaces facing the incoming direction receive more wetness than back-facing surfaces."))
    bool bUseNormalExposure = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (AdvancedDisplay, EditCondition = "bUseNormalExposure", ToolTip = "When normal exposure is enabled, use current skinned normals when available instead of static mesh normals."))
    bool bUseSkinnedNormalsForExposure = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (AdvancedDisplay, ToolTip = "Use a fixed random seed for deterministic vertex sampling."))
    bool bOverrideRandomSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (AdvancedDisplay, EditCondition = "bOverrideRandomSeed", ToolTip = "Random seed used when Override Random Seed is enabled."))
    int32 RandomSeed = 0;
};

USTRUCT(BlueprintType, meta = (DisplayName = "DWC Water Surface"))
struct DWC_API FDWCWaterSurfaceData
{
    GENERATED_BODY()

    FDWCWaterSurfaceData();
    ~FDWCWaterSurfaceData();
    FDWCWaterSurfaceData(const FDWCWaterSurfaceData& Other);
    FDWCWaterSurfaceData(FDWCWaterSurfaceData&& Other);
    FDWCWaterSurfaceData& operator=(const FDWCWaterSurfaceData& Other);
    FDWCWaterSurfaceData& operator=(FDWCWaterSurfaceData&& Other);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ToolTip = "World-space XY bounds covered by the surface height grid."))
    FBox Bounds = FBox(ForceInit);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ClampMin = "2", ToolTip = "Number of samples along the X axis. Must match SurfaceZ and Valid array sizes."))
    int32 SizeX = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ClampMin = "2", ToolTip = "Number of samples along the Y axis. Must match SurfaceZ and Valid array sizes."))
    int32 SizeY = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ToolTip = "World-space Z height for each grid sample. Expected size is SizeX * SizeY."))
    TArray<float> SurfaceZ;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ToolTip = "Per-sample validity mask. Expected size is SizeX * SizeY. Non-zero samples are valid."))
    TArray<uint8> Valid;

    int32 GetSampleIndex(int32 X, int32 Y) const;
    bool  IsValidSampleIndex(int32 X, int32 Y) const;
};
