// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorHostLifecycle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostLifecycleBlockerMatrixTest,
    "DWC.Editor.Lifecycle.BlockerMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostLifecycleBlockerMatrixTest::RunTest(const FString&)
{
    FDWCEditorHostLifecycleReducer Lifecycle(
        EDWCEditorHostLifecycleBlocker::HostUnavailable);
    TestFalse(TEXT("An unavailable host starts suspended."), Lifecycle.CanRunInteractivePreview());
    TestEqual(TEXT("The initial interactive generation is stable."),
              Lifecycle.GetSnapshot().InteractiveGeneration, uint64{ 1 });

    FDWCEditorHostLifecycleTransition Transition = Lifecycle.SetBlocker(
        EDWCEditorHostLifecycleBlocker::HostUnavailable, false);
    TestTrue(TEXT("Removing the final blocker activates preview."), Transition.bBecameInteractive);
    TestEqual(TEXT("Activation advances the interactive generation."),
              Lifecycle.GetSnapshot().InteractiveGeneration, uint64{ 2 });

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::TabBackground, true);
    TestTrue(TEXT("A background tab suspends preview."), Transition.bBecameSuspended);
    const uint64 SuspendedGeneration = Lifecycle.GetSnapshot().InteractiveGeneration;

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::PIE, true);
    TestTrue(TEXT("Adding a second blocker changes the blocker set."), Transition.bBlockersChanged);
    TestFalse(TEXT("Adding a blocker while suspended does not suspend twice."), Transition.bBecameSuspended);
    TestEqual(TEXT("Blocker churn while suspended does not advance the interactive generation."),
              Lifecycle.GetSnapshot().InteractiveGeneration, SuspendedGeneration);

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::TabBackground, false);
    TestFalse(TEXT("Removing one of two blockers does not activate preview."), Transition.bBecameInteractive);
    TestTrue(TEXT("PIE still blocks preview."), Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::PIE));

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::PIE, false);
    TestTrue(TEXT("Removing the final blocker activates preview once."), Transition.bBecameInteractive);
    TestTrue(TEXT("Preview is interactive after all blockers are gone."), Lifecycle.CanRunInteractivePreview());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostLifecycleNoOpAndClosingTest,
    "DWC.Editor.Lifecycle.NoOpAndClosing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostLifecycleNoOpAndClosingTest::RunTest(const FString&)
{
    FDWCEditorHostLifecycleReducer Lifecycle;
    const uint64                   InitialRevision = Lifecycle.GetSnapshot().StateRevision;
    const uint64                   InitialGeneration = Lifecycle.GetSnapshot().InteractiveGeneration;

    FDWCEditorHostLifecycleTransition Transition = Lifecycle.SetBlocker(
        EDWCEditorHostLifecycleBlocker::PIE, false);
    TestFalse(TEXT("Repeating an existing blocker value is a no-op."), Transition.bBlockersChanged);
    TestEqual(TEXT("A no-op does not advance revision."),
              Lifecycle.GetSnapshot().StateRevision, InitialRevision);
    TestEqual(TEXT("A no-op does not advance generation."),
              Lifecycle.GetSnapshot().InteractiveGeneration, InitialGeneration);

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing, true);
    TestTrue(TEXT("Closing is reported as a terminal transition."), Transition.bBecameClosing);
    TestTrue(TEXT("Closing suspends an interactive editor."), Transition.bBecameSuspended);
    TestEqual(TEXT("Closing selects the closing run state."),
              Lifecycle.GetSnapshot().RunState, EDWCEditorHostRunState::Closing);

    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing, false);
    TestFalse(TEXT("A late host event cannot clear the closing blocker."), Transition.bBlockersChanged);
    TestTrue(TEXT("The closing blocker remains set."),
             Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing));
    TestFalse(TEXT("A closing editor cannot resume preview."), Lifecycle.CanRunInteractivePreview());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostLifecycleVisibilitySnapshotTest,
    "DWC.Editor.Lifecycle.VisibilitySnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostLifecycleVisibilitySnapshotTest::RunTest(const FString&)
{
    FDWCEditorHostLifecycleReducer Lifecycle(
        EDWCEditorHostLifecycleBlocker::HostUnavailable |
        EDWCEditorHostLifecycleBlocker::PIE);

    FDWCEditorHostVisibilitySnapshot Visible;
    Visible.bHostAvailable = true;
    Visible.bTabForeground = true;
    Visible.bWindowVisible = true;
    Visible.bWindowActive = true;
    Visible.bApplicationActive = true;

    FDWCEditorHostLifecycleTransition Transition = Lifecycle.SetVisibilitySnapshot(Visible);
    TestTrue(TEXT("Visibility replacement clears the unavailable blocker."), Transition.bBlockersChanged);
    TestFalse(TEXT("Visibility replacement preserves PIE and therefore does not resume."), Transition.bBecameInteractive);
    TestTrue(TEXT("PIE is preserved across visibility replacement."),
             Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::PIE));

    FDWCEditorHostVisibilitySnapshot Background = Visible;
    Background.bTabForeground = false;
    Background.bWindowActive = false;
    Transition = Lifecycle.SetVisibilitySnapshot(Background);
    TestTrue(TEXT("Background state adds the tab blocker."),
             Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::TabBackground));
    TestTrue(TEXT("Inactive host window is represented independently."),
             Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::WindowInactive));
    TestTrue(TEXT("PIE remains preserved after another visibility update."),
             Lifecycle.HasBlocker(EDWCEditorHostLifecycleBlocker::PIE));

    const uint64 Revision = Lifecycle.GetSnapshot().StateRevision;
    Transition = Lifecycle.SetVisibilitySnapshot(Background);
    TestFalse(TEXT("An identical visibility snapshot is a no-op."), Transition.bBlockersChanged);
    TestEqual(TEXT("An identical visibility snapshot does not advance revision."),
              Lifecycle.GetSnapshot().StateRevision, Revision);

    Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::PIE, false);
    Transition = Lifecycle.SetVisibilitySnapshot(Visible);
    TestTrue(TEXT("Replacing all remaining visibility blockers resumes once."), Transition.bBecameInteractive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostVisibilityPolicyMatrixTest,
    "DWC.Editor.Lifecycle.Visibility.BlockerPolicyMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostVisibilityPolicyMatrixTest::RunTest(const FString&)
{
    auto MakeVisible = []
    {
        FDWCEditorHostVisibilitySnapshot Snapshot;
        Snapshot.bHostAvailable = true;
        Snapshot.bTabForeground = true;
        Snapshot.bWindowVisible = true;
        Snapshot.bWindowActive = true;
        Snapshot.bApplicationActive = true;
        return Snapshot;
    };

    struct FCase
    {
        const TCHAR* Name;
        FDWCEditorHostVisibilitySnapshot Snapshot;
        EDWCEditorHostLifecycleBlocker ExpectedBlockers;
        bool bExpectedImmediateRelease;
    };

    TArray<FCase> Cases;
    Cases.Add({TEXT("Foreground"), MakeVisible(), EDWCEditorHostLifecycleBlocker::None, false});

    FDWCEditorHostVisibilitySnapshot DockedBackground = MakeVisible();
    DockedBackground.bTabForeground = false;
    Cases.Add({TEXT("Docked background tab"), DockedBackground,
        EDWCEditorHostLifecycleBlocker::TabBackground, false});

    FDWCEditorHostVisibilitySnapshot StandaloneInactive = MakeVisible();
    StandaloneInactive.bWindowActive = false;
    Cases.Add({TEXT("Inactive standalone window"), StandaloneInactive,
        EDWCEditorHostLifecycleBlocker::WindowInactive, false});

    FDWCEditorHostVisibilitySnapshot Hidden = MakeVisible();
    Hidden.bWindowVisible = false;
    Cases.Add({TEXT("Hidden window"), Hidden,
        EDWCEditorHostLifecycleBlocker::WindowHidden, true});

    FDWCEditorHostVisibilitySnapshot Minimized = MakeVisible();
    Minimized.bWindowMinimized = true;
    Minimized.bWindowActive = false;
    Cases.Add({TEXT("Minimized window"), Minimized,
        EDWCEditorHostLifecycleBlocker::WindowMinimized |
            EDWCEditorHostLifecycleBlocker::WindowInactive, true});

    FDWCEditorHostVisibilitySnapshot ApplicationInactive = MakeVisible();
    ApplicationInactive.bApplicationActive = false;
    Cases.Add({TEXT("Inactive application"), ApplicationInactive,
        EDWCEditorHostLifecycleBlocker::ApplicationInactive, true});

    FDWCEditorHostVisibilitySnapshot Unavailable;
    Cases.Add({TEXT("Unavailable host"), Unavailable,
        EDWCEditorHostLifecycleBlocker::HostUnavailable, true});

    for (const FCase& Case : Cases)
    {
        const EDWCEditorHostLifecycleBlocker Actual = ResolveHostVisibilityBlockers(Case.Snapshot);
        TestEqual(Case.Name, static_cast<uint16>(Actual),
            static_cast<uint16>(Case.ExpectedBlockers));
        TestEqual(*FString::Printf(TEXT("%s release policy"), Case.Name),
            RequiresImmediatePreviewResourceRelease(Actual), Case.bExpectedImmediateRelease);
    }

    TestTrue(TEXT("PIE is an immediate-release blocker"),
        RequiresImmediatePreviewResourceRelease(EDWCEditorHostLifecycleBlocker::PIE));
    TestTrue(TEXT("Exclusive build is an immediate-release blocker"),
        RequiresImmediatePreviewResourceRelease(EDWCEditorHostLifecycleBlocker::ExclusiveBuild));
    TestTrue(TEXT("Closing is an immediate-release blocker"),
        RequiresImmediatePreviewResourceRelease(EDWCEditorHostLifecycleBlocker::EditorClosing));
    TestFalse(TEXT("Tab and window inactivity remain deferred together"),
        RequiresImmediatePreviewResourceRelease(
            EDWCEditorHostLifecycleBlocker::TabBackground |
            EDWCEditorHostLifecycleBlocker::WindowInactive));
    return true;
}

#endif
