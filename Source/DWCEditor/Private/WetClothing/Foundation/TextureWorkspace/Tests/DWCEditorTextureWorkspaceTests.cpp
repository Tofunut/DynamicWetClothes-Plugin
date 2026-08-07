//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    FDWCEditorTextureKey MakeTextureKey(
        UTexture2D* Owner,
        const EDWCEditorTexturePurpose Purpose,
        const int32 MaterialSlotIndex)
    {
        FDWCEditorTextureKey Key;
        Key.Owner = FObjectKey(Owner);
        Key.Purpose = Purpose;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        return Key;
    }

    FDWCEditorTextureDescriptor MakeBGRA8Descriptor(const FIntPoint Size)
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = Size;
        Descriptor.PixelFormat = PF_B8G8R8A8;
        Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);
        return Descriptor;
    }

    TArray<FColor> MakeFlatNormalPixels(const FDWCEditorTextureDescriptor& Descriptor)
    {
        TArray<FColor> Pixels;
        Pixels.Init(Descriptor.InitialBGRA8, Descriptor.Size.X * Descriptor.Size.Y);
        return Pixels;
    }

    constexpr uint64 FourKBGRA8Bytes = 4096ull * 4096ull * sizeof(FColor);

    const FDWCEditorPreviewMemoryBucket* FindMemoryBucket(
        const TArray<FDWCEditorPreviewMemoryBucket>& Buckets,
        const TCHAR* Name)
    {
        return Buckets.FindByPredicate(
            [Name](const FDWCEditorPreviewMemoryBucket& Bucket)
            {
                return Bucket.Name == Name;
            });
    }

    bool WaitForGPUResourceRetire(FDWCEditorTextureWorkspace& Workspace)
    {
        FlushRenderingCommands();
        Workspace.ProcessRetiredGPUResources();

        TArray<FDWCEditorPreviewMemoryBucket> Buckets;
        Workspace.AppendDiagnosticMemoryBucket(Buckets);
        const FDWCEditorPreviewMemoryBucket* Retiring = FindMemoryBucket(
            Buckets,
            TEXT("Preview GPU retiring resources"));
        return Retiring != nullptr && Retiring->EntryCount == 0 && Retiring->UsedBytes == 0;
    }

    const FDWCEditorPreviewOperationCounter* FindOperationCounter(
        const TArray<FDWCEditorPreviewOperationCounter>& Counters,
        const TCHAR* Name)
    {
        return Counters.FindByPredicate(
            [Name](const FDWCEditorPreviewOperationCounter& Counter)
            {
                return Counter.Name == Name;
            });
    }

    void ShutdownWorkspaceAfterRenderFence(
        FDWCEditorTextureWorkspace& Workspace,
        const TSharedRef<FDWCEditorRenderUploadQueue>& UploadQueue)
    {
        Workspace.Reset();
        FlushRenderingCommands();
        Workspace.ProcessRetiredGPUResources();
        UploadQueue->Shutdown();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorDirtyRegionSetTest,
    "DWC.Editor.Foundation.TextureWorkspace.DirtyRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorDirtyRegionSetTest::RunTest(const FString&)
{
    FDWCEditorDirtyRegionSet Regions;
    Regions.Add(FIntRect(1, 1, 3, 3), FIntPoint(8, 8), false);
    Regions.Add(FIntRect(3, 1, 5, 3), FIntPoint(8, 8), false);
    TestEqual(TEXT("Touching regions merge"), Regions.GetRegions().Num(), 1);
    TestEqual(TEXT("Merged area"), Regions.GetArea(), 8ull);

    Regions.Reset();
    Regions.Add(FIntRect(-2, -2, 2, 2), FIntPoint(8, 8), true);
    TestEqual(TEXT("Wrapped corner splits into four regions"), Regions.GetRegions().Num(), 4);
    TestEqual(TEXT("Wrapped area is preserved"), Regions.GetArea(), 16ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceReuseTest,
    "DWC.Editor.Foundation.TextureWorkspace.Reuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceReuseTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);

    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WrinkleAccumulated;
    Key.MaterialSlotIndex = 3;

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.PixelFormat = PF_B8G8R8A8;
    Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);

    TArray<FColor> Pixels;
    Pixels.Init(Descriptor.InitialBGRA8, 16);
    const FDWCEditorTextureHandle First = Workspace.PublishBGRA8(
        Key,
        Descriptor,
        MoveTemp(Pixels));
    TestTrue(TEXT("Published entry is valid"), First.IsValid());
    TestNotNull(TEXT("Published entry owns a transient texture"), First.IsValid() ? First->GetTexture() : nullptr);

    const FDWCEditorTextureHandle Reused = Workspace.Acquire(Key, Descriptor);
    TestTrue(TEXT("Same descriptor reuses the entry"), First == Reused);

    FDWCEditorTextureDescriptor ResizedDescriptor = Descriptor;
    ResizedDescriptor.Size = FIntPoint(8, 8);
    const FDWCEditorTextureHandle Resized = Workspace.Acquire(Key, ResizedDescriptor);
    TestTrue(TEXT("Descriptor changes replace the entry"), Resized.IsValid() && Resized != First);

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceLeaseTest,
    "DWC.Editor.Foundation.TextureWorkspace.Lease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceLeaseTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue, 1);

    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WrinkleAccumulated;
    Key.MaterialSlotIndex = 7;

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.PixelFormat = PF_B8G8R8A8;
    Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);

    TArray<FColor> Pixels;
    Pixels.Init(Descriptor.InitialBGRA8, 16);
    FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(Key, Descriptor, MoveTemp(Pixels));
    FDWCEditorTextureLease Lease = Workspace.AcquireLease(Handle);
    const TWeakPtr<FDWCEditorTextureWorkspaceEntry> WeakLeasedEntry = Lease.GetHandle();
    Handle.Reset();

    Workspace.TrimToBudget();
    TestTrue(TEXT("Active lease keeps its preview entry resident"), Lease.IsValid());
    TestEqual(TEXT("Active lease count is tracked"), Lease->GetActiveLeaseCount(), 1u);

    Lease.Reset();
    Workspace.TrimToBudget();
    // GPU retirement is fence-driven, so the old shared entry may remain
    // alive briefly while its transient RHI resource is released. It must no
    // longer be retained as the cache entry for this key, however.
    const FDWCEditorTextureHandle Reacquired = Workspace.Acquire(Key, Descriptor);
    TestTrue(TEXT("Released lease allows a new retained entry to be acquired"),
        Reacquired.IsValid() && Reacquired != WeakLeasedEntry.Pin() &&
        Reacquired->GetActiveLeaseCount() == 0);

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceDiscardLeaseTest,
    "DWC.Editor.Foundation.TextureWorkspace.DiscardLease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceDiscardLeaseTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);

    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureKey Key;
    Key.Owner = FObjectKey(Owner);
    Key.Purpose = EDWCEditorTexturePurpose::WrinkleProcedural;
    Key.MaterialSlotIndex = 8;

    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = FIntPoint(4, 4);
    Descriptor.PixelFormat = PF_B8G8R8A8;
    Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);

    TArray<FColor> Pixels;
    Pixels.Init(Descriptor.InitialBGRA8, 16);
    FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(Key, Descriptor, MoveTemp(Pixels));
    FDWCEditorTextureLease Lease = Workspace.AcquireLease(Handle);
    const TWeakPtr<FDWCEditorTextureWorkspaceEntry> WeakEntry = Lease.GetHandle();
    Handle.Reset();

    Workspace.Discard(Lease);
    TestTrue(TEXT("Discard keeps an actively leased entry valid until its owner releases it"), Lease.IsValid());
    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* WorkspaceBucket = Buckets.FindByPredicate(
        [](const FDWCEditorPreviewMemoryBucket& Bucket)
        {
            return Bucket.Name == TEXT("Editor texture workspace");
        });
    TestNotNull(TEXT("Workspace diagnostics report the discarded entry"), WorkspaceBucket);
    if (WorkspaceBucket != nullptr)
    {
        TestEqual(TEXT("Diagnostics report the active transient lease"), WorkspaceBucket->ActiveLeaseCount, 1);
        TestEqual(TEXT("Diagnostics report the retired transient entry"), WorkspaceBucket->RetiredEntryCount, 1);
    }
    Lease.Reset();
    Workspace.ProcessRetiredGPUResources();
    TestFalse(TEXT("Discarded transient entry no longer has an active lease"),
        WeakEntry.IsValid() && WeakEntry.Pin()->GetActiveLeaseCount() > 0);

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceGPUResidencyDiagnosticsTest,
    "DWC.Editor.Foundation.TextureWorkspace.GPUResidency.Diagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceGPUResidencyDiagnosticsTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));

    const FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleAccumulated, 20),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestTrue(TEXT("Resident diagnostics fixture publishes a texture"), Handle.IsValid() && Handle->IsGPUResident());

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Resident =
        FindMemoryBucket(Buckets, TEXT("Preview GPU resident resources"));
    const FDWCEditorPreviewMemoryBucket* Retiring =
        FindMemoryBucket(Buckets, TEXT("Preview GPU retiring resources"));
    const FDWCEditorPreviewMemoryBucket* CPUOnly =
        FindMemoryBucket(Buckets, TEXT("Preview GPU CPU-only workspace entries"));
    const FDWCEditorPreviewMemoryBucket* HighWater =
        FindMemoryBucket(Buckets, TEXT("Preview GPU residency high-water"));
    TestNotNull(TEXT("Resident GPU diagnostics bucket exists"), Resident);
    TestNotNull(TEXT("Retiring GPU diagnostics bucket exists"), Retiring);
    TestNotNull(TEXT("CPU-only GPU diagnostics bucket exists"), CPUOnly);
    TestNotNull(TEXT("GPU high-water diagnostics bucket exists"), HighWater);
    if (Resident != nullptr && HighWater != nullptr)
    {
        TestEqual(TEXT("One texture is counted as GPU resident"), Resident->EntryCount, 1);
        TestTrue(TEXT("Resident GPU bytes are reported"), Resident->UsedBytes > 0);
        TestTrue(TEXT("High-water is at least current resident bytes"), HighWater->UsedBytes >= Resident->UsedBytes);
    }

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceGPUBudgetRejectTest,
    "DWC.Editor.Foundation.TextureWorkspace.GPUResidency.BudgetReject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceGPUBudgetRejectTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    // A 4x4 BGRA8 resource needs 64 bytes, so this workspace must reject it.
    FDWCEditorTextureWorkspace Workspace(UploadQueue, 1024, 1);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));

    const FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleAccumulated, 21),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestFalse(TEXT("Workspace rejects a texture larger than the GPU budget"), Handle.IsValid());

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Resident =
        FindMemoryBucket(Buckets, TEXT("Preview GPU resident resources"));
    TestNotNull(TEXT("Resident GPU diagnostics bucket exists after a budget rejection"), Resident);
    if (Resident != nullptr)
    {
        TestEqual(TEXT("Rejected resource does not become GPU resident"), Resident->EntryCount, 0);
        TestEqual(TEXT("Rejected resource does not consume GPU bytes"), Resident->UsedBytes, 0ull);
    }

    TArray<FDWCEditorPreviewOperationCounter> Counters;
    Workspace.AppendDiagnosticOperationCounters(Counters);
    const FDWCEditorPreviewOperationCounter* Rejects =
        FindOperationCounter(Counters, TEXT("Preview GPU budget rejects"));
    TestNotNull(TEXT("GPU budget reject counter exists"), Rejects);
    if (Rejects != nullptr)
    {
        TestEqual(TEXT("GPU budget reject is recorded"), Rejects->Count, 1ull);
    }

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceGPURetireDiagnosticsTest,
    "DWC.Editor.Foundation.TextureWorkspace.GPUResidency.RetireLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceGPURetireDiagnosticsTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));
    const FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleProcedural, 22),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    FDWCEditorTextureLease Lease = Workspace.AcquireLease(Handle);
    TestTrue(TEXT("Retire diagnostics fixture acquires its texture"), Lease.IsValid());

    Workspace.Discard(Lease);

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Retiring =
        FindMemoryBucket(Buckets, TEXT("Preview GPU retiring resources"));
    TestNotNull(TEXT("Retiring GPU diagnostics bucket exists"), Retiring);
    if (Retiring != nullptr)
    {
        TestEqual(TEXT("Discarded leased texture enters the retiring GPU state"), Retiring->EntryCount, 1);
        TestTrue(TEXT("Retiring texture bytes remain visible until its fence completes"), Retiring->UsedBytes > 0);
    }

    TArray<FDWCEditorPreviewOperationCounter> Counters;
    Workspace.AppendDiagnosticOperationCounters(Counters);
    const FDWCEditorPreviewOperationCounter* Retires =
        FindOperationCounter(Counters, TEXT("Transient GPU resource retires"));
    TestNotNull(TEXT("GPU retire counter exists"), Retires);
    if (Retires != nullptr)
    {
        TestEqual(TEXT("Discard records one GPU retire request"), Retires->Count, 1ull);
    }

    Lease.Reset();
    Workspace.ProcessRetiredGPUResources();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceGPUHighWaterResetTest,
    "DWC.Editor.Foundation.TextureWorkspace.GPUResidency.HighWaterReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceGPUHighWaterResetTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor SmallDescriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));

    Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleAccumulated, 23),
        SmallDescriptor,
        MakeFlatNormalPixels(SmallDescriptor));
    Workspace.ResetDiagnosticCounters();

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Baseline =
        FindMemoryBucket(Buckets, TEXT("Preview GPU residency high-water"));
    TestNotNull(TEXT("GPU high-water diagnostics bucket exists after reset"), Baseline);
    const uint64 BaselineBytes = Baseline != nullptr ? Baseline->UsedBytes : 0;
    TestTrue(TEXT("Reset uses current resident bytes as the high-water baseline"), BaselineBytes > 0);
    Buckets.Reset();
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* HighWater =
        FindMemoryBucket(Buckets, TEXT("Preview GPU residency high-water"));
    const FDWCEditorPreviewMemoryBucket* Resident =
        FindMemoryBucket(Buckets, TEXT("Preview GPU resident resources"));
    TestNotNull(TEXT("GPU high-water diagnostics remain available after reset"), HighWater);
    TestNotNull(TEXT("GPU resident diagnostics remain available after reset"), Resident);
    if (HighWater != nullptr && Resident != nullptr)
    {
        TestEqual(TEXT("High-water reset baseline matches current resident bytes"), HighWater->UsedBytes, Resident->UsedBytes);
    }

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceFourKPIERetirementTest,
    "DWC.Editor.Regression.Preview.GPU.FourKPIERetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
        EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::RequiresUser)

