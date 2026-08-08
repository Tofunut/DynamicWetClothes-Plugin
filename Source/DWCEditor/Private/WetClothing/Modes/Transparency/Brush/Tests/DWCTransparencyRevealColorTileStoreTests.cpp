// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"

namespace
{
    FDWCTransparencyAutoBakeResult BuildRevealColorTestResult(const FIntPoint Resolution)
    {
        FDWCTransparencyAutoBakeResult Result;
        Result.Resolution = Resolution;
        const int32 PixelCount = Resolution.X * Resolution.Y;
        Result.InnerColorBuffer.SetNumUninitialized(PixelCount);
        Result.OuterCoverageBuffer.Init(255, PixelCount);
        Result.OuterIslandIDBuffer.Init(
            FDWCTransparencyAutoBakeResult::EncodeOuterIslandID(5),
            PixelCount);
        for (int32 Index = 0; Index < PixelCount; ++Index)
        {
            Result.InnerColorBuffer[Index] = FColor(
                static_cast<uint8>(20 + Index % 31),
                static_cast<uint8>(40 + Index % 17),
                static_cast<uint8>(60 + Index % 13),
                255);
        }
        return Result;
    }

    bool RasterizeRevealColorIncrementally(
        const FDWCTransparencyAutoBakeResult&    AutoResult,
        const FDWCTransparencyRevealColorStroke& Stroke,
        FDWCTransparencyRevealColorTileStore&    InOutStore)
    {
        FDWCEditorDirtyRegionSet DirtyRegions;
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            TArray<FIntRect> SampleRegions;
            FDWCTransparencyBrushRasterizer::BuildSampleRegions(
                Sample,
                AutoResult.Resolution,
                Stroke.UVAddressMode,
                SampleRegions);
            for (const FIntRect& Region : SampleRegions)
            {
                DirtyRegions.Add(Region, AutoResult.Resolution, false);
            }
        }

