// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorHostLifecycle.h"

class FTabManager;
class SDockTab;
class SWindow;

/** Publishes only meaningful visibility transitions and is independent of Slate handles. */
class FDWCEditorHostVisibilityPublisher final
{
public:
    using FVisibilityChanged = TFunction<void(const FDWCEditorHostVisibilitySnapshot&)>;

    void Initialize(FVisibilityChanged InVisibilityChanged);
    void Shutdown();
    bool Publish(const FDWCEditorHostVisibilitySnapshot& Snapshot, bool bForce = false);

private:
    FVisibilityChanged VisibilityChanged;
    TOptional<FDWCEditorHostVisibilitySnapshot> LastPublishedSnapshot;
    bool bInitialized = false;
};

/**
 * Translates Slate tab, window, and application state into the host lifecycle
 * visibility contract. It owns only event subscriptions and weak Slate handles.
 */
class FDWCEditorSlateHostVisibilityAdapter final
    : public TSharedFromThis<FDWCEditorSlateHostVisibilityAdapter>
{
public:
    using FVisibilityChanged = TFunction<void(const FDWCEditorHostVisibilitySnapshot&)>;

    ~FDWCEditorSlateHostVisibilityAdapter();

    void Initialize(
        const TSharedRef<SDockTab>& InContentTab,
        const TSharedPtr<FTabManager>& InLocalTabManager,
        FVisibilityChanged InVisibilityChanged);
    void Shutdown();

private:
    void HandleTabStateChanged(TSharedPtr<SDockTab> NewlyActive, TSharedPtr<SDockTab> PreviouslyActive);
    void HandleApplicationActivationChanged(bool bIsActive);
    void HandleWindowActivated();
    void HandleWindowDeactivated();
    void HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow);
    bool HandlePoll(float DeltaTime);

    void EvaluateAndPublish(bool bForce = false);
    FDWCEditorHostVisibilitySnapshot CaptureSnapshot();
    TSharedPtr<SDockTab> ResolveMajorTab() const;
    TSharedPtr<SWindow> ResolveHostWindow(const TSharedPtr<SDockTab>& MajorTab) const;
    void RebindHostWindow(const TSharedPtr<SWindow>& NewHostWindow);
    void UnbindHostWindow();

    TWeakPtr<SDockTab> ContentTab;
    TWeakPtr<FTabManager> LocalTabManager;
    TWeakPtr<SWindow> BoundHostWindow;
    TWeakPtr<SWindow> ClosedHostWindow;
    FDWCEditorHostVisibilityPublisher Publisher;
    FDelegateHandle ActiveTabChangedHandle;
    FDelegateHandle TabForegroundedHandle;
    FDelegateHandle ApplicationActivationHandle;
    FDelegateHandle WindowActivatedHandle;
    FDelegateHandle WindowDeactivatedHandle;
    FDelegateHandle WindowClosedHandle;
    FTSTicker::FDelegateHandle PollHandle;
    bool bInitialized = false;
};
