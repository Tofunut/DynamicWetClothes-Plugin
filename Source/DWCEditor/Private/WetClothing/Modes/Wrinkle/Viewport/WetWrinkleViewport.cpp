//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetWrinkleViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PrimitiveDrawInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RHITypes.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewGraphExtension.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewMaterialParameters.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleAccumulatedPreviewWorker.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"
#include "WetWrinkleViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetWrinklePreviewViewport, Log, All);

namespace
{
    constexpr int32 WrinkleViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.

    TAutoConsoleVariable<int32> CVarDWCWrinkleHoverDiagnostics(
        TEXT("dwc.Wrinkle.HoverDiagnostics"),
        0,
        TEXT("Wrinkle hover timing diagnostics: 0=off, 1=slow completions, 2=sampled completions, 3=include canceled requests."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarDWCWrinkleHoverSlowThresholdMs(
        TEXT("dwc.Wrinkle.HoverSlowThresholdMs"),
        16.0f,
        TEXT("End-to-end threshold in milliseconds for wrinkle hover diagnostics."),
        ECVF_Default);
    TAutoConsoleVariable<float> CVarDWCWrinkleHoverLogIntervalSeconds(
        TEXT("dwc.Wrinkle.HoverLogIntervalSeconds"),
        0.5f,
        TEXT("Minimum interval between wrinkle hover diagnostic log lines."),
        ECVF_Default);

    FDWCEditorTextureKey MakeWrinkleTextureKey(
        const UWetClothingAsset* Asset,
        const EDWCEditorTexturePurpose Purpose,
        const int32 MaterialSlotIndex,
        const FGuid& LayerGuid = FGuid())
    {
        FDWCEditorTextureKey Key;
        Key.Owner = FObjectKey(Asset);
        Key.Purpose = Purpose;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        Key.LayerGuid = LayerGuid;
        return Key;
    }

    const FGuid WrinkleHoverFrontLayerGuid(0x44574348, 0x6F766572, 0x46726F6E, 0x74000001);
    const FGuid WrinkleHoverBackLayerGuid(0x44574348, 0x6F766572, 0x4261636B, 0x00000002);

    FDWCEditorTextureDescriptor MakeWrinkleNormalDescriptor(
        const FIntPoint& Size,
        const FIntPoint& WorkingSize)
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = Size;
        Descriptor.WorkingSize = WorkingSize;
        Descriptor.PixelFormat = PF_B8G8R8A8;
        Descriptor.bSRGB = false;
        Descriptor.CompressionSettings = TC_Normalmap;
        Descriptor.MipGenSettings = TMGS_NoMipmaps;
        Descriptor.Filter = TF_Bilinear;
        Descriptor.AddressX = TA_Wrap;
        Descriptor.AddressY = TA_Wrap;
        Descriptor.LODGroup = TEXTUREGROUP_WorldNormalMap;
        Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);
        return Descriptor;
    }

    UMaterialInterface* ResolveSourceMeshMaterialForPreviewSlot(
        const USkeletalMesh* PreparedMesh,
        const USkeletalMesh* SourceMesh,
        const int32 MaterialSlotIndex)
    {
        if (PreparedMesh == nullptr || SourceMesh == nullptr || !PreparedMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            return nullptr;
        }

        const TArray<FSkeletalMaterial>& SourceMaterials = SourceMesh->GetMaterials();
        if (SourceMaterials.IsValidIndex(MaterialSlotIndex) && SourceMaterials[MaterialSlotIndex].MaterialInterface != nullptr)
        {
            return SourceMaterials[MaterialSlotIndex].MaterialInterface;
        }

        const FSkeletalMaterial& PreparedMaterial = PreparedMesh->GetMaterials()[MaterialSlotIndex];
        if (PreparedMaterial.MaterialSlotName.IsNone() && PreparedMaterial.ImportedMaterialSlotName.IsNone())
        {
            return nullptr;
        }

        for (const FSkeletalMaterial& SourceMaterial : SourceMaterials)
        {
            const bool bSlotNameMatches =
                !PreparedMaterial.MaterialSlotName.IsNone() &&
                (SourceMaterial.MaterialSlotName == PreparedMaterial.MaterialSlotName ||
                 SourceMaterial.ImportedMaterialSlotName == PreparedMaterial.MaterialSlotName);
            const bool bImportedNameMatches =
                !PreparedMaterial.ImportedMaterialSlotName.IsNone() &&
                (SourceMaterial.MaterialSlotName == PreparedMaterial.ImportedMaterialSlotName ||
                 SourceMaterial.ImportedMaterialSlotName == PreparedMaterial.ImportedMaterialSlotName);
            if ((bSlotNameMatches || bImportedNameMatches) && SourceMaterial.MaterialInterface != nullptr)
            {
                return SourceMaterial.MaterialInterface;
            }
        }

        return nullptr;
    }

    FVector MakeWetWrinkleAnyPerpendicular(const FVector& Direction)
    {
        FVector Perpendicular = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector::ForwardVector : Perpendicular;
    }

    bool IsWetWrinkleLinkedSurface(const FVector& PrimaryWorldPosition, const FVector& CandidateWorldPosition, float Radius)
    {
        const float MinLinkedDistance = FMath::Max(Radius * 0.5f, 1.0f);
        return FVector::DistSquared(PrimaryWorldPosition, CandidateWorldPosition) > FMath::Square(MinLinkedDistance);
    }

    FIntPoint ComputeWetWrinklePreviewTextureSize(const UWetClothingAsset* Asset)
    {
        const int32 Resolution = Asset != nullptr
            ? Asset->GetWrinkleMapResolution()
            : WetWrinkleTextureRaster::InternalBakeResolution;
        return WetWrinkleTextureRaster::ResolveFinalTextureSize(Resolution);
    }

    FDWCEditorWorkerMemoryEstimate EstimateWetWrinklePreviewWorkerMemory(
        const FWetWrinkleAccumulatedPreviewJobInput& Input)
    {
        FDWCEditorWorkerMemoryEstimate Estimate;
        const uint64 WorkingSurfaceBytes =
            static_cast<uint64>(Input.WorkingTextureSize.X) * Input.WorkingTextureSize.Y * sizeof(uint32);
        const uint64 FinalPixelsBytes =
            static_cast<uint64>(Input.TextureSize.X) * Input.TextureSize.Y * sizeof(FColor);
        const uint64 LargestRidgeScratchBytes = Input.RidgeStrokes.IsEmpty()
            ? 0
            : FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound();

        // The source readbacks are immutable shared snapshots. Count only the
        // per-job arrays and allocations that coexist while this worker runs.
        Estimate.SnapshotBytes = Input.SurfacePatches.GetAllocatedSize() +
            Input.RidgeStrokes.GetAllocatedSize();
        uint64 ProjectionScratchBytes = 0;
        for (const FWetWrinkleSurfacePatchPreviewInput& Patch : Input.SurfacePatches)
        {
            const int32 TriangleCount = Patch.Projection.SpatialHandle.IsValid()
                ? Patch.Projection.SpatialHandle->Triangles.Num()
                : 0;
            ProjectionScratchBytes = FMath::Max(
                ProjectionScratchBytes,
                FMath::Min(
                    static_cast<uint64>(TriangleCount) * 128ull,
                    Patch.Projection.MaxWorkingSetBytes));
        }
        for (const FWetProceduralRidgeStroke& Stroke : Input.RidgeStrokes)
        {
            Estimate.SnapshotBytes += Stroke.Points.GetAllocatedSize();
            Estimate.SnapshotBytes += Stroke.DisplayName.GetAllocatedSize();
        }
        Estimate.WorkingBytes = WorkingSurfaceBytes;
        Estimate.OutputBytes = FinalPixelsBytes;
        Estimate.ScratchBytes = FMath::Max(LargestRidgeScratchBytes, ProjectionScratchBytes);
        return Estimate;
    }

    FDWCEditorWorkerMemoryEstimate EstimateWetWrinklePreviewAdmissionMemory(
        const UWetClothingAsset* Asset,
        const FIntPoint& TextureSize,
        const FIntPoint& WorkingTextureSize)
    {
        FWetWrinkleAccumulatedPreviewJobInput EstimateInput;
        EstimateInput.TextureSize = TextureSize;
        EstimateInput.WorkingTextureSize = WorkingTextureSize;

        FDWCEditorWorkerMemoryEstimate Estimate = EstimateWetWrinklePreviewWorkerMemory(EstimateInput);
        if (Asset == nullptr)
        {
            return Estimate;
        }

        const FWetClothingWrinkleData& WrinkleData = Asset->Authored.WrinkleData;
        Estimate.SnapshotBytes =
            static_cast<uint64>(WrinkleData.EditablePatches.Num()) * sizeof(FWetWrinkleSurfacePatchPreviewInput) +
            WrinkleData.EditableProceduralRidgeStrokes.GetAllocatedSize();
        for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
        {
            Estimate.SnapshotBytes += Stroke.Points.GetAllocatedSize();
            Estimate.SnapshotBytes += Stroke.DisplayName.GetAllocatedSize();
        }
        return Estimate;
    }

    bool EncodeWetWrinklePreviewSurface(
        const FDWCEditorNormalRasterSurface& WorkingSurface,
        const FIntPoint& FinalTextureSize,
        TArray<FColor>& InOutPixels,
        const FIntRect& FinalDirtyRect)
    {
        if (!WorkingSurface.IsValid())
        {
            return false;
        }

        if (WorkingSurface.Size == FinalTextureSize)
        {
            FDWCEditorRasterPostProcess::EncodeNormalPixels(WorkingSurface, InOutPixels, &FinalDirtyRect);
            return true;
        }

        return FDWCEditorRasterPostProcess::ResampleAndEncodeNormalPixels(
            WorkingSurface,
            FinalTextureSize,
            InOutPixels,
            &FinalDirtyRect);
    }

    bool AreWetWrinkleSurfaceHitsEquivalentForPreview(const FWetWrinkleSurfaceHit& A, const FWetWrinkleSurfaceHit& B)
    {
        if (A.bHit != B.bHit)
        {
            return false;
        }

        if (!A.bHit)
        {
            return true;
        }

        constexpr double UVToleranceSq = 1.0e-8;
        constexpr double FrameDirectionTolerance = 0.99999;
        const FVector FrameUA = A.LocalSurfaceFrameU.GetSafeNormal();
        const FVector FrameUB = B.LocalSurfaceFrameU.GetSafeNormal();
        return A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.UVChannelIndex == B.UVChannelIndex &&
               A.TriangleID == B.TriangleID &&
               (A.UV - B.UV).SizeSquared() <= UVToleranceSq &&
               !FrameUA.IsNearlyZero() &&
               !FrameUB.IsNearlyZero() &&
               FVector::DotProduct(FrameUA, FrameUB) >= FrameDirectionTolerance;
    }

    FColor EncodeWetWrinkleNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    bool BuildWetWrinkleSurfacePatchPreviewInput(
        const FWetWrinklePatchPlacement& Stamp,
        const FDWCEditorSpatialHandle& SpatialHandle,
        FWetWrinkleSurfacePatchPreviewInput& OutInput,
        FString& OutError)
    {
        FDWCEditorWrinklePatchValidationResult Validation;
        if (!FDWCEditorWrinklePatchDescriptorBuilder::ValidatePlacement(
                Stamp,
                SpatialHandle.IsValid() ? SpatialHandle->UVChannelIndex : INDEX_NONE,
                Validation))
        {
            OutError = MoveTemp(Validation.Error);
            return false;
        }

        return
            FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInput(
                Validation.Descriptor, SpatialHandle, OutInput, &OutError);
    }

} // namespace

void SWetWrinkleViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    SessionStore = InArgs._SessionStore;
    SpatialQueryService = InArgs._SpatialQueryService;
    SurfacePatchProjectionCache = InArgs._SurfacePatchProjectionCache;
    TextureWorkspace = InArgs._TextureWorkspace;
    PreviewCommitCoordinator = InArgs._PreviewCommitCoordinator;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    OnSurfaceHitChanged = InArgs._OnSurfaceHitChanged;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    InputToolsHost = MakeUnique<FDWCEditorInteractiveToolsHost>(PreviewScene.Get(), this);

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMeshComponent->SetForcedLOD(WrinkleViewportForceRenderLOD0);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);

    InitializePreviewSession();
    RefreshPreviewMesh();
}

SWetWrinkleViewport::~SWetWrinkleViewport()
{
    PreviewCommitLifetime.Revoke();
    if (InputToolsHost)
    {
        InputToolsHost->Shutdown();
    }
    if (WorkerJobScheduler.IsValid())
    {
        for (const FWetWrinkleAccumulatedPreviewState& State : AccumulatedPreviewStates)
        {
            FDWCEditorWorkerJobKey Key;
            Key.Kind = EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview;
            Key.MaterialSlotIndex = State.MaterialSlotIndex;
            WorkerJobScheduler->Cancel(Key);
        }
    }
    // Release producer state while the scheduler is still alive so every
    // pending hover/ridge/accumulated job is canceled before weak callbacks
    // lose their viewport owner.
    ReleaseAccumulatedPreviewStates();
    ReleaseTransientProceduralPreviewState();
    ResetPatchHoverPreviewState();
    WorkerJobScheduler.Reset();
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->Shutdown();
        PreviewOrchestrator.Reset();
    }
    if (PreviewSession)
    {
        PreviewSession->Shutdown();
        PreviewSession.Reset();
    }
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

}

void SWetWrinkleViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    if (bPreviewSuspended)
    {
        return;
    }

    if (PendingTransientProceduralStroke.IsSet())
    {
        FWetProceduralRidgeStroke Stroke = MoveTemp(PendingTransientProceduralStroke.GetValue());
        PendingTransientProceduralStroke.Reset();
        UpdateTransientProceduralPreview(Stroke);
    }

    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Flush();
    }
    if (PatchHoverPreviewState.bPresentationSwapPending &&
        PatchHoverPreviewState.PendingPresentedPayload.IsSet() &&
        PatchHoverPreviewState.StagingTextureHandle.IsValid() &&
        RenderUploadQueue.IsValid())
    {
        const EDWCEditorTextureUploadStatus UploadStatus =
            RenderUploadQueue->GetStatus(PatchHoverPreviewState.PendingPresentationUpload);
        if (UploadStatus == EDWCEditorTextureUploadStatus::RenderEnqueued ||
            UploadStatus == EDWCEditorTextureUploadStatus::Stale ||
            UploadStatus == EDWCEditorTextureUploadStatus::Invalid)
        {
            HandlePatchHoverUploadStatus(
                PatchHoverPreviewState.PendingPresentationUpload,
                PatchHoverPreviewState.RequestSerial,
                UploadStatus);
        }
    }
    if (PatchHoverPreviewState.HandoffState ==
            EWetWrinklePatchHandoffState::AwaitingAccumulatedUpload &&
        PatchHoverPreviewState.PendingAccumulatedUpload.IsValid() &&
        RenderUploadQueue.IsValid())
    {
        const EDWCEditorTextureUploadStatus UploadStatus =
            RenderUploadQueue->GetStatus(PatchHoverPreviewState.PendingAccumulatedUpload);
        if (UploadStatus == EDWCEditorTextureUploadStatus::RenderEnqueued)
        {
            FinishPatchHoverHandoff();
        }
        else if (UploadStatus == EDWCEditorTextureUploadStatus::Stale ||
                 UploadStatus == EDWCEditorTextureUploadStatus::Invalid)
        {
            PatchHoverPreviewState.PendingAccumulatedUpload = {};
            RecoverPatchHoverHandoff(
                EDWCEditorPreviewInvalidationReason::ResourceGenerationMismatch);
        }
    }
    if (PreviewSession)
    {
        PreviewSession->TickPendingMaterialCompilations();
    }
    if (bPreviewMaterialsNeedReapply)
    {
        ApplyPreviewMaterialsToMesh();
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != FDWCEditorPreviewSession::AllWettableSlots)
    {
        FWetWrinkleAccumulatedPreviewState* RetryState = AccumulatedPreviewStates.FindByPredicate(
            [this, ActiveMaterialSlotIndex, InCurrentTime](const FWetWrinkleAccumulatedPreviewState& State)
            {
                return State.MaterialSlotIndex == ActiveMaterialSlotIndex &&
                    State.UVChannelIndex == BrushSettings.UVChannelIndex &&
                    State.bDirty &&
                    !State.bRebuildPending &&
                    (State.Recovery.GetState() == EDWCEditorPreviewRecoveryState::FullRebuildRequired ||
                     State.Recovery.IsRetryDue(InCurrentTime));
            });
        if (RetryState != nullptr)
        {
            RebuildAccumulatedPreviewTexture(*RetryState);
        }
    }
}

void SWetWrinkleViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(GeneratedNormalPreviewTexture);
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        Collector.AddReferencedObject(PreviewState.SourceTexture);
    }
    Collector.AddReferencedObject(TransientProceduralPreviewState.SourceTexture);
    Collector.AddReferencedObject(BrushSettings.WrinkleNormalTexture);
}

void SWetWrinkleViewport::InitializePreviewSession()
{
    PreviewSession = MakeUnique<FDWCEditorPreviewSession>();

    FDWCEditorPreviewSessionConfig Config;
    Config.DiagnosticLabel = TEXT("Wrinkle");
    Config.FeatureMask = EDWCEditorPreviewMaterialFeature::Wrinkle;
    Config.FeatureSchemaVersion = FWetWrinklePreviewGraphExtension::GraphSchemaVersion;
    Config.SurfaceWaterNormalUVChannelIndex = 0;
    Config.InitialPreviewWetness = BrushSettings.PreviewWetness;
    Config.ExtendGraph = &FWetWrinklePreviewGraphExtension::ExtendGraph;
    Config.InitializeMID = &FWetWrinklePreviewGraphExtension::InitializeMID;
    Config.CollectMemoryStats = [this](TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets)
    {
        CollectDiagnosticMemoryStats(OutBuckets);
    };
    Config.CollectOperationStats = [this](TArray<FDWCEditorPreviewOperationCounter>& OutCounters)
    {
        CollectDiagnosticOperationStats(OutCounters);
    };
    Config.ResetDiagnosticCounters = [this]()
    {
        ResetDiagnosticCounters();
    };

    PreviewSession->Initialize(
        WetClothingAsset.Get(),
        PreviewScene.IsValid() ? PreviewScene->GetWorld() : nullptr,
        Config);
    PreviewOrchestrator = MakeUnique<FDWCEditorPreviewOrchestrator>();
    PreviewOrchestrator->Initialize(
        WetClothingAsset.Get(),
        PreviewSession.Get(),
        EDWCEditorAuthoringDomain::Wrinkle,
        SessionStore);
    PreviewSession->OnSlotsChanged().AddRaw(
        this,
        &SWetWrinkleViewport::HandlePreviewSessionSlotsChanged);
    PreviewSession->OnMaterialReady().AddRaw(
        this,
        &SWetWrinkleViewport::HandlePreviewSessionMaterialReady);
}

void SWetWrinkleViewport::HandlePreviewSessionSlotsChanged()
{
    MarkPreviewMaterialsNeedReapply();
    ApplyMaterialSlotVisibility();
    ApplyPreviewMaterialsToMesh();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SWetWrinkleViewport::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
{
    if (bPreviewSuspended)
    {
        return;
    }

    PreviewCommitLifetime.Suspend();

    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelActiveInteraction(false);
    }

    ClearSurfaceHover();
    ResetPatchHoverPreviewState();
    ClearTransientProceduralStroke(false);
    ReleaseAccumulatedPreviewStates();
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->ClearAllLiveLayers();
    }
    if (PreviewSession)
    {
        PreviewSession->Suspend(Reason);
    }

    bPreviewSuspended = true;
    MarkPreviewMaterialsNeedReapply();
    ApplyPreviewMaterialsToMesh();
    Invalidate();
}

void SWetWrinkleViewport::ResumePreviewIfNeeded()
{
    if (!bPreviewSuspended)
    {
        return;
    }

    bPreviewSuspended = false;
    PreviewCommitLifetime.Resume();
    if (PreviewSession)
    {
        PreviewSession->Resume();
    }

    RefreshPreviewMesh(false);
    RefreshStoredStampOverlay(true);
}

void SWetWrinkleViewport::HandlePreviewSessionMaterialReady(
    const int32 MaterialSlotIndex,
    UMaterialInstanceDynamic* PreviewMID)
{
    if (PreviewMID == nullptr)
    {
        return;
    }

    if (MaterialSlotIndex == ResolveActivePreviewMaterialSlot())
    {
        RefreshWrinklePreviewAccumulatedParameters();
        RefreshWrinklePreviewTransientParameters();
        RefreshWrinklePreviewHoverParameters();
    }
    MarkPreviewMaterialsNeedReapply();
}

