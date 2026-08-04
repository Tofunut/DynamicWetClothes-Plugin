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
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewGraphExtension.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewMaterialParameters.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleAccumulatedPreviewWorker.h"
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

    uint64 EstimateWetWrinklePreviewWorkerPeakBytes(const FWetWrinkleAccumulatedPreviewJobInput& Input)
    {
        const uint64 WorkingSurfaceBytes =
            static_cast<uint64>(Input.WorkingTextureSize.X) * Input.WorkingTextureSize.Y * sizeof(uint32);
        const uint64 FinalPixelsBytes =
            static_cast<uint64>(Input.TextureSize.X) * Input.TextureSize.Y * sizeof(FColor);
        const uint64 LargestRidgeScratchBytes = Input.RidgeStrokes.IsEmpty()
            ? 0
            : FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound();

        // The source readbacks are immutable shared snapshots. Count only the
        // per-job arrays and allocations that coexist while this worker runs.
        return WorkingSurfaceBytes + FinalPixelsBytes +
            LargestRidgeScratchBytes + Input.Patches.GetAllocatedSize() + Input.RidgeStrokes.GetAllocatedSize();
    }

    bool ShouldDeferWetWrinkleInteractiveNormalRaster(const FIntPoint& WorkingTextureSize)
    {
        // Incremental rasterization writes the CPU working surface and encodes
        // the output in the editor tick. Keep that immediate path for small
        // maps only; 2K/4K maps are rebuilt by the bounded worker pipeline.
        constexpr uint64 MaxImmediatePixelCount = 1024ull * 1024ull;
        return static_cast<uint64>(FMath::Max(WorkingTextureSize.X, 0)) *
            FMath::Max(WorkingTextureSize.Y, 0) > MaxImmediatePixelCount;
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

    void IncludeWetWrinkleRect(FIntRect& InOutRect, bool& bHasRect, const FIntRect& Rect)
    {
        if (Rect.Width() <= 0 || Rect.Height() <= 0)
        {
            return;
        }

        if (!bHasRect)
        {
            InOutRect = Rect;
            bHasRect = true;
            return;
        }

        InOutRect.Min.X = FMath::Min(InOutRect.Min.X, Rect.Min.X);
        InOutRect.Min.Y = FMath::Min(InOutRect.Min.Y, Rect.Min.Y);
        InOutRect.Max.X = FMath::Max(InOutRect.Max.X, Rect.Max.X);
        InOutRect.Max.Y = FMath::Max(InOutRect.Max.Y, Rect.Max.Y);
    }
} // namespace

void SWetWrinkleViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    SessionStore = InArgs._SessionStore;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
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

    FlushTransientProceduralPreviewUpload();
    if (RenderUploadQueue.IsValid())
    {
        RenderUploadQueue->Flush();
    }
    if (bPreviewMaterialsNeedReapply)
    {
        ApplyPreviewMaterialsToMesh();
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
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    TArray<FColor>& Pixels = PreviewState->TextureHandle->GetMutableBGRA8Pixels();
    FDWCEditorNormalRasterSurface& WorkingSurface =
        PreviewState->TextureHandle->GetMutableWorkingNormalSurface();
    if (Pixels.Num() != PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != PreviewState->WorkingTextureSize)
    {
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

    if (ShouldDeferInteractiveNormalRaster(PreviewState->WorkingTextureSize))
    {
        // The patch was already committed to the asset. Rebuild from the
        // authoritative patch list on a worker instead of freezing Slate by
        // updating a 2K/4K CPU surface on this input event.
        PreviewState->bDirty = true;
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }
    const FDWCEditorRasterResult RasterResult =
        FDWCEditorNormalRasterCore::RasterizeStamp(Command, WorkingSurface);
    if (!RasterResult.bAffectedPixels)
    {
        return;
    }

    const FIntRect FinalDirtyRect = FDWCEditorRasterPostProcess::MapRect(
        RasterResult.DirtyRect,
        PreviewState->WorkingTextureSize,
        PreviewState->TextureSize);
    if (!EncodeWetWrinklePreviewSurface(
            WorkingSurface,
            PreviewState->TextureSize,
            Pixels,
            FinalDirtyRect))
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        return;
    }
    TextureWorkspace->MarkDirty(PreviewState->TextureHandle, FinalDirtyRect, true);
    PreviewState->bDirty = false;
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
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    TArray<FColor>& Pixels = PreviewState->TextureHandle->GetMutableBGRA8Pixels();
    FDWCEditorNormalRasterSurface& WorkingSurface =
        PreviewState->TextureHandle->GetMutableWorkingNormalSurface();
    if (Pixels.Num() != PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != PreviewState->WorkingTextureSize)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        return;
    }

    if (ShouldDeferInteractiveNormalRaster(PreviewState->WorkingTextureSize))
    {
        // Ridge strokes are committed on mouse-up. Use the same worker rebuild
        // as patches at high resolutions instead of rasterizing every edit on
        // the game thread.
        PreviewState->bDirty = true;
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, DataUVChannelIndex);
        return;
    }

    const FWetProceduralRidgeRasterResult RasterResult = FWetProceduralRidgeRasterizer::RasterizeToSurface(
        Stroke,
        WorkingSurface);
    if (RasterResult.bAffectedPixels)
    {
        const FIntRect FinalDirtyRect = FDWCEditorRasterPostProcess::MapRect(
            RasterResult.DirtyRect,
            PreviewState->WorkingTextureSize,
            PreviewState->TextureSize);
        if (!EncodeWetWrinklePreviewSurface(
                WorkingSurface,
                PreviewState->TextureSize,
                Pixels,
                FinalDirtyRect))
        {
            RebuildAccumulatedPreviewTexture(*PreviewState);
            return;
        }
        TextureWorkspace->MarkDirty(PreviewState->TextureHandle, FinalDirtyRect, true);
    }
    PreviewState->bDirty = false;
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

void SWetWrinkleViewport::ReleaseTransientProceduralPreviewState()
{
    if (TextureWorkspace.IsValid() && TransientProceduralPreviewState.TextureHandle.IsValid())
    {
        TextureWorkspace->Discard(TransientProceduralPreviewState.TextureHandle);
    }
    TransientProceduralPreviewState = FWetProceduralRidgeTransientPreviewState();
    bTransientProceduralPreviewBound = false;
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
    PendingTransientProceduralUploadRect = FIntRect();
    bHasPendingTransientProceduralUpload = false;
}

void SWetWrinkleViewport::FlushTransientProceduralPreviewUpload()
{
    if (!bHasPendingTransientProceduralUpload ||
        !TransientProceduralPreviewState.TextureHandle.IsValid() ||
        PendingTransientProceduralUploadRect.IsEmpty())
    {
        return;
    }

    TextureWorkspace->MarkDirty(
        TransientProceduralPreviewState.TextureHandle,
        PendingTransientProceduralUploadRect,
        true);
    PendingTransientProceduralUploadRect = FIntRect();
    bHasPendingTransientProceduralUpload = false;
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
    ReleaseTransientProceduralPreviewState();
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
    const FDWCEditorTextureHandle PublishedTexture = TextureWorkspace->PublishNormalBGRA8(
        MakeWrinkleTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::WrinkleProcedural,
            MaterialSlotIndex),
        MakeWrinkleNormalDescriptor(TextureSize, WorkingTextureSize),
        MoveTemp(Pixels),
        MoveTemp(WorkingSurface),
        EDWCEditorTextureUploadPriority::Interactive);
    TransientProceduralPreviewState.TextureHandle = TextureWorkspace->AcquireLease(PublishedTexture);
    if (!TransientProceduralPreviewState.TextureHandle.IsValid())
    {
        ReleaseTransientProceduralPreviewState();
        return false;
    }

    return true;
}

