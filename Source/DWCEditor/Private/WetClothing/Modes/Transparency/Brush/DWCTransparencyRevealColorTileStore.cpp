// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

namespace
{
    int32 RevealTilePositiveModulo(const int32 Value, const int32 Divisor)
    {
        return (Value % Divisor + Divisor) % Divisor;
    }
} // namespace

bool FDWCTransparencyRevealColorTilePayload::IsValidFor(
    const FIntPoint& Resolution,
    const int32      TileSize) const
{
    if (TileSize <= 0 || Rect.IsEmpty() || Rect.Min.X < 0 || Rect.Min.Y < 0 ||
        Rect.Max.X > Resolution.X || Rect.Max.Y > Resolution.Y ||
        Rect.Min.X / TileSize != TileCoordinate.X || Rect.Min.Y / TileSize != TileCoordinate.Y)
    {
        return false;
    }
    return Colors.Num() == Rect.Width() * Rect.Height();
}

uint64 FDWCTransparencyRevealColorTilePayload::GetAllocatedBytes() const
{
    return Colors.GetAllocatedSize();
}

void FDWCTransparencyRevealColorTileStore::Initialize(const FIntPoint& InResolution)
{
    if (Resolution == InResolution && IsValid())
    {
        return;
    }
    Reset();
    if (InResolution.X > 0 && InResolution.Y > 0)
    {
        Resolution = InResolution;
        ++Revision;
    }
}

void FDWCTransparencyRevealColorTileStore::Reset()
{
    Resolution = FIntPoint::ZeroValue;
    TMap<FIntPoint, TArray<FColor>> EmptyTiles;
    Tiles = MoveTemp(EmptyTiles);
    ++Revision;
}

uint64 FDWCTransparencyRevealColorTileStore::GetAllocatedBytes() const
{
    uint64 Bytes = Tiles.GetAllocatedSize();
    for (const TPair<FIntPoint, TArray<FColor>>& Pair : Tiles)
    {
        Bytes += Pair.Value.GetAllocatedSize();
    }
    return Bytes;
}

FColor FDWCTransparencyRevealColorTileStore::GetColor(
    const int32                   PixelIndex,
    const TConstArrayView<FColor> BaseColors) const
{
    if (!IsValid() || PixelIndex < 0 || PixelIndex >= Resolution.X * Resolution.Y)
    {
        return FColor::Black;
    }
    int32                 LocalIndex = INDEX_NONE;
    const TArray<FColor>* Tile = FindTileForPixel(
        PixelIndex % Resolution.X,
        PixelIndex / Resolution.X,
        LocalIndex);
    return Tile != nullptr && Tile->IsValidIndex(LocalIndex)
               ? (*Tile)[LocalIndex]
           : BaseColors.IsValidIndex(PixelIndex) ? BaseColors[PixelIndex]
                                                 : FColor::Black;
}