        TArray<FIntPoint> OutputTiles;
        InOutStore.GatherTileCoordinates(DirtyRegions.GetRegions(), false, false, OutputTiles);
        TArray<FIntPoint> SnapshotTiles;
        InOutStore.GatherTileCoordinates(
            DirtyRegions.GetRegions(),
            Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth,
            Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap,
            SnapshotTiles);
        TArray<FDWCTransparencyRevealColorTilePayload> Payloads;
        InOutStore.SnapshotTiles(
            SnapshotTiles,
            MakeArrayView(AutoResult.InnerColorBuffer),
            Payloads);
        if (!FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
                AutoResult,
                Stroke,
                Stroke.Samples,
                FLinearColor(0.1f, 0.2f, 0.3f),
                OutputTiles,
                Payloads))
        {
            return false;
        }
        return InOutStore.Commit(
            InOutStore.GetRevision(),
            Payloads,
            MakeArrayView(AutoResult.InnerColorBuffer));
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealColorTileParityTest,
    "DWC.Editor.Transparency.Brush.RevealColorTileIncrementalParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealColorTileParityTest::RunTest(const FString& Parameters)
{
    const FIntPoint                      Resolution(512, 384);
    const FDWCTransparencyAutoBakeResult AutoResult = BuildRevealColorTestResult(Resolution);

    FDWCTransparencyRevealColorStroke Stroke;
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.MaterialSlotIndex = 3;
    Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Wrap;
    Stroke.BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;
    Stroke.PaintColor = FLinearColor(0.8f, 0.15f, 0.45f);
    Stroke.Falloff = 0.65f;

    FDWCTransparencyBrushSample& Center = Stroke.Samples.AddDefaulted_GetRef();
    Center.PositionUV = FVector2D(0.49, 0.51);
    // Deliberately differs from the raster island ID to guard the live-paint
    // path used by meshes whose spatial and texture island numbering diverge.
    Center.UVIslandID = 525;
    Center.RadiusUV = 0.14f;
    Center.Strength = 0.8f;
    FDWCTransparencyBrushSample& Wrapped = Stroke.Samples.AddDefaulted_GetRef();
    Wrapped.PositionUV = FVector2D(0.99, 0.03);
    Wrapped.UVIslandID = 425;
    Wrapped.RadiusUV = 0.08f;
    Wrapped.Strength = 0.55f;

    FDWCTransparencyRevealColorStroke SmoothStroke;
    SmoothStroke.StrokeGuid = FGuid::NewGuid();
    SmoothStroke.MaterialSlotIndex = Stroke.MaterialSlotIndex;
    SmoothStroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
    SmoothStroke.BrushMode = EDWCTransparencyRevealColorBrushMode::Smooth;
    SmoothStroke.Falloff = 0.35f;
    FDWCTransparencyBrushSample& SmoothSample = SmoothStroke.Samples.AddDefaulted_GetRef();
    SmoothSample.PositionUV = FVector2D(0.25, 0.5);
    SmoothSample.UVIslandID = 525;
    SmoothSample.RadiusUV = 0.06f;
    SmoothSample.Strength = 0.7f;

    TArray<FColor> DenseColors = AutoResult.InnerColorBuffer;
    FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
        AutoResult,
        { Stroke, SmoothStroke },
        Stroke.MaterialSlotIndex,
        FLinearColor(0.1f, 0.2f, 0.3f),
        DenseColors);

    FDWCTransparencyRevealColorTileStore TileStore;
    TileStore.Initialize(Resolution);
    TestTrue(
        TEXT("Incremental reveal-color raster accepts the stroke"),
        RasterizeRevealColorIncrementally(AutoResult, Stroke, TileStore));
    TestTrue(
        TEXT("Incremental reveal-color raster accepts smoothing across a tile boundary"),
        RasterizeRevealColorIncrementally(AutoResult, SmoothStroke, TileStore));
    for (int32 PixelIndex = 0; PixelIndex < DenseColors.Num(); ++PixelIndex)
    {
        if (TileStore.GetColor(PixelIndex, MakeArrayView(AutoResult.InnerColorBuffer)) != DenseColors[PixelIndex])
        {
            AddError(FString::Printf(TEXT("Reveal-color parity failed at pixel %d"), PixelIndex));
            break;
        }
    }

    const int32 FullTileCount =
        FMath::DivideAndRoundUp(Resolution.X, FDWCTransparencyRevealColorTileStore::TileSize) *
        FMath::DivideAndRoundUp(Resolution.Y, FDWCTransparencyRevealColorTileStore::TileSize);
    TestTrue(TEXT("Only touched reveal-color tiles are resident"), TileStore.GetTileCount() < FullTileCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealColorTileRevisionTest,
    "DWC.Editor.Transparency.Brush.RevealColorTileRevisionGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealColorTileRevisionTest::RunTest(const FString& Parameters)
{
    const FIntPoint Resolution(256, 256);
    TArray<FColor>  BaseColors;
    BaseColors.Init(FColor(17, 29, 43, 255), Resolution.X * Resolution.Y);

    FDWCTransparencyRevealColorTileStore Store;
    Store.Initialize(Resolution);
    TArray<FDWCTransparencyRevealColorTilePayload> Payloads;
    Store.SnapshotTiles({ FIntPoint(0, 0) }, MakeArrayView(BaseColors), Payloads);
    const uint64 SnapshotRevision = Store.GetRevision();
    TestTrue(TEXT("A current reveal snapshot is committable"),
             Store.CanCommit(SnapshotRevision, Payloads, MakeArrayView(BaseColors)));
    TestTrue(TEXT("The current reveal snapshot commits"),
             Store.Commit(SnapshotRevision, Payloads, MakeArrayView(BaseColors)));
    TestFalse(TEXT("The same reveal snapshot is rejected after revision advances"),
              Store.CanCommit(SnapshotRevision, Payloads, MakeArrayView(BaseColors)));
    TestEqual(TEXT("Base-only reveal tiles are not retained"), Store.GetTileCount(), 0);
    TestEqual(TEXT("A missing sparse tile resolves the immutable base"),
              Store.GetColor(0, MakeArrayView(BaseColors)), BaseColors[0]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyCompositeIncrementalLifecycleParityTest,
    "DWC.Editor.Regression.IncrementalLifecycle.TransparencyCompositeParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyCompositeIncrementalLifecycleParityTest::RunTest(const FString&)
{
    const FIntPoint                Resolution(256, 192);
    FDWCTransparencyAutoBakeResult AutoResult = BuildRevealColorTestResult(Resolution);
    const int32                    PixelCount = Resolution.X * Resolution.Y;
    AutoResult.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    AutoResult.ValidHitBuffer.Init(true, PixelCount);
    AutoResult.HitDistanceBuffer.Init(1.0f, PixelCount);
    AutoResult.SourcePriorityBuffer.Init(0, PixelCount);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        AutoResult.AutoAlphaBuffer[PixelIndex] = static_cast<uint8>(96 + PixelIndex % 128);
    }

    FDWCTransparencyBrushStroke AlphaStroke;
    AlphaStroke.StrokeGuid = FGuid::NewGuid();
    AlphaStroke.MaterialSlotIndex = 3;
    AlphaStroke.UVAddressMode = EDWCTransparencyUVAddressMode::Wrap;
    AlphaStroke.BrushMode = EDWCTransparencyBrushMode::SetValue;
    AlphaStroke.TargetAlpha = 0.22f;
    AlphaStroke.Falloff = 0.55f;
    FDWCTransparencyBrushSample& AlphaSample = AlphaStroke.Samples.AddDefaulted_GetRef();
    AlphaSample.PositionUV = FVector2D(0.97, 0.48);
    AlphaSample.UVIslandID = 5;
    AlphaSample.RadiusUV = 0.12f;
    AlphaSample.Strength = 0.8f;

    FDWCTransparencyRevealColorStroke RevealStroke;
    RevealStroke.StrokeGuid = FGuid::NewGuid();
    RevealStroke.MaterialSlotIndex = 3;
    RevealStroke.UVAddressMode = EDWCTransparencyUVAddressMode::Wrap;
    RevealStroke.BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;
    RevealStroke.PaintColor = FLinearColor(0.75f, 0.12f, 0.36f);
    RevealStroke.Falloff = 0.6f;
    FDWCTransparencyBrushSample& RevealSample = RevealStroke.Samples.AddDefaulted_GetRef();
    RevealSample.PositionUV = FVector2D(0.49, 0.52);
    RevealSample.UVIslandID = 5;
    RevealSample.RadiusUV = 0.15f;
    RevealSample.Strength = 0.85f;

    TArray<uint8> DensePremultiplied;
    TArray<uint8> DenseWeight;
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        AutoResult,
        { AlphaStroke },
        0,
        3,
        0,
        DensePremultiplied,
        DenseWeight);
    FDWCTransparencyAlphaTileStore SparseAlpha;
    SparseAlpha.Initialize(Resolution);
    SparseAlpha.BuildFromDense(DensePremultiplied, DenseWeight);

    FDWCTransparencyRevealColorTileStore SparseReveal;
    SparseReveal.Initialize(Resolution);
    TestTrue(
        TEXT("Reveal color builds an incremental sparse state for composition"),
        RasterizeRevealColorIncrementally(AutoResult, RevealStroke, SparseReveal));

    const TSharedPtr<const FDWCTransparencyAutoBakeResult> SharedAutoResult =
        MakeShared<FDWCTransparencyAutoBakeResult>(MoveTemp(AutoResult));
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> SparseToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    FDWCTransparencyVisualizationJobInput SparseInput;
    SparseInput.AutoResult = SharedAutoResult;
    SparseInput.ManualAlphaTileStore = SparseAlpha;
    SparseInput.RevealColorTileStore = SparseReveal;
    SparseInput.MaterialSlotIndex = 3;
    SparseInput.UVChannelIndex = 0;
    SparseInput.VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    const TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> SparseResult =
        FDWCTransparencyVisualizationWorker::Build(MoveTemp(SparseInput), SparseToken);

    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> FullToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    FDWCTransparencyVisualizationJobInput FullInput;
    FullInput.AutoResult = SharedAutoResult;
    FullInput.bRebuildManualOverridesFromStrokes = true;
    FullInput.bRebuildRevealColorFromStrokes = true;
    FullInput.EditableStrokes = { AlphaStroke };
    FullInput.RevealColorPaintStrokes = { RevealStroke };
    FullInput.BaseRevealColor = FLinearColor(0.1f, 0.2f, 0.3f);
    FullInput.MaterialSlotIndex = 3;
    FullInput.UVChannelIndex = 0;
    FullInput.VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    const TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> FullResult =
        FDWCTransparencyVisualizationWorker::Build(MoveTemp(FullInput), FullToken);

    TestTrue(TEXT("Sparse incremental visualization succeeds"), SparseResult.IsValid() && SparseResult->bSucceeded);
    TestTrue(TEXT("Full transparency replay succeeds"), FullResult.IsValid() && FullResult->bSucceeded);
    if (SparseResult.IsValid() && SparseResult->bSucceeded &&
        FullResult.IsValid() && FullResult->bSucceeded)
    {
        TestEqual(
            TEXT("Sparse incremental transparency composition matches a full replay"),
            SparseResult->Pixels,
            FullResult->Pixels);

        TArray<uint8> RebuiltPremultiplied;
        TArray<uint8> RebuiltWeight;
        FullResult->RebuiltManualAlphaTileStore.BuildDense(
            RebuiltPremultiplied,
            RebuiltWeight);
        TestEqual(TEXT("Full replay rebuilds the same alpha values"), RebuiltPremultiplied, DensePremultiplied);
        TestEqual(TEXT("Full replay rebuilds the same alpha weights"), RebuiltWeight, DenseWeight);
    }
    return true;
}

#endif
