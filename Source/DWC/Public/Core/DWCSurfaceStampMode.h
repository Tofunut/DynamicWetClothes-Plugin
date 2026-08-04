#pragma once

#include "CoreMinimal.h"
#include "DWCSurfaceStampMode.generated.h"

/** Explicit surface-water stamp path used for performance experiments. */
UENUM(BlueprintType)
enum class EDWCSurfaceStampMode : uint8
{
    Legacy UMETA(DisplayName = "Legacy (One Pass Per Stamp)"),
    TileBatch UMETA(DisplayName = "Tile Batch Dense (Experimental Baseline)"),
    TileBatchSparse UMETA(DisplayName = "Tile Batch Sparse (Touched Tiles)")
};