void FDWCTransparencyRevealColorTileStore::SetColor(
    const int32                   X,
    const int32                   Y,
    const FColor                  Color,
    const TConstArrayView<FColor> BaseColors)
{
    if (!IsValid() || X < 0 || Y < 0 || X >= Resolution.X || Y >= Resolution.Y ||
        BaseColors.Num() != Resolution.X * Resolution.Y)
    {
        return;
    }
    const FIntPoint Coordinate(X / TileSize, Y / TileSize);
    TArray<FColor>& Tile = Tiles.FindOrAdd(Coordinate);
    const FIntRect  Rect = GetTileRect(Coordinate);
    if (Tile.Num() != Rect.Width() * Rect.Height())
    {
        Tile.SetNumUninitialized(Rect.Width() * Rect.Height());
        for (int32 TileY = Rect.Min.Y; TileY < Rect.Max.Y; ++TileY)
        {
            for (int32 TileX = Rect.Min.X; TileX < Rect.Max.X; ++TileX)
            {
                Tile[(TileY - Rect.Min.Y) * Rect.Width() + TileX - Rect.Min.X] =
                    BaseColors[TileY * Resolution.X + TileX];
            }
        }
    }
    Tile[(Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X] = Color;
}

FIntRect FDWCTransparencyRevealColorTileStore::GetTileRect(const FIntPoint& TileCoordinate) const
{
    if (!IsValid() || TileCoordinate.X < 0 || TileCoordinate.Y < 0)
    {
        return FIntRect();
    }
    const FIntPoint Min(TileCoordinate.X * TileSize, TileCoordinate.Y * TileSize);
    return FIntRect(
        Min,
        FIntPoint(
            FMath::Min(Min.X + TileSize, Resolution.X),
            FMath::Min(Min.Y + TileSize, Resolution.Y)));
}

void FDWCTransparencyRevealColorTileStore::GatherTileCoordinates(
    const TConstArrayView<FIntRect> Regions,
    const bool                      bIncludeOnePixelHalo,
    const bool                      bWrap,
    TArray<FIntPoint>&              OutTileCoordinates) const
{
    OutTileCoordinates.Reset();
    if (!IsValid())
    {
        return;
    }
    TSet<FIntPoint> UniqueCoordinates;
    const int32     TileCountX = FMath::DivideAndRoundUp(Resolution.X, TileSize);
    const int32     TileCountY = FMath::DivideAndRoundUp(Resolution.Y, TileSize);
    for (FIntRect Region : Regions)
    {
        if (bIncludeOnePixelHalo)
        {
            Region.Min -= FIntPoint(1, 1);
            Region.Max += FIntPoint(1, 1);
        }
        const int32 MinTileX = IntCastChecked<int32>(FMath::FloorToInt(static_cast<double>(Region.Min.X) / TileSize));
        const int32 MinTileY = IntCastChecked<int32>(FMath::FloorToInt(static_cast<double>(Region.Min.Y) / TileSize));
        const int32 MaxTileX = IntCastChecked<int32>(FMath::FloorToInt(static_cast<double>(Region.Max.X - 1) / TileSize));
        const int32 MaxTileY = IntCastChecked<int32>(FMath::FloorToInt(static_cast<double>(Region.Max.Y - 1) / TileSize));
        for (int32 RawY = MinTileY; RawY <= MaxTileY; ++RawY)
        {
            for (int32 RawX = MinTileX; RawX <= MaxTileX; ++RawX)
            {
                if (!bWrap && (RawX < 0 || RawY < 0 || RawX >= TileCountX || RawY >= TileCountY))
                {
                    continue;
                }
                UniqueCoordinates.Add(FIntPoint(
                    bWrap ? RevealTilePositiveModulo(RawX, TileCountX) : RawX,
                    bWrap ? RevealTilePositiveModulo(RawY, TileCountY) : RawY));
            }
        }
    }
    OutTileCoordinates.Reserve(UniqueCoordinates.Num());
    for (const FIntPoint& Coordinate : UniqueCoordinates)
    {
        OutTileCoordinates.Add(Coordinate);
    }
    OutTileCoordinates.Sort([](const FIntPoint& A, const FIntPoint& B)
                            { return A.Y == B.Y ? A.X < B.X : A.Y < B.Y; });
}

void FDWCTransparencyRevealColorTileStore::SnapshotTiles(
    const TArray<FIntPoint>&                        TileCoordinates,
    const TConstArrayView<FColor>                   BaseColors,
    TArray<FDWCTransparencyRevealColorTilePayload>& OutTiles) const
{
    OutTiles.Reset(TileCoordinates.Num());
    for (const FIntPoint& Coordinate : TileCoordinates)
    {
        const FIntRect Rect = GetTileRect(Coordinate);
        if (Rect.IsEmpty())
        {
            continue;
        }
        FDWCTransparencyRevealColorTilePayload& Payload = OutTiles.AddDefaulted_GetRef();
        Payload.TileCoordinate = Coordinate;
        Payload.Rect = Rect;
        if (const TArray<FColor>* Existing = Tiles.Find(Coordinate))
        {
            Payload.Colors = *Existing;
            continue;
        }
        Payload.Colors.SetNumUninitialized(Rect.Width() * Rect.Height());
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
                const int32 SourceIndex = Y * Resolution.X + X;
                Payload.Colors[LocalIndex] = BaseColors.IsValidIndex(SourceIndex)
                                                 ? BaseColors[SourceIndex]
                                                 : FColor::Black;
            }
        }
    }
}

