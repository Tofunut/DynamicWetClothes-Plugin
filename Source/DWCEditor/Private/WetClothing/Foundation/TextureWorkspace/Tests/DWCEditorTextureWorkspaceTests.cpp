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

    const FDWCEditorResourcePoolDiagnostics* FindGovernorPool(
        const FDWCEditorResourceGovernorDiagnostics& Diagnostics,
        const EDWCEditorResourcePool Pool)
    {
        return Diagnostics.Pools.FindByPredicate(
            [Pool](const FDWCEditorResourcePoolDiagnostics& Candidate)
            {
                return Candidate.Pool == Pool;
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

    FDWCEditorTextureLease MissingLease = Workspace.AcquireExistingLease(Key, Descriptor);
    TestFalse(TEXT("Existing-only acquire does not allocate a cache miss"), MissingLease.IsValid());

    TArray<FColor> Pixels;
    Pixels.Init(Descriptor.InitialBGRA8, 16);
    const FDWCEditorTextureHandle First = Workspace.PublishBGRA8(
        Key,
        Descriptor,
        MoveTemp(Pixels));
    TestTrue(TEXT("Published entry is valid"), First.IsValid());
    TestNotNull(TEXT("Published entry owns a transient texture"), First.IsValid() ? First->GetTexture() : nullptr);

    FDWCEditorTextureLease ExistingLease = Workspace.AcquireExistingLease(Key, Descriptor);
    TestTrue(TEXT("Existing-only acquire leases a published entry"), ExistingLease.IsValid());
    TestTrue(TEXT("Existing-only acquire returns the published entry"),
        ExistingLease.IsValid() && ExistingLease.GetHandle() == First);
    ExistingLease.Reset();

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceDirectNormalInitializationTest,
    "DWC.Editor.Foundation.TextureWorkspace.DirectNormalInitialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceDirectNormalInitializationTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);

    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureKey Key = MakeTextureKey(
        Owner,
        EDWCEditorTexturePurpose::WrinkleHover,
        2);
    FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    Descriptor.WorkingSize = FIntPoint(16, 16);

    FDWCEditorTextureLease Lease = Workspace.InitializeNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        false,
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Direct normal initialization returns a lease"), Lease.IsValid());
    if (Lease.IsValid())
    {
        TestEqual(TEXT("Output pixels are initialized in workspace storage"),
            Lease->GetBGRA8Pixels().Num(), 64);
        TestEqual(TEXT("Working normal pixels are initialized in workspace storage"),
            Lease->GetWorkingNormalSurface().PackedNormalXY.Num(), 256);
        TestFalse(TEXT("Coverage storage is omitted when it is not requested"),
            Lease->GetWorkingNormalSurface().HasCoverage());
        TestEqual(TEXT("Neutral output uses the descriptor color"),
            Lease->GetBGRA8Pixels()[0], Descriptor.InitialBGRA8);
    }

    Lease.Reset();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorRenderUploadQueueSubmissionTicketTest,
    "DWC.Editor.Foundation.TextureWorkspace.UploadQueue.SubmissionTicket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorRenderUploadQueueSubmissionTicketTest::RunTest(const FString&)
{
    constexpr uint64 RowBytes = 8ull * sizeof(FColor);
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(1024ull, RowBytes * 2ull);
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    const FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::WrinkleHover, 28),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestTrue(TEXT("Submission ticket fixture publishes a texture"), Handle.IsValid());
    if (!Handle.IsValid())
    {
        UploadQueue->Shutdown();
        return false;
    }

    Workspace.MarkDirty(Handle, FIntRect(0, 0, 8, 8), false,
        EDWCEditorTextureUploadPriority::Interactive);
    const FDWCEditorTextureUploadTicket Ticket = UploadQueue->CaptureTicket(Handle);
    TestTrue(TEXT("A queued content revision produces a valid ticket"), Ticket.IsValid());
    TestEqual(TEXT("The ticket starts queued"), UploadQueue->GetStatus(Ticket),
        EDWCEditorTextureUploadStatus::Queued);
    FDWCEditorTextureUploadTiming QueuedTiming;
    TestTrue(TEXT("A pending ticket exposes upload timing"),
        UploadQueue->GetTiming(Ticket, QueuedTiming));
    TestTrue(TEXT("The queued timestamp is recorded"), QueuedTiming.QueuedSeconds > 0.0);
    TestFalse(TEXT("The upload has not been selected before flush"), QueuedTiming.WasSelected());
    TestEqual(TEXT("One dirty request is attributed to the ticket"),
        QueuedTiming.RequestedRegionCount, 1u);
    TestTrue(TEXT("A full dirty request is identified"), QueuedTiming.bFullTextureUpload);

    int32 CompletedNotifications = 0;
    FDWCEditorTextureUploadObserverHandle ObserverHandle = UploadQueue->Observe(
        Ticket,
        [&CompletedNotifications](const EDWCEditorTextureUploadStatus Status)
        {
            if (Status == EDWCEditorTextureUploadStatus::Completed)
            {
                ++CompletedNotifications;
            }
        });
    TestTrue(TEXT("A queued ticket accepts a state observer"), ObserverHandle.IsValid());

    const EDWCEditorTextureUploadStatus FirstFlushStatus =
        UploadQueue->TrySubmitInteractive(Ticket, RowBytes * 2ull, 100.0);
    TestEqual(TEXT("A sliced interactive upload is not presented after its first submit"),
        static_cast<uint8>(FirstFlushStatus),
        static_cast<uint8>(EDWCEditorTextureUploadStatus::Queued));
    FDWCEditorTextureUploadTiming FirstFlushTiming;
    TestTrue(TEXT("The first flush retains timing telemetry"),
        UploadQueue->GetTiming(Ticket, FirstFlushTiming));
    TestTrue(TEXT("The first flush records queue selection"), FirstFlushTiming.WasSelected());
    TestTrue(TEXT("Queue selection does not precede enqueue"),
        FirstFlushTiming.SelectedSeconds >= FirstFlushTiming.QueuedSeconds);
    if (FirstFlushTiming.SubmittedRegionCount > 0)
    {
        TestTrue(TEXT("Scheduled byte accounting is bounded by the source texture"),
            FirstFlushTiming.SubmittedBytes > 0 &&
            FirstFlushTiming.SubmittedBytes <= 8ull * 8ull * sizeof(FColor));
    }
    else
    {
        TestEqual(TEXT("A missing RHI resource keeps all upload bytes pending"),
            FirstFlushTiming.SubmittedBytes, 0ull);
        TestEqual(TEXT("A missing RHI resource does not make the ticket presentable"),
            static_cast<uint8>(FirstFlushStatus),
            static_cast<uint8>(EDWCEditorTextureUploadStatus::Queued));
    }
    UploadQueue->Flush();
    UploadQueue->Flush();
    UploadQueue->Flush();
    const EDWCEditorTextureUploadStatus FinalFlushStatus = UploadQueue->GetStatus(Ticket);
    TestTrue(TEXT("Repeated flushes either finish the upload or retain it for a resource retry"),
        FinalFlushStatus == EDWCEditorTextureUploadStatus::RenderEnqueued ||
        FinalFlushStatus == EDWCEditorTextureUploadStatus::Completed ||
        FinalFlushStatus == EDWCEditorTextureUploadStatus::Queued);
    FDWCEditorTextureUploadTiming FinalTiming;
    TestTrue(TEXT("The final flush retains timing telemetry"),
        UploadQueue->GetTiming(Ticket, FinalTiming));
    TestTrue(TEXT("Timing durations never become negative"),
        FinalTiming.QueueWaitMs >= 0.0 && FinalTiming.SliceDelayMs >= 0.0 &&
        FinalTiming.StagingCopyMs >= 0.0 &&
        FinalTiming.SubmitCallMs >= 0.0 && FinalTiming.SubmittedToObservedMs >= 0.0);
    if (FinalFlushStatus == EDWCEditorTextureUploadStatus::RenderEnqueued ||
        FinalFlushStatus == EDWCEditorTextureUploadStatus::Completed)
    {
        TestTrue(TEXT("A render-enqueued ticket records its submit timestamp"), FinalTiming.WasSubmitted());
        TestTrue(TEXT("Submission does not precede selection"),
            FinalTiming.SubmittedSeconds >= FinalTiming.SelectedSeconds);
        TestEqual(TEXT("The complete sliced upload accounts for every source byte"),
            FinalTiming.SubmittedBytes, 8ull * 8ull * sizeof(FColor));
    }

    FlushRenderingCommands();
    UploadQueue->Flush();
    const EDWCEditorTextureUploadStatus CompletedStatus = UploadQueue->GetStatus(Ticket);
    FDWCEditorTextureUploadTiming RenderTiming;
    TestTrue(TEXT("Render callback telemetry remains queryable"),
        UploadQueue->GetTiming(Ticket, RenderTiming));
    if (FinalFlushStatus == EDWCEditorTextureUploadStatus::RenderEnqueued ||
        FinalFlushStatus == EDWCEditorTextureUploadStatus::Completed)
    {
        TestEqual(TEXT("A render callback promotes the upload to completed"),
            CompletedStatus, EDWCEditorTextureUploadStatus::Completed);
        TestEqual(TEXT("Completion notifies the observer exactly once"),
            CompletedNotifications, 1);
        TestEqual(TEXT("Every submitted slice reaches its render cleanup callback"),
            RenderTiming.CompletedRegionCount, RenderTiming.SubmittedRegionCount);
        TestTrue(TEXT("The final render callback timestamp is recorded"),
            RenderTiming.RenderCallbackSeconds > 0.0);
        TestTrue(TEXT("Completed status observation is recorded"), RenderTiming.WasObserved());

        int32 LateObserverNotifications = 0;
        const FDWCEditorTextureUploadObserverHandle LateObserver = UploadQueue->Observe(
            Ticket,
            [&LateObserverNotifications](const EDWCEditorTextureUploadStatus Status)
            {
                if (Status == EDWCEditorTextureUploadStatus::Completed)
                {
                    ++LateObserverNotifications;
                }
            });
        TestFalse(TEXT("A terminal ticket does not retain a late observer"), LateObserver.IsValid());
        TestEqual(TEXT("A late observer sees the terminal state immediately"),
            LateObserverNotifications, 1);
    }

    Workspace.MarkDirty(Handle, FIntRect(0, 0, 8, 8), false,
        EDWCEditorTextureUploadPriority::Interactive);
    const FDWCEditorTextureUploadTicket CanceledTicket = UploadQueue->CaptureTicket(Handle);
    int32 StaleNotifications = 0;
    FDWCEditorTextureUploadObserverHandle CanceledObserver = UploadQueue->Observe(
        CanceledTicket,
        [&StaleNotifications](const EDWCEditorTextureUploadStatus Status)
        {
            if (Status == EDWCEditorTextureUploadStatus::Stale)
            {
                ++StaleNotifications;
            }
        });
    UploadQueue->Cancel(Handle->GetKey());
    TestEqual(TEXT("Canceling an unsent revision makes its ticket stale"),
        UploadQueue->GetStatus(CanceledTicket), EDWCEditorTextureUploadStatus::Stale);
    TestEqual(TEXT("Canceling an unsent revision notifies its observer once"),
        StaleNotifications, 1);
    FDWCEditorTextureUploadTiming CanceledTiming;
    TestTrue(TEXT("Canceled upload timing remains queryable"),
        UploadQueue->GetTiming(CanceledTicket, CanceledTiming));
    TestTrue(TEXT("Canceled upload timing records stale state"),
        CanceledTiming.bStale && CanceledTiming.bCanceled);

    Workspace.MarkDirty(Handle, FIntRect(0, 0, 8, 8), false,
        EDWCEditorTextureUploadPriority::Interactive);
    const FDWCEditorTextureUploadTicket RemovedObserverTicket =
        UploadQueue->CaptureTicket(Handle);
    int32 RemovedObserverNotifications = 0;
    FDWCEditorTextureUploadObserverHandle RemovedObserver = UploadQueue->Observe(
        RemovedObserverTicket,
        [&RemovedObserverNotifications](const EDWCEditorTextureUploadStatus)
        {
            ++RemovedObserverNotifications;
        });
    UploadQueue->RemoveObserver(RemovedObserver);
    TestFalse(TEXT("Removing an observer invalidates its registration handle"),
        RemovedObserver.IsValid());
    UploadQueue->Cancel(Handle->GetKey());
    TestEqual(TEXT("A removed observer is not called when its ticket becomes stale"),
        RemovedObserverNotifications, 0);

    Workspace.MarkDirty(Handle, FIntRect(0, 0, 8, 8), false,
        EDWCEditorTextureUploadPriority::Interactive);
    const FDWCEditorTextureUploadTicket ShutdownTicket = UploadQueue->CaptureTicket(Handle);
    int32 ShutdownStaleNotifications = 0;
    UploadQueue->Observe(
        ShutdownTicket,
        [&ShutdownStaleNotifications](const EDWCEditorTextureUploadStatus Status)
        {
            if (Status == EDWCEditorTextureUploadStatus::Stale)
            {
                ++ShutdownStaleNotifications;
            }
        });
    UploadQueue->Shutdown();
    TestEqual(TEXT("Queue shutdown invalidates and notifies a pending ticket once"),
        ShutdownStaleNotifications, 1);

    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspacePreparedNormalUploadTest,
    "DWC.Editor.Foundation.TextureWorkspace.UploadQueue.PreparedNormalPayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspacePreparedNormalUploadTest::RunTest(const FString&)
{
    constexpr uint64 TwoRowsBytes = 4ull * 2ull * sizeof(FColor);
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(1024ull, TwoRowsBytes);
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();

    FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    Descriptor.WorkingSize = Descriptor.Size;
    const FDWCEditorTextureKey Key = MakeTextureKey(
        Owner,
        EDWCEditorTexturePurpose::WrinkleHover,
        29);
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(Descriptor.WorkingSize, true);
    FDWCEditorTextureLease Lease = Workspace.TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MakeFlatNormalPixels(Descriptor),
        MoveTemp(Surface),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Prepared upload fixture has an active normal texture lease"), Lease.IsValid());
    if (!Lease.IsValid())
    {
        UploadQueue->Shutdown();
        return false;
    }
    FlushRenderingCommands();

    FDWCEditorNormalRegionPayload Region;
    Region.WorkingRect = FIntRect(2, 2, 6, 6);
    Region.OutputRect = Region.WorkingRect;
    Region.PackedNormalXY.Init(0, 16);
    Region.Coverage.Init(0.75f, 16);
    Region.EncodedPixels.Init(FColor(64, 192, 240, 255), 16);
    TArray<FDWCEditorNormalRegionPayload> Regions;
    Regions.Add(MoveTemp(Region));

    FDWCEditorPreviewRegionTarget Target;
    Target.Key = Key;
    Target.Descriptor = Descriptor;
    Target.ExpectedDataRevision = Lease->GetDataRevision();
    Target.ExpectedResourceGeneration = Lease->GetResourceGeneration();
    const FDWCEditorPreviewRegionCommitOutcome Outcome = Workspace.CommitInteractiveNormalRegions(
        Lease,
        Target,
        MoveTemp(Regions));
    TestEqual(TEXT("Interactive normal region commit succeeds"),
        Outcome.Result, EDWCEditorPreviewRegionCommitResult::Applied);
    TestTrue(TEXT("Encoded region storage transfers out of the worker result"),
        Regions.Num() == 1 && Regions[0].EncodedPixels.IsEmpty());
    TestEqual(TEXT("Canonical CPU mirror receives the encoded normal"),
        Lease->GetBGRA8Pixels()[2 + 2 * Descriptor.Size.X], FColor(64, 192, 240, 255));

    FDWCEditorTextureUploadTiming QueuedTiming;
    TestTrue(TEXT("Prepared upload exposes timing telemetry"),
        UploadQueue->GetTiming(Outcome.UploadTicket, QueuedTiming));
    TestTrue(TEXT("Prepared upload fast path is selected"), QueuedTiming.bUsedPreparedPayload);
    TestEqual(TEXT("Prepared payload owns the exact encoded region bytes"),
        QueuedTiming.PreparedPayloadBytes, 16ull * sizeof(FColor));
    TestEqual(TEXT("No legacy staging copy occurs before submission"),
        QueuedTiming.StagingCopyMs, 0.0);
    TArray<FDWCEditorPreviewMemoryBucket> QueuedBuckets;
    UploadQueue->AppendDiagnosticMemoryBucket(QueuedBuckets);
    const FDWCEditorPreviewMemoryBucket* QueuedStaging = FindMemoryBucket(
        QueuedBuckets,
        TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Prepared payload staging diagnostics exist"), QueuedStaging);
    if (QueuedStaging != nullptr)
    {
        TestEqual(TEXT("Prepared payload reserves its physical bytes once"),
            QueuedStaging->UsedBytes, 16ull * sizeof(FColor));
    }

    UploadQueue->TrySubmitInteractive(Outcome.UploadTicket, TwoRowsBytes, 100.0);
    UploadQueue->Flush();
    FlushRenderingCommands();
    FDWCEditorTextureUploadTiming SubmittedTiming;
    TestTrue(TEXT("Prepared upload timing survives render completion"),
        UploadQueue->GetTiming(Outcome.UploadTicket, SubmittedTiming));
    TestEqual(TEXT("Prepared upload never performs a workspace-to-staging copy"),
        SubmittedTiming.StagingCopyMs, 0.0);
    if (SubmittedTiming.WasSubmitted())
    {
        TestEqual(TEXT("Every submitted byte bypasses the legacy staging copy"),
            SubmittedTiming.AvoidedStagingCopyBytes, SubmittedTiming.SubmittedBytes);
    }
    TArray<FDWCEditorPreviewMemoryBucket> CompletedBuckets;
    UploadQueue->AppendDiagnosticMemoryBucket(CompletedBuckets);
    const FDWCEditorPreviewMemoryBucket* CompletedStaging = FindMemoryBucket(
        CompletedBuckets,
        TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Completed prepared payload staging diagnostics exist"), CompletedStaging);
    if (CompletedStaging != nullptr && SubmittedTiming.WasSubmitted())
    {
        TestEqual(TEXT("Render completion releases the prepared payload reservation"),
            CompletedStaging->UsedBytes, 0ull);
    }

    Lease.Reset();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspacePreparedLatestMailboxTest,
    "DWC.Editor.Foundation.TextureWorkspace.UploadQueue.PreparedLatestMailbox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspacePreparedLatestMailboxTest::RunTest(const FString&)
{
    constexpr uint64 PayloadBytes = 4ull * 4ull * sizeof(FColor);
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(PayloadBytes, PayloadBytes);
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    Descriptor.WorkingSize = Descriptor.Size;
    const FDWCEditorTextureKey Key = MakeTextureKey(
        Owner, EDWCEditorTexturePurpose::WrinkleHover, 31);
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(Descriptor.WorkingSize, true);
    FDWCEditorTextureLease Lease = Workspace.TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MakeFlatNormalPixels(Descriptor),
        MoveTemp(Surface),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Latest mailbox fixture owns a texture lease"), Lease.IsValid());

    const auto MakeRegion = [](const FColor Color)
    {
        FDWCEditorNormalRegionPayload Region;
        Region.WorkingRect = FIntRect(2, 2, 6, 6);
        Region.OutputRect = Region.WorkingRect;
        Region.PackedNormalXY.Init(0, 16);
        Region.Coverage.Init(1.0f, 16);
        Region.EncodedPixels.Init(Color, 16);
        TArray<FDWCEditorNormalRegionPayload> Regions;
        Regions.Add(MoveTemp(Region));
        return Regions;
    };
    const auto MakeTarget = [&Key, &Descriptor](const FDWCEditorTextureLease& CurrentLease)
    {
        FDWCEditorPreviewRegionTarget Target;
        Target.Key = Key;
        Target.Descriptor = Descriptor;
        Target.ExpectedDataRevision = CurrentLease->GetDataRevision();
        Target.ExpectedResourceGeneration = CurrentLease->GetResourceGeneration();
        return Target;
    };

    TArray<FDWCEditorNormalRegionPayload> FirstRegions = MakeRegion(FColor::Red);
    const FDWCEditorPreviewRegionCommitOutcome First = Workspace.CommitInteractiveNormalRegions(
        Lease, MakeTarget(Lease), MoveTemp(FirstRegions));
    TArray<FDWCEditorNormalRegionPayload> LatestRegions = MakeRegion(FColor::Green);
    const FDWCEditorPreviewRegionCommitOutcome Latest = Workspace.CommitInteractiveNormalRegions(
        Lease, MakeTarget(Lease), MoveTemp(LatestRegions));

    TestEqual(TEXT("The older unsent prepared payload becomes stale"),
        UploadQueue->GetStatus(First.UploadTicket), EDWCEditorTextureUploadStatus::Stale);
    FDWCEditorTextureUploadTiming LatestTiming;
    TestTrue(TEXT("The latest prepared payload remains queryable"),
        UploadQueue->GetTiming(Latest.UploadTicket, LatestTiming));
    TestTrue(TEXT("Replacing an equally-sized payload stays on the prepared path"),
        LatestTiming.bUsedPreparedPayload);

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    UploadQueue->AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Staging = FindMemoryBucket(
        Buckets, TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Latest mailbox staging diagnostics exist"), Staging);
    if (Staging != nullptr)
    {
        TestEqual(TEXT("Only the latest unsent payload reserves staging memory"),
            Staging->UsedBytes, PayloadBytes);
    }
    TArray<FDWCEditorPreviewOperationCounter> Counters;
    UploadQueue->AppendDiagnosticOperationCounters(Counters);
    const FDWCEditorPreviewOperationCounter* Replacements = FindOperationCounter(
        Counters, TEXT("Superseded prepared upload payloads"));
    TestNotNull(TEXT("Prepared mailbox replacement diagnostics exist"), Replacements);
    if (Replacements != nullptr)
    {
        TestEqual(TEXT("One prepared payload was superseded"), Replacements->Count, 1ull);
        TestEqual(TEXT("Superseded payload bytes are reported"), Replacements->Bytes, PayloadBytes);
    }

    Lease.Reset();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceHoverUploadLatestOnlyWorkTest,
    "DWC.Editor.Regression.HoverUpload.Latency.LatestOnlyWork",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceHoverUploadLatestOnlyWorkTest::RunTest(const FString&)
{
    constexpr int32 RevisionCount = 16;
    constexpr int32 RegionPixelCount = 4 * 4;
    constexpr uint64 PayloadBytes = RegionPixelCount * sizeof(FColor);
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(PayloadBytes, PayloadBytes);
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    Descriptor.WorkingSize = Descriptor.Size;
    const FDWCEditorTextureKey Key = MakeTextureKey(
        Owner, EDWCEditorTexturePurpose::WrinkleHover, 32);
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(Descriptor.WorkingSize, true);
    FDWCEditorTextureLease Lease = Workspace.TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MakeFlatNormalPixels(Descriptor),
        MoveTemp(Surface),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Hover latency fixture owns a texture lease"), Lease.IsValid());
    if (!Lease.IsValid())
    {
        UploadQueue->Shutdown();
        return false;
    }

    FDWCEditorTextureUploadTicket PreviousTicket;
    FDWCEditorTextureUploadTicket LatestTicket;
    for (int32 RevisionIndex = 0; RevisionIndex < RevisionCount; ++RevisionIndex)
    {
        FDWCEditorNormalRegionPayload Region;
        Region.WorkingRect = FIntRect(2, 2, 6, 6);
        Region.OutputRect = Region.WorkingRect;
        Region.PackedNormalXY.Init(0, RegionPixelCount);
        Region.Coverage.Init(1.0f, RegionPixelCount);
        Region.EncodedPixels.Init(FColor(RevisionIndex, 255 - RevisionIndex, 192, 255), RegionPixelCount);
        TArray<FDWCEditorNormalRegionPayload> Regions;
        Regions.Add(MoveTemp(Region));

        FDWCEditorPreviewRegionTarget Target;
        Target.Key = Key;
        Target.Descriptor = Descriptor;
        Target.ExpectedDataRevision = Lease->GetDataRevision();
        Target.ExpectedResourceGeneration = Lease->GetResourceGeneration();
        const FDWCEditorPreviewRegionCommitOutcome Outcome = Workspace.CommitInteractiveNormalRegions(
            Lease,
            Target,
            MoveTemp(Regions));
        TestEqual(TEXT("Every hover revision commits to the CPU mirror"),
            Outcome.Result, EDWCEditorPreviewRegionCommitResult::Applied);
        if (PreviousTicket.IsValid())
        {
            TestEqual(TEXT("Each older unsent hover revision is retired immediately"),
                UploadQueue->GetStatus(PreviousTicket), EDWCEditorTextureUploadStatus::Stale);
        }
        PreviousTicket = Outcome.UploadTicket;
        LatestTicket = Outcome.UploadTicket;
    }

    FDWCEditorTextureUploadTiming Timing;
    TestTrue(TEXT("The latest hover revision keeps timing telemetry"),
        UploadQueue->GetTiming(LatestTicket, Timing));
    TestTrue(TEXT("Repeated hover commits stay on the prepared zero-copy path"),
        Timing.bUsedPreparedPayload);
    TestEqual(TEXT("Repeated hover commits perform no legacy staging copy"),
        Timing.StagingCopyMs, 0.0);
    TestEqual(TEXT("Only one hover region is pending for the latest revision"),
        Timing.RequestedRegionCount, 1u);

    TArray<FDWCEditorPreviewMemoryBucket> Buckets;
    UploadQueue->AppendDiagnosticMemoryBucket(Buckets);
    const FDWCEditorPreviewMemoryBucket* Staging = FindMemoryBucket(
        Buckets, TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Hover staging diagnostics exist"), Staging);
    if (Staging != nullptr)
    {
        TestEqual(TEXT("A hover burst retains only one unsent payload"),
            Staging->UsedBytes, PayloadBytes);
    }

    TArray<FDWCEditorPreviewOperationCounter> Counters;
    UploadQueue->AppendDiagnosticOperationCounters(Counters);
    const FDWCEditorPreviewOperationCounter* Replacements = FindOperationCounter(
        Counters, TEXT("Superseded prepared upload payloads"));
    TestNotNull(TEXT("Hover mailbox replacement diagnostics exist"), Replacements);
    if (Replacements != nullptr)
    {
        TestEqual(TEXT("Every obsolete hover payload is counted once"),
            Replacements->Count, static_cast<uint64>(RevisionCount - 1));
        TestEqual(TEXT("Superseded hover bytes remain bounded and exact"),
            Replacements->Bytes, PayloadBytes * (RevisionCount - 1));
    }

    Lease.Reset();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceHoverUploadReplacementShutdownLifetimeTest,
    "DWC.Editor.Regression.HoverUpload.Lifetime.ReplacementShutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceHoverUploadReplacementShutdownLifetimeTest::RunTest(const FString&)
{
    constexpr int32 RegionPixelCount = 4 * 4;
    constexpr uint64 PayloadBytes = RegionPixelCount * sizeof(FColor);
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(PayloadBytes, PayloadBytes);
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(8, 8));
    Descriptor.WorkingSize = Descriptor.Size;
    const FDWCEditorTextureKey Key = MakeTextureKey(
        Owner, EDWCEditorTexturePurpose::WrinkleHover, 33);
    FDWCEditorNormalRasterSurface Surface;
    Surface.Initialize(Descriptor.WorkingSize, true);
    FDWCEditorTextureLease Lease = Workspace.TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MakeFlatNormalPixels(Descriptor),
        MoveTemp(Surface),
        EDWCEditorTextureUploadPriority::Interactive);
    TestTrue(TEXT("Replacement lifetime fixture owns a texture lease"), Lease.IsValid());
    if (!Lease.IsValid())
    {
        UploadQueue->Shutdown();
        return false;
    }
    const auto CommitColor = [&Workspace, &Lease, &Key, &Descriptor](const FColor Color)
    {
        FDWCEditorNormalRegionPayload Region;
        Region.WorkingRect = FIntRect(2, 2, 6, 6);
        Region.OutputRect = Region.WorkingRect;
        Region.PackedNormalXY.Init(0, RegionPixelCount);
        Region.Coverage.Init(1.0f, RegionPixelCount);
        Region.EncodedPixels.Init(Color, RegionPixelCount);
        TArray<FDWCEditorNormalRegionPayload> Regions;
        Regions.Add(MoveTemp(Region));
        FDWCEditorPreviewRegionTarget Target;
        Target.Key = Key;
        Target.Descriptor = Descriptor;
        Target.ExpectedDataRevision = Lease->GetDataRevision();
        Target.ExpectedResourceGeneration = Lease->GetResourceGeneration();
        return Workspace.CommitInteractiveNormalRegions(Lease, Target, MoveTemp(Regions));
    };

    const FDWCEditorPreviewRegionCommitOutcome First = CommitColor(FColor::Red);
    int32 FirstStaleNotifications = 0;
    UploadQueue->Observe(
        First.UploadTicket,
        [&FirstStaleNotifications](const EDWCEditorTextureUploadStatus Status)
        {
            if (Status == EDWCEditorTextureUploadStatus::Stale)
            {
                ++FirstStaleNotifications;
            }
        });

    const FDWCEditorPreviewRegionCommitOutcome Latest = CommitColor(FColor::Green);
    TestEqual(TEXT("Replacing an unsent hover revision retires its ticket"),
        UploadQueue->GetStatus(First.UploadTicket), EDWCEditorTextureUploadStatus::Stale);
    TestEqual(TEXT("A replaced hover observer receives one terminal notification"),
        FirstStaleNotifications, 1);
    TestEqual(TEXT("The replacement remains queued"),
        UploadQueue->GetStatus(Latest.UploadTicket), EDWCEditorTextureUploadStatus::Queued);

    TArray<FDWCEditorPreviewMemoryBucket> ReplacementBuckets;
    UploadQueue->AppendDiagnosticMemoryBucket(ReplacementBuckets);
    const FDWCEditorPreviewMemoryBucket* ReplacementStaging = FindMemoryBucket(
        ReplacementBuckets, TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Replacement staging diagnostics exist"), ReplacementStaging);
    if (ReplacementStaging != nullptr)
    {
        TestEqual(TEXT("Replacing an unsent hover payload releases it before reserving the latest one"),
            ReplacementStaging->UsedBytes, PayloadBytes);
    }

    int32 LatestStaleNotifications = 0;
    UploadQueue->Observe(
        Latest.UploadTicket,
        [&LatestStaleNotifications](const EDWCEditorTextureUploadStatus Status)
        {
            if (Status == EDWCEditorTextureUploadStatus::Stale)
            {
                ++LatestStaleNotifications;
            }
        });

    UploadQueue->Shutdown();
    TArray<FDWCEditorPreviewMemoryBucket> ShutdownBuckets;
    UploadQueue->AppendDiagnosticMemoryBucket(ShutdownBuckets);
    const FDWCEditorPreviewMemoryBucket* ShutdownStaging = FindMemoryBucket(
        ShutdownBuckets, TEXT("Render upload staging (in-flight)"));
    TestNotNull(TEXT("Shutdown staging diagnostics exist"), ShutdownStaging);
    if (ShutdownStaging != nullptr)
    {
        TestEqual(TEXT("Shutdown releases the final unsent hover payload"),
            ShutdownStaging->UsedBytes, 0ull);
    }
    TestEqual(TEXT("Shutdown makes the latest hover ticket stale"),
        UploadQueue->GetStatus(Latest.UploadTicket), EDWCEditorTextureUploadStatus::Stale);
    TestEqual(TEXT("Shutdown notifies the latest hover observer once"),
        LatestStaleNotifications, 1);

    Lease.Reset();
    ShutdownWorkspaceAfterRenderFence(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorTextureWorkspaceGovernorResidencyLifetimeTest,
    "DWC.Editor.Foundation.TextureWorkspace.ResourceGovernor.ResidencyLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorTextureWorkspaceGovernorResidencyLifetimeTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 1024ull * 1024ull;
    FDWCEditorResourceBudgetConfig Config;
    Config.GlobalEditorCPUBytes = BudgetBytes;
    Config.WorkerPrivateCPUBytes = BudgetBytes;
    Config.PreviewWorkspaceCPUBytes = BudgetBytes;
    Config.SharedCacheCPUBytes = BudgetBytes;
    Config.UploadStagingCPUBytes = BudgetBytes;
    Config.PreviewGPUBytes = BudgetBytes;

    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(Config);
    const FGuid SessionEpoch = FGuid::NewGuid();
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>(
            Governor,
            SessionEpoch,
            BudgetBytes,
            BudgetBytes);
    FDWCEditorTextureWorkspace Workspace(
        UploadQueue,
        Governor,
        SessionEpoch,
        BudgetBytes,
        BudgetBytes);

    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeBGRA8Descriptor(FIntPoint(4, 4));
    FDWCEditorTextureHandle Handle = Workspace.PublishBGRA8(
        MakeTextureKey(Owner, EDWCEditorTexturePurpose::TransparencyVisualization, 40),
        Descriptor,
        MakeFlatNormalPixels(Descriptor));
    TestTrue(TEXT("Governor-backed workspace publishes a GPU resource"),
        Handle.IsValid() && Handle->IsGPUResident());

    FDWCEditorResourceGovernorDiagnostics Diagnostics = Governor->GetDiagnostics();
    const FDWCEditorResourcePoolDiagnostics* WorkspaceCPU = FindGovernorPool(
        Diagnostics,
        EDWCEditorResourcePool::PreviewWorkspaceCPU);
    const FDWCEditorResourcePoolDiagnostics* PreviewGPU = FindGovernorPool(
        Diagnostics,
        EDWCEditorResourcePool::PreviewGPU);
    TestNotNull(TEXT("Workspace CPU pool diagnostics exist"), WorkspaceCPU);
    TestNotNull(TEXT("Preview GPU pool diagnostics exist"), PreviewGPU);
    if (WorkspaceCPU != nullptr && PreviewGPU != nullptr)
    {
        TestTrue(TEXT("Workspace CPU pixels own a reservation"), WorkspaceCPU->UsedBytes > 0);
        TestEqual(TEXT("A 4x4 BGRA8 GPU texture reserves its exact payload"),
            PreviewGPU->UsedBytes, 64ull);
    }

    FDWCEditorTextureLease Lease = Workspace.AcquireLease(Handle);
    TestTrue(TEXT("The published workspace resource can be leased"), Lease.IsValid());
    Workspace.Discard(Lease);
    Handle.Reset();

    Diagnostics = Governor->GetDiagnostics();
    PreviewGPU = FindGovernorPool(Diagnostics, EDWCEditorResourcePool::PreviewGPU);
    TestNotNull(TEXT("GPU diagnostics remain available while retiring"), PreviewGPU);
    if (PreviewGPU != nullptr)
    {
        TestEqual(TEXT("A retiring GPU resource retains its reservation until fence completion"),
            PreviewGPU->UsedBytes, 64ull);
    }

    Lease.Reset();
    UploadQueue->Shutdown();
    FlushRenderingCommands();
    Workspace.ProcessRetiredGPUResources();
    Workspace.Reset();
    FlushRenderingCommands();
    Workspace.ProcessRetiredGPUResources();

    Diagnostics = Governor->GetDiagnostics();
    WorkspaceCPU = FindGovernorPool(Diagnostics, EDWCEditorResourcePool::PreviewWorkspaceCPU);
    PreviewGPU = FindGovernorPool(Diagnostics, EDWCEditorResourcePool::PreviewGPU);
    TestNotNull(TEXT("Workspace CPU diagnostics remain after teardown"), WorkspaceCPU);
    TestNotNull(TEXT("Preview GPU diagnostics remain after teardown"), PreviewGPU);
    if (WorkspaceCPU != nullptr && PreviewGPU != nullptr)
    {
        TestEqual(TEXT("Workspace teardown returns all CPU reservations"), WorkspaceCPU->UsedBytes, 0ull);
        TestEqual(TEXT("Render-fence retirement returns all GPU reservations"), PreviewGPU->UsedBytes, 0ull);
    }
    TestEqual(TEXT("No governor reservation survives workspace teardown"),
        Diagnostics.Reservations.Num(), 0);
    return true;
}

#endif
