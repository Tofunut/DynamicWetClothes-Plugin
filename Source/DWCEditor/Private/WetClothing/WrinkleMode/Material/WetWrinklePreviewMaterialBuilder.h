#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;

namespace WetWrinklePreviewMaterialParameters
{
    extern const FName UVChannel;
    extern const FName AccumulatedNormal;
    extern const FName AccumulatedEnabled;
    extern const FName AccumulatedStrength;
    extern const FName HoverNormal;
    extern const FName HoverEnabled;
    extern const FName HoverCenterUV;
    extern const FName HoverRadiusUV;
    extern const FName HoverRotation;
    extern const FName HoverScale;
    extern const FName HoverStrength;
    extern const FName HoverFalloff;
}

struct FWetWrinklePreviewMaterialBuildResult
{
    UMaterial* TransientBaseMaterial = nullptr;
    UMaterialInterface* TransientMaterialParent = nullptr;
    UMaterialInstanceDynamic* PreviewMID = nullptr;
    bool bSucceeded = false;
    FString ErrorMessage;
};

class FWetWrinklePreviewMaterialBuilder
{
  public:
    static FWetWrinklePreviewMaterialBuildResult Build(UMaterialInterface* SourceMaterial);
};
