// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCRenderProfileBuildTargetResolver.h"

class UWetClothingAsset;

class FWetClothingRenderProfileBakeService
{
  public:
    static bool HasPendingVisualBakeTasks(const UWetClothingAsset* WetClothingAsset, FString* OutSummary = nullptr);
    static bool BakeRenderProfileDataAndUpdateMaterials(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings = nullptr);
    static bool SaveBakedRenderProfileAssets(UWetClothingAsset* WetClothingAsset);
};
