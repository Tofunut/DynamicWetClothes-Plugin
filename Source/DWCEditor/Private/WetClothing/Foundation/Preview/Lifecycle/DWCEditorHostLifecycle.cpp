// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorHostLifecycle.h"

namespace
{
    constexpr EDWCEditorHostLifecycleBlocker VisibilityBlockerMask =
        EDWCEditorHostLifecycleBlocker::HostUnavailable |
        EDWCEditorHostLifecycleBlocker::TabBackground |
        EDWCEditorHostLifecycleBlocker::WindowHidden |
        EDWCEditorHostLifecycleBlocker::WindowMinimized |
        EDWCEditorHostLifecycleBlocker::WindowInactive |
        EDWCEditorHostLifecycleBlocker::ApplicationInactive;

    void AppendBlockerName(
        TArray<FString>&                     Names,
        const EDWCEditorHostLifecycleBlocker Blockers,
        const EDWCEditorHostLifecycleBlocker Blocker,
        const TCHAR*                         Name)
    {
        if (EnumHasAnyFlags(Blockers, Blocker))
        {
            Names.Emplace(Name);
        }
    }
} // namespace

FDWCEditorHostLifecycleReducer::FDWCEditorHostLifecycleReducer(
    const EDWCEditorHostLifecycleBlocker InitialBlockers)
{
    Snapshot.Blockers = InitialBlockers;
    Snapshot.RunState = ResolveRunState(InitialBlockers);
}

FDWCEditorHostLifecycleTransition FDWCEditorHostLifecycleReducer::SetBlocker(
    const EDWCEditorHostLifecycleBlocker Blocker,
    const bool                           bEnabled)
{
    if (Blocker == EDWCEditorHostLifecycleBlocker::None)
    {
        FDWCEditorHostLifecycleTransition Transition;
        Transition.Previous = Snapshot;
        Transition.Current = Snapshot;
        return Transition;
    }

    EDWCEditorHostLifecycleBlocker NewBlockers = Snapshot.Blockers;
    if (bEnabled)
    {
        NewBlockers |= Blocker;
    }
    else
    {
        NewBlockers &= ~Blocker;
    }
    return SetBlockers(NewBlockers);
}

FDWCEditorHostLifecycleTransition FDWCEditorHostLifecycleReducer::SetBlockers(
    EDWCEditorHostLifecycleBlocker Blockers)
{
    FDWCEditorHostLifecycleTransition Transition;
    Transition.Previous = Snapshot;

    // Closing is terminal. A late host event must not revive the editor.
    if (Snapshot.HasBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing))
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::EditorClosing;
    }

    if (Blockers == Snapshot.Blockers)
    {
        Transition.Current = Snapshot;
        return Transition;
    }

    const bool                   bWasInteractive = Snapshot.CanRunInteractivePreview();
    const EDWCEditorHostRunState PreviousRunState = Snapshot.RunState;
    Snapshot.Blockers = Blockers;
    Snapshot.RunState = ResolveRunState(Blockers);
    ++Snapshot.StateRevision;

    const bool bIsInteractive = Snapshot.CanRunInteractivePreview();
    if (bWasInteractive != bIsInteractive)
    {
        ++Snapshot.InteractiveGeneration;
    }

    Transition.Current = Snapshot;
    Transition.ChangedBlockers = Transition.Previous.Blockers ^ Snapshot.Blockers;
    Transition.bBlockersChanged = true;
    Transition.bBecameInteractive = !bWasInteractive && bIsInteractive;
    Transition.bBecameSuspended = bWasInteractive && !bIsInteractive;
    Transition.bBecameClosing =
        PreviousRunState != EDWCEditorHostRunState::Closing &&
        Snapshot.RunState == EDWCEditorHostRunState::Closing;
    return Transition;
}

