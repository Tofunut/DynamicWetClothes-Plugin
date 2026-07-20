#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInterface;

namespace WetTransparencyPreviewMaterialParameters
{
    static const FName Enabled(TEXT("DWC_TransparencyPreviewEnabled"));
    static const FName Map(TEXT("DWC_TransparencyPreviewMap"));
    static const FName Strength(TEXT("DWC_TransparencyPreviewStrength"));
    static const FName Wetness(TEXT("DWC_TransparencyPreviewWetness"));
    static const FName UVChannel(TEXT("DWC_TransparencyPreviewUVChannel"));
    static const FName Debug(TEXT("DWC_TransparencyPreviewDebug"));
}

struct FWetTransparencyPreviewMaterialBuildArgs
{
    TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
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