void SWetWrinkleViewport::RefreshPreviewMesh(const bool bForceMaterialRebuild)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_RefreshPreviewMesh);
    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    ++PreviewMeshRefreshCount;
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    const bool bMeshChanged = PreviewMeshComponent->GetSkeletalMeshAsset() != TargetMesh;
    if (bMeshChanged)
    {
        SpatialLease.Reset();
        SpatialHandle.Reset();
        PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
        InvalidatePreviewMaterialAssignmentCache();
    }
    else if (bForceMaterialRebuild)
    {
        SpatialLease.Reset();
        SpatialHandle.Reset();
        if (SpatialQueryService.IsValid())
        {
            SpatialQueryService->InvalidateMesh(TargetMesh);
        }
    }
    PreviewMeshComponent->SetForcedLOD(WrinkleViewportForceRenderLOD0);

    if (PreviewSession)
    {
        PreviewSession->RefreshSlotStates();
        PreviewSession->SetSelectedMaterialSlot(BrushSettings.MaterialSlotIndex);
        if (bForceMaterialRebuild)
        {
            PreviewSession->InvalidateMaterialGraphs();
        }
    }
    MarkPreviewMaterialsNeedReapply();
    ApplyMaterialSlotVisibility();
    if (bMeshChanged || bForceMaterialRebuild)
    {
        RebuildHitTriangles();
    }
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    if (bMeshChanged)
    {
        ReleaseAccumulatedPreviewStates();
        ReleaseTransientProceduralPreviewState();
        ResetPatchHoverPreviewState();
        RefreshStoredStampOverlay();
    }
    RefreshWrinklePreviewMaterials();

    if (TargetMesh != nullptr)
    {
        const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(FTransform::Identity);
        PreviewScene->SetFloorOffset(static_cast<float>(-Bounds.Origin.Z + Bounds.BoxExtent.Z));
    }
    else
    {
        PreviewScene->SetFloorOffset(0.0f);
    }

    RefreshViewportHint();

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        if (bMeshChanged)
        {
            ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
            ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
        }
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SWetWrinkleViewport::SynchronizeBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_SynchronizeBrushSettings);

    const int32 PreviousMaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        InBrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    if (bNeedsTriangleRebuild)
    {
        if (InputToolsHost)
        {
            InputToolsHost->CancelActiveInteraction();
        }
        PrepareAccumulatedPreviewStatesForSlot(
            InBrushSettings.MaterialSlotIndex,
            InBrushSettings.UVChannelIndex);
        ReleaseTransientProceduralPreviewState();
    }
    else if (bLeavingProceduralRidgeMode)
    {
        ReleaseTransientProceduralPreviewState();
    }

    InvalidatePatchHoverRequest();
    BrushSettings = InBrushSettings;
    if (bNeedsTriangleRebuild && PreviewOrchestrator && PreviousMaterialSlotIndex != INDEX_NONE)
    {
        PreviewOrchestrator->ClearLiveLayers(PreviousMaterialSlotIndex);
    }
    if (PreviewSession)
    {
        PreviewSession->SetSelectedMaterialSlot(BrushSettings.MaterialSlotIndex);
    }
    ApplyMaterialSlotVisibility();

    if (bNeedsTriangleRebuild)
    {
        CurrentSurfaceHit = FWetWrinkleSurfaceHit();
        ClearBrushCursor();
        RebuildHitTriangles();
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke && TransientProceduralStrokeHits.Num() >= 2)
    {
        SetTransientProceduralStroke(
            TransientProceduralStrokeHits,
            bTransientProceduralStartJunction,
            bTransientProceduralEndJunction);
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::SetBrushTopology(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_SetBrushTopology);

    if (BrushSettings.MaterialSlotIndex == MaterialSlotIndex &&
        BrushSettings.UVChannelIndex == UVChannelIndex)
    {
        return;
    }

    const int32 PreviousMaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    PrepareAccumulatedPreviewStatesForSlot(MaterialSlotIndex, UVChannelIndex);
    ReleaseTransientProceduralPreviewState();
    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    BrushSettings.UVChannelIndex = UVChannelIndex;
    if (PreviewOrchestrator && PreviousMaterialSlotIndex != INDEX_NONE)
    {
        PreviewOrchestrator->ClearLiveLayers(PreviousMaterialSlotIndex);
    }
    if (PreviewSession)
    {
        PreviewSession->SetSelectedMaterialSlot(MaterialSlotIndex);
    }
    ApplyMaterialSlotVisibility();

    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    ResetPatchHoverPreviewState();
    RebuildHitTriangles();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::UpdateBrushPreviewSettings(
    const FWetWrinkleBrushSettings& InBrushSettings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_UpdateBrushPreviewSettings);

    ensureMsgf(
        BrushSettings.MaterialSlotIndex == InBrushSettings.MaterialSlotIndex &&
            BrushSettings.UVChannelIndex == InBrushSettings.UVChannelIndex,
        TEXT("UpdateBrushPreviewSettings cannot change wrinkle topology. Use SetBrushTopology first."));

    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = BrushSettings.UVChannelIndex;
    const float PreviewWetness = BrushSettings.PreviewWetness;
    FWetWrinkleBrushSettings UpdatedSettings = InBrushSettings;
    UpdatedSettings.MaterialSlotIndex = MaterialSlotIndex;
    UpdatedSettings.UVChannelIndex = UVChannelIndex;
    UpdatedSettings.PreviewWetness = PreviewWetness;
    if (BrushSettings.IsEquivalent(UpdatedSettings))
    {
        return;
    }

    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        UpdatedSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;

    InvalidatePatchHoverRequest();
    BrushSettings = MoveTemp(UpdatedSettings);

    if (bLeavingProceduralRidgeMode)
    {
        ReleaseTransientProceduralPreviewState();
        RefreshWrinklePreviewTransientParameters();
    }
    else if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
             TransientProceduralStrokeHits.Num() >= 2)
    {
        SetTransientProceduralStroke(
            TransientProceduralStrokeHits,
            bTransientProceduralStartJunction,
            bTransientProceduralEndJunction);
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewHoverParameters();
    RefreshViewportHint();
    Invalidate();
}

void SWetWrinkleViewport::SetPreviewWetness(const float PreviewWetness)
{
    const float ClampedWetness = FMath::Clamp(PreviewWetness, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(BrushSettings.PreviewWetness, ClampedWetness))
    {
        return;
    }

    BrushSettings.PreviewWetness = ClampedWetness;
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->SetPreviewWetness(ClampedWetness);
    }
    Invalidate();
}

void SWetWrinkleViewport::SetShowBakedTransparency(const bool bInShowBakedTransparency)
{
    if (bShowBakedTransparency == bInShowBakedTransparency)
    {
        return;
    }

    bShowBakedTransparency = bInShowBakedTransparency;
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->SetShowSavedCrossLayer(bShowBakedTransparency);
    }
    Invalidate();
}

void SWetWrinkleViewport::SetGeneratedNormalPreviewTexture(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    UTexture2D* GeneratedNormalTexture,
    const bool bRefreshPreview)
{
    if (bGeneratedNormalPreviewOverrideActive &&
        GeneratedNormalPreviewMaterialSlotIndex == MaterialSlotIndex &&
        GeneratedNormalPreviewUVChannelIndex == UVChannelIndex &&
        GeneratedNormalPreviewTexture == GeneratedNormalTexture)
    {
        return;
    }

    bGeneratedNormalPreviewOverrideActive = true;
    GeneratedNormalPreviewMaterialSlotIndex = MaterialSlotIndex;
    GeneratedNormalPreviewUVChannelIndex = UVChannelIndex;
    GeneratedNormalPreviewTexture = GeneratedNormalTexture;
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
}

void SWetWrinkleViewport::ClearGeneratedNormalPreviewTexture(const bool bRefreshPreview)
{
    if (!bGeneratedNormalPreviewOverrideActive &&
        GeneratedNormalPreviewMaterialSlotIndex == INDEX_NONE &&
        GeneratedNormalPreviewUVChannelIndex == INDEX_NONE &&
        GeneratedNormalPreviewTexture == nullptr)
    {
        return;
    }

    bGeneratedNormalPreviewOverrideActive = false;
    GeneratedNormalPreviewMaterialSlotIndex = INDEX_NONE;
    GeneratedNormalPreviewUVChannelIndex = INDEX_NONE;
    GeneratedNormalPreviewTexture = nullptr;
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
}

void SWetWrinkleViewport::RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview)
{
    if (bRebuildAccumulatedPreview)
    {
        MarkAccumulatedPreviewStatesDirty();
    }

    RefreshWrinklePreviewAccumulatedParameters();
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedProceduralStrokeGuid(const FGuid& InStrokeGuid)
{
    if (SelectedProceduralStrokeGuid == InStrokeGuid)
    {
        return;
    }

    SelectedProceduralStrokeGuid = InStrokeGuid;
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedProceduralStrokePointIndex(const int32 InPointIndex)
{
    if (SelectedProceduralStrokePointIndex == InPointIndex)
    {
        return;
    }
    SelectedProceduralStrokePointIndex = InPointIndex;
    Invalidate();
}

void SWetWrinkleViewport::SetTransientProceduralStroke(
    const TArray<FWetWrinkleSurfaceHit>& SurfaceHits,
    const bool bStartJunction,
    const bool bEndJunction)
{
    TransientProceduralStrokeHits = SurfaceHits;
    bTransientProceduralStartJunction = bStartJunction;
    bTransientProceduralEndJunction = bEndJunction;
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
    if (bTransientProceduralPreviewBound)
    {
        bTransientProceduralPreviewBound = false;
        RefreshWrinklePreviewTransientParameters();
    }
    Invalidate();
}

void SWetWrinkleViewport::PreviewEditedProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    EditedProceduralStrokePreview = Stroke;
    PendingTransientProceduralStroke = Stroke;
    Invalidate();
}

bool SWetWrinkleViewport::SetEditingProceduralStrokeGuid(
    const FGuid& InStrokeGuid,
    const bool bRefreshPreview)
{
    if (EditingProceduralStrokeGuid == InStrokeGuid)
    {
        return false;
    }

    EditingProceduralStrokeGuid = InStrokeGuid;
    if (!EditingProceduralStrokeGuid.IsValid() ||
        (EditedProceduralStrokePreview.IsSet() &&
         EditedProceduralStrokePreview->StrokeGuid != EditingProceduralStrokeGuid))
    {
        EditedProceduralStrokePreview.Reset();
    }
    MarkAccumulatedPreviewStatesDirty();
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
    return true;
}

int32 SWetWrinkleViewport::FindNearestProceduralStrokePoint(
    const FWetProceduralRidgeStroke& Stroke,
    const FVector& WorldPosition,
    const float MaxDistance) const
{
    int32 NearestPointIndex = INDEX_NONE;
    double NearestDistanceSq = FMath::Square(FMath::Max(static_cast<double>(MaxDistance), 0.0));
    for (int32 PointIndex = 0; PointIndex < Stroke.Points.Num(); ++PointIndex)
    {
        FVector PointWorldPosition = FVector::ZeroVector;
        FVector PointWorldNormal = FVector::UpVector;
        if (!ResolveProceduralStrokePointWorld(
                Stroke.Points[PointIndex],
                Stroke.MaterialSlotIndex,
                PointWorldPosition,
                PointWorldNormal))
        {
            continue;
        }

        const double DistanceSq = FVector::DistSquared(PointWorldPosition, WorldPosition);
        if (DistanceSq <= NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestPointIndex = PointIndex;
        }
    }
    return NearestPointIndex;
}

bool SWetWrinkleViewport::ClearTransientProceduralStroke(const bool bRefreshPreview)
{
    const bool bHadVisibleTransientPreview =
        !TransientProceduralStrokeHits.IsEmpty() ||
        EditedProceduralStrokePreview.IsSet() ||
        PendingTransientProceduralStroke.IsSet() ||
        bTransientProceduralPreviewBound ||
        TransientProceduralPreviewState.TextureHandle.IsValid();
    if (!bHadVisibleTransientPreview)
    {
        return false;
    }

    TransientProceduralStrokeHits.Reset();
    bTransientProceduralStartJunction = false;
    bTransientProceduralEndJunction = false;
    bTransientProceduralPreviewBound = false;
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
    ReleaseTransientProceduralPreviewState();
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewTransientParameters();
        Invalidate();
    }
    return true;
}

bool SWetWrinkleViewport::TryBuildSurfaceHitAtUVNearWorldPosition(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    const FVector& ReferenceWorldPosition,
    FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.UV = UV;

    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.IsEmpty() || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FWetWrinkleProjectedSurface* Surface = &ProjectedSurfaces[0];
    double BestDistanceSq = FVector::DistSquared(Surface->WorldPosition, ReferenceWorldPosition);
    for (int32 SurfaceIndex = 1; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const double DistanceSq = FVector::DistSquared(ProjectedSurfaces[SurfaceIndex].WorldPosition, ReferenceWorldPosition);
        if (DistanceSq < BestDistanceSq)
        {
            Surface = &ProjectedSurfaces[SurfaceIndex];
            BestDistanceSq = DistanceSq;
        }
    }

    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = Surface->MaterialSlotIndex;
    OutHit.TriangleID = Surface->TriangleID;
    OutHit.UVIslandID = Surface->UVIslandID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = Surface->WorldPosition;
    OutHit.WorldNormal = Surface->WorldNormal;
    OutHit.WorldTangent = Surface->WorldTangent;
    OutHit.WorldBitangent = Surface->WorldBitangent;
    OutHit.WorldSurfaceFrameU = Surface->WorldSurfaceFrameU;
    OutHit.WorldSurfaceFrameV = Surface->WorldSurfaceFrameV;
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface->WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = Surface->LocalTangent;
    OutHit.LocalBitangent = Surface->LocalBitangent;
    OutHit.LocalSurfaceAxisU = Surface->LocalSurfaceAxisU;
    OutHit.LocalSurfaceAxisV = Surface->LocalSurfaceAxisV;
    OutHit.LocalSurfaceFrameU = Surface->LocalSurfaceFrameU;
    OutHit.LocalSurfaceFrameV = Surface->LocalSurfaceFrameV;
    OutHit.SurfaceUnitsPerUV = Surface->SurfaceUnitsPerUV;
    OutHit.UV = UV;
    OutHit.Barycentric = Surface->Barycentric;
    OutHit.DistanceSq = BestDistanceSq;
    return true;
}

bool SWetWrinkleViewport::TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE ||
        BrushSettings.UVChannelIndex == INDEX_NONE ||
        !SpatialQueryService.IsValid() ||
        !SpatialLease.IsValid() ||
        !SpatialHandle.IsValid() ||
        PreviewMeshComponent == nullptr)
    {
        return false;
    }

    FDWCEditorSurfaceHit SharedHit;
    if (!SpatialQueryService->TraceSurface(
            SpatialHandle,
            PreviewMeshComponent,
            RayOrigin,
            RayDirection,
            SharedHit))
    {
        return false;
    }

    OutHit.bHit = SharedHit.bHit;
    OutHit.MaterialSlotIndex = SharedHit.MaterialSlotIndex;
    OutHit.TriangleID = SharedHit.TriangleID;
    OutHit.UVIslandID = SharedHit.UVIslandID;
    OutHit.UVChannelIndex = SharedHit.UVChannelIndex;
    OutHit.WorldPosition = SharedHit.WorldPosition;
    OutHit.WorldNormal = SharedHit.WorldNormal;
    OutHit.WorldTangent = SharedHit.WorldTangent;
    OutHit.WorldBitangent = SharedHit.WorldBitangent;
    OutHit.WorldSurfaceFrameU = SharedHit.WorldSurfaceFrameU;
    OutHit.WorldSurfaceFrameV = SharedHit.WorldSurfaceFrameV;
    OutHit.LocalPosition = SharedHit.LocalPosition;
    OutHit.LocalNormal = SharedHit.LocalNormal;
    OutHit.LocalTangent = SharedHit.LocalTangent;
    OutHit.LocalBitangent = SharedHit.LocalBitangent;
    OutHit.LocalSurfaceAxisU = SharedHit.LocalSurfaceAxisU;
    OutHit.LocalSurfaceAxisV = SharedHit.LocalSurfaceAxisV;
    OutHit.LocalSurfaceFrameU = SharedHit.LocalSurfaceFrameU;
    OutHit.LocalSurfaceFrameV = SharedHit.LocalSurfaceFrameV;
    OutHit.SurfaceUnitsPerUV = SharedHit.SurfaceUnitsPerUV;
    OutHit.UV = SharedHit.UV;
    OutHit.Barycentric = SharedHit.Barycentric;
    OutHit.DistanceSq = SharedHit.DistanceSq;
    return true;
}

bool SWetWrinkleViewport::HitTestSurface(const FRay& WorldRay, double& OutHitDepth) const
{
    FWetWrinkleSurfaceHit Hit;
    if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        return false;
    }
    OutHitDepth = FVector::Distance(WorldRay.Origin, Hit.WorldPosition);
    return true;
}

bool SWetWrinkleViewport::IsSurfaceHitAnchoredAtDescriptor(
    const FWetWrinkleSurfaceHit& SurfaceHit,
    const FDWCEditorWrinklePatchDescriptor& Descriptor)
{
    if (!SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != Descriptor.MaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != Descriptor.UVChannelIndex ||
        SurfaceHit.TriangleID != Descriptor.AnchorTriangleID)
    {
        return false;
    }

    const FVector3f HitBarycentric(
        static_cast<float>(SurfaceHit.Barycentric.X),
        static_cast<float>(SurfaceHit.Barycentric.Y),
        static_cast<float>(SurfaceHit.Barycentric.Z));
    constexpr float AnchorTolerance = 1.0e-4f;
    return FVector3f::DistSquared(HitBarycentric, Descriptor.AnchorBarycentric) <=
        FMath::Square(AnchorTolerance);
}

bool SWetWrinkleViewport::CanBeginSurfaceInteraction(const FRay& WorldRay, double& OutHitDepth)
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch)
    {
        FWetWrinkleSurfaceHit SurfaceHit;
        if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, SurfaceHit))
        {
            return false;
        }
        OutHitDepth = FVector::Distance(WorldRay.Origin, SurfaceHit.WorldPosition);
        const bool bCanCommitPresentedPatch =
            !bPreviewSuspended &&
            !PatchHoverPreviewState.IsCommitHandoffPending() &&
            PatchHoverPreviewState.bBound &&
            PatchHoverPreviewState.PresentedPayload.IsSet() &&
            PatchHoverPreviewState.PresentedPayload->IsValid() &&
            PatchHoverPreviewState.PresentedPayload->Descriptor.MaterialSlotIndex ==
                BrushSettings.MaterialSlotIndex &&
            PatchHoverPreviewState.PresentedPayload->Descriptor.UVChannelIndex ==
                BrushSettings.UVChannelIndex;
        return bCanCommitPresentedPatch &&
            IsSurfaceHitAnchoredAtDescriptor(
                SurfaceHit,
                PatchHoverPreviewState.PresentedPayload->Descriptor);
    }
    return HitTestSurface(WorldRay, OutHitDepth);
}

void SWetWrinkleViewport::BeginSurfaceInteraction(const FRay& WorldRay)
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch)
    {
        FWetWrinkleSurfaceHit SurfaceHit;
        if (TraceSurface(WorldRay.Origin, WorldRay.Direction, SurfaceHit))
        {
            CommitPresentedPatch(&SurfaceHit);
        }
        return;
    }

    FWetWrinkleSurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->BeginSurfaceInteraction(CurrentSurfaceHit);
        }
    }
}

bool SWetWrinkleViewport::CommitPresentedPatch(const FWetWrinkleSurfaceHit* ExpectedSurfaceHit)
{
    if (bPreviewSuspended || !PatchHoverPreviewState.bBound ||
        PatchHoverPreviewState.IsCommitHandoffPending() ||
        !PatchHoverPreviewState.PresentedPayload.IsSet() ||
        !PatchHoverPreviewState.PresentedPayload->IsValid())
    {
        return false;
    }
    const FWetWrinklePresentedPatchPayload PresentedPayload =
        PatchHoverPreviewState.PresentedPayload.GetValue();
    const FDWCEditorWrinklePatchDescriptor& Descriptor = PresentedPayload.Descriptor;
    if (Descriptor.MaterialSlotIndex != BrushSettings.MaterialSlotIndex ||
        Descriptor.UVChannelIndex != BrushSettings.UVChannelIndex)
    {
        return false;
    }
    if (ExpectedSurfaceHit != nullptr &&
        !IsSurfaceHitAnchoredAtDescriptor(*ExpectedSurfaceHit, Descriptor))
    {
        return false;
    }
    const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin();
    if (!Controller.IsValid())
    {
        return false;
    }
    const FWetWrinklePatchCommitResult CommitResult =
        Controller->CommitPresentedPatch(Descriptor);
    if (!CommitResult.bSucceeded)
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("The presented wrinkle Patch was not committed: %s"),
            CommitResult.FailureReason.IsEmpty() ? TEXT("unknown reason") : *CommitResult.FailureReason);
        return false;
    }

    if (!AppendPresentedPatchToAccumulatedPreview(
            CommitResult.Placement, PresentedPayload.ProjectedPatch))
    {
        if (PatchHoverPreviewState.IsCommitHandoffPending())
        {
            RecoverPatchHoverHandoff(EDWCEditorPreviewInvalidationReason::InvalidPayload);
        }
        else
        {
            FinishPatchHoverHandoff();
        }
    }

    if (PatchHoverPreviewState.PendingTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->CancelTicket(PatchHoverPreviewState.PendingTicket);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->RemoveObserver(PatchHoverPreviewState.PendingPresentationObserver);
        if (PatchHoverPreviewState.bPresentationSwapPending &&
            PatchHoverPreviewState.StagingTextureHandle.IsValid())
        {
            RenderUploadQueue->Cancel(PatchHoverPreviewState.StagingTextureHandle->GetKey());
        }
    }
    PatchHoverPreviewState.PendingTicket = {};
    PatchHoverPreviewState.RequestedDescriptor.Reset();
    PatchHoverPreviewState.PendingPresentedPayload.Reset();
    PatchHoverPreviewState.PendingPresentationUpload = {};
    PatchHoverPreviewState.bPresentationSwapPending = false;
    ++PatchHoverPreviewState.RequestSerial;
    PatchHoverPreviewState.RequestHash = Descriptor.GetStableHash();
    return true;
}

