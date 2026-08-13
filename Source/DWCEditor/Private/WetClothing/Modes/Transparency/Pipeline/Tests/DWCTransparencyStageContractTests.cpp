//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
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
    Payload.OutputResolutionIdentity = TEXT("ResolutionIdentity");
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
    Identity.OutputResolutionIdentity = Payload.OutputResolutionIdentity;
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
    constexpr float MetallicDarkeningA = 0.25f;
    const FString RevealA = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer, MetallicDarkeningA);
    const FString RevealARepeat = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer, MetallicDarkeningA);
    TestEqual(TEXT("Reveal signatures are deterministic."), RevealA, RevealARepeat);

    FDWCTransparencyRevealColorStroke& RevealStroke =
        Layer.RevealColorPaintStrokes.AddDefaulted_GetRef();
    RevealStroke.StrokeGuid = FGuid::NewGuid();
    RevealStroke.MaterialSlotIndex = 3;
    RevealStroke.Samples.AddDefaulted();
    const FString RevealB = FDWCTransparencySignatureService::BuildRevealSignature(
        SourceSignature, Layer, MetallicDarkeningA);
    TestNotEqual(TEXT("Reveal edits invalidate the reveal signature."), RevealA, RevealB);

    const FString RevealWithDifferentMetallicDarkening =
        FDWCTransparencySignatureService::BuildRevealSignature(
            SourceSignature, Layer, 0.75f);
    TestNotEqual(
        TEXT("Metallic darkening invalidates the corrected reveal signature."),
        RevealB,
        RevealWithDifferentMetallicDarkening);

    const FString FinalA = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealB, Layer, TEXT("WrinkleA"), TEXT("SuppressionA"), 8, 4.0f);
    FDWCTransparencyFinalSignatureInputs AlphaInputs;
    AlphaInputs.SourceSignature = SourceSignature;
    AlphaInputs.RevealSignature = RevealB;
    AlphaInputs.AlphaAuthoringSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    AlphaInputs.WrinkleMaskBuildSignature = TEXT("WrinkleA");
    AlphaInputs.SuppressionSettingsSignature = TEXT("SuppressionA");
    AlphaInputs.PaddingPixels = 8;
    AlphaInputs.EdgeFeatherPixels = 4.0f;
    const FString FinalAlphaA =
        FDWCTransparencySignatureService::BuildFinalAlphaSignature(AlphaInputs);
    AlphaInputs.RevealSignature = TEXT("ChangedRevealOnly");
    TestEqual(TEXT("Stage 3 reveal changes do not invalidate Stage 4 alpha."),
        FinalAlphaA,
        FDWCTransparencySignatureService::BuildFinalAlphaSignature(AlphaInputs));
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
    FDWCTransparencyStrokeStorageSignatureParityTest,
    "DWC.Transparency.Pipeline.StrokeStorageSignatureParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStrokeStorageSignatureParityTest::RunTest(const FString&)
{
    FWetClothingTransparencyLayerData LegacyLayer;
    LegacyLayer.LayerGuid = FGuid::NewGuid();
    LegacyLayer.TargetSurface.OuterMaterialSlotIndex = 3;

    FDWCTransparencyBrushSample AlphaSample;
    AlphaSample.PositionUV = FVector2D(0.123456789123, 0.876543210987);
    AlphaSample.UVIslandID = 7;
    AlphaSample.RadiusUV = 0.0375f;
    AlphaSample.Strength = 0.8125f;
    FDWCTransparencyBrushStroke& AlphaStroke = LegacyLayer.EditableStrokes.AddDefaulted_GetRef();
    AlphaStroke.StrokeGuid = FGuid::NewGuid();
    AlphaStroke.MaterialSlotIndex = 3;
    AlphaStroke.BrushMode = EDWCTransparencyBrushMode::SetValue;
    AlphaStroke.TargetAlpha = 0.65f;
    AlphaStroke.Samples.Add(AlphaSample);

    FDWCTransparencyBrushSample RevealSample = AlphaSample;
    RevealSample.PositionUV = FVector2D(0.333333333333, 0.666666666667);
    FDWCTransparencyRevealColorStroke& RevealStroke =
        LegacyLayer.RevealColorPaintStrokes.AddDefaulted_GetRef();
    RevealStroke.StrokeGuid = FGuid::NewGuid();
    RevealStroke.MaterialSlotIndex = 3;
    RevealStroke.PaintColor = FLinearColor(0.2f, 0.4f, 0.8f, 1.0f);
    RevealStroke.Samples.Add(RevealSample);

    const FString LegacyAlphaSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(LegacyLayer);
    const FString LegacyRevealSignature =
        FDWCTransparencySignatureService::BuildRevealSignature(TEXT("Source"), LegacyLayer, 0.35f);

    FWetClothingTransparencyLayerData CompactLayer = LegacyLayer;
    TestTrue(TEXT("Alpha legacy samples compact."),
        CompactLayer.EditableStrokes[0].CompactLegacySamples());
    TestTrue(TEXT("Reveal legacy samples compact."),
        CompactLayer.RevealColorPaintStrokes[0].CompactLegacySamples());
    TestEqual(TEXT("Alpha signature is independent of persisted sample representation."),
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(CompactLayer),
        LegacyAlphaSignature);
    TestEqual(TEXT("Reveal signature is independent of persisted sample representation."),
        FDWCTransparencySignatureService::BuildRevealSignature(TEXT("Source"), CompactLayer, 0.35f),
        LegacyRevealSignature);

    FDWCTransparencyBrushSample AppendedSample = AlphaSample;
    AppendedSample.PositionUV = FVector2D(0.75, 0.25);
    CompactLayer.EditableStrokes[0].Samples.Add(AppendedSample);
    const FString MixedStorageSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(CompactLayer);
    TestTrue(TEXT("Mixed storage merges its legacy tail."),
        CompactLayer.EditableStrokes[0].CompactLegacySamples());
    TestEqual(TEXT("Compacting mixed storage does not change its signature."),
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(CompactLayer),
        MixedStorageSignature);

    CompactLayer.EditableStrokes[0].CompactSamples[0].Strength = 0.25f;
    TestNotEqual(TEXT("A semantic sample edit still changes the signature."),
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(CompactLayer),
        MixedStorageSignature);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStageArtifactSetContractTest,
    "DWC.Transparency.Pipeline.StageArtifactSetContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStageArtifactSetContractTest::RunTest(const FString& Parameters)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    const FString SourceSignature(TEXT("CanonicalSource"));
    const FIntPoint Resolution(256, 256);
    const FGuid Generation = FGuid::NewGuid();

    TArray<EDWCTransparencyTempArtifactKind> RequiredKinds;
    FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(true, RequiredKinds);
    for (const EDWCTransparencyTempArtifactKind Kind : RequiredKinds)
    {
        FDWCTransparencyTempArtifactReference& Reference =
            Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
        Reference.Kind = Kind;
        Reference.Texture = FSoftObjectPath(FString::Printf(
            TEXT("/Game/Generated/T_%d.T_%d"),
            static_cast<int32>(Kind), static_cast<int32>(Kind)));
        Reference.BuildSignature =
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                Kind, SourceSignature);
        Reference.ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
        Reference.CommitGeneration = Generation;
        Reference.TextureSourceId = FGuid::NewGuid();
        Reference.Resolution = Resolution;
    }

    FString Error;
    TestTrue(TEXT("A complete single-generation Stage 2 set is current."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer, SourceSignature, Resolution, false, Error));

    Layer.EditorStageCache.Artifacts.Last().CommitGeneration = FGuid::NewGuid();
    TestFalse(TEXT("Mixed Stage 2 generations are rejected atomically."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer, SourceSignature, Resolution, false, Error));
    Layer.EditorStageCache.Artifacts.Last().CommitGeneration = Generation;

    const FIntPoint OtherSlotResolution(1024, 1024);
    TestFalse(TEXT("A Stage 2 set cannot be reused by a slot with another resolved extent."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer, SourceSignature, OtherSlotResolution, false, Error));

    Layer.EditorStageCache.Artifacts.Last().TextureSourceId = FGuid();
    TestFalse(TEXT("A texture rewritten outside its published artifact metadata is stale."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer, SourceSignature, Resolution, false, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPerSlotArtifactResolutionIsolationTest,
    "DWC.Transparency.Pipeline.PerSlotArtifactResolutionIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPerSlotArtifactResolutionIsolationTest::RunTest(const FString& Parameters)
{
    const auto PopulateSourceArtifacts = [](
        FWetClothingTransparencyLayerData& Layer,
        const FString& Signature,
        const FIntPoint Resolution)
    {
        TArray<EDWCTransparencyTempArtifactKind> RequiredKinds;
        FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(true, RequiredKinds);
        const FGuid Generation = FGuid::NewGuid();
        for (const EDWCTransparencyTempArtifactKind Kind : RequiredKinds)
        {
            FDWCTransparencyTempArtifactReference& Reference =
                Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
            Reference.Kind = Kind;
            Reference.Texture = FSoftObjectPath(FString::Printf(
                TEXT("/Game/Generated/%s_%d.%s_%d"),
                *Signature, static_cast<int32>(Kind),
                *Signature, static_cast<int32>(Kind)));
            Reference.BuildSignature =
                FDWCTransparencyStageArtifactContract::BuildExpectedSignature(Kind, Signature);
            Reference.ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
            Reference.CommitGeneration = Generation;
            Reference.TextureSourceId = FGuid::NewGuid();
            Reference.Resolution = Resolution;
        }
    };

    FWetClothingTransparencyLayerData LayerA;
    LayerA.TargetSurface.OuterMaterialSlotIndex = 2;
    FWetClothingTransparencyLayerData LayerB;
    LayerB.TargetSurface.OuterMaterialSlotIndex = 7;
    const FIntPoint ResolutionA(1024, 1024);
    const FIntPoint ResolutionB(4096, 4096);
    const FString SignatureA(TEXT("SlotA"));
    const FString SignatureB(TEXT("SlotB"));
    PopulateSourceArtifacts(LayerA, SignatureA, ResolutionA);
    PopulateSourceArtifacts(LayerB, SignatureB, ResolutionB);

    FString Error;
    TestTrue(TEXT("Slot A accepts its own 1K artifact set."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            LayerA, SignatureA, ResolutionA, false, Error));
    TestTrue(TEXT("Slot B accepts its own 4K artifact set."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            LayerB, SignatureB, ResolutionB, false, Error));
    TestFalse(TEXT("Slot A cannot consume slot B's 4K extent."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            LayerA, SignatureA, ResolutionB, false, Error));
    TestFalse(TEXT("Slot B cannot consume slot A's 1K extent."),
        FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            LayerB, SignatureB, ResolutionA, false, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencySelectiveArtifactProfileTest,
    "DWC.Transparency.Pipeline.SelectiveArtifactProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencySelectiveArtifactProfileTest::RunTest(const FString&)
{
    TArray<EDWCTransparencyTempArtifactKind> CoreKinds;
    FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(
        FDWCTransparencySourceArtifactSelection::Canonical(true), CoreKinds);
    TestTrue(TEXT("Core artifacts retain target coverage."),
        CoreKinds.Contains(EDWCTransparencyTempArtifactKind::OuterCoverage));
    TestTrue(TEXT("Core projected sources retain reveal surface."),
        CoreKinds.Contains(EDWCTransparencyTempArtifactKind::BaseRevealSurface));
    TestFalse(TEXT("Hit source is diagnostic-only."),
        CoreKinds.Contains(EDWCTransparencyTempArtifactKind::HitSource));
    TestFalse(TEXT("Hit distance is diagnostic-only."),
        CoreKinds.Contains(EDWCTransparencyTempArtifactKind::HitDistance));

    TArray<EDWCTransparencyTempArtifactKind> SparseStage4Kinds;
    FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(
        FDWCTransparencySourceArtifactSelection::Stage4(true, false),
        SparseStage4Kinds);
    TestFalse(TEXT("Sparse Stage 4 does not retain island IDs."),
        SparseStage4Kinds.Contains(EDWCTransparencyTempArtifactKind::OuterIslandID));
    TestFalse(TEXT("Stage 4 never retains hit-source diagnostics."),
        SparseStage4Kinds.Contains(EDWCTransparencyTempArtifactKind::HitSource));

    TArray<EDWCTransparencyTempArtifactKind> DiagnosticKinds;
    FDWCTransparencyStageArtifactContract::GetRequiredSourceArtifacts(
        FDWCTransparencySourceArtifactSelection::Diagnostics(true), DiagnosticKinds);
    TestTrue(TEXT("Diagnostic view requests hit source."),
        DiagnosticKinds.Contains(EDWCTransparencyTempArtifactKind::HitSource));
    TestTrue(TEXT("Diagnostic view requests hit distance."),
        DiagnosticKinds.Contains(EDWCTransparencyTempArtifactKind::HitDistance));
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
    const FGuid SourceGeneration = FGuid::NewGuid();
    const auto AddArtifact = [&Metadata](
        const EDWCTransparencyTempArtifactKind Kind,
        const FString& SourceSignature,
        const FString& RevealSignature,
        const FGuid& Generation)
    {
        FDWCTransparencyTempArtifactReference& Artifact =
            Metadata.Artifacts.AddDefaulted_GetRef();
        Artifact.Kind = Kind;
        Artifact.Texture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(FString::Printf(
            TEXT("/Game/Generated/Temp/T_%d.T_%d"),
            static_cast<int32>(Kind), static_cast<int32>(Kind))));
        Artifact.BuildSignature =
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                Kind, SourceSignature, RevealSignature);
        Artifact.ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
        Artifact.CommitGeneration = Generation;
        Artifact.TextureSourceId = FGuid::NewGuid();
        Artifact.Resolution = FIntPoint(128, 128);
    };
    constexpr EDWCTransparencyTempArtifactKind SourceKinds[] = {
        EDWCTransparencyTempArtifactKind::BaseRevealColor,
        EDWCTransparencyTempArtifactKind::BaseRevealSurface,
        EDWCTransparencyTempArtifactKind::ValidHit,
        EDWCTransparencyTempArtifactKind::OuterCoverage,
        EDWCTransparencyTempArtifactKind::OuterIslandID,
        EDWCTransparencyTempArtifactKind::HitSource,
        EDWCTransparencyTempArtifactKind::HitDistance
    };
    for (const EDWCTransparencyTempArtifactKind Kind : SourceKinds)
    {
        AddArtifact(Kind, TEXT("Source"), FString(), SourceGeneration);
    }
    AddArtifact(
        EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        TEXT("Source"), TEXT("Reveal"), FGuid::NewGuid());

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
    TestTrue(TEXT("Reveal invalidation obsoletes corrected reveal artifacts."), Metadata.Artifacts.Last().bObsolete);

    Metadata.MarkSourceStale();
    TestFalse(TEXT("Source invalidation clears the source checkpoint."), Metadata.bSourceGenerated);
    TestTrue(TEXT("Source invalidation obsoletes every downstream artifact."), Metadata.Artifacts[0].bObsolete);
    return true;
}

#endif
