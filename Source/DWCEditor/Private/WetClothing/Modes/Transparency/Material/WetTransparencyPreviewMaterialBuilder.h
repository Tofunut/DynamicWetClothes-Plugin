#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace WetTransparencyPreviewMaterialParameters
{
    static const FName PreviewWetness(TEXT("DWC_PreviewWetness"));
    static const FName TransparencyMap(TEXT("DWC_TransparencyPreviewMap"));
    static const FName UseTransparencyMap(TEXT("DWC_UseTransparencyPreviewMap"));
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
