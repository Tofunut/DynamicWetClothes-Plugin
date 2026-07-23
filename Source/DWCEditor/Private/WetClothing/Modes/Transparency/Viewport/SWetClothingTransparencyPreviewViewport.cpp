#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "Materials/MaterialInstanceConstant.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Modes/DWCEditorPreviewSlotUtils.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialBuilder.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionProcessor.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyPreviewViewport"

namespace
{
    constexpr int32 TransparencyHitBVHLeafTriangleCount = 8;
    constexpr const TCHAR* TransparencyMapParameterName = TEXT("DWC_TransparencyMap");
    constexpr const TCHAR* UseRuntimeTransparencyParameterName = TEXT("DWC_UseTransparencyMap");
    constexpr const TCHAR* TransparencyWetnessMinParameterName = TEXT("DWC_TransparencyWetnessMin");
    constexpr const TCHAR* TransparencyWetnessMaxParameterName = TEXT("DWC_TransparencyWetnessMax");
    constexpr const TCHAR* WrinkleNormalMapParameterName = TEXT("DWC_WrinkleNormalMap");
    constexpr const TCHAR* UseWrinkleNormalMapParameterName = TEXT("DWC_UseWrinkleNormalMap");

    class FDWCTransparencyPreviewViewportClient : public FEditorViewportClient
    {
      public:
        FDWCTransparencyPreviewViewportClient(
            FAdvancedPreviewScene* InPreviewScene,
            const TSharedRef<SWetClothingTransparencyPreviewViewport>& InViewport)
            : FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
            , PreviewScene(InPreviewScene)
            , ViewportWidget(InViewport)
        {
            SetViewMode(VMI_Lit);
            SetRealtime(true);
            ViewFOV = 65.0f;
            FOVAngle = 65.0f;
            SetViewLocation(FVector(250.0f, 0.0f, 120.0f));
            SetViewRotation(FRotator(-20.0f, 180.0f, 0.0f));
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
                Pinned->FlushPendingPreviewTextureUpdates();
            }
        }

        virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override
        {
            const bool bLeftMouse = EventArgs.Key == EKeys::LeftMouseButton;
            const bool bCameraModifier = Viewport != nullptr &&
                (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt));
            if (bLeftMouse && EventArgs.Event == IE_Released && bPainting)
            {
                if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                {
                    Pinned->EndPaintStrokeFromClient();
                }
                bPainting = false;
                return true;
            }
            if (bLeftMouse && EventArgs.Event == IE_Pressed && !bCameraModifier)
            {
                FDWCTransparencySurfaceHit Hit;
                if (TraceUnderCursor(Hit))
                {
                    if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin();
                        Pinned.IsValid() && Pinned->CanPaint())
                    {
                        Pinned->HandleSurfaceHitFromClient(Hit);
                        Pinned->BeginPaintStrokeFromClient(Hit);
                        bPainting = true;
                        return true;
                    }
                }
            }
            return FEditorViewportClient::InputKey(EventArgs);
        }

        virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override
        {
            FEditorViewportClient::MouseMove(InViewport, X, Y);
            UpdateHit();
        }

        virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override
        {
            if (bPainting)
            {
                FDWCTransparencySurfaceHit Hit;
                if (TraceUnderCursor(Hit))
                {
                    if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                    {
                        Pinned->HandleSurfaceHitFromClient(Hit);
                        Pinned->RequestPaintStampFromClient(Hit);
                    }
                }
                return;
            }
            FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
            UpdateHit();
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
        bool TraceUnderCursor(FDWCTransparencySurfaceHit& OutHit)
        {
            if (Viewport == nullptr)
            {
                return false;
            }
            const FViewportCursorLocation Cursor = GetCursorWorldLocationFromMousePos();
            if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                return Pinned->TraceSurface(Cursor.GetOrigin(), Cursor.GetDirection(), OutHit);
            }
            return false;
        }

        void UpdateHit()
        {
            FDWCTransparencySurfaceHit Hit;
            if (TraceUnderCursor(Hit))
            {
                if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
                {
                    Pinned->HandleSurfaceHitFromClient(Hit);
                }
            }
            else if (const TSharedPtr<SWetClothingTransparencyPreviewViewport> Pinned = ViewportWidget.Pin())
            {
                Pinned->HandleSurfaceHitFromClient(FDWCTransparencySurfaceHit());
            }
        }

        FAdvancedPreviewScene* PreviewScene = nullptr;
        TWeakPtr<SWetClothingTransparencyPreviewViewport> ViewportWidget;
        bool bPainting = false;
    };

    const FWetWrinkleBakedMapSet* FindExactWrinkleNormalMap(
        const UWetClothingAsset* Asset,
        const int32 MaterialSlotIndex,
        const int32 UVChannelIndex,
        const int32 LODIndex)
    {
        if (Asset == nullptr)
        {
            return nullptr;
        }

        return Asset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex, UVChannelIndex, LODIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       Candidate.UVChannelIndex == UVChannelIndex &&
                       Candidate.LODIndex == LODIndex &&
                       Candidate.BakedWrinkleNormalMap != nullptr;
            });
    }

    FVector ComputeBarycentric(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
    {
        const FVector V0 = B - A;
        const FVector V1 = C - A;
        const FVector V2 = Point - A;
        const double D00 = FVector::DotProduct(V0, V0);
        const double D01 = FVector::DotProduct(V0, V1);
        const double D11 = FVector::DotProduct(V1, V1);
        const double D20 = FVector::DotProduct(V2, V0);
        const double D21 = FVector::DotProduct(V2, V1);
        const double Denominator = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denominator))
        {
            return FVector(1.0, 0.0, 0.0);
        }
        const double V = (D11 * D20 - D01 * D21) / Denominator;
        const double W = (D00 * D21 - D01 * D20) / Denominator;
        return FVector(1.0 - V - W, V, W);
    }

    FVector AnyPerpendicular(const FVector& Normal)
    {
        FVector Result = FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
        if (Result.IsNearlyZero())
        {
            Result = FVector::CrossProduct(Normal, FVector::RightVector).GetSafeNormal();
        }
        return Result.IsNearlyZero() ? FVector::ForwardVector : Result;
    }

    bool DoesTransparencySegmentIntersectBox(const FBox& Box, const FVector& SegmentStart, const FVector& SegmentEnd)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        const FVector Direction = SegmentEnd - SegmentStart;
        double Entry = 0.0;
        double Exit = 1.0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double Origin = SegmentStart[Axis];
            const double Delta = Direction[Axis];
            const double MinValue = Box.Min[Axis];
            const double MaxValue = Box.Max[Axis];
            if (FMath::IsNearlyZero(Delta))
            {
                if (Origin < MinValue || Origin > MaxValue)
                {
                    return false;
                }
                continue;
            }

            double AxisEntry = (MinValue - Origin) / Delta;
            double AxisExit = (MaxValue - Origin) / Delta;
            if (AxisEntry > AxisExit)
            {
                Swap(AxisEntry, AxisExit);
            }
            Entry = FMath::Max(Entry, AxisEntry);
            Exit = FMath::Min(Exit, AxisExit);
            if (Entry > Exit)
            {
                return false;
            }
        }
        return Exit >= 0.0 && Entry <= 1.0;
    }
}

void SWetClothingTransparencyPreviewViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    OnStrokesChanged = InArgs._OnStrokesChanged;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    SEditorViewport::Construct(SEditorViewport::FArguments());
    RefreshPreview();
}

SWetClothingTransparencyPreviewViewport::~SWetClothingTransparencyPreviewViewport()
{
    AutoBakePreviewResult.Reset();
    WrinkleSuppressionBuffer.Reset();
    TransparencyPreviewTexture = nullptr;
    ClearPreview();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetClothingTransparencyPreviewViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(TargetMeshPreviewComponent);
    Collector.AddReferencedObject(PreviewActor);
    Collector.AddReferencedObjects(PreviewMeshComponents);
    Collector.AddReferencedObjects(PreviewMIDs);
    Collector.AddReferencedObjects(TransparencyPreviewBaseMaterials);
    Collector.AddReferencedObjects(TransparencyPreviewMaterialParents);
    Collector.AddReferencedObject(CachedPreviewSourceMaterial);
    Collector.AddReferencedObject(CachedPreviewBaseMaterial);
    Collector.AddReferencedObject(CachedPreviewMaterialParent);
    Collector.AddReferencedObject(CachedPreviewMID);
    Collector.AddReferencedObject(TransparencyPreviewTexture);
    Collector.AddReferencedObject(BrushCursorComponent);
}

void SWetClothingTransparencyPreviewViewport::RefreshPreview()
{
    ClearPreview();

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
    if (PreviewMode == NewMode)
    {
        return;
    }

    PreviewMode = NewMode;
    RefreshPreview();
}

