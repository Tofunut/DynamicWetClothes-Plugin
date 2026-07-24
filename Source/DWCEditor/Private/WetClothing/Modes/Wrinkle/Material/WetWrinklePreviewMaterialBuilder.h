#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;

namespace WetWrinklePreviewMaterialParameters
{
    extern const FName PreviewWetness;
    extern const FName AccumulatedNormal;
    extern const FName AccumulatedEnabled;
    extern const FName AccumulatedStrength;
    extern const FName TransientRidgeNormal;
    extern const FName TransientRidgeEnabled;
    extern const FName HoverNormal;
    extern const FName HoverEnabled;
    extern const FName HoverCenterUV;
    extern const FName HoverRadiusUV;
    extern const FName HoverRotation;
    extern const FName HoverScale;
    extern const FName HoverStrength;
    extern const FName HoverFalloff;
}

struct FWetWrinklePreviewMaterialBuildArgs
{
    UMaterialInterface* SourceMaterial = nullptr;
    int32 UVChannelIndex = INDEX_NONE;
    bool bOverrideCpuWetnessInput = false;
    bool bBuildNormalOverlay = true;
};

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
    static FWetWrinklePreviewMaterialBuildResult Build(const FWetWrinklePreviewMaterialBuildArgs& Args);
};
