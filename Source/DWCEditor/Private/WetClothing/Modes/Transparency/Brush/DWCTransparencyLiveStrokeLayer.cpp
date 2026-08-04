#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"

namespace
{
    int32 FloorDivide(const int32 Value, const int32 Divisor)
    {
        check(Divisor > 0);
        return Value >= 0 ? Value / Divisor : -(((-Value) + Divisor - 1) / Divisor);
    }

    int32 PositiveModulo(const int32 Value, const int32 Divisor)
    {
        return (Value % Divisor + Divisor) % Divisor;
    }
}

void FDWCTransparencyLiveStrokeLayer::Begin(const FGuid& InStrokeGuid, const FIntPoint& InResolution)
{
    Reset();
    if (!InStrokeGuid.IsValid() || InResolution.X <= 0 || InResolution.Y <= 0)
    {
        return;
    }

    StrokeGuid = InStrokeGuid;
    Resolution = InResolution;
}

void FDWCTransparencyLiveStrokeLayer::Reset()
{
    StrokeGuid.Invalidate();
    Resolution = FIntPoint::ZeroValue;
    // A finished or canceled stroke has no reuse guarantee. Release the sparse
    // index immediately so a one-off 4K stroke cannot remain resident.
    Samples.Empty();
    Tiles.Empty();
    DirtyRegions.Empty();
}

void FDWCTransparencyLiveStrokeLayer::RecordSample(
    const FDWCTransparencyBrushSample& Sample,
    const EDWCTransparencyUVAddressMode AddressMode)
{
    if (!IsActive() || Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return;
    }

    const int32 SampleIndex = Samples.Add(Sample);
    const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Resolution.X, 1.0f);
    const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Resolution.Y, 1.0f);
    const float CenterX = Sample.PositionUV.X * Resolution.X;
    const float CenterY = Sample.PositionUV.Y * Resolution.Y;
    const int32 MinX = FMath::FloorToInt(CenterX - RadiusPixelsX - 1.0f);
    const int32 MaxX = FMath::CeilToInt(CenterX + RadiusPixelsX + 1.0f);
    const int32 MinY = FMath::FloorToInt(CenterY - RadiusPixelsY - 1.0f);
    const int32 MaxY = FMath::CeilToInt(CenterY + RadiusPixelsY + 1.0f);
    const bool bWrap = AddressMode == EDWCTransparencyUVAddressMode::Wrap;

    const int32 TileCountX = FMath::DivideAndRoundUp(Resolution.X, TileSize);
    const int32 TileCountY = FMath::DivideAndRoundUp(Resolution.Y, TileSize);
    const int32 MinTileX = FloorDivide(MinX, TileSize);
    const int32 MaxTileX = FloorDivide(MaxX, TileSize);
    const int32 MinTileY = FloorDivide(MinY, TileSize);
    const int32 MaxTileY = FloorDivide(MaxY, TileSize);

    for (int32 RawTileY = MinTileY; RawTileY <= MaxTileY; ++RawTileY)
    {
        if (!bWrap && (RawTileY < 0 || RawTileY >= TileCountY))
        {
            continue;
        }
        const int32 TileY = bWrap ? PositiveModulo(RawTileY, TileCountY) : RawTileY;
        for (int32 RawTileX = MinTileX; RawTileX <= MaxTileX; ++RawTileX)
        {
            if (!bWrap && (RawTileX < 0 || RawTileX >= TileCountX))
            {
                continue;
            }
            const int32 TileX = bWrap ? PositiveModulo(RawTileX, TileCountX) : RawTileX;
            AddTileSample(TileX, TileY, SampleIndex);
        }
    }

    AddDirtyRegion(FIntRect(MinX, MinY, MaxX + 1, MaxY + 1), bWrap);
}

uint64 FDWCTransparencyLiveStrokeLayer::GetAllocatedBytes() const
{
    uint64 Bytes = Samples.GetAllocatedSize() + Tiles.GetAllocatedSize() + DirtyRegions.GetAllocatedSize();
    for (const TPair<FIntPoint, FTile>& Pair : Tiles)
    {
        Bytes += Pair.Value.SampleIndices.GetAllocatedSize();
    }
    return Bytes;
}

void FDWCTransparencyLiveStrokeLayer::AddTileSample(const int32 TileX, const int32 TileY, const int32 SampleIndex)
{
    FTile& Tile = Tiles.FindOrAdd(FIntPoint(TileX, TileY));
    Tile.SampleIndices.Add(SampleIndex);
}

void FDWCTransparencyLiveStrokeLayer::AddDirtyRegion(const FIntRect& Region, const bool bWrap)
{
    if (Region.IsEmpty())
    {
        return;
    }

    if (!bWrap)
    {
        DirtyRegions.Add(FIntRect(
            FMath::Clamp(Region.Min.X, 0, Resolution.X),
            FMath::Clamp(Region.Min.Y, 0, Resolution.Y),
            FMath::Clamp(Region.Max.X, 0, Resolution.X),
            FMath::Clamp(Region.Max.Y, 0, Resolution.Y)));
        return;
    }

    // Keep the raw region here. The viewport already owns the authoritative
    // wrap splitter used by render uploads and can convert this without
    // duplicating texture-addressing policy in the live layer.
    DirtyRegions.Add(Region);
}
