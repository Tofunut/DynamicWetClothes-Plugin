// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetClothing/Foundation/MaterialGraph/DWCRevealSurfaceMaterialGraph.h"
#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"

namespace
{
    UTexture* LoadDefaultBlackTexture()
    {
        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
    }

    bool Connect(
        const FDWCMaterialGraphPin& From,
        UMaterialExpression*        To,
        const TCHAR*                ToInput,
        FString&                    OutErrorMessage)
    {
        if (!From.IsValid() || To == nullptr ||
            !UMaterialEditingLibrary::ConnectMaterialExpressions(
                From.Expression,
                From.OutputName,
                To,
                ToInput))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Failed to connect the transparency preview graph input '%s'."),
                ToInput);
            return false;
        }
        return true;
    }

    UMaterialExpressionScalarParameter* CreateScalarParameter(
        UMaterial*  Material,
        const FName ParameterName,
        const float DefaultValue,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter =
            Cast<UMaterialExpressionScalarParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionScalarParameter::StaticClass(),
                    -650,
                    NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* CreateColorTextureParameter(
        UMaterial*  Material,
        const FName ParameterName,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter =
            Cast<UMaterialExpressionTextureObjectParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionTextureObjectParameter::StaticClass(),
                    -650,
                    NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadDefaultBlackTexture();
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* CreateMaskTextureParameter(
        UMaterial*  Material,
        const FName ParameterName,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter =
            Cast<UMaterialExpressionTextureObjectParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionTextureObjectParameter::StaticClass(),
                    -650,
                    NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Masks;
            Parameter->Texture = LoadDefaultBlackTexture();
        }
        return Parameter;
    }

    UMaterialExpressionVectorParameter* CreateVectorParameter(
        UMaterial*          Material,
        const FName         ParameterName,
        const FLinearColor& DefaultValue,
        const int32         NodeY)
    {
        UMaterialExpressionVectorParameter* Parameter =
            Cast<UMaterialExpressionVectorParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionVectorParameter::StaticClass(),
                    -650,
                    NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionScalarParameter* FindPreviewWetnessParameter(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionScalarParameter* Parameter =
                Cast<UMaterialExpressionScalarParameter>(Expression);
            if (Parameter != nullptr &&
                Parameter->ParameterName == DWCEditorPreviewMaterialParameters::PreviewWetness())
            {
                return Parameter;
            }
        }
        return nullptr;
    }
} // namespace

