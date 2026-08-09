//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionReducer.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowPolicy.h"

namespace
{
    bool HasEffect(
        const EDWCEditorSessionEffect Effects,
        const EDWCEditorSessionEffect Expected)
    {
        return EnumHasAnyFlags(Effects, Expected);
    }
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

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext RevealContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::RevealEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::Final,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            false,
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
    TestFalse(TEXT("Disabled Reveal Paint keeps the reveal target but disables writes"),
        RevealContext.bEnableRevealColorPainting);

    const DWCTransparencyWorkflow::FDWCTransparencyPreviewContext FinalContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            EDWCTransparencyEditorStage::FinalEditing,
            EDWCTransparencySourceType::ManualColorOrTexture,
            EDWCTransparencyVisualizationMode::AutoAlpha,
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            true,
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
    FDWCTransparencyWorkflowRevealLifecycleTest,
    "DWC.Editor.Transparency.Workflow.RevealPaintLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkflowRevealLifecycleTest::RunTest(const FString& Parameters)
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
    FDWCEditorSessionReducer::Reduce(State, RevealContextAction);

    FDWCSetTransparencyPaintAction EnableRevealAction;
    EnableRevealAction.bRevealPaint = true;
    EnableRevealAction.Paint = State.Transparency.RevealPaint;
    EnableRevealAction.Paint.bEnabled = true;
    const EDWCEditorSessionEffect EnableEffects =
        FDWCEditorSessionReducer::Reduce(State, EnableRevealAction);
    TestTrue(TEXT("Enabling Reveal Paint updates preview parameters"),
        HasEffect(EnableEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestEqual(TEXT("Stage 3 keeps the reveal paint target"),
        State.Transparency.EditContext.PaintTarget,
        EDWCTransparencyPaintTarget::RevealColor);

    FDWCSetTransparencyPaintAction DisableRevealAction = EnableRevealAction;
    DisableRevealAction.Paint = State.Transparency.RevealPaint;
    DisableRevealAction.Paint.bEnabled = false;
    const EDWCEditorSessionEffect DisableEffects =
        FDWCEditorSessionReducer::Reduce(State, DisableRevealAction);
    TestTrue(TEXT("Disabling Reveal Paint updates preview parameters"),
        HasEffect(DisableEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestFalse(TEXT("Disabling Reveal Paint changes only the write gate"),
        State.Transparency.RevealPaint.bEnabled);
    TestEqual(TEXT("Disabling Reveal Paint does not clear the reveal target"),
        State.Transparency.EditContext.PaintTarget,
        EDWCTransparencyPaintTarget::RevealColor);

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

#endif
