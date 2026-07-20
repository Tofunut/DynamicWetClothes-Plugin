#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "StaticParameterSet.h"
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
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");
    constexpr const TCHAR* GeneratedDwcUnifiedMaterialSuffix = TEXT("_DWC");
    constexpr const TCHAR* GeneratedDwcUnifiedCpuInstanceSuffix = TEXT("_DWC_CPU");
    constexpr const TCHAR* GeneratedDwcUnifiedGpuInstanceSuffix = TEXT("_DWC_GPU");
    constexpr const TCHAR* DwcApplyWetnessFunctionCPU = TEXT("MF_DWC_ApplyWetness_CPU");
    constexpr const TCHAR* DwcApplyWetnessFunctionGPU = TEXT("MF_DWC_ApplyWetness_GPU");

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
        FString*                 OutObjectPath = nullptr)
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
        for (const FWetClothingWettableMaterialSlotState& SlotState : WetClothingAsset.Authored.PartData.EditableWetPartData.WettableMaterialSlots)
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
        const int32              MaterialSlotIndex)
    {
        return WetClothingAsset.Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
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
        if (Expression == nullptr)
        {
            return false;
        }

        const TArray<FString> InputNames =
            UMaterialEditingLibrary::GetMaterialExpressionInputNames(Expression);

        // ?대쫫 ?녿뒗 ?⑥씪 湲곕낯 ?낅젰???ъ슜?섎뒗 ?몃뱶
        if (InputName.IsEmpty())
        {
            return InputNames.IsEmpty();
        }

        return InputNames.Contains(InputName);
    }

    bool IsFunctionInputConnected(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const FName                                    InputName)
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
        int32      NodePosX,
        int32      NodePosY)
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
        UMaterial*  Material,
        const int32 DWCDataUVChannelIndex,
        int32       NodePosX,
        int32       NodePosY)
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
        static const TCHAR* KnownSuffixes[] = {
            GeneratedDwcUnifiedCpuInstanceSuffix,
            GeneratedDwcUnifiedGpuInstanceSuffix,
            GeneratedDwcUnifiedMaterialSuffix
        };
        for (const TCHAR* KnownSuffix : KnownSuffixes)
        {
            if (PackageName.EndsWith(KnownSuffix))
            {
                return PackageName.LeftChop(FCString::Strlen(KnownSuffix));
            }
        }
        return PackageName;
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
        static const FString  CandidateInputNames[] = { TEXT("UVs"), TEXT("Coordinates") };
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
        UMaterialFunction*         MaterialFunction,
        int32&                     OutRedOutputIndex,
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

        int32                           RedOutputIndex = INDEX_NONE;
        TArray<FExpressionInput*>       WetnessInputs;
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
        UMaterialFunctionInterface*                ApplyFunction,
        const FWCAMaterialGenerator::FOptions& Options,
        TArray<FString>&                           FailureReasons)
    {
        if (Options.SimulationMode != EDWCSimulationMode::VertexCPU)
        {
            return true;
        }

        return EnsureCPUApplyWetnessFunctionUsesExplicitWetnessInput(ApplyFunction, FailureReasons);
    }

} // namespace

namespace
{
    const FName            DwcUseGpuBackendParameterName(TEXT("DWC_UseGPUBackend"));

    FString GetDwcGeneratedAssetStem(const FString& InAssetName)
    {
        FString AssetName = InAssetName;
        if (AssetName.StartsWith(TEXT("MI_")))
        {
            AssetName = AssetName.RightChop(3);
        }
        else if (AssetName.StartsWith(TEXT("M_")))
        {
            AssetName = AssetName.RightChop(2);
        }

        static const TCHAR* KnownSuffixes[] = {
            GeneratedDwcUnifiedCpuInstanceSuffix,
            GeneratedDwcUnifiedGpuInstanceSuffix,
            GeneratedDwcUnifiedMaterialSuffix
        };
        for (const TCHAR* KnownSuffix : KnownSuffixes)
        {
            if (AssetName.EndsWith(KnownSuffix))
            {
                AssetName.LeftChopInline(FCString::Strlen(KnownSuffix));
                break;
            }
        }
        return AssetName;
    }

    FString BuildUnifiedGeneratedMaterialFolder(
        const FWCAMaterialGenerator::FOptions& Options,
        const UObject*                             FallbackSourceAsset)
    {
        if (Options.OwningWetClothingAsset != nullptr)
        {
            const FString WcaPackageName = Options.OwningWetClothingAsset->GetOutermost()->GetName();
            const FString WcaFolder = FPackageName::GetLongPackagePath(WcaPackageName);
            return WcaFolder / TEXT("Generated") / Options.OwningWetClothingAsset->GetName() / TEXT("Materials");
        }

        return FallbackSourceAsset != nullptr
                   ? FPackageName::GetLongPackagePath(FallbackSourceAsset->GetOutermost()->GetName())
                   : FString();
    }

    FString BuildUnifiedBaseMaterialPackageName(
        const UMaterial*                           SourceBaseMaterial,
        const FWCAMaterialGenerator::FOptions& Options)
    {
        if (SourceBaseMaterial == nullptr)
        {
            return FString();
        }

        const FString Folder = BuildUnifiedGeneratedMaterialFolder(Options, SourceBaseMaterial);
        const FString SourceAssetName = FPackageName::GetLongPackageAssetName(
            StripKnownDwcSuffix(SourceBaseMaterial->GetOutermost()->GetName()));
        const FString AssetStem = GetDwcGeneratedAssetStem(SourceAssetName);
        return Folder / FString::Printf(TEXT("M_%s%s"), *AssetStem, GeneratedDwcUnifiedMaterialSuffix);
    }

    FString BuildUnifiedBackendInstancePackageName(
        const UMaterialInterface*                  SourceMaterial,
        const TCHAR*                               BackendSuffix,
        const FWCAMaterialGenerator::FOptions& Options)
    {
        if (SourceMaterial == nullptr || BackendSuffix == nullptr)
        {
            return FString();
        }

        const FString Folder = BuildUnifiedGeneratedMaterialFolder(Options, SourceMaterial);
        const FString SourceAssetName = FPackageName::GetLongPackageAssetName(
            StripKnownDwcSuffix(SourceMaterial->GetOutermost()->GetName()));
        const FString AssetStem = GetDwcGeneratedAssetStem(SourceAssetName);
        return Folder / FString::Printf(TEXT("MI_%s%s"), *AssetStem, BackendSuffix);
    }

