#pragma once

#include "CoreMinimal.h"
#include "DynamicWetContactTypes.generated.h"

USTRUCT(BlueprintType)
struct DYNAMICWETCLOTHES_API FDWCWetContact
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
};

USTRUCT(BlueprintType)
struct DYNAMICWETCLOTHES_API FDWCWetRainData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain")
    float Amount = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain")
    FVector Direction = FVector::DownVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain", meta = (ClampMin = "1"))
    int32 SampleCount = 300;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain")
    bool bUseNormalExposure = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain")
    bool bUseSkinnedNormalsForExposure = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain")
    bool bOverrideRandomSeed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rain", meta = (EditCondition = "bOverrideRandomSeed"))
    int32 RandomSeed = 0;
};

USTRUCT(BlueprintType)
struct DYNAMICWETCLOTHES_API FDWCWetSurfaceData
{
    GENERATED_BODY()

    FDWCWetSurfaceData();
    ~FDWCWetSurfaceData();
    FDWCWetSurfaceData(const FDWCWetSurfaceData& Other);
    FDWCWetSurfaceData(FDWCWetSurfaceData&& Other);
    FDWCWetSurfaceData& operator=(const FDWCWetSurfaceData& Other);
    FDWCWetSurfaceData& operator=(FDWCWetSurfaceData&& Other);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Surface")
    FBox Bounds = FBox(ForceInit);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Surface", meta = (ClampMin = "2"))
    int32 SizeX = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Surface", meta = (ClampMin = "2"))
    int32 SizeY = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Surface")
    TArray<float> SurfaceZ;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Surface")
    TArray<uint8> Valid;

    int32 GetSampleIndex(int32 X, int32 Y) const;
    bool IsValidSampleIndex(int32 X, int32 Y) const;
};
