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

FWetClothingTransparencyLayerData* SWetClothingTransparencyPreviewViewport::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer)
        {
            return Layer.LayerGuid == SelectedLayerGuid;
        }) : nullptr;
}

const FWetClothingTransparencyLayerData* SWetClothingTransparencyPreviewViewport::GetSelectedLayer() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer)
        {
            return Layer.LayerGuid == SelectedLayerGuid;
        }) : nullptr;
}

bool SWetClothingTransparencyPreviewViewport::CanPaint() const
{
    const bool bCanRevealPaint = bRevealColorPaintingEnabled && PaintSettings.bRevealColorPaint &&
        SelectedMaterialSlotIndex != INDEX_NONE;
    const bool bCanAlphaPaint = bTransparencyPaintingEnabled && !PaintSettings.bRevealColorPaint;
    const bool bSupportsCurrentVisualization = bCanRevealPaint
        ? VisualizationMode == EDWCTransparencyVisualizationMode::InnerColor
        : (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
           VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha);
    return !bPreviewSuspended && bSurfacePaintingEnabled &&
        (bCanRevealPaint || bCanAlphaPaint) && PaintSettings.bEnabled &&
        PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        AutoBakePreviewResult.IsValid() &&
        SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelIndex >= 0 &&
        bSupportsCurrentVisualization;
}

bool SWetClothingTransparencyPreviewViewport::CanShowBrushCursor() const
{
    const bool bHasActivePaintTarget = bTransparencyPaintingEnabled || bRevealColorPaintingEnabled;
    const bool bRevealVisualizationSupportsBrush = !PaintSettings.bRevealColorPaint ||
        VisualizationMode == EDWCTransparencyVisualizationMode::InnerColor;
    const bool bAlphaVisualizationSupportsBrush = PaintSettings.bRevealColorPaint ||
        VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
        VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha;
    return !bPreviewSuspended && bSurfacePaintingEnabled && bHasActivePaintTarget &&
        bRevealVisualizationSupportsBrush && bAlphaVisualizationSupportsBrush &&
        PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        TargetMeshPreviewComponent != nullptr && SpatialLease.IsValid() && SpatialHandle.IsValid() &&
        SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelIndex >= 0;
}

void SWetClothingTransparencyPreviewViewport::RebuildHitTriangles()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RebuildHitTriangles);
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (TargetMeshPreviewComponent == nullptr ||
        Asset == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE ||
        !SpatialQueryService.IsValid())
    {
        SpatialLease.Reset();
        SpatialHandle.Reset();
        return;
    }

    ++HitTrianglePrepareCount;
    SpatialLease.Reset();
    SpatialHandle.Reset();
    SpatialLease = SpatialQueryService->AcquireLease(
        Asset,
        Asset->GetDWCSkeletalMesh(),
        SelectedUVChannelIndex,
        SelectedMaterialSlotIndex,
        nullptr);
    SpatialHandle = StaticCastSharedPtr<const FDWCEditorSpatialData>(SpatialLease.GetSharedValue());
}

bool SWetClothingTransparencyPreviewViewport::TraceSurface(
    const FVector& RayOrigin,
    const FVector& RayDirection,
    FDWCTransparencySurfaceHit& OutHit) const
{
    if (!SpatialQueryService.IsValid() || !SpatialLease.IsValid() || !SpatialHandle.IsValid())
    {
        OutHit = FDWCTransparencySurfaceHit();
        return false;
    }
    return SpatialQueryService->TraceSurface(
        SpatialHandle,
        TargetMeshPreviewComponent,
        RayOrigin,
        RayDirection,
        OutHit);
}

bool SWetClothingTransparencyPreviewViewport::HitTestSurface(
    const FRay& WorldRay,
    double& OutHitDepth) const
{
    FDWCTransparencySurfaceHit Hit;
    if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        return false;
    }
    OutHitDepth = FVector::Distance(WorldRay.Origin, Hit.WorldPosition);
    return true;
}

bool SWetClothingTransparencyPreviewViewport::CanBeginSurfaceInteraction(
    const FRay& WorldRay,
    double& OutHitDepth)
{
    if (!CanPaint())
    {
        return false;
    }
    FDWCTransparencySurfaceHit Hit;
    if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin();
    if (!Controller.IsValid() || !Controller->CanBeginSurfaceInteraction(Hit))
    {
        return false;
    }
    OutHitDepth = FVector::Distance(WorldRay.Origin, Hit.WorldPosition);
    return true;
}

void SWetClothingTransparencyPreviewViewport::BeginSurfaceInteraction(const FRay& WorldRay)
{
    FDWCTransparencySurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->BeginSurfaceInteraction(Hit);
            ClearMaterialHoverLayer();
        }
    }
}

void SWetClothingTransparencyPreviewViewport::UpdateSurfaceInteraction(const FRay& WorldRay)
{
    FDWCTransparencySurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->UpdateSurfaceInteraction(Hit);
        }
    }
    else
    {
        ClearSurfaceHover();
    }
}

void SWetClothingTransparencyPreviewViewport::EndSurfaceInteraction()
{
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->EndSurfaceInteraction();
    }
}

void SWetClothingTransparencyPreviewViewport::CancelSurfaceInteraction()
{
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelSurfaceInteraction();
    }
}

bool SWetClothingTransparencyPreviewViewport::UpdateSurfaceHover(const FRay& WorldRay)
{
    FDWCTransparencySurfaceHit Hit;
    if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        ClearSurfaceHover();
        return false;
    }
    HandleSurfaceHitFromClient(Hit);
    return true;
}

void SWetClothingTransparencyPreviewViewport::ClearSurfaceHover()
{
    HandleSurfaceHitFromClient(FDWCTransparencySurfaceHit());
}

void SWetClothingTransparencyPreviewViewport::EnsureBrushCursor()
{
    if (BrushCursorComponent == nullptr || BrushCursorComponent->GetNumSections() > 0)
    {
        return;
    }

    constexpr float HalfWidth = 0.03f;
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    const FLinearColor MarkerColor(0.08f, 0.72f, 0.95f, 1.0f);
    const auto AddDoubleSidedRibbon = [&Vertices, &Indices, &Normals, &UVs, &Colors, &Tangents, MarkerColor](
                                           const FVector& A,
                                           const FVector& B,
                                           const FVector& C,
                                           const FVector& D,
                                           const FVector& Normal)
    {
        const int32 BaseIndex = Vertices.Num();
        Vertices.Append({A, B, C, D});
        Normals.Append({Normal, Normal, Normal, Normal});
        UVs.Append({FVector2D::ZeroVector, FVector2D(1.0f, 0.0f), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f)});
        Colors.Append({MarkerColor, MarkerColor, MarkerColor, MarkerColor});
        Tangents.Append({
            FProcMeshTangent(FVector::ForwardVector, false),
            FProcMeshTangent(FVector::ForwardVector, false),
            FProcMeshTangent(FVector::ForwardVector, false),
            FProcMeshTangent(FVector::ForwardVector, false)});
        Indices.Append({
            BaseIndex, BaseIndex + 1, BaseIndex + 2,
            BaseIndex, BaseIndex + 2, BaseIndex + 3,
            BaseIndex + 2, BaseIndex + 1, BaseIndex,
            BaseIndex + 3, BaseIndex + 2, BaseIndex});
    };
    // A cross-shaped vertical pin stays readable from every camera angle while
    // leaving the painted surface unobscured. Local +Z is aligned to hit normal.
    AddDoubleSidedRibbon(
        FVector(-HalfWidth, 0.0f, 0.0f), FVector(HalfWidth, 0.0f, 0.0f),
        FVector(HalfWidth, 0.0f, 1.0f), FVector(-HalfWidth, 0.0f, 1.0f), FVector::YAxisVector);
    AddDoubleSidedRibbon(
        FVector(0.0f, -HalfWidth, 0.0f), FVector(0.0f, HalfWidth, 0.0f),
        FVector(0.0f, HalfWidth, 1.0f), FVector(0.0f, -HalfWidth, 1.0f), FVector::XAxisVector);
    if (GEngine != nullptr)
    {
        BrushCursorComponent->SetMaterial(0, GEngine->VertexColorMaterial);
    }
    BrushCursorComponent->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, UVs, Colors, Tangents, false, false);
    BrushCursorComponent->SetVisibility(false, true);
}

