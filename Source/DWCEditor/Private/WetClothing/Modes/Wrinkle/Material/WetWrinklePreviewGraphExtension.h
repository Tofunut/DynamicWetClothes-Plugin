//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
struct FDWCSurfaceGraphBuildResult;

/** Adds the Wrinkle Editor normal layers to a common DWC editor preview graph. */
class FWetWrinklePreviewGraphExtension
{
  public:
    static constexpr uint32 GraphSchemaVersion = 3;

    static bool ExtendGraph(
        UMaterial* Material,
        const FDWCSurfaceGraphBuildResult& SurfaceGraph,
        FString& OutErrorMessage);

    static void InitializeMID(
        int32 MaterialSlotIndex,
        UMaterialInstanceDynamic& PreviewMID);
};
