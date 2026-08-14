// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyViewportWorkPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyViewportIdleWorkPolicyTest,
    "DWC.Editor.Transparency.Viewport.IdleWorkPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyViewportIdleWorkPolicyTest::RunTest(const FString& Parameters)
{
    const FDWCTransparencyViewportWorkDecision IdleDecision =
        FDWCTransparencyViewportWorkPolicy::Resolve({});
    TestFalse(TEXT("An idle viewport does not poll editor subsystems"), IdleDecision.HasWork());

    FDWCTransparencyViewportWorkState Pending;
    Pending.bMaterialCompilationPending = true;
    Pending.bPreviewRebuildRequired = true;
    Pending.bAlphaCommandsPending = true;
    Pending.bUploadPending = true;
    const FDWCTransparencyViewportWorkDecision PendingDecision =
        FDWCTransparencyViewportWorkPolicy::Resolve(Pending);
    TestTrue(TEXT("Pending material compilation is polled"), PendingDecision.bPollMaterialCompilations);
    TestTrue(TEXT("Required preview rebuild is retried"), PendingDecision.bRetryPreviewRebuild);
    TestTrue(TEXT("Queued paint commands are scheduled"), PendingDecision.bProcessInteractivePaint);
    TestTrue(TEXT("Pending uploads are flushed"), PendingDecision.bFlushUploads);

    Pending.bSuspended = true;
    TestFalse(
        TEXT("A suspended viewport performs no pending work"),
        FDWCTransparencyViewportWorkPolicy::Resolve(Pending).HasWork());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyViewportInFlightWorkPolicyTest,
    "DWC.Editor.Transparency.Viewport.InFlightWorkPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyViewportInFlightWorkPolicyTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyViewportWorkState State;
    State.bPreviewRebuildRequired = true;
    State.bPreviewRebuildInFlight = true;
    State.bAlphaCommandsPending = true;
    State.bAlphaJobPending = true;
    State.bAuthoringFinishPending = true;

    FDWCTransparencyViewportWorkDecision Decision =
        FDWCTransparencyViewportWorkPolicy::Resolve(State);
    TestFalse(TEXT("An in-flight preview build is not submitted twice"), Decision.bRetryPreviewRebuild);
    TestFalse(TEXT("An in-flight paint job is not polled every frame"), Decision.bProcessInteractivePaint);

    State.bPreviewRebuildInFlight = false;
    State.bAlphaCommandsPending = false;
    State.bAlphaJobPending = false;
    Decision = FDWCTransparencyViewportWorkPolicy::Resolve(State);
    TestTrue(TEXT("The required preview build resumes after completion"), Decision.bRetryPreviewRebuild);
    TestTrue(TEXT("Finished authoring is finalized once no paint work remains"), Decision.bProcessInteractivePaint);
    return true;
}
