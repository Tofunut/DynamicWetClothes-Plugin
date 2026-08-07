//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleAccumulatedPreviewWorker.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"

namespace
{
    FDWCEditorNormalSourceSnapshot MakeConstantNormalSource(const FColor Color)
    {
        FDWCEditorNormalSourceSnapshot Source;
        Source.Texture.Width = 1;
        Source.Texture.Height = 1;
        Source.Texture.BytesPerPixel = sizeof(FColor);
        Source.Texture.bSRGB = false;
        Source.Texture.Format = TSF_BGRA8;
        Source.Texture.RawData = MakeShared<TArray64<uint8>>();
        Source.Texture.RawData->SetNumUninitialized(sizeof(FColor));
        FMemory::Memcpy(Source.Texture.RawData->GetData(), &Color, sizeof(FColor));
        return Source;
    }

    FDWCEditorNormalStampCommand MakeTestStamp(const FVector2f CenterUV)
    {
        FDWCEditorNormalStampCommand Command;
        Command.Footprint.CenterUV = CenterUV;
        Command.Footprint.RadiusUV = 0.18f;
        Command.Footprint.Scale = FVector2f(1.0f, 0.7f);
        Command.Footprint.Falloff = 0.2f;
        Command.Footprint.bWrap = true;
        Command.NormalSource = MakeConstantNormalSource(FColor(220, 128, 220, 255));
        Command.Strength = 1.0f;
        return Command;
    }

    FWetProceduralRidgeStroke MakeTestRidge()
    {
        FWetProceduralRidgeStroke Stroke;
        Stroke.MaterialSlotIndex = 2;
        Stroke.WidthUV = 0.06f;
        Stroke.Strength = 1.25f;
        Stroke.Falloff = 0.45f;
        for (const FVector2D UV : {
                 FVector2D(0.20, 0.30),
                 FVector2D(0.42, 0.48),
                 FVector2D(0.70, 0.62) })
        {
            FWetProceduralRidgeStrokePoint& Point = Stroke.Points.AddDefaulted_GetRef();
            Point.PositionUV = UV;
        }
        return Stroke;
    }

