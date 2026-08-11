// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Shared producer/consumer contract for DWC runtime vertex colors. */
namespace DWCWetVertexColorContract
{
    /** Material output that carries normalized CPU wetness. */
    DWC_API const FName& CPUWetnessOutput();

    /** Encodes CPU wetness in R and the Wet Part debug color in GBA. */
    DWC_API FLinearColor Encode(float Wetness, const FLinearColor& WetPartDebugColor);
} // namespace DWCWetVertexColorContract
