#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
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
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionProcessor.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"
#include "WetRendering/WetMaterialParameters.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyPreviewViewport"

namespace
{
    constexpr int32 TransparencyViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.
    // Keep normal 4K brush sizes interactive through region raster/upload.
    // Bigger strokes compose once at interaction end instead of snapshotting
    // the complete preview for every pointer sample.
    constexpr int32 TransparencyPreviewAsyncComposeThresholdPixels = 512 * 1024;
    FDWCEditorTextureKey MakeTransparencyTextureKey(
        const UWetClothingAsset* Asset,
        const EDWCEditorTexturePurpose Purpose,
        const int32 MaterialSlotIndex,
        const FGuid& LayerGuid)
    {
        FDWCEditorTextureKey Key;
        Key.Owner = FObjectKey(Asset);
        Key.Purpose = Purpose;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        Key.LayerGuid = LayerGuid;
        return Key;
    }

    FDWCEditorTextureDescriptor MakeTransparencyDescriptor(
        const FIntPoint& Size,
        const TextureAddress Address)
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = Size;
        Descriptor.PixelFormat = PF_B8G8R8A8;
        Descriptor.bSRGB = true;
        Descriptor.CompressionSettings = TC_Default;
        Descriptor.MipGenSettings = TMGS_NoMipmaps;
        Descriptor.Filter = TF_Bilinear;
        Descriptor.AddressX = Address;
        Descriptor.AddressY = Address;
        Descriptor.LODGroup = TEXTUREGROUP_World;
        Descriptor.InitialBGRA8 = FColor::Black;
        return Descriptor;
    }

    const TCHAR* GetTransparencyBrushModeLabel(const EDWCTransparencyBrushMode Mode)
    {
        switch (Mode)
        {
        case EDWCTransparencyBrushMode::Erase:
            return TEXT("Erase");
        case EDWCTransparencyBrushMode::SetValue:
            return TEXT("Set");
        case EDWCTransparencyBrushMode::Smooth:
            return TEXT("Smooth");
        case EDWCTransparencyBrushMode::ResetToAuto:
            return TEXT("Reset");
        case EDWCTransparencyBrushMode::Apply:
        default:
            return TEXT("Apply");
        }
    }

    bool ArePaintSettingsEquivalent(
        const FDWCTransparencyPaintSettings& A,
        const FDWCTransparencyPaintSettings& B)
    {
        return A.Mode == B.Mode &&
            A.RevealColorMode == B.RevealColorMode &&
            FMath::IsNearlyEqual(A.RadiusUV, B.RadiusUV) &&
            FMath::IsNearlyEqual(A.Strength, B.Strength) &&
            FMath::IsNearlyEqual(A.Falloff, B.Falloff) &&
            FMath::IsNearlyEqual(A.Spacing, B.Spacing) &&
            FMath::IsNearlyEqual(A.TargetAlpha, B.TargetAlpha) &&
            A.bEnabled == B.bEnabled &&
            A.bRevealColorPaint == B.bRevealColorPaint &&
            A.RevealColor.Equals(B.RevealColor);
    }

    bool PassesTransparencyPreviewIslandClip(
        const FDWCTransparencyAutoBakeResult& Result,
        const int32 PixelIndex,
        const int32 UVIslandID)
    {
        if (UVIslandID == INDEX_NONE)
        {
            return true;
        }
        return Result.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencyAutoBakeResult::MatchesOuterIslandID(
                Result.OuterIslandIDBuffer[PixelIndex],
                UVIslandID);
    }

    int32 ResolveTransparencyPreviewSampleIslandID(
        const FDWCTransparencyAutoBakeResult& Result,
        const FVector2D& PositionUV,
        const int32 UVIslandID,
        const int32 Width,
        const int32 Height,
        const bool bWrap)
    {
        if (UVIslandID != INDEX_NONE)
        {
            return UVIslandID;
        }
        if (Result.OuterIslandIDBuffer.Num() != Width * Height)
        {
            return INDEX_NONE;
        }

        int32 X = FMath::FloorToInt(PositionUV.X * Width);
        int32 Y = FMath::FloorToInt(PositionUV.Y * Height);
        if (bWrap)
        {
            X = (X % Width + Width) % Width;
            Y = (Y % Height + Height) % Height;
        }
        else if (X < 0 || X >= Width || Y < 0 || Y >= Height)
        {
            return INDEX_NONE;
        }
        else
        {
            X = FMath::Clamp(X, 0, Width - 1);
            Y = FMath::Clamp(Y, 0, Height - 1);
        }
        return FDWCTransparencyAutoBakeResult::DecodeOuterIslandID(
            Result.OuterIslandIDBuffer[Y * Width + X]);
    }

    class FDWCTransparencyPreviewViewportClient : public FEditorViewportClient
    {
      public:
        FDWCTransparencyPreviewViewportClient(
            FAdvancedPreviewScene* InPreviewScene,
            const TSharedRef<SWetClothingTransparencyPreviewViewport>& InViewport,
            FDWCEditorInteractiveToolsHost* InInputToolsHost)
            : FEditorViewportClient(
                  InInputToolsHost != nullptr ? InInputToolsHost->GetModeTools() : nullptr,
                  InPreviewScene,
                  StaticCastSharedRef<SEditorViewport>(InViewport))
            , PreviewScene(InPreviewScene)
            , ViewportWidget(InViewport)
            , InputToolsHost(InInputToolsHost)
        {
            SetViewMode(VMI_Lit);
            SetRealtime(true);
            ViewFOV = 65.0f;
            FOVAngle = 65.0f;
            SetViewLocation(FVector(250.0f, 0.0f, 120.0f));
            SetViewRotation(FRotator(-20.0f, 180.0f, 0.0f));
            UE::DWCEditor::ApplyDWCPreviewCameraSpeedSettings(*this);
            EngineShowFlags.SetGrid(true);
            EngineShowFlags.SetSelectionOutline(true);
            EngineShowFlags.SetCompositeEditorPrimitives(true);
            bSetListenerPosition = false;
            bUsingOrbitCamera = true;
        }

        virtual void Tick(float DeltaSeconds) override
        {
            FEditorViewportClient::Tick(DeltaSeconds);
            if (PreviewScene != nullptr && PreviewScene->GetWorld() != nullptr)
            {
                PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
            }
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                Pinned->ProcessInteractivePaintWork();
                Pinned->FlushPendingPreviewTextureUpdates();
            }
        }

        virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
        {
            if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed &&
                InputToolsHost != nullptr && InputToolsHost->CancelActiveInteraction())
            {
                return true;
            }
            return FEditorViewportClient::InputKey(EventArgs);
        }

        void FocusOnMesh(const USkeletalMeshComponent* MeshComponent, bool bInstant)
        {
            if (MeshComponent == nullptr || MeshComponent->GetSkeletalMeshAsset() == nullptr)
            {
                return;
            }

            const FBoxSphereBounds Bounds = MeshComponent->CalcBounds(MeshComponent->GetComponentTransform());
            float Radius = FMath::Max3(
                static_cast<float>(Bounds.BoxExtent.X),
                static_cast<float>(Bounds.BoxExtent.Y),
                static_cast<float>(Bounds.BoxExtent.Z));
            Radius = FMath::Max(Radius, static_cast<float>(Bounds.SphereRadius));
            Radius = FMath::Max(Radius, MinimumFocusRadius);

            float AspectToUse = AspectRatio;
            if (Viewport != nullptr)
            {
                const FIntPoint ViewportSize = Viewport->GetSizeXY();
                if (ViewportSize.X > 0 && ViewportSize.Y > 0)
                {
                    AspectToUse = Viewport->GetDesiredAspectRatio();
                }
            }

            if (AspectToUse > 1.0f)
            {
                Radius *= AspectToUse;
            }

            const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Max(ViewFOV, 5.0f) * 0.5f);
            const float DistanceToCamera = (Radius / FMath::Tan(HalfFOVRadians)) * 1.15f;
            ToggleOrbitCamera(true);
            SetViewLocationForOrbiting(Bounds.Origin, DistanceToCamera);
            Invalidate();
        }

      private:
        FAdvancedPreviewScene* PreviewScene = nullptr;
        TWeakPtr<SWetClothingTransparencyPreviewViewport> ViewportWidget;
        FDWCEditorInteractiveToolsHost* InputToolsHost = nullptr;
    };

    FVector AnyPerpendicular(const FVector& Normal)
    {
        FVector Result = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
        if (Result.IsNearlyZero())
        {
            Result = FVector::CrossProduct(Normal, FVector::RightVector).GetSafeNormal();
        }
        return Result.IsNearlyZero() ? FVector::ForwardVector : Result;
    }

}

void SWetClothingTransparencyPreviewViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    SessionStore = InArgs._SessionStore;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    InputToolsHost = MakeUnique<FDWCEditorInteractiveToolsHost>(PreviewScene.Get(), this);
    InitializePreviewSession();
    SEditorViewport::Construct(SEditorViewport::FArguments());
    RefreshPreview();
}

SWetClothingTransparencyPreviewViewport::~SWetClothingTransparencyPreviewViewport()
{
    if (InputToolsHost)
    {
        InputToolsHost->Shutdown();
    }
    if (WorkerJobScheduler.IsValid() && AutoBakePreviewResult.IsValid())
    {
        FDWCEditorWorkerJobKey Key;
        Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
        Key.MaterialSlotIndex = AutoBakePreviewResult->MaterialSlotIndex;
        Key.LayerGuid = AutoBakePreviewResult->LayerGuid;
        WorkerJobScheduler->Cancel(Key);
    }
    WorkerJobScheduler.Reset();
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelActiveInteraction(false);
    }
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->Shutdown();
        PreviewOrchestrator.Reset();
    }
    if (PreviewSession)
    {
        PreviewSession->Shutdown();
    }
    AutoBakePreviewResult.Reset();
    WrinkleSuppressionBuffer.Reset();
    TransparencyPreviewHandle.Reset();
    WrinkleSuppressionPreviewHandle.Reset();
    ClearPreview();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
    InputToolsHost.Reset();
}

void SWetClothingTransparencyPreviewViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(TargetMeshPreviewComponent);
    Collector.AddReferencedObject(PreviewActor);
    Collector.AddReferencedObjects(PreviewMeshComponents);
    Collector.AddReferencedObject(CachedWrinkleSuppressionMaskTexture);
    Collector.AddReferencedObject(BrushCursorComponent);
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetTransparencyPreviewTexture() const
{
    return TransparencyPreviewHandle.IsValid()
        ? TransparencyPreviewHandle->GetTexture()
        : nullptr;
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetWrinkleSuppressionPreviewTexture() const
{
    return WrinkleSuppressionPreviewHandle.IsValid()
        ? WrinkleSuppressionPreviewHandle->GetTexture()
        : nullptr;
}

void SWetClothingTransparencyPreviewViewport::RefreshPreview()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RefreshPreview);
    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    CancelAuthoringLiveStroke();
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelActiveInteraction(true);
    }
    ++PreviewRefreshCount;
    ClearPreview();

    if (PreviewSession)
    {
        PreviewSession->RefreshSlotStates();
        PreviewSession->SetSelectedMaterialSlot(
            PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint
                ? FDWCEditorPreviewSession::AllWettableSlots
                : SelectedMaterialSlotIndex);
    }

    if (PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint)
    {
        BuildFullBlueprintPreview();
    }
    else
    {
        BuildTargetMeshPreview();
    }

    FocusOnPreviewMesh(true);
    Invalidate();
}

void SWetClothingTransparencyPreviewViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (FDWCTransparencyPreviewViewportClient* PreviewClient = static_cast<FDWCTransparencyPreviewViewportClient*>(ViewportClient.Get()))
    {
        PreviewClient->FocusOnMesh(FindFocusMeshComponent(), bInstant);
    }
}

void SWetClothingTransparencyPreviewViewport::SetPreviewMode(const EWetClothingTransparencyPreviewMode NewMode)
{
    // Painting relies on the single target mesh's Data UV hit cache. Do not
    // allow a restored Full Blueprint preview mode to silently disable the
    // active Stage 2/3 input tool.
    const bool bHasActivePaintTarget = bTransparencyPaintingEnabled || bRevealColorPaintingEnabled;
    const EWetClothingTransparencyPreviewMode EffectiveMode = bHasActivePaintTarget
        ? EWetClothingTransparencyPreviewMode::TargetMeshOnly
        : NewMode;
    if (PreviewMode == EffectiveMode)
    {
        return;
    }

    PreviewMode = EffectiveMode;
    if (PreviewSession)
    {
        PreviewSession->SetSelectedMaterialSlot(
            PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint
                ? FDWCEditorPreviewSession::AllWettableSlots
                : SelectedMaterialSlotIndex);
    }
    RefreshPreview();
}