void SWetWrinkleViewport::UpdateSurfaceInteraction(const FRay& WorldRay)
{
    FWetWrinkleSurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->UpdateSurfaceInteraction(CurrentSurfaceHit);
        }
    }
    else
    {
        ClearSurfaceHover();
    }
}

void SWetWrinkleViewport::EndSurfaceInteraction()
{
    if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->EndSurfaceInteraction();
    }
}

void SWetWrinkleViewport::CancelSurfaceInteraction()
{
    if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelSurfaceInteraction();
    }
}

bool SWetWrinkleViewport::UpdateSurfaceHover(const FRay& WorldRay)
{
    FWetWrinkleSurfaceHit Hit;
    if (!TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        ClearSurfaceHover();
        return false;
    }
    HandleSurfaceHitFromClient(Hit);
    return true;
}

void SWetWrinkleViewport::ClearSurfaceHover()
{
    HandleSurfaceHitFromClient(FWetWrinkleSurfaceHit());
}

void SWetWrinkleViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetWrinkleViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetWrinkleViewportClient>(
        PreviewScene.Get(),
        SharedThis(this),
        InputToolsHost.Get());

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetWrinkleViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetWrinkleEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::DWCEditor::CreateDWCViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SWetWrinkleViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Left)
        .Padding(8.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::Get().GetBrush("FloatingBorder"))
                 .Padding(6.0f)
                     [SAssignNew(OverlayText, STextBlock)
                          .ColorAndOpacity(this, &SWetWrinkleViewport::GetViewportHintColor)
                          .Text(GetViewportHintText())]];
}

void SWetWrinkleViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

USkeletalMesh* SWetWrinkleViewport::ResolveTargetMesh() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    return Asset->GetDWCSkeletalMesh() != nullptr
               ? Asset->GetDWCSkeletalMesh()
               : Asset->GetSourceSkeletalMesh();
}

const UWetClothingAsset* SWetWrinkleViewport::ResolveSourceWetClothingAsset() const
{
    return WetClothingAsset.Get();
}

UTexture* SWetWrinkleViewport::ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset != nullptr)
    {
#if WITH_EDITORONLY_DATA
        const FWetClothingAuthoredMaterialSlot* SlotData =
            SourceWetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (SlotData != nullptr && SlotData->bHasSourceTextureSelection)
        {
            return SlotData->SourceTexture.Get();
        }
#endif
    }

    const USkeletalMesh* TargetMesh = ResolveTargetMesh();
    UMaterialInterface* SourceMaterial =
        TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex)
            ? TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface
            : nullptr;
    if (SourceMaterial == nullptr && SourceWetClothingAsset != nullptr)
    {
        SourceMaterial = ResolveSourceMeshMaterialForPreviewSlot(
            TargetMesh,
            SourceWetClothingAsset->GetSourceSkeletalMesh(),
            MaterialSlotIndex);
    }
    if (SourceMaterial != nullptr)
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(SourceMaterial);
    }

    return nullptr;
}

void SWetWrinkleViewport::ApplyPreviewMaterialsToMesh()
{
    if (PreviewMeshComponent == nullptr || !PreviewSession)
    {
        return;
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    const bool bPreviewAllReadySlots = ActiveMaterialSlotIndex == FDWCEditorPreviewSession::AllWettableSlots;
    PreviewSession->SetPreviewMaterialScope(
        bPreviewAllReadySlots
            ? EDWCEditorPreviewMaterialScope::AllWettableSlots
            : EDWCEditorPreviewMaterialScope::SingleSlot,
        ActiveMaterialSlotIndex);
    const TConstArrayView<int32> PreviewMaterialSlotIndices =
        PreviewSession->GetActivePreviewMaterialSlots();
    if (!PreviewSession->IsSuspended())
    {
        if (PreviewOrchestrator)
        {
            PreviewOrchestrator->PreparePreviewMaterials(PreviewMaterialSlotIndices);
        }
        else
        {
            PreviewSession->PreparePreviewMaterials(PreviewMaterialSlotIndices);
        }
    }

    for (const FDWCEditorPreviewSlotState& SlotState : PreviewSession->GetSlotStates().Slots)
    {
        ApplyPreviewMaterialToMeshSlot(
            SlotState.MaterialSlotIndex,
            ResolvePreviewMaterialForSlot(SlotState, PreviewMaterialSlotIndices));
    }

    LastAppliedActivePreviewMaterialSlot = ActiveMaterialSlotIndex;
    bPreviewMaterialsNeedReapply = false;
}

void SWetWrinkleViewport::MarkPreviewMaterialsNeedReapply()
{
    LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    bPreviewMaterialsNeedReapply = true;
}

void SWetWrinkleViewport::RefreshWrinklePreviewMaterials()
{
    if (bPreviewSuspended)
    {
        return;
    }
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != FDWCEditorPreviewSession::AllWettableSlots)
    {
        GetActiveWrinklePreviewMID();
    }

    RefreshWrinklePreviewAccumulatedParameters();
    RefreshWrinklePreviewTransientParameters();
    RefreshWrinklePreviewHoverParameters();
    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}

void SWetWrinkleViewport::RefreshWrinklePreviewAccumulatedParameters()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (PreviewOrchestrator && ActiveMaterialSlotIndex != FDWCEditorPreviewSession::AllWettableSlots)
    {
        PreviewOrchestrator->SetLiveLayer(
            ActiveMaterialSlotIndex,
            BuildAccumulatedPreviewLayer(ActiveMaterialSlotIndex));
    }
}

FDWCEditorPreviewLayer SWetWrinkleViewport::BuildAccumulatedPreviewLayer(
    const int32 MaterialSlotIndex)
{
    UTexture2D* PreviewNormalTexture = nullptr;
    if (GeneratedNormalPreviewTexture != nullptr &&
        GeneratedNormalPreviewMaterialSlotIndex == MaterialSlotIndex &&
        GeneratedNormalPreviewUVChannelIndex == BrushSettings.UVChannelIndex)
    {
        PreviewNormalTexture = GeneratedNormalPreviewTexture.Get();
    }
    else
    {
        UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(MaterialSlotIndex);
        PreviewNormalTexture = ResolveAccumulatedPreviewTexture(
            SourceTexture,
            MaterialSlotIndex,
            BrushSettings.UVChannelIndex);
    }

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleAccumulated;
    Layer.MaterialSlotIndex = MaterialSlotIndex;
    Layer.AddTexture(DWCWetMaterialParameters::WrinkleNormalMap(), nullptr);
    Layer.AddScalar(DWCWetMaterialParameters::UseWrinkleNormalMap(), 0.0f);
    Layer.AddTexture(WetWrinklePreviewMaterialParameters::AccumulatedNormal, PreviewNormalTexture);
    Layer.AddScalar(
        WetWrinklePreviewMaterialParameters::AccumulatedEnabled,
        PreviewNormalTexture != nullptr ? 1.0f : 0.0f);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f, 1.0f);
    return Layer;
}

void SWetWrinkleViewport::RefreshWrinklePreviewTransientParameters()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (PreviewOrchestrator && ActiveMaterialSlotIndex != FDWCEditorPreviewSession::AllWettableSlots)
    {
        PreviewOrchestrator->SetLiveLayer(
            ActiveMaterialSlotIndex,
            BuildTransientPreviewLayer(ActiveMaterialSlotIndex));
    }
}

FDWCEditorPreviewLayer SWetWrinkleViewport::BuildTransientPreviewLayer(
    const int32 MaterialSlotIndex) const
{
    const bool bEnabled = BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        (!TransientProceduralStrokeHits.IsEmpty() || EditedProceduralStrokePreview.IsSet()) &&
        TransientProceduralPreviewState.MaterialSlotIndex == MaterialSlotIndex &&
        TransientProceduralPreviewState.UVChannelIndex == BrushSettings.UVChannelIndex &&
        TransientProceduralPreviewState.TextureHandle.IsValid() &&
        TransientProceduralPreviewState.TextureHandle->GetTexture() != nullptr;

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleTransient;
    Layer.MaterialSlotIndex = MaterialSlotIndex;
    Layer.AddTexture(
        WetWrinklePreviewMaterialParameters::TransientRidgeNormal,
        bEnabled ? TransientProceduralPreviewState.TextureHandle->GetTexture() : nullptr);
    Layer.AddScalar(
        WetWrinklePreviewMaterialParameters::TransientRidgeEnabled,
        bEnabled ? 1.0f : 0.0f);
    return Layer;
}

void SWetWrinkleViewport::RefreshWrinklePreviewHoverParameters()
{
    const bool bShouldProject = BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
        BrushSettings.bShowPreview && CurrentSurfaceHit.bHit &&
        BrushSettings.WrinkleNormalTexture != nullptr;
    if (bShouldProject)
    {
        SchedulePatchHoverPreview();
    }
    else
    {
        InvalidatePatchHoverRequest();
        return;
    }
    ApplyPatchHoverPreviewLayer();
}

UMaterialInterface* SWetWrinkleViewport::ResolvePreviewMaterialForSlot(
    const FDWCEditorPreviewSlotState& SlotState,
    const TConstArrayView<int32> PreviewMaterialSlotIndices) const
{
    UMaterialInterface* MaterialToApply = SlotState.SourceMaterial.Get();
    const bool bUsePreviewMaterial = SlotState.bPreviewReady &&
        PreviewMaterialSlotIndices.Contains(SlotState.MaterialSlotIndex);
    if (bUsePreviewMaterial)
    {
        const FDWCEditorPreviewSessionSlot* PreviewSlot =
            PreviewSession->FindSlot(SlotState.MaterialSlotIndex);
        if (UMaterialInstanceDynamic* PreviewMID =
                PreviewSlot != nullptr ? PreviewSlot->PreviewMID.Get() : nullptr)
        {
            MaterialToApply = PreviewMID;
        }
    }

    return MaterialToApply != nullptr
               ? MaterialToApply
               : UMaterial::GetDefaultMaterial(MD_Surface);
}

bool SWetWrinkleViewport::ApplyPreviewMaterialToMeshSlot(
    const int32 MaterialSlotIndex,
    UMaterialInterface* MaterialToApply)
{
    if (PreviewMeshComponent == nullptr || MaterialSlotIndex < 0)
    {
        return false;
    }

    ++PreviewMaterialAssignmentCheckCount;
    const FDWCEditorPreviewMaterialBindingDecision BindingDecision = PreviewMaterialBindingCache.Evaluate(
        PreviewMeshComponent->GetSkeletalMeshAsset(),
        PreviewMeshComponent->GetNumMaterials(),
        MaterialSlotIndex,
        MaterialToApply,
        PreviewMeshComponent->GetMaterial(MaterialSlotIndex));
    if (!BindingDecision.bNeedsAssignment)
    {
        ++PreviewMaterialAssignmentSkipCount;
        if (BindingDecision.bCacheMatched)
        {
            ++PreviewMaterialAssignmentCacheHitCount;
        }
        return false;
    }

    PreviewMeshComponent->SetMaterial(MaterialSlotIndex, MaterialToApply);
    PreviewMaterialBindingCache.RecordApplied(
        PreviewMeshComponent->GetSkeletalMeshAsset(),
        PreviewMeshComponent->GetNumMaterials(),
        MaterialSlotIndex,
        MaterialToApply);
    ++PreviewMaterialAssignmentWriteCount;
    return true;
}

void SWetWrinkleViewport::InvalidatePreviewMaterialAssignmentCache()
{
    PreviewMaterialBindingCache.Reset();
}

void SWetWrinkleViewport::ApplyPatchHoverPreviewLayer()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (PreviewOrchestrator && ActiveMaterialSlotIndex != FDWCEditorPreviewSession::AllWettableSlots)
    {
        PreviewOrchestrator->SetLiveLayer(
            ActiveMaterialSlotIndex,
            BuildHoverPreviewLayer(ActiveMaterialSlotIndex));
    }
}

FDWCEditorPreviewLayer SWetWrinkleViewport::BuildHoverPreviewLayer(
    const int32 MaterialSlotIndex) const
{
    const bool bEnableHover = PatchHoverPreviewState.bBound &&
        PatchHoverPreviewState.MaterialSlotIndex == MaterialSlotIndex &&
        PatchHoverPreviewState.UVChannelIndex == BrushSettings.UVChannelIndex &&
        PatchHoverPreviewState.TextureHandle.IsValid() &&
        PatchHoverPreviewState.TextureHandle->GetTexture() != nullptr;

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleHover;
    Layer.MaterialSlotIndex = MaterialSlotIndex;
    Layer.AddTexture(
        WetWrinklePreviewMaterialParameters::HoverNormal,
        bEnableHover ? PatchHoverPreviewState.TextureHandle->GetTexture() : nullptr);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverEnabled, bEnableHover ? 1.0f : 0.0f);
    return Layer;
}

UMaterialInstanceDynamic* SWetWrinkleViewport::GetActiveWrinklePreviewMID(
    const bool bCreateIfMissing)
{
    if (!PreviewSession || PreviewSession->IsSuspended())
    {
        return nullptr;
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex == FDWCEditorPreviewSession::AllWettableSlots)
    {
        return nullptr;
    }

    const FDWCEditorPreviewSessionSlot* Slot = PreviewSession->FindSlot(ActiveMaterialSlotIndex);
    if (Slot == nullptr || !Slot->Eligibility.bPreviewReady)
    {
        return nullptr;
    }
    if (UMaterialInstanceDynamic* ExistingMID = Slot->PreviewMID.Get())
    {
        return ExistingMID;
    }
    if (!bCreateIfMissing)
    {
        return nullptr;
    }

    const FDWCEditorPreviewSessionMaterialResult Result =
        PreviewSession->GetOrCreatePreviewMaterial(ActiveMaterialSlotIndex);
    if (Result.PreviewMID != nullptr && Result.bCreated)
    {
        MarkPreviewMaterialsNeedReapply();
    }
    return Result.PreviewMID;
}

