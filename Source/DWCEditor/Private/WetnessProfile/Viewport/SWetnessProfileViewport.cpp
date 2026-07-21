#include "SWetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "MaterialShared.h"
#include "ProceduralMeshComponent.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "DataAssets/WetnessProfile.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetnessProfileViewportClient.h"

#define LOCTEXT_NAMESPACE "WetnessProfileViewport"

namespace
{
    constexpr int32 PreviewLatSegments = 36;
    constexpr int32 PreviewLonSegments = 72;
    constexpr int32 PreviewRainParticleCount = 180;
    constexpr float PreviewSphereRadius = 67.5f;
    constexpr float PreviewWetOverlayRadius = 68.35f;
    constexpr float PreviewRainMinRadius = 35.0f;
    constexpr float PreviewRainMaxRadius = 180.0f;
    constexpr float PreviewRainMaxAmountScale = 3.0f;
    constexpr float PreviewSceneLift = 82.0f;
    constexpr float PreviewRainTopZ = 160.0f;
    constexpr float PreviewRainBottomZ = -95.0f;
    constexpr float PreviewRainStreakLength = 18.0f;
    constexpr float PreviewRainStreakWidth = 0.75f;
    constexpr float PreviewCPUWetnessUpdateInterval = 0.05f;
    constexpr float PreviewGPUWetnessUpdateInterval = 1.0f / 60.0f;
    constexpr int32 PreviewMaxWetnessStageStepsPerTick = 4;

    void FinalizeTransientPreviewMaterial(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return;
        }

        Material->UpdateCachedExpressionData();

        FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
        UpdateContext.AddMaterial(Material);
        Material->PreEditChange(nullptr);
        Material->PostEditChange();
    }

    UMaterial* CreateWhitePreviewMaterial()
    {
        UMaterial* Material = NewObject<UMaterial>(
            GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("DWC_WetnessProfileWhitePreviewMaterial")),
            RF_Transient);
        if (Material == nullptr)
        {
            return nullptr;
        }

        UMaterialExpressionConstant3Vector* BaseColor = Cast<UMaterialExpressionConstant3Vector>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -300, 0));
        UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -300, 120));
        if (BaseColor != nullptr)
        {
            BaseColor->Constant = FLinearColor::White;
            UMaterialEditingLibrary::ConnectMaterialProperty(BaseColor, FString(), MP_BaseColor);
        }
        if (Roughness != nullptr)
        {
            Roughness->R = 0.82f;
            UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, FString(), MP_Roughness);
        }

        FinalizeTransientPreviewMaterial(Material);
        return Material;
    }

    UMaterial* CreateTranslucentVertexColorMaterial()
    {
        UMaterial* Material = NewObject<UMaterial>(
            GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("DWC_WetnessProfileVertexColorPreviewMaterial")),
            RF_Transient);
        if (Material == nullptr)
        {
            return nullptr;
        }

        Material->BlendMode = BLEND_Translucent;
        Material->TwoSided = true;
        Material->SetShadingModel(MSM_Unlit);

        UMaterialExpressionVertexColor* VertexColor = Cast<UMaterialExpressionVertexColor>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVertexColor::StaticClass(), -520, 0));
        UMaterialExpressionComponentMask* RGBMask = Cast<UMaterialExpressionComponentMask>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -260, -80));
        UMaterialExpressionComponentMask* AlphaMask = Cast<UMaterialExpressionComponentMask>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -260, 80));
        UMaterialExpressionConstant* Roughness = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), -260, 210));

        if (VertexColor != nullptr && RGBMask != nullptr)
        {
            RGBMask->R = true;
            RGBMask->G = true;
            RGBMask->B = true;
            RGBMask->A = false;
            UMaterialEditingLibrary::ConnectMaterialExpressions(VertexColor, FString(), RGBMask, FString());
            UMaterialEditingLibrary::ConnectMaterialProperty(RGBMask, FString(), MP_BaseColor);
            UMaterialEditingLibrary::ConnectMaterialProperty(RGBMask, FString(), MP_EmissiveColor);
        }

        if (VertexColor != nullptr && AlphaMask != nullptr)
        {
            AlphaMask->R = false;
            AlphaMask->G = false;
            AlphaMask->B = false;
            AlphaMask->A = true;
            UMaterialEditingLibrary::ConnectMaterialExpressions(VertexColor, FString(), AlphaMask, FString());
            UMaterialEditingLibrary::ConnectMaterialProperty(AlphaMask, FString(), MP_Opacity);
        }

        if (Roughness != nullptr)
        {
            Roughness->R = 0.16f;
            UMaterialEditingLibrary::ConnectMaterialProperty(Roughness, FString(), MP_Roughness);
        }

        FinalizeTransientPreviewMaterial(Material);
        return Material;
    }

    const TCHAR* PreviewSimulationModeLabel(EDWCSimulationMode Mode)
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU ? TEXT("GPU") : TEXT("CPU");
    }

    const FTransform PreviewLiftTransform()
    {
        return FTransform(FVector(0.0, 0.0, PreviewSceneLift));
    }
}

