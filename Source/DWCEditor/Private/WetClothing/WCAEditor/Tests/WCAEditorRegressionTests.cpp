// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorHostLifecycle.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorPreviewModeLifetime.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"
#include "WetClothing/WCAEditor/UI/WCAEditorRefreshState.h"

namespace
{
    struct FWCAEditorTestCacheValue final : IDWCEditorCacheValue
    {
        explicit FWCAEditorTestCacheValue(const uint64 InBytes)
            : Bytes(InBytes)
        {
        }

        static FName StaticCacheTypeName()
        {
            return TEXT("WCAEditorRegressionCacheValue");
        }

        virtual FName GetCacheTypeName() const override
        {
            return StaticCacheTypeName();
        }

        virtual uint64 GetAllocatedSizeBytes() const override
        {
            return Bytes;
        }

        uint64 Bytes = 0;
    };

    FDWCEditorResourceBudgetConfig MakeWCAEditorTestBudget()
    {
        constexpr uint64 BudgetBytes = 16ull * 1024ull * 1024ull;
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = BudgetBytes;
        Config.WorkerPrivateCPUBytes = BudgetBytes;
        Config.PreviewWorkspaceCPUBytes = BudgetBytes;
        Config.SharedCacheCPUBytes = BudgetBytes;
        Config.UploadStagingCPUBytes = BudgetBytes;
        Config.PreviewGPUBytes = BudgetBytes;
        return Config;
    }

