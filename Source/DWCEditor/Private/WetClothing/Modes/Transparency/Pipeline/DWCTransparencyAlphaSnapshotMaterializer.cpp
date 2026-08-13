//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAlphaSnapshotMaterializer.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"

namespace
{
    bool SnapshotFail(FString* OutError, const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
        return false;
    }
}

bool FDWCTransparencyAlphaSnapshotView::Initialize(
    const FDWCTransparencyAlphaWorkingSnapshot& InSnapshot,
    FString* OutError)
{
    Snapshot = nullptr;
    TileLookup.Reset();
    TileGrid = FIntPoint::ZeroValue;
    if (InSnapshot.Mode != EDWCTransparencyAlphaSnapshotMode::SparseTiles ||
        !InSnapshot.IsValid(OutError))
    {
        return false;
    }

    TileGrid = FIntPoint(
        FMath::DivideAndRoundUp(InSnapshot.Resolution.X, FDWCTransparencyAlphaTileStore::TileSize),
        FMath::DivideAndRoundUp(InSnapshot.Resolution.Y, FDWCTransparencyAlphaTileStore::TileSize));
    TileLookup.Init(INDEX_NONE, TileGrid.X * TileGrid.Y);
    for (int32 TileIndex = 0; TileIndex < InSnapshot.ModifiedTiles.Num(); ++TileIndex)
    {
        const FIntPoint Coordinate = InSnapshot.ModifiedTiles[TileIndex].TileCoordinate;
        if (Coordinate.X < 0 || Coordinate.Y < 0 ||
            Coordinate.X >= TileGrid.X || Coordinate.Y >= TileGrid.Y)
        {
            return SnapshotFail(OutError, TEXT("The sparse alpha snapshot contains an out-of-range tile."));
        }
        TileLookup[Coordinate.Y * TileGrid.X + Coordinate.X] = TileIndex;
    }
    Snapshot = &InSnapshot;
    return true;
}

uint8 FDWCTransparencyAlphaSnapshotView::GetPremultiplied(const int32 PixelIndex) const
{
    if (!IsValid() || PixelIndex < 0 || PixelIndex >= Snapshot->Resolution.X * Snapshot->Resolution.Y)
    {
        return 0;
    }
    const int32 X = PixelIndex % Snapshot->Resolution.X;
    const int32 Y = PixelIndex / Snapshot->Resolution.X;
    const int32 TileIndex = TileLookup[(Y / FDWCTransparencyAlphaTileStore::TileSize) * TileGrid.X +
        X / FDWCTransparencyAlphaTileStore::TileSize];
    if (!Snapshot->ModifiedTiles.IsValidIndex(TileIndex))
    {
        return 0;
    }
    const FDWCTransparencyAlphaTilePayload& Tile = Snapshot->ModifiedTiles[TileIndex];
    const int32 LocalIndex = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width() + X - Tile.Rect.Min.X;
    return Tile.Premultiplied.IsValidIndex(LocalIndex) ? Tile.Premultiplied[LocalIndex] : 0;
}

uint8 FDWCTransparencyAlphaSnapshotView::GetWeight(const int32 PixelIndex) const
{
    if (!IsValid() || PixelIndex < 0 || PixelIndex >= Snapshot->Resolution.X * Snapshot->Resolution.Y)
    {
        return 0;
    }
    const int32 X = PixelIndex % Snapshot->Resolution.X;
    const int32 Y = PixelIndex / Snapshot->Resolution.X;
    const int32 TileIndex = TileLookup[(Y / FDWCTransparencyAlphaTileStore::TileSize) * TileGrid.X +
        X / FDWCTransparencyAlphaTileStore::TileSize];
    if (!Snapshot->ModifiedTiles.IsValidIndex(TileIndex))
    {
        return 0;
    }
    const FDWCTransparencyAlphaTilePayload& Tile = Snapshot->ModifiedTiles[TileIndex];
    const int32 LocalIndex = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width() + X - Tile.Rect.Min.X;
    return Tile.Weight.IsValidIndex(LocalIndex) ? Tile.Weight[LocalIndex] : 0;
}