void SWetnessProfileViewport::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    PreviewRandomStream.Initialize(49157);
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    PreviewScene->SetFloorVisibility(true, true);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    InitializePreviewComponents();
    ResetPreviewSimulation();
    RefreshPreviewScene();
}

SWetnessProfileViewport::~SWetnessProfileViewport()
{
    if (PreviewScene.IsValid())
    {
        if (PreviewMeshComponent != nullptr)
        {
            PreviewScene->RemoveComponent(PreviewMeshComponent);
        }
        if (PreviewSimulationMeshComponent != nullptr)
        {
            PreviewScene->RemoveComponent(PreviewSimulationMeshComponent);
        }
        if (WetnessOverlayComponent != nullptr)
        {
            PreviewScene->RemoveComponent(WetnessOverlayComponent);
        }
        if (RainParticleComponent != nullptr)
        {
            PreviewScene->RemoveComponent(RainParticleComponent);
        }
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->Viewport = nullptr;
    }
}

void SWetnessProfileViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(PreviewSimulationMeshComponent);
    Collector.AddReferencedObject(WetnessOverlayComponent);
    Collector.AddReferencedObject(RainParticleComponent);
    Collector.AddReferencedObject(WhitePreviewMaterial);
    Collector.AddReferencedObject(TranslucentVertexColorMaterial);
}

void SWetnessProfileViewport::RefreshPreviewScene()
{
    if (!PreviewScene.IsValid())
    {
        return;
    }
    PreviewScene->SetFloorVisibility(true, true);

    if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        PreviewMeshComponent->SetStaticMesh(SphereMesh);
        PreviewMeshComponent->SetRelativeScale3D(FVector(1.35f));
        PreviewMeshComponent->SetMaterial(0, ResolveWhitePreviewMaterial());
    }
    RefreshPreviewWetnessProfileParameters();

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        if (!bPreviewCameraInitialized)
        {
            ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
            bPreviewCameraInitialized = true;
        }
    }
}

void SWetnessProfileViewport::SetPreviewSimulationMode(EDWCSimulationMode NewMode)
{
    if (PreviewSimulationMode == NewMode)
    {
        return;
    }

    PreviewSimulationMode = NewMode;
    ResetPreviewSimulation();
}

void SWetnessProfileViewport::SetPreviewRainRadius(float InRadius)
{
    const float NewRadius = FMath::Clamp(InRadius, PreviewRainMinRadius, PreviewRainMaxRadius);
    if (FMath::IsNearlyEqual(PreviewRainRadius, NewRadius))
    {
        return;
    }

    PreviewRainRadius = NewRadius;
    ResetRainParticles();
    RefreshRainVisuals();
}

void SWetnessProfileViewport::SetPreviewRainAmountScale(float InAmountScale)
{
    const float NewAmountScale = FMath::Clamp(InAmountScale, 0.0f, PreviewRainMaxAmountScale);
    if (FMath::IsNearlyEqual(PreviewRainAmountScale, NewAmountScale))
    {
        return;
    }

    PreviewRainAmountScale = NewAmountScale;
    ResetRainParticles();
    RefreshRainVisuals();
}

