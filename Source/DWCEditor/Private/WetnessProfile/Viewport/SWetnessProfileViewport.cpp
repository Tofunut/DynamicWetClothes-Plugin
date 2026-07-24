#include "SWetnessProfileViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfileViewportClient.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileViewport"

namespace
{
    constexpr float PreviewSceneLift = 82.0f;
    constexpr float PreviewSphereScale = 1.35f;

    const FName PreviewAbsorbedWaterParameter(TEXT("DWCPreview_AbsorbedWater"));
    const FName PreviewSurfaceWaterParameter(TEXT("DWCPreview_SurfaceWater"));
    const FName PreviewAbsorbedEnabledParameter(TEXT("DWCPreview_AbsorbedEnabled"));
    const FName PreviewSurfaceEnabledParameter(TEXT("DWCPreview_SurfaceEnabled"));
    const FName PreviewWetVisualStrengthParameter(TEXT("DWCPreview_WetVisualStrength"));
    const FName PreviewSurfaceNormalStrengthParameter(TEXT("DWCPreview_SurfaceNormalStrength"));
    const FName PreviewSurfaceRoughnessStrengthParameter(TEXT("DWCPreview_SurfaceRoughnessStrength"));
    const FName PreviewSurfaceVisibilityThresholdParameter(TEXT("DWCPreview_SurfaceVisibilityThreshold"));
    const FName PreviewDropletsEnabledParameter(TEXT("DWCPreview_DropletsEnabled"));
    const FName PreviewRivuletsEnabledParameter(TEXT("DWCPreview_RivuletsEnabled"));
    const FName PreviewRivuletScrollSpeedParameter(TEXT("DWCPreview_RivuletScrollSpeed"));
    const FName PreviewDropletNormalTextureParameter(TEXT("DWCPreview_DropletNormal"));
    const FName PreviewRivuletNormalTextureParameter(TEXT("DWCPreview_RivuletNormal"));

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
        if (UTexture* DefaultNormal = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return DefaultNormal;
        }

        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

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

    UMaterialExpressionScalarParameter* CreateScalarParameter(
        UMaterial* Material,
        const FName ParameterName,
        const float DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionScalarParameter::StaticClass(),
                NodeX,
                NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* CreateNormalTextureParameter(
        UMaterial* Material,
        const FName ParameterName,
        UTexture* DefaultTexture,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureObjectParameter::StaticClass(),
                NodeX,
                NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Normal;
            Parameter->Texture = DefaultTexture;
        }
        return Parameter;
    }

