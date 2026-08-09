//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyCanonicalSourcePayloadTest,
    "DWC.Transparency.Pipeline.CanonicalSourcePayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyCanonicalSourcePayloadTest::RunTest(const FString& Parameters)
{
    FDWCTransparencySourcePayload Payload;
    Payload.LayerGuid = FGuid::NewGuid();
    Payload.MaterialSlotIndex = 4;
    Payload.UVChannelIndex = 2;
    Payload.LODIndex = 0;
    Payload.Resolution = FIntPoint(2, 2);
    Payload.BuildSignature = TEXT("SourceSignature");
    Payload.OuterIslandIDBuffer.Init(
        FDWCTransparencySourcePayload::InvalidOuterIslandID, 4);
    Payload.OuterIslandIDBuffer[0] = FDWCTransparencySourcePayload::EncodeOuterIslandID(7);

    FDWCTransparencyStageIdentity Identity;
    Identity.LayerGuid = Payload.LayerGuid;
    Identity.MaterialSlotIndex = Payload.MaterialSlotIndex;
    Identity.DataUVChannelIndex = Payload.UVChannelIndex;
    Identity.LODIndex = Payload.LODIndex;
    Identity.Resolution = Payload.Resolution;
    Identity.Revision = 17;

    TestTrue(TEXT("The canonical source payload maps to a valid stage identity."), Identity.IsValid());
    TestEqual(TEXT("The canonical payload resolves covered island IDs."),
        Payload.ResolveOuterIslandIDAtUV(FVector2D(0.1, 0.1), 3, false), 7);
    TestEqual(TEXT("The canonical payload uses the hit fallback for uncovered texels."),
        Payload.ResolveOuterIslandIDAtUV(FVector2D(0.75, 0.75), 3, false), 3);
    TestEqual(TEXT("The request revision remains part of stage identity, not source pixels."),
        Identity.Revision, uint64(17));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStageSignatureDependencyTest,
    "DWC.Transparency.Pipeline.StageSignatureDependencies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStageSignatureDependencyTest::RunTest(const FString& Parameters)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 3;

    const FString SourceSignature(TEXT("SourceA"));
    const FString RevealA = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer);
    const FString RevealARepeat = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer);
    TestEqual(TEXT("Reveal signatures are deterministic."), RevealA, RevealARepeat);

    FDWCTransparencyRevealColorStroke& RevealStroke =
        Layer.RevealColorPaintStrokes.AddDefaulted_GetRef();
    RevealStroke.StrokeGuid = FGuid::NewGuid();
    RevealStroke.MaterialSlotIndex = 3;
    RevealStroke.Samples.AddDefaulted();
    const FString RevealB = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer);
    TestNotEqual(TEXT("Reveal edits invalidate the reveal signature."), RevealA, RevealB);

    const FString FinalA = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealB, Layer, TEXT("WrinkleA"), TEXT("SuppressionA"), 8, 4.0f);
    FDWCTransparencyBrushStroke& AlphaStroke = Layer.EditableStrokes.AddDefaulted_GetRef();
    AlphaStroke.StrokeGuid = FGuid::NewGuid();
    AlphaStroke.MaterialSlotIndex = 3;
    AlphaStroke.Samples.AddDefaulted();
    const FString FinalB = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealB, Layer, TEXT("WrinkleA"), TEXT("SuppressionA"), 8, 4.0f);
    TestNotEqual(TEXT("Alpha edits invalidate only the final signature input."), FinalA, FinalB);

    const FString FinalC = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealB, Layer, TEXT("WrinkleB"), TEXT("SuppressionA"), 8, 4.0f);
    TestNotEqual(TEXT("Wrinkle dependency changes invalidate the final signature."), FinalB, FinalC);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStageCacheInvalidationTest,
    "DWC.Transparency.Pipeline.StageCacheInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStageCacheInvalidationTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyEditorStageCacheMetadata Metadata;
    Metadata.SourceSignature = TEXT("Source");
    Metadata.RevealSignature = TEXT("Reveal");
    Metadata.bSourceGenerated = true;
    Metadata.bRevealReviewed = true;
    Metadata.Artifacts.AddDefaulted_GetRef().Kind =
        EDWCTransparencyTempArtifactKind::BaseRevealColor;
    Metadata.Artifacts[0].Texture = TSoftObjectPtr<UTexture2D>(
        FSoftObjectPath(TEXT("/Game/Generated/Temp/T_BaseReveal.T_BaseReveal")));
    Metadata.Artifacts[0].BuildSignature = TEXT("Source");
    Metadata.Artifacts.AddDefaulted_GetRef().Kind =
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor;

    FWetClothingTransparencyLayerData Layer;
    Layer.EditorStageCache = Metadata;
    const FDWCTransparencyStageStatus Current =
        FDWCTransparencySignatureService::EvaluateEditorStageCache(
            Layer, TEXT("Source"), TEXT("Reveal"));
    TestTrue(TEXT("A complete matching stage cache is current."), Current.IsCurrent());

    const FDWCTransparencyStageStatus RevealChanged =
        FDWCTransparencySignatureService::EvaluateEditorStageCache(
            Layer, TEXT("Source"), TEXT("RevealChanged"));
    TestEqual(TEXT("Reveal mismatches report the reveal stage."),
        RevealChanged.Stage, EDWCTransparencyStage::Reveal);
    TestEqual(TEXT("Reveal mismatches return a precise stale reason."),
        RevealChanged.Reason, EDWCTransparencyStaleReason::RevealEditsChanged);

    Metadata.MarkRevealStale();
    TestTrue(TEXT("Reveal invalidation preserves the source checkpoint."), Metadata.bSourceGenerated);
    TestFalse(TEXT("Reveal invalidation clears review state."), Metadata.bRevealReviewed);
    TestFalse(TEXT("Reveal invalidation preserves source artifacts."), Metadata.Artifacts[0].bObsolete);
    TestTrue(TEXT("Reveal invalidation obsoletes corrected reveal artifacts."), Metadata.Artifacts[1].bObsolete);

    Metadata.MarkSourceStale();
    TestFalse(TEXT("Source invalidation clears the source checkpoint."), Metadata.bSourceGenerated);
    TestTrue(TEXT("Source invalidation obsoletes every downstream artifact."), Metadata.Artifacts[0].bObsolete);
    return true;
}

#endif
