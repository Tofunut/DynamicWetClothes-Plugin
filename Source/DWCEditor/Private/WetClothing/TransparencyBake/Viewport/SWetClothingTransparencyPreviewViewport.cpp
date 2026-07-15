#include "WetClothing/TransparencyBake/Viewport/SWetClothingTransparencyPreviewViewport.h"

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
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"
#include "WetClothing/TransparencyBake/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/TransparencyBake/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/TransparencyBake/Material/WetTransparencyPreviewMaterialBuilder.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyPreviewViewport"

namespace
{
    constexpr const TCHAR* UseRevealPreviewParameterName = TEXT("DWC_UseRevealPreview");
    constexpr const TCHAR* RevealPreviewBlendParameterName = TEXT("DWC_RevealPreviewBlend");
    constexpr const TCHAR* RevealMaskMultiplierParameterName = TEXT("DWC_RevealMaskMultiplier");
    constexpr const TCHAR* RevealConfidenceMultiplierParameterName = TEXT("DWC_RevealConfidenceMultiplier");
    constexpr const TCHAR* UseRuntimeTransparencyParameterName = TEXT("DWC_UseTransparencyMap");
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

    int32 GetLOD0VertexCount(const USkeletalMesh* SkeletalMesh)
    {
        if (SkeletalMesh == nullptr)
        {
            return 0;
        }

        const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(0))
        {
            return 0;
        }

        return RenderData->LODRenderData[0].GetNumVertices();
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
    const bool bAddressModeChanged = SelectedUVAddressMode != InAddressMode;
    const bool bContextChanged = SelectedLayerGuid != InLayerGuid ||
        SelectedMaterialSlotIndex != InMaterialSlotIndex ||
        SelectedUVChannelIndex != InUVChannelIndex ||
        SelectedUVAddressMode != InAddressMode;
    SelectedLayerGuid = InLayerGuid;
    SelectedMaterialSlotIndex = InMaterialSlotIndex;
    SelectedUVChannelIndex = InUVChannelIndex;
    SelectedUVAddressMode = InAddressMode;
    if (bMaterialSlotChanged)
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
    TransparencyPreviewStrength = FMath::Max(0.0f, InStrength);
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
    RebuildManualOverridesFromStrokes();
    RebuildTransparencyPreviewTexture();
    ApplyTransparencyPreviewParameters();
    InvalidatePreviewViewport();
}

void SWetClothingTransparencyPreviewViewport::ClearAutoBakePreviewResult()
{
    AutoBakePreviewResult.Reset();
    WrinkleSuppressionBuffer.Reset();
    ManualPremultipliedBuffer.Reset();
    ManualWeightBuffer.Reset();
    LastHoverDirtyRect = FIntRect();
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
}

void SWetClothingTransparencyPreviewViewport::BuildTargetMeshPreview()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->TargetMesh == nullptr || !PreviewScene.IsValid())
    {
        return;
    }

    TargetMeshPreviewComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    TargetMeshPreviewComponent->SetMobility(EComponentMobility::Movable);
    TargetMeshPreviewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TargetMeshPreviewComponent->SetSkeletalMeshAsset(Asset->TargetMesh);
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

    TSubclassOf<AActor> BlueprintClass = Asset->TransparencyData.SourceBlueprintClass.LoadSynchronous();
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
        if (MeshComponent != nullptr && MeshComponent->GetSkeletalMeshAsset() == Asset->TargetMesh)
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
    ApplyRevealMaterials(MeshComponent);
    ApplyWetnessPreview(MeshComponent);
    MeshComponent->MarkRenderStateDirty();
}

