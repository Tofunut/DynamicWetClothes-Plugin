//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyDirtyTileReplayWorker.h"

namespace
{
    constexpr int32 ReplayTestSlot = 3;
    constexpr int32 ReplayTestIsland = 5;

    TSharedRef<FDWCTransparencySourcePayload> BuildReplayTestResult()
    {
        constexpr int32 Size = FDWCTransparencyAlphaTileStore::TileSize;
        TSharedRef<FDWCTransparencySourcePayload> Result = MakeShared<FDWCTransparencySourcePayload>();
        Result->MaterialSlotIndex = ReplayTestSlot;
        Result->Resolution = FIntPoint(Size, Size);
        const int32 PixelCount = Size * Size;
        Result->InnerColorBuffer.Init(FColor(32, 64, 96, 255), PixelCount);
        Result->AutoAlphaBuffer.Init(96, PixelCount);
        Result->OuterCoverageBuffer.Init(255, PixelCount);
        Result->OuterIslandIDBuffer.Init(
            FDWCTransparencySourcePayload::EncodeOuterIslandID(ReplayTestIsland),
            PixelCount);
        return Result;
    }

    FDWCTransparencyBrushSample MakeSample(const FVector2D Position, const float Radius, const float Strength)
    {
        FDWCTransparencyBrushSample Sample;
        Sample.PositionUV = Position;
        Sample.UVIslandID = ReplayTestIsland;
        Sample.RadiusUV = Radius;
        Sample.Strength = Strength;
        return Sample;
    }

    FDWCTransparencyBrushStroke MakeAlphaStroke(
        const FVector2D Position,
        const float TargetAlpha)
    {
        FDWCTransparencyBrushStroke Stroke;
        Stroke.StrokeGuid = FGuid::NewGuid();
        Stroke.MaterialSlotIndex = ReplayTestSlot;
        Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
        Stroke.BrushMode = EDWCTransparencyBrushMode::SetValue;
        Stroke.Falloff = 0.45f;
        Stroke.TargetAlpha = TargetAlpha;
        Stroke.Samples.Add(MakeSample(Position, 0.24f, 0.8f));
        return Stroke;
    }

