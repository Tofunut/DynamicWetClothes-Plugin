#include "SWetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/WetClothingSettings.h"
#include "GPU/DWCGPUBackend.h"
#include "DataAssets/WetClothingPartData.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/UObjectGlobals.h"
#include "Utility/DWCLog.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVPreviewTriangleReader.h"
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
    constexpr float PreviewFixedStep = 0.1f;
    constexpr float PreviewRestartDebounce = 0.12f;
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

    uint32 BuildSimulationParameterHash(const FWetnessProfileParameters& Parameters)
    {
        const FResolvedAbsorbedWaterSimulationParameters Resolved =
            Parameters.ResolveAbsorbedWaterSimulation();
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        uint32                                Hash = 0u;
        const auto                            AddValue = [&Hash](const auto& Value)
        {
            Hash = HashCombine(Hash, GetTypeHash(Value));
};

        AddValue(Parameters.AbsorbedWetness.bEnabled);
        AddValue(Resolved.AbsorptionMultiplier);
        AddValue(Parameters.GetMaxPendingWaterPerPixel());
        AddValue(Resolved.SpreadRatePerSecond);
        AddValue(Resolved.DryRatePerSecond);
        AddValue(Resolved.GravityFlowStrength);
        AddValue(Surface.bEnabled);
        AddValue(Parameters.GetDropletDryRatePerSecond());
        AddValue(Surface.DropletSpawnProbability);
        AddValue(Surface.DropletRadiusPixels);
        AddValue(Surface.DropletHeightPixels);
        AddValue(Surface.DropletFlowSpawnProbability);
        AddValue(Surface.DropletFlowRadiusPixels);
        AddValue(Surface.DropletFlowHeightPixels);
        AddValue(Surface.DropletFlowSpawnPositionSpread);
        return Hash;
    }
} // namespace

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
    ShutdownGPUPreviewSimulator();
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
    Collector.AddReferencedObject(PreviewSurfaceFlowDropletTexture);
    Collector.AddReferencedObject(PreviewDefaultNormalTexture);
    Collector.AddReferencedObject(PreviewDefaultMaskTexture);
}

void SWetnessProfileViewport::RefreshFromProfile()
{
    if (PreviewBehavior == EPreviewBehavior::Simulation)
    {
        if (EnsureGPUPreviewSimulator() && WetnessProfile.IsValid())
        {
            const FWetnessProfileParameters Parameters =
                GetSanitizedProfileParameters(WetnessProfile.Get());
            const uint32 SimulationParameterHash = BuildSimulationParameterHash(Parameters);
            GPUPreviewSimulator->SetProfileParameters(Parameters);
            if (!bHasSimulationParameterHash ||
                SimulationParameterHash != LastSimulationParameterHash)
            {
                LastSimulationParameterHash = SimulationParameterHash;
                bHasSimulationParameterHash = true;
                ScheduleSimulationRestart();
            }
        }
    }
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

    if (PreviewBehavior== EPreviewBehavior::Simulation)
    {
        TickGPUPreviewSimulation(InDeltaTime);
    }
}

