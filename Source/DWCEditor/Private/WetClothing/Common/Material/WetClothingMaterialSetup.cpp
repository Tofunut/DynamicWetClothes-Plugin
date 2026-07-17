#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");
    constexpr const TCHAR* GeneratedDwcMaterialSuffixCPU = TEXT("_DWC_CPU");
    constexpr const TCHAR* GeneratedDwcMaterialSuffixGPU = TEXT("_DWC_GPU");
    constexpr const TCHAR* DwcApplyWetnessFunctionCPU = TEXT("MF_DWC_ApplyWetness_CPU");
    constexpr const TCHAR* DwcApplyWetnessFunctionGPU = TEXT("MF_DWC_ApplyWetness_GPU");
    constexpr const TCHAR* DwcWetPartDebugFunction = TEXT("MF_DWC_WetPartDebug");
    constexpr const TCHAR* DwcWetPartDebugUseWetnessMaskParameter = TEXT("DWC_WetPartDebugUseWetnessMask");

    const TCHAR* GetGeneratedDwcMaterialSuffix(const EDWCSimulationMode SimulationMode)
    {
        return SimulationMode == EDWCSimulationMode::WetnessMapGPU
                   ? GeneratedDwcMaterialSuffixGPU
                   : GeneratedDwcMaterialSuffixCPU;
    }

    const TCHAR* GetDwcApplyWetnessFunctionName(const EDWCSimulationMode SimulationMode)
    {
        return SimulationMode == EDWCSimulationMode::WetnessMapGPU
                   ? DwcApplyWetnessFunctionGPU
                   : DwcApplyWetnessFunctionCPU;
    }

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

    UMaterialFunctionInterface* LoadDwcApplyWetnessMaterialFunction(
        const EDWCSimulationMode SimulationMode,
        FString* OutObjectPath = nullptr)
    {
        return LoadDwcMaterialFunction(GetDwcApplyWetnessFunctionName(SimulationMode), OutObjectPath);
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

    TArray<int32> CollectWettableMaterialSlotIndices(const UWetClothingAsset& WetClothingAsset)
    {
        TArray<int32> WettableSlots;
        for (const FWetClothingWettableMaterialSlotState& SlotState : WetClothingAsset.PartData.EditableWetPartData.WettableMaterialSlots)
        {
            if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
            {
                WettableSlots.AddUnique(SlotState.MaterialSlotIndex);
            }
        }
        WettableSlots.Sort();
        return WettableSlots;
    }

    const FWetClothingGeneratedWetMaterialOverride* FindGeneratedWetMaterialOverride(
        const UWetClothingAsset& WetClothingAsset,
        const int32 MaterialSlotIndex)
    {
        return WetClothingAsset.PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });
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

    bool IsFunctionInputConnected(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const FName InputName)
    {
        if (FunctionCall == nullptr)
        {
            return false;
        }

        for (const FFunctionExpressionInput& FunctionInput : FunctionCall->FunctionInputs)
        {
            if (FunctionInput.ExpressionInput != nullptr &&
                FunctionInput.ExpressionInput->InputName == InputName)
            {
                return FunctionInput.Input.Expression != nullptr;
            }
        }

        return false;
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

    TArray<FString> GetMaterialExpressionOutputNames(UMaterialExpression* Expression)
    {
        TArray<FString> OutputNames;
        if (Expression == nullptr)
        {
            return OutputNames;
        }

        for (const FExpressionOutput& Output : Expression->GetOutputs())
        {
            OutputNames.Add(Output.OutputName.ToString());
        }
        return OutputNames;
    }

    bool ResolveRequiredOutputName(UMaterialExpression* Expression, const FString& OutputName, FString& OutResolvedOutputName)
    {
        const TArray<FString> OutputNames = GetMaterialExpressionOutputNames(Expression);
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

    UMaterialExpressionVertexColor* FindOrCreateVertexColor(UMaterial* Material, const int32 NodePosX, const int32 NodePosY)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (UMaterialExpressionVertexColor* VertexColor = Cast<UMaterialExpressionVertexColor>(Expression))
            {
                return VertexColor;
            }
        }

        return Cast<UMaterialExpressionVertexColor>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVertexColor::StaticClass(), NodePosX, NodePosY));
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

    UTexture* LoadDefaultBlackTexture()
    {
        if (UTexture* BlackTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black")))
        {
            return BlackTexture;
        }

        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    }

    UTexture* LoadFallbackTextureForSamplerType(const EMaterialSamplerType SamplerType)
    {
        switch (SamplerType)
        {
        case SAMPLERTYPE_Normal:
            return LoadDefaultNormalTexture();
        case SAMPLERTYPE_Masks:
        case SAMPLERTYPE_Grayscale:
        case SAMPLERTYPE_LinearColor:
            return LoadDefaultBlackTexture();
        default:
            return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
        }
    }

    bool ReplaceMissingTextureSamplesWithFallbacks(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        bool bChanged = false;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureSample* TextureSample = Cast<UMaterialExpressionTextureSample>(Expression);
            if (TextureSample == nullptr || TextureSample->Texture != nullptr)
            {
                continue;
            }

            if (UTexture* FallbackTexture = LoadFallbackTextureForSamplerType(TextureSample->SamplerType))
            {
                TextureSample->Texture = FallbackTexture;
                bChanged = true;
            }
        }

        return bChanged;
    }

    FString BuildCompileErrorMessage(const FString& Prefix, const TArray<FString>& CompileErrors)
    {
        if (CompileErrors.IsEmpty())
        {
            return Prefix;
        }

        return FString::Printf(
            TEXT("%s\n%s"),
            *Prefix,
            *FString::Join(CompileErrors, TEXT("\n")));
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

    UMaterialExpressionTextureSampleParameter2D* FindOrCreateGPUWetnessMapParameter(
        UMaterial* Material,
        int32 NodePosX,
        int32 NodePosY)
    {
        static const FName ParameterName(TEXT("DWC_WetnessMap"));
        if (UMaterialExpressionTextureSampleParameter2D* ExistingParameter = FindTextureSampleParameter(Material, ParameterName))
        {
            ExistingParameter->SamplerType = SAMPLERTYPE_Color;
            ExistingParameter->Desc = TEXT("DWC GPU absorbed-wetness map. Assigned by the GPU backend at runtime.");
            if (ExistingParameter->Texture == nullptr)
            {
                ExistingParameter->Texture = LoadDefaultBlackTexture();
            }
            return ExistingParameter;
        }

        UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureSampleParameter2D::StaticClass(),
                NodePosX,
                NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadDefaultBlackTexture();
            Parameter->Desc = TEXT("DWC GPU absorbed-wetness map. Assigned by the GPU backend at runtime.");
        }
        return Parameter;
    }

    UMaterialExpressionTextureCoordinate* FindOrCreateDWCDataTextureCoordinate(
        UMaterial* Material,
        const int32 DWCDataUVChannelIndex,
        int32 NodePosX,
        int32 NodePosY)
    {
        if (Material == nullptr || DWCDataUVChannelIndex < 0)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
            {
                if (TextureCoordinate->Desc == TEXT("DWC Data UV"))
                {
                    TextureCoordinate->CoordinateIndex = DWCDataUVChannelIndex;
                    return TextureCoordinate;
                }
            }
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), NodePosX, NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = DWCDataUVChannelIndex;
            TextureCoordinate->Desc = TEXT("DWC Data UV");
        }
        return TextureCoordinate;
    }

    FString StripKnownDwcSuffix(const FString& PackageName)
    {
        static const TCHAR* KnownSuffixes[] = { GeneratedDwcMaterialSuffixCPU, GeneratedDwcMaterialSuffixGPU, TEXT("_DWC") };
        for (const TCHAR* KnownSuffix : KnownSuffixes)
        {
            if (PackageName.EndsWith(KnownSuffix))
            {
                return PackageName.LeftChop(FCString::Strlen(KnownSuffix));
            }
        }
        return PackageName;
    }

    bool PackageHasMissingPackageDependencies(const FString& PackageName)
    {
        if (PackageName.IsEmpty())
        {
            return false;
        }

        FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        TArray<FName> Dependencies;
        AssetRegistryModule.Get().GetDependencies(
            FName(*PackageName),
            Dependencies,
            UE::AssetRegistry::EDependencyCategory::Package);

        for (const FName Dependency : Dependencies)
        {
            const FString DependencyPackageName = Dependency.ToString();
            if (DependencyPackageName.StartsWith(TEXT("/Script/")) ||
                DependencyPackageName.StartsWith(TEXT("/Memory/")) ||
                DependencyPackageName.StartsWith(TEXT("/Temp/")) ||
                DependencyPackageName == PackageName)
            {
                continue;
            }

            FString ExistingPackageFilename;
            if (!FPackageName::DoesPackageExist(DependencyPackageName, &ExistingPackageFilename))
            {
                return true;
            }
        }

        return false;
    }

    UMaterial* LoadExistingDwcMaterialForSource(
        const UMaterial* SourceMaterial,
        const TCHAR*     GeneratedDwcMaterialSuffix)
    {
        if (SourceMaterial == nullptr)
        {
            return nullptr;
        }

        const FString SourcePackageName = SourceMaterial->GetOutermost()->GetName();
        if (SourcePackageName.EndsWith(GeneratedDwcMaterialSuffix))
        {
            return const_cast<UMaterial*>(SourceMaterial);
        }

        const FString DwcPackageName = StripKnownDwcSuffix(SourcePackageName) + GeneratedDwcMaterialSuffix;
        if (PackageHasMissingPackageDependencies(DwcPackageName))
        {
            return nullptr;
        }

        const FString DwcAssetName = FPackageName::GetLongPackageAssetName(DwcPackageName);
        const FString DwcObjectPath = DwcPackageName + TEXT(".") + DwcAssetName;
        return LoadObject<UMaterial>(nullptr, *DwcObjectPath);
    }

    FString BuildDwcPackageNameForSourceInterface(
        const UMaterialInterface* SourceMaterialInterface,
        const UMaterial*          FallbackParentMaterial,
        const TCHAR*              GeneratedDwcMaterialSuffix)
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

        if (SourcePackageName.EndsWith(GeneratedDwcMaterialSuffix))
        {
            return SourcePackageName;
        }

        return StripKnownDwcSuffix(SourcePackageName) + GeneratedDwcMaterialSuffix;
    }

    UMaterialInstanceConstant* LoadExistingDwcMaterialInstanceForSource(
        const UMaterialInstance* SourceInstance,
        const UMaterial*         FallbackParentMaterial,
        const TCHAR*             GeneratedDwcMaterialSuffix)
    {
        const FString DwcPackageName = BuildDwcPackageNameForSourceInterface(SourceInstance, FallbackParentMaterial, GeneratedDwcMaterialSuffix);
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
        TargetInstance->UpdateStaticPermutation(StaticParameters, BasePropertyOverrides);
        TargetInstance->PostEditChange();
        TargetInstance->MarkPackageDirty();
    }

    TArray<FString> RecompileMaterialAndCollectErrors(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return { TEXT("Cannot compile a null material.") };
        }

        UMaterialEditingLibrary::RecompileMaterial(Material);
        if (const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform))
        {
            return Resource->GetCompileErrors();
        }
        return {};
    }

    UMaterialInstanceConstant* CreateOrUpdateDwcMaterialInstanceForSource(
        const UMaterialInstance* SourceInstance,
        UMaterialInterface*      WetParent,
        const UMaterial*         FallbackParentMaterial,
        const TCHAR*             GeneratedDwcMaterialSuffix,
        FString&                 OutErrorMessage,
        bool&                    bOutReusedExisting)
    {
        bOutReusedExisting = false;

        if (SourceInstance == nullptr || WetParent == nullptr)
        {
            OutErrorMessage = TEXT("Material instance setup requires a source instance and wet parent material.");
            return nullptr;
        }

        if (UMaterialInstanceConstant* ExistingInstance = LoadExistingDwcMaterialInstanceForSource(SourceInstance, FallbackParentMaterial, GeneratedDwcMaterialSuffix))
        {
            bOutReusedExisting = true;
            CopyMaterialInstanceOverrides(SourceInstance, ExistingInstance, WetParent);
            return ExistingInstance;
        }

        FString DwcPackageName = BuildDwcPackageNameForSourceInterface(SourceInstance, FallbackParentMaterial, GeneratedDwcMaterialSuffix);
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
                                               *JoinPinNames(GetMaterialExpressionOutputNames(FromExpression))));
            return false;
        }

        return true;
    }

    bool ConnectTextureCoordinateChecked(UMaterialExpressionTextureCoordinate* TextureCoordinate, UMaterialExpressionTextureSampleParameter2D* TextureSample, TArray<FString>& FailureReasons)
    {
        if (TextureCoordinate == nullptr || TextureSample == nullptr)
        {
            FailureReasons.Add(TEXT("DWC Data UV connection requires a texture coordinate node and a texture sample node."));
            return false;
        }

        const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(TextureSample);
        static const FString CandidateInputNames[] = { TEXT("UVs"), TEXT("Coordinates") };
        for (const FString& CandidateInputName : CandidateInputNames)
        {
            if (InputNames.Contains(CandidateInputName) &&
                UMaterialEditingLibrary::ConnectMaterialExpressions(TextureCoordinate, FString(), TextureSample, CandidateInputName))
            {
                return true;
            }
        }

        FailureReasons.Add(FString::Printf(
            TEXT("Failed to connect the DWC Data UV texture coordinate to the texture sample. Available texture sample inputs: %s"),
            *JoinPinNames(InputNames)));
        return false;
    }

    int32 ResolveExpressionOutputIndex(UMaterialExpression* Expression, const FString& OutputName, const int32 FallbackOutputIndex)
    {
        if (Expression == nullptr)
        {
            return INDEX_NONE;
        }

        const TArray<FString> OutputNames = GetMaterialExpressionOutputNames(Expression);
        for (int32 OutputIndex = 0; OutputIndex < OutputNames.Num(); ++OutputIndex)
        {
            if (OutputNames[OutputIndex] == OutputName)
            {
                return OutputIndex;
            }
        }

        return OutputNames.IsValidIndex(FallbackOutputIndex) ? FallbackOutputIndex : INDEX_NONE;
    }

    UMaterialExpressionFunctionInput* FindFunctionInputExpression(UMaterialFunction* MaterialFunction, const FName InputName)
    {
        if (MaterialFunction == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(Expression);
            if (FunctionInput != nullptr && FunctionInput->InputName == InputName)
            {
                return FunctionInput;
            }
        }

        return nullptr;
    }

    UMaterialExpressionFunctionInput* FindOrCreateScalarFunctionInput(
        UMaterialFunction* MaterialFunction,
        const FName        InputName,
        const FString&     Description,
        const float        PreviewValue,
        const int32        SortPriority,
        const int32        NodePosX,
        const int32        NodePosY)
    {
        if (UMaterialExpressionFunctionInput* ExistingInput = FindFunctionInputExpression(MaterialFunction, InputName))
        {
            ExistingInput->InputType = FunctionInput_Scalar;
            ExistingInput->Description = Description;
            ExistingInput->PreviewValue = FVector4f(PreviewValue, PreviewValue, PreviewValue, PreviewValue);
            ExistingInput->bUsePreviewValueAsDefault = true;
            ExistingInput->SortPriority = SortPriority;
            return ExistingInput;
        }

        UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                MaterialFunction,
                UMaterialExpressionFunctionInput::StaticClass(),
                NodePosX,
                NodePosY));
        if (FunctionInput != nullptr)
        {
            FunctionInput->InputName = InputName;
            FunctionInput->Description = Description;
            FunctionInput->InputType = FunctionInput_Scalar;
            FunctionInput->PreviewValue = FVector4f(PreviewValue, PreviewValue, PreviewValue, PreviewValue);
            FunctionInput->bUsePreviewValueAsDefault = true;
            FunctionInput->SortPriority = SortPriority;
            FunctionInput->ConditionallyGenerateId(true);
            FunctionInput->ValidateName();
        }

        return FunctionInput;
    }

    UMaterialExpressionVertexColor* FindBestVertexColorWetnessSource(
        UMaterialFunction* MaterialFunction,
        int32&             OutRedOutputIndex,
        TArray<FExpressionInput*>& OutWetnessInputs)
    {
        OutRedOutputIndex = INDEX_NONE;
        OutWetnessInputs.Reset();

        UMaterialExpressionVertexColor* BestVertexColor = nullptr;
        int32                           BestRedOutputIndex = INDEX_NONE;
        TArray<FExpressionInput*>       BestWetnessInputs;

        if (MaterialFunction == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            UMaterialExpressionVertexColor* VertexColor = Cast<UMaterialExpressionVertexColor>(Expression);
            if (VertexColor == nullptr)
            {
                continue;
            }

            const int32 RedOutputIndex = ResolveExpressionOutputIndex(VertexColor, TEXT("R"), 1);
            if (RedOutputIndex == INDEX_NONE)
            {
                continue;
            }

            TArray<FExpressionInput*> CandidateWetnessInputs;
            for (UMaterialExpression* CandidateExpression : MaterialFunction->GetExpressions())
            {
                if (CandidateExpression == nullptr || CandidateExpression == VertexColor)
                {
                    continue;
                }

                for (FExpressionInputIterator InputIt{ CandidateExpression }; InputIt; ++InputIt)
                {
                    FExpressionInput* Input = InputIt.operator->();
                    if (Input != nullptr &&
                        Input->Expression == VertexColor &&
                        Input->OutputIndex == RedOutputIndex)
                    {
                        CandidateWetnessInputs.Add(Input);
                    }
                }
            }

            if (CandidateWetnessInputs.Num() > BestWetnessInputs.Num())
            {
                BestVertexColor = VertexColor;
                BestRedOutputIndex = RedOutputIndex;
                BestWetnessInputs = MoveTemp(CandidateWetnessInputs);
            }
        }

        OutRedOutputIndex = BestRedOutputIndex;
        OutWetnessInputs = MoveTemp(BestWetnessInputs);
        return BestVertexColor;
    }

    bool EnsureCPUApplyWetnessFunctionUsesExplicitWetnessInput(
        UMaterialFunctionInterface* ApplyFunction,
        TArray<FString>&            FailureReasons)
    {
        UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(ApplyFunction);
        if (MaterialFunction == nullptr)
        {
            FailureReasons.Add(TEXT("MF_DWC_ApplyWetness_CPU is not an editable material function."));
            return false;
        }

        UMaterialExpressionFunctionInput* WetnessInput = FindOrCreateScalarFunctionInput(
            MaterialFunction,
            TEXT("Wetness"),
            TEXT("CPU vertex wetness from VertexColor.R"),
            0.0f,
            0,
            -1300,
            -220);
        if (WetnessInput == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create Wetness input on MF_DWC_ApplyWetness_CPU."));
            return false;
        }

        int32 RedOutputIndex = INDEX_NONE;
        TArray<FExpressionInput*> WetnessInputs;
        UMaterialExpressionVertexColor* VertexColor =
            FindBestVertexColorWetnessSource(MaterialFunction, RedOutputIndex, WetnessInputs);

        if (VertexColor != nullptr && RedOutputIndex != INDEX_NONE)
        {
            for (FExpressionInput* WetnessConsumerInput : WetnessInputs)
            {
                if (WetnessConsumerInput == nullptr)
                {
                    continue;
                }

                WetnessConsumerInput->Expression = WetnessInput;
                WetnessConsumerInput->OutputIndex = 0;
                WetnessConsumerInput->InputName = NAME_None;
            }
        }

        MaterialFunction->MarkPackageDirty();
        MaterialFunction->PostEditChange();
        return true;
    }

    bool PrepareDwcApplyWetnessFunction(
        UMaterialFunctionInterface* ApplyFunction,
        const FWetClothingMaterialSetup::FOptions& Options,
        TArray<FString>&            FailureReasons)
    {
        if (Options.SimulationMode != EDWCSimulationMode::VertexCPU)
        {
            return true;
        }

        return EnsureCPUApplyWetnessFunctionUsesExplicitWetnessInput(ApplyFunction, FailureReasons);
    }

    bool DoesWetPartDebugFunctionUseWetnessMask(UMaterialFunctionInterface* DebugFunction)
    {
        UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(DebugFunction);
        if (MaterialFunction == nullptr)
        {
            return false;
        }

        if (MaterialFunction->Description.Contains(DwcWetPartDebugUseWetnessMaskParameter))
        {
            return true;
        }

        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression);
            if (ScalarParameter != nullptr &&
                ScalarParameter->ParameterName == DwcWetPartDebugUseWetnessMaskParameter)
            {
                return true;
            }
        }

        return false;
    }

    bool RemoveWetPartDebugWetnessMask(UMaterialFunctionInterface* DebugFunction, TArray<FString>& FailureReasons)
    {
        UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(DebugFunction);
        if (MaterialFunction == nullptr)
        {
            FailureReasons.Add(TEXT("MF_DWC_WetPartDebug is not an editable material function."));
            return false;
        }

        bool bModified = false;
        if (MaterialFunction->Description.Contains(DwcWetPartDebugUseWetnessMaskParameter))
        {
            MaterialFunction->Description = TEXT("Displays Dynamic Wet Clothes WetPart/Profile debug colors. The function reads VertexColor.R as wetness and VertexColor.GBA as the debug color.");
            bModified = true;
        }

        TArray<UMaterialExpressionScalarParameter*> WetnessMaskParameters;
        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression);
            if (ScalarParameter != nullptr &&
                ScalarParameter->ParameterName == DwcWetPartDebugUseWetnessMaskParameter)
            {
                WetnessMaskParameters.Add(ScalarParameter);
            }
        }

        for (UMaterialExpressionScalarParameter* WetnessMaskParameter : WetnessMaskParameters)
        {
            UMaterialExpressionConstant* ConstantOne = Cast<UMaterialExpressionConstant>(
                UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                    MaterialFunction,
                    UMaterialExpressionConstant::StaticClass(),
                    WetnessMaskParameter->MaterialExpressionEditorX,
                    WetnessMaskParameter->MaterialExpressionEditorY));
            if (ConstantOne == nullptr)
            {
                FailureReasons.Add(TEXT("Could not create constant replacement for DWC_WetPartDebugUseWetnessMask."));
                return false;
            }
            ConstantOne->R = 1.0f;

            for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
            {
                if (Expression == nullptr || Expression == WetnessMaskParameter)
                {
                    continue;
                }

                for (FExpressionInputIterator InputIt{ Expression }; InputIt; ++InputIt)
                {
                    FExpressionInput* Input = InputIt.operator->();
                    if (Input != nullptr && Input->Expression == WetnessMaskParameter)
                    {
                        Input->Expression = ConstantOne;
                        Input->OutputIndex = 0;
                        Input->InputName = NAME_None;
                    }
                }
            }

            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MaterialFunction, WetnessMaskParameter);
            bModified = true;
        }

        if (bModified)
        {
            MaterialFunction->MarkPackageDirty();
            MaterialFunction->PostEditChange();
        }

        return true;
    }

    bool ConnectDwcCPUVertexWetnessGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        const FWetClothingMaterialSetup::FOptions& Options,
        TArray<FString>&                         FailureReasons)
    {
        if (Options.SimulationMode != EDWCSimulationMode::VertexCPU)
        {
            return true;
        }

        if (Material == nullptr || ApplyCall == nullptr)
        {
            FailureReasons.Add(TEXT("DWC CPU wetness setup requires a material and MF_DWC_ApplyWetness_CPU call."));
            return false;
        }

        ApplyCall->UpdateFromFunctionResource();
        if (!HasInput(ApplyCall, TEXT("Wetness")))
        {
            FailureReasons.Add(FString::Printf(
                TEXT("MF_DWC_ApplyWetness_CPU must expose 'Wetness' input for CPU vertex wetness rendering. Available inputs: %s"),
                *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionInputNames(ApplyCall))));
            return false;
        }

        UMaterialExpressionVertexColor* VertexColor = FindOrCreateVertexColor(Material, -900, -270);
        if (VertexColor == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create VertexColor node for CPU wetness rendering."));
            return false;
        }

        return ConnectChecked(VertexColor, TEXT("R"), ApplyCall, TEXT("Wetness"), FailureReasons);
    }

    bool ConnectDwcWetnessMapGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        const FWetClothingMaterialSetup::FOptions& Options,
        TArray<FString>&                         FailureReasons)
    {
        if (Material == nullptr || ApplyCall == nullptr)
        {
            FailureReasons.Add(TEXT("DWC wetness-map setup requires a material and MF_DWC_ApplyWetness call."));
            return false;
        }

        if (!Options.bConnectWetnessMapPath)
        {
            return true;
        }

        if (!HasInput(ApplyCall, TEXT("WetnessMap")))
        {
            FailureReasons.Add(FString::Printf(
                TEXT("MF_DWC_ApplyWetness_GPU must expose 'WetnessMap' input for DWC wetness-map rendering. Available inputs: %s"),
                *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionInputNames(ApplyCall))));
            return false;
        }

        UMaterialExpressionTextureSampleParameter2D* WetnessMap = FindOrCreateGPUWetnessMapParameter(
            Material,
            -900,
            1220);
        UMaterialExpressionTextureCoordinate* DWCDataUV = FindOrCreateDWCDataTextureCoordinate(
            Material,
            Options.DWCDataUVChannelIndex,
            -1150,
            1220);
        if (WetnessMap == nullptr || DWCDataUV == nullptr)
        {
            FailureReasons.Add(TEXT("DWC material setup could not create the wetness-map parameter nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WetnessMap, FailureReasons);
        bConnected &= ConnectChecked(WetnessMap, TEXT("R"), ApplyCall, TEXT("WetnessMap"), FailureReasons);
        return bConnected;
    }

    bool ConnectDwcDebugBaseColorGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        UMaterialExpressionMaterialFunctionCall* DebugCall,
        const FWetClothingMaterialSetup::FOptions& Options,
        const FString&                           ApplyBaseColorOutput,
        TArray<FString>&                         FailureReasons)
    {
        if (Material == nullptr || ApplyCall == nullptr || DebugCall == nullptr)
        {
            FailureReasons.Add(TEXT("DWC debug base-color setup requires a material, apply function call, and debug function call."));
            return false;
        }

        if (Options.SimulationMode != EDWCSimulationMode::WetnessMapGPU)
        {
            UMaterialExpressionScalarParameter* DebugStrength = FindOrCreateScalarParameter(
                Material,
                TEXT("DWC_WetPartDebugStrength"),
                0.0f,
                -900,
                430);
            UMaterialExpressionVertexColor* VertexColor = FindOrCreateVertexColor(Material, -900, -270);
            UMaterialExpressionAppendVector* DebugColorGB = Cast<UMaterialExpressionAppendVector>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -620, -260));
            UMaterialExpressionAppendVector* DebugColorGBA = Cast<UMaterialExpressionAppendVector>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -420, -230));
            UMaterialExpressionMultiply* DebugMask = Cast<UMaterialExpressionMultiply>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -620, 430));
            UMaterialExpressionLinearInterpolate* DebugLerp = Cast<UMaterialExpressionLinearInterpolate>(
                UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -110, -145));

            if (DebugStrength == nullptr || VertexColor == nullptr || DebugColorGB == nullptr ||
                DebugColorGBA == nullptr || DebugMask == nullptr || DebugLerp == nullptr)
            {
                FailureReasons.Add(TEXT("DWC CPU debug setup could not create one or more required nodes."));
                return false;
            }

            bool bConnected = true;
            bConnected &= ConnectChecked(VertexColor, TEXT("R"), DebugMask, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(DebugStrength, FString(), DebugMask, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(VertexColor, TEXT("G"), DebugColorGB, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(VertexColor, TEXT("B"), DebugColorGB, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(DebugColorGB, FString(), DebugColorGBA, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(VertexColor, TEXT("A"), DebugColorGBA, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, DebugLerp, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(DebugColorGBA, FString(), DebugLerp, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(DebugMask, FString(), DebugLerp, TEXT("Alpha"), FailureReasons);
            if (!UMaterialEditingLibrary::ConnectMaterialProperty(DebugLerp, FString(), MP_BaseColor))
            {
                FailureReasons.Add(TEXT("Failed to connect CPU wet-part debug blend to Material BaseColor."));
                bConnected = false;
            }
            return bConnected;
        }

        UMaterialExpressionTextureSampleParameter2D* WetnessMap = FindOrCreateGPUWetnessMapParameter(
            Material,
            -900,
            1220);
        UMaterialExpressionTextureCoordinate* DWCDataUV = FindOrCreateDWCDataTextureCoordinate(
            Material,
            Options.DWCDataUVChannelIndex,
            -1150,
            1220);
        UMaterialExpressionScalarParameter* DebugStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WetPartDebugStrength"),
            0.0f,
            -900,
            430);
        UMaterialExpressionScalarParameter* WetDarkeningStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WetDarkeningStrength"),
            0.35f,
            -900,
            140);
        UMaterialExpressionVertexColor* VertexColor = FindOrCreateVertexColor(Material, -900, -270);
        UMaterialExpressionAppendVector* DebugColorGB = Cast<UMaterialExpressionAppendVector>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -620, -260));
        UMaterialExpressionAppendVector* DebugColorGBA = Cast<UMaterialExpressionAppendVector>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionAppendVector::StaticClass(), -420, -230));
        UMaterialExpressionMultiply* WetDarkeningMask = Cast<UMaterialExpressionMultiply>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -620, 140));
        UMaterialExpressionConstant3Vector* WetDarkenTarget = Cast<UMaterialExpressionConstant3Vector>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -420, 35));
        UMaterialExpressionLinearInterpolate* WetBaseColorLerp = Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -250, 40));
        UMaterialExpressionMultiply* DebugMask = Cast<UMaterialExpressionMultiply>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionMultiply::StaticClass(), -620, 430));
        UMaterialExpressionLinearInterpolate* DebugLerp = Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionLinearInterpolate::StaticClass(), -110, -145));

        if (WetnessMap == nullptr || DWCDataUV == nullptr || DebugStrength == nullptr || WetDarkeningStrength == nullptr ||
            VertexColor == nullptr || DebugColorGB == nullptr || DebugColorGBA == nullptr ||
            WetDarkeningMask == nullptr || WetDarkenTarget == nullptr || WetBaseColorLerp == nullptr ||
            DebugMask == nullptr || DebugLerp == nullptr)
        {
            FailureReasons.Add(TEXT("DWC GPU debug setup could not create one or more required nodes."));
            return false;
        }

        WetDarkenTarget->Constant = FLinearColor::Black;

        bool bConnected = true;
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WetnessMap, FailureReasons);
        bConnected &= ConnectChecked(WetnessMap, TEXT("R"), WetDarkeningMask, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(WetDarkeningStrength, FString(), WetDarkeningMask, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, WetBaseColorLerp, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(WetDarkenTarget, FString(), WetBaseColorLerp, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(WetDarkeningMask, FString(), WetBaseColorLerp, TEXT("Alpha"), FailureReasons);
        bConnected &= ConnectChecked(WetnessMap, TEXT("R"), DebugMask, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(DebugStrength, FString(), DebugMask, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("G"), DebugColorGB, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("B"), DebugColorGB, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(DebugColorGB, FString(), DebugColorGBA, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("A"), DebugColorGBA, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(WetBaseColorLerp, FString(), DebugLerp, TEXT("A"), FailureReasons);
        bConnected &= ConnectChecked(DebugColorGBA, FString(), DebugLerp, TEXT("B"), FailureReasons);
        bConnected &= ConnectChecked(DebugMask, FString(), DebugLerp, TEXT("Alpha"), FailureReasons);
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(DebugLerp, FString(), MP_BaseColor))
        {
            FailureReasons.Add(TEXT("Failed to connect GPU wet-part debug blend to Material BaseColor."));
            bConnected = false;
        }

        return bConnected;
    }

    bool ConnectDwcApplyWetnessNormalGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        const FWetClothingMaterialSetup::FOptions& Options,
        TArray<FString>&                         FailureReasons)
    {
        if (Material == nullptr || ApplyCall == nullptr)
        {
            FailureReasons.Add(TEXT("Normal setup requires a material and MF_DWC_ApplyWetness call."));
            return false;
        }

        if (!Options.bEnableDWCDataUVSampling)
        {
            return true;
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
        UMaterialExpressionTextureCoordinate* WrinkleUV = FindOrCreateDWCDataTextureCoordinate(
            Material,
            Options.DWCDataUVChannelIndex,
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
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
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
        const FWetClothingMaterialSetup::FOptions& Options,
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
        UMaterialExpressionScalarParameter* WetRoughness = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_WetRoughness"),
            0.12f,
            -900,
            230);
        UMaterialExpressionScalarParameter* SurfaceWaterStrength = FindOrCreateScalarParameter(
            Material,
            TEXT("DWC_SurfaceWaterStrength"),
            1.0f,
            -900,
            330);

        if (WetDarkeningStrength == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create DWC_WetDarkeningStrength scalar parameter."));
            bConnected = false;
        }
        else
        {
            bConnected &= ConnectChecked(WetDarkeningStrength, FString(), ApplyCall, TEXT("WetDarkeningStrength"), FailureReasons);
        }
        if (WetRoughness == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create DWC_WetRoughness scalar parameter."));
            bConnected = false;
        }
        else
        {
            bConnected &= ConnectChecked(WetRoughness, FString(), ApplyCall, TEXT("WetRoughness"), FailureReasons);
        }
        if (SurfaceWaterStrength == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create DWC_SurfaceWaterStrength scalar parameter."));
            bConnected = false;
        }
        else
        {
            bConnected &= ConnectChecked(SurfaceWaterStrength, FString(), ApplyCall, TEXT("SurfaceWaterStrength"), FailureReasons);
        }
        bConnected &= ConnectDwcCPUVertexWetnessGraph(Material, ApplyCall, Options, FailureReasons);
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, Options, FailureReasons);
        bConnected &= ConnectDwcWetnessMapGraph(Material, ApplyCall, Options, FailureReasons);

        FString ApplyBaseColorOutput;
        FString ApplyRoughnessOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }

        if (!ApplyBaseColorOutput.IsEmpty())
        {
            bConnected &= ConnectDwcDebugBaseColorGraph(
                Material,
                ApplyCall,
                DebugCall,
                Options,
                ApplyBaseColorOutput,
                FailureReasons);
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
        const FWetClothingMaterialSetup::FOptions& Options,
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
        bConnected &= ConnectDwcCPUVertexWetnessGraph(Material, ApplyCall, Options, FailureReasons);
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, Options, FailureReasons);
        bConnected &= ConnectDwcWetnessMapGraph(Material, ApplyCall, Options, FailureReasons);

        FString ApplyBaseColorOutput;
        FString ApplyRoughnessOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }

        if (!ApplyBaseColorOutput.IsEmpty())
        {
            bConnected &= ConnectDwcDebugBaseColorGraph(
                Material,
                ApplyCall,
                DebugCall,
                Options,
                ApplyBaseColorOutput,
                FailureReasons);
        }
        if (!ApplyRoughnessOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
        {
            FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_ApplyWetness output '%s' to Material Roughness. Available outputs: %s"),
                                               ApplyRoughnessOutput.IsEmpty() ? TEXT("<first>") : *ApplyRoughnessOutput,
                                               *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
            bConnected = false;
        }

        return bConnected;
    }
} // namespace

FWetClothingMaterialSetup::FOptions FWetClothingMaterialSetup::MakeOptionsForAsset(
    const UWetClothingAsset* WetClothingAsset,
    const EDWCSimulationMode SimulationMode)
{
    FOptions Options;
    Options.SimulationMode = SimulationMode;
    if (WetClothingAsset != nullptr)
    {
        Options.DWCDataUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
        Options.bEnableDWCDataUVSampling = Options.DWCDataUVChannelIndex != INDEX_NONE;
        Options.bConnectWetnessMapPath =
            SimulationMode == EDWCSimulationMode::WetnessMapGPU &&
            Options.bEnableDWCDataUVSampling;
    }
    return Options;
}

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
    UMaterialInterface* MaterialInterface,
    const FOptions& Options)
{
    FWetClothingMaterialSetupResult Result;

    if (MaterialInterface == nullptr)
    {
        Result.Message = TEXT("No material is assigned to the selected material slot.");
        return Result;
    }

    if ((Options.bConnectWetnessMapPath || Options.bEnableDWCDataUVSampling) &&
        (Options.DWCDataUVChannelIndex < 0 || Options.DWCDataUVChannelIndex > 7))
    {
        Result.Message = TEXT("DWC material setup requires a generated DWC Data UV channel. Generate DWC Data UV first.");
        return Result;
    }

    UMaterial* Material = Cast<UMaterial>(MaterialInterface);
    const TCHAR* GeneratedDwcMaterialSuffix = GetGeneratedDwcMaterialSuffix(Options.SimulationMode);
    if (Material == nullptr)
    {
        const UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(MaterialInterface);
        if (MaterialInstance == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' is not an editable material asset."), *MaterialInterface->GetName());
            return Result;
        }

        UMaterial* ParentMaterial = const_cast<UMaterial*>(MaterialInstance->GetMaterial());
        if (IsMaterialConfiguredForDwc(MaterialInterface, Options))
        {
            if (ParentMaterial != nullptr)
            {
                FWetClothingMaterialSetupResult ParentRefreshResult = DuplicateAndApplyToMaterialInterface(ParentMaterial, Options);
                if (!ParentRefreshResult.bSucceeded)
                {
                    Result.Message = FString::Printf(
                        TEXT("'%s' is already backed by a DWC material, but the parent material could not be refreshed.\n%s"),
                        *MaterialInterface->GetName(),
                        *ParentRefreshResult.Message);
                    return Result;
                }
            }

            Result.bSucceeded = true;
            Result.bAlreadyConfigured = true;
            Result.ConfiguredMaterial = MaterialInterface;
            Result.Message = FString::Printf(
                TEXT("'%s' is already backed by a DWC material. Refreshed its parent material."),
                *MaterialInterface->GetName());
            return Result;
        }

        if (ParentMaterial == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' has no editable parent material."), *MaterialInterface->GetName());
            return Result;
        }

        FWetClothingMaterialSetupResult ParentResult = DuplicateAndApplyToMaterialInterface(ParentMaterial, Options);
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
            GeneratedDwcMaterialSuffix,
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
    UMaterialFunctionInterface* ApplyFunction = LoadDwcApplyWetnessMaterialFunction(
        Options.SimulationMode,
        &ApplyFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_WetPartDebug"), &DebugFunctionPath);
    if (ApplyFunction == nullptr || DebugFunction == nullptr)
    {
        Result.Message = FString::Printf(
            TEXT("Could not load DWC material functions. Apply: '%s', Debug: '%s'."),
            ApplyFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *ApplyFunctionPath,
            DebugFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *DebugFunctionPath);
        return Result;
    }

    {
        TArray<FString> FailureReasons;
        if (!PrepareDwcApplyWetnessFunction(ApplyFunction, Options, FailureReasons))
        {
            Result.Message = TEXT("Could not prepare DWC apply wetness material function.\n");
            Result.Message += FString::Join(FailureReasons, TEXT("\n"));
            return Result;
        }
    }

    if (HasFunctionCall(Material, ApplyFunction) || HasFunctionCall(Material, DebugFunction))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "RepairWetnessMaterialSetup", "Repair Dynamic Wet Clothes Material Setup"));
        Material->Modify();

        TArray<FString>       FailureReasons;
        const bool            bConfigured = ConfigureExistingDwcMaterial(Material, ApplyFunction, DebugFunction, Options, FailureReasons);
        ReplaceMissingTextureSamplesWithFallbacks(Material);
        const TArray<FString> CompileErrors = bConfigured ? RecompileMaterialAndCollectErrors(Material) : TArray<FString>();
        Material->MarkPackageDirty();

        Result.bSucceeded = bConfigured && CompileErrors.Num() == 0;
        Result.bAlreadyConfigured = Result.bSucceeded;
        Result.ConfiguredMaterial = Result.bSucceeded ? Material : nullptr;
        Result.Message = Result.bSucceeded
                             ? FString::Printf(TEXT("'%s' already contains DWC material functions. Refreshed DWC output connections."), *Material->GetName())
                             : BuildCompileErrorMessage(
                                   FString::Printf(TEXT("'%s' contains DWC material functions but setup refresh failed.\n%s"), *Material->GetName(), *FString::Join(FailureReasons, TEXT("\n"))),
                                   CompileErrors);
        return Result;
    }

    if (UMaterial* ExistingDwcMaterial = LoadExistingDwcMaterialForSource(Material, GeneratedDwcMaterialSuffix))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "ReuseWetnessMaterialSetup", "Reuse Dynamic Wet Clothes Material Setup"));
        ExistingDwcMaterial->Modify();

        TArray<FString>       FailureReasons;
        const bool            bHasDwcFunctionCall = HasFunctionCall(ExistingDwcMaterial, ApplyFunction) || HasFunctionCall(ExistingDwcMaterial, DebugFunction);
        const bool            bConfigured = bHasDwcFunctionCall
                                                ? ConfigureExistingDwcMaterial(ExistingDwcMaterial, ApplyFunction, DebugFunction, Options, FailureReasons)
                                                : CreateDwcMaterialGraph(ExistingDwcMaterial, ApplyFunction, DebugFunction, Options, FailureReasons);
        ReplaceMissingTextureSamplesWithFallbacks(ExistingDwcMaterial);
        const TArray<FString> CompileErrors = bConfigured ? RecompileMaterialAndCollectErrors(ExistingDwcMaterial) : TArray<FString>();
        ExistingDwcMaterial->MarkPackageDirty();

        Result.bSucceeded = bConfigured && CompileErrors.Num() == 0;
        Result.bAlreadyConfigured = true;
        Result.ConfiguredMaterial = Result.bSucceeded ? ExistingDwcMaterial : nullptr;
        Result.Message = Result.bSucceeded
                             ? FString::Printf(TEXT("Reused existing DWC material '%s' and refreshed DWC material setup."), *ExistingDwcMaterial->GetName())
                             : BuildCompileErrorMessage(
                                   FString::Printf(TEXT("Existing DWC material '%s' could not be refreshed.\n%s"), *ExistingDwcMaterial->GetName(), *FString::Join(FailureReasons, TEXT("\n"))),
                                   CompileErrors);
        return Result;
    }

    const FString      OriginalPackageName = Material->GetOutermost()->GetName();
    FString            NewPackageName;
    FString            NewAssetName;
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(
        StripKnownDwcSuffix(OriginalPackageName),
        GeneratedDwcMaterialSuffix,
        NewPackageName,
        NewAssetName);

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
    ReplaceMissingTextureSamplesWithFallbacks(Material);

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
    bConnected &= ConnectDwcCPUVertexWetnessGraph(Material, ApplyCall, Options, FailureReasons);
    bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, Options, FailureReasons);
    bConnected &= ConnectDwcWetnessMapGraph(Material, ApplyCall, Options, FailureReasons);

    FString ApplyBaseColorOutput;
    FString ApplyRoughnessOutput;
    if (!ResolveRequiredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                           *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ResolveRequiredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                           *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ApplyBaseColorOutput.IsEmpty())
    {
        bConnected &= ConnectDwcDebugBaseColorGraph(
            Material,
            ApplyCall,
            DebugCall,
            Options,
            ApplyBaseColorOutput,
            FailureReasons);
    }
    if (!ApplyRoughnessOutput.IsEmpty() && !UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
    {
        FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_ApplyWetness output '%s' to Material Roughness. Available outputs: %s"),
                                           ApplyRoughnessOutput.IsEmpty() ? TEXT("<first>") : *ApplyRoughnessOutput,
                                           *JoinPinNames(GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }

    if (!bConnected)
    {
        Result.Message = TEXT("DWC material setup created nodes but could not connect one or more expected pins.\n");
        Result.Message += FString::Join(FailureReasons, TEXT("\n"));
        return Result;
    }

    const TArray<FString> CompileErrors = RecompileMaterialAndCollectErrors(Material);
    Material->MarkPackageDirty();

    Result.bSucceeded = CompileErrors.Num() == 0;
    Result.ConfiguredMaterial = Result.bSucceeded ? Material : nullptr;
    Result.Message = Result.bSucceeded
                         ? FString::Printf(TEXT("Duplicated the source material and applied DWC material setup to '%s'."), *Material->GetName())
                         : BuildCompileErrorMessage(
                               FString::Printf(TEXT("Duplicated the source material as '%s', but material compilation reported %d error(s)."), *Material->GetName(), CompileErrors.Num()),
                               CompileErrors);
    return Result;
}

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
    UMaterialInterface* MaterialInterface,
    const int32 WrinkleUVChannelIndex,
    const int32 FallbackDWCDataUVChannelIndex)
{
    FOptions Options;
    Options.SimulationMode = EDWCSimulationMode::VertexCPU;
    Options.DWCDataUVChannelIndex = WrinkleUVChannelIndex != INDEX_NONE ? WrinkleUVChannelIndex : FallbackDWCDataUVChannelIndex;
    Options.bEnableDWCDataUVSampling = Options.DWCDataUVChannelIndex != INDEX_NONE;
    Options.bConnectWetnessMapPath = false;
    return DuplicateAndApplyToMaterialInterface(MaterialInterface, Options);
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

    UMaterialFunctionInterface* DebugFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_WetPartDebug"));
    UMaterialFunctionInterface* CPUApplyFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionCPU);
    UMaterialFunctionInterface* GPUApplyFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionGPU);

    return HasFunctionCall(Material, DebugFunction) &&
           (HasFunctionCall(Material, CPUApplyFunction) ||
            HasFunctionCall(Material, GPUApplyFunction));
}

