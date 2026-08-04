#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

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