void SWetClothingTransparencyPreviewViewport::RefreshBrushCursor()
{
    if (BrushCursorComponent == nullptr)
    {
        return;
    }
    EnsureBrushCursor();
    // The marker is a hit-test affordance, not a paint result. It remains
    // visible for an available Stage 2 reveal target even before the user
    // enables painting, while CanPaint() remains the authoritative write gate.
    if (!CanShowBrushCursor() || !CurrentSurfaceHit.bHit)
    {
        if (BrushCursorComponent->IsVisible())
        {
            BrushCursorComponent->SetVisibility(false, false);
        }
        return;
    }
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(TargetMeshPreviewComponent->Bounds.SphereRadius));
    const float MarkerHeight = FMath::Clamp(
        MeshRadius * FMath::Max(PaintSettings.RadiusUV * 0.8f, 0.035f),
        2.0f,
        MeshRadius * 0.18f);
    const FVector Normal = CurrentSurfaceHit.WorldNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    // SetWorldTransform marks only the render transform dirty. Calling
    // MarkRenderStateDirty here recreated the procedural mesh render proxy on
    // every hover event even though its geometry and material never change.
    BrushCursorComponent->SetWorldTransform(FTransform(
        FRotationMatrix::MakeFromZ(Normal).ToQuat(),
        CurrentSurfaceHit.WorldPosition + Normal * 0.5f,
        FVector(MarkerHeight)));
    if (!BrushCursorComponent->IsVisible())
    {
        BrushCursorComponent->SetVisibility(true, false);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ClearBrushCursor()
{
    if (BrushCursorComponent != nullptr)
    {
        BrushCursorComponent->SetVisibility(false, true);
    }
}

void SWetClothingTransparencyPreviewViewport::HandleSurfaceHitFromClient(const FDWCTransparencySurfaceHit& SurfaceHit)
{
    CurrentSurfaceHit = SurfaceHit;
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->HandleSurfaceHitChanged(SurfaceHit);
    }
    UpdateMaterialHoverLayer();
    RefreshBrushCursor();
}


void SWetClothingTransparencyPreviewViewport::ApplyAuthoringBrushSample(
    const FDWCTransparencyBrushStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    if (AutoBakePreviewResult.IsValid())
    {
        if (!LiveStrokeLayer)
        {
            LiveStrokeLayer = MakeUnique<FDWCTransparencyLiveStrokeLayer>();
        }
        if (!LiveStrokeLayer->IsForStroke(Stroke.StrokeGuid))
        {
            LiveStrokeLayer->Begin(Stroke.StrokeGuid, AutoBakePreviewResult->Resolution);
        }
        LiveStrokeLayer->RecordSample(Sample, Stroke.UVAddressMode);
    }
    QueueAlphaIncrementalSample(Stroke, Sample);
}

void SWetClothingTransparencyPreviewViewport::ApplyAuthoringRevealColorSample(
    const FDWCTransparencyRevealColorStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    if (AutoBakePreviewResult.IsValid())
    {
        if (!LiveStrokeLayer)
        {
            LiveStrokeLayer = MakeUnique<FDWCTransparencyLiveStrokeLayer>();
        }
        if (!LiveStrokeLayer->IsForStroke(Stroke.StrokeGuid))
        {
            LiveStrokeLayer->Begin(Stroke.StrokeGuid, AutoBakePreviewResult->Resolution);
        }
        LiveStrokeLayer->RecordSample(Sample, Stroke.UVAddressMode);
    }

    QueueRevealColorIncrementalSample(Stroke, Sample);
}

void SWetClothingTransparencyPreviewViewport::FinishAuthoringPreviewUpdate()
{
    if (PendingRevealColorIncrementalTicket.IsValid() || !PendingRevealColorCommands.IsEmpty() ||
        PendingAlphaIncrementalTicket.IsValid() || !PendingAlphaCommands.IsEmpty())
    {
        bAuthoringFinishPending = true;
        return;
    }

    FinalizeAuthoringPreviewUpdate();
}

void SWetClothingTransparencyPreviewViewport::CommitAuthoringPreviewUpdate(
    const EDWCTransparencyPaintTarget PaintTarget)
{
    if (PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        bRevealColorRequiresWorkerRebuild = RevealColorPreviewRecovery.RequiresFullRebuild();
    }
    else if (PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha)
    {
        // The live FIFO has already applied every sample to the derived tile
        // store. Mouse-up only persists the source stroke; it must not replay
        // every historical stroke again.
        bManualOverridesRequireWorkerRebuild = AlphaPreviewRecovery.RequiresFullRebuild();
    }
    bAuthoringWorkerRebuildRequested = RevealColorPreviewRecovery.RequiresFullRebuild() ||
        AlphaPreviewRecovery.RequiresFullRebuild();
    if (bAuthoringWorkerRebuildRequested)
    {
        ++InteractivePaintAuthoritativeReplayCount;
    }
    FinishAuthoringPreviewUpdate();
}

void SWetClothingTransparencyPreviewViewport::FinalizeAuthoringPreviewUpdate()
{
    if (bAuthoringWorkerRebuildRequested)
    {
        bAuthoringWorkerRebuildRequested = false;
        RebuildTransparencyPreviewTexture();
    }
    CancelAuthoringLiveStroke();
    UpdateMaterialHoverLayer();
}

void SWetClothingTransparencyPreviewViewport::CancelAuthoringLiveStroke()
{
    CancelAlphaIncrementalWork(false);
    CancelRevealColorIncrementalWork(false);
    bAuthoringFinishPending = false;
    bAuthoringWorkerRebuildRequested = false;
    if (LiveStrokeLayer)
    {
        LiveStrokeLayer->Reset();
    }
}

void SWetClothingTransparencyPreviewViewport::ProcessInteractivePaintWork()
{
    ScheduleRevealColorIncrementalJob();
    ScheduleAlphaIncrementalJob();
    if (!PendingRevealColorIncrementalTicket.IsValid() && PendingRevealColorCommands.IsEmpty() &&
        !PendingAlphaIncrementalTicket.IsValid() && PendingAlphaCommands.IsEmpty() &&
        bAuthoringFinishPending)
    {
        bAuthoringFinishPending = false;
        FinalizeAuthoringPreviewUpdate();
    }
}

void SWetClothingTransparencyPreviewViewport::ReplayAlphaStrokeHistory(
    const TArray<FDWCTransparencyBrushStroke>& InvalidatedStrokes)
{
    if (!AutoBakePreviewResult.IsValid() || InvalidatedStrokes.IsEmpty())
    {
        return;
    }

    FDWCEditorDirtyRegionSet DirtyRegions;
    for (const FDWCTransparencyBrushStroke& Stroke : InvalidatedStrokes)
    {
        Stroke.ForEachSample(
            [this, &Stroke, &DirtyRegions](const FDWCTransparencyBrushSample& Sample)
            {
                TArray<FIntRect> Regions;
                FDWCTransparencyBrushRasterizer::BuildSampleRegions(
                    Sample,
                    AutoBakePreviewResult->Resolution,
                    Stroke.UVAddressMode,
                    Regions);
                for (const FIntRect& Region : Regions)
                {
                    DirtyRegions.Add(Region, AutoBakePreviewResult->Resolution, false);
                }
            });
    }
    for (const FIntRect& Region : DirtyRegions.GetRegions())
    {
        PendingAlphaReplayRegions.AddUnique(Region);
    }
    ScheduleDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::Alpha);
}

void SWetClothingTransparencyPreviewViewport::ReplayRevealColorStrokeHistory(
    const TArray<FDWCTransparencyRevealColorStroke>& InvalidatedStrokes)
{
    if (!AutoBakePreviewResult.IsValid() || InvalidatedStrokes.IsEmpty())
    {
        return;
    }

    FDWCEditorDirtyRegionSet DirtyRegions;
    for (const FDWCTransparencyRevealColorStroke& Stroke : InvalidatedStrokes)
    {
        Stroke.ForEachSample(
            [this, &Stroke, &DirtyRegions](const FDWCTransparencyBrushSample& Sample)
            {
                TArray<FIntRect> Regions;
                FDWCTransparencyBrushRasterizer::BuildSampleRegions(
                    Sample,
                    AutoBakePreviewResult->Resolution,
                    Stroke.UVAddressMode,
                    Regions);
                for (const FIntRect& Region : Regions)
                {
                    DirtyRegions.Add(Region, AutoBakePreviewResult->Resolution, false);
                }
            });
    }
    for (const FIntRect& Region : DirtyRegions.GetRegions())
    {
        PendingRevealColorReplayRegions.AddUnique(Region);
    }
    ScheduleDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::RevealColor);
}

