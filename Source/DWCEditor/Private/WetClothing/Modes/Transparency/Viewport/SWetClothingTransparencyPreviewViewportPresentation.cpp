//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyPreviewUtilities.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MouseDeltaTracker.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneView.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBlueprintHierarchySession.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyAlphaIncrementalWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyRevealColorIncrementalWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyViewportWorkPolicy.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyDirtyTileReplayWorker.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyPreviewViewport"

void SWetClothingTransparencyPreviewViewport::RebuildManualOverridesFromStrokes()
{
    CancelAlphaIncrementalWork(false);
    InvalidatePreviewContent(true);
    ManualAlphaTileStore.Reset();
    if (!AutoBakePreviewResult.IsValid())
    {
        return;
    }
    ManualAlphaTileStore.Initialize(AutoBakePreviewResult->Resolution);
    bManualOverridesRequireWorkerRebuild = true;
    AlphaPreviewRecovery.Invalidate(
        EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
}


void SWetClothingTransparencyPreviewViewport::RefreshManualPreviewFromStrokes()
{
    // Cancellation and external edits discard all provisional tile state,
    // then replay both saved authoring layers in one worker job. This matters
    // for reveal paint because its immediate feedback writes only transient
    // preview pixels, never the persistent auto-bake source.
    CancelAlphaIncrementalWork(false);
    CancelRevealColorIncrementalWork(false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::Alpha, false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::RevealColor, false);
    ManualAlphaTileStore.Reset();
    RevealColorTileStore.Reset();
    if (AutoBakePreviewResult.IsValid())
    {
        ManualAlphaTileStore.Initialize(AutoBakePreviewResult->Resolution);
        RevealColorTileStore.Initialize(AutoBakePreviewResult->Resolution);
    }
    bManualOverridesRequireWorkerRebuild = true;
    bRevealColorRequiresWorkerRebuild = true;
    AlphaPreviewRecovery.Invalidate(
        EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    RevealColorPreviewRecovery.Invalidate(
        EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    bAuthoringWorkerRebuildRequested = false;
    InvalidatePreviewContent(true);
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

bool SWetClothingTransparencyPreviewViewport::CanUseMaterialDrivenPreviewPresentation() const
{
    return AutoBakePreviewResult.IsValid() &&
        !AutoBakePreviewResult->bIsFinalBakedBaseline &&
        GetTransparencyPreviewTexture() != nullptr;
}

bool SWetClothingTransparencyPreviewViewport::UsesFinalAlphaPreview() const
{
    return VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
        VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha;
}

void SWetClothingTransparencyPreviewViewport::RefreshDeferredFinalPreviewBuffers()
{
    if (bOuterEdgeFeatherPreviewDirty && UsesFinalAlphaPreview())
    {
        RebuildOuterEdgeFeatherBuffer();
        bOuterEdgeFeatherPreviewDirty = false;
    }
}

bool SWetClothingTransparencyPreviewViewport::RebuildOuterEdgeFeatherBuffer()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RebuildOuterEdgeFeatherBuffer);
    InvalidatePreviewContent(true);
    ++OuterEdgeFeatherRebuildCount;
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Discard(HoverEdgeFeatherPreviewHandle);
    }
    HoverEdgeFeatherPreviewHandle.Reset();
    OuterEdgeFeatherBuffer.Reset();
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const float FeatherPixels = Asset != nullptr
        ? Asset->Authored.TransparencyData.TransparencyEdgeFeatherPixels
        : 0.0f;
    return FDWCTransparencyComposite::BuildCoverageEdgeFeatherBuffer(
        AutoBakePreviewResult->Resolution,
        AutoBakePreviewResult->OuterCoverageBuffer,
        FeatherPixels,
        OuterEdgeFeatherBuffer);
}

void SWetClothingTransparencyPreviewViewport::InvalidatePreviewContent(
    const bool bRequireFullRebuild)
{
    ++PreviewContentRevision;
    if (bRequireFullRebuild)
    {
        PreviewTextureRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    }
}

void SWetClothingTransparencyPreviewViewport::RetryPreviewTextureRebuildIfNeeded()
{
    const double CurrentTime = FPlatformTime::Seconds();
    const bool bCanStartRequiredRebuild =
        PreviewTextureRecovery.GetState() == EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    if (PendingPreviewTicket.IsValid() || bPreviewSuspended ||
        (!bCanStartRequiredRebuild && !PreviewTextureRecovery.IsRetryDue(CurrentTime)))
    {
        return;
    }

    RebuildTransparencyPreviewTexture();
}

void SWetClothingTransparencyPreviewViewport::FlushPendingPreviewTextureUpdates()
{
    if (bPreviewSuspended)
    {
        return;
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Flush();
    }
}

bool SWetClothingTransparencyPreviewViewport::RebuildTransparencyPreviewTexture()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RebuildTransparencyPreviewTexture);

    if (bPreviewSuspended)
    {
        return false;
    }

    if (!WorkerJobScheduler.IsValid() || !TextureWorkspace.IsValid() || !AutoBakePreviewResult.IsValid())
    {
        ClearMaterialHoverLayer();
        PreviewTextureRecovery.RequestFullRebuild(
            EDWCEditorPreviewInvalidationReason::InvalidPayload);
        return false;
    }

    const uint64 SnapshotContentRevision = PreviewContentRevision;
    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    const FDWCEditorPreviewRunToken PreviewRunToken = PreviewSession
        ? PreviewSession->CaptureRunToken()
        : FDWCEditorPreviewRunToken();
    if (PendingPreviewTicket.IsValid() && PendingPreviewContentRevision == SnapshotContentRevision)
    {
        // Multiple UI refresh requests can target the same immutable state in
        // a frame. The in-flight worker already owns that snapshot.
        return true;
    }
    if (PreviewTextureRecovery.IsReady())
    {
        PreviewTextureRecovery.RequestFullRebuild(
            EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    }
    if (!PreviewTextureRecovery.TryBeginFullRebuild(FPlatformTime::Seconds()))
    {
        return TransparencyPreviewHandle.IsValid();
    }
    const bool bRebuildManualOverrides = bManualOverridesRequireWorkerRebuild;
    const bool bRebuildRevealColor = bRevealColorRequiresWorkerRebuild;
    const TSharedPtr<const FDWCTransparencySourcePayload> AdmissionAutoResult = AutoBakePreviewResult;
    const FWetClothingTransparencyLayerData* AdmissionLayer =
        (bRebuildManualOverrides || bRebuildRevealColor) ? GetSelectedLayer() : nullptr;
    if ((bRebuildManualOverrides || bRebuildRevealColor) && AdmissionLayer == nullptr)
    {
        PreviewTextureRecovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::InvalidPayload,
            FPlatformTime::Seconds());
        return false;
    }

    const FIntPoint Resolution = AdmissionAutoResult->Resolution;
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (PixelCount <= 0)
    {
        PreviewTextureRecovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::InvalidPayload,
            FPlatformTime::Seconds());
        return false;
    }

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
    Descriptor.Key.MaterialSlotIndex = AdmissionAutoResult->MaterialSlotIndex;
    Descriptor.Key.LayerGuid = AdmissionAutoResult->LayerGuid;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Transparency;
    Descriptor.DomainRevision = WorkerJobScheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    static const FDWCTransparencyRevealColorTileStore EmptyRevealColorTileStore;
    static const TArray<FDWCTransparencyRevealColorStroke> EmptyRevealColorStrokes;
    Descriptor.MemoryEstimate = UE::DWCEditor::TransparencyPreview::EstimateVisualizationMemory(
        *AdmissionAutoResult,
        bRebuildRevealColor ? EmptyRevealColorTileStore : RevealColorTileStore,
        bRebuildManualOverrides
            ? UE::DWCEditor::TransparencyPreview::GetStrokeSnapshotBytes(AdmissionLayer->GetEditableStrokes(), EmptyRevealColorStrokes)
            : ManualAlphaTileStore.GetAllocatedBytes(),
        OuterEdgeFeatherBuffer,
        bRebuildRevealColor ? AdmissionLayer->GetRevealColorPaintStrokes() : EmptyRevealColorStrokes,
        bRebuildManualOverrides,
        bRebuildRevealColor);
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency visualization slot %d"),
        Descriptor.Key.MaterialSlotIndex);

    const FGuid ExpectedLayerGuid = Descriptor.Key.LayerGuid;
    const int32 ExpectedSlotIndex = Descriptor.Key.MaterialSlotIndex;
    const int32 ExpectedUVChannelIndex = SelectedUVChannelIndex;
    const EDWCTransparencyVisualizationMode ExpectedVisualizationMode = VisualizationMode;
    const EDWCTransparencyUVAddressMode AddressMode = SelectedUVAddressMode;
    TWeakPtr<SWetClothingTransparencyPreviewViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis,
         ExpectedLayerGuid,
         ExpectedSlotIndex,
         ExpectedUVChannelIndex,
         ExpectedVisualizationMode,
         AddressMode,
         SnapshotContentRevision,
         bRebuildManualOverrides,
         bRebuildRevealColor](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled())
            {
                OutPrepareError = TEXT("The transparency preview request was canceled before its snapshot was prepared.");
                return false;
            }
            if (!Viewport->AutoBakePreviewResult.IsValid() ||
                Viewport->AutoBakePreviewResult->LayerGuid != ExpectedLayerGuid ||
                Viewport->AutoBakePreviewResult->MaterialSlotIndex != ExpectedSlotIndex ||
                Viewport->SelectedMaterialSlotIndex != ExpectedSlotIndex ||
                Viewport->SelectedUVChannelIndex != ExpectedUVChannelIndex ||
                Viewport->SelectedUVAddressMode != AddressMode ||
                Viewport->bManualOverridesRequireWorkerRebuild != bRebuildManualOverrides ||
                Viewport->bRevealColorRequiresWorkerRebuild != bRebuildRevealColor ||
                Viewport->PreviewContentRevision != SnapshotContentRevision)
            {
                OutPrepareError = TEXT("The transparency preview source changed before admission.");
                return false;
            }

            const FWetClothingTransparencyLayerData* Layer =
                (bRebuildManualOverrides || bRebuildRevealColor) ? Viewport->GetSelectedLayer() : nullptr;
            if ((bRebuildManualOverrides || bRebuildRevealColor) && Layer == nullptr)
            {
                OutPrepareError = TEXT("The selected transparency layer is no longer available.");
                return false;
            }

            FDWCTransparencyVisualizationJobInput Input;
            Input.SourcePayload = Viewport->AutoBakePreviewResult;
            if (!bRebuildRevealColor)
            {
                Input.RevealColorTileStore = Viewport->RevealColorTileStore;
            }
            if (!Viewport->BuildAlphaWorkingSnapshot(Input.AlphaSnapshot, OutPrepareError))
            {
                return false;
            }
            Input.OuterEdgeFeatherBuffer = Viewport->OuterEdgeFeatherBuffer;
            Input.VisualizationMode = ExpectedVisualizationMode;
            if (const UWetClothingAsset* Asset = Viewport->WetClothingAsset.Get())
            {
                Input.RevealMetallicDarkeningStrength =
                    Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
            }
            Input.bRebuildRevealColorFromStrokes = bRebuildRevealColor;
            if (Layer != nullptr)
            {
                if (bRebuildRevealColor)
                {
                    Input.RevealColorPaintStrokes = Layer->GetRevealColorPaintStrokes();
                }
                Input.BaselineStrokeCount = Input.SourcePayload->BaselineStrokeCount;
                Input.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
                Input.MaterialSlotIndex = ExpectedSlotIndex;
                Input.UVChannelIndex = ExpectedUVChannelIndex;
            }

            OutPrepared.ActualMemoryEstimate = UE::DWCEditor::TransparencyPreview::EstimateVisualizationMemory(
                *Input.SourcePayload,
                Input.RevealColorTileStore,
                Input.AlphaSnapshot.GetAllocatedBytes(),
                Input.OuterEdgeFeatherBuffer,
                Input.RevealColorPaintStrokes,
                Input.AlphaSnapshot.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay,
                bRebuildRevealColor);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerCancellationToken) mutable
            {
                return FDWCTransparencyVisualizationWorker::Build(
                    MoveTemp(Input),
                    WorkerCancellationToken);
            };
            return true;
        },
        [WeakThis, ExpectedLayerGuid, ExpectedSlotIndex, AddressMode, SnapshotContentRevision,
         CommitToken, PreviewRunToken](
            const FDWCEditorWorkerJobTicket& Ticket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyVisualizationJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }

            const FIntPoint ResultResolution = Result->Resolution;
            const TextureAddress Address = AddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.PreviewRunToken = PreviewRunToken;
            CommitContext.ProducerSessionEpoch = Ticket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Transparency visualization slot %d"),
                ExpectedSlotIndex);
            CommitContext.IsCurrent = [Viewport, ExpectedLayerGuid, ExpectedSlotIndex, SnapshotContentRevision, Ticket]()
            {
                return !Viewport->bPreviewSuspended && Viewport->AutoBakePreviewResult.IsValid() &&
                    Viewport->AutoBakePreviewResult->LayerGuid == ExpectedLayerGuid &&
                    Viewport->AutoBakePreviewResult->MaterialSlotIndex == ExpectedSlotIndex &&
                    Viewport->PendingPreviewTicket.JobId == Ticket.JobId &&
                    Viewport->PendingPreviewTicket.Generation == Ticket.Generation &&
                    Viewport->PreviewContentRevision == SnapshotContentRevision;
            };

            FDWCEditorTextureLease NewLease;
            const EDWCEditorPreviewCommitResult CommitResult =
                Viewport->PreviewCommitCoordinator->CommitBGRA8(
                CommitContext,
                UE::DWCEditor::TransparencyPreview::MakeTextureKey(
                    Viewport->WetClothingAsset.Get(),
                    EDWCEditorTexturePurpose::TransparencyVisualization,
                    ExpectedSlotIndex,
                    ExpectedLayerGuid),
                UE::DWCEditor::TransparencyPreview::MakeColorDescriptor(ResultResolution, Address),
                MoveTemp(Result->Pixels),
                NewLease,
                EDWCEditorTextureUploadPriority::Interactive);

            const bool bOwnsPendingTicket =
                Viewport->PendingPreviewTicket.JobId == Ticket.JobId &&
                Viewport->PendingPreviewTicket.Generation == Ticket.Generation;
            if (!bOwnsPendingTicket)
            {
                return;
            }
            Viewport->PendingPreviewTicket = {};
            Viewport->PendingPreviewContentRevision = 0;

            if (CommitResult != EDWCEditorPreviewCommitResult::Applied)
            {
                const EDWCEditorPreviewRecoveryAction RecoveryAction =
                    Viewport->PreviewTextureRecovery.HandleCommitResult(
                        CommitResult,
                        FPlatformTime::Seconds());
                if (CommitResult == EDWCEditorPreviewCommitResult::StaleRequest)
                {
                    if (Viewport->IsAuthoringInteractionActive() ||
                        !Viewport->PendingRevealColorCommands.IsEmpty() ||
                        Viewport->PendingRevealColorIncrementalTicket.IsValid() ||
                        !Viewport->PendingAlphaCommands.IsEmpty() ||
                        Viewport->PendingAlphaIncrementalTicket.IsValid() ||
                        Viewport->bAuthoringFinishPending)
                    {
                        Viewport->bAuthoringWorkerRebuildRequested = true;
                    }
                }
                else if (RecoveryAction == EDWCEditorPreviewRecoveryAction::Degraded)
                {
                    UE_LOG(
                        LogWetTransparencyPreviewViewport,
                        Warning,
                        TEXT("Transparency preview recovery entered degraded mode for slot %d; keeping the last valid texture."),
                        ExpectedSlotIndex);
                }
                return;
            }
            if (Result->bIncludesMaterializedAlphaSnapshot)
            {
                FDWCTransparencyAlphaTileStore RebuiltStore;
                RebuiltStore.Initialize(Result->MaterializedAlphaSnapshot.Resolution);
                if (!Result->MaterializedAlphaSnapshot.ModifiedTiles.IsEmpty() &&
                    !RebuiltStore.Commit(
                        RebuiltStore.GetRevision(),
                        Result->MaterializedAlphaSnapshot.ModifiedTiles))
                {
                    Viewport->bManualOverridesRequireWorkerRebuild = true;
                    Viewport->AlphaPreviewRecovery.RequestFullRebuild(
                        EDWCEditorPreviewInvalidationReason::InvalidPayload);
                    return;
                }
                Viewport->ManualAlphaTileStore = MoveTemp(RebuiltStore);
                Viewport->bManualOverridesRequireWorkerRebuild = false;
                Viewport->AlphaPreviewRecovery.MarkSucceeded();
            }
            if (Result->bIncludesRebuiltRevealColor)
            {
                Viewport->RevealColorTileStore = MoveTemp(Result->RebuiltRevealColorTileStore);
                Viewport->bRevealColorRequiresWorkerRebuild = false;
                Viewport->RevealColorPreviewRecovery.MarkSucceeded();
            }
            Viewport->TransparencyPreviewHandle = MoveTemp(NewLease);
            Viewport->PreviewTextureRecovery.MarkSucceeded();
            ++Viewport->PreviewTextureRebuildCount;
            Viewport->ApplyTransparencyPreviewParameters();
            Viewport->UpdateMaterialHoverLayer();
            Viewport->InvalidatePreviewViewport();
        },
        &SubmitError,
        [WeakThis](
            const FDWCEditorWorkerJobTicket& Ticket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() ||
                Viewport->PendingPreviewTicket.JobId != Ticket.JobId ||
                Viewport->PendingPreviewTicket.Generation != Ticket.Generation)
            {
                return;
            }
            Viewport->PendingPreviewTicket = {};
            Viewport->PendingPreviewContentRevision = 0;
            if (Completion == EDWCEditorWorkerJobCompletion::Failed && !Error.IsEmpty())
            {
                UE_LOG(
                    LogWetTransparencyPreviewViewport,
                    Warning,
                    TEXT("Transparency preview worker failed: %s"),
                    *Error);
            }
            if (Completion == EDWCEditorWorkerJobCompletion::Failed)
            {
                Viewport->PreviewTextureRecovery.MarkFailure(
                    EDWCEditorPreviewInvalidationReason::WorkerFailed,
                    FPlatformTime::Seconds());
            }
            else
            {
                Viewport->PreviewTextureRecovery.RecordStaleResult();
            }
        });
    PendingPreviewTicket = Ticket;
    PendingPreviewContentRevision = Ticket.IsValid() ? SnapshotContentRevision : 0;
    if (!Ticket.IsValid())
    {
        PreviewTextureRecovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::SchedulerDeferred,
            FPlatformTime::Seconds());
        UE_LOG(
            LogWetTransparencyPreviewViewport,
            Warning,
            TEXT("Transparency visualization job was not submitted for slot %d: %s"),
            ExpectedSlotIndex,
            SubmitError.IsEmpty() ? TEXT("unknown scheduler failure") : *SubmitError);
    }
    return Ticket.IsValid();
}

