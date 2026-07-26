#pragma once

#include "CoreMinimal.h"
#include "SurfaceWaterSimulationSettings.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterMaterialSlotData
{
    GENERATED_BODY()

    /**
     * Mesh UV channel used to sample repeating droplet/streak normal textures.
     * INDEX_NONE falls back to the WCA Original UV channel.
     */
    UPROPERTY(EditAnywhere, Category="Surface Water|Material Slot|Rendering", meta=(ClampMin="-1", ClampMax="7"))
    int32 SurfaceWaterNormalUVChannel = INDEX_NONE;

    /** X/Y repeat count for the droplet normal texture. */
    UPROPERTY(EditAnywhere, Category="Surface Water|Material Slot|Rendering", meta=(ClampMin="0.001"))
    FVector2D DropletUVTiling = FVector2D(1.0, 1.0);

    /** Across/along-flow repeat count for the streak normal texture. */
    UPROPERTY(EditAnywhere, Category="Surface Water|Material Slot|Rendering", meta=(ClampMin="0.001", DisplayName="Streak UV Tiling"))
    FVector2D RivuletUVTiling = FVector2D(1.0, 1.0);
};
