#include "WetClothing/TransparencyMode/Viewport/SWetClothingTransparencyPreviewViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Widgets/SNullWidget.h"

namespace
{
    constexpr const TCHAR* UseRevealPreviewParameterName = TEXT("DWC_UseRevealPreview");
    constexpr const TCHAR* RevealPreviewBlendParameterName = TEXT("DWC_RevealPreviewBlend");
    constexpr const TCHAR* RevealMaskMultiplierParameterName = TEXT("DWC_RevealMaskMultiplier");
    constexpr const TCHAR* RevealConfidenceMultiplierParameterName = TEXT("DWC_RevealConfidenceMultiplier");

    class FDWCTransparencyPreviewViewportClient : public FEditorViewportClient
    {
      public:
        FDWCTransparencyPreviewViewportClient(
            FAdvancedPreviewScene* InPreviewScene,
            const TSharedRef<SEditorViewport>& InViewport)
            : FEditorViewportClient(nullptr, InPreviewScene, InViewport)
            , PreviewScene(InPreviewScene)
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
}

void SWetClothingTransparencyPreviewViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    SEditorViewport::Construct(SEditorViewport::FArguments());
    RefreshPreview();
}

SWetClothingTransparencyPreviewViewport::~SWetClothingTransparencyPreviewViewport()
{
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

TSharedRef<FEditorViewportClient> SWetClothingTransparencyPreviewViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FDWCTransparencyPreviewViewportClient>(PreviewScene.Get(), SharedThis(this));
    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetClothingTransparencyPreviewViewport::BuildViewportToolbar()
{
    return SNullWidget::NullWidget;
}

void SWetClothingTransparencyPreviewViewport::ClearPreview()
{
    if (PreviewScene.IsValid())
    {
        if (TargetMeshPreviewComponent != nullptr)
        {
            PreviewScene->RemoveComponent(TargetMeshPreviewComponent);
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

    for (const FWetClothingBakedTransparencyRevealLayer& BakedLayer : Asset->TransparencyData.BakedRevealLayers)
    {
        if (BakedLayer.MaterialSlotIndex == INDEX_NONE || BakedLayer.RevealMaterial == nullptr)
        {
            continue;
        }

        if (BakedLayer.MaterialSlotIndex < MeshComponent->GetNumMaterials())
        {
            MeshComponent->SetMaterial(BakedLayer.MaterialSlotIndex, BakedLayer.RevealMaterial);
            if (UMaterialInstanceDynamic* MID = MeshComponent->CreateAndSetMaterialInstanceDynamic(BakedLayer.MaterialSlotIndex))
            {
                PreviewMIDs.Add(MID);
                MID->SetScalarParameterValue(UseRevealPreviewParameterName, 1.0f);
                MID->SetScalarParameterValue(RevealPreviewBlendParameterName, 1.0f);
                MID->SetScalarParameterValue(RevealMaskMultiplierParameterName, 1.0f);
                MID->SetScalarParameterValue(RevealConfidenceMultiplierParameterName, 1.0f);
            }
        }
    }
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
