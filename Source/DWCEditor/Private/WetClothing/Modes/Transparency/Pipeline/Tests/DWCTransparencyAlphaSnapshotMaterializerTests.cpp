//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAlphaSnapshotMaterializer.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyAlphaSnapshotMaterializerParityTest,
    "DWC.Editor.Transparency.Pipeline.AlphaSnapshotMaterializerParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyAlphaSnapshotMaterializerParityTest::RunTest(const FString& Parameters)
{
    constexpr int32 MaterialSlotIndex = 4;
    const FIntPoint Resolution(256, 128);
    const int32 PixelCount = Resolution.X * Resolution.Y;

    FDWCTransparencySourcePayload SourcePayload;
    SourcePayload.LayerGuid = FGuid::NewGuid();
    SourcePayload.MaterialSlotIndex = MaterialSlotIndex;
    SourcePayload.Resolution = Resolution;
    SourcePayload.OutputResolutionIdentity = TEXT("ResolutionIdentity");
    SourcePayload.BuildSignature = TEXT("AlphaDomainSource");
    SourcePayload.AutoAlphaBuffer.Init(51, PixelCount);
    SourcePayload.OuterCoverageBuffer.Init(255, PixelCount);
    SourcePayload.ValidHitBuffer.Init(true, PixelCount);
    SourcePayload.OuterIslandIDBuffer.Init(
        FDWCTransparencySourcePayload::EncodeOuterIslandID(2),
        PixelCount);

    FDWCTransparencyBrushStroke Stroke;
    Stroke.MaterialSlotIndex = MaterialSlotIndex;
    Stroke.UVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
    Stroke.BrushMode = EDWCTransparencyBrushMode::SetValue;
    Stroke.TargetAlpha = 0.8f;
    Stroke.Falloff = 0.55f;
    FDWCTransparencyBrushSample& Sample = Stroke.Samples.AddDefaulted_GetRef();
    Sample.PositionUV = FVector2D(0.25, 0.48);
    Sample.UVIslandID = 2;
    Sample.RadiusUV = 0.08f;
    Sample.Strength = 0.9f;

    TArray<uint8> DensePremultiplied;
    TArray<uint8> DenseWeight;
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        SourcePayload,
        {Stroke},
        0,
        MaterialSlotIndex,
        0,
        DensePremultiplied,
        DenseWeight);
    TestTrue(TEXT("Persisted replay uses compact samples."), Stroke.CompactLegacySamples());
    TestTrue(TEXT("Legacy sample storage is released."), Stroke.Samples.IsEmpty());
    TestEqual(TEXT("Compact storage preserves sample count."), Stroke.GetSampleCount(), 1);

    FDWCTransparencyAlphaWorkingSnapshot Replay;
    Replay.Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    Replay.Resolution = Resolution;
    Replay.AuthoredStrokeCount = 1;
    Replay.AppliedSampleCount = 1;
    Replay.FallbackStrokes = {Stroke};

    FDWCTransparencyAlphaWorkingSnapshot Sparse;
    FString Error;
    const TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot> AlphaDomain =
        FDWCTransparencyAlphaDomainSnapshot::Create(SourcePayload, &Error);
    TestTrue(TEXT("A valid alpha-only domain is extracted from the source payload."), AlphaDomain.IsValid());
    if (!AlphaDomain.IsValid())
    {
        return false;
    }
    TestTrue(
        TEXT("Stroke replay materializes into a sparse tile snapshot."),
        FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
            *AlphaDomain,
            MoveTemp(Replay),
            Sparse,
            Error));
    TestTrue(TEXT("Materialized snapshot is valid."), Sparse.IsValid(&Error));
    TestTrue(
        TEXT("A local brush occupies fewer tiles than the complete texture."),
        Sparse.ModifiedTiles.Num() <
            FMath::DivideAndRoundUp(Resolution.X, FDWCTransparencyAlphaTileStore::TileSize) *
            FMath::DivideAndRoundUp(Resolution.Y, FDWCTransparencyAlphaTileStore::TileSize));

    FDWCTransparencyAlphaSnapshotView View;
    TestTrue(TEXT("Materialized sparse snapshot is readable."), View.Initialize(Sparse, &Error));
    if (View.IsValid())
    {
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            TestEqual(
                TEXT("Sparse replay premultiplied alpha matches dense replay."),
                View.GetPremultiplied(PixelIndex),
                DensePremultiplied[PixelIndex]);
            TestEqual(
                TEXT("Sparse replay weight matches dense replay."),
                View.GetWeight(PixelIndex),
                DenseWeight[PixelIndex]);
        }
    }

    const int32 TileCount = Sparse.ModifiedTiles.Num();
    FDWCTransparencyAlphaWorkingSnapshot PassThrough;
    TestTrue(
        TEXT("A sparse snapshot passes through without dense reconstruction."),
        FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
            *AlphaDomain,
            MoveTemp(Sparse),
            PassThrough,
            Error));
    TestEqual(TEXT("Sparse pass-through preserves tile count."), PassThrough.ModifiedTiles.Num(), TileCount);
    return true;
}

#endif
