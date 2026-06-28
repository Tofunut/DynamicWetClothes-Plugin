#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfile.generated.h"

USTRUCT(BlueprintType)
struct FWetnessProfileParameters
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
	float Absorption = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
	float SpreadRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
	float DryRate = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
	float GravityFlowStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
	float SurfaceWaterStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
	float RunoffStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rendering")
	float WetVisualStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rendering")
	float TransparencyStrength = 0.0f;
};

UCLASS(BlueprintType)
class DYNAMICWETCLOTHES_API UWetnessProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
	FWetnessProfileParameters Parameters;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Wetness Profile")
	FString PreferredSaveDirectory = TEXT("/Game/WetnessProfiles");
#endif
};