bool SWetClothingTransparencyPreviewViewport::BuildRevealColorCommitInput(
    FDWCTransparencyRevealCommitJobInput& OutInput,
    FString& OutError)
{
    OutInput = FDWCTransparencyRevealCommitJobInput();
    OutError.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AutoBakePreviewResult.IsValid() || Layer == nullptr ||
        Layer->LayerGuid != AutoBakePreviewResult->LayerGuid ||
        SelectedMaterialSlotIndex != AutoBakePreviewResult->MaterialSlotIndex)
    {
        OutError = TEXT("The Stage 3 reveal-color working set does not match the selected layer.");
        return false;
    }

    OutInput.SourceResult = AutoBakePreviewResult;
    OutInput.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        OutInput.RevealMetallicDarkeningStrength =
            Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
    }
    const bool bSparseWorkingSetIsCurrent = RevealColorTileStore.IsValid() &&
        RevealColorTileStore.GetResolution() == AutoBakePreviewResult->Resolution &&
        !bRevealColorRequiresWorkerRebuild && !bAuthoringWorkerRebuildRequested &&
        !bAuthoringFinishPending && PendingRevealColorCommands.IsEmpty() &&
        !PendingRevealColorIncrementalTicket.IsValid() &&
        PendingRevealColorReplayRegions.IsEmpty() && !PendingRevealColorReplayTicket.IsValid() &&
        !IsAuthoringInteractionActive();
    if (bSparseWorkingSetIsCurrent)
    {
        RevealColorTileStore.SnapshotModifiedTiles(OutInput.ModifiedTiles);
        OutInput.bUseSparseTiles = true;
    }
    else
    {
        // The serialized strokes are the canonical fallback. Replaying them in
        // the admitted worker avoids blocking the game thread or losing an edit
        // whose incremental preview has not committed yet.
        OutInput.FallbackStrokes = Layer->GetRevealColorPaintStrokes();
    }
    return true;
}

bool SWetClothingTransparencyPreviewViewport::BuildAlphaWorkingSnapshot(
    FDWCTransparencyAlphaWorkingSnapshot& OutSnapshot,
    FString& OutError)
{
    OutSnapshot = FDWCTransparencyAlphaWorkingSnapshot();
    OutError.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AutoBakePreviewResult.IsValid() || Layer == nullptr ||
        Layer->LayerGuid != AutoBakePreviewResult->LayerGuid ||
        SelectedMaterialSlotIndex != AutoBakePreviewResult->MaterialSlotIndex)
    {
        OutError = TEXT("The Stage 4 alpha working set does not match the selected layer.");
        return false;
    }

    OutSnapshot.Resolution = AutoBakePreviewResult->Resolution;
    const TArray<FDWCTransparencyBrushStroke>& EditableStrokes = Layer->GetEditableStrokes();
    OutSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoBakePreviewResult->BaselineStrokeCount,
        0,
        EditableStrokes.Num());
    OutSnapshot.AuthoredStrokeCount = EditableStrokes.Num();
    for (int32 StrokeIndex = OutSnapshot.BaselineStrokeCount;
         StrokeIndex < EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = EditableStrokes[StrokeIndex];
        OutSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.GetSampleCount() : 0;
    }
    const bool bSparseWorkingSetIsCurrent = ManualAlphaTileStore.IsValid() &&
        ManualAlphaTileStore.GetResolution() == AutoBakePreviewResult->Resolution &&
        !bManualOverridesRequireWorkerRebuild && !bAuthoringWorkerRebuildRequested &&
        !bAuthoringFinishPending && PendingAlphaCommands.IsEmpty() &&
        !PendingAlphaIncrementalTicket.IsValid() && PendingAlphaReplayRegions.IsEmpty() &&
        !PendingAlphaReplayTicket.IsValid() && !IsAuthoringInteractionActive();
    if (bSparseWorkingSetIsCurrent)
    {
        OutSnapshot.Mode = EDWCTransparencyAlphaSnapshotMode::SparseTiles;
        OutSnapshot.StoreRevision = ManualAlphaTileStore.GetRevision();
        ManualAlphaTileStore.SnapshotModifiedTiles(OutSnapshot.ModifiedTiles);
    }
    else
    {
        // Serialized strokes remain authoritative whenever incremental state is
        // pending, rebuilding, or actively edited.
        OutSnapshot.Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
        OutSnapshot.FallbackStrokes = EditableStrokes;
    }
    return OutSnapshot.IsValid(&OutError);
}

void SWetClothingTransparencyPreviewViewport::CancelDirtyTileReplay(
    const EDWCTransparencyDirtyReplayTarget Target,
    const bool bRequireFullRebuild)
{
    const EDWCEditorWorkerJobKind Kind = Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? EDWCEditorWorkerJobKind::TransparencyAlphaDirtyReplay
        : EDWCEditorWorkerJobKind::TransparencyRevealColorDirtyReplay;
    if (WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel({Kind, SelectedMaterialSlotIndex, SelectedLayerGuid});
    }
    if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
    {
        PendingAlphaReplayTicket = {};
        PendingAlphaReplayRegions.Reset();
        ++AlphaReplayEpoch;
        if (bRequireFullRebuild)
        {
            bManualOverridesRequireWorkerRebuild = true;
            AlphaPreviewRecovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::WorkerFailed);
        }
    }
    else
    {
        PendingRevealColorReplayTicket = {};
        PendingRevealColorReplayRegions.Reset();
        ++RevealColorReplayEpoch;
        if (bRequireFullRebuild)
        {
            bRevealColorRequiresWorkerRebuild = true;
            RevealColorPreviewRecovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::WorkerFailed);
        }
    }
}