    UMaterialExpressionStaticSwitchParameter* CreateDwcBackendStaticSwitch(
        UMaterial*   Material,
        const int32  NodePosX,
        const int32  NodePosY,
        const TCHAR* Description)
    {
        UMaterialExpressionStaticSwitchParameter* Switch = Cast<UMaterialExpressionStaticSwitchParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionStaticSwitchParameter::StaticClass(),
                NodePosX,
                NodePosY));
        if (Switch != nullptr)
        {
            // SetParameterName updates the material parameter bookkeeping; assigning
            // ParameterName directly can leave the editor parameter cache stale.
            Switch->SetParameterName(DwcUseGpuBackendParameterName);
            Switch->DefaultValue = false;
            Switch->Group = TEXT("DWC Backend");
            Switch->Desc = Description;
            Switch->UpdateParameterGuid(false, false);
            Material->UpdateExpressionParameterName(Switch);
        }
        return Switch;
    }

    UMaterialExpressionStaticSwitchParameter* FindConnectedBackendSwitch(
        const UMaterial*                         Material,
        UMaterialExpressionMaterialFunctionCall* CPUApply,
        UMaterialExpressionMaterialFunctionCall* GPUApply,
        const FString&                           RequiredOutputName)
    {
        if (Material == nullptr || CPUApply == nullptr || GPUApply == nullptr)
        {
            return nullptr;
        }

        const int32 CPUOutputIndex = ResolveExpressionOutputIndex(CPUApply, RequiredOutputName, INDEX_NONE);
        const int32 GPUOutputIndex = ResolveExpressionOutputIndex(GPUApply, RequiredOutputName, INDEX_NONE);
        if (CPUOutputIndex == INDEX_NONE || GPUOutputIndex == INDEX_NONE)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionStaticSwitchParameter* Switch = Cast<UMaterialExpressionStaticSwitchParameter>(Expression);
            if (Switch != nullptr &&
                Switch->ParameterName == DwcUseGpuBackendParameterName &&
                Switch->ExpressionGUID.IsValid() &&
                Switch->A.Expression == GPUApply && Switch->A.OutputIndex == GPUOutputIndex &&
                Switch->B.Expression == CPUApply && Switch->B.OutputIndex == CPUOutputIndex)
            {
                return Switch;
            }
        }
        return nullptr;
    }

    bool IsBackendSwitchConnectedForProperty(
        const UMaterial*                         Material,
        const EMaterialProperty                  Property,
        UMaterialExpressionMaterialFunctionCall* CPUApply,
        UMaterialExpressionMaterialFunctionCall* GPUApply,
        const FString&                           RequiredOutputName)
    {
        if (Material == nullptr || CPUApply == nullptr || GPUApply == nullptr)
        {
            return false;
        }

        UMaterialExpression* PropertyInput = UMaterialEditingLibrary::GetMaterialPropertyInputNode(
            const_cast<UMaterial*>(Material),
            Property);
        const UMaterialExpressionStaticSwitchParameter* Switch =
            Cast<UMaterialExpressionStaticSwitchParameter>(PropertyInput);
        return Switch != nullptr &&
               Switch == FindConnectedBackendSwitch(Material, CPUApply, GPUApply, RequiredOutputName);
    }

    bool IsWetnessDebugBaseColorConnected(
        const UMaterial*                         Material,
        UMaterialExpressionMaterialFunctionCall* CPUApply,
        UMaterialExpressionMaterialFunctionCall* GPUApply)
    {
        if (Material == nullptr || CPUApply == nullptr || GPUApply == nullptr)
        {
            return false;
        }

        const UMaterialExpressionStaticSwitchParameter* BaseColorSwitch =
            Cast<UMaterialExpressionStaticSwitchParameter>(
                UMaterialEditingLibrary::GetMaterialPropertyInputNode(
                    const_cast<UMaterial*>(Material), MP_BaseColor));
        if (BaseColorSwitch == nullptr ||
            BaseColorSwitch->ParameterName != DwcUseGpuBackendParameterName ||
            !BaseColorSwitch->ExpressionGUID.IsValid() ||
            Cast<UMaterialExpressionLinearInterpolate>(BaseColorSwitch->A.Expression) == nullptr ||
            Cast<UMaterialExpressionLinearInterpolate>(BaseColorSwitch->B.Expression) == nullptr)
        {
            return false;
        }

        return FindScalarParameter(
                   const_cast<UMaterial*>(Material),
                   FName(TEXT("DWC_WetPartDebugStrength"))) != nullptr;
    }

    bool HasDwcBackendStaticSwitchParameter(const UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        const_cast<UMaterial*>(Material)->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterIds);
        for (int32 Index = 0; Index < ParameterInfos.Num(); ++Index)
        {
            if (ParameterInfos[Index].Name == DwcUseGpuBackendParameterName &&
                ParameterInfos[Index].Association == EMaterialParameterAssociation::GlobalParameter &&
                ParameterIds.IsValidIndex(Index) && ParameterIds[Index].IsValid())
            {
                return true;
            }
        }
        return false;
    }

    bool IsUnifiedDwcMaterial(const UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return false;
        }

        UMaterialFunctionInterface*              CPUFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionCPU);
        UMaterialFunctionInterface*              GPUFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionGPU);
        UMaterialExpressionMaterialFunctionCall* CPUApply = FindFunctionCall(
            const_cast<UMaterial*>(Material), CPUFunction);
        UMaterialExpressionMaterialFunctionCall* GPUApply = FindFunctionCall(
            const_cast<UMaterial*>(Material), GPUFunction);
        if (CPUApply == nullptr || GPUApply == nullptr)
        {
            return false;
        }

        return HasDwcBackendStaticSwitchParameter(Material) &&
               IsWetnessDebugBaseColorConnected(Material, CPUApply, GPUApply) &&
               IsBackendSwitchConnectedForProperty(Material, MP_Roughness, CPUApply, GPUApply, TEXT("Roughness")) &&
               IsBackendSwitchConnectedForProperty(Material, MP_Normal, CPUApply, GPUApply, TEXT("Normal"));
    }

    void DiscardNewGeneratedAsset(UObject* Asset)
    {
        if (Asset == nullptr)
        {
            return;
        }

        FAssetRegistryModule::AssetDeleted(Asset);
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->MarkAsGarbage();
    }

    bool ConnectExpressionToBothApplyCalls(
        UMaterialExpression*                     Source,
        const FString&                           SourceOutput,
        UMaterialExpressionMaterialFunctionCall* CPUApply,
        UMaterialExpressionMaterialFunctionCall* GPUApply,
        const FString&                           InputName,
        TArray<FString>&                         FailureReasons)
    {
        bool bConnected = true;
        bConnected &= ConnectChecked(Source, SourceOutput, CPUApply, InputName, FailureReasons);
        bConnected &= ConnectChecked(Source, SourceOutput, GPUApply, InputName, FailureReasons);
        return bConnected;
    }

    bool ConnectUnifiedNormalInputs(
        UMaterial*                                 Material,
        UMaterialExpressionMaterialFunctionCall*   CPUApply,
        UMaterialExpressionMaterialFunctionCall*   GPUApply,
        const FWCAMaterialGenerator::FOptions& Options,
        TArray<FString>&                           FailureReasons)
    {
        if (Material == nullptr || CPUApply == nullptr || GPUApply == nullptr)
        {
            FailureReasons.Add(TEXT("Unified normal setup requires a material and both DWC apply calls."));
            return false;
        }

        FString              BaseNormalOutputName;
        UMaterialExpression* BaseNormalInput = ResolveMaterialPropertyInputOrFallback(
            Material,
            MP_Normal,
            FVector2D(-1120.0f, 520.0f),
            BaseNormalOutputName);

        if (!Options.bEnableDWCDataUVSampling)
        {
            return ConnectExpressionToBothApplyCalls(
                BaseNormalInput,
                BaseNormalOutputName,
                CPUApply,
                GPUApply,
                TEXT("BaseNormal"),
                FailureReasons);
        }

        UMaterialExpressionTextureSampleParameter2D* WrinkleNormalMap = FindOrCreateTextureSampleParameter(
            Material,
            TEXT("DWC_WrinkleNormalMap"),
            -900,
            670);
        UMaterialExpressionTextureCoordinate* DWCDataUV = FindOrCreateDWCDataTextureCoordinate(
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

        if (BaseNormalInput == nullptr || WrinkleNormalMap == nullptr || DWCDataUV == nullptr ||
            UseWrinkleNormalMap == nullptr || WrinkleStrength == nullptr ||
            WrinkleWetnessMin == nullptr || WrinkleWetnessMax == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create one or more shared wrinkle-normal nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WrinkleNormalMap, FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(BaseNormalInput, BaseNormalOutputName, CPUApply, GPUApply, TEXT("BaseNormal"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WrinkleNormalMap, TEXT("RGB"), CPUApply, GPUApply, TEXT("WrinkleNormal"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(UseWrinkleNormalMap, FString(), CPUApply, GPUApply, TEXT("UseWrinkleNormalMap"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WrinkleStrength, FString(), CPUApply, GPUApply, TEXT("WrinkleStrength"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WrinkleWetnessMin, FString(), CPUApply, GPUApply, TEXT("WrinkleWetnessMin"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WrinkleWetnessMax, FString(), CPUApply, GPUApply, TEXT("WrinkleWetnessMax"), FailureReasons);
        return bConnected;
    }

    bool CreateUnifiedDwcMaterialGraph(
        UMaterial*                                 Material,
        const FWCAMaterialGenerator::FOptions& Options,
        TArray<FString>&                           FailureReasons)
    {
        if (Material == nullptr)
        {
            FailureReasons.Add(TEXT("Cannot configure a null generated material."));
            return false;
        }

        UMaterialFunctionInterface* CPUFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionCPU);
        UMaterialFunctionInterface* GPUFunction = LoadDwcMaterialFunction(DwcApplyWetnessFunctionGPU);
        if (CPUFunction == nullptr || GPUFunction == nullptr)
        {
            FailureReasons.Add(TEXT("Could not load the CPU/GPU DWC apply material functions."));
            return false;
        }

        FWCAMaterialGenerator::FOptions CPUOptions = Options;
        CPUOptions.SimulationMode = EDWCSimulationMode::VertexCPU;
        CPUOptions.bConnectWetnessMapPath = false;
        if (!PrepareDwcApplyWetnessFunction(CPUFunction, CPUOptions, FailureReasons))
        {
            return false;
        }

        FString              BaseColorOutputName;
        UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_BaseColor, FVector2D(-1180.0f, -160.0f), BaseColorOutputName);
        FString              RoughnessOutputName;
        UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_Roughness, FVector2D(-1180.0f, 140.0f), RoughnessOutputName);

        UMaterialExpressionMaterialFunctionCall* CPUApply = CreateFunctionCall(Material, CPUFunction, -430, -240);
        UMaterialExpressionMaterialFunctionCall* GPUApply = CreateFunctionCall(Material, GPUFunction, -430, 250);
        UMaterialExpressionScalarParameter*      WetDarkeningStrength = CreateScalarParameter(Material, TEXT("DWC_WetDarkeningStrength"), 0.35f, -930, 100);
        UMaterialExpressionScalarParameter*      WetRoughness = CreateScalarParameter(Material, TEXT("DWC_WetRoughness"), 0.12f, -930, 190);
        UMaterialExpressionScalarParameter*      SurfaceWaterStrength = CreateScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength"), 1.0f, -930, 280);
        UMaterialExpressionVertexColor*          VertexColor = FindOrCreateVertexColor(Material, -1180, -360);

        if (BaseColorInput == nullptr || RoughnessInput == nullptr || CPUApply == nullptr || GPUApply == nullptr ||
            WetDarkeningStrength == nullptr || WetRoughness == nullptr || SurfaceWaterStrength == nullptr ||
            VertexColor == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create one or more required unified DWC material nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectExpressionToBothApplyCalls(BaseColorInput, BaseColorOutputName, CPUApply, GPUApply, TEXT("BaseColor"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WetDarkeningStrength, FString(), CPUApply, GPUApply, TEXT("WetDarkeningStrength"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(RoughnessInput, RoughnessOutputName, CPUApply, GPUApply, TEXT("BaseRoughness"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(WetRoughness, FString(), CPUApply, GPUApply, TEXT("WetRoughness"), FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(SurfaceWaterStrength, FString(), CPUApply, GPUApply, TEXT("SurfaceWaterStrength"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("R"), CPUApply, TEXT("Wetness"), FailureReasons);
        bConnected &= ConnectUnifiedNormalInputs(Material, CPUApply, GPUApply, Options, FailureReasons);

        UMaterialExpressionTextureSampleParameter2D* WetnessMap = FindOrCreateGPUWetnessMapParameter(Material, -930, 1270);
        UMaterialExpressionTextureCoordinate*        DWCDataUV = FindOrCreateDWCDataTextureCoordinate(
            Material,
            Options.DWCDataUVChannelIndex,
            -1180,
            1270);
        if (WetnessMap == nullptr || DWCDataUV == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create the shared GPU wetness-map input nodes."));
            return false;
        }
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WetnessMap, FailureReasons);
        bConnected &= ConnectExpressionToBothApplyCalls(
            DWCDataUV,
            FString(),
            CPUApply,
            GPUApply,
            TEXT("SurfaceWaterUV"),
            FailureReasons);
        bConnected &= ConnectChecked(WetnessMap, TEXT("R"), GPUApply, TEXT("WetnessMap"), FailureReasons);

        FString CPUBaseColorOutput;
        FString GPUBaseColorOutput;
        FString CPURoughnessOutput;
        FString GPURoughnessOutput;
        FString CPUNormalOutput;
        FString GPUNormalOutput;
        bConnected &= ResolveRequiredOutputName(CPUApply, TEXT("BaseColor"), CPUBaseColorOutput);
        bConnected &= ResolveRequiredOutputName(GPUApply, TEXT("BaseColor"), GPUBaseColorOutput);
        bConnected &= ResolveRequiredOutputName(CPUApply, TEXT("Roughness"), CPURoughnessOutput);
        bConnected &= ResolveRequiredOutputName(GPUApply, TEXT("Roughness"), GPURoughnessOutput);
        bConnected &= ResolveRequiredOutputName(CPUApply, TEXT("Normal"), CPUNormalOutput);
        bConnected &= ResolveRequiredOutputName(GPUApply, TEXT("Normal"), GPUNormalOutput);
        if (!bConnected)
        {
            FailureReasons.Add(TEXT("CPU/GPU DWC apply functions do not expose the required BaseColor, Roughness, and Normal outputs."));
            return false;
        }

        UMaterialExpressionStaticSwitchParameter* BaseColorSwitch = CreateDwcBackendStaticSwitch(Material, 120, -170, TEXT("Selects the compiled DWC backend BaseColor branch."));
        UMaterialExpressionStaticSwitchParameter* RoughnessSwitch = CreateDwcBackendStaticSwitch(Material, 120, 40, TEXT("Selects the compiled DWC backend Roughness branch."));
        UMaterialExpressionStaticSwitchParameter* NormalSwitch = CreateDwcBackendStaticSwitch(Material, 120, 250, TEXT("Selects the compiled DWC backend Normal branch."));

        if (BaseColorSwitch == nullptr || RoughnessSwitch == nullptr || NormalSwitch == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create one or more backend static-switch nodes."));
            return false;
        }

        // A is selected when DWC_UseGPUBackend is true; B is selected when false.
        // UMaterialExpressionStaticSwitchParameter exposes its graph pins as True / False.
        // The internal members are named A / B, but those are not valid pin names for
        // UMaterialEditingLibrary::ConnectMaterialExpressions().
        bConnected &= ConnectChecked(GPUApply, GPUBaseColorOutput, BaseColorSwitch, TEXT("True"), FailureReasons);
        bConnected &= ConnectChecked(CPUApply, CPUBaseColorOutput, BaseColorSwitch, TEXT("False"), FailureReasons);
        bConnected &= ConnectChecked(GPUApply, GPURoughnessOutput, RoughnessSwitch, TEXT("True"), FailureReasons);
        bConnected &= ConnectChecked(CPUApply, CPURoughnessOutput, RoughnessSwitch, TEXT("False"), FailureReasons);
        bConnected &= ConnectChecked(GPUApply, GPUNormalOutput, NormalSwitch, TEXT("True"), FailureReasons);
        bConnected &= ConnectChecked(CPUApply, CPUNormalOutput, NormalSwitch, TEXT("False"), FailureReasons);

        UMaterialExpressionScalarParameter* DebugStrength = CreateScalarParameter(
            Material, TEXT("DWC_WetPartDebugStrength"), 0.0f, 360, -260);
        UMaterialExpressionComponentMask* PartColorGB = Cast<UMaterialExpressionComponentMask>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionComponentMask::StaticClass(), 360, -430));
        UMaterialExpressionAppendVector* PartColor = Cast<UMaterialExpressionAppendVector>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionAppendVector::StaticClass(), 590, -400));
        UMaterialExpressionMultiply* CPUDebugAlpha = Cast<UMaterialExpressionMultiply>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionMultiply::StaticClass(), 710, -250));
        UMaterialExpressionMultiply* GPUDebugAlpha = Cast<UMaterialExpressionMultiply>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionMultiply::StaticClass(), 710, -110));
        UMaterialExpressionLinearInterpolate* CPUBaseColorDebugLerp = Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionLinearInterpolate::StaticClass(), 850, -250));
        UMaterialExpressionLinearInterpolate* GPUBaseColorDebugLerp = Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionLinearInterpolate::StaticClass(), 850, -80));

        if (DebugStrength == nullptr || PartColorGB == nullptr ||
            PartColor == nullptr ||
            CPUDebugAlpha == nullptr || GPUDebugAlpha == nullptr ||
            CPUBaseColorDebugLerp == nullptr || GPUBaseColorDebugLerp == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create Wet Part vertex-color debug nodes."));
            bConnected = false;
        }
        else
        {
            PartColorGB->R = false;
            PartColorGB->G = true;
            PartColorGB->B = true;
            PartColorGB->A = false;

            // UMaterialExpressionVertexColor exposes RGB as its first output and the
            // individual R/G/B/A channels as outputs 1/2/3/4. In some engine versions,
            // these output names are empty through the editor API, so use the known
            // output indices as fallbacks.
            const int32 VertexColorRGBOutputIndex =
                ResolveExpressionOutputIndex(VertexColor, FString(), 0);
            const int32 VertexColorRedOutputIndex =
                ResolveExpressionOutputIndex(VertexColor, TEXT("R"), 1);
            const int32 VertexColorAlphaOutputIndex =
                ResolveExpressionOutputIndex(VertexColor, TEXT("A"), 4);
            const int32 WetnessMapRedOutputIndex =
                ResolveExpressionOutputIndex(WetnessMap, TEXT("R"), INDEX_NONE);

            if (VertexColorRGBOutputIndex == INDEX_NONE ||
                VertexColorRedOutputIndex == INDEX_NONE ||
                VertexColorAlphaOutputIndex == INDEX_NONE ||
                WetnessMapRedOutputIndex == INDEX_NONE)
            {
                FailureReasons.Add(FString::Printf(
                    TEXT("Could not resolve the material outputs required by the Wet Part debug graph. VertexColor outputs: %s. Wetness map outputs: %s"),
                    *JoinPinNames(GetMaterialExpressionOutputNames(VertexColor)),
                    *JoinPinNames(GetMaterialExpressionOutputNames(WetnessMap))));
                bConnected = false;
            }
            else
            {
                // Connect these raw expression inputs directly instead of relying on
                // editor pin-name lookup across engine versions.
                PartColorGB->Input.Connect(VertexColorRGBOutputIndex, VertexColor);
                CPUDebugAlpha->A.Connect(VertexColorRedOutputIndex, VertexColor);
                GPUDebugAlpha->A.Connect(WetnessMapRedOutputIndex, WetnessMap);

                // PartColorGB outputs VertexColor.GB (float2). Append VertexColor.A as the
                // third component so the final debug color is reconstructed from GBA.
                PartColor->B.Connect(VertexColorAlphaOutputIndex, VertexColor);
            }

            bConnected &= ConnectChecked(PartColorGB, FString(), PartColor, TEXT("A"), FailureReasons);

            bConnected &= ConnectChecked(DebugStrength, FString(), CPUDebugAlpha, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(DebugStrength, FString(), GPUDebugAlpha, TEXT("B"), FailureReasons);

            bConnected &= ConnectChecked(CPUApply, CPUBaseColorOutput, CPUBaseColorDebugLerp, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(PartColor, FString(), CPUBaseColorDebugLerp, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(CPUDebugAlpha, FString(), CPUBaseColorDebugLerp, TEXT("Alpha"), FailureReasons);
            bConnected &= ConnectChecked(GPUApply, GPUBaseColorOutput, GPUBaseColorDebugLerp, TEXT("A"), FailureReasons);
            bConnected &= ConnectChecked(PartColor, FString(), GPUBaseColorDebugLerp, TEXT("B"), FailureReasons);
            bConnected &= ConnectChecked(GPUDebugAlpha, FString(), GPUBaseColorDebugLerp, TEXT("Alpha"), FailureReasons);

            bConnected &= ConnectChecked(GPUBaseColorDebugLerp, FString(), BaseColorSwitch, TEXT("True"), FailureReasons);
            bConnected &= ConnectChecked(CPUBaseColorDebugLerp, FString(), BaseColorSwitch, TEXT("False"), FailureReasons);
        }

        if (!UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorSwitch, FString(), MP_BaseColor))
        {
            FailureReasons.Add(TEXT("Failed to connect the unified DWC BaseColor/debug output."));
            bConnected = false;
        }
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(RoughnessSwitch, FString(), MP_Roughness))
        {
            FailureReasons.Add(TEXT("Failed to connect the unified DWC Roughness output."));
            bConnected = false;
        }
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(NormalSwitch, FString(), MP_Normal))
        {
            FailureReasons.Add(TEXT("Failed to connect the unified DWC Normal output."));
            bConnected = false;
        }

        if (bConnected)
        {
            Material->UpdateCachedExpressionData();
        }
        return bConnected;
    }

    UMaterial* CreateOrLoadUnifiedDwcBaseMaterial(
        UMaterial*                                 SourceBaseMaterial,
        const FWCAMaterialGenerator::FOptions& Options,
        FString&                                   OutErrorMessage,
        bool&                                      bOutReusedExisting)
    {
        bOutReusedExisting = false;
        if (SourceBaseMaterial == nullptr)
        {
            OutErrorMessage = TEXT("The source material interface has no editable base material.");
            return nullptr;
        }

        const FString GeneratedPackageName = BuildUnifiedBaseMaterialPackageName(SourceBaseMaterial, Options);
        if (GeneratedPackageName.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not determine a deterministic package path for the shared DWC material.");
            return nullptr;
        }
        const FString GeneratedAssetName = FPackageName::GetLongPackageAssetName(GeneratedPackageName);
        const FString GeneratedObjectPath = GeneratedPackageName + TEXT(".") + GeneratedAssetName;

        UObject* ExistingObject = LoadObject<UObject>(nullptr, *GeneratedObjectPath);
        if (ExistingObject != nullptr)
        {
            UMaterial* GeneratedMaterial = Cast<UMaterial>(ExistingObject);
            if (GeneratedMaterial == nullptr)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Generated material path '%s' is occupied by '%s' (%s)."),
                    *GeneratedObjectPath,
                    *GetNameSafe(ExistingObject),
                    *GetNameSafe(ExistingObject->GetClass()));
                return nullptr;
            }

            bOutReusedExisting = true;
            GeneratedMaterial->Modify();

            auto RebuildUnifiedGraphFromSource = [&]() -> bool
            {
                // Generated DWC materials are fully owned outputs. Reset the object from the
                // current source graph, then recreate all DWC nodes and connections in one pass.
                UEngine::CopyPropertiesForUnrelatedObjects(SourceBaseMaterial, GeneratedMaterial);
                ReplaceMissingTextureSamplesWithFallbacks(GeneratedMaterial);
                TArray<FString> FailureReasons;
                if (!CreateUnifiedDwcMaterialGraph(GeneratedMaterial, Options, FailureReasons))
                {
                    OutErrorMessage = TEXT("Could not rebuild the unified CPU/GPU DWC material graph.\n") + FString::Join(FailureReasons, TEXT("\n"));
                    return false;
                }
                return true;
            };

            if (!IsUnifiedDwcMaterial(GeneratedMaterial))
            {
                if (!RebuildUnifiedGraphFromSource())
                {
                    return nullptr;
                }
            }
            else
            {
                FindOrCreateDWCDataTextureCoordinate(GeneratedMaterial, Options.DWCDataUVChannelIndex, -1180, 1270);
            }

            GeneratedMaterial->UpdateCachedExpressionData();
            GeneratedMaterial->PostEditChange();
            TArray<FString> CompileErrors = RecompileMaterialAndCollectErrors(GeneratedMaterial);
            if (!CompileErrors.IsEmpty())
            {
                // A graph may look structurally complete while containing stale broken links.
                // Perform one full source-based rebuild before reporting a failure.
                if (!RebuildUnifiedGraphFromSource())
                {
                    return nullptr;
                }
                CompileErrors = RecompileMaterialAndCollectErrors(GeneratedMaterial);
            }
            GeneratedMaterial->MarkPackageDirty();
            if (!CompileErrors.IsEmpty())
            {
                OutErrorMessage = BuildCompileErrorMessage(
                    FString::Printf(TEXT("Unified generated material '%s' failed to compile after full rebuild."), *GetNameSafe(GeneratedMaterial)),
                    CompileErrors);
                return nullptr;
            }
            return GeneratedMaterial;
        }

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        UMaterial*         DuplicatedMaterial = Cast<UMaterial>(AssetToolsModule.Get().DuplicateAsset(
            GeneratedAssetName,
            FPackageName::GetLongPackagePath(GeneratedPackageName),
            SourceBaseMaterial));
        if (DuplicatedMaterial == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to duplicate '%s' as '%s'."), *GetNameSafe(SourceBaseMaterial), *GeneratedObjectPath);
            return nullptr;
        }

        DuplicatedMaterial->Modify();
        ReplaceMissingTextureSamplesWithFallbacks(DuplicatedMaterial);
        TArray<FString> FailureReasons;
        if (!CreateUnifiedDwcMaterialGraph(DuplicatedMaterial, Options, FailureReasons))
        {
            OutErrorMessage = TEXT("Could not create the unified CPU/GPU DWC material graph.\n") + FString::Join(FailureReasons, TEXT("\n"));
            DiscardNewGeneratedAsset(DuplicatedMaterial);
            return nullptr;
        }

        DuplicatedMaterial->UpdateCachedExpressionData();
        DuplicatedMaterial->PostEditChange();
        const TArray<FString> CompileErrors = RecompileMaterialAndCollectErrors(DuplicatedMaterial);
        DuplicatedMaterial->MarkPackageDirty();
        if (!CompileErrors.IsEmpty())
        {
            OutErrorMessage = BuildCompileErrorMessage(
                FString::Printf(TEXT("Unified generated material '%s' failed to compile."), *GetNameSafe(DuplicatedMaterial)),
                CompileErrors);
            DiscardNewGeneratedAsset(DuplicatedMaterial);
            return nullptr;
        }
        return DuplicatedMaterial;
    }

    bool SetDwcBackendStaticSwitchOverride(
        UMaterialInstanceConstant* Instance,
        UMaterialInterface*        GeneratedParent,
        const bool                 bUseGPUBackend,
        FString&                   OutErrorMessage)
    {
        if (Instance == nullptr || GeneratedParent == nullptr)
        {
            OutErrorMessage = TEXT("Static backend permutation setup requires an instance and parent material.");
            return false;
        }

        if (UMaterial* ParentMaterial = Cast<UMaterial>(GeneratedParent))
        {
            ParentMaterial->UpdateCachedExpressionData();
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        GeneratedParent->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterIds);

        int32 ParameterIndex = INDEX_NONE;
        for (int32 Index = 0; Index < ParameterInfos.Num(); ++Index)
        {
            if (ParameterInfos[Index].Name == DwcUseGpuBackendParameterName &&
                ParameterInfos[Index].Association == EMaterialParameterAssociation::GlobalParameter)
            {
                ParameterIndex = Index;
                break;
            }
        }

        if (ParameterIndex == INDEX_NONE || !ParameterIds.IsValidIndex(ParameterIndex) ||
            !ParameterIds[ParameterIndex].IsValid())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Parent material '%s' does not expose a valid %s static parameter."),
                *GetPathNameSafe(GeneratedParent),
                *DwcUseGpuBackendParameterName.ToString());
            return false;
        }

        FStaticParameterSet     StaticParameters = Instance->GetStaticParameters();
        FStaticSwitchParameter* ExistingParameter = StaticParameters.StaticSwitchParameters.FindByPredicate(
            [&](const FStaticSwitchParameter& Parameter)
            {
                return Parameter.ParameterInfo == ParameterInfos[ParameterIndex];
            });

        if (ExistingParameter != nullptr)
        {
            ExistingParameter->Value = bUseGPUBackend;
            ExistingParameter->bOverride = true;
            ExistingParameter->ExpressionGUID = ParameterIds[ParameterIndex];
        }
        else
        {
            StaticParameters.StaticSwitchParameters.Add(FStaticSwitchParameter(
                ParameterInfos[ParameterIndex],
                bUseGPUBackend,
                true,
                ParameterIds[ParameterIndex]));
        }

        // Apply the complete static parameter set in one operation. This avoids the
        // MaterialEditingLibrary lookup path failing on a freshly rebuilt parent cache.
        Instance->UpdateStaticPermutation(StaticParameters, nullptr);
        Instance->UpdateCachedData();
        return true;
    }

    UMaterialInstanceConstant* CreateOrUpdateBackendMaterialInstance(
        UMaterialInterface*                        SourceMaterial,
        UMaterial*                                 GeneratedParent,
        const TCHAR*                               Suffix,
        const FWCAMaterialGenerator::FOptions& Options,
        const bool                                 bUseGPUBackend,
        FString&                                   OutErrorMessage,
        bool&                                      bOutReusedExisting)
    {
        bOutReusedExisting = false;
        if (SourceMaterial == nullptr || GeneratedParent == nullptr)
        {
            OutErrorMessage = TEXT("Backend material instance creation requires a source material and generated parent.");
            return nullptr;
        }

        const FString PackageName = BuildUnifiedBackendInstancePackageName(SourceMaterial, Suffix, Options);
        if (PackageName.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not determine a deterministic package path for the backend material instance.");
            return nullptr;
        }
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;

        UObject*                   ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath);
        UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(ExistingObject);
        if (ExistingObject != nullptr && Instance == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Generated material instance path '%s' is occupied by '%s' (%s)."),
                *ObjectPath,
                *GetNameSafe(ExistingObject),
                *GetNameSafe(ExistingObject->GetClass()));
            return nullptr;
        }

        const bool bCreatedNewInstance = Instance == nullptr;
        if (bCreatedNewInstance)
        {
            UPackage* Package = CreatePackage(*PackageName);
            Instance = NewObject<UMaterialInstanceConstant>(
                Package,
                *AssetName,
                RF_Public | RF_Standalone | RF_Transactional);
            if (Instance == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Could not create generated material instance '%s'."), *ObjectPath);
                return nullptr;
            }
            FAssetRegistryModule::AssetCreated(Instance);
        }
        else
        {
            bOutReusedExisting = true;
        }

        if (const UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(SourceMaterial))
        {
            CopyMaterialInstanceOverrides(SourceInstance, Instance, GeneratedParent);
        }
        else
        {
            Instance->Modify();
            Instance->SetParentEditorOnly(GeneratedParent);
        }

        FString StaticSwitchError;
        if (!SetDwcBackendStaticSwitchOverride(
                Instance,
                GeneratedParent,
                bUseGPUBackend,
                StaticSwitchError))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not set %s=%s on '%s'. %s"),
                *DwcUseGpuBackendParameterName.ToString(),
                bUseGPUBackend ? TEXT("true") : TEXT("false"),
                *ObjectPath,
                *StaticSwitchError);

            // Do not immediately garbage a newly created material asset here. Material
            // compilation and editor refresh can still hold references to it. Leaving the
            // deterministic output unreferenced is safe and lets the next Generate repair it.
            return nullptr;
        }

        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
        Instance->PostEditChange();
        Instance->MarkPackageDirty();
        if (UPackage* Package = Instance->GetOutermost())
        {
            Package->MarkPackageDirty();
        }
        return Instance;
    }
} // namespace

