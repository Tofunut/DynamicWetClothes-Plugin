#include "WetWrinklePreviewMaterialBuilder.h"

#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "UObject/UObjectGlobals.h"

namespace WetWrinklePreviewMaterialParameters
{
    const FName PreviewWetness(TEXT("DWC_PreviewWetness"));
    const FName AccumulatedNormal(TEXT("DWC_WrinklePreview_AccumulatedNormal"));
    const FName AccumulatedEnabled(TEXT("DWC_WrinklePreview_AccumulatedEnabled"));
    const FName AccumulatedStrength(TEXT("DWC_WrinklePreview_AccumulatedStrength"));
    const FName TransientRidgeNormal(TEXT("DWC_WrinklePreview_TransientRidgeNormal"));
    const FName TransientRidgeEnabled(TEXT("DWC_WrinklePreview_TransientRidgeEnabled"));
    const FName HoverNormal(TEXT("DWC_WrinklePreview_HoverNormal"));
    const FName HoverEnabled(TEXT("DWC_WrinklePreview_HoverEnabled"));
    const FName HoverCenterUV(TEXT("DWC_WrinklePreview_HoverCenterUV"));
    const FName HoverRadiusUV(TEXT("DWC_WrinklePreview_HoverRadiusUV"));
    const FName HoverRotation(TEXT("DWC_WrinklePreview_HoverRotation"));
    const FName HoverScale(TEXT("DWC_WrinklePreview_HoverScale"));
    const FName HoverStrength(TEXT("DWC_WrinklePreview_HoverStrength"));
    const FName HoverFalloff(TEXT("DWC_WrinklePreview_HoverFalloff"));
}

namespace
{
    constexpr int32 MaxGpuSkinUVChannelCount = 4;
    constexpr const TCHAR* PreviewBlendDescription = TEXT("DWC Wrinkle Preview Normal Blend");
    constexpr const TCHAR* LegacyPreviewBlendDescription = TEXT("DWC Preview Brush Normal Blend");

    UTexture* LoadWetWrinkleDefaultNormalTexture()
    {
        if (UTexture* DefaultNormal = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return DefaultNormal;
        }

        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

    UMaterialFunctionInterface* LoadDwcEvaluateSurfaceAppearanceFunction()
    {
        return LoadObject<UMaterialFunctionInterface>(
            nullptr,
            TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_EvaluateSurfaceAppearance.MF_DWC_EvaluateSurfaceAppearance"));
    }

    bool IsExpectedFunctionCall(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const UMaterialFunctionInterface* Function,
        const TCHAR* ExpectedFunctionName)
    {
        return FunctionCall != nullptr &&
               FunctionCall->MaterialFunction != nullptr &&
               (FunctionCall->MaterialFunction == Function ||
                FunctionCall->MaterialFunction->GetName().Equals(ExpectedFunctionName, ESearchCase::CaseSensitive));
    }

    UMaterialExpressionMaterialFunctionCall* FindFunctionCall(
        UMaterial* Material,
        const UMaterialFunctionInterface* Function,
        const TCHAR* ExpectedFunctionName)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (IsExpectedFunctionCall(FunctionCall, Function, ExpectedFunctionName))
            {
                return FunctionCall;
            }
        }

        return nullptr;
    }

    FString DescribeMaterialFunctionCalls(const UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return TEXT("<none>");
        }

        TArray<FString> FunctionNames;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (const UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
            {
                FunctionNames.Add(GetNameSafe(FunctionCall->MaterialFunction));
            }
        }