FDWCEditorHostLifecycleTransition FDWCEditorHostLifecycleReducer::SetVisibilitySnapshot(
    const FDWCEditorHostVisibilitySnapshot& Visibility)
{
    const EDWCEditorHostLifecycleBlocker PreservedBlockers = Snapshot.Blockers & ~VisibilityBlockerMask;
    return SetBlockers(PreservedBlockers | ResolveHostVisibilityBlockers(Visibility));
}

EDWCEditorHostRunState FDWCEditorHostLifecycleReducer::ResolveRunState(
    const EDWCEditorHostLifecycleBlocker Blockers)
{
    if (EnumHasAnyFlags(Blockers, EDWCEditorHostLifecycleBlocker::EditorClosing))
    {
        return EDWCEditorHostRunState::Closing;
    }
    return Blockers == EDWCEditorHostLifecycleBlocker::None
               ? EDWCEditorHostRunState::Interactive
               : EDWCEditorHostRunState::Suspended;
}

EDWCEditorHostLifecycleBlocker ResolveHostVisibilityBlockers(
    const FDWCEditorHostVisibilitySnapshot& Visibility)
{
    if (!Visibility.bHostAvailable)
    {
        return EDWCEditorHostLifecycleBlocker::HostUnavailable;
    }

    EDWCEditorHostLifecycleBlocker Blockers = EDWCEditorHostLifecycleBlocker::None;
    if (!Visibility.bTabForeground)
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::TabBackground;
    }
    if (!Visibility.bWindowVisible)
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::WindowHidden;
    }
    if (Visibility.bWindowMinimized)
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::WindowMinimized;
    }
    if (!Visibility.bWindowActive)
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::WindowInactive;
    }
    if (!Visibility.bApplicationActive)
    {
        Blockers |= EDWCEditorHostLifecycleBlocker::ApplicationInactive;
    }
    return Blockers;
}

bool RequiresImmediatePreviewResourceRelease(
    const EDWCEditorHostLifecycleBlocker Blockers)
{
    constexpr EDWCEditorHostLifecycleBlocker ImmediateBlockers =
        EDWCEditorHostLifecycleBlocker::HostUnavailable |
        EDWCEditorHostLifecycleBlocker::WindowHidden |
        EDWCEditorHostLifecycleBlocker::WindowMinimized |
        EDWCEditorHostLifecycleBlocker::ApplicationInactive |
        EDWCEditorHostLifecycleBlocker::PIE |
        EDWCEditorHostLifecycleBlocker::ExclusiveBuild |
        EDWCEditorHostLifecycleBlocker::EditorClosing;
    return EnumHasAnyFlags(Blockers, ImmediateBlockers);
}

FString LexToString(const EDWCEditorHostLifecycleBlocker Blockers)
{
    if (Blockers == EDWCEditorHostLifecycleBlocker::None)
    {
        return TEXT("None");
    }

    TArray<FString> Names;
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::HostUnavailable, TEXT("HostUnavailable"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::TabBackground, TEXT("TabBackground"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::WindowHidden, TEXT("WindowHidden"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::WindowMinimized, TEXT("WindowMinimized"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::WindowInactive, TEXT("WindowInactive"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::ApplicationInactive, TEXT("ApplicationInactive"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::PIE, TEXT("PIE"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::ExclusiveBuild, TEXT("ExclusiveBuild"));
    AppendBlockerName(Names, Blockers, EDWCEditorHostLifecycleBlocker::EditorClosing, TEXT("EditorClosing"));
    return FString::Join(Names, TEXT("|"));
}

const TCHAR* LexToString(const EDWCEditorHostRunState RunState)
{
    switch (RunState)
    {
    case EDWCEditorHostRunState::Suspended:
        return TEXT("Suspended");
    case EDWCEditorHostRunState::Interactive:
        return TEXT("Interactive");
    case EDWCEditorHostRunState::Closing:
        return TEXT("Closing");
    default:
        return TEXT("Unknown");
    }
}
