#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

/**
 * Transient, sparse index of the tiles touched by the stroke currently being
 * authored. It intentionally contains no WCA ownership: the controller owns
 * the authoritative stroke and commits it only on mouse-up.
 */
class FDWCTransparencyLiveStrokeLayer final
{
  public:
    static constexpr int32 TileSize = 128;

    void Begin(const FGuid& InStrokeGuid, const FIntPoint& InResolution);
    void Reset();

    void RecordSample(
        const FDWCTransparencyBrushSample& Sample,
        EDWCTransparencyUVAddressMode AddressMode);

    bool IsActive() const { return StrokeGuid.IsValid(); }
    bool IsForStroke(const FGuid& InStrokeGuid) const { return StrokeGuid == InStrokeGuid; }
    int32 GetTileCount() const { return Tiles.Num(); }
    int32 GetSampleCount() const { return Samples.Num(); }
    uint64 GetAllocatedBytes() const;
    const TArray<FIntRect>& GetDirtyRegions() const { return DirtyRegions; }

  private:
    struct FTile
    {
        TArray<int32> SampleIndices;
    };

    void AddTileSample(int32 TileX, int32 TileY, int32 SampleIndex);
    void AddDirtyRegion(const FIntRect& Region, bool bWrap);

    FGuid StrokeGuid;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    TArray<FDWCTransparencyBrushSample> Samples;
    TMap<FIntPoint, FTile> Tiles;
    TArray<FIntRect> DirtyRegions;
};