void SWetnessProfileViewport::SetPreviewWetnessDebugColorEnabled(bool bEnabled)
{
    if (bPreviewWetnessDebugColorEnabled == bEnabled)
    {
        return;
    }

    bPreviewWetnessDebugColorEnabled = bEnabled;
    bWetnessOverlayDirty = true;
    RebuildWetnessOverlayMesh();
}

void SWetnessProfileViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

void SWetnessProfileViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    UpdatePreviewSimulation(FMath::Clamp(InDeltaTime, 0.0f, 1.0f / 15.0f));
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
    if (!PreviewScene.IsValid())
    {
        return;
    }

    if (PreviewMeshComponent == nullptr)
    {
        PreviewMeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
        PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PreviewMeshComponent->SetCastShadow(false);
        PreviewScene->AddComponent(PreviewMeshComponent, PreviewLiftTransform());
    }

    if (PreviewSimulationMeshComponent == nullptr)
    {
        PreviewSimulationMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        PreviewSimulationMeshComponent->SetMobility(EComponentMobility::Movable);
        PreviewSimulationMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        PreviewSimulationMeshComponent->SetCastShadow(false);
        PreviewSimulationMeshComponent->SetVisibility(false, true);
        PreviewScene->AddComponent(PreviewSimulationMeshComponent, PreviewLiftTransform());
    }

    if (WetnessOverlayComponent == nullptr)
    {
        WetnessOverlayComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        WetnessOverlayComponent->SetMobility(EComponentMobility::Movable);
        WetnessOverlayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        WetnessOverlayComponent->SetCastShadow(false);
        WetnessOverlayComponent->SetMaterial(0, ResolveTranslucentVertexColorMaterial());
        PreviewScene->AddComponent(WetnessOverlayComponent, PreviewLiftTransform());
    }

    if (RainParticleComponent == nullptr)
    {
        RainParticleComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
        RainParticleComponent->SetMobility(EComponentMobility::Movable);
        RainParticleComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        RainParticleComponent->SetCastShadow(false);
        RainParticleComponent->SetMaterial(0, ResolveTranslucentVertexColorMaterial());
        PreviewScene->AddComponent(RainParticleComponent, PreviewLiftTransform());
    }

    RefreshRainVisuals();
}

void SWetnessProfileViewport::ResetPreviewSimulation()
{
    RebuildPreviewWetnessRuntime();
    ResetRainParticles();

    PreviewTimeSeconds = 0.0f;
    PreviewWetnessUpdateAccumulator = 0.0f;
    bWetnessOverlayDirty = true;
    RebuildWetnessOverlayMesh();
    RefreshRainVisuals();
}

void SWetnessProfileViewport::ResetRainParticles()
{
    const int32 ActiveParticleCount = FMath::Clamp(
        FMath::RoundToInt(static_cast<float>(PreviewRainParticleCount) * PreviewRainAmountScale),
        0,
        PreviewRainParticleCount * 3);

    RainParticles.Reset();
    RainParticles.SetNum(ActiveParticleCount);
    for (FPreviewRainParticle& Particle : RainParticles)
    {
        RespawnRainParticle(Particle, true);
    }

    RefreshRainVisuals();
}

