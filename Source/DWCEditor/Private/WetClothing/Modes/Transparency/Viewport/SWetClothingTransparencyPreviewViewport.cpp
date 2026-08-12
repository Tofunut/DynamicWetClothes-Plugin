//Copyright 2026 Team Tofunut. All Rights Reserved.
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
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyAlphaIncrementalWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyRevealColorIncrementalWorker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyDirtyTileReplayWorker.h"
#include "WetRendering/WetMaterialParameters.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyPreviewViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetTransparencyPreviewViewport, Log, All);

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

    FDWCEditorTextureDescriptor MakeTransparencyMaskDescriptor(
        const FIntPoint& Size,
        const TextureAddress Address)
    {
        FDWCEditorTextureDescriptor Descriptor;
        Descriptor.Size = Size;
        Descriptor.PixelFormat = PF_G8;
        Descriptor.bSRGB = false;
        Descriptor.CompressionSettings = TC_Masks;
        Descriptor.MipGenSettings = TMGS_NoMipmaps;
        Descriptor.Filter = TF_Bilinear;
        Descriptor.AddressX = Address;
        Descriptor.AddressY = Address;
        Descriptor.LODGroup = TEXTUREGROUP_World;
        Descriptor.InitialG8 = 0;
        return Descriptor;
    }

    uint64 GetTransparencyStrokeSnapshotBytes(
        const TArray<FDWCTransparencyBrushStroke>& Strokes,
        const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes)
    {
        uint64 Bytes = Strokes.GetAllocatedSize() + RevealColorStrokes.GetAllocatedSize();
        for (const FDWCTransparencyBrushStroke& Stroke : Strokes)
        {
            Bytes += Stroke.Samples.GetAllocatedSize();
            Bytes += Stroke.DisplayName.GetAllocatedSize();
        }
        for (const FDWCTransparencyRevealColorStroke& Stroke : RevealColorStrokes)
        {
            Bytes += Stroke.Samples.GetAllocatedSize();
        }
        return Bytes;
    }

    FDWCEditorWorkerMemoryEstimate EstimateTransparencyVisualizationMemory(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyRevealColorTileStore& RevealColorTileStore,
        const uint64 AlphaSnapshotBytes,
        const TArray<uint8>& OuterEdgeFeatherBuffer,
        const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes,
        const bool bMaterializeAlpha,
        const bool bRebuildRevealColor)
    {
        FDWCEditorWorkerMemoryEstimate Estimate;
        const uint64 PixelCount =
            static_cast<uint64>(FMath::Max(SourcePayload.Resolution.X, 0)) *
            static_cast<uint64>(FMath::Max(SourcePayload.Resolution.Y, 0));
        Estimate.ResidentSharedBytes = SourcePayload.GetAllocatedBytes();
        Estimate.SnapshotBytes =
            RevealColorTileStore.GetAllocatedBytes() +
            AlphaSnapshotBytes +
            OuterEdgeFeatherBuffer.GetAllocatedSize() +
            GetTransparencyStrokeSnapshotBytes({}, RevealColorStrokes) +
            sizeof(FLinearColor);
        Estimate.WorkingBytes =
            (bMaterializeAlpha ? PixelCount * sizeof(uint8) * 2ull : 0ull) +
            (bRebuildRevealColor ? PixelCount * sizeof(FColor) : 0ull);
        Estimate.OutputBytes = PixelCount * sizeof(FColor);
        return Estimate;
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
                Pinned->TickPreviewMaterialCompilations();
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
            if ((EventArgs.Event == IE_Pressed || EventArgs.Event == IE_Repeat))
            {
                if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                {
                    if (Pinned->IsExternalSourcePlacementActive() && EventArgs.Event == IE_Pressed)
                    {
                        if (EventArgs.Key == EKeys::W)
                        {
                            ExternalSourceWidgetMode = UE::Widget::WM_Translate;
                            Invalidate();
                            return true;
                        }
                        if (EventArgs.Key == EKeys::E)
                        {
                            ExternalSourceWidgetMode = UE::Widget::WM_Rotate;
                            Invalidate();
                            return true;
                        }
                    }
                    if (Pinned->NudgeExternalSourcePlacement(
                            EventArgs.Key,
                            Viewport != nullptr &&
                                (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift)),
                            Viewport != nullptr &&
                                (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt))))
                    {
                        return true;
                    }
                }
            }
            return FEditorViewportClient::InputKey(EventArgs);
        }

        virtual bool CanSetWidgetMode(const UE::Widget::EWidgetMode NewMode) const override
        {
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                if (Pinned->IsExternalSourcePlacementActive())
                {
                    return NewMode == UE::Widget::WM_Translate || NewMode == UE::Widget::WM_Rotate;
                }
            }
            return FEditorViewportClient::CanSetWidgetMode(NewMode);
        }

        virtual void SetWidgetMode(const UE::Widget::EWidgetMode NewMode) override
        {
            if (CanSetWidgetMode(NewMode))
            {
                ExternalSourceWidgetMode = NewMode;
                return;
            }
            FEditorViewportClient::SetWidgetMode(NewMode);
        }

        virtual void TrackingStarted(
            const FInputEventState& InInputState,
            const bool bIsDraggingWidget,
            const bool bNudge) override
        {
            FEditorViewportClient::TrackingStarted(InInputState, bIsDraggingWidget, bNudge);
            if (bIsDraggingWidget)
            {
                if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                {
                    Pinned->BeginExternalSourceTransformInteraction();
                }
            }
        }

        virtual void TrackingStopped() override
        {
            FEditorViewportClient::TrackingStopped();
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                Pinned->FinishExternalSourceTransformInteraction();
            }
        }

        virtual bool InputWidgetDelta(
            FViewport* InViewport,
            const EAxisList::Type CurrentAxis,
            FVector& Drag,
            FRotator& Rot,
            FVector& Scale) override
        {
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                if (Pinned->ApplyExternalSourceWidgetDelta(CurrentAxis, Drag, Rot, Scale))
                {
                    return true;
                }
            }
            return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
        }

        virtual UE::Widget::EWidgetMode GetWidgetMode() const override
        {
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                if (Pinned->IsExternalSourcePlacementActive())
                {
                    return ExternalSourceWidgetMode;
                }
            }
            return UE::Widget::WM_None;
        }

        virtual FVector GetWidgetLocation() const override
        {
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                if (Pinned->IsExternalSourcePlacementActive())
                {
                    return Pinned->GetExternalSourceWidgetLocation();
                }
            }
            return FEditorViewportClient::GetWidgetLocation();
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
        UE::Widget::EWidgetMode ExternalSourceWidgetMode = UE::Widget::WM_Translate;
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
    WrinkleSuppressionCoverageService = InArgs._WrinkleSuppressionCoverageService;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
    PreviewCommitCoordinator = InArgs._PreviewCommitCoordinator;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    InputToolsHost = MakeUnique<FDWCEditorInteractiveToolsHost>(PreviewScene.Get(), this);
    InitializePreviewSession();
    SEditorViewport::Construct(SEditorViewport::FArguments());
    RefreshPreview();
}

