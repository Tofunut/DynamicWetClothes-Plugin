#include "SWetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfilePreviewMaterial.h"
#include "WetnessProfileViewportClient.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileViewport"

namespace
{
    constexpr float PreviewSceneLift = 82.0f;
    constexpr float PreviewSphereScale = 1.35f;

    FWetnessProfileParameters GetSanitizedProfileParameters(const UWetnessProfile* Profile)
    {
        FWetnessProfileParameters Parameters = Profile != nullptr
                                                   ? Profile->GetParameters()
                                                   : FWetnessProfileParameters();
        FWetnessProfileEditorPolicy::SanitizeParameters(Parameters);
        return Parameters;
    }

    const FTransform PreviewLiftTransform()
    {
        return FTransform(FVector(0.0, 0.0, PreviewSceneLift));
    }

    UTexture* LoadDefaultNormalTexture()
    {
        if (UTexture* DefaultNormal = LoadObject<UTexture>(
                nullptr,
                TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return DefaultNormal;
        }

        return LoadObject<UTexture>(
            nullptr,
            TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

    UTexture* LoadDefaultMaskTexture()
    {
        if (UTexture* BlackTexture = LoadObject<UTexture>(
                nullptr,
                TEXT("/Engine/EngineResources/Black.Black")))
        {
            return BlackTexture;
        }

        return LoadObject<UTexture>(
            nullptr,
            TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    }
}

void SWetnessProfileViewport::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    PreviewScene->SetFloorVisibility(true, true);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    InitializePreviewComponents();
    RefreshFromProfile();
}

SWetnessProfileViewport::~SWetnessProfileViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetnessProfileViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(PreviewBaseMaterial);
    Collector.AddReferencedObject(PreviewMaterialInstance);
    Collector.AddReferencedObject(PreviewDefaultNormalTexture);
    Collector.AddReferencedObject(PreviewDefaultMaskTexture);
}

void SWetnessProfileViewport::RefreshFromProfile()
{
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewAbsorbedWater(float InAmount)
{
    const float NewAmount = FMath::Clamp(InAmount, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(PreviewAbsorbedWater, NewAmount))
    {
        return;
    }

    PreviewAbsorbedWater = NewAmount;
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewSurfaceWater(float InAmount)
{
    const float NewAmount = FMath::Clamp(InAmount, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(PreviewSurfaceWater, NewAmount))
    {
        return;
    }

    PreviewSurfaceWater = NewAmount;
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetnessProfileViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FWetnessProfileViewportClient>(PreviewScene.Get(), SharedThis(this));
    ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
    return ViewportClient.ToSharedRef();
}

void SWetnessProfileViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(10.0f)
            [SAssignNew(OverlayText, SRichTextBlock)
                 .Text(this, &SWetnessProfileViewport::GetOverlayText)
                 .DecoratorStyleSet(&FAppStyle::Get())];
}

void SWetnessProfileViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh();
}

void SWetnessProfileViewport::InitializePreviewComponents()
{
    if (!PreviewScene.IsValid() || PreviewMeshComponent != nullptr)
    {
        return;
    }

    PreviewMeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewMeshComponent->SetCastShadow(true);

    if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(
            nullptr,
            TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        PreviewMeshComponent->SetStaticMesh(SphereMesh);
        PreviewMeshComponent->SetRelativeScale3D(FVector(PreviewSphereScale));
    }

    PreviewBaseMaterial = DWCWetnessProfilePreviewMaterial::LoadOrCreateBaseMaterial();
    if (PreviewBaseMaterial == nullptr)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DWC: M_DWC_WetnessProfilePreview could not be loaded or created. Using the engine default material for this preview."));
        PreviewBaseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    }

    if (PreviewBaseMaterial != nullptr)
    {
        PreviewMaterialInstance = UMaterialInstanceDynamic::Create(
            PreviewBaseMaterial.Get(),
            GetTransientPackage());
        PreviewMeshComponent->SetMaterial(0, PreviewMaterialInstance.Get());
    }

    PreviewDefaultNormalTexture = LoadDefaultNormalTexture();
    PreviewDefaultMaskTexture = LoadDefaultMaskTexture();
    PreviewScene->AddComponent(PreviewMeshComponent, PreviewLiftTransform());

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
        ViewportClient->Invalidate();
    }
}

void SWetnessProfileViewport::RefreshPreviewMaterialParameters()
{
    if (PreviewMaterialInstance == nullptr)
    {
        return;
    }

    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FAbsorbedWetnessProfileParameters& Absorbed = Parameters.AbsorbedWetness;
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    using namespace DWCWetnessProfilePreviewMaterial;
    PreviewMaterialInstance->SetScalarParameterValue(AbsorbedWaterParameter, PreviewAbsorbedWater);
    PreviewMaterialInstance->SetScalarParameterValue(SurfaceWaterParameter, PreviewSurfaceWater);
    PreviewMaterialInstance->SetScalarParameterValue(AbsorbedEnabledParameter, Absorbed.bEnabled ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(SurfaceEnabledParameter, Surface.bEnabled ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        AbsorbedDarkeningStrengthParameter,
        Parameters.GetAbsorbedDarkeningStrength());
    PreviewMaterialInstance->SetScalarParameterValue(
        AbsorbedGlossinessStrengthParameter,
        Parameters.GetAbsorbedGlossinessStrength());
    PreviewMaterialInstance->SetScalarParameterValue(
        SurfaceNormalStrengthParameter,
        FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 8.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        SurfaceRoughnessStrengthParameter,
        FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        SurfaceVisibilityThresholdParameter,
        FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        DropletsEnabledParameter,
        Surface.bEnableDroplets ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        RivuletsEnabledParameter,
        Surface.bEnableRivulets ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        RivuletScrollSpeedParameter,
        FMath::Clamp(Surface.RivuletUVScrollSpeed, 0.0f, 10.0f));

    PreviewMaterialInstance->SetTextureParameterValue(
        DropletNormalTextureParameter,
        Surface.DropletNormalTexture != nullptr
            ? Surface.DropletNormalTexture.Get()
            : PreviewDefaultNormalTexture.Get());
    PreviewMaterialInstance->SetTextureParameterValue(
        RivuletNormalTextureParameter,
        Surface.RivuletNormalTexture != nullptr
            ? Surface.RivuletNormalTexture.Get()
            : PreviewDefaultNormalTexture.Get());
    PreviewMaterialInstance->SetTextureParameterValue(
        DropletMaskTextureParameter,
        Surface.DropletMaskTexture != nullptr
            ? Surface.DropletMaskTexture.Get()
            : PreviewDefaultMaskTexture.Get());
    PreviewMaterialInstance->SetTextureParameterValue(
        RivuletMaskTextureParameter,
        Surface.RivuletMaskTexture != nullptr
            ? Surface.RivuletMaskTexture.Get()
            : PreviewDefaultMaskTexture.Get());

    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

FText SWetnessProfileViewport::GetOverlayText() const
{
    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    return FText::Format(
        LOCTEXT(
            "PreviewHint",
            "Wetness Profile Preview\nAbsorbed Wetness {0}%  |  Surface Water {1}%\nDarkening {2}%  |  Absorbed Glossiness {3}%\nBumpiness {4}%  |  Surface Glossiness {5}%"),
        FText::AsNumber(FMath::RoundToInt(PreviewAbsorbedWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(PreviewSurfaceWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedDarkeningStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedGlossinessStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterNormalStrength / 2.0f, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f) * 100.0f)));
}

#undef LOCTEXT_NAMESPACE
