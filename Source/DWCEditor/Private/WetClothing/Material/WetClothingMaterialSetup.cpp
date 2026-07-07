#include "WetClothing/Material/WetClothingMaterialSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");

    FString BuildDwcMaterialFunctionPath(const TCHAR* FunctionName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DynamicWetClothesPluginName);
        if (!Plugin.IsValid())
        {
            return FString();
        }

        FString MountedAssetPath = Plugin->GetMountedAssetPath();
        MountedAssetPath.RemoveFromEnd(TEXT("/"));
        return FString::Printf(TEXT("%s/Materials/Functions/%s.%s"), *MountedAssetPath, FunctionName, FunctionName);
    }

    UMaterialFunctionInterface* LoadDwcMaterialFunction(const TCHAR* FunctionName, FString* OutObjectPath = nullptr)
    {
        const FString ObjectPath = BuildDwcMaterialFunctionPath(FunctionName);
        if (OutObjectPath != nullptr)
        {
            *OutObjectPath = ObjectPath;
        }

        return ObjectPath.IsEmpty() ? nullptr : LoadObject<UMaterialFunctionInterface>(nullptr, *ObjectPath);
    }

    bool HasFunctionCall(const UMaterial* Material, const UMaterialFunctionInterface* Function)
    {
        if (Material == nullptr || Function == nullptr)
        {
            return false;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            const UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (FunctionCall != nullptr && FunctionCall->MaterialFunction == Function)
            {
                return true;
            }
        }

        return false;
    }

    UMaterialExpressionMaterialFunctionCall* FindFunctionCall(UMaterial* Material, const UMaterialFunctionInterface* Function)
    {
        if (Material == nullptr || Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (FunctionCall != nullptr && FunctionCall->MaterialFunction == Function)
            {
                return FunctionCall;
            }
        }

        return nullptr;
    }

    bool HasInput(UMaterialExpression* Expression, const FString& InputName)
    {
        return UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression).Contains(InputName);
    }

    FString JoinPinNames(const TArray<FString>& PinNames)
    {
        FString Result;
        for (int32 Index = 0; Index < PinNames.Num(); ++Index)
        {
            if (Index > 0)
            {
                Result += TEXT(", ");
            }
            Result += PinNames[Index].IsEmpty() ? TEXT("<first>") : PinNames[Index];
        }
        return Result.IsEmpty() ? TEXT("<none>") : Result;
    }

    bool ResolveRequiredOutputName(UMaterialExpression* Expression, const FString& OutputName, FString& OutResolvedOutputName)
    {
        const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
        if (OutputNames.Contains(OutputName))
        {
            OutResolvedOutputName = OutputName;
            return true;
        }

        OutResolvedOutputName.Reset();
        return false;
    }

    UMaterialExpressionMaterialFunctionCall* CreateFunctionCall(UMaterial* Material, UMaterialFunctionInterface* Function, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMaterialFunctionCall::StaticClass(), NodePosX, NodePosY));
        if (FunctionCall == nullptr || !FunctionCall->SetMaterialFunction(Function))
        {
            return nullptr;
        }

        FunctionCall->UpdateFromFunctionResource();
        return FunctionCall;
    }

    UMaterialExpression* ResolveMaterialPropertyInputOrFallback(UMaterial* Material, EMaterialProperty Property, const FVector2D& NodePosition, FString& OutOutputName)
    {
        UMaterialExpression* ExistingExpression = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, Property);
        if (ExistingExpression != nullptr)
        {
            OutOutputName = UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, Property);
            return ExistingExpression;
        }

        if (Property == MP_BaseColor)
        {
            UMaterialExpressionConstant3Vector* Fallback = Cast<UMaterialExpressionConstant3Vector>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), NodePosition.X, NodePosition.Y));
            if (Fallback != nullptr)
            {
                Fallback->Constant = FLinearColor::White;
            }
            OutOutputName.Reset();
            return Fallback;
        }

        if (Property == MP_Normal)
        {
            UMaterialExpressionConstant3Vector* Fallback = Cast<UMaterialExpressionConstant3Vector>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), NodePosition.X, NodePosition.Y));
            if (Fallback != nullptr)
            {
                Fallback->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
            }
            OutOutputName.Reset();
            return Fallback;
        }

        UMaterialExpressionConstant* Fallback = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), NodePosition.X, NodePosition.Y));
        if (Fallback != nullptr)
        {
            Fallback->R = 0.5f;
        }
        OutOutputName.Reset();
        return Fallback;
    }

    UMaterialExpressionScalarParameter* CreateScalarParameter(UMaterial* Material, const FName ParameterName, float DefaultValue, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), NodePosX, NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionScalarParameter* FindScalarParameter(UMaterial* Material, const FName ParameterName)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }

        return nullptr;
    }

    UMaterialExpressionScalarParameter* FindOrCreateScalarParameter(UMaterial* Material, const FName ParameterName, float DefaultValue, int32 NodePosX, int32 NodePosY)
    {
        if (UMaterialExpressionScalarParameter* ExistingParameter = FindScalarParameter(Material, ParameterName))
        {
            return ExistingParameter;
        }

        return CreateScalarParameter(Material, ParameterName, DefaultValue, NodePosX, NodePosY);
    }

    UMaterialExpressionVectorParameter* FindVectorParameter(UMaterial* Material, const FName ParameterName)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionVectorParameter* Parameter = Cast<UMaterialExpressionVectorParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }

        return nullptr;
    }

    UMaterialExpressionVectorParameter* CreateVectorParameter(UMaterial* Material, const FName ParameterName, const FLinearColor& DefaultValue, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionVectorParameter* Parameter = Cast<UMaterialExpressionVectorParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), NodePosX, NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionVectorParameter* FindOrCreateVectorParameter(
        UMaterial*          Material,
        const FName         ParameterName,
        const FLinearColor& DefaultValue,
        int32               NodePosX,
        int32               NodePosY)
    {
        if (UMaterialExpressionVectorParameter* ExistingParameter = FindVectorParameter(Material, ParameterName))
        {
            return ExistingParameter;
        }

        return CreateVectorParameter(Material, ParameterName, DefaultValue, NodePosX, NodePosY);
    }

    UTexture* LoadDefaultNormalTexture()
    {
        if (UTexture* DefaultNormal = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return DefaultNormal;
        }

        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

    UMaterialExpressionTextureObjectParameter* FindTextureObjectParameter(UMaterial* Material, const FName ParameterName)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }

        return nullptr;
    }

    UMaterialExpressionTextureObjectParameter* CreateTextureObjectParameter(UMaterial* Material, const FName ParameterName, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionTextureObjectParameter* Parameter = Cast<UMaterialExpressionTextureObjectParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass(), NodePosX, NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_LinearColor;
            Parameter->Texture = LoadDefaultNormalTexture();
            Parameter->Desc = TEXT("DWC preview wrinkle height map.");
        }
        return Parameter;
    }

    UMaterialExpressionTextureObjectParameter* FindOrCreateTextureObjectParameter(UMaterial* Material, const FName ParameterName, int32 NodePosX, int32 NodePosY)
    {
        if (UMaterialExpressionTextureObjectParameter* ExistingParameter = FindTextureObjectParameter(Material, ParameterName))
        {
            ExistingParameter->SamplerType = SAMPLERTYPE_LinearColor;
            if (ExistingParameter->Texture == nullptr)
            {
                ExistingParameter->Texture = LoadDefaultNormalTexture();
            }
            return ExistingParameter;
        }

        return CreateTextureObjectParameter(Material, ParameterName, NodePosX, NodePosY);
    }

    UMaterialExpressionTextureSampleParameter2D* FindTextureSampleParameter(UMaterial* Material, const FName ParameterName)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }

        return nullptr;
    }

    UMaterialExpressionTextureSampleParameter2D* CreateTextureSampleParameter(UMaterial* Material, const FName ParameterName, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), NodePosX, NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Normal;
            Parameter->Texture = LoadDefaultNormalTexture();
            Parameter->Desc = TEXT("DWC baked wrinkle tangent-space normal map.");
        }
        return Parameter;
    }

    UMaterialExpressionTextureSampleParameter2D* FindOrCreateTextureSampleParameter(UMaterial* Material, const FName ParameterName, int32 NodePosX, int32 NodePosY)
    {
        if (UMaterialExpressionTextureSampleParameter2D* ExistingParameter = FindTextureSampleParameter(Material, ParameterName))
        {
            ExistingParameter->SamplerType = SAMPLERTYPE_Normal;
            if (ExistingParameter->Texture == nullptr)
            {
                ExistingParameter->Texture = LoadDefaultNormalTexture();
            }
            return ExistingParameter;
        }

        return CreateTextureSampleParameter(Material, ParameterName, NodePosX, NodePosY);
    }

    UMaterialExpressionTextureCoordinate* FindTextureCoordinate(UMaterial* Material, int32 CoordinateIndex)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            if (TextureCoordinate != nullptr && TextureCoordinate->CoordinateIndex == CoordinateIndex)
            {
                return TextureCoordinate;
            }
        }

        return nullptr;
    }

    UMaterialExpressionTextureCoordinate* FindOrCreateTextureCoordinate(UMaterial* Material, int32 CoordinateIndex, int32 NodePosX, int32 NodePosY)
    {
        if (UMaterialExpressionTextureCoordinate* ExistingCoordinate = FindTextureCoordinate(Material, CoordinateIndex))
        {
            return ExistingCoordinate;
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), NodePosX, NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = CoordinateIndex;
            TextureCoordinate->UTiling = 1.0f;
            TextureCoordinate->VTiling = 1.0f;
            TextureCoordinate->Desc = FString::Printf(TEXT("DWC Preview UV%d"), CoordinateIndex);
        }
        return TextureCoordinate;
    }

    UMaterialExpressionCustom* FindPreviewBrushCustomExpression(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom != nullptr && Custom->Description == TEXT("DWC Preview Brush Normal Blend"))
            {
                return Custom;
            }
        }

        return nullptr;
    }

    void ConfigurePreviewBrushCustomInputs(UMaterialExpressionCustom* Custom)
    {
        if (Custom == nullptr)
        {
            return;
        }

        static const FName InputNames[] = {
            TEXT("BaseNormal"),
            TEXT("UV0"),
            TEXT("UV1"),
            TEXT("UV2"),
            TEXT("UV3"),
            TEXT("UV4"),
            TEXT("UV5"),
            TEXT("UV6"),
            TEXT("UV7"),
            TEXT("PreviewBrushEnabled"),
            TEXT("PreviewBrushCenterUV"),
            TEXT("PreviewBrushRadiusUV"),
            TEXT("PreviewBrushStrength"),
            TEXT("PreviewBrushFalloff"),
            TEXT("PreviewBrushRotation"),
            TEXT("PreviewBrushScale"),
            TEXT("PreviewBrushTexelSize"),
            TEXT("PreviewBrushUVChannel"),
            TEXT("PreviewBrushHeightTex"),
        };

        Custom->Inputs.Reset();
        for (const FName& InputName : InputNames)
        {
            FCustomInput& NewInput = Custom->Inputs.AddDefaulted_GetRef();
            NewInput.InputName = InputName;
        }

        Custom->Code = TEXT(R"(
float3 BaseTS = normalize(BaseNormal);
if (PreviewBrushEnabled <= 0.5 || PreviewBrushRadiusUV <= 0.000001)
{
    return BaseTS;
}

int PreviewUVIndex = (int)round(clamp(PreviewBrushUVChannel, 0.0, 7.0));
float2 SelectedUV = UV0;
if (PreviewUVIndex == 1) { SelectedUV = UV1; }
else if (PreviewUVIndex == 2) { SelectedUV = UV2; }
else if (PreviewUVIndex == 3) { SelectedUV = UV3; }
else if (PreviewUVIndex == 4) { SelectedUV = UV4; }
else if (PreviewUVIndex == 5) { SelectedUV = UV5; }
else if (PreviewUVIndex == 6) { SelectedUV = UV6; }
else if (PreviewUVIndex == 7) { SelectedUV = UV7; }

float2 SafeScale = max(abs(PreviewBrushScale.xy), float2(0.0001, 0.0001));
float2 DeltaUV = SelectedUV - PreviewBrushCenterUV.xy;
float CosRotation = cos(PreviewBrushRotation);
float SinRotation = sin(PreviewBrushRotation);
float2 LocalBrush = float2(
    (CosRotation * DeltaUV.x + SinRotation * DeltaUV.y) / (PreviewBrushRadiusUV * SafeScale.x),
    (-SinRotation * DeltaUV.x + CosRotation * DeltaUV.y) / (PreviewBrushRadiusUV * SafeScale.y));

float DistanceFromCenter = length(LocalBrush);
if (DistanceFromCenter >= 1.0)
{
    return BaseTS;
}

float EdgeFadeStart = clamp(1.0 - PreviewBrushFalloff, 0.0, 0.98);
float EdgeFade = 1.0 - smoothstep(EdgeFadeStart, 1.0, DistanceFromCenter);
float2 BrushUV = LocalBrush * 0.5 + 0.5;
float2 TexelSize = max(abs(PreviewBrushTexelSize.xy), float2(0.0001, 0.0001));

float3 HeightLeftColor = Texture2DSampleLevel(PreviewBrushHeightTex, PreviewBrushHeightTexSampler, saturate(BrushUV - float2(TexelSize.x, 0.0)), 0).rgb;
float3 HeightRightColor = Texture2DSampleLevel(PreviewBrushHeightTex, PreviewBrushHeightTexSampler, saturate(BrushUV + float2(TexelSize.x, 0.0)), 0).rgb;
float3 HeightDownColor = Texture2DSampleLevel(PreviewBrushHeightTex, PreviewBrushHeightTexSampler, saturate(BrushUV - float2(0.0, TexelSize.y)), 0).rgb;
float3 HeightUpColor = Texture2DSampleLevel(PreviewBrushHeightTex, PreviewBrushHeightTexSampler, saturate(BrushUV + float2(0.0, TexelSize.y)), 0).rgb;

float HeightLeft = dot(HeightLeftColor, float3(0.33333334, 0.33333334, 0.33333334));
float HeightRight = dot(HeightRightColor, float3(0.33333334, 0.33333334, 0.33333334));
float HeightDown = dot(HeightDownColor, float3(0.33333334, 0.33333334, 0.33333334));
float HeightUp = dot(HeightUpColor, float3(0.33333334, 0.33333334, 0.33333334));

float2 HeightGradient = float2(HeightRight - HeightLeft, HeightUp - HeightDown);
float PreviewStrength = saturate(PreviewBrushStrength) * EdgeFade * 12.0;
float3 PreviewTS = normalize(float3(-HeightGradient.x * PreviewStrength, -HeightGradient.y * PreviewStrength, 1.0));
return normalize(float3(BaseTS.xy + PreviewTS.xy, max(BaseTS.z * PreviewTS.z, 0.0001)));
)");
        Custom->OutputType = CMOT_Float3;
        Custom->Description = TEXT("DWC Preview Brush Normal Blend");
#if WITH_EDITOR
        Custom->RebuildOutputs();
#endif
    }

    UMaterialExpressionCustom* FindOrCreatePreviewBrushCustomExpression(UMaterial* Material, int32 NodePosX, int32 NodePosY)
    {
        UMaterialExpressionCustom* Custom = FindPreviewBrushCustomExpression(Material);
        if (Custom == nullptr)
        {
            Custom = Cast<UMaterialExpressionCustom>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), NodePosX, NodePosY));
        }

        ConfigurePreviewBrushCustomInputs(Custom);
        return Custom;
    }

    bool HasPreviewBrushSupport(const UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        bool bHasEnabledParameter = false;
        bool bHasCenterParameter = false;
        bool bHasHeightTextureParameter = false;
        bool bHasCustomExpression = false;

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
            {
                if (Scalar->ParameterName == TEXT("DWC_PreviewBrushEnabled"))
                {
                    bHasEnabledParameter = true;
                }
            }
            else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
            {
                if (Vector->ParameterName == TEXT("DWC_PreviewBrushCenterUV"))
                {
                    bHasCenterParameter = true;
                }
            }
            else if (const UMaterialExpressionTextureObjectParameter* TextureObject = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
            {
                if (TextureObject->ParameterName == TEXT("DWC_PreviewBrushHeightTex"))
                {
                    bHasHeightTextureParameter = true;
                }
            }
            else if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
            {
                if (Custom->Description == TEXT("DWC Preview Brush Normal Blend"))
                {
                    bHasCustomExpression = true;
                }
            }
        }

        return bHasEnabledParameter && bHasCenterParameter && bHasHeightTextureParameter && bHasCustomExpression;
    }

    UMaterial* LoadExistingDwcMaterialForSource(const UMaterial* SourceMaterial)
    {
        if (SourceMaterial == nullptr)
        {
            return nullptr;
        }

        const FString SourcePackageName = SourceMaterial->GetOutermost()->GetName();
        if (SourcePackageName.EndsWith(TEXT("_DWC")))
        {
            return const_cast<UMaterial*>(SourceMaterial);
        }

        const FString DwcPackageName = SourcePackageName + TEXT("_DWC");
        const FString DwcAssetName = FPackageName::GetLongPackageAssetName(DwcPackageName);
        const FString DwcObjectPath = DwcPackageName + TEXT(".") + DwcAssetName;
        return LoadObject<UMaterial>(nullptr, *DwcObjectPath);
    }

    FString BuildDwcPackageNameForSourceInterface(const UMaterialInterface* SourceMaterialInterface, const UMaterial* FallbackParentMaterial)
    {
        FString SourcePackageName;
        if (SourceMaterialInterface != nullptr && SourceMaterialInterface->GetOutermost() != nullptr && SourceMaterialInterface->GetOutermost() != GetTransientPackage())
        {
            SourcePackageName = SourceMaterialInterface->GetOutermost()->GetName();
        }

        if (SourcePackageName.IsEmpty() && FallbackParentMaterial != nullptr)
        {
            SourcePackageName = FallbackParentMaterial->GetOutermost()->GetName() + TEXT("_") + SourceMaterialInterface->GetName();
        }

        if (SourcePackageName.EndsWith(TEXT("_DWC")))
        {
            return SourcePackageName;
        }

        return SourcePackageName + TEXT("_DWC");
    }

    UMaterialInstanceConstant* LoadExistingDwcMaterialInstanceForSource(const UMaterialInstance* SourceInstance, const UMaterial* FallbackParentMaterial)
    {
        const FString DwcPackageName = BuildDwcPackageNameForSourceInterface(SourceInstance, FallbackParentMaterial);
        if (DwcPackageName.IsEmpty())
        {
            return nullptr;
        }

        const FString DwcAssetName = FPackageName::GetLongPackageAssetName(DwcPackageName);
        const FString DwcObjectPath = DwcPackageName + TEXT(".") + DwcAssetName;
        return LoadObject<UMaterialInstanceConstant>(nullptr, *DwcObjectPath);
    }

    void CopyMaterialInstanceOverrides(const UMaterialInstance* SourceInstance, UMaterialInstanceConstant* TargetInstance, UMaterialInterface* WetParent)
    {
        check(SourceInstance != nullptr);
        check(TargetInstance != nullptr);

        TargetInstance->Modify();
        TargetInstance->SetParentEditorOnly(WetParent);

        TargetInstance->ScalarParameterValues = SourceInstance->ScalarParameterValues;
        TargetInstance->VectorParameterValues = SourceInstance->VectorParameterValues;
        TargetInstance->DoubleVectorParameterValues = SourceInstance->DoubleVectorParameterValues;
        TargetInstance->TextureParameterValues = SourceInstance->TextureParameterValues;
        TargetInstance->TextureCollectionParameterValues = SourceInstance->TextureCollectionParameterValues;
        TargetInstance->ParameterCollectionParameterValues = SourceInstance->ParameterCollectionParameterValues;
        TargetInstance->RuntimeVirtualTextureParameterValues = SourceInstance->RuntimeVirtualTextureParameterValues;
        TargetInstance->SparseVolumeTextureParameterValues = SourceInstance->SparseVolumeTextureParameterValues;
        TargetInstance->FontParameterValues = SourceInstance->FontParameterValues;
        TargetInstance->UserSceneTextureOverrides = SourceInstance->UserSceneTextureOverrides;

        FStaticParameterSet                    StaticParameters = SourceInstance->GetStaticParameters();
        FMaterialInstanceBasePropertyOverrides BasePropertyOverrides = SourceInstance->BasePropertyOverrides;
        TargetInstance->SetPermutationParameters(StaticParameters, BasePropertyOverrides);
        TargetInstance->UpdateStaticPermutation();
        TargetInstance->PostEditChange();
        TargetInstance->MarkPackageDirty();
    }

    UMaterialInstanceConstant* CreateOrUpdateDwcMaterialInstanceForSource(
        const UMaterialInstance* SourceInstance,
        UMaterialInterface*      WetParent,
        const UMaterial*         FallbackParentMaterial,
        FString&                 OutErrorMessage,
        bool&                    bOutReusedExisting)
    {
        bOutReusedExisting = false;

        if (SourceInstance == nullptr || WetParent == nullptr)
        {
            OutErrorMessage = TEXT("Material instance setup requires a source instance and wet parent material.");
            return nullptr;
        }

        if (UMaterialInstanceConstant* ExistingInstance = LoadExistingDwcMaterialInstanceForSource(SourceInstance, FallbackParentMaterial))
        {
            bOutReusedExisting = true;
            CopyMaterialInstanceOverrides(SourceInstance, ExistingInstance, WetParent);
            return ExistingInstance;
        }

        FString DwcPackageName = BuildDwcPackageNameForSourceInterface(SourceInstance, FallbackParentMaterial);
        if (DwcPackageName.IsEmpty())
        {
            OutErrorMessage = FString::Printf(TEXT("Could not determine a package path for '%s'."), *GetNameSafe(SourceInstance));
            return nullptr;
        }

        if (FindObject<UObject>(nullptr, *(DwcPackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(DwcPackageName))) != nullptr)
        {
            FString            UniquePackageName;
            FString            UniqueAssetName;
            FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
            AssetToolsModule.Get().CreateUniqueAssetName(DwcPackageName, FString(), UniquePackageName, UniqueAssetName);
            DwcPackageName = UniquePackageName;
        }

        const FString DwcAssetName = FPackageName::GetLongPackageAssetName(DwcPackageName);
        UPackage*     Package = CreatePackage(*DwcPackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not create package '%s'."), *DwcPackageName);
            return nullptr;
        }

        UMaterialInstanceConstant* NewInstance = NewObject<UMaterialInstanceConstant>(
            Package,
            *DwcAssetName,
            RF_Public | RF_Standalone | RF_Transactional);
        if (NewInstance == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not create wet material instance '%s'."), *DwcAssetName);
            return nullptr;
        }

        CopyMaterialInstanceOverrides(SourceInstance, NewInstance, WetParent);
        FAssetRegistryModule::AssetCreated(NewInstance);
        Package->MarkPackageDirty();
        return NewInstance;
    }

    bool ConnectChecked(UMaterialExpression* FromExpression, const FString& FromOutputName, UMaterialExpression* ToExpression, const FString& ToInputName, TArray<FString>& FailureReasons)
    {
        if (!HasInput(ToExpression, ToInputName))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing input '%s' on %s. Available inputs: %s"),
                                               *ToInputName,
                                               *GetNameSafe(ToExpression),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionInputNames(ToExpression))));
            return false;
        }

        if (!UMaterialEditingLibrary::ConnectMaterialExpressions(FromExpression, FromOutputName, ToExpression, ToInputName))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect %s output '%s' to %s input '%s'. Source outputs: %s"),
                                               *GetNameSafe(FromExpression),
                                               FromOutputName.IsEmpty() ? TEXT("<first>") : *FromOutputName,
                                               *GetNameSafe(ToExpression),
                                               *ToInputName,
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(FromExpression))));
            return false;
        }

        return true;
    }

    bool ConnectDwcApplyWetnessNormalGraph(
        UMaterial*                           Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        TArray<FString>&                     FailureReasons)
    {
        if (Material == nullptr || ApplyCall == nullptr)
        {
            FailureReasons.Add(TEXT("Normal setup requires a material and MF_DWC_ApplyWetness call."));
            return false;
        }

        FString              BaseNormalOutputName;
        UMaterialExpression* BaseNormalInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Normal, FVector2D(-900.0f, 500.0f), BaseNormalOutputName);
        if (BaseNormalInput == ApplyCall)
        {
            BaseNormalInput = Cast<UMaterialExpressionConstant3Vector>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -900, 500));
            if (UMaterialExpressionConstant3Vector* FlatNormal = Cast<UMaterialExpressionConstant3Vector>(BaseNormalInput))
            {
                FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
            }
            BaseNormalOutputName.Reset();
        }

        UMaterialExpressionTextureSampleParameter2D* WrinkleNormalMap = FindOrCreateTextureSampleParameter(
            Material,
            TEXT("DWC_WrinkleNormalMap"),
            -900,
            670);
        UMaterialExpressionScalarParameter* UseWrinkleNormalMap = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_UseWrinkleNormalMap"),
            0.0f,
            -900,
            840);
        UMaterialExpressionScalarParameter* WrinkleStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WrinkleStrength"),
            1.0f,
            -900,
            930);
        UMaterialExpressionScalarParameter* WrinkleWetnessMin = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WrinkleWetnessMin"),
            0.25f,
            -900,
            1020);
        UMaterialExpressionScalarParameter* WrinkleWetnessMax = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WrinkleWetnessMax"),
            1.0f,
            -900,
            1110);
        UMaterialExpressionScalarParameter* PreviewBrushEnabled = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushEnabled"),
            0.0f,
            -520,
            1220);
        UMaterialExpressionVectorParameter* PreviewBrushCenterUV = FindOrCreateVectorParameter(
            Material,
            TEXT("DWC_PreviewBrushCenterUV"),
            FLinearColor::Black,
            -520,
            1310);
        UMaterialExpressionScalarParameter* PreviewBrushRadiusUV = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushRadiusUV"),
            0.025f,
            -520,
            1400);
        UMaterialExpressionScalarParameter* PreviewBrushStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushStrength"),
            1.0f,
            -520,
            1490);
        UMaterialExpressionScalarParameter* PreviewBrushFalloff = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushFalloff"),
            0.5f,
            -520,
            1580);
        UMaterialExpressionScalarParameter* PreviewBrushRotation = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushRotation"),
            0.0f,
            -520,
            1670);
        UMaterialExpressionVectorParameter* PreviewBrushScale = FindOrCreateVectorParameter(
            Material,
            TEXT("DWC_PreviewBrushScale"),
            FLinearColor(1.0f, 1.0f, 0.0f, 0.0f),
            -520,
            1760);
        UMaterialExpressionVectorParameter* PreviewBrushTexelSize = FindOrCreateVectorParameter(
            Material,
            TEXT("DWC_PreviewBrushTexelSize"),
            FLinearColor(1.0f / 512.0f, 1.0f / 512.0f, 0.0f, 0.0f),
            -520,
            1850);
        UMaterialExpressionScalarParameter* PreviewBrushUVChannel = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_PreviewBrushUVChannel"),
            0.0f,
            -520,
            1940);
        UMaterialExpressionTextureObjectParameter* PreviewBrushHeightTexture = FindOrCreateTextureObjectParameter(
            Material,
            TEXT("DWC_PreviewBrushHeightTex"),
            -520,
            2030);
        UMaterialExpressionCustom* PreviewBrushBlend = FindOrCreatePreviewBrushCustomExpression(Material, -60, 1360);

        TArray<UMaterialExpressionTextureCoordinate*> PreviewUVCoordinates;
        PreviewUVCoordinates.SetNum(8);
        for (int32 CoordinateIndex = 0; CoordinateIndex < PreviewUVCoordinates.Num(); ++CoordinateIndex)
        {
            PreviewUVCoordinates[CoordinateIndex] = FindOrCreateTextureCoordinate(Material, CoordinateIndex, -520, 2140 + CoordinateIndex * 90);
        }

        const bool bCreatedRequiredNodes = BaseNormalInput != nullptr &&
                                           WrinkleNormalMap != nullptr &&
                                           UseWrinkleNormalMap != nullptr &&
                                           WrinkleStrength != nullptr &&
                                           WrinkleWetnessMin != nullptr &&
                                           WrinkleWetnessMax != nullptr &&
                                           PreviewBrushEnabled != nullptr &&
                                           PreviewBrushCenterUV != nullptr &&
                                           PreviewBrushRadiusUV != nullptr &&
                                           PreviewBrushStrength != nullptr &&
                                           PreviewBrushFalloff != nullptr &&
                                           PreviewBrushRotation != nullptr &&
                                           PreviewBrushScale != nullptr &&
                                           PreviewBrushTexelSize != nullptr &&
                                           PreviewBrushUVChannel != nullptr &&
                                           PreviewBrushHeightTexture != nullptr &&
                                           PreviewBrushBlend != nullptr &&
                                           !PreviewUVCoordinates.Contains(nullptr);
        if (!bCreatedRequiredNodes)
        {
            FailureReasons.Add(TEXT("DWC material setup could not create one or more wrinkle normal nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseNormalInput, BaseNormalOutputName, ApplyCall, TEXT("BaseNormal"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleNormalMap, TEXT("RGB"), ApplyCall, TEXT("WrinkleNormal"), FailureReasons);
        bConnected &= ConnectChecked(UseWrinkleNormalMap, FString(), ApplyCall, TEXT("UseWrinkleNormalMap"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleStrength, FString(), ApplyCall, TEXT("WrinkleStrength"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMin, FString(), ApplyCall, TEXT("WrinkleWetnessMin"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMax, FString(), ApplyCall, TEXT("WrinkleWetnessMax"), FailureReasons);

        FString ApplyNormalOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Normal"), ApplyNormalOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Normal' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            return false;
        }

        bConnected &= ConnectChecked(ApplyCall, ApplyNormalOutput, PreviewBrushBlend, TEXT("BaseNormal"), FailureReasons);
        for (int32 CoordinateIndex = 0; CoordinateIndex < PreviewUVCoordinates.Num(); ++CoordinateIndex)
        {
            bConnected &= ConnectChecked(
                PreviewUVCoordinates[CoordinateIndex],
                FString(),
                PreviewBrushBlend,
                FString::Printf(TEXT("UV%d"), CoordinateIndex),
                FailureReasons);
        }
        bConnected &= ConnectChecked(PreviewBrushEnabled, FString(), PreviewBrushBlend, TEXT("PreviewBrushEnabled"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushCenterUV, FString(), PreviewBrushBlend, TEXT("PreviewBrushCenterUV"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushRadiusUV, FString(), PreviewBrushBlend, TEXT("PreviewBrushRadiusUV"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushStrength, FString(), PreviewBrushBlend, TEXT("PreviewBrushStrength"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushFalloff, FString(), PreviewBrushBlend, TEXT("PreviewBrushFalloff"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushRotation, FString(), PreviewBrushBlend, TEXT("PreviewBrushRotation"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushScale, FString(), PreviewBrushBlend, TEXT("PreviewBrushScale"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushTexelSize, FString(), PreviewBrushBlend, TEXT("PreviewBrushTexelSize"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushUVChannel, FString(), PreviewBrushBlend, TEXT("PreviewBrushUVChannel"), FailureReasons);
        bConnected &= ConnectChecked(PreviewBrushHeightTexture, FString(), PreviewBrushBlend, TEXT("PreviewBrushHeightTex"), FailureReasons);

        if (!UMaterialEditingLibrary::ConnectMaterialProperty(PreviewBrushBlend, FString(), MP_Normal))
        {
            FailureReasons.Add(TEXT("Failed to connect DWC preview brush normal blend output to Material Normal."));
            bConnected = false;
        }

        return bConnected;
    }

    bool ConfigureExistingDwcMaterial(
        UMaterial*                  Material,
        UMaterialFunctionInterface* ApplyFunction,
        UMaterialFunctionInterface* DebugFunction,
        TArray<FString>&            FailureReasons)
    {
        UMaterialExpressionMaterialFunctionCall* ApplyCall = FindFunctionCall(Material, ApplyFunction);
        UMaterialExpressionMaterialFunctionCall* DebugCall = FindFunctionCall(Material, DebugFunction);
        if (ApplyCall == nullptr || DebugCall == nullptr)
        {
            FailureReasons.Add(TEXT("Existing DWC material is missing MF_DWC_ApplyWetness or MF_DWC_WetPartDebug."));
            return false;
        }

        bool                                bConnected = true;
        UMaterialExpressionScalarParameter* WetDarkeningStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WetDarkeningStrength"),
            0.35f,
            -900,
            140);

        if (WetDarkeningStrength == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create DWC_WetDarkeningStrength scalar parameter."));
            bConnected = false;
        }
        else
        {
            bConnected &= ConnectChecked(WetDarkeningStrength, FString(), ApplyCall, TEXT("WetDarkeningStrength"), FailureReasons);
        }
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);

        FString ApplyBaseColorOutput;
        FString ApplyRoughnessOutput;
        FString DebugColorOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(DebugCall, TEXT("Color"), DebugColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Color' on MF_DWC_WetPartDebug. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
            bConnected = false;
        }

        if (!ApplyBaseColorOutput.IsEmpty())
        {
            bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, DebugCall, TEXT("BaseColor"), FailureReasons);
        }
        if (!DebugColorOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(DebugCall, DebugColorOutput, MP_BaseColor))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_WetPartDebug output '%s' to Material BaseColor."),
                                               DebugColorOutput.IsEmpty() ? TEXT("<first>") : *DebugColorOutput));
            bConnected = false;
        }
        if (!ApplyRoughnessOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_ApplyWetness output '%s' to Material Roughness."),
                                               ApplyRoughnessOutput.IsEmpty() ? TEXT("<first>") : *ApplyRoughnessOutput));
            bConnected = false;
        }

        return bConnected;
    }

    bool CreateDwcMaterialGraph(
        UMaterial*                  Material,
        UMaterialFunctionInterface* ApplyFunction,
        UMaterialFunctionInterface* DebugFunction,
        TArray<FString>&            FailureReasons)
    {
        FString              BaseColorOutputName;
        UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(Material, MP_BaseColor, FVector2D(-900.0f, -120.0f), BaseColorOutputName);
        FString              RoughnessOutputName;
        UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Roughness, FVector2D(-900.0f, 160.0f), RoughnessOutputName);

        UMaterialExpressionMaterialFunctionCall* ApplyCall = CreateFunctionCall(Material, ApplyFunction, -360, -70);
        UMaterialExpressionMaterialFunctionCall* DebugCall = CreateFunctionCall(Material, DebugFunction, 60, -95);

        UMaterialExpressionScalarParameter* WetDarkeningStrength = CreateScalarParameter(Material, TEXT("DWC_WetDarkeningStrength"), 0.35f, -900, 140);
        UMaterialExpressionScalarParameter* WetRoughness = CreateScalarParameter(Material, TEXT("DWC_WetRoughness"), 0.12f, -900, 230);
        UMaterialExpressionScalarParameter* SurfaceWaterStrength = CreateScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength"), 1.0f, -900, 330);

        const bool bCreatedRequiredNodes = ApplyCall != nullptr && DebugCall != nullptr &&
                                           BaseColorInput != nullptr && RoughnessInput != nullptr &&
                                           WetDarkeningStrength != nullptr && WetRoughness != nullptr && SurfaceWaterStrength != nullptr;
        if (!bCreatedRequiredNodes)
        {
            FailureReasons.Add(TEXT("DWC material setup could not create one or more required nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, ApplyCall, TEXT("BaseColor"), FailureReasons);
        bConnected &= ConnectChecked(WetDarkeningStrength, FString(), ApplyCall, TEXT("WetDarkeningStrength"), FailureReasons);
        bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, ApplyCall, TEXT("BaseRoughness"), FailureReasons);
        bConnected &= ConnectChecked(WetRoughness, FString(), ApplyCall, TEXT("WetRoughness"), FailureReasons);
        bConnected &= ConnectChecked(SurfaceWaterStrength, FString(), ApplyCall, TEXT("SurfaceWaterStrength"), FailureReasons);
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);

        FString ApplyBaseColorOutput;
        FString ApplyRoughnessOutput;
        FString DebugColorOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(DebugCall, TEXT("Color"), DebugColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Color' on MF_DWC_WetPartDebug. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
            bConnected = false;
        }

        if (!ApplyBaseColorOutput.IsEmpty())
        {
            bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, DebugCall, TEXT("BaseColor"), FailureReasons);
        }
        if (!DebugColorOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(DebugCall, DebugColorOutput, MP_BaseColor))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_WetPartDebug output '%s' to Material BaseColor. Available outputs: %s"),
                                               DebugColorOutput.IsEmpty() ? TEXT("<first>") : *DebugColorOutput,
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
            bConnected = false;
        }
        if (!ApplyRoughnessOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_ApplyWetness output '%s' to Material Roughness. Available outputs: %s"),
                                               ApplyRoughnessOutput.IsEmpty() ? TEXT("<first>") : *ApplyRoughnessOutput,
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }

        return bConnected;
    }
} // namespace

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(UMaterialInterface* MaterialInterface)
{
    FWetClothingMaterialSetupResult Result;

    if (MaterialInterface == nullptr)
    {
        Result.Message = TEXT("No material is assigned to the selected material slot.");
        return Result;
    }

    UMaterial* Material = Cast<UMaterial>(MaterialInterface);
    if (Material == nullptr)
    {
        const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);
        if (MaterialInstance == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' is not an editable material asset."), *MaterialInterface->GetName());
            return Result;
        }

        if (IsMaterialConfiguredForDwc(MaterialInterface))
        {
            Result.bSucceeded = true;
            Result.bAlreadyConfigured = true;
            Result.ConfiguredMaterial = MaterialInterface;
            Result.Message = FString::Printf(TEXT("'%s' is already backed by a DWC material."), *MaterialInterface->GetName());
            return Result;
        }

        UMaterial* ParentMaterial = const_cast<UMaterial*>(MaterialInstance->GetMaterial());
        if (ParentMaterial == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' has no editable parent material."), *MaterialInterface->GetName());
            return Result;
        }

        FWetClothingMaterialSetupResult ParentResult = DuplicateAndApplyToMaterialInterface(ParentMaterial);
        if (!ParentResult.bSucceeded || ParentResult.ConfiguredMaterial == nullptr)
        {
            Result.Message = FString::Printf(
                TEXT("Could not create a DWC parent material for material instance '%s'.\n%s"),
                *MaterialInterface->GetName(),
                *ParentResult.Message);
            return Result;
        }

        FString                    InstanceErrorMessage;
        bool                       bReusedExistingInstance = false;
        UMaterialInstanceConstant* WetInstance = CreateOrUpdateDwcMaterialInstanceForSource(
            MaterialInstance,
            ParentResult.ConfiguredMaterial,
            ParentMaterial,
            InstanceErrorMessage,
            bReusedExistingInstance);
        if (WetInstance == nullptr)
        {
            Result.Message = FString::Printf(
                TEXT("Created DWC parent material for '%s', but could not create a matching wet material instance.\n%s"),
                *MaterialInterface->GetName(),
                *InstanceErrorMessage);
            return Result;
        }

        Result.bSucceeded = true;
        Result.bAlreadyConfigured = bReusedExistingInstance;
        Result.ConfiguredMaterial = WetInstance;
        Result.Message = bReusedExistingInstance
                             ? FString::Printf(TEXT("Reused wet material instance '%s' and copied overrides from '%s'."), *WetInstance->GetName(), *MaterialInterface->GetName())
                             : FString::Printf(TEXT("Created wet material instance '%s' from '%s'."), *WetInstance->GetName(), *MaterialInterface->GetName());
        return Result;
    }

    FString ApplyFunctionPath;
    FString DebugFunctionPath;
    UMaterialFunctionInterface* ApplyFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_ApplyWetness"), &ApplyFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_WetPartDebug"), &DebugFunctionPath);
    if (ApplyFunction == nullptr || DebugFunction == nullptr)
    {
        Result.Message = FString::Printf(
            TEXT("Could not load DWC material functions. Apply: '%s', Debug: '%s'."),
            ApplyFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *ApplyFunctionPath,
            DebugFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *DebugFunctionPath);
        return Result;
    }

    if (HasFunctionCall(Material, ApplyFunction) || HasFunctionCall(Material, DebugFunction))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "RepairWetnessMaterialSetup", "Repair Dynamic Wet Clothes Material Setup"));
        Material->Modify();

        TArray<FString>       FailureReasons;
        const bool            bConfigured = ConfigureExistingDwcMaterial(Material, ApplyFunction, DebugFunction, FailureReasons);
        const TArray<FString> CompileErrors = bConfigured ? UMaterialEditingLibrary::RecompileMaterial(Material) : TArray<FString>();
        Material->MarkPackageDirty();

        Result.bSucceeded = bConfigured && CompileErrors.Num() == 0;
        Result.bAlreadyConfigured = Result.bSucceeded;
        Result.ConfiguredMaterial = Result.bSucceeded ? Material : nullptr;
        Result.Message = Result.bSucceeded
                             ? FString::Printf(TEXT("'%s' already contains DWC material functions. Refreshed DWC output connections."), *Material->GetName())
                             : FString::Printf(TEXT("'%s' contains DWC material functions but setup refresh failed.\n%s"), *Material->GetName(), *FString::Join(FailureReasons, TEXT("\n")));
        return Result;
    }

    if (UMaterial* ExistingDwcMaterial = LoadExistingDwcMaterialForSource(Material))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "ReuseWetnessMaterialSetup", "Reuse Dynamic Wet Clothes Material Setup"));
        ExistingDwcMaterial->Modify();

        TArray<FString>       FailureReasons;
        const bool            bHasDwcFunctionCall = HasFunctionCall(ExistingDwcMaterial, ApplyFunction) || HasFunctionCall(ExistingDwcMaterial, DebugFunction);
        const bool            bConfigured = bHasDwcFunctionCall
                                                ? ConfigureExistingDwcMaterial(ExistingDwcMaterial, ApplyFunction, DebugFunction, FailureReasons)
                                                : CreateDwcMaterialGraph(ExistingDwcMaterial, ApplyFunction, DebugFunction, FailureReasons);
        const TArray<FString> CompileErrors = bConfigured ? UMaterialEditingLibrary::RecompileMaterial(ExistingDwcMaterial) : TArray<FString>();
        ExistingDwcMaterial->MarkPackageDirty();

        Result.bSucceeded = bConfigured && CompileErrors.Num() == 0;
        Result.bAlreadyConfigured = true;
        Result.ConfiguredMaterial = Result.bSucceeded ? ExistingDwcMaterial : nullptr;
        Result.Message = Result.bSucceeded
                             ? FString::Printf(TEXT("Reused existing DWC material '%s' and refreshed DWC material setup."), *ExistingDwcMaterial->GetName())
                             : FString::Printf(TEXT("Existing DWC material '%s' could not be refreshed.\n%s"), *ExistingDwcMaterial->GetName(), *FString::Join(FailureReasons, TEXT("\n")));
        return Result;
    }

    const FString      OriginalPackageName = Material->GetOutermost()->GetName();
    FString            NewPackageName;
    FString            NewAssetName;
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(OriginalPackageName, TEXT("_DWC"), NewPackageName, NewAssetName);

    const FString NewPackagePath = FPackageName::GetLongPackagePath(NewPackageName);
    UMaterial*    DuplicatedMaterial = Cast<UMaterial>(AssetToolsModule.Get().DuplicateAsset(NewAssetName, NewPackagePath, Material));
    if (DuplicatedMaterial == nullptr)
    {
        Result.Message = FString::Printf(TEXT("Failed to duplicate '%s'."), *Material->GetName());
        return Result;
    }

    Material = DuplicatedMaterial;

    const FScopedTransaction Transaction(NSLOCTEXT("DWC", "ApplyWetnessMaterialSetup", "Apply Dynamic Wet Clothes Material Setup"));
    Material->Modify();

    FString              BaseColorOutputName;
    UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(Material, MP_BaseColor, FVector2D(-900.0f, -120.0f), BaseColorOutputName);
    FString              RoughnessOutputName;
    UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Roughness, FVector2D(-900.0f, 160.0f), RoughnessOutputName);

    UMaterialExpressionMaterialFunctionCall* ApplyCall = CreateFunctionCall(Material, ApplyFunction, -360, -70);
    UMaterialExpressionMaterialFunctionCall* DebugCall = CreateFunctionCall(Material, DebugFunction, 60, -95);

    UMaterialExpressionScalarParameter* WetDarkeningStrength = CreateScalarParameter(Material, TEXT("DWC_WetDarkeningStrength"), 0.35f, -900, 140);
    UMaterialExpressionScalarParameter* WetRoughness = CreateScalarParameter(Material, TEXT("DWC_WetRoughness"), 0.12f, -900, 230);
    UMaterialExpressionScalarParameter* SurfaceWaterStrength = CreateScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength"), 1.0f, -900, 330);

    const bool bCreatedRequiredNodes = ApplyCall != nullptr && DebugCall != nullptr &&
                                       BaseColorInput != nullptr && RoughnessInput != nullptr &&
                                       WetDarkeningStrength != nullptr && WetRoughness != nullptr && SurfaceWaterStrength != nullptr;
    if (!bCreatedRequiredNodes)
    {
        Result.Message = TEXT("DWC material setup could not create one or more required nodes.");
        return Result;
    }

    TArray<FString> FailureReasons;
    bool            bConnected = true;
    bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, ApplyCall, TEXT("BaseColor"), FailureReasons);
    bConnected &= ConnectChecked(WetDarkeningStrength, FString(), ApplyCall, TEXT("WetDarkeningStrength"), FailureReasons);
    bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, ApplyCall, TEXT("BaseRoughness"), FailureReasons);
    bConnected &= ConnectChecked(WetRoughness, FString(), ApplyCall, TEXT("WetRoughness"), FailureReasons);
    bConnected &= ConnectChecked(SurfaceWaterStrength, FString(), ApplyCall, TEXT("SurfaceWaterStrength"), FailureReasons);
    bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);

    FString ApplyBaseColorOutput;
    FString ApplyRoughnessOutput;
    FString DebugColorOutput;
    if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                           *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                           *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ResolveRequiredOutputName(DebugCall, TEXT("Color"), DebugColorOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'Color' on MF_DWC_WetPartDebug. Available outputs: %s"),
                                           *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
        bConnected = false;
    }
    if (!ApplyBaseColorOutput.IsEmpty())
    {
        bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, DebugCall, TEXT("BaseColor"), FailureReasons);
    }
    if (!DebugColorOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(DebugCall, DebugColorOutput, MP_BaseColor))
    {
        FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_WetPartDebug output '%s' to Material BaseColor. Available outputs: %s"),
                                           DebugColorOutput.IsEmpty() ? TEXT("<first>") : *DebugColorOutput,
                                           *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
        bConnected = false;
    }
    if (!ApplyRoughnessOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
    {
        FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_ApplyWetness output '%s' to Material Roughness. Available outputs: %s"),
                                           ApplyRoughnessOutput.IsEmpty() ? TEXT("<first>") : *ApplyRoughnessOutput,
                                           *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }

    if (!bConnected)
    {
        Result.Message = TEXT("DWC material setup created nodes but could not connect one or more expected pins.\n");
        Result.Message += FString::Join(FailureReasons, TEXT("\n"));
        return Result;
    }

    const TArray<FString> CompileErrors = UMaterialEditingLibrary::RecompileMaterial(Material);
    Material->MarkPackageDirty();

    Result.bSucceeded = CompileErrors.Num() == 0;
    Result.ConfiguredMaterial = Result.bSucceeded ? Material : nullptr;
    Result.Message = Result.bSucceeded
                         ? FString::Printf(TEXT("Duplicated the source material and applied DWC material setup to '%s'."), *Material->GetName())
                         : FString::Printf(TEXT("Duplicated the source material as '%s', but material compilation reported %d error(s)."), *Material->GetName(), CompileErrors.Num());
    return Result;
}

