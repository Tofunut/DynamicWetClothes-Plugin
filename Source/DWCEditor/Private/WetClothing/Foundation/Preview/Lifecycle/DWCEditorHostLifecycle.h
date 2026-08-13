// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Conditions that prevent a WCA editor instance from running interactive preview work. */
enum class EDWCEditorHostLifecycleBlocker : uint16
{
    None = 0,
    HostUnavailable = 1 << 0,
    TabBackground = 1 << 1,
    WindowHidden = 1 << 2,
    WindowMinimized = 1 << 3,
    WindowInactive = 1 << 4,
    ApplicationInactive = 1 << 5,
    PIE = 1 << 6,
    ExclusiveBuild = 1 << 7,
    EditorClosing = 1 << 8
};
ENUM_CLASS_FLAGS(EDWCEditorHostLifecycleBlocker);

/** Slate host facts sampled by the adapter and consumed atomically by the lifecycle reducer. */
struct FDWCEditorHostVisibilitySnapshot
{
    bool bHostAvailable = false;
    bool bTabForeground = false;
    bool bWindowVisible = false;
    bool bWindowMinimized = false;
    bool bWindowActive = false;
    bool bApplicationActive = false;

    bool operator==(const FDWCEditorHostVisibilitySnapshot& Other) const = default;
};

enum class EDWCEditorHostRunState : uint8
{
    Suspended,
    Interactive,
    Closing
};

struct FDWCEditorHostLifecycleSnapshot
{
    EDWCEditorHostLifecycleBlocker Blockers = EDWCEditorHostLifecycleBlocker::None;
    EDWCEditorHostRunState         RunState = EDWCEditorHostRunState::Interactive;
    uint64                         StateRevision = 0;
    uint64                         InteractiveGeneration = 1;

    bool CanRunInteractivePreview() const
    {
        return RunState == EDWCEditorHostRunState::Interactive;
    }

    bool HasBlocker(const EDWCEditorHostLifecycleBlocker Blocker) const
    {
        return EnumHasAnyFlags(Blockers, Blocker);
    }
};

struct FDWCEditorHostLifecycleTransition
{
    FDWCEditorHostLifecycleSnapshot Previous;
    FDWCEditorHostLifecycleSnapshot Current;
    EDWCEditorHostLifecycleBlocker  ChangedBlockers = EDWCEditorHostLifecycleBlocker::None;
    bool                            bBlockersChanged = false;
    bool                            bBecameInteractive = false;
    bool                            bBecameSuspended = false;
    bool                            bBecameClosing = false;
};

/**
 * Pure state reducer for one WCA editor host. Slate and viewport owners feed it
 * blockers and react only to the returned state-edge transition.
 */
class FDWCEditorHostLifecycleReducer final
{
  public:
    explicit FDWCEditorHostLifecycleReducer(
        EDWCEditorHostLifecycleBlocker InitialBlockers = EDWCEditorHostLifecycleBlocker::None);

    FDWCEditorHostLifecycleTransition SetBlocker(
        EDWCEditorHostLifecycleBlocker Blocker,
        bool                           bEnabled);
    FDWCEditorHostLifecycleTransition SetBlockers(
        EDWCEditorHostLifecycleBlocker Blockers);
    FDWCEditorHostLifecycleTransition SetVisibilitySnapshot(
        const FDWCEditorHostVisibilitySnapshot& Visibility);

    const FDWCEditorHostLifecycleSnapshot& GetSnapshot() const { return Snapshot; }
    bool                                   CanRunInteractivePreview() const { return Snapshot.CanRunInteractivePreview(); }
    bool                                   HasBlocker(EDWCEditorHostLifecycleBlocker Blocker) const
    {
        return Snapshot.HasBlocker(Blocker);
    }

  private:
    static EDWCEditorHostRunState ResolveRunState(EDWCEditorHostLifecycleBlocker Blockers);

    FDWCEditorHostLifecycleSnapshot Snapshot;
};

EDWCEditorHostLifecycleBlocker ResolveHostVisibilityBlockers(
    const FDWCEditorHostVisibilitySnapshot& Visibility);
bool RequiresImmediatePreviewResourceRelease(EDWCEditorHostLifecycleBlocker Blockers);
FString      LexToString(EDWCEditorHostLifecycleBlocker Blockers);
const TCHAR* LexToString(EDWCEditorHostRunState RunState);
