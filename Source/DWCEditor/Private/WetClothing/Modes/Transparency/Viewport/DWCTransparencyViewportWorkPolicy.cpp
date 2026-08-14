// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyViewportWorkPolicy.h"

FDWCTransparencyViewportWorkDecision FDWCTransparencyViewportWorkPolicy::Resolve(
    const FDWCTransparencyViewportWorkState& State)
{
    FDWCTransparencyViewportWorkDecision Decision;
    if (State.bSuspended)
    {
        return Decision;
    }

    Decision.bPollMaterialCompilations = State.bMaterialCompilationPending;
    Decision.bRetryPreviewRebuild = !State.bPreviewRebuildInFlight &&
        (State.bPreviewRebuildRequired || State.bPreviewRetryDue);

    const bool bCanScheduleAlpha = State.bAlphaCommandsPending && !State.bAlphaJobPending;
    const bool bCanScheduleReveal = State.bRevealCommandsPending && !State.bRevealJobPending;
    const bool bCanFinalizeAuthoring = State.bAuthoringFinishPending &&
        !State.bAlphaCommandsPending && !State.bAlphaJobPending &&
        !State.bRevealCommandsPending && !State.bRevealJobPending;
    Decision.bProcessInteractivePaint =
        bCanScheduleAlpha || bCanScheduleReveal || bCanFinalizeAuthoring;
    Decision.bFlushUploads = State.bUploadPending;
    return Decision;
}
