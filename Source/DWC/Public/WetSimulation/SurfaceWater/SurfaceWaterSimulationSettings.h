#pragma once

#include "CoreMinimal.h"
#include "SurfaceWaterSimulationSettings.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterMaterialSlotData
{
    GENERATED_BODY()

    /**
     * Mesh UV channel used to sample repeating Droplet/Rivulet normal textures.
     * INDEX_NONE falls back to the WCA Original UV channel.
     */
    UPROPERTY(EditAnywhere, Category="Surface Water|Material Slot|Rendering", meta=(ClampMin="-1", ClampMax="7"))
    int32 SurfaceWaterNormalUVChannel = INDEX_NONE;
};