void SWetClothingTransparencyPreviewViewport::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
{
    if (bPreviewSuspended)
    {
        return;
    }

    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        Controller->CancelActiveInteraction(false);
    }
    ClearSurfaceHover();

    if (WorkerJobScheduler.IsValid() && AutoBakePreviewResult.IsValid())
    {
        FDWCEditorWorkerJobKey Key;
        Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
        Key.MaterialSlotIndex = AutoBakePreviewResult->MaterialSlotIndex;
        Key.LayerGuid = AutoBakePreviewResult->LayerGuid;
        WorkerJobScheduler->Cancel(Key);
    }
    PendingPreviewTicket = {};
    PendingPreviewContentRevision = 0;
    bDeferredBrushPreviewRebuild = false;

    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->ClearAllLiveLayers();
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Discard(TransparencyPreviewHandle);
        TextureWorkspace->Discard(WrinkleSuppressionPreviewHandle);
    }
    TransparencyPreviewHandle.Reset();
    WrinkleSuppressionPreviewHandle.Reset();
    if (PreviewSession)
    {
        PreviewSession->Suspend(Reason);
    }

    bPreviewSuspended = true;
    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        ApplyPreviewMaterials(MeshComponent);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ResumePreviewIfNeeded()
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

    RefreshPreview();
    if (AutoBakePreviewResult.IsValid())
    {
        RefreshDeferredFinalPreviewBuffers();
        RebuildTransparencyPreviewTexture();
    }
    ApplyTransparencyPreviewParameters();
}

void SWetClothingTransparencyPreviewViewport::SetWetnessPreviewPercent(const float InPercent)
{
    const float NewPercent = FMath::Clamp(InPercent, 0.0f, 100.0f);
    if (FMath::IsNearlyEqual(WetnessPreviewPercent, NewPercent))
    {
        return;
    }

    WetnessPreviewPercent = NewPercent;
    ApplyWetnessPreview();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetTransparencyEditContext(
    const FGuid& InLayerGuid,
    int32 InMaterialSlotIndex,
    int32 InUVChannelIndex,
    EDWCTransparencyUVAddressMode InAddressMode,
    EDWCTransparencyPaintTarget InPaintTarget)
{
    const int32 PreviousMaterialSlotIndex = SelectedMaterialSlotIndex;
    const bool bMaterialSlotChanged = SelectedMaterialSlotIndex != InMaterialSlotIndex;
    const bool bUVChannelChanged = SelectedUVChannelIndex != InUVChannelIndex;
    const bool bLayerChanged = SelectedLayerGuid != InLayerGuid;
    const bool bAddressModeChanged = SelectedUVAddressMode != InAddressMode;
    const bool bNewAlphaPaintingEnabled = InPaintTarget == EDWCTransparencyPaintTarget::FinalAlpha;
    const bool bNewRevealColorPaintingEnabled = InPaintTarget == EDWCTransparencyPaintTarget::RevealColor;
    const bool bPaintTargetChanged =
        bTransparencyPaintingEnabled != bNewAlphaPaintingEnabled ||
        bRevealColorPaintingEnabled != bNewRevealColorPaintingEnabled;
    const bool bTopologyChanged = bMaterialSlotChanged || bUVChannelChanged;
    const bool bContextChanged = bTopologyChanged || bLayerChanged || bAddressModeChanged || bPaintTargetChanged;
    if (!bContextChanged)
    {
        return;
    }

    InvalidatePreviewContent();

    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }

    SelectedLayerGuid = InLayerGuid;
    SelectedMaterialSlotIndex = InMaterialSlotIndex;
    SelectedUVChannelIndex = InUVChannelIndex;
    SelectedUVAddressMode = InAddressMode;
    bTransparencyPaintingEnabled = bNewAlphaPaintingEnabled;
    bRevealColorPaintingEnabled = bNewRevealColorPaintingEnabled;
    const bool bForcedTargetMeshPreview =
        (bTransparencyPaintingEnabled || bRevealColorPaintingEnabled) &&
        PreviewMode != EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    if (bForcedTargetMeshPreview)
    {
        SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    }
    if (bMaterialSlotChanged && PreviewOrchestrator && PreviousMaterialSlotIndex != INDEX_NONE)
    {
        PreviewOrchestrator->ClearLiveLayers(PreviousMaterialSlotIndex);
    }
    if (PreviewSession)
    {
        PreviewSession->SetSelectedMaterialSlot(
            PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint
                ? FDWCEditorPreviewSession::AllWettableSlots
                : SelectedMaterialSlotIndex);
    }
    if (bMaterialSlotChanged || bUVChannelChanged)
    {
        InvalidateWrinkleSuppressionSourceCache();
        if (PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint && PreviewActor != nullptr)
        {
            RefreshExistingFullBlueprintPreviewMaterials();
            RebuildHitTriangles();
            CurrentSurfaceHit = FDWCTransparencySurfaceHit();
            ClearBrushCursor();
            InvalidatePreviewViewport();
            return;
        }
        if (!bForcedTargetMeshPreview)
        {
            RefreshPreview();
        }
        return;
    }
    if (bAddressModeChanged && TransparencyPreviewHandle.IsValid() && TextureWorkspace.IsValid())
    {
        const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
        TextureWorkspace->RecreateWithAddressMode(TransparencyPreviewHandle, Address, Address);
        TextureWorkspace->RecreateWithAddressMode(WrinkleSuppressionPreviewHandle, Address, Address);
    }
    if (bPaintTargetChanged || bLayerChanged)
    {
        // A stroke is scoped to one layer and paint target. Clear the old
        // hover state before exposing the new target so a stale capture or
        // cursor cannot survive a Stage 2/3 transition.
        CurrentSurfaceHit = FDWCTransparencySurfaceHit();
        LastHoverDirtyRect = FIntRect();
        ReleaseSmoothBrushScratch();
    }
    ApplyRevealColorPaintTargetVisibility();
    RefreshHoverPreviewRegion();
    RefreshBrushCursor();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetTransparencyPreviewStrength(const float InStrength)
{
    const float NewStrength = FMath::Max(0.0f, InStrength);
    if (FMath::IsNearlyEqual(TransparencyPreviewStrength, NewStrength))
    {
        return;
    }

    TransparencyPreviewStrength = NewStrength;
    if (!CanUseDynamicFinalPreviewComposition())
    {
        InvalidatePreviewContent();
        RebuildTransparencyPreviewTexture();
    }
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetShowSavedWrinkle(const bool bInShowSavedWrinkle)
{
    if (bShowSavedWrinkle == bInShowSavedWrinkle)
    {
        return;
    }

    bShowSavedWrinkle = bInShowSavedWrinkle;
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->SetShowSavedCrossLayer(bShowSavedWrinkle);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetWrinkleSuppressionStrength(const float InStrength)
{
    const float NewStrength = FMath::Clamp(InStrength, 0.0f, 5.0f);
    if (FMath::IsNearlyEqual(WrinkleSuppressionStrength, NewStrength))
    {
        return;
    }

    WrinkleSuppressionStrength = NewStrength;
    if (!CanUseDynamicFinalPreviewComposition())
    {
        InvalidatePreviewContent();
        RebuildTransparencyPreviewTexture();
    }
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshWrinkleSuppressionPreview()
{
    bWrinkleSuppressionPreviewDirty = true;
    if (!UsesWrinkleSuppressionPreview())
    {
        return;
    }

    RefreshDeferredFinalPreviewBuffers();
    if (!CanUseDynamicFinalPreviewComposition())
    {
        RebuildTransparencyPreviewTexture();
    }
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshOuterEdgeFeatherPreview()
{
    bOuterEdgeFeatherPreviewDirty = true;
    if (!UsesFinalAlphaPreview())
    {
        return;
    }

    RefreshDeferredFinalPreviewBuffers();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetPaintSettings(const FDWCTransparencyPaintSettings& InSettings)
{
    FDWCTransparencyPaintSettings NewSettings = InSettings;
    NewSettings.RadiusUV = FMath::Clamp(NewSettings.RadiusUV, 0.0001f, 0.5f);
    NewSettings.Strength = FMath::Clamp(NewSettings.Strength, 0.0f, 1.0f);
    NewSettings.Falloff = FMath::Clamp(NewSettings.Falloff, 0.0f, 1.0f);
    NewSettings.Spacing = FMath::Clamp(NewSettings.Spacing, 0.01f, 2.0f);
    NewSettings.TargetAlpha = FMath::Clamp(NewSettings.TargetAlpha, 0.0f, 1.0f);
    // Session refreshes push both persistent setting groups. Only the group
    // selected by the edit context may control the live tool; otherwise a
    // disabled Stage 2 reveal setting can overwrite the Stage 3 alpha brush.
    const bool bTargetsRevealColor = NewSettings.bRevealColorPaint;
    if ((bTargetsRevealColor && !bRevealColorPaintingEnabled) ||
        (!bTargetsRevealColor && !bTransparencyPaintingEnabled))
    {
        return;
    }
    if (ArePaintSettingsEquivalent(PaintSettings, NewSettings))
    {
        return;
    }

    const bool bWasSmooth = PaintSettings.Mode == EDWCTransparencyBrushMode::Smooth;
    PaintSettings = NewSettings;
    if (bWasSmooth && PaintSettings.Mode != EDWCTransparencyBrushMode::Smooth && !IsAuthoringInteractionActive())
    {
        ReleaseSmoothBrushScratch();
    }
    RefreshHoverPreviewRegion();
    RefreshBrushCursor();
}

void SWetClothingTransparencyPreviewViewport::SetVisualizationMode(const EDWCTransparencyVisualizationMode InMode)
{
    if (VisualizationMode == InMode)
    {
        return;
    }

    VisualizationMode = InMode;
    InvalidatePreviewContent();
    RefreshDeferredFinalPreviewBuffers();
    RebuildTransparencyPreviewTexture();
    RefreshBrushCursor();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetAutoBakePreviewResult(
    TSharedPtr<const FDWCTransparencyAutoBakeResult> InResult)
{
    if (AutoBakePreviewResult == InResult && GetTransparencyPreviewTexture() != nullptr)
    {
        return;
    }

    AutoBakePreviewResult = MoveTemp(InResult);
    InvalidatePreviewContent();
    RevealColorBuffer.Reset();
    bRevealColorRequiresWorkerRebuild = false;
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        bRevealColorRequiresWorkerRebuild = Layer->RevealColorPaintStrokes.ContainsByPredicate(
            [this](const FDWCTransparencyRevealColorStroke& Stroke)
            {
                return Stroke.bEnabled &&
                    Stroke.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                    !Stroke.Samples.IsEmpty();
            });
    }
    InvalidateWrinkleSuppressionSourceCache();
    bWrinkleSuppressionPreviewDirty = true;
    bOuterEdgeFeatherPreviewDirty = true;
    RefreshDeferredFinalPreviewBuffers();
    RebuildManualOverridesFromStrokes();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    RefreshHoverPreviewRegion();
    RefreshBrushCursor();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ClearAutoBakePreviewResult()
{
    if (!AutoBakePreviewResult.IsValid() &&
        WrinkleSuppressionBuffer.IsEmpty() &&
        OuterEdgeFeatherBuffer.IsEmpty() &&
        ManualPremultipliedBuffer.IsEmpty() &&
        ManualWeightBuffer.IsEmpty() &&
        RevealColorBuffer.IsEmpty() &&
        GetTransparencyPreviewTexture() == nullptr &&
        LastHoverDirtyRect.IsEmpty())
    {
        return;
    }

    if (WorkerJobScheduler.IsValid() && AutoBakePreviewResult.IsValid())
    {
        FDWCEditorWorkerJobKey Key;
        Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
        Key.MaterialSlotIndex = AutoBakePreviewResult->MaterialSlotIndex;
        Key.LayerGuid = AutoBakePreviewResult->LayerGuid;
        WorkerJobScheduler->Cancel(Key);
    }
    if (TextureWorkspace.IsValid() && TransparencyPreviewHandle.IsValid())
    {
        TextureWorkspace->Invalidate(TransparencyPreviewHandle->GetKey());
    }
    if (TextureWorkspace.IsValid() && WrinkleSuppressionPreviewHandle.IsValid())
    {
        TextureWorkspace->Invalidate(WrinkleSuppressionPreviewHandle->GetKey());
    }
    AutoBakePreviewResult.Reset();
    InvalidatePreviewContent();
    PendingPreviewTicket = {};
    PendingPreviewContentRevision = 0;
    WrinkleSuppressionBuffer.Empty();
    InvalidateWrinkleSuppressionSourceCache();
    OuterEdgeFeatherBuffer.Empty();
    bWrinkleSuppressionPreviewDirty = false;
    bOuterEdgeFeatherPreviewDirty = false;
    ManualPremultipliedBuffer.Empty();
    ManualWeightBuffer.Empty();
    RevealColorBuffer.Empty();
    bManualOverridesRequireWorkerRebuild = false;
    bRevealColorRequiresWorkerRebuild = false;
    bDeferredBrushPreviewRebuild = false;
    ReleaseSmoothBrushScratch();
    CurrentSurfaceHit = FDWCTransparencySurfaceHit();
    LastHoverDirtyRect = FIntRect();
    ClearBrushCursor();
    TransparencyPreviewHandle.Reset();
    WrinkleSuppressionPreviewHandle.Reset();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

TSharedRef<FEditorViewportClient> SWetClothingTransparencyPreviewViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FDWCTransparencyPreviewViewportClient>(
        PreviewScene.Get(),
        SharedThis(this),
        InputToolsHost.Get());
    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetClothingTransparencyPreviewViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetClothingTransparencyEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(
            ViewportToolbarName,
            NAME_None,
            EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(
            UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::DWCEditor::CreateDWCViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(
        UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SWetClothingTransparencyPreviewViewport::ClearPreview()
{
    ++PreviewClearCount;
    if (PreviewScene.IsValid())
    {
        if (TargetMeshPreviewComponent != nullptr)
        {
            PreviewScene->RemoveComponent(TargetMeshPreviewComponent);
        }
        if (BrushCursorComponent != nullptr)
        {
            PreviewScene->RemoveComponent(BrushCursorComponent);
        }
        if (PreviewActor != nullptr && PreviewScene->GetWorld() != nullptr)
        {
            PreviewScene->GetWorld()->DestroyActor(PreviewActor);
        }
    }

    TargetMeshPreviewComponent = nullptr;
    PreviewActor = nullptr;
    PreviewMeshComponents.Reset();
    BrushCursorComponent = nullptr;
    CancelAuthoringLiveStroke();
}

void SWetClothingTransparencyPreviewViewport::InitializePreviewSession()
{
    PreviewSession = MakeUnique<FDWCEditorPreviewSession>();

    FDWCEditorPreviewSessionConfig Config;
    Config.DiagnosticLabel = TEXT("Transparency");
    Config.FeatureMask = EDWCEditorPreviewMaterialFeature::Transparency;
    Config.FeatureSchemaVersion = FWetTransparencyPreviewGraphExtension::GraphSchemaVersion;
    Config.SurfaceWaterNormalUVChannelIndex = 0;
    Config.InitialPreviewWetness = FMath::Clamp(WetnessPreviewPercent / 100.0f, 0.0f, 1.0f);
    Config.ExtendGraph = &FWetTransparencyPreviewGraphExtension::ExtendGraph;
    Config.InitializeMID = &FWetTransparencyPreviewGraphExtension::InitializeMID;
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
        EDWCEditorAuthoringDomain::Transparency,
        SessionStore);
    PreviewSession->OnSlotsChanged().AddRaw(
        this,
        &SWetClothingTransparencyPreviewViewport::HandlePreviewSessionSlotsChanged);
    PreviewSession->OnMaterialReady().AddRaw(
        this,
        &SWetClothingTransparencyPreviewViewport::HandlePreviewSessionMaterialReady);
}

void SWetClothingTransparencyPreviewViewport::HandlePreviewSessionSlotsChanged()
{
    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        ApplyPreviewMaterials(MeshComponent);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetAuthoringController(
    const TSharedPtr<FDWCTransparencyAuthoringController>& InController)
{
    if (AuthoringController.Pin() == InController)
    {
        return;
    }
    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }
    AuthoringController = InController;
}

bool SWetClothingTransparencyPreviewViewport::IsAuthoringInteractionActive() const
{
    const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin();
    return Controller.IsValid() && Controller->IsInteracting();
}

void SWetClothingTransparencyPreviewViewport::HandlePreviewSessionMaterialReady(
    const int32 MaterialSlotIndex,
    UMaterialInstanceDynamic* PreviewMID)
{
    if (PreviewMID == nullptr)
    {
        return;
    }

    if (MaterialSlotIndex == SelectedMaterialSlotIndex)
    {
        ApplyTransparencyPreviewParameters();
    }
}

void SWetClothingTransparencyPreviewViewport::BuildTargetMeshPreview()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr || !PreviewScene.IsValid())
    {
        return;
    }

    TargetMeshPreviewComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    TargetMeshPreviewComponent->SetMobility(EComponentMobility::Movable);
    TargetMeshPreviewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TargetMeshPreviewComponent->SetSkeletalMeshAsset(Asset->GetDWCSkeletalMesh());
    PreviewScene->AddComponent(TargetMeshPreviewComponent, FTransform::Identity);
    ConfigurePreviewMeshComponent(TargetMeshPreviewComponent);

    BrushCursorComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    BrushCursorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushCursorComponent->SetCastShadow(false);
    BrushCursorComponent->SetReceivesDecals(false);
    BrushCursorComponent->SetDepthPriorityGroup(SDPG_Foreground);
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);
    EnsureBrushCursor();
    RebuildHitTriangles();
    ApplyRevealColorPaintTargetVisibility();

    const FBoxSphereBounds Bounds = TargetMeshPreviewComponent->CalcBounds(FTransform::Identity);
    PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
}

void SWetClothingTransparencyPreviewViewport::BuildFullBlueprintPreview()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !PreviewScene.IsValid() || PreviewScene->GetWorld() == nullptr)
    {
        return;
    }

    TSubclassOf<AActor> BlueprintClass = Asset->Authored.TransparencyData.SourceBlueprintClass.LoadSynchronous();
    if (BlueprintClass == nullptr)
    {
        BuildTargetMeshPreview();
        return;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.Name = MakeUniqueObjectName(PreviewScene->GetWorld(), BlueprintClass.Get(), TEXT("DWC_TransparencyPreviewActor"));
    SpawnParameters.ObjectFlags = RF_Transient;
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParameters.bTemporaryEditorActor = true;

    PreviewActor = PreviewScene->GetWorld()->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, SpawnParameters);
    if (PreviewActor == nullptr)
    {
        BuildTargetMeshPreview();
        return;
    }

    TArray<USkeletalMeshComponent*> MeshComponents;
    PreviewActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
    for (USkeletalMeshComponent* MeshComponent : MeshComponents)
    {
        if (MeshComponent == nullptr)
        {
            continue;
        }

        MeshComponent->SetForcedLOD(TransparencyViewportForceRenderLOD0);
        if (MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh())
        {
            ConfigurePreviewMeshComponent(MeshComponent);
        }
    }

    for (USkeletalMeshComponent* MeshComponent : MeshComponents)
    {
        PreviewMeshComponents.AddUnique(MeshComponent);
    }

    if (USkeletalMeshComponent* FocusMesh = FindFocusMeshComponent())
    {
        const FBoxSphereBounds Bounds = FocusMesh->CalcBounds(FocusMesh->GetComponentTransform());
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
}

void SWetClothingTransparencyPreviewViewport::ConfigurePreviewMeshComponent(USkeletalMeshComponent* MeshComponent)
{
    if (MeshComponent == nullptr)
    {
        return;
    }

    PreviewMeshComponents.AddUnique(MeshComponent);
    MeshComponent->SetForcedLOD(TransparencyViewportForceRenderLOD0);
    ApplyPreviewMaterials(MeshComponent);
    ApplyWetnessPreview();
    MeshComponent->MarkRenderStateDirty();
}

void SWetClothingTransparencyPreviewViewport::ApplyPreviewMaterials(USkeletalMeshComponent* MeshComponent)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MeshComponent == nullptr || !PreviewSession)
    {
        return;
    }

    const bool bIsTargetMesh = MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh();
    if (!bIsTargetMesh)
    {
        return;
    }

    const bool bPreviewAllReadySlots =
        PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint;
    PreviewSession->SetPreviewMaterialScope(
        bPreviewAllReadySlots
            ? EDWCEditorPreviewMaterialScope::AllWettableSlots
            : EDWCEditorPreviewMaterialScope::SingleSlot,
        SelectedMaterialSlotIndex);
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
        if (SlotState.MaterialSlotIndex < 0 ||
            SlotState.MaterialSlotIndex >= MeshComponent->GetNumMaterials())
        {
            continue;
        }

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
        MeshComponent->SetMaterial(SlotState.MaterialSlotIndex, MaterialToApply);
    }

    if (!PreviewSession->IsSuspended())
    {
        ApplyTransparencyPreviewParameters();
    }
}

void SWetClothingTransparencyPreviewViewport::ApplyRevealColorPaintTargetVisibility()
{
    if (TargetMeshPreviewComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* SkeletalMesh = TargetMeshPreviewComponent->GetSkeletalMeshAsset();
    const FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr
        ? SkeletalMesh->GetResourceForRendering()
        : nullptr;
    constexpr int32 PreviewLODIndex = 0;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(PreviewLODIndex))
    {
        return;
    }

    const bool bShowOnlyTargetPart =
        bRevealColorPaintingEnabled &&
        PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        SelectedMaterialSlotIndex != INDEX_NONE;
    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[PreviewLODIndex];
    for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
        const bool bShowSection = !bShowOnlyTargetPart ||
            Section.MaterialIndex == SelectedMaterialSlotIndex;
        TargetMeshPreviewComponent->ShowMaterialSection(
            Section.MaterialIndex,
            SectionIndex,
            bShowSection,
            PreviewLODIndex);
    }
    TargetMeshPreviewComponent->MarkRenderStateDirty();
}

void SWetClothingTransparencyPreviewViewport::RefreshExistingFullBlueprintPreviewMaterials()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || PreviewMode != EWetClothingTransparencyPreviewMode::FullBlueprint ||
        !PreviewSession)
    {
        return;
    }

    PreviewSession->SetSelectedMaterialSlot(FDWCEditorPreviewSession::AllWettableSlots);
    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        if (MeshComponent != nullptr && MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh())
        {
            ApplyPreviewMaterials(MeshComponent);
            MeshComponent->MarkRenderStateDirty();
        }
    }
}