bool SWetWrinkleViewport::AppendPresentedPatchToAccumulatedPreview(
    const FWetWrinklePatchPlacement& Stamp,
    const FDWCEditorProjectedNormalPatchCommand& ProjectedPatch)
{
    const int32 DataUVChannelIndex = WetClothingAsset.IsValid()
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (Stamp.MaterialSlotIndex == INDEX_NONE || DataUVChannelIndex < 0 ||
        !ProjectedPatch.IsValid())
    {
        return false;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(Stamp.MaterialSlotIndex);
    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(SourceTexture, Stamp.MaterialSlotIndex, DataUVChannelIndex);
    if (PreviewState == nullptr)
    {
        return false;
    }
    ++PreviewState->ContentRevision;
    PatchHoverPreviewState.HandoffState =
        EWetWrinklePatchHandoffState::AwaitingAccumulatedCommit;
    PatchHoverPreviewState.HandoffRecoveryAttempts = 0;
    PatchHoverPreviewState.PendingAccumulatedUpload = {};
    PatchHoverPreviewState.PendingAccumulatedSequence = 0;
    PatchHoverPreviewState.PendingAccumulatedContentRevision = PreviewState->ContentRevision;

    auto RebuildForHandoff = [this, PreviewState, &Stamp, DataUVChannelIndex](
        const EDWCEditorPreviewInvalidationReason Reason)
    {
        PatchHoverPreviewState.HandoffState =
            EWetWrinklePatchHandoffState::RecoveringFullRebuild;
        PreviewState->bDirty = true;
        PreviewState->Recovery.Invalidate(Reason);
        const bool bScheduled = RebuildAccumulatedPreviewTexture(*PreviewState);
        const bool bHasPendingRebuild =
            PreviewState->bRebuildPending || PreviewState->PendingTicket.IsValid();
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
        return bScheduled && bHasPendingRebuild;
    };

    if (PreviewState->bDirty || !PreviewState->TextureHandle.IsValid() ||
        PreviewState->TextureHandle->GetTexture() == nullptr)
    {
        return RebuildForHandoff(EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    }

    PreviewState->TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState->WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState->TextureSize);
    if (PreviewState->TextureSize.X <= 0 || PreviewState->TextureSize.Y <= 0 ||
        PreviewState->TextureHandle->GetDescriptor().Size != PreviewState->TextureSize)
    {
        return RebuildForHandoff(EDWCEditorPreviewInvalidationReason::ResolutionChanged);
    }

    const FDWCEditorNormalRasterSurface& WorkingSurface =
        PreviewState->TextureHandle->GetWorkingNormalSurface();
    if (PreviewState->TextureHandle->GetBGRA8Pixels().Num() !=
            PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != PreviewState->WorkingTextureSize)
    {
        return RebuildForHandoff(EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
    }

    FWetWrinkleIncrementalCommand Delta;
    Delta.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    Delta.ProjectedPatch = ProjectedPatch;
    QueueAccumulatedIncrementalCommand(*PreviewState, MoveTemp(Delta));
    PatchHoverPreviewState.PendingAccumulatedSequence = PreviewState->NextIncrementalSequence;
    if (PreviewState->PendingIncrementalTicket.IsValid())
    {
        PatchHoverPreviewState.HandoffState =
            EWetWrinklePatchHandoffState::AwaitingAccumulatedCommit;
    }
    else if (PreviewState->bRebuildPending || PreviewState->PendingTicket.IsValid())
    {
        PatchHoverPreviewState.HandoffState =
            EWetWrinklePatchHandoffState::RecoveringFullRebuild;
    }
    else
    {
        return RebuildForHandoff(EDWCEditorPreviewInvalidationReason::SchedulerDeferred);
    }
    PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
    return true;
}

void SWetWrinkleViewport::CancelPatchHoverAsyncWork()
{
    if (PatchHoverPreviewState.PendingTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->CancelTicket(PatchHoverPreviewState.PendingTicket);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->RemoveObserver(PatchHoverPreviewState.PendingPresentationObserver);
        if (PatchHoverPreviewState.bPresentationSwapPending &&
            PatchHoverPreviewState.StagingTextureHandle.IsValid())
        {
            RenderUploadQueue->Cancel(PatchHoverPreviewState.StagingTextureHandle->GetKey());
        }
    }

    PatchHoverPreviewState.PendingTicket = {};
    PatchHoverPreviewState.PendingPresentationUpload = {};
    PatchHoverPreviewState.PendingPresentationObserver = {};
    PatchHoverPreviewState.PendingPerformanceDiagnostics.Reset();
    PatchHoverPreviewState.bPresentationSwapPending = false;
}

void SWetWrinkleViewport::ClearPatchHoverPresentation(const bool bPreserveCommitHandoff)
{
    const bool bKeepCommitHandoff =
        bPreserveCommitHandoff && PatchHoverPreviewState.IsCommitHandoffPending();
    CancelPatchHoverAsyncWork();

    ++PatchHoverPreviewState.RequestSerial;
    PatchHoverPreviewState.RequestedDescriptor.Reset();
    PatchHoverPreviewState.PendingPresentedPayload.Reset();
    if (!bKeepCommitHandoff)
    {
        PatchHoverPreviewState.bBound = false;
        PatchHoverPreviewState.PresentedPayload.Reset();
        PatchHoverPreviewState.RequestHash = 0;
        PatchHoverPreviewState.PendingAccumulatedUpload = {};
        PatchHoverPreviewState.PendingAccumulatedSequence = 0;
        PatchHoverPreviewState.PendingAccumulatedContentRevision = 0;
    }

    ApplyPatchHoverPreviewLayer();
}

void SWetWrinkleViewport::InvalidatePatchHoverRequest()
{
    ClearPatchHoverPresentation(true);
}

void SWetWrinkleViewport::HandlePatchHoverUploadStatus(
    const FDWCEditorTextureUploadTicket& UploadTicket,
    const uint64 RequestSerial,
    const EDWCEditorTextureUploadStatus Status)
{
    FWetWrinklePatchHoverPreviewState& State = PatchHoverPreviewState;
    const bool bMatchingTicket = State.PendingPresentationUpload.State == UploadTicket.State &&
        State.PendingPresentationUpload.ResourceGeneration == UploadTicket.ResourceGeneration &&
        State.PendingPresentationUpload.ContentRevision == UploadTicket.ContentRevision;
    if (!bMatchingTicket || State.RequestSerial != RequestSerial || !State.bPresentationSwapPending)
    {
        return;
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->RemoveObserver(State.PendingPresentationObserver);
    }

    const bool bCanPresent = Status == EDWCEditorTextureUploadStatus::RenderEnqueued &&
        State.PendingPresentedPayload.IsSet() &&
        State.PendingPresentedPayload->IsValid() &&
        State.StagingTextureHandle.IsValid();
    if (!bCanPresent)
    {
        State.PendingPerformanceDiagnostics.Reset();
        State.PendingPresentedPayload.Reset();
        State.PendingPresentationUpload = {};
        State.bPresentationSwapPending = false;
        State.RequestHash = State.PresentedPayload.IsSet()
            ? State.PresentedPayload->Descriptor.GetStableHash()
            : 0;
        SchedulePatchHoverPreview();
        return;
    }

    TOptional<FWetWrinkleHoverPerformanceDiagnostics> CompletedDiagnostics;
    if (State.PendingPerformanceDiagnostics.IsSet())
    {
        CompletedDiagnostics = MoveTemp(State.PendingPerformanceDiagnostics.GetValue());
        State.PendingPerformanceDiagnostics.Reset();
        const double NowSeconds = FPlatformTime::Seconds();
        FWetWrinkleHoverPerformanceDiagnostics& Diagnostics = CompletedDiagnostics.GetValue();
        Diagnostics.UploadWaitMs = Diagnostics.CommitFinishedSeconds > 0.0
            ? (NowSeconds - Diagnostics.CommitFinishedSeconds) * 1000.0
            : 0.0;
        FDWCEditorTextureUploadTiming UploadTiming;
        if (RenderUploadQueue.IsValid())
        {
            RenderUploadQueue->GetStatus(UploadTicket);
            if (RenderUploadQueue->GetTiming(UploadTicket, UploadTiming))
            {
                Diagnostics.UploadQueueWaitMs = UploadTiming.QueueWaitMs;
                Diagnostics.UploadSliceDelayMs = UploadTiming.SliceDelayMs;
                Diagnostics.UploadStagingCopyMs = UploadTiming.StagingCopyMs;
                Diagnostics.UploadSubmitCallMs = UploadTiming.SubmitCallMs;
                Diagnostics.UploadPollDelayMs = UploadTiming.SubmittedToObservedMs;
                Diagnostics.UploadRenderCallbackLatencyMs = UploadTiming.RenderCallbackLatencyMs;
                Diagnostics.UploadBytes = UploadTiming.SubmittedBytes;
                Diagnostics.UploadPreparedPayloadBytes = UploadTiming.PreparedPayloadBytes;
                Diagnostics.UploadAvoidedStagingCopyBytes = UploadTiming.AvoidedStagingCopyBytes;
                Diagnostics.UploadRequestedRegionCount = UploadTiming.RequestedRegionCount;
                Diagnostics.UploadSubmittedRegionCount = UploadTiming.SubmittedRegionCount;
                Diagnostics.UploadCompletedRegionCount = UploadTiming.CompletedRegionCount;
                Diagnostics.UploadCoalescedRequestCount = UploadTiming.CoalescedRequestCount;
                Diagnostics.UploadQueueDepthAtSelection = UploadTiming.QueueDepthAtSelection;
                Diagnostics.bFullTextureUpload = UploadTiming.bFullTextureUpload;
                Diagnostics.bUsedPreparedUpload = UploadTiming.bUsedPreparedPayload;
            }
        }
    }

    const double PresentationStartSeconds = FPlatformTime::Seconds();
    Swap(State.TextureHandle, State.StagingTextureHandle);
    Swap(State.FrontOutputRects, State.StagingOutputRects);
    State.PresentedPayload = MoveTemp(State.PendingPresentedPayload.GetValue());
    State.PendingPresentedPayload.Reset();
    State.PendingPresentationUpload = {};
    State.bPresentationSwapPending = false;
    State.bBound = true;
    ApplyPatchHoverPreviewLayer();

    if (CompletedDiagnostics.IsSet())
    {
        FWetWrinkleHoverPerformanceDiagnostics Diagnostics =
            MoveTemp(CompletedDiagnostics.GetValue());
        const double NowSeconds = FPlatformTime::Seconds();
        Diagnostics.PresentationSwapMs =
            (NowSeconds - PresentationStartSeconds) * 1000.0;
        Diagnostics.EndToEndMs = Diagnostics.RequestStartSeconds > 0.0
            ? (NowSeconds - Diagnostics.RequestStartSeconds) * 1000.0
            : 0.0;
        RecordPatchHoverDiagnostics(MoveTemp(Diagnostics));
    }
    SchedulePatchHoverPreview();
}

void SWetWrinkleViewport::AppendAccumulatedPreviewProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    const int32 DataUVChannelIndex = WetClothingAsset.IsValid()
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (!Stroke.bEnabled || Stroke.MaterialSlotIndex == INDEX_NONE || DataUVChannelIndex < 0 || Stroke.Points.Num() < 2)
    {
        return;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(Stroke.MaterialSlotIndex);
    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(SourceTexture, Stroke.MaterialSlotIndex, DataUVChannelIndex);
    if (PreviewState == nullptr)
    {
        return;
    }
    ++PreviewState->ContentRevision;

    if (PreviewState->bDirty || !PreviewState->TextureHandle.IsValid() ||
        PreviewState->TextureHandle->GetTexture() == nullptr)
    {
        PreviewState->Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    const FDWCEditorNormalRasterSurface& WorkingSurface =
        PreviewState->TextureHandle->GetWorkingNormalSurface();
    if (PreviewState->TextureHandle->GetBGRA8Pixels().Num() !=
            PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != PreviewState->WorkingTextureSize)
    {
        PreviewState->Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
        RebuildAccumulatedPreviewTexture(*PreviewState);
        return;
    }

    FWetWrinkleIncrementalCommand Delta;
    Delta.Kind = EWetWrinkleIncrementalCommandKind::Ridge;
    Delta.Ridge = Stroke;
    QueueAccumulatedIncrementalCommand(*PreviewState, MoveTemp(Delta));
    PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, DataUVChannelIndex);
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStates()
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        ReleaseAccumulatedPreviewStateResources(PreviewState, true);
    }
    AccumulatedPreviewStates.Reset();
    AccumulatedPreviewUseSerial = 0;
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStateResources(
    FWetWrinkleAccumulatedPreviewState& PreviewState,
    const bool bClearMaterialBinding)
{
    if (PreviewState.bRebuildPending && WorkerJobScheduler.IsValid())
    {
        // A slot that is no longer visible must not keep an obsolete worker
        // snapshot alive. The completion callback rejects the cleared ticket.
        WorkerJobScheduler->Cancel(PreviewState.PendingTicket.Key);
    }
    InvalidateAccumulatedIncrementalState(PreviewState);

    if (bClearMaterialBinding && PreviewOrchestrator)
    {
        FDWCEditorPreviewLayer Layer;
        Layer.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleAccumulated;
        Layer.MaterialSlotIndex = PreviewState.MaterialSlotIndex;
        Layer.AddTexture(WetWrinklePreviewMaterialParameters::AccumulatedNormal, nullptr);
        Layer.AddScalar(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f);
        Layer.AddScalar(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f, 1.0f);
        PreviewOrchestrator->SetLiveLayer(PreviewState.MaterialSlotIndex, MoveTemp(Layer));
    }

    if (TextureWorkspace.IsValid() && PreviewState.TextureHandle.IsValid())
    {
        TextureWorkspace->Discard(PreviewState.TextureHandle);
    }
    PreviewState.TextureHandle.Reset();
    // A released inactive slot has no CPU working surface to incrementally
    // update. Force a worker rebuild when that slot becomes active again.
    PreviewState.bDirty = true;
    PreviewState.Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
    PreviewState.bRebuildPending = false;
    PreviewState.PendingTicket = {};
    PreviewState.PendingContentRevision = 0;
}

void SWetWrinkleViewport::PrepareAccumulatedPreviewStatesForSlot(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.MaterialSlotIndex == MaterialSlotIndex &&
            PreviewState.UVChannelIndex == UVChannelIndex)
        {
            PreviewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
            continue;
        }

        // Workspace entries are shared with this state, so their LRU cannot evict
        // them while the state still owns a handle. Drop inactive slot buffers here
        // and rebuild them on demand when the user returns to that material slot.
        ReleaseAccumulatedPreviewStateResources(PreviewState, true);
    }

    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
}

void SWetWrinkleViewport::PruneAccumulatedPreviewStates(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    (void)MaterialSlotIndex;
    (void)UVChannelIndex;
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->TrimToBudget();
    }
}

void SWetWrinkleViewport::ResetTransientProceduralPreviewResources()
{
    if (TransientProceduralPreviewState.PendingIncrementalTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(TransientProceduralPreviewState.PendingIncrementalTicket.Key);
    }
    if (TextureWorkspace.IsValid() && TransientProceduralPreviewState.TextureHandle.IsValid())
    {
        TextureWorkspace->Discard(TransientProceduralPreviewState.TextureHandle);
    }
    TransientProceduralPreviewState = FWetProceduralRidgeTransientPreviewState();
    bTransientProceduralPreviewBound = false;
}

void SWetWrinkleViewport::ReleaseTransientProceduralPreviewState()
{
    ResetTransientProceduralPreviewResources();
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
}

bool SWetWrinkleViewport::EnsureTransientProceduralPreviewState(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0)
    {
        return false;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(MaterialSlotIndex);
    const FIntPoint TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    const FIntPoint WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(TextureSize);
    const int32 PixelCount = TextureSize.X * TextureSize.Y;
    const int32 WorkingPixelCount = WorkingTextureSize.X * WorkingTextureSize.Y;
    if (TextureSize.X <= 0 || TextureSize.Y <= 0 || PixelCount <= 0 || WorkingPixelCount <= 0)
    {
        return false;
    }

    const bool bNeedsNewState =
        !TransientProceduralPreviewState.TextureHandle.IsValid() ||
        TransientProceduralPreviewState.SourceTexture.Get() != SourceTexture ||
        TransientProceduralPreviewState.MaterialSlotIndex != MaterialSlotIndex ||
        TransientProceduralPreviewState.UVChannelIndex != UVChannelIndex ||
        TransientProceduralPreviewState.TextureSize != TextureSize ||
        TransientProceduralPreviewState.WorkingTextureSize != WorkingTextureSize ||
        TransientProceduralPreviewState.TextureHandle->GetBGRA8Pixels().Num() != PixelCount ||
        !TransientProceduralPreviewState.TextureHandle->GetWorkingNormalSurface().IsValid() ||
        TransientProceduralPreviewState.TextureHandle->GetWorkingNormalSurface().Size != WorkingTextureSize;
    if (!bNeedsNewState)
    {
        return true;
    }

    const FColor FlatNormal = EncodeWetWrinkleNormal(FVector(0.0f, 0.0f, 1.0f));
    // The edited stroke is logical tool input and survives resource recreation.
    ResetTransientProceduralPreviewResources();
    TransientProceduralPreviewState.SourceTexture = SourceTexture;
    TransientProceduralPreviewState.MaterialSlotIndex = MaterialSlotIndex;
    TransientProceduralPreviewState.UVChannelIndex = UVChannelIndex;
    TransientProceduralPreviewState.TextureSize = TextureSize;
    TransientProceduralPreviewState.WorkingTextureSize = WorkingTextureSize;
    if (!TextureWorkspace.IsValid())
    {
        ReleaseTransientProceduralPreviewState();
        return false;
    }
    TArray<FColor> Pixels;
    Pixels.Init(FlatNormal, PixelCount);
    FDWCEditorNormalRasterSurface WorkingSurface;
    WorkingSurface.Initialize(WorkingTextureSize, false);
    FDWCEditorPreviewCommitContext CommitContext;
    CommitContext.ConsumerToken = PreviewCommitLifetime.CaptureToken();
    CommitContext.DebugName = TEXT("Transient procedural wrinkle preview");
    CommitContext.IsCurrent = [this]() { return !bPreviewSuspended; };
    const EDWCEditorPreviewCommitResult CommitResult = PreviewCommitCoordinator.IsValid()
        ? PreviewCommitCoordinator->CommitNormalBGRA8(
        CommitContext,
        MakeWrinkleTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::WrinkleProcedural,
            MaterialSlotIndex),
        MakeWrinkleNormalDescriptor(TextureSize, WorkingTextureSize),
        MoveTemp(Pixels),
        MoveTemp(WorkingSurface),
        TransientProceduralPreviewState.TextureHandle,
        EDWCEditorTextureUploadPriority::Interactive)
        : EDWCEditorPreviewCommitResult::CoordinatorShutdown;
    if (CommitResult != EDWCEditorPreviewCommitResult::Applied)
    {
        ResetTransientProceduralPreviewResources();
        return false;
    }

    return true;
}

bool SWetWrinkleViewport::UpdateTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke)
{
    return ScheduleTransientProceduralPreview(Stroke);
}

void SWetWrinkleViewport::ResetPatchHoverPreviewState()
{
    ClearPatchHoverPresentation(false);
    if (TextureWorkspace.IsValid() && PatchHoverPreviewState.TextureHandle.IsValid())
    {
        TextureWorkspace->Discard(PatchHoverPreviewState.TextureHandle);
    }
    if (TextureWorkspace.IsValid() && PatchHoverPreviewState.StagingTextureHandle.IsValid())
    {
        TextureWorkspace->Discard(PatchHoverPreviewState.StagingTextureHandle);
    }
    PatchHoverPreviewState = FWetWrinklePatchHoverPreviewState();
    ApplyPatchHoverPreviewLayer();
}

void SWetWrinkleViewport::RecordPatchHoverDiagnostics(
    FWetWrinkleHoverPerformanceDiagnostics Diagnostics)
{
    constexpr int32 MaxHistoryEntries = 120;
    PatchHoverDiagnosticsHistory.Add(Diagnostics);
    if (PatchHoverDiagnosticsHistory.Num() > MaxHistoryEntries)
    {
        PatchHoverDiagnosticsHistory.RemoveAt(
            0,
            PatchHoverDiagnosticsHistory.Num() - MaxHistoryEntries,
            EAllowShrinking::No);
    }

    const int32 DiagnosticsLevel = CVarDWCWrinkleHoverDiagnostics.GetValueOnGameThread();
    const double NowSeconds = FPlatformTime::Seconds();
    const bool bSlow = Diagnostics.EndToEndMs >=
        CVarDWCWrinkleHoverSlowThresholdMs.GetValueOnGameThread();
    const double LogInterval = FMath::Max(
        static_cast<double>(CVarDWCWrinkleHoverLogIntervalSeconds.GetValueOnGameThread()),
        0.0);
    if (DiagnosticsLevel <= 0 || (DiagnosticsLevel == 1 && !bSlow) ||
        NowSeconds - LastPatchHoverDiagnosticsLogSeconds < LogInterval)
    {
        return;
    }
    LastPatchHoverDiagnosticsLogSeconds = NowSeconds;

    struct FStageTiming
    {
        const TCHAR* Name;
        double Milliseconds;
    };
    const FStageTiming Stages[] = {
        {TEXT("phase-a-admission"), Diagnostics.AdmissionWaitMs},
        {TEXT("phase-b-admission"), Diagnostics.RasterAdmissionWaitMs},
        {TEXT("projection"), Diagnostics.ProjectionMs},
        {TEXT("region-plan"), Diagnostics.RegionPlanMs + Diagnostics.RegionAllocationMs},
        {TEXT("raster"), Diagnostics.RasterMs},
        {TEXT("encode"), Diagnostics.ResampleEncodeMs},
        {TEXT("commit"), Diagnostics.CommitMs},
        {TEXT("upload-queue"), Diagnostics.UploadQueueWaitMs},
        {TEXT("upload-slice-delay"), Diagnostics.UploadSliceDelayMs},
        {TEXT("upload-staging"), Diagnostics.UploadStagingCopyMs},
        {TEXT("upload-submit"), Diagnostics.UploadSubmitCallMs},
        {TEXT("upload-poll"), Diagnostics.UploadPollDelayMs},
        {TEXT("present-swap"), Diagnostics.PresentationSwapMs}
    };
    const FStageTiming* Bottleneck = &Stages[0];
    for (const FStageTiming& Stage : Stages)
    {
        if (Stage.Milliseconds > Bottleneck->Milliseconds)
        {
            Bottleneck = &Stage;
        }
    }

    const TCHAR* ModeName = Diagnostics.ProjectionMode == EWetWrinklePatchProjectionMode::SurfaceDecal
        ? TEXT("UVSeam")
        : TEXT("NonUVSeam");
    const TCHAR* UploadPlanName = TEXT("bounded");
    if (Diagnostics.UploadPlan == EDWCEditorSparseUploadPlan::Sparse)
    {
        UploadPlanName = TEXT("sparse");
    }
    else if (Diagnostics.UploadPlan == EDWCEditorSparseUploadPlan::MergedSparse)
    {
        UploadPlanName = TEXT("merged-sparse");
    }
    UE_LOG(
        LogWetWrinklePreviewViewport,
        Display,
        TEXT("Wrinkle hover perf: request=%llu slot=%d mode=%s size=%.1fcm total=%.2fms "
             "prepare={descriptor=%.2f,source=%.2f} worker={phaseAWait=%.2f,phaseBWait=%.2f,projection=%.2f,plan=%.2f,bin=%.2f,alloc=%.2f,raster=%.2f,encode=%.2f,direct=%.2f,resample=%.2f,total=%.2f} "
             "present={commit=%.2f,uploadTotal=%.2f,queue=%.2f,sliceDelay=%.2f,staging=%.2f,submit=%.2f,poll=%.2f,renderCallback=%.2f,swap=%.2f,bytes=%llu,regions=%u/%u/%u,coalesced=%u,queueDepth=%u,full=%s,prepared=%s,payload=%llu,avoided=%llu} "
             "geometry={visited=%d,fragments=%d,regions=%d,rowRefs=%llu,parallel=%s/%d} "
             "regions={mode=%s,source=%d,planned=%d,current=%d,previous=%d,clear=%d,refs=%llu,boundedPixels=%llu,sparsePixels=%llu,plannedPixels=%llu} "
             "pixels={candidates=%llu,affected=%llu,working=%llu,output=%llu} encodePath={direct=%s,parallel=%s} "
             "memory={phaseA=%.2fMiB,projectionWorking=%.2fMiB,projectionPrivate=%.2fMiB,shared=%.2fMiB,retained=%.2fMiB,phaseB=%.2fMiB,result=%.2fMiB} bottleneck=%s(%.2fms)"),
        Diagnostics.RequestId,
        Diagnostics.MaterialSlotIndex,
        ModeName,
        Diagnostics.BrushDiameterLocal,
        Diagnostics.EndToEndMs,
        Diagnostics.DescriptorBuildMs,
        Diagnostics.TextureResolveMs,
        Diagnostics.AdmissionWaitMs,
        Diagnostics.RasterAdmissionWaitMs,
        Diagnostics.ProjectionMs,
        Diagnostics.RegionPlanMs,
        Diagnostics.TileBinningMs,
        Diagnostics.RegionAllocationMs,
        Diagnostics.RasterMs,
        Diagnostics.ResampleEncodeMs,
        Diagnostics.DirectEncodeMs,
        Diagnostics.NormalAwareResampleMs,
        Diagnostics.WorkerTotalMs,
        Diagnostics.CommitMs,
        Diagnostics.UploadWaitMs,
        Diagnostics.UploadQueueWaitMs,
        Diagnostics.UploadSliceDelayMs,
        Diagnostics.UploadStagingCopyMs,
        Diagnostics.UploadSubmitCallMs,
        Diagnostics.UploadPollDelayMs,
        Diagnostics.UploadRenderCallbackLatencyMs,
        Diagnostics.PresentationSwapMs,
        Diagnostics.UploadBytes,
        Diagnostics.UploadRequestedRegionCount,
        Diagnostics.UploadSubmittedRegionCount,
        Diagnostics.UploadCompletedRegionCount,
        Diagnostics.UploadCoalescedRequestCount,
        Diagnostics.UploadQueueDepthAtSelection,
        Diagnostics.bFullTextureUpload ? TEXT("yes") : TEXT("no"),
        Diagnostics.bUsedPreparedUpload ? TEXT("yes") : TEXT("no"),
        Diagnostics.UploadPreparedPayloadBytes,
        Diagnostics.UploadAvoidedStagingCopyBytes,
        Diagnostics.VisitedTriangleCount,
        Diagnostics.ProjectedFragmentCount,
        Diagnostics.DirtyRegionCount,
        Diagnostics.RowReferenceCount,
        Diagnostics.bUsedParallelRaster ? TEXT("yes") : TEXT("no"),
        Diagnostics.ParallelRowCount,
        UploadPlanName,
        Diagnostics.SourceUploadRegionCount,
        Diagnostics.DirtyRegionCount,
        Diagnostics.CurrentTileCount,
        Diagnostics.PreviousTileCount,
        Diagnostics.ClearOnlyTileCount,
        Diagnostics.TileFragmentReferenceCount,
        Diagnostics.BoundedOutputPixelCount,
        Diagnostics.SparseOutputPixelCount,
        Diagnostics.PlannedOutputPixelCount,
        Diagnostics.CandidatePixelCount,
        Diagnostics.AffectedPixelCount,
        Diagnostics.DirtyWorkingPixelCount,
        Diagnostics.EncodedOutputPixelCount,
        Diagnostics.bUsedDirectEncode ? TEXT("yes") : TEXT("no"),
        Diagnostics.bUsedParallelEncode ? TEXT("yes") : TEXT("no"),
        static_cast<double>(Diagnostics.EstimatedMemoryBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.ProjectionWorkingSetBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.ProjectionPrivateResultBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.SharedResidentBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.RetainedPhaseBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.RasterPhaseMemoryBytes) / (1024.0 * 1024.0),
        static_cast<double>(Diagnostics.ActualResultBytes) / (1024.0 * 1024.0),
        Bottleneck->Name,
        Bottleneck->Milliseconds);
}