        return FunctionNames.IsEmpty() ? TEXT("<none>") : FString::Join(FunctionNames, TEXT(", "));
    }

    bool ConnectExpression(
        UMaterialExpression* FromExpression,
        const FString& FromOutput,
        UMaterialExpression* ToExpression,
        const FString& ToInput,
        FString& OutError)
    {
        if (FromExpression == nullptr || ToExpression == nullptr ||
            !UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutput, ToExpression, ToInput))
        {
            OutError = FString::Printf(
                TEXT("Failed to connect '%s.%s' to '%s.%s'."),
                *GetNameSafe(FromExpression),
                FromOutput.IsEmpty() ? TEXT("<default>") : *FromOutput,
                *GetNameSafe(ToExpression),
                *ToInput);
            return false;
        }

        return true;
    }

    FString ResolveOutputName(const FExpressionInput& Input)
    {
        UMaterialExpression* Expression = Input.Expression;
        if (Expression == nullptr)
        {
            return FString();
        }

        TArray<FString> OutputNames;
        for (const FExpressionOutput& Output : Expression->GetOutputs())
        {
            OutputNames.Add(Output.OutputName.ToString());
        }
        return OutputNames.IsValidIndex(Input.OutputIndex) ? OutputNames[Input.OutputIndex] : FString();
    }

    bool IsLegacyPreviewParameter(const UMaterialExpression* Expression)
    {
        FName ParameterName = NAME_None;
        if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
        {
            ParameterName = Scalar->ParameterName;
        }
        else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
        {
            ParameterName = Vector->ParameterName;
        }
        else if (const UMaterialExpressionTextureObjectParameter* Texture = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
        {
            ParameterName = Texture->ParameterName;
        }

        return ParameterName.ToString().StartsWith(TEXT("DWC_PreviewBrush"));
    }

    void RemoveLegacyPreviewGraph(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return;
        }

        TArray<UMaterialExpression*> ExpressionsToDelete;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
            {
                if (Custom->Description == LegacyPreviewBlendDescription)
                {
                    for (const FCustomInput& Input : Custom->Inputs)
                    {
                        if (Input.InputName == TEXT("BaseNormal") && Input.Input.Expression != nullptr)
                        {
                            UMaterialEditingLibrary::ConnectMaterialProperty(
                                Input.Input.Expression,
                                ResolveOutputName(Input.Input),
                                MP_Normal);
                            break;
                        }
                    }
                    ExpressionsToDelete.Add(Custom);
                }
            }

            if (IsLegacyPreviewParameter(Expression))
            {
                ExpressionsToDelete.AddUnique(Expression);
            }

            if (const UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
            {
                if (TextureCoordinate->Desc.StartsWith(TEXT("DWC Preview UV")))
                {
                    ExpressionsToDelete.AddUnique(Expression);
                }
            }
        }

        for (UMaterialExpression* Expression : ExpressionsToDelete)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
    }

    UMaterialExpressionScalarParameter* CreateWetWrinkleScalarParameter(
        UMaterial* Material,
        const FName ParameterName,
        const float DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), NodeX, NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionVectorParameter* CreateWetWrinkleVectorParameter(
        UMaterial* Material,
        const FName ParameterName,
        const FLinearColor& DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionVectorParameter* Parameter = Cast<UMaterialExpressionVectorParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), NodeX, NodeY));
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
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass(), NodeX, NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Normal;
            Parameter->Texture = LoadWetWrinkleDefaultNormalTexture();
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* CreateColorTextureParameter(
        UMaterial* Material,
        const FName ParameterName,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass(), NodeX, NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
        }
        return Parameter;
    }

    UMaterialExpressionCustom* CreatePreviewBlendExpression(UMaterial* Material, const int32 NodeX, const int32 NodeY)
    {
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), NodeX, NodeY));
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
            TEXT("HoverCenterUV"),
            TEXT("HoverRadiusUV"),
            TEXT("HoverRotation"),
            TEXT("HoverScale"),
            TEXT("HoverStrength"),
            TEXT("HoverFalloff"),
            TEXT("HoverNormalTex"),
        };

        Custom->Inputs.Reset();
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

if (HoverEnabled > 0.5 && HoverRadiusUV > 0.000001)
{
    float2 SafeScale = max(abs(HoverScale.xy), float2(0.0001, 0.0001));
    float2 DeltaUV = SelectedUV - HoverCenterUV.xy;
    DeltaUV = DeltaUV - round(DeltaUV);
    float CosRotation = cos(HoverRotation);
    float SinRotation = sin(HoverRotation);
    float2 LocalBrush = float2(
        (CosRotation * DeltaUV.x + SinRotation * DeltaUV.y) / (HoverRadiusUV * SafeScale.x),
        (-SinRotation * DeltaUV.x + CosRotation * DeltaUV.y) / (HoverRadiusUV * SafeScale.y));
    float DistanceFromCenter = length(LocalBrush);
    if (DistanceFromCenter < 1.0)
    {
        float EdgeFadeStart = clamp(1.0 - HoverFalloff, 0.0, 0.98);
        float EdgeFade = 1.0 - smoothstep(EdgeFadeStart, 1.0, DistanceFromCenter);
        float2 HoverUV = saturate(LocalBrush * 0.5 + 0.5);
        float2 HoverXY = Texture2DSampleLevel(HoverNormalTex, HoverNormalTexSampler, HoverUV, 0).rg * 2.0 - 1.0;
        HoverXY.y = -HoverXY.y;
        HoverXY = float2(
            HoverXY.x * CosRotation - HoverXY.y * SinRotation,
            HoverXY.x * SinRotation + HoverXY.y * CosRotation);
        float HoverWeight = max(HoverStrength, 0.0) * EdgeFade * PreviewWetnessScale;
        float3 HoverTS = normalize(float3(HoverXY * HoverWeight, 1.0));
        CombinedTS = normalize(float3(CombinedTS.xy + HoverTS.xy, CombinedTS.z * HoverTS.z));
    }
}

