#pragma once

#include "CoreMinimal.h"
#include "WetContactTypes.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FDWCWetContact
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    float Amount = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    FVector Location = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    FVector Normal = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact", meta = (ClampMin = "0.0"))
    float Radius = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    FVector Direction = FVector::DownVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    FName BoneName = NAME_None;

    /** Optional exact simulation-LOD index-buffer triangle ID from a trace hit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    int32 RenderTriangleID = INDEX_NONE;

    /** Optional material-slot restriction for the contact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Contact")
    int32 MaterialSlotIndex = INDEX_NONE;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCWetAreaData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area")
    float Amount = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area")
    FVector Direction = FVector::DownVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (ClampMin = "1"))
    int32 SampleCount = 300;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area")
    bool bUseNormalExposure = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area")
    bool bUseSkinnedNormalsForExposure = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area")
    bool bOverrideRandomSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Area", meta = (EditCondition = "bOverrideRandomSeed"))
    int32 RandomSeed = 0;
};

USTRUCT(BlueprintType)
struct DWC_API FDWCWaterSurfaceData
{
    GENERATED_BODY()

    FDWCWaterSurfaceData();
    ~FDWCWaterSurfaceData();
    FDWCWaterSurfaceData(const FDWCWaterSurfaceData& Other);
    FDWCWaterSurfaceData(FDWCWaterSurfaceData&& Other);
    FDWCWaterSurfaceData& operator=(const FDWCWaterSurfaceData& Other);
    FDWCWaterSurfaceData& operator=(FDWCWaterSurfaceData&& Other);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
    FBox Bounds = FBox(ForceInit);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ClampMin = "2"))
    int32 SizeX = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface", meta = (ClampMin = "2"))
    int32 SizeY = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
    TArray<float> SurfaceZ;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
    TArray<uint8> Valid;

    int32 GetSampleIndex(int32 X, int32 Y) const;
    bool  IsValidSampleIndex(int32 X, int32 Y) const;
};
