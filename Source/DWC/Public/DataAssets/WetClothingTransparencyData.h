#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "WetClothingTransparencyData.generated.h"

class AActor;
class UMaterialInterface;
class UTexture2D;

USTRUCT(BlueprintType)
struct DWC_API FWetClothingBakedTransparencyRevealLayer
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FName LayerId;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> LookupMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> ColorMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> MaskMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UTexture2D> ConfidenceMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    TObjectPtr<UMaterialInterface> RevealMaterial = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Transparency")
    FGuid BakeGuid;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingTransparencyData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Bake")
    TSoftClassPtr<AActor> SourceBlueprintClass;

    UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "16", UIMin = "128", UIMax = "4096"))
    int32 RevealBakeResolution = 2048;

    UPROPERTY(EditAnywhere, Category = "Bake", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
    float RevealMaskFeatherRadiusPixels = 4.0f;

    UPROPERTY(VisibleAnywhere, Category = "Baked")
    TArray<FWetClothingBakedTransparencyRevealLayer> BakedRevealLayers;
};