void SWetClothingTransparencyPreviewViewport::SetWetnessPreviewPercent(const float InPercent)
{
    WetnessPreviewPercent = FMath::Clamp(InPercent, 0.0f, 100.0f);
    for (USkeletalMeshComponent* MeshComponent : PreviewMeshComponents)
    {
        ApplyWetnessPreview(MeshComponent);
    }
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetTransparencyEditContext(
    const FGuid& InLayerGuid,
    int32 InMaterialSlotIndex,
    int32 InUVChannelIndex,
    EDWCTransparencyUVAddressMode InAddressMode)
{
    const bool bMaterialSlotChanged = SelectedMaterialSlotIndex != InMaterialSlotIndex;
    const bool bUVChannelChanged = SelectedUVChannelIndex != InUVChannelIndex;
    const bool bAddressModeChanged = SelectedUVAddressMode != InAddressMode;
    const bool bContextChanged = SelectedLayerGuid != InLayerGuid ||
        SelectedMaterialSlotIndex != InMaterialSlotIndex ||
        SelectedUVChannelIndex != InUVChannelIndex ||
        SelectedUVAddressMode != InAddressMode;
    SelectedLayerGuid = InLayerGuid;
    SelectedMaterialSlotIndex = InMaterialSlotIndex;
    SelectedUVChannelIndex = InUVChannelIndex;
    SelectedUVAddressMode = InAddressMode;
    if (bMaterialSlotChanged || bUVChannelChanged)
    {
        RefreshPreview();
        return;
    }
    if (bContextChanged)
    {
        RebuildHitTriangles();
        CurrentSurfaceHit = FDWCTransparencySurfaceHit();
        ClearBrushCursor();
    }
    if (bContextChanged && AutoBakePreviewResult.IsValid())
    {
        RebuildWrinkleSuppressionBuffer();
        RebuildOuterEdgeFeatherBuffer();
        RebuildManualOverridesFromStrokes();
        RebuildTransparencyPreviewTexture();
    }
    else if (bAddressModeChanged && TransparencyPreviewTexture != nullptr)
    {
        const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
        TransparencyPreviewTexture->AddressX = Address;
        TransparencyPreviewTexture->AddressY = Address;
        TransparencyPreviewTexture->UpdateResource();
    }
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
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
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
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::RefreshWrinkleSuppressionPreview()
{
    RebuildWrinkleSuppressionBuffer();
    RebuildOuterEdgeFeatherBuffer();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetPaintSettings(const FDWCTransparencyPaintSettings& InSettings)
{
    PaintSettings = InSettings;
    PaintSettings.RadiusUV = FMath::Clamp(PaintSettings.RadiusUV, 0.0001f, 0.5f);
    PaintSettings.Strength = FMath::Clamp(PaintSettings.Strength, 0.0f, 1.0f);
    PaintSettings.Falloff = FMath::Clamp(PaintSettings.Falloff, 0.0f, 1.0f);
    PaintSettings.Spacing = FMath::Clamp(PaintSettings.Spacing, 0.01f, 2.0f);
    PaintSettings.TargetAlpha = FMath::Clamp(PaintSettings.TargetAlpha, 0.0f, 1.0f);
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
    RebuildTransparencyPreviewTexture();
    RefreshBrushCursor();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::SetAutoBakePreviewResult(
    TSharedPtr<const FDWCTransparencyAutoBakeResult> InResult)
{
    if (AutoBakePreviewResult == InResult)
    {
        ApplyTransparencyPreviewParameters();
        return;
    }

    AutoBakePreviewResult = MoveTemp(InResult);
    RebuildWrinkleSuppressionBuffer();
    RebuildOuterEdgeFeatherBuffer();
    RebuildManualOverridesFromStrokes();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ClearAutoBakePreviewResult()
{
    AutoBakePreviewResult.Reset();
    WrinkleSuppressionBuffer.Reset();
    OuterEdgeFeatherBuffer.Reset();
    OuterEdgeFeatherBuffer.Reset();
    ManualPremultipliedBuffer.Reset();
    ManualWeightBuffer.Reset();
    LastHoverDirtyRect = FIntRect();
    PendingPreviewDirtyRect = FIntRect();
    TransparencyPreviewTexture = nullptr;
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

TSharedRef<FEditorViewportClient> SWetClothingTransparencyPreviewViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FDWCTransparencyPreviewViewportClient>(PreviewScene.Get(), SharedThis(this));
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
        RightSection.AddEntry(UE::UnrealEd::CreateViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(
        UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SWetClothingTransparencyPreviewViewport::ClearPreview()
{
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
    PreviewMIDs.Reset();
    TransparencyPreviewBaseMaterials.Reset();
    TransparencyPreviewMaterialParents.Reset();
    BrushCursorComponent = nullptr;
    CachedHitTriangles.Reset();
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
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
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);
    EnsureBrushCursor();
    RebuildHitTriangles();

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
        if (MeshComponent != nullptr && MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh())
        {
            ConfigurePreviewMeshComponent(MeshComponent);
        }
    }

    PreviewMeshComponents.Append(MeshComponents);

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
    ApplyPreviewMaterials(MeshComponent);
    ApplyWetnessPreview(MeshComponent);
    MeshComponent->MarkRenderStateDirty();
}

void SWetClothingTransparencyPreviewViewport::ApplyPreviewMaterials(USkeletalMeshComponent* MeshComponent)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MeshComponent == nullptr)
    {
        return;
    }

    const bool bIsTargetMesh = MeshComponent->GetSkeletalMeshAsset() == Asset->GetDWCSkeletalMesh();
    if (!bIsTargetMesh)
    {
        return;
    }

    const int32 MaterialCount = MeshComponent->GetNumMaterials();
    PreviewMIDs.Init(nullptr, MaterialCount);
    TransparencyPreviewBaseMaterials.Init(nullptr, MaterialCount);
    TransparencyPreviewMaterialParents.Init(nullptr, MaterialCount);

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        if (UMaterialInstanceConstant* CpuMaterial =
                DWCEditorPreviewSlotUtils::ResolveCpuPreviewMaterial(Asset, MaterialSlotIndex))
        {
            MeshComponent->SetMaterial(MaterialSlotIndex, CpuMaterial);
        }
    }

    if (SelectedMaterialSlotIndex >= 0 && SelectedMaterialSlotIndex < MaterialCount)
    {
        UMaterialInstanceConstant* CpuMaterial =
            DWCEditorPreviewSlotUtils::ResolveCpuPreviewMaterial(Asset, SelectedMaterialSlotIndex);
        if (CpuMaterial != nullptr)
        {
            if (UMaterialInstanceDynamic* PreviewMID = GetOrBuildSelectedPreviewMID(CpuMaterial))
            {
                MeshComponent->SetMaterial(SelectedMaterialSlotIndex, PreviewMID);
                PreviewMIDs[SelectedMaterialSlotIndex] = PreviewMID;
                TransparencyPreviewBaseMaterials[SelectedMaterialSlotIndex] = CachedPreviewBaseMaterial;
                TransparencyPreviewMaterialParents[SelectedMaterialSlotIndex] = CachedPreviewMaterialParent;
            }
        }
        else
        {
            UE_LOG(
                LogTemp,
                Verbose,
                TEXT("DWC transparency preview skipped slot %d because its CPU DWC material is not ready."),
                SelectedMaterialSlotIndex);
        }
    }

    ApplyTransparencyPreviewParameters();
}

UMaterialInstanceDynamic* SWetClothingTransparencyPreviewViewport::GetOrBuildSelectedPreviewMID(UMaterialInterface* SourceMaterial)
{
    if (SourceMaterial == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0)
    {
        return nullptr;
    }

    const bool bCacheMatches =
        CachedPreviewMID != nullptr &&
        CachedPreviewSourceMaterial == SourceMaterial &&
        CachedPreviewMaterialSlotIndex == SelectedMaterialSlotIndex &&
        CachedPreviewUVChannelIndex == SelectedUVChannelIndex;
    if (bCacheMatches)
    {
        return CachedPreviewMID;
    }

    CachedPreviewSourceMaterial = nullptr;
    CachedPreviewBaseMaterial = nullptr;
    CachedPreviewMaterialParent = nullptr;
    CachedPreviewMID = nullptr;
    CachedPreviewMaterialSlotIndex = INDEX_NONE;
    CachedPreviewUVChannelIndex = INDEX_NONE;

    FWetTransparencyPreviewMaterialBuildArgs BuildArgs;
    BuildArgs.SourceMaterial = SourceMaterial;
    BuildArgs.UVChannelIndex = SelectedUVChannelIndex;
    const FWetTransparencyPreviewMaterialBuildResult BuildResult =
        FWetTransparencyPreviewMaterialBuilder::Build(BuildArgs);
    if (!BuildResult.bSucceeded || BuildResult.PreviewMID == nullptr)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC transparency preview material build failed for slot %d ('%s'): %s"),
            SelectedMaterialSlotIndex,
            *GetNameSafe(SourceMaterial),
            *BuildResult.ErrorMessage);
        return nullptr;
    }

    CachedPreviewSourceMaterial = SourceMaterial;
    CachedPreviewBaseMaterial = BuildResult.TransientBaseMaterial;
    CachedPreviewMaterialParent = BuildResult.TransientMaterialParent;
    CachedPreviewMID = BuildResult.PreviewMID;
    CachedPreviewMaterialSlotIndex = SelectedMaterialSlotIndex;
    CachedPreviewUVChannelIndex = SelectedUVChannelIndex;
    return CachedPreviewMID;
}

void SWetClothingTransparencyPreviewViewport::ApplyWetnessPreview(USkeletalMeshComponent* MeshComponent)
{
    if (MeshComponent == nullptr)
    {
        return;
    }

    const float Wetness = FMath::Clamp(WetnessPreviewPercent / 100.0f, 0.0f, 1.0f);
    for (UMaterialInstanceDynamic* MID : PreviewMIDs)
    {
        if (MID != nullptr)
        {
            MID->SetScalarParameterValue(WetTransparencyPreviewMaterialParameters::PreviewWetness, Wetness);
        }
    }
    ApplyTransparencyPreviewParameters();
}

void SWetClothingTransparencyPreviewViewport::ApplyTransparencyPreviewParameters()
{
    for (UMaterialInstanceDynamic* MID : PreviewMIDs)
    {
        if (MID != nullptr)
        {
            MID->SetTextureParameterValue(TransparencyMapParameterName, nullptr);
            MID->SetScalarParameterValue(UseRuntimeTransparencyParameterName, 0.0f);
        }
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const bool bResultMatchesSelection = AutoBakePreviewResult.IsValid() &&
        AutoBakePreviewResult->LayerGuid == SelectedLayerGuid &&
        AutoBakePreviewResult->MaterialSlotIndex == SelectedMaterialSlotIndex &&
        AutoBakePreviewResult->UVChannelIndex == SelectedUVChannelIndex;
    if (!PreviewMIDs.IsValidIndex(SelectedMaterialSlotIndex))
    {
        return;
    }

    UMaterialInstanceDynamic* MID = PreviewMIDs[SelectedMaterialSlotIndex];
    if (MID == nullptr)
    {
        return;
    }

    const int32 PreviewLODIndex = bResultMatchesSelection
        ? AutoBakePreviewResult->LODIndex
        : (Asset != nullptr ? Asset->GetSimulationLODIndex() : 0);
    UTexture2D* PreviewTransparencyMap =
        bResultMatchesSelection && TransparencyPreviewTexture != nullptr
        ? TransparencyPreviewTexture.Get()
        : nullptr;
    if (PreviewTransparencyMap == nullptr && Layer != nullptr)
    {
        if (const FWetClothingBakedTransparencyMap* BakedMap = Layer->BakedMaps.FindByPredicate(
                [this, PreviewLODIndex](const FWetClothingBakedTransparencyMap& Candidate)
                {
                    return Candidate.MaterialSlotIndex == SelectedMaterialSlotIndex &&
                           Candidate.UVChannelIndex == SelectedUVChannelIndex &&
                           Candidate.LODIndex == PreviewLODIndex &&
                           Candidate.TransparencyMap != nullptr;
                }))
        {
            PreviewTransparencyMap = BakedMap->TransparencyMap;
        }
    }
    if (PreviewTransparencyMap == nullptr)
    {
        return;
    }

    MID->SetTextureParameterValue(TransparencyMapParameterName, PreviewTransparencyMap);
    MID->SetScalarParameterValue(
        WetTransparencyPreviewMaterialParameters::PreviewWetness,
        WetnessPreviewPercent / 100.0f);
    MID->SetScalarParameterValue(UseRuntimeTransparencyParameterName, 1.0f);
    MID->SetScalarParameterValue(TransparencyWetnessMinParameterName, 0.0f);
    MID->SetScalarParameterValue(TransparencyWetnessMaxParameterName, 1.0f);

    const FWetWrinkleBakedMapSet* WrinkleMap = FindExactWrinkleNormalMap(
        Asset,
        SelectedMaterialSlotIndex,
        SelectedUVChannelIndex,
        PreviewLODIndex);
    const bool bHasWrinkleNormal = WrinkleMap != nullptr;
    if (bHasWrinkleNormal)
    {
        MID->SetTextureParameterValue(WrinkleNormalMapParameterName, WrinkleMap->BakedWrinkleNormalMap);
    }
    MID->SetScalarParameterValue(UseWrinkleNormalMapParameterName, bHasWrinkleNormal ? 1.0f : 0.0f);
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
    return PaintSettings.bEnabled && PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        AutoBakePreviewResult.IsValid() && TransparencyPreviewTexture != nullptr &&
        SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelIndex >= 0 &&
        (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
         VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha);
}

void SWetClothingTransparencyPreviewViewport::RebuildHitTriangles()
{
    CachedHitTriangles.Reset();
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr || TargetMeshPreviewComponent == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0)
    {
        return;
    }

    TArray<FWetClothingAssetUVIsland> Islands;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            Asset->GetDWCSkeletalMesh(),
            0,
            SelectedUVChannelIndex,
            SelectedMaterialSlotIndex,
            Islands,
            nullptr))
    {
        return;
    }

    const FTransform ComponentTransform = TargetMeshPreviewComponent->GetComponentTransform();
    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            FDWCTransparencyCachedHitTriangle& Cached = CachedHitTriangles.AddDefaulted_GetRef();
            Cached.MaterialSlotIndex = Triangle.MaterialSlotIndex;
            Cached.TriangleID = Triangle.TriangleID;
            for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
            {
                Cached.LocalPositions[VertexIndex] = Triangle.LocalPositions[VertexIndex];
                Cached.WorldPositions[VertexIndex] = ComponentTransform.TransformPosition(Triangle.LocalPositions[VertexIndex]);
                Cached.UVs[VertexIndex] = Triangle.UVs[VertexIndex];
                Cached.WorldBounds += Cached.WorldPositions[VertexIndex];
            }
            Cached.WorldNormal = FVector::CrossProduct(
                Cached.WorldPositions[1] - Cached.WorldPositions[0],
                Cached.WorldPositions[2] - Cached.WorldPositions[0]).GetSafeNormal();
            if (Cached.WorldNormal.IsNearlyZero())
            {
                Cached.WorldNormal = FVector::UpVector;
            }
            Cached.WorldTangent = (Cached.WorldPositions[1] - Cached.WorldPositions[0]).GetSafeNormal();
            Cached.WorldTangent = (Cached.WorldTangent - Cached.WorldNormal * FVector::DotProduct(Cached.WorldTangent, Cached.WorldNormal)).GetSafeNormal();
            if (Cached.WorldTangent.IsNearlyZero())
            {
                Cached.WorldTangent = AnyPerpendicular(Cached.WorldNormal);
            }
        }
    }
    RebuildHitTriangleAccelerationStructures();
}