void SWetClothingTransparencyPreviewViewport::ApplyRevealMaterials(USkeletalMeshComponent* MeshComponent)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MeshComponent == nullptr)
    {
        return;
    }

    const bool bIsTargetMesh = MeshComponent->GetSkeletalMeshAsset() == Asset->TargetMesh;
    if (!bIsTargetMesh)
    {
        return;
    }

    const int32 MaterialCount = MeshComponent->GetNumMaterials();
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        const FWetClothingGeneratedWetMaterialOverride* WetOverride =
            Asset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex && Candidate.WetMaterial != nullptr;
                });
        if (WetOverride != nullptr)
        {
            MeshComponent->SetMaterial(MaterialSlotIndex, WetOverride->WetMaterial);
        }
    }

    for (const FWetClothingBakedTransparencyRevealLayer& BakedLayer : Asset->TransparencyData.BakedRevealLayers)
    {
        if (BakedLayer.MaterialSlotIndex == INDEX_NONE || BakedLayer.RevealMaterial == nullptr)
        {
            continue;
        }

        if (BakedLayer.MaterialSlotIndex < MeshComponent->GetNumMaterials())
        {
            MeshComponent->SetMaterial(BakedLayer.MaterialSlotIndex, BakedLayer.RevealMaterial);
        }
    }

    PreviewMIDs.SetNum(FMath::Max(PreviewMIDs.Num(), MaterialCount));
    TransparencyPreviewBaseMaterials.SetNum(FMath::Max(TransparencyPreviewBaseMaterials.Num(), MaterialCount));
    TransparencyPreviewMaterialParents.SetNum(FMath::Max(TransparencyPreviewMaterialParents.Num(), MaterialCount));
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        UMaterialInstanceDynamic* MID = nullptr;
        if (MaterialSlotIndex == SelectedMaterialSlotIndex)
        {
            FWetTransparencyPreviewMaterialBuildArgs BuildArgs;
            BuildArgs.SourceMaterial = MeshComponent->GetMaterial(MaterialSlotIndex);
            FWetTransparencyPreviewMaterialBuildResult BuildResult =
                FWetTransparencyPreviewMaterialBuilder::Build(BuildArgs);
            if (BuildResult.bSucceeded && BuildResult.PreviewMID != nullptr)
            {
                TransparencyPreviewBaseMaterials[MaterialSlotIndex] = BuildResult.TransientBaseMaterial;
                TransparencyPreviewMaterialParents[MaterialSlotIndex] = BuildResult.TransientMaterialParent;
                MID = BuildResult.PreviewMID;
                MeshComponent->SetMaterial(MaterialSlotIndex, MID);
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DWC transparency preview material build failed for slot %d ('%s'): %s"),
                    MaterialSlotIndex,
                    *GetNameSafe(BuildArgs.SourceMaterial),
                    *BuildResult.ErrorMessage);
            }
        }

        if (MID == nullptr)
        {
            MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(MaterialSlotIndex);
        }
        PreviewMIDs[MaterialSlotIndex] = MID;
    }

    for (const FWetClothingBakedTransparencyRevealLayer& BakedLayer : Asset->TransparencyData.BakedRevealLayers)
    {
        if (!PreviewMIDs.IsValidIndex(BakedLayer.MaterialSlotIndex))
        {
            continue;
        }

        if (UMaterialInstanceDynamic* MID = PreviewMIDs[BakedLayer.MaterialSlotIndex])
        {
            MID->SetScalarParameterValue(UseRevealPreviewParameterName, 1.0f);
            MID->SetScalarParameterValue(RevealPreviewBlendParameterName, 1.0f);
            MID->SetScalarParameterValue(RevealMaskMultiplierParameterName, 1.0f);
            MID->SetScalarParameterValue(RevealConfidenceMultiplierParameterName, 1.0f);
        }
    }

    ApplyTransparencyPreviewParameters();
}

