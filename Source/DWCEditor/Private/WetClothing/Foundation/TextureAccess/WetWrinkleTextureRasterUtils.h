//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

namespace WetWrinkleTextureRaster
{
    inline constexpr int32 InternalBakeResolution = 2048;
    inline constexpr int32 MinTextureResolution = 16;
    inline constexpr int32 MaxTextureResolution = 4096;

    inline FIntPoint ResolveFinalTextureSize(const int32 Resolution)
    {
        const int32 ClampedResolution = FMath::Clamp(Resolution, MinTextureResolution, MaxTextureResolution);
        return FIntPoint(ClampedResolution, ClampedResolution);
    }

    inline FIntPoint ResolveWorkingTextureSize(const FIntPoint& FinalTextureSize)
    {
        const int32 WorkingResolution = FMath::Max(
            InternalBakeResolution,
            FMath::Max(FinalTextureSize.X, FinalTextureSize.Y));
        return FIntPoint(WorkingResolution, WorkingResolution);
    }
}
