// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/MaterialGraph/DWCRevealSurfaceMaterialGraph.h"

#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"

namespace
{
    UTexture* LoadRevealSurfaceDefaultBlackTexture()
    {
        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
    }

    bool ConnectRevealSurfaceInput(
        const FDWCMaterialGraphPin& From,
        UMaterialExpression*        To,
        const TCHAR*                 ToInput,
        FString&                     OutFailureReason)
    {
        if (!From.IsValid() || To == nullptr ||
            !UMaterialEditingLibrary::ConnectMaterialExpressions(
                From.Expression, From.OutputName, To, ToInput))
        {
            OutFailureReason = FString::Printf(
                TEXT("Failed to connect Reveal Surface graph input '%s'."), ToInput);
            return false;
        }
        return true;
    }

    UMaterialExpressionTextureObjectParameter* CreateRevealSurfaceTextureParameter(
        UMaterial* Material,
        const FName ParameterName,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter =
            Cast<UMaterialExpressionTextureObjectParameter>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material,
                    UMaterialExpressionTextureObjectParameter::StaticClass(),
                    NodeX,
                    NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadRevealSurfaceDefaultBlackTexture();
        }
        return Parameter;
    }

    UMaterialExpressionScalarParameter* CreateRevealSurfaceScalarParameter(
        UMaterial* Material,
        const FName ParameterName,
        const float DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter =
            Cast<UMaterialExpressionScalarParameter>(
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

    UMaterialExpressionCustom* CreateRevealSurfaceCompositeExpression(
        UMaterial* Material,
        const int32 NodeX,
        const int32 NodeY,
        const FString& Description)
    {
        UMaterialExpressionCustom* Composite = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionCustom::StaticClass(), NodeX, NodeY));
        if (Composite == nullptr)
        {
            return nullptr;
        }

        static const FName InputNames[] = {
            TEXT("BaseColor"),
            TEXT("BaseNormal"),
            TEXT("SelectedUV"),
            TEXT("Visibility"),
            TEXT("RevealSurfaceMapTex"),
            TEXT("UseRevealSurfaceMap"),
            TEXT("MetallicDarkeningStrength"),
        };
        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Composite->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }

        FCustomOutput& NormalOutput = Composite->AdditionalOutputs.AddDefaulted_GetRef();
        NormalOutput.OutputName = TEXT("Normal");
        NormalOutput.OutputType = CMOT_Float3;

        Composite->OutputType = CMOT_Float3;
        Composite->Description = Description;
        Composite->Code = TEXT(R"(
float4 RevealSurface = Texture2DSampleLevel(
    RevealSurfaceMapTex,
    RevealSurfaceMapTexSampler,
    SelectedUV,
    0);
float RevealWeight = saturate(Visibility) * saturate(UseRevealSurfaceMap) * saturate(RevealSurface.a);

float2 RevealXY = RevealSurface.rg * 2.0 - 1.0;
float RevealZ = sqrt(saturate(1.0 - dot(RevealXY, RevealXY)));
float3 BaseTS = normalize(BaseNormal);
float3 RevealTS = normalize(float3(RevealXY, RevealZ));

// Same angle-corrected tangent-space composition convention used by the
// wrinkle preview: reveal surface first, then outer wrinkle detail.
Normal = normalize(float3(
    BaseTS.xy + RevealTS.xy * RevealWeight,
    BaseTS.z * lerp(1.0, RevealTS.z, RevealWeight)));

float MetallicDarkening = saturate(
    RevealSurface.b * max(MetallicDarkeningStrength, 0.0) * RevealWeight);
return max(BaseColor * (1.0 - MetallicDarkening), 0.0);
)");
        Composite->RebuildOutputs();
        return Composite;
    }
} // namespace

FDWCRevealSurfaceMaterialGraphResult FDWCRevealSurfaceMaterialGraph::Build(
    const FDWCRevealSurfaceMaterialGraphRequest& Request)
{
    FDWCRevealSurfaceMaterialGraphResult Result;
    if (Request.Material == nullptr || !Request.BaseColor.IsValid() ||
        !Request.BaseNormal.IsValid() || !Request.DataUV.IsValid() ||
        !Request.Visibility.IsValid() || Request.SurfaceTextureParameterName.IsNone() ||
        Request.UseSurfaceParameterName.IsNone() || Request.MetallicDarkeningParameterName.IsNone())
    {
        Result.FailureReason = TEXT("Reveal Surface graph request is missing a material, graph input, or parameter name.");
        return Result;
    }

    Result.SurfaceTextureParameter = CreateRevealSurfaceTextureParameter(
        Request.Material, Request.SurfaceTextureParameterName, Request.NodePosX, Request.NodePosY);
    Result.UseSurfaceParameter = CreateRevealSurfaceScalarParameter(
        Request.Material, Request.UseSurfaceParameterName, 0.0f, Request.NodePosX + 180, Request.NodePosY);
    Result.MetallicDarkeningParameter = CreateRevealSurfaceScalarParameter(
        Request.Material, Request.MetallicDarkeningParameterName, 0.25f, Request.NodePosX + 360, Request.NodePosY);
    UMaterialExpressionCustom* Composite = CreateRevealSurfaceCompositeExpression(
        Request.Material, Request.NodePosX + 560, Request.NodePosY,
        Request.Description.IsEmpty() ? TEXT("DWC Reveal Surface Composite") : Request.Description);

    if (Result.SurfaceTextureParameter == nullptr || Result.UseSurfaceParameter == nullptr ||
        Result.MetallicDarkeningParameter == nullptr || Composite == nullptr)
    {
        Result.FailureReason = TEXT("Could not create the Reveal Surface material expressions.");
        return Result;
    }

    bool bConnected = true;
    bConnected &= ConnectRevealSurfaceInput(Request.BaseColor, Composite, TEXT("BaseColor"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput(Request.BaseNormal, Composite, TEXT("BaseNormal"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput(Request.DataUV, Composite, TEXT("SelectedUV"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput(Request.Visibility, Composite, TEXT("Visibility"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput({ Result.SurfaceTextureParameter, FString() }, Composite, TEXT("RevealSurfaceMapTex"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput({ Result.UseSurfaceParameter, FString() }, Composite, TEXT("UseRevealSurfaceMap"), Result.FailureReason);
    bConnected &= ConnectRevealSurfaceInput({ Result.MetallicDarkeningParameter, FString() }, Composite, TEXT("MetallicDarkeningStrength"), Result.FailureReason);
    if (!bConnected)
    {
        return Result;
    }

    Result.BaseColor = { Composite, TEXT("return") };
    Result.Normal = { Composite, TEXT("Normal") };
    Result.bSucceeded = true;
    return Result;
}
