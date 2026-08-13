// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorPreviewModeLifetime.h"

bool FDWCEditorPreviewRunToken::IsValid() const
{
    return SessionEpoch.IsValid() && Mode != EDWCEditorPreviewMode::None &&
        HostGeneration != 0 && Generation != 0 && State.IsValid();
}

bool FDWCEditorPreviewRunToken::IsCurrent() const
{
    const TSharedPtr<FDWCEditorPreviewModeLifetimeState, ESPMode::ThreadSafe> PinnedState = State.Pin();
    return IsValid() && PinnedState.IsValid() &&
        PinnedState->SessionEpoch == SessionEpoch &&
        PinnedState->Mode == Mode &&
        PinnedState->RunState == EDWCEditorPreviewModeRunState::Active &&
        PinnedState->HostGeneration == HostGeneration &&
        PinnedState->Generation == Generation;
}

FDWCEditorPreviewModeLifetime::FDWCEditorPreviewModeLifetime(
    const EDWCEditorPreviewMode Mode,
    const FGuid& SessionEpoch)
    : State(MakeShared<FDWCEditorPreviewModeLifetimeState, ESPMode::ThreadSafe>())
{
    check(Mode != EDWCEditorPreviewMode::None);
    State->Mode = Mode;
    State->SessionEpoch = SessionEpoch.IsValid() ? SessionEpoch : FGuid::NewGuid();
}

void FDWCEditorPreviewModeLifetime::Activate(const uint64 HostGeneration)
{
    TransitionTo(EDWCEditorPreviewModeRunState::Active, HostGeneration);
}

void FDWCEditorPreviewModeLifetime::Suspend(const uint64 HostGeneration)
{
    if (State->RunState != EDWCEditorPreviewModeRunState::Revoked)
    {
        TransitionTo(EDWCEditorPreviewModeRunState::Suspended, HostGeneration);
    }
}

void FDWCEditorPreviewModeLifetime::Deactivate(const uint64 HostGeneration)
{
    if (State->RunState != EDWCEditorPreviewModeRunState::Revoked)
    {
        TransitionTo(EDWCEditorPreviewModeRunState::Inactive, HostGeneration);
    }
}

void FDWCEditorPreviewModeLifetime::Revoke(const uint64 HostGeneration)
{
    TransitionTo(EDWCEditorPreviewModeRunState::Revoked, HostGeneration);
}

FDWCEditorPreviewRunToken FDWCEditorPreviewModeLifetime::CaptureToken() const
{
    check(IsInGameThread());
    if (State->RunState != EDWCEditorPreviewModeRunState::Active)
    {
        return {};
    }

    FDWCEditorPreviewRunToken Token;
    Token.State = State;
    Token.SessionEpoch = State->SessionEpoch;
    Token.Mode = State->Mode;
    Token.HostGeneration = State->HostGeneration;
    Token.Generation = State->Generation;
    return Token;
}

bool FDWCEditorPreviewModeLifetime::IsActive() const
{
    check(IsInGameThread());
    return State->RunState == EDWCEditorPreviewModeRunState::Active;
}

EDWCEditorPreviewModeRunState FDWCEditorPreviewModeLifetime::GetRunState() const
{
    check(IsInGameThread());
    return State->RunState;
}

uint64 FDWCEditorPreviewModeLifetime::GetGeneration() const
{
    check(IsInGameThread());
    return State->Generation;
}

void FDWCEditorPreviewModeLifetime::TransitionTo(
    const EDWCEditorPreviewModeRunState NewState,
    const uint64 HostGeneration)
{
    check(IsInGameThread());
    if (State->RunState == EDWCEditorPreviewModeRunState::Revoked &&
        NewState != EDWCEditorPreviewModeRunState::Revoked)
    {
        return;
    }
    if (State->RunState == NewState && State->HostGeneration == HostGeneration)
    {
        return;
    }

    State->RunState = NewState;
    State->HostGeneration = HostGeneration;
    ++State->Generation;
}

const TCHAR* LexToString(const EDWCEditorPreviewMode Mode)
{
    switch (Mode)
    {
    case EDWCEditorPreviewMode::WetPart: return TEXT("WetPart");
    case EDWCEditorPreviewMode::Wrinkle: return TEXT("Wrinkle");
    case EDWCEditorPreviewMode::Transparency: return TEXT("Transparency");
    default: return TEXT("None");
    }
}

const TCHAR* LexToString(const EDWCEditorPreviewModeRunState RunState)
{
    switch (RunState)
    {
    case EDWCEditorPreviewModeRunState::Inactive: return TEXT("Inactive");
    case EDWCEditorPreviewModeRunState::Active: return TEXT("Active");
    case EDWCEditorPreviewModeRunState::Suspended: return TEXT("Suspended");
    case EDWCEditorPreviewModeRunState::Revoked: return TEXT("Revoked");
    default: return TEXT("Unknown");
    }
}
