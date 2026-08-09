//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

class FDWCEditorTextureWorkspace;

/**
 * WCA editor-instance commit gate. It validates consumer lifetime and request
 * freshness before transferring a completed preview result into the shared
 * texture workspace and acquiring the consumer lease.
 */
class FDWCEditorPreviewCommitCoordinator final
{
  public:
    explicit FDWCEditorPreviewCommitCoordinator(
        TSharedRef<FDWCEditorTextureWorkspace> InTextureWorkspace,
        FGuid InProducerSessionEpoch = FGuid());

    EDWCEditorPreviewCommitResult CommitBGRA8(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureKey& Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&& Pixels,
        FDWCEditorTextureLease& OutLease,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal);

    EDWCEditorPreviewCommitResult CommitNormalBGRA8(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureKey& Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&& Pixels,
        FDWCEditorNormalRasterSurface&& WorkingSurface,
        FDWCEditorTextureLease& OutLease,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal);

    EDWCEditorPreviewCommitResult CommitBGRA8Regions(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureLease& Lease,
        const FDWCEditorPreviewRegionTarget& Target,
        const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
        FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);

    EDWCEditorPreviewCommitResult CommitG8Regions(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureLease& Lease,
        const FDWCEditorPreviewRegionTarget& Target,
        const TArray<FDWCEditorG8RegionPayload>& Regions,
        FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);

    EDWCEditorPreviewCommitResult CommitNormalRegions(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureLease& Lease,
        const FDWCEditorPreviewRegionTarget& Target,
        const TArray<FDWCEditorNormalRegionPayload>& Regions,
        FDWCEditorPreviewRegionCommitOutcome& OutOutcome,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);

    EDWCEditorPreviewCommitResult CommitInteractiveNormalRegions(
        const FDWCEditorPreviewCommitContext& Context,
        const FDWCEditorTextureLease& Lease,
        const FDWCEditorPreviewRegionTarget& Target,
        TArray<FDWCEditorNormalRegionPayload>&& Regions,
        FDWCEditorPreviewRegionCommitOutcome& OutOutcome);

    void Shutdown();
    bool IsShuttingDown() const { return bShuttingDown; }
    FDWCEditorPreviewCommitDiagnostics GetDiagnostics() const { return Diagnostics; }
    void ResetDiagnosticCounters() { Diagnostics = {}; }

  private:
    EDWCEditorPreviewCommitResult Validate(const FDWCEditorPreviewCommitContext& Context) const;
    EDWCEditorPreviewCommitResult RecordResult(EDWCEditorPreviewCommitResult Result);

    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    FGuid ProducerSessionEpoch;
    bool bShuttingDown = false;
    FDWCEditorPreviewCommitDiagnostics Diagnostics;
};