void SWetnessProfileViewport::RebuildPreviewWetnessRuntime()
{
    const int32 VertexCount = (PreviewLatSegments + 1) * PreviewLonSegments;
    if (VertexCount <= 0)
    {
        return;
    }

    if (!PreviewRuntimeData.IsValid())
    {
        PreviewRuntimeData = MakeUnique<FWetClothingRuntimeData>();
    }
    if (!PreviewSimulationState.IsValid())
    {
        PreviewSimulationState = MakeUnique<FAbsorbedWetnessSimulationState>();
    }
    if (!PreviewSimulationStage.IsValid())
    {
        PreviewSimulationStage = MakeUnique<FWetSimulationStage>();
    }
    if (!PreviewMeshSampler.IsValid())
    {
        PreviewMeshSampler = MakeUnique<FWetClothingMeshSampler>();
    }

    PreviewWetnessSettings = FWetClothingSettings();
    PreviewWetnessSettings.WetnessUpdateInterval = PreviewSimulationMode == EDWCSimulationMode::WetnessMapGPU
                                                       ? PreviewGPUWetnessUpdateInterval
                                                       : PreviewCPUWetnessUpdateInterval;
    PreviewWetnessSettings.WetnessRenderUpdateInterval = PreviewWetnessSettings.WetnessUpdateInterval;
    PreviewWetnessSettings.MaxPendingWetnessVerticesPerUpdate = VertexCount;

    PreviewRuntimeData->VertexWettableFlags.Init(true, VertexCount);
    PreviewRuntimeData->VertexWetPartIDs.Init(0, VertexCount);
    PreviewRuntimeData->WetnessProfileTable.Reset();
    PreviewRuntimeData->WetnessProfileTable.AddDefaulted();
    PreviewRuntimeData->VertexWetnessProfileIndices.Init(0, VertexCount);
    RefreshPreviewWetnessProfileParameters();
    PreviewRuntimeData->NeighborRanges.SetNum(VertexCount);
    PreviewRuntimeData->FlatNeighborIndices.Reset();
    PreviewRuntimeData->FlatNeighborIndices.Reserve(VertexCount * 4);
    PreviewRuntimeData->bHasNeighborGraph = true;

    for (int32 LatIndex = 0; LatIndex <= PreviewLatSegments; ++LatIndex)
    {
        for (int32 LonIndex = 0; LonIndex < PreviewLonSegments; ++LonIndex)
        {
            const int32 VertexIndex = GetWetnessSampleIndex(LatIndex, LonIndex);
            FWetVertexNeighborRange& NeighborRange = PreviewRuntimeData->NeighborRanges[VertexIndex];
            NeighborRange.StartOffset = PreviewRuntimeData->FlatNeighborIndices.Num();
            NeighborRange.Count = 0;

            auto AddNeighbor = [this, &NeighborRange](const int32 NeighborIndex)
            {
                for (int32 Offset = NeighborRange.StartOffset;
                     Offset < PreviewRuntimeData->FlatNeighborIndices.Num();
                     ++Offset)
                {
                    if (PreviewRuntimeData->FlatNeighborIndices[Offset] == NeighborIndex)
                    {
                        return;
                    }
                }

                PreviewRuntimeData->FlatNeighborIndices.Add(NeighborIndex);
                ++NeighborRange.Count;
            };

            AddNeighbor(GetWetnessSampleIndex(LatIndex, LonIndex - 1));
            AddNeighbor(GetWetnessSampleIndex(LatIndex, LonIndex + 1));
            if (LatIndex > 0)
            {
                AddNeighbor(GetWetnessSampleIndex(LatIndex - 1, LonIndex));
            }
            if (LatIndex < PreviewLatSegments)
            {
                AddNeighbor(GetWetnessSampleIndex(LatIndex + 1, LonIndex));
            }
        }
    }

    PreviewSimulationState->ResetForVertexCount(VertexCount);
    PreviewMeshSampler->CachedSkinnedPositions.SetNum(VertexCount);
    for (int32 LatIndex = 0; LatIndex <= PreviewLatSegments; ++LatIndex)
    {
        for (int32 LonIndex = 0; LonIndex < PreviewLonSegments; ++LonIndex)
        {
            const int32 VertexIndex = GetWetnessSampleIndex(LatIndex, LonIndex);
            PreviewMeshSampler->CachedSkinnedPositions[VertexIndex] =
                FVector3f(GetWetnessSamplePosition(LatIndex, LonIndex, PreviewSphereRadius));
        }
    }
}

void SWetnessProfileViewport::RefreshPreviewWetnessProfileParameters()
{
    if (!PreviewRuntimeData.IsValid())
    {
        return;
    }

    const UWetnessProfile* Profile = WetnessProfile.Get();
    const FWetnessProfileParameters ProfileParameters = Profile != nullptr
                                                            ? Profile->GetParameters()
                                                            : FWetnessProfileParameters();
    if (PreviewRuntimeData->WetnessProfileTable.IsEmpty())
    {
        PreviewRuntimeData->WetnessProfileTable.Add(ProfileParameters);
    }
    else
    {
        PreviewRuntimeData->WetnessProfileTable[0] = ProfileParameters;
    }
}

