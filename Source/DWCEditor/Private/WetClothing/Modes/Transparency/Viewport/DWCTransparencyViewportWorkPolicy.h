// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Cheap game-thread state sampled by the viewport before polling any subsystem. */
struct FDWCTransparencyViewportWorkState
{
    bool bSuspended = false;
    bool bMaterialCompilationPending = false;
    bool bPreviewRebuildInFlight = false;
    bool bPreviewRebuildRequired = false;
    bool bPreviewRetryDue = false;
    bool bAlphaCommandsPending = false;
    bool bAlphaJobPending = false;
    bool bRevealCommandsPending = false;
    bool bRevealJobPending = false;
    bool bAuthoringFinishPending = false;
    bool bUploadPending = false;
};

struct FDWCTransparencyViewportWorkDecision
{
    bool bPollMaterialCompilations = false;
    bool bRetryPreviewRebuild = false;
    bool bProcessInteractivePaint = false;
    bool bFlushUploads = false;

    bool HasWork() const
    {
        return bPollMaterialCompilations || bRetryPreviewRebuild ||
            bProcessInteractivePaint || bFlushUploads;
    }
};

/** Resolves one frame of pending work without touching heavyweight editor state. */
class FDWCTransparencyViewportWorkPolicy final
{
public:
    static FDWCTransparencyViewportWorkDecision Resolve(
        const FDWCTransparencyViewportWorkState& State);
};
