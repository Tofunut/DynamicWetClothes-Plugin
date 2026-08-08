// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDWCTransparencyRevealColorTilePayload
{
    FIntPoint      TileCoordinate = FIntPoint::ZeroValue;
    FIntRect       Rect;
    TArray<FColor> Colors;

    bool   IsValidFor(const FIntPoint& Resolution, int32 TileSize) const;
    uint64 GetAllocatedBytes() const;
};

/**
 * Sparse CPU-only derived state for authored reveal color. Missing tiles
 * resolve to the immutable auto-bake InnerColorBuffer supplied by the caller.
 */
class FDWCTransparencyRevealColorTileStore final
{
  public:
    static constexpr int32 TileSize = 128;

    void Initialize(const FIntPoint& InResolution);
    void Reset();

    bool             IsValid() const { return Resolution.X > 0 && Resolution.Y > 0; }
    const FIntPoint& GetResolution() const { return Resolution; }
    uint64           GetRevision() const { return Revision; }
    int32            GetTileCount() const { return Tiles.Num(); }
    uint64           GetAllocatedBytes() const;

    FColor   GetColor(int32 PixelIndex, TConstArrayView<FColor> BaseColors) const;
    void     SetColor(int32 X, int32 Y, FColor Color, TConstArrayView<FColor> BaseColors);
    FIntRect GetTileRect(const FIntPoint& TileCoordinate) const;
    void     GatherTileCoordinates(
            TConstArrayView<FIntRect> Regions,
            bool                      bIncludeOnePixelHalo,
            bool                      bWrap,
            TArray<FIntPoint>&        OutTileCoordinates) const;
    void SnapshotTiles(
        const TArray<FIntPoint>&                        TileCoordinates,
        TConstArrayView<FColor>                         BaseColors,
        TArray<FDWCTransparencyRevealColorTilePayload>& OutTiles) const;

    bool CanCommit(
        uint64                                                ExpectedRevision,
        const TArray<FDWCTransparencyRevealColorTilePayload>& Payloads,
        TConstArrayView<FColor>                               BaseColors) const;
    bool Commit(
        uint64                                                ExpectedRevision,
        const TArray<FDWCTransparencyRevealColorTilePayload>& Payloads,
        TConstArrayView<FColor>                               BaseColors);

    void BuildFromDense(
        TConstArrayView<FColor> Colors,
        TConstArrayView<FColor> BaseColors);

  private:
    const TArray<FColor>* FindTileForPixel(int32 X, int32 Y, int32& OutLocalIndex) const;
    bool                  IsTileEqualToBase(
                         const FIntPoint&        TileCoordinate,
                         TConstArrayView<FColor> Colors,
                         TConstArrayView<FColor> BaseColors) const;

    FIntPoint                       Resolution = FIntPoint::ZeroValue;
    TMap<FIntPoint, TArray<FColor>> Tiles;
    uint64                          Revision = 0;
};
