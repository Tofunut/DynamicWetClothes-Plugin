//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Preview/Recovery/DWCEditorPreviewRecovery.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    FDWCEditorTextureKey MakeRegionKey(
        UTexture2D* Owner,
        const EDWCEditorTexturePurpose Purpose,
        const int32 SlotIndex)
    {
        FDWCEditorTextureKey Key;
        Key.Owner = FObjectKey(Owner);
        Key.Purpose = Purpose;
        Key.MaterialSlotIndex = SlotIndex;
        return Key;
    }

    FDWCEditorTextureDescriptor MakeRegionDescriptor(
        const EPixelFormat PixelFormat,
        const FIntPoint Size = FIntPoint(4, 4))
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = Size;
        Descriptor.PixelFormat = PixelFormat;
        Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);
        return Descriptor;
    }

    FDWCEditorPreviewRegionTarget MakeRegionTarget(
        const FDWCEditorTextureKey& Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        const FDWCEditorTextureLease& Lease)
    {
        FDWCEditorPreviewRegionTarget Target;
        Target.Key = Key;
        Target.Descriptor = Descriptor;
        Target.ExpectedDataRevision = Lease->GetDataRevision();
        Target.ExpectedResourceGeneration = Lease->GetResourceGeneration();
        return Target;
    }

    void ShutdownRegionWorkspace(
        FDWCEditorTextureWorkspace& Workspace,
        const TSharedRef<FDWCEditorRenderUploadQueue>& UploadQueue)
    {
        Workspace.Reset();
        FlushRenderingCommands();
        Workspace.ProcessRetiredGPUResources();
        UploadQueue->Shutdown();
    }

    TArray<FColor> ApplyUploadRegions(
        const TArray<FColor>& PreviousPixels,
        const TArray<FColor>& LatestPixels,
        const FIntPoint Size,
        const TArray<FIntRect>& Regions)
    {
        TArray<FColor> Result = PreviousPixels;
        for (const FIntRect& Rect : Regions)
        {
            for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
            {
                const int32 RowStart = Y * Size.X;
                for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
                {
                    Result[RowStart + X] = LatestPixels[RowStart + X];
                }
            }
        }
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewRegionScalarCommitTest,
    "DWC.Editor.Foundation.PreviewRegion.ScalarCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewRegionScalarCommitTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();

    const FDWCEditorTextureDescriptor BGRA8Descriptor = MakeRegionDescriptor(PF_B8G8R8A8);
    const FDWCEditorTextureKey BGRA8Key = MakeRegionKey(
        Owner,
        EDWCEditorTexturePurpose::WrinkleAccumulated,
        1);
    TArray<FColor> InitialColors;
    InitialColors.Init(BGRA8Descriptor.InitialBGRA8, 16);
    FDWCEditorTextureLease BGRA8Lease = Workspace.TransferBGRA8AndAcquireLease(
        BGRA8Key,
        BGRA8Descriptor,
        MoveTemp(InitialColors));
    TestTrue(TEXT("BGRA8 working texture has an active lease"), BGRA8Lease.IsValid());

    const uint64 InitialDataRevision = BGRA8Lease->GetDataRevision();
    const uint64 InitialContentRevision = BGRA8Lease->GetContentRevision();
    FDWCEditorBGRA8RegionPayload ColorRegion;
    ColorRegion.Rect = FIntRect(1, 1, 3, 3);
    ColorRegion.Pixels.Init(FColor::Red, 4);
    const FDWCEditorPreviewRegionCommitOutcome ColorOutcome = Workspace.CommitBGRA8Regions(
        BGRA8Lease,
        MakeRegionTarget(BGRA8Key, BGRA8Descriptor, BGRA8Lease),
        {ColorRegion});
    TestEqual(TEXT("BGRA8 region commit is applied"), ColorOutcome.Result, EDWCEditorPreviewRegionCommitResult::Applied);
    TestEqual(TEXT("BGRA8 commit advances data once"), BGRA8Lease->GetDataRevision(), InitialDataRevision + 1);
    TestEqual(TEXT("BGRA8 commit advances content once"), BGRA8Lease->GetContentRevision(), InitialContentRevision + 1);
    TestEqual(TEXT("BGRA8 commit reports four pixels"), ColorOutcome.CommittedPixelCount, 4ull);
    TestEqual(TEXT("BGRA8 region changes an interior pixel"), BGRA8Lease->GetBGRA8Pixels()[5], FColor::Red);
    TestEqual(TEXT("BGRA8 region leaves an exterior pixel unchanged"), BGRA8Lease->GetBGRA8Pixels()[0], BGRA8Descriptor.InitialBGRA8);

    const uint64 DataBeforePresentation = BGRA8Lease->GetDataRevision();
    const uint64 ContentBeforePresentation = BGRA8Lease->GetContentRevision();
    Workspace.MarkPresentationDirty(BGRA8Lease, FIntRect(1, 1, 2, 2), false);
    TestEqual(TEXT("Presentation upload preserves persistent data revision"), BGRA8Lease->GetDataRevision(), DataBeforePresentation);
    TestEqual(TEXT("Presentation upload advances content revision"), BGRA8Lease->GetContentRevision(), ContentBeforePresentation + 1);

    const FDWCEditorTextureDescriptor G8Descriptor = MakeRegionDescriptor(PF_G8);
    const FDWCEditorTextureKey G8Key = MakeRegionKey(
        Owner,
        EDWCEditorTexturePurpose::TransparencyVisualization,
        2);
    TArray<uint8> InitialScalars;
    InitialScalars.Init(0, 16);
    FDWCEditorTextureLease G8Lease = Workspace.TransferG8AndAcquireLease(
        G8Key,
        G8Descriptor,
        MoveTemp(InitialScalars));
    FDWCEditorG8RegionPayload ScalarRegion;
    ScalarRegion.Rect = FIntRect(2, 0, 4, 2);
    ScalarRegion.Pixels.Init(200, 4);
    const FDWCEditorPreviewRegionCommitOutcome ScalarOutcome = Workspace.CommitG8Regions(
        G8Lease,
        MakeRegionTarget(G8Key, G8Descriptor, G8Lease),
        {ScalarRegion});
    TestEqual(TEXT("G8 region commit is applied"), ScalarOutcome.Result, EDWCEditorPreviewRegionCommitResult::Applied);
    TestEqual(TEXT("G8 region changes its first pixel"), G8Lease->GetG8Pixels()[2], static_cast<uint8>(200));
    TestEqual(TEXT("G8 region leaves an exterior pixel unchanged"), G8Lease->GetG8Pixels()[0], static_cast<uint8>(0));

    BGRA8Lease.Reset();
    G8Lease.Reset();
    ShutdownRegionWorkspace(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewNormalRegionAtomicityTest,
    "DWC.Editor.Foundation.PreviewRegion.NormalAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewNormalRegionAtomicityTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    FDWCEditorTextureWorkspace Workspace(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorTextureDescriptor Descriptor = MakeRegionDescriptor(PF_B8G8R8A8);
    Descriptor.WorkingSize = FIntPoint(4, 4);
    const FDWCEditorTextureKey Key = MakeRegionKey(
        Owner,
        EDWCEditorTexturePurpose::WrinkleAccumulated,
        3);

    TArray<FColor> InitialPixels;
    InitialPixels.Init(Descriptor.InitialBGRA8, 16);
    FDWCEditorNormalRasterSurface InitialSurface;
    InitialSurface.Initialize(FIntPoint(4, 4), true);
    FDWCEditorTextureLease Lease = Workspace.TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(InitialPixels),
        MoveTemp(InitialSurface));

    FDWCEditorNormalRasterSurface PatchSurface;
    PatchSurface.Initialize(FIntPoint(2, 2), true);
    for (int32 PixelIndex = 0; PixelIndex < 4; ++PixelIndex)
    {
        PatchSurface.SetNormal(PixelIndex, FVector3f(0.5f, 0.0f, 0.8660254f));
        PatchSurface.Coverage[PixelIndex] = 0.75f;
    }
    FDWCEditorNormalRegionPayload Region;
    Region.WorkingRect = FIntRect(1, 1, 3, 3);
    Region.PackedNormalXY = PatchSurface.PackedNormalXY;
    Region.Coverage = PatchSurface.Coverage;
    Region.OutputRect = FIntRect(1, 1, 3, 3);
    Region.EncodedPixels.Init(FColor(128, 191, 238, 255), 4);

    const FDWCEditorPreviewRegionCommitOutcome Applied = Workspace.CommitNormalRegions(
        Lease,
        MakeRegionTarget(Key, Descriptor, Lease),
        {Region});
    TestEqual(TEXT("Normal region atomically commits both surfaces"), Applied.Result, EDWCEditorPreviewRegionCommitResult::Applied);
    TestEqual(TEXT("Normal working data changed"), Lease->GetWorkingNormalSurface().PackedNormalXY[5], Region.PackedNormalXY[0]);
    TestEqual(TEXT("Normal coverage changed"), Lease->GetWorkingNormalSurface().Coverage[5], 0.75f);
    TestEqual(TEXT("Encoded output changed"), Lease->GetBGRA8Pixels()[5], Region.EncodedPixels[0]);

    const TArray<uint32> PackedBeforeReject = Lease->GetWorkingNormalSurface().PackedNormalXY;
    const TArray<float> CoverageBeforeReject = Lease->GetWorkingNormalSurface().Coverage;
    const TArray<FColor> PixelsBeforeReject = Lease->GetBGRA8Pixels();
    const uint64 RevisionBeforeReject = Lease->GetDataRevision();
    FDWCEditorNormalRegionPayload InvalidRegion = Region;
    InvalidRegion.EncodedPixels.SetNum(3);
    const FDWCEditorPreviewRegionCommitOutcome Rejected = Workspace.CommitNormalRegions(
        Lease,
        MakeRegionTarget(Key, Descriptor, Lease),
        {InvalidRegion});
    TestEqual(TEXT("Invalid normal payload is rejected"), Rejected.Result, EDWCEditorPreviewRegionCommitResult::InvalidPayload);
    TestEqual(TEXT("Rejected normal payload preserves revision"), Lease->GetDataRevision(), RevisionBeforeReject);
    TestTrue(TEXT("Rejected normal payload preserves packed normals"), Lease->GetWorkingNormalSurface().PackedNormalXY == PackedBeforeReject);
    TestTrue(TEXT("Rejected normal payload preserves coverage"), Lease->GetWorkingNormalSurface().Coverage == CoverageBeforeReject);
    TestTrue(TEXT("Rejected normal payload preserves output pixels"), Lease->GetBGRA8Pixels() == PixelsBeforeReject);

    Lease.Reset();
    ShutdownRegionWorkspace(Workspace, UploadQueue);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewRegionFreshnessTest,
    "DWC.Editor.Foundation.PreviewRegion.Freshness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewRegionFreshnessTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeRegionDescriptor(PF_B8G8R8A8);
    const FDWCEditorTextureKey Key = MakeRegionKey(
        Owner,
        EDWCEditorTexturePurpose::WrinkleProcedural,
        4);
    TArray<FColor> InitialPixels;
    InitialPixels.Init(Descriptor.InitialBGRA8, 16);
    FDWCEditorTextureLease Lease = Workspace->TransferBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(InitialPixels));
    FDWCEditorBGRA8RegionPayload Region;
    Region.Rect = FIntRect(0, 0, 1, 1);
    Region.Pixels = {FColor::Green};

    FDWCEditorPreviewRegionTarget StaleData = MakeRegionTarget(Key, Descriptor, Lease);
    ++StaleData.ExpectedDataRevision;
    TestEqual(
        TEXT("A stale data revision is rejected explicitly"),
        Workspace->CommitBGRA8Regions(Lease, StaleData, {Region}).Result,
        EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch);

    FDWCEditorPreviewRegionTarget StaleResource = MakeRegionTarget(Key, Descriptor, Lease);
    ++StaleResource.ExpectedResourceGeneration;
    TestEqual(
        TEXT("A stale resource generation is rejected explicitly"),
        Workspace->CommitBGRA8Regions(Lease, StaleResource, {Region}).Result,
        EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch);

    FDWCEditorPreviewRegionTarget WrongDescriptor = MakeRegionTarget(Key, Descriptor, Lease);
    WrongDescriptor.Descriptor.Filter = TF_Nearest;
    TestEqual(
        TEXT("A mismatched descriptor is rejected explicitly"),
        Workspace->CommitBGRA8Regions(Lease, WrongDescriptor, {Region}).Result,
        EDWCEditorPreviewRegionCommitResult::DescriptorMismatch);

    const FGuid SessionEpoch = FGuid::NewGuid();
    FDWCEditorPreviewCommitCoordinator Coordinator(Workspace, SessionEpoch);
    FDWCEditorPreviewConsumerLifetime Lifetime;
    FDWCEditorPreviewCommitContext Context;
    Context.ConsumerToken = Lifetime.CaptureToken();
    Context.ProducerSessionEpoch = SessionEpoch;
    Context.IsCurrent = []() { return true; };
    FDWCEditorPreviewRegionCommitOutcome CoordinatorOutcome;
    const EDWCEditorPreviewCommitResult CoordinatorResult = Coordinator.CommitBGRA8Regions(
        Context,
        Lease,
        MakeRegionTarget(Key, Descriptor, Lease),
        {Region},
        CoordinatorOutcome);
    TestEqual(TEXT("Coordinator forwards an active region commit"), CoordinatorResult, EDWCEditorPreviewCommitResult::Applied);

    Lifetime.Suspend();
    Context.ConsumerToken = Lifetime.CaptureToken();
    TestEqual(
        TEXT("Coordinator rejects a region commit while suspended"),
        Coordinator.CommitBGRA8Regions(
            Context,
            Lease,
            MakeRegionTarget(Key, Descriptor, Lease),
            {Region},
            CoordinatorOutcome),
        EDWCEditorPreviewCommitResult::ConsumerSuspended);

    const FDWCEditorPreviewCommitDiagnostics Diagnostics = Coordinator.GetDiagnostics();
    TestEqual(TEXT("Coordinator counts applied region commits"), Diagnostics.RegionAppliedCount, 1ull);
    TestEqual(TEXT("Coordinator counts rejected region commits"), Diagnostics.RegionRejectedCount, 1ull);

    Workspace->Discard(Lease);
    TestEqual(
        TEXT("A retired entry cannot accept a late region result"),
        Workspace->CommitBGRA8Regions(
            Lease,
            MakeRegionTarget(Key, Descriptor, Lease),
            {Region}).Result,
        EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing);

    Lease.Reset();
    Coordinator.Shutdown();
    Workspace->Reset();
    FlushRenderingCommands();
    Workspace->ProcessRetiredGPUResources();
    UploadQueue->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewRegionMemoryEstimateTest,
    "DWC.Editor.Foundation.PreviewRegion.MemoryEstimate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewRegionMemoryEstimateTest::RunTest(const FString&)
{
    FDWCEditorBGRA8RegionPayload ValidRegion;
    ValidRegion.Rect = FIntRect(0, 0, 2, 2);
    ValidRegion.Pixels.Init(FColor::White, 4);
    FDWCEditorPreviewRegionMemoryEstimate Estimate;
    TestTrue(
        TEXT("Valid BGRA8 region memory can be estimated"),
        FDWCEditorPreviewRegionMemory::TryEstimateBGRA8({ValidRegion}, Estimate));
    TestEqual(TEXT("BGRA8 estimate includes only region bytes"), Estimate.ResultBytes, 16ull);

    ValidRegion.Pixels.SetNum(3);
    TestFalse(
        TEXT("Mismatched region payload is not admitted by the estimator"),
        FDWCEditorPreviewRegionMemory::TryEstimateBGRA8({ValidRegion}, Estimate));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorIncrementalCommitRecoveryLifecycleTest,
    "DWC.Editor.Regression.IncrementalLifecycle.CommitRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorIncrementalCommitRecoveryLifecycleTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorRenderUploadQueue> UploadQueue =
        MakeShared<FDWCEditorRenderUploadQueue>();
    const TSharedRef<FDWCEditorTextureWorkspace> Workspace =
        MakeShared<FDWCEditorTextureWorkspace>(UploadQueue);
    UTexture2D* Owner = NewObject<UTexture2D>();
    const FDWCEditorTextureDescriptor Descriptor = MakeRegionDescriptor(PF_B8G8R8A8);
    const FDWCEditorTextureKey Key = MakeRegionKey(
        Owner,
        EDWCEditorTexturePurpose::TransparencyVisualization,
        7);
    TArray<FColor> InitialPixels;
    InitialPixels.Init(FColor::Black, 16);
    FDWCEditorTextureLease Lease = Workspace->TransferBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(InitialPixels));

    const FGuid SessionEpoch = FGuid::NewGuid();
    FDWCEditorPreviewCommitCoordinator Coordinator(Workspace, SessionEpoch);
    FDWCEditorPreviewConsumerLifetime Lifetime;
    FDWCEditorPreviewRecoveryPolicy Policy;
    Policy.WorkspaceRetryLimit = 2;
    FDWCEditorPreviewRecoveryController Recovery(Policy);

    FDWCEditorBGRA8RegionPayload Region;
    Region.Rect = FIntRect(1, 1, 3, 3);
    Region.Pixels.Init(FColor::Green, 4);
    FDWCEditorPreviewCommitContext Context;
    Context.ConsumerToken = Lifetime.CaptureToken();
    Context.ProducerSessionEpoch = SessionEpoch;
    Context.IsCurrent = []() { return true; };
    FDWCEditorPreviewRegionCommitOutcome Outcome;
    const EDWCEditorPreviewCommitResult Applied = Coordinator.CommitBGRA8Regions(
        Context,
        Lease,
        MakeRegionTarget(Key, Descriptor, Lease),
        {Region},
        Outcome);
    TestEqual(TEXT("The current incremental result commits"), Applied, EDWCEditorPreviewCommitResult::Applied);
    Recovery.MarkIncrementalSucceeded();
    const TArray<FColor> LastKnownGoodPixels = Lease->GetBGRA8Pixels();
    const uint64 LastKnownGoodRevision = Lease->GetDataRevision();

    FDWCEditorPreviewRegionTarget StaleTarget = MakeRegionTarget(Key, Descriptor, Lease);
    ++StaleTarget.ExpectedDataRevision;
    Region.Pixels.Init(FColor::Red, 4);
    const EDWCEditorPreviewCommitResult StaleResult = Coordinator.CommitBGRA8Regions(
        Context,
        Lease,
        StaleTarget,
        {Region},
        Outcome);
    TestEqual(
        TEXT("A stale incremental result is rejected"),
        StaleResult,
        EDWCEditorPreviewCommitResult::DataRevisionMismatch);
    TestEqual(
        TEXT("Rejected incremental data preserves the last-known-good pixels"),
        Lease->GetBGRA8Pixels(),
        LastKnownGoodPixels);
    TestEqual(
        TEXT("Rejected incremental data preserves the data revision"),
        Lease->GetDataRevision(),
        LastKnownGoodRevision);
    TestEqual(
        TEXT("A revision mismatch requests a bounded full rebuild"),
        Recovery.HandleCommitResult(StaleResult, 0.0),
        EDWCEditorPreviewRecoveryAction::RetryFullRebuild);
    TestTrue(TEXT("Recovery records a pending full rebuild"), Recovery.RequiresFullRebuild());

    Lifetime.Suspend();
    Context.ConsumerToken = Lifetime.CaptureToken();
    const EDWCEditorPreviewCommitResult SuspendedResult = Coordinator.CommitBGRA8Regions(
        Context,
        Lease,
        MakeRegionTarget(Key, Descriptor, Lease),
        {Region},
        Outcome);
    TestEqual(
        TEXT("A suspended preview consumer rejects late incremental data"),
        SuspendedResult,
        EDWCEditorPreviewCommitResult::ConsumerSuspended);
    TestEqual(
        TEXT("Suspension also preserves the last-known-good pixels"),
        Lease->GetBGRA8Pixels(),
        LastKnownGoodPixels);

    Lifetime.Resume();
    Recovery.Suspend();
    Recovery.Resume(true);
    TestTrue(TEXT("Resume explicitly requires one full rebuild"), Recovery.RequiresFullRebuild());
    TestTrue(TEXT("The bounded full rebuild can begin"), Recovery.TryBeginFullRebuild(0.0));
    Recovery.MarkSucceeded();
    TestTrue(TEXT("A successful rebuild returns recovery to ready"), Recovery.IsReady());

    Lease.Reset();
    Coordinator.Shutdown();
    Workspace->Reset();
    FlushRenderingCommands();
    Workspace->ProcessRetiredGPUResources();
    UploadQueue->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSparseUploadPolicyTest,
    "DWC.Editor.Foundation.PreviewRegion.SparseUploadPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSparseUploadPolicyTest::RunTest(const FString&)
{
    const TArray<FIntRect> AdjacentTiles = {
        FIntRect(0, 0, 128, 128), FIntRect(128, 0, 256, 128),
        FIntRect(0, 128, 128, 256), FIntRect(128, 128, 256, 256)};
    const FDWCEditorSparseUploadDecision Merged = FDWCEditorSparseUploadPolicy::Choose(
        AdjacentTiles,
        {FIntRect(0, 0, 1024, 1024)},
        FIntPoint(1024, 1024));
    TestEqual(TEXT("Adjacent sparse tiles use one merged upload"),
        Merged.Plan, EDWCEditorSparseUploadPlan::MergedSparse);
    TestEqual(TEXT("Merged sparse upload has one region"), Merged.Regions.Num(), 1);
    TestEqual(TEXT("Merging adjacent tiles adds no pixels"),
        Merged.PlannedPixelCount, Merged.SourcePixelCount);

    FDWCEditorSparseUploadPolicyConfig StrictConfig;
    StrictConfig.MaxRegions = 1;
    const TArray<FIntRect> DistantTiles = {
        FIntRect(0, 0, 64, 64), FIntRect(960, 960, 1024, 1024)};
    const FDWCEditorSparseUploadDecision Bounded = FDWCEditorSparseUploadPolicy::Choose(
        DistantTiles,
        {FIntRect(0, 0, 1024, 1024)},
        FIntPoint(1024, 1024),
        StrictConfig);
    TestEqual(TEXT("An equally expensive sparse merge selects the bounded fallback"),
        Bounded.Plan, EDWCEditorSparseUploadPlan::Bounded);
    TestEqual(TEXT("Bounded fallback remains deterministic"), Bounded.Regions.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSparseUploadPolicyPixelParityTest,
    "DWC.Editor.Regression.HoverUpload.Parity.RegionPlans",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSparseUploadPolicyPixelParityTest::RunTest(const FString&)
{
    const FIntPoint Size(16, 16);
    TArray<FColor> PreviousPixels;
    PreviousPixels.Init(FColor(8, 16, 24, 255), Size.X * Size.Y);
    TArray<FColor> LatestPixels = PreviousPixels;
    const TArray<FIntRect> DirtyRegions = {
        FIntRect(1, 1, 4, 4),
        FIntRect(5, 1, 8, 4),
        FIntRect(11, 11, 15, 15)};
    for (const FIntRect& Rect : DirtyRegions)
    {
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                LatestPixels[Y * Size.X + X] = FColor(X * 11, Y * 13, X + Y, 255);
            }
        }
    }
    const TArray<FIntRect> BoundedRegions = {FIntRect(1, 1, 15, 15)};

    FDWCEditorSparseUploadPolicyConfig SparseConfig;
    SparseConfig.MaxRegions = 8;
    SparseConfig.RegionSubmissionPenaltyPixels = 0;
    const FDWCEditorSparseUploadDecision Sparse = FDWCEditorSparseUploadPolicy::Choose(
        DirtyRegions, BoundedRegions, Size, SparseConfig);
    TestEqual(TEXT("Separated regions can retain a sparse upload plan"),
        Sparse.Plan, EDWCEditorSparseUploadPlan::Sparse);

    const TArray<FIntRect> TouchingRegions = {
        FIntRect(1, 1, 4, 4), FIntRect(4, 1, 8, 4), FIntRect(11, 11, 15, 15)};
    const FDWCEditorSparseUploadDecision Merged = FDWCEditorSparseUploadPolicy::Choose(
        TouchingRegions, BoundedRegions, Size, SparseConfig);
    TestEqual(TEXT("Touching regions select merged sparse presentation"),
        Merged.Plan, EDWCEditorSparseUploadPlan::MergedSparse);

    FDWCEditorSparseUploadPolicyConfig BoundedConfig;
    BoundedConfig.MaxRegions = 1;
    BoundedConfig.RegionSubmissionPenaltyPixels = 4096;
    const FDWCEditorSparseUploadDecision Bounded = FDWCEditorSparseUploadPolicy::Choose(
        DirtyRegions, BoundedRegions, Size, BoundedConfig);
    TestEqual(TEXT("A strict region budget selects bounded presentation"),
        Bounded.Plan, EDWCEditorSparseUploadPlan::Bounded);

    const TArray<FColor> SparseResult = ApplyUploadRegions(
        PreviousPixels, LatestPixels, Size, Sparse.Regions);
    const TArray<FColor> MergedResult = ApplyUploadRegions(
        PreviousPixels, LatestPixels, Size, Merged.Regions);
    const TArray<FColor> BoundedResult = ApplyUploadRegions(
        PreviousPixels, LatestPixels, Size, Bounded.Regions);
    TestEqual(TEXT("Sparse presentation reproduces the latest hover pixels"),
        SparseResult, LatestPixels);
    TestEqual(TEXT("Merged sparse presentation reproduces the latest hover pixels"),
        MergedResult, LatestPixels);
    TestEqual(TEXT("Bounded presentation reproduces the latest hover pixels"),
        BoundedResult, LatestPixels);
    return true;
}

#endif
