//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"

namespace WetClothingTextureAddressUtils
{
    inline double Apply(double Value, double IslandCenter, TextureAddress AddressMode)
    {
        switch (AddressMode)
        {
        case TA_Wrap:
            return Value - FMath::FloorToDouble(IslandCenter);

        case TA_Mirror:
        {
            const int64  TileIndex = FMath::FloorToInt64(IslandCenter);
            const double TileValue = Value - static_cast<double>(TileIndex);
            return FMath::Abs(TileIndex) % 2 == 0 ? TileValue : 1.0 - TileValue;
        }

        case TA_Clamp:
        default:
            return FMath::Clamp(Value, 0.0, 1.0);
        }
    }
} // namespace WetClothingTextureAddressUtils
