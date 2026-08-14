// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Coalesces deferred WCA editor refresh requests without tying their lifetime
 * to a Slate active timer. A full mode refresh always dominates a lightweight
 * request, while status refresh remains an independent channel.
 */
class FWCAEditorRefreshState final
{
public:
    /** Returns true only when the caller must register a new mode-refresh timer. */
    bool RequestModeRefresh(const bool bRebuildActiveModePreview)
    {
        bFullModeRefreshRequested |= bRebuildActiveModePreview;
        if (bModeRefreshPending)
        {
            return false;
        }

        bModeRefreshPending = true;
        return true;
    }

    /** Consumes one coalesced mode refresh. */
    bool ConsumeModeRefresh(bool& bOutRebuildActiveModePreview)
    {
        if (!bModeRefreshPending)
        {
            bOutRebuildActiveModePreview = false;
            return false;
        }

        bOutRebuildActiveModePreview = bFullModeRefreshRequested;
        bModeRefreshPending = false;
        bFullModeRefreshRequested = false;
        return true;
    }

    void CancelModeRefresh()
    {
        bModeRefreshPending = false;
        bFullModeRefreshRequested = false;
    }

    /** Returns true only when the caller must register a new status timer. */
    bool RequestStatusRefresh()
    {
        if (bStatusRefreshPending)
        {
            return false;
        }

        bStatusRefreshPending = true;
        return true;
    }

    bool ConsumeStatusRefresh()
    {
        const bool bWasPending = bStatusRefreshPending;
        bStatusRefreshPending = false;
        return bWasPending;
    }

    void CancelStatusRefresh()
    {
        bStatusRefreshPending = false;
    }

    bool IsModeRefreshPending() const { return bModeRefreshPending; }
    bool IsStatusRefreshPending() const { return bStatusRefreshPending; }

private:
    bool bModeRefreshPending = false;
    bool bFullModeRefreshRequested = false;
    bool bStatusRefreshPending = false;
};