bool SWetWrinkleViewport::UpdateTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke)
{
    const int32 DataUVChannelIndex = WetClothingAsset.IsValid()
        ? WetClothingAsset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
    if (Stroke.Points.Num() < 2 || Stroke.MaterialSlotIndex == INDEX_NONE || DataUVChannelIndex < 0)
    {
        return false;
    }

    const FIntPoint RequestedTextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    const FIntPoint RequestedWorkingTextureSize =
        WetWrinkleTextureRaster::ResolveWorkingTextureSize(RequestedTextureSize);
    if (ShouldDeferInteractiveNormalRaster(RequestedWorkingTextureSize))
    {
        // High-resolution ridge editing uses only the control-line overlay while
        // dragging. Do this before allocating or touching a full normal surface;
        // the committed mouse-up stroke is rebuilt on a worker.
        ReleaseTransientProceduralPreviewState();
        if (bTransientProceduralPreviewBound)
        {
            bTransientProceduralPreviewBound = false;
            RefreshWrinklePreviewTransientParameters();
        }
        return true;
    }

    if (!EnsureTransientProceduralPreviewState(Stroke.MaterialSlotIndex, DataUVChannelIndex))
    {
        return false;
    }

    const FIntPoint TextureSize = TransientProceduralPreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = TransientProceduralPreviewState.WorkingTextureSize;
    FWetProceduralRidgeStroke PreviousStroke;
    PreviousStroke.MaterialSlotIndex = TransientProceduralPreviewState.MaterialSlotIndex;
    PreviousStroke.Shape = static_cast<EWetProceduralRidgeShape>(TransientProceduralPreviewState.PreviousShape);
    PreviousStroke.bFlipFoldSide = TransientProceduralPreviewState.bPreviousFlipFoldSide;
    PreviousStroke.WidthUV = TransientProceduralPreviewState.PreviousWidthUV;
    PreviousStroke.Strength = TransientProceduralPreviewState.PreviousStrength;
    PreviousStroke.Falloff = TransientProceduralPreviewState.PreviousFalloff;
    PreviousStroke.StartTaper = TransientProceduralPreviewState.PreviousStartTaper;
    PreviousStroke.EndTaper = TransientProceduralPreviewState.PreviousEndTaper;
    PreviousStroke.StartEndpoint.Mode = static_cast<EWetProceduralRidgeEndpointMode>(TransientProceduralPreviewState.PreviousStartEndpointMode);
    PreviousStroke.EndEndpoint.Mode = static_cast<EWetProceduralRidgeEndpointMode>(TransientProceduralPreviewState.PreviousEndEndpointMode);
    PreviousStroke.FlareSettings = TransientProceduralPreviewState.PreviousFlareSettings;
    PreviousStroke.NaturalVariation = TransientProceduralPreviewState.PreviousNaturalVariation;
    for (const FVector2D& UV : TransientProceduralPreviewState.PreviousPointUVs)
    {
        FWetProceduralRidgeStrokePoint& Point = PreviousStroke.Points.AddDefaulted_GetRef();
        Point.PositionUV = UV;
    }

    int32 CommonPointCount = 0;
    while (CommonPointCount < PreviousStroke.Points.Num() && CommonPointCount < Stroke.Points.Num() &&
           PreviousStroke.Points[CommonPointCount].PositionUV.Equals(Stroke.Points[CommonPointCount].PositionUV, 1.0e-6))
    {
        ++CommonPointCount;
    }

    const auto VariationsEqual = [](const FWetProceduralRidgeVariationSettings& A, const FWetProceduralRidgeVariationSettings& B)
    {
        return A.bEnabled == B.bEnabled &&
            FMath::IsNearlyEqual(A.CenterlineAmount, B.CenterlineAmount) &&
            FMath::IsNearlyEqual(A.CenterlineFrequency, B.CenterlineFrequency) &&
            FMath::IsNearlyEqual(A.WidthVariation, B.WidthVariation) &&
            FMath::IsNearlyEqual(A.WidthFrequency, B.WidthFrequency) &&
            A.NoiseSeed == B.NoiseSeed;
    };
    const bool bSettingsChanged =
        PreviousStroke.Shape != Stroke.Shape ||
        PreviousStroke.bFlipFoldSide != Stroke.bFlipFoldSide ||
        !FMath::IsNearlyEqual(PreviousStroke.WidthUV, Stroke.WidthUV) ||
        !FMath::IsNearlyEqual(PreviousStroke.Strength, Stroke.Strength) ||
        !FMath::IsNearlyEqual(PreviousStroke.Falloff, Stroke.Falloff) ||
        !FMath::IsNearlyEqual(PreviousStroke.StartTaper, Stroke.StartTaper) ||
        !FMath::IsNearlyEqual(PreviousStroke.EndTaper, Stroke.EndTaper) ||
        PreviousStroke.StartEndpoint.Mode != Stroke.StartEndpoint.Mode ||
        PreviousStroke.EndEndpoint.Mode != Stroke.EndEndpoint.Mode ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.Length, Stroke.FlareSettings.Length) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.WidthScale, Stroke.FlareSettings.WidthScale) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.EndStrength, Stroke.FlareSettings.EndStrength) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.Softness, Stroke.FlareSettings.Softness) ||
        !VariationsEqual(PreviousStroke.NaturalVariation, Stroke.NaturalVariation);
    const int32 FirstChangedPoint = bSettingsChanged ? 0 : FMath::Max(CommonPointCount - 2, 0);

    FIntRect DirtyRect;
    bool bHasDirtyRect = false;
    if (PreviousStroke.Points.Num() >= 2)
    {
        IncludeWetWrinkleRect(
            DirtyRect,
            bHasDirtyRect,
            FWetProceduralRidgeRasterizer::ComputeBounds(PreviousStroke, WorkingTextureSize, FirstChangedPoint));
    }
    IncludeWetWrinkleRect(
        DirtyRect,
        bHasDirtyRect,
        FWetProceduralRidgeRasterizer::ComputeBounds(Stroke, WorkingTextureSize, FirstChangedPoint));
    if (!bHasDirtyRect)
    {
        return false;
    }

    FDWCEditorNormalRasterSurface& WorkingSurface =
        TransientProceduralPreviewState.TextureHandle->GetMutableWorkingNormalSurface();
    TArray<FColor>& Pixels =
        TransientProceduralPreviewState.TextureHandle->GetMutableBGRA8Pixels();
    for (int32 PixelY = DirtyRect.Min.Y; PixelY < DirtyRect.Max.Y; ++PixelY)
    {
        float* CoverageRow = WorkingSurface.HasCoverage()
            ? WorkingSurface.Coverage.GetData() + PixelY * WorkingTextureSize.X
            : nullptr;
        for (int32 PixelX = DirtyRect.Min.X; PixelX < DirtyRect.Max.X; ++PixelX)
        {
            WorkingSurface.SetNormal(
                PixelY * WorkingTextureSize.X + PixelX,
                FVector3f(0.0f, 0.0f, 1.0f));
            if (CoverageRow != nullptr)
            {
                CoverageRow[PixelX] = 0.0f;
            }
        }
    }

    FWetProceduralRidgeRasterizer::RasterizeToSurface(
        Stroke,
        WorkingSurface,
        &DirtyRect);
    const FIntRect FinalDirtyRect = FDWCEditorRasterPostProcess::MapRect(
        DirtyRect,
        WorkingTextureSize,
        TextureSize);
    if (!EncodeWetWrinklePreviewSurface(
            WorkingSurface,
            TextureSize,
            Pixels,
            FinalDirtyRect))
    {
        return false;
    }

    IncludeWetWrinkleRect(
        PendingTransientProceduralUploadRect,
        bHasPendingTransientProceduralUpload,
        FinalDirtyRect);

    TransientProceduralPreviewState.PreviousPointUVs.Reset(Stroke.Points.Num());
    for (const FWetProceduralRidgeStrokePoint& Point : Stroke.Points)
    {
        TransientProceduralPreviewState.PreviousPointUVs.Add(Point.PositionUV);
    }
    TransientProceduralPreviewState.PreviousShape = static_cast<uint8>(Stroke.Shape);
    TransientProceduralPreviewState.bPreviousFlipFoldSide = Stroke.bFlipFoldSide;
    TransientProceduralPreviewState.PreviousWidthUV = Stroke.WidthUV;
    TransientProceduralPreviewState.PreviousStrength = Stroke.Strength;
    TransientProceduralPreviewState.PreviousFalloff = Stroke.Falloff;
    TransientProceduralPreviewState.PreviousStartTaper = Stroke.StartTaper;
    TransientProceduralPreviewState.PreviousEndTaper = Stroke.EndTaper;
    TransientProceduralPreviewState.PreviousStartEndpointMode = static_cast<uint8>(Stroke.StartEndpoint.Mode);
    TransientProceduralPreviewState.PreviousEndEndpointMode = static_cast<uint8>(Stroke.EndEndpoint.Mode);
    TransientProceduralPreviewState.PreviousFlareSettings = Stroke.FlareSettings;
    TransientProceduralPreviewState.PreviousNaturalVariation = Stroke.NaturalVariation;
    if (!bTransientProceduralPreviewBound)
    {
        bTransientProceduralPreviewBound = true;
        RefreshWrinklePreviewTransientParameters();
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
        PreviewState.bRebuildPending = false;
        PreviewState.PendingTicket = {};
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
                PreviewState.SourceTexture = SourceTexture;
                PreviewState.bDirty = true;
                ++PreviewState.ContentRevision;
            }
            if (PreviewState.TextureSize != ExpectedTextureSize ||
                PreviewState.WorkingTextureSize != ExpectedWorkingTextureSize)
            {
                PreviewState.TextureSize = ExpectedTextureSize;
                PreviewState.WorkingTextureSize = ExpectedWorkingTextureSize;
                PreviewState.bDirty = true;
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

    if (!WorkerJobScheduler.IsValid() || !TextureWorkspace.IsValid() ||
        PreviewState.MaterialSlotIndex == INDEX_NONE || PreviewState.UVChannelIndex < 0)
    {
        PreviewState.TextureHandle.Reset();
        return false;
    }

    PreviewState.TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState.WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState.TextureSize);
    if (PreviewState.TextureSize.X <= 0 || PreviewState.TextureSize.Y <= 0)
    {
        PreviewState.TextureHandle.Reset();
        return false;
    }

    FWetWrinkleAccumulatedPreviewJobInput Input;
    Input.TextureSize = PreviewState.TextureSize;
    Input.WorkingTextureSize = PreviewState.WorkingTextureSize;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr)
    {
        for (const FWetWrinklePatchPlacement& Stamp : Asset->Authored.WrinkleData.EditablePatches)
        {
            if (!Stamp.bEnabled)
            {
                continue;
            }

            if (Stamp.MaterialSlotIndex != PreviewState.MaterialSlotIndex)
            {
                continue;
            }

            FDWCEditorNormalStampCommand Command;
            if (!BuildWetWrinkleNormalStampCommand(Stamp, Command))
            {
                continue;
            }
            Input.Patches.Add(MoveTemp(Command));
        }

        for (const FWetProceduralRidgeStroke& Stroke : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != PreviewState.MaterialSlotIndex ||
                Stroke.StrokeGuid == EditingProceduralStrokeGuid)
            {
                continue;
            }

            Input.RidgeStrokes.Add(Stroke);
        }
    }

    const int32 MaterialSlotIndex = PreviewState.MaterialSlotIndex;
    const int32 UVChannelIndex = PreviewState.UVChannelIndex;
    const uint64 SnapshotContentRevision = PreviewState.ContentRevision;
    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview;
    Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Descriptor.DomainRevision = WorkerJobScheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.EstimatedBytes = EstimateWetWrinklePreviewWorkerPeakBytes(Input);
    Descriptor.DebugName = FString::Printf(TEXT("Wrinkle preview slot %d"), MaterialSlotIndex);

    TWeakPtr<SWetWrinkleViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->Submit(
        Descriptor,
        [Input = MoveTemp(Input)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken) mutable
        {
            return FWetWrinkleAccumulatedPreviewWorker::Build(MoveTemp(Input), CancellationToken);
        },
        [WeakThis, MaterialSlotIndex, UVChannelIndex, SnapshotContentRevision](
            const FDWCEditorWorkerJobTicket& Ticket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetWrinkleViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FWetWrinkleAccumulatedPreviewJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid())
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

            if (State->PendingTicket.JobId != Ticket.JobId ||
                State->PendingTicket.Generation != Ticket.Generation)
            {
                return;
            }

            State->bRebuildPending = false;
            State->PendingTicket = {};
            State->PendingContentRevision = 0;
            if (!Result->bSucceeded)
            {
                State->bDirty = true;
                UE_LOG(
                    LogWetWrinklePreviewViewport,
                    Warning,
                    TEXT("Failed to rebuild the accumulated wrinkle preview for slot %d: %s"),
                    MaterialSlotIndex,
                    *Result->Error);
                return;
            }
            if (State->ContentRevision != SnapshotContentRevision)
            {
                // A newer patch or ridge was committed while this snapshot was
                // rasterizing. Do not let the older result overwrite it.
                State->bDirty = true;
                Viewport->RebuildAccumulatedPreviewTexture(*State);
                return;
            }

            const FDWCEditorTextureHandle PublishedTexture = Viewport->TextureWorkspace->PublishNormalBGRA8(
                MakeWrinkleTextureKey(
                    Viewport->WetClothingAsset.Get(),
                    EDWCEditorTexturePurpose::WrinkleAccumulated,
                    MaterialSlotIndex),
                MakeWrinkleNormalDescriptor(State->TextureSize, State->WorkingTextureSize),
                MoveTemp(Result->Pixels),
                MoveTemp(Result->WorkingSurface),
                EDWCEditorTextureUploadPriority::Interactive);
            if (PublishedTexture.IsValid())
            {
                State->TextureSize = Result->TextureSize;
                State->WorkingTextureSize = Result->WorkingTextureSize;
                State->TextureHandle = Viewport->TextureWorkspace->AcquireLease(PublishedTexture);
                State->bDirty = false;
                ++Viewport->AccumulatedPreviewRebuildCount;
                Viewport->RefreshWrinklePreviewAccumulatedParameters();
                Viewport->Invalidate();
            }
            else
            {
                // Keep the last successful texture bound and retry from the next
                // preview update instead of silently marking the state clean.
                State->bDirty = true;
                UE_LOG(
                    LogWetWrinklePreviewViewport,
                    Warning,
                    TEXT("Failed to publish the accumulated wrinkle preview for slot %d; keeping the previous preview and scheduling a retry."),
                    MaterialSlotIndex);
                Viewport->Invalidate();
            }
        },
        &SubmitError,
        [WeakThis, MaterialSlotIndex, UVChannelIndex](
            const FDWCEditorWorkerJobTicket& Ticket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
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
            }
        });
    PreviewState.bRebuildPending = Ticket.IsValid();
    PreviewState.PendingTicket = Ticket;
    PreviewState.PendingContentRevision = Ticket.IsValid() ? SnapshotContentRevision : 0;
    if (!Ticket.IsValid())
    {
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
    Procedural.UsedBytes =
        static_cast<uint64>(TransientProceduralPreviewState.PreviousPointUVs.GetAllocatedSize()) +
        static_cast<uint64>(TransientProceduralStrokeHits.GetAllocatedSize());
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
    OutCounters.Add({TEXT("Wrinkle spatial-cache acquisitions"), HitTriangleBuildCount, 0});
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
    HitTriangleBuildCount = 0;
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

bool SWetWrinkleViewport::ShouldDeferInteractiveNormalRaster(const FIntPoint& WorkingTextureSize) const
{
    return ShouldDeferWetWrinkleInteractiveNormalRaster(WorkingTextureSize);
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