FWCAMaterialGenerator::FOptions FWCAMaterialGenerator::MakeOptionsForAsset(
    const UWetClothingAsset* WetClothingAsset,
    const EDWCSimulationMode SimulationMode)
{
    FOptions Options;
    Options.SimulationMode = SimulationMode;
    if (WetClothingAsset != nullptr)
    {
        Options.OwningWetClothingAsset = WetClothingAsset;
        Options.DWCDataUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
        Options.bEnableDWCDataUVSampling = Options.DWCDataUVChannelIndex != INDEX_NONE;
        Options.bConnectWetnessMapPath =
            SimulationMode == EDWCSimulationMode::WetnessMapGPU &&
            Options.bEnableDWCDataUVSampling;
    }
    return Options;
}

FWetClothingUnifiedMaterialSetupResult FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(
    UMaterialInterface* SourceMaterial,
    const FOptions&     Options)
{
    FWetClothingUnifiedMaterialSetupResult Result;
    if (SourceMaterial == nullptr)
    {
        Result.Message = TEXT("No source material is assigned.");
        return Result;
    }

    if (Options.DWCDataUVChannelIndex < 0 || Options.DWCDataUVChannelIndex > 7)
    {
        Result.Message = TEXT("Unified DWC material generation requires a valid DWC Data UV channel.");
        return Result;
    }

    UMaterial* SourceBaseMaterial = const_cast<UMaterial*>(SourceMaterial->GetMaterial());
    if (SourceBaseMaterial == nullptr)
    {
        Result.Message = FString::Printf(TEXT("'%s' has no editable base material."), *GetNameSafe(SourceMaterial));
        return Result;
    }

    FOptions UnifiedOptions = Options;
    UnifiedOptions.SimulationMode = EDWCSimulationMode::VertexCPU;
    UnifiedOptions.bEnableDWCDataUVSampling = true;
    UnifiedOptions.bConnectWetnessMapPath = true;

    FString    BaseError;
    bool       bReusedBase = false;
    UMaterial* GeneratedMaterial = CreateOrLoadUnifiedDwcBaseMaterial(
        SourceBaseMaterial,
        UnifiedOptions,
        BaseError,
        bReusedBase);
    if (GeneratedMaterial == nullptr)
    {
        Result.Message = BaseError;
        return Result;
    }

    FString                    CPUError;
    FString                    GPUError;
    bool                       bReusedCPU = false;
    bool                       bReusedGPU = false;
    UMaterialInstanceConstant* CPUInstance = CreateOrUpdateBackendMaterialInstance(
        SourceMaterial,
        GeneratedMaterial,
        GeneratedDwcUnifiedCpuInstanceSuffix,
        UnifiedOptions,
        false,
        CPUError,
        bReusedCPU);
    UMaterialInstanceConstant* GPUInstance = CreateOrUpdateBackendMaterialInstance(
        SourceMaterial,
        GeneratedMaterial,
        GeneratedDwcUnifiedGpuInstanceSuffix,
        UnifiedOptions,
        true,
        GPUError,
        bReusedGPU);

    if (CPUInstance == nullptr || GPUInstance == nullptr)
    {
        // Keep deterministic generated assets alive on failure. Shader compilation and
        // Material Editor refresh can retain transient references; immediate MarkAsGarbage
        // caused an access violation in MaterialEditor.dll. The WCA override is not updated,
        // and the next Generate operation reuses and repairs these outputs in place.

        Result.Message = FString::Printf(
            TEXT("Generated the shared DWC material, but backend instance generation failed. CPU: %s GPU: %s"),
            CPUError.IsEmpty() ? TEXT("OK") : *CPUError,
            GPUError.IsEmpty() ? TEXT("OK") : *GPUError);
        return Result;
    }

    Result.bSucceeded = true;
    Result.bAlreadyConfigured = bReusedBase && bReusedCPU && bReusedGPU;
    Result.GeneratedMaterial = GeneratedMaterial;
    Result.CPUMaterialInstance = CPUInstance;
    Result.GPUMaterialInstance = GPUInstance;
    Result.Message = FString::Printf(
        TEXT("%s unified DWC material '%s' with CPU '%s' and GPU '%s' permutations."),
        Result.bAlreadyConfigured ? TEXT("Refreshed") : TEXT("Created"),
        *GetNameSafe(GeneratedMaterial),
        *GetNameSafe(CPUInstance),
        *GetNameSafe(GPUInstance));
    return Result;
}

