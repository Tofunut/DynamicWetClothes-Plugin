//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionReducer.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowPolicy.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowStateResolver.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBlueprintHierarchySession.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"

namespace
{
    bool HasEffect(
        const EDWCEditorSessionEffect Effects,
        const EDWCEditorSessionEffect Expected)
    {
        return EnumHasAnyFlags(Effects, Expected);
    }

    void AddCurrentArtifact(
        FWetClothingTransparencyLayerData& Layer,
        const EDWCTransparencyTempArtifactKind Kind,
        const FString& SourceSignature,
        const FString& RevealSignature,
        const FGuid& CommitGeneration)
    {
#if WITH_EDITORONLY_DATA
        FDWCTransparencyTempArtifactReference& Reference =
            Layer.EditorStageCache.Artifacts.AddDefaulted_GetRef();
        Reference.Kind = Kind;
        Reference.BuildSignature =
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                Kind, SourceSignature, RevealSignature);
        Reference.ContractVersion = FDWCTransparencyStageArtifactContract::ContractVersion;
        Reference.CommitGeneration = CommitGeneration;
        Reference.TextureSourceId = FGuid::NewGuid();
        Reference.Resolution = FIntPoint(128, 128);
        Reference.Texture = FSoftObjectPath(TEXT("/Game/DWC_TestArtifact.DWC_TestArtifact"));
#endif
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkflowStageResolutionTest,
    "DWC.Editor.Transparency.Workflow.StageResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkflowStageResolutionTest::RunTest(const FString& Parameters)
{
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState Unconfigured =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(false, nullptr, false);
    TestEqual(TEXT("An unconfigured WCA starts in Stage 1"),
        Unconfigured.DefaultStage,
        EDWCTransparencyEditorStage::StructureSetup);

    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState ConfiguredWithoutLayer =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, nullptr, false);
    TestEqual(TEXT("A configured WCA without a target part starts in Stage 2"),
        ConfiguredWithoutLayer.DefaultStage,
        EDWCTransparencyEditorStage::MapGeneration);

    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState MissingSource =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, &Layer, false);
    TestEqual(TEXT("A target part without a source starts in Stage 2"),
        MissingSource.DefaultStage,
        EDWCTransparencyEditorStage::MapGeneration);

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.bSourceGenerated = true;
    Layer.EditorStageCache.SourceSignature = TEXT("SourceSignature");
    const FGuid SourceGeneration = FGuid::NewGuid();
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::BaseRevealSurface,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::ValidHit,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::OuterCoverage,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::OuterIslandID,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::HitSource,
        TEXT("SourceSignature"), FString(), SourceGeneration);
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::HitDistance,
        TEXT("SourceSignature"), FString(), SourceGeneration);
#endif
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState SourceReady =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, &Layer, false);
    TestTrue(TEXT("Current Stage 2 artifacts enable Stage 3"), SourceReady.CanEnterRevealEditing());
    TestEqual(TEXT("An unreviewed source starts in Stage 3"),
        SourceReady.DefaultStage,
        EDWCTransparencyEditorStage::RevealEditing);

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.Artifacts.RemoveAll(
        [](const FDWCTransparencyTempArtifactReference& Reference)
        {
            return Reference.Kind == EDWCTransparencyTempArtifactKind::HitDistance;
        });
#endif
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState IncompleteDiagnosticSource =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, &Layer, false);
    TestTrue(TEXT("Missing diagnostic artifacts require a Stage 2 rebuild instead of default diagnostics."),
        IncompleteDiagnosticSource.bRequiresSourceRegeneration);
    TestEqual(TEXT("An incomplete diagnostic source returns to Stage 2."),
        IncompleteDiagnosticSource.DefaultStage,
        EDWCTransparencyEditorStage::MapGeneration);

#if WITH_EDITORONLY_DATA
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::HitDistance,
        TEXT("SourceSignature"), FString(), SourceGeneration);
#endif

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.bRevealReviewed = true;
    Layer.EditorStageCache.RevealSignature = TEXT("RevealSignature");
    AddCurrentArtifact(Layer, EDWCTransparencyTempArtifactKind::CorrectedRevealColor,
        TEXT("SourceSignature"), TEXT("RevealSignature"), FGuid::NewGuid());
#endif
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState RevealReady =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, &Layer, false);
    TestTrue(TEXT("A corrected reveal enables Stage 4"), RevealReady.CanEnterFinalEditing());
    TestEqual(TEXT("A reviewed source starts in Stage 4"),
        RevealReady.DefaultStage,
        EDWCTransparencyEditorStage::FinalEditing);

