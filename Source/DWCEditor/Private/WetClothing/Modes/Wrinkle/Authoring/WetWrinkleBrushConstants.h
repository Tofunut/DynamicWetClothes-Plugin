//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

namespace WetWrinkleBrushConstants
{
    inline constexpr float DefaultSizeCm = 8.0f;
    inline constexpr float DefaultRadiusUV = 0.0677f;
    inline constexpr float UVPerCm = DefaultRadiusUV / DefaultSizeCm;
    inline constexpr float MaxSizeCm = 40.0f;
    inline constexpr float MaxRadiusUV = MaxSizeCm * UVPerCm;
}
