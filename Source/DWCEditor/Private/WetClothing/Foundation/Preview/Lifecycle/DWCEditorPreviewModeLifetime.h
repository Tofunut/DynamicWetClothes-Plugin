// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Stable preview domains owned by one WCA editor instance. */
enum class EDWCEditorPreviewMode : uint8
{
    None,
    WetPart,
    Wrinkle,
    Transparency
};

enum class EDWCEditorPreviewModeRunState : uint8
{
    Inactive,
    Active,
    Suspended,
    Revoked
};

struct FDWCEditorPreviewModeLifetimeState
{
    FGuid SessionEpoch;
    EDWCEditorPreviewMode Mode = EDWCEditorPreviewMode::None;
    EDWCEditorPreviewModeRunState RunState = EDWCEditorPreviewModeRunState::Inactive;
    uint64 HostGeneration = 0;
    uint64 Generation = 1;
};

/** Immutable proof that interactive work belongs to the currently active preview generation. */
struct FDWCEditorPreviewRunToken
{
    TWeakPtr<FDWCEditorPreviewModeLifetimeState, ESPMode::ThreadSafe> State;
    FGuid SessionEpoch;
    EDWCEditorPreviewMode Mode = EDWCEditorPreviewMode::None;
    uint64 HostGeneration = 0;
    uint64 Generation = 0;

    bool IsValid() const;
    bool IsCurrent() const;
};

/**
 * Game-thread state machine for one preview mode. State changes invalidate all
 * worker and presentation tokens captured by the previous generation.
 */
class FDWCEditorPreviewModeLifetime final
{
public:
    FDWCEditorPreviewModeLifetime(EDWCEditorPreviewMode Mode, const FGuid& SessionEpoch);

    void Activate(uint64 HostGeneration);
    void Suspend(uint64 HostGeneration);
    void Deactivate(uint64 HostGeneration);
    void Revoke(uint64 HostGeneration);

    FDWCEditorPreviewRunToken CaptureToken() const;
    bool IsActive() const;
    EDWCEditorPreviewModeRunState GetRunState() const;
    uint64 GetGeneration() const;

private:
    void TransitionTo(EDWCEditorPreviewModeRunState NewState, uint64 HostGeneration);

    TSharedRef<FDWCEditorPreviewModeLifetimeState, ESPMode::ThreadSafe> State;
};

const TCHAR* LexToString(EDWCEditorPreviewMode Mode);
const TCHAR* LexToString(EDWCEditorPreviewModeRunState RunState);
