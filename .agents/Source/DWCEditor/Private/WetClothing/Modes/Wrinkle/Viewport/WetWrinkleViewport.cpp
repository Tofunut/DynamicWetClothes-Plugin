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
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
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
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetWrinklePreviewViewport, Log, All);

namespace
{
    constexpr int32 WrinkleViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.

    FDWCEditorTextureKey MakeWrinkleTextureKey(
        const UWetClothingAsset* Asset,
        const EDWCEditorTexturePurpose Purpose,
        const int32 MaterialSlotIndex)
    {
        FDWCEditorTextureKey Key;
        Key.Owner = FObjectKey(Asset);
        Key.Purpose = Purpose;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        return Key;
    }

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
            ? Asset->Authored.WrinkleData.BakeSettings.DefaultResolution
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
        Estimate.SnapshotBytes = Input.Patches.GetAllocatedSize() + Input.RidgeStrokes.GetAllocatedSize();
        for (const FWetProceduralRidgeStroke& Stroke : Input.RidgeStrokes)
        {
            Estimate.SnapshotBytes += Stroke.Points.GetAllocatedSize();
            Estimate.SnapshotBytes += Stroke.DisplayName.GetAllocatedSize();
        }
        Estimate.WorkingBytes = WorkingSurfaceBytes;
        Estimate.OutputBytes = FinalPixelsBytes;
        Estimate.ScratchBytes = LargestRidgeScratchBytes;
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
            static_cast<uint64>(WrinkleData.EditablePatches.Num()) * sizeof(FDWCEditorNormalStampCommand) +
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
        return A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.UVChannelIndex == B.UVChannelIndex &&
               A.TriangleID == B.TriangleID &&
               (A.UV - B.UV).SizeSquared() <= UVToleranceSq;
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

    bool BuildWetWrinkleNormalStampCommand(
        const FWetWrinklePatchPlacement& Stamp,
        FDWCEditorNormalStampCommand& OutCommand)
    {
        if (Stamp.WrinkleNormalTexture == nullptr || Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            return false;
        }
        FString Error;
        if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                Stamp.WrinkleNormalTexture,
                OutCommand.NormalSource.Texture,
                Error))
        {
            return false;
        }
        OutCommand.NormalSource.bFlipGreenChannel = Stamp.WrinkleNormalTexture->bFlipGreenChannel;
        OutCommand.Footprint.CenterUV = FVector2f(Stamp.PositionUV);
        OutCommand.Footprint.RadiusUV = Stamp.BrushRadiusUV;
        OutCommand.Footprint.RotationRadians = Stamp.RotationRadians;
        OutCommand.Footprint.Scale = FVector2f(Stamp.Scale);
        OutCommand.Footprint.Falloff = Stamp.Falloff;
        OutCommand.Footprint.bWrap = true;
        OutCommand.Strength = Stamp.Strength;
        return OutCommand.NormalSource.IsValid();
    }

} // namespace

void SWetWrinkleViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    SessionStore = InArgs._SessionStore;
    SpatialQueryService = InArgs._SpatialQueryService;
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
    ReleaseAccumulatedPreviewStates();
    ReleaseTransientProceduralPreviewState();

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
        RefreshStoredStampOverlay();
    }
    RefreshWrinklePreviewMaterials();

    if (TargetMesh != nullptr)
    {
        const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(FTransform::Identity);
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
    else
    {
        PreviewScene->SetFloorOffset(0.0f);
    }

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }

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
    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        InBrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;

    BrushSettings = InBrushSettings;
    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    BrushSettings.UVChannelIndex = UVChannelIndex;
    BrushSettings.PreviewWetness = PreviewWetness;

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
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface->WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldTangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldBitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
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
    OutHit.LocalPosition = SharedHit.LocalPosition;
    OutHit.LocalNormal = SharedHit.LocalNormal;
    OutHit.LocalTangent = SharedHit.LocalTangent;
    OutHit.LocalBitangent = SharedHit.LocalBitangent;
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

bool SWetWrinkleViewport::CanBeginSurfaceInteraction(const FRay& WorldRay, double& OutHitDepth)
{
    return HitTestSurface(WorldRay, OutHitDepth);
}

void SWetWrinkleViewport::BeginSurfaceInteraction(const FRay& WorldRay)
{
    FWetWrinkleSurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->BeginSurfaceInteraction(Hit);
        }
    }
}

