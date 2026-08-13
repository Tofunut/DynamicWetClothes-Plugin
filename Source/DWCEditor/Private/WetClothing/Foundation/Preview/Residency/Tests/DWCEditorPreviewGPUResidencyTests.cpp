// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorHostLifecycle.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorPreviewModeLifetime.h"
#include "WetClothing/Foundation/Preview/Residency/DWCEditorPreviewGPUResidencyManager.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewGPUResidencyLifecycleTest,
    "DWC.Editor.Preview.GPUResidency.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewGPUResidencyLifecycleTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    TestEqual(
        TEXT("Wet Part surface textures map to the Wet Part domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::WetPartSurfaceDroplet),
        EDWCEditorPreviewGPUDomain::WetPart);
    TestEqual(
        TEXT("Wrinkle hover maps to the Wrinkle domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::WrinkleHover),
        EDWCEditorPreviewGPUDomain::Wrinkle);
    TestEqual(
        TEXT("Transparency visualization maps to the Transparency domain"),
        FDWCEditorPreviewGPUResidencyManager::GetDomainForPurpose(
            EDWCEditorTexturePurpose::TransparencyVisualization),
        EDWCEditorPreviewGPUDomain::Transparency);

    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(
            UploadQueue,
            16ull * 1024ull * 1024ull,
            16ull * 1024ull * 1024ull);
    FDWCEditorPreviewGPUResidencyManager Manager(UploadQueue, Workspace);

    UTexture2D* Owner = NewObject<UTexture2D>(GetTransientPackage());
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WetPartSurfaceWetness;
    Key.MaterialSlotIndex = 3;
    Key.LayerGuid = FGuid::NewGuid();

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.WorkingSize = Descriptor.Size;
    Descriptor.PixelFormat = PF_R32_FLOAT;
    Descriptor.InitialR32F = 0.25f;

    TArray<float> Pixels;
    Pixels.Init(0.75f, Descriptor.Size.X * Descriptor.Size.Y);
    FDWCEditorTextureLease Lease = Workspace->TransferR32FAndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("R32F preview data acquires a workspace lease"), Lease.IsValid());

    TArray<FDWCEditorTextureGPUResidencyRecord> Records;
    Workspace->GetGPUResidencySnapshot(Records);
    const FDWCEditorTextureGPUResidencyRecord* Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestNotNull(TEXT("The resident texture is visible in diagnostics"), Resident);
    if (Resident != nullptr)
    {
        TestEqual(TEXT("The R32F format is retained"), Resident->PixelFormat, PF_R32_FLOAT);
        TestEqual(TEXT("An active lease prevents retirement"), Resident->ActiveLeaseCount, 1u);
        TestEqual(TEXT("The resource is resident"), Resident->State, EDWCEditorTextureGPUState::Resident);
    }

    Manager.SuspendDomain(EDWCEditorPreviewGPUDomain::WetPart);
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("Suspension cannot retire a texture while a viewport still owns its lease"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::Resident);

    Lease.Reset();
    Manager.SuspendDomain(EDWCEditorPreviewGPUDomain::WetPart);
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("An unleased inactive-domain texture begins retirement"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::Retiring);

    FlushRenderingCommands();
    Manager.Tick();
    Workspace->GetGPUResidencySnapshot(Records);
    Resident = Records.FindByPredicate(
        [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
        {
            return Record.Key == Key;
        });
    TestTrue(
        TEXT("The completed fence leaves a reusable CPU-only workspace entry"),
        Resident != nullptr && Resident->State == EDWCEditorTextureGPUState::CPUOnly);

    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Transparency);
    TestEqual(
        TEXT("The active preview domain is explicit"),
        Manager.GetActiveDomain(),
        EDWCEditorPreviewGPUDomain::Transparency);
    Manager.SuspendAll();
    TestTrue(TEXT("PIE/editor suspension is represented explicitly"), Manager.IsSuspended());
    TestEqual(
        TEXT("Suspending all clears the active domain"),
        Manager.GetActiveDomain(),
        EDWCEditorPreviewGPUDomain::None);

    Manager.Shutdown();
    UploadQueue->Shutdown();
    Workspace->Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewGPUResidencyDeferredPolicyTest,
    "DWC.Editor.Preview.GPUResidency.DeferredPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewGPUResidencyDeferredPolicyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(
            UploadQueue,
            16ull * 1024ull * 1024ull,
            16ull * 1024ull * 1024ull);
    FDWCEditorPreviewResidencyPolicy Policy;
    Policy.HostInactiveGPUGraceSeconds = 1.0;
    Policy.HostInactiveCPUGraceSeconds = 2.0;
    FDWCEditorPreviewGPUResidencyManager Manager(UploadQueue, Workspace, Policy);

    UTexture2D* Owner = NewObject<UTexture2D>(GetTransientPackage());
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WrinkleAccumulated;
    Key.MaterialSlotIndex = 2;

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.WorkingSize = Descriptor.Size;
    Descriptor.PixelFormat = PF_B8G8R8A8;
    Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);

    TArray<FColor> Pixels;
    Pixels.Init(Descriptor.InitialBGRA8, Descriptor.Size.X * Descriptor.Size.Y);
    FDWCEditorTextureLease Lease = Workspace->TransferBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Deferred policy fixture acquires its workspace lease"), Lease.IsValid());
    Lease.Reset();

    Manager.SuspendDomain(
        EDWCEditorPreviewGPUDomain::Wrinkle,
        EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive,
        10.0);
    TestTrue(TEXT("Deferred suspension schedules maintenance"), Manager.HasPendingMaintenance());
    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Wrinkle);
    Manager.Tick(20.0);

    TArray<FDWCEditorTextureGPUResidencyRecord> Records;
    Workspace->GetGPUResidencySnapshot(Records);
    TestTrue(
        TEXT("Resuming before the deadline cancels deferred retirement"),
        Records.ContainsByPredicate(
            [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
            {
                return Record.Key == Key && Record.State == EDWCEditorTextureGPUState::Resident;
            }));

    Manager.SuspendDomain(
        EDWCEditorPreviewGPUDomain::Wrinkle,
        EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive,
        30.0);
    Manager.Tick(30.5);
    Workspace->GetGPUResidencySnapshot(Records);
    TestTrue(
        TEXT("GPU residency remains warm inside the grace period"),
        Records.ContainsByPredicate(
            [&Key](const FDWCEditorTextureGPUResidencyRecord& Record)
            {
                return Record.Key == Key && Record.State == EDWCEditorTextureGPUState::Resident;
            }));

    Manager.Tick(31.0);
    TestTrue(TEXT("The GPU resource begins retirement at its deadline"),
        Workspace->HasRetiringGPUResources());
    Manager.Tick(32.0);
    TestEqual(
        TEXT("The CPU working data is reclaimed at its later deadline"),
        Workspace->GetReclaimableCPUBytesForPurposes(
            MakeArrayView(&Key.Purpose, 1)),
        0ull);

    const FDWCEditorPreviewResidencyDiagnostics Diagnostics = Manager.GetDiagnostics();
    TestEqual(TEXT("Two deferred transitions are recorded"), Diagnostics.DeferredReleaseCount, 2ull);
    TestTrue(TEXT("Deferred CPU reclaim is diagnosed"), Diagnostics.ReclaimedCPUBytes > 0);

    FlushRenderingCommands();
    Manager.Tick(33.0);
    Manager.Shutdown();
    UploadQueue->Shutdown();
    Workspace->Reset();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorHostVisibilityPreviewIntegrationTest,
    "DWC.Editor.Lifecycle.Visibility.HostPreviewIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorHostVisibilityPreviewIntegrationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(
            UploadQueue,
            16ull * 1024ull * 1024ull,
            16ull * 1024ull * 1024ull);
    FDWCEditorPreviewResidencyPolicy Policy;
    Policy.HostInactiveGPUGraceSeconds = 1.0;
    Policy.HostInactiveCPUGraceSeconds = 2.0;
    FDWCEditorPreviewGPUResidencyManager Manager(UploadQueue, Workspace, Policy);

    auto AddPreviewTexture =
        [&Workspace](const EDWCEditorTexturePurpose Purpose, const int32 Slot)
        {
            UTexture2D* Owner = NewObject<UTexture2D>(GetTransientPackage());
            FDWCEditorTextureKey Key;
            Key.Owner = FObjectKey(Owner);
            Key.Purpose = Purpose;
            Key.MaterialSlotIndex = Slot;
            Key.LayerGuid = FGuid::NewGuid();

            FDWCEditorTextureDescriptor Descriptor;
            Descriptor.Size = FIntPoint(4, 4);
            Descriptor.WorkingSize = Descriptor.Size;
            Descriptor.PixelFormat = PF_B8G8R8A8;
            Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);

            TArray<FColor> Pixels;
            Pixels.Init(Descriptor.InitialBGRA8, Descriptor.Size.X * Descriptor.Size.Y);
            FDWCEditorTextureLease Lease = Workspace->TransferBGRA8AndAcquireLease(
                Key,
                Descriptor,
                MoveTemp(Pixels),
                EDWCEditorTextureUploadPriority::Interactive);
            Lease.Reset();
            return Key;
        };

    const FDWCEditorTextureKey WrinkleKey = AddPreviewTexture(
        EDWCEditorTexturePurpose::WrinkleAccumulated, 4);
    const FDWCEditorTextureKey TransparencyKey = AddPreviewTexture(
        EDWCEditorTexturePurpose::TransparencyVisualization, 4);

    FDWCEditorHostLifecycleReducer Lifecycle(
        EDWCEditorHostLifecycleBlocker::HostUnavailable);
    FDWCEditorPreviewModeLifetime WrinkleLifetime(
        EDWCEditorPreviewMode::Wrinkle,
        FGuid::NewGuid());

    FDWCEditorHostVisibilitySnapshot Foreground;
    Foreground.bHostAvailable = true;
    Foreground.bTabForeground = true;
    Foreground.bWindowVisible = true;
    Foreground.bWindowActive = true;
    Foreground.bApplicationActive = true;

    FDWCEditorHostLifecycleTransition Transition = Lifecycle.SetVisibilitySnapshot(Foreground);
    TestTrue(TEXT("The foreground WCA tab becomes interactive"), Transition.bBecameInteractive);
    WrinkleLifetime.Activate(Transition.Current.InteractiveGeneration);
    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Wrinkle);
    const FDWCEditorPreviewRunToken ForegroundToken = WrinkleLifetime.CaptureToken();
    TestTrue(TEXT("Foreground preview work receives a current token"), ForegroundToken.IsCurrent());

    FDWCEditorHostVisibilitySnapshot Background = Foreground;
    Background.bTabForeground = false;
    Transition = Lifecycle.SetVisibilitySnapshot(Background);
    TestTrue(TEXT("Moving the WCA tab to the background suspends preview"),
        Transition.bBecameSuspended);
    TestFalse(TEXT("A background tab uses deferred resource release"),
        RequiresImmediatePreviewResourceRelease(Transition.Current.Blockers));
    WrinkleLifetime.Suspend(Transition.Current.InteractiveGeneration);
    Manager.SuspendAll(
        EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive,
        10.0);
    TestFalse(TEXT("Suspension invalidates the in-flight preview token"),
        ForegroundToken.IsCurrent());
    FDWCEditorPreviewResidencyDiagnostics Diagnostics = Manager.GetDiagnostics();
    TestEqual(TEXT("All preview domains receive a deferred GPU deadline"),
        Diagnostics.PendingGPUReleaseCount, 3);
    TestEqual(TEXT("All preview domains receive a deferred CPU deadline"),
        Diagnostics.PendingCPUReleaseCount, 3);

    Transition = Lifecycle.SetVisibilitySnapshot(Foreground);
    TestTrue(TEXT("Returning before the grace deadline resumes preview"),
        Transition.bBecameInteractive);
    WrinkleLifetime.Activate(Transition.Current.InteractiveGeneration);
    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Wrinkle);
    const FDWCEditorPreviewRunToken ResumedToken = WrinkleLifetime.CaptureToken();
    TestTrue(TEXT("The resumed generation issues a current token"), ResumedToken.IsCurrent());
    TestFalse(TEXT("The old generation never becomes current again"), ForegroundToken.IsCurrent());

    Manager.Tick(11.0);
    TArray<FDWCEditorTextureGPUResidencyRecord> Records;
    Workspace->GetGPUResidencySnapshot(Records);
    TestTrue(TEXT("The resumed Wrinkle domain remains GPU resident"),
        Records.ContainsByPredicate(
            [&WrinkleKey](const FDWCEditorTextureGPUResidencyRecord& Record)
            {
                return Record.Key == WrinkleKey &&
                    Record.State == EDWCEditorTextureGPUState::Resident;
            }));
    TestTrue(TEXT("An inactive Transparency domain leaves resident state"),
        Records.ContainsByPredicate(
            [&TransparencyKey](const FDWCEditorTextureGPUResidencyRecord& Record)
            {
                return Record.Key == TransparencyKey &&
                    Record.State != EDWCEditorTextureGPUState::Resident;
            }));

    Transition = Lifecycle.SetVisibilitySnapshot(Background);
    WrinkleLifetime.Suspend(Transition.Current.InteractiveGeneration);
    Manager.SuspendAll(
        EDWCEditorPreviewResourceReleasePolicy::DeferredHostInactive,
        20.0);
    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::PIE, true);
    TestFalse(TEXT("Adding PIE while already suspended does not suspend twice"),
        Transition.bBecameSuspended);
    TestTrue(TEXT("PIE upgrades the release requirement to immediate"),
        RequiresImmediatePreviewResourceRelease(Transition.Current.Blockers));
    Manager.SuspendAll(EDWCEditorPreviewResourceReleasePolicy::Immediate, 20.1);
    Diagnostics = Manager.GetDiagnostics();
    TestEqual(TEXT("Immediate escalation clears deferred GPU deadlines"),
        Diagnostics.PendingGPUReleaseCount, 0);
    TestEqual(TEXT("Immediate escalation clears deferred CPU deadlines"),
        Diagnostics.PendingCPUReleaseCount, 0);

    Transition = Lifecycle.SetVisibilitySnapshot(Foreground);
    TestFalse(TEXT("Foreground visibility cannot resume while PIE remains"),
        Transition.bBecameInteractive);
    TestFalse(TEXT("PIE keeps interactive preview blocked"),
        Lifecycle.CanRunInteractivePreview());
    Transition = Lifecycle.SetBlocker(EDWCEditorHostLifecycleBlocker::PIE, false);
    TestTrue(TEXT("Removing the final blocker resumes exactly once"),
        Transition.bBecameInteractive);
    WrinkleLifetime.Activate(Transition.Current.InteractiveGeneration);
    Manager.SetActiveDomain(EDWCEditorPreviewGPUDomain::Wrinkle);
    TestTrue(TEXT("The final resumed preview token is current"),
        WrinkleLifetime.CaptureToken().IsCurrent());

    FlushRenderingCommands();
    Manager.Tick(30.0);
    Manager.Shutdown();
    UploadQueue->Shutdown();
    Workspace->Reset();
    return true;
}

#endif