void SWetClothingTransparencyPreviewViewport::RebuildHitTriangleAccelerationStructures()
{
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
    if (CachedHitTriangles.IsEmpty())
    {
        return;
    }

    HitBVHTriangleIndices.Reserve(CachedHitTriangles.Num());
    for (int32 TriangleIndex = 0; TriangleIndex < CachedHitTriangles.Num(); ++TriangleIndex)
    {
        HitBVHTriangleIndices.Add(TriangleIndex);
    }

    TFunction<int32(int32, int32)> BuildNode;
    BuildNode = [this, &BuildNode](const int32 FirstIndex, const int32 TriangleCount)
    {
        const int32 NodeIndex = HitBVHNodes.AddDefaulted();
        FBox Bounds(ForceInit);
        FBox CenterBounds(ForceInit);
        for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
        {
            const FDWCTransparencyCachedHitTriangle& Triangle = CachedHitTriangles[HitBVHTriangleIndices[FirstIndex + Offset]];
            Bounds += Triangle.WorldBounds;
            CenterBounds += Triangle.WorldBounds.GetCenter();
        }

        HitBVHNodes[NodeIndex].Bounds = Bounds;
        HitBVHNodes[NodeIndex].FirstTriangleIndex = FirstIndex;
        HitBVHNodes[NodeIndex].TriangleCount = TriangleCount;
        if (TriangleCount <= TransparencyHitBVHLeafTriangleCount)
        {
            return NodeIndex;
        }

        const FVector Extent = CenterBounds.GetExtent();
        const int32 SplitAxis = Extent.Y > Extent.X
            ? (Extent.Z > Extent.Y ? 2 : 1)
            : (Extent.Z > Extent.X ? 2 : 0);
        TArrayView<int32> TriangleRange(HitBVHTriangleIndices.GetData() + FirstIndex, TriangleCount);
        Algo::Sort(TriangleRange, [this, SplitAxis](const int32 A, const int32 B)
        {
            return CachedHitTriangles[A].WorldBounds.GetCenter()[SplitAxis] <
                   CachedHitTriangles[B].WorldBounds.GetCenter()[SplitAxis];
        });

        const int32 LeftCount = TriangleCount / 2;
        const int32 LeftChild = BuildNode(FirstIndex, LeftCount);
        const int32 RightChild = BuildNode(FirstIndex + LeftCount, TriangleCount - LeftCount);
        // Recursive AddDefaulted calls may grow the backing array, so reacquire by index.
        HitBVHNodes[NodeIndex].LeftChildIndex = LeftChild;
        HitBVHNodes[NodeIndex].RightChildIndex = RightChild;
        HitBVHNodes[NodeIndex].TriangleCount = 0;
        return NodeIndex;
    };

    BuildNode(0, HitBVHTriangleIndices.Num());
}