void SWetClothingTransparencyPreviewViewport::ScheduleDirtyTileReplay(
    const EDWCTransparencyDirtyReplayTarget Target)
{
    if (bPreviewSuspended || !WorkerJobScheduler.IsValid() || !PreviewCommitCoordinator.IsValid() ||
        !AutoBakePreviewResult.IsValid() || !TransparencyPreviewHandle.IsValid())
    {
        CancelDirtyTileReplay(Target, true);
        RefreshManualPreviewFromStrokes();
        return;
    }

    TArray<FIntRect>& PendingRegions = Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? PendingAlphaReplayRegions
        : PendingRevealColorReplayRegions;
    FDWCEditorWorkerJobTicket& PendingTicket = Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? PendingAlphaReplayTicket
        : PendingRevealColorReplayTicket;
    uint64& ReplayEpoch = Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? AlphaReplayEpoch
        : RevealColorReplayEpoch;
    if (PendingRegions.IsEmpty())
    {
        return;
    }

    const EDWCEditorWorkerJobKind Kind = Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? EDWCEditorWorkerJobKind::TransparencyAlphaDirtyReplay
        : EDWCEditorWorkerJobKind::TransparencyRevealColorDirtyReplay;
    if (PendingTicket.IsValid())
    {
        WorkerJobScheduler->Cancel({Kind, SelectedMaterialSlotIndex, SelectedLayerGuid});
        PendingTicket = {};
        ++ReplayEpoch;
    }
    if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
    {
        CancelAlphaIncrementalWork(false);
    }
    else
    {
        CancelRevealColorIncrementalWork(false);
    }

    TArray<FIntPoint> DirtyTiles;
    if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
    {
        ManualAlphaTileStore.GatherTileCoordinates(PendingRegions, true, false, DirtyTiles);
    }
    else
    {
        RevealColorTileStore.GatherTileCoordinates(PendingRegions, true, false, DirtyTiles);
    }
    if (DirtyTiles.IsEmpty())
    {
        PendingRegions.Reset();
        return;
    }

    const int32 TileCountX = FMath::DivideAndRoundUp(
        AutoBakePreviewResult->Resolution.X, FDWCTransparencyAlphaTileStore::TileSize);
    const int32 TileCountY = FMath::DivideAndRoundUp(
        AutoBakePreviewResult->Resolution.Y, FDWCTransparencyAlphaTileStore::TileSize);
    if (DirtyTiles.Num() * 2 >= TileCountX * TileCountY)
    {
        CancelDirtyTileReplay(Target, true);
        RefreshManualPreviewFromStrokes();
        return;
    }

    InvalidatePreviewContent();
    const int32 ExpectedSlot = SelectedMaterialSlotIndex;
    const FGuid ExpectedLayer = SelectedLayerGuid;
    const uint64 ExpectedEpoch = ReplayEpoch;
    const TArray<FIntRect> RequestRegions = PendingRegions;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = Kind;
    Descriptor.Key.MaterialSlotIndex = ExpectedSlot;
    Descriptor.Key.LayerGuid = ExpectedLayer;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    const uint64 TilePixels = static_cast<uint64>(DirtyTiles.Num()) *
        FDWCTransparencyAlphaTileStore::TileSize * FDWCTransparencyAlphaTileStore::TileSize;
    Descriptor.MemoryEstimate.WorkingBytes = TilePixels * sizeof(FColor);
    Descriptor.MemoryEstimate.OutputBytes = TilePixels * sizeof(FColor) * 2ull;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency %s dirty replay slot %d (%d tiles)"),
        Target == EDWCTransparencyDirtyReplayTarget::Alpha ? TEXT("alpha") : TEXT("reveal color"),
        ExpectedSlot,
        DirtyTiles.Num());

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    const FDWCEditorPreviewRunToken PreviewRunToken = PreviewSession
        ? PreviewSession->CaptureRunToken()
        : FDWCEditorPreviewRunToken();
    TWeakPtr<SWetClothingTransparencyPreviewViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    PendingTicket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, Target, ExpectedSlot, ExpectedLayer, ExpectedEpoch,
         DirtyTiles = MoveTemp(DirtyTiles)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled() || Viewport->bPreviewSuspended ||
                Viewport->SelectedMaterialSlotIndex != ExpectedSlot ||
                Viewport->SelectedLayerGuid != ExpectedLayer ||
                (Target == EDWCTransparencyDirtyReplayTarget::Alpha
                    ? Viewport->AlphaReplayEpoch != ExpectedEpoch
                    : Viewport->RevealColorReplayEpoch != ExpectedEpoch))
            {
                OutPrepareError = TEXT("The transparency dirty replay source changed before admission.");
                return false;
            }
            const FWetClothingTransparencyLayerData* Layer = Viewport->GetSelectedLayer();
            if (Layer == nullptr || !Viewport->AutoBakePreviewResult.IsValid())
            {
                OutPrepareError = TEXT("The transparency dirty replay layer is unavailable.");
                return false;
            }

            FDWCTransparencyDirtyTileReplayJobInput Input;
            Input.Target = Target;
            Input.SourcePayload = Viewport->AutoBakePreviewResult;
            Input.MaterialSlotIndex = ExpectedSlot;
            Input.DirtyTileCoordinates = DirtyTiles;
            Input.VisualizationMode = Viewport->VisualizationMode;
            if (const UWetClothingAsset* Asset = Viewport->WetClothingAsset.Get())
            {
                Input.RevealMetallicDarkeningStrength =
                    Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
            }
            Input.PreviewTarget.Key = UE::DWCEditor::TransparencyPreview::MakeTextureKey(
                Viewport->WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::TransparencyVisualization,
                ExpectedSlot,
                ExpectedLayer);
            Input.PreviewTarget.Descriptor = Viewport->TransparencyPreviewHandle->GetDescriptor();
            Input.PreviewTarget.ExpectedDataRevision = Viewport->TransparencyPreviewHandle->GetDataRevision();
            Input.PreviewTarget.ExpectedResourceGeneration =
                Viewport->TransparencyPreviewHandle->GetResourceGeneration();
            if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
            {
                Input.AlphaStrokes = Layer->GetEditableStrokes();
                Input.BaselineStrokeCount = Input.SourcePayload->BaselineStrokeCount;
                Input.ExpectedStoreRevision = Viewport->ManualAlphaTileStore.GetRevision();
                if (!Viewport->BuildAlphaComposeTileSnapshots(DirtyTiles, Input.AlphaComposeTiles))
                {
                    OutPrepareError = TEXT("Failed to snapshot alpha replay compose tiles.");
                    return false;
                }
            }
            else
            {
                Input.RevealColorStrokes = Layer->GetRevealColorPaintStrokes();
                Input.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
                Input.ExpectedStoreRevision = Viewport->RevealColorTileStore.GetRevision();
                if (!Viewport->BuildRevealColorComposeTileSnapshots(DirtyTiles, Input.RevealComposeTiles))
                {
                    OutPrepareError = TEXT("Failed to snapshot reveal-color replay compose tiles.");
                    return false;
                }
            }
            OutPrepared.ActualMemoryEstimate = FDWCTransparencyDirtyTileReplayWorker::EstimateMemory(Input);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FDWCTransparencyDirtyTileReplayWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, Target, ExpectedSlot, ExpectedLayer, ExpectedEpoch,
         CommitToken, PreviewRunToken, RequestRegions](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyDirtyTileReplayJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Result->bSucceeded ||
                !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            FDWCEditorWorkerJobTicket& CurrentTicket = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                ? Viewport->PendingAlphaReplayTicket
                : Viewport->PendingRevealColorReplayTicket;
            const uint64 CurrentEpoch = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                ? Viewport->AlphaReplayEpoch
                : Viewport->RevealColorReplayEpoch;
            if (CurrentEpoch != ExpectedEpoch || CurrentTicket.JobId != CompletedTicket.JobId ||
                CurrentTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }

            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.PreviewRunToken = PreviewRunToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = TEXT("Transparency dirty-tile history replay");
            CommitContext.IsCurrent = [Viewport, Target, ExpectedSlot, ExpectedLayer, ExpectedEpoch, CompletedTicket]()
            {
                const FDWCEditorWorkerJobTicket& Ticket = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                    ? Viewport->PendingAlphaReplayTicket
                    : Viewport->PendingRevealColorReplayTicket;
                return !Viewport->bPreviewSuspended &&
                    Viewport->SelectedMaterialSlotIndex == ExpectedSlot &&
                    Viewport->SelectedLayerGuid == ExpectedLayer &&
                    (Target == EDWCTransparencyDirtyReplayTarget::Alpha
                        ? Viewport->AlphaReplayEpoch == ExpectedEpoch
                        : Viewport->RevealColorReplayEpoch == ExpectedEpoch) &&
                    Ticket.JobId == CompletedTicket.JobId && Ticket.Generation == CompletedTicket.Generation;
            };

            bool bStoreCanCommit = false;
            if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
            {
                bStoreCanCommit = Viewport->ManualAlphaTileStore.CanCommit(
                    Result->ExpectedStoreRevision, Result->AlphaTiles);
            }
            else if (Viewport->AutoBakePreviewResult.IsValid())
            {
                bStoreCanCommit = Viewport->RevealColorTileStore.CanCommit(
                    Result->ExpectedStoreRevision,
                    Result->RevealColorTiles,
                    MakeArrayView(Viewport->AutoBakePreviewResult->InnerColorBuffer));
            }

            FDWCEditorPreviewRegionCommitOutcome Outcome;
            const EDWCEditorPreviewCommitResult CommitResult = bStoreCanCommit
                ? Viewport->PreviewCommitCoordinator->CommitBGRA8Regions(
                    CommitContext,
                    Viewport->TransparencyPreviewHandle,
                    Result->PreviewTarget,
                    Result->PreviewRegions,
                    Outcome,
                    EDWCEditorTextureUploadPriority::Interactive)
                : EDWCEditorPreviewCommitResult::DataRevisionMismatch;
            bool bStoreCommitted = false;
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied)
            {
                bStoreCommitted = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                    ? Viewport->ManualAlphaTileStore.Commit(
                        Result->ExpectedStoreRevision, Result->AlphaTiles)
                    : Viewport->RevealColorTileStore.Commit(
                        Result->ExpectedStoreRevision,
                        Result->RevealColorTiles,
                        MakeArrayView(Viewport->AutoBakePreviewResult->InnerColorBuffer));
            }
            CurrentTicket = {};
            if (!bStoreCommitted)
            {
                Viewport->CancelDirtyTileReplay(Target, true);
                Viewport->RefreshManualPreviewFromStrokes();
                return;
            }

            TArray<FIntRect>& Regions = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                ? Viewport->PendingAlphaReplayRegions
                : Viewport->PendingRevealColorReplayRegions;
            for (const FIntRect& Region : RequestRegions)
            {
                Regions.Remove(Region);
            }
            if (Target == EDWCTransparencyDirtyReplayTarget::Alpha)
            {
                Viewport->AlphaPreviewRecovery.MarkIncrementalSucceeded();
            }
            else
            {
                Viewport->RevealColorPreviewRecovery.MarkIncrementalSucceeded();
            }
            Viewport->InvalidatePreviewViewport();
            if (!Regions.IsEmpty())
            {
                Viewport->ScheduleDirtyTileReplay(Target);
            }
        },
        &SubmitError,
        [WeakThis, Target, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& FinishedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid())
            {
                return;
            }
            FDWCEditorWorkerJobTicket& CurrentTicket = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                ? Viewport->PendingAlphaReplayTicket
                : Viewport->PendingRevealColorReplayTicket;
            const uint64 CurrentEpoch = Target == EDWCTransparencyDirtyReplayTarget::Alpha
                ? Viewport->AlphaReplayEpoch
                : Viewport->RevealColorReplayEpoch;
            if (CurrentEpoch != ExpectedEpoch ||
                CurrentTicket.JobId != FinishedTicket.JobId ||
                CurrentTicket.Generation != FinishedTicket.Generation)
            {
                return;
            }
            Viewport->CancelDirtyTileReplay(Target, true);
            if (!Viewport->bPreviewSuspended)
            {
                Viewport->RefreshManualPreviewFromStrokes();
            }
        });

    if (!PendingTicket.IsValid())
    {
        CancelDirtyTileReplay(Target, true);
        RefreshManualPreviewFromStrokes();
    }
}

