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

    int32 GetSampleIndex(const int32 X, const int32 Y) const
    {
        return Y * SizeX + X;
    }

    bool IsValidSampleIndex(const int32 X, const int32 Y) const
    {
        const int32 SampleIndex = GetSampleIndex(X, Y);
        return X >= 0 &&
               Y >= 0 &&
               X < SizeX &&
               Y < SizeY &&
               SurfaceZ.IsValidIndex(SampleIndex) &&
               Valid.IsValidIndex(SampleIndex) &&
               Valid[SampleIndex] != 0;
    }
};