bool FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(
    UMaterialInterface* MaterialInterface,
    const FOptions& Options)
{
    if (!IsMaterialConfiguredForDwc(MaterialInterface))
    {
        return false;
    }

    UMaterial* Material = MaterialInterface != nullptr ? MaterialInterface->GetMaterial() : nullptr;
    UMaterialFunctionInterface* ApplyFunction = LoadDwcApplyWetnessMaterialFunction(Options.SimulationMode);
    UMaterialExpressionMaterialFunctionCall* ApplyCall = FindFunctionCall(Material, ApplyFunction);
    if (ApplyCall == nullptr)
    {
        return false;
    }

    if (Options.SimulationMode == EDWCSimulationMode::VertexCPU)
    {
        return FindScalarParameter(Material, TEXT("DWC_WetRoughness")) != nullptr &&
               FindScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength")) != nullptr &&
               IsFunctionInputConnected(ApplyCall, TEXT("Wetness")) &&
               IsFunctionInputConnected(ApplyCall, TEXT("WetRoughness")) &&
               IsFunctionInputConnected(ApplyCall, TEXT("SurfaceWaterStrength"));
    }

    if (!Options.bConnectWetnessMapPath)
    {
        return true;
    }

    return FindTextureSampleParameter(Material, TEXT("DWC_WetnessMap")) != nullptr &&
           FindScalarParameter(Material, TEXT("DWC_WetRoughness")) != nullptr &&
           FindScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength")) != nullptr &&
           IsFunctionInputConnected(ApplyCall, TEXT("WetnessMap")) &&
           IsFunctionInputConnected(ApplyCall, TEXT("WetRoughness")) &&
           IsFunctionInputConnected(ApplyCall, TEXT("SurfaceWaterStrength"));
}