bool FWCAMaterialGenerator::IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface)
{
    return MaterialInterface != nullptr && IsUnifiedDwcMaterial(MaterialInterface->GetMaterial());
}

bool FWCAMaterialGenerator::IsMaterialConfiguredForDwc(
    UMaterialInterface* MaterialInterface,
    const FOptions&     Options)
{
    if (!IsMaterialConfiguredForDwc(MaterialInterface))
    {
        return false;
    }

    UMaterial*                               Material = MaterialInterface->GetMaterial();
    UMaterialExpressionMaterialFunctionCall* CPUApply = FindFunctionCall(
        Material,
        LoadDwcMaterialFunction(DwcApplyWetnessFunctionCPU));
    UMaterialExpressionMaterialFunctionCall* GPUApply = FindFunctionCall(
        Material,
        LoadDwcMaterialFunction(DwcApplyWetnessFunctionGPU));
    if (CPUApply == nullptr || GPUApply == nullptr ||
        FindTextureSampleParameter(Material, TEXT("DWC_WetnessMap")) == nullptr ||
        FindScalarParameter(Material, TEXT("DWC_WetPartDebugStrength")) == nullptr ||
        FindScalarParameter(Material, TEXT("DWC_WetRoughness")) == nullptr ||
        FindScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength")) == nullptr ||
        !IsFunctionInputConnected(CPUApply, TEXT("Wetness")) ||
        !IsFunctionInputConnected(CPUApply, TEXT("SurfaceWaterUV")) ||
        !IsFunctionInputConnected(GPUApply, TEXT("SurfaceWaterUV")) ||
        !IsFunctionInputConnected(GPUApply, TEXT("WetnessMap")))
    {
        return false;
    }

    const UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(MaterialInterface);
    if (Instance == nullptr)
    {
        return true;
    }

    const bool bConfiguredForGPU = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(
        const_cast<UMaterialInstanceConstant*>(Instance),
        DwcUseGpuBackendParameterName,
        EMaterialParameterAssociation::GlobalParameter);
    return bConfiguredForGPU == (Options.SimulationMode == EDWCSimulationMode::WetnessMapGPU);
}

