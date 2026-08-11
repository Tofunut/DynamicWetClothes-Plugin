// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetRendering/DWCWetVertexColorContract.h"

namespace DWCWetVertexColorContract
{
    const FName& CPUWetnessOutput()
    {
        static const FName Name(TEXT("R"));
        return Name;
    }

    FLinearColor Encode(const float Wetness, const FLinearColor& WetPartDebugColor)
    {
        return FLinearColor(
            FMath::Clamp(Wetness, 0.0f, 1.0f),
            FMath::Clamp(WetPartDebugColor.R, 0.0f, 1.0f),
            FMath::Clamp(WetPartDebugColor.G, 0.0f, 1.0f),
            FMath::Clamp(WetPartDebugColor.B, 0.0f, 1.0f));
    }
} // namespace DWCWetVertexColorContract