FReply SWetnessProfileViewport::OnMouseButtonDown(
    const FGeometry& MyGeometry,
    const FPointerEvent& MouseEvent)
{
    RefreshScenarioSplashUVFromCamera();
    return SEditorViewport::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SWetnessProfileViewport::OnKeyDown(
    const FGeometry& MyGeometry,
    const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::SpaceBar &&
        PreviewBehavior == EPreviewBehavior::Simulation)
    {
        SetPreviewAnimationEnabled(!bPreviewAnimationEnabled);
        return FReply::Handled();
    }

    return SEditorViewport::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SWetnessProfileViewport::OnKeyUp(
    const FGeometry& MyGeometry,
    const FKeyEvent& InKeyEvent)
{
    return SEditorViewport::OnKeyUp(MyGeometry, InKeyEvent);
}

void SWetnessProfileViewport::OnFocusLost(const FFocusEvent& InFocusEvent)
{
    SEditorViewport::OnFocusLost(InFocusEvent);
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

void SWetnessProfileViewport::SetPreviewDropletDetailSizes(const float InDroplet1DetailSize,
    const float InDroplet2DetailSize)
{
    const float NewDroplet1DetailSize = FMath::Clamp(InDroplet1DetailSize, 0.0f, 4.0f);
    const float NewDroplet2DetailSize = FMath::Clamp(InDroplet2DetailSize, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(PreviewDroplet1DetailSize, NewDroplet1DetailSize) &&
        FMath::IsNearlyEqual(PreviewDroplet2DetailSize, NewDroplet2DetailSize))
    {
        return;
    }

    PreviewDroplet1DetailSize = NewDroplet1DetailSize;
    PreviewDroplet2DetailSize = NewDroplet2DetailSize;
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetInteractionCursorScale(const float InScale)
{
    const float NewScale = FMath::Clamp(InScale, 0.05f, 8.0f);
    if (FMath::IsNearlyEqual(InteractionCursorScale, NewScale))
    {
        return;
    }

    InteractionCursorScale = NewScale;
    RefreshScenarioSplashUVFromCamera();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
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

void SWetnessProfileViewport::SetPreviewBehavior(const EPreviewBehavior InBehavior)
{
    if (PreviewBehavior == InBehavior)
    {
        return;
    }
    PreviewBehavior = InBehavior;
    if (PreviewBehavior == EPreviewBehavior::Simulation)
    {
        EnsureGPUPreviewSimulator();
        if (WetnessProfile.IsValid())
        {
            const FWetnessProfileParameters Parameters =
                GetSanitizedProfileParameters(WetnessProfile.Get());
            LastSimulationParameterHash = BuildSimulationParameterHash(Parameters);
            bHasSimulationParameterHash = true;
        }
        RestartPreviewSimulation();
    }
    ApplyResolvedPreviewMesh(true);
    RefreshPreviewMaterialParameters();
    UpdateRealtimeState();
}

void SWetnessProfileViewport::SetPreviewAnimationEnabled(const bool bInEnabled)
{
    if (bPreviewAnimationEnabled == bInEnabled)
    {
        return;
    }

    bPreviewAnimationEnabled = bInEnabled;
    UpdateRealtimeState();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
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
}

void SWetnessProfileViewport::SetPreviewLoopEnabled(const bool bInEnabled)
{
    bPreviewLoopEnabled = bInEnabled;
}

void SWetnessProfileViewport::SetPreviewSimulationTarget(
    const bool bHasSelection,
    const bool bSurfaceSelected,
    const bool bSecondarySelected,
    const bool bSelectedChannelEnabled)
{
    if (bHasPreviewWaterSelection == bHasSelection &&
        bPreviewSurfaceSelection == bSurfaceSelected &&
        bPreviewSecondarySelection == bSecondarySelected &&
        bPreviewSelectedChannelEnabled == bSelectedChannelEnabled)
    {
        return;
    }

    bHasPreviewWaterSelection = bHasSelection;
    bPreviewSurfaceSelection = bSurfaceSelected;
    bPreviewSecondarySelection = bSecondarySelected;
    bPreviewSelectedChannelEnabled = bSelectedChannelEnabled;
    ScheduleSimulationRestart();
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewSimulationLayers(
    const bool bAbsorbedEnabled,
    const bool bSurfaceEnabled)
{
    if (bPreviewAbsorbedLayerEnabled == bAbsorbedEnabled &&
        bPreviewSurfaceLayerEnabled == bSurfaceEnabled)
    {
        return;
    }
    bPreviewAbsorbedLayerEnabled = bAbsorbedEnabled;
    bPreviewSurfaceLayerEnabled = bSurfaceEnabled;
    RestartPreviewSimulation();
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::SetPreviewDropletVisibility(
    const bool bDroplet1Enabled,
    const bool bDroplet2Enabled)
{
    if (bPreviewDroplet1Enabled == bDroplet1Enabled &&
        bPreviewDroplet2Enabled == bDroplet2Enabled)
    {
        return;
    }
    bPreviewDroplet1Enabled = bDroplet1Enabled;
    bPreviewDroplet2Enabled = bDroplet2Enabled;
    RestartPreviewSimulation();
    RefreshPreviewMaterialParameters();
}

void SWetnessProfileViewport::RestartPreviewSimulation()
{
    PreviewAnimationTime = 0.0f;
    PreviewSimulationAccumulator = 0.0f;
    PendingSimulationRestartDelay = -1.0f;
    if (EnsureGPUPreviewSimulator())
    {
        if (WetnessProfile.IsValid())
        {
            GPUPreviewSimulator->SetProfileParameters(GetSanitizedProfileParameters(WetnessProfile.Get()));
        }
        GPUPreviewSimulator->SetScenarioSplashUV(PreviewScenarioSplashUV);
        GPUPreviewSimulator->SetPreviewChannels(
            bPreviewAbsorbedLayerEnabled,
            bPreviewSurfaceLayerEnabled,
            bPreviewDroplet1Enabled,
            bPreviewDroplet2Enabled);
        GPUPreviewSimulator->Restart();
        BindGPUPreviewTextures();
    }
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetnessProfileViewport::ApplyPreviewSplash()
{
    if (PreviewBehavior != EPreviewBehavior::Simulation ||
        !EnsureGPUPreviewSimulator())
    {
        return;
    }

    RefreshScenarioSplashUVFromCamera();
    GPUPreviewSimulator->SetScenarioSplashUV(PreviewScenarioSplashUV);
    GPUPreviewSimulator->SetPreviewChannels(
        bPreviewAbsorbedLayerEnabled,
        bPreviewSurfaceLayerEnabled,
        bPreviewDroplet1Enabled,
        bPreviewDroplet2Enabled);
    GPUPreviewSimulator->RequestSplash();
    GPUPreviewSimulator->Step(0.0f, PreviewAnimationTime);
    BindGPUPreviewTextures();
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
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
            EncodePreviewDetailSize(PreviewDroplet1DetailSize),
            EncodePreviewDetailSize(PreviewDroplet2DetailSize),
            255u));
    PreviewSurfaceDropletTexture = CreateSinglePixelPreviewTexture(
        GetTransientPackage(),
        TEXT("DWC_WetnessProfilePreviewDropletRT"),
        MakeScalarPreviewColor(PreviewSurfaceWater));
    PreviewSurfaceFlowDropletTexture = CreateSinglePixelPreviewTexture(
        GetTransientPackage(),
        TEXT("DWC_WetnessProfilePreviewFlowDropletRT"),
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

    // Simulation uses the same currently selected model/sphere as Manual preview.
    // The GPU solver remains a normalized 2D domain and is sampled through the model UVs.

    PreviewMeshComponent->SetStaticMesh(PreviewSphereMesh);
    PreviewMeshComponent->SetRelativeRotation(FRotator::ZeroRotator);
    PreviewMeshComponent->SetRelativeScale3D(FVector(PreviewSphereScale));

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

    RefreshScenarioSplashUV();

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
        PreviewMaterialSet.GeneratedMaterialInstance == nullptr)
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
        PreviewMaterialSet.GeneratedMaterialInstance,
        GetTransientPackage());
    if (PreviewMID == nullptr)
    {
        SetPreviewMaterialOnMesh(PreviewMeshComponent, SourceMaterial);
        GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
        GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GeneratedMaterialInstance);
        GeneratedPreviewDynamicMaterials.Add(nullptr);
        return;
    }

    GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
    GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GeneratedMaterialInstance);
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
            PreviewMaterialSet.GeneratedMaterialInstance == nullptr)
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
            PreviewMaterialSet.GeneratedMaterialInstance,
            GetTransientPackage());
        if (PreviewMID == nullptr)
        {
            PreviewSkeletalMeshComponent->SetMaterial(MaterialIndex, SourceMaterial);
            GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
            GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GeneratedMaterialInstance);
            GeneratedPreviewDynamicMaterials.Add(nullptr);
            continue;
        }

        GeneratedPreviewMaterials.Add(PreviewMaterialSet.GeneratedMaterial);
        GeneratedPreviewMaterialInstances.Add(PreviewMaterialSet.GeneratedMaterialInstance);
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
        const bool bManualPreview = PreviewBehavior == EPreviewBehavior::Manual;
        PreviewMaterialInstance->SetScalarParameterValue(AbsorbedWaterParameter,
            bManualPreview ? PreviewAbsorbedWater : 0.0f);
        PreviewMaterialInstance->SetScalarParameterValue(SurfaceWaterParameter,
            bManualPreview ? PreviewSurfaceWater : 0.0f);
        const bool bPreviewAbsorbedEnabled = Absorbed.bEnabled &&
                                             (PreviewBehavior == EPreviewBehavior::Manual || bPreviewAbsorbedLayerEnabled);
        const bool bPreviewSurfaceEnabled = Surface.bEnabled &&
                                            (PreviewBehavior == EPreviewBehavior::Manual || bPreviewSurfaceLayerEnabled);
        PreviewMaterialInstance->SetScalarParameterValue(AbsorbedEnabledParameter, bPreviewAbsorbedEnabled ? 1.0f : 0.0f);
        PreviewMaterialInstance->SetScalarParameterValue(SurfaceEnabledParameter, bPreviewSurfaceEnabled ? 1.0f : 0.0f);
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
            bPreviewSurfaceEnabled && bPreviewDroplet1Enabled ? 1.0f : 0.0f);
        PreviewMaterialInstance->SetScalarParameterValue(
            DropletStampSizeParameter,
            FMath::Clamp(Surface.DropletRadiusPixels, 1.0f, 256.0f));
        PreviewMaterialInstance->SetScalarParameterValue(
            DropletDetailSizeParameter,
            FMath::Clamp(PreviewDroplet1DetailSize, 0.0f, 4.0f));
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

    if (PreviewBehavior == EPreviewBehavior::Manual)
    {

    WriteSinglePixelTexture(PreviewWetnessMapTexture, MakeScalarPreviewColor(PreviewAbsorbedWater));
    WriteSinglePixelTexture(PreviewSurfaceDropletTexture, MakeScalarPreviewColor(PreviewSurfaceWater));
    WriteSinglePixelTexture(PreviewSurfaceFlowDropletTexture, MakeScalarPreviewColor(PreviewSurfaceWater));
    }
    WriteSinglePixelTexture(
        PreviewWetPartDataTexture,
        FColor(
            0u,
            EncodePreviewDetailSize(PreviewDroplet1DetailSize),
            EncodePreviewDetailSize(PreviewDroplet2DetailSize),
            255u));

    const FLinearColor FallbackProfile0(
        Parameters.GetAbsorbedDarkeningStrength(),
        Parameters.GetAbsorbedGlossinessStrength(),
        0.0f, 0.0f);
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
    const FLinearColor FallbackProfile3 = FLinearColor::Black;
    const FLinearColor FallbackProfile4(0.0f, 0.0f, 0.0f, 0.0f);
    const FLinearColor FallbackProfile5(
        FMath::Clamp(Surface.DropletFlowTotalStrength, 0.0f, 1.0f),
        FMath::Clamp(Surface.DropletFlowTargetRoughness, 0.0f, 1.0f),
        FMath::Clamp(Surface.DropletFlowRoughnessBlend, 0.0f, 1.0f),
        FMath::Clamp(Surface.DropletFlowSpecular, 0.0f, 1.0f));
    const FLinearColor FallbackProfile6(
        FMath::Clamp(Surface.SurfaceWaterColorBlend, 0.0f, 1.0f),
        FMath::Clamp(Surface.DropletFlowColorBlend, 0.0f, 1.0f),
        FMath::Clamp(Surface.DropletFlowNormalStrength, 0.0f, 3.0f),
        0.0f);

    const bool                     bManualPreview = PreviewBehavior == EPreviewBehavior::Manual;
    const float SurfacePreviewAmount = Surface.bEnabled && bManualPreview ? PreviewSurfaceWater : 0.0f;
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

        // The transient preview material is unified CPU/GPU. Force the GPU branch on
        // the dynamic instance as well, so source MIC overrides or stale cached data
        // can never fall back to vertex-color alpha (commonly 1.0 across the mesh).
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::UseGPUBackend(), 1.0f);
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::WetPartDataTexture(), PreviewWetPartDataTexture);
        if (PreviewBehavior == EPreviewBehavior::Simulation && EnsureGPUPreviewSimulator())
        {
            PreviewMID->SetTextureParameterValue(
                DWCWetMaterialParameters::WetnessMap(),
                GPUPreviewSimulator->GetWetnessMap());
            PreviewMID->SetTextureParameterValue(
                DWCWetMaterialParameters::SurfaceDroplet1RT(),
                GPUPreviewSimulator->GetDroplet1Map());
            PreviewMID->SetTextureParameterValue(
                DWCWetMaterialParameters::SurfaceDroplet2RT(),
                GPUPreviewSimulator->GetDroplet2Map());
            PreviewMID->SetScalarParameterValue(
                DWCWetMaterialParameters::SurfaceWaterTexelSize(),
                1.0f / static_cast<float>(FMath::Max(GPUPreviewSimulator->GetResolution(), 1)));
        }
        else
        {

        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::WetnessMap(), PreviewWetnessMapTexture);
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDroplet1RT(), PreviewSurfaceDropletTexture);
        PreviewMID->SetTextureParameterValue(
            DWCWetMaterialParameters::SurfaceDroplet2RT(),
            PreviewSurfaceFlowDropletTexture);
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterTexelSize(), 1.0f);
        }
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
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(3), FallbackProfile3);
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(4), FallbackProfile4);
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(5), FallbackProfile5);
            PreviewMID->SetVectorParameterValue(DWCWetMaterialParameters::FallbackRenderProfileTexel(6), FallbackProfile6);
        }
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter,
            bManualPreview ? 1.0f : 0.0f);
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, SurfacePreviewAmount);
        const bool bSurfaceLayerVisible = bManualPreview || bPreviewSurfaceLayerEnabled;
        PreviewMID->SetScalarParameterValue(
            DWCWetMaterialParameters::Droplet1RenderingEnabled(),
            bSurfaceLayerVisible && bPreviewDroplet1Enabled ? 1.0f : 0.0f);
        PreviewMID->SetScalarParameterValue(
            DWCWetMaterialParameters::Droplet2RenderingEnabled(),
            bSurfaceLayerVisible && bPreviewDroplet2Enabled ? 1.0f : 0.0f);
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
    BindGPUPreviewTextures();
}