bool SWetClothingTransparencyPreviewViewport::TraceSurface(
    const FVector& RayOrigin,
    const FVector& RayDirection,
    FDWCTransparencySurfaceHit& OutHit) const
{
    OutHit = FDWCTransparencySurfaceHit();
    const FVector Direction = RayDirection.GetSafeNormal();
    if (Direction.IsNearlyZero() || CachedHitTriangles.IsEmpty())
    {
        return false;
    }

    const FVector RayEnd = RayOrigin + Direction * 1000000.0f;
    auto TestTriangle = [&OutHit, &RayOrigin, &RayEnd, &Direction](const FDWCTransparencyCachedHitTriangle& Triangle)
    {
        if (Triangle.WorldBounds.IsValid && !DoesTransparencySegmentIntersectBox(Triangle.WorldBounds.ExpandBy(0.1f), RayOrigin, RayEnd))
        {
            return;
        }
        FVector Intersection = FVector::ZeroVector;
        FVector TriangleNormal = FVector::ZeroVector;
        if (!FMath::SegmentTriangleIntersection(
                RayOrigin,
                RayEnd,
                Triangle.WorldPositions[0],
                Triangle.WorldPositions[1],
                Triangle.WorldPositions[2],
                Intersection,
                TriangleNormal))
        {
            return;
        }
        const double DistanceSq = FVector::DistSquared(RayOrigin, Intersection);
        if (DistanceSq >= OutHit.DistanceSq)
        {
            return;
        }

        FVector Normal = Triangle.WorldNormal;
        if (FVector::DotProduct(Normal, Direction) > 0.0f)
        {
            Normal *= -1.0f;
        }
        FVector Tangent = (Triangle.WorldTangent - Normal * FVector::DotProduct(Triangle.WorldTangent, Normal)).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = AnyPerpendicular(Normal);
        }
        const FVector Barycentric = ComputeBarycentric(
            Intersection,
            Triangle.WorldPositions[0],
            Triangle.WorldPositions[1],
            Triangle.WorldPositions[2]);

        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.WorldPosition = Intersection;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.UV = Triangle.UVs[0] * Barycentric.X + Triangle.UVs[1] * Barycentric.Y + Triangle.UVs[2] * Barycentric.Z;
        OutHit.DistanceSq = DistanceSq;
    };

    if (!HitBVHNodes.IsEmpty())
    {
        TArray<int32, TInlineAllocator<64>> NodeStack;
        NodeStack.Add(0);
        while (!NodeStack.IsEmpty())
        {
            const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
            if (!HitBVHNodes.IsValidIndex(NodeIndex))
            {
                continue;
            }
            const FDWCTransparencyHitBVHNode& Node = HitBVHNodes[NodeIndex];
            if (!Node.Bounds.IsValid || !DoesTransparencySegmentIntersectBox(Node.Bounds.ExpandBy(0.1f), RayOrigin, RayEnd))
            {
                continue;
            }
            if (Node.IsLeaf())
            {
                for (int32 Offset = 0; Offset < Node.TriangleCount; ++Offset)
                {
                    const int32 OrderedIndex = Node.FirstTriangleIndex + Offset;
                    if (HitBVHTriangleIndices.IsValidIndex(OrderedIndex) && CachedHitTriangles.IsValidIndex(HitBVHTriangleIndices[OrderedIndex]))
                    {
                        TestTriangle(CachedHitTriangles[HitBVHTriangleIndices[OrderedIndex]]);
                    }
                }
            }
            else
            {
                if (Node.LeftChildIndex != INDEX_NONE)
                {
                    NodeStack.Add(Node.LeftChildIndex);
                }
                if (Node.RightChildIndex != INDEX_NONE)
                {
                    NodeStack.Add(Node.RightChildIndex);
                }
            }
        }
    }
    else
    {
        for (const FDWCTransparencyCachedHitTriangle& Triangle : CachedHitTriangles)
        {
            TestTriangle(Triangle);
        }
    }
    return OutHit.bHit;
}

void SWetClothingTransparencyPreviewViewport::EnsureBrushCursor()
{
    if (BrushCursorComponent == nullptr || BrushCursorComponent->GetNumSections() > 0)
    {
        return;
    }

    constexpr int32 SegmentCount = 48;
    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    TArray<FProcMeshTangent> Tangents;
    for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
    {
        const float Angle = UE_TWO_PI * Segment / SegmentCount;
        const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f);
        Vertices.Add(Direction);
        Vertices.Add(Direction * 0.91f);
        Normals.Add(FVector::UpVector);
        Normals.Add(FVector::UpVector);
        UVs.Add(FVector2D::ZeroVector);
        UVs.Add(FVector2D::ZeroVector);
        Colors.Add(FLinearColor(0.05f, 0.85f, 1.0f, 1.0f));
        Colors.Add(FLinearColor(0.05f, 0.85f, 1.0f, 1.0f));
        Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
        Tangents.Add(FProcMeshTangent(FVector::ForwardVector, false));
    }
    for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
    {
        const int32 Next = (Segment + 1) % SegmentCount;
        const int32 A = Segment * 2;
        const int32 B = Next * 2;
        Indices.Append({A, B, B + 1, A, B + 1, A + 1});
    }
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
    BrushCursorComponent->SetVisibility(false, true);
    if (!CanPaint() || !CurrentSurfaceHit.bHit || TargetMeshPreviewComponent == nullptr)
    {
        return;
    }
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(TargetMeshPreviewComponent->Bounds.SphereRadius));
    const float Radius = FMath::Clamp(MeshRadius * PaintSettings.RadiusUV, 0.25f, MeshRadius * 0.35f);
    const FVector Normal = CurrentSurfaceHit.WorldNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    FVector Tangent = (CurrentSurfaceHit.WorldTangent - Normal * FVector::DotProduct(CurrentSurfaceHit.WorldTangent, Normal)).GetSafeNormal();
    if (Tangent.IsNearlyZero())
    {
        Tangent = AnyPerpendicular(Normal);
    }
    BrushCursorComponent->SetWorldLocationAndRotation(
        CurrentSurfaceHit.WorldPosition + Normal * FMath::Max(1.0f, Radius * 0.12f),
        FRotationMatrix::MakeFromXZ(Tangent, Normal).ToQuat());
    BrushCursorComponent->SetWorldScale3D(FVector(Radius));
    BrushCursorComponent->SetVisibility(true, true);
    BrushCursorComponent->MarkRenderStateDirty();
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
    RefreshHoverPreviewRegion();
    RefreshBrushCursor();
}

void SWetClothingTransparencyPreviewViewport::BeginPaintStrokeFromClient(const FDWCTransparencySurfaceHit& SurfaceHit)
{
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!CanPaint() || Layer == nullptr || Asset == nullptr || !SurfaceHit.bHit)
    {
        return;
    }

    ActivePaintTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("PaintTransparencyStroke", "Paint Transparency Stroke"));
    Asset->Modify();
    FDWCTransparencyBrushStroke& Stroke = Layer->EditableStrokes.AddDefaulted_GetRef();
    Stroke.StrokeGuid = FGuid::NewGuid();
    Stroke.DisplayName = FString::Printf(TEXT("Stroke %d"), Layer->EditableStrokes.Num());
    Stroke.MaterialSlotIndex = SelectedMaterialSlotIndex;
    Stroke.UVChannelIndex = SelectedUVChannelIndex;
    Stroke.UVAddressMode = SelectedUVAddressMode;
    Stroke.BrushMode = PaintSettings.Mode;
    Stroke.Falloff = PaintSettings.Falloff;
    Stroke.TargetAlpha = PaintSettings.TargetAlpha;
    Stroke.Spacing = PaintSettings.Spacing;
    ActiveStrokeGuid = Stroke.StrokeGuid;
    RefreshHoverPreviewRegion();
    LastPointerUV = SurfaceHit.UV;
    DistanceToNextStamp = PaintSettings.RadiusUV * PaintSettings.Spacing;
    AppendPaintSample(SurfaceHit.UV);
}

void SWetClothingTransparencyPreviewViewport::RequestPaintStampFromClient(const FDWCTransparencySurfaceHit& SurfaceHit)
{
    if (!ActiveStrokeGuid.IsValid() || !SurfaceHit.bHit)
    {
        return;
    }
    FVector2D Delta = SurfaceHit.UV - LastPointerUV;
    if (SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap)
    {
        Delta.X -= FMath::RoundToDouble(Delta.X);
        Delta.Y -= FMath::RoundToDouble(Delta.Y);
    }
    float RemainingDistance = Delta.Size();
    FVector2D SegmentStart = LastPointerUV;
    const FVector2D Direction = RemainingDistance > UE_SMALL_NUMBER ? Delta / RemainingDistance : FVector2D::ZeroVector;
    while (RemainingDistance + UE_SMALL_NUMBER >= DistanceToNextStamp)
    {
        SegmentStart += Direction * DistanceToNextStamp;
        if (SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap)
        {
            SegmentStart.X -= FMath::FloorToDouble(SegmentStart.X);
            SegmentStart.Y -= FMath::FloorToDouble(SegmentStart.Y);
        }
        AppendPaintSample(SegmentStart);
        RemainingDistance -= DistanceToNextStamp;
        DistanceToNextStamp = FMath::Max(PaintSettings.RadiusUV * PaintSettings.Spacing, 0.00001f);
    }
    DistanceToNextStamp -= RemainingDistance;
    LastPointerUV = SurfaceHit.UV;
}

