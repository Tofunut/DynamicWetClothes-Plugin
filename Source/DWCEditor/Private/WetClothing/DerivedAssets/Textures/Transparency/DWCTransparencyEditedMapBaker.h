#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;
struct FDWCTransparencyAutoBakeResult;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyEditedMapBakeResult
{
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    FString WarningMessage;
};

class FDWCTransparencyEditedMapBaker
{
  public:
    static bool IsAutoResultCompatible(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        FString& OutReason);

    static bool Bake(
        UWetClothingAsset& WetClothingAsset,
        FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        FDWCTransparencyEditedMapBakeResult& OutResult,
        FString& OutErrorMessage);
};