FVector2f SWetnessProfileViewport::ResolveScenarioSplashUV() const
{
    const USkeletalMesh* SkeletalMesh = GetDisplayedPreviewSkeletalMesh();
    if (SkeletalMesh == nullptr)
    {
        // The engine sphere has valid coverage around the center of UV0.
        return FVector2f(0.5f, 0.5f);
    }

    TArray<int32> MaterialSlots;
    const int32   MaterialCount = SkeletalMesh->GetMaterials().Num();
    MaterialSlots.Reserve(MaterialCount);
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        MaterialSlots.Add(MaterialIndex);
    }

    TArray<FWCAUVPreviewSourceTriangle> Triangles;
    FString                             ReadError;
    if (!FWCAUVPreviewTriangleReader::ReadFromSkeletalMesh(
            SkeletalMesh,
            0,
            0,
            MakeArrayView(MaterialSlots),
            Triangles,
            &ReadError) ||
        Triangles.IsEmpty())
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC Wetness Profile preview could not resolve a visible UV0 splash point for '%s': %s"),
            *GetPathNameSafe(SkeletalMesh),
            *ReadError);
        return FVector2f(0.5f, 0.5f);
    }

    const FBoxSphereBounds ImportedBounds = SkeletalMesh->GetImportedBounds();
    const FVector3f        TargetPoint(
        static_cast<float>(ImportedBounds.Origin.X + ImportedBounds.BoxExtent.X * 0.82),
        static_cast<float>(ImportedBounds.Origin.Y),
        static_cast<float>(ImportedBounds.Origin.Z + ImportedBounds.BoxExtent.Z * 0.15));

    const FWCAUVPreviewSourceTriangle* BestTriangle = nullptr;
    double                             BestScore = TNumericLimits<double>::Max();
    for (const FWCAUVPreviewSourceTriangle& Triangle : Triangles)
    {
        const FVector3f Center =
            (Triangle.LocalPositions[0] + Triangle.LocalPositions[1] + Triangle.LocalPositions[2]) / 3.0f;
        const FVector3f Edge0 = Triangle.LocalPositions[1] - Triangle.LocalPositions[0];
        const FVector3f Edge1 = Triangle.LocalPositions[2] - Triangle.LocalPositions[0];
        const double    AreaWeight = FMath::Max(
            static_cast<double>(FVector3f::CrossProduct(Edge0, Edge1).Size()),
            1.0e-4);
        const double DistanceSquared = static_cast<double>((Center - TargetPoint).SizeSquared());
        const double Score = DistanceSquared / FMath::Sqrt(AreaWeight);
        if (Score < BestScore)
        {
            BestScore = Score;
            BestTriangle = &Triangle;
        }
    }

    if (BestTriangle == nullptr)
    {
        return FVector2f(0.5f, 0.5f);
    }

    const FVector2f UV0 = BestTriangle->UVs[0];
    const auto      UnwrapNear = [](const FVector2f UV, const FVector2f Reference)
    {
        return FVector2f(
            Reference.X + (UV.X - Reference.X) - FMath::RoundToFloat(UV.X - Reference.X),
            Reference.Y + (UV.Y - Reference.Y) - FMath::RoundToFloat(UV.Y - Reference.Y));
    };
    const FVector2f UV1 = UnwrapNear(BestTriangle->UVs[1], UV0);
    const FVector2f UV2 = UnwrapNear(BestTriangle->UVs[2], UV0);
    const FVector2f UV = (UV0 + UV1 + UV2) / 3.0f;
    if (!FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y))
    {
        return FVector2f(0.5f, 0.5f);
    }

    return FVector2f(
        FMath::Clamp(UV.X - FMath::Floor(UV.X), 0.001f, 0.999f),
        FMath::Clamp(UV.Y - FMath::Floor(UV.Y), 0.001f, 0.999f));
}