void SWetClothingTransparencyPreviewViewport::EndPaintStrokeFromClient()
{
    if (!ActiveStrokeGuid.IsValid())
    {
        return;
    }
    if (FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        Layer->MarkFinalBakeStale();
    }
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->MarkPackageDirty();
    }
    ActiveStrokeGuid.Invalidate();
    ActivePaintTransaction.Reset();
    RefreshHoverPreviewRegion();
    OnStrokesChanged.ExecuteIfBound();
}

void SWetClothingTransparencyPreviewViewport::AppendPaintSample(const FVector2D& PositionUV)
{
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return;
    }
    FDWCTransparencyBrushStroke* Stroke = Layer->EditableStrokes.FindByPredicate(
        [this](const FDWCTransparencyBrushStroke& Candidate)
        {
            return Candidate.StrokeGuid == ActiveStrokeGuid;
        });
    if (Stroke == nullptr)
    {
        return;
    }

    FDWCTransparencyBrushSample& Sample = Stroke->Samples.AddDefaulted_GetRef();
    Sample.PositionUV = PositionUV;
    Sample.RadiusUV = PaintSettings.RadiusUV;
    Sample.Strength = PaintSettings.Strength;
    FIntRect DirtyRect;
    if (RasterizeBrushSample(*Stroke, Sample, &DirtyRect))
    {
        UpdatePreviewTextureRegion(DirtyRect);
    }
}

bool SWetClothingTransparencyPreviewViewport::RasterizeBrushSample(
    const FDWCTransparencyBrushStroke& Stroke,
    const FDWCTransparencyBrushSample& Sample,
    FIntRect* OutDirtyRect)
{
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }
    const int32 Width = AutoBakePreviewResult->Resolution.X;
    const int32 Height = AutoBakePreviewResult->Resolution.Y;
    const int32 PixelCount = Width * Height;
    if (Width <= 0 || Height <= 0 || ManualPremultipliedBuffer.Num() != PixelCount || ManualWeightBuffer.Num() != PixelCount)
    {
        return false;
    }

    const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const float RadiusPixelsX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
    const float RadiusPixelsY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
    const FVector2D CenterPixels(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
    const int32 MinX = FMath::FloorToInt(CenterPixels.X - RadiusPixelsX - 1.0f);
    const int32 MaxX = FMath::CeilToInt(CenterPixels.X + RadiusPixelsX + 1.0f);
    const int32 MinY = FMath::FloorToInt(CenterPixels.Y - RadiusPixelsY - 1.0f);
    const int32 MaxY = FMath::CeilToInt(CenterPixels.Y + RadiusPixelsY + 1.0f);
    auto WrapIndex = [](int32 Value, int32 Size)
    {
        return (Value % Size + Size) % Size;
    };

    const bool bSmooth = Stroke.BrushMode == EDWCTransparencyBrushMode::Smooth;
    const int32 SnapshotMinX = MinX - 1;
    const int32 SnapshotMinY = MinY - 1;
    const int32 SnapshotWidth = MaxX - MinX + 3;
    const int32 SnapshotHeight = MaxY - MinY + 3;
    TArray<uint8> PreviousPremultiplied;
    TArray<uint8> PreviousWeight;
    if (bSmooth)
    {
        const int32 SnapshotPixelCount = SnapshotWidth * SnapshotHeight;
        PreviousPremultiplied.SetNumUninitialized(SnapshotPixelCount);
        PreviousWeight.SetNumUninitialized(SnapshotPixelCount);
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
                PreviousPremultiplied[SnapshotIndex] = ManualPremultipliedBuffer[SourceIndex];
                PreviousWeight[SnapshotIndex] = ManualWeightBuffer[SourceIndex];
            }
        }
    }

    auto EditedAlphaAt = [this, Width, Height, bWrap, SnapshotMinX, SnapshotMinY, SnapshotWidth,
                          &PreviousPremultiplied, &PreviousWeight, &WrapIndex](int32 UnwrappedX, int32 UnwrappedY)
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
        const bool bHasSnapshotPixel = PreviousPremultiplied.IsValidIndex(SnapshotIndex) && PreviousWeight.IsValidIndex(SnapshotIndex);
        const float ManualWeight = (bHasSnapshotPixel ? PreviousWeight[SnapshotIndex] : ManualWeightBuffer[Index]) / 255.0f;
        const float ManualPremultiplied = (bHasSnapshotPixel ? PreviousPremultiplied[SnapshotIndex] : ManualPremultipliedBuffer[Index]) / 255.0f;
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
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            Target += EditedAlphaAt(UnwrappedX + OffsetX, UnwrappedY + OffsetY);
                        }
                    }
                    Target /= 9.0f;
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
        *OutDirtyRect = bWrap && (MinX < 0 || MinY < 0 || MaxX >= Width || MaxY >= Height)
            ? FIntRect(0, 0, Width, Height)
            : FIntRect(
                FMath::Clamp(MinX, 0, Width),
                FMath::Clamp(MinY, 0, Height),
                FMath::Clamp(MaxX + 1, 0, Width),
                FMath::Clamp(MaxY + 1, 0, Height));
    }
    return bChanged;
}

FIntRect SWetClothingTransparencyPreviewViewport::ComputeCurrentHoverDirtyRect() const
{
    if (!CanPaint() || ActiveStrokeGuid.IsValid() || !CurrentSurfaceHit.bHit || !AutoBakePreviewResult.IsValid())
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
    if (bWrap && (MinX < 0 || MinY < 0 || MaxX >= Width || MaxY >= Height))
    {
        return FIntRect(0, 0, Width, Height);
    }
    return FIntRect(
        FMath::Clamp(MinX, 0, Width),
        FMath::Clamp(MinY, 0, Height),
        FMath::Clamp(MaxX + 1, 0, Width),
        FMath::Clamp(MaxY + 1, 0, Height));
}

void SWetClothingTransparencyPreviewViewport::RefreshHoverPreviewRegion()
{
    const FIntRect NewHoverDirtyRect = ComputeCurrentHoverDirtyRect();
    FIntRect DirtyRect;
    if (LastHoverDirtyRect.IsEmpty())
    {
        DirtyRect = NewHoverDirtyRect;
    }
    else if (NewHoverDirtyRect.IsEmpty())
    {
        DirtyRect = LastHoverDirtyRect;
    }
    else
    {
        DirtyRect = FIntRect(
            FMath::Min(LastHoverDirtyRect.Min.X, NewHoverDirtyRect.Min.X),
            FMath::Min(LastHoverDirtyRect.Min.Y, NewHoverDirtyRect.Min.Y),
            FMath::Max(LastHoverDirtyRect.Max.X, NewHoverDirtyRect.Max.X),
            FMath::Max(LastHoverDirtyRect.Max.Y, NewHoverDirtyRect.Max.Y));
    }
    LastHoverDirtyRect = NewHoverDirtyRect;
    if (!DirtyRect.IsEmpty())
    {
        UpdatePreviewTextureRegion(DirtyRect);
    }
}