void SWetClothingTransparencyPreviewViewport::ApplyWetnessPreview()
{
    const float Wetness = FMath::Clamp(WetnessPreviewPercent / 100.0f, 0.0f, 1.0f);
    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->SetPreviewWetness(Wetness);
    }
}

void SWetClothingTransparencyPreviewViewport::ApplyTransparencyPreviewParameters()
{
    if (!PreviewOrchestrator || SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    PreviewOrchestrator->SetLiveLayer(
        SelectedMaterialSlotIndex,
        BuildTransparencyPreviewLayer());
}

FDWCEditorPreviewLayer SWetClothingTransparencyPreviewViewport::BuildTransparencyPreviewLayer()
{
    const FWetClothingTransparencyLayerData* LayerData = GetSelectedLayer();
    const bool bResultMatchesSelection = AutoBakePreviewResult.IsValid() &&
        AutoBakePreviewResult->LayerGuid == SelectedLayerGuid &&
        AutoBakePreviewResult->MaterialSlotIndex == SelectedMaterialSlotIndex &&
        AutoBakePreviewResult->UVChannelIndex == SelectedUVChannelIndex;

    UTexture2D* PreviewTransparencyMap = bResultMatchesSelection
        ? GetTransparencyPreviewTexture()
        : nullptr;
    if (PreviewTransparencyMap == nullptr && LayerData != nullptr)
    {
        if (const FWetClothingBakedTransparencyMap* BakedMap = LayerData->BakedMaps.FindByPredicate(
                [this](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                           Candidate.TransparencyMap != nullptr;
                }))
        {
            PreviewTransparencyMap = BakedMap->TransparencyMap;
        }
    }
    const bool bUseDynamicFinalComposition = CanUseDynamicFinalPreviewComposition();
    const bool bUseSuppression = bUseDynamicFinalComposition &&
        GetWrinkleSuppressionPreviewTexture() != nullptr;

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveTransparency;
    Layer.MaterialSlotIndex = SelectedMaterialSlotIndex;
    Layer.AddTexture(DWCWetMaterialParameters::TransparencyMap(), nullptr);
    Layer.AddScalar(DWCWetMaterialParameters::UseTransparencyMap(), 0.0f);
    Layer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::TransparencyMap(),
        PreviewTransparencyMap);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseTransparencyMap(),
        PreviewTransparencyMap != nullptr ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::TransparencyStrength(),
        bUseDynamicFinalComposition ? TransparencyPreviewStrength : 1.0f);
    Layer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionMap(),
        bUseSuppression ? GetWrinkleSuppressionPreviewTexture() : nullptr);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseWrinkleSuppressionMap(),
        bUseSuppression ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(),
        bUseDynamicFinalComposition ? WrinkleSuppressionStrength : 0.0f);
    return Layer;
}

void SWetClothingTransparencyPreviewViewport::RefreshSavedWrinklePreviewParameters()
{
    if (!PreviewOrchestrator)
    {
        return;
    }
    PreviewOrchestrator->SetShowSavedCrossLayer(bShowSavedWrinkle);
}

FWetClothingTransparencyLayerData* SWetClothingTransparencyPreviewViewport::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
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
        ? (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
           VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha ||
           VisualizationMode == EDWCTransparencyVisualizationMode::InnerColor)
        : (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
           VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha);
    return !bPreviewSuspended && (bCanRevealPaint || bCanAlphaPaint) && PaintSettings.bEnabled &&
        PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        AutoBakePreviewResult.IsValid() && GetTransparencyPreviewTexture() != nullptr &&
        SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelIndex >= 0 &&
        bSupportsCurrentVisualization;
}

