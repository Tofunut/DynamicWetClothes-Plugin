// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    FDWCEditorTextureDescriptor MakeCommitTestDescriptor()
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = FIntPoint(4, 4);
        Descriptor.PixelFormat = PF_B8G8R8A8;
        Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);
        return Descriptor;
    }

    TArray<FColor> MakeCommitTestPixels(const FDWCEditorTextureDescriptor& Descriptor)
    {
        TArray<FColor> Pixels;
        Pixels.Init(Descriptor.InitialBGRA8, Descriptor.Size.X * Descriptor.Size.Y);
        return Pixels;
    }
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewCommitLifetimeTest,
    "DWC.Editor.Foundation.PreviewCommit.ConsumerLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewCommitLifetimeTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(UploadQueue);
    const FGuid                        ProducerSessionEpoch = FGuid::NewGuid();
    FDWCEditorPreviewCommitCoordinator Coordinator(Workspace, ProducerSessionEpoch);
    FDWCEditorPreviewConsumerLifetime  Lifetime;

    UTexture2D*          Owner = NewObject<UTexture2D>();
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::TransparencyVisualization;
    Key.MaterialSlotIndex = 2;
    const FDWCEditorTextureDescriptor Descriptor = MakeCommitTestDescriptor();

    FDWCEditorPreviewCommitContext Context;
    Context.ConsumerToken = Lifetime.CaptureToken();
    Context.ProducerSessionEpoch = ProducerSessionEpoch;
    Context.IsCurrent = []()
    { return true; };
    Context.DebugName = TEXT("Commit lifetime test");

    FDWCEditorTextureLease Lease;
    TestEqual(
        TEXT("An active current consumer accepts the result"),
        Coordinator.CommitBGRA8(
            Context,
            Key,
            Descriptor,
            MakeCommitTestPixels(Descriptor),
            Lease),
        EDWCEditorPreviewCommitResult::Applied);
    TestTrue(TEXT("An applied result transfers an active workspace lease"), Lease.IsValid());

    Lifetime.Suspend();
    Context.ConsumerToken = Lifetime.CaptureToken();
    FDWCEditorTextureLease SuspendedLease;
    TestEqual(
        TEXT("A suspended consumer rejects a completed result"),
        Coordinator.CommitBGRA8(
            Context,
            Key,
            Descriptor,
            MakeCommitTestPixels(Descriptor),
            SuspendedLease),
        EDWCEditorPreviewCommitResult::ConsumerSuspended);
    TestFalse(TEXT("A rejected result does not acquire a lease"), SuspendedLease.IsValid());

    Lifetime.Resume();
    Context.ConsumerToken = Lifetime.CaptureToken();
    Context.IsCurrent = []()
    { return false; };
    FDWCEditorTextureLease StaleLease;
    TestEqual(
        TEXT("A stale request cannot replace the current texture"),
        Coordinator.CommitBGRA8(
            Context,
            Key,
            Descriptor,
            MakeCommitTestPixels(Descriptor),
            StaleLease),
        EDWCEditorPreviewCommitResult::StaleRequest);

    Context.IsCurrent = []()
    { return true; };
    Context.ProducerSessionEpoch = FGuid::NewGuid();
    FDWCEditorTextureLease ForeignSessionLease;
    TestEqual(
        TEXT("A result from another scheduler session is rejected"),
        Coordinator.CommitBGRA8(
            Context,
            Key,
            Descriptor,
            MakeCommitTestPixels(Descriptor),
            ForeignSessionLease),
        EDWCEditorPreviewCommitResult::StaleRequest);

    Coordinator.Shutdown();
    Context.ProducerSessionEpoch = ProducerSessionEpoch;
    FDWCEditorTextureLease ShutdownLease;
    TestEqual(
        TEXT("A shutdown coordinator rejects late results"),
        Coordinator.CommitBGRA8(
            Context,
            Key,
            Descriptor,
            MakeCommitTestPixels(Descriptor),
            ShutdownLease),
        EDWCEditorPreviewCommitResult::CoordinatorShutdown);

    const FDWCEditorPreviewCommitDiagnostics Diagnostics = Coordinator.GetDiagnostics();
    TestEqual(TEXT("Applied commits are counted"), Diagnostics.AppliedCount, 1ull);
    TestEqual(TEXT("Stale commits are counted"), Diagnostics.StaleRequestCount, 2ull);
    TestEqual(TEXT("Consumer rejections are counted"), Diagnostics.ConsumerRejectedCount, 1ull);
    TestEqual(TEXT("Shutdown rejections are counted"), Diagnostics.ShutdownRejectedCount, 1ull);

    Lease.Reset();
    Workspace->Reset();
    FlushRenderingCommands();
    Workspace->ProcessRetiredGPUResources();
    UploadQueue->Shutdown();
    return true;
}

#endif
