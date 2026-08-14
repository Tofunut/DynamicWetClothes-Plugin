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

namespace
{
    constexpr int32 TransparencyViewportForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.

    void ConfigureStaticTransparencyPreviewPose(USkeletalMeshComponent* MeshComponent)
    {
        if (MeshComponent == nullptr)
        {
            return;
        }

        MeshComponent->SetForcedLOD(TransparencyViewportForceRenderLOD0);
        MeshComponent->SetEnableAnimation(false);
        MeshComponent->SetUpdateAnimationInEditor(false);
        MeshComponent->SetDisablePostProcessBlueprint(true);
        MeshComponent->SetUpdateClothInEditor(false);
        MeshComponent->SetForceRefPose(true);
        MeshComponent->SetComponentTickEnabled(false);
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent->SetSimulatePhysics(false);
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
            ShowWidget(false);
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
                Pinned->ProcessPendingViewportWork();
            }
        }

        virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
        {
            if (EventArgs.Key == EKeys::LeftMouseButton && EventArgs.Viewport != nullptr)
            {
                if (EventArgs.Event == IE_Pressed)
                {
                    bPlacementSelectionClickPending = true;
                    PlacementSelectionPressPosition = FIntPoint(
                        EventArgs.Viewport->GetMouseX(),
                        EventArgs.Viewport->GetMouseY());
                }
                else if (EventArgs.Event == IE_Released && bPlacementSelectionClickPending)
                {
                    const FIntPoint ReleasePosition(
                        EventArgs.Viewport->GetMouseX(),
                        EventArgs.Viewport->GetMouseY());
                    const bool bWasClick =
                        (ReleasePosition - PlacementSelectionPressPosition).SizeSquared() <
                        MOUSE_CLICK_DRAG_DELTA;
                    bPlacementSelectionClickPending = false;

                    // Let the base client finish its tracking/camera lifecycle first. Type 3
                    // selection is then resolved explicitly because a viewport backed by ITF
                    // can bypass the legacy ProcessClick path entirely.
                    const bool bBaseHandled = FEditorViewportClient::InputKey(EventArgs);
                    if (bWasClick)
                    {
                        if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned =
                                ViewportWidget.Pin())
                        {
                            FSceneViewFamilyContext ViewFamily(
                                FSceneViewFamily::ConstructionValues(
                                    EventArgs.Viewport,
                                    GetScene(),
                                    EngineShowFlags)
                                .SetRealtimeUpdate(IsRealtime()));
                            if (FSceneView* View = CalcSceneView(&ViewFamily))
                            {
                                const FViewportCursorLocation Cursor(
                                    View,
                                    this,
                                    ReleasePosition.X,
                                    ReleasePosition.Y);
                                const bool bCycle =
                                    EventArgs.Viewport->KeyState(EKeys::LeftAlt) ||
                                    EventArgs.Viewport->KeyState(EKeys::RightAlt);
                                if (Pinned->SelectType3PlacementAtRay(
                                        Cursor.GetOrigin(),
                                        Cursor.GetDirection(),
                                        bCycle))
                                {
                                    EventArgs.Viewport->InvalidateHitProxy();
                                    Invalidate();
                                    return true;
                                }
                            }
                        }
                    }
                    return bBaseHandled;
                }
            }
            if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed)
            {
                if (InputToolsHost != nullptr && InputToolsHost->CancelActiveInteraction())
                {
                    return true;
                }
            }
            if ((EventArgs.Event == IE_Pressed || EventArgs.Event == IE_Repeat))
            {
                if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                {
                    if (EventArgs.Event == IE_Pressed &&
                        Pinned->GetPlacementSelection().Type !=
                            EDWCTransparencyPlacementSelectionType::None)
                    {
                        if (EventArgs.Key == EKeys::F)
                        {
                            Pinned->FocusSelectedPlacement(false);
                            return true;
                        }
                    }
                    if (EventArgs.Event == IE_Pressed && EventArgs.Key == EKeys::Home)
                    {
                        Pinned->FocusType3Assembly(false);
                        return true;
                    }
                }
            }
            return FEditorViewportClient::InputKey(EventArgs);
        }

        void FocusOnMesh(const USkeletalMeshComponent* MeshComponent, bool bInstant)
        {
            if (MeshComponent == nullptr || MeshComponent->GetSkeletalMeshAsset() == nullptr)
            {
                return;
            }

            FocusOnBounds(
                MeshComponent->CalcBounds(MeshComponent->GetComponentTransform()),
                bInstant);
        }

        void FocusOnBounds(const FBoxSphereBounds& Bounds, bool bInstant)
        {
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
        bool bPlacementSelectionClickPending = false;
        FIntPoint PlacementSelectionPressPosition = FIntPoint::ZeroValue;
    };

    bool IntersectPlacementBounds(
        const FVector& RayOrigin,
        const FVector& RayDirection,
        const FBox& Bounds,
        double& OutDistance)
    {
        double NearDistance = 0.0;
        double FarDistance = TNumericLimits<double>::Max();
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double Direction = RayDirection[Axis];
            if (FMath::IsNearlyZero(Direction))
            {
                if (RayOrigin[Axis] < Bounds.Min[Axis] || RayOrigin[Axis] > Bounds.Max[Axis])
                {
                    return false;
                }
                continue;
            }
            double AxisNear = (Bounds.Min[Axis] - RayOrigin[Axis]) / Direction;
            double AxisFar = (Bounds.Max[Axis] - RayOrigin[Axis]) / Direction;
            if (AxisNear > AxisFar)
            {
                Swap(AxisNear, AxisFar);
            }
            NearDistance = FMath::Max(NearDistance, AxisNear);
            FarDistance = FMath::Min(FarDistance, AxisFar);
            if (NearDistance > FarDistance)
            {
                return false;
            }
        }
        OutDistance = NearDistance;
        return FarDistance >= 0.0;
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
    PreviewModeLifetime = InArgs._PreviewModeLifetime;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    PlacementSession = InArgs._PlacementSession;
    if (!PlacementSession.IsValid())
    {
        PlacementSession = MakeShared<FDWCTransparencyPlacementSession>();
    }
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
    Collector.AddReferencedObjects(PreviewMeshComponents);
    for (TPair<FName, TObjectPtr<USkeletalMeshComponent>>& Pair : BlueprintSourcePreviewComponents)
    {
        Collector.AddReferencedObject(Pair.Value);
    }
    for (TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ExternalSourcePreviewComponents)
    {
        Collector.AddReferencedObject(Pair.Value);
    }
    Collector.AddReferencedObject(HoverIslandIDTexture);
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