bool SWetClothingTransparencyPreviewViewport::CanShowBrushCursor() const
{
    const bool bHasActivePaintTarget = bTransparencyPaintingEnabled || bRevealColorPaintingEnabled;
    return !bPreviewSuspended && bHasActivePaintTarget &&
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
    RefreshHoverPreviewRegion();
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

    if (ShouldDeferBrushRaster(Sample) && Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
    {
        // The controller keeps the authoritative stroke transient until
        // mouse-up. Avoid a complete auto-bake snapshot for each pointer
        // sample; the sparse live layer retains the touched tiles instead.
        bManualOverridesRequireWorkerRebuild = true;
        bDeferredBrushPreviewRebuild = true;
        InvalidatePreviewContent();
        return;
    }
    if (ShouldDeferBrushRaster(Sample))
    {
        QueueInteractivePaintWork(Stroke, Sample);
        return;
    }

    FIntRect DirtyRect;
    if (RasterizeBrushSample(Stroke, Sample, &DirtyRect))
    {
        InvalidatePreviewContent();
        QueuePreviewTextureUpdate(
            DirtyRect,
            Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap);
    }
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

    // Reveal paint owns a live color layer while dragging. The committed
    // stroke is replayed once on mouse-up; setting the rebuild flag here used
    // to let unrelated refreshes replace live feedback with stale data.
    if (ShouldDeferBrushRaster(Sample) && Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
    {
        bDeferredBrushPreviewRebuild = true;
        InvalidatePreviewContent();
        return;
    }
    if (ShouldDeferBrushRaster(Sample))
    {
        QueueInteractivePaintWork(Stroke, Sample);
        return;
    }

    FIntRect DirtyRect;
    if (RasterizeRevealColorSample(Stroke, Sample, &DirtyRect))
    {
        InvalidatePreviewContent();
        QueuePreviewTextureUpdate(
            DirtyRect,
            Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap);
    }
}

void SWetClothingTransparencyPreviewViewport::FinishAuthoringPreviewUpdate()
{
    if (!PendingInteractivePaintWork.IsEmpty())
    {
        bAuthoringFinishPending = true;
        return;
    }

    FinalizeAuthoringPreviewUpdate();
}

void SWetClothingTransparencyPreviewViewport::CommitAuthoringPreviewUpdate(
    const EDWCTransparencyPaintTarget PaintTarget)
{
    // Interactive tiles are intentionally provisional. Rebuild once from the
    // committed WCA stroke so undo/redo, slot refresh, and the final preview
    // all converge on exactly the same authored source.
    if (PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        bRevealColorRequiresWorkerRebuild = true;
    }
    else if (PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha)
    {
        bManualOverridesRequireWorkerRebuild = true;
    }
    bAuthoringWorkerRebuildRequested = true;
    ++InteractivePaintAuthoritativeReplayCount;
    FinishAuthoringPreviewUpdate();
}

void SWetClothingTransparencyPreviewViewport::FinalizeAuthoringPreviewUpdate()
{
    ReleaseSmoothBrushScratch();
    if (bDeferredBrushPreviewRebuild || bAuthoringWorkerRebuildRequested)
    {
        bDeferredBrushPreviewRebuild = false;
        bAuthoringWorkerRebuildRequested = false;
        RebuildTransparencyPreviewTexture();
    }
    CancelAuthoringLiveStroke();
    RefreshHoverPreviewRegion();
}

void SWetClothingTransparencyPreviewViewport::CancelAuthoringLiveStroke()
{
    InteractivePaintCanceledRegionCount += GetPendingInteractivePaintRegionCount();
    PendingInteractivePaintWork.Reset();
    bAuthoringFinishPending = false;
    bAuthoringWorkerRebuildRequested = false;
    if (LiveStrokeLayer)
    {
        LiveStrokeLayer->Reset();
    }
}

void SWetClothingTransparencyPreviewViewport::ProcessInteractivePaintWork()
{
    if (bPreviewSuspended || PendingInteractivePaintWork.IsEmpty())
    {
        return;
    }

    // 64px tiles keep 4K brushes bounded, but eight tiles per frame left the
    // cursor several samples behind a normal drag. Spend a small, explicit
    // frame budget on the newest live authoring work instead.
    constexpr int32 MaxRegionsPerTick = 32;
    constexpr double MaxTickSeconds = 0.004;
    const double StartTime = FPlatformTime::Seconds();
    int32 ProcessedRegions = 0;
    while (!PendingInteractivePaintWork.IsEmpty() && ProcessedRegions < MaxRegionsPerTick &&
           FPlatformTime::Seconds() - StartTime < MaxTickSeconds)
    {
        FInteractivePaintWork& Work = PendingInteractivePaintWork[0];
        if (!Work.Regions.IsValidIndex(Work.NextRegionIndex))
        {
            PendingInteractivePaintWork.RemoveAt(0, 1, EAllowShrinking::No);
            continue;
        }

        const FIntRect& Region = Work.Regions[Work.NextRegionIndex++];
        FIntRect DirtyRect;
        bool bChanged = false;
        bool bWrap = false;
        if (Work.AlphaStroke.IsSet())
        {
            bWrap = Work.AlphaStroke->UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
            bChanged = RasterizeBrushSample(Work.AlphaStroke.GetValue(), Work.Sample, &DirtyRect, &Region);
        }
        else if (Work.RevealStroke.IsSet())
        {
            bWrap = Work.RevealStroke->UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
            bChanged = RasterizeRevealColorSample(Work.RevealStroke.GetValue(), Work.Sample, &DirtyRect, &Region);
        }

        if (bChanged)
        {
            InvalidatePreviewContent();
            QueuePreviewTextureUpdate(DirtyRect, bWrap, false);
        }
        ++ProcessedRegions;
        ++InteractivePaintProcessedRegionCount;
    }

    if (PendingInteractivePaintWork.IsEmpty() && bAuthoringFinishPending)
    {
        bAuthoringFinishPending = false;
        FinalizeAuthoringPreviewUpdate();
    }
}

void SWetClothingTransparencyPreviewViewport::QueueInteractivePaintWork(
    const FDWCTransparencyBrushStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    FInteractivePaintWork& Work = PendingInteractivePaintWork.AddDefaulted_GetRef();
    Work.AlphaStroke = Stroke;
    // The live layer owns the accumulated samples. Tile work only needs the
    // brush descriptor plus the one sample being rasterized.
    Work.AlphaStroke->Samples.Reset();
    Work.Sample = Sample;
    Work.Regions = BuildInteractivePaintRegions(Sample, Stroke.UVAddressMode);
    InteractivePaintQueuedRegionCount += Work.Regions.Num();
    InteractivePaintPeakQueuedRegionCount = FMath::Max(
        InteractivePaintPeakQueuedRegionCount,
        GetPendingInteractivePaintRegionCount());
}

void SWetClothingTransparencyPreviewViewport::QueueInteractivePaintWork(
    const FDWCTransparencyRevealColorStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample)
{
    FInteractivePaintWork& Work = PendingInteractivePaintWork.AddDefaulted_GetRef();
    Work.RevealStroke = Stroke;
    // See the alpha path above. Do not duplicate the complete live stroke for
    // every queued tile batch.
    Work.RevealStroke->Samples.Reset();
    Work.Sample = Sample;
    Work.Regions = BuildInteractivePaintRegions(Sample, Stroke.UVAddressMode);
    InteractivePaintQueuedRegionCount += Work.Regions.Num();
    InteractivePaintPeakQueuedRegionCount = FMath::Max(
        InteractivePaintPeakQueuedRegionCount,
        GetPendingInteractivePaintRegionCount());
}

TArray<FIntRect> SWetClothingTransparencyPreviewViewport::BuildInteractivePaintRegions(
    const FDWCTransparencyBrushSample& Sample,
    const EDWCTransparencyUVAddressMode AddressMode) const
{
    TArray<FIntRect> Regions;
    if (!AutoBakePreviewResult.IsValid())
    {
        return Regions;
    }

    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Resolution.X, 1.0f);
    const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Resolution.Y, 1.0f);
    const FVector2D Center(Sample.PositionUV.X * Resolution.X, Sample.PositionUV.Y * Resolution.Y);
    FDWCEditorDirtyRegionSet DirtyRegionSet;
    DirtyRegionSet.Add(
        FIntRect(
            FMath::FloorToInt(Center.X - RadiusPixelsX - 1.0f),
            FMath::FloorToInt(Center.Y - RadiusPixelsY - 1.0f),
            FMath::CeilToInt(Center.X + RadiusPixelsX + 2.0f),
            FMath::CeilToInt(Center.Y + RadiusPixelsY + 2.0f)),
        Resolution,
        AddressMode == EDWCTransparencyUVAddressMode::Wrap);

    for (const FIntRect& DirtyRegion : DirtyRegionSet.GetRegions())
    {
        for (int32 Y = DirtyRegion.Min.Y; Y < DirtyRegion.Max.Y; Y += FDWCTransparencyLiveStrokeLayer::TileSize)
        {
            for (int32 X = DirtyRegion.Min.X; X < DirtyRegion.Max.X; X += FDWCTransparencyLiveStrokeLayer::TileSize)
            {
                Regions.Add(FIntRect(
                    X,
                    Y,
                    FMath::Min(X + FDWCTransparencyLiveStrokeLayer::TileSize, DirtyRegion.Max.X),
                    FMath::Min(Y + FDWCTransparencyLiveStrokeLayer::TileSize, DirtyRegion.Max.Y)));
            }
        }
    }
    return Regions;
}

uint64 SWetClothingTransparencyPreviewViewport::GetPendingInteractivePaintRegionCount() const
{
    uint64 RegionCount = 0;
    for (const FInteractivePaintWork& Work : PendingInteractivePaintWork)
    {
        RegionCount += static_cast<uint64>(FMath::Max(Work.Regions.Num() - Work.NextRegionIndex, 0));
    }
    return RegionCount;
}

uint64 SWetClothingTransparencyPreviewViewport::GetInteractivePaintWorkAllocatedBytes() const
{
    uint64 Bytes = PendingInteractivePaintWork.GetAllocatedSize();
    for (const FInteractivePaintWork& Work : PendingInteractivePaintWork)
    {
        Bytes += Work.Regions.GetAllocatedSize();
        if (Work.AlphaStroke.IsSet())
        {
            Bytes += Work.AlphaStroke->Samples.GetAllocatedSize();
        }
        if (Work.RevealStroke.IsSet())
        {
            Bytes += Work.RevealStroke->Samples.GetAllocatedSize();
        }
    }
    return Bytes;
}