bool FDWCTransparencyRevealColorTileStore::CanCommit(
    const uint64                                          ExpectedRevision,
    const TArray<FDWCTransparencyRevealColorTilePayload>& Payloads,
    const TConstArrayView<FColor>                         BaseColors) const
{
    if (!IsValid() || Revision != ExpectedRevision || Payloads.IsEmpty() ||
        BaseColors.Num() != Resolution.X * Resolution.Y)
    {
        return false;
    }
    TSet<FIntPoint> Coordinates;
    for (const FDWCTransparencyRevealColorTilePayload& Payload : Payloads)
    {
        if (!Payload.IsValidFor(Resolution, TileSize) || Coordinates.Contains(Payload.TileCoordinate))
        {
            return false;
        }
        Coordinates.Add(Payload.TileCoordinate);
    }
    return true;
}

bool FDWCTransparencyRevealColorTileStore::Commit(
    const uint64                                          ExpectedRevision,
    const TArray<FDWCTransparencyRevealColorTilePayload>& Payloads,
    const TConstArrayView<FColor>                         BaseColors)
{
    if (!CanCommit(ExpectedRevision, Payloads, BaseColors))
    {
        return false;
    }
    for (const FDWCTransparencyRevealColorTilePayload& Payload : Payloads)
    {
        if (IsTileEqualToBase(Payload.TileCoordinate, Payload.Colors, BaseColors))
        {
            Tiles.Remove(Payload.TileCoordinate);
        }
        else
        {
            Tiles.Add(Payload.TileCoordinate, Payload.Colors);
        }
    }
    ++Revision;
    return true;
}

void FDWCTransparencyRevealColorTileStore::BuildFromDense(
    const TConstArrayView<FColor> Colors,
    const TConstArrayView<FColor> BaseColors)
{
    if (!IsValid() || Colors.Num() != Resolution.X * Resolution.Y || BaseColors.Num() != Colors.Num())
    {
        return;
    }
    Tiles.Reset();
    const int32 TileCountX = FMath::DivideAndRoundUp(Resolution.X, TileSize);
    const int32 TileCountY = FMath::DivideAndRoundUp(Resolution.Y, TileSize);
    for (int32 TileY = 0; TileY < TileCountY; ++TileY)
    {
        for (int32 TileX = 0; TileX < TileCountX; ++TileX)
        {
            const FIntPoint Coordinate(TileX, TileY);
            const FIntRect  Rect = GetTileRect(Coordinate);
            TArray<FColor>  TileColors;
            TileColors.SetNumUninitialized(Rect.Width() * Rect.Height());
            for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
            {
                for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
                {
                    TileColors[(Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X] = Colors[Y * Resolution.X + X];
                }
            }
            if (!IsTileEqualToBase(Coordinate, TileColors, BaseColors))
            {
                Tiles.Add(Coordinate, MoveTemp(TileColors));
            }
        }
    }
    ++Revision;
}

const TArray<FColor>* FDWCTransparencyRevealColorTileStore::FindTileForPixel(
    const int32 X,
    const int32 Y,
    int32&      OutLocalIndex) const
{
    OutLocalIndex = INDEX_NONE;
    const FIntPoint       Coordinate(X / TileSize, Y / TileSize);
    const TArray<FColor>* Tile = Tiles.Find(Coordinate);
    if (Tile != nullptr)
    {
        const FIntRect Rect = GetTileRect(Coordinate);
        OutLocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
    }
    return Tile;
}

bool FDWCTransparencyRevealColorTileStore::IsTileEqualToBase(
    const FIntPoint&              TileCoordinate,
    const TConstArrayView<FColor> Colors,
    const TConstArrayView<FColor> BaseColors) const
{
    const FIntRect Rect = GetTileRect(TileCoordinate);
    if (Rect.IsEmpty() || Colors.Num() != Rect.Width() * Rect.Height())
    {
        return false;
    }
    for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
    {
        for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
        {
            const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
            const int32 BaseIndex = Y * Resolution.X + X;
            if (!BaseColors.IsValidIndex(BaseIndex) || Colors[LocalIndex] != BaseColors[BaseIndex])
            {
                return false;
            }
        }
    }
    return true;
}