UTexture2D* SWetClothingTransparencyPreviewViewport::GetHoverIslandIDTexture() const
{
    return HoverIslandIDTexture;
}

UTexture2D* SWetClothingTransparencyPreviewViewport::GetHoverEdgeFeatherTexture() const
{
    return HoverEdgeFeatherPreviewHandle.IsValid()
        ? HoverEdgeFeatherPreviewHandle->GetTexture()
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
        UE::DWCEditor::TransparencyPreview::MakeTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::TransparencyHoverBaseline,
            SelectedMaterialSlotIndex,
            SelectedLayerGuid),
        UE::DWCEditor::TransparencyPreview::MakeColorDescriptor(AutoBakePreviewResult->Resolution, Address),
        MoveTemp(BaselinePixels),
        EDWCEditorTextureUploadPriority::Interactive);
    if (HoverBaselinePreviewHandle.IsValid())
    {
        ++HoverBaselineBuildCount;
    }
    return GetHoverBaselineTexture() != nullptr;
}

bool SWetClothingTransparencyPreviewViewport::EnsureHoverIslandIDTexture()
{
    if (HoverIslandIDTexture != nullptr)
    {
        return true;
    }
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return false;
    }

    HoverIslandIDTexture = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        *Layer,
        EDWCTransparencyTempArtifactKind::OuterIslandID,
        FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
            EDWCTransparencyTempArtifactKind::OuterIslandID,
            AutoBakePreviewResult->BuildSignature),
        true);
    if (HoverIslandIDTexture != nullptr)
    {
        ++HoverIslandIDResolveCount;
    }
    return HoverIslandIDTexture != nullptr;
}

