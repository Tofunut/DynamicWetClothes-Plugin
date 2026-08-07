//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"

namespace
{
    int32 AlphaTilePositiveModulo(const int32 Value, const int32 Divisor)
    {
        return (Value % Divisor + Divisor) % Divisor;
    }
}

bool FDWCTransparencyAlphaTilePayload::IsValidFor(
    const FIntPoint& Resolution,
    const int32 TileSize) const
{
    if (TileSize <= 0 || Rect.IsEmpty() || Rect.Min.X < 0 || Rect.Min.Y < 0 ||
        Rect.Max.X > Resolution.X || Rect.Max.Y > Resolution.Y ||
        Rect.Min.X / TileSize != TileCoordinate.X || Rect.Min.Y / TileSize != TileCoordinate.Y)
    {
        return false;
    }
    const int32 PixelCount = Rect.Width() * Rect.Height();
    return Premultiplied.Num() == PixelCount && Weight.Num() == PixelCount;
}

uint64 FDWCTransparencyAlphaTilePayload::GetAllocatedBytes() const
{
    return static_cast<uint64>(Premultiplied.GetAllocatedSize()) +
        static_cast<uint64>(Weight.GetAllocatedSize());
}

void FDWCTransparencyAlphaTileStore::Initialize(const FIntPoint& InResolution)
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

void FDWCTransparencyAlphaTileStore::Reset()
{
    Resolution = FIntPoint::ZeroValue;
    TMap<FIntPoint, FTile> EmptyTiles;
    Tiles = MoveTemp(EmptyTiles);
    ++Revision;
}

uint64 FDWCTransparencyAlphaTileStore::GetAllocatedBytes() const
{
    uint64 Bytes = Tiles.GetAllocatedSize();
    for (const TPair<FIntPoint, FTile>& Pair : Tiles)
    {
        Bytes += Pair.Value.Premultiplied.GetAllocatedSize();
        Bytes += Pair.Value.Weight.GetAllocatedSize();
    }
    return Bytes;
}

uint8 FDWCTransparencyAlphaTileStore::GetPremultiplied(const int32 PixelIndex) const
{
    if (!IsValid() || PixelIndex < 0 || PixelIndex >= Resolution.X * Resolution.Y)
    {
        return 0;
    }
    int32 LocalIndex = INDEX_NONE;
    const FTile* Tile = FindTileForPixel(PixelIndex % Resolution.X, PixelIndex / Resolution.X, LocalIndex);
    return Tile != nullptr && Tile->Premultiplied.IsValidIndex(LocalIndex)
        ? Tile->Premultiplied[LocalIndex]
        : 0;
}

uint8 FDWCTransparencyAlphaTileStore::GetWeight(const int32 PixelIndex) const
{
    if (!IsValid() || PixelIndex < 0 || PixelIndex >= Resolution.X * Resolution.Y)
    {
        return 0;
    }
    int32 LocalIndex = INDEX_NONE;
    const FTile* Tile = FindTileForPixel(PixelIndex % Resolution.X, PixelIndex / Resolution.X, LocalIndex);
    return Tile != nullptr && Tile->Weight.IsValidIndex(LocalIndex)
        ? Tile->Weight[LocalIndex]
        : 0;
}

void FDWCTransparencyAlphaTileStore::SetPixel(
    const int32 X,
    const int32 Y,
    const uint8 Premultiplied,
    const uint8 Weight)
{
    if (!IsValid() || X < 0 || Y < 0 || X >= Resolution.X || Y >= Resolution.Y)
    {
        return;
    }
    const FIntPoint Coordinate(X / TileSize, Y / TileSize);
    FTile& Tile = FindOrAddTile(Coordinate);
    const FIntRect Rect = GetTileRect(Coordinate);
    const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
    Tile.Premultiplied[LocalIndex] = Premultiplied;
    Tile.Weight[LocalIndex] = Weight;
}

FIntRect FDWCTransparencyAlphaTileStore::GetTileRect(const FIntPoint& TileCoordinate) const
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

