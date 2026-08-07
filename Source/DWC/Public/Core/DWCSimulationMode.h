//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DWCSimulationMode.generated.h"

UENUM(BlueprintType)
enum class EDWCSimulationMode : uint8
{
    VertexCPU UMETA(DisplayName = "Vertex (CPU)"),
    WetnessMapGPU UMETA(DisplayName = "Wetness Map (GPU)")
};