bool SWetClothingTransparencyPreviewViewport::EnsureHoverEdgeFeatherTexture()
{
    if (HoverEdgeFeatherPreviewHandle.IsValid())
    {
        return GetHoverEdgeFeatherTexture() != nullptr;
    }
    if (OuterEdgeFeatherBuffer.IsEmpty())
    {
        return true;
    }
    if (!TextureWorkspace.IsValid() || !AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const int32 PixelCount = AutoBakePreviewResult->Resolution.X * AutoBakePreviewResult->Resolution.Y;
    if (PixelCount <= 0 || OuterEdgeFeatherBuffer.Num() != PixelCount)
    {
        return false;
    }
    const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap
        ? TA_Wrap
        : TA_Clamp;
    TArray<uint8> EdgeFeatherPixels = OuterEdgeFeatherBuffer;
    HoverEdgeFeatherPreviewHandle = TextureWorkspace->TransferG8AndAcquireLease(
        UE::DWCEditor::TransparencyPreview::MakeTextureKey(
            WetClothingAsset.Get(),
            EDWCEditorTexturePurpose::TransparencyHoverEdgeFeather,
            SelectedMaterialSlotIndex,
            SelectedLayerGuid),
        UE::DWCEditor::TransparencyPreview::MakeMaskDescriptor(AutoBakePreviewResult->Resolution, Address),
        MoveTemp(EdgeFeatherPixels),
        EDWCEditorTextureUploadPriority::Interactive);
    if (HoverEdgeFeatherPreviewHandle.IsValid())
    {
        ++HoverEdgeFeatherBuildCount;
    }
    return GetHoverEdgeFeatherTexture() != nullptr;
}

void SWetClothingTransparencyPreviewViewport::ReleaseHoverAuxiliaryResources()
{
    if (TextureWorkspace.IsValid())
    {
        TextureWorkspace->Discard(HoverBaselinePreviewHandle);
        TextureWorkspace->Discard(HoverEdgeFeatherPreviewHandle);
    }
    HoverBaselinePreviewHandle.Reset();
    HoverEdgeFeatherPreviewHandle.Reset();
    HoverIslandIDTexture = nullptr;
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
        FBoxSphereBounds AssemblyBounds;
        if (PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint &&
            ResolveType2AssemblyBounds(AssemblyBounds))
        {
            PreviewClient->FocusOnBounds(AssemblyBounds, bInstant);
            return;
        }
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
    // Blueprint/target changes rebuild the canonical Type 2 assembly. Type 3
    // placement changes still rebuild its source layout. Type 2 source check
    // changes use SyncType2SelectedSourceComponents instead.
    if (!bPreviewSuspended && PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint)
    {
        RefreshPreview();
    }
}

void SWetClothingTransparencyPreviewViewport::SetType2BlueprintHierarchySnapshot(
    const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot)
{
    if (Snapshot.State != EDWCTransparencyBlueprintHierarchyState::Ready)
    {
        Type2BlueprintHierarchy.Reset();
        Type2BlueprintHierarchyLayerGuid.Invalidate();
        Type2BlueprintClassPath.Reset();
        Type2BlueprintHierarchyRevision = Snapshot.Revision;
        return;
    }

    if (Type2BlueprintHierarchy.IsValid() &&
        Type2BlueprintHierarchyRevision == Snapshot.Revision &&
        Type2BlueprintHierarchyLayerGuid == Snapshot.LayerGuid &&
        Type2BlueprintClassPath == Snapshot.BlueprintClassPath)
    {
        return;
    }

    Type2BlueprintHierarchy =
        MakeShared<FDWCTransparencyBlueprintHierarchyMetadata>(Snapshot.Hierarchy);
    Type2BlueprintHierarchyLayerGuid = Snapshot.LayerGuid;
    Type2BlueprintClassPath = Snapshot.BlueprintClassPath;
    Type2BlueprintHierarchyRevision = Snapshot.Revision;
}

void SWetClothingTransparencyPreviewViewport::SyncType2SelectedSourceComponents()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (bPreviewSuspended || PreviewMode != EWetClothingTransparencyPreviewMode::FullBlueprint ||
        Layer == nullptr || Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        !Type2BlueprintHierarchy.IsValid() ||
        Type2BlueprintHierarchyLayerGuid != Layer->LayerGuid)
    {
        return;
    }

    TSet<FName> DesiredComponents;
    for (const FWetClothingTransparencyBlueprintComponentBinding& Binding :
         Layer->BlueprintSource.SourcePriority)
    {
        if (Binding.IsBound() &&
            Binding.ComponentName != Layer->BlueprintSource.TargetComponent.ComponentName)
        {
            DesiredComponents.Add(Binding.ComponentName);
        }
    }

    TArray<FName> ComponentsToRemove;
    for (const TPair<FName, TObjectPtr<USkeletalMeshComponent>>& Pair :
         BlueprintSourcePreviewComponents)
    {
        if (!DesiredComponents.Contains(Pair.Key))
        {
            ComponentsToRemove.Add(Pair.Key);
        }
    }
    for (const FName ComponentName : ComponentsToRemove)
    {
        USkeletalMeshComponent* Component = BlueprintSourcePreviewComponents.FindRef(ComponentName);
        if (Component != nullptr && PreviewScene.IsValid())
        {
            PreviewScene->RemoveComponent(Component);
            PreviewMeshComponents.Remove(Component);
        }
        BlueprintSourcePreviewComponents.Remove(ComponentName);
    }

    for (const FName ComponentName : DesiredComponents)
    {
        if (!BlueprintSourcePreviewComponents.Contains(ComponentName))
        {
            CreateType2PreviewComponent(ComponentName, false);
        }
    }

    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetPlacementSelection(
    const FDWCTransparencyPlacementSelection& Selection)
{
    if (!PlacementSession.IsValid())
    {
        return;
    }
    const bool bChanged = !(PlacementSession->GetSelection() == Selection);
    PlacementSession->SetSelection(Selection);
    RefreshType3PlacementPresentation();
    if (bChanged)
    {
        PlacementSelectionChanged.ExecuteIfBound(Selection);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetPlacementSelectionChangedDelegate(
    FDWCTransparencyPlacementSelectionChanged InDelegate)
{
    PlacementSelectionChanged = MoveTemp(InDelegate);
}

const FDWCTransparencyPlacementSelection&
SWetClothingTransparencyPreviewViewport::GetPlacementSelection() const
{
    static const FDWCTransparencyPlacementSelection EmptySelection;
    return PlacementSession.IsValid() ? PlacementSession->GetSelection() : EmptySelection;
}

void SWetClothingTransparencyPreviewViewport::SetExternalSourceTransformCommittedDelegate(
    FDWCTransparencyExternalSourceTransformCommitted InDelegate)
{
    ExternalSourceTransformCommitted = MoveTemp(InDelegate);
}

void SWetClothingTransparencyPreviewViewport::SetPlacementHelpVisible(const bool bVisible)
{
    if (bPlacementHelpVisible == bVisible)
    {
        return;
    }
    bPlacementHelpVisible = bVisible;
    InvalidatePreviewViewport();
}

FTransform SWetClothingTransparencyPreviewViewport::GetSelectedPlacementTransform() const
{
    if (!PlacementSession.IsValid())
    {
        return FTransform::Identity;
    }
    const FDWCTransparencyPlacementSelection& Selection = PlacementSession->GetSelection();
    return Selection.IsSource()
        ? PlacementSession->GetSourceTransform(Selection.SourceGuid)
        : PlacementSession->GetAssemblyTransform();
}

void SWetClothingTransparencyPreviewViewport::SetSelectedPlacementTransform(
    const FTransform& Transform,
    const bool bCommit)
{
    if (!PlacementSession.IsValid() || PlacementSession->IsSelectionLocked() ||
        Transform.ContainsNaN())
    {
        return;
    }
    FTransform Sanitized = Transform;
    Sanitized.SetScale3D(FVector::OneVector);
    Sanitized.NormalizeRotation();
    const FDWCTransparencyPlacementSelection Selection = PlacementSession->GetSelection();
    if (Selection.Type == EDWCTransparencyPlacementSelectionType::Target)
    {
        PlacementSession->SetAssemblyTransform(Sanitized);
        RefreshType3PlacementPresentation();
    }
    else if (Selection.IsSource())
    {
        PlacementSession->SetSourceTransform(Selection.SourceGuid, Sanitized);
        if (USkeletalMeshComponent* Component = ExternalSourcePreviewComponents.FindRef(Selection.SourceGuid))
        {
            Component->SetWorldTransform(Sanitized * PlacementSession->GetAssemblyTransform());
            Component->MarkRenderTransformDirty();
        }
        if (bCommit && ExternalSourceTransformCommitted.IsBound())
        {
            ExternalSourceTransformCommitted.Execute(Selection.SourceGuid, Sanitized);
        }
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshType3PlacementPresentation()
{
    if (!PlacementSession.IsValid())
    {
        return;
    }
    const FDWCTransparencyPlacementSelection Selection = PlacementSession->GetSelection();
    const FTransform AssemblyTransform = PlacementSession->GetAssemblyTransform();
    if (TargetMeshPreviewComponent != nullptr)
    {
        TargetMeshPreviewComponent->SetWorldTransform(AssemblyTransform);
        if (Selection.Type == EDWCTransparencyPlacementSelectionType::Target)
        {
            TargetMeshPreviewComponent->SetOverlayColor(FColor(48, 160, 255, 48));
        }
        else
        {
            TargetMeshPreviewComponent->RemoveOverlayColor();
        }
    }
    for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ExternalSourcePreviewComponents)
    {
        if (Pair.Value == nullptr)
        {
            continue;
        }
        Pair.Value->SetVisibility(PlacementSession->ShouldShowSource(Pair.Key), true);
        Pair.Value->SetWorldTransform(
            PlacementSession->GetSourceTransform(Pair.Key) * AssemblyTransform);
        if (Selection.IsSource() && Selection.SourceGuid == Pair.Key)
        {
            Pair.Value->SetOverlayColor(FColor(48, 160, 255, 48));
        }
        else
        {
            Pair.Value->RemoveOverlayColor();
        }
    }
    if (TargetMeshPreviewComponent != nullptr)
    {
        const FBoxSphereBounds Bounds = TargetMeshPreviewComponent->CalcBounds(
            TargetMeshPreviewComponent->GetComponentTransform());
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
    InvalidatePreviewViewport();
}

bool SWetClothingTransparencyPreviewViewport::SelectType3PlacementAtRay(
    const FVector& RayOrigin,
    const FVector& RayDirection,
    const bool bCycleSelection)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!PlacementSession.IsValid() || Layer == nullptr ||
        Layer->SourceType != EDWCTransparencySourceType::ExternalSkeletalMesh ||
        PreviewMode != EWetClothingTransparencyPreviewMode::FullBlueprint || bSurfacePaintingEnabled)
    {
        return false;
    }

    struct FPlacementHit
    {
        FDWCTransparencyPlacementSelection Selection;
        double Distance = 0.0;
    };
    TArray<FPlacementHit> Hits;
    auto AddHit = [&Hits, &RayOrigin, &RayDirection](
        USkeletalMeshComponent* Component,
        const FDWCTransparencyPlacementSelection& Selection)
    {
        if (Component == nullptr || !Component->IsVisible())
        {
            return;
        }
        double Distance = 0.0;
        const FBox CurrentBounds = Component->CalcBounds(
            Component->GetComponentTransform()).GetBox();
        if (IntersectPlacementBounds(
                RayOrigin,
                RayDirection,
                CurrentBounds,
                Distance))
        {
            Hits.Add({Selection, Distance});
        }
    };
    AddHit(TargetMeshPreviewComponent, FDWCTransparencyPlacementSelection::Target());
    for (const TPair<FGuid, TObjectPtr<USkeletalMeshComponent>>& Pair : ExternalSourcePreviewComponents)
    {
        AddHit(Pair.Value, FDWCTransparencyPlacementSelection::Source(Pair.Key));
    }
    Hits.Sort([](const FPlacementHit& A, const FPlacementHit& B)
    {
        return A.Distance < B.Distance;
    });
    if (Hits.IsEmpty())
    {
        SetPlacementSelection({});
        return false;
    }

    int32 SelectedHitIndex = 0;
    if (bCycleSelection)
    {
        const int32 CurrentIndex = Hits.IndexOfByPredicate(
            [this](const FPlacementHit& Hit)
            {
                return Hit.Selection == PlacementSession->GetSelection();
            });
        SelectedHitIndex = CurrentIndex == INDEX_NONE ? 0 : (CurrentIndex + 1) % Hits.Num();
    }
    SetPlacementSelection(Hits[SelectedHitIndex].Selection);
    return true;
}

void SWetClothingTransparencyPreviewViewport::FocusSelectedPlacement(const bool bInstant)
{
    FDWCTransparencyPreviewViewportClient* PlacementViewportClient =
        static_cast<FDWCTransparencyPreviewViewportClient*>(ViewportClient.Get());
    if (PlacementViewportClient == nullptr || !PlacementSession.IsValid())
    {
        return;
    }
    const FDWCTransparencyPlacementSelection& Selection = PlacementSession->GetSelection();
    USkeletalMeshComponent* Component = Selection.Type == EDWCTransparencyPlacementSelectionType::Target
        ? TargetMeshPreviewComponent.Get()
        : ExternalSourcePreviewComponents.FindRef(Selection.SourceGuid).Get();
    PlacementViewportClient->FocusOnMesh(Component, bInstant);
}

void SWetClothingTransparencyPreviewViewport::FocusType3Assembly(const bool bInstant)
{
    FDWCTransparencyPreviewViewportClient* PlacementViewportClient =
        static_cast<FDWCTransparencyPreviewViewportClient*>(ViewportClient.Get());
    if (PlacementViewportClient == nullptr)
    {
        return;
    }
    FBox Bounds(ForceInit);
    for (USkeletalMeshComponent* Component : PreviewMeshComponents)
    {
        if (Component != nullptr && Component->IsVisible())
        {
            Bounds += Component->Bounds.GetBox();
        }
    }
    if (Bounds.IsValid)
    {
        PlacementViewportClient->FocusOnBounds(FBoxSphereBounds(Bounds), bInstant);
    }
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
        if (PreviewMode == EWetClothingTransparencyPreviewMode::FullBlueprint &&
            TargetMeshPreviewComponent != nullptr)
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
        bRevealColorRequiresWorkerRebuild = Layer->GetRevealColorPaintStrokes().ContainsByPredicate(
            [this](const FDWCTransparencyRevealColorStroke& Stroke)
            {
                return Stroke.bEnabled &&
                    Stroke.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                    Stroke.HasSamples();
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

        // Keep an expandable leading section so the camera and view controls in
        // the Last-aligned section remain anchored to the right edge.
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

    const auto BuildShortcutRow = [](const FText& Action, const FText& Shortcut)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 1, 16, 1)
              [SNew(STextBlock).Text(Action)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 1)
              [SNew(STextBlock)
                 .Text(Shortcut)
                 .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))
                 .ColorAndOpacity(FStyleColors::AccentBlue)];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.0f)
          [UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext)]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f, 0.0f)
          [SNew(SComboButton)
             .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .HasDownArrow(false)
             .ToolTipText(LOCTEXT("Type3PlacementHelpTooltip", "Type 3 placement controls"))
             .Visibility_Lambda([this]()
             {
                 return bPlacementHelpVisible ? EVisibility::Visible : EVisibility::Collapsed;
             })
             .ButtonContent()
             [SNew(STextBlock)
                .Text(LOCTEXT("Type3PlacementHelpIcon", "?"))
                .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
             .MenuContent()
             [SNew(SBorder)
                .Padding(10.0f)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                [SNew(SVerticalBox)
                 + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                   [SNew(STextBlock)
                      .Text(LOCTEXT("Type3PlacementHelpTitle", "Type 3 Placement Controls"))
                      .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
                 + SVerticalBox::Slot().AutoHeight()[BuildShortcutRow(
                     LOCTEXT("Type3HelpSelect", "Select mesh"),
                     LOCTEXT("Type3HelpSelectKey", "Click"))]
                 + SVerticalBox::Slot().AutoHeight()[BuildShortcutRow(
                     LOCTEXT("Type3HelpCycle", "Cycle overlapping meshes"),
                     LOCTEXT("Type3HelpCycleKey", "Alt + Click"))]
                 + SVerticalBox::Slot().AutoHeight()[BuildShortcutRow(
                     LOCTEXT("Type3HelpCamera", "Move camera"),
                     LOCTEXT("Type3HelpCameraKey", "W A S D"))]
                 + SVerticalBox::Slot().AutoHeight()[BuildShortcutRow(
                     LOCTEXT("Type3HelpFocus", "Focus selected mesh"),
                     LOCTEXT("Type3HelpFocusKey", "F"))]
                 + SVerticalBox::Slot().AutoHeight()[BuildShortcutRow(
                     LOCTEXT("Type3HelpFocusAll", "Frame full assembly"),
                     LOCTEXT("Type3HelpFocusAllKey", "Home"))]]]];
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
        for (const TPair<FName, TObjectPtr<USkeletalMeshComponent>>& Pair :
             BlueprintSourcePreviewComponents)
        {
            if (Pair.Value != nullptr)
            {
                PreviewScene->RemoveComponent(Pair.Value);
            }
        }
    }

    TargetMeshPreviewComponent = nullptr;
    PreviewMeshComponents.Reset();
    BlueprintSourcePreviewComponents.Reset();
    ExternalSourcePreviewComponents.Reset();
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
    PreviewSession->BindModeLifetime(PreviewModeLifetime);
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
    if (PreviewSession && !bPreviewSuspended && PreviewSession->HasPendingMaterialCompilations())
    {
        PreviewSession->TickPendingMaterialCompilations();
    }
}