void SWetnessProfileViewport::UpdatePreviewSimulation(float DeltaSeconds)
{
    if (DeltaSeconds <= 0.0f)
    {
        return;
    }

    PreviewTimeSeconds += DeltaSeconds;
    UpdateRainParticles(DeltaSeconds);
    UpdateWetnessSimulation(DeltaSeconds);

    if (bWetnessOverlayDirty)
    {
        RebuildWetnessOverlayMesh();
    }
    RebuildRainParticleMesh();

    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetnessProfileViewport::UpdateRainParticles(float DeltaSeconds)
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    const float Absorption = Profile != nullptr ? Profile->GetAbsorptionMultiplier() : 1.0f;
    const float WetnessPerImpact = 0.045f * FMath::Max(0.1f, Absorption);

    for (FPreviewRainParticle& Particle : RainParticles)
    {
        const double PreviousZ = Particle.Position.Z;
        Particle.Position.Z -= Particle.Speed * DeltaSeconds;

        const double XYDistanceSq = Particle.Position.X * Particle.Position.X + Particle.Position.Y * Particle.Position.Y;
        const double SphereRadiusSq = FMath::Square(static_cast<double>(PreviewSphereRadius));
        if (XYDistanceSq <= SphereRadiusSq)
        {
            const double ImpactZ = FMath::Sqrt(FMath::Max(SphereRadiusSq - XYDistanceSq, 0.0));
            if (PreviousZ >= ImpactZ && Particle.Position.Z < ImpactZ)
            {
                AddWetnessAtWorldPoint(FVector(Particle.Position.X, Particle.Position.Y, ImpactZ), WetnessPerImpact);
            }
        }

        if (Particle.Position.Z < PreviewRainBottomZ)
        {
            RespawnRainParticle(Particle, false);
        }
    }
}

void SWetnessProfileViewport::UpdateWetnessSimulation(float DeltaSeconds)
{
    if (!PreviewSimulationStage.IsValid() ||
        !PreviewSimulationState.IsValid() ||
        !PreviewRuntimeData.IsValid() ||
        !PreviewMeshSampler.IsValid() ||
        DeltaSeconds <= 0.0f)
    {
        return;
    }

    PreviewWetnessUpdateAccumulator += DeltaSeconds;
    int32 StepCount = 0;
    while (PreviewWetnessUpdateAccumulator >= PreviewWetnessSettings.WetnessUpdateInterval &&
           StepCount < PreviewMaxWetnessStageStepsPerTick)
    {
        FWetSimulationStageArgs Args = MakePreviewWetSimulationArgs();
        if (PreviewSimulationStage->UpdateWetness(Args))
        {
            bWetnessOverlayDirty = true;
        }

        PreviewWetnessUpdateAccumulator -= PreviewWetnessSettings.WetnessUpdateInterval;
        ++StepCount;
    }

    if (StepCount >= PreviewMaxWetnessStageStepsPerTick)
    {
        PreviewWetnessUpdateAccumulator = 0.0f;
    }
}

void SWetnessProfileViewport::AddWetnessAtWorldPoint(const FVector& WorldPoint, float Amount)
{
    const FVector Normal = WorldPoint.GetSafeNormal();
    if (Normal.IsNearlyZero() ||
        !PreviewSimulationStage.IsValid() ||
        !PreviewSimulationState.IsValid() ||
        !PreviewRuntimeData.IsValid())
    {
        return;
    }

    const double Theta = FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0));
    double Phi = FMath::Atan2(Normal.Y, Normal.X);
    if (Phi < 0.0f)
    {
        Phi += UE_TWO_PI;
    }

    const int32 CenterLat = FMath::Clamp(FMath::RoundToInt((Theta / UE_PI) * PreviewLatSegments), 0, PreviewLatSegments);
    const int32 CenterLon = FMath::RoundToInt((Phi / UE_TWO_PI) * PreviewLonSegments);
    FWetSimulationStageArgs Args = MakePreviewWetSimulationArgs();
    for (int32 LatOffset = -2; LatOffset <= 2; ++LatOffset)
    {
        for (int32 LonOffset = -2; LonOffset <= 2; ++LonOffset)
        {
            const float Distance = FVector2D(static_cast<float>(LatOffset), static_cast<float>(LonOffset)).Size();
            if (Distance > 2.25f)
            {
                continue;
            }

            const int32 SampleIndex = GetWetnessSampleIndex(FMath::Clamp(CenterLat + LatOffset, 0, PreviewLatSegments), CenterLon + LonOffset);
            const float Falloff = 1.0f - FMath::Clamp(Distance / 2.25f, 0.0f, 1.0f);
            PreviewSimulationStage->QueuePendingWetness(Args, SampleIndex, Amount * Falloff);
        }
    }

    PreviewWetnessUpdateAccumulator = FMath::Max(
        PreviewWetnessUpdateAccumulator,
        PreviewWetnessSettings.WetnessUpdateInterval);
    bWetnessOverlayDirty = true;
}