bool SWetnessProfileViewport::TryResolveCameraCenterSplashUV(FVector2f& OutUV) const
{
    OutUV = ResolveScenarioSplashUV();
    return true;
}

void SWetnessProfileViewport::RefreshScenarioSplashUV()
{
    PreviewScenarioSplashUV = ResolveScenarioSplashUV();
    if (GPUPreviewSimulator && GPUPreviewSimulator->IsReady())
    {
        GPUPreviewSimulator->SetScenarioSplashUV(PreviewScenarioSplashUV);
    }
}

void SWetnessProfileViewport::RefreshScenarioSplashUVFromCamera()
{
    FVector2f ResolvedUV = FVector2f(0.5f, 0.5f);
    if (TryResolveCameraCenterSplashUV(ResolvedUV))
    {
        PreviewScenarioSplashUV = ResolvedUV;
    }
    else
    {
        PreviewScenarioSplashUV = ResolveScenarioSplashUV();
    }

    if (GPUPreviewSimulator && GPUPreviewSimulator->IsReady())
    {
        GPUPreviewSimulator->SetScenarioSplashUV(PreviewScenarioSplashUV);
    }
}

bool SWetnessProfileViewport::EnsureGPUPreviewSimulator()
{
    if (GPUPreviewSimulator && GPUPreviewSimulator->IsReady())
    {
        return true;
    }
    if (bGPUPreviewUnavailable || !PreviewScene.IsValid())
    {
        return false;
    }

    IDWCGPUModule* GPUModule = FModuleManager::Get().LoadModulePtr<IDWCGPUModule>(TEXT("DWCGPU"));
    if (GPUModule == nullptr)
    {
        bGPUPreviewUnavailable = true;
        UE_LOG(LogDWC, Warning, TEXT("DWC Wetness Profile simulation preview could not load the optional DWCGPU module."));
        return false;
    }

    TUniquePtr<IDWCGPUPreviewSimulator> NewSimulator = GPUModule->CreatePreviewSimulator();
    if (!NewSimulator)
    {
        bGPUPreviewUnavailable = true;
        return false;
    }

    const FWetClothingSettings Defaults;
    FDWCGPUPreviewInitArgs     Args;
    Args.WorldContextObject = PreviewScene->GetWorld();
    Args.Resolution = 512;
    Args.MaxWetness = Defaults.MaxWetness;
    Args.CapillaryImmediateAbsorptionFraction = Defaults.CapillaryImmediateAbsorptionFraction;
    Args.bUseEightDirectionDiffusion = false;
    if (!NewSimulator->Initialize(Args))
    {
        bGPUPreviewUnavailable = true;
        return false;
    }
    if (WetnessProfile.IsValid())
    {
        NewSimulator->SetProfileParameters(GetSanitizedProfileParameters(WetnessProfile.Get()));
    }
    NewSimulator->SetScenarioSplashUV(PreviewScenarioSplashUV);
    NewSimulator->SetPreviewChannels(
        bPreviewAbsorbedLayerEnabled,
        bPreviewSurfaceLayerEnabled,
        bPreviewDroplet1Enabled,
        bPreviewDroplet2Enabled);
    GPUPreviewSimulator = MoveTemp(NewSimulator);
    return true;
}