void FWetClothingMaterialSetup::ValidateGeneratedMaterialOverrides(
    const UWetClothingAsset* WetClothingAsset,
    TArray<FString>& OutMessages)
{
    OutMessages.Reset();
    if (WetClothingAsset == nullptr)
    {
        return;
    }

    const FDWCWetClothingAssetSetupSettings& Setup = WetClothingAsset->GetSetupSettings();
    if (!Setup.bBuildCPUVertexSimulationData && !Setup.bBuildGPUWetnessMapSimulationData)
    {
        return;
    }

    const TArray<int32> WettableSlots = CollectWettableMaterialSlotIndices(*WetClothingAsset);
    if (WettableSlots.IsEmpty())
    {
        return;
    }

    USkeletalMesh* RuntimeMesh = WetClothingAsset->GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutMessages.Add(TEXT("Generated Materials: Assign a runtime skeletal mesh before generating wet materials."));
        return;
    }

    const FWetClothingMaterialSetup::FOptions CPUOptions = MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::VertexCPU);
    const FWetClothingMaterialSetup::FOptions GPUOptions = MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::WetnessMapGPU);
    const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();

    for (const int32 MaterialSlotIndex : WettableSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: wettable material slot is out of range for the runtime mesh."),
                MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = Materials[MaterialSlotIndex].MaterialInterface;
        if (SourceMaterial == nullptr)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: runtime mesh has no source material."),
                MaterialSlotIndex));
            continue;
        }

        const FWetClothingGeneratedWetMaterialOverride* MaterialOverride =
            FindGeneratedWetMaterialOverride(*WetClothingAsset, MaterialSlotIndex);
        UMaterialInterface* CPUWetMaterial = MaterialOverride != nullptr ? MaterialOverride->CPUWetMaterial.Get() : nullptr;
        UMaterialInterface* GPUWetMaterial = MaterialOverride != nullptr ? MaterialOverride->GPUWetMaterial.Get() : nullptr;

        if (MaterialOverride == nullptr ||
            (Setup.bBuildCPUVertexSimulationData && CPUWetMaterial == nullptr) ||
            (Setup.bBuildGPUWetnessMapSimulationData && GPUWetMaterial == nullptr))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: missing generated DWC %s material. Use Generate Materials."),
                MaterialSlotIndex,
                Setup.bBuildCPUVertexSimulationData && CPUWetMaterial == nullptr ? TEXT("CPU") : TEXT("GPU")));
            continue;
        }

        if (MaterialOverride->SourceMaterial != SourceMaterial)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated materials are out of date because the runtime mesh source material changed. Use Generate Materials."),
                MaterialSlotIndex));
            continue;
        }

        if (Setup.bBuildCPUVertexSimulationData &&
            !IsMaterialConfiguredForDwc(CPUWetMaterial, CPUOptions))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated CPU material '%s' is missing DWC CPU material setup. Use Generate Materials."),
                MaterialSlotIndex,
                *GetNameSafe(CPUWetMaterial)));
            continue;
        }

        if (Setup.bBuildGPUWetnessMapSimulationData &&
            !IsMaterialConfiguredForDwc(GPUWetMaterial, GPUOptions))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated GPU material '%s' is missing DWC GPU material setup or wetness-map parameters. Use Generate Materials."),
                MaterialSlotIndex,
                *GetNameSafe(GPUWetMaterial)));
        }
    }
}

