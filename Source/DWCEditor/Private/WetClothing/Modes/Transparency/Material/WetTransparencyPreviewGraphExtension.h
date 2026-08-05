#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInstanceDynamic;
struct FDWCSurfaceGraphBuildResult;

/** Adds the Transparency Editor working-map layer after the common DWC surface graph. */
class FWetTransparencyPreviewGraphExtension
{
  public:
    static constexpr uint32 GraphSchemaVersion = 5;

    static bool ExtendGraph(
        UMaterial* Material,
        const FDWCSurfaceGraphBuildResult& SurfaceGraph,
        FString& OutErrorMessage);

    static void InitializeMID(
        int32 MaterialSlotIndex,
        UMaterialInstanceDynamic& PreviewMID);
};
