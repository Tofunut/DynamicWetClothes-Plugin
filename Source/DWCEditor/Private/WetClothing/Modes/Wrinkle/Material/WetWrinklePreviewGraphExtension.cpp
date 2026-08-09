//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewGraphExtension.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewMaterialParameters.h"

namespace
{
    constexpr const TCHAR* WrinkleSessionPreviewBlendDescription = TEXT("DWC Wrinkle Preview Normal Blend");

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

    bool Connect(
        const FDWCMaterialGraphPin& From,
        UMaterialExpression* To,
        const TCHAR* ToInput,
        FString& OutErrorMessage)
    {
        if (!From.IsValid() || To == nullptr ||
            !UMaterialEditingLibrary::ConnectMaterialExpressions(
                From.Expression,
                From.OutputName,
                To,
                ToInput))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Failed to connect the wrinkle preview graph input '%s'."),
                ToInput);
            return false;
        }
        return true;
    }

    UMaterialExpressionScalarParameter* CreateScalarParameter(
        UMaterial* Material,
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

    UMaterialExpressionTextureObjectParameter* CreateNormalParameter(
        UMaterial* Material,
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
            Parameter->SamplerType = SAMPLERTYPE_Normal;
            Parameter->Texture = LoadDefaultNormalTexture();
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

    UMaterialExpressionCustom* CreatePreviewBlend(UMaterial* Material)
    {
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionCustom::StaticClass(),
                -100,
                1300));
        if (Custom == nullptr)
        {
            return nullptr;
        }

        static const FName InputNames[] = {
            TEXT("BaseNormal"),
            TEXT("SelectedUV"),
            TEXT("PreviewWetness"),
            TEXT("AccumulatedEnabled"),
            TEXT("AccumulatedStrength"),
            TEXT("AccumulatedNormalTex"),
            TEXT("TransientRidgeEnabled"),
            TEXT("TransientRidgeNormalTex"),
            TEXT("HoverEnabled"),
            TEXT("HoverNormalTex"),
        };

        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }

        Custom->Code = TEXT(R"(
float3 BaseTS = normalize(BaseNormal);
float PreviewWetnessScale = saturate(PreviewWetness);

float3 CombinedTS = BaseTS;
if (AccumulatedEnabled > 0.5)
{
    float2 AccumulatedXY = Texture2DSampleLevel(AccumulatedNormalTex, AccumulatedNormalTexSampler, frac(SelectedUV), 0).rg * 2.0 - 1.0;
    float AccumulatedWeight = max(AccumulatedStrength, 0.0) * PreviewWetnessScale;
    float3 AccumulatedTS = normalize(float3(AccumulatedXY * AccumulatedWeight, 1.0));
    CombinedTS = normalize(float3(CombinedTS.xy + AccumulatedTS.xy, CombinedTS.z * AccumulatedTS.z));
}

if (TransientRidgeEnabled > 0.5)
{
    float2 TransientRidgeXY = Texture2DSampleLevel(TransientRidgeNormalTex, TransientRidgeNormalTexSampler, frac(SelectedUV), 0).rg * 2.0 - 1.0;
    float3 TransientRidgeTS = normalize(float3(TransientRidgeXY * PreviewWetnessScale, 1.0));
    CombinedTS = normalize(float3(CombinedTS.xy + TransientRidgeTS.xy, CombinedTS.z * TransientRidgeTS.z));
}

if (HoverEnabled > 0.5)
{
    float2 HoverXY = Texture2DSampleLevel(HoverNormalTex, HoverNormalTexSampler, frac(SelectedUV), 0).rg * 2.0 - 1.0;
    float3 HoverTS = normalize(float3(HoverXY * PreviewWetnessScale, 1.0));
    CombinedTS = normalize(float3(CombinedTS.xy + HoverTS.xy, CombinedTS.z * HoverTS.z));
}

return CombinedTS;
)");
        Custom->OutputType = CMOT_Float3;
        Custom->Description = WrinkleSessionPreviewBlendDescription;
        Custom->RebuildOutputs();
        return Custom;
    }
}