void SWetClothingTransparencyPreviewViewport::RebuildManualOverridesFromStrokes()
{
    ManualPremultipliedBuffer.Reset();
    ManualWeightBuffer.Reset();
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
        *Layer,
        SelectedMaterialSlotIndex,
        SelectedUVChannelIndex,
        ManualPremultipliedBuffer,
        ManualWeightBuffer);
}

void SWetClothingTransparencyPreviewViewport::RefreshManualPreviewFromStrokes()
{
    RebuildManualOverridesFromStrokes();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

bool SWetClothingTransparencyPreviewViewport::RebuildWrinkleSuppressionBuffer()
{
    WrinkleSuppressionBuffer.Reset();
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const FDWCTransparencyAutoBakeResult& Result = *AutoBakePreviewResult;
    const int32 Width = Result.Resolution.X;
    const int32 Height = Result.Resolution.Y;
    const int32 PixelCount = Width * Height;
    if (Width <= 0 || Height <= 0 || PixelCount <= 0)
    {
        return false;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return true;
    }

    const FDWCWrinkleSuppressionSource SuppressionSource =
        FDWCWrinkleSuppressionProcessor::FindExactSource(
        Asset,
        SelectedMaterialSlotIndex,
        SelectedUVChannelIndex,
        Result.LODIndex);
    if (!SuppressionSource.IsValid())
    {
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return true;
    }

    FString ProcessingError;
    if (!FDWCWrinkleSuppressionProcessor::BuildProcessedBuffer(
            SuppressionSource,
            Result.Resolution,
            Asset->Authored.TransparencyData.WrinkleSuppressionCoverageThreshold,
            Asset->Authored.TransparencyData.WrinkleSuppressionMaskSoftness,
            WrinkleSuppressionBuffer,
            ProcessingError))
    {
        WrinkleSuppressionBuffer.Init(0, PixelCount);
        return true;
    }
    return true;
}

bool SWetClothingTransparencyPreviewViewport::RebuildOuterEdgeFeatherBuffer()
{
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
    if (!PaintSettings.bEnabled || ActiveStrokeGuid.IsValid() || !CurrentSurfaceHit.bHit ||
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
                TargetAlpha += GetStoredEditedAlpha(SampleY * Width + SampleX);
            }
        }
        TargetAlpha /= 9.0f;
    }
    return FMath::Clamp(FMath::Lerp(EditedAlpha, TargetAlpha, BrushWeight), 0.0f, 1.0f);
}

bool SWetClothingTransparencyPreviewViewport::BuildVisualizationPixels(TArray<FColor>& OutPixels) const
{
    OutPixels.Reset();
    if (!AutoBakePreviewResult.IsValid())
    {
        return false;
    }

    const FDWCTransparencyAutoBakeResult& Result = *AutoBakePreviewResult;
    const int32 PixelCount = Result.Resolution.X * Result.Resolution.Y;
    if (Result.Resolution.X <= 0 || Result.Resolution.Y <= 0 ||
        Result.InnerColorBuffer.Num() != PixelCount || Result.AutoAlphaBuffer.Num() != PixelCount)
    {
        return false;
    }

    OutPixels.SetNumUninitialized(PixelCount);
    float MaximumHitDistance = 0.0f;
    if (VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance)
    {
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            if (Result.ValidHitBuffer.IsValidIndex(PixelIndex) && Result.ValidHitBuffer[PixelIndex] != 0 &&
                Result.HitDistanceBuffer.IsValidIndex(PixelIndex))
            {
                MaximumHitDistance = FMath::Max(MaximumHitDistance, Result.HitDistanceBuffer[PixelIndex]);
            }
        }
        MaximumHitDistance = FMath::Max(MaximumHitDistance, KINDA_SMALL_NUMBER);
    }

    static const FColor PriorityColors[] =
    {
        FColor(230, 70, 70), FColor(70, 170, 240), FColor(80, 210, 120), FColor(235, 185, 65),
        FColor(180, 95, 225), FColor(65, 215, 205), FColor(240, 120, 185), FColor(180, 180, 180)
    };

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const float EditedAlpha = ApplyHoverToEditedAlpha(PixelIndex, GetStoredEditedAlpha(PixelIndex));
        const uint8 Alpha = FDWCTransparencyComposite::ResolveFinalAlpha8(
            EditedAlpha,
            TransparencyPreviewStrength,
            WrinkleSuppressionBuffer.IsValidIndex(PixelIndex) ? WrinkleSuppressionBuffer[PixelIndex] : 0,
            WrinkleSuppressionStrength);
        const uint8 FeatheredAlpha = OuterEdgeFeatherBuffer.IsValidIndex(PixelIndex)
            ? static_cast<uint8>(
                (static_cast<uint32>(Alpha) * OuterEdgeFeatherBuffer[PixelIndex] + 127u) / 255u)
            : Alpha;
        FColor Pixel = Result.InnerColorBuffer[PixelIndex];
        Pixel.A = FeatheredAlpha;

        switch (VisualizationMode)
        {
        case EDWCTransparencyVisualizationMode::InnerColor:
            Pixel.A = 255;
            break;
        case EDWCTransparencyVisualizationMode::AutoAlpha:
            Pixel = FColor(FeatheredAlpha, FeatheredAlpha, FeatheredAlpha, FeatheredAlpha);
            break;
        case EDWCTransparencyVisualizationMode::WrinkleSeparation:
        {
            const uint8 Separation = WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
                ? WrinkleSuppressionBuffer[PixelIndex]
                : 0;
            Pixel = FColor(Separation, Separation, Separation, 255);
            break;
        }
        case EDWCTransparencyVisualizationMode::ValidHit:
        {
            const bool bValid = Result.ValidHitBuffer.IsValidIndex(PixelIndex) && Result.ValidHitBuffer[PixelIndex] != 0;
            Pixel = bValid ? FColor(70, 210, 95, 255) : FColor(25, 25, 25, 255);
            break;
        }
        case EDWCTransparencyVisualizationMode::HitDistance:
        {
            const float Distance = Result.HitDistanceBuffer.IsValidIndex(PixelIndex) ? Result.HitDistanceBuffer[PixelIndex] : 0.0f;
            const uint8 Value = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Distance / MaximumHitDistance, 0.0f, 1.0f) * 255.0f));
            Pixel = FColor(Value, 32, 255 - Value, 255);
            break;
        }
        case EDWCTransparencyVisualizationMode::RayConfidence:
        {
            const float Confidence = Result.RayConfidenceBuffer.IsValidIndex(PixelIndex)
                ? Result.RayConfidenceBuffer[PixelIndex] / 255.0f
                : 0.0f;
            const uint8 Value = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Confidence, 0.0f, 1.0f) * 255.0f));
            Pixel = FColor(Value, Value, Value, 255);
            break;
        }
        case EDWCTransparencyVisualizationMode::SourcePriority:
        {
            const int32 Priority = Result.SourcePriorityBuffer.IsValidIndex(PixelIndex) ? Result.SourcePriorityBuffer[PixelIndex] : INDEX_NONE;
            Pixel = Priority >= 0
                ? PriorityColors[Priority % UE_ARRAY_COUNT(PriorityColors)]
                : FColor(20, 20, 20, 255);
            break;
        }
        default:
            break;
        }

        OutPixels[PixelIndex] = Pixel;
    }
    return true;
}