void SWetnessProfileViewport::ShutdownGPUPreviewSimulator()
{
    if (GPUPreviewSimulator)
    {
        GPUPreviewSimulator->Shutdown();
        GPUPreviewSimulator.Reset();
    }
}

void SWetnessProfileViewport::TickGPUPreviewSimulation(const float InDeltaTime)
{
    if (PendingSimulationRestartDelay >= 0.0f)
    {
        PendingSimulationRestartDelay -= FMath::Max(InDeltaTime, 0.0f);
        if (PendingSimulationRestartDelay <= 0.0f)
        {
            RestartPreviewSimulation();
        }
    }

    if (!bPreviewAnimationEnabled || PreviewAnimationSpeed <= KINDA_SMALL_NUMBER ||
        !EnsureGPUPreviewSimulator())
    {
        return;
    }

    PreviewSimulationAccumulator += FMath::Max(InDeltaTime, 0.0f) * PreviewAnimationSpeed;
    bool  bStepped = false;
    int32 SafetyCounter = 0;
    while (PreviewSimulationAccumulator >= PreviewFixedStep && SafetyCounter++ < 8)
    {
        if (PreviewAnimationTime >= GetPreviewLoopDuration() - KINDA_SMALL_NUMBER)
        {
            if (bPreviewLoopEnabled)
            {
                GPUPreviewSimulator->Restart();
                PreviewAnimationTime = 0.0f;
                BindGPUPreviewTextures();
            }
            else
            {
                PreviewAnimationTime = GetPreviewLoopDuration();
                bPreviewAnimationEnabled = false;
                UpdateRealtimeState();
                break;
            }
        }
        GPUPreviewSimulator->Step(PreviewFixedStep, PreviewAnimationTime);
        PreviewAnimationTime += PreviewFixedStep;
        PreviewSimulationAccumulator -= PreviewFixedStep;
        bStepped = true;
    }

    if (bStepped)
    {
        RefreshGeneratedPreviewAnimationTime();
        if (ViewportClient.IsValid())
        {
            ViewportClient->Invalidate();
        }
    }
}