void FDWCTransparencyAlphaTileStore::GatherTileCoordinates(
    const TConstArrayView<FIntRect> Regions,
    const bool bIncludeOnePixelHalo,
    const bool bWrap,
    TArray<FIntPoint>& OutTileCoordinates) const
{
    OutTileCoordinates.Reset();
    if (!IsValid())
    {
        return;
    }
    TSet<FIntPoint> UniqueCoordinates;
    const int32 TileCountX = FMath::DivideAndRoundUp(Resolution.X, TileSize);
    const int32 TileCountY = FMath::DivideAndRoundUp(Resolution.Y, TileSize);
    for (FIntRect Region : Regions)
    {
        if (bIncludeOnePixelHalo)
        {
            Region.Min -= FIntPoint(1, 1);
            Region.Max += FIntPoint(1, 1);
        }
        const int32 MinTileX = FMath::FloorToInt(static_cast<double>(Region.Min.X) / TileSize);
        const int32 MinTileY = FMath::FloorToInt(static_cast<double>(Region.Min.Y) / TileSize);
        const int32 MaxTileX = FMath::FloorToInt(static_cast<double>(Region.Max.X - 1) / TileSize);
        const int32 MaxTileY = FMath::FloorToInt(static_cast<double>(Region.Max.Y - 1) / TileSize);
        for (int32 RawY = MinTileY; RawY <= MaxTileY; ++RawY)
        {
            for (int32 RawX = MinTileX; RawX <= MaxTileX; ++RawX)
            {
                if (!bWrap && (RawX < 0 || RawY < 0 || RawX >= TileCountX || RawY >= TileCountY))
                {
                    continue;
                }
                UniqueCoordinates.Add(FIntPoint(
                    bWrap ? AlphaTilePositiveModulo(RawX, TileCountX) : RawX,
                    bWrap ? AlphaTilePositiveModulo(RawY, TileCountY) : RawY));
            }
        }
    }
    OutTileCoordinates.Reserve(UniqueCoordinates.Num());
    for (const FIntPoint& Coordinate : UniqueCoordinates)
    {
        OutTileCoordinates.Add(Coordinate);
    }
    OutTileCoordinates.Sort([](const FIntPoint& A, const FIntPoint& B)
    {
        return A.Y == B.Y ? A.X < B.X : A.Y < B.Y;
    });
}

void FDWCTransparencyAlphaTileStore::SnapshotTiles(
    const TArray<FIntPoint>& TileCoordinates,
    TArray<FDWCTransparencyAlphaTilePayload>& OutTiles) const
{
    OutTiles.Reset(TileCoordinates.Num());
    for (const FIntPoint& Coordinate : TileCoordinates)
    {
        const FIntRect Rect = GetTileRect(Coordinate);
        if (Rect.IsEmpty())
        {
            continue;
        }
        FDWCTransparencyAlphaTilePayload& Payload = OutTiles.AddDefaulted_GetRef();
        Payload.TileCoordinate = Coordinate;
        Payload.Rect = Rect;
        const int32 PixelCount = Rect.Width() * Rect.Height();
        if (const FTile* Existing = Tiles.Find(Coordinate))
        {
            Payload.Premultiplied = Existing->Premultiplied;
            Payload.Weight = Existing->Weight;
        }
        else
        {
            Payload.Premultiplied.Init(0, PixelCount);
            Payload.Weight.Init(0, PixelCount);
        }
    }
}

bool FDWCTransparencyAlphaTileStore::CanCommit(
    const uint64 ExpectedRevision,
    const TArray<FDWCTransparencyAlphaTilePayload>& Payloads) const
{
    if (!IsValid() || Revision != ExpectedRevision || Payloads.IsEmpty())
    {
        return false;
    }
    TSet<FIntPoint> Coordinates;
    for (const FDWCTransparencyAlphaTilePayload& Payload : Payloads)
    {
        if (!Payload.IsValidFor(Resolution, TileSize) || Coordinates.Contains(Payload.TileCoordinate))
        {
            return false;
        }
        Coordinates.Add(Payload.TileCoordinate);
    }
    return true;
}

