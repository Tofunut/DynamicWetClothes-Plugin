#include "SWetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Utility/DWCLog.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfilePreviewMaterial.h"
#include "WetnessProfileViewportClient.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileViewport"

namespace
{
    constexpr float PreviewSceneLift = 82.0f;
    constexpr float PreviewSphereScale = 1.35f;
    const FName PreviewSurfaceWaterOverrideParameter(TEXT("DWC_PreviewSurfaceWaterOverride"));
    const FName PreviewSurfaceWaterAmountParameter(TEXT("DWC_PreviewSurfaceWaterAmount"));

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

    void SetPreviewMaterialOnMesh(UMeshComponent* MeshComponent, UMaterialInterface* Material)
    {
        if (MeshComponent == nullptr || Material == nullptr)
        {
            return;
        }

        const int32 MaterialCount = FMath::Max(MeshComponent->GetNumMaterials(), 1);
        for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
        {
            MeshComponent->SetMaterial(MaterialIndex, Material);
        }
    }

    void WriteSinglePixelTexture(UTexture2D* Texture, const FColor& Color)
    {
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.IsEmpty())
        {
            return;
        }

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        FColor* Data = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
        if (Data != nullptr)
        {
            *Data = Color;
        }
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
    }

    UTexture2D* CreateSinglePixelPreviewTexture(UObject* Outer, const TCHAR* DebugName, const FColor& Color)
    {
        UTexture2D* Texture = UTexture2D::CreateTransient(1, 1, PF_B8G8R8A8, FName(DebugName));
        if (Texture == nullptr)
        {
            return nullptr;
        }

        Texture->Rename(nullptr, Outer != nullptr ? Outer : GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        Texture->SRGB = false;
        Texture->CompressionSettings = TC_VectorDisplacementmap;
        Texture->Filter = TF_Nearest;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->NeverStream = true;
        WriteSinglePixelTexture(Texture, Color);
        return Texture;
    }

    FColor MakeScalarPreviewColor(const float Value)
    {
        const uint8 Encoded = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
        return FColor(Encoded, 0u, 0u, 255u);
    }

    uint8 EncodePreviewDetailSize(const float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value / 4.0f, 0.0f, 1.0f) * 255.0f));
    }
}

void SWetnessProfileViewport::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    PreviewScene->SetFloorVisibility(true, true);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    InitializePreviewComponents();
    UpdateRealtimeState();
    RefreshFromProfile();
}

SWetnessProfileViewport::~SWetnessProfileViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }
    if (PreviewScene.IsValid() && PreviewSkeletalMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewSkeletalMeshComponent);
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetnessProfileViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(PreviewSkeletalMeshComponent);
    Collector.AddReferencedObject(PreviewSphereMesh);
    Collector.AddReferencedObject(PreviewMeshOverride);
    Collector.AddReferencedObject(PreviewBaseMaterial);
    Collector.AddReferencedObject(PreviewMaterialInstance);
    Collector.AddReferencedObject(GeneratedPreviewMesh);
    for (TObjectPtr<UMaterial>& GeneratedMaterial : GeneratedPreviewMaterials)
    {
        Collector.AddReferencedObject(GeneratedMaterial);
    }
    for (TObjectPtr<UMaterialInstanceConstant>& GeneratedInstance : GeneratedPreviewMaterialInstances)
    {
        Collector.AddReferencedObject(GeneratedInstance);
    }
    for (TObjectPtr<UMaterialInstanceDynamic>& GeneratedMID : GeneratedPreviewDynamicMaterials)
    {
        Collector.AddReferencedObject(GeneratedMID);
    }
    Collector.AddReferencedObject(PreviewWetnessMapTexture);
    Collector.AddReferencedObject(PreviewWetPartDataTexture);
    Collector.AddReferencedObject(PreviewSurfaceDropletTexture);
    Collector.AddReferencedObject(PreviewDefaultNormalTexture);
    Collector.AddReferencedObject(PreviewDefaultMaskTexture);
}