SWetClothingTransparencyPreviewViewport::SWetClothingTransparencyPreviewViewport() = default;

SWetClothingTransparencyPreviewViewport::~SWetClothingTransparencyPreviewViewport()
{
    PreviewCommitLifetime.Revoke();
    ClearMaterialHoverLayer();
    ReleaseHoverAuxiliaryResources();
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
    TransparencyPreviewHandle.Reset();
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
    for (TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ExternalSourcePreviewComponents)
    {
        Collector.AddReferencedObject(Pair.Value);
    }
    Collector.AddReferencedObject(BrushCursorComponent);
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetTransparencyPreviewTexture() const
{
    return TransparencyPreviewHandle.IsValid()
        ? TransparencyPreviewHandle->GetTexture()
        : nullptr;
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetVisualizationPreviewTexture() const
{
    return GetTransparencyPreviewTexture();
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetWrinkleCoverageTexture() const
{
    return WrinkleSuppressionDependency.ResolveTexture();
}

void SWetClothingTransparencyPreviewViewport::RefreshWrinkleSuppressionDependency()
{
    WrinkleSuppressionDependency = WrinkleSuppressionCoverageService.IsValid()
        ? WrinkleSuppressionCoverageService->ResolveDependency(
            WetClothingAsset.Get(), SelectedMaterialSlotIndex)
        : FDWCWrinkleSuppressionDependencySnapshot();
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetHoverBaselineTexture() const
{
    return HoverBaselinePreviewHandle.IsValid()
        ? HoverBaselinePreviewHandle->GetTexture()
        : nullptr;
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetHoverIslandMaskTexture() const
{
    return HoverIslandMaskPreviewHandle.IsValid()
        ? HoverIslandMaskPreviewHandle->GetTexture()
        : nullptr;
}

bool SWetClothingTransparencyPreviewViewport::EnsureHoverBaselineTexture()
{
    if (HoverBaselinePreviewHandle.IsValid())
    {
        return GetHoverBaselineTexture() != nullptr;
    }
    if (!TextureWorkspace.IsValid() || !AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const int32 PixelCount = AutoBakePreviewResult->Resolution.X * AutoBakePreviewResult->Resolution.Y;
    if (PixelCount <= 0 || AutoBakePreviewResult->InnerColorBuffer.Num() != PixelCount ||
        AutoBakePreviewResult->AutoAlphaBuffer.Num() != PixelCount)
    {
        return false;
    }

    TArray<FColor> BaselinePixels = AutoBakePreviewResult->InnerColorBuffer;
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        BaselinePixels[PixelIndex].A = AutoBakePreviewResult->AutoAlphaBuffer[PixelIndex];
    }
    const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap
        ? TA_Wrap
        : TA_Clamp;
    HoverBaselinePreviewHandle = TextureWorkspace->TransferBGRA8AndAcquireLease(
        MakeTransparencyTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::TransparencyHoverBaseline,
            SelectedMaterialSlotIndex,
            SelectedLayerGuid),
        MakeTransparencyDescriptor(AutoBakePreviewResult->Resolution, Address),
        MoveTemp(BaselinePixels),
        EDWCEditorTextureUploadPriority::Interactive);
    if (HoverBaselinePreviewHandle.IsValid())
    {
        ++HoverBaselineBuildCount;
    }
    return GetHoverBaselineTexture() != nullptr;
}

bool SWetClothingTransparencyPreviewViewport::EnsureHoverIslandMaskTexture(const int32 UVIslandID)
{
    if (UVIslandID == INDEX_NONE || !TextureWorkspace.IsValid() || !AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    if (HoverIslandMaskID == UVIslandID && HoverIslandMaskPreviewHandle.IsValid())
    {
        return GetHoverIslandMaskTexture() != nullptr;
    }

    const int32 PixelCount = AutoBakePreviewResult->Resolution.X * AutoBakePreviewResult->Resolution.Y;
    if (PixelCount <= 0 || AutoBakePreviewResult->OuterIslandIDBuffer.Num() != PixelCount)
    {
        return false;
    }
    TArray<uint8> IslandMask;
    IslandMask.SetNumZeroed(PixelCount);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (FDWCTransparencySourcePayload::MatchesOuterIslandID(
                AutoBakePreviewResult->OuterIslandIDBuffer[PixelIndex],
                UVIslandID))
        {
            IslandMask[PixelIndex] = OuterEdgeFeatherBuffer.IsValidIndex(PixelIndex)
                ? OuterEdgeFeatherBuffer[PixelIndex]
                : 255;
        }
    }

    if (HoverIslandMaskPreviewHandle.IsValid())
    {
        TextureWorkspace->Discard(HoverIslandMaskPreviewHandle);
        HoverIslandMaskPreviewHandle.Reset();
    }
    const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap
        ? TA_Wrap
        : TA_Clamp;
    HoverIslandMaskPreviewHandle = TextureWorkspace->TransferG8AndAcquireLease(
        MakeTransparencyTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::TransparencyHoverIslandMask,
            SelectedMaterialSlotIndex,
            SelectedLayerGuid),
        MakeTransparencyMaskDescriptor(AutoBakePreviewResult->Resolution, Address),
        MoveTemp(IslandMask),
        EDWCEditorTextureUploadPriority::Interactive);
    HoverIslandMaskID = HoverIslandMaskPreviewHandle.IsValid() ? UVIslandID : INDEX_NONE;
    if (HoverIslandMaskPreviewHandle.IsValid())
    {
        ++HoverIslandMaskBuildCount;
    }
    return GetHoverIslandMaskTexture() != nullptr;
}

void SWetClothingTransparencyPreviewViewport::ReleaseHoverAuxiliaryResources()
{
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Discard(HoverBaselinePreviewHandle);
        TextureWorkspace->Discard(HoverIslandMaskPreviewHandle);
    }
    HoverBaselinePreviewHandle.Reset();
    HoverIslandMaskPreviewHandle.Reset();
    HoverIslandMaskID = INDEX_NONE;
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

void SWetClothingTransparencyPreviewViewport::InvalidateFullSourceLayout()
{
    // Source component selection is structural scene state. Parameter refreshes
    // intentionally do not rebuild FullBlueprint preview, so make that boundary
    // explicit for Type 2/3 source add, remove, and target changes.
    if (!bPreviewSuspended && PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint)
    {
        RefreshPreview();
    }
}

void SWetClothingTransparencyPreviewViewport::SetExternalSourcePlacementSelection(const FGuid& SourceGuid)
{
    SelectedExternalSourceGuid = SourceGuid;
    if (ViewportClient.IsValid() && SourceGuid.IsValid())
    {
        ViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetExternalSourceTransformCommittedDelegate(
    FDWCTransparencyExternalSourceTransformCommitted InDelegate)
{
    ExternalSourceTransformCommitted = MoveTemp(InDelegate);
}

bool SWetClothingTransparencyPreviewViewport::IsExternalSourcePlacementActive() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return !bPreviewSuspended &&
        PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint &&
        Layer != nullptr &&
        Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh &&
        !bSurfacePaintingEnabled &&
        SelectedExternalSourceGuid.IsValid() &&
        ExternalSourcePreviewComponents.Contains(SelectedExternalSourceGuid);
}

FVector SWetClothingTransparencyPreviewViewport::GetExternalSourceWidgetLocation() const
{
    if (const TObjectPtr<USkeletalMeshComponent>* Component =
            ExternalSourcePreviewComponents.Find(SelectedExternalSourceGuid))
    {
        if (*Component != nullptr)
        {
            return (*Component)->GetComponentLocation();
        }
    }
    return FVector::ZeroVector;
}

bool SWetClothingTransparencyPreviewViewport::ApplyExternalSourceWidgetDelta(
    const EAxisList::Type,
    const FVector& Drag,
    const FRotator& Rot,
    const FVector&)
{
    if (!IsExternalSourcePlacementActive())
    {
        return false;
    }
    TObjectPtr<USkeletalMeshComponent>* Component =
        ExternalSourcePreviewComponents.Find(SelectedExternalSourceGuid);
    if (Component == nullptr || *Component == nullptr)
    {
        return false;
    }

    FTransform Transform = (*Component)->GetComponentTransform();
    Transform.AddToTranslation(Drag);
    if (!Rot.IsNearlyZero())
    {
        Transform.ConcatenateRotation(Rot.Quaternion());
        Transform.NormalizeRotation();
    }
    (*Component)->SetWorldTransform(Transform);
    (*Component)->MarkRenderTransformDirty();
    InvalidatePreviewViewport();
    return true;
}

void SWetClothingTransparencyPreviewViewport::BeginExternalSourceTransformInteraction()
{
    bExternalSourceTransformInteractionActive = IsExternalSourcePlacementActive();
}

void SWetClothingTransparencyPreviewViewport::FinishExternalSourceTransformInteraction()
{
    if (!bExternalSourceTransformInteractionActive)
    {
        return;
    }
    bExternalSourceTransformInteractionActive = false;
    if (const TObjectPtr<USkeletalMeshComponent>* Component =
            ExternalSourcePreviewComponents.Find(SelectedExternalSourceGuid))
    {
        if (*Component != nullptr && ExternalSourceTransformCommitted.IsBound())
        {
            ExternalSourceTransformCommitted.Execute(
                SelectedExternalSourceGuid,
                (*Component)->GetComponentTransform());
        }
    }
}

bool SWetClothingTransparencyPreviewViewport::NudgeExternalSourcePlacement(
    const FKey& Key,
    const bool bLargeStep,
    const bool bRotate)
{
    if (!IsExternalSourcePlacementActive())
    {
        return false;
    }

    constexpr float SmallTranslationStep = 1.0f;
    constexpr float LargeTranslationStep = 10.0f;
    constexpr float SmallRotationStep = 1.0f;
    constexpr float LargeRotationStep = 10.0f;
    const float Step = bLargeStep
        ? (bRotate ? LargeRotationStep : LargeTranslationStep)
        : (bRotate ? SmallRotationStep : SmallTranslationStep);
    FVector Translation = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    if (Key == EKeys::Left)
    {
        bRotate ? Rotation.Yaw = -Step : Translation.Y = -Step;
    }
    else if (Key == EKeys::Right)
    {
        bRotate ? Rotation.Yaw = Step : Translation.Y = Step;
    }
    else if (Key == EKeys::Up)
    {
        bRotate ? Rotation.Pitch = Step : Translation.Z = Step;
    }
    else if (Key == EKeys::Down)
    {
        bRotate ? Rotation.Pitch = -Step : Translation.Z = -Step;
    }
    else if (Key == EKeys::PageUp)
    {
        bRotate ? Rotation.Roll = Step : Translation.X = Step;
    }
    else if (Key == EKeys::PageDown)
    {
        bRotate ? Rotation.Roll = -Step : Translation.X = -Step;
    }
    else
    {
        return false;
    }

    if (!ApplyExternalSourceWidgetDelta(EAxisList::None, Translation, Rotation, FVector::ZeroVector))
    {
        return false;
    }
    if (const TObjectPtr<USkeletalMeshComponent>* Component =
            ExternalSourcePreviewComponents.Find(SelectedExternalSourceGuid))
    {
        if (*Component != nullptr && ExternalSourceTransformCommitted.IsBound())
        {
            ExternalSourceTransformCommitted.Execute(
                SelectedExternalSourceGuid,
                (*Component)->GetComponentTransform());
        }
    }
    return true;
}

void SWetClothingTransparencyPreviewViewport::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
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
    const bool bHadPendingAlphaWork = PendingAlphaIncrementalTicket.IsValid() || !PendingAlphaCommands.IsEmpty();
    const bool bHadPendingRevealWork = PendingRevealColorIncrementalTicket.IsValid() ||
        !PendingRevealColorCommands.IsEmpty();
    bool bCanceledAuthoringInteraction = false;
    if (const TSharedPtr<FDWCTransparencyAuthoringController> Controller = AuthoringController.Pin())
    {
        bCanceledAuthoringInteraction = Controller->CancelActiveInteraction(false);
    }
    CancelAlphaIncrementalWork(false);
    CancelRevealColorIncrementalWork(false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::Alpha, false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::RevealColor, false);
    AlphaPreviewRecovery.Suspend();
    RevealColorPreviewRecovery.Suspend();
    PreviewTextureRecovery.Suspend();
    if (bCanceledAuthoringInteraction || bHadPendingAlphaWork || bHadPendingRevealWork)
    {
        // A suspended editor must not retain a partially committed derived
        // layer. Resume reconstructs both layers from their authoritative
        // WCA stroke histories.
        ManualAlphaTileStore.Reset();
        if (AutoBakePreviewResult.IsValid())
        {
            ManualAlphaTileStore.Initialize(AutoBakePreviewResult->Resolution);
        }
        RevealColorTileStore.Reset();
        if (AutoBakePreviewResult.IsValid())
        {
            RevealColorTileStore.Initialize(AutoBakePreviewResult->Resolution);
        }
        bManualOverridesRequireWorkerRebuild = true;
        bRevealColorRequiresWorkerRebuild = true;
        InvalidatePreviewContent(true);
    }
    ClearSurfaceHover();
    ClearMaterialHoverLayer();
    ReleaseHoverAuxiliaryResources();

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

    if (PreviewOrchestrator)
    {
        PreviewOrchestrator->ClearAllLiveLayers();
    }
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Discard(TransparencyPreviewHandle);
    }
    TransparencyPreviewHandle.Reset();
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
    PreviewCommitLifetime.Resume();
    AlphaPreviewRecovery.Resume(bManualOverridesRequireWorkerRebuild);
    RevealColorPreviewRecovery.Resume(bRevealColorRequiresWorkerRebuild);
    PreviewTextureRecovery.Resume(AutoBakePreviewResult.IsValid());
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
    EDWCTransparencyPaintTarget InPaintTarget,
    const bool bInSurfacePaintingEnabled)
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
    const bool bInputEnabledChanged = bSurfacePaintingEnabled != bInSurfacePaintingEnabled;
    const bool bTopologyChanged = bMaterialSlotChanged || bUVChannelChanged;
    const bool bContextChanged =
        bTopologyChanged || bLayerChanged || bAddressModeChanged || bPaintTargetChanged || bInputEnabledChanged;
    if (!bContextChanged)
    {
        return;
    }

    ClearMaterialHoverLayer();
    if (bTopologyChanged || bLayerChanged || bAddressModeChanged)
    {
        ReleaseHoverAuxiliaryResources();
    }

    InvalidatePreviewContent(true);

    if (InputToolsHost)
    {
        InputToolsHost->CancelActiveInteraction();
    }

    SelectedLayerGuid = InLayerGuid;
    SelectedMaterialSlotIndex = InMaterialSlotIndex;
    SelectedUVChannelIndex = InUVChannelIndex;
    SelectedUVAddressMode = InAddressMode;
    RefreshWrinkleSuppressionDependency();
    bTransparencyPaintingEnabled = bNewAlphaPaintingEnabled;
    bRevealColorPaintingEnabled = bNewRevealColorPaintingEnabled;
    bSurfacePaintingEnabled = bInSurfacePaintingEnabled;
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
        if (PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint && PreviewActor != nullptr)
        {
            RefreshExistingFullBlueprintPreviewMaterials();
            RebuildHitTriangles();
            CurrentSurfaceHit = FDWCTransparencySurfaceHit();
            ClearBrushCursor();
            InvalidatePreviewViewport();
            return;
        }

        // Reveal Color editing forces TargetMeshOnly, but it still needs the
        // selected slot's preview material and hit-test topology to be
        // refreshed.  The old early return skipped both when the slot changed,
        // leaving the working map alive in memory while the mesh continued to
        // render the previous/source material.  Refresh only the existing
        // target component here; rebuilding the whole preview would also
        // discard the interactive state and needlessly recreate the scene.
        if (bForcedTargetMeshPreview)
        {
            if (TargetMeshPreviewComponent != nullptr)
            {
                ConfigurePreviewMeshComponent(TargetMeshPreviewComponent);
                RebuildHitTriangles();
                CurrentSurfaceHit = FDWCTransparencySurfaceHit();
                ApplyRevealColorPaintTargetVisibility();
                ClearBrushCursor();
                InvalidatePreviewViewport();
                return;
            }

            // The target component may not exist during initial construction.
            // Fall back to the normal preview build so the next context push
            // starts with a valid component instead of silently doing nothing.
            RefreshPreview();
            return;
        }

        RefreshPreview();
        return;
    }
    if (bAddressModeChanged && TransparencyPreviewHandle.IsValid() && TextureWorkspace.IsValid())
    {
        const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
        TextureWorkspace->RecreateWithAddressMode(TransparencyPreviewHandle, Address, Address);
    }
    if ((bPaintTargetChanged || bLayerChanged) && TargetMeshPreviewComponent != nullptr)
    {
        // A Stage 2/3 transition can keep the same material slot while the
        // preview layer and input target change. Re-apply the existing preview
        // material so a slot that was still showing its source material cannot
        // miss the newly selected editor overlay.
        ConfigurePreviewMeshComponent(TargetMeshPreviewComponent);
    }
    if (bPaintTargetChanged || bLayerChanged)
    {
        // A stroke is scoped to one layer and paint target. Clear the old
        // hover state before exposing the new target so a stale capture or
        // cursor cannot survive a Stage 2/3 transition.
        CurrentSurfaceHit = FDWCTransparencySurfaceHit();
    }
    ApplyRevealColorPaintTargetVisibility();
    UpdateMaterialHoverLayer();
    RefreshBrushCursor();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ApplyTransparencyPreviewSettings(
    const FDWCTransparencyPreviewSettings& InSettings)
{
    const float NewTransparencyStrength = FMath::Max(0.0f, InSettings.TransparencyStrength);
    const float NewSuppressionStrength = FMath::Clamp(InSettings.WrinkleSuppressionStrength, 0.0f, 5.0f);
    const float NewMaskThreshold = FMath::Clamp(InSettings.WrinkleMaskThreshold, 0.0f, 1.0f);
    const float NewMaskSoftness = FMath::Clamp(InSettings.WrinkleMaskSoftness, 0.0f, 1.0f);
    const float NewRevealNormalStrength = FMath::Clamp(InSettings.RevealNormalStrength, 0.0f, 4.0f);
    const bool bSettingsChanged =
        !FMath::IsNearlyEqual(TransparencyPreviewStrength, NewTransparencyStrength) ||
        !FMath::IsNearlyEqual(WrinkleSuppressionStrength, NewSuppressionStrength) ||
        !FMath::IsNearlyEqual(WrinkleMaskThreshold, NewMaskThreshold) ||
        !FMath::IsNearlyEqual(WrinkleMaskSoftness, NewMaskSoftness) ||
        !FMath::IsNearlyEqual(RevealNormalStrength, NewRevealNormalStrength) ||
        bShowRevealNormal != InSettings.bShowRevealNormal ||
        RequestedRevealNormalSource != InSettings.RevealNormalSource;
    if (!bSettingsChanged)
    {
        return;
    }

    TransparencyPreviewStrength = NewTransparencyStrength;
    WrinkleSuppressionStrength = NewSuppressionStrength;
    WrinkleMaskThreshold = NewMaskThreshold;
    WrinkleMaskSoftness = NewMaskSoftness;
    RevealNormalStrength = NewRevealNormalStrength;
    bShowRevealNormal = InSettings.bShowRevealNormal;
    RequestedRevealNormalSource = InSettings.RevealNormalSource;
    SchedulePreviewSettingsApply();
}

FText SWetClothingTransparencyPreviewViewport::GetRevealNormalPreviewSourceStatusText() const
{
    if (!bWorkingRevealNormalAvailable && !bBakedRevealNormalAvailable)
    {
        return NSLOCTEXT("DWCTransparency", "RevealNormalPreviewUnavailable", "Preview Source: unavailable");
    }

    const bool bFellBack = RequestedRevealNormalSource != EffectiveRevealNormalSource;
    const FText Source = EffectiveRevealNormalSource == EDWCTransparencyRevealNormalPreviewSource::Working
        ? NSLOCTEXT("DWCTransparency", "RevealNormalPreviewWorking", "Working")
        : NSLOCTEXT("DWCTransparency", "RevealNormalPreviewBaked", "Baked");
    return bFellBack
        ? FText::Format(
            NSLOCTEXT("DWCTransparency", "RevealNormalPreviewFallback", "Preview Source: {0} (requested source unavailable)"),
            Source)
        : FText::Format(
            NSLOCTEXT("DWCTransparency", "RevealNormalPreviewSource", "Preview Source: {0}"),
            Source);
}

void SWetClothingTransparencyPreviewViewport::SchedulePreviewSettingsApply()
{
    if (bPreviewSettingsApplyScheduled)
    {
        return;
    }

    bPreviewSettingsApplyScheduled = true;
    RegisterActiveTimer(
        0.0f,
        FWidgetActiveTimerDelegate::CreateSP(
            this,
            &SWetClothingTransparencyPreviewViewport::HandlePreviewSettingsApply));
}

EActiveTimerReturnType SWetClothingTransparencyPreviewViewport::HandlePreviewSettingsApply(
    double,
    float)
{
    bPreviewSettingsApplyScheduled = false;
    if (bPreviewSuspended)
    {
        return EActiveTimerReturnType::Stop;
    }

    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
    return EActiveTimerReturnType::Stop;
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
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshWrinkleSuppressionPreview()
{
    RefreshWrinkleSuppressionDependency();
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
    UpdateMaterialHoverLayer();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshRevealColorCorrectionPreview()
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return;
    }

    InvalidatePreviewContent(true);
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    UpdateMaterialHoverLayer();
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
    // selected by the edit context may control the live tool.
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

    PaintSettings = NewSettings;
    UpdateMaterialHoverLayer();
    RefreshBrushCursor();
}

void SWetClothingTransparencyPreviewViewport::SetVisualizationMode(const EDWCTransparencyVisualizationMode InMode)
{
    if (VisualizationMode == InMode)
    {
        return;
    }

    ClearMaterialHoverLayer();
    VisualizationMode = InMode;
    InvalidatePreviewContent(true);
    RefreshDeferredFinalPreviewBuffers();
    RebuildTransparencyPreviewTexture();
    RefreshBrushCursor();
    ApplyTransparencyPreviewParameters();
    UpdateMaterialHoverLayer();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetAutoBakePreviewResult(
    TSharedPtr<const FDWCTransparencySourcePayload> InResult)
{
    if (AutoBakePreviewResult == InResult)
    {
        // Stage/context refreshes may push the same immutable working map while
        // its asynchronous visualization is still queued. Do not reset retry
        // state or supersede that job. Only recover if no request owns it.
        if (GetTransparencyPreviewTexture() == nullptr &&
            !PendingPreviewTicket.IsValid() &&
            !PreviewTextureRecovery.RequiresFullRebuild() &&
            !bPreviewSuspended &&
            AutoBakePreviewResult.IsValid())
        {
            RebuildTransparencyPreviewTexture();
        }
        return;
    }

    ClearMaterialHoverLayer();
    ReleaseHoverAuxiliaryResources();
    AutoBakePreviewResult = MoveTemp(InResult);
    PreviewTextureRecovery.Reset();
    AlphaPreviewRecovery.Reset();
    RevealColorPreviewRecovery.Reset();
    InvalidatePreviewContent(true);
    RevealColorTileStore.Reset();
    if (AutoBakePreviewResult.IsValid())
    {
        RevealColorTileStore.Initialize(AutoBakePreviewResult->Resolution);
    }
    bRevealColorRequiresWorkerRebuild = false;
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        // The sparse store owns authored (pre-correction) color only. Rebuild
        // it from serialized strokes; the committed checkpoint already has
        // metallic correction baked in and must not be fed back into Stage 3.
        bRevealColorRequiresWorkerRebuild = Layer->RevealColorPaintStrokes.ContainsByPredicate(
            [this](const FDWCTransparencyRevealColorStroke& Stroke)
            {
                return Stroke.bEnabled &&
                    Stroke.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                    !Stroke.Samples.IsEmpty();
            });
    }
    if (bRevealColorRequiresWorkerRebuild)
    {
        RevealColorPreviewRecovery.Invalidate(
            EDWCEditorPreviewInvalidationReason::AuthoredDataChanged);
    }
    bOuterEdgeFeatherPreviewDirty = true;
    RefreshDeferredFinalPreviewBuffers();
    RebuildManualOverridesFromStrokes();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    UpdateMaterialHoverLayer();
    RefreshBrushCursor();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ClearAutoBakePreviewResult()
{
    if (!AutoBakePreviewResult.IsValid() &&
        OuterEdgeFeatherBuffer.IsEmpty() &&
        !ManualAlphaTileStore.IsValid() &&
        !RevealColorTileStore.IsValid() &&
        GetTransparencyPreviewTexture() == nullptr &&
        HoverLayerMaterialSlotIndex == INDEX_NONE)
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
    AutoBakePreviewResult.Reset();
    ClearMaterialHoverLayer();
    ReleaseHoverAuxiliaryResources();
    CancelAlphaIncrementalWork(false);
    CancelRevealColorIncrementalWork(false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::Alpha, false);
    CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget::RevealColor, false);
    InvalidatePreviewContent(true);
    PendingPreviewTicket = {};
    PendingPreviewContentRevision = 0;
    PreviewTextureRecovery.Reset();
    AlphaPreviewRecovery.Reset();
    RevealColorPreviewRecovery.Reset();
    OuterEdgeFeatherBuffer.Empty();
    bOuterEdgeFeatherPreviewDirty = false;
    ManualAlphaTileStore.Reset();
    RevealColorTileStore.Reset();
    bManualOverridesRequireWorkerRebuild = false;
    bRevealColorRequiresWorkerRebuild = false;
    CurrentSurfaceHit = FDWCTransparencySurfaceHit();
    ClearBrushCursor();
    TransparencyPreviewHandle.Reset();
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
        for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ExternalSourcePreviewComponents)
        {
            if (Pair.Value != nullptr)
            {
                PreviewScene->RemoveComponent(Pair.Value);
            }
        }
        if (PreviewActor != nullptr && PreviewScene->GetWorld() != nullptr)
        {
            PreviewScene->GetWorld()->DestroyActor(PreviewActor);
        }
    }

    TargetMeshPreviewComponent = nullptr;
    PreviewActor = nullptr;
    PreviewMeshComponents.Reset();
    ExternalSourcePreviewComponents.Reset();
    bExternalSourceTransformInteractionActive = false;
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

    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        if (MeshComponent != nullptr)
        {
            ApplyPreviewMaterials(MeshComponent);
        }
    }
    if (MaterialSlotIndex == SelectedMaterialSlotIndex)
    {
        ApplyTransparencyPreviewParameters();
    }
    ApplyRevealColorPaintTargetVisibility();
    RefreshBrushCursor();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::TickPreviewMaterialCompilations()
{
    if (PreviewSession && !bPreviewSuspended)
    {
        PreviewSession->TickPendingMaterialCompilations();
    }
    RetryPreviewTextureRebuildIfNeeded();
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
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr || !PreviewScene.IsValid() || PreviewScene->GetWorld() == nullptr)
    {
        return;
    }

    if (Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        BuildFullExternalMeshPreview();
        return;
    }

    const FWetClothingTransparencyBlueprintSource& BlueprintSource = Layer->BlueprintSource;
    TSubclassOf<AActor> BlueprintClass = BlueprintSource.BlueprintClass.LoadSynchronous();
    if (BlueprintClass == nullptr || !BlueprintSource.TargetComponent.IsBound())
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

    TSet<FName> VisibleSourceComponents;
    for (const FWetClothingTransparencyBlueprintComponentBinding& Source : BlueprintSource.SourcePriority)
    {
        if (Source.IsBound())
        {
            VisibleSourceComponents.Add(Source.ComponentName);
        }
    }

    TArray<USkeletalMeshComponent*> MeshComponents;
    PreviewActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
    for (USkeletalMeshComponent* MeshComponent : MeshComponents)
    {
        if (MeshComponent == nullptr)
        {
            continue;
        }

        const FName ComponentName = MeshComponent->GetFName();
        const bool bIsTarget = ComponentName == BlueprintSource.TargetComponent.ComponentName;
        const bool bIsSelectedSource = VisibleSourceComponents.Contains(ComponentName);
        if (!bIsTarget && !bIsSelectedSource)
        {
            MeshComponent->SetVisibility(false, true);
            continue;
        }

        MeshComponent->SetVisibility(true, true);
        MeshComponent->SetForcedLOD(TransparencyViewportForceRenderLOD0);
        if (bIsTarget)
        {
            TargetMeshPreviewComponent = MeshComponent;
            if (Asset->GetDWCSkeletalMesh() != nullptr &&
                MeshComponent->GetSkeletalMeshAsset() != Asset->GetDWCSkeletalMesh())
            {
                MeshComponent->SetSkeletalMeshAsset(Asset->GetDWCSkeletalMesh());
            }
            ConfigurePreviewMeshComponent(MeshComponent);
        }
        else
        {
            PreviewMeshComponents.AddUnique(MeshComponent);
        }
    }

    if (TargetMeshPreviewComponent == nullptr)
    {
        BuildTargetMeshPreview();
        return;
    }

    BrushCursorComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    BrushCursorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushCursorComponent->SetCastShadow(false);
    BrushCursorComponent->SetReceivesDecals(false);
    BrushCursorComponent->SetDepthPriorityGroup(SDPG_Foreground);
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);
    EnsureBrushCursor();
    RebuildHitTriangles();
    ApplyRevealColorPaintTargetVisibility();

    if (USkeletalMeshComponent* FocusMesh = FindFocusMeshComponent())
    {
        const FBoxSphereBounds Bounds = FocusMesh->CalcBounds(FocusMesh->GetComponentTransform());
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
}

