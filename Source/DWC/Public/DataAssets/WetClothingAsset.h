#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/ObjectSaveContext.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationSettings.h"
#include "WetClothingAsset.generated.h"

class USkeletalMesh;

UCLASS(BlueprintType)
class DWC_API UWetClothingAsset : public UDataAsset
{
    GENERATED_BODY()

  public:
#if WITH_EDITOR
    virtual void PreSave(FObjectPreSaveContext SaveContext) override;
    bool RebuildPrecomputedSimulationData(FString* OutErrorMessage = nullptr, int32 LODIndex = 0);
#endif
    void ClearPrecomputedSimulationData();
    bool IsPrecomputedSimulationDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0) const;
    const FWetClothingPrecomputedSimulationData& GetPrecomputedSimulationData() const { return PartData.PrecomputedSimulationData; }

    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TObjectPtr<USkeletalMesh> TargetMesh = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Part")
    FWetClothingPartData PartData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Wrinkle")
    FWetClothingWrinkleData WrinkleData;

    UPROPERTY(EditAnywhere, Category = "Wet Clothing|Transparency")
    FWetClothingTransparencyData TransparencyData;

    UPROPERTY(EditAnywhere, Category = "Surface Water", meta = (ShowOnlyInnerProperties))
    FSurfaceWaterSimulationSettings SurfaceWaterSettings;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wet Clothing")
    TArray<FString> AdditionalProfileSearchPaths;
#endif
};