bool SWetClothingTransparencyPreviewViewport::RasterizeBrushSample(
    const FDWCTransparencyBrushStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample,
    FIntRect* OutDirtyRect,
    const FIntRect* ClipRect)
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0 || !EnsureManualOverrideBuffers())
    {
        return false;
    }

    const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
    const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
    const FVector2D CenterPixels(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
    int32 MinX = FMath::FloorToInt(CenterPixels.X - RadiusPixelsX - 1.0f);
    int32 MaxX = FMath::CeilToInt(CenterPixels.X + RadiusPixelsX + 1.0f);
    int32 MinY = FMath::FloorToInt(CenterPixels.Y - RadiusPixelsY - 1.0f);
    int32 MaxY = FMath::CeilToInt(CenterPixels.Y + RadiusPixelsY + 1.0f);
    if (ClipRect != nullptr)
    {
        MinX = FMath::Max(MinX, ClipRect->Min.X);
        MaxX = FMath::Min(MaxX, ClipRect->Max.X - 1);
        MinY = FMath::Max(MinY, ClipRect->Min.Y);
        MaxY = FMath::Min(MaxY, ClipRect->Max.Y - 1);
        if (MinX > MaxX || MinY > MaxY)
        {
            return false;
        }
    }
    const int32 ClipUVIslandID = ResolveTransparencyPreviewSampleIslandID(
        *AutoBakePreviewResult,
        Sample.PositionUV,
        Sample.UVIslandID,
        Width,
        Height,
        bWrap);
    auto WrapIndex = [](int32 Value, int32 Size)
    {
        return (Value % Size + Size) % Size;
    };

    const bool bSmooth = Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth;
    const int32 SnapshotMinX = MinX - 1;
    const int32 SnapshotMinY = MinY - 1;
    const int32 SnapshotWidth = MaxX - MinX + 3;
    const int32 SnapshotHeight = MaxY - MinY + 3;
    TArray<uint8>* PreviousPremultiplied = nullptr;
    TArray<uint8>* PreviousWeight = nullptr;
    if (bSmooth)
    {
        const int32 SnapshotPixelCount = SnapshotWidth * SnapshotHeight;
        SmoothBrushPremultipliedScratch.SetNumUninitialized(SnapshotPixelCount);
        SmoothBrushWeightScratch.SetNumUninitialized(SnapshotPixelCount);
        PreviousPremultiplied = &SmoothBrushPremultipliedScratch;
        PreviousWeight = &SmoothBrushWeightScratch;
        for (int32 SnapshotY = 0; SnapshotY < SnapshotHeight; ++SnapshotY)
        {
            for (int32 SnapshotX = 0; SnapshotX < SnapshotWidth; ++SnapshotX)
            {
                int32 SourceX = SnapshotMinX + SnapshotX;
                int32 SourceY = SnapshotMinY + SnapshotY;
                if (bWrap)
                {
                    SourceX = WrapIndex(SourceX, Width);
                    SourceY = WrapIndex(SourceY, Height);
                }
                else
                {
                    SourceX = FMath::Clamp(SourceX, 0, Width - 1);
                    SourceY = FMath::Clamp(SourceY, 0, Height - 1);
                }
                const int32 SourceIndex = SourceY * Width + SourceX;
                const int32 SnapshotIndex = SnapshotY * SnapshotWidth + SnapshotX;
                (*PreviousPremultiplied)[SnapshotIndex] = ManualPremultipliedBuffer[SourceIndex];
                (*PreviousWeight)[SnapshotIndex] = ManualWeightBuffer[SourceIndex];
            }
        }
    }

    auto EditedAlphaAt = [this, Width, Height, bWrap, SnapshotMinX, SnapshotMinY, SnapshotWidth,
                          PreviousPremultiplied, PreviousWeight, &WrapIndex](int32 UnwrappedX, int32 UnwrappedY)
    {
        int32 X = UnwrappedX;
        int32 Y = UnwrappedY;
        if (bWrap)
        {
            X = WrapIndex(X, Width);
            Y = WrapIndex(Y, Height);
        }
        else
        {
            X = FMath::Clamp(X, 0, Width - 1);
            Y = FMath::Clamp(Y, 0, Height - 1);
        }
        const int32 Index = Y * Width + X;
        const int32 SnapshotIndex = (UnwrappedY - SnapshotMinY) * SnapshotWidth + (UnwrappedX - SnapshotMinX);
        const bool bHasSnapshotPixel =
            PreviousPremultiplied != nullptr &&
            PreviousWeight != nullptr &&
            PreviousPremultiplied->IsValidIndex(SnapshotIndex) &&
            PreviousWeight->IsValidIndex(SnapshotIndex);
        const float ManualWeight =
            (bHasSnapshotPixel ? (*PreviousWeight)[SnapshotIndex] : ManualWeightBuffer[Index]) / 255.0f;
        const float ManualPremultiplied =
            (bHasSnapshotPixel ? (*PreviousPremultiplied)[SnapshotIndex] : ManualPremultipliedBuffer[Index]) / 255.0f;
        const float AutoAlpha = AutoBakePreviewResult->AutoAlphaBuffer.IsValidIndex(Index)
            ? AutoBakePreviewResult->AutoAlphaBuffer[Index] / 255.0f : 0.0f;
        return AutoAlpha * (1.0f - ManualWeight) + ManualPremultiplied;
    };

    bool bChanged = false;
    for (int32 UnwrappedY = MinY; UnwrappedY <= MaxY; ++UnwrappedY)
    {
        for (int32 UnwrappedX = MinX; UnwrappedX <= MaxX; ++UnwrappedX)
        {
            if (!bWrap && (UnwrappedX < 0 || UnwrappedX >= Width || UnwrappedY < 0 || UnwrappedY >= Height))
            {
                continue;
            }
            const float DX = (UnwrappedX + 0.5f - CenterPixels.X) / RadiusPixelsX;
            const float DY = (UnwrappedY + 0.5f - CenterPixels.Y) / RadiusPixelsY;
            const float Distance = FMath::Sqrt(DX * DX + DY * DY);
            if (Distance > 1.0f)
            {
                continue;
            }
            const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
            const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                ? 1.0f
                : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
            const float BrushWeight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
            if (BrushWeight <= 0.0f)
            {
                continue;
            }

            const int32 X = bWrap ? WrapIndex(UnwrappedX, Width) : UnwrappedX;
            const int32 Y = bWrap ? WrapIndex(UnwrappedY, Height) : UnwrappedY;
            const int32 PixelIndex = Y * Width + X;
            if (!PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, PixelIndex, ClipUVIslandID))
            {
                continue;
            }

            const float OldPremultiplied = ManualPremultipliedBuffer[PixelIndex] / 255.0f;
            const float OldWeight = ManualWeightBuffer[PixelIndex] / 255.0f;
            float NewPremultiplied = OldPremultiplied;
            float NewWeight = OldWeight;
            if (Stroke.BrushMode == EDWCTransparencyBrushMode::ResetToAuto)
            {
                NewPremultiplied *= 1.0f - BrushWeight;
                NewWeight *= 1.0f - BrushWeight;
            }
            else
            {
                float Target = Stroke.TargetAlpha;
                if (Stroke.BrushMode == EDWCTransparencyBrushMode::Apply)
                {
                    Target = 1.0f;
                }
                else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Erase)
                {
                    Target = 0.0f;
                }
                else if (Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth)
                {
                    Target = 0.0f;
                    int32 SmoothSampleCount = 0;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            int32 NeighborX = UnwrappedX + OffsetX;
                            int32 NeighborY = UnwrappedY + OffsetY;
                            if (bWrap)
                            {
                                NeighborX = WrapIndex(NeighborX, Width);
                                NeighborY = WrapIndex(NeighborY, Height);
                            }
                            else
                            {
                                NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                                NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                            }
                            const int32 NeighborIndex = NeighborY * Width + NeighborX;
                            if (PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, NeighborIndex, ClipUVIslandID))
                            {
                                Target += EditedAlphaAt(UnwrappedX + OffsetX, UnwrappedY + OffsetY);
                                ++SmoothSampleCount;
                            }
                        }
                    }
                    Target = SmoothSampleCount > 0
                        ? Target / static_cast<float>(SmoothSampleCount)
                        : GetStoredEditedAlpha(PixelIndex);
                }
                NewPremultiplied = Target * BrushWeight + OldPremultiplied * (1.0f - BrushWeight);
                NewWeight = BrushWeight + OldWeight * (1.0f - BrushWeight);
            }
            ManualPremultipliedBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewPremultiplied, 0.0f, 1.0f) * 255.0f));
            ManualWeightBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(NewWeight, 0.0f, 1.0f) * 255.0f));
            bChanged = true;
        }
    }

    if (OutDirtyRect != nullptr)
    {
        *OutDirtyRect = FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
    }
    return bChanged;
}

bool SWetClothingTransparencyPreviewViewport::RasterizeRevealColorSample(
    const FDWCTransparencyRevealColorStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample,
    FIntRect* OutDirtyRect,
    const FIntRect* ClipRect)
{
    if (!AutoBakePreviewResult.IsValid() || !EnsureRevealColorBuffer()) return false;
    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0 || RevealColorBuffer.Num() != Width * Height) return false;
    const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const auto WrapIndex = [](int32 Value, int32 Size) { return (Value % Size + Size) % Size; };
    const float RadiusX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
    const float RadiusY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
    const FVector2D Center(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
    int32 MinX = FMath::FloorToInt(Center.X - RadiusX - 1.0f);
    int32 MaxX = FMath::CeilToInt(Center.X + RadiusX + 1.0f);
    int32 MinY = FMath::FloorToInt(Center.Y - RadiusY - 1.0f);
    int32 MaxY = FMath::CeilToInt(Center.Y + RadiusY + 1.0f);
    if (ClipRect != nullptr)
    {
        MinX = FMath::Max(MinX, ClipRect->Min.X);
        MaxX = FMath::Min(MaxX, ClipRect->Max.X - 1);
        MinY = FMath::Max(MinY, ClipRect->Min.Y);
        MaxY = FMath::Min(MaxY, ClipRect->Max.Y - 1);
        if (MinX > MaxX || MinY > MaxY) return false;
    }
    const int32 ClipUVIslandID = ResolveTransparencyPreviewSampleIslandID(
        *AutoBakePreviewResult,
        Sample.PositionUV,
        Sample.UVIslandID,
        Width,
        Height,
        bWrap);
    const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FLinearColor BaseColor = Layer != nullptr
        ? Layer->ManualColorSource.BaseRevealColor.CopyWithNewOpacity(1.0f)
        : FLinearColor::White;
    const FLinearColor PaintColor = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
    const bool bSmooth = Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth;
    const int32 SnapshotMinX = MinX - 1;
    const int32 SnapshotMinY = MinY - 1;
    const int32 SnapshotWidth = MaxX - MinX + 3;
    const int32 SnapshotHeight = MaxY - MinY + 3;
    TArray<FColor>& SmoothSnapshot = SmoothRevealColorScratch;
    if (bSmooth)
    {
        SmoothSnapshot.SetNumUninitialized(SnapshotWidth * SnapshotHeight);
        for (int32 SnapshotY = 0; SnapshotY < SnapshotHeight; ++SnapshotY)
        {
            for (int32 SnapshotX = 0; SnapshotX < SnapshotWidth; ++SnapshotX)
            {
                int32 SourceX = SnapshotMinX + SnapshotX;
                int32 SourceY = SnapshotMinY + SnapshotY;
                if (bWrap)
                {
                    SourceX = WrapIndex(SourceX, Width);
                    SourceY = WrapIndex(SourceY, Height);
                }
                else
                {
                    SourceX = FMath::Clamp(SourceX, 0, Width - 1);
                    SourceY = FMath::Clamp(SourceY, 0, Height - 1);
                }
                SmoothSnapshot[SnapshotY * SnapshotWidth + SnapshotX] =
                    RevealColorBuffer[SourceY * Width + SourceX];
            }
        }
    }
    else
    {
        // Do not accidentally reuse a preceding smooth-brush snapshot for a
        // paint/erase sample. Retain capacity, release logical contents.
        SmoothSnapshot.Reset();
    }
    auto ReadColorAt = [this, Width, Height, bWrap, SnapshotMinX, SnapshotMinY, SnapshotWidth,
                        &WrapIndex, &SmoothSnapshot](int32 RawX, int32 RawY)
    {
        const int32 SnapshotIndex = (RawY - SnapshotMinY) * SnapshotWidth + (RawX - SnapshotMinX);
        if (SmoothSnapshot.IsValidIndex(SnapshotIndex))
        {
            return FLinearColor(SmoothSnapshot[SnapshotIndex]);
        }

        if (bWrap)
        {
            RawX = WrapIndex(RawX, Width);
            RawY = WrapIndex(RawY, Height);
        }
        else
        {
            RawX = FMath::Clamp(RawX, 0, Width - 1);
            RawY = FMath::Clamp(RawY, 0, Height - 1);
        }

        const int32 Index = RawY * Width + RawX;
        return FLinearColor(RevealColorBuffer[Index]);
    };
    bool bChanged = false;
    for (int32 RawY = MinY; RawY <= MaxY; ++RawY)
    for (int32 RawX = MinX; RawX <= MaxX; ++RawX)
    {
        if (!bWrap && (RawX < 0 || RawX >= Width || RawY < 0 || RawY >= Height)) continue;
        const float DX = (RawX + 0.5f - Center.X) / RadiusX;
        const float DY = (RawY + 0.5f - Center.Y) / RadiusY;
        const float Distance = FMath::Sqrt(DX * DX + DY * DY);
        if (Distance > 1.0f) continue;
        const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
            ? 1.0f : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
        const float Weight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
        const int32 X = bWrap ? WrapIndex(RawX, Width) : RawX;
        const int32 Y = bWrap ? WrapIndex(RawY, Height) : RawY;
        const int32 Index = Y * Width + X;
        if (!AutoBakePreviewResult->OuterCoverageBuffer.IsValidIndex(Index) || AutoBakePreviewResult->OuterCoverageBuffer[Index] == 0) continue;
        if (!PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, Index, ClipUVIslandID)) continue;
        FLinearColor TargetColor = PaintColor;
        if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::EraseToBase)
        {
            TargetColor = BaseColor;
        }
        else if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
        {
            TargetColor = FLinearColor::Black;
            for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
            {
                for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                {
                    int32 NeighborX = RawX + OffsetX;
                    int32 NeighborY = RawY + OffsetY;
                    if (bWrap)
                    {
                        NeighborX = WrapIndex(NeighborX, Width);
                        NeighborY = WrapIndex(NeighborY, Height);
                    }
                    else
                    {
                        NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                        NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                    }
                    const int32 NeighborIndex = NeighborY * Width + NeighborX;
                    if (PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, NeighborIndex, ClipUVIslandID))
                    {
                        TargetColor += ReadColorAt(RawX + OffsetX, RawY + OffsetY);
                    }
                    else
                    {
                        TargetColor += FLinearColor(RevealColorBuffer[Index]);
                    }
                }
            }
            TargetColor /= 9.0f;
            TargetColor.A = 1.0f;
        }
        RevealColorBuffer[Index] = FMath::Lerp(
            FLinearColor(RevealColorBuffer[Index]),
            TargetColor.CopyWithNewOpacity(1.0f),
            Weight).ToFColor(true);
        bChanged = true;
    }
    if (OutDirtyRect != nullptr)
    {
        *OutDirtyRect = FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
    }
    return bChanged;
}