#if WITH_EDITORONLY_DATA
    Layer.EditorStageCache.Artifacts[0].bObsolete = true;
#endif
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState StaleSource =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(true, &Layer, true);
    TestTrue(TEXT("Invalid source artifacts require Stage 2 regeneration"), StaleSource.bRequiresSourceRegeneration);
    TestEqual(TEXT("A stale source wins over an older baked map for the default stage"),
        StaleSource.DefaultStage,
        EDWCTransparencyEditorStage::MapGeneration);

    FDWCEditorSessionState SessionState;
    const FGuid FirstLayerGuid = FGuid::NewGuid();
    const FGuid SecondLayerGuid = FGuid::NewGuid();
    SessionState.AuthoringIndex.TransparencyLayerGuids.Add(FirstLayerGuid);
    SessionState.AuthoringIndex.TransparencyLayerGuids.Add(SecondLayerGuid);
    SessionState.AuthoringIndex.TransparencyLayerByMaterialSlot.Add(3, FirstLayerGuid);
    SessionState.AuthoringIndex.TransparencyLayerByMaterialSlot.Add(7, SecondLayerGuid);
    FDWCEditorSessionReducer::Reduce(
        SessionState,
        FDWCSelectTransparencyTargetSlotAndStageAction{
            3,
            EDWCTransparencyEditorStage::MapGeneration});
    const EDWCEditorSessionEffect SelectionEffects = FDWCEditorSessionReducer::Reduce(
        SessionState,
        FDWCSelectTransparencyTargetSlotAndStageAction{
            7,
            EDWCTransparencyEditorStage::FinalEditing});
    TestEqual(TEXT("Atomic selection switches to the new target part"),
        SessionState.Transparency.SelectedMaterialSlotIndex,
        7);
    TestEqual(TEXT("Atomic selection uses the new layer's resolved Stage instead of copying Stage 2"),
        SessionState.Transparency.StageByLayer.FindRef(SecondLayerGuid),
        EDWCTransparencyEditorStage::FinalEditing);
    TestTrue(TEXT("Atomic selection refreshes stage content"),
        HasEffect(SelectionEffects, EDWCEditorSessionEffect::RefreshStageContent));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkflowInputRoutingTest,
    "DWC.Editor.Transparency.Workflow.InputRouting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkflowInputRoutingTest::RunTest(const FString& Parameters)
{
    TestTrue(
        TEXT("A configured implemented structure can continue to Stage 2"),
        DWCTransparencyWorkflow::CanContinueToGeneration(
            true,
            true,
            EDWCTransparencySourceType::SameMeshMaterialSlots));
    TestFalse(
        TEXT("An unconfigured structure cannot continue to Stage 2"),
        DWCTransparencyWorkflow::CanContinueToGeneration(
            true,
            false,
            EDWCTransparencySourceType::SameMeshMaterialSlots));
    TestTrue(
        TEXT("The Blueprint multi-mesh structure can continue to Stage 2"),
        DWCTransparencyWorkflow::CanContinueToGeneration(
            true,
            true,
            EDWCTransparencySourceType::OtherSkeletalMeshComponents));
    TestTrue(
        TEXT("The external skeletal mesh structure can continue to Stage 2"),
        DWCTransparencyWorkflow::CanContinueToGeneration(
            true,
            true,
            EDWCTransparencySourceType::ExternalSkeletalMesh));

    TestEqual(
        TEXT("Stage 1 never enables painting"),
        DWCTransparencyWorkflow::ResolvePaintTarget(
            EDWCTransparencyEditorStage::StructureSetup,
            EDWCTransparencySourceType::ManualColorOrTexture),
        EDWCTransparencyPaintTarget::None);
    TestEqual(
        TEXT("Stage 2 same-mesh workflow does not enable a brush"),
        DWCTransparencyWorkflow::ResolvePaintTarget(
            EDWCTransparencyEditorStage::MapGeneration,
            EDWCTransparencySourceType::SameMeshMaterialSlots),
        EDWCTransparencyPaintTarget::None);
    TestEqual(
        TEXT("Stage 2 manual color only authors the source"),
        DWCTransparencyWorkflow::ResolvePaintTarget(
            EDWCTransparencyEditorStage::MapGeneration,
            EDWCTransparencySourceType::ManualColorOrTexture),
        EDWCTransparencyPaintTarget::None);
    TestEqual(
        TEXT("Stage 3 exposes a reveal target for every source type"),
        DWCTransparencyWorkflow::ResolvePaintTarget(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots),
        EDWCTransparencyPaintTarget::RevealColor);
    TestEqual(
        TEXT("Stage 4 always routes to alpha painting"),
        DWCTransparencyWorkflow::ResolvePaintTarget(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::ManualColorOrTexture),
        EDWCTransparencyPaintTarget::FinalAlpha);

    TestTrue(
        TEXT("Stage 3 allows the immutable Base Reveal Color view"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::BaseRevealColor));
    TestTrue(
        TEXT("Stage 3 allows the live correction-difference view"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::CorrectionDifference));
    TestTrue(
        TEXT("Raycast source types expose the Stage 3 gap diagnostic"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::RaycastGaps));
    TestFalse(
        TEXT("Manual color sources do not expose raycast-only diagnostics"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::RaycastGaps));
    TestFalse(
        TEXT("Stage 3 does not expose final transparency visualization"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::Final));
    TestTrue(
        TEXT("Stage 4 keeps wrinkle separation visualization"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::WrinkleSeparation));
    TestTrue(
        TEXT("Raycast source types expose Reveal Normal inspection in Stage 4"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::RevealNormalOnly));
    TestTrue(
        TEXT("Raycast source types expose source coverage in Stage 4"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::ExternalSkeletalMesh,
            EDWCTransparencyVisualizationMode::SourceCoverage));
    TestFalse(
        TEXT("Manual color sources do not expose Reveal Normal inspection"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::RevealNormalOnly));
    TestFalse(
        TEXT("Stage 4 does not expose reveal correction visualization"),
        DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::CorrectionDifference));

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext RevealContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::Final,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            true,
            true,
            false);
    TestEqual(TEXT("Stage 3 derives reveal visualization"),
        RevealContext.VisualizationMode,
        EDWCTransparencyVisualizationMode::InnerColor);
    TestEqual(TEXT("Stage 3 derives target-mesh preview"),
        RevealContext.PreviewMode,
        EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    TestTrue(TEXT("Stage 3 keeps its source working map"),
        RevealContext.bUseRevealWorkingMap);
    TestTrue(TEXT("A valid Stage 2 source map enables Stage 3 reveal painting"),
        RevealContext.bEnableRevealColorPainting);

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext Type2GenerationContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::MapGeneration,
            EDWCTransparencySourceType::OtherSkeletalMeshComponents,
            EDWCTransparencyVisualizationMode::Final,
            EWetClothingTransparencyPreviewMode::TargetMeshOnly,
            true,
            false,
            false);
    TestEqual(TEXT("Type 2 Stage 2 always exposes selected Blueprint source meshes"),
        Type2GenerationContext.PreviewMode,
        EWetClothingTransparencyPreviewMode::FullBlueprint);

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext DifferenceContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::SameMeshMaterialSlots,
            EDWCTransparencyVisualizationMode::CorrectionDifference,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            true,
            true,
            false);
    TestEqual(TEXT("Stage 3 preserves its correction visualization selection"),
        DifferenceContext.VisualizationMode,
        EDWCTransparencyVisualizationMode::CorrectionDifference);

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext MissingRevealMapContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::Final,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            true,
            false,
            false);
    TestFalse(TEXT("Stage 3 blocks reveal input until its source working map is ready"),
        MissingRevealMapContext.bEnableRevealColorPainting);

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext FinalContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::AutoAlpha,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            true,
            false,
            true);
    TestEqual(TEXT("Stage 4 derives final-alpha painting"),
        FinalContext.PaintTarget,
        EDWCTransparencyPaintTarget::FinalAlpha);
    TestEqual(TEXT("Stage 4 preserves the requested visualization"),
        FinalContext.VisualizationMode,
        EDWCTransparencyVisualizationMode::AutoAlpha);
    TestTrue(TEXT("Stage 4 enables alpha painting when a working map exists"),
        FinalContext.bEnableFinalAlphaPainting);

    FDWCEditorSessionState State;
    const FGuid LayerGuid = FGuid::NewGuid();
    FDWCSetTransparencyEditContextAction InitialContext;
    InitialContext.Context.LayerGuid = LayerGuid;
    InitialContext.Context.MaterialSlotIndex = 7;
    InitialContext.Context.UVChannelIndex = 2;
    InitialContext.Context.PaintTarget = EDWCTransparencyPaintTarget::RevealColor;
    FDWCEditorSessionReducer::Reduce(State, InitialContext);

    FDWCSetTransparencyEditContextAction PaintTargetOnlyChange = InitialContext;
    PaintTargetOnlyChange.Context.PaintTarget = EDWCTransparencyPaintTarget::FinalAlpha;
    const EDWCEditorSessionEffect PaintTargetEffects =
        FDWCEditorSessionReducer::Reduce(State, PaintTargetOnlyChange);
    TestTrue(TEXT("Paint-target transition updates preview parameters"),
        HasEffect(PaintTargetEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestFalse(TEXT("Paint-target transition does not rebuild hit topology"),
        HasEffect(PaintTargetEffects, EDWCEditorSessionEffect::RebuildHitTopology));

    FDWCSetTransparencyEditContextAction SlotChange = PaintTargetOnlyChange;
    SlotChange.Context.MaterialSlotIndex = 8;
    const EDWCEditorSessionEffect SlotEffects = FDWCEditorSessionReducer::Reduce(State, SlotChange);
    TestTrue(TEXT("Material-slot transition rebuilds hit topology"),
        HasEffect(SlotEffects, EDWCEditorSessionEffect::RebuildHitTopology));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkflowPersistentStateTest,
    "DWC.Editor.Transparency.Workflow.PersistentState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkflowPersistentStateTest::RunTest(const FString& Parameters)
{
    FDWCEditorSessionState State;
    const FGuid FirstLayerGuid = FGuid::NewGuid();
    const FGuid SecondLayerGuid = FGuid::NewGuid();

    const EDWCEditorSessionEffect FirstStageEffects = FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSetTransparencyStageAction{FirstLayerGuid, EDWCTransparencyEditorStage::MapGeneration});
    TestTrue(TEXT("A new layer stage refreshes stage content"),
        HasEffect(FirstStageEffects, EDWCEditorSessionEffect::RefreshStageContent));

    const EDWCEditorSessionEffect RepeatedStageEffects = FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSetTransparencyStageAction{FirstLayerGuid, EDWCTransparencyEditorStage::MapGeneration});
    TestEqual(TEXT("Reapplying the active stage does not recreate stage content"),
        RepeatedStageEffects,
        EDWCEditorSessionEffect::None);

    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSetTransparencyStageAction{SecondLayerGuid, EDWCTransparencyEditorStage::FinalEditing});
    TestEqual(TEXT("The first layer retains its Stage 2 selection"),
        State.Transparency.StageByLayer.FindRef(FirstLayerGuid),
        EDWCTransparencyEditorStage::MapGeneration);
    TestEqual(TEXT("The second layer keeps its independent Stage 3 selection"),
        State.Transparency.StageByLayer.FindRef(SecondLayerGuid),
        EDWCTransparencyEditorStage::FinalEditing);

    FDWCSetTransparencyPaintAction AlphaPaint;
    AlphaPaint.Paint.bRevealColorPaint = false;
    AlphaPaint.Paint.Strength = 0.25f;
    FDWCEditorSessionReducer::Reduce(State, AlphaPaint);

    FDWCSetTransparencyPaintAction RevealPaint;
    RevealPaint.bRevealPaint = true;
    RevealPaint.Paint.bEnabled = true;
    RevealPaint.Paint.Strength = 0.75f;
    RevealPaint.Paint.Spacing = 0.9f;
    RevealPaint.Paint.TargetAlpha = 0.0f;
    RevealPaint.Paint.RevealColor = FLinearColor(0.2f, 0.3f, 0.4f, 0.0f);
    const EDWCEditorSessionEffect RevealPaintEffects =
        FDWCEditorSessionReducer::Reduce(State, RevealPaint);

    TestEqual(TEXT("Alpha brush settings remain independent"), State.Transparency.Paint.Strength, 0.25f);
    TestEqual(TEXT("Reveal brush settings remain independent"), State.Transparency.RevealPaint.Strength, 0.75f);
    TestTrue(TEXT("Reveal-paint reducer marks the reveal layer explicitly"),
        State.Transparency.RevealPaint.bRevealColorPaint);
    TestTrue(TEXT("Reveal-paint reducer uses the fixed reveal spacing"),
        FMath::IsNearlyEqual(State.Transparency.RevealPaint.Spacing, 0.25f));
    TestTrue(TEXT("Reveal-paint reducer retains full reveal coverage"),
        FMath::IsNearlyEqual(State.Transparency.RevealPaint.TargetAlpha, 1.0f));
    TestTrue(TEXT("Reveal-paint reducer forces opaque reveal color"),
        FMath::IsNearlyEqual(State.Transparency.RevealPaint.RevealColor.A, 1.0f));
    TestTrue(TEXT("Reveal-paint state changes update the viewport through the session"),
        HasEffect(RevealPaintEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkflowRevealInputAvailabilityTest,
    "DWC.Editor.Transparency.Workflow.RevealInputAvailability",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkflowRevealInputAvailabilityTest::RunTest(const FString& Parameters)
{
    FDWCEditorSessionState State;
    const FGuid LayerGuid = FGuid::NewGuid();

    FDWCSetTransparencyStageAction Stage2Action;
    Stage2Action.LayerGuid = LayerGuid;
    Stage2Action.Stage = EDWCTransparencyEditorStage::RevealEditing;
    FDWCEditorSessionReducer::Reduce(State, Stage2Action);

    FDWCSetTransparencyEditContextAction RevealContextAction;
    RevealContextAction.Context.LayerGuid = LayerGuid;
    RevealContextAction.Context.MaterialSlotIndex = 3;
    RevealContextAction.Context.UVChannelIndex = 1;
    RevealContextAction.Context.PaintTarget = EDWCTransparencyPaintTarget::RevealColor;
    RevealContextAction.Context.bSurfacePaintingEnabled = false;
    FDWCEditorSessionReducer::Reduce(State, RevealContextAction);

    TestFalse(TEXT("An unavailable Stage 2 result blocks Stage 3 surface input"),
        State.Transparency.EditContext.bSurfacePaintingEnabled);
    TestEqual(TEXT("Stage 3 keeps the reveal paint target while waiting for its source"),
        State.Transparency.EditContext.PaintTarget,
        EDWCTransparencyPaintTarget::RevealColor);

    FDWCSetTransparencyEditContextAction ReadyRevealContextAction = RevealContextAction;
    ReadyRevealContextAction.Context.bSurfacePaintingEnabled = true;
    const EDWCEditorSessionEffect ReadyEffects =
        FDWCEditorSessionReducer::Reduce(State, ReadyRevealContextAction);
    TestTrue(TEXT("A ready Stage 2 result updates Stage 3 preview input state"),
        HasEffect(ReadyEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestFalse(TEXT("Working-map readiness does not rebuild hit topology"),
        HasEffect(ReadyEffects, EDWCEditorSessionEffect::RebuildHitTopology));
    TestTrue(TEXT("A ready Stage 2 result enables Stage 3 surface input"),
        State.Transparency.EditContext.bSurfacePaintingEnabled);

    FDWCSetTransparencyStageAction Stage4Action;
    Stage4Action.LayerGuid = LayerGuid;
    Stage4Action.Stage = EDWCTransparencyEditorStage::FinalEditing;
    const EDWCEditorSessionEffect Stage4Effects =
        FDWCEditorSessionReducer::Reduce(State, Stage4Action);
    TestTrue(TEXT("Stage 4 refreshes stage content"),
        HasEffect(Stage4Effects, EDWCEditorSessionEffect::RefreshStageContent));

    FDWCSetTransparencyEditContextAction FinalContextAction = RevealContextAction;
    FinalContextAction.Context.PaintTarget = EDWCTransparencyPaintTarget::FinalAlpha;
    const EDWCEditorSessionEffect FinalContextEffects =
        FDWCEditorSessionReducer::Reduce(State, FinalContextAction);
    TestTrue(TEXT("Stage 4 context updates preview parameters"),
        HasEffect(FinalContextEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestEqual(TEXT("Stage 4 switches to final-alpha painting"),
        State.Transparency.EditContext.PaintTarget,
        EDWCTransparencyPaintTarget::FinalAlpha);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyCharacterTypeDraftCommitTest,
    "DWC.Editor.Transparency.Workflow.CharacterTypeDraftCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyCharacterTypeDraftCommitTest::RunTest(const FString& Parameters)
{
    FDWCEditorSessionState State;
    FDWCInitializeTransparencyCharacterTypeAction Initialize;
    Initialize.SavedType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    Initialize.bSavedTypeConfigured = true;
    FDWCEditorSessionReducer::Reduce(State, Initialize);

    const EDWCEditorSessionEffect DraftEffects = FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSelectTransparencyCharacterTypeDraftAction{
            EDWCTransparencySourceType::ManualColorOrTexture});
    TestEqual(TEXT("Selecting a type changes only the draft"),
        State.Transparency.SavedCharacterType,
        EDWCTransparencySourceType::SameMeshMaterialSlots);
    TestEqual(TEXT("The selected type is retained in the session draft"),
        State.Transparency.DraftCharacterType,
        EDWCTransparencySourceType::ManualColorOrTexture);
    TestTrue(TEXT("A different draft is marked dirty"),
        State.Transparency.bCharacterTypeDraftDirty);
    TestFalse(TEXT("Draft selection does not rebuild preview content"),
        HasEffect(DraftEffects, EDWCEditorSessionEffect::RebuildPreviewContent));
    TestFalse(TEXT("Draft selection does not rebuild preview materials"),
        HasEffect(DraftEffects, EDWCEditorSessionEffect::RebuildPreviewMaterials));

    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCCancelTransparencyCharacterTypeDraftAction{});
    TestEqual(TEXT("Cancel restores the saved type"),
        State.Transparency.DraftCharacterType,
        EDWCTransparencySourceType::SameMeshMaterialSlots);
    TestFalse(TEXT("Cancel clears dirty state"),
        State.Transparency.bCharacterTypeDraftDirty);

    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSelectTransparencyCharacterTypeDraftAction{
            EDWCTransparencySourceType::ManualColorOrTexture});
    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCCommitTransparencyCharacterTypeSucceededAction{
            EDWCTransparencySourceType::ManualColorOrTexture});
    TestEqual(TEXT("Successful commit updates the saved snapshot"),
        State.Transparency.SavedCharacterType,
        EDWCTransparencySourceType::ManualColorOrTexture);
    TestFalse(TEXT("Successful commit clears dirty state"),
        State.Transparency.bCharacterTypeDraftDirty);

    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSelectTransparencyCharacterTypeDraftAction{
            EDWCTransparencySourceType::SameMeshMaterialSlots});
    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCReconcileTransparencyCharacterTypeAction{
            EDWCTransparencySourceType::ManualColorOrTexture,
            true});
    TestEqual(TEXT("Reconcile preserves an active draft"),
        State.Transparency.DraftCharacterType,
        EDWCTransparencySourceType::SameMeshMaterialSlots);

    FWetClothingTransparencyData TransparencyData;
    TransparencyData.CharacterStructureType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    TransparencyData.bCharacterStructureTypeConfigured = true;
    FWetClothingTransparencyLayerData& Layer =
        TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    Layer.SameMeshSource.InnerSlotPriority.AddDefaulted();
    Layer.RevealColorPaintStrokes.AddDefaulted();
    Layer.EditableStrokes.AddDefaulted();
    Layer.BakedMaps.AddDefaulted();

    DWCTransparencyWorkflow::ApplyCharacterTypeCommit(
        TransparencyData,
        EDWCTransparencySourceType::ManualColorOrTexture);
    TestEqual(TEXT("Commit synchronizes every existing layer"),
        Layer.SourceType,
        EDWCTransparencySourceType::ManualColorOrTexture);
    TestEqual(TEXT("Commit clears previous type-specific source settings"),
        Layer.SameMeshSource.InnerSlotPriority.Num(),
        0);
    TestEqual(TEXT("Commit preserves reveal strokes"), Layer.RevealColorPaintStrokes.Num(), 1);
    TestEqual(TEXT("Commit preserves alpha strokes"), Layer.EditableStrokes.Num(), 1);
    TestEqual(TEXT("Commit preserves the last baked result for stale inspection"),
        Layer.BakedMaps.Num(),
        1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyFinalBaselineBakeSourceTest,
    "DWC.Editor.Transparency.Bake.RejectsFinalBakedBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyFinalBaselineBakeSourceTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 2;

    TSharedRef<FDWCTransparencySourcePayload> FinalBaseline =
        MakeShared<FDWCTransparencySourcePayload>();
    FinalBaseline->LayerGuid = Layer.LayerGuid;
    FinalBaseline->MaterialSlotIndex = 2;
    FinalBaseline->bIsFinalBakedBaseline = true;

    FDWCTransparencyEditedMapBakeSnapshot Snapshot;
    FString Error;
    const bool bBuilt = FDWCTransparencyEditedMapBaker::BuildSnapshot(
        *Asset,
        Layer,
        FinalBaseline,
        Snapshot,
        Error);

    TestFalse(TEXT("A final baked map is never accepted as a canonical bake source"), bBuilt);
    TestTrue(TEXT("The failure explains that canonical working data must be rebuilt"),
        Error.Contains(TEXT("canonical working map")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyRevealNormalSettingsContractTest,
    "DWC.Editor.Transparency.RevealNormal.SettingsContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyRevealNormalSettingsContractTest::RunTest(const FString&)
{
    FWetClothingTransparencyLayerData Layer;
    Layer.SourceType = EDWCTransparencySourceType::SameMeshMaterialSlots;
    TestTrue(TEXT("Reveal Normal is enabled by default for raycast layers"),
        Layer.bEnableRevealNormal);
    TestEqual(TEXT("Reveal Normal strength defaults to one"),
        Layer.RevealNormalStrength, 1.0f);

    const FString RevealBefore = FDWCTransparencySignatureService::BuildRevealSignature(
        TEXT("Source"), Layer, 0.35f);
    const FString AlphaBefore =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    Layer.bEnableRevealNormal = false;
    Layer.RevealNormalStrength = 3.5f;
    TestEqual(TEXT("Runtime Reveal settings do not stale Stage 3 color data"),
        FDWCTransparencySignatureService::BuildRevealSignature(TEXT("Source"), Layer, 0.35f),
        RevealBefore);
    TestEqual(TEXT("Runtime Reveal settings do not stale Stage 4 alpha data"),
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer),
        AlphaBefore);

    FDWCEditorSessionState State;
    FDWCInitializeTransparencyPreviewSettingsAction Action;
    Action.bForce = true;
    Action.Settings.RevealNormalStrength = 8.0f;
    Action.Settings.bShowRevealNormal = false;
    Action.Settings.RevealNormalSource = EDWCTransparencyRevealNormalPreviewSource::Baked;
    FDWCEditorSessionReducer::Reduce(State, Action);
    TestEqual(TEXT("Session clamps Reveal Normal strength to the material range"),
        State.Transparency.PreviewSettings.RevealNormalStrength, 4.0f);
    TestFalse(TEXT("Session owns the preview-only Reveal Normal visibility"),
        State.Transparency.PreviewSettings.bShowRevealNormal);
    TestEqual(TEXT("Session owns the requested Reveal Normal preview source"),
        State.Transparency.PreviewSettings.RevealNormalSource,
        EDWCTransparencyRevealNormalPreviewSource::Baked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyBlueprintHierarchyReadinessTest,
    "DWC.Editor.Transparency.Type2.BlueprintHierarchyReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyBlueprintHierarchyReadinessTest::RunTest(const FString&)
{
    USkeletalMesh* TargetMesh = NewObject<USkeletalMesh>(GetTransientPackage());
    USkeletalMesh* SourceMesh = NewObject<USkeletalMesh>(GetTransientPackage());

    FWetClothingTransparencyLayerData Layer;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.SourceType = EDWCTransparencySourceType::OtherSkeletalMeshComponents;
    Layer.BlueprintSource.BlueprintClass = AActor::StaticClass();

    FDWCTransparencyBlueprintHierarchySnapshot Snapshot;
    Snapshot.LayerGuid = Layer.LayerGuid;
    Snapshot.BlueprintClassPath = Layer.BlueprintSource.BlueprintClass.ToSoftObjectPath();
    Snapshot.State = EDWCTransparencyBlueprintHierarchyState::Loading;

    FDWCTransparencyType2Readiness Readiness =
        FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
            TargetMesh, TargetMesh, Layer, Snapshot);
    TestFalse(TEXT("Type 2 stays disabled while the hierarchy is loading"), Readiness.bReady);
    TestTrue(TEXT("Loading state has an actionable reason"),
        Readiness.DisabledReason.Contains(TEXT("Loading")));

    Snapshot.State = EDWCTransparencyBlueprintHierarchyState::Ready;
    const FName TargetComponentName(TEXT("TargetMesh"));
    const FName SourceComponentName(TEXT("InnerMesh"));
    FDWCTransparencyBlueprintMeshComponent& Target =
        Snapshot.Hierarchy.MeshComponents.AddDefaulted_GetRef();
    Target.ComponentName = TargetComponentName;
    Target.SkeletalMesh = TargetMesh;
    FDWCTransparencyBlueprintMeshComponent& Source =
        Snapshot.Hierarchy.MeshComponents.AddDefaulted_GetRef();
    Source.ComponentName = SourceComponentName;
    Source.SkeletalMesh = SourceMesh;

    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, Snapshot);
    TestFalse(TEXT("A ready hierarchy still requires an explicit target binding"), Readiness.bReady);
    TestFalse(TEXT("The target is unresolved before binding"), Readiness.bTargetResolved);

    Layer.BlueprintSource.TargetComponent.ComponentName = TargetComponentName;
    Layer.BlueprintSource.TargetComponent.ExpectedSkeletalMesh = TargetMesh;
    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, Snapshot);
    TestFalse(TEXT("A resolved target still requires a raycast source"), Readiness.bReady);
    TestTrue(TEXT("The target readiness is reported independently"), Readiness.bTargetResolved);

    FWetClothingTransparencyBlueprintComponentBinding& SourceBinding =
        Layer.BlueprintSource.SourcePriority.AddDefaulted_GetRef();
    SourceBinding.ComponentName = SourceComponentName;
    SourceBinding.ExpectedSkeletalMesh = SourceMesh;
    SourceBinding.Role = EDWCTransparencyBlueprintSourceRole::RevealSource;
    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, Snapshot);
    TestTrue(TEXT("A current hierarchy with target and reveal source is ready"), Readiness.bReady);
    TestTrue(TEXT("Ready state has no disabled reason"), Readiness.DisabledReason.IsEmpty());

    Layer.BlueprintSource.TargetComponent.ComponentName = TEXT("RemovedTarget");
    Layer.BlueprintSource.TargetComponent.ExpectedSkeletalMesh = TargetMesh;
    FWetClothingTransparencyBlueprintSource ReconciledSource = Layer.BlueprintSource;
    const FDWCTransparencyType2BindingReconcileResult ReconcileResult =
        FDWCTransparencyBlueprintHierarchySession::ReconcileBindings(
            TargetMesh,
            TargetMesh,
            Layer,
            Snapshot,
            ReconciledSource);
    TestTrue(TEXT("A stale target binding is repaired when the hierarchy has one target"),
        ReconcileResult.bChanged && ReconcileResult.bTargetResolved);
    TestEqual(TEXT("The repaired target uses the current Blueprint component"),
        ReconciledSource.TargetComponent.ComponentName,
        TargetComponentName);
    TestTrue(TEXT("A preselected raycast source survives target reconciliation"),
        ReconciledSource.SourcePriority.ContainsByPredicate(
            [SourceComponentName](
                const FWetClothingTransparencyBlueprintComponentBinding& Binding)
            {
                return Binding.ComponentName == SourceComponentName;
            }));
    Layer.BlueprintSource = MoveTemp(ReconciledSource);
    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, Snapshot);
    TestTrue(TEXT("Reconciled bindings enable Type 2 generation"), Readiness.bReady);

    FDWCTransparencyBlueprintHierarchySnapshot MissingTargetSnapshot = Snapshot;
    MissingTargetSnapshot.LayerGuid = Layer.LayerGuid;
    MissingTargetSnapshot.Hierarchy.MeshComponents.RemoveAll(
        [TargetMesh](const FDWCTransparencyBlueprintMeshComponent& Component)
        {
            return Component.SkeletalMesh == TargetMesh;
        });
    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, MissingTargetSnapshot);
    TestFalse(TEXT("Type 2 generation is blocked when the Blueprint target mesh is missing"),
        Readiness.bReady);
    TestTrue(TEXT("A missing Blueprint target mesh reports the actual blocking condition"),
        Readiness.DisabledReason.Contains(TEXT("does not contain")));
    FWetClothingTransparencyBlueprintSource MissingTargetSource = Layer.BlueprintSource;
    const FName PreservedTargetName = MissingTargetSource.TargetComponent.ComponentName;
    const FDWCTransparencyType2BindingReconcileResult MissingTargetReconcile =
        FDWCTransparencyBlueprintHierarchySession::ReconcileBindings(
            TargetMesh,
            TargetMesh,
            Layer,
            MissingTargetSnapshot,
            MissingTargetSource);
    TestFalse(TEXT("Inspecting an invalid Blueprint does not mutate authored target bindings"),
        MissingTargetReconcile.bChanged);
    TestEqual(TEXT("The authored target binding is preserved while the Blueprint is invalid"),
        MissingTargetSource.TargetComponent.ComponentName,
        PreservedTargetName);

    Snapshot.LayerGuid = FGuid::NewGuid();
    Readiness = FDWCTransparencyBlueprintHierarchySession::EvaluateReadiness(
        TargetMesh, TargetMesh, Layer, Snapshot);
    TestFalse(TEXT("A snapshot from another layer cannot enable generation"), Readiness.bReady);
    return true;
}

#endif