void SWetnessProfileViewport::RefreshFromProfile()
{
    if (!bHasPreviewMeshOverride)
    {
        ApplyResolvedPreviewMesh(false);
    }
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::Tick(
    const FGeometry& AllottedGeometry,
    const double InCurrentTime,
    const float InDeltaTime)
{
    SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (!bPreviewAnimationEnabled || PreviewAnimationSpeed <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    PreviewAnimationTime += FMath::Max(InDeltaTime, 0.0f) * PreviewAnimationSpeed;
    RefreshGeneratedPreviewAnimationTime();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
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

void SWetnessProfileViewport::SetPreviewDropletDetailSize(const float InDropletDetailSize)
{
    const float NewDropletDetailSize = FMath::Clamp(InDropletDetailSize, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(PreviewDropletDetailSize, NewDropletDetailSize))
    {
        return;
    }

    PreviewDropletDetailSize = NewDropletDetailSize;
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewMode(const EPreviewMode InPreviewMode)
{
    if (PreviewMode == InPreviewMode)
    {
        return;
    }

    PreviewMode = InPreviewMode;
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewAnimationEnabled(const bool bInEnabled)
{
    if (bPreviewAnimationEnabled == bInEnabled)
    {
        return;
    }

    bPreviewAnimationEnabled = bInEnabled;
    if (!bPreviewAnimationEnabled)
    {
        PreviewAnimationTime = 0.0f;
    }
    UpdateRealtimeState();
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewAnimationSpeed(const float InSpeed)
{
    const float NewSpeed = FMath::Clamp(InSpeed, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(PreviewAnimationSpeed, NewSpeed))
    {
        return;
    }

    PreviewAnimationSpeed = NewSpeed;
    UpdateRealtimeState();
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(GetActivePreviewComponent(), bInstant);
    }
}

void SWetnessProfileViewport::SetPreviewSkeletalMeshOverride(USkeletalMesh* InPreviewMesh)
{
    bHasPreviewMeshOverride = InPreviewMesh != nullptr;
    PreviewMeshOverride = InPreviewMesh;
    ApplyResolvedPreviewMesh(true);
}

void SWetnessProfileViewport::ClearPreviewSkeletalMeshOverride()
{
    bHasPreviewMeshOverride = false;
    PreviewMeshOverride = nullptr;
    ApplyResolvedPreviewMesh(true);
}

void SWetnessProfileViewport::UseSpherePreview()
{
    bHasPreviewMeshOverride = true;
    PreviewMeshOverride = nullptr;
    ApplyResolvedPreviewMesh(true);
}

USkeletalMesh* SWetnessProfileViewport::GetDisplayedPreviewSkeletalMesh() const
{
    return PreviewSkeletalMeshComponent != nullptr && PreviewSkeletalMeshComponent->IsVisible()
               ? PreviewSkeletalMeshComponent->GetSkeletalMeshAsset()
               : nullptr;
}

TSharedRef<FEditorViewportClient> SWetnessProfileViewport::MakeEditorViewportClient()
{
    ViewportClient = MakeShared<FWetnessProfileViewportClient>(PreviewScene.Get(), SharedThis(this));
    ViewportClient->SetPreviewMeshComponent(GetActivePreviewComponent());
    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetnessProfileViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetnessProfileEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu =
            UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
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

void SWetnessProfileViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(10.0f)
            [SAssignNew(OverlayText, STextBlock)
                 .Text(this, &SWetnessProfileViewport::GetOverlayText)
                 .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.92f, 0.30f, 1.0f)))
                 .ShadowColorAndOpacity(FLinearColor::Black)
                 .ShadowOffset(FVector2D(1.0f, 1.0f))];
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

    PreviewSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewSkeletalMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewSkeletalMeshComponent->SetCastShadow(true);
    PreviewSkeletalMeshComponent->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;

    PreviewSphereMesh = LoadObject<UStaticMesh>(
        nullptr,
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (PreviewSphereMesh != nullptr)
    {
        PreviewMeshComponent->SetStaticMesh(PreviewSphereMesh);
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
        SetPreviewMaterialOnMesh(PreviewMeshComponent, PreviewMaterialInstance.Get());
        SetPreviewMaterialOnMesh(PreviewSkeletalMeshComponent, PreviewMaterialInstance.Get());
    }

    PreviewDefaultNormalTexture = LoadDefaultNormalTexture();
    PreviewDefaultMaskTexture = LoadDefaultMaskTexture();
    PreviewWetnessMapTexture = CreateSinglePixelPreviewTexture(
        GetTransientPackage(),
        TEXT("DWC_WetnessProfilePreviewWetnessMap"),
        MakeScalarPreviewColor(PreviewAbsorbedWater));
    PreviewWetPartDataTexture = CreateSinglePixelPreviewTexture(
        GetTransientPackage(),
        TEXT("DWC_WetnessProfilePreviewWetPartData"),
        FColor(
            0u,
            EncodePreviewDetailSize(PreviewDropletDetailSize),
            0u,
            255u));
    PreviewSurfaceDropletTexture = CreateSinglePixelPreviewTexture(
        GetTransientPackage(),
        TEXT("DWC_WetnessProfilePreviewDropletRT"),
        MakeScalarPreviewColor(PreviewSurfaceWater));
    PreviewScene->AddComponent(PreviewMeshComponent, PreviewLiftTransform());
    PreviewScene->AddComponent(PreviewSkeletalMeshComponent, PreviewLiftTransform());
    ApplyResolvedPreviewMesh(false);

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(GetActivePreviewComponent());
        ViewportClient->FocusOnPreviewMesh(GetActivePreviewComponent(), true);
        ViewportClient->Invalidate();
    }
}

void SWetnessProfileViewport::ApplyResolvedPreviewMesh(bool bFocus)
{
    if (PreviewMeshComponent == nullptr || PreviewSkeletalMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* ResolvedSkeletalMesh =
        bHasPreviewMeshOverride ? PreviewMeshOverride.Get() : ResolveProfilePreviewSkeletalMesh();
    if (ResolvedSkeletalMesh != nullptr)
    {
        PreviewSkeletalMeshComponent->SetSkeletalMesh(ResolvedSkeletalMesh);
        RebuildGeneratedPreviewMaterials(ResolvedSkeletalMesh);
        PreviewSkeletalMeshComponent->SetVisibility(true);
        PreviewMeshComponent->SetVisibility(false);
    }
    else
    {
        PreviewSkeletalMeshComponent->SetSkeletalMesh(nullptr);
        PreviewSkeletalMeshComponent->SetVisibility(false);
        PreviewMeshComponent->SetVisibility(true);
        RebuildGeneratedSpherePreviewMaterial();
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(GetActivePreviewComponent());
        if (bFocus)
        {
            ViewportClient->FocusOnPreviewMesh(GetActivePreviewComponent(), false);
        }
        ViewportClient->Invalidate();
    }
}

USkeletalMesh* SWetnessProfileViewport::ResolveProfilePreviewSkeletalMesh() const
{
#if WITH_EDITORONLY_DATA
    return WetnessProfile.IsValid() ? WetnessProfile->PreviewSkeletalMesh.Get() : nullptr;
#else
    return nullptr;
#endif
}

UPrimitiveComponent* SWetnessProfileViewport::GetActivePreviewComponent() const
{
    return PreviewSkeletalMeshComponent != nullptr && PreviewSkeletalMeshComponent->IsVisible()
               ? Cast<UPrimitiveComponent>(PreviewSkeletalMeshComponent)
               : Cast<UPrimitiveComponent>(PreviewMeshComponent);
}

void SWetnessProfileViewport::RebuildGeneratedSpherePreviewMaterial()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    if (bGeneratedSpherePreviewMaterialValid &&
        GeneratedPreviewMesh == nullptr &&
        GeneratedPreviewDynamicMaterials.Num() == 1 &&
        GeneratedPreviewDynamicMaterials[0] != nullptr)
    {
        PreviewMeshComponent->SetMaterial(0, GeneratedPreviewDynamicMaterials[0]);
        RefreshGeneratedPreviewMaterialParameters();
        return;
    }

    GeneratedPreviewMesh = nullptr;
    GeneratedPreviewMaterials.Reset();
    GeneratedPreviewMaterialInstances.Reset();
    GeneratedPreviewDynamicMaterials.Reset();
    GeneratedPreviewMaterialSlotCount = 1;
    bGeneratedSpherePreviewMaterialValid = false;

    UMaterialInterface* SourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    FWCAMaterialGenerator::FOptions Options;
    Options.SimulationMode = EDWCSimulationMode::WetnessMapGPU;
    Options.DWCDataUVChannelIndex = 0;
    Options.OriginalUVChannelIndex = 0;
    Options.MaterialSlotIndex = 0;
    Options.SurfaceWaterNormalUVChannelIndex = 0;
    Options.bUseSurfaceWater = true;
    Options.bEnableDWCDataUVSampling = true;
    Options.bConnectWetnessMapPath = true;

    const FWetClothingUnifiedMaterialSetupResult PreviewMaterialSet =
        FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(SourceMaterial, Options);
    if (!PreviewMaterialSet.bSucceeded ||
        PreviewMaterialSet.GeneratedMaterial == nullptr ||
        PreviewMaterialSet.GPUMaterialInstance == nullptr)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC: Wetness Profile sphere preview material generation failed. %s"),
            *PreviewMaterialSet.Message);
        SetPreviewMaterialOnMesh(
            PreviewMeshComponent,
            PreviewMaterialInstance != nullptr
                ? static_cast<UMaterialInterface*>(PreviewMaterialInstance.Get())
                : SourceMaterial);
        GeneratedPreviewMaterials.Add(nullptr);
        GeneratedPreviewMaterialInstances.Add(nullptr);
        GeneratedPreviewDynamicMaterials.Add(nullptr);
        return;
    }

    UMaterialInstanceDynamic* PreviewMID = UMaterialInstanceDynamic::Create(
        PreviewMaterialSet.GPUMaterialInstance,
        GetTransientPackage());
    if (PreviewMID == nullptr)
    {
        SetPreviewMaterialOnMesh(PreviewMeshComponent, SourceMaterial);
        GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
        GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GPUMaterialInstance);
        GeneratedPreviewDynamicMaterials.Add(nullptr);
        return;
    }

    GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
    GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GPUMaterialInstance);
    GeneratedPreviewDynamicMaterials.Add(PreviewMID);
    PreviewMeshComponent->SetMaterial(0, PreviewMID);
    bGeneratedSpherePreviewMaterialValid = true;
    RefreshGeneratedPreviewMaterialParameters();
}

