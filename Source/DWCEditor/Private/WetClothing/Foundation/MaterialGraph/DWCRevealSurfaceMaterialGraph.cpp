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

    UMaterialExpressionCustom* CreateRevealSurfaceNormalExpression(
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
            TEXT("BaseNormal"),
            TEXT("SelectedUV"),
            TEXT("Visibility"),
            TEXT("RevealTexture"),
            TEXT("UseRevealTexture"),
            TEXT("RevealStrength"),
            TEXT("ShowRevealNormal"),
            TEXT("VisualizationMode"),
        };
        for (const FName InputName : InputNames)
        {
            FCustomInput& Input = Composite->Inputs.AddDefaulted_GetRef();
            Input.InputName = InputName;
        }

        Composite->OutputType = CMOT_Float3;
        Composite->Description = Description;
        Composite->Code = TEXT(R"(
float4 RevealSample = Texture2DSampleLevel(
    RevealTexture,
    RevealTextureSampler,
    SelectedUV,
    0);
float RevealWeight =
    saturate(Visibility) * saturate(UseRevealTexture) * saturate(ShowRevealNormal) *
    saturate(RevealSample.a);

float2 RevealXY = (RevealSample.rg * 2.0 - 1.0) * max(RevealStrength, 0.0);
float RevealXYLengthSquared = dot(RevealXY, RevealXY);
if (RevealXYLengthSquared > 0.999)
{
    RevealXY *= sqrt(0.999 / RevealXYLengthSquared);
}
float RevealZ = sqrt(saturate(1.0 - dot(RevealXY, RevealXY)));
float3 BaseTS = normalize(BaseNormal);
float3 RevealTS = float3(RevealXY, RevealZ);
int SelectedVisualizationMode = (int)floor(VisualizationMode + 0.5);

if (SelectedVisualizationMode == 11 || SelectedVisualizationMode == 12)
{
    return BaseTS;
}
if (SelectedVisualizationMode == 10)
{
    BaseTS = float3(0.0, 0.0, 1.0);
}

// Same angle-corrected tangent-space composition convention used by the
// wrinkle preview: reveal surface first, then outer wrinkle detail.
return normalize(float3(
    BaseTS.xy + RevealTS.xy * RevealWeight,
    BaseTS.z * lerp(1.0, RevealTS.z, RevealWeight)));
)");
        Composite->RebuildOutputs();
        return Composite;
    }

    FDWCRevealSurfaceMaterialGraphResult BuildRevealNormalExpression(
        const FDWCRevealSurfaceMaterialGraphRequest& Request)
    {
        FDWCRevealSurfaceMaterialGraphResult Result;
        constexpr const TCHAR* ContractLabel = TEXT("editor Reveal Surface authoring");
        if (Request.Material == nullptr || !Request.BaseNormal.IsValid() ||
            !Request.DataUV.IsValid() || !Request.Visibility.IsValid() ||
            !Request.VisualizationMode.IsValid() ||
            Request.SurfaceTextureParameterName.IsNone() ||
            Request.UseSurfaceParameterName.IsNone() || Request.StrengthParameterName.IsNone() ||
            Request.ShowParameterName.IsNone())
        {
            Result.FailureReason = FString::Printf(
                TEXT("The %s graph request is missing a material, graph input, or parameter name."),
                ContractLabel);
            return Result;
        }

        Result.SurfaceTextureParameter = CreateRevealSurfaceTextureParameter(
            Request.Material,
            Request.SurfaceTextureParameterName,
            Request.NodePosX,
            Request.NodePosY);
        Result.UseSurfaceParameter = CreateRevealSurfaceScalarParameter(
            Request.Material,
            Request.UseSurfaceParameterName,
            0.0f,
            Request.NodePosX + 180,
            Request.NodePosY);
        Result.StrengthParameter = CreateRevealSurfaceScalarParameter(
            Request.Material,
            Request.StrengthParameterName,
            1.0f,
            Request.NodePosX + 180,
            Request.NodePosY + 100);
        Result.ShowParameter = CreateRevealSurfaceScalarParameter(
            Request.Material,
            Request.ShowParameterName,
            1.0f,
            Request.NodePosX + 180,
            Request.NodePosY + 200);
        const FString DefaultDescription = TEXT("DWC Editor Reveal Surface Preview Normal");
        UMaterialExpressionCustom* Composite = CreateRevealSurfaceNormalExpression(
            Request.Material,
            Request.NodePosX + 380,
            Request.NodePosY,
            Request.Description.IsEmpty() ? DefaultDescription : Request.Description);

        if (Result.SurfaceTextureParameter == nullptr ||
            Result.UseSurfaceParameter == nullptr || Result.StrengthParameter == nullptr ||
            Result.ShowParameter == nullptr || Composite == nullptr)
        {
            Result.FailureReason = FString::Printf(
                TEXT("Could not create the %s material expressions."), ContractLabel);
            return Result;
        }

        bool bConnected = true;
        bConnected &= ConnectRevealSurfaceInput(
            Request.BaseNormal, Composite, TEXT("BaseNormal"), Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            Request.DataUV, Composite, TEXT("SelectedUV"), Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            Request.Visibility, Composite, TEXT("Visibility"), Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            { Result.SurfaceTextureParameter, FString() },
            Composite,
            TEXT("RevealTexture"),
            Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            { Result.UseSurfaceParameter, FString() },
            Composite,
            TEXT("UseRevealTexture"),
            Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            { Result.StrengthParameter, FString() },
            Composite,
            TEXT("RevealStrength"),
            Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            { Result.ShowParameter, FString() },
            Composite,
            TEXT("ShowRevealNormal"),
            Result.FailureReason);
        bConnected &= ConnectRevealSurfaceInput(
            Request.VisualizationMode,
            Composite,
            TEXT("VisualizationMode"),
            Result.FailureReason);
        if (!bConnected)
        {
            return Result;
        }

        Result.Normal = { Composite, FString() };
        Result.bSucceeded = true;
        return Result;
    }
} // namespace

FDWCRevealSurfaceMaterialGraphResult FDWCRevealSurfaceMaterialGraph::BuildAuthoringPreview(
    const FDWCRevealSurfaceMaterialGraphRequest& Request)
{
    return BuildRevealNormalExpression(Request);
}