void SWetClothingTransparencyPreviewViewport::BuildFullExternalMeshPreview()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr || !PreviewScene.IsValid())
    {
        return;
    }

    // The target keeps the normal DWC preview material. Every Type 3 source
    // stays on its original material and is only visual placement context.
    BuildTargetMeshPreview();
    for (const FWetClothingTransparencyExternalMeshEntry& Entry :
         Layer->ExternalMeshSource.SourcePriority)
    {
        if (!Entry.SourceGuid.IsValid() || Entry.SkeletalMesh == nullptr)
        {
            continue;
        }

        USkeletalMeshComponent* SourceComponent = NewObject<USkeletalMeshComponent>(
            GetTransientPackage(),
            NAME_None,
            RF_Transient);
        SourceComponent->SetMobility(EComponentMobility::Movable);
        SourceComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SourceComponent->SetSkeletalMeshAsset(Entry.SkeletalMesh);
        SourceComponent->SetForcedLOD(TransparencyViewportForceRenderLOD0);
        SourceComponent->SetCastShadow(false);
        PreviewScene->AddComponent(SourceComponent, Entry.BakeTransform);
        PreviewMeshComponents.AddUnique(SourceComponent);
        ExternalSourcePreviewComponents.Add(Entry.SourceGuid, SourceComponent);
    }

    if (!ExternalSourcePreviewComponents.Contains(SelectedExternalSourceGuid) &&
        !Layer->ExternalMeshSource.SourcePriority.IsEmpty())
    {
        SelectedExternalSourceGuid = Layer->ExternalMeshSource.SourcePriority[0].SourceGuid;
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

    if (MeshComponent != TargetMeshPreviewComponent)
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
        if (MeshComponent->GetMaterial(SlotState.MaterialSlotIndex) != MaterialToApply)
        {
            MeshComponent->SetMaterial(SlotState.MaterialSlotIndex, MaterialToApply);
        }
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

void SWetClothingTransparencyPreviewViewport::ClearMaterialHoverLayer()
{
    if (PreviewOrchestrator && HoverLayerMaterialSlotIndex != INDEX_NONE)
    {
        PreviewOrchestrator->ClearLiveLayer(
            HoverLayerMaterialSlotIndex,
            EDWCEditorPreviewLayerKind::LiveTransparencyHover);
    }
    HoverLayerMaterialSlotIndex = INDEX_NONE;
}

void SWetClothingTransparencyPreviewViewport::UpdateMaterialHoverLayer()
{
    const bool bRevealTarget = bRevealColorPaintingEnabled && PaintSettings.bRevealColorPaint;
    const bool bAlphaTarget = bTransparencyPaintingEnabled && !PaintSettings.bRevealColorPaint;
    const bool bVisualizationSupported = bRevealTarget
        ? VisualizationMode == EDWCTransparencyVisualizationMode::InnerColor
        : (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
           VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha);
    const bool bResultMatchesSelection = AutoBakePreviewResult.IsValid() &&
        AutoBakePreviewResult->LayerGuid == SelectedLayerGuid &&
        AutoBakePreviewResult->MaterialSlotIndex == SelectedMaterialSlotIndex &&
        AutoBakePreviewResult->UVChannelIndex == SelectedUVChannelIndex;
    if (bPreviewSuspended || !PreviewOrchestrator || !bSurfacePaintingEnabled || !PaintSettings.bEnabled ||
        (!bRevealTarget && !bAlphaTarget) || !bVisualizationSupported ||
        IsAuthoringInteractionActive() || !CurrentSurfaceHit.bHit ||
        PreviewMode != EWetClothingTransparencyPreviewMode::TargetMeshOnly ||
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0 ||
        !bResultMatchesSelection || GetTransparencyPreviewTexture() == nullptr)
    {
        ClearMaterialHoverLayer();
        return;
    }

    const bool bWrap = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    if (Width <= 0 || Height <= 0)
    {
        ClearMaterialHoverLayer();
        return;
    }

    const int32 HoverIslandID = AutoBakePreviewResult->ResolveOuterIslandIDAtUV(
        CurrentSurfaceHit.UV,
        CurrentSurfaceHit.UVIslandID,
        bWrap);
    if (!EnsureHoverIslandMaskTexture(HoverIslandID))
    {
        ClearMaterialHoverLayer();
        return;
    }

    EDWCTransparencyMaterialHoverOperation Operation =
        EDWCTransparencyMaterialHoverOperation::PaintOrApply;
    float TargetAlpha = PaintSettings.TargetAlpha;
    bool bNeedsBaseline = false;
    if (bRevealTarget)
    {
        switch (PaintSettings.RevealColorMode)
        {
        case EDWCTransparencyRevealColorBrushMode::EraseToBase:
            Operation = EDWCTransparencyMaterialHoverOperation::Erase;
            bNeedsBaseline = true;
            break;
        case EDWCTransparencyRevealColorBrushMode::Smooth:
            Operation = EDWCTransparencyMaterialHoverOperation::Smooth;
            break;
        case EDWCTransparencyRevealColorBrushMode::Paint:
        default:
            break;
        }
    }
    else
    {
        switch (PaintSettings.Mode)
        {
        case EDWCTransparencyBrushMode::Apply:
            TargetAlpha = 1.0f;
            break;
        case EDWCTransparencyBrushMode::Erase:
            TargetAlpha = 0.0f;
            Operation = EDWCTransparencyMaterialHoverOperation::Erase;
            break;
        case EDWCTransparencyBrushMode::ResetToAuto:
            Operation = EDWCTransparencyMaterialHoverOperation::Reset;
            bNeedsBaseline = true;
            break;
        case EDWCTransparencyBrushMode::Smooth:
            Operation = EDWCTransparencyMaterialHoverOperation::Smooth;
            break;
        case EDWCTransparencyBrushMode::SetValue:
        default:
            break;
        }
    }

    if (bNeedsBaseline && !EnsureHoverBaselineTexture())
    {
        ClearMaterialHoverLayer();
        return;
    }

    FVector2D CenterUV = CurrentSurfaceHit.UV;
    if (bWrap)
    {
        CenterUV.X -= FMath::FloorToDouble(CenterUV.X);
        CenterUV.Y -= FMath::FloorToDouble(CenterUV.Y);
    }

    FDWCEditorPreviewLayer HoverLayer;
    HoverLayer.Kind = EDWCEditorPreviewLayerKind::LiveTransparencyHover;
    HoverLayer.MaterialSlotIndex = SelectedMaterialSlotIndex;
    HoverLayer.AuthoringRevision = PreviewContentRevision;
    HoverLayer.ResourceRevision = TransparencyPreviewHandle.IsValid()
        ? TransparencyPreviewHandle->GetContentRevision()
        : 0;
    HoverLayer.AddVector(
        DWCTransparencyPreviewMaterialParameters::HoverState0(),
        FLinearColor(
            static_cast<float>(CenterUV.X),
            static_cast<float>(CenterUV.Y),
            PaintSettings.RadiusUV,
            PaintSettings.Falloff));
    HoverLayer.AddVector(
        DWCTransparencyPreviewMaterialParameters::HoverState1(),
        FLinearColor(
            1.0f,
            PaintSettings.Strength,
            FMath::Clamp(TargetAlpha, 0.0f, 1.0f),
            static_cast<float>(Operation)));
    HoverLayer.AddVector(
        DWCTransparencyPreviewMaterialParameters::HoverColor(),
        PaintSettings.RevealColor);
    HoverLayer.AddVector(
        DWCTransparencyPreviewMaterialParameters::HoverTexelSize(),
        FLinearColor(1.0f / Width, 1.0f / Height, 0.0f, 0.0f));
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::HoverTarget(),
        static_cast<float>(bRevealTarget
            ? EDWCTransparencyMaterialHoverTarget::RevealColor
            : EDWCTransparencyMaterialHoverTarget::TransparencyAlpha));
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::HoverWrap(),
        bWrap ? 1.0f : 0.0f);
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::HoverVisualizationMode(),
        static_cast<float>(VisualizationMode));
    HoverLayer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::HoverBaselineMap(),
        bNeedsBaseline ? GetHoverBaselineTexture() : nullptr);
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseHoverBaselineMap(),
        bNeedsBaseline ? 1.0f : 0.0f);
    HoverLayer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap(),
        GetHoverIslandMaskTexture());
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap(),
        1.0f);

    if (HoverLayerMaterialSlotIndex != INDEX_NONE &&
        HoverLayerMaterialSlotIndex != SelectedMaterialSlotIndex)
    {
        PreviewOrchestrator->ClearLiveLayer(
            HoverLayerMaterialSlotIndex,
            EDWCEditorPreviewLayerKind::LiveTransparencyHover);
    }
    HoverLayerMaterialSlotIndex = SelectedMaterialSlotIndex;
    PreviewOrchestrator->SetLiveLayer(SelectedMaterialSlotIndex, MoveTemp(HoverLayer));
    ++HoverParameterUpdateCount;
    InvalidatePreviewViewport();
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
    UTexture2D* PreviewRevealSurfaceMap = nullptr;
    UTexture2D* SavedTransparencyMap = nullptr;
    UTexture2D* SavedRevealNormalMap = nullptr;
    if (bResultMatchesSelection && LayerData != nullptr)
    {
        PreviewRevealSurfaceMap = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
            *LayerData,
            EDWCTransparencyTempArtifactKind::BaseRevealSurface,
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                EDWCTransparencyTempArtifactKind::BaseRevealSurface,
                AutoBakePreviewResult->BuildSignature),
            true);
    }
    if (LayerData != nullptr)
    {
        if (const FWetClothingBakedTransparencyMap* BakedMap = LayerData->BakedMaps.FindByPredicate(
                [this](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                           Candidate.TransparencyMap != nullptr;
                }))
        {
            SavedTransparencyMap = BakedMap->TransparencyMap;
            SavedRevealNormalMap = BakedMap->HasRuntimeRevealNormalPayload()
                ? BakedMap->RevealNormalMap.Get()
                : nullptr;
        }
    }
    if (PreviewTransparencyMap == nullptr)
    {
        PreviewTransparencyMap = SavedTransparencyMap;
    }
    bWorkingRevealNormalAvailable = PreviewRevealSurfaceMap != nullptr;
    bBakedRevealNormalAvailable = SavedTransparencyMap != nullptr && SavedRevealNormalMap != nullptr;
    EffectiveRevealNormalSource = RequestedRevealNormalSource;
    if (EffectiveRevealNormalSource == EDWCTransparencyRevealNormalPreviewSource::Working &&
        !bWorkingRevealNormalAvailable && bBakedRevealNormalAvailable)
    {
        EffectiveRevealNormalSource = EDWCTransparencyRevealNormalPreviewSource::Baked;
    }
    else if (EffectiveRevealNormalSource == EDWCTransparencyRevealNormalPreviewSource::Baked &&
        !bBakedRevealNormalAvailable && bWorkingRevealNormalAvailable)
    {
        EffectiveRevealNormalSource = EDWCTransparencyRevealNormalPreviewSource::Working;
    }
    const bool bSupportsRevealPresentation =
        VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
        VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha ||
        VisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
        VisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalTexture ||
        VisualizationMode == EDWCTransparencyVisualizationMode::SourceCoverage;
    const bool bCanUseWorkingPresentation =
        CanUseMaterialDrivenPreviewPresentation() && bSupportsRevealPresentation;
    const bool bLayerEnablesRevealNormal = LayerData != nullptr &&
        LayerData->RequiresRuntimeRevealNormal() && bShowRevealNormal;
    const bool bUseSavedRuntimeRevealNormal = bSupportsRevealPresentation && bLayerEnablesRevealNormal &&
        EffectiveRevealNormalSource == EDWCTransparencyRevealNormalPreviewSource::Baked &&
        bBakedRevealNormalAvailable;
    const bool bUseWorkingRevealNormal = bCanUseWorkingPresentation && bLayerEnablesRevealNormal &&
        EffectiveRevealNormalSource == EDWCTransparencyRevealNormalPreviewSource::Working &&
        bWorkingRevealNormalAvailable;
    UTexture2D* EditorTransparencyMap = bUseSavedRuntimeRevealNormal
        ? nullptr
        : PreviewTransparencyMap;
    UTexture2D* WrinkleCoverageTexture = GetWrinkleCoverageTexture();
    const bool bUseCoverage = bShowSavedWrinkle && WrinkleCoverageTexture != nullptr &&
        (bCanUseWorkingPresentation || bUseSavedRuntimeRevealNormal ||
            VisualizationMode == EDWCTransparencyVisualizationMode::WrinkleSeparation);

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveTransparency;
    Layer.MaterialSlotIndex = SelectedMaterialSlotIndex;
    Layer.AddTexture(
        DWCWetMaterialParameters::TransparencyMap(),
        bUseSavedRuntimeRevealNormal ? SavedTransparencyMap : nullptr);
    Layer.AddScalar(
        DWCWetMaterialParameters::UseTransparencyMap(),
        bUseSavedRuntimeRevealNormal ? 1.0f : 0.0f);
    Layer.AddTexture(
        DWCWetMaterialParameters::RevealNormalMap(),
        bUseSavedRuntimeRevealNormal ? SavedRevealNormalMap : nullptr);
    Layer.AddScalar(
        DWCWetMaterialParameters::UseRevealNormalMap(),
        bUseSavedRuntimeRevealNormal ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCWetMaterialParameters::RevealNormalStrength(),
        bUseSavedRuntimeRevealNormal ? RevealNormalStrength : 0.0f);
    Layer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::TransparencyMap(),
        EditorTransparencyMap);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseTransparencyMap(),
        EditorTransparencyMap != nullptr ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::ShowInnerColor(),
        VisualizationMode == EDWCTransparencyVisualizationMode::InnerColor &&
                EditorTransparencyMap != nullptr
            ? 1.0f
            : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::TransparencyStrength(),
        bCanUseWorkingPresentation ? TransparencyPreviewStrength : 1.0f);
    Layer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::RevealSurfaceMap(),
        bUseWorkingRevealNormal ? PreviewRevealSurfaceMap : nullptr);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseRevealSurfaceMap(),
        bUseWorkingRevealNormal ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::RevealNormalStrength(),
        bUseWorkingRevealNormal ? RevealNormalStrength : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::ShowRevealNormal(),
        bUseWorkingRevealNormal ? 1.0f : 0.0f);
    Layer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::WrinkleCoverageMap(),
        bUseCoverage ? WrinkleCoverageTexture : nullptr);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseWrinkleCoverageMap(),
        bUseCoverage ? 1.0f : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(),
        bUseCoverage ? WrinkleSuppressionStrength : 0.0f);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::WrinkleMaskThreshold(),
        WrinkleMaskThreshold);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::WrinkleMaskSoftness(),
        WrinkleMaskSoftness);
    Layer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::VisualizationMode(),
        static_cast<float>(VisualizationMode));
    return Layer;
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
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
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
        }
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
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
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
        }
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
        OutInput.FallbackStrokes = Layer->RevealColorPaintStrokes;
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
    OutSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoBakePreviewResult->BaselineStrokeCount,
        0,
        Layer->EditableStrokes.Num());
    OutSnapshot.AuthoredStrokeCount = Layer->EditableStrokes.Num();
    for (int32 StrokeIndex = OutSnapshot.BaselineStrokeCount;
         StrokeIndex < Layer->EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Layer->EditableStrokes[StrokeIndex];
        OutSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.Samples.Num() : 0;
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
        OutSnapshot.FallbackStrokes = Layer->EditableStrokes;
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
            Input.PreviewTarget.Key = MakeTransparencyTextureKey(
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
                Input.AlphaStrokes = Layer->EditableStrokes;
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
                Input.RevealColorStrokes = Layer->RevealColorPaintStrokes;
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
        [WeakThis, Target, ExpectedSlot, ExpectedLayer, ExpectedEpoch, CommitToken, RequestRegions](
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
            Input.PreviewTarget.Key = MakeTransparencyTextureKey(
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
         FirstSequence, LastSequence, CommitToken](
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
            Input.PreviewTarget.Key = MakeTransparencyTextureKey(
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
         FirstSequence, LastSequence, CommitToken](
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
        TextureWorkspace->Discard(HoverIslandMaskPreviewHandle);
    }
    HoverIslandMaskPreviewHandle.Reset();
    HoverIslandMaskID = INDEX_NONE;
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
    Descriptor.MemoryEstimate = EstimateTransparencyVisualizationMemory(
        *AdmissionAutoResult,
        bRebuildRevealColor ? EmptyRevealColorTileStore : RevealColorTileStore,
        bRebuildManualOverrides
            ? GetTransparencyStrokeSnapshotBytes(AdmissionLayer->EditableStrokes, EmptyRevealColorStrokes)
            : ManualAlphaTileStore.GetAllocatedBytes(),
        OuterEdgeFeatherBuffer,
        bRebuildRevealColor ? AdmissionLayer->RevealColorPaintStrokes : EmptyRevealColorStrokes,
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
                    Input.RevealColorPaintStrokes = Layer->RevealColorPaintStrokes;
                }
                Input.BaselineStrokeCount = Input.SourcePayload->BaselineStrokeCount;
                Input.BaseRevealColor = Layer->ManualColorSource.BaseRevealColor;
                Input.MaterialSlotIndex = ExpectedSlotIndex;
                Input.UVChannelIndex = ExpectedUVChannelIndex;
            }

            OutPrepared.ActualMemoryEstimate = EstimateTransparencyVisualizationMemory(
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
        [WeakThis, ExpectedLayerGuid, ExpectedSlotIndex, AddressMode, SnapshotContentRevision, CommitToken](
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
                MakeTransparencyTextureKey(
                    Viewport->WetClothingAsset.Get(),
                    EDWCEditorTexturePurpose::TransparencyVisualization,
                    ExpectedSlotIndex,
                    ExpectedLayerGuid),
                MakeTransparencyDescriptor(ResultResolution, Address),
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
            InteractiveQueue.UsedBytes += Command.Stroke.Samples.GetAllocatedSize();
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
    OutCounters.Add({TEXT("Transparency hover island-mask builds"), HoverIslandMaskBuildCount, 0});
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
    HoverIslandMaskBuildCount = 0;
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
    PreviewTextureRecovery.ResetDiagnostics();
    AlphaPreviewRecovery.ResetDiagnostics();
    RevealColorPreviewRecovery.ResetDiagnostics();
}

#undef LOCTEXT_NAMESPACE
