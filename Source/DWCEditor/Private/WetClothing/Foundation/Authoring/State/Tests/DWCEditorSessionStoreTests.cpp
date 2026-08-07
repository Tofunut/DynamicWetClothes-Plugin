//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

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
    TestEqual(TEXT("Brush radius is normalized"), State.Wrinkle.Brush.BrushRadiusUV, 0.5f);
    TestEqual(TEXT("Brush strength is normalized"), State.Wrinkle.Brush.Strength, 4.0f);
    TestEqual(TEXT("Brush falloff is normalized"), State.Wrinkle.Brush.Falloff, 0.0f);
    TestEqual(TEXT("Preview wetness is normalized"), State.Wrinkle.Brush.PreviewWetness, 1.0f);
    TestEqual(TEXT("Display size is normalized"), State.Wrinkle.BrushSizeCm, 100.0f);

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

    const FGuid LayerGuid = FGuid::NewGuid();
    FDWCEditorSessionReducer::Reduce(State, FDWCSelectTransparencyLayerAction{LayerGuid});
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
    FDWCEditorSessionReducer::Reduce(
        State,
        FDWCSelectWrinkleElementAction{
            WrinkleElementGuid,
            EWetWrinkleElementType::ProceduralRidgeStroke,
            2});
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

#endif