bool FWetClothingMaterialSetup::ValidateSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    OutErrorMessage.Reset();

    FString CPUFunctionPath;
    FString GPUFunctionPath;
    FString DebugFunctionPath;
    UMaterialFunctionInterface* CPUFunction =
        LoadDwcApplyWetnessMaterialFunction(EDWCSimulationMode::VertexCPU, &CPUFunctionPath);
    UMaterialFunctionInterface* GPUFunction =
        LoadDwcApplyWetnessMaterialFunction(EDWCSimulationMode::WetnessMapGPU, &GPUFunctionPath);
    UMaterialFunctionInterface* DebugFunction =
        LoadDwcMaterialFunction(DwcWetPartDebugFunction, &DebugFunctionPath);

    if (CPUFunction == nullptr || GPUFunction == nullptr || DebugFunction == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not load DWC material functions. CPU='%s' GPU='%s' Debug='%s'."),
            CPUFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *CPUFunctionPath,
            GPUFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *GPUFunctionPath,
            DebugFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *DebugFunctionPath);
        return false;
    }

    if (DoesWetPartDebugFunctionUseWetnessMask(DebugFunction))
    {
        OutErrorMessage = TEXT("MF_DWC_WetPartDebug still contains DWC_WetPartDebugUseWetnessMask. Run DWC.RepairApplyWetnessFunction to remove the legacy wetness-mask branch.");
        return false;
    }

    return true;
}

bool FWetClothingMaterialSetup::RepairOrUpgradeSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    FString DebugFunctionPath;
    UMaterialFunctionInterface* DebugFunction =
        LoadDwcMaterialFunction(DwcWetPartDebugFunction, &DebugFunctionPath);
    if (DebugFunction == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not load MF_DWC_WetPartDebug material function. Debug='%s'."),
            DebugFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *DebugFunctionPath);
        return false;
    }

    TArray<FString> FailureReasons;
    if (!RemoveWetPartDebugWetnessMask(DebugFunction, FailureReasons))
    {
        OutErrorMessage = FString::Join(FailureReasons, TEXT("\n"));
        return false;
    }

    return ValidateSharedApplyWetnessFunction(OutErrorMessage);
}