void SWetClothingTransparencyPreviewViewport::QueueRevealColorIncrementalSample(
    const FDWCTransparencyRevealColorStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    if (bPreviewSuspended || !AutoBakePreviewResult.IsValid() ||
        !RevealColorTileStore.IsValid() ||
        RevealColorTileStore.GetResolution() != AutoBakePreviewResult->Resolution ||
        !TransparencyPreviewHandle.IsValid())
    {
        RevealColorPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
        bRevealColorRequiresWorkerRebuild = true;
        return;
    }

    InvalidatePreviewContent();
    FPendingRevealColorCommand& Command = PendingRevealColorCommands.AddDefaulted_GetRef();
    Command.Stroke = Stroke;
    Command.Stroke.Samples.Reset();
    Command.Stroke.CompactSamples.Reset();
    Command.Sample = Sample;
    Command.Sequence = NextRevealColorCommandSequence++;
    RevealColorPreviewRecovery.MarkIncrementalPending();
    ScheduleRevealColorIncrementalJob();
}

void SWetClothingTransparencyPreviewViewport::QueueAlphaIncrementalSample(
    const FDWCTransparencyBrushStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    if (bPreviewSuspended || !AutoBakePreviewResult.IsValid())
    {
        AlphaPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::ContextChanged);
        bManualOverridesRequireWorkerRebuild = true;
        return;
    }
    if (!ManualAlphaTileStore.IsValid() ||
        ManualAlphaTileStore.GetResolution() != AutoBakePreviewResult->Resolution ||
        !TransparencyPreviewHandle.IsValid())
    {
        AlphaPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
        bManualOverridesRequireWorkerRebuild = true;
        return;
    }

    // A full visualization result captured before this sample must never
    // replace the newer incremental tile commit.
    InvalidatePreviewContent();

    FPendingAlphaCommand& Command = PendingAlphaCommands.AddDefaulted_GetRef();
    Command.Stroke = Stroke;
    Command.Stroke.Samples.Reset();
    Command.Stroke.CompactSamples.Reset();
    Command.Sample = Sample;
    Command.Sequence = NextAlphaCommandSequence++;
    AlphaPreviewRecovery.MarkIncrementalPending();
    ScheduleAlphaIncrementalJob();
}

bool SWetClothingTransparencyPreviewViewport::BuildAlphaComposeTileSnapshots(
    const TArray<FIntPoint>& TileCoordinates,
    TArray<FDWCTransparencyAlphaComposeTileSnapshot>& OutTiles) const
{
    OutTiles.Reset(TileCoordinates.Num());
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    for (const FIntPoint& Coordinate : TileCoordinates)
    {
        const FIntRect Rect = ManualAlphaTileStore.GetTileRect(Coordinate);
        if (Rect.IsEmpty())
        {
            return false;
        }
        FDWCTransparencyAlphaComposeTileSnapshot& Tile = OutTiles.AddDefaulted_GetRef();
        Tile.TileCoordinate = Coordinate;
        Tile.Rect = Rect;
        const int32 PixelCount = Rect.Width() * Rect.Height();
        Tile.RevealColor.SetNumUninitialized(PixelCount);
        if (OuterEdgeFeatherBuffer.Num() == Resolution.X * Resolution.Y)
        {
            Tile.OuterEdgeFeather.SetNumUninitialized(PixelCount);
        }
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                const int32 SourceIndex = Y * Resolution.X + X;
                const int32 LocalIndex = (Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X;
                Tile.RevealColor[LocalIndex] = RevealColorTileStore.GetColor(
                    SourceIndex,
                    MakeArrayView(AutoBakePreviewResult->InnerColorBuffer));
                if (!Tile.OuterEdgeFeather.IsEmpty())
                {
                    Tile.OuterEdgeFeather[LocalIndex] = OuterEdgeFeatherBuffer[SourceIndex];
                }
            }
        }
    }
    return true;
}

