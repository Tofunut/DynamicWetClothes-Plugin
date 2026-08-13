// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorSlateHostVisibilityAdapter.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

namespace
{
    constexpr float HostVisibilityPollIntervalSeconds = 0.1f;
}

void FDWCEditorHostVisibilityPublisher::Initialize(FVisibilityChanged InVisibilityChanged)
{
    VisibilityChanged = MoveTemp(InVisibilityChanged);
    LastPublishedSnapshot.Reset();
    bInitialized = true;
}

void FDWCEditorHostVisibilityPublisher::Shutdown()
{
    VisibilityChanged = nullptr;
    LastPublishedSnapshot.Reset();
    bInitialized = false;
}

bool FDWCEditorHostVisibilityPublisher::Publish(
    const FDWCEditorHostVisibilitySnapshot& Snapshot,
    const bool bForce)
{
    if (!bInitialized || !VisibilityChanged ||
        (!bForce && LastPublishedSnapshot.IsSet() && LastPublishedSnapshot.GetValue() == Snapshot))
    {
        return false;
    }

    LastPublishedSnapshot = Snapshot;
    VisibilityChanged(Snapshot);
    return true;
}

FDWCEditorSlateHostVisibilityAdapter::~FDWCEditorSlateHostVisibilityAdapter()
{
    Shutdown();
}

void FDWCEditorSlateHostVisibilityAdapter::Initialize(
    const TSharedRef<SDockTab>& InContentTab,
    const TSharedPtr<FTabManager>& InLocalTabManager,
    FVisibilityChanged InVisibilityChanged)
{
    check(IsInGameThread());
    Shutdown();

    ContentTab = InContentTab;
    LocalTabManager = InLocalTabManager;
    Publisher.Initialize(MoveTemp(InVisibilityChanged));
    bInitialized = true;

    const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
    ActiveTabChangedHandle = GlobalTabManager->OnActiveTabChanged_Subscribe(
        FOnActiveTabChanged::FDelegate::CreateSP(
            this,
            &FDWCEditorSlateHostVisibilityAdapter::HandleTabStateChanged));
    TabForegroundedHandle = GlobalTabManager->OnTabForegrounded_Subscribe(
        FOnActiveTabChanged::FDelegate::CreateSP(
            this,
            &FDWCEditorSlateHostVisibilityAdapter::HandleTabStateChanged));

    if (FSlateApplication::IsInitialized())
    {
        ApplicationActivationHandle = FSlateApplication::Get()
            .OnApplicationActivationStateChanged()
            .AddSP(this, &FDWCEditorSlateHostVisibilityAdapter::HandleApplicationActivationChanged);
    }

    PollHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(this, &FDWCEditorSlateHostVisibilityAdapter::HandlePoll),
        HostVisibilityPollIntervalSeconds);
    EvaluateAndPublish(true);
}

void FDWCEditorSlateHostVisibilityAdapter::Shutdown()
{
    check(IsInGameThread());

    if (ActiveTabChangedHandle.IsValid())
    {
        FGlobalTabmanager::Get()->OnActiveTabChanged_Unsubscribe(ActiveTabChangedHandle);
        ActiveTabChangedHandle.Reset();
    }
    if (TabForegroundedHandle.IsValid())
    {
        FGlobalTabmanager::Get()->OnTabForegrounded_Unsubscribe(TabForegroundedHandle);
        TabForegroundedHandle.Reset();
    }
    if (ApplicationActivationHandle.IsValid() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
        ApplicationActivationHandle.Reset();
    }
    if (PollHandle.IsValid())
    {
        FTSTicker::RemoveTicker(PollHandle);
        PollHandle.Reset();
    }

    UnbindHostWindow();
    ContentTab.Reset();
    LocalTabManager.Reset();
    ClosedHostWindow.Reset();
    Publisher.Shutdown();
    bInitialized = false;
}

void FDWCEditorSlateHostVisibilityAdapter::HandleTabStateChanged(
    TSharedPtr<SDockTab>,
    TSharedPtr<SDockTab>)
{
    EvaluateAndPublish();
}

void FDWCEditorSlateHostVisibilityAdapter::HandleApplicationActivationChanged(const bool)
{
    EvaluateAndPublish();
}

void FDWCEditorSlateHostVisibilityAdapter::HandleWindowActivated()
{
    EvaluateAndPublish();
}

void FDWCEditorSlateHostVisibilityAdapter::HandleWindowDeactivated()
{
    EvaluateAndPublish();
}

void FDWCEditorSlateHostVisibilityAdapter::HandleWindowClosed(const TSharedRef<SWindow>& ClosedWindow)
{
    ClosedHostWindow = ClosedWindow;
    EvaluateAndPublish(true);
}

