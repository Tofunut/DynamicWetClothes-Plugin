#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace WetTransparencyPreviewMaterialParameters
{
    static const FName PreviewWetness(TEXT("DWC_PreviewWetness"));
}

struct FWetTransparencyPreviewMaterialBuildArgs
{
    TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
    int32 UVChannelIndex = INDEX_NONE;
};

struct FWetTransparencyPreviewMaterialBuildResult
{
    bool bSucceeded = false;
    FString ErrorMessage;
    TObjectPtr<UMaterial> TransientBaseMaterial = nullptr;
    TObjectPtr<UMaterialInterface> TransientMaterialParent = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> PreviewMID = nullptr;
};

class FWetTransparencyPreviewMaterialBuilder
{
  public:
    static FWetTransparencyPreviewMaterialBuildResult Build(
        const FWetTransparencyPreviewMaterialBuildArgs& Args);
};