void SWetClothingTransparencyPreviewViewport::ApplyWetnessPreview(USkeletalMeshComponent* MeshComponent)
{
    if (MeshComponent == nullptr || MeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const float Wetness = FMath::Clamp(WetnessPreviewPercent / 100.0f, 0.0f, 1.0f);
    const int32 VertexCount = GetLOD0VertexCount(MeshComponent->GetSkeletalMeshAsset());
    if (VertexCount <= 0)
    {
        return;
    }

    TArray<FLinearColor> Colors;
    Colors.Init(FLinearColor(Wetness, Wetness, 0.0f, 1.0f), VertexCount);
    MeshComponent->SetVertexColorOverride_LinearColor(0, Colors);
    MeshComponent->MarkRenderStateDirty();
    MeshComponent->MarkRenderDynamicDataDirty();

    for (UMaterialInstanceDynamic* MID : PreviewMIDs)
    {
        if (MID != nullptr)
        {
            MID->SetScalarParameterValue(UseRevealPreviewParameterName, Wetness > 0.0f ? 1.0f : 0.0f);
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
            MID->SetScalarParameterValue(WetTransparencyPreviewMaterialParameters::Enabled, 0.0f);
            MID->SetScalarParameterValue(UseRuntimeTransparencyParameterName, 0.0f);
        }
    }

    const bool bResultMatchesSelection = AutoBakePreviewResult.IsValid() &&
        AutoBakePreviewResult->LayerGuid == SelectedLayerGuid &&
        AutoBakePreviewResult->MaterialSlotIndex == SelectedMaterialSlotIndex &&
        AutoBakePreviewResult->UVChannelIndex == SelectedUVChannelIndex;
    if (!bResultMatchesSelection || TransparencyPreviewTexture == nullptr ||
        !PreviewMIDs.IsValidIndex(SelectedMaterialSlotIndex))
    {
        return;
    }

    UMaterialInstanceDynamic* MID = PreviewMIDs[SelectedMaterialSlotIndex];
    if (MID == nullptr)
    {
        return;
    }

    MID->SetTextureParameterValue(WetTransparencyPreviewMaterialParameters::Map, TransparencyPreviewTexture);
    MID->SetScalarParameterValue(WetTransparencyPreviewMaterialParameters::Enabled, 1.0f);
    MID->SetScalarParameterValue(WetTransparencyPreviewMaterialParameters::Strength, TransparencyPreviewStrength);
    MID->SetScalarParameterValue(WetTransparencyPreviewMaterialParameters::Wetness, WetnessPreviewPercent / 100.0f);
    MID->SetScalarParameterValue(
        WetTransparencyPreviewMaterialParameters::UVChannel,
        static_cast<float>(FMath::Clamp(SelectedUVChannelIndex, 0, 7)));
    MID->SetScalarParameterValue(
        WetTransparencyPreviewMaterialParameters::Debug,
        VisualizationMode == EDWCTransparencyVisualizationMode::Final ? 0.0f : 1.0f);

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetWrinkleBakedMapSet* WrinkleMap = Asset != nullptr
        ? Asset->WrinkleData.FindBakedWrinkleMap(SelectedMaterialSlotIndex, SelectedUVChannelIndex, AutoBakePreviewResult->LODIndex)
        : nullptr;
    const bool bHasWrinkleCoverage = WrinkleMap != nullptr &&
        WrinkleMap->BakedWrinkleNormalMap != nullptr &&
        WrinkleMap->AlphaSemantic == EDWCWrinkleAlphaSemantic::ConvexSeparation;
    if (bHasWrinkleCoverage)
    {
        MID->SetTextureParameterValue(WrinkleNormalMapParameterName, WrinkleMap->BakedWrinkleNormalMap);
    }
    MID->SetScalarParameterValue(UseWrinkleNormalMapParameterName, bHasWrinkleCoverage ? 1.0f : 0.0f);
}

FWetClothingTransparencyLayerData* SWetClothingTransparencyPreviewViewport::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer)
        {
            return Layer.LayerGuid == SelectedLayerGuid;
        }) : nullptr;
}

bool SWetClothingTransparencyPreviewViewport::CanPaint() const
{
    return PaintSettings.bEnabled && PreviewMode == EWetClothingTransparencyPreviewMode::TargetMeshOnly &&
        AutoBakePreviewResult.IsValid() && TransparencyPreviewTexture != nullptr &&
        SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelIndex == 0 &&
        (VisualizationMode == EDWCTransparencyVisualizationMode::Final ||
         VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha);
}

