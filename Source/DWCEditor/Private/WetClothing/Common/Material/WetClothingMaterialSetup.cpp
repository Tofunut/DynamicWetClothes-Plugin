#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
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

    UMaterialExpressionTextureCoordinate* FindOrCreateWrinkleTextureCoordinate(UMaterial* Material, int32 WrinkleUVChannelIndex, int32 NodePosX, int32 NodePosY)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        const int32 SafeWrinkleUVChannelIndex = FMath::Clamp(WrinkleUVChannelIndex, 0, 7);
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            if (TextureCoordinate != nullptr && TextureCoordinate->Desc == TEXT("DWC Wrinkle UV"))
            {
                TextureCoordinate->CoordinateIndex = SafeWrinkleUVChannelIndex;
                return TextureCoordinate;
            }
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), NodePosX, NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = SafeWrinkleUVChannelIndex;
            TextureCoordinate->Desc = TEXT("DWC Wrinkle UV");
        }
        return TextureCoordinate;
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

    bool ConnectTextureCoordinateChecked(UMaterialExpression* UVExpression, UMaterialExpressionTextureSampleParameter2D* TextureSample, TArray<FString>& FailureReasons)
    {
        if (UVExpression == nullptr || TextureSample == nullptr)
        {
            FailureReasons.Add(TEXT("Wrinkle normal map UV connection requires a TextureCoordinate node and a texture sample node."));
            return false;
        }

        const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(TextureSample);
        static const FString CandidateInputNames[] = { TEXT("UVs"), TEXT("Coordinates") };
        for (const FString& CandidateInputName : CandidateInputNames)
        {
            if (InputNames.Contains(CandidateInputName) &&
                UMaterialEditingLibrary::ConnectMaterialExpressions(UVExpression, FString(), TextureSample, CandidateInputName))
            {
                return true;
            }
        }

        FailureReasons.Add(FString::Printf(
            TEXT("Failed to connect the DWC wrinkle TextureCoordinate to DWC_WrinkleNormalMap. Available texture sample inputs: %s"),
            *JoinPinNames(InputNames)));
        return false;
    }

    bool ConnectDwcApplyWetnessNormalGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        int32                                    WrinkleUVChannelIndex,
        TArray<FString>&                         FailureReasons)
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
        UMaterialExpressionTextureCoordinate* WrinkleUV = FindOrCreateWrinkleTextureCoordinate(
            Material,
            WrinkleUVChannelIndex,
            -1150,
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

        const bool bCreatedRequiredNodes = BaseNormalInput != nullptr &&
                                           WrinkleNormalMap != nullptr &&
                                           WrinkleUV != nullptr &&
                                           UseWrinkleNormalMap != nullptr &&
                                           WrinkleStrength != nullptr &&
                                           WrinkleWetnessMin != nullptr &&
                                           WrinkleWetnessMax != nullptr;
        if (!bCreatedRequiredNodes)
        {
            FailureReasons.Add(TEXT("DWC material setup could not create one or more wrinkle normal nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseNormalInput, BaseNormalOutputName, ApplyCall, TEXT("BaseNormal"), FailureReasons);
        bConnected &= ConnectTextureCoordinateChecked(WrinkleUV, WrinkleNormalMap, FailureReasons);
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

        if (!UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyNormalOutput, MP_Normal))
        {
            FailureReasons.Add(TEXT("Failed to connect the DWC wetness normal output to Material Normal."));
            bConnected = false;
        }

        return bConnected;
    }

    bool ConfigureExistingDwcMaterial(
        UMaterial*                  Material,
        UMaterialFunctionInterface* ApplyFunction,
        UMaterialFunctionInterface* DebugFunction,
        int32                       WrinkleUVChannelIndex,
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
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, WrinkleUVChannelIndex, FailureReasons);

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
        int32                       WrinkleUVChannelIndex,
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
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, WrinkleUVChannelIndex, FailureReasons);

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

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(UMaterialInterface* MaterialInterface, int32 WrinkleUVChannelIndex)
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

        UMaterial* ParentMaterial = const_cast<UMaterial*>(MaterialInstance->GetMaterial());
        if (IsMaterialConfiguredForDwc(MaterialInterface))
        {
            if (ParentMaterial != nullptr)
            {
                FWetClothingMaterialSetupResult ParentRefreshResult = DuplicateAndApplyToMaterialInterface(ParentMaterial, WrinkleUVChannelIndex);
                if (!ParentRefreshResult.bSucceeded)
                {
                    Result.Message = FString::Printf(
                        TEXT("'%s' is already backed by a DWC material, but the parent material could not be refreshed for wrinkle UV channel %d.\n%s"),
                        *MaterialInterface->GetName(),
                        FMath::Max(WrinkleUVChannelIndex, 0),
                        *ParentRefreshResult.Message);
                    return Result;
                }
            }

            Result.bSucceeded = true;
            Result.bAlreadyConfigured = true;
            Result.ConfiguredMaterial = MaterialInterface;
            Result.Message = FString::Printf(
                TEXT("'%s' is already backed by a DWC material. Refreshed wrinkle UV channel %d on its parent material."),
                *MaterialInterface->GetName(),
                FMath::Max(WrinkleUVChannelIndex, 0));
            return Result;
        }

        if (ParentMaterial == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' has no editable parent material."), *MaterialInterface->GetName());
            return Result;
        }

        FWetClothingMaterialSetupResult ParentResult = DuplicateAndApplyToMaterialInterface(ParentMaterial, WrinkleUVChannelIndex);
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
        const bool            bConfigured = ConfigureExistingDwcMaterial(Material, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, FailureReasons);
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
                                                ? ConfigureExistingDwcMaterial(ExistingDwcMaterial, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, FailureReasons)
                                                : CreateDwcMaterialGraph(ExistingDwcMaterial, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, FailureReasons);
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
    bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, WrinkleUVChannelIndex, FailureReasons);

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
