#include "WetWrinklePreviewMaterialBuilder.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace WetWrinklePreviewMaterialParameters
{
    const FName UVChannel(TEXT("DWC_WrinklePreview_UVChannel"));
    const FName AccumulatedNormal(TEXT("DWC_WrinklePreview_AccumulatedNormal"));
    const FName AccumulatedEnabled(TEXT("DWC_WrinklePreview_AccumulatedEnabled"));
    const FName AccumulatedStrength(TEXT("DWC_WrinklePreview_AccumulatedStrength"));
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

        const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
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
            TEXT("UV0"), TEXT("UV1"), TEXT("UV2"), TEXT("UV3"),
            TEXT("UV4"), TEXT("UV5"), TEXT("UV6"), TEXT("UV7"),
            TEXT("UVChannel"),
            TEXT("AccumulatedEnabled"),
            TEXT("AccumulatedStrength"),
            TEXT("AccumulatedNormalTex"),
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
int UVIndex = (int)round(clamp(UVChannel, 0.0, 7.0));
float2 SelectedUV = UV0;
if (UVIndex == 1) { SelectedUV = UV1; }
else if (UVIndex == 2) { SelectedUV = UV2; }
else if (UVIndex == 3) { SelectedUV = UV3; }
else if (UVIndex == 4) { SelectedUV = UV4; }
else if (UVIndex == 5) { SelectedUV = UV5; }
else if (UVIndex == 6) { SelectedUV = UV6; }
else if (UVIndex == 7) { SelectedUV = UV7; }

float3 CombinedTS = BaseTS;
if (AccumulatedEnabled > 0.5)
{
    float2 AccumulatedXY = Texture2DSampleLevel(AccumulatedNormalTex, AccumulatedNormalTexSampler, frac(SelectedUV), 0).rg * 2.0 - 1.0;
    float3 AccumulatedTS = normalize(float3(AccumulatedXY, sqrt(saturate(1.0 - dot(AccumulatedXY, AccumulatedXY)))));
    AccumulatedTS = normalize(lerp(float3(0.0, 0.0, 1.0), AccumulatedTS, saturate(AccumulatedStrength)));
    CombinedTS = normalize(float3(CombinedTS.xy + AccumulatedTS.xy, CombinedTS.z * AccumulatedTS.z));
}

if (HoverEnabled > 0.5 && HoverRadiusUV > 0.000001)
{
    float2 SafeScale = max(abs(HoverScale.xy), float2(0.0001, 0.0001));
    float2 DeltaUV = SelectedUV - HoverCenterUV.xy;
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
        float3 HoverTS = normalize(float3(HoverXY, sqrt(saturate(1.0 - dot(HoverXY, HoverXY)))));
        HoverTS = normalize(lerp(float3(0.0, 0.0, 1.0), HoverTS, saturate(HoverStrength) * EdgeFade));
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

    bool ConnectPreviewGraph(UMaterial* Material, FString& OutError)
    {
        UMaterialExpressionCustom* Blend = CreatePreviewBlendExpression(Material, -100, 1300);
        UMaterialExpressionScalarParameter* UVChannel = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::UVChannel, 0.0f, -650, 1250);
        UMaterialExpressionTextureObjectParameter* AccumulatedNormal = CreateNormalTextureParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedNormal, -650, 1350);
        UMaterialExpressionScalarParameter* AccumulatedEnabled = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f, -650, 1450);
        UMaterialExpressionScalarParameter* AccumulatedStrength = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f, -650, 1550);
        UMaterialExpressionTextureObjectParameter* HoverNormal = CreateNormalTextureParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverNormal, -650, 1650);
        UMaterialExpressionScalarParameter* HoverEnabled = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f, -650, 1750);
        UMaterialExpressionVectorParameter* HoverCenterUV = CreateWetWrinkleVectorParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverCenterUV, FLinearColor::Black, -650, 1850);
        UMaterialExpressionScalarParameter* HoverRadiusUV = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverRadiusUV, 0.025f, -650, 1950);
        UMaterialExpressionScalarParameter* HoverRotation = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverRotation, 0.0f, -650, 2050);
        UMaterialExpressionVectorParameter* HoverScale = CreateWetWrinkleVectorParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverScale, FLinearColor(1.0f, 1.0f, 0.0f, 0.0f), -650, 2150);
        UMaterialExpressionScalarParameter* HoverStrength = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverStrength, 1.0f, -650, 2250);
        UMaterialExpressionScalarParameter* HoverFalloff = CreateWetWrinkleScalarParameter(
            Material, WetWrinklePreviewMaterialParameters::HoverFalloff, 0.5f, -650, 2350);

        if (Blend == nullptr || UVChannel == nullptr || AccumulatedNormal == nullptr || AccumulatedEnabled == nullptr ||
            AccumulatedStrength == nullptr || HoverNormal == nullptr || HoverEnabled == nullptr || HoverCenterUV == nullptr ||
            HoverRadiusUV == nullptr || HoverRotation == nullptr || HoverScale == nullptr || HoverStrength == nullptr ||
            HoverFalloff == nullptr)
        {
            OutError = TEXT("Failed to create one or more wrinkle preview material expressions.");
            return false;
        }

        TArray<UMaterialExpressionTextureCoordinate*> UVCoordinates;
        UVCoordinates.SetNum(8);
        for (int32 UVIndex = 0; UVIndex < UVCoordinates.Num(); ++UVIndex)
        {
            UVCoordinates[UVIndex] = Cast<UMaterialExpressionTextureCoordinate>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionTextureCoordinate::StaticClass(),
                    -900,
                    1250 + UVIndex * 90));
            if (UVCoordinates[UVIndex] == nullptr)
            {
                OutError = FString::Printf(TEXT("Failed to create preview UV coordinate %d."), UVIndex);
                return false;
            }
            UVCoordinates[UVIndex]->CoordinateIndex = UVIndex;
            UVCoordinates[UVIndex]->Desc = FString::Printf(TEXT("DWC Wrinkle Preview UV%d"), UVIndex);
        }

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
        for (int32 UVIndex = 0; bConnected && UVIndex < UVCoordinates.Num(); ++UVIndex)
        {
            bConnected &= ConnectExpression(UVCoordinates[UVIndex], FString(), Blend, FString::Printf(TEXT("UV%d"), UVIndex), OutError);
        }
        bConnected &= ConnectExpression(UVChannel, FString(), Blend, TEXT("UVChannel"), OutError);
        bConnected &= ConnectExpression(AccumulatedEnabled, FString(), Blend, TEXT("AccumulatedEnabled"), OutError);
        bConnected &= ConnectExpression(AccumulatedStrength, FString(), Blend, TEXT("AccumulatedStrength"), OutError);
        bConnected &= ConnectExpression(AccumulatedNormal, FString(), Blend, TEXT("AccumulatedNormalTex"), OutError);
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
        TransientInstance->PostEditChange();
        return TransientInstance;
    }
}

FWetWrinklePreviewMaterialBuildResult FWetWrinklePreviewMaterialBuilder::Build(UMaterialInterface* SourceMaterial)
{
    FWetWrinklePreviewMaterialBuildResult Result;
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

    const bool bSourcePackageWasDirty = SourceMaterial->GetOutermost()->IsDirty();
    UMaterial* TransientMaterial = DuplicateObject<UMaterial>(
        SourceBaseMaterial,
        GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("DWC_WrinklePreviewMaterial")));
    if (TransientMaterial == nullptr)
    {
        Result.ErrorMessage = FString::Printf(TEXT("Failed to duplicate '%s' for wrinkle preview."), *SourceBaseMaterial->GetName());
        return Result;
    }

    TransientMaterial->SetFlags(RF_Transient);
    TransientMaterial->ClearFlags(RF_Standalone | RF_Transactional);
    RemoveLegacyPreviewGraph(TransientMaterial);

    if (!ConnectPreviewGraph(TransientMaterial, Result.ErrorMessage))
    {
        return Result;
    }

    const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(TransientMaterial);
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