bool SWetClothingTransparencyPreviewViewport::ShouldDeferBrushRaster(
    const FDWCTransparencyBrushSample& Sample) const
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const int64 RadiusX = FMath::Max<int64>(
        FMath::CeilToInt(Sample.RadiusUV * AutoBakePreviewResult->Resolution.X),
        1);
    const int64 RadiusY = FMath::Max<int64>(
        FMath::CeilToInt(Sample.RadiusUV * AutoBakePreviewResult->Resolution.Y),
        1);
    // Queue before the main-thread rasterizer reaches roughly the same dirty
    // area that already uses asynchronous full preview composition.
    return 4 * RadiusX * RadiusY >= TransparencyPreviewAsyncComposeThresholdPixels;
}

FIntRect SWetClothingTransparencyPreviewViewport::ComputeCurrentHoverDirtyRect() const
{
    const bool bHasHoverTarget = bTransparencyPaintingEnabled || bRevealColorPaintingEnabled;
    if (bPreviewSuspended || !bHasHoverTarget || IsAuthoringInteractionActive() ||
        !CurrentSurfaceHit.bHit || !AutoBakePreviewResult.IsValid() ||
        PreviewMode != EWetClothingTransparencyPreviewMode::TargetMeshOnly ||
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0)
    {
        return FIntRect();
    }

    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0)
    {
        return FIntRect();
    }

    FVector2D CenterUV = CurrentSurfaceHit.UV;
    const bool bWrap = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    if (bWrap)
    {
        CenterUV.X -= FMath::FloorToDouble(CenterUV.X);
        CenterUV.Y -= FMath::FloorToDouble(CenterUV.Y);
    }
    const float RadiusPixelsX = FMath::Max(PaintSettings.RadiusUV * Width, 1.0f);
    const float RadiusPixelsY = FMath::Max(PaintSettings.RadiusUV * Height, 1.0f);
    const FVector2D CenterPixels(CenterUV.X * Width, CenterUV.Y * Height);
    const int32 MinX = FMath::FloorToInt(CenterPixels.X - RadiusPixelsX - 1.0f);
    const int32 MinY = FMath::FloorToInt(CenterPixels.Y - RadiusPixelsY - 1.0f);
    const int32 MaxX = FMath::CeilToInt(CenterPixels.X + RadiusPixelsX + 1.0f);
    const int32 MaxY = FMath::CeilToInt(CenterPixels.Y + RadiusPixelsY + 1.0f);
    return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

void SWetClothingTransparencyPreviewViewport::RefreshHoverPreviewRegion()
{
    // Recompose only the previous and current hover regions. The stable
    // buffers remain authoritative; hover is never serialized or committed.
    const bool bWrap = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    if (!LastHoverDirtyRect.IsEmpty())
    {
        UploadPreviewTextureRegion(LastHoverDirtyRect, true, false);
    }

    LastHoverDirtyRect = ComputeCurrentHoverDirtyRect();
    if (!LastHoverDirtyRect.IsEmpty())
    {
        UploadPreviewTextureRegion(LastHoverDirtyRect, true, true);
    }
}

void SWetClothingTransparencyPreviewViewport::RebuildManualOverridesFromStrokes()
{
    InvalidatePreviewContent();
    ManualPremultipliedBuffer.Empty();
    ManualWeightBuffer.Empty();
    if (!AutoBakePreviewResult.IsValid())
    {
        return;
    }
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return;
    }
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        *AutoBakePreviewResult,
        Layer->EditableStrokes,
        AutoBakePreviewResult->BaselineStrokeCount,
        SelectedMaterialSlotIndex,
        SelectedUVChannelIndex,
        ManualPremultipliedBuffer,
        ManualWeightBuffer);
    bManualOverridesRequireWorkerRebuild = false;
}

bool SWetClothingTransparencyPreviewViewport::EnsureManualOverrideBuffers()
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    const int32 PixelCount = Width * Height;
    if (Width <= 0 || Height <= 0 || PixelCount <= 0)
    {
        return false;
    }

    if (ManualPremultipliedBuffer.Num() == PixelCount && ManualWeightBuffer.Num() == PixelCount)
    {
        return true;
    }

    ManualPremultipliedBuffer.Init(0, PixelCount);
    ManualWeightBuffer.Init(0, PixelCount);
    return true;
}

bool SWetClothingTransparencyPreviewViewport::EnsureRevealColorBuffer()
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const int32 PixelCount = AutoBakePreviewResult->Resolution.X * AutoBakePreviewResult->Resolution.Y;
    if (PixelCount <= 0 || AutoBakePreviewResult->InnerColorBuffer.Num() != PixelCount)
    {
        return false;
    }

    if (RevealColorBuffer.Num() != PixelCount)
    {
        // Preserve the auto result as the immutable replay baseline. Live
        // Stage 2 edits belong to this separate display/authoring layer.
        RevealColorBuffer = AutoBakePreviewResult->InnerColorBuffer;
    }
    return true;
}

void SWetClothingTransparencyPreviewViewport::ReleaseSmoothBrushScratch()
{
    SmoothBrushPremultipliedScratch.Empty();
    SmoothBrushWeightScratch.Empty();
    SmoothRevealColorScratch.Empty();
}

void SWetClothingTransparencyPreviewViewport::RefreshManualPreviewFromStrokes()
{
    // Cancellation and external edits discard all provisional tile state,
    // then replay both saved authoring layers in one worker job. This matters
    // for reveal paint because its immediate feedback writes only transient
    // preview pixels, never the persistent auto-bake source.
    bManualOverridesRequireWorkerRebuild = true;
    bRevealColorRequiresWorkerRebuild = true;
    bDeferredBrushPreviewRebuild = false;
    bAuthoringWorkerRebuildRequested = false;
    InvalidatePreviewContent();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

bool SWetClothingTransparencyPreviewViewport::RebuildWrinkleSuppressionBuffer()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RebuildWrinkleSuppressionBuffer);
    InvalidatePreviewContent();
    ++WrinkleSuppressionRebuildCount;
    WrinkleSuppressionBuffer.Reset();
    if (!AutoBakePreviewResult.IsValid())
    {
        WrinkleSuppressionPreviewHandle.Reset();
        return false;
    }

    const FDWCTransparencyAutoBakeResult& Result = *AutoBakePreviewResult;
    const int32 Width = Result.Resolution.X;
    const int32 Height = Result.Resolution.Y;
    const int32 PixelCount = Width * Height;
    if (Width <= 0 || Height <= 0 || PixelCount <= 0)
    {
        WrinkleSuppressionPreviewHandle.Reset();
        return false;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return UpdateWrinkleSuppressionPreviewTexture();
    }

    const FDWCWrinkleSuppressionSource SuppressionSource =
        FDWCWrinkleSuppressionProcessor::FindExactSource(
            Asset,
            SelectedMaterialSlotIndex);
    if (!SuppressionSource.IsValid())
    {
        InvalidateWrinkleSuppressionSourceCache();
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return UpdateWrinkleSuppressionPreviewTexture();
    }

    FString ProcessingError;
    const bool bCanReuseCoverage =
        CachedWrinkleSuppressionMaskTexture == SuppressionSource.MaskTexture &&
        SuppressionSource.BakedMap != nullptr &&
        CachedWrinkleSuppressionBakeGuid == SuppressionSource.BakedMap->BakeGuid &&
        CachedWrinkleSuppressionResolution == Result.Resolution &&
        CachedWrinkleSuppressionCoverageBuffer.Num() == PixelCount;
    if (!bCanReuseCoverage)
    {
        InvalidateWrinkleSuppressionSourceCache();
        if (!FDWCWrinkleSuppressionProcessor::BuildResampledCoverageBuffer(
                SuppressionSource,
                Result.Resolution,
                CachedWrinkleSuppressionCoverageBuffer,
                ProcessingError))
        {
            WrinkleSuppressionBuffer.Init(0, PixelCount);
            return UpdateWrinkleSuppressionPreviewTexture();
        }
        CachedWrinkleSuppressionMaskTexture = SuppressionSource.MaskTexture;
        CachedWrinkleSuppressionBakeGuid = SuppressionSource.BakedMap->BakeGuid;
        CachedWrinkleSuppressionResolution = Result.Resolution;
    }

    if (!FDWCWrinkleSuppressionProcessor::BuildProcessedBufferFromCoverage(
            CachedWrinkleSuppressionCoverageBuffer,
            Asset->Authored.TransparencyData.WrinkleSuppressionCoverageThreshold,
            Asset->Authored.TransparencyData.WrinkleSuppressionMaskSoftness,
            WrinkleSuppressionBuffer,
            ProcessingError))
    {
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return UpdateWrinkleSuppressionPreviewTexture();
    }
    return UpdateWrinkleSuppressionPreviewTexture();
}

bool SWetClothingTransparencyPreviewViewport::UpdateWrinkleSuppressionPreviewTexture()
{
    if (!AutoBakePreviewResult.IsValid())
    {
        WrinkleSuppressionPreviewHandle.Reset();
        return false;
    }

    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (Resolution.X <= 0 || Resolution.Y <= 0 || WrinkleSuppressionBuffer.Num() != PixelCount)
    {
        WrinkleSuppressionPreviewHandle.Reset();
        return false;
    }

    if (!TextureWorkspace.IsValid())
    {
        return false;
    }

    const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
    FDWCEditorTextureDescriptor Descriptor;
    Descriptor.Size = Resolution;
    Descriptor.PixelFormat = PF_G8;
    Descriptor.bSRGB = false;
    Descriptor.CompressionSettings = TC_Grayscale;
    Descriptor.MipGenSettings = TMGS_NoMipmaps;
    Descriptor.Filter = TF_Bilinear;
    Descriptor.AddressX = Address;
    Descriptor.AddressY = Address;
    Descriptor.LODGroup = TEXTUREGROUP_World;
    Descriptor.InitialG8 = 0;
    TArray<uint8> Pixels = WrinkleSuppressionBuffer;
    const FDWCEditorTextureHandle PublishedTexture = TextureWorkspace->PublishG8(
        MakeTransparencyTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::TransparencyWrinkleSuppression,
            SelectedMaterialSlotIndex,
            SelectedLayerGuid),
        Descriptor,
        MoveTemp(Pixels),
        EDWCEditorTextureUploadPriority::Normal);
    WrinkleSuppressionPreviewHandle = TextureWorkspace->AcquireLease(PublishedTexture);
    return WrinkleSuppressionPreviewHandle.IsValid();
}

bool SWetClothingTransparencyPreviewViewport::CanUseDynamicFinalPreviewComposition() const
{
    return VisualizationMode == EDWCTransparencyVisualizationMode::Final &&
        AutoBakePreviewResult.IsValid() &&
        !AutoBakePreviewResult->bIsFinalBakedBaseline &&
        GetTransparencyPreviewTexture() != nullptr;
}

bool SWetClothingTransparencyPreviewViewport::UsesFinalAlphaPreview() const
{
    return VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
        VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha;
}

bool SWetClothingTransparencyPreviewViewport::UsesWrinkleSuppressionPreview() const
{
    return UsesFinalAlphaPreview() ||
        VisualizationMode == EDWCTransparencyVisualizationMode::WrinkleSeparation;
}

void SWetClothingTransparencyPreviewViewport::RefreshDeferredFinalPreviewBuffers()
{
    if (bWrinkleSuppressionPreviewDirty && UsesWrinkleSuppressionPreview())
    {
        RebuildWrinkleSuppressionBuffer();
        bWrinkleSuppressionPreviewDirty = false;
    }

    if (bOuterEdgeFeatherPreviewDirty && UsesFinalAlphaPreview())
    {
        RebuildOuterEdgeFeatherBuffer();
        bOuterEdgeFeatherPreviewDirty = false;
    }
}

void SWetClothingTransparencyPreviewViewport::InvalidateWrinkleSuppressionSourceCache()
{
    CachedWrinkleSuppressionMaskTexture = nullptr;
    CachedWrinkleSuppressionBakeGuid.Invalidate();
    CachedWrinkleSuppressionResolution = FIntPoint::ZeroValue;
    CachedWrinkleSuppressionCoverageBuffer.Empty();
}

bool SWetClothingTransparencyPreviewViewport::RebuildOuterEdgeFeatherBuffer()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetClothingTransparencyPreviewViewport_RebuildOuterEdgeFeatherBuffer);
    InvalidatePreviewContent();
    ++OuterEdgeFeatherRebuildCount;
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

float SWetClothingTransparencyPreviewViewport::GetStoredEditedAlpha(const int32 PixelIndex) const
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return 0.0f;
    }
    return FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
        *AutoBakePreviewResult,
        ManualPremultipliedBuffer,
        ManualWeightBuffer,
        PixelIndex);
}

