#include "WetnessProfilePreviewMaterial.h"

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
    constexpr const TCHAR* PreviewMaterialAssetName = TEXT("M_DWC_WetnessProfilePreviewV9");

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
        UMaterialExpressionScalarParameter* SurfaceVisibilityThreshold = CreateScalarParameter(
            Material, SurfaceVisibilityThresholdParameter, 0.25f, -1250, 380);
        UMaterialExpressionScalarParameter* DropletsEnabled = CreateScalarParameter(
            Material, DropletsEnabledParameter, 1.0f, -1250, 480);
        UMaterialExpressionScalarParameter* RivuletsEnabled = CreateScalarParameter(
            Material, RivuletsEnabledParameter, 1.0f, -1250, 580);
        UMaterialExpressionScalarParameter* RivuletScrollSpeed = CreateScalarParameter(
            Material, RivuletScrollSpeedParameter, 0.0f, -1250, 680);
        UMaterialExpressionScalarParameter* DropletDetailSize = CreateScalarParameter(
            Material, DropletDetailSizeParameter, 1.0f, -1250, 780);
        UMaterialExpressionScalarParameter* RivuletDetailSize = CreateScalarParameter(
            Material, RivuletDetailSizeParameter, 1.0f, -1250, 880);

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                -1250,
                900));
        UMaterialExpressionTime* Time = Cast<UMaterialExpressionTime>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTime::StaticClass(),
                -1250,
                1000));

        UTexture* DefaultNormalTexture = LoadDefaultNormalTexture();
        UTexture* DefaultMaskTexture = LoadDefaultMaskTexture();
        UMaterialExpressionTextureObjectParameter* DropletNormal = CreateNormalTextureParameter(
            Material,
            DropletNormalTextureParameter,
            DefaultNormalTexture,
            -1250,
            840);
        UMaterialExpressionTextureObjectParameter* RivuletNormal = CreateNormalTextureParameter(
            Material,
            RivuletNormalTextureParameter,
            DefaultNormalTexture,
            -1250,
            960);
        UMaterialExpressionTextureObjectParameter* DropletMask = CreateMaskTextureParameter(
            Material,
            DropletMaskTextureParameter,
            DefaultMaskTexture,
            -1250,
            1080);
        UMaterialExpressionTextureObjectParameter* RivuletMask = CreateMaskTextureParameter(
            Material,
            RivuletMaskTextureParameter,
            DefaultMaskTexture,
            -1250,
            1200);

        UMaterialExpressionCustom* BaseColorExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Base Color"),
            TEXT(R"(
float Absorbed = saturate(AbsorbedWater) * saturate(AbsorbedEnabled) * saturate(AbsorbedDarkeningStrength);
float3 DryGray = float3(0.1, 0.1, 0.1);
float3 AbsorbedGray = DryGray * lerp(1.0, 0.45, Absorbed);
return AbsorbedGray;
)"),
            CMOT_Float3,
            {
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("AbsorbedDarkeningStrength"),
                TEXT("SurfaceVisibilityThreshold"),
            },
            -620,
            -360);

        UMaterialExpressionCustom* RoughnessExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Roughness"),
            TEXT(R"(
float Absorbed = saturate(AbsorbedWater) * saturate(AbsorbedEnabled);
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float ThresholdMin = saturate(SurfaceVisibilityThreshold);
float ThresholdMax = min(ThresholdMin + 0.4, 1.0);
float Surface = smoothstep(ThresholdMin, ThresholdMax, EnabledSurface);
float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float2 RivuletUV = frac(UV / max(RivuletDetailSize, 1.0e-4) + float2(0.0, TimeValue * RivuletScrollSpeed * 0.08));
float DropletMask = Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r;
float RivuletMask = Texture2DSampleLevel(RivuletMaskTex, RivuletMaskTexSampler, RivuletUV, 0).r;
DropletMask = saturate(DropletMask);
RivuletMask = saturate(RivuletMask);
float Coverage = saturate(
    Surface * saturate(DropletsEnabled) * DropletMask +
    Surface * saturate(RivuletsEnabled) * RivuletMask);
float AbsorbedRoughness = lerp(0.72, 0.52, saturate(Absorbed * AbsorbedGlossinessStrength));
return saturate(lerp(AbsorbedRoughness, saturate(SurfaceTargetRoughness), saturate(Coverage * SurfaceRoughnessBlend)));
)"),
            CMOT_Float1,
            {
                TEXT("UV"),
                TEXT("TimeValue"),
                TEXT("AbsorbedWater"),
                TEXT("SurfaceWater"),
                TEXT("AbsorbedEnabled"),
                TEXT("SurfaceEnabled"),
                TEXT("AbsorbedGlossinessStrength"),
                TEXT("SurfaceTargetRoughness"),
                TEXT("SurfaceVisibilityThreshold"),
                TEXT("SurfaceRoughnessBlend"),
                TEXT("DropletsEnabled"),
                TEXT("RivuletsEnabled"),
                TEXT("RivuletScrollSpeed"),
                TEXT("DropletDetailSize"),
                TEXT("RivuletDetailSize"),
                TEXT("DropletMaskTex"),
                TEXT("RivuletMaskTex"),
            },
            -620,
            80);

        UMaterialExpressionCustom* NormalExpression = CreateCustomExpression(
            Material,
            TEXT("DWC Wetness Profile Preview Surface Normal"),
            TEXT(R"(
float EnabledSurface = saturate(SurfaceWater) * saturate(SurfaceEnabled);
float ThresholdMin = saturate(SurfaceVisibilityThreshold);
float ThresholdMax = min(ThresholdMin + 0.4, 1.0);
float Surface = smoothstep(ThresholdMin, ThresholdMax, EnabledSurface);

float2 DropletUV = frac(UV / max(DropletDetailSize, 1.0e-4));
float2 RivuletUV = frac(UV / max(RivuletDetailSize, 1.0e-4) + float2(0.0, TimeValue * RivuletScrollSpeed * 0.08));
float2 DropletXY = -(Texture2DSampleLevel(DropletNormalTex, DropletNormalTexSampler, DropletUV, 0).rg * 2.0 - 1.0);
float2 RivuletXY = -(Texture2DSampleLevel(RivuletNormalTex, RivuletNormalTexSampler, RivuletUV, 0).rg * 2.0 - 1.0);
float DropletMask = Texture2DSampleLevel(DropletMaskTex, DropletMaskTexSampler, DropletUV, 0).r;
float RivuletMask = Texture2DSampleLevel(RivuletMaskTex, RivuletMaskTexSampler, RivuletUV, 0).r;
DropletMask = saturate(DropletMask);
RivuletMask = saturate(RivuletMask);

float DropletWeight = Surface * saturate(DropletsEnabled) * DropletMask;
float RivuletWeight = Surface * saturate(RivuletsEnabled) * RivuletMask;
float Strength = clamp(SurfaceNormalStrength, 0.0, 8.0);
float DropletVisualHeightBoost = 1.65;
float2 CombinedXY = DropletXY * DropletWeight * min(Strength * DropletVisualHeightBoost, 12.0);
CombinedXY += RivuletXY * RivuletWeight * Strength;
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
                TEXT("DropletDetailSize"),
                TEXT("RivuletDetailSize"),
                TEXT("DropletNormalTex"),
                TEXT("RivuletNormalTex"),
                TEXT("DropletMaskTex"),
                TEXT("RivuletMaskTex"),
            },
            -620,
            520);

        bool bConnected = true;
        bConnected &= ConnectExpression(AbsorbedWater, BaseColorExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, BaseColorExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, BaseColorExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, BaseColorExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(AbsorbedDarkeningStrength, BaseColorExpression, TEXT("AbsorbedDarkeningStrength"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, BaseColorExpression, TEXT("SurfaceVisibilityThreshold"));

        bConnected &= ConnectExpression(TextureCoordinate, RoughnessExpression, TEXT("UV"));
        bConnected &= ConnectExpression(Time, RoughnessExpression, TEXT("TimeValue"));
        bConnected &= ConnectExpression(AbsorbedWater, RoughnessExpression, TEXT("AbsorbedWater"));
        bConnected &= ConnectExpression(SurfaceWater, RoughnessExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(AbsorbedEnabled, RoughnessExpression, TEXT("AbsorbedEnabled"));
        bConnected &= ConnectExpression(SurfaceEnabled, RoughnessExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(AbsorbedGlossinessStrength, RoughnessExpression, TEXT("AbsorbedGlossinessStrength"));
        bConnected &= ConnectExpression(SurfaceTargetRoughness, RoughnessExpression, TEXT("SurfaceTargetRoughness"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, RoughnessExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectExpression(SurfaceRoughnessBlend, RoughnessExpression, TEXT("SurfaceRoughnessBlend"));
        bConnected &= ConnectExpression(DropletsEnabled, RoughnessExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(RivuletsEnabled, RoughnessExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectExpression(RivuletScrollSpeed, RoughnessExpression, TEXT("RivuletScrollSpeed"));
        bConnected &= ConnectExpression(DropletDetailSize, RoughnessExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(RivuletDetailSize, RoughnessExpression, TEXT("RivuletDetailSize"));
        bConnected &= ConnectExpression(DropletMask, RoughnessExpression, TEXT("DropletMaskTex"));
        bConnected &= ConnectExpression(RivuletMask, RoughnessExpression, TEXT("RivuletMaskTex"));

        bConnected &= ConnectExpression(TextureCoordinate, NormalExpression, TEXT("UV"));
        bConnected &= ConnectExpression(Time, NormalExpression, TEXT("TimeValue"));
        bConnected &= ConnectExpression(SurfaceWater, NormalExpression, TEXT("SurfaceWater"));
        bConnected &= ConnectExpression(SurfaceEnabled, NormalExpression, TEXT("SurfaceEnabled"));
        bConnected &= ConnectExpression(SurfaceNormalStrength, NormalExpression, TEXT("SurfaceNormalStrength"));
        bConnected &= ConnectExpression(SurfaceVisibilityThreshold, NormalExpression, TEXT("SurfaceVisibilityThreshold"));
        bConnected &= ConnectExpression(DropletsEnabled, NormalExpression, TEXT("DropletsEnabled"));
        bConnected &= ConnectExpression(RivuletsEnabled, NormalExpression, TEXT("RivuletsEnabled"));
        bConnected &= ConnectExpression(RivuletScrollSpeed, NormalExpression, TEXT("RivuletScrollSpeed"));
        bConnected &= ConnectExpression(DropletDetailSize, NormalExpression, TEXT("DropletDetailSize"));
        bConnected &= ConnectExpression(RivuletDetailSize, NormalExpression, TEXT("RivuletDetailSize"));
        bConnected &= ConnectExpression(DropletNormal, NormalExpression, TEXT("DropletNormalTex"));
        bConnected &= ConnectExpression(RivuletNormal, NormalExpression, TEXT("RivuletNormalTex"));
        bConnected &= ConnectExpression(DropletMask, NormalExpression, TEXT("DropletMaskTex"));
        bConnected &= ConnectExpression(RivuletMask, NormalExpression, TEXT("RivuletMaskTex"));

        bConnected &= BaseColorExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpression, FString(), MP_BaseColor);
        bConnected &= RoughnessExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessExpression, FString(), MP_Roughness);
        bConnected &= NormalExpression != nullptr &&
                      UMaterialEditingLibrary::ConnectMaterialProperty(NormalExpression, FString(), MP_Normal);
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
            LogTemp,
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
            LogTemp,
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
            LogTemp,
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
        UE_LOG(LogTemp, Error, TEXT("DWC: Failed to create package '%s'."), *PackageName);
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
            LogTemp,
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
            LogTemp,
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
            LogTemp,
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
            LogTemp,
            Warning,
            TEXT("DWC: Created '%s' in memory but could not save it. The viewport remains usable for this editor session."),
            *ObjectPath);
    }
    else
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("DWC: Created persistent Wetness Profile preview material '%s'."),
            *ObjectPath);
    }

    PreviewMaterialCreationState = EPreviewMaterialCreationState::Succeeded;
    return Material;
}
} // namespace DWCWetnessProfilePreviewMaterial