bool FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface)
{
    if (MaterialInterface == nullptr)
    {
        return false;
    }

    UMaterial* Material = MaterialInterface->GetMaterial();
    if (Material == nullptr)
    {
        return false;
    }

    UMaterialFunctionInterface* ApplyFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_ApplyWetness"));
    UMaterialFunctionInterface* DebugFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_WetPartDebug"));
    return HasFunctionCall(Material, ApplyFunction) && HasFunctionCall(Material, DebugFunction);
}

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::EnsurePreviewSupportOnMaterialInterface(UMaterialInterface* MaterialInterface)
{
    FWetClothingMaterialSetupResult Result;

    if (MaterialInterface == nullptr)
    {
        Result.Message = TEXT("No material was supplied for DWC preview support.");
        return Result;
    }

    UMaterial* Material = MaterialInterface->GetMaterial();
    if (Material == nullptr)
    {
        Result.Message = FString::Printf(TEXT("'%s' does not resolve to an editable material."), *MaterialInterface->GetName());
        return Result;
    }

    FString ApplyFunctionPath;
    FString DebugFunctionPath;
    UMaterialFunctionInterface* ApplyFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_ApplyWetness"), &ApplyFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_WetPartDebug"), &DebugFunctionPath);
    if (ApplyFunction == nullptr || DebugFunction == nullptr)
    {
        Result.Message = FString::Printf(
            TEXT("Could not load DWC material functions. Apply: '%s', Debug: '%s'."),
            ApplyFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *ApplyFunctionPath,
            DebugFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *DebugFunctionPath);
        return Result;
    }

    if (!HasFunctionCall(Material, ApplyFunction) || !HasFunctionCall(Material, DebugFunction))
    {
        Result.Message = FString::Printf(TEXT("'%s' is not configured with the required DWC material functions."), *MaterialInterface->GetName());
        return Result;
    }

    if (HasPreviewBrushSupport(Material))
    {
        Result.bSucceeded = true;
        Result.bAlreadyConfigured = true;
        Result.ConfiguredMaterial = MaterialInterface;
        Result.Message = FString::Printf(TEXT("'%s' already supports DWC wrinkle brush preview."), *Material->GetName());
        return Result;
    }

    const FScopedTransaction Transaction(NSLOCTEXT("DWC", "EnsureWetnessMaterialPreviewSupport", "Ensure Dynamic Wet Clothes Preview Support"));
    Material->Modify();

    TArray<FString> FailureReasons;
    const bool bConfigured = ConfigureExistingDwcMaterial(Material, ApplyFunction, DebugFunction, FailureReasons);
    const TArray<FString> CompileErrors = bConfigured ? UMaterialEditingLibrary::RecompileMaterial(Material) : TArray<FString>();
    Material->MarkPackageDirty();

    Result.bSucceeded = bConfigured && CompileErrors.Num() == 0;
    Result.bAlreadyConfigured = Result.bSucceeded;
    Result.ConfiguredMaterial = Result.bSucceeded ? MaterialInterface : nullptr;
    Result.Message = Result.bSucceeded
                         ? FString::Printf(TEXT("Enabled DWC wrinkle brush preview support on '%s'."), *Material->GetName())
                         : FString::Printf(TEXT("Failed to enable DWC wrinkle brush preview support on '%s'.\n%s"), *Material->GetName(), *FString::Join(FailureReasons, TEXT("\n")));
    return Result;
}