bool FWetTransparencyPreviewGraphExtension::ExtendGraph(
    UMaterial*                         Material,
    const FDWCSurfaceGraphBuildResult& SurfaceGraph,
    FString&                           OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (Material == nullptr || !SurfaceGraph.Outputs.BaseColor.IsValid() ||
        !SurfaceGraph.Outputs.Normal.IsValid() ||
        SurfaceGraph.DWCDataUVExpression == nullptr)
    {
        OutErrorMessage = TEXT("The common DWC surface graph is missing the Base Color, Normal, or Data UV output required by the Transparency preview.");
        return false;
    }

    UMaterialExpressionCustom* State = Cast<UMaterialExpressionCustom>(
        UMaterialEditingLibrary::CreateMaterialExpression(
            Material,
            UMaterialExpressionCustom::StaticClass(),
            -100,
            2900));
    UMaterialExpressionLinearInterpolate* ColorCompose =
        Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionLinearInterpolate::StaticClass(),
                250,
                2900));
    UMaterialExpressionTextureObjectParameter* TransparencyMap = CreateColorTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::TransparencyMap(), 2900);
    UMaterialExpressionScalarParameter* UseTransparencyMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseTransparencyMap(), 0.0f, 3000);
    UMaterialExpressionScalarParameter* TransparencyStrength = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::TransparencyStrength(), 1.0f, 3100);
    UMaterialExpressionScalarParameter* ShowInnerColor = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::ShowInnerColor(), 0.0f, 3200);
    UMaterialExpressionTextureObjectParameter* WrinkleCoverageMap = CreateMaskTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleCoverageMap(), 3300);
    UMaterialExpressionScalarParameter* UseWrinkleCoverageMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseWrinkleCoverageMap(), 0.0f, 3400);
    UMaterialExpressionScalarParameter* WrinkleSuppressionStrength = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(), 0.0f, 3500);
    UMaterialExpressionScalarParameter* WrinkleMaskThreshold = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleMaskThreshold(), 0.15f, 3600);
    UMaterialExpressionScalarParameter* WrinkleMaskSoftness = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleMaskSoftness(), 0.05f, 3700);
    UMaterialExpressionScalarParameter* VisualizationMode = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::VisualizationMode(), 0.0f, 3800);
    UMaterialExpressionVectorParameter* HoverState0 = CreateVectorParameter(
        Material,
        DWCTransparencyPreviewMaterialParameters::HoverState0(),
        FLinearColor(0.0f, 0.0f, 0.025f, 0.5f),
        3900);
    UMaterialExpressionVectorParameter* HoverState1 = CreateVectorParameter(
        Material,
        DWCTransparencyPreviewMaterialParameters::HoverState1(),
        FLinearColor::Black,
        4000);
    UMaterialExpressionVectorParameter* HoverColor = CreateVectorParameter(
        Material,
        DWCTransparencyPreviewMaterialParameters::HoverColor(),
        FLinearColor::Black,
        4100);
    UMaterialExpressionScalarParameter* HoverTarget = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverTarget(), 0.0f, 4200);
    UMaterialExpressionScalarParameter* HoverWrap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverWrap(), 0.0f, 4300);
    UMaterialExpressionVectorParameter* HoverTexelSize = CreateVectorParameter(
        Material,
        DWCTransparencyPreviewMaterialParameters::HoverTexelSize(),
        FLinearColor::Black,
        4400);
    UMaterialExpressionScalarParameter* HoverVisualizationMode = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverVisualizationMode(), 0.0f, 4450);
    UMaterialExpressionTextureObjectParameter* HoverBaselineMap = CreateColorTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverBaselineMap(), 4500);
    UMaterialExpressionScalarParameter* UseHoverBaselineMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseHoverBaselineMap(), 0.0f, 4600);
    UMaterialExpressionTextureObjectParameter* HoverIslandIDMap = CreateMaskTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverIslandIDMap(), 4700);
    UMaterialExpressionScalarParameter* UseHoverIslandIDMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseHoverIslandIDMap(), 0.0f, 4800);
    UMaterialExpressionScalarParameter* HoverIslandID = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverIslandID(), -1.0f, 4900);
    UMaterialExpressionTextureObjectParameter* HoverEdgeFeatherMap = CreateMaskTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap(), 5000);
    UMaterialExpressionScalarParameter* UseHoverEdgeFeatherMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap(), 0.0f, 5100);
    UMaterialExpressionScalarParameter* PreviewWetness = FindPreviewWetnessParameter(Material);
    if (State == nullptr || ColorCompose == nullptr || TransparencyMap == nullptr || UseTransparencyMap == nullptr ||
        TransparencyStrength == nullptr || ShowInnerColor == nullptr || WrinkleCoverageMap == nullptr ||
        UseWrinkleCoverageMap == nullptr || WrinkleSuppressionStrength == nullptr ||
        WrinkleMaskThreshold == nullptr || WrinkleMaskSoftness == nullptr || VisualizationMode == nullptr ||
        HoverState0 == nullptr || HoverState1 == nullptr || HoverColor == nullptr ||
        HoverTarget == nullptr || HoverWrap == nullptr || HoverTexelSize == nullptr ||
        HoverVisualizationMode == nullptr ||
        HoverBaselineMap == nullptr || UseHoverBaselineMap == nullptr ||
        HoverIslandIDMap == nullptr || UseHoverIslandIDMap == nullptr || HoverIslandID == nullptr ||
        HoverEdgeFeatherMap == nullptr || UseHoverEdgeFeatherMap == nullptr ||
        PreviewWetness == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create one or more Transparency preview expressions.");
        return false;
    }

    FCustomOutput& BlendWeight = State->AdditionalOutputs.AddDefaulted_GetRef();
    BlendWeight.OutputName = TEXT("BaseColorBlendWeight");
    BlendWeight.OutputType = CMOT_Float1;
    FCustomOutput& FinalRevealVisibility = State->AdditionalOutputs.AddDefaulted_GetRef();
    FinalRevealVisibility.OutputName = TEXT("FinalRevealVisibility");
    FinalRevealVisibility.OutputType = CMOT_Float1;

    static const FName InputNames[] = {
        TEXT("TransparencyMapTex"),
        TEXT("SelectedUV"),
        TEXT("UseTransparencyMap"),
        TEXT("PreviewWetness"),
        TEXT("TransparencyStrength"),
        TEXT("ShowInnerColor"),
        TEXT("WrinkleCoverageMapTex"),
        TEXT("UseWrinkleCoverageMap"),
        TEXT("WrinkleSuppressionStrength"),
        TEXT("WrinkleMaskThreshold"),
        TEXT("WrinkleMaskSoftness"),
        TEXT("VisualizationMode"),
        TEXT("HoverState0"),
        TEXT("HoverState1"),
        TEXT("HoverFalloff"),
        TEXT("HoverOperation"),
        TEXT("HoverColor"),
        TEXT("HoverTarget"),
        TEXT("HoverWrap"),
        TEXT("HoverTexelSize"),
        TEXT("HoverVisualizationMode"),
        TEXT("HoverBaselineMapTex"),
        TEXT("UseHoverBaselineMap"),
        TEXT("HoverIslandIDMapTex"),
        TEXT("UseHoverIslandIDMap"),
        TEXT("HoverIslandID"),
        TEXT("HoverEdgeFeatherMapTex"),
        TEXT("UseHoverEdgeFeatherMap"),
    };
    for (const FName InputName : InputNames)
    {
        FCustomInput& Input = State->Inputs.AddDefaulted_GetRef();
        Input.InputName = InputName;
    }
    State->Code = TEXT(R"(
float4 TransparencySample = Texture2DSampleLevel(TransparencyMapTex, TransparencyMapTexSampler, SelectedUV, 0);

// Hover is a presentation-only layer. Disabled hover returns the committed
// sample unchanged and does not require either optional baseline texture.
if (HoverState1.x > 0.0 && HoverTarget > 0.5)
{
    float2 HoverDelta = SelectedUV - HoverState0.xy;
    if (HoverWrap > 0.5)
    {
        HoverDelta -= round(HoverDelta);
    }

    float HoverRadius = max(HoverState0.z, 0.00001);
    float HoverDistance = length(HoverDelta) / HoverRadius;
    if (HoverDistance <= 1.0)
    {
        float ClampedHoverFalloff = saturate(HoverFalloff);
        float HoverInnerRadius = 1.0 - ClampedHoverFalloff;
        float HoverRadialWeight = ClampedHoverFalloff <= 0.00001 || HoverDistance <= HoverInnerRadius
            ? 1.0
            : 1.0 - smoothstep(HoverInnerRadius, 1.0, HoverDistance);
        float2 SafeTexelSize = max(HoverTexelSize.xy, float2(0.000001, 0.000001));
        float2 HoverTextureSize = max(round(1.0 / SafeTexelSize), float2(1.0, 1.0));
        float2 HoverAddressedUV = HoverWrap > 0.5 ? frac(SelectedUV) : saturate(SelectedUV);
        int2 HoverPixelCoord = clamp(
            (int2)floor(HoverAddressedUV * HoverTextureSize),
            int2(0, 0),
            (int2)HoverTextureSize - int2(1, 1));
        float SampledHoverIslandID = HoverIslandIDMapTex.Load(int3(HoverPixelCoord, 0)).r;
        float HoverIslandEligibility = UseHoverIslandIDMap > 0.5
            ? 1.0 - step(0.5 / 65535.0, abs(SampledHoverIslandID - HoverIslandID))
            : 1.0;
        float HoverIslandFeather = UseHoverEdgeFeatherMap > 0.5
            ? Texture2DSampleLevel(
                HoverEdgeFeatherMapTex,
                HoverEdgeFeatherMapTexSampler,
                SelectedUV,
                0).r
            : 1.0;
        HoverIslandEligibility *= step(0.5 / 255.0, HoverIslandFeather);
        float HoverWeight = saturate(HoverRadialWeight * max(HoverState1.y, 0.0)) * HoverIslandEligibility;
        int SelectedHoverOperation = (int)floor(HoverOperation + 0.5);

        float4 SmoothSample = 0.0;
        float SmoothAlphaWeight = 0.0;
        if (SelectedHoverOperation == 3)
        {
            [unroll]
            for (int HoverY = -1; HoverY <= 1; ++HoverY)
            {
                [unroll]
                for (int HoverX = -1; HoverX <= 1; ++HoverX)
                {
                    float2 SampleUV = SelectedUV + float2(HoverX, HoverY) * SafeTexelSize;
                    SampleUV = HoverWrap > 0.5 ? frac(SampleUV) : saturate(SampleUV);
                    float4 NeighborSample = Texture2DSampleLevel(
                        TransparencyMapTex,
                        TransparencyMapTexSampler,
                        SampleUV,
                        0);
                    float2 NeighborAddressedUV = HoverWrap > 0.5 ? frac(SampleUV) : saturate(SampleUV);
                    int2 NeighborPixelCoord = clamp(
                        (int2)floor(NeighborAddressedUV * HoverTextureSize),
                        int2(0, 0),
                        (int2)HoverTextureSize - int2(1, 1));
                    float NeighborIslandID = HoverIslandIDMapTex.Load(int3(NeighborPixelCoord, 0)).r;
                    float NeighborEligibility = UseHoverIslandIDMap > 0.5
                        ? 1.0 - step(0.5 / 65535.0, abs(NeighborIslandID - HoverIslandID))
                        : 1.0;
                    NeighborEligibility *= UseHoverEdgeFeatherMap > 0.5
                        ? step(
                            0.5 / 255.0,
                            Texture2DSampleLevel(
                                HoverEdgeFeatherMapTex,
                                HoverEdgeFeatherMapTexSampler,
                                SampleUV,
                                0).r)
                        : 1.0;
                    SmoothSample.rgb += lerp(TransparencySample.rgb, NeighborSample.rgb, NeighborEligibility);
                    SmoothSample.a += NeighborSample.a * NeighborEligibility;
                    SmoothAlphaWeight += NeighborEligibility;
                }
            }
            SmoothSample.rgb /= 9.0;
            SmoothSample.a = SmoothAlphaWeight > 0.0
                ? SmoothSample.a / SmoothAlphaWeight
                : TransparencySample.a;
        }

        float4 BaselineSample = TransparencySample;
        if (UseHoverBaselineMap > 0.5 && (SelectedHoverOperation == 1 || SelectedHoverOperation == 2))
        {
            BaselineSample = Texture2DSampleLevel(
                HoverBaselineMapTex,
                HoverBaselineMapTexSampler,
                SelectedUV,
                0);
        }

        if (HoverTarget < 1.5)
        {
            float3 HoverTargetColor = HoverColor.rgb;
            if (SelectedHoverOperation == 1 || SelectedHoverOperation == 2)
            {
                HoverTargetColor = BaselineSample.rgb;
            }
            else if (SelectedHoverOperation == 3)
            {
                HoverTargetColor = SmoothSample.rgb;
            }
            TransparencySample.rgb = lerp(TransparencySample.rgb, HoverTargetColor, HoverWeight);
        }
        else
        {
            float HoverTargetAlpha = saturate(HoverState1.z);
            if (SelectedHoverOperation == 1)
            {
                HoverTargetAlpha = 0.0;
            }
            else if (SelectedHoverOperation == 2)
            {
                HoverTargetAlpha = BaselineSample.a;
            }
            else if (SelectedHoverOperation == 3)
            {
                HoverTargetAlpha = SmoothSample.a;
            }

            if (UseHoverEdgeFeatherMap > 0.5 && SelectedHoverOperation != 3)
            {
                HoverTargetAlpha *= HoverIslandFeather;
            }
            TransparencySample.a = lerp(TransparencySample.a, HoverTargetAlpha, HoverWeight);
            if (HoverVisualizationMode > 1.5 && HoverVisualizationMode < 2.5)
            {
                TransparencySample.rgb = TransparencySample.aaa;
            }
        }
    }
}

float Coverage = Texture2DSampleLevel(
    WrinkleCoverageMapTex,
    WrinkleCoverageMapTexSampler,
    SelectedUV,
    0).r * saturate(UseWrinkleCoverageMap);
float SafeThreshold = saturate(WrinkleMaskThreshold);
float SafeSoftness = saturate(WrinkleMaskSoftness);
float TransitionEnd = min(SafeThreshold + SafeSoftness, 1.0);
float ThresholdGate = SafeSoftness <= 0.00001 || TransitionEnd <= SafeThreshold + 0.00001
    ? step(SafeThreshold, Coverage)
    : smoothstep(SafeThreshold, TransitionEnd, Coverage);
float Suppression = saturate(Coverage * ThresholdGate);
float SuppressionWeight = saturate(Suppression * max(WrinkleSuppressionStrength, 0.0));
float FinalAlpha = saturate(
    saturate(TransparencySample.a) * max(TransparencyStrength, 0.0) * (1.0 - SuppressionWeight));
int SelectedVisualizationMode = (int)floor(VisualizationMode + 0.5);

float3 DisplayColor = TransparencySample.rgb;
float MapBlendWeight = FinalAlpha;
if (SelectedVisualizationMode == 1)
{
    MapBlendWeight = 1.0;
}
else if (SelectedVisualizationMode == 2)
{
    DisplayColor = float3(FinalAlpha, FinalAlpha, FinalAlpha);
    // A grayscale alpha view must be fully visible. Blending it back by the
    // same alpha made low-alpha regions look identical to the base material.
    MapBlendWeight = 1.0;
}
else if (SelectedVisualizationMode == 3)
{
    DisplayColor = float3(Suppression, Suppression, Suppression);
    MapBlendWeight = 1.0;
}
else if (SelectedVisualizationMode >= 4)
{
    MapBlendWeight = saturate(TransparencySample.a);
}

float InnerColorBlendWeight = saturate(ShowInnerColor);
BaseColorBlendWeight = max(MapBlendWeight, InnerColorBlendWeight) *
    saturate(UseTransparencyMap) * saturate(PreviewWetness);
FinalRevealVisibility = FinalAlpha * saturate(UseTransparencyMap) * saturate(PreviewWetness);
return DisplayColor;
)");
    State->OutputType = CMOT_Float3;
    State->Description = TEXT("DWC Transparency Live Preview State");
    State->RebuildOutputs();

    FDWCMaterialGraphPin DataUVPin;
    DataUVPin.Expression = SurfaceGraph.DWCDataUVExpression;
    bool bConnected = Connect({ TransparencyMap, FString() }, State, TEXT("TransparencyMapTex"), OutErrorMessage);
    bConnected &= Connect(DataUVPin, State, TEXT("SelectedUV"), OutErrorMessage);
    bConnected &= Connect({ UseTransparencyMap, FString() }, State, TEXT("UseTransparencyMap"), OutErrorMessage);
    bConnected &= Connect({ PreviewWetness, FString() }, State, TEXT("PreviewWetness"), OutErrorMessage);
    bConnected &= Connect({ TransparencyStrength, FString() }, State, TEXT("TransparencyStrength"), OutErrorMessage);
    bConnected &= Connect({ ShowInnerColor, FString() }, State, TEXT("ShowInnerColor"), OutErrorMessage);
    bConnected &= Connect({ WrinkleCoverageMap, FString() }, State, TEXT("WrinkleCoverageMapTex"), OutErrorMessage);
    bConnected &= Connect({ UseWrinkleCoverageMap, FString() }, State, TEXT("UseWrinkleCoverageMap"), OutErrorMessage);
    bConnected &= Connect({ WrinkleSuppressionStrength, FString() }, State, TEXT("WrinkleSuppressionStrength"), OutErrorMessage);
    bConnected &= Connect({ WrinkleMaskThreshold, FString() }, State, TEXT("WrinkleMaskThreshold"), OutErrorMessage);
    bConnected &= Connect({ WrinkleMaskSoftness, FString() }, State, TEXT("WrinkleMaskSoftness"), OutErrorMessage);
    bConnected &= Connect({ VisualizationMode, FString() }, State, TEXT("VisualizationMode"), OutErrorMessage);
    bConnected &= Connect({ HoverState0, FString() }, State, TEXT("HoverState0"), OutErrorMessage);
    bConnected &= Connect({ HoverState1, FString() }, State, TEXT("HoverState1"), OutErrorMessage);
    bConnected &= Connect({ HoverState0, TEXT("A") }, State, TEXT("HoverFalloff"), OutErrorMessage);
    bConnected &= Connect({ HoverState1, TEXT("A") }, State, TEXT("HoverOperation"), OutErrorMessage);
    bConnected &= Connect({ HoverColor, FString() }, State, TEXT("HoverColor"), OutErrorMessage);
    bConnected &= Connect({ HoverTarget, FString() }, State, TEXT("HoverTarget"), OutErrorMessage);
    bConnected &= Connect({ HoverWrap, FString() }, State, TEXT("HoverWrap"), OutErrorMessage);
    bConnected &= Connect({ HoverTexelSize, FString() }, State, TEXT("HoverTexelSize"), OutErrorMessage);
    bConnected &= Connect({ HoverVisualizationMode, FString() }, State, TEXT("HoverVisualizationMode"), OutErrorMessage);
    bConnected &= Connect({ HoverBaselineMap, FString() }, State, TEXT("HoverBaselineMapTex"), OutErrorMessage);
    bConnected &= Connect({ UseHoverBaselineMap, FString() }, State, TEXT("UseHoverBaselineMap"), OutErrorMessage);
    bConnected &= Connect({ HoverIslandIDMap, FString() }, State, TEXT("HoverIslandIDMapTex"), OutErrorMessage);
    bConnected &= Connect({ UseHoverIslandIDMap, FString() }, State, TEXT("UseHoverIslandIDMap"), OutErrorMessage);
    bConnected &= Connect({ HoverIslandID, FString() }, State, TEXT("HoverIslandID"), OutErrorMessage);
    bConnected &= Connect({ HoverEdgeFeatherMap, FString() }, State, TEXT("HoverEdgeFeatherMapTex"), OutErrorMessage);
    bConnected &= Connect({ UseHoverEdgeFeatherMap, FString() }, State, TEXT("UseHoverEdgeFeatherMap"), OutErrorMessage);
    bConnected &= Connect(SurfaceGraph.Outputs.BaseColor, ColorCompose, TEXT("A"), OutErrorMessage);
    bConnected &= Connect({ State, FString() }, ColorCompose, TEXT("B"), OutErrorMessage);
    bConnected &= Connect({ State, TEXT("BaseColorBlendWeight") }, ColorCompose, TEXT("Alpha"), OutErrorMessage);
    FDWCRevealSurfaceMaterialGraphRequest RevealSurfaceRequest;
    RevealSurfaceRequest.Material = Material;
    RevealSurfaceRequest.BaseNormal = SurfaceGraph.Outputs.Normal;
    RevealSurfaceRequest.DataUV = DataUVPin;
    RevealSurfaceRequest.Visibility = { State, TEXT("FinalRevealVisibility") };
    RevealSurfaceRequest.VisualizationMode = { VisualizationMode, FString() };
    RevealSurfaceRequest.SurfaceTextureParameterName =
        DWCTransparencyPreviewMaterialParameters::RevealSurfaceMap();
    RevealSurfaceRequest.UseSurfaceParameterName =
        DWCTransparencyPreviewMaterialParameters::UseRevealSurfaceMap();
    RevealSurfaceRequest.StrengthParameterName =
        DWCTransparencyPreviewMaterialParameters::RevealNormalStrength();
    RevealSurfaceRequest.ShowParameterName =
        DWCTransparencyPreviewMaterialParameters::ShowRevealNormal();
    RevealSurfaceRequest.NodePosX = 520;
    RevealSurfaceRequest.NodePosY = 2950;
    RevealSurfaceRequest.Description = TEXT("DWC Transparency Preview Reveal Surface Normal");
    const FDWCRevealSurfaceMaterialGraphResult RevealSurfaceResult =
        FDWCRevealSurfaceMaterialGraph::BuildAuthoringPreview(RevealSurfaceRequest);
    if (!bConnected || !RevealSurfaceResult.bSucceeded ||
        !UMaterialEditingLibrary::ConnectMaterialProperty(
            ColorCompose,
            FString(),
            MP_BaseColor) ||
        !UMaterialEditingLibrary::ConnectMaterialProperty(
            RevealSurfaceResult.Normal.Expression,
            RevealSurfaceResult.Normal.OutputName,
            MP_Normal))
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = RevealSurfaceResult.FailureReason.IsEmpty()
                ? TEXT("Failed to connect the Transparency preview Reveal Surface outputs.")
                : RevealSurfaceResult.FailureReason;
        }
        return false;
    }
    return true;
}