void SWetClothingTransparencyPreviewViewport::ProcessPendingViewportWork()
{
    const double CurrentTime = FPlatformTime::Seconds();
    FDWCTransparencyViewportWorkState State;
    State.bSuspended = bPreviewSuspended;
    State.bMaterialCompilationPending =
        PreviewSession && PreviewSession->HasPendingMaterialCompilations();
    State.bPreviewRebuildInFlight = PendingPreviewTicket.IsValid();
    State.bPreviewRebuildRequired =
        PreviewTextureRecovery.GetState() == EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    State.bPreviewRetryDue = PreviewTextureRecovery.IsRetryDue(CurrentTime);
    State.bAlphaCommandsPending = !PendingAlphaCommands.IsEmpty();
    State.bAlphaJobPending = PendingAlphaIncrementalTicket.IsValid();
    State.bRevealCommandsPending = !PendingRevealColorCommands.IsEmpty();
    State.bRevealJobPending = PendingRevealColorIncrementalTicket.IsValid();
    State.bAuthoringFinishPending = bAuthoringFinishPending;
    State.bUploadPending = RenderUploadQueue.IsValid() && RenderUploadQueue->HasPendingWork();

    const FDWCTransparencyViewportWorkDecision Decision =
        FDWCTransparencyViewportWorkPolicy::Resolve(State);
    if (!Decision.HasWork())
    {
        ++IdleWorkTickSkipCount;
        return;
    }

    ++PendingWorkTickCount;
    if (Decision.bPollMaterialCompilations)
    {
        ++MaterialCompilationPollCount;
        TickPreviewMaterialCompilations();
    }
    if (Decision.bRetryPreviewRebuild)
    {
        ++PreviewRecoveryWorkCount;
        RetryPreviewTextureRebuildIfNeeded();
    }
    if (Decision.bProcessInteractivePaint)
    {
        ++InteractivePaintWorkCount;
        ProcessInteractivePaintWork();
    }
    if (Decision.bFlushUploads)
    {
        ++UploadFlushWorkCount;
        FlushPendingPreviewTextureUpdates();
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

    BuildType2BlueprintAssemblyPreview();
}

void SWetClothingTransparencyPreviewViewport::BuildType2BlueprintAssemblyPreview()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || !Type2BlueprintHierarchy.IsValid() ||
        Type2BlueprintHierarchyLayerGuid != Layer->LayerGuid ||
        !Layer->BlueprintSource.TargetComponent.IsBound())
    {
        BuildTargetMeshPreview();
        return;
    }

    TargetMeshPreviewComponent = CreateType2PreviewComponent(
        Layer->BlueprintSource.TargetComponent.ComponentName,
        true);
    if (TargetMeshPreviewComponent == nullptr)
    {
        BuildTargetMeshPreview();
        return;
    }

    SyncType2SelectedSourceComponents();

    BrushCursorComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    BrushCursorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushCursorComponent->SetCastShadow(false);
    BrushCursorComponent->SetReceivesDecals(false);
    BrushCursorComponent->SetDepthPriorityGroup(SDPG_Foreground);
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);
    EnsureBrushCursor();
    RebuildHitTriangles();
    ApplyRevealColorPaintTargetVisibility();

    FBoxSphereBounds AssemblyBounds;
    if (ResolveType2AssemblyBounds(AssemblyBounds))
    {
        PreviewScene->SetFloorOffset(-AssemblyBounds.Origin.Z + AssemblyBounds.BoxExtent.Z);
    }
}