return CombinedTS;
)");
        Custom->OutputType = CMOT_Float3;
        Custom->Description = PreviewBlendDescription;
        Custom->RebuildOutputs();
        return Custom;
    }

    bool OverrideEvaluateSurfaceWetnessInput(
        UMaterial* Material,
        UMaterialFunctionInterface* EvaluateSurfaceFunction,
        UMaterialExpressionScalarParameter* PreviewWetness,
        FString& OutError)
    {
        if (Material == nullptr || EvaluateSurfaceFunction == nullptr || PreviewWetness == nullptr)
        {
            OutError = TEXT("Cannot override the preview wetness input without a surface-evaluation function call and scalar parameter.");
            return false;
        }

        UMaterialExpressionMaterialFunctionCall* EvaluateSurface = FindFunctionCall(
            Material,
            EvaluateSurfaceFunction,
            *EvaluateSurfaceFunction->GetName());
        if (EvaluateSurface == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The duplicated preview graph does not contain MF_DWC_EvaluateSurfaceAppearance. Function calls found: %s."),
                *DescribeMaterialFunctionCalls(Material));
            return false;
        }

        return ConnectExpression(PreviewWetness, FString(), EvaluateSurface, TEXT("Wetness"), OutError);
    }

    bool ConnectPreviewGraph(
        UMaterial* Material,
        const int32 UVChannelIndex,
        UMaterialExpressionScalarParameter* PreviewWetness,
        FString& OutError)
    {
        UMaterialExpressionCustom* Blend = CreatePreviewBlendExpression(Material, -100, 1300);
        UMaterialExpressionTextureObjectParameter* AccumulatedNormal = CreateNormalTextureParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedNormal, -650, 1350);
        UMaterialExpressionScalarParameter* AccumulatedEnabled = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f, -650, 1450);
        UMaterialExpressionScalarParameter* AccumulatedStrength = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f, -650, 1550);
        UMaterialExpressionTextureObjectParameter* TransientRidgeNormal = CreateNormalTextureParameter(
            Material, WetWrinklePreviewMaterialParameters::TransientRidgeNormal, -650, 1650);
        UMaterialExpressionScalarParameter* TransientRidgeEnabled = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::TransientRidgeEnabled, 0.0f, -650, 1750);
        UMaterialExpressionTextureObjectParameter* HoverNormal = CreateNormalTextureParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverNormal, -650, 1850);
        UMaterialExpressionScalarParameter* HoverEnabled = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f, -650, 1950);
        UMaterialExpressionVectorParameter* HoverCenterUV = CreateWetWrinkleVectorParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverCenterUV, FLinearColor::Black, -650, 2050);
        UMaterialExpressionScalarParameter* HoverRadiusUV = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverRadiusUV, 0.025f, -650, 2150);
        UMaterialExpressionScalarParameter* HoverRotation = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverRotation, 0.0f, -650, 2250);
        UMaterialExpressionVectorParameter* HoverScale = CreateWetWrinkleVectorParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverScale, FLinearColor(1.0f, 1.0f, 0.0f, 0.0f), -650, 2350);
        UMaterialExpressionScalarParameter* HoverStrength = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverStrength, 1.0f, -650, 2450);
        UMaterialExpressionScalarParameter* HoverFalloff = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverFalloff, 0.5f, -650, 2550);

        if (Blend == nullptr || PreviewWetness == nullptr || AccumulatedNormal == nullptr || AccumulatedEnabled == nullptr ||
            AccumulatedStrength == nullptr || TransientRidgeNormal == nullptr || TransientRidgeEnabled == nullptr ||
            HoverNormal == nullptr || HoverEnabled == nullptr || HoverCenterUV == nullptr ||
            HoverRadiusUV == nullptr || HoverRotation == nullptr || HoverScale == nullptr || HoverStrength == nullptr ||
            HoverFalloff == nullptr)
        {
            OutError = TEXT("Failed to create one or more wrinkle preview material expressions.");
            return false;
        }

        UMaterialExpressionTextureCoordinate* UVCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                -900,
                1250));
        if (UVCoordinate == nullptr)
        {
            OutError = FString::Printf(TEXT("Failed to create preview UV coordinate %d."), UVChannelIndex);
            return false;
        }
        UVCoordinate->CoordinateIndex = UVChannelIndex;
        UVCoordinate->Desc = FString::Printf(TEXT("DWC Wrinkle Preview UV%d"), UVChannelIndex);

        UMaterialExpression* BaseNormal = nullptr;
        FString BaseNormalOutput;
        UMaterialExpressionGetMaterialAttributes* GetAttributes = nullptr;
        UMaterialExpressionSetMaterialAttributes* SetAttributes = nullptr;
        UMaterialExpression* MaterialAttributesInput = nullptr;
        FString MaterialAttributesOutput;

        if (Material->bUseMaterialAttributes)
        {
            MaterialAttributesInput = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_MaterialAttributes);
            MaterialAttributesOutput = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_MaterialAttributes);
            if (MaterialAttributesInput == nullptr)
            {
                OutError = TEXT("Material Attributes are enabled, but the Material Attributes input is not connected.");
                return false;
            }

            GetAttributes = Cast<UMaterialExpressionGetMaterialAttributes>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionGetMaterialAttributes::StaticClass(), -400, 1100));
            SetAttributes = Cast<UMaterialExpressionSetMaterialAttributes>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSetMaterialAttributes::StaticClass(), 180, 1200));
            if (GetAttributes == nullptr || SetAttributes == nullptr)
            {
                OutError = TEXT("Failed to create Material Attributes preview nodes.");
                return false;
            }

            GetAttributes->CreateOrGetOutputAttribute(MP_Normal);
            SetAttributes->CreateOrGetInputAttribute(MP_Normal);
            BaseNormal = GetAttributes;
            BaseNormalOutput = TEXT("Normal");
        }
        else
        {
            BaseNormal = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Normal);
            BaseNormalOutput = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_Normal);
            if (BaseNormal == nullptr)
            {
                UMaterialExpressionConstant3Vector* FlatNormal = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -400, 1100));
                if (FlatNormal == nullptr)
                {
                    OutError = TEXT("Failed to create the fallback flat normal.");
                    return false;
                }
                FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
                BaseNormal = FlatNormal;
                BaseNormalOutput.Reset();
            }
        }

        bool bConnected = ConnectExpression(BaseNormal, BaseNormalOutput, Blend, TEXT("BaseNormal"), OutError);
        bConnected &= ConnectExpression(UVCoordinate, FString(), Blend, TEXT("SelectedUV"), OutError);
        bConnected &= ConnectExpression(PreviewWetness, FString(), Blend, TEXT("PreviewWetness"), OutError);
        bConnected &= ConnectExpression(AccumulatedEnabled, FString(), Blend, TEXT("AccumulatedEnabled"), OutError);
        bConnected &= ConnectExpression(AccumulatedStrength, FString(), Blend, TEXT("AccumulatedStrength"), OutError);
        bConnected &= ConnectExpression(AccumulatedNormal, FString(), Blend, TEXT("AccumulatedNormalTex"), OutError);
        bConnected &= ConnectExpression(TransientRidgeEnabled, FString(), Blend, TEXT("TransientRidgeEnabled"), OutError);
        bConnected &= ConnectExpression(TransientRidgeNormal, FString(), Blend, TEXT("TransientRidgeNormalTex"), OutError);
        bConnected &= ConnectExpression(HoverEnabled, FString(), Blend, TEXT("HoverEnabled"), OutError);
        bConnected &= ConnectExpression(HoverCenterUV, FString(), Blend, TEXT("HoverCenterUV"), OutError);
        bConnected &= ConnectExpression(HoverRadiusUV, FString(), Blend, TEXT("HoverRadiusUV"), OutError);
        bConnected &= ConnectExpression(HoverRotation, FString(), Blend, TEXT("HoverRotation"), OutError);
        bConnected &= ConnectExpression(HoverScale, FString(), Blend, TEXT("HoverScale"), OutError);
        bConnected &= ConnectExpression(HoverStrength, FString(), Blend, TEXT("HoverStrength"), OutError);
        bConnected &= ConnectExpression(HoverFalloff, FString(), Blend, TEXT("HoverFalloff"), OutError);
        bConnected &= ConnectExpression(HoverNormal, FString(), Blend, TEXT("HoverNormalTex"), OutError);
        if (!bConnected)
        {
            return false;
        }

        if (Material->bUseMaterialAttributes)
        {
            bConnected &= ConnectExpression(MaterialAttributesInput, MaterialAttributesOutput, GetAttributes, TEXT("MaterialAttributes"), OutError);
            bConnected &= ConnectExpression(MaterialAttributesInput, MaterialAttributesOutput, SetAttributes, TEXT("MaterialAttributes"), OutError);
            bConnected &= ConnectExpression(Blend, FString(), SetAttributes, TEXT("Normal"), OutError);
            if (!bConnected || !UMaterialEditingLibrary::ConnectMaterialProperty(SetAttributes, FString(), MP_MaterialAttributes))
            {
                if (OutError.IsEmpty())
                {
                    OutError = TEXT("Failed to connect the wrinkle preview Material Attributes output.");
                }
                return false;
            }
        }
        else if (!UMaterialEditingLibrary::ConnectMaterialProperty(Blend, FString(), MP_Normal))
        {
            OutError = TEXT("Failed to connect the wrinkle preview output to Material Normal.");
            return false;
        }

        return true;
    }

    bool ConnectTransparencyPreviewGraph(
        UMaterial* Material,
        const int32 UVChannelIndex,
        UMaterialExpressionScalarParameter* PreviewWetness,
        FString& OutError)
    {
        UMaterialExpressionCustom* Blend = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), -50, 2900));
        UMaterialExpressionTextureObjectParameter* TransparencyMap = CreateColorTextureParameter(
            Material, TEXT("DWC_TransparencyPreviewMap"), -650, 2900);
        UMaterialExpressionScalarParameter* UseTransparencyMap = CreateWetWrinkleScalarParameter(
            Material, TEXT("DWC_UseTransparencyPreviewMap"), 0.0f, -650, 3000);
        UMaterialExpressionScalarParameter* TransparencyStrength = CreateWetWrinkleScalarParameter(
            Material, TEXT("DWC_TransparencyPreviewStrength"), 1.0f, -650, 3050);
        UMaterialExpressionTextureObjectParameter* WrinkleSuppressionMap = CreateColorTextureParameter(
            Material, TEXT("DWC_TransparencyPreviewSuppressionMap"), -650, 3100);
        UMaterialExpressionScalarParameter* UseWrinkleSuppressionMap = CreateWetWrinkleScalarParameter(
            Material, TEXT("DWC_UseTransparencyPreviewSuppression"), 0.0f, -650, 3150);
        UMaterialExpressionScalarParameter* WrinkleSuppressionStrength = CreateWetWrinkleScalarParameter(
            Material, TEXT("DWC_TransparencyPreviewWrinkleSuppressionStrength"), 0.0f, -650, 3200);
        UMaterialExpressionTextureCoordinate* UVCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -650, 3250));
        if (Blend == nullptr || TransparencyMap == nullptr || UseTransparencyMap == nullptr ||
            TransparencyStrength == nullptr || WrinkleSuppressionMap == nullptr ||
            UseWrinkleSuppressionMap == nullptr || WrinkleSuppressionStrength == nullptr ||
            PreviewWetness == nullptr || UVCoordinate == nullptr)
        {
            OutError = TEXT("Failed to create one or more transparency preview material expressions.");
            return false;
        }

        UVCoordinate->CoordinateIndex = UVChannelIndex;
        UVCoordinate->Desc = FString::Printf(TEXT("DWC Transparency Preview UV%d"), UVChannelIndex);
        static const FName InputNames[] = {
            TEXT("BaseColor"), TEXT("TransparencyMapTex"), TEXT("SelectedUV"),
            TEXT("UseTransparencyMap"), TEXT("PreviewWetness"), TEXT("TransparencyStrength"),
            TEXT("WrinkleSuppressionMapTex"), TEXT("UseWrinkleSuppressionMap"), TEXT("WrinkleSuppressionStrength")};
        Blend->Inputs.Reset();
        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Blend->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }
        Blend->Code = TEXT(R"(
float4 TransparencySample = Texture2DSampleLevel(TransparencyMapTex, TransparencyMapTexSampler, SelectedUV, 0);
float Suppression = Texture2DSampleLevel(WrinkleSuppressionMapTex, WrinkleSuppressionMapTexSampler, SelectedUV, 0).r;
float SuppressionWeight = saturate(Suppression * max(WrinkleSuppressionStrength, 0.0f)) * saturate(UseWrinkleSuppressionMap);
float BlendWeight = saturate(TransparencySample.a) * max(TransparencyStrength, 0.0f) * saturate(UseTransparencyMap) * saturate(PreviewWetness) * (1.0f - SuppressionWeight);
return lerp(BaseColor, TransparencySample.rgb, BlendWeight);
)");
        Blend->OutputType = CMOT_Float3;
        Blend->Description = TEXT("DWC Transparency Preview BaseColor Blend");
        Blend->RebuildOutputs();

        UMaterialExpression* BaseColor = nullptr;
        FString BaseColorOutput;
        UMaterialExpressionGetMaterialAttributes* GetAttributes = nullptr;
        UMaterialExpressionSetMaterialAttributes* SetAttributes = nullptr;
        UMaterialExpression* MaterialAttributesInput = nullptr;
        FString MaterialAttributesOutput;
        if (Material->bUseMaterialAttributes)
        {
            MaterialAttributesInput = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_MaterialAttributes);
            MaterialAttributesOutput = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_MaterialAttributes);
            if (MaterialAttributesInput == nullptr)
            {
                OutError = TEXT("Material Attributes are enabled, but the Material Attributes input is not connected.");
                return false;
            }
            GetAttributes = Cast<UMaterialExpressionGetMaterialAttributes>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionGetMaterialAttributes::StaticClass(), -350, 2800));
            SetAttributes = Cast<UMaterialExpressionSetMaterialAttributes>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionSetMaterialAttributes::StaticClass(), 180, 2900));
            if (GetAttributes == nullptr || SetAttributes == nullptr)
            {
                OutError = TEXT("Failed to create Material Attributes transparency preview nodes.");
                return false;
            }
            GetAttributes->CreateOrGetOutputAttribute(MP_BaseColor);
            SetAttributes->CreateOrGetInputAttribute(MP_BaseColor);
            BaseColor = GetAttributes;
            BaseColorOutput = TEXT("BaseColor");
        }
        else
        {
            BaseColor = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_BaseColor);
            BaseColorOutput = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, MP_BaseColor);
            if (BaseColor == nullptr)
            {
                UMaterialExpressionConstant3Vector* Fallback = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -350, 2800));
                if (Fallback == nullptr)
                {
                    OutError = TEXT("Failed to create the transparency preview fallback BaseColor.");
                    return false;
                }
                Fallback->Constant = FLinearColor::White;
                BaseColor = Fallback;
                BaseColorOutput.Reset();
            }
        }

        bool bConnected = ConnectExpression(BaseColor, BaseColorOutput, Blend, TEXT("BaseColor"), OutError);
        bConnected &= ConnectExpression(TransparencyMap, FString(), Blend, TEXT("TransparencyMapTex"), OutError);
        bConnected &= ConnectExpression(UVCoordinate, FString(), Blend, TEXT("SelectedUV"), OutError);
        bConnected &= ConnectExpression(UseTransparencyMap, FString(), Blend, TEXT("UseTransparencyMap"), OutError);
        bConnected &= ConnectExpression(PreviewWetness, FString(), Blend, TEXT("PreviewWetness"), OutError);
        bConnected &= ConnectExpression(TransparencyStrength, FString(), Blend, TEXT("TransparencyStrength"), OutError);
        bConnected &= ConnectExpression(WrinkleSuppressionMap, FString(), Blend, TEXT("WrinkleSuppressionMapTex"), OutError);
        bConnected &= ConnectExpression(UseWrinkleSuppressionMap, FString(), Blend, TEXT("UseWrinkleSuppressionMap"), OutError);
        bConnected &= ConnectExpression(WrinkleSuppressionStrength, FString(), Blend, TEXT("WrinkleSuppressionStrength"), OutError);
        if (!bConnected)
        {
            return false;
        }

        if (Material->bUseMaterialAttributes)
        {
            bConnected &= ConnectExpression(MaterialAttributesInput, MaterialAttributesOutput, GetAttributes, TEXT("MaterialAttributes"), OutError);
            bConnected &= ConnectExpression(MaterialAttributesInput, MaterialAttributesOutput, SetAttributes, TEXT("MaterialAttributes"), OutError);
            bConnected &= ConnectExpression(Blend, FString(), SetAttributes, TEXT("BaseColor"), OutError);
            if (!bConnected || !UMaterialEditingLibrary::ConnectMaterialProperty(SetAttributes, FString(), MP_MaterialAttributes))
            {
                if (OutError.IsEmpty())
                {
                    OutError = TEXT("Failed to connect the transparency preview Material Attributes output.");
                }
                return false;
            }
        }
        else if (!UMaterialEditingLibrary::ConnectMaterialProperty(Blend, FString(), MP_BaseColor))
        {
            OutError = TEXT("Failed to connect the transparency preview output to Material Base Color.");
            return false;
        }
        return true;
    }

    UMaterialInterface* CreateTransientParentForSource(
        UMaterialInterface* SourceMaterial,
        UMaterial* TransientBaseMaterial,
        FString& OutError)
    {
        if (!SourceMaterial->IsA<UMaterialInstance>())
        {
            return TransientBaseMaterial;
        }

        UMaterialInstanceConstant* TransientInstance = NewObject<UMaterialInstanceConstant>(
            GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UMaterialInstanceConstant::StaticClass(), TEXT("DWC_WrinklePreviewMIC")),
            RF_Transient);
        if (TransientInstance == nullptr)
        {
            OutError = TEXT("Failed to create the transient wrinkle preview material instance.");
            return nullptr;
        }

        TransientInstance->SetParentEditorOnly(TransientBaseMaterial, false);
        TransientInstance->CopyMaterialUniformParametersEditorOnly(SourceMaterial, true);
        if (const UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(SourceMaterial))
        {
            FMaterialInstanceBasePropertyOverrides BasePropertyOverrides = SourceInstance->BasePropertyOverrides;
            TransientInstance->UpdateStaticPermutation(
                SourceInstance->GetStaticParameters(),
                BasePropertyOverrides);
        }
        TransientInstance->PostEditChange();
        return TransientInstance;
    }

    TArray<FString> CompileTransientPreviewMaterial(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return { TEXT("Cannot compile a null wrinkle preview material.") };
        }

        Material->SetMaterialUsage(MATUSAGE_SkeletalMesh);
        Material->UpdateCachedExpressionData();

        UTexture* const DefaultNormalTexture = LoadWetWrinkleDefaultNormalTexture();
        if (DefaultNormalTexture == nullptr)
        {
            return { TEXT("Could not load the engine default normal texture used by wrinkle preview parameters.") };
        }

        if (!Material->ContainsDefaultTexture(DefaultNormalTexture))
        {
            return {
                FString::Printf(
                    TEXT("The transient wrinkle preview material did not rebuild its default texture cache for '%s'."),
                    *DefaultNormalTexture->GetPathName())
            };
        }

        {
            FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
            UpdateContext.AddMaterial(Material);
            Material->PreEditChange(nullptr);
            Material->PostEditChange();
        }

        if (FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
        {
            if (!Resource->IsGameThreadShaderMapComplete())
            {
                Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
            }
            Resource->FinishCompilation();
            return Resource->GetCompileErrors();
        }

        return { TEXT("The transient wrinkle preview material has no material resource after compilation.") };
    }

}