void SWetnessProfileViewport::BindGPUPreviewTextures()
{
    if (!GPUPreviewSimulator || !GPUPreviewSimulator->IsReady())
    {
        return;
    }
    const float TexelSize = 1.0f / static_cast<float>(FMath::Max(GPUPreviewSimulator->GetResolution(), 1));
    for (UMaterialInstanceDynamic* PreviewMID : GeneratedPreviewDynamicMaterials)
    {
        if (PreviewMID == nullptr)
        {
            continue;
        }
            PreviewMID->SetScalarParameterValue(
                DWCWetMaterialParameters::UseGPUBackend(), 1.0f);
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterOverrideParameter, 0.0f);
        PreviewMID->SetScalarParameterValue(PreviewSurfaceWaterAmountParameter, 0.0f);
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::WetnessMap(), GPUPreviewSimulator->GetWetnessMap());
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDroplet1RT(), GPUPreviewSimulator->GetDroplet1Map());
        PreviewMID->SetTextureParameterValue(DWCWetMaterialParameters::SurfaceDroplet2RT(), GPUPreviewSimulator->GetDroplet2Map());
        PreviewMID->SetScalarParameterValue(DWCWetMaterialParameters::SurfaceWaterTexelSize(), TexelSize);
        }
}

void SWetnessProfileViewport::ScheduleSimulationRestart()
{
    if (PreviewBehavior == EPreviewBehavior::Simulation)
    {
        PendingSimulationRestartDelay = PreviewRestartDebounce;
    }
}