bool SWetWrinkleViewport::EnsurePatchHoverPreviewState(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0 ||
        !TextureWorkspace.IsValid() || !PreviewCommitCoordinator.IsValid())
    {
        return false;
    }
    const FIntPoint TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    const FIntPoint WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(TextureSize);
    const bool bReusable = PatchHoverPreviewState.TextureHandle.IsValid() &&
        PatchHoverPreviewState.StagingTextureHandle.IsValid() &&
        PatchHoverPreviewState.MaterialSlotIndex == MaterialSlotIndex &&
        PatchHoverPreviewState.UVChannelIndex == UVChannelIndex &&
        PatchHoverPreviewState.TextureSize == TextureSize &&
        PatchHoverPreviewState.WorkingTextureSize == WorkingTextureSize;
    if (bReusable)
    {
        return true;
    }

    ResetPatchHoverPreviewState();
    if (TextureSize.X <= 0 || TextureSize.Y <= 0 ||
        WorkingTextureSize.X <= 0 || WorkingTextureSize.Y <= 0)
    {
        return false;
    }
    PatchHoverPreviewState.MaterialSlotIndex = MaterialSlotIndex;
    PatchHoverPreviewState.UVChannelIndex = UVChannelIndex;
    PatchHoverPreviewState.TextureSize = TextureSize;
    PatchHoverPreviewState.WorkingTextureSize = WorkingTextureSize;

    FDWCEditorPreviewCommitContext CommitContext;
    CommitContext.ConsumerToken = PreviewCommitLifetime.CaptureToken();
    CommitContext.DebugName = TEXT("Projected wrinkle patch hover");
    CommitContext.IsCurrent = [this]() { return !bPreviewSuspended; };
    const auto InitializeBuffer = [this, &CommitContext, MaterialSlotIndex, TextureSize, WorkingTextureSize](
        const FGuid& LayerGuid,
        FDWCEditorTextureLease& OutLease)
    {
        TArray<FColor> Pixels;
        Pixels.Init(EncodeWetWrinkleNormal(FVector::UpVector), TextureSize.X * TextureSize.Y);
        FDWCEditorNormalRasterSurface WorkingSurface;
        if (!WorkingSurface.Initialize(WorkingTextureSize, false))
        {
            return false;
        }
        return PreviewCommitCoordinator->CommitNormalBGRA8(
            CommitContext,
            MakeWrinkleTextureKey(
                WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::WrinkleHover,
                MaterialSlotIndex,
                LayerGuid),
            MakeWrinkleNormalDescriptor(TextureSize, WorkingTextureSize),
            MoveTemp(Pixels),
            MoveTemp(WorkingSurface),
            OutLease,
            EDWCEditorTextureUploadPriority::Interactive) == EDWCEditorPreviewCommitResult::Applied;
    };
    if (!InitializeBuffer(WrinkleHoverFrontLayerGuid, PatchHoverPreviewState.TextureHandle) ||
        !InitializeBuffer(WrinkleHoverBackLayerGuid, PatchHoverPreviewState.StagingTextureHandle))
    {
        ResetPatchHoverPreviewState();
        return false;
    }
    return true;
}

bool SWetWrinkleViewport::SchedulePatchHoverPreview()
{
    if (bPreviewSuspended || !WorkerJobScheduler.IsValid() ||
        !PreviewCommitCoordinator.IsValid() || !SpatialHandle.IsValid())
    {
        return false;
    }
    const int32 DiagnosticsLevel = CVarDWCWrinkleHoverDiagnostics.GetValueOnGameThread();
    const bool bCollectDiagnostics = DiagnosticsLevel > 0;
    const double RequestStartSeconds = bCollectDiagnostics ? FPlatformTime::Seconds() : 0.0;
    const int32 MaterialSlotIndex = CurrentSurfaceHit.MaterialSlotIndex;
    const int32 UVChannelIndex = CurrentSurfaceHit.UVChannelIndex;
    if (!EnsurePatchHoverPreviewState(MaterialSlotIndex, UVChannelIndex))
    {
        return false;
    }
    const uint64 RequestSerial = PatchHoverPreviewState.RequestSerial + 1;
    FDWCEditorWrinklePatchDescriptor PatchDescriptor;
    FString ProjectionError;
    if (!FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
            CurrentSurfaceHit, BrushSettings, RequestSerial, PatchDescriptor, &ProjectionError))
    {
        PatchPreviewValidationError = ProjectionError.IsEmpty()
            ? TEXT("The wrinkle patch cannot build a valid surface placement here.")
            : MoveTemp(ProjectionError);
        InvalidatePatchHoverRequest();
        RefreshViewportHint();
        return false;
    }
    const double DescriptorBuildMs = bCollectDiagnostics
        ? (FPlatformTime::Seconds() - RequestStartSeconds) * 1000.0
        : 0.0;
    const uint32 RequestHash = PatchDescriptor.GetStableHash();
    const bool bAlreadyPresented = PatchHoverPreviewState.bBound &&
        PatchHoverPreviewState.PresentedPayload.IsSet() &&
        PatchHoverPreviewState.PresentedPayload->Descriptor.GetStableHash() == RequestHash;
    if (bAlreadyPresented ||
        PatchHoverPreviewState.bPresentationSwapPending ||
        (PatchHoverPreviewState.RequestHash == RequestHash &&
         PatchHoverPreviewState.PendingTicket.IsValid()))
    {
        return true;
    }

    const double TextureResolveStartSeconds = bCollectDiagnostics ? FPlatformTime::Seconds() : 0.0;
    FWetWrinkleSurfacePatchPreviewInput SurfaceInput;
    if (!FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInput(
            PatchDescriptor, SpatialHandle, SurfaceInput, &ProjectionError))
    {
        PatchPreviewValidationError = ProjectionError.IsEmpty()
            ? TEXT("The wrinkle patch cannot build a valid surface projection here.")
            : MoveTemp(ProjectionError);
        InvalidatePatchHoverRequest();
        RefreshViewportHint();
        return false;
    }

    PatchPreviewValidationError.Reset();
    const double TextureResolveMs = bCollectDiagnostics
        ? (FPlatformTime::Seconds() - TextureResolveStartSeconds) * 1000.0
        : 0.0;

    PatchHoverPreviewState.RequestHash = RequestHash;
    PatchHoverPreviewState.RequestSerial = RequestSerial;
    PatchHoverPreviewState.RequestedDescriptor = PatchDescriptor;
    ++PatchHoverRequestCount;
    const FIntPoint TextureSize = PatchHoverPreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = PatchHoverPreviewState.WorkingTextureSize;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
    Descriptor.Domain = EDWCEditorAuthoringDomain::None;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.MemoryEstimate = FWetWrinkleIncrementalPreviewWorker::EstimateProjectedHoverMemory(
        SurfaceInput,
        WorkingTextureSize,
        TextureSize);
    Descriptor.DebugName = FString::Printf(TEXT("Projected wrinkle hover slot %d"), MaterialSlotIndex);

    FWetWrinkleHoverPerformanceDiagnostics HoverDiagnostics;
    if (bCollectDiagnostics)
    {
        HoverDiagnostics.RequestId = RequestSerial;
        HoverDiagnostics.MaterialSlotIndex = MaterialSlotIndex;
        HoverDiagnostics.ProjectionMode =
            FDWCEditorWrinklePatchDescriptorBuilder::ResolveAuthoredProjectionMode(
                PatchDescriptor.ProjectionSettings.BoundaryPolicy);
        HoverDiagnostics.BrushDiameterLocal = PatchDescriptor.SurfaceHalfExtentLocal.GetMax() * 2.0f;
        HoverDiagnostics.TextureSize = TextureSize;
        HoverDiagnostics.WorkingTextureSize = WorkingTextureSize;
        HoverDiagnostics.RequestStartSeconds = RequestStartSeconds;
        HoverDiagnostics.SubmitSeconds = FPlatformTime::Seconds();
        HoverDiagnostics.DescriptorBuildMs = DescriptorBuildMs;
        HoverDiagnostics.TextureResolveMs = TextureResolveMs;
        HoverDiagnostics.EstimatedMemoryBytes = Descriptor.MemoryEstimate.GetTotalBytes();
    }

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, MaterialSlotIndex, UVChannelIndex, RequestSerial, TextureSize, WorkingTextureSize,
         bCollectDiagnostics, HoverDiagnostics, SurfaceInput = MoveTemp(SurfaceInput)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled())
            {
                OutPrepareError = TEXT("The projected wrinkle hover was canceled before prepare.");
                return false;
            }
            FWetWrinklePatchHoverPreviewState& State = Viewport->PatchHoverPreviewState;
            if (State.RequestSerial != RequestSerial ||
                State.MaterialSlotIndex != MaterialSlotIndex ||
                State.UVChannelIndex != UVChannelIndex || !State.StagingTextureHandle.IsValid())
            {
                OutPrepareError = TEXT("The projected wrinkle hover source changed before admission.");
                return false;
            }
            FWetWrinkleProjectedHoverPreviewJobInput Input;
            Input.SurfaceInput = MoveTemp(SurfaceInput);
            Input.ProjectionCache = Viewport->SurfacePatchProjectionCache;
            Input.TextureSize = TextureSize;
            Input.WorkingTextureSize = WorkingTextureSize;
            Input.PreviousOutputRects = State.StagingOutputRects;
            Input.Target.Key = State.StagingTextureHandle->GetKey();
            Input.Target.Descriptor = State.StagingTextureHandle->GetDescriptor();
            Input.Target.ExpectedDataRevision = State.StagingTextureHandle->GetDataRevision();
            Input.Target.ExpectedResourceGeneration = State.StagingTextureHandle->GetResourceGeneration();
            Input.bCollectPerformanceDiagnostics = bCollectDiagnostics;
            Input.PerformanceDiagnostics = HoverDiagnostics;
            OutPrepared.ActualMemoryEstimate =
                FWetWrinkleIncrementalPreviewWorker::EstimateProjectedHoverMemory(
                    Input.SurfaceInput,
                    WorkingTextureSize,
                    TextureSize);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FWetWrinkleIncrementalPreviewWorker::BuildProjectedHoverProjectionPhase(
                    MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, MaterialSlotIndex, UVChannelIndex, RequestSerial, PatchDescriptor, CommitToken](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult) mutable
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FWetWrinkleIncrementalPreviewJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            FWetWrinklePatchHoverPreviewState& State = Viewport->PatchHoverPreviewState;
            if (!Result->PresentedProjectedPatch.IsSet() ||
                !Result->PresentedProjectedPatch->IsValid())
            {
                if (State.PendingTicket.JobId == CompletedTicket.JobId &&
                    State.PendingTicket.Generation == CompletedTicket.Generation)
                {
                    Viewport->PatchPreviewValidationError =
                        TEXT("The wrinkle hover projection did not produce a valid patch.");
                    Viewport->ClearPatchHoverPresentation(true);
                    Viewport->RefreshViewportHint();
                }
                UE_LOG(
                    LogWetWrinklePreviewViewport,
                    Warning,
                    TEXT("Projected wrinkle hover completed without a commit payload for slot %d."),
                    MaterialSlotIndex);
                return;
            }
            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = TEXT("Projected wrinkle patch hover");
            CommitContext.IsCurrent = [Viewport, MaterialSlotIndex, UVChannelIndex,
                                       RequestSerial, CompletedTicket, PatchDescriptor]()
            {
                const FWetWrinklePatchHoverPreviewState& Current = Viewport->PatchHoverPreviewState;
                return !Viewport->bPreviewSuspended && PatchDescriptor.HasCurrentNormalTextureContent() &&
                    Current.RequestSerial == RequestSerial &&
                    Current.MaterialSlotIndex == MaterialSlotIndex &&
                    Current.UVChannelIndex == UVChannelIndex &&
                    Current.PendingTicket.JobId == CompletedTicket.JobId &&
                    Current.PendingTicket.Generation == CompletedTicket.Generation;
            };
            FDWCEditorPreviewRegionCommitOutcome Outcome;
            const double CommitStartSeconds = Result->HoverDiagnostics.IsSet()
                ? FPlatformTime::Seconds()
                : 0.0;
            const EDWCEditorPreviewCommitResult CommitResult =
                Viewport->PreviewCommitCoordinator->CommitInteractiveNormalRegions(
                    CommitContext,
                    State.StagingTextureHandle,
                    Result->Target,
                    MoveTemp(Result->Regions),
                    Outcome);
            if (Result->HoverDiagnostics.IsSet())
            {
                Result->HoverDiagnostics->CommitMs =
                    (FPlatformTime::Seconds() - CommitStartSeconds) * 1000.0;
                Result->HoverDiagnostics->CommitFinishedSeconds = FPlatformTime::Seconds();
            }
            if (State.PendingTicket.JobId != CompletedTicket.JobId ||
                State.PendingTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            State.PendingTicket = {};
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied)
            {
                ++Viewport->PatchHoverAppliedCount;
                Viewport->PatchPreviewValidationError.Reset();
                Viewport->RefreshViewportHint();
                State.StagingOutputRects = MoveTemp(Result->ProjectedOutputRects);
                FWetWrinklePresentedPatchPayload PendingPayload;
                PendingPayload.Descriptor = PatchDescriptor;
                PendingPayload.ProjectedPatch = MoveTemp(Result->PresentedProjectedPatch.GetValue());
                State.PendingPresentedPayload = MoveTemp(PendingPayload);
                State.PendingPresentationUpload = Outcome.UploadTicket;
                State.PendingPerformanceDiagnostics = MoveTemp(Result->HoverDiagnostics);
                State.RequestedDescriptor.Reset();
                State.RequestHash = PatchDescriptor.GetStableHash();
                State.bPresentationSwapPending = true;
                Viewport->Invalidate();
                if (Viewport->RenderUploadQueue.IsValid())
                {
                    const FDWCEditorTextureUploadTicket UploadTicket = State.PendingPresentationUpload;
                    State.PendingPresentationObserver = Viewport->RenderUploadQueue->Observe(
                        UploadTicket,
                        [WeakThis, UploadTicket, RequestSerial](
                            const EDWCEditorTextureUploadStatus UploadStatus)
                        {
                            if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = WeakThis.Pin())
                            {
                                PinnedViewport->HandlePatchHoverUploadStatus(
                                    UploadTicket,
                                    RequestSerial,
                                    UploadStatus);
                            }
                        });
                    Viewport->RenderUploadQueue->TrySubmitInteractive(UploadTicket);
                }
                else
                {
                    Viewport->HandlePatchHoverUploadStatus(
                        State.PendingPresentationUpload,
                        RequestSerial,
                        EDWCEditorTextureUploadStatus::Invalid);
                }
            }
        },
        &SubmitError,
        [WeakThis, MaterialSlotIndex, UVChannelIndex, RequestSerial,
         bCollectDiagnostics, HoverDiagnostics](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid())
            {
                return;
            }
            if (bCollectDiagnostics &&
                CVarDWCWrinkleHoverDiagnostics.GetValueOnAnyThread() >= 3)
            {
                const TCHAR* CompletionName = TEXT("unknown");
                switch (Completion)
                {
                case EDWCEditorWorkerJobCompletion::Superseded: CompletionName = TEXT("superseded"); break;
                case EDWCEditorWorkerJobCompletion::Stale: CompletionName = TEXT("stale"); break;
                case EDWCEditorWorkerJobCompletion::Canceled: CompletionName = TEXT("canceled"); break;
                case EDWCEditorWorkerJobCompletion::Failed: CompletionName = TEXT("failed"); break;
                default: break;
                }
                UE_LOG(
                    LogWetWrinklePreviewViewport,
                    Display,
                    TEXT("Wrinkle hover perf: request=%llu slot=%d completion=%s elapsed=%.2fms reason=%s"),
                    RequestSerial,
                    MaterialSlotIndex,
                    CompletionName,
                    HoverDiagnostics.RequestStartSeconds > 0.0
                        ? (FPlatformTime::Seconds() - HoverDiagnostics.RequestStartSeconds) * 1000.0
                        : 0.0,
                    Error.IsEmpty() ? TEXT("none") : *Error);
            }
            switch (Completion)
            {
            case EDWCEditorWorkerJobCompletion::Superseded:
            case EDWCEditorWorkerJobCompletion::Stale:
                ++Viewport->PatchHoverSupersededCount;
                break;
            case EDWCEditorWorkerJobCompletion::Canceled:
                ++Viewport->PatchHoverCanceledCount;
                break;
            case EDWCEditorWorkerJobCompletion::Failed:
                ++Viewport->PatchHoverFailedCount;
                break;
            default:
                break;
            }
            FWetWrinklePatchHoverPreviewState& State = Viewport->PatchHoverPreviewState;
            if (State.RequestSerial == RequestSerial &&
                State.MaterialSlotIndex == MaterialSlotIndex &&
                State.UVChannelIndex == UVChannelIndex &&
                State.PendingTicket.JobId == CompletedTicket.JobId &&
                State.PendingTicket.Generation == CompletedTicket.Generation)
            {
                State.PendingTicket = {};
                State.RequestedDescriptor.Reset();
                State.PendingPresentationUpload = {};
                State.PendingPerformanceDiagnostics.Reset();
                State.RequestHash = State.PresentedPayload.IsSet()
                    ? State.PresentedPayload->Descriptor.GetStableHash()
                    : 0;
                if (Completion == EDWCEditorWorkerJobCompletion::Failed)
                {
                    Viewport->PatchPreviewValidationError = Error.IsEmpty()
                        ? TEXT("The wrinkle hover projection failed.")
                        : Error;
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Projected wrinkle hover failed for slot %d: %s"),
                        MaterialSlotIndex,
                        Error.IsEmpty() ? TEXT("unknown worker failure") : *Error);
                    Viewport->ClearPatchHoverPresentation(true);
                    Viewport->RefreshViewportHint();
                }
            }
        });
    PatchHoverPreviewState.PendingTicket = Ticket;
    if (!Ticket.IsValid())
    {
        PatchPreviewValidationError = SubmitError.IsEmpty()
            ? TEXT("The wrinkle hover request could not be scheduled.")
            : MoveTemp(SubmitError);
        PatchHoverPreviewState.RequestedDescriptor.Reset();
        PatchHoverPreviewState.RequestHash = PatchHoverPreviewState.PresentedPayload.IsSet()
            ? PatchHoverPreviewState.PresentedPayload->Descriptor.GetStableHash()
            : 0;
        RefreshViewportHint();
        return false;
    }
    ++PatchHoverSubmittedCount;
    return true;
}

void SWetWrinkleViewport::QueueAccumulatedIncrementalCommand(
    FWetWrinkleAccumulatedPreviewState& PreviewState,
    FWetWrinkleIncrementalCommand&& Command)
{
    Command.Sequence = ++PreviewState.NextIncrementalSequence;
    PreviewState.PendingIncrementalCommands.Add(MoveTemp(Command));
    PreviewState.Recovery.MarkIncrementalPending();
    ScheduleAccumulatedIncrementalPreview(PreviewState);
}

void SWetWrinkleViewport::InvalidateAccumulatedIncrementalState(
    FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    if (PreviewState.PendingIncrementalTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(PreviewState.PendingIncrementalTicket.Key);
    }
    PreviewState.PendingIncrementalTicket = {};
    PreviewState.PendingIncrementalCommands.Reset();
    ++PreviewState.IncrementalGeneration;
}

