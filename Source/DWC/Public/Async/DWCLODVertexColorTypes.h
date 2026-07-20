#pragma once

#include "CoreMinimal.h"

struct DWC_API FDWCLODVertexColorTransferSettings
{
    float MaxNormalAngleDot = 0.35f;
    float DistanceTieTolerance = 0.01f;
    float InitialSearchRadius = 10.0f;
    float MaxSearchRadius = 200.0f;
};
