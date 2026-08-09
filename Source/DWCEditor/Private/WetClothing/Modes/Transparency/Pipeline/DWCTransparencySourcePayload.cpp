//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

int32 FDWCTransparencySourcePayload::ResolveOuterIslandIDAtUV(
    const FVector2D& PositionUV,
    const int32 FallbackUVIslandID,
    const bool bWrap) const
{
    const int32 Width = Resolution.X;
    const int32 Height = Resolution.Y;
    if (Width <= 0 || Height <= 0 || OuterIslandIDBuffer.Num() != Width * Height)
    {
        return FallbackUVIslandID;
    }

    int32 X = FMath::FloorToInt(PositionUV.X * Width);
    int32 Y = FMath::FloorToInt(PositionUV.Y * Height);
    if (bWrap)
    {
        X = (X % Width + Width) % Width;
        Y = (Y % Height + Height) % Height;
    }
    else if (X < 0 || X >= Width || Y < 0 || Y >= Height)
    {
        return INDEX_NONE;
    }

    const int32 RasterIslandID = DecodeOuterIslandID(OuterIslandIDBuffer[Y * Width + X]);
    // Spatial hit IDs are local to the query cache and can differ from the
    // raster workspace. Only use the hit ID when the sampled texel is uncovered.
    return RasterIslandID != INDEX_NONE ? RasterIslandID : FallbackUVIslandID;
}
