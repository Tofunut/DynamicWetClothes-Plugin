//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"

namespace
{
    FDWCTransparencySourcePayload BuildAlphaTestResult(const FIntPoint Resolution)
    {
        FDWCTransparencySourcePayload Result;
        Result.Resolution = Resolution;
        const int32 PixelCount = Resolution.X * Resolution.Y;
        Result.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
        Result.OuterIslandIDBuffer.Init(
            FDWCTransparencySourcePayload::EncodeOuterIslandID(7),
            PixelCount);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Result.AutoAlphaBuffer[Index] = static_cast<uint8>((Index * 37) % 256);
        }
        return Result;
    }

    bool RasterizeIncrementally(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyBrushStroke& Stroke,
        FDWCTransparencyAlphaTileStore& InOutStore)
    {
        TArray<FIntRect> SampleRegions;
        FDWCEditorDirtyRegionSet DirtyRegions;
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            FDWCTransparencyBrushRasterizer::BuildSampleRegions(
                Sample,
                SourcePayload.Resolution,
                Stroke.UVAddressMode,
                SampleRegions);
            for (const FIntRect& Region : SampleRegions)
            {
                DirtyRegions.Add(Region, SourcePayload.Resolution, false);
            }
        }

        TArray<FIntPoint> OutputTiles;
        InOutStore.GatherTileCoordinates(DirtyRegions.GetRegions(), false, false, OutputTiles);
        TArray<FIntPoint> SnapshotTiles;
        InOutStore.GatherTileCoordinates(
            DirtyRegions.GetRegions(),
            Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth,
            Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap,
            SnapshotTiles);
        TArray<FDWCTransparencyAlphaTilePayload> Payloads;
        InOutStore.SnapshotTiles(SnapshotTiles, Payloads);
        if (!FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
                SourcePayload,
                Stroke,
                Stroke.Samples,
                OutputTiles,
                Payloads))
        {
            return false;
        }

        TSet<FIntPoint> OutputTileSet;
        for (const FIntPoint& Coordinate : OutputTiles)
        {
            OutputTileSet.Add(Coordinate);
        }
        Payloads.RemoveAll([&OutputTileSet](const FDWCTransparencyAlphaTilePayload& Payload)
        {
            return !OutputTileSet.Contains(Payload.TileCoordinate);
        });
        return InOutStore.Commit(InOutStore.GetRevision(), Payloads);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAlphaTileParityTest,
    "DWC.Editor.Transparency.Brush.AlphaTileIncrementalParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAlphaTileParityTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(512, 384);
    const FDWCTransparencySourcePayload SourcePayload = BuildAlphaTestResult(Resolution);

    FDWCTransparencyBrushStroke Stroke;
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.MaterialSlotIndex = 3;
    Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Wrap;
    Stroke.BrushMode = EDWCTransparencyBrushMode::SetValue;
    Stroke.Falloff = 0.65f;
    Stroke.TargetAlpha = 0.2f;

    FDWCTransparencyBrushSample& Center = Stroke.Samples.AddDefaulted_GetRef();
    Center.PositionUV = FVector2D(0.49, 0.51);
    // Spatial-query island IDs can use a different numbering scheme than the
    // texture-space raster. Lynae exercises this case in production.
    Center.UVIslandID = 525;
    Center.RadiusUV = 0.14f;
    Center.Strength = 0.8f;
    FDWCTransparencyBrushSample& Wrapped = Stroke.Samples.AddDefaulted_GetRef();
    Wrapped.PositionUV = FVector2D(0.99, 0.03);
    Wrapped.UVIslandID = 425;
    Wrapped.RadiusUV = 0.08f;
    Wrapped.Strength = 0.55f;

    FDWCTransparencyBrushStroke SmoothStroke;
    SmoothStroke.StrokeGuid = FGuid::NewGuid();
    SmoothStroke.MaterialSlotIndex = Stroke.MaterialSlotIndex;
    SmoothStroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
    SmoothStroke.BrushMode = EDWCTransparencyBrushMode::Smooth;
    SmoothStroke.Falloff = 0.35f;
    FDWCTransparencyBrushSample& SmoothSample = SmoothStroke.Samples.AddDefaulted_GetRef();
    SmoothSample.PositionUV = FVector2D(0.49, 0.51);
    SmoothSample.UVIslandID = 525;
    SmoothSample.RadiusUV = 0.07f;
    SmoothSample.Strength = 0.65f;

    TArray<uint8> DensePremultiplied;
    TArray<uint8> DenseWeight;
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        SourcePayload,
        {Stroke, SmoothStroke},
        0,
        Stroke.MaterialSlotIndex,
        0,
        DensePremultiplied,
        DenseWeight);

    FDWCTransparencyAlphaTileStore TileStore;
    TileStore.Initialize(Resolution);
    TestTrue(TEXT("Incremental tile raster accepts the stroke"), RasterizeIncrementally(SourcePayload, Stroke, TileStore));
    TestTrue(
        TEXT("Incremental alpha smoothing reuses the prior sparse tile state"),
        RasterizeIncrementally(SourcePayload, SmoothStroke, TileStore));

    TArray<uint8> TilePremultiplied;
    TArray<uint8> TileWeight;
    TileStore.BuildDense(TilePremultiplied, TileWeight);
    TestEqual(TEXT("Premultiplied alpha matches full replay"), TilePremultiplied, DensePremultiplied);
    TestEqual(TEXT("Manual weights match full replay"), TileWeight, DenseWeight);
    const int32 FullTileCount =
        FMath::DivideAndRoundUp(Resolution.X, FDWCTransparencyAlphaTileStore::TileSize) *
        FMath::DivideAndRoundUp(Resolution.Y, FDWCTransparencyAlphaTileStore::TileSize);
    TestTrue(TEXT("Only touched alpha tiles are resident"), TileStore.GetTileCount() < FullTileCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAlphaTileRevisionTest,
    "DWC.Editor.Transparency.Brush.AlphaTileRevisionGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAlphaTileRevisionTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyAlphaTileStore Store;
    Store.Initialize(FIntPoint(256, 256));
    TArray<FDWCTransparencyAlphaTilePayload> Payloads;
    Store.SnapshotTiles({FIntPoint(0, 0)}, Payloads);
    const uint64 SnapshotRevision = Store.GetRevision();
    TestTrue(TEXT("A current snapshot is committable"), Store.CanCommit(SnapshotRevision, Payloads));
    TestTrue(TEXT("The current snapshot commits"), Store.Commit(SnapshotRevision, Payloads));
    TestFalse(TEXT("The same snapshot is rejected after the revision advances"), Store.CanCommit(SnapshotRevision, Payloads));
    TestEqual(TEXT("Zero-only tiles are not retained"), Store.GetTileCount(), 0);
    return true;
}

#endif