void SWetClothingTransparencyPreviewViewport::InvalidatePreviewViewport()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }

    Invalidate();
}

USkeletalMeshComponent* SWetClothingTransparencyPreviewViewport::FindFocusMeshComponent() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        if (MeshComponent != nullptr && (Asset == nullptr || MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh()))
        {
            return MeshComponent;
        }
    }

    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        if (MeshComponent != nullptr && MeshComponent->GetSkeletalMeshAsset() != nullptr)
        {
            return MeshComponent;
        }
    }

    return nullptr;
}

void SWetClothingTransparencyPreviewViewport::CollectDiagnosticMemoryStats(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    FDWCEditorPreviewMemoryBucket& SourcePayload = OutBuckets.AddDefaulted_GetRef();
    SourcePayload.Name = TEXT("Transparency auto result");
    if (AutoBakePreviewResult.IsValid())
    {
        SourcePayload.UsedBytes = AutoBakePreviewResult->GetAllocatedBytes();
        SourcePayload.EntryCount = 1;
    }
    SourcePayload.GlobalCategory = EDWCEditorMemoryCategory::PersistentEditorCPU;
    SourcePayload.bIncludeInGlobalSnapshot = true;

    FDWCEditorPreviewMemoryBucket& Working = OutBuckets.AddDefaulted_GetRef();
    Working.Name = TEXT("Transparency working buffers");
    Working.UsedBytes =
        static_cast<uint64>(OuterEdgeFeatherBuffer.GetAllocatedSize()) +
        ManualAlphaTileStore.GetAllocatedBytes() +
        RevealColorTileStore.GetAllocatedBytes();
    Working.EntryCount = 1;
    Working.GlobalCategory = EDWCEditorMemoryCategory::PersistentEditorCPU;
    Working.bIncludeInGlobalSnapshot = true;

    if (LiveStrokeLayer && LiveStrokeLayer->IsActive())
    {
        FDWCEditorPreviewMemoryBucket& LiveStroke = OutBuckets.AddDefaulted_GetRef();
        LiveStroke.Name = TEXT("Transparency live sparse stroke");
        LiveStroke.UsedBytes = LiveStrokeLayer->GetAllocatedBytes();
        LiveStroke.EntryCount = LiveStrokeLayer->GetTileCount();
        LiveStroke.GlobalCategory = EDWCEditorMemoryCategory::OperationPrivateCPU;
        LiveStroke.bIncludeInGlobalSnapshot = true;
    }

    if (!PendingRevealColorCommands.IsEmpty())
    {
        FDWCEditorPreviewMemoryBucket& InteractiveQueue = OutBuckets.AddDefaulted_GetRef();
        InteractiveQueue.Name = TEXT("Transparency reveal-color command queue");
        InteractiveQueue.UsedBytes = PendingRevealColorCommands.GetAllocatedSize();
        for (const FPendingRevealColorCommand& Command : PendingRevealColorCommands)
        {
            InteractiveQueue.UsedBytes += Command.Stroke.GetSampleAllocatedSize();
        }
        InteractiveQueue.EntryCount = PendingRevealColorCommands.Num();
        InteractiveQueue.GlobalCategory = EDWCEditorMemoryCategory::OperationPrivateCPU;
        InteractiveQueue.bIncludeInGlobalSnapshot = true;
    }

    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->AppendDiagnosticMemoryBucket(OutBuckets);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->AppendDiagnosticMemoryBucket(OutBuckets);
    }

    if (SpatialQueryService.IsValid())
    {
        SpatialQueryService->AppendDiagnosticMemoryBucket(OutBuckets);
    }

}