    bool CompareIncrementalResultToFullSurface(
        FAutomationTestBase& Test,
        const FWetWrinkleIncrementalPreviewJobResult& Result,
        const FDWCEditorNormalRasterSurface& FullSurface,
        TConstArrayView<FColor> FullPixels)
    {
        for (const FDWCEditorNormalRegionPayload& Region : Result.Regions)
        {
            for (int32 Y = Region.WorkingRect.Min.Y; Y < Region.WorkingRect.Max.Y; ++Y)
            {
                for (int32 X = Region.WorkingRect.Min.X; X < Region.WorkingRect.Max.X; ++X)
                {
                    const int32 LocalIndex =
                        (Y - Region.WorkingRect.Min.Y) * Region.WorkingRect.Width() +
                        (X - Region.WorkingRect.Min.X);
                    const int32 FullIndex = Y * FullSurface.Size.X + X;
                    if (Region.PackedNormalXY[LocalIndex] != FullSurface.PackedNormalXY[FullIndex])
                    {
                        Test.AddError(FString::Printf(
                            TEXT("Incremental working normal differs at (%d, %d)."), X, Y));
                        return false;
                    }
                }
            }
            for (int32 Y = Region.OutputRect.Min.Y; Y < Region.OutputRect.Max.Y; ++Y)
            {
                for (int32 X = Region.OutputRect.Min.X; X < Region.OutputRect.Max.X; ++X)
                {
                    const int32 LocalIndex =
                        (Y - Region.OutputRect.Min.Y) * Region.OutputRect.Width() +
                        (X - Region.OutputRect.Min.X);
                    const int32 FullIndex = Y * Result.Target.Descriptor.Size.X + X;
                    if (Region.EncodedPixels[LocalIndex] != FullPixels[FullIndex])
                    {
                        Test.AddError(FString::Printf(
                            TEXT("Incremental encoded normal differs at (%d, %d)."), X, Y));
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool ApplyIncrementalNormalRegions(
        const FWetWrinkleIncrementalPreviewJobResult& Result,
        FDWCEditorNormalRasterSurface& InOutSurface,
        TArray<FColor>& InOutPixels)
    {
        for (const FDWCEditorNormalRegionPayload& Region : Result.Regions)
        {
            const int32 WorkingPixelCount = Region.WorkingRect.Width() * Region.WorkingRect.Height();
            const int32 OutputPixelCount = Region.OutputRect.Width() * Region.OutputRect.Height();
            if (Region.PackedNormalXY.Num() != WorkingPixelCount ||
                (!Region.Coverage.IsEmpty() && Region.Coverage.Num() != WorkingPixelCount) ||
                Region.EncodedPixels.Num() != OutputPixelCount)
            {
                return false;
            }

            for (int32 Y = Region.WorkingRect.Min.Y; Y < Region.WorkingRect.Max.Y; ++Y)
            {
                for (int32 X = Region.WorkingRect.Min.X; X < Region.WorkingRect.Max.X; ++X)
                {
                    const int32 LocalIndex =
                        (Y - Region.WorkingRect.Min.Y) * Region.WorkingRect.Width() +
                        (X - Region.WorkingRect.Min.X);
                    const int32 SurfaceIndex = Y * InOutSurface.Size.X + X;
                    InOutSurface.PackedNormalXY[SurfaceIndex] = Region.PackedNormalXY[LocalIndex];
                    if (!Region.Coverage.IsEmpty() && InOutSurface.HasCoverage())
                    {
                        InOutSurface.Coverage[SurfaceIndex] = Region.Coverage[LocalIndex];
                    }
                }
            }

            for (int32 Y = Region.OutputRect.Min.Y; Y < Region.OutputRect.Max.Y; ++Y)
            {
                for (int32 X = Region.OutputRect.Min.X; X < Region.OutputRect.Max.X; ++X)
                {
                    const int32 LocalIndex =
                        (Y - Region.OutputRect.Min.Y) * Region.OutputRect.Width() +
                        (X - Region.OutputRect.Min.X);
                    InOutPixels[Y * Result.Target.Descriptor.Size.X + X] =
                        Region.EncodedPixels[LocalIndex];
                }
            }
        }
        return true;
    }

    bool RunIncrementalWrinkleBatch(
        FAutomationTestBase& Test,
        TArray<FWetWrinkleIncrementalCommand> Commands,
        FDWCEditorNormalRasterSurface& InOutSurface,
        TArray<FColor>& InOutPixels,
        const FIntPoint OutputSize)
    {
        TArray<FWetWrinkleIncrementalRegionPlan> Plan;
        if (!FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(
                Commands,
                InOutSurface.Size,
                OutputSize,
                Plan))
        {
            Test.AddError(TEXT("Failed to build a mixed wrinkle incremental region plan."));
            return false;
        }

        FWetWrinkleIncrementalPreviewJobInput Input;
        Input.TextureSize = OutputSize;
        Input.WorkingTextureSize = InOutSurface.Size;
        Input.Commands = MoveTemp(Commands);
        Input.Target.Descriptor.Size = OutputSize;
        for (const FWetWrinkleIncrementalRegionPlan& Item : Plan)
        {
            FWetWrinkleIncrementalRegionSnapshot& Snapshot = Input.Regions.AddDefaulted_GetRef();
            Snapshot.Plan = Item;
            if (!Snapshot.Region.InitializeFromSurface(InOutSurface, Item.WorkingRect))
            {
                Test.AddError(TEXT("Failed to snapshot the current wrinkle working surface."));
                return false;
            }
        }

        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
            MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
        const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
            FWetWrinkleIncrementalPreviewWorker::Build(MoveTemp(Input), Token);
        if (!Result.IsValid() || !Result->bSucceeded)
        {
            Test.AddError(TEXT("Mixed wrinkle incremental worker failed."));
            return false;
        }
        if (!ApplyIncrementalNormalRegions(*Result, InOutSurface, InOutPixels))
        {
            Test.AddError(TEXT("Mixed wrinkle incremental payload was invalid."));
            return false;
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorNormalRasterIncrementalParityTest,
    "DWC.Editor.Foundation.Raster.NormalIncrementalParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorNormalRasterIncrementalParityTest::RunTest(const FString&)
{
    FDWCEditorNormalRasterSurface BatchSurface;
    FDWCEditorNormalRasterSurface IncrementalSurface;
    BatchSurface.Initialize(FIntPoint(32, 32), true);
    IncrementalSurface.Initialize(FIntPoint(32, 32), true);
    const FDWCEditorNormalStampCommand First = MakeTestStamp(FVector2f(0.35f, 0.5f));
    FDWCEditorNormalStampCommand Second = MakeTestStamp(FVector2f(0.62f, 0.5f));
    Second.Footprint.RotationRadians = 0.7f;

    FDWCEditorNormalRasterCore::RasterizeStamp(First, BatchSurface);
    FDWCEditorNormalRasterCore::RasterizeStamp(Second, BatchSurface);
    const FDWCEditorRasterResult FirstResult =
        FDWCEditorNormalRasterCore::RasterizeStamp(First, IncrementalSurface);
    const FDWCEditorRasterResult SecondResult =
        FDWCEditorNormalRasterCore::RasterizeStamp(Second, IncrementalSurface);

    TestTrue(TEXT("Both incremental stamps affect pixels"),
        FirstResult.bAffectedPixels && SecondResult.bAffectedPixels);
    TestEqual(TEXT("Normal counts match"), BatchSurface.GetPixelCount(), IncrementalSurface.GetPixelCount());
    for (int32 Index = 0; Index < BatchSurface.GetPixelCount(); ++Index)
    {
        if (!BatchSurface.GetNormal(Index).Equals(IncrementalSurface.GetNormal(Index), 1.0e-4f) ||
            !FMath::IsNearlyEqual(BatchSurface.Coverage[Index], IncrementalSurface.Coverage[Index], 1.0e-6f))
        {
            AddError(FString::Printf(TEXT("Batch and incremental surfaces differ at pixel %d."), Index));
            break;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorNormalRasterWrapTest,
    "DWC.Editor.Foundation.Raster.WrapBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorNormalRasterWrapTest::RunTest(const FString&)
{
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(FIntPoint(32, 16), false);
    FDWCEditorNormalStampCommand Command = MakeTestStamp(FVector2f(0.98f, 0.5f));
    Command.Footprint.RadiusUV = 0.12f;
    const FDWCEditorRasterResult Result = FDWCEditorNormalRasterCore::RasterizeStamp(Command, Surface);

    TestTrue(TEXT("Wrapped stamp affects pixels"), Result.bAffectedPixels);
    const int32 Row = Surface.Size.Y / 2;
    TestTrue(TEXT("Right edge receives the stamp"), FMath::Abs(Surface.GetNormal(Row * Surface.Size.X + 31).X) > 0.05f);
    TestTrue(TEXT("Left edge receives the wrapped stamp"), FMath::Abs(Surface.GetNormal(Row * Surface.Size.X).X) > 0.05f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorNormalRasterPostProcessTest,
    "DWC.Editor.Foundation.Raster.PostProcess",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorNormalRasterPostProcessTest::RunTest(const FString&)
{
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(FIntPoint(5, 5), true);
    const int32 CenterIndex = 2 * Surface.Size.X + 2;
    Surface.SetNormal(CenterIndex, FVector3f(0.5f, 0.0f, 0.8660254f));
    Surface.Coverage[CenterIndex] = 0.75f;
    TArray<uint8> IslandMask;
    IslandMask.Init(0, 25);
    IslandMask[CenterIndex] = 255;
    FDWCEditorRasterPostProcess::DilateIntoPadding(Surface, IslandMask, 1);

    const int32 NeighborIndex = 2 * Surface.Size.X + 3;
    TestTrue(TEXT("Dilation copies the boundary normal"),
        Surface.GetNormal(NeighborIndex).Equals(Surface.GetNormal(CenterIndex), 1.0e-4f));
    TestTrue(TEXT("Dilation copies coverage"),
        FMath::IsNearlyEqual(Surface.Coverage[NeighborIndex], 0.75f));

    FDWCEditorNormalRasterSurface Downsampled;
    TestTrue(TEXT("Normal-aware downsample succeeds"),
        FDWCEditorRasterPostProcess::DownsampleNormalSurface(Surface, FIntPoint(1, 1), Downsampled));
    TestTrue(TEXT("Downsampled normal stays normalized"),
        FMath::IsNearlyEqual(Downsampled.GetNormal(0).Size(), 1.0f, 1.0e-5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWrinklePatchRegionParityTest,
    "DWC.Editor.Wrinkle.IncrementalPreview.PatchParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWrinklePatchRegionParityTest::RunTest(const FString&)
{
    const FIntPoint WorkingSize(64, 64);
    const FIntPoint OutputSize(32, 32);
    FWetWrinkleIncrementalCommand Command;
    Command.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    Command.Patch = MakeTestStamp(FVector2f(0.98f, 0.53f));
    TArray<FWetWrinkleIncrementalCommand> Commands = { Command };

    FDWCEditorNormalRasterSurface FullSurface;
    FullSurface.Initialize(WorkingSize, false);
    FDWCEditorNormalRasterCore::RasterizeStamp(Command.Patch, FullSurface);
    TArray<FColor> FullPixels;
    TestTrue(TEXT("Full patch output encodes"),
        FDWCEditorRasterPostProcess::ResampleAndEncodeNormalPixels(
            FullSurface, OutputSize, FullPixels));

    TArray<FWetWrinkleIncrementalRegionPlan> Plan;
    TestTrue(TEXT("Patch region plan builds"),
        FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(
            Commands, WorkingSize, OutputSize, Plan));
    FWetWrinkleIncrementalPreviewJobInput Input;
    Input.TextureSize = OutputSize;
    Input.WorkingTextureSize = WorkingSize;
    Input.Commands = Commands;
    Input.Target.Descriptor.Size = OutputSize;
    for (const FWetWrinkleIncrementalRegionPlan& Item : Plan)
    {
        FWetWrinkleIncrementalRegionSnapshot& Snapshot = Input.Regions.AddDefaulted_GetRef();
        Snapshot.Plan = Item;
        TestTrue(TEXT("Patch source region initializes"),
            Snapshot.Region.Initialize(WorkingSize, Item.WorkingRect, false));
    }
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
        FWetWrinkleIncrementalPreviewWorker::Build(MoveTemp(Input), Token);
    TestTrue(TEXT("Patch region worker succeeds"), Result.IsValid() && Result->bSucceeded);
    if (Result.IsValid() && Result->bSucceeded)
    {
        TestTrue(TEXT("Patch region result matches full raster"),
            CompareIncrementalResultToFullSurface(*this, *Result, FullSurface, FullPixels));
    }

    const FDWCEditorWorkerMemoryEstimate Estimate =
        FWetWrinkleIncrementalPreviewWorker::EstimateMemory(Commands, Plan, false);
    const uint64 FullSurfaceBytes =
        static_cast<uint64>(WorkingSize.X) * WorkingSize.Y * sizeof(uint32) +
        static_cast<uint64>(OutputSize.X) * OutputSize.Y * sizeof(FColor);
    TestTrue(TEXT("Patch region job stays below a full-buffer snapshot"),
        Estimate.GetTotalBytes() < FullSurfaceBytes);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWrinkleRidgeRegionParityTest,
    "DWC.Editor.Wrinkle.IncrementalPreview.RidgeParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWrinkleRidgeRegionParityTest::RunTest(const FString&)
{
    const FIntPoint Size(64, 64);
    const FWetProceduralRidgeStroke Stroke = MakeTestRidge();
    FWetWrinkleIncrementalCommand Command;
    Command.Kind = EWetWrinkleIncrementalCommandKind::Ridge;
    Command.Ridge = Stroke;
    TArray<FWetWrinkleIncrementalCommand> Commands = { Command };

    FDWCEditorNormalRasterSurface FullSurface;
    FullSurface.Initialize(Size, false);
    FWetProceduralRidgeRasterizer::RasterizeToSurface(Stroke, FullSurface);
    TArray<FColor> FullPixels;
    TestTrue(TEXT("Full ridge output encodes"),
        FDWCEditorRasterPostProcess::ResampleAndEncodeNormalPixels(FullSurface, Size, FullPixels));

    TArray<FWetWrinkleIncrementalRegionPlan> Plan;
    TestTrue(TEXT("Ridge region plan builds"),
        FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(Commands, Size, Size, Plan));
    FWetWrinkleIncrementalPreviewJobInput Input;
    Input.TextureSize = Size;
    Input.WorkingTextureSize = Size;
    Input.Commands = Commands;
    Input.Target.Descriptor.Size = Size;
    Input.bClearRegionsToFlat = true;
    for (const FWetWrinkleIncrementalRegionPlan& Item : Plan)
    {
        FWetWrinkleIncrementalRegionSnapshot& Snapshot = Input.Regions.AddDefaulted_GetRef();
        Snapshot.Plan = Item;
        TestTrue(TEXT("Ridge source region initializes"),
            Snapshot.Region.Initialize(Size, Item.WorkingRect, false));
    }
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
        FWetWrinkleIncrementalPreviewWorker::Build(MoveTemp(Input), Token);
    TestTrue(TEXT("Ridge region worker succeeds"), Result.IsValid() && Result->bSucceeded);
    if (Result.IsValid() && Result->bSucceeded)
    {
        TestTrue(TEXT("Ridge region result matches full raster"),
            CompareIncrementalResultToFullSurface(*this, *Result, FullSurface, FullPixels));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWrinkleMixedIncrementalLifecycleParityTest,
    "DWC.Editor.Regression.IncrementalLifecycle.WrinkleMixedParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWrinkleMixedIncrementalLifecycleParityTest::RunTest(const FString&)
{
    const FIntPoint WorkingSize(64, 64);
    const FIntPoint OutputSize(32, 32);
    FDWCEditorNormalStampCommand FirstPatch = MakeTestStamp(FVector2f(0.31f, 0.47f));
    FDWCEditorNormalStampCommand WrappedPatch = MakeTestStamp(FVector2f(0.98f, 0.56f));
    WrappedPatch.Footprint.RadiusUV = 0.12f;
    WrappedPatch.Footprint.RotationRadians = 0.63f;
    WrappedPatch.Strength = 0.72f;
    const FWetProceduralRidgeStroke Ridge = MakeTestRidge();

    FWetWrinkleAccumulatedPreviewJobInput FullInput;
    FullInput.TextureSize = OutputSize;
    FullInput.WorkingTextureSize = WorkingSize;
    FullInput.Patches = {FirstPatch, WrappedPatch};
    FullInput.RidgeStrokes = {Ridge};
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> FullToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> FullResult =
        FWetWrinkleAccumulatedPreviewWorker::Build(MoveTemp(FullInput), FullToken);
    TestTrue(TEXT("Full mixed wrinkle replay succeeds"), FullResult.IsValid() && FullResult->bSucceeded);
    if (!FullResult.IsValid() || !FullResult->bSucceeded)
    {
        return false;
    }

    FDWCEditorNormalRasterSurface IncrementalSurface;
    IncrementalSurface.Initialize(WorkingSize, false);
    TArray<FColor> IncrementalPixels;
    IncrementalPixels.Init(FColor(128, 128, 255, 255), OutputSize.X * OutputSize.Y);

    FWetWrinkleIncrementalCommand FirstCommand;
    FirstCommand.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    FirstCommand.Sequence = 1;
    FirstCommand.Patch = FirstPatch;
    FWetWrinkleIncrementalCommand WrappedCommand;
    WrappedCommand.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    WrappedCommand.Sequence = 2;
    WrappedCommand.Patch = WrappedPatch;
    TestTrue(
        TEXT("Overlapping and wrapped patches commit incrementally"),
        RunIncrementalWrinkleBatch(
            *this,
            {FirstCommand, WrappedCommand},
            IncrementalSurface,
            IncrementalPixels,
            OutputSize));

    FWetWrinkleIncrementalCommand RidgeCommand;
    RidgeCommand.Kind = EWetWrinkleIncrementalCommandKind::Ridge;
    RidgeCommand.Sequence = 3;
    RidgeCommand.Ridge = Ridge;
    TestTrue(
        TEXT("Ridge commits on top of the current incremental surface"),
        RunIncrementalWrinkleBatch(
            *this,
            {RidgeCommand},
            IncrementalSurface,
            IncrementalPixels,
            OutputSize));

    TestEqual(
        TEXT("Incremental mixed wrinkle packed normals match a full replay"),
        IncrementalSurface.PackedNormalXY,
        FullResult->WorkingSurface.PackedNormalXY);
    TestEqual(
        TEXT("Incremental mixed wrinkle pixels match a full replay"),
        IncrementalPixels,
        FullResult->Pixels);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPixelComposerParityTest,
    "DWC.Editor.Foundation.Raster.TransparencyComposerParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPixelComposerParityTest::RunTest(const FString&)
{
    FDWCTransparencyAutoBakeResult AutoResult;
    AutoResult.Resolution = FIntPoint(2, 1);
    AutoResult.InnerColorBuffer = { FColor(10, 20, 30, 255), FColor(40, 50, 60, 255) };
    AutoResult.AutoAlphaBuffer = { 128, 220 };
    AutoResult.ValidHitBuffer.Init(false, 2);
    AutoResult.ValidHitBuffer[0] = true;
    AutoResult.HitDistanceBuffer = { 2.0f, 0.0f };
    AutoResult.SourcePriorityBuffer = { 1, INDEX_NONE };
    const TArray<uint8> ManualPremultiplied = { 64, 0 };
    const TArray<uint8> ManualWeight = { 128, 0 };
    const TArray<uint8> Suppression = { 32, 0 };
    const TArray<uint8> Feather = { 200, 255 };

    FDWCTransparencyPixelComposeContext Context;
    Context.AutoResult = &AutoResult;
    Context.ManualPremultipliedBuffer = MakeArrayView(ManualPremultiplied);
    Context.ManualWeightBuffer = MakeArrayView(ManualWeight);
    Context.WrinkleSuppressionBuffer = MakeArrayView(Suppression);
    Context.OuterEdgeFeatherBuffer = MakeArrayView(Feather);
    Context.VisualizationMode = EDWCTransparencyVisualizationMode::AutoAlpha;
    TArray<FColor> Pixels;
    TestTrue(TEXT("Batch composition succeeds"),
        FDWCTransparencyComposite::ComposeVisualizationPixels(Context, Pixels));
    TestEqual(TEXT("Batch and single-pixel composition match"),
        Pixels[0], FDWCTransparencyComposite::ComposeVisualizationPixel(Context, 0));
    TestEqual(TEXT("Batch and single-pixel composition match for second pixel"),
        Pixels[1], FDWCTransparencyComposite::ComposeVisualizationPixel(Context, 1));

    TestTrue(
        TEXT("Outer island IDs fit in the compact representation"),
        FDWCTransparencyAutoBakeResult::CanEncodeOuterIslandID(12));
    TestEqual(
        TEXT("Compact outer island IDs round-trip"),
        FDWCTransparencyAutoBakeResult::DecodeOuterIslandID(
            FDWCTransparencyAutoBakeResult::EncodeOuterIslandID(12)),
        12);
    TestEqual(
        TEXT("Invalid outer island sentinel decodes to INDEX_NONE"),
        FDWCTransparencyAutoBakeResult::DecodeOuterIslandID(
            FDWCTransparencyAutoBakeResult::InvalidOuterIslandID),
        INDEX_NONE);
    return true;
}

#endif