FWetSimulationStageArgs SWetnessProfileViewport::MakePreviewWetSimulationArgs()
{
    FWetSimulationStageArgs Args;
    Args.TargetSkeletalMesh = PreviewSimulationMeshComponent;
    Args.WetnessSettings = &PreviewWetnessSettings;
    Args.RuntimeData = PreviewRuntimeData.Get();
    Args.SimulationState = PreviewSimulationState.Get();
    Args.MeshSampler = PreviewMeshSampler.Get();
    return Args;
}

void SWetnessProfileViewport::RebuildWetnessOverlayMesh()
{
    bWetnessOverlayDirty = false;
    if (WetnessOverlayComponent == nullptr)
    {
        return;
    }

    const TArray<float>* WetnessSamples = PreviewSimulationState.IsValid()
                                              ? &PreviewSimulationState->AbsorbedWetnessPerVertex
                                              : nullptr;
    if (WetnessSamples == nullptr || WetnessSamples->IsEmpty())
    {
        WetnessOverlayComponent->ClearAllMeshSections();
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    const UWetnessProfile* Profile = WetnessProfile.Get();
    const float VisualStrength = Profile != nullptr ? Profile->GetWetVisualStrength() : 1.0f;

    for (int32 LatIndex = 0; LatIndex < PreviewLatSegments; ++LatIndex)
    {
        for (int32 LonIndex = 0; LonIndex < PreviewLonSegments; ++LonIndex)
        {
            const int32 NextLat = LatIndex + 1;
            const int32 NextLon = LonIndex + 1;
            const float W00 = (*WetnessSamples)[GetWetnessSampleIndex(LatIndex, LonIndex)];
            const float W10 = (*WetnessSamples)[GetWetnessSampleIndex(NextLat, LonIndex)];
            const float W11 = (*WetnessSamples)[GetWetnessSampleIndex(NextLat, NextLon)];
            const float W01 = (*WetnessSamples)[GetWetnessSampleIndex(LatIndex, NextLon)];
            const float AverageWetness = (W00 + W10 + W11 + W01) * 0.25f;
            if (AverageWetness < 0.018f)
            {
                continue;
            }

            const int32 BaseIndex = Vertices.Num();
            const FVector Positions[4] = {
                GetWetnessSamplePosition(LatIndex, LonIndex, PreviewWetOverlayRadius),
                GetWetnessSamplePosition(NextLat, LonIndex, PreviewWetOverlayRadius),
                GetWetnessSamplePosition(NextLat, NextLon, PreviewWetOverlayRadius),
                GetWetnessSamplePosition(LatIndex, NextLon, PreviewWetOverlayRadius),
            };
            const float WetValues[4] = { W00, W10, W11, W01 };
            const FVector2D QuadUVs[4] = {
                FVector2D(0.0f, 0.0f),
                FVector2D(0.0f, 1.0f),
                FVector2D(1.0f, 1.0f),
                FVector2D(1.0f, 0.0f),
            };

            for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
            {
                const float Wetness = FMath::Clamp(WetValues[CornerIndex] * VisualStrength, 0.0f, 1.0f);
                Vertices.Add(Positions[CornerIndex]);
                Normals.Add(Positions[CornerIndex].GetSafeNormal());
                UVs.Add(QuadUVs[CornerIndex]);
                const FLinearColor DryColor = bPreviewWetnessDebugColorEnabled
                                                  ? FLinearColor(1.0f, 0.20f, 0.72f, 0.18f)
                                                  : FLinearColor(0.62f, 0.86f, 1.0f, 0.18f);
                const FLinearColor WetColor = bPreviewWetnessDebugColorEnabled
                                                  ? FLinearColor(1.0f, 0.0f, 0.45f, 0.90f)
                                                  : FLinearColor(0.02f, 0.15f, 0.32f, 0.78f);
                const FLinearColor Color = FLinearColor::LerpUsingHSV(DryColor, WetColor, Wetness);
                const float Alpha = bPreviewWetnessDebugColorEnabled
                                        ? FMath::Clamp(0.20f + Wetness * 0.75f, 0.0f, 0.95f)
                                        : FMath::Clamp(0.12f + Wetness * 0.66f, 0.0f, 0.82f);
                VertexColors.Add(FLinearColor(Color.R, Color.G, Color.B, Alpha));
                Tangents.Add(FProcMeshTangent(FVector::RightVector, false));
            }

            Indices.Add(BaseIndex + 0);
            Indices.Add(BaseIndex + 1);
            Indices.Add(BaseIndex + 2);
            Indices.Add(BaseIndex + 0);
            Indices.Add(BaseIndex + 2);
            Indices.Add(BaseIndex + 3);
        }
    }

    WetnessOverlayComponent->ClearAllMeshSections();
    if (Vertices.Num() > 0)
    {
        WetnessOverlayComponent->CreateMeshSection_LinearColor(
            0,
            Vertices,
            Indices,
            Normals,
            UVs,
            VertexColors,
            Tangents,
            false,
            false);
        WetnessOverlayComponent->SetMaterial(0, ResolveTranslucentVertexColorMaterial());
    }
}

void SWetnessProfileViewport::RebuildRainParticleMesh()
{
    if (RainParticleComponent == nullptr)
    {
        return;
    }

    const bool bUseFallbackRainMesh = PreviewRainAmountScale > 0.0f;
    RainParticleComponent->SetVisibility(bUseFallbackRainMesh, true);
    if (!bUseFallbackRainMesh)
    {
        RainParticleComponent->ClearAllMeshSections();
        return;
    }

    TArray<FVector> Vertices;
    TArray<int32> Indices;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(RainParticles.Num() * 8);
    Indices.Reserve(RainParticles.Num() * 12);

    for (const FPreviewRainParticle& Particle : RainParticles)
    {
        const FVector Top = Particle.Position + FVector(0.0, 0.0, PreviewRainStreakLength);
        const FVector Bottom = Particle.Position;
        const FVector WidthAxes[2] = {
            FVector(PreviewRainStreakWidth, 0.0, 0.0),
            FVector(0.0, PreviewRainStreakWidth, 0.0),
        };

        for (const FVector& Width : WidthAxes)
        {
            const int32 BaseIndex = Vertices.Num();
            Vertices.Add(Top - Width);
            Vertices.Add(Top + Width);
            Vertices.Add(Bottom + Width);
            Vertices.Add(Bottom - Width);

            Indices.Add(BaseIndex + 0);
            Indices.Add(BaseIndex + 1);
            Indices.Add(BaseIndex + 2);
            Indices.Add(BaseIndex + 0);
            Indices.Add(BaseIndex + 2);
            Indices.Add(BaseIndex + 3);

            for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
            {
                Normals.Add(FVector::ForwardVector);
                UVs.Add(FVector2D::ZeroVector);
                const float ParticleAlpha = FMath::Clamp(0.22f + PreviewRainAmountScale * 0.18f, 0.0f, 0.72f);
                VertexColors.Add(FLinearColor::White.CopyWithNewOpacity(ParticleAlpha));
                Tangents.Add(FProcMeshTangent(FVector::RightVector, false));
            }
        }
    }

    RainParticleComponent->ClearAllMeshSections();
    RainParticleComponent->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Indices,
        Normals,
        UVs,
        VertexColors,
        Tangents,
        false,
        false);
    RainParticleComponent->SetMaterial(0, ResolveTranslucentVertexColorMaterial());
}