void FWetTransparencyPreviewGraphExtension::InitializeMID(
    const int32,
    UMaterialInstanceDynamic& PreviewMID)
{
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::TransparencyMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseTransparencyMap(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::TransparencyStrength(), 1.0f);
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::RevealSurfaceMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseRevealSurfaceMap(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::RevealNormalStrength(), 1.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::ShowRevealNormal(), 1.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::ShowInnerColor(), 0.0f);
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleCoverageMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseWrinkleCoverageMap(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleMaskThreshold(), 0.15f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleMaskSoftness(), 0.05f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::VisualizationMode(), 0.0f);
    PreviewMID.SetVectorParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverState0(),
        FLinearColor(0.0f, 0.0f, 0.025f, 0.5f));
    PreviewMID.SetVectorParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverState1(),
        FLinearColor::Black);
    PreviewMID.SetVectorParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverColor(),
        FLinearColor::Black);
    PreviewMID.SetScalarParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverTarget(),
        static_cast<float>(EDWCTransparencyMaterialHoverTarget::None));
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::HoverWrap(), 0.0f);
    PreviewMID.SetVectorParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverTexelSize(),
        FLinearColor::Black);
    PreviewMID.SetScalarParameterValue(
        DWCTransparencyPreviewMaterialParameters::HoverVisualizationMode(),
        0.0f);
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::HoverBaselineMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseHoverBaselineMap(), 0.0f);
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::HoverIslandIDMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseHoverIslandIDMap(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::HoverIslandID(), -1.0f);
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap(), 0.0f);
}