void SWetnessProfileViewport::RebuildGeneratedPreviewMaterials(USkeletalMesh* SkeletalMesh)
{
    if (PreviewSkeletalMeshComponent == nullptr || SkeletalMesh == nullptr)
    {
        return;
    }

    const int32 MaterialCount = FMath::Max(SkeletalMesh->GetMaterials().Num(), 1);
    if (GeneratedPreviewMesh == SkeletalMesh &&
        GeneratedPreviewMaterialSlotCount == MaterialCount)
    {
        for (int32 MaterialIndex = 0; MaterialIndex < GeneratedPreviewDynamicMaterials.Num(); ++MaterialIndex)
        {
            if (GeneratedPreviewDynamicMaterials[MaterialIndex] != nullptr)
            {
                PreviewSkeletalMeshComponent->SetMaterial(MaterialIndex, GeneratedPreviewDynamicMaterials[MaterialIndex]);
            }
        }
        RefreshGeneratedPreviewMaterialParameters();
        return;
    }

    GeneratedPreviewMesh = SkeletalMesh;
    GeneratedPreviewMaterials.Reset();
    GeneratedPreviewMaterialInstances.Reset();
    GeneratedPreviewDynamicMaterials.Reset();
    GeneratedPreviewMaterialSlotCount = MaterialCount;
    bGeneratedSpherePreviewMaterialValid = false;

    const TArray<FSkeletalMaterial>& SourceMaterials = SkeletalMesh->GetMaterials();
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        UMaterialInterface* SourceMaterial = SourceMaterials.IsValidIndex(MaterialIndex)
                                                 ? SourceMaterials[MaterialIndex].MaterialInterface
                                                 : nullptr;
        if (SourceMaterial == nullptr)
        {
            SourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        }

        FWCAMaterialGenerator::FOptions Options;
        Options.SimulationMode = EDWCSimulationMode::WetnessMapGPU;
        Options.DWCDataUVChannelIndex = 0;
        Options.OriginalUVChannelIndex = 0;
        Options.MaterialSlotIndex = MaterialIndex;
        Options.SurfaceWaterNormalUVChannelIndex = 0;
        Options.bUseSurfaceWater = true;
        Options.bEnableDWCDataUVSampling = true;
        Options.bConnectWetnessMapPath = true;

        const FWetClothingUnifiedMaterialSetupResult PreviewMaterialSet =
            FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(SourceMaterial, Options);
        if (!PreviewMaterialSet.bSucceeded ||
            PreviewMaterialSet.GeneratedMaterial == nullptr ||
            PreviewMaterialSet.GPUMaterialInstance == nullptr)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC: Wetness Profile skeletal preview material generation failed for slot %d on '%s'. %s"),
                MaterialIndex,
                *GetNameSafe(SkeletalMesh),
                *PreviewMaterialSet.Message);
            PreviewSkeletalMeshComponent->SetMaterial(MaterialIndex, SourceMaterial);
            GeneratedPreviewMaterials.Add(nullptr);
            GeneratedPreviewMaterialInstances.Add(nullptr);
            GeneratedPreviewDynamicMaterials.Add(nullptr);
            continue;
        }

        UMaterialInstanceDynamic* PreviewMID = UMaterialInstanceDynamic::Create(
            PreviewMaterialSet.GPUMaterialInstance,
            GetTransientPackage());
        if (PreviewMID == nullptr)
        {
            PreviewSkeletalMeshComponent->SetMaterial(MaterialIndex, SourceMaterial);
            GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
            GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GPUMaterialInstance);
            GeneratedPreviewDynamicMaterials.Add(nullptr);
            continue;
        }

        GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
        GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GPUMaterialInstance);
        GeneratedPreviewDynamicMaterials.Add(PreviewMID);
        PreviewSkeletalMeshComponent->SetMaterial(MaterialIndex, PreviewMID);
    }

    RefreshGeneratedPreviewMaterialParameters();
}