void SWetnessProfileViewport::RefreshRainVisuals()
{
    RebuildRainParticleMesh();
}

void SWetnessProfileViewport::RespawnRainParticle(FPreviewRainParticle& Particle, bool bRandomizeHeight)
{
    const float Radius = PreviewRainRadius * FMath::Sqrt(PreviewRandomStream.GetFraction());
    const float Angle = PreviewRandomStream.GetFraction() * UE_TWO_PI;
    Particle.Position = FVector(
        FMath::Cos(Angle) * Radius,
        FMath::Sin(Angle) * Radius,
        PreviewRainTopZ + (bRandomizeHeight ? PreviewRandomStream.FRandRange(0.0f, PreviewRainTopZ - PreviewRainBottomZ) : PreviewRandomStream.FRandRange(0.0f, 40.0f)));
    Particle.Speed = PreviewRandomStream.FRandRange(190.0f, 315.0f);
}

int32 SWetnessProfileViewport::GetWetnessSampleIndex(int32 LatIndex, int32 LonIndex) const
{
    const int32 ClampedLat = FMath::Clamp(LatIndex, 0, PreviewLatSegments);
    const int32 WrappedLon = (LonIndex % PreviewLonSegments + PreviewLonSegments) % PreviewLonSegments;
    return ClampedLat * PreviewLonSegments + WrappedLon;
}