void SWetClothingTransparencyPreviewViewport::CollectDiagnosticOperationStats(
    TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const
{
    OutCounters.Add({TEXT("Transparency preview refreshes"), PreviewRefreshCount, 0});
    OutCounters.Add({TEXT("Transparency preview clears"), PreviewClearCount, 0});
    OutCounters.Add({TEXT("Transparency spatial-cache acquisitions"), HitTrianglePrepareCount, 0});
    OutCounters.Add({TEXT("Transparency full texture rebuilds"), PreviewTextureRebuildCount, 0});
    OutCounters.Add({TEXT("Transparency outer-edge feather rebuilds"), OuterEdgeFeatherRebuildCount, 0});
    OutCounters.Add({TEXT("Transparency hover parameter updates"), HoverParameterUpdateCount, 0});
    OutCounters.Add({TEXT("Transparency hover baseline builds"), HoverBaselineBuildCount, 0});
    OutCounters.Add({TEXT("Transparency hover island-ID resolves"), HoverIslandIDResolveCount, 0});
    OutCounters.Add({TEXT("Transparency hover edge-feather builds"), HoverEdgeFeatherBuildCount, 0});
    OutCounters.Add({
        TEXT("Transparency authoritative stroke replays"),
        InteractivePaintAuthoritativeReplayCount,
        0});
    OutCounters.Add({TEXT("Transparency alpha incremental commits"), AlphaIncrementalCommitCount, 0});
    OutCounters.Add({
        TEXT("Transparency alpha incremental tiles"),
        AlphaIncrementalCommittedTileCount,
        AlphaIncrementalCommittedBytes});
    OutCounters.Add({TEXT("Transparency alpha full-rebuild fallbacks"), AlphaIncrementalFallbackCount, 0});
    OutCounters.Add({TEXT("Transparency reveal-color incremental commits"), RevealColorIncrementalCommitCount, 0});
    OutCounters.Add({
        TEXT("Transparency reveal-color incremental tiles"),
        RevealColorIncrementalCommittedTileCount,
        RevealColorIncrementalCommittedBytes});
    OutCounters.Add({
        TEXT("Transparency reveal-color full-rebuild fallbacks"),
        RevealColorIncrementalFallbackCount,
        0});
    OutCounters.Add({TEXT("Transparency pending-work ticks"), PendingWorkTickCount, 0});
    OutCounters.Add({TEXT("Transparency idle-work tick skips"), IdleWorkTickSkipCount, 0});
    OutCounters.Add({TEXT("Transparency material compilation polls"), MaterialCompilationPollCount, 0});
    OutCounters.Add({TEXT("Transparency interactive paint work ticks"), InteractivePaintWorkCount, 0});
    OutCounters.Add({TEXT("Transparency preview recovery work ticks"), PreviewRecoveryWorkCount, 0});
    OutCounters.Add({TEXT("Transparency upload flush work ticks"), UploadFlushWorkCount, 0});
    const FDWCEditorPreviewRecoveryDiagnostics& PreviewRecoveryDiagnostics =
        PreviewTextureRecovery.GetDiagnostics();
    OutCounters.Add({
        TEXT("Transparency preview recovery retries"),
        PreviewRecoveryDiagnostics.RetryCount,
        0});
    OutCounters.Add({
        TEXT("Transparency preview stale result drops"),
        PreviewRecoveryDiagnostics.StaleDropCount,
        0});
    OutCounters.Add({
        TEXT("Transparency preview degraded transitions"),
        PreviewRecoveryDiagnostics.DegradedCount,
        0});
    if (LiveStrokeLayer && LiveStrokeLayer->IsActive())
    {
        OutCounters.Add({
            TEXT("Transparency live stroke samples"),
            static_cast<uint64>(LiveStrokeLayer->GetSampleCount()),
            static_cast<uint64>(LiveStrokeLayer->GetTileCount())});
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->AppendDiagnosticOperationCounters(OutCounters);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->AppendDiagnosticOperationCounters(OutCounters);
    }
}

void SWetClothingTransparencyPreviewViewport::ResetDiagnosticCounters()
{
    if (SpatialQueryService.IsValid())
    {
        SpatialQueryService->ResetDiagnosticCounters();
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->ResetDiagnosticCounters();
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->ResetDiagnosticCounters();
    }
    PreviewRefreshCount = 0;
    PreviewClearCount = 0;
    HitTrianglePrepareCount = 0;
    PreviewTextureRebuildCount = 0;
    HoverParameterUpdateCount = 0;
    HoverBaselineBuildCount = 0;
    HoverIslandIDResolveCount = 0;
    HoverEdgeFeatherBuildCount = 0;
    OuterEdgeFeatherRebuildCount = 0;
    InteractivePaintAuthoritativeReplayCount = 0;
    AlphaIncrementalCommitCount = 0;
    AlphaIncrementalCommittedTileCount = 0;
    AlphaIncrementalCommittedBytes = 0;
    AlphaIncrementalFallbackCount = 0;
    RevealColorIncrementalCommitCount = 0;
    RevealColorIncrementalCommittedTileCount = 0;
    RevealColorIncrementalCommittedBytes = 0;
    RevealColorIncrementalFallbackCount = 0;
    PendingWorkTickCount = 0;
    IdleWorkTickSkipCount = 0;
    MaterialCompilationPollCount = 0;
    InteractivePaintWorkCount = 0;
    PreviewRecoveryWorkCount = 0;
    UploadFlushWorkCount = 0;
    PreviewTextureRecovery.ResetDiagnostics();
    AlphaPreviewRecovery.ResetDiagnostics();
    RevealColorPreviewRecovery.ResetDiagnostics();
}


#undef LOCTEXT_NAMESPACE

