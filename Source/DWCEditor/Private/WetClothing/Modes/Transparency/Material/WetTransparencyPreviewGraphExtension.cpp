#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"

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
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"

namespace
{
    UTexture* LoadDefaultBlackTexture()
    {
        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
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
                TEXT("Failed to connect the transparency preview graph input '%s'."),
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

    UMaterialExpressionTextureObjectParameter* CreateColorTextureParameter(
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
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadDefaultBlackTexture();
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
}

bool FWetTransparencyPreviewGraphExtension::ExtendGraph(
    UMaterial* Material,
    const FDWCSurfaceGraphBuildResult& SurfaceGraph,
    FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    if (Material == nullptr || !SurfaceGraph.Outputs.BaseColor.IsValid() ||
        SurfaceGraph.DWCDataUVExpression == nullptr)
    {
        OutErrorMessage = TEXT("The common DWC surface graph is missing the Base Color or Data UV output required by the Transparency preview.");
        return false;
    }

    UMaterialExpressionCustom* Blend = Cast<UMaterialExpressionCustom>(
        UMaterialEditingLibrary::CreateMaterialExpression(
            Material,
            UMaterialExpressionCustom::StaticClass(),
            -100,
            2900));
    UMaterialExpressionTextureObjectParameter* TransparencyMap = CreateColorTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::TransparencyMap(), 2900);
    UMaterialExpressionScalarParameter* UseTransparencyMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseTransparencyMap(), 0.0f, 3000);
    UMaterialExpressionScalarParameter* TransparencyStrength = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::TransparencyStrength(), 1.0f, 3100);
    UMaterialExpressionTextureObjectParameter* WrinkleSuppressionMap = CreateColorTextureParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionMap(), 3200);
    UMaterialExpressionScalarParameter* UseWrinkleSuppressionMap = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::UseWrinkleSuppressionMap(), 0.0f, 3300);
    UMaterialExpressionScalarParameter* WrinkleSuppressionStrength = CreateScalarParameter(
        Material, DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(), 0.0f, 3400);
    UMaterialExpressionScalarParameter* PreviewWetness = FindPreviewWetnessParameter(Material);
    if (Blend == nullptr || TransparencyMap == nullptr || UseTransparencyMap == nullptr ||
        TransparencyStrength == nullptr || WrinkleSuppressionMap == nullptr ||
        UseWrinkleSuppressionMap == nullptr || WrinkleSuppressionStrength == nullptr ||
        PreviewWetness == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create one or more Transparency preview expressions.");
        return false;
    }

    static const FName InputNames[] = {
        TEXT("BaseColor"),
        TEXT("TransparencyMapTex"),
        TEXT("SelectedUV"),
        TEXT("UseTransparencyMap"),
        TEXT("PreviewWetness"),
        TEXT("TransparencyStrength"),
        TEXT("WrinkleSuppressionMapTex"),
        TEXT("UseWrinkleSuppressionMap"),
        TEXT("WrinkleSuppressionStrength"),
    };
    for (const FName InputName : InputNames)
    {
        FCustomInput& Input = Blend->Inputs.AddDefaulted_GetRef();
        Input.InputName = InputName;
    }
    Blend->Code = TEXT(R"(
float4 TransparencySample = Texture2DSampleLevel(TransparencyMapTex, TransparencyMapTexSampler, SelectedUV, 0);
float Suppression = Texture2DSampleLevel(WrinkleSuppressionMapTex, WrinkleSuppressionMapTexSampler, SelectedUV, 0).r;
float SuppressionWeight = saturate(Suppression * max(WrinkleSuppressionStrength, 0.0)) * saturate(UseWrinkleSuppressionMap);
float BlendWeight = saturate(TransparencySample.a) * max(TransparencyStrength, 0.0) * saturate(UseTransparencyMap) * saturate(PreviewWetness) * (1.0 - SuppressionWeight);
return lerp(BaseColor, TransparencySample.rgb, BlendWeight);
)");
    Blend->OutputType = CMOT_Float3;
    Blend->Description = TEXT("DWC Transparency Live Preview BaseColor Blend");
    Blend->RebuildOutputs();

    FDWCMaterialGraphPin DataUVPin;
    DataUVPin.Expression = SurfaceGraph.DWCDataUVExpression;
    bool bConnected = Connect(SurfaceGraph.Outputs.BaseColor, Blend, TEXT("BaseColor"), OutErrorMessage);
    bConnected &= Connect({ TransparencyMap, FString() }, Blend, TEXT("TransparencyMapTex"), OutErrorMessage);
    bConnected &= Connect(DataUVPin, Blend, TEXT("SelectedUV"), OutErrorMessage);
    bConnected &= Connect({ UseTransparencyMap, FString() }, Blend, TEXT("UseTransparencyMap"), OutErrorMessage);
    bConnected &= Connect({ PreviewWetness, FString() }, Blend, TEXT("PreviewWetness"), OutErrorMessage);
    bConnected &= Connect({ TransparencyStrength, FString() }, Blend, TEXT("TransparencyStrength"), OutErrorMessage);
    bConnected &= Connect({ WrinkleSuppressionMap, FString() }, Blend, TEXT("WrinkleSuppressionMapTex"), OutErrorMessage);
    bConnected &= Connect({ UseWrinkleSuppressionMap, FString() }, Blend, TEXT("UseWrinkleSuppressionMap"), OutErrorMessage);
    bConnected &= Connect({ WrinkleSuppressionStrength, FString() }, Blend, TEXT("WrinkleSuppressionStrength"), OutErrorMessage);
    if (!bConnected || !UMaterialEditingLibrary::ConnectMaterialProperty(Blend, FString(), MP_BaseColor))
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = TEXT("Failed to connect the Transparency preview Base Color output.");
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
    PreviewMID.SetTextureParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionMap(), nullptr);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::UseWrinkleSuppressionMap(), 0.0f);
    PreviewMID.SetScalarParameterValue(DWCTransparencyPreviewMaterialParameters::WrinkleSuppressionStrength(), 0.0f);
}