bool SWetWrinkleViewport::ScheduleAccumulatedIncrementalPreview(
    FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    if (bPreviewSuspended || PreviewState.PendingIncrementalTicket.IsValid() ||
        PreviewState.PendingIncrementalCommands.IsEmpty())
    {
        return false;
    }
    if (!WorkerJobScheduler.IsValid() || !PreviewCommitCoordinator.IsValid() ||
        !PreviewState.TextureHandle.IsValid() ||
        !PreviewState.TextureHandle->GetWorkingNormalSurface().IsValid())
    {
        InvalidateAccumulatedIncrementalState(PreviewState);
        PreviewState.bDirty = true;
        PreviewState.Recovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::WorkspaceEvicted);
        ++AccumulatedIncrementalFallbackCount;
        return RebuildAccumulatedPreviewTexture(PreviewState);
    }

    const int32 MaterialSlotIndex = PreviewState.MaterialSlotIndex;
    const int32 UVChannelIndex = PreviewState.UVChannelIndex;
    const FIntPoint TextureSize = PreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = PreviewState.WorkingTextureSize;
    const uint64 IncrementalGeneration = PreviewState.IncrementalGeneration;
    const int32 BatchCommandCount = PreviewState.PendingIncrementalCommands.Num();
    const uint64 FirstSequence = PreviewState.PendingIncrementalCommands[0].Sequence;
    const uint64 LastSequence = PreviewState.PendingIncrementalCommands[BatchCommandCount - 1].Sequence;

    TArray<FWetWrinkleIncrementalCommand> PlanCommands;
    PlanCommands.Append(PreviewState.PendingIncrementalCommands.GetData(), BatchCommandCount);
    TArray<FWetWrinkleIncrementalRegionPlan> RegionPlan;
    if (!FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(
            PlanCommands,
            WorkingTextureSize,
            TextureSize,
            RegionPlan))
    {
        InvalidateAccumulatedIncrementalState(PreviewState);
        PreviewState.bDirty = true;
        PreviewState.Recovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::InvalidPayload);
        ++AccumulatedIncrementalFallbackCount;
        return RebuildAccumulatedPreviewTexture(PreviewState);
    }

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleIncrementalPreview;
    Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
    Descriptor.Domain = EDWCEditorAuthoringDomain::None;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    Descriptor.MemoryEstimate = FWetWrinkleIncrementalPreviewWorker::EstimateMemory(
        PlanCommands,
        RegionPlan,
        PreviewState.TextureHandle->GetWorkingNormalSurface().HasCoverage());
    Descriptor.DebugName = FString::Printf(
        TEXT("Wrinkle incremental preview slot %d [%llu-%llu]"),
        MaterialSlotIndex,
        FirstSequence,
        LastSequence);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, MaterialSlotIndex, UVChannelIndex, TextureSize, WorkingTextureSize,
         IncrementalGeneration, BatchCommandCount, FirstSequence, LastSequence,
         RegionPlan = MoveTemp(RegionPlan)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled())
            {
                OutPrepareError = TEXT("The wrinkle incremental preview was canceled before prepare.");
                return false;
            }
            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            if (State == nullptr || State->IncrementalGeneration != IncrementalGeneration ||
                State->PendingIncrementalCommands.Num() < BatchCommandCount ||
                State->PendingIncrementalCommands[0].Sequence != FirstSequence ||
                State->PendingIncrementalCommands[BatchCommandCount - 1].Sequence != LastSequence ||
                !State->TextureHandle.IsValid())
            {
                OutPrepareError = TEXT("The wrinkle incremental preview source changed before admission.");
                return false;
            }

            const FDWCEditorNormalRasterSurface& SourceSurface =
                State->TextureHandle->GetWorkingNormalSurface();
            if (!SourceSurface.IsValid() || SourceSurface.Size != WorkingTextureSize ||
                State->TextureHandle->GetDescriptor().Size != TextureSize)
            {
                OutPrepareError = TEXT("The wrinkle incremental preview working surface is unavailable.");
                return false;
            }

            FWetWrinkleIncrementalPreviewJobInput Input;
            Input.TextureSize = TextureSize;
            Input.WorkingTextureSize = WorkingTextureSize;
            Input.Commands.Append(State->PendingIncrementalCommands.GetData(), BatchCommandCount);
            Input.FirstSequence = FirstSequence;
            Input.LastSequence = LastSequence;
            Input.Target.Key = MakeWrinkleTextureKey(
                Viewport->WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::WrinkleAccumulated,
                MaterialSlotIndex);
            Input.Target.Descriptor = State->TextureHandle->GetDescriptor();
            Input.Target.ExpectedDataRevision = State->TextureHandle->GetDataRevision();
            Input.Target.ExpectedResourceGeneration = State->TextureHandle->GetResourceGeneration();
            Input.Regions.Reserve(RegionPlan.Num());
            for (const FWetWrinkleIncrementalRegionPlan& Plan : RegionPlan)
            {
                FWetWrinkleIncrementalRegionSnapshot& Snapshot = Input.Regions.AddDefaulted_GetRef();
                Snapshot.Plan = Plan;
                if (!Snapshot.Region.InitializeFromSurface(SourceSurface, Plan.WorkingRect))
                {
                    OutPrepareError = TEXT("Failed to capture a wrinkle incremental preview region.");
                    return false;
                }
            }

            OutPrepared.ActualMemoryEstimate = FWetWrinkleIncrementalPreviewWorker::EstimateMemory(
                Input.Commands,
                RegionPlan,
                SourceSurface.HasCoverage());
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FWetWrinkleIncrementalPreviewWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, MaterialSlotIndex, UVChannelIndex, IncrementalGeneration,
         BatchCommandCount, FirstSequence, LastSequence, CommitToken](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FWetWrinkleIncrementalPreviewJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            if (State == nullptr)
            {
                return;
            }

            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Wrinkle incremental preview slot %d"),
                MaterialSlotIndex);
            CommitContext.IsCurrent = [Viewport, MaterialSlotIndex, UVChannelIndex,
                                       IncrementalGeneration, FirstSequence, LastSequence,
                                       CompletedTicket]()
            {
                const FWetWrinkleAccumulatedPreviewState* Current =
                    Viewport->AccumulatedPreviewStates.FindByPredicate(
                        [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                        {
                            return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                                Candidate.UVChannelIndex == UVChannelIndex;
                        });
                return !Viewport->bPreviewSuspended && Current != nullptr &&
                    Current->IncrementalGeneration == IncrementalGeneration &&
                    Current->PendingIncrementalTicket.JobId == CompletedTicket.JobId &&
                    Current->PendingIncrementalTicket.Generation == CompletedTicket.Generation &&
                    !Current->PendingIncrementalCommands.IsEmpty() &&
                    Current->PendingIncrementalCommands[0].Sequence == FirstSequence &&
                    Current->PendingIncrementalCommands.Num() >= 1 &&
                    Current->PendingIncrementalCommands.ContainsByPredicate(
                        [LastSequence](const FWetWrinkleIncrementalCommand& Command)
                        {
                            return Command.Sequence == LastSequence;
                        });
            };

            FDWCEditorPreviewRegionCommitOutcome Outcome;
            const EDWCEditorPreviewCommitResult CommitResult =
                Viewport->PreviewCommitCoordinator->CommitNormalRegions(
                    CommitContext,
                    State->TextureHandle,
                    Result->Target,
                    Result->Regions,
                    Outcome,
                    EDWCEditorTextureUploadPriority::Interactive);
            const bool bOwnsTicket = State->PendingIncrementalTicket.JobId == CompletedTicket.JobId &&
                State->PendingIncrementalTicket.Generation == CompletedTicket.Generation;
            if (!bOwnsTicket)
            {
                return;
            }
            State->PendingIncrementalTicket = {};
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied &&
                State->PendingIncrementalCommands.Num() >= BatchCommandCount &&
                State->PendingIncrementalCommands[0].Sequence == FirstSequence &&
                State->PendingIncrementalCommands[BatchCommandCount - 1].Sequence == LastSequence)
            {
                State->PendingIncrementalCommands.RemoveAt(0, BatchCommandCount, EAllowShrinking::No);
                State->bDirty = false;
                State->Recovery.MarkIncrementalSucceeded();
                ++Viewport->AccumulatedIncrementalCommitCount;
                Viewport->RefreshWrinklePreviewAccumulatedParameters();
                Viewport->CompletePatchHoverHandoff(
                    MaterialSlotIndex, UVChannelIndex, Outcome.UploadTicket, LastSequence);
                Viewport->Invalidate();
                Viewport->ScheduleAccumulatedIncrementalPreview(*State);
                return;
            }

            Viewport->InvalidateAccumulatedIncrementalState(*State);
            State->bDirty = true;
            State->Recovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::WorkerFailed);
            ++Viewport->AccumulatedIncrementalFallbackCount;
            Viewport->RebuildAccumulatedPreviewTexture(*State);
        },
        &SubmitError,
        [WeakThis, MaterialSlotIndex, UVChannelIndex, IncrementalGeneration](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid())
            {
                return;
            }
            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            if (State == nullptr || State->IncrementalGeneration != IncrementalGeneration ||
                State->PendingIncrementalTicket.JobId != CompletedTicket.JobId ||
                State->PendingIncrementalTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            Viewport->InvalidateAccumulatedIncrementalState(*State);
            State->bDirty = true;
            State->Recovery.RequestFullRebuild(
                Completion == EDWCEditorWorkerJobCompletion::Failed
                    ? EDWCEditorPreviewInvalidationReason::WorkerFailed
                    : EDWCEditorPreviewInvalidationReason::SchedulerDeferred);
            ++Viewport->AccumulatedIncrementalFallbackCount;
            if (Completion == EDWCEditorWorkerJobCompletion::Failed)
            {
                UE_LOG(
                    LogWetWrinklePreviewViewport,
                    Warning,
                    TEXT("Wrinkle incremental preview failed for slot %d; rebuilding from authored data: %s"),
                    MaterialSlotIndex,
                    Error.IsEmpty() ? TEXT("unknown worker failure") : *Error);
            }
            Viewport->RebuildAccumulatedPreviewTexture(*State);
        });
    PreviewState.PendingIncrementalTicket = Ticket;
    if (!Ticket.IsValid())
    {
        InvalidateAccumulatedIncrementalState(PreviewState);
        PreviewState.bDirty = true;
        PreviewState.Recovery.RequestFullRebuild(EDWCEditorPreviewInvalidationReason::SchedulerDeferred);
        ++AccumulatedIncrementalFallbackCount;
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Failed to schedule %s; rebuilding from authored data: %s"),
            *Descriptor.DebugName,
            SubmitError.IsEmpty() ? TEXT("unknown scheduler rejection") : *SubmitError);
        return RebuildAccumulatedPreviewTexture(PreviewState);
    }
    return true;
}

void SWetWrinkleViewport::CompletePatchHoverHandoff(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    const FDWCEditorTextureUploadTicket& UploadTicket,
    const uint64 LastAppliedSequence,
    const uint64 AppliedContentRevision)
{
    const bool bSequenceReached = PatchHoverPreviewState.PendingAccumulatedSequence > 0 &&
        LastAppliedSequence >= PatchHoverPreviewState.PendingAccumulatedSequence;
    const bool bRevisionReached = PatchHoverPreviewState.PendingAccumulatedContentRevision > 0 &&
        AppliedContentRevision >= PatchHoverPreviewState.PendingAccumulatedContentRevision;
    if (!PatchHoverPreviewState.IsCommitHandoffPending() ||
        PatchHoverPreviewState.MaterialSlotIndex != MaterialSlotIndex ||
        PatchHoverPreviewState.UVChannelIndex != UVChannelIndex ||
        (!bSequenceReached && !bRevisionReached))
    {
        return;
    }

    if (!UploadTicket.IsValid())
    {
        RecoverPatchHoverHandoff(
            EDWCEditorPreviewInvalidationReason::ResourceGenerationMismatch);
        return;
    }

    PatchHoverPreviewState.PendingAccumulatedUpload = UploadTicket;
    PatchHoverPreviewState.HandoffState =
        EWetWrinklePatchHandoffState::AwaitingAccumulatedUpload;
}

void SWetWrinkleViewport::RecoverPatchHoverHandoff(
    const EDWCEditorPreviewInvalidationReason Reason)
{
    if (!PatchHoverPreviewState.IsCommitHandoffPending())
    {
        return;
    }

    constexpr int32 MaxHandoffRecoveryAttempts = 2;
    PatchHoverPreviewState.PendingAccumulatedUpload = {};
    if (++PatchHoverPreviewState.HandoffRecoveryAttempts > MaxHandoffRecoveryAttempts)
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Wrinkle patch preview handoff for slot %d exceeded its bounded recovery limit; the authored patch remains committed and the accumulated preview will recover independently."),
            PatchHoverPreviewState.MaterialSlotIndex);
        FinishPatchHoverHandoff();
        return;
    }

    FWetWrinkleAccumulatedPreviewState* PreviewState =
        AccumulatedPreviewStates.FindByPredicate(
            [this](const FWetWrinkleAccumulatedPreviewState& Candidate)
            {
                return Candidate.MaterialSlotIndex == PatchHoverPreviewState.MaterialSlotIndex &&
                    Candidate.UVChannelIndex == PatchHoverPreviewState.UVChannelIndex;
            });
    if (PreviewState == nullptr)
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Wrinkle patch preview handoff lost its accumulated preview state for slot %d."),
            PatchHoverPreviewState.MaterialSlotIndex);
        FinishPatchHoverHandoff();
        return;
    }

    PatchHoverPreviewState.HandoffState =
        EWetWrinklePatchHandoffState::RecoveringFullRebuild;
    PreviewState->bDirty = true;
    PreviewState->Recovery.RequestFullRebuild(Reason);
    const bool bScheduled = RebuildAccumulatedPreviewTexture(*PreviewState);
    const bool bCanRetry =
        PreviewState->bRebuildPending || PreviewState->PendingTicket.IsValid() ||
        PreviewState->Recovery.GetState() == EDWCEditorPreviewRecoveryState::RetryBackoff ||
        PreviewState->Recovery.GetState() == EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    if (!bScheduled && !bCanRetry)
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Wrinkle patch preview handoff could not schedule recovery for slot %d; the authored patch remains committed."),
            PatchHoverPreviewState.MaterialSlotIndex);
        FinishPatchHoverHandoff();
    }
}

void SWetWrinkleViewport::FinishPatchHoverHandoff()
{
    PatchHoverPreviewState.HandoffState = EWetWrinklePatchHandoffState::Idle;
    PatchHoverPreviewState.HandoffRecoveryAttempts = 0;
    PatchHoverPreviewState.PendingAccumulatedUpload = {};
    PatchHoverPreviewState.PendingAccumulatedSequence = 0;
    PatchHoverPreviewState.PendingAccumulatedContentRevision = 0;
    PatchHoverPreviewState.bBound = false;
    PatchHoverPreviewState.PresentedPayload.Reset();
    PatchHoverPreviewState.RequestHash = 0;
    ApplyPatchHoverPreviewLayer();
}

bool SWetWrinkleViewport::ScheduleTransientProceduralPreview(
    const FWetProceduralRidgeStroke& Stroke)
{
    const int32 DataUVChannelIndex = WetClothingAsset.IsValid()
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (bPreviewSuspended || Stroke.Points.Num() < 2 ||
        Stroke.MaterialSlotIndex == INDEX_NONE || DataUVChannelIndex < 0 ||
        !WorkerJobScheduler.IsValid() || !PreviewCommitCoordinator.IsValid() ||
        !EnsureTransientProceduralPreviewState(Stroke.MaterialSlotIndex, DataUVChannelIndex))
    {
        return false;
    }

    FWetWrinkleIncrementalCommand Command;
    Command.Kind = EWetWrinkleIncrementalCommandKind::Ridge;
    Command.Ridge = Stroke;
    TArray<FWetWrinkleIncrementalCommand> Commands;
    Commands.Add(Command);
    TArray<FIntRect> AdditionalWorkingRects;
    if (TransientProceduralPreviewState.LastCommittedStroke.IsSet())
    {
        AdditionalWorkingRects.Add(FWetProceduralRidgeRasterizer::ComputeBounds(
            TransientProceduralPreviewState.LastCommittedStroke.GetValue(),
            TransientProceduralPreviewState.WorkingTextureSize));
    }
    TArray<FWetWrinkleIncrementalRegionPlan> RegionPlan;
    if (!FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(
            Commands,
            TransientProceduralPreviewState.WorkingTextureSize,
            TransientProceduralPreviewState.TextureSize,
            RegionPlan,
            &AdditionalWorkingRects))
    {
        return false;
    }

    const int32 MaterialSlotIndex = Stroke.MaterialSlotIndex;
    const FIntPoint TextureSize = TransientProceduralPreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = TransientProceduralPreviewState.WorkingTextureSize;
    const uint64 RequestSerial = ++TransientProceduralPreviewState.RequestSerial;
    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleTransientPreview;
    Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
    Descriptor.Domain = EDWCEditorAuthoringDomain::None;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.MemoryEstimate = FWetWrinkleIncrementalPreviewWorker::EstimateMemory(
        Commands,
        RegionPlan,
        false);
    Descriptor.DebugName = FString::Printf(TEXT("Wrinkle transient preview slot %d"), MaterialSlotIndex);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, MaterialSlotIndex, DataUVChannelIndex, TextureSize, WorkingTextureSize,
         RequestSerial, Stroke, RegionPlan = MoveTemp(RegionPlan)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError) mutable
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled())
            {
                OutPrepareError = TEXT("The transient ridge preview was canceled before prepare.");
                return false;
            }
            FWetProceduralRidgeTransientPreviewState& State = Viewport->TransientProceduralPreviewState;
            if (State.RequestSerial != RequestSerial ||
                State.MaterialSlotIndex != MaterialSlotIndex ||
                State.UVChannelIndex != DataUVChannelIndex ||
                !State.TextureHandle.IsValid())
            {
                OutPrepareError = TEXT("The transient ridge preview source changed before admission.");
                return false;
            }

            FWetWrinkleIncrementalPreviewJobInput Input;
            Input.TextureSize = TextureSize;
            Input.WorkingTextureSize = WorkingTextureSize;
            Input.bClearRegionsToFlat = true;
            FWetWrinkleIncrementalCommand RidgeCommand;
            RidgeCommand.Kind = EWetWrinkleIncrementalCommandKind::Ridge;
            RidgeCommand.Ridge = Stroke;
            Input.Commands.Add(MoveTemp(RidgeCommand));
            Input.Target.Key = MakeWrinkleTextureKey(
                Viewport->WetClothingAsset.Get(),
                EDWCEditorTexturePurpose::WrinkleProcedural,
                MaterialSlotIndex);
            Input.Target.Descriptor = State.TextureHandle->GetDescriptor();
            Input.Target.ExpectedDataRevision = State.TextureHandle->GetDataRevision();
            Input.Target.ExpectedResourceGeneration = State.TextureHandle->GetResourceGeneration();
            for (const FWetWrinkleIncrementalRegionPlan& Plan : RegionPlan)
            {
                FWetWrinkleIncrementalRegionSnapshot& Snapshot = Input.Regions.AddDefaulted_GetRef();
                Snapshot.Plan = Plan;
                if (!Snapshot.Region.Initialize(WorkingTextureSize, Plan.WorkingRect, false))
                {
                    OutPrepareError = TEXT("Failed to allocate a transient ridge preview region.");
                    return false;
                }
            }
            OutPrepared.ActualMemoryEstimate = FWetWrinkleIncrementalPreviewWorker::EstimateMemory(
                Input.Commands,
                RegionPlan,
                false);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FWetWrinkleIncrementalPreviewWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakThis, MaterialSlotIndex, DataUVChannelIndex, RequestSerial, Stroke, CommitToken](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FWetWrinkleIncrementalPreviewJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            FWetProceduralRidgeTransientPreviewState& State = Viewport->TransientProceduralPreviewState;
            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.ProducerSessionEpoch = CompletedTicket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Wrinkle transient preview slot %d"),
                MaterialSlotIndex);
            CommitContext.IsCurrent = [Viewport, MaterialSlotIndex, DataUVChannelIndex,
                                       RequestSerial, CompletedTicket]()
            {
                const FWetProceduralRidgeTransientPreviewState& Current =
                    Viewport->TransientProceduralPreviewState;
                return !Viewport->bPreviewSuspended && Current.RequestSerial == RequestSerial &&
                    Current.MaterialSlotIndex == MaterialSlotIndex &&
                    Current.UVChannelIndex == DataUVChannelIndex &&
                    Current.PendingIncrementalTicket.JobId == CompletedTicket.JobId &&
                    Current.PendingIncrementalTicket.Generation == CompletedTicket.Generation;
            };
            FDWCEditorPreviewRegionCommitOutcome Outcome;
            const EDWCEditorPreviewCommitResult CommitResult =
                Viewport->PreviewCommitCoordinator->CommitNormalRegions(
                    CommitContext,
                    State.TextureHandle,
                    Result->Target,
                    Result->Regions,
                    Outcome,
                    EDWCEditorTextureUploadPriority::Interactive);
            const bool bOwnsTicket = State.PendingIncrementalTicket.JobId == CompletedTicket.JobId &&
                State.PendingIncrementalTicket.Generation == CompletedTicket.Generation;
            if (!bOwnsTicket)
            {
                return;
            }
            State.PendingIncrementalTicket = {};
            if (CommitResult == EDWCEditorPreviewCommitResult::Applied)
            {
                State.LastCommittedStroke = Stroke;
                ++Viewport->TransientIncrementalCommitCount;
                if (!Viewport->bTransientProceduralPreviewBound)
                {
                    Viewport->bTransientProceduralPreviewBound = true;
                    Viewport->RefreshWrinklePreviewTransientParameters();
                }
                Viewport->Invalidate();
            }
        },
        &SubmitError,
        [WeakThis, MaterialSlotIndex, DataUVChannelIndex, RequestSerial](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid())
            {
                return;
            }
            FWetProceduralRidgeTransientPreviewState& State = Viewport->TransientProceduralPreviewState;
            if (State.RequestSerial == RequestSerial &&
                State.MaterialSlotIndex == MaterialSlotIndex &&
                State.UVChannelIndex == DataUVChannelIndex &&
                State.PendingIncrementalTicket.JobId == CompletedTicket.JobId &&
                State.PendingIncrementalTicket.Generation == CompletedTicket.Generation)
            {
                State.PendingIncrementalTicket = {};
                if (Completion == EDWCEditorWorkerJobCompletion::Failed)
                {
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Transient ridge preview failed for slot %d: %s"),
                        MaterialSlotIndex,
                        Error.IsEmpty() ? TEXT("unknown worker failure") : *Error);
                }
            }
        });
    TransientProceduralPreviewState.PendingIncrementalTicket = Ticket;
    if (!Ticket.IsValid())
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Failed to schedule %s: %s"),
            *Descriptor.DebugName,
            SubmitError.IsEmpty() ? TEXT("unknown scheduler rejection") : *SubmitError);
        return false;
    }
    return true;
}