    UMaterialExpressionCustom* CreateCustomExpression(
        UMaterial* Material,
        const TCHAR* Description,
        const TCHAR* Code,
        const ECustomMaterialOutputType OutputType,
        const TArray<FName>& InputNames,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionCustom::StaticClass(),
                NodeX,
                NodeY));
        if (Custom == nullptr)
        {
            return nullptr;
        }

        Custom->Description = Description;
        Custom->Code = Code;
        Custom->OutputType = OutputType;
        Custom->Inputs.Reset();
        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }
        return Custom;
    }

    bool ConnectExpression(
        UMaterialExpression* FromExpression,
        UMaterialExpression* ToExpression,
        const TCHAR* ToInput)
    {
        return FromExpression != nullptr &&
               ToExpression != nullptr &&
               UMaterialEditingLibrary::ConnectMaterialExpressions(
                   FromExpression,
                   FString(),
                   ToExpression,
                   ToInput);
    }

    UMaterial* CreateWetnessPreviewMaterial()
    {
        UMaterial* Material = NewObject<UMaterial>(
            GetTransientPackage(),
            MakeUniqueObjectName(
                GetTransientPackage(),
                UMaterial::StaticClass(),
                TEXT("DWC_WetnessProfileGrayPreviewMaterial")),
            RF_Transient);
        if (Material == nullptr)
        {
            return nullptr;
        }

        Material->BlendMode = BLEND_Opaque;
        Material->TwoSided = false;
        Material->SetShadingModel(MSM_DefaultLit);

        UMaterialExpressionScalarParameter* AbsorbedWater = CreateScalarParameter(
            Material, PreviewAbsorbedWaterParameter, 0.5f, -1250, -520);
        UMaterialExpressionScalarParameter* SurfaceWater = CreateScalarParameter(
            Material, PreviewSurfaceWaterParameter, 0.5f, -1250, -420);
        UMaterialExpressionScalarParameter* AbsorbedEnabled = CreateScalarParameter(
            Material, PreviewAbsorbedEnabledParameter, 1.0f, -1250, -320);
        UMaterialExpressionScalarParameter* SurfaceEnabled = CreateScalarParameter(
            Material, PreviewSurfaceEnabledParameter, 1.0f, -1250, -220);
        UMaterialExpressionScalarParameter* WetVisualStrength = CreateScalarParameter(
            Material, PreviewWetVisualStrengthParameter, 1.0f, -1250, -120);
        UMaterialExpressionScalarParameter* SurfaceNormalStrength = CreateScalarParameter(
            Material, PreviewSurfaceNormalStrengthParameter, 1.0f, -1250, -20);
        UMaterialExpressionScalarParameter* SurfaceRoughnessStrength = CreateScalarParameter(
            Material, PreviewSurfaceRoughnessStrengthParameter, 1.0f, -1250, 80);
        UMaterialExpressionScalarParameter* SurfaceVisibilityThreshold = CreateScalarParameter(
            Material, PreviewSurfaceVisibilityThresholdParameter, 0.25f, -1250, 180);
        UMaterialExpressionScalarParameter* DropletsEnabled = CreateScalarParameter(
            Material, PreviewDropletsEnabledParameter, 1.0f, -1250, 280);
        UMaterialExpressionScalarParameter* RivuletsEnabled = CreateScalarParameter(
            Material, PreviewRivuletsEnabledParameter, 1.0f, -1250, 380);
        UMaterialExpressionScalarParameter* RivuletScrollSpeed = CreateScalarParameter(
            Material, PreviewRivuletScrollSpeedParameter, 0.0f, -1250, 480);

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                -1250,
                620));
        UMaterialExpressionTime* Time = Cast<UMaterialExpressionTime>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTime::StaticClass(),
                -1250,
                720));

        UTexture* DefaultNormalTexture = LoadDefaultNormalTexture();
        UMaterialExpressionTextureObjectParameter* DropletNormal = CreateNormalTextureParameter(
            Material,
            PreviewDropletNormalTextureParameter,
            DefaultNormalTexture,
            -1250,
            840);
        UMaterialExpressionTextureObjectParameter* RivuletNormal = CreateNormalTextureParameter(
            Material,
            PreviewRivuletNormalTextureParameter,
            DefaultNormalTexture,
            -1250,
            960);

        UMaterialExpressionCustom* BaseColorExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Base Color"),
            TEXT(R"(
float Absorbed = saturate(AbsorbedWater) * saturate(AbsorbedEnabled) * saturate(WetVisualStrength);
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Threshold = saturate(SurfaceVisibilityThreshold);
float Surface = saturate((EnabledSurface - Threshold) / max(1.0 - Threshold, 0.001));

float3 DryGray = float3(0.34, 0.35, 0.37);
float3 AbsorbedGray = DryGray * lerp(1.0, 0.45, Absorbed);
float3 SurfaceGray = AbsorbedGray * float3(0.78, 0.82, 0.88);
return lerp(AbsorbedGray, SurfaceGray, Surface * 0.55);
)"),
            CMOT_Float3,
            {
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("WetVisualStrength"),
                TEXT("SurfaceVisibilityThreshold"),
            },
            -620,
            -360);

        UMaterialExpressionCustom* RoughnessExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Roughness"),
            TEXT(R"(
float Absorbed = saturate(AbsorbedWater) * saturate(AbsorbedEnabled) * saturate(WetVisualStrength);
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Threshold = saturate(SurfaceVisibilityThreshold);
float Surface = saturate((EnabledSurface - Threshold) / max(1.0 - Threshold, 0.001));

float AbsorbedRoughness = lerp(0.72, 0.52, Absorbed);
float SurfaceBlend = saturate(Surface * SurfaceRoughnessStrength);
return saturate(lerp(AbsorbedRoughness, 0.06, SurfaceBlend));
)"),
            CMOT_Float1,
            {
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("WetVisualStrength"),
                TEXT("SurfaceVisibilityThreshold"),
                TEXT("SurfaceRoughnessStrength"),
            },
            -620,
            80);

        UMaterialExpressionCustom* NormalExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Surface Normal"),
            TEXT(R"(
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Threshold = saturate(SurfaceVisibilityThreshold);
float Surface = saturate((EnabledSurface - Threshold) / max(1.0 - Threshold, 0.001));

float2 DropletUV = frac(UV * 5.0);
float2 RivuletUV = frac(UV * float2(2.5, 1.5) + float2(0.0, TimeValue * RivuletScrollSpeed * 0.08));
float2 DropletXY = Texture2DSampleLevel(DropletNormalTex, DropletNormalTexSampler, DropletUV, 0).rg * 2.0 - 1.0;
float2 RivuletXY = Texture2DSampleLevel(RivuletNormalTex, RivuletNormalTexSampler, RivuletUV, 0).rg * 2.0 - 1.0;

float2 CombinedXY = DropletXY * saturate(DropletsEnabled) * 0.65;
CombinedXY += RivuletXY * saturate(RivuletsEnabled) * 0.65;
CombinedXY *= Surface * clamp(SurfaceNormalStrength, 0.0, 8.0);
return normalize(float3(CombinedXY, 1.0));
)"),
            CMOT_Float3,
            {
                TEXT("UV"),
                TEXT("TimeValue"),
                TEXT("SurfaceWater"),
                TEXT("SurfaceEnabled"),
                TEXT("SurfaceNormalStrength"),
                TEXT("SurfaceVisibilityThreshold"),
                TEXT("DropletsEnabled"),
                TEXT("RivuletsEnabled"),
                TEXT("RivuletScrollSpeed"),
                TEXT("DropletNormalTex"),
                TEXT("RivuletNormalTex"),
            },
            -620,
            520);

        bool bConnected = true;
        bConnected &= ConnectExpression(AbsorbedWater, BaseColorExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, BaseColorExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, BaseColorExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, BaseColorExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(WetVisualStrength, BaseColorExpression, TEXT("WetVisualStrength"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, BaseColorExpression, TEXT("SurfaceVisibilityThreshold"));

        bConnected &= ConnectExpression(AbsorbedWater, RoughnessExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, RoughnessExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, RoughnessExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, RoughnessExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(WetVisualStrength, RoughnessExpression, TEXT("WetVisualStrength"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, RoughnessExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectExpression(SurfaceRoughnessStrength, RoughnessExpression, TEXT("SurfaceRoughnessStrength"));

        bConnected &= ConnectExpression(TextureCoordinate, NormalExpression, TEXT("UV"));
        bConnected &= ConnectExpression(Time, NormalExpression, TEXT("TimeValue"));
        bConnected &= ConnectExpression(SurfaceWater, NormalExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(SurfaceEnabled, NormalExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(SurfaceNormalStrength, NormalExpression, TEXT("SurfaceNormalStrength"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, NormalExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectExpression(DropletsEnabled, NormalExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(RivuletsEnabled, NormalExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectExpression(RivuletScrollSpeed, NormalExpression, TEXT("RivuletScrollSpeed"));
        bConnected &= ConnectExpression(DropletNormal, NormalExpression, TEXT("DropletNormalTex"));
        bConnected &= ConnectExpression(RivuletNormal, NormalExpression, TEXT("RivuletNormalTex"));

        bConnected &= BaseColorExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpression, FString(), MP_BaseColor);
        bConnected &= RoughnessExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, FString(), MP_Roughness);
        bConnected &= NormalExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(NormalExpression, FString(), MP_Normal);

        if (!bConnected)
        {
            UE_LOG(LogTemp, Warning, TEXT("Failed to connect one or more Wetness Profile preview material expressions."));
        }

        FinalizeTransientPreviewMaterial(Material);
        return Material;
    }
}

void SWetnessProfileViewport::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
    PreviewScene->SetFloorVisibility(true, true);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    InitializePreviewComponents();
    RefreshPreviewScene();
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
}

void SWetnessProfileViewport::RefreshPreviewScene()
{
    if (!PreviewScene.IsValid() || PreviewMeshComponent == nullptr)
    {
        return;
    }

    PreviewScene->SetFloorVisibility(true, true);

    if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
    {
        PreviewMeshComponent->SetStaticMesh(SphereMesh);
        PreviewMeshComponent->SetRelativeScale3D(FVector(PreviewSphereScale));
    }

    if (PreviewMaterialInstance == nullptr)
    {
        if (UMaterial* BaseMaterial = ResolvePreviewBaseMaterial())
        {
            PreviewMaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
        }
    }

    if (PreviewMaterialInstance != nullptr)
    {
        PreviewMeshComponent->SetMaterial(0, PreviewMaterialInstance);
    }

    RefreshPreviewMaterialParameters();

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        if (!bPreviewCameraInitialized)
        {
            ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
            bPreviewCameraInitialized = true;
        }
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
    PreviewScene->AddComponent(PreviewMeshComponent, PreviewLiftTransform());
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

    PreviewMaterialInstance->SetScalarParameterValue(PreviewAbsorbedWaterParameter, PreviewAbsorbedWater);
    PreviewMaterialInstance->SetScalarParameterValue(PreviewSurfaceWaterParameter, PreviewSurfaceWater);
    PreviewMaterialInstance->SetScalarParameterValue(PreviewAbsorbedEnabledParameter, Absorbed.bEnabled ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(PreviewSurfaceEnabledParameter, Surface.bEnabled ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewWetVisualStrengthParameter,
        FMath::Clamp(Parameters.GetWetVisualStrength(), 0.0f, 1.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewSurfaceNormalStrengthParameter,
        FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 8.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewSurfaceRoughnessStrengthParameter,
        FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewSurfaceVisibilityThresholdParameter,
        FMath::Clamp(Surface.SurfaceVisibilityThreshold, 0.0f, 1.0f));
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewDropletsEnabledParameter,
        Surface.bEnableDroplets ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewRivuletsEnabledParameter,
        Surface.bEnableRivulets ? 1.0f : 0.0f);
    PreviewMaterialInstance->SetScalarParameterValue(
        PreviewRivuletScrollSpeedParameter,
        FMath::Clamp(Surface.RivuletUVScrollSpeed, 0.0f, 10.0f));

    UTexture* DefaultNormalTexture = LoadDefaultNormalTexture();
    PreviewMaterialInstance->SetTextureParameterValue(
        PreviewDropletNormalTextureParameter,
        Surface.DropletNormalTexture != nullptr ? Surface.DropletNormalTexture.Get() : DefaultNormalTexture);
    PreviewMaterialInstance->SetTextureParameterValue(
        PreviewRivuletNormalTextureParameter,
        Surface.RivuletNormalTexture != nullptr ? Surface.RivuletNormalTexture.Get() : DefaultNormalTexture);

    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

UMaterial* SWetnessProfileViewport::ResolvePreviewBaseMaterial()
{
    if (PreviewBaseMaterial == nullptr)
    {
        PreviewBaseMaterial = CreateWetnessPreviewMaterial();
    }
    return PreviewBaseMaterial;
}

FText SWetnessProfileViewport::GetOverlayText() const
{
    const FWetnessProfileParameters Parameters = GetSanitizedProfileParameters(WetnessProfile.Get());
    const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;

    return FText::Format(
        LOCTEXT(
            "PreviewHint",
            "Wetness Profile Preview\nAbsorbed Water {0}%  |  Surface Water {1}%\nWet Appearance {2}%  |  Surface Normal {3}x  |  Surface Roughness {4}%"),
        FText::AsNumber(FMath::RoundToInt(PreviewAbsorbedWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(PreviewSurfaceWater * 100.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Parameters.GetWetVisualStrength(), 0.0f, 1.0f) * 100.0f)),
        FText::AsNumber(FMath::Clamp(Surface.SurfaceWaterNormalStrength, 0.0f, 8.0f)),
        FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Surface.SurfaceWaterRoughnessStrength, 0.0f, 1.0f) * 100.0f)));
}

#undef LOCTEXT_NAMESPACE
