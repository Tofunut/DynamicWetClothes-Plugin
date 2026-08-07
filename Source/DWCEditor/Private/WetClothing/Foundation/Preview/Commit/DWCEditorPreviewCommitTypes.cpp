//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitTypes.h"

FDWCEditorPreviewConsumerLifetime::FDWCEditorPreviewConsumerLifetime()
    : State(MakeShared<FDWCEditorPreviewConsumerLifetimeState>())
{
}

FDWCEditorPreviewConsumerToken FDWCEditorPreviewConsumerLifetime::CaptureToken() const
{
    check(IsInGameThread());
    FDWCEditorPreviewConsumerToken Token;
    Token.State = State;
    Token.ConsumerEpoch = State->ConsumerEpoch;
    Token.Generation = State->Generation;
    return Token;
}

void FDWCEditorPreviewConsumerLifetime::AdvanceGeneration()
{
    check(IsInGameThread());
    ++State->Generation;
}

void FDWCEditorPreviewConsumerLifetime::Suspend()
{
    check(IsInGameThread());
    if (State->State != EDWCEditorPreviewConsumerState::Revoked)
    {
        State->State = EDWCEditorPreviewConsumerState::Suspended;
        ++State->Generation;
    }
}

void FDWCEditorPreviewConsumerLifetime::Resume()
{
    check(IsInGameThread());
    if (State->State != EDWCEditorPreviewConsumerState::Revoked)
    {
        State->State = EDWCEditorPreviewConsumerState::Active;
        ++State->Generation;
    }
}

void FDWCEditorPreviewConsumerLifetime::Revoke()
{
    check(IsInGameThread());
    State->State = EDWCEditorPreviewConsumerState::Revoked;
    ++State->Generation;
}

bool FDWCEditorPreviewConsumerLifetime::IsActive() const
{
    check(IsInGameThread());
    return State->State == EDWCEditorPreviewConsumerState::Active;
}