FWetWrinklePreviewMaterialBuildResult FWetWrinklePreviewMaterialBuilder::Build(const FWetWrinklePreviewMaterialBuildArgs& Args)
{
    FWetWrinklePreviewMaterialBuildResult Result;
    UMaterialInterface* const SourceMaterial = Args.SourceMaterial;
    if (SourceMaterial == nullptr)
    {
        Result.ErrorMessage = TEXT("No source material was supplied for wrinkle preview.");
        return Result;
    }

    UMaterial* SourceBaseMaterial = SourceMaterial->GetMaterial();
    if (SourceBaseMaterial == nullptr)
    {
        Result.ErrorMessage = FString::Printf(TEXT("'%s' does not resolve to a base material."), *SourceMaterial->GetName());
        return Result;
    }

    if (Args.UVChannelIndex < 0 || Args.UVChannelIndex >= MaxGpuSkinUVChannelCount)
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("Wrinkle preview UV channel %d is not supported by the skeletal GPUSkin path. Valid channels are 0 through %d."),
            Args.UVChannelIndex,
            MaxGpuSkinUVChannelCount - 1);
        return Result;
    }

    UMaterialFunctionInterface* EvaluateSurfaceFunction = nullptr;
    if (Args.bOverrideCpuWetnessInput)
    {
        EvaluateSurfaceFunction = LoadDwcEvaluateSurfaceAppearanceFunction();
        if (EvaluateSurfaceFunction == nullptr)
        {
            Result.ErrorMessage = TEXT("Could not load MF_DWC_EvaluateSurfaceAppearance for the DWC preview path.");
            return Result;
        }
    }

    const bool bSourcePackageWasDirty = SourceMaterial->GetOutermost()->IsDirty();
    UMaterial* TransientMaterial = DuplicateObject<UMaterial>(
        SourceBaseMaterial,
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("DWC_WrinklePreviewMaterial")));
    if (TransientMaterial == nullptr)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to create a transient copy of '%s' for wrinkle preview."), *SourceBaseMaterial->GetName());
        return Result;
    }

    // A graph-aware duplication preserves the function-call expressions and their input names.
    // The preview compilation path below rebuilds cached expression data after adding preview nodes.
    TransientMaterial->ClearFlags(RF_Standalone | RF_Transactional);
    TransientMaterial->SetFlags(RF_Transient);
    RemoveLegacyPreviewGraph(TransientMaterial);

    UMaterialExpressionScalarParameter* PreviewWetness = CreateWetWrinkleScalarParameter(
        TransientMaterial,
        WetWrinklePreviewMaterialParameters::PreviewWetness,
        1.0f,
        -650,
        1300);
    if (PreviewWetness == nullptr)
    {
        Result.ErrorMessage = TEXT("Failed to create the preview wetness scalar parameter.");
        return Result;
    }

    if (Args.bOverrideCpuWetnessInput &&
        !OverrideEvaluateSurfaceWetnessInput(TransientMaterial, EvaluateSurfaceFunction, PreviewWetness, Result.ErrorMessage))
    {
        return Result;
    }

    if (Args.bBuildNormalOverlay &&
        !ConnectPreviewGraph(TransientMaterial, Args.UVChannelIndex, PreviewWetness, Result.ErrorMessage))
    {
        return Result;
    }

    if (Args.bBuildTransparencyPreviewOverlay &&
        !ConnectTransparencyPreviewGraph(TransientMaterial, Args.UVChannelIndex, PreviewWetness, Result.ErrorMessage))
    {
        return Result;
    }

    const TArray<FString> CompileErrors = CompileTransientPreviewMaterial(TransientMaterial);
    if (CompileErrors.Num() > 0)
    {
        Result.ErrorMessage = FString::Printf(
            TEXT("Wrinkle preview material compilation failed for '%s':\n%s"),
            *SourceMaterial->GetName(),
            *FString::Join(CompileErrors, TEXT("\n")));
        return Result;
    }

    UMaterialInterface* TransientParent = CreateTransientParentForSource(SourceMaterial, TransientMaterial, Result.ErrorMessage);
    if (TransientParent == nullptr)
    {
        return Result;
    }

    UMaterialInstanceDynamic* PreviewMID = UMaterialInstanceDynamic::Create(TransientParent, GetTransientPackage());
    if (PreviewMID == nullptr)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to create a wrinkle preview MID for '%s'."), *SourceMaterial->GetName());
        return Result;
    }

    PreviewMID->SetFlags(RF_Transient);
    PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::PreviewWetness, 1.0f);
    PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f);
    PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f);

    ensureMsgf(
        SourceMaterial->GetOutermost()->IsDirty() == bSourcePackageWasDirty,
        TEXT("Building a transient wrinkle preview unexpectedly changed the dirty state of '%s'."),
        *SourceMaterial->GetPathName());

    Result.TransientBaseMaterial = TransientMaterial;
    Result.TransientMaterialParent = TransientParent;
    Result.PreviewMID = PreviewMID;
    Result.bSucceeded = true;
    return Result;
}