void SWetWrinkleViewport::MarkAccumulatedPreviewStatesDirty()
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.bRebuildPending && WorkerJobScheduler.IsValid())
        {
            WorkerJobScheduler->Cancel(PreviewState.PendingTicket.Key);
        }
        PreviewState.bDirty = true;
        PreviewState.Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
        PreviewState.bRebuildPending = false;
        PreviewState.PendingTicket = {};
        InvalidateAccumulatedIncrementalState(PreviewState);
        ++PreviewState.ContentRevision;
    }
}

FWetWrinkleAccumulatedPreviewState* SWetWrinkleViewport::FindOrAddAccumulatedPreviewState(
    UTexture* SourceTexture,
    int32 MaterialSlotIndex,
    int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0)
    {
        return nullptr;
    }

    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.MaterialSlotIndex == MaterialSlotIndex && PreviewState.UVChannelIndex == UVChannelIndex)
        {
            PreviewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
            const FIntPoint ExpectedTextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
            const FIntPoint ExpectedWorkingTextureSize =
                WetWrinkleTextureRaster::ResolveWorkingTextureSize(ExpectedTextureSize);
            if (PreviewState.SourceTexture.Get() != SourceTexture)
            {
                InvalidateAccumulatedIncrementalState(PreviewState);
                PreviewState.SourceTexture = SourceTexture;
                PreviewState.bDirty = true;
                PreviewState.Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ContextChanged);
                ++PreviewState.ContentRevision;
            }
            if (PreviewState.TextureSize != ExpectedTextureSize ||
                PreviewState.WorkingTextureSize != ExpectedWorkingTextureSize)
            {
                InvalidateAccumulatedIncrementalState(PreviewState);
                PreviewState.TextureSize = ExpectedTextureSize;
                PreviewState.WorkingTextureSize = ExpectedWorkingTextureSize;
                PreviewState.bDirty = true;
                PreviewState.Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ResolutionChanged);
                ++PreviewState.ContentRevision;
            }
            return &PreviewState;
        }
    }

    FWetWrinkleAccumulatedPreviewState& NewState = AccumulatedPreviewStates.AddDefaulted_GetRef();
    NewState.SourceTexture = SourceTexture;
    NewState.MaterialSlotIndex = MaterialSlotIndex;
    NewState.UVChannelIndex = UVChannelIndex;
    NewState.TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    NewState.WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(NewState.TextureSize);
    NewState.bDirty = true;
    NewState.Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ContextChanged);
    NewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
    return AccumulatedPreviewStates.FindByPredicate(
        [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& PreviewState)
        {
            return PreviewState.MaterialSlotIndex == MaterialSlotIndex &&
                   PreviewState.UVChannelIndex == UVChannelIndex;
        });
}

UTexture2D* SWetWrinkleViewport::ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    if (bPreviewSuspended)
    {
        return nullptr;
    }
    FWetWrinkleAccumulatedPreviewState* PreviewState = FindOrAddAccumulatedPreviewState(SourceTexture, MaterialSlotIndex, UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return nullptr;
    }

    if (PreviewState->bDirty && !RebuildAccumulatedPreviewTexture(*PreviewState))
    {
        PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
        return nullptr;
    }

    UTexture2D* Result = PreviewState->TextureHandle.IsValid()
        ? PreviewState->TextureHandle->GetTexture()
        : nullptr;
    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
    return Result;
}

bool SWetWrinkleViewport::RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_RebuildAccumulatedPreviewTexture);
    if (bPreviewSuspended)
    {
        return false;
    }
    if (PreviewState.bRebuildPending && !PreviewState.PendingTicket.IsValid())
    {
        PreviewState.bRebuildPending = false;
    }
    if (PreviewState.bRebuildPending)
    {
        return true;
    }
    if (PreviewState.Recovery.IsReady())
    {
        PreviewState.Recovery.RequestFullRebuild(
            EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    }
    if (!PreviewState.Recovery.TryBeginFullRebuild(FPlatformTime::Seconds()))
    {
        // Keep the last successful lease visible while a bounded retry is in
        // backoff or the producer has entered degraded mode.
        return PreviewState.TextureHandle.IsValid();
    }
    if (PreviewState.PendingIncrementalTicket.IsValid() ||
        !PreviewState.PendingIncrementalCommands.IsEmpty())
    {
        InvalidateAccumulatedIncrementalState(PreviewState);
    }

    if (!WorkerJobScheduler.IsValid() || !TextureWorkspace.IsValid() ||
        PreviewState.MaterialSlotIndex == INDEX_NONE || PreviewState.UVChannelIndex < 0)
    {
        PreviewState.Recovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::InvalidPayload,
            FPlatformTime::Seconds());
        return false;
    }

    PreviewState.TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState.WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState.TextureSize);
    if (PreviewState.TextureSize.X <= 0 || PreviewState.TextureSize.Y <= 0)
    {
        PreviewState.Recovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::InvalidPayload,
            FPlatformTime::Seconds());
        return false;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 MaterialSlotIndex = PreviewState.MaterialSlotIndex;
    const int32 UVChannelIndex = PreviewState.UVChannelIndex;
    const FIntPoint TextureSize = PreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = PreviewState.WorkingTextureSize;
    const uint64 SnapshotContentRevision = PreviewState.ContentRevision;
    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview;
    Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Descriptor.DomainRevision = WorkerJobScheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.MemoryEstimate = EstimateWetWrinklePreviewAdmissionMemory(
        Asset,
        TextureSize,
        WorkingTextureSize);
    Descriptor.DebugName = FString::Printf(TEXT("Wrinkle preview slot %d"), MaterialSlotIndex);

    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, MaterialSlotIndex, UVChannelIndex, TextureSize, WorkingTextureSize, SnapshotContentRevision](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError)
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid() || CancellationToken->IsCanceled())
            {
                OutPrepareError = TEXT("The wrinkle preview request was canceled before its snapshot was prepared.");
                return false;
            }

            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            const UWetClothingAsset* CurrentAsset = Viewport->WetClothingAsset.Get();
            if (State == nullptr || CurrentAsset == nullptr ||
                State->ContentRevision != SnapshotContentRevision)
            {
                OutPrepareError = TEXT("The wrinkle preview source changed before admission.");
                return false;
            }

            FWetWrinkleAccumulatedPreviewJobInput Input;
            Input.TextureSize = TextureSize;
            Input.WorkingTextureSize = WorkingTextureSize;
            if (!Viewport->SpatialQueryService.IsValid())
            {
                OutPrepareError = TEXT("The wrinkle preview spatial service is unavailable.");
                return false;
            }
            TSharedPtr<FWetWrinkleSpatialLeaseOwner, ESPMode::ThreadSafe> SpatialLeaseOwner =
                MakeShared<FWetWrinkleSpatialLeaseOwner, ESPMode::ThreadSafe>();
            FString SpatialError;
            SpatialLeaseOwner->Lease = Viewport->SpatialQueryService->AcquireLease(
                CurrentAsset,
                Viewport->ResolveTargetMesh(),
                UVChannelIndex,
                MaterialSlotIndex,
                &SpatialError);
            if (!SpatialLeaseOwner->Lease.IsValid())
            {
                OutPrepareError = SpatialError.IsEmpty()
                    ? TEXT("The wrinkle preview could not lease the selected surface topology.")
                    : SpatialError;
                return false;
            }
            const FDWCEditorSpatialHandle JobSpatialHandle =
                StaticCastSharedPtr<const FDWCEditorSpatialData>(
                    SpatialLeaseOwner->Lease.GetSharedValue());
            if (!JobSpatialHandle.IsValid())
            {
                OutPrepareError = TEXT("The wrinkle preview spatial lease has no usable payload.");
                return false;
            }
            Input.SpatialLeaseOwner = SpatialLeaseOwner;
            Input.SurfacePatchProjectionCache = Viewport->SurfacePatchProjectionCache;
            for (const FWetWrinklePatchPlacement& Stamp : CurrentAsset->Authored.WrinkleData.EditablePatches)
            {
                if (!Stamp.bEnabled || Stamp.MaterialSlotIndex != MaterialSlotIndex)
                {
                    continue;
                }

                FWetWrinkleSurfacePatchPreviewInput PatchInput;
                FString PatchError;
                if (BuildWetWrinkleSurfacePatchPreviewInput(
                        Stamp,
                        JobSpatialHandle,
                        PatchInput,
                        PatchError))
                {
                    Input.SurfacePatches.Add(MoveTemp(PatchInput));
                }
                else
                {
                    ++Input.InvalidSurfacePatchCount;
                    if (Input.FirstSurfacePatchError.IsEmpty())
                    {
                        Input.FirstSurfacePatchError = FString::Printf(
                            TEXT("Patch '%s' (%s): %s"),
                            Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName,
                            *Stamp.PatchGuid.ToString(EGuidFormats::Digits),
                            PatchError.IsEmpty() ? TEXT("invalid surface patch input") : *PatchError);
                    }
                }
            }
            for (const FWetProceduralRidgeStroke& Stroke :
                 CurrentAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
            {
                if (Stroke.bEnabled && Stroke.MaterialSlotIndex == MaterialSlotIndex &&
                    Stroke.StrokeGuid != Viewport->EditingProceduralStrokeGuid)
                {
                    Input.RidgeStrokes.Add(Stroke);
                }
            }

            OutPrepared.ActualMemoryEstimate = EstimateWetWrinklePreviewWorkerMemory(Input);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerCancellationToken) mutable
            {
                return FWetWrinkleAccumulatedPreviewWorker::Build(
                    MoveTemp(Input),
                    WorkerCancellationToken);
            };
            return true;
        },
        [WeakThis, MaterialSlotIndex, UVChannelIndex, SnapshotContentRevision, CommitToken](
            const FDWCEditorWorkerJobTicket& Ticket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FWetWrinkleAccumulatedPreviewJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() || !Viewport->PreviewCommitCoordinator.IsValid())
            {
                return;
            }
            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            if (State == nullptr)
            {
                return;
            }

            FDWCEditorPreviewCommitContext CommitContext;
            CommitContext.ConsumerToken = CommitToken;
            CommitContext.ProducerSessionEpoch = Ticket.SessionEpoch;
            CommitContext.DebugName = FString::Printf(
                TEXT("Wrinkle accumulated preview slot %d"),
                MaterialSlotIndex);
            CommitContext.IsCurrent = [Viewport, MaterialSlotIndex, UVChannelIndex, SnapshotContentRevision, Ticket]()
            {
                const FWetWrinkleAccumulatedPreviewState* CurrentState =
                    Viewport->AccumulatedPreviewStates.FindByPredicate(
                        [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                        {
                            return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                                Candidate.UVChannelIndex == UVChannelIndex;
                        });
                return !Viewport->bPreviewSuspended && CurrentState != nullptr &&
                    CurrentState->PendingTicket.JobId == Ticket.JobId &&
                    CurrentState->PendingTicket.Generation == Ticket.Generation &&
                    CurrentState->ContentRevision == SnapshotContentRevision;
            };

            FDWCEditorTextureLease NewLease;
            const EDWCEditorPreviewCommitResult CommitResult =
                Viewport->PreviewCommitCoordinator->CommitNormalBGRA8(
                CommitContext,
                MakeWrinkleTextureKey(
                    Viewport->WetClothingAsset.Get(),
                    EDWCEditorTexturePurpose::WrinkleAccumulated,
                    MaterialSlotIndex),
                MakeWrinkleNormalDescriptor(State->TextureSize, State->WorkingTextureSize),
                MoveTemp(Result->Pixels),
                MoveTemp(Result->WorkingSurface),
                NewLease,
                EDWCEditorTextureUploadPriority::Interactive);
            const bool bOwnsPendingTicket =
                State->PendingTicket.JobId == Ticket.JobId &&
                State->PendingTicket.Generation == Ticket.Generation;
            if (!bOwnsPendingTicket)
            {
                return;
            }
            State->bRebuildPending = false;
            State->PendingTicket = {};
            State->PendingContentRevision = 0;

            if (CommitResult == EDWCEditorPreviewCommitResult::Applied)
            {
                State->TextureSize = Result->TextureSize;
                State->WorkingTextureSize = Result->WorkingTextureSize;
                State->TextureHandle = MoveTemp(NewLease);
                State->bDirty = false;
                State->Recovery.MarkSucceeded();
                Viewport->PatchPreviewValidationError.Reset();
                Viewport->RefreshViewportHint();
                ++Viewport->AccumulatedPreviewRebuildCount;
                Viewport->RefreshWrinklePreviewAccumulatedParameters();
                Viewport->CompletePatchHoverHandoff(
                    MaterialSlotIndex,
                    UVChannelIndex,
                    Viewport->RenderUploadQueue.IsValid()
                        ? Viewport->RenderUploadQueue->CaptureTicket(State->TextureHandle.GetHandle())
                        : FDWCEditorTextureUploadTicket(),
                    0,
                    SnapshotContentRevision);
                Viewport->Invalidate();
            }
            else
            {
                // Keep the last successful texture bound and retry from the next
                // preview update instead of silently marking the state clean.
                State->bDirty = true;
                if (CommitResult == EDWCEditorPreviewCommitResult::StaleRequest &&
                    State->ContentRevision != SnapshotContentRevision)
                {
                    State->Recovery.Invalidate(
                        EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
                }
                const EDWCEditorPreviewRecoveryAction RecoveryAction =
                    State->Recovery.HandleCommitResult(
                        CommitResult,
                        FPlatformTime::Seconds());
                if (CommitResult == EDWCEditorPreviewCommitResult::WorkspaceRejected)
                {
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Failed to transfer the accumulated wrinkle preview for slot %d; keeping the previous preview and scheduling a retry."),
                        MaterialSlotIndex);
                }
                if (RecoveryAction == EDWCEditorPreviewRecoveryAction::Degraded)
                {
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Accumulated wrinkle preview recovery entered degraded mode for slot %d; the last valid preview will remain visible until the content or context changes."),
                        MaterialSlotIndex);
                    if (Viewport->PatchHoverPreviewState.IsCommitHandoffPending() &&
                        Viewport->PatchHoverPreviewState.MaterialSlotIndex == MaterialSlotIndex)
                    {
                        Viewport->RecoverPatchHoverHandoff(
                            EDWCEditorPreviewInvalidationReason::WorkerFailed);
                    }
                }
                Viewport->Invalidate();
            }
        },
        &SubmitError,
        [WeakThis, MaterialSlotIndex, UVChannelIndex](
            const FDWCEditorWorkerJobTicket& Ticket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Error)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }

            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            if (!Viewport.IsValid())
            {
                return;
            }
            FWetWrinkleAccumulatedPreviewState* State = Viewport->AccumulatedPreviewStates.FindByPredicate(
                [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                        Candidate.UVChannelIndex == UVChannelIndex;
                });
            if (State != nullptr && State->PendingTicket.JobId == Ticket.JobId &&
                State->PendingTicket.Generation == Ticket.Generation)
            {
                State->PendingTicket = {};
                State->bRebuildPending = false;
                State->PendingContentRevision = 0;
                State->bDirty = true;
                if (Completion == EDWCEditorWorkerJobCompletion::Failed)
                {
                    Viewport->PatchPreviewValidationError = Error.IsEmpty()
                        ? TEXT("The wrinkle preview could not validate one or more surface patches.")
                        : Error;
                    Viewport->RefreshViewportHint();
                    const EDWCEditorPreviewRecoveryAction RecoveryAction =
                        State->Recovery.MarkFailure(
                        EDWCEditorPreviewInvalidationReason::WorkerFailed,
                        FPlatformTime::Seconds());
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Failed to rebuild the accumulated wrinkle preview for slot %d: %s"),
                        MaterialSlotIndex,
                        Error.IsEmpty() ? TEXT("unknown worker failure") : *Error);
                    if (RecoveryAction == EDWCEditorPreviewRecoveryAction::Degraded &&
                        Viewport->PatchHoverPreviewState.IsCommitHandoffPending() &&
                        Viewport->PatchHoverPreviewState.MaterialSlotIndex == MaterialSlotIndex)
                    {
                        Viewport->RecoverPatchHoverHandoff(
                            EDWCEditorPreviewInvalidationReason::WorkerFailed);
                    }
                }
                else
                {
                    State->Recovery.RecordStaleResult();
                }
            }
        });
    PreviewState.bRebuildPending = Ticket.IsValid();
    PreviewState.PendingTicket = Ticket;
    PreviewState.PendingContentRevision = Ticket.IsValid() ? SnapshotContentRevision : 0;
    if (!Ticket.IsValid())
    {
        PreviewState.Recovery.MarkFailure(
            EDWCEditorPreviewInvalidationReason::SchedulerDeferred,
            FPlatformTime::Seconds());
        UE_LOG(LogWetWrinklePreviewViewport, Warning, TEXT("Failed to schedule %s: %s"), *Descriptor.DebugName, *SubmitError);
    }
    return Ticket.IsValid();
}

int32 SWetWrinkleViewport::ResolveActivePreviewMaterialSlot() const
{
    if (PreviewSession &&
        PreviewSession->GetSlotStates().IsReady(BrushSettings.MaterialSlotIndex))
    {
        return BrushSettings.MaterialSlotIndex;
    }

    return FDWCEditorPreviewSession::AllWettableSlots;
}

void SWetWrinkleViewport::ApplyMaterialSlotVisibility()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* SkeletalMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr)
    {
        return;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    constexpr int32 PreviewLODIndex = 0;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(PreviewLODIndex))
    {
        return;
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[PreviewLODIndex];
    for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
        const bool bShowSection = ActiveMaterialSlotIndex == FDWCEditorPreviewSession::AllWettableSlots
                                      ? PreviewSession && PreviewSession->GetSlotStates().IsReady(Section.MaterialIndex)
                                      : Section.MaterialIndex == ActiveMaterialSlotIndex;
        PreviewMeshComponent->ShowMaterialSection(
            Section.MaterialIndex,
            SectionIndex,
            bShowSection,
            PreviewLODIndex);
    }
}

void SWetWrinkleViewport::InvalidateAccumulatedPreviewTextures()
{
    MarkAccumulatedPreviewStatesDirty();
}