bool FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
    const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain,
    const FDWCTransparencyAlphaWorkingSnapshot& Input,
    FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
    FString& OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    OutSparseSnapshot = FDWCTransparencyAlphaWorkingSnapshot();
    OutError.Reset();
    if (!AlphaDomain.IsValid(&OutError) || !Input.IsValid(&OutError) ||
        Input.Resolution != AlphaDomain.Resolution)
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("The alpha snapshot resolution does not match its transparency source.");
        }
        return false;
    }
    if (Input.Mode == EDWCTransparencyAlphaSnapshotMode::SparseTiles)
    {
        OutSparseSnapshot = Input;
        return true;
    }

    FDWCTransparencyAlphaTileStore Store;
    Store.Initialize(AlphaDomain.Resolution);
    const int32 FirstStroke = FMath::Clamp(Input.BaselineStrokeCount, 0, Input.FallbackStrokes.Num());
    TArray<FIntRect> StrokeRegions;
    TArray<FIntRect> SampleRegions;
    TArray<FIntPoint> OutputTiles;
    TArray<FIntPoint> SnapshotTiles;
    TArray<FDWCTransparencyAlphaTilePayload> Payloads;
    TArray<FDWCTransparencyBrushSample> DecodedSamples;
    for (int32 StrokeIndex = FirstStroke; StrokeIndex < Input.FallbackStrokes.Num(); ++StrokeIndex)
    {
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            OutError = TEXT("The transparency alpha snapshot materialization was canceled.");
            return false;
        }
        const FDWCTransparencyBrushStroke& Stroke = Input.FallbackStrokes[StrokeIndex];
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != AlphaDomain.MaterialSlotIndex || !Stroke.HasSamples())
        {
            continue;
        }
        StrokeRegions.Reset();
        Stroke.ForEachSample(
            [&StrokeRegions, &SampleRegions, &AlphaDomain, &Stroke](const FDWCTransparencyBrushSample& Sample)
            {
                FDWCTransparencyBrushRasterizer::BuildSampleRegions(
                    Sample, AlphaDomain.Resolution, Stroke.UVAddressMode, SampleRegions);
                StrokeRegions.Append(SampleRegions);
            });
        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
        Store.GatherTileCoordinates(StrokeRegions, false, bWrap, OutputTiles);
        Store.GatherTileCoordinates(
            StrokeRegions,
            Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth,
            bWrap,
            SnapshotTiles);
        if (OutputTiles.IsEmpty())
        {
            continue;
        }
        Store.SnapshotTiles(SnapshotTiles, Payloads);
        Stroke.DecodeSamples(DecodedSamples);
        if (FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
                FDWCTransparencyAlphaRasterContext::FromAlphaDomain(AlphaDomain),
                Stroke,
                DecodedSamples,
                OutputTiles,
                Payloads) &&
            !Store.Commit(Store.GetRevision(), Payloads))
        {
            OutError = TEXT("The transparency alpha sparse tile replay could not commit its rasterized payload.");
            return false;
        }
    }

    OutSparseSnapshot.Mode = EDWCTransparencyAlphaSnapshotMode::SparseTiles;
    OutSparseSnapshot.Resolution = Input.Resolution;
    OutSparseSnapshot.StoreRevision = Store.GetRevision();
    OutSparseSnapshot.BaselineStrokeCount = Input.BaselineStrokeCount;
    OutSparseSnapshot.AuthoredStrokeCount = Input.AuthoredStrokeCount;
    OutSparseSnapshot.AppliedSampleCount = Input.AppliedSampleCount;
    Store.SnapshotModifiedTiles(OutSparseSnapshot.ModifiedTiles);
    return OutSparseSnapshot.IsValid(&OutError);
}

bool FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
    const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain,
    FDWCTransparencyAlphaWorkingSnapshot&& Input,
    FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
    FString& OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    if (Input.Mode != EDWCTransparencyAlphaSnapshotMode::SparseTiles)
    {
        return Materialize(
            AlphaDomain,
            static_cast<const FDWCTransparencyAlphaWorkingSnapshot&>(Input),
            OutSparseSnapshot,
            OutError,
            CancellationToken);
    }
    OutSparseSnapshot = FDWCTransparencyAlphaWorkingSnapshot();
    OutError.Reset();
    if (!AlphaDomain.IsValid(&OutError) || !Input.IsValid(&OutError) ||
        Input.Resolution != AlphaDomain.Resolution)
    {
        if (OutError.IsEmpty())
        {
            OutError = TEXT("The alpha snapshot resolution does not match its transparency source.");
        }
        return false;
    }
    OutSparseSnapshot = MoveTemp(Input);
    return true;
}

bool FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyAlphaWorkingSnapshot& Input,
    FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
    FString& OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot> AlphaDomain =
        FDWCTransparencyAlphaDomainSnapshot::Create(SourcePayload, &OutError);
    return AlphaDomain.IsValid() && Materialize(
        *AlphaDomain, Input, OutSparseSnapshot, OutError, CancellationToken);
}

bool FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
    const FDWCTransparencySourcePayload& SourcePayload,
    FDWCTransparencyAlphaWorkingSnapshot&& Input,
    FDWCTransparencyAlphaWorkingSnapshot& OutSparseSnapshot,
    FString& OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot> AlphaDomain =
        FDWCTransparencyAlphaDomainSnapshot::Create(SourcePayload, &OutError);
    return AlphaDomain.IsValid() && Materialize(
        *AlphaDomain, MoveTemp(Input), OutSparseSnapshot, OutError, CancellationToken);
}