float SWetClothingTransparencyPreviewViewport::ApplyHoverToEditedAlpha(
    const int32 PixelIndex,
    const float EditedAlpha) const
{
    if (!bTransparencyPaintingEnabled || IsAuthoringInteractionActive() || !CurrentSurfaceHit.bHit ||
        PreviewMode != EWetClothingTransparencyPreviewMode::TargetMeshOnly ||
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0 ||
        (VisualizationMode != EDWCTransparencyVisualizationMode::Final &&
         VisualizationMode != EDWCTransparencyVisualizationMode::AutoAlpha) ||
        !AutoBakePreviewResult.IsValid())
    {
        return EditedAlpha;
    }

    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0 || PixelIndex < 0 || PixelIndex >= Width * Height)
    {
        return EditedAlpha;
    }
    if (!PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, PixelIndex, CurrentSurfaceHit.UVIslandID))
    {
        return EditedAlpha;
    }

    const int32 PixelX = PixelIndex % Width;
    const int32 PixelY = PixelIndex / Width;
    FVector2D Delta(
        (PixelX + 0.5) / Width - CurrentSurfaceHit.UV.X,
        (PixelY + 0.5) / Height - CurrentSurfaceHit.UV.Y);
    const bool bWrap = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    if (bWrap)
    {
        Delta.X -= FMath::RoundToDouble(Delta.X);
        Delta.Y -= FMath::RoundToDouble(Delta.Y);
    }
    const float Radius = FMath::Max(PaintSettings.RadiusUV, 0.0001f);
    const float Distance = Delta.Size() / Radius;
    if (Distance > 1.0f)
    {
        return EditedAlpha;
    }

    const float Falloff = FMath::Clamp(PaintSettings.Falloff, 0.0f, 1.0f);
    const float InnerRadius = 1.0f - Falloff;
    const float RadialWeight = Distance <= InnerRadius || Falloff <= KINDA_SMALL_NUMBER
        ? 1.0f
        : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
    const float BrushWeight = FMath::Clamp(RadialWeight * PaintSettings.Strength, 0.0f, 1.0f);
    if (BrushWeight <= 0.0f)
    {
        return EditedAlpha;
    }

    if (PaintSettings.Mode == EDWCTransparencyBrushMode::ResetToAuto)
    {
        const float AutoAlpha = AutoBakePreviewResult->AutoAlphaBuffer[PixelIndex] / 255.0f;
        const float ManualWeight = ManualWeightBuffer.IsValidIndex(PixelIndex) ? ManualWeightBuffer[PixelIndex] / 255.0f : 0.0f;
        const float ManualPremultiplied = ManualPremultipliedBuffer.IsValidIndex(PixelIndex) ? ManualPremultipliedBuffer[PixelIndex] / 255.0f : 0.0f;
        const float RemainingWeight = ManualWeight * (1.0f - BrushWeight);
        const float RemainingPremultiplied = ManualPremultiplied * (1.0f - BrushWeight);
        return FMath::Clamp(AutoAlpha * (1.0f - RemainingWeight) + RemainingPremultiplied, 0.0f, 1.0f);
    }

    float TargetAlpha = PaintSettings.TargetAlpha;
    if (PaintSettings.Mode == EDWCTransparencyBrushMode::Apply)
    {
        TargetAlpha = 1.0f;
    }
    else if (PaintSettings.Mode == EDWCTransparencyBrushMode::Erase)
    {
        TargetAlpha = 0.0f;
    }
    else if (PaintSettings.Mode == EDWCTransparencyBrushMode::Smooth)
    {
        TargetAlpha = 0.0f;
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                int32 SampleX = PixelX + OffsetX;
                int32 SampleY = PixelY + OffsetY;
                if (bWrap)
                {
                    SampleX = (SampleX % Width + Width) % Width;
                    SampleY = (SampleY % Height + Height) % Height;
                }
                else
                {
                    SampleX = FMath::Clamp(SampleX, 0, Width - 1);
                    SampleY = FMath::Clamp(SampleY, 0, Height - 1);
                }
                const int32 NeighborIndex = SampleY * Width + SampleX;
                TargetAlpha += PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, NeighborIndex, CurrentSurfaceHit.UVIslandID)
                    ? GetStoredEditedAlpha(NeighborIndex)
                    : EditedAlpha;
            }
        }
        TargetAlpha /= 9.0f;
    }
    return FMath::Clamp(FMath::Lerp(EditedAlpha, TargetAlpha, BrushWeight), 0.0f, 1.0f);
}

FColor SWetClothingTransparencyPreviewViewport::ApplyHoverToRevealColor(
    const int32 PixelIndex,
    const FColor& BaseColor) const
{
    if (!bRevealColorPaintingEnabled || IsAuthoringInteractionActive() ||
        !CurrentSurfaceHit.bHit || PreviewMode != EWetClothingTransparencyPreviewMode::TargetMeshOnly ||
        VisualizationMode != EDWCTransparencyVisualizationMode::InnerColor ||
        !AutoBakePreviewResult.IsValid() ||
        !AutoBakePreviewResult->InnerColorBuffer.IsValidIndex(PixelIndex))
    {
        return BaseColor;
    }

    if (!PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, PixelIndex, CurrentSurfaceHit.UVIslandID))
    {
        return BaseColor;
    }

    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0 || PixelIndex < 0 || PixelIndex >= Width * Height)
    {
        return BaseColor;
    }

    const int32 PixelX = PixelIndex % Width;
    const int32 PixelY = PixelIndex / Width;
    FVector2D Delta(
        (PixelX + 0.5) / Width - CurrentSurfaceHit.UV.X,
        (PixelY + 0.5) / Height - CurrentSurfaceHit.UV.Y);
    const bool bWrap = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    if (bWrap)
    {
        Delta.X -= FMath::RoundToDouble(Delta.X);
        Delta.Y -= FMath::RoundToDouble(Delta.Y);
    }

    const float Radius = FMath::Max(PaintSettings.RadiusUV, 0.0001f);
    const float Distance = Delta.Size() / Radius;
    if (Distance > 1.0f)
    {
        return BaseColor;
    }

    const float Falloff = FMath::Clamp(PaintSettings.Falloff, 0.0f, 1.0f);
    const float InnerRadius = 1.0f - Falloff;
    const float RadialWeight = Distance <= InnerRadius || Falloff <= KINDA_SMALL_NUMBER
        ? 1.0f
        : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
    const float BrushWeight = FMath::Clamp(RadialWeight * PaintSettings.Strength, 0.0f, 1.0f);
    if (BrushWeight <= 0.0f)
    {
        return BaseColor;
    }

    FLinearColor TargetColor(BaseColor);
    switch (PaintSettings.RevealColorMode)
    {
    case EDWCTransparencyRevealColorBrushMode::EraseToBase:
        TargetColor = FLinearColor(AutoBakePreviewResult->InnerColorBuffer[PixelIndex]);
        break;
    case EDWCTransparencyRevealColorBrushMode::Smooth:
    {
        FLinearColor Average = FLinearColor::Black;
        int32 SampleCount = 0;
        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
        {
            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
            {
                int32 SampleX = PixelX + OffsetX;
                int32 SampleY = PixelY + OffsetY;
                if (bWrap)
                {
                    SampleX = (SampleX % Width + Width) % Width;
                    SampleY = (SampleY % Height + Height) % Height;
                }
                else
                {
                    SampleX = FMath::Clamp(SampleX, 0, Width - 1);
                    SampleY = FMath::Clamp(SampleY, 0, Height - 1);
                }
                const int32 NeighborIndex = SampleY * Width + SampleX;
                if (RevealColorBuffer.IsValidIndex(NeighborIndex) &&
                    PassesTransparencyPreviewIslandClip(*AutoBakePreviewResult, NeighborIndex, CurrentSurfaceHit.UVIslandID))
                {
                    Average += FLinearColor(RevealColorBuffer[NeighborIndex]);
                    ++SampleCount;
                }
            }
        }
        TargetColor = SampleCount > 0 ? Average / static_cast<float>(SampleCount) : FLinearColor(BaseColor);
        break;
    }
    case EDWCTransparencyRevealColorBrushMode::Paint:
    default:
        TargetColor = PaintSettings.RevealColor;
        break;
    }

    return FMath::Lerp(FLinearColor(BaseColor), TargetColor, BrushWeight).ToFColor(false);
}

FColor SWetClothingTransparencyPreviewViewport::BuildVisualizationPixel(
    const int32 PixelIndex,
    const FDWCTransparencyPixelComposeContext& Context,
    const bool bIncludeHover) const
{
    if (!AutoBakePreviewResult.IsValid() || !AutoBakePreviewResult->InnerColorBuffer.IsValidIndex(PixelIndex) ||
        !AutoBakePreviewResult->AutoAlphaBuffer.IsValidIndex(PixelIndex))
    {
        return FColor::Black;
    }
    const float StoredAlpha = GetStoredEditedAlpha(PixelIndex);
    const float EditedAlpha = bIncludeHover
        ? ApplyHoverToEditedAlpha(PixelIndex, StoredAlpha)
        : StoredAlpha;
    FColor Pixel = FDWCTransparencyComposite::ComposeVisualizationPixel(Context, PixelIndex, EditedAlpha);
    return bIncludeHover ? ApplyHoverToRevealColor(PixelIndex, Pixel) : Pixel;
}