void SWetWrinkleViewport::CollectDiagnosticMemoryStats(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
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
    if (SurfacePatchProjectionCache.IsValid())
    {
        SurfacePatchProjectionCache->AppendDiagnosticMemoryBucket(OutBuckets);
    }

    FDWCEditorPreviewMemoryBucket& Procedural = OutBuckets.AddDefaulted_GetRef();
    Procedural.Name = TEXT("Wrinkle procedural control state");
    Procedural.UsedBytes = static_cast<uint64>(TransientProceduralStrokeHits.GetAllocatedSize());
    if (TransientProceduralPreviewState.LastCommittedStroke.IsSet())
    {
        Procedural.UsedBytes += static_cast<uint64>(
            TransientProceduralPreviewState.LastCommittedStroke->Points.GetAllocatedSize());
    }
    Procedural.EntryCount = !TransientProceduralStrokeHits.IsEmpty() ? 1 : 0;

    FDWCEditorPreviewMemoryBucket& Generated = OutBuckets.AddDefaulted_GetRef();
    Generated.Name = TEXT("Wrinkle generated-map preview");
    Generated.UsedBytes = FDWCEditorPreviewDiagnostics::EstimateTextureBytes(GeneratedNormalPreviewTexture);
    Generated.EntryCount = GeneratedNormalPreviewTexture != nullptr ? 1 : 0;
}

void SWetWrinkleViewport::CollectDiagnosticOperationStats(
    TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const
{
    OutCounters.Add({TEXT("Wrinkle preview mesh refreshes"), PreviewMeshRefreshCount, 0});
    OutCounters.Add({TEXT("Wrinkle accumulated texture rebuilds"), AccumulatedPreviewRebuildCount, 0});
    OutCounters.Add({TEXT("Wrinkle incremental region commits"), AccumulatedIncrementalCommitCount, 0});
    OutCounters.Add({TEXT("Wrinkle incremental full-rebuild fallbacks"), AccumulatedIncrementalFallbackCount, 0});
    OutCounters.Add({TEXT("Wrinkle transient region commits"), TransientIncrementalCommitCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover requests"), PatchHoverRequestCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover submissions"), PatchHoverSubmittedCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover presentations"), PatchHoverAppliedCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover superseded"), PatchHoverSupersededCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover canceled"), PatchHoverCanceledCount, 0});
    OutCounters.Add({TEXT("Wrinkle patch hover failures"), PatchHoverFailedCount, 0});
    OutCounters.Add({TEXT("Wrinkle spatial-cache acquisitions"), HitTriangleBuildCount, 0});
    OutCounters.Add({TEXT("Wrinkle preview material assignment checks"), PreviewMaterialAssignmentCheckCount, 0});
    OutCounters.Add({TEXT("Wrinkle preview material assignment writes"), PreviewMaterialAssignmentWriteCount, 0});
    OutCounters.Add({TEXT("Wrinkle preview material assignment skips"), PreviewMaterialAssignmentSkipCount, 0});
    OutCounters.Add({TEXT("Wrinkle preview material assignment cache hits"), PreviewMaterialAssignmentCacheHitCount, 0});
    uint64 RecoveryRetries = 0;
    uint64 RecoveryStaleDrops = 0;
    uint64 RecoveryDegraded = 0;
    for (const FWetWrinkleAccumulatedPreviewState& State : AccumulatedPreviewStates)
    {
        const FDWCEditorPreviewRecoveryDiagnostics& Diagnostics = State.Recovery.GetDiagnostics();
        RecoveryRetries += Diagnostics.RetryCount;
        RecoveryStaleDrops += Diagnostics.StaleDropCount;
        RecoveryDegraded += Diagnostics.DegradedCount;
    }
    OutCounters.Add({TEXT("Wrinkle preview recovery retries"), RecoveryRetries, 0});
    OutCounters.Add({TEXT("Wrinkle preview stale result drops"), RecoveryStaleDrops, 0});
    OutCounters.Add({TEXT("Wrinkle preview degraded transitions"), RecoveryDegraded, 0});
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->AppendDiagnosticOperationCounters(OutCounters);
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->AppendDiagnosticOperationCounters(OutCounters);
    }
}

void SWetWrinkleViewport::ResetDiagnosticCounters()
{
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->ResetDiagnosticCounters();
    }
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->ResetDiagnosticCounters();
    }
    if (SpatialQueryService.IsValid())
    {
        SpatialQueryService->ResetDiagnosticCounters();
    }
    if (SurfacePatchProjectionCache.IsValid())
    {
        SurfacePatchProjectionCache->ResetDiagnosticCounters();
    }
    PreviewMeshRefreshCount = 0;
    AccumulatedPreviewRebuildCount = 0;
    AccumulatedIncrementalCommitCount = 0;
    AccumulatedIncrementalFallbackCount = 0;
    TransientIncrementalCommitCount = 0;
    PatchHoverRequestCount = 0;
    PatchHoverSubmittedCount = 0;
    PatchHoverAppliedCount = 0;
    PatchHoverSupersededCount = 0;
    PatchHoverCanceledCount = 0;
    PatchHoverFailedCount = 0;
    HitTriangleBuildCount = 0;
    PreviewMaterialAssignmentCheckCount = 0;
    PreviewMaterialAssignmentWriteCount = 0;
    PreviewMaterialAssignmentSkipCount = 0;
    PreviewMaterialAssignmentCacheHitCount = 0;
    for (FWetWrinkleAccumulatedPreviewState& State : AccumulatedPreviewStates)
    {
        State.Recovery.ResetDiagnostics();
    }
}

void SWetWrinkleViewport::RebuildHitTriangles()
{
    SpatialLease.Reset();
    SpatialHandle.Reset();
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE ||
        BrushSettings.UVChannelIndex == INDEX_NONE ||
        !SpatialQueryService.IsValid())
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh == nullptr)
    {
        return;
    }

    ++HitTriangleBuildCount;
    FString Error;
    SpatialLease = SpatialQueryService->AcquireLease(
        WetClothingAsset.Get(),
        TargetMesh,
        BrushSettings.UVChannelIndex,
        BrushSettings.MaterialSlotIndex,
        &Error);
    SpatialHandle = StaticCastSharedPtr<const FDWCEditorSpatialData>(SpatialLease.GetSharedValue());
    if (!SpatialHandle.IsValid() && !Error.IsEmpty())
    {
        UE_LOG(
            LogWetWrinklePreviewViewport,
            VeryVerbose,
            TEXT("Unable to prepare shared spatial data: %s"),
            *Error);
    }
}

void SWetWrinkleViewport::HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (AreWetWrinkleSurfaceHitsEquivalentForPreview(CurrentSurfaceHit, SurfaceHit))
    {
        return;
    }

    CurrentSurfaceHit = SurfaceHit;
    RefreshViewportHint();
    if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->HandleSurfaceHitChanged(CurrentSurfaceHit);
    }
    RefreshBrushCursor();
    RefreshWrinklePreviewHoverParameters();

    if (OnSurfaceHitChanged.IsBound())
    {
        OnSurfaceHitChanged.Execute(CurrentSurfaceHit);
    }
}

void SWetWrinkleViewport::SetAuthoringController(
    const TSharedPtr<FWetWrinkleAuthoringController>& InController)
{
    AuthoringController = InController;
}

void SWetWrinkleViewport::RefreshBrushCursor()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetWrinkleViewport::ClearBrushCursor()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetWrinkleViewport::DrawBrushCursor(FPrimitiveDrawInterface* PDI) const
{
    if (PDI == nullptr || !BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    const float Radius = CalculateBrushCursorWorldRadius();
    if (Radius <= UE_SMALL_NUMBER)
    {
        return;
    }

    FVector SurfaceNormal = CurrentSurfaceHit.WorldNormal.GetSafeNormal();
    if (SurfaceNormal.IsNearlyZero())
    {
        SurfaceNormal = FVector::UpVector;
    }

    FVector SurfaceTangent = CurrentSurfaceHit.WorldSurfaceFrameU.GetSafeNormal();
    SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
    if (SurfaceTangent.IsNearlyZero())
    {
        SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
    }
    const FVector SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
    const FVector Center = CurrentSurfaceHit.WorldPosition + SurfaceNormal * FMath::Max(Radius * 0.01f, 0.15f);
    constexpr float Thickness = 2.0f;
    const FLinearColor CursorColor(1.0f, 0.35f, 0.03f, 1.0f);

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        FVector StrokeDirection = SurfaceTangent;
        if (TransientProceduralStrokeHits.Num() >= 2)
        {
            StrokeDirection = TransientProceduralStrokeHits.Last().WorldPosition -
                TransientProceduralStrokeHits[TransientProceduralStrokeHits.Num() - 2].WorldPosition;
            StrokeDirection = (StrokeDirection - SurfaceNormal * FVector::DotProduct(StrokeDirection, SurfaceNormal)).GetSafeNormal();
            if (StrokeDirection.IsNearlyZero())
            {
                StrokeDirection = SurfaceTangent;
            }
        }

        FVector WidthDirection = FVector::CrossProduct(SurfaceNormal, StrokeDirection).GetSafeNormal();
        if (WidthDirection.IsNearlyZero())
        {
            WidthDirection = SurfaceBitangent;
        }

        const float HalfWidth = FMath::Max(Radius * 0.5f, 0.25f);
        const float EndTickLength = FMath::Clamp(HalfWidth * 0.3f, 0.15f, 1.5f);
        const FVector WidthStart = Center - WidthDirection * HalfWidth;
        const FVector WidthEnd = Center + WidthDirection * HalfWidth;
        PDI->DrawLine(WidthStart, WidthEnd, CursorColor, SDPG_Foreground, Thickness, 0.0f, true);
        PDI->DrawLine(
            WidthStart - StrokeDirection * EndTickLength,
            WidthStart + StrokeDirection * EndTickLength,
            CursorColor,
            SDPG_Foreground,
            Thickness,
            0.0f,
            true);
        PDI->DrawLine(
            WidthEnd - StrokeDirection * EndTickLength,
            WidthEnd + StrokeDirection * EndTickLength,
            CursorColor,
            SDPG_Foreground,
            Thickness,
            0.0f,
            true);
        PDI->DrawPoint(Center, CursorColor, 6.0f, SDPG_Foreground);
        return;
    }

    constexpr int32 SegmentCount = 64;
    FVector Previous = Center + SurfaceTangent * Radius;
    for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
    {
        const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount)) * UE_TWO_PI;
        const FVector Current = Center +
            (SurfaceTangent * FMath::Cos(Angle) + SurfaceBitangent * FMath::Sin(Angle)) * Radius;
        PDI->DrawLine(Previous, Current, CursorColor, SDPG_Foreground, Thickness, 0.0f, true);
        Previous = Current;
    }
}

float SWetWrinkleViewport::CalculateBrushCursorWorldRadius() const
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return 5.0f;
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch)
    {
        return FMath::Max(BrushSettings.PatchDiameterLocal * 0.5f, 0.05f);
    }

    const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform());
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    return FMath::Clamp(MeshRadius * BrushSettings.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
}

FText SWetWrinkleViewport::GetViewportHintText() const
{
    if (ResolveTargetMesh() == nullptr)
    {
        return LOCTEXT("NoTargetMeshHint", "Assign a Target Mesh or Source Wet Clothing Asset.");
    }

    if (!SpatialLease.IsValid() || !SpatialHandle.IsValid() || SpatialHandle->Triangles.IsEmpty())
    {
        return LOCTEXT("NoHitTrianglesHint", "No triangles available for the selected UV channel/material slot.");
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
        !PatchPreviewValidationError.IsEmpty())
    {
        return FText::FromString(PatchPreviewValidationError);
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        if (BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Draw &&
            (bTransientProceduralStartJunction || bTransientProceduralEndJunction))
        {
            if (bTransientProceduralStartJunction && bTransientProceduralEndJunction)
            {
                return LOCTEXT("RidgeBothJunctionCandidateHint", "Junction candidate: Start + End");
            }
            return bTransientProceduralStartJunction
                ? LOCTEXT("RidgeStartJunctionCandidateHint", "Junction candidate: Start")
                : LOCTEXT("RidgeEndJunctionCandidateHint", "Junction candidate: End");
        }
        if (BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
        {
            return BrushSettings.bRidgeJunctionModeEnabled
                ? LOCTEXT("RidgeEditViewportHint", "Drag a selected ridge control point. Shift-click a segment to insert a point. Endpoints snap to nearby ridges.")
                : LOCTEXT("RidgeEditNoJunctionViewportHint", "Drag a selected ridge control point. Shift-click a segment to insert a point. Junction snapping is off.");
        }
        return BrushSettings.bRidgeJunctionModeEnabled
            ? LOCTEXT("RidgeDrawViewportHint", "Drag on the mesh to draw a ridge. Endpoints snap to nearby ridges to form junctions.")
            : LOCTEXT("RidgeDrawNoJunctionViewportHint", "Drag on the mesh to draw a ridge. Junction snapping is off.");
    }

    return LOCTEXT("ViewportHint", "Move the cursor over the mesh to inspect wrinkle brush UV hits.");
}

FSlateColor SWetWrinkleViewport::GetViewportHintColor() const
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
        !PatchPreviewValidationError.IsEmpty())
    {
        return FSlateColor(FLinearColor(1.0f, 0.72f, 0.15f));
    }
    return FSlateColor::UseForeground();
}

void SWetWrinkleViewport::RefreshViewportHint()
{
    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
        OverlayText->SetColorAndOpacity(GetViewportHintColor());
    }
}

void SWetWrinkleViewport::FindProjectedSurfacesAtUV(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();
    if (!SpatialQueryService.IsValid() || !SpatialLease.IsValid() || !SpatialHandle.IsValid() ||
        PreviewMeshComponent == nullptr || UVChannelIndex != SpatialHandle->UVChannelIndex ||
        MaterialSlotIndex != SpatialHandle->MaterialSlotIndex)
    {
        return;
    }

    SpatialQueryService->FindSurfacesAtUV(
        SpatialHandle,
        PreviewMeshComponent,
        UV,
        OutSurfaces);
}

bool SWetWrinkleViewport::ResolveProceduralStrokePointWorld(
    const FWetProceduralRidgeStrokePoint& Point,
    int32 MaterialSlotIndex,
    FVector& OutWorldPosition,
    FVector& OutWorldNormal) const
{
    if (!SpatialQueryService.IsValid() || !SpatialLease.IsValid() ||
        !SpatialHandle.IsValid() || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    FDWCEditorProjectedSurface Surface;
    if (!SpatialQueryService->ResolveTriangleAnchor(
            SpatialHandle,
            PreviewMeshComponent,
            MaterialSlotIndex,
            Point.AnchorTriangleID,
            Point.AnchorBarycentric,
            Surface))
    {
        return false;
    }
    OutWorldPosition = Surface.WorldPosition;
    OutWorldNormal = Surface.WorldNormal;
    return true;
}

bool SWetWrinkleViewport::TryBuildSurfaceHitFromProceduralStrokePoint(
    const FWetProceduralRidgeStrokePoint& Point,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    if (!SpatialQueryService.IsValid() || !SpatialLease.IsValid() ||
        !SpatialHandle.IsValid() || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    FDWCEditorProjectedSurface Surface;
    if (!SpatialQueryService->ResolveTriangleAnchor(
            SpatialHandle,
            PreviewMeshComponent,
            MaterialSlotIndex,
            Point.AnchorTriangleID,
            Point.AnchorBarycentric,
            Surface))
    {
        return false;
    }

    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = MaterialSlotIndex;
    OutHit.TriangleID = Surface.TriangleID;
    OutHit.UVIslandID = Surface.UVIslandID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = Surface.WorldPosition;
    OutHit.WorldNormal = Surface.WorldNormal;
    OutHit.WorldTangent = Surface.WorldTangent;
    OutHit.WorldBitangent = Surface.WorldBitangent;
    OutHit.WorldSurfaceFrameU = Surface.WorldSurfaceFrameU;
    OutHit.WorldSurfaceFrameV = Surface.WorldSurfaceFrameV;
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface.WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldNormal)
        .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = Surface.LocalTangent;
    OutHit.LocalBitangent = Surface.LocalBitangent;
    OutHit.LocalSurfaceAxisU = Surface.LocalSurfaceAxisU;
    OutHit.LocalSurfaceAxisV = Surface.LocalSurfaceAxisV;
    OutHit.LocalSurfaceFrameU = Surface.LocalSurfaceFrameU;
    OutHit.LocalSurfaceFrameV = Surface.LocalSurfaceFrameV;
    OutHit.SurfaceUnitsPerUV = Surface.SurfaceUnitsPerUV;
    OutHit.UV = Point.PositionUV;
    OutHit.Barycentric = Surface.Barycentric;
    OutHit.DistanceSq = 0.0;
    return true;
}

void SWetWrinkleViewport::DrawProceduralStrokeGuides(FPrimitiveDrawInterface* PDI) const
{
    if (PDI == nullptr)
    {
        return;
    }

    constexpr float GuideOffset = 0.35f;
    constexpr float GuideThickness = 2.0f;
    const FLinearColor StoredColor(1.0f, 0.35f, 0.05f, 1.0f);
    const FLinearColor TransientColor(0.0f, 0.85f, 1.0f, 1.0f);
    const FLinearColor JunctionColor(1.0f, 0.72f, 0.05f, 1.0f);
    const FLinearColor FlaredColor(0.85f, 0.45f, 1.0f, 1.0f);

    const FWetProceduralRidgeStroke* StrokeToDraw = nullptr;
    if (EditedProceduralStrokePreview.IsSet() &&
        EditedProceduralStrokePreview->StrokeGuid == SelectedProceduralStrokeGuid)
    {
        StrokeToDraw = &EditedProceduralStrokePreview.GetValue();
    }
    else if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        StrokeToDraw = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
            [this](const FWetProceduralRidgeStroke& Candidate)
            {
                return Candidate.StrokeGuid == SelectedProceduralStrokeGuid;
            });
    }

    if (StrokeToDraw != nullptr)
    {
        const FWetProceduralRidgeStroke* Stroke = StrokeToDraw;
        FVector Previous = FVector::ZeroVector;
        bool bHasPrevious = false;
        for (int32 PointIndex = 0; PointIndex < Stroke->Points.Num(); ++PointIndex)
        {
            const FWetProceduralRidgeStrokePoint& Point = Stroke->Points[PointIndex];
            FVector Position = FVector::ZeroVector;
            FVector Normal = FVector::UpVector;
            if (!ResolveProceduralStrokePointWorld(Point, Stroke->MaterialSlotIndex, Position, Normal))
            {
                bHasPrevious = false;
                continue;
            }

            Position += Normal * GuideOffset;
            if (bHasPrevious)
            {
                PDI->DrawLine(Previous, Position, StoredColor, SDPG_Foreground, GuideThickness, 0.0f, true);
            }
            Previous = Position;
            bHasPrevious = true;

            const bool bStartJunction = PointIndex == 0 && Stroke->StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Junction;
            const bool bEndJunction = PointIndex == Stroke->Points.Num() - 1 && Stroke->EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Junction;
            const bool bJunction = bStartJunction || bEndJunction;
            const bool bStartFlared = PointIndex == 0 && Stroke->StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared;
            const bool bEndFlared = PointIndex == Stroke->Points.Num() - 1 && Stroke->EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared;
            const bool bFlared = bStartFlared || bEndFlared;
            const FLinearColor PointColor = bJunction
                ? JunctionColor
                : (bFlared
                       ? FlaredColor
                       : (PointIndex == SelectedProceduralStrokePointIndex ? FLinearColor::White : StoredColor));
            PDI->DrawPoint(
                Position,
                PointColor,
                bJunction || bFlared || PointIndex == SelectedProceduralStrokePointIndex ? 10.0f : 6.0f,
                SDPG_Foreground);
        }
    }

    for (int32 PointIndex = 1; PointIndex < TransientProceduralStrokeHits.Num(); ++PointIndex)
    {
        const FWetWrinkleSurfaceHit& PreviousHit = TransientProceduralStrokeHits[PointIndex - 1];
        const FWetWrinkleSurfaceHit& CurrentHit = TransientProceduralStrokeHits[PointIndex];
        const FVector Previous = PreviousHit.WorldPosition + PreviousHit.WorldNormal * GuideOffset;
        const FVector Current = CurrentHit.WorldPosition + CurrentHit.WorldNormal * GuideOffset;
        PDI->DrawLine(Previous, Current, TransientColor, SDPG_Foreground, GuideThickness, 0.0f, true);
    }

    if (!TransientProceduralStrokeHits.IsEmpty())
    {
        const FWetWrinkleSurfaceHit& First = TransientProceduralStrokeHits[0];
        const FWetWrinkleSurfaceHit& Last = TransientProceduralStrokeHits.Last();
        PDI->DrawPoint(
            First.WorldPosition + First.WorldNormal * GuideOffset,
            bTransientProceduralStartJunction ? JunctionColor : TransientColor,
            bTransientProceduralStartJunction ? 10.0f : 7.0f,
            SDPG_Foreground);
        if (TransientProceduralStrokeHits.Num() > 1)
        {
            PDI->DrawPoint(
                Last.WorldPosition + Last.WorldNormal * GuideOffset,
                bTransientProceduralEndJunction ? JunctionColor : TransientColor,
                bTransientProceduralEndJunction ? 10.0f : 7.0f,
                SDPG_Foreground);
        }
    }
}

#undef LOCTEXT_NAMESPACE