USkeletalMeshComponent* SWetClothingTransparencyPreviewViewport::CreateType2PreviewComponent(
    const FName& ComponentName,
    const bool bTargetComponent)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !PreviewScene.IsValid() || !Type2BlueprintHierarchy.IsValid())
    {
        return nullptr;
    }

    const FDWCTransparencyBlueprintMeshComponentMetadata* SnapshotComponent =
        Type2BlueprintHierarchy->MeshComponents.FindByPredicate(
            [ComponentName](const FDWCTransparencyBlueprintMeshComponentMetadata& Candidate)
            {
                return Candidate.ComponentName == ComponentName;
            });
    USkeletalMesh* Mesh = bTargetComponent
        ? Asset->GetDWCSkeletalMesh()
        : (SnapshotComponent != nullptr
            ? Cast<USkeletalMesh>(SnapshotComponent->SkeletalMeshPath.ResolveObject())
            : nullptr);
    if (SnapshotComponent == nullptr || Mesh == nullptr)
    {
        return nullptr;
    }

    USkeletalMeshComponent* PreviewComponent = NewObject<USkeletalMeshComponent>(
        GetTransientPackage(), NAME_None, RF_Transient);
    PreviewComponent->SetMobility(EComponentMobility::Movable);
    ConfigureStaticTransparencyPreviewPose(PreviewComponent);
    PreviewComponent->SetSkeletalMeshAsset(Mesh);
    PreviewComponent->SetCastShadow(false);
    if (!bTargetComponent)
    {
        for (int32 MaterialIndex = 0;
             MaterialIndex < SnapshotComponent->MaterialPaths.Num();
             ++MaterialIndex)
        {
            if (UMaterialInterface* Material = Cast<UMaterialInterface>(
                    SnapshotComponent->MaterialPaths[MaterialIndex].ResolveObject()))
            {
                PreviewComponent->SetMaterial(MaterialIndex, Material);
            }
        }
    }
    PreviewScene->AddComponent(PreviewComponent, SnapshotComponent->BakeTransform);
    PreviewMeshComponents.AddUnique(PreviewComponent);

    if (bTargetComponent)
    {
        ConfigurePreviewMeshComponent(PreviewComponent);
    }
    else
    {
        BlueprintSourcePreviewComponents.Add(ComponentName, PreviewComponent);
    }
    return PreviewComponent;
}