void SWetWrinkleViewport::UpdateSurfaceInteraction(const FRay& WorldRay)
{
    FWetWrinkleSurfaceHit Hit;
    if (TraceSurface(WorldRay.Origin, WorldRay.Direction, Hit))
    {
        HandleSurfaceHitFromClient(Hit);
        if (const TSharedPtr<FWetWrinkleAuthoringController> Controller = AuthoringController.Pin())
        {
            Controller->UpdateSurfaceInteraction(Hit);
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
                     [SAssignNew(OverlayText, SRichTextBlock)
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
        if (MaterialToApply == nullptr)
        {
            MaterialToApply = UMaterial::GetDefaultMaterial(MD_Surface);
        }
        PreviewMeshComponent->SetMaterial(SlotState.MaterialSlotIndex, MaterialToApply);
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
    const bool bEnableHover =
        BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
        BrushSettings.bShowPreview &&
        CurrentSurfaceHit.bHit &&
        CurrentSurfaceHit.MaterialSlotIndex == MaterialSlotIndex &&
        CurrentSurfaceHit.UVChannelIndex == BrushSettings.UVChannelIndex &&
        BrushSettings.WrinkleNormalTexture != nullptr;
    const float RadiusUV = bEnableHover
        ? FMath::Max(BrushSettings.BrushRadiusUV, UE_SMALL_NUMBER)
        : 0.0f;
    const float Strength = FMath::Clamp(BrushSettings.Strength, 0.0f, 4.0f);
    const float Falloff = FMath::Clamp(BrushSettings.Falloff, 0.0f, 1.0f);

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleHover;
    Layer.MaterialSlotIndex = MaterialSlotIndex;
    Layer.AddTexture(
        WetWrinklePreviewMaterialParameters::HoverNormal,
        bEnableHover ? BrushSettings.WrinkleNormalTexture.Get() : nullptr);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverEnabled, bEnableHover ? 1.0f : 0.0f);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverRadiusUV, RadiusUV);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverRotation, BrushSettings.RotationRadians);
    Layer.AddVector(
        WetWrinklePreviewMaterialParameters::HoverScale,
        FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverStrength, Strength);
    Layer.AddScalar(WetWrinklePreviewMaterialParameters::HoverFalloff, Falloff);
    Layer.AddVector(
        WetWrinklePreviewMaterialParameters::HoverCenterUV,
        bEnableHover
            ? FLinearColor(CurrentSurfaceHit.UV.X, CurrentSurfaceHit.UV.Y, 0.0f, 0.0f)
            : FLinearColor::Black);
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

void SWetWrinkleViewport::AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp)
{
    const int32 DataUVChannelIndex = WetClothingAsset.IsValid()
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (Stamp.MaterialSlotIndex == INDEX_NONE || DataUVChannelIndex < 0)
    {
        return;
    }

    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(Stamp.SourceTexture.Get(), Stamp.MaterialSlotIndex, DataUVChannelIndex);
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
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    PreviewState->TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState->WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState->TextureSize);
    if (PreviewState->TextureSize.X <= 0 || PreviewState->TextureSize.Y <= 0 ||
        PreviewState->TextureHandle->GetDescriptor().Size != PreviewState->TextureSize)
    {
        PreviewState->Recovery.Invalidate(EDWCEditorPreviewInvalidationReason::ResolutionChanged);
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
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
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    FDWCEditorNormalStampCommand Command;
    if (!BuildWetWrinkleNormalStampCommand(Stamp, Command))
    {
        return;
    }

    FWetWrinkleIncrementalCommand Delta;
    Delta.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    Delta.Patch = MoveTemp(Command);
    QueueAccumulatedIncrementalCommand(*PreviewState, MoveTemp(Delta));
    PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
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
    Descriptor.EstimatedBytes = Descriptor.MemoryEstimate.GetTotalBytes();
    Descriptor.DebugName = FString::Printf(
        TEXT("Wrinkle incremental preview slot %d [%llu-%llu]"),
        MaterialSlotIndex,
        FirstSequence,
        LastSequence);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitTwoPhase(
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
            OutPrepared.ActualEstimatedBytes = OutPrepared.ActualMemoryEstimate.GetTotalBytes();
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
    Descriptor.EstimatedBytes = Descriptor.MemoryEstimate.GetTotalBytes();
    Descriptor.DebugName = FString::Printf(TEXT("Wrinkle transient preview slot %d"), MaterialSlotIndex);

    const FDWCEditorPreviewConsumerToken CommitToken = PreviewCommitLifetime.CaptureToken();
    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitTwoPhase(
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
            OutPrepared.ActualEstimatedBytes = OutPrepared.ActualMemoryEstimate.GetTotalBytes();
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
    Descriptor.EstimatedBytes = Descriptor.MemoryEstimate.GetTotalBytes();
    Descriptor.DebugName = FString::Printf(TEXT("Wrinkle preview slot %d"), MaterialSlotIndex);

    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->SubmitTwoPhase(
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
            for (const FWetWrinklePatchPlacement& Stamp : CurrentAsset->Authored.WrinkleData.EditablePatches)
            {
                if (!Stamp.bEnabled || Stamp.MaterialSlotIndex != MaterialSlotIndex)
                {
                    continue;
                }

                FDWCEditorNormalStampCommand Command;
                if (BuildWetWrinkleNormalStampCommand(Stamp, Command))
                {
                    Input.Patches.Add(MoveTemp(Command));
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
            OutPrepared.ActualEstimatedBytes = OutPrepared.ActualMemoryEstimate.GetTotalBytes();
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
                ++Viewport->AccumulatedPreviewRebuildCount;
                Viewport->RefreshWrinklePreviewAccumulatedParameters();
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
                    State->Recovery.MarkFailure(
                        EDWCEditorPreviewInvalidationReason::WorkerFailed,
                        FPlatformTime::Seconds());
                    UE_LOG(
                        LogWetWrinklePreviewViewport,
                        Warning,
                        TEXT("Failed to rebuild the accumulated wrinkle preview for slot %d: %s"),
                        MaterialSlotIndex,
                        Error.IsEmpty() ? TEXT("unknown worker failure") : *Error);
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
    OutCounters.Add({TEXT("Wrinkle spatial-cache acquisitions"), HitTriangleBuildCount, 0});
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
    PreviewMeshRefreshCount = 0;
    AccumulatedPreviewRebuildCount = 0;
    AccumulatedIncrementalCommitCount = 0;
    AccumulatedIncrementalFallbackCount = 0;
    TransientIncrementalCommitCount = 0;
    HitTriangleBuildCount = 0;
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

    FVector SurfaceTangent = CurrentSurfaceHit.WorldTangent.GetSafeNormal();
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
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface.WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldNormal)
        .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldTangent)
        .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldBitangent)
        .GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
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