void SWetClothingTransparencyPreviewViewport::ScheduleAlphaIncrementalJob()
{
    if (bPreviewSuspended || PendingAlphaIncrementalTicket.IsValid() || PendingAlphaCommands.IsEmpty())
    {
        return;
    }
    if (!WorkerJobScheduler.IsValid() || !PreviewCommitCoordinator.IsValid() ||
        !AutoBakePreviewResult.IsValid() || !TransparencyPreviewHandle.IsValid() ||
        !ManualAlphaTileStore.IsValid())
    {
        CancelAlphaIncrementalWork(true);
        return;
    }

    const FGuid StrokeGuid = PendingAlphaCommands[0].Stroke.StrokeGuid;
    int32 BatchCount = 0;
    while (BatchCount < PendingAlphaCommands.Num() &&
           PendingAlphaCommands[BatchCount].Stroke.StrokeGuid == StrokeGuid)
    {
        ++BatchCount;
    }
    if (BatchCount <= 0)
    {
        return;
    }

    TArray<FIntRect> OutputRegions;
    FDWCEditorDirtyRegionSet RegionSet;
    for (int32 Index = 0; Index < BatchCount; ++Index)
    {
        TArray<FIntRect> SampleRegions;
        FDWCTransparencyBrushRasterizer::BuildSampleRegions(
            PendingAlphaCommands[Index].Sample,
            AutoBakePreviewResult->Resolution,
            PendingAlphaCommands[Index].Stroke.UVAddressMode,
            SampleRegions);
        for (const FIntRect& Region : SampleRegions)
        {
            RegionSet.Add(Region, AutoBakePreviewResult->Resolution, false);
        }
    }
    OutputRegions = RegionSet.GetRegions();
    TArray<FIntPoint> OutputTileCoordinates;
    ManualAlphaTileStore.GatherTileCoordinates(OutputRegions, false, false, OutputTileCoordinates);
    if (OutputTileCoordinates.IsEmpty())
    {
        PendingAlphaCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
        ScheduleAlphaIncrementalJob();
        return;
    }

    const bool bSmooth = PendingAlphaCommands[0].Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth;
    TArray<FIntPoint> SnapshotTileCoordinates;
    ManualAlphaTileStore.GatherTileCoordinates(
        OutputRegions,
        bSmooth,
        PendingAlphaCommands[0].Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap,
        SnapshotTileCoordinates);

    const int32 ExpectedSlot = SelectedMaterialSlotIndex;
    const FGuid ExpectedLayer = SelectedLayerGuid;
    const uint64 ExpectedEpoch = AlphaIncrementalEpoch;
    const uint64 FirstSequence = PendingAlphaCommands[0].Sequence;
    const uint64 LastSequence = PendingAlphaCommands[BatchCount - 1].Sequence;
    const FDWCTransparencyBrushStroke StrokeDescriptor = PendingAlphaCommands[0].Stroke;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyAlphaIncremental;
    Descriptor.Key.MaterialSlotIndex = ExpectedSlot;
    Descriptor.Key.LayerGuid = ExpectedLayer;
    Descriptor.Domain = EDWCEditorAuthoringDomain::None;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    const uint64 EstimatedTilePixels =
        static_cast<uint64>(SnapshotTileCoordinates.Num() + OutputTileCoordinates.Num()) *
        FDWCTransparencyAlphaTileStore::TileSize * FDWCTransparencyAlphaTileStore::TileSize;
    Descriptor.MemoryEstimate.SnapshotBytes = EstimatedTilePixels * 7ull;
    Descriptor.MemoryEstimate.OutputBytes =
        static_cast<uint64>(OutputTileCoordinates.Num()) *
        FDWCTransparencyAlphaTileStore::TileSize * FDWCTransparencyAlphaTileStore::TileSize * 6ull;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency alpha incremental slot %d [%llu-%llu]"),
        ExpectedSlot,
        FirstSequence,
        LastSequence);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    const FDWCEditorPreviewRunToken PreviewRunToken = PreviewSession
        ? PreviewSession->CaptureRunToken()
        : FDWCEditorPreviewRunToken();
    TWeakPtr<SWetClothingTransparencyPreviewViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, ExpectedSlot, ExpectedLayer, ExpectedEpoch, BatchCount,
         FirstSequence, LastSequence, StrokeDescriptor,
         OutputTileCoordinates = MoveTemp(OutputTileCoordinates),
         SnapshotTileCoordinates = MoveTemp(SnapshotTileCoordinates)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled() || Viewport->bPreviewSuspended ||
                Viewport->AlphaIncrementalEpoch != ExpectedEpoch ||
                Viewport->SelectedLayerGuid != ExpectedLayer ||
                Viewport->SelectedMaterialSlotIndex != ExpectedSlot ||
                Viewport->PendingAlphaCommands.Num() < BatchCount ||
                Viewport->PendingAlphaCommands[0].Sequence != FirstSequence ||
                Viewport->PendingAlphaCommands[BatchCount - 1].Sequence != LastSequence ||
                !Viewport->AutoBakePreviewResult.IsValid() ||
                !Viewport->TransparencyPreviewHandle.IsValid())
            {
                OutPrepareError = TEXT("The transparency alpha source changed before admission.");
                return false;
            }

            FDWCTransparencyAlphaIncrementalJobInput Input;
            Input.SourcePayload = Viewport->AutoBakePreviewResult;
            Input.Stroke = StrokeDescriptor;
            Input.Samples.Reserve(BatchCount);
            for (int32 Index = 0; Index < BatchCount; ++Index)
            {
                Input.Samples.Add(Viewport->PendingAlphaCommands[Index].Sample);
            }
            Input.OutputTileCoordinates = OutputTileCoordinates;
            Input.ExpectedAlphaRevision = Viewport->ManualAlphaTileStore.GetRevision();
            Viewport->ManualAlphaTileStore.SnapshotTiles(SnapshotTileCoordinates, Input.SnapshotTiles);
            if (!Viewport->BuildAlphaComposeTileSnapshots(OutputTileCoordinates, Input.ComposeTiles))
            {
                OutPrepareError = TEXT("Failed to snapshot transparency alpha compose tiles.");
                return false;
            }
            Input.VisualizationMode = Viewport->VisualizationMode;
            if (const UWetClothingAsset* Asset = Viewport->WetClothingAsset.Get())
            {
                Input.RevealMetallicDarkeningStrength =
                    Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
            }
            Input.PreviewTarget.Key = UE::DWCEditor::TransparencyPreview::MakeTextureKey(
                Viewport->WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::TransparencyVisualization,
                ExpectedSlot,
                ExpectedLayer);
            Input.PreviewTarget.Descriptor = Viewport->TransparencyPreviewHandle->GetDescriptor();
            Input.PreviewTarget.ExpectedDataRevision = Viewport->TransparencyPreviewHandle->GetDataRevision();
            Input.PreviewTarget.ExpectedResourceGeneration =
                Viewport->TransparencyPreviewHandle->GetResourceGeneration();

            OutPrepared.ActualMemoryEstimate = FDWCTransparencyAlphaIncrementalWorker::EstimateMemory(
                Input.SnapshotTiles,
                Input.ComposeTiles,
                Input.OutputTileCoordinates.Num());
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FDWCTransparencyAlphaIncrementalWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, ExpectedSlot, ExpectedLayer, ExpectedEpoch, BatchCount,
         FirstSequence, LastSequence, CommitToken, PreviewRunToken](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FDWCTransparencyAlphaIncrementalJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyAlphaIncrementalJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            const bool bOwnsTicket = Viewport->PendingAlphaIncrementalTicket.JobId == CompletedTicket.JobId &&
                Viewport->PendingAlphaIncrementalTicket.Generation == CompletedTicket.Generation;
            if (!bOwnsTicket)
            {
                return;
            }

            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.PreviewRunToken = PreviewRunToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Transparency alpha incremental slot %d"),
                ExpectedSlot);
            CommitContext.IsCurrent = [Viewport, ExpectedSlot, ExpectedLayer, ExpectedEpoch,
                                       FirstSequence, LastSequence, CompletedTicket]()
            {
                return !Viewport->bPreviewSuspended &&
                    Viewport->AlphaIncrementalEpoch == ExpectedEpoch &&
                    Viewport->SelectedMaterialSlotIndex == ExpectedSlot &&
                    Viewport->SelectedLayerGuid == ExpectedLayer &&
                    Viewport->PendingAlphaIncrementalTicket.JobId == CompletedTicket.JobId &&
                    Viewport->PendingAlphaIncrementalTicket.Generation == CompletedTicket.Generation &&
                    Viewport->PendingAlphaCommands.Num() >= 1 &&
                    Viewport->PendingAlphaCommands[0].Sequence == FirstSequence &&
                    Viewport->PendingAlphaCommands.ContainsByPredicate(
                        [LastSequence](const FPendingAlphaCommand& Command)
                        {
                            return Command.Sequence == LastSequence;
                        });
            };

            if (!Result->bHasChanges)
            {
                Viewport->PendingAlphaIncrementalTicket = {};
                if (Viewport->PendingAlphaCommands.Num() >= BatchCount &&
                    Viewport->PendingAlphaCommands[0].Sequence == FirstSequence &&
                    Viewport->PendingAlphaCommands[BatchCount - 1].Sequence == LastSequence)
                {
                    Viewport->PendingAlphaCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
                    Viewport->ScheduleAlphaIncrementalJob();
                    if (!Viewport->PendingAlphaIncrementalTicket.IsValid() &&
                        Viewport->PendingAlphaCommands.IsEmpty() && Viewport->bAuthoringFinishPending)
                    {
                        Viewport->bAuthoringFinishPending = false;
                        Viewport->FinalizeAuthoringPreviewUpdate();
                    }
                    return;
                }
                Viewport->CancelAlphaIncrementalWork(true);
                return;
            }

            FDWCEditorPreviewRegionCommitOutcome Outcome;
            EDWCEditorPreviewCommitResult CommitResult = EDWCEditorPreviewCommitResult::InvalidPayload;
            if (Viewport->ManualAlphaTileStore.CanCommit(
                    Result->ExpectedAlphaRevision,
                    Result->AlphaTiles))
            {
                CommitResult = Viewport->PreviewCommitCoordinator->CommitBGRA8Regions(
                    CommitContext,
                    Viewport->TransparencyPreviewHandle,
                    Result->PreviewTarget,
                    Result->PreviewRegions,
                    Outcome,
                    EDWCEditorTextureUploadPriority::Interactive);
            }

            Viewport->PendingAlphaIncrementalTicket = {};
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied &&
                Viewport->ManualAlphaTileStore.Commit(Result->ExpectedAlphaRevision, Result->AlphaTiles) &&
                Viewport->PendingAlphaCommands.Num() >= BatchCount &&
                Viewport->PendingAlphaCommands[0].Sequence == FirstSequence &&
                Viewport->PendingAlphaCommands[BatchCount - 1].Sequence == LastSequence)
            {
                Viewport->PendingAlphaCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
                Viewport->AlphaPreviewRecovery.MarkIncrementalSucceeded();
                ++Viewport->AlphaIncrementalCommitCount;
                Viewport->AlphaIncrementalCommittedTileCount += Result->AlphaTiles.Num();
                Viewport->AlphaIncrementalCommittedBytes += Outcome.CommittedBytes;
                Viewport->InvalidatePreviewViewport();
                Viewport->ScheduleAlphaIncrementalJob();
                if (!Viewport->PendingAlphaIncrementalTicket.IsValid() &&
                    Viewport->PendingAlphaCommands.IsEmpty() && Viewport->bAuthoringFinishPending)
                {
                    Viewport->bAuthoringFinishPending = false;
                    Viewport->FinalizeAuthoringPreviewUpdate();
                }
                return;
            }

            Viewport->CancelAlphaIncrementalWork(true);
            if (Viewport->bAuthoringFinishPending)
            {
                Viewport->bAuthoringFinishPending = false;
                Viewport->FinalizeAuthoringPreviewUpdate();
            }
        },
        &SubmitError,
        [WeakThis, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || Viewport->AlphaIncrementalEpoch != ExpectedEpoch ||
                Viewport->PendingAlphaIncrementalTicket.JobId != CompletedTicket.JobId ||
                Viewport->PendingAlphaIncrementalTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            if (Completion == EDWCEditorWorkerJobCompletion::Failed && !Error.IsEmpty())
            {
                UE_LOG(LogWetTransparencyPreviewViewport, Warning,
                    TEXT("Transparency alpha incremental worker failed: %s"), *Error);
            }
            Viewport->PendingAlphaIncrementalTicket = {};
            Viewport->CancelAlphaIncrementalWork(true);
            if (Viewport->bAuthoringFinishPending)
            {
                Viewport->bAuthoringFinishPending = false;
                Viewport->FinalizeAuthoringPreviewUpdate();
            }
        });

    PendingAlphaIncrementalTicket = Ticket;
    if (!Ticket.IsValid())
    {
        UE_LOG(LogWetTransparencyPreviewViewport, Warning,
            TEXT("Transparency alpha incremental job was not submitted for slot %d: %s"),
            ExpectedSlot,
            SubmitError.IsEmpty() ? TEXT("unknown scheduler failure") : *SubmitError);
        CancelAlphaIncrementalWork(true);
    }
}

void SWetClothingTransparencyPreviewViewport::CancelAlphaIncrementalWork(const bool bRequireFullRebuild)
{
    if (PendingAlphaIncrementalTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(PendingAlphaIncrementalTicket.Key);
    }
    PendingAlphaIncrementalTicket = {};
    PendingAlphaCommands.Reset();
    ++AlphaIncrementalEpoch;
    if (bRequireFullRebuild)
    {
        AlphaPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::WorkerFailed);
        bManualOverridesRequireWorkerRebuild = true;
        bAuthoringWorkerRebuildRequested = true;
        ++AlphaIncrementalFallbackCount;
    }
}