bool FDWCEditorSlateHostVisibilityAdapter::HandlePoll(const float)
{
    EvaluateAndPublish();
    return bInitialized;
}

void FDWCEditorSlateHostVisibilityAdapter::EvaluateAndPublish(const bool bForce)
{
    check(IsInGameThread());
    if (!bInitialized)
    {
        return;
    }

    Publisher.Publish(CaptureSnapshot(), bForce);
}

FDWCEditorHostVisibilitySnapshot FDWCEditorSlateHostVisibilityAdapter::CaptureSnapshot()
{
    FDWCEditorHostVisibilitySnapshot Snapshot;
    const TSharedPtr<SDockTab> PinnedContentTab = ContentTab.Pin();
    const TSharedPtr<SDockTab> MajorTab = ResolveMajorTab();
    const TSharedPtr<SWindow> HostWindow = ResolveHostWindow(MajorTab);
    RebindHostWindow(HostWindow);

    const bool bClosedWindow = HostWindow.IsValid() && ClosedHostWindow.Pin() == HostWindow;
    Snapshot.bHostAvailable = PinnedContentTab.IsValid() && HostWindow.IsValid() && !bClosedWindow;
    if (!Snapshot.bHostAvailable)
    {
        return Snapshot;
    }

    Snapshot.bTabForeground = PinnedContentTab->IsForeground() &&
        (!MajorTab.IsValid() || MajorTab->IsForeground());
    Snapshot.bWindowVisible = HostWindow->IsVisible();
    Snapshot.bWindowMinimized = HostWindow->IsWindowMinimized();
    Snapshot.bApplicationActive = FSlateApplication::IsInitialized() && FSlateApplication::Get().IsActive();

    if (FSlateApplication::IsInitialized())
    {
        const TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
        Snapshot.bWindowActive = ActiveWindow == HostWindow ||
            (ActiveWindow.IsValid() && ActiveWindow->IsDescendantOf(HostWindow));
    }
    return Snapshot;
}

TSharedPtr<SDockTab> FDWCEditorSlateHostVisibilityAdapter::ResolveMajorTab() const
{
    const TSharedPtr<FTabManager> PinnedTabManager = LocalTabManager.Pin();
    return PinnedTabManager.IsValid()
        ? FGlobalTabmanager::Get()->GetMajorTabForTabManager(PinnedTabManager.ToSharedRef())
        : nullptr;
}

TSharedPtr<SWindow> FDWCEditorSlateHostVisibilityAdapter::ResolveHostWindow(
    const TSharedPtr<SDockTab>& MajorTab) const
{
    if (MajorTab.IsValid())
    {
        if (const TSharedPtr<SWindow> MajorWindow = MajorTab->GetParentWindow())
        {
            return MajorWindow;
        }
    }
    if (const TSharedPtr<SDockTab> PinnedContentTab = ContentTab.Pin())
    {
        return PinnedContentTab->GetParentWindow();
    }
    return nullptr;
}

void FDWCEditorSlateHostVisibilityAdapter::RebindHostWindow(
    const TSharedPtr<SWindow>& NewHostWindow)
{
    if (BoundHostWindow.Pin() == NewHostWindow)
    {
        return;
    }

    UnbindHostWindow();
    if (!NewHostWindow.IsValid())
    {
        return;
    }

    if (ClosedHostWindow.Pin() != NewHostWindow)
    {
        ClosedHostWindow.Reset();
    }
    BoundHostWindow = NewHostWindow;
    WindowActivatedHandle = NewHostWindow->GetOnWindowActivatedEvent().AddSP(
        this,
        &FDWCEditorSlateHostVisibilityAdapter::HandleWindowActivated);
    WindowDeactivatedHandle = NewHostWindow->GetOnWindowDeactivatedEvent().AddSP(
        this,
        &FDWCEditorSlateHostVisibilityAdapter::HandleWindowDeactivated);
    WindowClosedHandle = NewHostWindow->GetOnWindowClosedEvent().AddSP(
        this,
        &FDWCEditorSlateHostVisibilityAdapter::HandleWindowClosed);
}

void FDWCEditorSlateHostVisibilityAdapter::UnbindHostWindow()
{
    if (const TSharedPtr<SWindow> HostWindow = BoundHostWindow.Pin())
    {
        HostWindow->GetOnWindowActivatedEvent().Remove(WindowActivatedHandle);
        HostWindow->GetOnWindowDeactivatedEvent().Remove(WindowDeactivatedHandle);
        HostWindow->GetOnWindowClosedEvent().Remove(WindowClosedHandle);
    }
    WindowActivatedHandle.Reset();
    WindowDeactivatedHandle.Reset();
    WindowClosedHandle.Reset();
    BoundHostWindow.Reset();
}