bool SWetClothingTransparencyPreviewViewport::ResolveType2AssemblyBounds(
    FBoxSphereBounds& OutBounds) const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        !Type2BlueprintHierarchy.IsValid() || Type2BlueprintHierarchyLayerGuid != Layer->LayerGuid)
    {
        return false;
    }

    FBox CombinedBounds(ForceInit);
    for (const FDWCTransparencyBlueprintMeshComponentMetadata& Component :
         Type2BlueprintHierarchy->MeshComponents)
    {
        USkeletalMesh* Mesh = Cast<USkeletalMesh>(Component.SkeletalMeshPath.ResolveObject());
        if (Mesh == nullptr || Component.BakeTransform.ContainsNaN())
        {
            continue;
        }
        const FBoxSphereBounds ComponentBounds =
            Mesh->GetBounds().TransformBy(Component.BakeTransform);
        CombinedBounds += FBox::BuildAABB(ComponentBounds.Origin, ComponentBounds.BoxExtent);
    }
    if (!CombinedBounds.IsValid)
    {
        return false;
    }

    OutBounds = FBoxSphereBounds(CombinedBounds);
    return true;
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
    TMap<FGuid, FTransform> CanonicalTransforms;
    for (const FWetClothingTransparencyExternalMeshEntry& Entry :
         Layer->ExternalMeshSource.SourcePriority)
    {
        if (Entry.SourceGuid.IsValid())
        {
            CanonicalTransforms.Add(Entry.SourceGuid, Entry.BakeTransform);
        }
    }
    PlacementSession->SynchronizeSources(CanonicalTransforms);
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
        ConfigureStaticTransparencyPreviewPose(SourceComponent);
        SourceComponent->SetSkeletalMeshAsset(Entry.SkeletalMesh);
        SourceComponent->SetCastShadow(false);
        PreviewScene->AddComponent(
            SourceComponent,
            PlacementSession->GetSourceTransform(Entry.SourceGuid) *
                PlacementSession->GetAssemblyTransform());
        PreviewMeshComponents.AddUnique(SourceComponent);
        ExternalSourcePreviewComponents.Add(Entry.SourceGuid, SourceComponent);
    }
    RefreshType3PlacementPresentation();
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
    if (HoverIslandID == INDEX_NONE ||
        !EnsureHoverIslandIDTexture() ||
        !EnsureHoverEdgeFeatherTexture())
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
        DWCTransparencyPreviewMaterialParameters::HoverIslandIDMap(),
        GetHoverIslandIDTexture());
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseHoverIslandIDMap(),
        1.0f);
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::HoverIslandID(),
        DWCTransparencyPreviewMaterialParameters::EncodeHoverIslandID(HoverIslandID));
    HoverLayer.AddTexture(
        DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap(),
        GetHoverEdgeFeatherTexture());
    HoverLayer.AddScalar(
        DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap(),
        GetHoverEdgeFeatherTexture() != nullptr ? 1.0f : 0.0f);

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


#undef LOCTEXT_NAMESPACE