bool FWetWrinklePreviewGraphExtension::ExtendGraph(
    UMaterial* Material,
    const FDWCSurfaceGraphBuildResult& SurfaceGraph,
    FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (Material == nullptr || !SurfaceGraph.Outputs.Normal.IsValid() ||
        SurfaceGraph.DWCDataUVExpression == nullptr)
    {
        OutErrorMessage = TEXT("The common DWC surface graph is missing the normal or Data UV output required by the Wrinkle preview.");
        return false;
    }

    UMaterialExpressionScalarParameter* PreviewWetness = FindPreviewWetnessParameter(Material);
    UMaterialExpressionCustom* Blend = CreatePreviewBlend(Material);
    UMaterialExpressionTextureObjectParameter* AccumulatedNormal = CreateNormalParameter(
        Material, WetWrinklePreviewMaterialParameters::AccumulatedNormal, 1350);
    UMaterialExpressionScalarParameter* AccumulatedEnabled = CreateScalarParameter(
        Material, WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f, 1450);
    UMaterialExpressionScalarParameter* AccumulatedStrength = CreateScalarParameter(
        Material, WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f, 1550);
    UMaterialExpressionTextureObjectParameter* TransientRidgeNormal = CreateNormalParameter(
        Material, WetWrinklePreviewMaterialParameters::TransientRidgeNormal, 1650);
    UMaterialExpressionScalarParameter* TransientRidgeEnabled = CreateScalarParameter(
        Material, WetWrinklePreviewMaterialParameters::TransientRidgeEnabled, 0.0f, 1750);
    UMaterialExpressionTextureObjectParameter* HoverNormal = CreateNormalParameter(
        Material, WetWrinklePreviewMaterialParameters::HoverNormal, 1850);
    UMaterialExpressionScalarParameter* HoverEnabled = CreateScalarParameter(
        Material, WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f, 1950);

    if (PreviewWetness == nullptr || Blend == nullptr || AccumulatedNormal == nullptr ||
        AccumulatedEnabled == nullptr || AccumulatedStrength == nullptr ||
        TransientRidgeNormal == nullptr || TransientRidgeEnabled == nullptr ||
        HoverNormal == nullptr || HoverEnabled == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create one or more Wrinkle preview material expressions.");
        return false;
    }

    FDWCMaterialGraphPin DataUVPin;
    DataUVPin.Expression = SurfaceGraph.DWCDataUVExpression;
    bool bConnected = Connect(SurfaceGraph.Outputs.Normal, Blend, TEXT("BaseNormal"), OutErrorMessage);
    bConnected &= Connect(DataUVPin, Blend, TEXT("SelectedUV"), OutErrorMessage);
    bConnected &= Connect({ PreviewWetness, FString() }, Blend, TEXT("PreviewWetness"), OutErrorMessage);
    bConnected &= Connect({ AccumulatedEnabled, FString() }, Blend, TEXT("AccumulatedEnabled"), OutErrorMessage);
    bConnected &= Connect({ AccumulatedStrength, FString() }, Blend, TEXT("AccumulatedStrength"), OutErrorMessage);
    bConnected &= Connect({ AccumulatedNormal, FString() }, Blend, TEXT("AccumulatedNormalTex"), OutErrorMessage);
    bConnected &= Connect({ TransientRidgeEnabled, FString() }, Blend, TEXT("TransientRidgeEnabled"), OutErrorMessage);
    bConnected &= Connect({ TransientRidgeNormal, FString() }, Blend, TEXT("TransientRidgeNormalTex"), OutErrorMessage);
    bConnected &= Connect({ HoverEnabled, FString() }, Blend, TEXT("HoverEnabled"), OutErrorMessage);
    bConnected &= Connect({ HoverNormal, FString() }, Blend, TEXT("HoverNormalTex"), OutErrorMessage);
    if (!bConnected || !UMaterialEditingLibrary::ConnectMaterialProperty(Blend, FString(), MP_Normal))
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = TEXT("Failed to connect the Wrinkle preview normal output.");
        }
        return false;
    }
    return true;
}

void FWetWrinklePreviewGraphExtension::InitializeMID(
    const int32,
    UMaterialInstanceDynamic& PreviewMID)
{
    PreviewMID.SetTextureParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedNormal, nullptr);
    PreviewMID.SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f);
    PreviewMID.SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f);
    PreviewMID.SetTextureParameterValue(WetWrinklePreviewMaterialParameters::TransientRidgeNormal, nullptr);
    PreviewMID.SetScalarParameterValue(WetWrinklePreviewMaterialParameters::TransientRidgeEnabled, 0.0f);
    PreviewMID.SetTextureParameterValue(WetWrinklePreviewMaterialParameters::HoverNormal, nullptr);
    PreviewMID.SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f);
}
