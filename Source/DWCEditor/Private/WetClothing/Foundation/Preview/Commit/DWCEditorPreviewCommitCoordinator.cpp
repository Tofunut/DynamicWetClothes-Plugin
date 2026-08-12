//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"

#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    EDWCEditorPreviewCommitResult MapRegionCommitResult(
        const EDWCEditorPreviewRegionCommitResult Result)
    {
        switch (Result)
        {
        case EDWCEditorPreviewRegionCommitResult::Applied:
            return EDWCEditorPreviewCommitResult::Applied;
        case EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing:
            return EDWCEditorPreviewCommitResult::WorkspaceEntryMissing;
        case EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch:
            return EDWCEditorPreviewCommitResult::DataRevisionMismatch;
        case EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch:
            return EDWCEditorPreviewCommitResult::ResourceGenerationMismatch;
        case EDWCEditorPreviewRegionCommitResult::DescriptorMismatch:
            return EDWCEditorPreviewCommitResult::DescriptorMismatch;
        case EDWCEditorPreviewRegionCommitResult::InvalidPayload:
            return EDWCEditorPreviewCommitResult::InvalidPayload;
        default:
            return EDWCEditorPreviewCommitResult::WorkspaceRejected;
        }
    }
}

FDWCEditorPreviewCommitCoordinator::FDWCEditorPreviewCommitCoordinator(
    TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace,
    FGuid InProducerSessionEpoch)
    : TextureWorkspace(InTextureWorkspace)
    , ProducerSessionEpoch(InProducerSessionEpoch.IsValid() ? InProducerSessionEpoch : FGuid::NewGuid())
{
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::RecordResult(
    const EDWCEditorPreviewCommitResult Result)
{
    switch (Result)
    {
    case EDWCEditorPreviewCommitResult::Applied: ++Diagnostics.AppliedCount; break;
    case EDWCEditorPreviewCommitResult::StaleRequest: ++Diagnostics.StaleRequestCount; break;
    case EDWCEditorPreviewCommitResult::ConsumerExpired:
    case EDWCEditorPreviewCommitResult::ConsumerSuspended:
        ++Diagnostics.ConsumerRejectedCount;
        break;
    case EDWCEditorPreviewCommitResult::WorkspaceRejected: ++Diagnostics.WorkspaceRejectedCount; break;
    case EDWCEditorPreviewCommitResult::WorkspaceEntryMissing:
        ++Diagnostics.WorkspaceEntryMissingCount;
        break;
    case EDWCEditorPreviewCommitResult::DataRevisionMismatch:
        ++Diagnostics.DataRevisionMismatchCount;
        break;
    case EDWCEditorPreviewCommitResult::ResourceGenerationMismatch:
        ++Diagnostics.ResourceGenerationMismatchCount;
        break;
    case EDWCEditorPreviewCommitResult::DescriptorMismatch:
        ++Diagnostics.DescriptorMismatchCount;
        break;
    case EDWCEditorPreviewCommitResult::InvalidPayload:
        ++Diagnostics.InvalidPayloadCount;
        break;
    case EDWCEditorPreviewCommitResult::CoordinatorShutdown: ++Diagnostics.ShutdownRejectedCount; break;
    default: break;
    }
    return Result;
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::Validate(
    const FDWCEditorPreviewCommitContext& Context) const
{
    check(IsInGameThread());
    if (bShuttingDown || !TextureWorkspace.IsValid())
    {
        return EDWCEditorPreviewCommitResult::CoordinatorShutdown;
    }
    if (Context.ProducerSessionEpoch.IsValid() &&
        Context.ProducerSessionEpoch != ProducerSessionEpoch)
    {
        return EDWCEditorPreviewCommitResult::StaleRequest;
    }

    const TSharedPtr<FDWCEditorPreviewConsumerLifetimeState> ConsumerState = Context.ConsumerToken.State.Pin();
    if (!ConsumerState.IsValid() ||
        ConsumerState->ConsumerEpoch != Context.ConsumerToken.ConsumerEpoch ||
        ConsumerState->Generation != Context.ConsumerToken.Generation ||
        ConsumerState->State == EDWCEditorPreviewConsumerState::Revoked)
    {
        return EDWCEditorPreviewCommitResult::ConsumerExpired;
    }
    if (ConsumerState->State == EDWCEditorPreviewConsumerState::Suspended)
    {
        return EDWCEditorPreviewCommitResult::ConsumerSuspended;
    }
    if (Context.IsCurrent && !Context.IsCurrent())
    {
        return EDWCEditorPreviewCommitResult::StaleRequest;
    }
    return EDWCEditorPreviewCommitResult::Applied;
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitBGRA8(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    FDWCEditorTextureLease& OutLease,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutLease.Reset();
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        return RecordResult(Validation);
    }

    OutLease = TextureWorkspace->TransferBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        Priority);
    return RecordResult(OutLease.IsValid()
        ? EDWCEditorPreviewCommitResult::Applied
        : EDWCEditorPreviewCommitResult::WorkspaceRejected);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitNormalBGRA8(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    FDWCEditorNormalRasterSurface&& WorkingSurface,
    FDWCEditorTextureLease& OutLease,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutLease.Reset();
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        return RecordResult(Validation);
    }

    OutLease = TextureWorkspace->TransferNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        MoveTemp(WorkingSurface),
        Priority);
    return RecordResult(OutLease.IsValid()
        ? EDWCEditorPreviewCommitResult::Applied
        : EDWCEditorPreviewCommitResult::WorkspaceRejected);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::InitializeNormalBGRA8(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    const bool bWithCoverage,
    FDWCEditorTextureLease& OutLease,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutLease.Reset();
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        return RecordResult(Validation);
    }

    OutLease = TextureWorkspace->InitializeNormalBGRA8AndAcquireLease(
        Key,
        Descriptor,
        bWithCoverage,
        Priority);
    return RecordResult(OutLease.IsValid()
        ? EDWCEditorPreviewCommitResult::Applied
        : EDWCEditorPreviewCommitResult::WorkspaceRejected);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitBGRA8Regions(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
    FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutOutcome = {};
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionRejectedCount;
        return RecordResult(Validation);
    }
    OutOutcome = TextureWorkspace->CommitBGRA8Regions(Lease, Target, Regions, Priority);
    const EDWCEditorPreviewCommitResult Result = MapRegionCommitResult(OutOutcome.Result);
    if (Result == EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionAppliedCount;
    }
    else
    {
        ++Diagnostics.RegionRejectedCount;
    }
    return RecordResult(Result);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitG8Regions(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorG8RegionPayload>& Regions,
    FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutOutcome = {};
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionRejectedCount;
        return RecordResult(Validation);
    }
    OutOutcome = TextureWorkspace->CommitG8Regions(Lease, Target, Regions, Priority);
    const EDWCEditorPreviewCommitResult Result = MapRegionCommitResult(OutOutcome.Result);
    if (Result == EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionAppliedCount;
    }
    else
    {
        ++Diagnostics.RegionRejectedCount;
    }
    return RecordResult(Result);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitNormalRegions(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorNormalRegionPayload>& Regions,
    FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
    const EDWCEditorTextureUploadPriority Priority)
{
    OutOutcome = {};
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionRejectedCount;
        return RecordResult(Validation);
    }
    OutOutcome = TextureWorkspace->CommitNormalRegions(Lease, Target, Regions, Priority);
    const EDWCEditorPreviewCommitResult Result = MapRegionCommitResult(OutOutcome.Result);
    if (Result == EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionAppliedCount;
    }
    else
    {
        ++Diagnostics.RegionRejectedCount;
    }
    return RecordResult(Result);
}

EDWCEditorPreviewCommitResult FDWCEditorPreviewCommitCoordinator::CommitInteractiveNormalRegions(
    const FDWCEditorPreviewCommitContext& Context,
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    TArray<FDWCEditorNormalRegionPayload>&& Regions,
    FDWCEditorPreviewRegionCommitOutcome& OutOutcome)
{
    OutOutcome = {};
    const EDWCEditorPreviewCommitResult Validation = Validate(Context);
    if (Validation != EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionRejectedCount;
        return RecordResult(Validation);
    }
    OutOutcome = TextureWorkspace->CommitInteractiveNormalRegions(
        Lease,
        Target,
        MoveTemp(Regions));
    const EDWCEditorPreviewCommitResult Result = MapRegionCommitResult(OutOutcome.Result);
    if (Result == EDWCEditorPreviewCommitResult::Applied)
    {
        ++Diagnostics.RegionAppliedCount;
    }
    else
    {
        ++Diagnostics.RegionRejectedCount;
    }
    return RecordResult(Result);
}

void FDWCEditorPreviewCommitCoordinator::Shutdown()
{
    check(IsInGameThread());
    bShuttingDown = true;
    TextureWorkspace.Reset();
}