FVector SWetnessProfileViewport::GetWetnessSamplePosition(int32 LatIndex, int32 LonIndex, float Radius) const
{
    const float Theta = (static_cast<float>(FMath::Clamp(LatIndex, 0, PreviewLatSegments)) / static_cast<float>(PreviewLatSegments)) * UE_PI;
    const float Phi = (static_cast<float>((LonIndex % PreviewLonSegments + PreviewLonSegments) % PreviewLonSegments) / static_cast<float>(PreviewLonSegments)) * UE_TWO_PI;
    const float SinTheta = FMath::Sin(Theta);
    return FVector(
        Radius * SinTheta * FMath::Cos(Phi),
        Radius * SinTheta * FMath::Sin(Phi),
        Radius * FMath::Cos(Theta));
}

UMaterialInterface* SWetnessProfileViewport::ResolveWhitePreviewMaterial()
{
    if (WhitePreviewMaterial == nullptr)
    {
        WhitePreviewMaterial = CreateWhitePreviewMaterial();
    }
    return WhitePreviewMaterial;
}

UMaterialInterface* SWetnessProfileViewport::ResolveTranslucentVertexColorMaterial()
{
    if (TranslucentVertexColorMaterial == nullptr)
    {
        TranslucentVertexColorMaterial = CreateTranslucentVertexColorMaterial();
    }
    return TranslucentVertexColorMaterial;
}

FText SWetnessProfileViewport::GetOverlayText() const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    const float Absorption = Profile != nullptr ? Profile->GetAbsorptionMultiplier() : 1.0f;
    const float SpreadRate = Profile != nullptr ? Profile->GetSpreadRatePerSecond() : 1.0f;
    const float DryRatePercent = Profile != nullptr ? Profile->GetParameters().DryRate : 0.0f;
    return FText::Format(
        LOCTEXT(
            "PreviewHint",
            "Wetness Profile Preview\nMode: {0}  Wetness Debug: {1}\nRain radius {2} cm  Amount {3}%\nAbsorb {4}  Spread {5}  Dry {6}%"),
        FText::FromString(PreviewSimulationModeLabel(PreviewSimulationMode)),
        bPreviewWetnessDebugColorEnabled ? LOCTEXT("WetnessDebugOn", "Pink") : LOCTEXT("WetnessDebugOff", "Off"),
        FText::AsNumber(FMath::RoundToInt(PreviewRainRadius)),
        FText::AsNumber(FMath::RoundToInt(PreviewRainAmountScale * 100.0f)),
        FText::AsNumber(Absorption),
        FText::AsNumber(SpreadRate),
        FText::AsNumber(DryRatePercent));
}

#undef LOCTEXT_NAMESPACE
