#pragma once

#include "CoreMinimal.h"
#include "DynamicWetSourceTypes.generated.h"

UENUM(BlueprintType)
enum class EDWCSourceBindingType : uint8
{
	Auto UMETA(DisplayName = "Auto"),
	Manual UMETA(DisplayName = "Manual"),
	UnrealWater UMETA(DisplayName = "Unreal Water"),
	Niagara UMETA(DisplayName = "Niagara"),
	Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class EDWCInfluenceType : uint8
{
	Volume UMETA(DisplayName = "Volume"),
	Directional UMETA(DisplayName = "Directional"),
	Spray UMETA(DisplayName = "Spray"),
	Stream UMETA(DisplayName = "Stream"),
	Burst UMETA(DisplayName = "Burst")
};

UENUM(BlueprintType)
enum class EDWCInfluenceShape : uint8
{
	Sphere UMETA(DisplayName = "Sphere"),
	Box UMETA(DisplayName = "Box"),
	Capsule UMETA(DisplayName = "Capsule"),
	Cone UMETA(DisplayName = "Cone"),
	Plane UMETA(DisplayName = "Plane"),
	Custom UMETA(DisplayName = "Custom")
};

USTRUCT(BlueprintType)
struct DYNAMICWETCLOTHES_API FDWCWetSourceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	EDWCSourceBindingType SourceBinding = EDWCSourceBindingType::Manual;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	EDWCInfluenceType InfluenceType = EDWCInfluenceType::Volume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	EDWCInfluenceShape InfluenceShape = EDWCInfluenceShape::Box;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	FVector Direction = FVector(0.0, 0.0, -1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source", meta = (ClampMin = "0.0"))
	float Intensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source", meta = (ClampMin = "0.0"))
	float Radius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source", meta = (ClampMin = "0.0"))
	float Range = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source", meta = (ClampMin = "0.0"))
	float Falloff = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	FBox WorldBounds = FBox(ForceInit);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	float WaterLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	bool bUseSourceSurfaceHeightQuery = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source", meta = (ClampMin = "0.0"))
	float FullWetDepth = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	bool bAffectBelowWaterOnly = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	bool bUseNormalExposure = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	bool bRequireLineOfSight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Source")
	bool bIsValid = false;
};