    FDWCTransparencyRevealColorStroke MakeRevealStroke(
        const FVector2D Position,
        const FLinearColor Color)
    {
        FDWCTransparencyRevealColorStroke Stroke;
        Stroke.StrokeGuid = FGuid::NewGuid();
        Stroke.MaterialSlotIndex = ReplayTestSlot;
        Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
        Stroke.BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;
        Stroke.PaintColor = Color;
        Stroke.Falloff = 0.45f;
        Stroke.Samples.Add(MakeSample(Position, 0.24f, 0.8f));
        return Stroke;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAlphaDirtyTileReplayParityTest,
    "DWC.Editor.Transparency.DirtyReplay.AlphaParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAlphaDirtyTileReplayParityTest::RunTest(const FString&)
{
    const TSharedRef<FDWCTransparencySourcePayload> SourcePayload = BuildReplayTestResult();
    const FDWCTransparencyBrushStroke SurvivingStroke = MakeAlphaStroke(FVector2D(0.58, 0.5), 0.8f);

    FDWCTransparencyAlphaTileStore ExpectedStore;
    ExpectedStore.Initialize(SourcePayload->Resolution);
    TArray<FDWCTransparencyAlphaTilePayload> ExpectedTiles;
    ExpectedStore.SnapshotTiles({FIntPoint::ZeroValue}, ExpectedTiles);
    TestTrue(
        TEXT("The surviving alpha stroke rasterizes for the full-replay reference"),
        FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
            *SourcePayload,
            SurvivingStroke,
            SurvivingStroke.Samples,
            {FIntPoint::ZeroValue},
            ExpectedTiles));

    FDWCTransparencyDirtyTileReplayJobInput Input;
    Input.Target = EDWCTransparencyDirtyReplayTarget::Alpha;
    Input.SourcePayload = SourcePayload;
    Input.MaterialSlotIndex = ReplayTestSlot;
    FDWCTransparencyBrushStroke CompactStroke = SurvivingStroke;
    TestTrue(TEXT("The replay fixture converts legacy samples to compact storage"), CompactStroke.CompactLegacySamples());
    Input.AlphaStrokes = {MoveTemp(CompactStroke)};
    Input.DirtyTileCoordinates = {FIntPoint::ZeroValue};
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe> Result =
        FDWCTransparencyDirtyTileReplayWorker::Build(MoveTemp(Input), Token);

    TestTrue(TEXT("Alpha dirty replay succeeds"), Result.IsValid() && Result->bSucceeded);
    if (!Result.IsValid() || !Result->bSucceeded || Result->AlphaTiles.Num() != 1 || ExpectedTiles.Num() != 1)
    {
        return false;
    }
    TestEqual(TEXT("Alpha replay premultiplied values match full replay"),
        Result->AlphaTiles[0].Premultiplied, ExpectedTiles[0].Premultiplied);
    TestEqual(TEXT("Alpha replay weights match full replay"),
        Result->AlphaTiles[0].Weight, ExpectedTiles[0].Weight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealColorDirtyTileReplayParityTest,
    "DWC.Editor.Transparency.DirtyReplay.RevealColorParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealColorDirtyTileReplayParityTest::RunTest(const FString&)
{
    const TSharedRef<FDWCTransparencySourcePayload> SourcePayload = BuildReplayTestResult();
    const FDWCTransparencyRevealColorStroke SurvivingStroke =
        MakeRevealStroke(FVector2D(0.58, 0.5), FLinearColor(0.1f, 0.8f, 0.2f));
    const FLinearColor BaseRevealColor(0.1f, 0.2f, 0.3f);

    FDWCTransparencyRevealColorTileStore ExpectedStore;
    ExpectedStore.Initialize(SourcePayload->Resolution);
    TArray<FDWCTransparencyRevealColorTilePayload> ExpectedTiles;
    ExpectedStore.SnapshotTiles(
        {FIntPoint::ZeroValue},
        MakeArrayView(SourcePayload->InnerColorBuffer),
        ExpectedTiles);
    TestTrue(
        TEXT("The surviving reveal-color stroke rasterizes for the full-replay reference"),
        FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
            *SourcePayload,
            SurvivingStroke,
            SurvivingStroke.Samples,
            BaseRevealColor,
            {FIntPoint::ZeroValue},
            ExpectedTiles));

    FDWCTransparencyDirtyTileReplayJobInput Input;
    Input.Target = EDWCTransparencyDirtyReplayTarget::RevealColor;
    Input.SourcePayload = SourcePayload;
    Input.MaterialSlotIndex = ReplayTestSlot;
    Input.BaseRevealColor = BaseRevealColor;
    FDWCTransparencyRevealColorStroke CompactStroke = SurvivingStroke;
    TestTrue(TEXT("The reveal fixture converts legacy samples to compact storage"), CompactStroke.CompactLegacySamples());
    Input.RevealColorStrokes = {MoveTemp(CompactStroke)};
    Input.DirtyTileCoordinates = {FIntPoint::ZeroValue};
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe> Result =
        FDWCTransparencyDirtyTileReplayWorker::Build(MoveTemp(Input), Token);

    TestTrue(TEXT("Reveal-color dirty replay succeeds"), Result.IsValid() && Result->bSucceeded);
    if (!Result.IsValid() || !Result->bSucceeded || Result->RevealColorTiles.Num() != 1 || ExpectedTiles.Num() != 1)
    {
        return false;
    }
    TestEqual(TEXT("Reveal-color replay values match full replay"),
        Result->RevealColorTiles[0].Colors, ExpectedTiles[0].Colors);
    return true;
}

#endif