bool FDWCEditorTextureWorkspaceFourKPIERetirementTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    // This intentionally mirrors the live 4K wrinkle accumulated/procedural
    // layers plus a transparency preview layer without exceeding the normal
    // workspace GPU budget.
    FDWCEditorTextureWorkspace Workspace(
        UploadQueue,
        FourKBGRA8Bytes * 3,
        FourKBGRA8Bytes * 3);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4096, 4096));

    const FDWCEditorTextureHandle Accumulated = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleAccumulated, 30),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    const FDWCEditorTextureHandle Procedural = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleProcedural, 30),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    const FDWCEditorTextureHandle Transparency = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::TransparencyVisualization, 31),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestTrue(TEXT("4K accumulated preview texture is resident"),
        Accumulated.IsValid() && Accumulated->IsGPUResident());
    TestTrue(TEXT("4K procedural preview texture is resident"),
        Procedural.IsValid() && Procedural->IsGPUResident());
    TestTrue(TEXT("4K transparency preview texture is resident"),
        Transparency.IsValid() && Transparency->IsGPUResident());

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Resident = FindMemoryBucket(
        Buckets,
        TEXT("Preview GPU resident resources"));
    TestNotNull(TEXT("4K residency diagnostics are available"), Resident);
    if (Resident != nullptr)
    {
        TestEqual(TEXT("Three 4K preview layers are resident"), Resident->EntryCount, 3);
        TestEqual(TEXT("4K resident bytes match the preview layers"),
            Resident->UsedBytes,
            FourKBGRA8Bytes * 3);
    }

    // Preview viewports own leases. PIE suspension discards their workspace
    // entries, then fence completion is responsible for releasing VRAM.
    FDWCEditorTextureLease AccumulatedLease = Workspace.AcquireLease(Accumulated);
    FDWCEditorTextureLease ProceduralLease = Workspace.AcquireLease(Procedural);
    FDWCEditorTextureLease TransparencyLease = Workspace.AcquireLease(Transparency);
    Workspace.Discard(AccumulatedLease);
    Workspace.Discard(ProceduralLease);
    Workspace.Discard(TransparencyLease);
    AccumulatedLease.Reset();
    ProceduralLease.Reset();
    TransparencyLease.Reset();

    Buckets.Reset();
    Workspace.AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Retiring = FindMemoryBucket(
        Buckets,
        TEXT("Preview GPU retiring resources"));
    TestNotNull(TEXT("PIE suspension exposes retiring preview resources"), Retiring);
    if (Retiring != nullptr)
    {
        TestEqual(TEXT("All 4K preview layers begin GPU retirement"), Retiring->EntryCount, 3);
        TestEqual(TEXT("Retiring bytes remain visible until the render fence completes"),
            Retiring->UsedBytes,
            FourKBGRA8Bytes * 3);
    }

    TestTrue(TEXT("4K preview resources are released after the PIE retire fence"),
        WaitForGPUResourceRetire(Workspace));
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewSessionPIESuspendResumeTest,
    "DWC.Editor.Regression.Preview.PIE.SessionSuspendResume",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewSessionPIESuspendResumeTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FDWCEditorPreviewSession Session;
    FDWCEditorPreviewSessionConfig Config;
    Config.bObserveRelevantObjectChanges = false;
    Session.Initialize(Asset, nullptr, Config);

    TestTrue(TEXT("Preview session initializes for an editor-owned WCA"), Session.IsInitialized());
    Session.Suspend(EDWCEditorPreviewSuspendReason::BeginPIE);
    TestTrue(TEXT("PreBeginPIE preview suspension is idempotently represented by the session"),
        Session.IsSuspended());

    Session.Suspend(EDWCEditorPreviewSuspendReason::BeginPIE);
    TestTrue(TEXT("Repeated PreBeginPIE notifications keep the session suspended"),
        Session.IsSuspended());

    Session.Resume();
    TestFalse(TEXT("Preview session resumes lazily after EndPIE"), Session.IsSuspended());
    Session.Resume();
    TestFalse(TEXT("Repeated EndPIE notifications leave the session resumed"), Session.IsSuspended());
    Session.Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorRenderUploadQueuePriorityDiagnosticsTest,
    "DWC.Editor.Foundation.TextureWorkspace.UploadQueue.PriorityDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorRenderUploadQueuePriorityDiagnosticsTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));

    const FDWCEditorTextureHandle Background = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleAccumulated, 25),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    const FDWCEditorTextureHandle Normal = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleProcedural, 26),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    const FDWCEditorTextureHandle Interactive = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::TransparencyVisualization, 27),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestTrue(TEXT("Upload priority diagnostics fixtures publish textures"),
        Background.IsValid() && Normal.IsValid() && Interactive.IsValid());

    const FIntRect DirtyRect(0, 0, 2, 2);
    Workspace.MarkDirty(Background, DirtyRect, false, EDWCEditorTextureUploadPriority::Background);
    Workspace.MarkDirty(Normal, DirtyRect, false, EDWCEditorTextureUploadPriority::Normal);
    Workspace.MarkDirty(Interactive, DirtyRect, false, EDWCEditorTextureUploadPriority::Interactive);

    TArray<FDWCEditorPreviewOperationCounter> Counters;
    UploadQueue->AppendDiagnosticOperationCounters(Counters);
    const FDWCEditorPreviewOperationCounter* PendingBackground =
        FindOperationCounter(Counters, TEXT("Pending background texture uploads"));
    const FDWCEditorPreviewOperationCounter* PendingNormal =
        FindOperationCounter(Counters, TEXT("Pending normal texture uploads"));
    const FDWCEditorPreviewOperationCounter* PendingInteractive =
        FindOperationCounter(Counters, TEXT("Pending interactive texture uploads"));
    TestNotNull(TEXT("Background pending upload counter exists"), PendingBackground);
    TestNotNull(TEXT("Normal pending upload counter exists"), PendingNormal);
    TestNotNull(TEXT("Interactive pending upload counter exists"), PendingInteractive);
    if (PendingBackground != nullptr && PendingNormal != nullptr && PendingInteractive != nullptr)
    {
        TestEqual(TEXT("One background upload is pending"), PendingBackground->Count, 1ull);
        TestEqual(TEXT("One normal upload is pending"), PendingNormal->Count, 1ull);
        TestEqual(TEXT("One interactive upload is pending"), PendingInteractive->Count, 1ull);
        TestTrue(TEXT("Pending upload byte estimates are reported"),
            PendingBackground->Bytes > 0 && PendingNormal->Bytes > 0 && PendingInteractive->Bytes > 0);
    }

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

#endif