bool FDWCTransparencyAlphaTileStore::Commit(
    const uint64 ExpectedRevision,
    const TArray<FDWCTransparencyAlphaTilePayload>& Payloads)
{
    if (!CanCommit(ExpectedRevision, Payloads))
    {
        return false;
    }
    for (const FDWCTransparencyAlphaTilePayload& Payload : Payloads)
    {
        FTile Tile;
        Tile.Premultiplied = Payload.Premultiplied;
        Tile.Weight = Payload.Weight;
        if (IsTileAllZero(Tile))
        {
            Tiles.Remove(Payload.TileCoordinate);
        }
        else
        {
            Tiles.Add(Payload.TileCoordinate, MoveTemp(Tile));
        }
    }
    ++Revision;
    return true;
}

void FDWCTransparencyAlphaTileStore::BuildFromDense(
    const TArray<uint8>& Premultiplied,
    const TArray<uint8>& Weight)
{
    if (!IsValid() || Premultiplied.Num() != Resolution.X * Resolution.Y || Weight.Num() != Premultiplied.Num())
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
            const FIntRect Rect = GetTileRect(Coordinate);
            FTile Tile;
            Tile.Premultiplied.SetNumUninitialized(Rect.Width() * Rect.Height());
            Tile.Weight.SetNumUninitialized(Rect.Width() * Rect.Height());
            bool bAny = false;
            for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
            {
                for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
                {
                    const int32 SourceIndex = Y * Resolution.X + X;
                    const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
                    Tile.Premultiplied[LocalIndex] = Premultiplied[SourceIndex];
                    Tile.Weight[LocalIndex] = Weight[SourceIndex];
                    bAny |= Premultiplied[SourceIndex] != 0 || Weight[SourceIndex] != 0;
                }
            }
            if (bAny)
            {
                Tiles.Add(Coordinate, MoveTemp(Tile));
            }
        }
    }
    ++Revision;
}

void FDWCTransparencyAlphaTileStore::BuildDense(
    TArray<uint8>& OutPremultiplied,
    TArray<uint8>& OutWeight) const
{
    if (!IsValid())
    {
        OutPremultiplied.Reset();
        OutWeight.Reset();
        return;
    }
    const int32 PixelCount = Resolution.X * Resolution.Y;
    OutPremultiplied.Init(0, PixelCount);
    OutWeight.Init(0, PixelCount);
    for (const TPair<FIntPoint, FTile>& Pair : Tiles)
    {
        const FIntRect Rect = GetTileRect(Pair.Key);
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                const int32 DestinationIndex = Y * Resolution.X + X;
                const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
                OutPremultiplied[DestinationIndex] = Pair.Value.Premultiplied[LocalIndex];
                OutWeight[DestinationIndex] = Pair.Value.Weight[LocalIndex];
            }
        }
    }
}

const FDWCTransparencyAlphaTileStore::FTile* FDWCTransparencyAlphaTileStore::FindTileForPixel(
    const int32 X,
    const int32 Y,
    int32& OutLocalIndex) const
{
    OutLocalIndex = INDEX_NONE;
    const FIntPoint Coordinate(X / TileSize, Y / TileSize);
    const FTile* Tile = Tiles.Find(Coordinate);
    if (Tile != nullptr)
    {
        const FIntRect Rect = GetTileRect(Coordinate);
        OutLocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
    }
    return Tile;
}

FDWCTransparencyAlphaTileStore::FTile& FDWCTransparencyAlphaTileStore::FindOrAddTile(
    const FIntPoint& TileCoordinate)
{
    FTile& Tile = Tiles.FindOrAdd(TileCoordinate);
    const FIntRect Rect = GetTileRect(TileCoordinate);
    const int32 PixelCount = Rect.Width() * Rect.Height();
    if (Tile.Premultiplied.Num() != PixelCount)
    {
        Tile.Premultiplied.Init(0, PixelCount);
        Tile.Weight.Init(0, PixelCount);
    }
    return Tile;
}

bool FDWCTransparencyAlphaTileStore::IsTileAllZero(const FTile& Tile) const
{
    for (int32 Index = 0; Index < Tile.Premultiplied.Num(); ++Index)
    {
        if (Tile.Premultiplied[Index] != 0 || Tile.Weight[Index] != 0)
        {
            return false;
        }
    }
    return true;
}