bool SWetClothingTransparencyPreviewViewport::BuildRevealColorComposeTileSnapshots(
    const TArray<FIntPoint>& TileCoordinates,
    TArray<FDWCTransparencyRevealColorComposeTileSnapshot>& OutTiles) const
{
    OutTiles.Reset(TileCoordinates.Num());
    if (!AutoBakePreviewResult.IsValid() || !ManualAlphaTileStore.IsValid())
    {
        return false;
    }
    TArray<FDWCTransparencyAlphaTilePayload> AlphaTiles;
    ManualAlphaTileStore.SnapshotTiles(TileCoordinates, AlphaTiles);
    if (AlphaTiles.Num() != TileCoordinates.Num())
    {
        return false;
    }
    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    for (FDWCTransparencyAlphaTilePayload& AlphaTile : AlphaTiles)
    {
        FDWCTransparencyRevealColorComposeTileSnapshot& Tile = OutTiles.AddDefaulted_GetRef();
        Tile.TileCoordinate = AlphaTile.TileCoordinate;
        Tile.Rect = AlphaTile.Rect;
        Tile.ManualPremultiplied = MoveTemp(AlphaTile.Premultiplied);
        Tile.ManualWeight = MoveTemp(AlphaTile.Weight);
        const int32 PixelCount = Tile.Rect.Width() * Tile.Rect.Height();
        if (OuterEdgeFeatherBuffer.Num() == Resolution.X * Resolution.Y)
        {
            Tile.OuterEdgeFeather.SetNumUninitialized(PixelCount);
        }
        for (int32 Y = Tile.Rect.Min.Y; Y < Tile.Rect.Max.Y; ++Y)
        {
            for (int32 X = Tile.Rect.Min.X; X < Tile.Rect.Max.X; ++X)
            {
                const int32 SourceIndex = Y * Resolution.X + X;
                const int32 LocalIndex = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width() + X - Tile.Rect.Min.X;
                if (!Tile.OuterEdgeFeather.IsEmpty())
                {
                    Tile.OuterEdgeFeather[LocalIndex] = OuterEdgeFeatherBuffer[SourceIndex];
                }
            }
        }
    }
    return true;
}