    void ApplyPreviewLifecycleTransition(
        const FDWCEditorHostLifecycleTransition& Transition,
        FDWCEditorPreviewModeLifetime& ActiveLifetime)
    {
        if (Transition.bBecameClosing)
        {
            ActiveLifetime.Revoke(Transition.Current.InteractiveGeneration);
        }
        else if (Transition.bBecameSuspended)
        {
            ActiveLifetime.Suspend(Transition.Current.InteractiveGeneration);
        }
        else if (Transition.bBecameInteractive)
        {
            ActiveLifetime.Activate(Transition.Current.InteractiveGeneration);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorRefreshCoalescingRegressionTest,
    "DWC.Editor.WCA.Refresh.Coalescing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorRefreshCoalescingRegressionTest::RunTest(const FString&)
{
    FWCAEditorRefreshState State;

    TestTrue(TEXT("The first lightweight request schedules one timer."),
        State.RequestModeRefresh(false));
    TestFalse(TEXT("A repeated lightweight request is coalesced."),
        State.RequestModeRefresh(false));
    TestFalse(TEXT("A full request reuses the pending timer."),
        State.RequestModeRefresh(true));

    bool bFullRefresh = false;
    TestTrue(TEXT("The pending mode refresh can be consumed."),
        State.ConsumeModeRefresh(bFullRefresh));
    TestTrue(TEXT("A full request dominates earlier lightweight requests."), bFullRefresh);
    TestFalse(TEXT("A consumed mode request cannot run twice."),
        State.ConsumeModeRefresh(bFullRefresh));

    TestTrue(TEXT("Status refresh owns an independent timer channel."),
        State.RequestStatusRefresh());
    TestTrue(TEXT("Mode refresh can be pending beside status refresh."),
        State.RequestModeRefresh(false));
    TestFalse(TEXT("A repeated status request is coalesced."),
        State.RequestStatusRefresh());
    TestTrue(TEXT("Status refresh is consumed exactly once."),
        State.ConsumeStatusRefresh());
    TestFalse(TEXT("Status refresh cannot run twice."),
        State.ConsumeStatusRefresh());

    State.CancelModeRefresh();
    TestFalse(TEXT("A direct refresh cancels its deferred duplicate."),
        State.ConsumeModeRefresh(bFullRefresh));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorPanelExplicitShutdownRegressionTest,
    "DWC.Editor.WCA.Lifecycle.ExplicitPanelShutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorPanelExplicitShutdownRegressionTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceBroker> Broker = FDWCEditorResourceBroker::Get();
    const FDWCEditorResourceBrokerDiagnostics Before = Broker->GetDiagnostics();
    TStrongObjectPtr<UWetClothingAsset> Asset(
        NewObject<UWetClothingAsset>(GetTransientPackage()));
    TSharedPtr<SWCAEditorPanel> Panel;
    SAssignNew(Panel, SWCAEditorPanel)
        .WetClothingAsset(Asset.Get());

    TestFalse(TEXT("A newly constructed WCA panel accepts work."), Panel->IsShuttingDown());

    // Leave panel-owned upload and deferred refresh timers pending at shutdown.
    Panel->RequestRefreshFromAsset();
    Panel->RefreshStatusFromAsset();
    Panel->Shutdown();

    TestTrue(TEXT("Explicit shutdown reaches the terminal state."),
        Panel->IsShutdownComplete());
    const FDWCEditorResourceBrokerDiagnostics AfterShutdown = Broker->GetDiagnostics();
    TestEqual(TEXT("Explicit shutdown closes the panel resource session."),
        AfterShutdown.SessionCount, Before.SessionCount);
    TestEqual(TEXT("Explicit shutdown unregisters every panel resource participant."),
        AfterShutdown.ParticipantCount, Before.ParticipantCount);
    FString RejectionReason;
    TestFalse(TEXT("A closed panel rejects new Build work."),
        Panel->CanStartBuildAction(&RejectionReason));
    TestEqual(TEXT("Build rejection reports the editor lifecycle state."),
        RejectionReason, FString(TEXT("The WCA editor is closing.")));

    // Shutdown is intentionally idempotent because close requests and the toolkit
    // destructor can both observe the same panel instance.
    Panel->Shutdown();
    TestTrue(TEXT("Repeated shutdown remains complete."), Panel->IsShutdownComplete());
    Panel.Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorAuthoringRefreshEscalationRegressionTest,
    "DWC.Editor.WCA.Authoring.RefreshEscalation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorAuthoringRefreshEscalationRegressionTest::RunTest(const FString&)
{
    TSharedRef<FDWCEditorSessionStore> SessionStore = MakeShared<FDWCEditorSessionStore>();
    FWCAEditorRefreshState RefreshState;
    int32 ScheduledTimerCount = 0;

    SessionStore->OnChanged().AddLambda(
        [&RefreshState, &ScheduledTimerCount](
            const FDWCEditorSessionState&,
            const EDWCEditorSessionEffect Effects,
            const uint64)
        {
            const bool bFullRefresh = EnumHasAnyFlags(
                Effects,
                EDWCEditorSessionEffect::RebuildPreviewContent |
                    EDWCEditorSessionEffect::RebuildPreviewMaterials |
                    EDWCEditorSessionEffect::RebuildHitTopology);
            const bool bLightweightRefresh = EnumHasAnyFlags(
                Effects,
                EDWCEditorSessionEffect::RefreshElementList |
                    EDWCEditorSessionEffect::RefreshUVView |
                    EDWCEditorSessionEffect::RefreshDetails |
                    EDWCEditorSessionEffect::RefreshPartSlotPresentation);
            if ((bFullRefresh || bLightweightRefresh) &&
                RefreshState.RequestModeRefresh(bFullRefresh))
            {
                ++ScheduledTimerCount;
            }
            if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshStatus))
            {
                RefreshState.RequestStatusRefresh();
            }
        });

    FDWCReconcileAuthoringAction Incremental;
    Incremental.AuthoringRevision = 1;
    Incremental.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Incremental.Impact = EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::PreviewIncremental;
    SessionStore->Dispatch(Incremental);

    FDWCReconcileAuthoringAction Full = Incremental;
    Full.AuthoringRevision = 2;
    Full.Impact = EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::Details;
    SessionStore->Dispatch(Full);

    TestEqual(TEXT("Incremental and full authoring changes share one deferred timer."),
        ScheduledTimerCount, 1);
    TestEqual(TEXT("The session records the latest authoring revision."),
        SessionStore->GetState().WrinkleAuthoringRevision, uint64{2});

    bool bFullRefresh = false;
    TestTrue(TEXT("The coalesced authoring refresh is available."),
        RefreshState.ConsumeModeRefresh(bFullRefresh));
    TestTrue(TEXT("A later full authoring impact escalates the pending refresh."),
        bFullRefresh);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorCacheOwnershipRegressionTest,
    "DWC.Editor.WCA.Cache.InvalidationAndLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorCacheOwnershipRegressionTest::RunTest(const FString&)
{
    FDWCEditorCacheStore Store(4096);
    UTexture2D* AssetOwner = NewObject<UTexture2D>();
    UTexture2D* MeshIdentity = NewObject<UTexture2D>();

    FDWCEditorCacheKey TopologyKey;
    TopologyKey.Namespace = TEXT("DWC.UVTopology");
    TopologyKey.Owner = FObjectKey(AssetOwner);
    TopologyKey.ResourceIdentity = MeshIdentity;
    TopologyKey.UVChannelIndex = 2;
    TopologyKey.MaterialSlotIndex = 4;

    FDWCEditorCacheKey ProjectionKey = TopologyKey;
    ProjectionKey.Namespace = TEXT("DWC.SurfaceProjection");
    FDWCEditorCacheKey MaterialKey = TopologyKey;
    MaterialKey.Namespace = TEXT("DWC.PreviewMaterial");

    Store.Put(TopologyKey, MakeShared<FWCAEditorTestCacheValue, ESPMode::ThreadSafe>(256));
    Store.Put(ProjectionKey, MakeShared<FWCAEditorTestCacheValue, ESPMode::ThreadSafe>(256));
    Store.Put(MaterialKey, MakeShared<FWCAEditorTestCacheValue, ESPMode::ThreadSafe>(256));
    FDWCEditorCacheLease TopologyLease =
        Store.FindLease<FWCAEditorTestCacheValue>(TopologyKey);

    Store.InvalidateResourceIdentity(MeshIdentity, TopologyKey.Namespace);
    TestFalse(TEXT("A mesh topology invalidation removes only the topology index."),
        Store.Contains<FWCAEditorTestCacheValue>(TopologyKey));
    TestTrue(TEXT("The projection cache remains reusable until its own contract changes."),
        Store.Contains<FWCAEditorTestCacheValue>(ProjectionKey));
    TestTrue(TEXT("The preview material cache remains independent from topology."),
        Store.Contains<FWCAEditorTestCacheValue>(MaterialKey));
    TestTrue(TEXT("An in-flight topology user keeps the invalidated payload alive."),
        TopologyLease.IsValid());
    TestEqual(TEXT("The leased invalidated entry is tracked as retired."),
        Store.GetRetiredEntryCount(), 1);

    Store.ReclaimUnleasedBytes(MAX_uint64);
    TestEqual(TEXT("Pressure reclaim removes all unleased indexed entries."),
        Store.GetEntryCount(), 0);
    TestEqual(TEXT("Pressure reclaim cannot destroy a leased retired payload."),
        Store.GetRetiredEntryCount(), 1);

    TopologyLease.Reset();
    TestEqual(TEXT("Releasing the final lease retires the old topology payload."),
        Store.GetRetiredEntryCount(), 0);
    TestEqual(TEXT("All WCA cache memory is released after shutdown-style cleanup."),
        Store.GetUsedBytes(), uint64{0});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorLifecycleBlockerCompositionRegressionTest,
    "DWC.Editor.WCA.Lifecycle.BlockerComposition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorLifecycleBlockerCompositionRegressionTest::RunTest(const FString&)
{
    FDWCEditorHostLifecycleReducer Host(EDWCEditorHostLifecycleBlocker::HostUnavailable);
    FDWCEditorPreviewModeLifetime WrinkleLifetime(
        EDWCEditorPreviewMode::Wrinkle,
        FGuid::NewGuid());

    FDWCEditorHostVisibilitySnapshot Visible;
    Visible.bHostAvailable = true;
    Visible.bTabForeground = true;
    Visible.bWindowVisible = true;
    Visible.bWindowActive = true;
    Visible.bApplicationActive = true;
    FDWCEditorHostLifecycleTransition Transition = Host.SetVisibilitySnapshot(Visible);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    const FDWCEditorPreviewRunToken InitialToken = WrinkleLifetime.CaptureToken();
    TestTrue(TEXT("A visible WCA host activates its selected preview mode."),
        InitialToken.IsCurrent());

    Transition = Host.SetBlocker(EDWCEditorHostLifecycleBlocker::TabBackground, true);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    TestFalse(TEXT("Moving the WCA tab to the background invalidates preview work."),
        InitialToken.IsCurrent());

    Transition = Host.SetBlocker(EDWCEditorHostLifecycleBlocker::ExclusiveBuild, true);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    Transition = Host.SetBlocker(EDWCEditorHostLifecycleBlocker::ExclusiveBuild, false);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    TestFalse(TEXT("Ending a Build does not resume while the tab remains hidden."),
        Host.CanRunInteractivePreview());
    TestFalse(TEXT("The selected preview remains suspended behind another blocker."),
        WrinkleLifetime.IsActive());

    Transition = Host.SetBlocker(EDWCEditorHostLifecycleBlocker::TabBackground, false);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    const FDWCEditorPreviewRunToken ResumedToken = WrinkleLifetime.CaptureToken();
    TestTrue(TEXT("Removing the final blocker resumes the selected preview."),
        ResumedToken.IsCurrent());
    TestNotEqual(TEXT("Resume uses a new generation."),
        ResumedToken.Generation, InitialToken.Generation);

    Transition = Host.SetBlocker(EDWCEditorHostLifecycleBlocker::EditorClosing, true);
    ApplyPreviewLifecycleTransition(Transition, WrinkleLifetime);
    TestEqual(TEXT("Closing permanently revokes the preview lifetime."),
        WrinkleLifetime.GetRunState(), EDWCEditorPreviewModeRunState::Revoked);
    TestFalse(TEXT("No preview token survives WCA editor shutdown."),
        ResumedToken.IsCurrent());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAEditorExclusiveBuildBarrierRegressionTest,
    "DWC.Editor.WCA.Build.ExclusiveBarrier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAEditorExclusiveBuildBarrierRegressionTest::RunTest(const FString&)
{
    TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeWCAEditorTestBudget());
    const FGuid SessionId = Broker->OpenSession(TEXT("WCA regression session"));
    bool bBuildBarrierActive = false;
    Broker->SetSessionBuildBarrierHooks(
        SessionId,
        [&bBuildBarrierActive](const bool bActive)
        {
            bBuildBarrierActive = bActive;
        },
        []()
        {
            return false;
        });

    FDWCEditorExclusiveBuildRequest Request;
    Request.SessionId = SessionId;
    Request.AssetPath = TEXT("/DWC/Tests/WCA");
    Request.DebugName = TEXT("WCA regression Build");
    FString Error;
    TUniquePtr<FDWCEditorExclusiveBuildLease> Lease =
        Broker->TryBeginExclusiveBuild(Request, &Error);
    TestTrue(TEXT("A WCA session acquires the exclusive Build scope."),
        Lease.IsValid() && Lease->IsValid());
    TestTrue(TEXT("Acquiring the scope activates the preview barrier."),
        bBuildBarrierActive);
    TestFalse(TEXT("Interactive preview is rejected during exclusive Build."),
        Broker->CanAdmitWork(
            SessionId,
            EDWCEditorWorkClass::InteractivePreview,
            FGuid(),
            &Error));
    TestTrue(TEXT("Work owned by the active Build scope remains admissible."),
        Broker->CanAdmitWork(
            SessionId,
            EDWCEditorWorkClass::ExclusiveBuild,
            Lease->GetScopeId(),
            &Error));

    Lease.Reset();
    TestFalse(TEXT("Releasing the scope clears the preview barrier."),
        bBuildBarrierActive);
    TestTrue(TEXT("Interactive preview is admitted after Build completion."),
        Broker->CanAdmitWork(
            SessionId,
            EDWCEditorWorkClass::InteractivePreview,
            FGuid(),
            &Error));

    Broker->CloseSession(SessionId);
    const FDWCEditorResourceBrokerDiagnostics Diagnostics = Broker->GetDiagnostics();
    TestEqual(TEXT("WCA shutdown closes the broker session."),
        Diagnostics.SessionCount, 0);
    TestEqual(TEXT("WCA shutdown leaves no reclaim participant."),
        Diagnostics.ParticipantCount, 0);
    return true;
}

#endif