void SWetnessProfileViewport::RefreshPreviewMaterialParameters()
{
    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FAbsorbedWetnessProfileParameters& Absorbed = Parameters.AbsorbedWetness;
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    if (PreviewMaterialInstance != nullptr)
    {
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
            SurfaceTargetRoughnessParameter,
            FMath::Clamp(Surface.SurfaceWaterTargetRoughness, 0.0f, 1.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            SurfaceNormalStrengthParameter,
            FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            SurfaceRoughnessBlendParameter,
            FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            SurfaceTotalStrengthParameter,
            FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            SurfaceSpecularParameter,
            FMath::Clamp(Surface.SurfaceWaterSpecular, 0.0f, 1.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            DropletsEnabledParameter,
            Surface.bEnabled ? 1.0f : 0.0f);
        PreviewMaterialInstance->SetScalarParameterValue(
            DropletStampSizeParameter,
            FMath::Clamp(Surface.DropletRadiusPixels, 1.0f, 256.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            DropletDetailSizeParameter,
            FMath::Clamp(PreviewDropletDetailSize, 0.0f, 4.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            DebugModeParameter,
            static_cast<float>(PreviewMode));

        PreviewMaterialInstance->SetTextureParameterValue(
            DropletNormalTextureParameter,
            Surface.DropletNormalTexture != nullptr
                ? Surface.DropletNormalTexture.Get()
                : PreviewDefaultNormalTexture.Get());
        PreviewMaterialInstance->SetTextureParameterValue(
            DropletMaskTextureParameter,
            Surface.DropletMaskTexture != nullptr
                ? Surface.DropletMaskTexture.Get()
                : PreviewDefaultMaskTexture.Get());
    }

    RefreshGeneratedPreviewMaterialParameters();

    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetnessProfileViewport::RefreshGeneratedPreviewMaterialParameters()
{
    if (GeneratedPreviewDynamicMaterials.IsEmpty())
    {
        return;
    }

    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    WriteSinglePixelTexture(PreviewWetnessMapTexture, MakeScalarPreviewColor(PreviewAbsorbedWater));
    WriteSinglePixelTexture(
        PreviewWetPartDataTexture,
        FColor(
            0u,
            EncodePreviewDetailSize(PreviewDropletDetailSize),
            0u,
            255u));
    WriteSinglePixelTexture(PreviewSurfaceDropletTexture, MakeScalarPreviewColor(PreviewSurfaceWater));

    const FLinearColor FallbackProfile0(
        Parameters.GetAbsorbedDarkeningStrength(),
        Parameters.GetAbsorbedGlossinessStrength(),
        0.0f,
        0.0f);
    const FLinearColor FallbackProfile1(
        FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f),
        FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f),
        0.0f,
        FMath::Clamp(Surface.SurfaceWaterSpecular, 0.0f, 1.0f));
    const FLinearColor FallbackProfile2(
        0.0f,
        0.0f,
        FMath::Clamp(Surface.SurfaceWaterTargetRoughness, 0.0f, 1.0f),
        FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f));

    const float SurfacePreviewAmount = Surface.bEnabled ? PreviewSurfaceWater : 0.0f;
    FWetClothingLocalRenderProfile PreviewLocalProfile;
    PreviewLocalProfile.Parameters = Parameters;
    PreviewLocalProfile.StableKey = FString::Printf(
        TEXT("WetnessProfileViewport|%s"),
        *GetPathNameSafe(WetnessProfile.Get()));
    FString PreparedSurfaceTextureError;
    if (!FWetClothingSurfaceTextureNormalizer::PrepareProfileTextures(
            Parameters,
            PreviewLocalProfile,
            PreparedSurfaceTextureError))
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC Wetness Profile preview could not prepare 512 Surface Water textures for '%s': %s"),
            *GetPathNameSafe(WetnessProfile.Get()),
            *PreparedSurfaceTextureError);
    }

    UDWCGPUResourceSubsystem* ResourceSubsystem = nullptr;
    if (PreviewScene.IsValid())
    {
        if (UWorld* PreviewWorld = PreviewScene->GetWorld())
        {
            ResourceSubsystem = PreviewWorld->GetSubsystem<UDWCGPUResourceSubsystem>();
        }
    }

    for (UMaterialInstanceDynamic* PreviewMID : GeneratedPreviewDynamicMaterials)
    {
        if (PreviewMID == nullptr)
        {
            continue;
        }

        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::WetnessMap(), PreviewWetnessMapTexture);
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::WetPartDataTexture(), PreviewWetPartDataTexture);
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDropletRT(), PreviewSurfaceDropletTexture);
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterTime(), 0.0f);
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterTexelSize(), 1.0f);
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::UseRenderProfileLUT(), 0.0f);
        const bool bAppliedProfileTextures =
            ResourceSubsystem != nullptr &&
            ResourceSubsystem->ApplyPreviewRenderProfileFallbackProfile(
                nullptr,
                0,
                PreviewLocalProfile,
                *PreviewMID);
        if (!bAppliedProfileTextures)
        {
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(0), FallbackProfile0);
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(1), FallbackProfile1);
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(2), FallbackProfile2);
        }
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter, 1.0f);
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, SurfacePreviewAmount);
        PreviewMID->SetScalarParameterValue(
            DWCWetnessProfilePreviewMaterial::DebugModeParameter,
            static_cast<float>(PreviewMode));
        const bool bSurfaceDebugMode =
            PreviewMode == EPreviewMode::SurfaceCoverage ||
            PreviewMode == EPreviewMode::FinalDropletCoverage ||
            PreviewMode == EPreviewMode::DropletStampTest;
        PreviewMID->SetScalarParameterValue(
            DWCWetMaterialParameters::SurfaceWaterDebugStrength(),
            bSurfaceDebugMode ? 1.0f : 0.0f);
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::WetPartDebugStrength(), 0.0f);
    }
}

void SWetnessProfileViewport::RefreshGeneratedPreviewAnimationTime()
{
    for (UMaterialInstanceDynamic* PreviewMID : GeneratedPreviewDynamicMaterials)
    {
        if (PreviewMID != nullptr)
        {
            PreviewMID->SetScalarParameterValue(
                DWCWetMaterialParameters::SurfaceWaterTime(),
                PreviewAnimationTime);
        }
    }
}

void SWetnessProfileViewport::UpdateRealtimeState()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->SetRealtime(
            bPreviewAnimationEnabled && PreviewAnimationSpeed > KINDA_SMALL_NUMBER);
    }
}

FText SWetnessProfileViewport::GetOverlayText() const
{
    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    return FText::Format(
        LOCTEXT(
            "PreviewHint",
            "Wetness Profile Preview\nAbsorbed Wetness {0}%  |  Total Strength {1}%\nDarkening {2}%  |  Absorbed Glossiness {3}%\nWater Normal {4}%  |  Roughness Blend {5}%"),
        FText::AsNumber(FMath::RoundToInt(PreviewAbsorbedWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedDarkeningStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedGlossinessStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f) * 100.0f)));
}

#undef LOCTEXT_NAMESPACE
