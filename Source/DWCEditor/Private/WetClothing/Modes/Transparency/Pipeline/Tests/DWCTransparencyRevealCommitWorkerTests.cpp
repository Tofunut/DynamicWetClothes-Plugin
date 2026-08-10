//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealCommitSparseParityTest,
    "DWC.Editor.Transparency.Stage3Commit.SparseTileParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealCommitSparseParityTest::RunTest(const FString&)
{
    constexpr int32 Size = FDWCTransparencyRevealColorTileStore::TileSize * 2;
    const FColor BaseColor(20, 40, 60, 255);
    const FColor EditedColor(180, 80, 30, 255);
    constexpr uint8 AutoAlpha = 83;
    TSharedRef<FDWCTransparencySourcePayload> Source =
        MakeShared<FDWCTransparencySourcePayload>();
    Source->Resolution = FIntPoint(Size, Size);
    Source->MaterialSlotIndex = 4;
    Source->BuildSignature = TEXT("Stage3Source");
    Source->InnerColorBuffer.Init(BaseColor, Size * Size);
    Source->AutoAlphaBuffer.Init(AutoAlpha, Size * Size);

    FDWCTransparencyRevealColorTileStore Store;
    Store.Initialize(Source->Resolution);
    for (int32 Y = 0; Y < FDWCTransparencyRevealColorTileStore::TileSize; ++Y)
    {
        for (int32 X = 0; X < FDWCTransparencyRevealColorTileStore::TileSize; ++X)
        {
            Store.SetColor(X, Y, EditedColor, MakeArrayView(Source->InnerColorBuffer));
        }
    }

    FDWCTransparencyRevealCommitJobInput Input;
    Input.SourceResult = Source;
    Input.bUseSparseTiles = true;
    Store.SnapshotModifiedTiles(Input.ModifiedTiles);
    TestEqual(TEXT("Only the edited tile is snapshotted"), Input.ModifiedTiles.Num(), 1);

    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Result =
        FDWCTransparencyRevealCommitWorker::Build(MoveTemp(Input), Token);
    TestTrue(TEXT("Sparse reveal commit succeeds"), Result.IsValid() && Result->bSucceeded);
    if (!Result.IsValid() || !Result->bSucceeded)
    {
        return false;
    }

    TestEqual(
        TEXT("Edited tile reaches the dense artifact while preserving automatic alpha"),
        Result->CorrectedRevealPixels[0],
        FColor(EditedColor.R, EditedColor.G, EditedColor.B, AutoAlpha));
    TestEqual(
        TEXT("Untouched tiles preserve the immutable Stage 2 source"),
        Result->CorrectedRevealPixels[Size - 1],
        FColor(BaseColor.R, BaseColor.G, BaseColor.B, AutoAlpha));
    TestEqual(TEXT("Source signature survives worker transfer"), Result->SourceSignature, Source->BuildSignature);
    TestTrue(TEXT("Result records the sparse path"), Result->bUsedSparseTiles);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealCommitFallbackParityTest,
    "DWC.Editor.Transparency.Stage3Commit.PendingPreviewFallbackParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealCommitFallbackParityTest::RunTest(const FString&)
{
    constexpr int32 Size = FDWCTransparencyRevealColorTileStore::TileSize;
    TSharedRef<FDWCTransparencySourcePayload> Source =
        MakeShared<FDWCTransparencySourcePayload>();
    Source->Resolution = FIntPoint(Size, Size);
    Source->MaterialSlotIndex = 7;
    Source->BuildSignature = TEXT("PendingStage3Source");
    Source->InnerColorBuffer.Init(FColor(25, 50, 75, 255), Size * Size);
    Source->AutoAlphaBuffer.SetNumUninitialized(Size * Size);
    for (int32 PixelIndex = 0; PixelIndex < Source->AutoAlphaBuffer.Num(); ++PixelIndex)
    {
        Source->AutoAlphaBuffer[PixelIndex] = static_cast<uint8>((PixelIndex * 19) & 0xff);
    }
    Source->OuterCoverageBuffer.Init(255, Size * Size);
    Source->OuterIslandIDBuffer.Init(
        FDWCTransparencySourcePayload::EncodeOuterIslandID(0), Size * Size);

    FDWCTransparencyRevealColorStroke Stroke;
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.MaterialSlotIndex = Source->MaterialSlotIndex;
    Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
    Stroke.BrushMode = EDWCTransparencyRevealColorBrushMode::Paint;
    Stroke.PaintColor = FLinearColor(0.8f, 0.2f, 0.1f);
    Stroke.Falloff = 0.5f;
    FDWCTransparencyBrushSample& Sample = Stroke.Samples.AddDefaulted_GetRef();
    Sample.PositionUV = FVector2D(0.5, 0.5);
    Sample.UVIslandID = 0;
    Sample.RadiusUV = 0.25f;
    Sample.Strength = 0.75f;

    TArray<FColor> Expected = Source->InnerColorBuffer;
    FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
        *Source,
        TArray<FDWCTransparencyRevealColorStroke>{Stroke},
        Source->MaterialSlotIndex,
        FLinearColor::White,
        Expected);
    for (int32 PixelIndex = 0; PixelIndex < Expected.Num(); ++PixelIndex)
    {
        Expected[PixelIndex].A = Source->AutoAlphaBuffer[PixelIndex];
    }

    FDWCTransparencyRevealCommitJobInput Input;
    Input.SourceResult = Source;
    Input.FallbackStrokes.Add(Stroke);
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Result =
        FDWCTransparencyRevealCommitWorker::Build(MoveTemp(Input), Token);

    TestTrue(TEXT("Pending-preview fallback commit succeeds"), Result.IsValid() && Result->bSucceeded);
    if (!Result.IsValid() || !Result->bSucceeded)
    {
        return false;
    }
    TestEqual(TEXT("Fallback output pixel count matches"), Result->CorrectedRevealPixels.Num(), Expected.Num());
    TestTrue(
        TEXT("Worker fallback matches the canonical full stroke replay"),
        Result->CorrectedRevealPixels == Expected);
    TestFalse(TEXT("Fallback result records the replay path"), Result->bUsedSparseTiles);
    return true;
}

#endif