FColor SWetClothingTransparencyPreviewViewport::BuildVisualizationPixel(const int32 PixelIndex) const
{
    if (!AutoBakePreviewResult.IsValid() || !AutoBakePreviewResult->InnerColorBuffer.IsValidIndex(PixelIndex) ||
        !AutoBakePreviewResult->AutoAlphaBuffer.IsValidIndex(PixelIndex))
    {
        return FColor::Black;
    }
    const float EditedAlpha = ApplyHoverToEditedAlpha(PixelIndex, GetStoredEditedAlpha(PixelIndex));
    const uint8 Alpha = FDWCTransparencyComposite::ResolveFinalAlpha8(
        EditedAlpha,
        TransparencyPreviewStrength,
        WrinkleSuppressionBuffer.IsValidIndex(PixelIndex) ? WrinkleSuppressionBuffer[PixelIndex] : 0,
        WrinkleSuppressionStrength);
    const uint8 FeatheredAlpha = OuterEdgeFeatherBuffer.IsValidIndex(PixelIndex)
        ? static_cast<uint8>(
            (static_cast<uint32>(Alpha) * OuterEdgeFeatherBuffer[PixelIndex] + 127u) / 255u)
        : Alpha;
    if (VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha)
    {
        return FColor(FeatheredAlpha, FeatheredAlpha, FeatheredAlpha, FeatheredAlpha);
    }
    if (VisualizationMode == EDWCTransparencyVisualizationMode::WrinkleSeparation)
    {
        const uint8 Separation = WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
            ? WrinkleSuppressionBuffer[PixelIndex]
            : 0;
        return FColor(Separation, Separation, Separation, 255);
    }
    FColor Pixel = AutoBakePreviewResult->InnerColorBuffer[PixelIndex];
    Pixel.A = FeatheredAlpha;
    return Pixel;
}

void SWetClothingTransparencyPreviewViewport::UpdatePreviewTextureRegion(const FIntRect& DirtyRect)
{
    if (DirtyRect.IsEmpty())
    {
        return;
    }
    PendingPreviewDirtyRect = PendingPreviewDirtyRect.IsEmpty()
        ? DirtyRect
        : FIntRect(
            FMath::Min(PendingPreviewDirtyRect.Min.X, DirtyRect.Min.X),
            FMath::Min(PendingPreviewDirtyRect.Min.Y, DirtyRect.Min.Y),
            FMath::Max(PendingPreviewDirtyRect.Max.X, DirtyRect.Max.X),
            FMath::Max(PendingPreviewDirtyRect.Max.Y, DirtyRect.Max.Y));
}

void SWetClothingTransparencyPreviewViewport::FlushPendingPreviewTextureUpdates()
{
    if (PendingPreviewDirtyRect.IsEmpty())
    {
        return;
    }
    const FIntRect DirtyRect = PendingPreviewDirtyRect;
    PendingPreviewDirtyRect = FIntRect();
    UploadPreviewTextureRegion(DirtyRect);
}

void SWetClothingTransparencyPreviewViewport::UploadPreviewTextureRegion(const FIntRect& DirtyRect)
{
    if (TransparencyPreviewTexture == nullptr || !AutoBakePreviewResult.IsValid() || DirtyRect.IsEmpty())
    {
        return;
    }
    const int32 TextureWidth = AutoBakePreviewResult->Resolution.X;
    const int32 RegionWidth = DirtyRect.Width();
    const int32 RegionHeight = DirtyRect.Height();
    if (RegionWidth <= 0 || RegionHeight <= 0)
    {
        return;
    }

    uint8* RegionData = new uint8[static_cast<int64>(RegionWidth) * RegionHeight * sizeof(FColor)];
    FColor* RegionColors = reinterpret_cast<FColor*>(RegionData);
    for (int32 Y = 0; Y < RegionHeight; ++Y)
    {
        for (int32 X = 0; X < RegionWidth; ++X)
        {
            const int32 PixelIndex = (DirtyRect.Min.Y + Y) * TextureWidth + DirtyRect.Min.X + X;
            RegionColors[Y * RegionWidth + X] = BuildVisualizationPixel(PixelIndex);
        }
    }

    FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
        DirtyRect.Min.X,
        DirtyRect.Min.Y,
        0,
        0,
        RegionWidth,
        RegionHeight);
    TransparencyPreviewTexture->UpdateTextureRegions(
        0,
        1,
        Region,
        RegionWidth * sizeof(FColor),
        sizeof(FColor),
        RegionData,
        [](uint8* Data, const FUpdateTextureRegion2D* Regions)
        {
            delete[] Data;
            delete Regions;
        });
    InvalidatePreviewViewport();
}

bool SWetClothingTransparencyPreviewViewport::RebuildTransparencyPreviewTexture()
{
    TArray<FColor> Pixels;
    if (!BuildVisualizationPixels(Pixels))
    {
        TransparencyPreviewTexture = nullptr;
        LastHoverDirtyRect = FIntRect();
        return false;
    }

    const FIntPoint Resolution = AutoBakePreviewResult->Resolution;
    const bool bNeedsNewTexture = TransparencyPreviewTexture == nullptr ||
        TransparencyPreviewTexture->GetSizeX() != Resolution.X ||
        TransparencyPreviewTexture->GetSizeY() != Resolution.Y;
    if (bNeedsNewTexture)
    {
        TransparencyPreviewTexture = UTexture2D::CreateTransient(Resolution.X, Resolution.Y, PF_B8G8R8A8);
        if (TransparencyPreviewTexture == nullptr || TransparencyPreviewTexture->GetPlatformData() == nullptr ||
            !TransparencyPreviewTexture->GetPlatformData()->Mips.IsValidIndex(0))
        {
            TransparencyPreviewTexture = nullptr;
            return false;
        }

        TransparencyPreviewTexture->SRGB = true;
        TransparencyPreviewTexture->NeverStream = true;
        TransparencyPreviewTexture->CompressionSettings = TC_Default;
        TransparencyPreviewTexture->MipGenSettings = TMGS_NoMipmaps;
        TransparencyPreviewTexture->Filter = TF_Bilinear;
        TransparencyPreviewTexture->LODGroup = TEXTUREGROUP_World;
    }

    const TextureAddress Address = SelectedUVAddressMode == EDWCTransparencyUVAddressMode::Wrap ? TA_Wrap : TA_Clamp;
    TransparencyPreviewTexture->AddressX = Address;
    TransparencyPreviewTexture->AddressY = Address;
    FTexture2DMipMap& Mip = TransparencyPreviewTexture->GetPlatformData()->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    TransparencyPreviewTexture->UpdateResource();
    LastHoverDirtyRect = ComputeCurrentHoverDirtyRect();
    PendingPreviewDirtyRect = FIntRect();
    return true;
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

#undef LOCTEXT_NAMESPACE
