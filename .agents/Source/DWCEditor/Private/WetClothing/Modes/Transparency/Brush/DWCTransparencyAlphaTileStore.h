#pragma once

#include "CoreMinimal.h"

struct FDWCTransparencyAlphaTilePayload
{
    FIntPoint TileCoordinate = FIntPoint::ZeroValue;
    FIntRect Rect;
    TArray<uint8> Premultiplied;
    TArray<uint8> Weight;

    bool IsValidFor(const FIntPoint& Resolution, int32 TileSize) const;
    uint64 GetAllocatedBytes() const;
};

/**
 * Sparse CPU-only derived state for authored transparency alpha. Missing
 * tiles represent the zero manual override and therefore need no allocation.
 */
class FDWCTransparencyAlphaTileStore final
{
  public:
    static constexpr int32 TileSize = 128;

    void Initialize(const FIntPoint& InResolution);
    void Reset();

    bool IsValid() const { return Resolution.X > 0 && Resolution.Y > 0; }
    const FIntPoint& GetResolution() const { return Resolution; }
    uint64 GetRevision() const { return Revision; }
    int32 GetTileCount() const { return Tiles.Num(); }
    uint64 GetAllocatedBytes() const;

    uint8 GetPremultiplied(int32 PixelIndex) const;
    uint8 GetWeight(int32 PixelIndex) const;
    void SetPixel(int32 X, int32 Y, uint8 Premultiplied, uint8 Weight);

    FIntRect GetTileRect(const FIntPoint& TileCoordinate) const;
    void GatherTileCoordinates(
        TConstArrayView<FIntRect> Regions,
        bool bIncludeOnePixelHalo,
        bool bWrap,
        TArray<FIntPoint>& OutTileCoordinates) const;
    void SnapshotTiles(
        const TArray<FIntPoint>& TileCoordinates,
        TArray<FDWCTransparencyAlphaTilePayload>& OutTiles) const;

    bool CanCommit(
        uint64 ExpectedRevision,
        const TArray<FDWCTransparencyAlphaTilePayload>& Payloads) const;
    bool Commit(
        uint64 ExpectedRevision,
        const TArray<FDWCTransparencyAlphaTilePayload>& Payloads);

    void BuildFromDense(
        const TArray<uint8>& Premultiplied,
        const TArray<uint8>& Weight);
    void BuildDense(
        TArray<uint8>& OutPremultiplied,
        TArray<uint8>& OutWeight) const;

  private:
    struct FTile
    {
        TArray<uint8> Premultiplied;
        TArray<uint8> Weight;
    };

    const FTile* FindTileForPixel(int32 X, int32 Y, int32& OutLocalIndex) const;
    FTile& FindOrAddTile(const FIntPoint& TileCoordinate);
    bool IsTileAllZero(const FTile& Tile) const;

    FIntPoint Resolution = FIntPoint::ZeroValue;
    TMap<FIntPoint, FTile> Tiles;
    uint64 Revision = 0;
};
