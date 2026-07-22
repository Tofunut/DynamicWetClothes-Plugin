#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

class FWetWrinkleBakeService
{
public:
    static bool BakeAllWrinkleMaps(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings = nullptr);
    static void RefreshBakeStatusFromCurrentOutputs(UWetClothingAsset* WetClothingAsset, const FString& Failure = FString());
};