void SWetClothingTransparencyPreviewViewport::ScheduleRevealColorIncrementalJob()
{
    if (bPreviewSuspended || PendingRevealColorIncrementalTicket.IsValid() ||
        PendingRevealColorCommands.IsEmpty())
    {
        return;
    }
    if (!WorkerJobScheduler.IsValid() || !PreviewCommitCoordinator.IsValid() ||
        !AutoBakePreviewResult.IsValid() || !TransparencyPreviewHandle.IsValid() ||
        !RevealColorTileStore.IsValid())
    {
        CancelRevealColorIncrementalWork(true);
        return;
    }

    const FGuid StrokeGuid = PendingRevealColorCommands[0].Stroke.StrokeGuid;
    int32 BatchCount = 0;
    while (BatchCount < PendingRevealColorCommands.Num() &&
           PendingRevealColorCommands[BatchCount].Stroke.StrokeGuid == StrokeGuid)
    {
        ++BatchCount;
    }
    if (BatchCount <= 0)
    {
        return;
    }

    FDWCEditorDirtyRegionSet RegionSet;
    for (int32 Index = 0; Index < BatchCount; ++Index)
    {
        TArray<FIntRect> SampleRegions;
        FDWCTransparencyBrushRasterizer::BuildSampleRegions(
            PendingRevealColorCommands[Index].Sample,
            AutoBakePreviewResult->Resolution,
            PendingRevealColorCommands[Index].Stroke.UVAddressMode,
            SampleRegions);
        for (const FIntRect& Region : SampleRegions)
        {
            RegionSet.Add(Region, AutoBakePreviewResult->Resolution, false);
        }
    }
    const auto& OutputRegions = RegionSet.GetRegions();
    TArray<FIntPoint> OutputTileCoordinates;
    RevealColorTileStore.GatherTileCoordinates(OutputRegions, false, false, OutputTileCoordinates);
    if (OutputTileCoordinates.IsEmpty())
    {
        PendingRevealColorCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
        ScheduleRevealColorIncrementalJob();
        return;
    }

    const bool bSmooth = PendingRevealColorCommands[0].Stroke.BrushMode ==
        EDWCTransparencyRevealColorBrushMode::Smooth;
    TArray<FIntPoint> SnapshotTileCoordinates;
    RevealColorTileStore.GatherTileCoordinates(
        OutputRegions,
        bSmooth,
        PendingRevealColorCommands[0].Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap,
        SnapshotTileCoordinates);

    const int32 ExpectedSlot = SelectedMaterialSlotIndex;
    const FGuid ExpectedLayer = SelectedLayerGuid;
    const uint64 ExpectedEpoch = RevealColorIncrementalEpoch;
    const uint64 FirstSequence = PendingRevealColorCommands[0].Sequence;
    const uint64 LastSequence = PendingRevealColorCommands[BatchCount - 1].Sequence;
    const FDWCTransparencyRevealColorStroke StrokeDescriptor = PendingRevealColorCommands[0].Stroke;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyRevealColorIncremental;
    Descriptor.Key.MaterialSlotIndex = ExpectedSlot;
    Descriptor.Key.LayerGuid = ExpectedLayer;
    Descriptor.Domain = EDWCEditorAuthoringDomain::None;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    const uint64 EstimatedTilePixels =
        static_cast<uint64>(SnapshotTileCoordinates.Num() + OutputTileCoordinates.Num()) *
        FDWCTransparencyRevealColorTileStore::TileSize *
        FDWCTransparencyRevealColorTileStore::TileSize;
    Descriptor.MemoryEstimate.SnapshotBytes = EstimatedTilePixels * sizeof(FColor);
    Descriptor.MemoryEstimate.OutputBytes =
        static_cast<uint64>(OutputTileCoordinates.Num()) *
        FDWCTransparencyRevealColorTileStore::TileSize *
        FDWCTransparencyRevealColorTileStore::TileSize * sizeof(FColor) * 2ull;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency reveal color incremental slot %d [%llu-%llu]"),
        ExpectedSlot,
        FirstSequence,
        LastSequence);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    const FDWCEditorPreviewRunToken PreviewRunToken = PreviewSession
        ? PreviewSession->CaptureRunToken()
        : FDWCEditorPreviewRunToken();
    TWeakPtr<SWetClothingTransparencyPreviewViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, ExpectedSlot, ExpectedLayer, ExpectedEpoch, BatchCount,
         FirstSequence, LastSequence, StrokeDescriptor,
         OutputTileCoordinates = MoveTemp(OutputTileCoordinates),
         SnapshotTileCoordinates = MoveTemp(SnapshotTileCoordinates)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled() || Viewport->bPreviewSuspended ||
                Viewport->RevealColorIncrementalEpoch != ExpectedEpoch ||
                Viewport->SelectedLayerGuid != ExpectedLayer ||
                Viewport->SelectedMaterialSlotIndex != ExpectedSlot ||
                Viewport->PendingRevealColorCommands.Num() < BatchCount ||
                Viewport->PendingRevealColorCommands[0].Sequence != FirstSequence ||
                Viewport->PendingRevealColorCommands[BatchCount - 1].Sequence != LastSequence ||
                !Viewport->AutoBakePreviewResult.IsValid() ||
                !Viewport->TransparencyPreviewHandle.IsValid())
            {
                OutPrepareError = TEXT("The transparency reveal-color source changed before admission.");
                return false;
            }

            const FWetClothingTransparencyLayerData* Layer = Viewport->GetSelectedLayer();
            if (Layer == nullptr)
            {
                OutPrepareError = TEXT("The selected transparency layer is no longer available.");
                return false;
            }

            FDWCTransparencyRevealColorIncrementalJobInput Input;
            Input.SourcePayload = Viewport->AutoBakePreviewResult;
            Input.Stroke = StrokeDescriptor;
            Input.Samples.Reserve(BatchCount);
            for (int32 Index = 0; Index < BatchCount; ++Index)
            {
                Input.Samples.Add(Viewport->PendingRevealColorCommands[Index].Sample);
            }
            Input.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
            Input.OutputTileCoordinates = OutputTileCoordinates;
            Input.ExpectedRevealRevision = Viewport->RevealColorTileStore.GetRevision();
            Viewport->RevealColorTileStore.SnapshotTiles(
                SnapshotTileCoordinates,
                MakeArrayView(Input.SourcePayload->InnerColorBuffer),
                Input.SnapshotTiles);
            if (!Viewport->BuildRevealColorComposeTileSnapshots(
                    OutputTileCoordinates,
                    Input.ComposeTiles))
            {
                OutPrepareError = TEXT("Failed to snapshot transparency reveal-color compose tiles.");
                return false;
            }
            Input.VisualizationMode = Viewport->VisualizationMode;
            if (const UWetClothingAsset* Asset = Viewport->WetClothingAsset.Get())
            {
                Input.RevealMetallicDarkeningStrength =
                    Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
            }
            Input.PreviewTarget.Key = UE::DWCEditor::TransparencyPreview::MakeTextureKey(
                Viewport->WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::TransparencyVisualization,
                ExpectedSlot,
                ExpectedLayer);
            Input.PreviewTarget.Descriptor = Viewport->TransparencyPreviewHandle->GetDescriptor();
            Input.PreviewTarget.ExpectedDataRevision = Viewport->TransparencyPreviewHandle->GetDataRevision();
            Input.PreviewTarget.ExpectedResourceGeneration =
                Viewport->TransparencyPreviewHandle->GetResourceGeneration();

            OutPrepared.ActualMemoryEstimate = FDWCTransparencyRevealColorIncrementalWorker::EstimateMemory(
                Input.SnapshotTiles,
                Input.ComposeTiles,
                Input.OutputTileCoordinates.Num());
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FDWCTransparencyRevealColorIncrementalWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, ExpectedSlot, ExpectedLayer, ExpectedEpoch, BatchCount,
         FirstSequence, LastSequence, CommitToken, PreviewRunToken](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FDWCTransparencyRevealColorIncrementalJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyRevealColorIncrementalJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            const bool bOwnsTicket = Viewport->PendingRevealColorIncrementalTicket.JobId == CompletedTicket.JobId &&
                Viewport->PendingRevealColorIncrementalTicket.Generation == CompletedTicket.Generation;
            if (!bOwnsTicket)
            {
                return;
            }

            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.PreviewRunToken = PreviewRunToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Transparency reveal color incremental slot %d"),
                ExpectedSlot);
            CommitContext.IsCurrent = [Viewport, ExpectedSlot, ExpectedLayer, ExpectedEpoch,
                                       FirstSequence, LastSequence, CompletedTicket]()
            {
                return !Viewport->bPreviewSuspended &&
                    Viewport->RevealColorIncrementalEpoch == ExpectedEpoch &&
                    Viewport->SelectedMaterialSlotIndex == ExpectedSlot &&
                    Viewport->SelectedLayerGuid == ExpectedLayer &&
                    Viewport->PendingRevealColorIncrementalTicket.JobId == CompletedTicket.JobId &&
                    Viewport->PendingRevealColorIncrementalTicket.Generation == CompletedTicket.Generation &&
                    Viewport->PendingRevealColorCommands.Num() >= 1 &&
                    Viewport->PendingRevealColorCommands[0].Sequence == FirstSequence &&
                    Viewport->PendingRevealColorCommands.ContainsByPredicate(
                        [LastSequence](const FPendingRevealColorCommand& Command)
                        {
                            return Command.Sequence == LastSequence;
                        });
            };

            if (!Result->bHasChanges)
            {
                Viewport->PendingRevealColorIncrementalTicket = {};
                if (Viewport->PendingRevealColorCommands.Num() >= BatchCount &&
                    Viewport->PendingRevealColorCommands[0].Sequence == FirstSequence &&
                    Viewport->PendingRevealColorCommands[BatchCount - 1].Sequence == LastSequence)
                {
                    Viewport->PendingRevealColorCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
                    Viewport->ScheduleRevealColorIncrementalJob();
                    if (!Viewport->PendingRevealColorIncrementalTicket.IsValid() &&
                        Viewport->PendingRevealColorCommands.IsEmpty() &&
                        !Viewport->PendingAlphaIncrementalTicket.IsValid() &&
                        Viewport->PendingAlphaCommands.IsEmpty() && Viewport->bAuthoringFinishPending)
                    {
                        Viewport->bAuthoringFinishPending = false;
                        Viewport->FinalizeAuthoringPreviewUpdate();
                    }
                    return;
                }
                Viewport->CancelRevealColorIncrementalWork(true);
                return;
            }

            FDWCEditorPreviewRegionCommitOutcome Outcome;
            EDWCEditorPreviewCommitResult CommitResult = EDWCEditorPreviewCommitResult::InvalidPayload;
            if (Viewport->AutoBakePreviewResult.IsValid() &&
                Viewport->RevealColorTileStore.CanCommit(
                    Result->ExpectedRevealRevision,
                    Result->RevealTiles,
                    MakeArrayView(Viewport->AutoBakePreviewResult->InnerColorBuffer)))
            {
                CommitResult = Viewport->PreviewCommitCoordinator->CommitBGRA8Regions(
                    CommitContext,
                    Viewport->TransparencyPreviewHandle,
                    Result->PreviewTarget,
                    Result->PreviewRegions,
                    Outcome,
                    EDWCEditorTextureUploadPriority::Interactive);
            }

            Viewport->PendingRevealColorIncrementalTicket = {};
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied &&
                Viewport->AutoBakePreviewResult.IsValid() &&
                Viewport->RevealColorTileStore.Commit(
                    Result->ExpectedRevealRevision,
                    Result->RevealTiles,
                    MakeArrayView(Viewport->AutoBakePreviewResult->InnerColorBuffer)) &&
                Viewport->PendingRevealColorCommands.Num() >= BatchCount &&
                Viewport->PendingRevealColorCommands[0].Sequence == FirstSequence &&
                Viewport->PendingRevealColorCommands[BatchCount - 1].Sequence == LastSequence)
            {
                Viewport->PendingRevealColorCommands.RemoveAt(0, BatchCount, EAllowShrinking::No);
                Viewport->RevealColorPreviewRecovery.MarkIncrementalSucceeded();
                ++Viewport->RevealColorIncrementalCommitCount;
                Viewport->RevealColorIncrementalCommittedTileCount += Result->RevealTiles.Num();
                Viewport->RevealColorIncrementalCommittedBytes += Outcome.CommittedBytes;
                Viewport->InvalidatePreviewViewport();
                Viewport->ScheduleRevealColorIncrementalJob();
                if (!Viewport->PendingRevealColorIncrementalTicket.IsValid() &&
                    Viewport->PendingRevealColorCommands.IsEmpty() &&
                    !Viewport->PendingAlphaIncrementalTicket.IsValid() &&
                    Viewport->PendingAlphaCommands.IsEmpty() && Viewport->bAuthoringFinishPending)
                {
                    Viewport->bAuthoringFinishPending = false;
                    Viewport->FinalizeAuthoringPreviewUpdate();
                }
                return;
            }

            Viewport->CancelRevealColorIncrementalWork(true);
            if (Viewport->bAuthoringFinishPending)
            {
                Viewport->bAuthoringFinishPending = false;
                Viewport->FinalizeAuthoringPreviewUpdate();
            }
        },
        &SubmitError,
        [WeakThis, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || Viewport->RevealColorIncrementalEpoch != ExpectedEpoch ||
                Viewport->PendingRevealColorIncrementalTicket.JobId != CompletedTicket.JobId ||
                Viewport->PendingRevealColorIncrementalTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            if (Completion == EDWCEditorWorkerJobCompletion::Failed && !Error.IsEmpty())
            {
                UE_LOG(LogWetTransparencyPreviewViewport, Warning,
                    TEXT("Transparency reveal-color incremental worker failed: %s"), *Error);
            }
            Viewport->PendingRevealColorIncrementalTicket = {};
            Viewport->CancelRevealColorIncrementalWork(true);
            if (Viewport->bAuthoringFinishPending)
            {
                Viewport->bAuthoringFinishPending = false;
                Viewport->FinalizeAuthoringPreviewUpdate();
            }
        });

    PendingRevealColorIncrementalTicket = Ticket;
    if (!Ticket.IsValid())
    {
        UE_LOG(LogWetTransparencyPreviewViewport, Warning,
            TEXT("Transparency reveal-color incremental job was not submitted for slot %d: %s"),
            ExpectedSlot,
            SubmitError.IsEmpty() ? TEXT("unknown scheduler failure") : *SubmitError);
        CancelRevealColorIncrementalWork(true);
    }
}

void SWetClothingTransparencyPreviewViewport::CancelRevealColorIncrementalWork(
    const bool bRequireFullRebuild)
{
    if (PendingRevealColorIncrementalTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(PendingRevealColorIncrementalTicket.Key);
    }
    PendingRevealColorIncrementalTicket = {};
    PendingRevealColorCommands.Reset();
    ++RevealColorIncrementalEpoch;
    if (bRequireFullRebuild)
    {
        RevealColorPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::WorkerFailed);
        bRevealColorRequiresWorkerRebuild = true;
        bAuthoringWorkerRebuildRequested = true;
        ++RevealColorIncrementalFallbackCount;
    }
}


#undef LOCTEXT_NAMESPACE

