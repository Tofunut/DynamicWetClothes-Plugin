//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Preview/Recovery/DWCEditorPreviewRecovery.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewRecoveryBoundedRetryTest,
    "DWC.Editor.Preview.Recovery.BoundedRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewRecoveryBoundedRetryTest::RunTest(const FString& Parameters)
{
    FDWCEditorPreviewRecoveryPolicy Policy;
    Policy.WorkerRetryLimit = 3;
    FDWCEditorPreviewRecoveryController Recovery(Policy);
    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    for (int32 Attempt = 0; Attempt < 3; ++Attempt)
    {
        const double BeginTime = Attempt == 0 ? 0.0 : Recovery.GetNextRetryTimeSeconds();
        TestTrue(TEXT("A required rebuild can begin"), Recovery.TryBeginFullRebuild(BeginTime));
        const EDWCEditorPreviewRecoveryAction Action = Recovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::WorkerFailed,
            BeginTime);
        if (Attempt < 2)
        {
            TestEqual(TEXT("Failure remains retryable within budget"),
                Action, EDWCEditorPreviewRecoveryAction::RetryFullRebuild);
        }
    }
    TestTrue(TEXT("Repeated failure enters degraded state"), Recovery.IsDegraded());
    TestFalse(TEXT("Degraded recovery cannot submit another rebuild"), Recovery.TryBeginFullRebuild(100.0));
    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ContextChanged);
    TestTrue(TEXT("A new content generation exits degraded state"), Recovery.TryBeginFullRebuild(100.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewRecoveryStaleAndResumeTest,
    "DWC.Editor.Preview.Recovery.StaleAndResume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewRecoveryStaleAndResumeTest::RunTest(const FString& Parameters)
{
    FDWCEditorPreviewRecoveryController Recovery;
    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ContextChanged);
    TestTrue(TEXT("Full rebuild starts"), Recovery.TryBeginFullRebuild());
    TestEqual(TEXT("Stale result is dropped"),
        Recovery.HandleCommitResult(EDWCEditorPreviewCommitResult::StaleRequest, 0.0),
        EDWCEditorPreviewRecoveryAction::DropStale);
    TestFalse(TEXT("Stale result does not schedule a fallback"), Recovery.RequiresFullRebuild());

    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ResolutionChanged);
    Recovery.Suspend();
    TestTrue(TEXT("Controller is suspended"), Recovery.IsSuspended());
    Recovery.Resume(false);
    TestTrue(TEXT("Pending invalidation survives suspend/resume"), Recovery.RequiresFullRebuild());

    Recovery.Reset();
    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ContextChanged);
    TestTrue(TEXT("Older generation starts"), Recovery.TryBeginFullRebuild());
    Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    Recovery.RecordStaleResult();
    TestTrue(
        TEXT("Dropping an older result preserves a newer generation rebuild"),
        Recovery.RequiresFullRebuild());
    return true;
}

#endif