void SWetClothingTransparencyPreviewViewport::QueuePreviewTextureUpdate(
    const FIntRect& DirtyRect,
    const bool bWrap,
    const bool bAllowFullRebuild)
{
    if (DirtyRect.IsEmpty() || !AutoBakePreviewResult.IsValid() || !TransparencyPreviewHandle.IsValid())
    {
        return;
    }

    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    FDWCEditorDirtyRegionSet Regions;
    Regions.Add(DirtyRect, Resolution, bWrap);
    int32 TotalDirtyPixels = 0;
    for (const FIntRect& Region : Regions.GetRegions())
    {
        TotalDirtyPixels += Region.Width() * Region.Height();
    }
    if (TotalDirtyPixels >= TransparencyPreviewAsyncComposeThresholdPixels)
    {
        if (bAllowFullRebuild)
        {
            RebuildTransparencyPreviewTexture();
        }
        return;
    }
    for (const FIntRect& Region : Regions.GetRegions())
    {
        UploadPreviewTextureRegion(Region, true);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::InvalidatePreviewContent()
{
    ++PreviewContentRevision;
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

void SWetClothingTransparencyPreviewViewport::UploadPreviewTextureRegion(
    const FIntRect& DirtyRect,
    const bool bRebuildPixels,
    const bool bIncludeHover)
{
    if (!TransparencyPreviewHandle.IsValid() || !TextureWorkspace.IsValid() ||
        !AutoBakePreviewResult.IsValid() || DirtyRect.IsEmpty())
    {
        return;
    }
    const int32 TextureWidth = AutoBakePreviewResult->Resolution.X;
    const int32 TextureHeight = AutoBakePreviewResult->Resolution.Y;
    const FIntRect ClampedDirtyRect(
        FMath::Clamp(DirtyRect.Min.X, 0, TextureWidth),
        FMath::Clamp(DirtyRect.Min.Y, 0, TextureHeight),
        FMath::Clamp(DirtyRect.Max.X, 0, TextureWidth),
        FMath::Clamp(DirtyRect.Max.Y, 0, TextureHeight));
    const int32 RegionWidth = ClampedDirtyRect.Width();
    const int32 RegionHeight = ClampedDirtyRect.Height();
    if (RegionWidth <= 0 || RegionHeight <= 0)
    {
        return;
    }
    ++PreviewTextureRegionUploadCount;
    PreviewTextureRegionUploadBytes +=
        static_cast<uint64>(RegionWidth) * static_cast<uint64>(RegionHeight) * sizeof(FColor);

    TArray<FColor>& PreviewPixels = TransparencyPreviewHandle->GetMutableBGRA8Pixels();
    if (PreviewPixels.Num() != TextureWidth * TextureHeight)
    {
        RebuildTransparencyPreviewTexture();
        return;
    }

    FDWCTransparencyPixelComposeContext Context;
    Context.AutoResult = AutoBakePreviewResult.Get();
    Context.RevealColorBuffer = MakeArrayView(RevealColorBuffer);
    Context.ManualPremultipliedBuffer = MakeArrayView(ManualPremultipliedBuffer);
    Context.ManualWeightBuffer = MakeArrayView(ManualWeightBuffer);
    Context.WrinkleSuppressionBuffer = MakeArrayView(WrinkleSuppressionBuffer);
    Context.OuterEdgeFeatherBuffer = MakeArrayView(OuterEdgeFeatherBuffer);
    Context.VisualizationMode = VisualizationMode;
    Context.TransparencyStrength = TransparencyPreviewStrength;
    Context.WrinkleSuppressionStrength = WrinkleSuppressionStrength;
    Context.MaximumHitDistance = VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance
        ? FDWCTransparencyComposite::ComputeMaximumHitDistance(*AutoBakePreviewResult)
        : KINDA_SMALL_NUMBER;
    for (int32 Y = 0; Y < RegionHeight; ++Y)
    {
        for (int32 X = 0; X < RegionWidth; ++X)
        {
            const int32 PixelIndex = (ClampedDirtyRect.Min.Y + Y) * TextureWidth + ClampedDirtyRect.Min.X + X;
            if (bRebuildPixels)
            {
                PreviewPixels[PixelIndex] = BuildVisualizationPixel(PixelIndex, Context, bIncludeHover);
            }
        }
    }
    TextureWorkspace->MarkDirty(
        TransparencyPreviewHandle,
        ClampedDirtyRect,
        false,
        EDWCEditorTextureUploadPriority::Interactive);
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
        TransparencyPreviewHandle.Reset();
        LastHoverDirtyRect = FIntRect();
        return false;
    }

    const uint64 SnapshotContentRevision = PreviewContentRevision;
    if (PendingPreviewTicket.IsValid() && PendingPreviewContentRevision == SnapshotContentRevision)
    {
        // Multiple UI refresh requests can target the same immutable state in
        // a frame. The in-flight worker already owns that snapshot.
        return true;
    }
    const bool bRebuildManualOverrides = bManualOverridesRequireWorkerRebuild;
    const bool bRebuildRevealColor = bRevealColorRequiresWorkerRebuild;
    FDWCTransparencyVisualizationJobInput Input;
    // AutoBakePreviewResult is published read-only and shared with the job.
    // This removes the full-resolution deep copy that previously happened on
    // every visualization request.
    Input.AutoResult = AutoBakePreviewResult;
    // A rebuild creates authoritative buffers from strokes, so do not copy
    // the old working buffer only to overwrite it on the worker.
    if (!bRebuildRevealColor)
    {
        Input.RevealColorBuffer = RevealColorBuffer;
    }
    if (!bRebuildManualOverrides)
    {
        Input.ManualPremultipliedBuffer = ManualPremultipliedBuffer;
        Input.ManualWeightBuffer = ManualWeightBuffer;
    }
    Input.WrinkleSuppressionBuffer = WrinkleSuppressionBuffer;
    Input.OuterEdgeFeatherBuffer = OuterEdgeFeatherBuffer;
    Input.VisualizationMode = VisualizationMode;
    Input.TransparencyPreviewStrength = TransparencyPreviewStrength;
    Input.WrinkleSuppressionStrength = WrinkleSuppressionStrength;
    Input.bRebuildManualOverridesFromStrokes = bRebuildManualOverrides;
    Input.bRebuildRevealColorFromStrokes = bRebuildRevealColor;
    if (bRebuildManualOverrides || bRebuildRevealColor)
    {
        const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
        if (Layer == nullptr)
        {
            return false;
        }
        if (bRebuildManualOverrides)
        {
            Input.EditableStrokes = Layer->EditableStrokes;
        }
        if (bRebuildRevealColor)
        {
            Input.RevealColorPaintStrokes = Layer->RevealColorPaintStrokes;
        }
        Input.BaselineStrokeCount = Input.AutoResult->BaselineStrokeCount;
        Input.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
        Input.MaterialSlotIndex = SelectedMaterialSlotIndex;
        Input.UVChannelIndex = SelectedUVChannelIndex;
    }

    const FIntPoint Resolution = Input.AutoResult->Resolution;
    const int32 PixelCount = Resolution.X * Resolution.Y;
    if (PixelCount <= 0)
    {
        return false;
    }

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
    Descriptor.Key.MaterialSlotIndex = Input.AutoResult->MaterialSlotIndex;
    Descriptor.Key.LayerGuid = Input.AutoResult->LayerGuid;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Transparency;
    Descriptor.DomainRevision = WorkerJobScheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    // The auto result is shared, but remains resident for the lifetime of the
    // job. Count it once in the reservation so the scheduler does not accept
    // a job whose shared base plus private working/output memory exceeds the
    // budget. Rebuild buffers are moved into the result rather than copied.
    Descriptor.EstimatedBytes =
        Input.AutoResult->GetAllocatedBytes() +
        Input.ManualPremultipliedBuffer.GetAllocatedSize() +
        Input.ManualWeightBuffer.GetAllocatedSize() +
        Input.RevealColorBuffer.GetAllocatedSize() +
        Input.WrinkleSuppressionBuffer.GetAllocatedSize() +
        Input.OuterEdgeFeatherBuffer.GetAllocatedSize() +
        Input.EditableStrokes.GetAllocatedSize() +
        Input.RevealColorPaintStrokes.GetAllocatedSize() +
        sizeof(FLinearColor) +
        static_cast<uint64>(PixelCount) * sizeof(FColor);
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency visualization slot %d"),
        Descriptor.Key.MaterialSlotIndex);

    const FGuid ExpectedLayerGuid = Descriptor.Key.LayerGuid;
    const int32 ExpectedSlotIndex = Descriptor.Key.MaterialSlotIndex;
    const EDWCTransparencyUVAddressMode AddressMode = SelectedUVAddressMode;
    TWeakPtr<SWetClothingTransparencyPreviewViewport> WeakThis = SharedThis(this);
    FString SubmitError;
    const FDWCEditorWorkerJobTicket Ticket = WorkerJobScheduler->Submit(
        Descriptor,
        [Input = MoveTemp(Input)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken) mutable
        {
            return FDWCTransparencyVisualizationWorker::Build(MoveTemp(Input), CancellationToken);
        },
        [WeakThis, ExpectedLayerGuid, ExpectedSlotIndex, AddressMode, SnapshotContentRevision](
            const FDWCEditorWorkerJobTicket& Ticket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyPreviewViewport> Viewport = WeakThis.Pin();
            const TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyVisualizationJobResult>(BaseResult);
            if (!Viewport.IsValid() || !Result.IsValid() ||
                !Viewport->AutoBakePreviewResult.IsValid() ||
                Viewport->AutoBakePreviewResult->LayerGuid != ExpectedLayerGuid ||
                Viewport->AutoBakePreviewResult->MaterialSlotIndex != ExpectedSlotIndex ||
                Viewport->PendingPreviewTicket.JobId != Ticket.JobId ||
                Viewport->PendingPreviewTicket.Generation != Ticket.Generation)
            {
                return;
            }

            Viewport->PendingPreviewTicket = {};
            Viewport->PendingPreviewContentRevision = 0;
            if (!Result->bSucceeded)
            {
                return;
            }
            if (Viewport->PreviewContentRevision != SnapshotContentRevision)
            {
                // During a drag, or while its sparse tile queue is still
                // converging, one later commit rebuild owns the final state.
                // Do not recursively submit obsolete full-image snapshots.
                if (Viewport->IsAuthoringInteractionActive() ||
                    !Viewport->PendingInteractivePaintWork.IsEmpty() ||
                    Viewport->bAuthoringFinishPending)
                {
                    Viewport->bAuthoringWorkerRebuildRequested = true;
                }
                else
                {
                    Viewport->RebuildTransparencyPreviewTexture();
                }
                return;
            }

            if (Result->bIncludesRebuiltManualOverrides)
            {
                Viewport->ManualPremultipliedBuffer = MoveTemp(Result->RebuiltManualPremultipliedBuffer);
                Viewport->ManualWeightBuffer = MoveTemp(Result->RebuiltManualWeightBuffer);
                Viewport->bManualOverridesRequireWorkerRebuild = false;
            }
            if (Result->bIncludesRebuiltRevealColor)
            {
                Viewport->RevealColorBuffer = MoveTemp(Result->RebuiltRevealColorBuffer);
                Viewport->bRevealColorRequiresWorkerRebuild = false;
            }

            const FIntPoint ResultResolution = Result->Resolution;
            const TextureAddress Address = AddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
            const FDWCEditorTextureHandle PublishedTexture = Viewport->TextureWorkspace->PublishBGRA8(
                MakeTransparencyTextureKey(
                    Viewport->WetClothingAsset.Get(),
                    EDWCEditorTexturePurpose::TransparencyVisualization,
                    ExpectedSlotIndex,
                    ExpectedLayerGuid),
                MakeTransparencyDescriptor(ResultResolution, Address),
                MoveTemp(Result->Pixels),
                EDWCEditorTextureUploadPriority::Interactive);
            Viewport->TransparencyPreviewHandle = Viewport->TextureWorkspace->AcquireLease(PublishedTexture);
            if (!Viewport->TransparencyPreviewHandle.IsValid())
            {
                return;
            }
            ++Viewport->PreviewTextureRebuildCount;
            Viewport->LastHoverDirtyRect = Viewport->ComputeCurrentHoverDirtyRect();
            Viewport->ApplyTransparencyPreviewParameters();
            Viewport->RefreshHoverPreviewRegion();
            Viewport->InvalidatePreviewViewport();
        },
        &SubmitError);
    PendingPreviewTicket = Ticket;
    PendingPreviewContentRevision = Ticket.IsValid() ? SnapshotContentRevision : 0;
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
    FDWCEditorPreviewMemoryBucket& AutoResult = OutBuckets.AddDefaulted_GetRef();
    AutoResult.Name = TEXT("Transparency auto result");
    if (AutoBakePreviewResult.IsValid())
    {
        AutoResult.UsedBytes = AutoBakePreviewResult->GetAllocatedBytes();
        AutoResult.EntryCount = 1;
    }

    FDWCEditorPreviewMemoryBucket& Working = OutBuckets.AddDefaulted_GetRef();
    Working.Name = TEXT("Transparency working buffers");
    Working.UsedBytes =
        static_cast<uint64>(WrinkleSuppressionBuffer.GetAllocatedSize()) +
        static_cast<uint64>(CachedWrinkleSuppressionCoverageBuffer.GetAllocatedSize()) +
        static_cast<uint64>(OuterEdgeFeatherBuffer.GetAllocatedSize()) +
        static_cast<uint64>(ManualPremultipliedBuffer.GetAllocatedSize()) +
        static_cast<uint64>(ManualWeightBuffer.GetAllocatedSize()) +
        static_cast<uint64>(RevealColorBuffer.GetAllocatedSize()) +
        static_cast<uint64>(SmoothBrushPremultipliedScratch.GetAllocatedSize()) +
        static_cast<uint64>(SmoothBrushWeightScratch.GetAllocatedSize()) +
        static_cast<uint64>(SmoothRevealColorScratch.GetAllocatedSize());
    Working.EntryCount = 1;

    if (LiveStrokeLayer && LiveStrokeLayer->IsActive())
    {
        FDWCEditorPreviewMemoryBucket& LiveStroke = OutBuckets.AddDefaulted_GetRef();
        LiveStroke.Name = TEXT("Transparency live sparse stroke");
        LiveStroke.UsedBytes = LiveStrokeLayer->GetAllocatedBytes();
        LiveStroke.EntryCount = LiveStrokeLayer->GetTileCount();
    }

    if (!PendingInteractivePaintWork.IsEmpty())
    {
        FDWCEditorPreviewMemoryBucket& InteractiveQueue = OutBuckets.AddDefaulted_GetRef();
        InteractiveQueue.Name = TEXT("Transparency interactive paint queue");
        InteractiveQueue.UsedBytes = GetInteractivePaintWorkAllocatedBytes();
        InteractiveQueue.EntryCount = static_cast<int32>(GetPendingInteractivePaintRegionCount());
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
    OutCounters.Add({
        TEXT("Transparency texture region uploads"),
        PreviewTextureRegionUploadCount,
        PreviewTextureRegionUploadBytes});
    OutCounters.Add({TEXT("Transparency wrinkle suppression rebuilds"), WrinkleSuppressionRebuildCount, 0});
    OutCounters.Add({TEXT("Transparency outer-edge feather rebuilds"), OuterEdgeFeatherRebuildCount, 0});
    OutCounters.Add({TEXT("Transparency queued interactive regions"), InteractivePaintQueuedRegionCount, 0});
    OutCounters.Add({TEXT("Transparency processed interactive regions"), InteractivePaintProcessedRegionCount, 0});
    OutCounters.Add({TEXT("Transparency canceled interactive regions"), InteractivePaintCanceledRegionCount, 0});
    OutCounters.Add({
        TEXT("Transparency peak interactive region backlog"),
        InteractivePaintPeakQueuedRegionCount,
        GetPendingInteractivePaintRegionCount()});
    OutCounters.Add({
        TEXT("Transparency authoritative stroke replays"),
        InteractivePaintAuthoritativeReplayCount,
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
    PreviewTextureRegionUploadCount = 0;
    PreviewTextureRegionUploadBytes = 0;
    WrinkleSuppressionRebuildCount = 0;
    OuterEdgeFeatherRebuildCount = 0;
    InteractivePaintQueuedRegionCount = 0;
    InteractivePaintProcessedRegionCount = 0;
    InteractivePaintCanceledRegionCount = 0;
    InteractivePaintPeakQueuedRegionCount = 0;
    InteractivePaintAuthoritativeReplayCount = 0;
}

#undef LOCTEXT_NAMESPACE
