//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetnessProfilePreviewMaterial.h"
#include "Utility/DWCLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace DWCWetnessProfilePreviewMaterial
{
namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");
    constexpr const TCHAR* PreviewMaterialAssetName = TEXT("M_DWC_WetnessProfilePreview");

    enum class EPreviewMaterialCreationState : uint8
    {
        NotAttempted,
        Creating,
        Succeeded,
        Failed,
    };

    EPreviewMaterialCreationState PreviewMaterialCreationState =
        EPreviewMaterialCreationState::NotAttempted;

    FString BuildPreviewMaterialPackageName()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DynamicWetClothesPluginName);
        if (!Plugin.IsValid())
        {
            return FString();
        }

        FString MountedAssetPath = Plugin->GetMountedAssetPath();
        MountedAssetPath.RemoveFromEnd(TEXT("/"));
        return FString::Printf(
            TEXT("%s/Editor/Preview/%s"),
            *MountedAssetPath,
            PreviewMaterialAssetName);
    }

    FString BuildPreviewMaterialObjectPath()
    {
        const FString PackageName = BuildPreviewMaterialPackageName();
        return PackageName.IsEmpty()
                   ? FString()
                   : FString::Printf(TEXT("%s.%s"), *PackageName, PreviewMaterialAssetName);
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

    UMaterialExpressionTextureObjectParameter* CreateMaskTextureParameter(
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
            Parameter->SamplerType = SAMPLERTYPE_LinearColor;
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

    bool BuildMaterialGraph(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        Material->BlendMode = BLEND_Opaque;
        Material->TwoSided = false;
        Material->SetShadingModel(MSM_DefaultLit);

        UMaterialExpressionScalarParameter* AbsorbedWater = CreateScalarParameter(
            Material, AbsorbedWaterParameter, 0.5f, -1250, -520);
        UMaterialExpressionScalarParameter* SurfaceWater = CreateScalarParameter(
            Material, SurfaceWaterParameter, 0.5f, -1250, -420);
        UMaterialExpressionScalarParameter* AbsorbedEnabled = CreateScalarParameter(
            Material, AbsorbedEnabledParameter, 1.0f, -1250, -320);
        UMaterialExpressionScalarParameter* SurfaceEnabled = CreateScalarParameter(
            Material, SurfaceEnabledParameter, 1.0f, -1250, -220);
        UMaterialExpressionScalarParameter* AbsorbedDarkeningStrength = CreateScalarParameter(
            Material, AbsorbedDarkeningStrengthParameter, 0.5f, -1250, -120);
        UMaterialExpressionScalarParameter* AbsorbedGlossinessStrength = CreateScalarParameter(
            Material, AbsorbedGlossinessStrengthParameter, 0.5f, -1250, -20);
        UMaterialExpressionScalarParameter* SurfaceTargetRoughness = CreateScalarParameter(
            Material, SurfaceTargetRoughnessParameter, 0.02f, -1250, 80);
        UMaterialExpressionScalarParameter* SurfaceNormalStrength = CreateScalarParameter(
            Material, SurfaceNormalStrengthParameter, 1.0f, -1250, 180);
        UMaterialExpressionScalarParameter* SurfaceRoughnessBlend = CreateScalarParameter(
            Material, SurfaceRoughnessBlendParameter, 1.0f, -1250, 280);
        UMaterialExpressionScalarParameter* SurfaceTotalStrength = CreateScalarParameter(
            Material, SurfaceTotalStrengthParameter, 0.5f, -1250, 380);
        UMaterialExpressionScalarParameter* SurfaceSpecular = CreateScalarParameter(
            Material, SurfaceSpecularParameter, 0.5f, -1250, 480);
        UMaterialExpressionScalarParameter* DropletsEnabled = CreateScalarParameter(
            Material, DropletsEnabledParameter, 1.0f, -1250, 580);
        UMaterialExpressionScalarParameter* DropletStampSize = CreateScalarParameter(
            Material, DropletStampSizeParameter, 16.0f, -1250, 680);
        UMaterialExpressionScalarParameter* DropletDetailSize = CreateScalarParameter(
            Material, DropletDetailSizeParameter, 1.0f, -1250, 780);
        UMaterialExpressionScalarParameter* DebugMode = CreateScalarParameter(
            Material, DebugModeParameter, 0.0f, -1250, 880);

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                -1250,
                1020));

        UTexture* DefaultNormalTexture = LoadDefaultNormalTexture();
        UTexture* DefaultMaskTexture = LoadDefaultMaskTexture();
        UMaterialExpressionTextureObjectParameter* DropletNormal = CreateNormalTextureParameter(
            Material, DropletNormalTextureParameter, DefaultNormalTexture, -1250, 1140);
        UMaterialExpressionTextureObjectParameter* DropletMask = CreateMaskTextureParameter(
            Material, DropletMaskTextureParameter, DefaultMaskTexture, -1250, 1260);

        UMaterialExpressionCustom* BaseColorExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Base Color"),
            TEXT(R"(
float Absorbed = saturate(saturate(AbsorbedWater) * saturate(AbsorbedEnabled) * clamp(AbsorbedDarkeningStrength, 0.0, 3.0));
float3 DryGray = float3(0.1, 0.1, 0.1);
float3 AbsorbedGray = DryGray * lerp(1.0, 0.45, Absorbed);
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Surface = EnabledSurface > 1.0e-4 ? 1.0 : 0.0;
float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float DropletMaskValue = saturate(Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r);
float Coverage = Surface * saturate(DropletsEnabled) * DropletMaskValue;
float ResponseSurface = (SurfaceWater > 1.0e-4 ? 1.0 : 0.0) * saturate(SurfaceEnabled);
float ResponseCoverage = ResponseSurface * saturate(DropletsEnabled) * DropletMaskValue;
float2 DropletXY = -(Texture2DSampleLevel(DropletNormalTex, DropletNormalTexSampler, DropletUV, 0).rg * 2.0 - 1.0);
float3 DropletNormalColor = normalize(float3(DropletXY, 1.0)) * 0.5 + 0.5;
float2 StampCell = frac(UV * 6.0) - 0.5;
float StampRadius = lerp(0.04, 0.42, saturate(sqrt(max(DropletStampSize, 1.0) / 64.0)));
float Stamp = 1.0 - smoothstep(StampRadius, StampRadius + 0.025, length(StampCell));
float Brush = DropletMaskValue;
float SpecularCue = saturate(SurfaceSpecular);
float BaseLuminance = dot(DryGray, float3(0.299, 0.587, 0.114));
float DarkSurfaceDamp = lerp(0.28, 1.0, smoothstep(0.05, 0.55, BaseLuminance));
float3 ClearWaterGray = lerp(AbsorbedGray, DryGray, 0.38 + 0.16 * SpecularCue);
float Edge = smoothstep(0.08, 0.48, Brush) * (1.0 - smoothstep(0.55, 0.96, Brush));
float Center = smoothstep(0.50, 1.0, Brush);
float CenterLift = 0.008 * Center * DarkSurfaceDamp;
float EdgeLift = (0.08 + 0.09 * SpecularCue) * Edge * DarkSurfaceDamp;
float3 WaterGlint = CenterLift + EdgeLift;
float LitBlend = ResponseCoverage * saturate(SurfaceTotalStrength) * (0.58 + 0.18 * SpecularCue);
float3 LitColor = saturate(lerp(AbsorbedGray, ClearWaterGray + WaterGlint, LitBlend));
float Mode = floor(DebugMode + 0.5);
if (Mode == 1.0) return lerp(float3(0.02, 0.02, 0.02), float3(0.05, 0.35, 1.0), saturate(AbsorbedWater) * saturate(AbsorbedEnabled));
if (Mode == 2.0) return lerp(float3(0.02, 0.02, 0.02), float3(0.0, 0.72, 1.0), Surface);
if (Mode == 3.0) return lerp(float3(0.02, 0.02, 0.02), float3(1.0, 0.85, 0.05), ResponseCoverage);
if (Mode == 4.0) return DropletNormalColor;
if (Mode == 5.0) return lerp(float3(0.02, 0.02, 0.02), float3(1.0, 0.15, 0.65), Stamp);
return LitColor;
)"),
            CMOT_Float3,
            {
                TEXT("UV"),
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("AbsorbedDarkeningStrength"),
                TEXT("SurfaceTotalStrength"),
                TEXT("SurfaceSpecular"),
                TEXT("DropletsEnabled"),
                TEXT("DropletStampSize"),
                TEXT("DropletDetailSize"),
                TEXT("DebugMode"),
                TEXT("DropletNormalTex"),
                TEXT("DropletMaskTex"),
            },
            -620,
            -360);

        UMaterialExpressionCustom* RoughnessExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Roughness"),
            TEXT(R"(
float Absorbed = saturate(AbsorbedWater) * saturate(AbsorbedEnabled);
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Surface = EnabledSurface > 1.0e-4 ? 1.0 : 0.0;
float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float DropletMaskValue = saturate(Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r);
float Coverage = Surface * saturate(DropletsEnabled) * DropletMaskValue;
float ResponseSurface = (SurfaceWater > 1.0e-4 ? 1.0 : 0.0) * saturate(SurfaceEnabled);
float ResponseCoverage = ResponseSurface * saturate(DropletsEnabled) * DropletMaskValue;
float AbsorbedRoughness = lerp(0.72, 0.52, saturate(Absorbed * clamp(AbsorbedGlossinessStrength, 0.0, 3.0)));
return saturate(lerp(AbsorbedRoughness, saturate(SurfaceTargetRoughness), saturate(ResponseCoverage * SurfaceRoughnessBlend * SurfaceTotalStrength)));
)"),
            CMOT_Float1,
            {
                TEXT("UV"),
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("AbsorbedGlossinessStrength"),
                TEXT("SurfaceTargetRoughness"),
                TEXT("SurfaceRoughnessBlend"),
                TEXT("SurfaceTotalStrength"),
                TEXT("DropletsEnabled"),
                TEXT("DropletDetailSize"),
                TEXT("DropletMaskTex"),
            },
            -620,
            80);

        UMaterialExpressionCustom* NormalExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Surface Normal"),
            TEXT(R"(
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Surface = EnabledSurface > 1.0e-4 ? 1.0 : 0.0;
float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float2 DropletXY = -(Texture2DSampleLevel(DropletNormalTex, DropletNormalTexSampler, DropletUV, 0).rg * 2.0 - 1.0);
float DropletMaskValue = saturate(Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r);
float ResponseSurface = (SurfaceWater > 1.0e-4 ? 1.0 : 0.0) * saturate(SurfaceEnabled);
float DropletWeight = ResponseSurface * saturate(DropletsEnabled) * DropletMaskValue * saturate(SurfaceTotalStrength);
float Strength = clamp(SurfaceNormalStrength, 0.0, 3.0);
float DropletVisualHeightBoost = 1.65;
float2 CombinedXY = DropletXY * DropletWeight * min(Strength * DropletVisualHeightBoost, 12.0);
return normalize(float3(CombinedXY, 1.0));
)"),
            CMOT_Float3,
            {
                TEXT("UV"),
                TEXT("SurfaceWater"),
                TEXT("SurfaceEnabled"),
                TEXT("SurfaceNormalStrength"),
                TEXT("SurfaceTotalStrength"),
                TEXT("DropletsEnabled"),
                TEXT("DropletDetailSize"),
                TEXT("DropletNormalTex"),
                TEXT("DropletMaskTex"),
            },
            -620,
            520);

        UMaterialExpressionCustom* SpecularExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Specular"),
            TEXT(R"(
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float Surface = EnabledSurface > 1.0e-4 ? 1.0 : 0.0;
float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float DropletMaskValue = saturate(Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r);
float Coverage = Surface * saturate(DropletsEnabled) * DropletMaskValue;
float ResponseSurface = (SurfaceWater > 1.0e-4 ? 1.0 : 0.0) * saturate(SurfaceEnabled);
float ResponseCoverage = ResponseSurface * saturate(DropletsEnabled) * DropletMaskValue;
return saturate(lerp(0.5, SurfaceSpecular, ResponseCoverage * saturate(SurfaceTotalStrength)));
)"),
            CMOT_Float1,
            {
                TEXT("UV"),
                TEXT("SurfaceWater"),
                TEXT("SurfaceEnabled"),
                TEXT("SurfaceSpecular"),
                TEXT("SurfaceTotalStrength"),
                TEXT("DropletsEnabled"),
                TEXT("DropletDetailSize"),
                TEXT("DropletMaskTex"),
            },
            -620,
            960);

        bool bConnected = true;
        bConnected &= ConnectExpression(TextureCoordinate, BaseColorExpression, TEXT("UV"));
        bConnected &= ConnectExpression(AbsorbedWater, BaseColorExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, BaseColorExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, BaseColorExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, BaseColorExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(AbsorbedDarkeningStrength, BaseColorExpression, TEXT("AbsorbedDarkeningStrength"));
        bConnected &= ConnectExpression(SurfaceTotalStrength, BaseColorExpression, TEXT("SurfaceTotalStrength"));
        bConnected &= ConnectExpression(SurfaceSpecular, BaseColorExpression, TEXT("SurfaceSpecular"));
        bConnected &= ConnectExpression(DropletsEnabled, BaseColorExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(DropletStampSize, BaseColorExpression, TEXT("DropletStampSize"));
        bConnected &= ConnectExpression(DropletDetailSize, BaseColorExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(DebugMode, BaseColorExpression, TEXT("DebugMode"));
        bConnected &= ConnectExpression(DropletNormal, BaseColorExpression, TEXT("DropletNormalTex"));
        bConnected &= ConnectExpression(DropletMask, BaseColorExpression, TEXT("DropletMaskTex"));

        bConnected &= ConnectExpression(TextureCoordinate, RoughnessExpression, TEXT("UV"));
        bConnected &= ConnectExpression(AbsorbedWater, RoughnessExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, RoughnessExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, RoughnessExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, RoughnessExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(AbsorbedGlossinessStrength, RoughnessExpression, TEXT("AbsorbedGlossinessStrength"));
        bConnected &= ConnectExpression(SurfaceTargetRoughness, RoughnessExpression, TEXT("SurfaceTargetRoughness"));
        bConnected &= ConnectExpression(SurfaceRoughnessBlend, RoughnessExpression, TEXT("SurfaceRoughnessBlend"));
        bConnected &= ConnectExpression(SurfaceTotalStrength, RoughnessExpression, TEXT("SurfaceTotalStrength"));
        bConnected &= ConnectExpression(DropletsEnabled, RoughnessExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(DropletDetailSize, RoughnessExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(DropletMask, RoughnessExpression, TEXT("DropletMaskTex"));

        bConnected &= ConnectExpression(TextureCoordinate, NormalExpression, TEXT("UV"));
        bConnected &= ConnectExpression(SurfaceWater, NormalExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(SurfaceEnabled, NormalExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(SurfaceNormalStrength, NormalExpression, TEXT("SurfaceNormalStrength"));
        bConnected &= ConnectExpression(SurfaceTotalStrength, NormalExpression, TEXT("SurfaceTotalStrength"));
        bConnected &= ConnectExpression(DropletsEnabled, NormalExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(DropletDetailSize, NormalExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(DropletNormal, NormalExpression, TEXT("DropletNormalTex"));
        bConnected &= ConnectExpression(DropletMask, NormalExpression, TEXT("DropletMaskTex"));

        bConnected &= ConnectExpression(TextureCoordinate, SpecularExpression, TEXT("UV"));
        bConnected &= ConnectExpression(SurfaceWater, SpecularExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(SurfaceEnabled, SpecularExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(SurfaceSpecular, SpecularExpression, TEXT("SurfaceSpecular"));
        bConnected &= ConnectExpression(SurfaceTotalStrength, SpecularExpression, TEXT("SurfaceTotalStrength"));
        bConnected &= ConnectExpression(DropletsEnabled, SpecularExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(DropletDetailSize, SpecularExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(DropletMask, SpecularExpression, TEXT("DropletMaskTex"));

        bConnected &= BaseColorExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpression, FString(), MP_BaseColor);
        bConnected &= RoughnessExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, FString(), MP_Roughness);
        bConnected &= NormalExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(NormalExpression, FString(), MP_Normal);
        bConnected &= SpecularExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(SpecularExpression, FString(), MP_Specular);
        return bConnected;
    }

    bool SaveMaterialAsset(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        UPackage* Package = Material->GetOutermost();
        if (Package == nullptr)
        {
            return false;
        }

        const FString Filename = FPackageName::LongPackageNameToFilename(
            Package->GetName(),
            FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);

        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Package, Material, *Filename, SaveArgs);
    }
}

UMaterialInterface* LoadBaseMaterial()
{
    const FString ObjectPath = BuildPreviewMaterialObjectPath();
    if (ObjectPath.IsEmpty())
    {
        return nullptr;
    }

    // Missing the optional generated asset is expected on the first run. Suppress the
    // normal LoadObject warning so the log only reports an actual creation failure.
    return Cast<UMaterialInterface>(StaticLoadObject(
        UMaterialInterface::StaticClass(),
        nullptr,
        *ObjectPath,
        nullptr,
        LOAD_NoWarn | LOAD_Quiet));
}

UMaterialInterface* LoadOrCreateBaseMaterial()
{
    if (UMaterialInterface* ExistingMaterial = LoadBaseMaterial())
    {
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Succeeded;
        return ExistingMaterial;
    }

    // Prevent recursive creation and repeated attempts after a failure. The viewport
    // safely falls back to the engine default material for the rest of this session.
    if (PreviewMaterialCreationState == EPreviewMaterialCreationState::Creating ||
        PreviewMaterialCreationState == EPreviewMaterialCreationState::Failed)
    {
        return nullptr;
    }

    if (!IsInGameThread() || IsRunningCommandlet() || IsGarbageCollecting() ||
        GEditor == nullptr || !FSlateApplication::IsInitialized())
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC: Wetness Profile preview material creation was requested in an unsafe editor state."));
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    }

    PreviewMaterialCreationState = EPreviewMaterialCreationState::Creating;

    const FString PackageName = BuildPreviewMaterialPackageName();
    const FString ObjectPath = BuildPreviewMaterialObjectPath();
    if (PackageName.IsEmpty() || ObjectPath.IsEmpty())
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DWC: Could not resolve the plugin mount path for the Wetness Profile preview material."));
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    }

    if (UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath))
    {
        if (UMaterialInterface* ExistingMaterial = Cast<UMaterialInterface>(ExistingObject))
        {
            PreviewMaterialCreationState = EPreviewMaterialCreationState::Succeeded;
            return ExistingMaterial;
        }

        UE_LOG(
            LogDWC,
            Error,
            TEXT("DWC: Preview material path '%s' is occupied by '%s' (%s)."),
            *ObjectPath,
            *GetNameSafe(ExistingObject),
            *GetNameSafe(ExistingObject->GetClass()));
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (Package == nullptr)
    {
        UE_LOG(LogDWC, Error, TEXT("DWC: Failed to create package '%s'."), *PackageName);
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    }

    UMaterial* Material = NewObject<UMaterial>(
        Package,
        PreviewMaterialAssetName,
        RF_Public | RF_Standalone | RF_Transactional);
    if (Material == nullptr)
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DWC: Failed to create persistent Wetness Profile preview material '%s'."),
            *ObjectPath);
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    }

    auto FailCreation = [&]() -> UMaterialInterface*
    {
        Material->ClearFlags(RF_Public | RF_Standalone);
        Material->MarkAsGarbage();
        PreviewMaterialCreationState = EPreviewMaterialCreationState::Failed;
        return nullptr;
    };

    if (!BuildMaterialGraph(Material))
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DWC: Failed to build one or more connections in '%s'."),
            *ObjectPath);
        return FailCreation();
    }

    // This is intentionally lazy: it runs only when the first Wetness Profile
    // viewport is opened and the persistent asset is missing. Module startup never
    // enters MaterialEditingLibrary, avoiding the early-initialization crash.
    const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
    if (!CompileErrors.IsEmpty())
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DWC: Failed to compile '%s':\n- %s"),
            *ObjectPath,
            *FString::Join(CompileErrors, TEXT("\n- ")));
        return FailCreation();
    }

    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    FAssetRegistryModule::AssetCreated(Material);

    Material->MarkPackageDirty();
    Package->MarkPackageDirty();

    if (!SaveMaterialAsset(Material))
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC: Created '%s' in memory but could not save it. The viewport remains usable for this editor session."),
            *ObjectPath);
    }

    PreviewMaterialCreationState = EPreviewMaterialCreationState::Succeeded;
    return Material;
}
} // namespace DWCWetnessProfilePreviewMaterial