void SWetnessProfileViewport::UpdateRealtimeState()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->SetRealtime(
            PreviewBehavior == EPreviewBehavior::Simulation &&
            bPreviewAnimationEnabled && PreviewAnimationSpeed > KINDA_SMALL_NUMBER);
    }
}

FText SWetnessProfileViewport::GetOverlayText() const
{
    if (PreviewBehavior == EPreviewBehavior::Simulation)
    {
        if (bGPUPreviewUnavailable)
        {
            return LOCTEXT(
                "SimulationPreviewUnavailable",
                "GPU Simulation Preview unavailable\nThe optional DWCGPU module could not be initialized.");
        }
        FNumberFormattingOptions TimeOptions;
        TimeOptions.MinimumFractionalDigits = 1;
        TimeOptions.MaximumFractionalDigits = 1;
        return FText::Format(
            LOCTEXT(
                "SimulationPreviewHint",
                "GPU Simulation Preview  |  Single Splash\nTime {0} / {1} s  |  {2}x  |  {3}"),
            FText::AsNumber(PreviewAnimationTime, &TimeOptions),
            FText::AsNumber(GetPreviewLoopDuration(), &TimeOptions),
            FText::AsNumber(PreviewAnimationSpeed),
            bPreviewAnimationEnabled ? LOCTEXT("SimulationPlaying", "Playing") : LOCTEXT("SimulationPaused", "Paused"));
    }
    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    return FText::Format(
        LOCTEXT(
            "PreviewHint",
            "Manual Preview\nAbsorbed Wetness {0}%  |  Droplet1/Droplet2 Strength {1}%/{6}%\nDarkening {2}%  |  Absorbed Glossiness {3}%\nDroplet1/Droplet2 Normal {4}%/{8}%  |  Roughness Blend {5}%/{7}%"),
        FText::AsNumber(FMath::RoundToInt(PreviewAbsorbedWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterTotalStrength, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedDarkeningStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(Parameters.GetAbsorbedGlossinessStrength() * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 3.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterRoughnessBlend, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.DropletFlowTotalStrength, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.DropletFlowRoughnessBlend, 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.DropletFlowNormalStrength, 0.0f, 3.0f) * 100.0f)));
}

#undef LOCTEXT_NAMESPACE
