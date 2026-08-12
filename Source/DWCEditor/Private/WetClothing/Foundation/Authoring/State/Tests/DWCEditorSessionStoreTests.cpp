//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleBrushConstants.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSessionReducerContractTest,
    "DWC.Editor.Authoring.Session.ReducerContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSessionReducerContractTest::RunTest(const FString& Parameters)
{
    FDWCEditorSessionState State;

    FDWCSetWrinkleBrushAction BrushAction;
    BrushAction.Brush.BrushRadiusUV = 9.0f;
    BrushAction.Brush.Strength = 20.0f;
    BrushAction.Brush.Falloff = -2.0f;
    BrushAction.Brush.PreviewWetness = 3.0f;
    BrushAction.BrushSizeCm = 400.0f;
    BrushAction.BrushSizeUV = 9.0f;
    const EDWCEditorSessionEffect BrushEffects =
        FDWCEditorSessionReducer::Reduce(State, BrushAction);

    TestTrue(
        TEXT("A brush action requests control synchronization"),
        EnumHasAnyFlags(BrushEffects, EDWCEditorSessionEffect::SyncControls));
    TestEqual(
        TEXT("Brush radius is normalized"),
        State.Wrinkle.Brush.BrushRadiusUV,
        WetWrinkleBrushConstants::MaxRadiusUV);
    TestEqual(TEXT("Brush strength is normalized"), State.Wrinkle.Brush.Strength, 4.0f);
    TestEqual(TEXT("Brush falloff is normalized"), State.Wrinkle.Brush.Falloff, 0.0f);
    TestEqual(TEXT("Preview wetness is normalized"), State.Wrinkle.Brush.PreviewWetness, 1.0f);
    TestEqual(
        TEXT("Display size is normalized"),
        State.Wrinkle.BrushSizeCm,
        WetWrinkleBrushConstants::MaxSizeCm);
    TestEqual(
        TEXT("Display UV size is normalized"),
        State.Wrinkle.BrushSizeUV,
        WetWrinkleBrushConstants::MaxRadiusUV);

    FDWCInitializeTransparencyPreviewSettingsAction InitializeTransparency;
    InitializeTransparency.Settings.TransparencyStrength = 0.8f;
    InitializeTransparency.Settings.WrinkleSuppressionStrength = 1.2f;
    InitializeTransparency.Settings.WrinkleMaskThreshold = 0.2f;
    InitializeTransparency.Settings.WrinkleMaskSoftness = 0.04f;
    const EDWCEditorSessionEffect InitializeEffects =
        FDWCEditorSessionReducer::Reduce(State, InitializeTransparency);
    TestTrue(
        TEXT("Transparency preview settings initialize the live controls"),
        EnumHasAnyFlags(InitializeEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestTrue(
        TEXT("Transparency preview settings remember that asset defaults were imported"),
        State.Transparency.bPreviewSettingsInitialized);

    FDWCInitializeTransparencyPreviewSettingsAction IgnoreRepeatedInitialize;
    IgnoreRepeatedInitialize.Settings.TransparencyStrength = 4.0f;
    TestEqual(
        TEXT("Repeated asset initialization does not overwrite the live session"),
        FDWCEditorSessionReducer::Reduce(State, IgnoreRepeatedInitialize),
        EDWCEditorSessionEffect::None);
    TestEqual(
        TEXT("The initialized live transparency strength is preserved"),
        State.Transparency.PreviewSettings.TransparencyStrength,
        0.8f);

    FDWCSetTransparencyPreviewAction PreviewAction;
    PreviewAction.Settings = State.Transparency.PreviewSettings;
    PreviewAction.Settings.TransparencyStrength = -1.0f;
    PreviewAction.Settings.WrinkleSuppressionStrength = 9.0f;
    PreviewAction.Settings.WrinkleMaskThreshold = 2.0f;
    PreviewAction.Settings.WrinkleMaskSoftness = -1.0f;
    const EDWCEditorSessionEffect PreviewEffects =
        FDWCEditorSessionReducer::Reduce(State, PreviewAction);
    TestTrue(
        TEXT("Live transparency settings update preview parameters"),
        EnumHasAnyFlags(PreviewEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    TestEqual(
        TEXT("Transparency strength is normalized"),
        State.Transparency.PreviewSettings.TransparencyStrength,
        0.0f);
    TestEqual(
        TEXT("Suppression strength is normalized"),
        State.Transparency.PreviewSettings.WrinkleSuppressionStrength,
        5.0f);
    TestEqual(
        TEXT("Wrinkle threshold is normalized"),
        State.Transparency.PreviewSettings.WrinkleMaskThreshold,
        1.0f);
    TestEqual(
        TEXT("Wrinkle softness is normalized"),
        State.Transparency.PreviewSettings.WrinkleMaskSoftness,
        0.0f);

    FDWCSetTransparencyPreviewAction RevealVisualizationAction;
    RevealVisualizationAction.Stage = EDWCTransparencyEditorStage::RevealEditing;
    RevealVisualizationAction.PreviewMode = State.Transparency.PreviewMode;
    RevealVisualizationAction.VisualizationMode = EDWCTransparencyVisualizationMode::CorrectionDifference;
    RevealVisualizationAction.WetnessPreviewPercent = State.Transparency.WetnessPreviewPercent;
    RevealVisualizationAction.Settings = State.Transparency.PreviewSettings;
    RevealVisualizationAction.bShowSavedWrinkle = State.Transparency.bShowSavedWrinkle;
    FDWCEditorSessionReducer::Reduce(State, RevealVisualizationAction);
    TestEqual(
        TEXT("Stage 3 owns its correction visualization selection"),
        State.Transparency.RevealVisualizationMode,
        EDWCTransparencyVisualizationMode::CorrectionDifference);

    FDWCSetTransparencyPreviewAction FinalVisualizationAction = RevealVisualizationAction;
    FinalVisualizationAction.Stage = EDWCTransparencyEditorStage::FinalEditing;
    FinalVisualizationAction.VisualizationMode = EDWCTransparencyVisualizationMode::AutoAlpha;
    FDWCEditorSessionReducer::Reduce(State, FinalVisualizationAction);
    TestEqual(
        TEXT("Stage 4 owns its alpha visualization selection"),
        State.Transparency.FinalVisualizationMode,
        EDWCTransparencyVisualizationMode::AutoAlpha);
    TestEqual(
        TEXT("Stage 4 does not overwrite the Stage 3 visualization selection"),
        State.Transparency.RevealVisualizationMode,
        EDWCTransparencyVisualizationMode::CorrectionDifference);

    const FGuid LayerGuid = FGuid::NewGuid();
    FDWCEditorSessionReducer::Reduce(State, FDWCSelectTransparencyLayerAction{LayerGuid, 6});
    FDWCReconcileAuthoringAction Reconcile;
    Reconcile.AuthoringRevision = 7;
    Reconcile.Domain = EDWCEditorAuthoringDomain::Transparency;
    Reconcile.Impact = EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::Details;
    const EDWCEditorSessionEffect ReconcileEffects =
        FDWCEditorSessionReducer::Reduce(State, Reconcile);

    TestFalse(
        TEXT("A removed transparency layer is no longer selected"),
        State.Transparency.SelectedLayerGuid.IsValid());
    TestEqual(
        TEXT("Removing a layer preserves the inspected material slot"),
        State.Transparency.SelectedMaterialSlotIndex,
        6);
    TestEqual(TEXT("The authoring revision is reconciled"), State.AuthoringRevision, uint64(7));
    TestEqual(TEXT("The transparency revision is reconciled"), State.TransparencyAuthoringRevision, uint64(7));
    TestEqual(TEXT("An unrelated domain revision is unchanged"), State.WrinkleAuthoringRevision, uint64(0));
    TestTrue(
        TEXT("Authoring preview impact becomes a preview rebuild effect"),
        EnumHasAnyFlags(ReconcileEffects, EDWCEditorSessionEffect::RebuildPreviewContent));
    TestTrue(
        TEXT("Authoring details impact becomes a details effect"),
        EnumHasAnyFlags(ReconcileEffects, EDWCEditorSessionEffect::RefreshDetails));
    TestEqual(
        TEXT("Authoring reconciliation does not restore stale transparency preview settings"),
        State.Transparency.PreviewSettings.WrinkleSuppressionStrength,
        5.0f);

    const FGuid WrinkleElementGuid = FGuid::NewGuid();
    const EDWCEditorSessionEffect SelectionEffects = FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSelectWrinkleElementAction{
            WrinkleElementGuid,
            EWetWrinkleElementType::ProceduralRidgeStroke,
            2});
    TestTrue(
        TEXT("Selecting a wrinkle synchronizes selection state"),
        EnumHasAnyFlags(SelectionEffects, EDWCEditorSessionEffect::SyncSelection));
    TestFalse(
        TEXT("Selecting a wrinkle does not resend unchanged brush preview settings"),
        EnumHasAnyFlags(SelectionEffects, EDWCEditorSessionEffect::UpdatePreviewParameters));
    FDWCReconcileAuthoringAction PreserveWrinkleSelection;
    PreserveWrinkleSelection.AuthoringRevision = 8;
    PreserveWrinkleSelection.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    PreserveWrinkleSelection.Index.WrinkleElementGuids.Add(WrinkleElementGuid);
    FDWCEditorSessionReducer::Reduce(State, PreserveWrinkleSelection);
    TestEqual(
        TEXT("A wrinkle element that remains in the authoring index stays selected"),
        State.Wrinkle.SelectedElementGuid,
        WrinkleElementGuid);

    FDWCReconcileAuthoringAction RemoveWrinkleSelection;
    RemoveWrinkleSelection.AuthoringRevision = 9;
    RemoveWrinkleSelection.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    const EDWCEditorSessionEffect RemovedSelectionEffects =
        FDWCEditorSessionReducer::Reduce(State, RemoveWrinkleSelection);
    TestFalse(
        TEXT("A removed wrinkle element is no longer selected"),
        State.Wrinkle.SelectedElementGuid.IsValid());
    TestEqual(
        TEXT("Removing the selected wrinkle also clears its ridge point"),
        State.Wrinkle.SelectedRidgePointIndex,
        INDEX_NONE);
    TestTrue(
        TEXT("Removing the selected wrinkle synchronizes selection controls"),
        EnumHasAnyFlags(RemovedSelectionEffects, EDWCEditorSessionEffect::SyncSelection));

    FDWCReconcileAuthoringAction IncrementalReconcile;
    IncrementalReconcile.AuthoringRevision = 10;
    IncrementalReconcile.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    IncrementalReconcile.Impact = EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::PreviewIncremental;
    const EDWCEditorSessionEffect IncrementalEffects =
        FDWCEditorSessionReducer::Reduce(State, IncrementalReconcile);
    TestTrue(
        TEXT("An appended wrinkle element refreshes the list"),
        EnumHasAnyFlags(IncrementalEffects, EDWCEditorSessionEffect::RefreshElementList));
    TestTrue(
        TEXT("An appended wrinkle element refreshes UV markers"),
        EnumHasAnyFlags(IncrementalEffects, EDWCEditorSessionEffect::RefreshUVView));
    TestFalse(
        TEXT("An incremental wrinkle append does not request a full preview rebuild"),
        EnumHasAnyFlags(IncrementalEffects, EDWCEditorSessionEffect::RebuildPreviewContent));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSessionStoreDispatchTest,
    "DWC.Editor.Authoring.Session.StoreDispatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSessionStoreDispatchTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorSessionStore> Store = MakeShared<FDWCEditorSessionStore>();
    int32 NotificationCount = 0;
    Store->OnChanged().AddLambda(
        [&Store, &NotificationCount](
            const FDWCEditorSessionState& State,
            EDWCEditorSessionEffect,
            uint64 Revision)
        {
            ++NotificationCount;
            if (Revision == 1)
            {
                Store->Dispatch(FDWCSetWrinkleCrossPreviewAction{false});
            }
        });

    Store->Dispatch(FDWCActivateEditorModeAction{EWCAEditorMode::WrinkleEdit});
    TestEqual(TEXT("Nested dispatch is drained after the current notification"), NotificationCount, 2);
    TestEqual(TEXT("Each changed action advances one session revision"), Store->GetRevision(), uint64(2));
    TestEqual(
        TEXT("The first action changes the active mode"),
        Store->GetState().ActiveMode,
        EWCAEditorMode::WrinkleEdit);
    TestFalse(
        TEXT("The queued action updates cross-preview state"),
        Store->GetState().Wrinkle.bShowBakedTransparency);

    Store->Dispatch(FDWCActivateEditorModeAction{EWCAEditorMode::WrinkleEdit});
    TestEqual(TEXT("A no-op action does not notify"), NotificationCount, 2);
    TestEqual(TEXT("A no-op action does not advance revision"), Store->GetRevision(), uint64(2));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWrinkleEditContextAtomicityTest,
    "DWC.Editor.Authoring.Session.WrinkleEditContextAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWrinkleEditContextAtomicityTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorSessionStore> Store = MakeShared<FDWCEditorSessionStore>();

    FDWCSetWrinkleBrushAction InitialBrush;
    InitialBrush.Brush.MaterialSlotIndex = 14;
    InitialBrush.Brush.UVChannelIndex = 2;
    InitialBrush.Effects = EDWCEditorSessionEffect::None;
    Store->Dispatch(InitialBrush);

    const FGuid SelectedPatchGuid = FGuid::NewGuid();
    Store->Dispatch(FDWCSelectWrinkleElementAction{
        SelectedPatchGuid,
        EWetWrinkleElementType::Patch,
        INDEX_NONE});

    int32 NotificationCount = 0;
    bool bObservedMixedState = false;
    EDWCEditorSessionEffect ObservedEffects = EDWCEditorSessionEffect::None;
    Store->OnChanged().AddLambda(
        [&NotificationCount, &bObservedMixedState, &ObservedEffects](
            const FDWCEditorSessionState& State,
            const EDWCEditorSessionEffect Effects,
            uint64)
        {
            ++NotificationCount;
            ObservedEffects = Effects;
            bObservedMixedState |=
                State.Wrinkle.Brush.MaterialSlotIndex == 12 &&
                State.Wrinkle.SelectedElementGuid.IsValid();
        });

    FDWCSetWrinkleEditContextAction SelectNewSlot;
    SelectNewSlot.MaterialSlotIndex = 12;
    SelectNewSlot.UVChannelIndex = 3;
    SelectNewSlot.bClearElementSelection = true;
    Store->Dispatch(SelectNewSlot);

    TestEqual(
        TEXT("A slot transition emits one canonical session notification"),
        NotificationCount,
        1);
    TestFalse(
        TEXT("Observers never receive the new slot with the previous slot selection"),
        bObservedMixedState);
    TestEqual(
        TEXT("The new material slot is committed"),
        Store->GetState().Wrinkle.Brush.MaterialSlotIndex,
        12);
    TestEqual(
        TEXT("The new Data UV is committed with the material slot"),
        Store->GetState().Wrinkle.Brush.UVChannelIndex,
        3);
    TestFalse(
        TEXT("A hidden element selection is cleared in the same transition"),
        Store->GetState().Wrinkle.SelectedElementGuid.IsValid());
    TestTrue(
        TEXT("The transition rebuilds viewport hit topology"),
        EnumHasAnyFlags(ObservedEffects, EDWCEditorSessionEffect::RebuildHitTopology));
    TestTrue(
        TEXT("The transition synchronizes element selection"),
        EnumHasAnyFlags(ObservedEffects, EDWCEditorSessionEffect::SyncSelection));
    TestTrue(
        TEXT("The transition refreshes the slot-filtered element list"),
        EnumHasAnyFlags(ObservedEffects, EDWCEditorSessionEffect::RefreshElementList));
    return true;
}

#endif