void SWetClothingTransparencyPreviewViewport::RebuildHitTriangles()
{
    CachedHitTriangles.Reset();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->TargetMesh == nullptr || TargetMeshPreviewComponent == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex < 0)
    {
        return;
    }

    TArray<FWetClothingAssetUVIsland> Islands;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            Asset->TargetMesh,
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
    for (const FDWCTransparencyCachedHitTriangle& Triangle : CachedHitTriangles)
    {
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
            continue;
        }
        const double DistanceSq = FVector::DistSquared(RayOrigin, Intersection);
        if (DistanceSq >= OutHit.DistanceSq)
        {
            continue;
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
            ? FMath::Clamp(AutoBakePreviewResult->AutoAlphaBuffer[Index], 0.0f, 1.0f) : 0.0f;
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

    WrinkleSuppressionBuffer.Init(0, PixelCount);
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetWrinkleBakedMapSet* WrinkleMap = Asset != nullptr
        ? Asset->WrinkleData.FindBakedWrinkleMap(
            SelectedMaterialSlotIndex,
            SelectedUVChannelIndex,
            Result.LODIndex)
        : nullptr;
    if (WrinkleMap == nullptr ||
        WrinkleMap->BakedWrinkleNormalMap == nullptr ||
        WrinkleMap->AlphaSemantic != EDWCWrinkleAlphaSemantic::ConvexSeparation)
    {
        return true;
    }

    FWetClothingTextureReadback WrinkleReadback;
    FString ReadError;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            WrinkleMap->BakedWrinkleNormalMap,
            WrinkleReadback,
            ReadError))
    {
        return true;
    }

    for (int32 Y = 0; Y < Height; ++Y)
    {
        const int32 SourceY = FMath::Clamp(
            FMath::FloorToInt((static_cast<float>(Y) + 0.5f) * WrinkleReadback.Height / Height),
            0,
            WrinkleReadback.Height - 1);
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 SourceX = FMath::Clamp(
                FMath::FloorToInt((static_cast<float>(X) + 0.5f) * WrinkleReadback.Width / Width),
                0,
                WrinkleReadback.Width - 1);
            const float Separation = FMath::Clamp(
                WrinkleReadback.GetLinearColor(SourceX, SourceY).A,
                0.0f,
                1.0f);
            WrinkleSuppressionBuffer[Y * Width + X] =
                static_cast<uint8>(FMath::RoundToInt(Separation * 255.0f));
        }
    }
    return true;
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
        SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVChannelIndex != 0 ||
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
        const float AutoAlpha = FMath::Clamp(AutoBakePreviewResult->AutoAlphaBuffer[PixelIndex], 0.0f, 1.0f);
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
        const float WrinkleSuppression = WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
            ? FMath::Clamp(WrinkleSuppressionBuffer[PixelIndex] / 255.0f * WrinkleSuppressionStrength, 0.0f, 1.0f)
            : 0.0f;
        const float EditedAlpha = ApplyHoverToEditedAlpha(PixelIndex, GetStoredEditedAlpha(PixelIndex));
        const uint8 Alpha = static_cast<uint8>(FMath::RoundToInt(
            FMath::Clamp(EditedAlpha, 0.0f, 1.0f) * (1.0f - WrinkleSuppression) * 255.0f));
        FColor Pixel = Result.InnerColorBuffer[PixelIndex];
        Pixel.A = Alpha;

        switch (VisualizationMode)
        {
        case EDWCTransparencyVisualizationMode::InnerColor:
            Pixel.A = 255;
            break;
        case EDWCTransparencyVisualizationMode::AutoAlpha:
            Pixel = FColor(Alpha, Alpha, Alpha, 255);
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
            const float Confidence = Result.RayConfidenceBuffer.IsValidIndex(PixelIndex) ? Result.RayConfidenceBuffer[PixelIndex] : 0.0f;
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
    const float Suppression = WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
        ? FMath::Clamp(WrinkleSuppressionBuffer[PixelIndex] / 255.0f * WrinkleSuppressionStrength, 0.0f, 1.0f)
        : 0.0f;
    const float EditedAlpha = ApplyHoverToEditedAlpha(PixelIndex, GetStoredEditedAlpha(PixelIndex));
    const uint8 Alpha = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(EditedAlpha, 0.0f, 1.0f) * (1.0f - Suppression) * 255.0f));
    if (VisualizationMode == EDWCTransparencyVisualizationMode::AutoAlpha)
    {
        return FColor(Alpha, Alpha, Alpha, 255);
    }
    if (VisualizationMode == EDWCTransparencyVisualizationMode::WrinkleSeparation)
    {
        const uint8 Separation = WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
            ? WrinkleSuppressionBuffer[PixelIndex]
            : 0;
        return FColor(Separation, Separation, Separation, 255);
    }
    FColor Pixel = AutoBakePreviewResult->InnerColorBuffer[PixelIndex];
    Pixel.A = Alpha;
    return Pixel;
}

void SWetClothingTransparencyPreviewViewport::UpdatePreviewTextureRegion(const FIntRect& DirtyRect)
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
        if (MeshComponent != nullptr && (Asset == nullptr || MeshComponent->GetSkeletalMeshAsset() == Asset->TargetMesh))
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