void FWCAMaterialGenerator::ValidateGeneratedMaterialOverrideReferences(
    const UWetClothingAsset* WetClothingAsset,
    TArray<FString>&         OutMessages)
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

        UMaterialInterface*                             SourceMaterial = Materials[MaterialSlotIndex].MaterialInterface;
        const FWetClothingGeneratedWetMaterialOverride* MaterialOverride =
            FindGeneratedWetMaterialOverride(*WetClothingAsset, MaterialSlotIndex);
        UMaterial*          GeneratedMaterial = MaterialOverride != nullptr ? MaterialOverride->GeneratedMaterial.Get() : nullptr;
        UMaterialInterface* CPUMaterialInstance = MaterialOverride != nullptr ? MaterialOverride->CPUMaterialInstance.Get() : nullptr;
        UMaterialInterface* GPUMaterialInstance = MaterialOverride != nullptr ? MaterialOverride->GPUMaterialInstance.Get() : nullptr;

        if (SourceMaterial == nullptr)
        {
            OutMessages.Add(FString::Printf(TEXT("Slot %d: runtime mesh has no source material."), MaterialSlotIndex));
        }
        else if (MaterialOverride == nullptr || GeneratedMaterial == nullptr ||
                 CPUMaterialInstance == nullptr || GPUMaterialInstance == nullptr)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: missing unified generated DWC material or backend permutation. Use Generate Materials."),
                MaterialSlotIndex));
        }
        else if (MaterialOverride->SourceMaterial != SourceMaterial)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated materials reference an outdated source material. Use Generate Materials."),
                MaterialSlotIndex));
        }
        else if (CPUMaterialInstance->GetMaterial() != GeneratedMaterial ||
                 GPUMaterialInstance->GetMaterial() != GeneratedMaterial)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: CPU/GPU material permutations no longer share the recorded generated parent. Use Generate Materials."),
                MaterialSlotIndex));
        }
    }
}

void FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(
    const UWetClothingAsset* WetClothingAsset,
    TArray<FString>&         OutMessages)
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

    const FWCAMaterialGenerator::FOptions CPUOptions = MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::VertexCPU);
    const FWCAMaterialGenerator::FOptions GPUOptions = MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::WetnessMapGPU);
    const TArray<FSkeletalMaterial>&          Materials = RuntimeMesh->GetMaterials();

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
        UMaterial*          GeneratedMaterial = MaterialOverride != nullptr ? MaterialOverride->GeneratedMaterial.Get() : nullptr;
        UMaterialInterface* CPUMaterialInstance = MaterialOverride != nullptr ? MaterialOverride->CPUMaterialInstance.Get() : nullptr;
        UMaterialInterface* GPUMaterialInstance = MaterialOverride != nullptr ? MaterialOverride->GPUMaterialInstance.Get() : nullptr;

        if (MaterialOverride == nullptr || GeneratedMaterial == nullptr ||
            CPUMaterialInstance == nullptr || GPUMaterialInstance == nullptr)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: missing unified generated DWC material or backend permutation. Use Generate Materials."),
                MaterialSlotIndex));
            continue;
        }

        if (MaterialOverride->SourceMaterial != SourceMaterial)
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated materials are out of date because the runtime mesh source material changed. Use Generate Materials."),
                MaterialSlotIndex));
            continue;
        }

        if (!IsUnifiedDwcMaterial(GeneratedMaterial) ||
            (CPUMaterialInstance != nullptr && CPUMaterialInstance->GetMaterial() != GeneratedMaterial) ||
            (GPUMaterialInstance != nullptr && GPUMaterialInstance->GetMaterial() != GeneratedMaterial))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated material permutations do not share the recorded unified parent. Use Generate Materials."),
                MaterialSlotIndex));
            continue;
        }

        if (!IsMaterialConfiguredForDwc(CPUMaterialInstance, CPUOptions))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated CPU material '%s' is missing DWC CPU material setup. Use Generate Materials."),
                MaterialSlotIndex,
                *GetNameSafe(CPUMaterialInstance)));
            continue;
        }

        if (!IsMaterialConfiguredForDwc(GPUMaterialInstance, GPUOptions))
        {
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated GPU material '%s' is missing DWC GPU material setup or wetness-map parameters. Use Generate Materials."),
                MaterialSlotIndex,
                *GetNameSafe(GPUMaterialInstance)));
        }
    }
}

bool FWCAMaterialGenerator::ValidateSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    OutErrorMessage.Reset();

    FString                     CPUFunctionPath;
    FString                     GPUFunctionPath;
    UMaterialFunctionInterface* CPUFunction =
        LoadDwcApplyWetnessMaterialFunction(EDWCSimulationMode::VertexCPU, &CPUFunctionPath);
    UMaterialFunctionInterface* GPUFunction =
        LoadDwcApplyWetnessMaterialFunction(EDWCSimulationMode::WetnessMapGPU, &GPUFunctionPath);

    if (CPUFunction == nullptr || GPUFunction == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not load DWC apply material functions. CPU='%s' GPU='%s'."),
            CPUFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *CPUFunctionPath,
            GPUFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *GPUFunctionPath);
        return false;
    }

    return true;
}

bool FWCAMaterialGenerator::RepairOrUpgradeSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    return ValidateSharedApplyWetnessFunction(OutErrorMessage);
}
