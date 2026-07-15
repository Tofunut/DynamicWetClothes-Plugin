#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
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

    bool ConnectChecked(UMaterialExpression* FromExpression, const FString& FromOutputName, UMaterialExpression* ToExpression, const FString& ToInputName, TArray<FString>& FailureReasons);
    bool ConnectFunctionOutputChecked(UMaterialExpression* FromExpression, const FString& FromOutputName, UMaterialExpressionFunctionOutput* FunctionOutput, TArray<FString>& FailureReasons);
    bool ConnectTextureCoordinateChecked(UMaterialExpression* UVExpression, UMaterialExpressionTextureSampleParameter2D* TextureSample, TArray<FString>& FailureReasons);

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

    bool ResolveConnectedFunctionInput(
        UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const FString& InputName,
        UMaterialExpression*& OutExpression,
        FString& OutOutputName)
    {
        OutExpression = nullptr;
        OutOutputName.Reset();
        if (!FunctionCall) return false;

        const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(FunctionCall);
        const int32 InputIndex = InputNames.IndexOfByKey(InputName);
        FExpressionInput* Input = InputIndex != INDEX_NONE ? FunctionCall->GetInput(InputIndex) : nullptr;
        if (!Input || !Input->Expression) return false;

        OutExpression = Input->Expression;
        TArray<FExpressionOutput>& Outputs = OutExpression->GetOutputs();
        if (Outputs.IsValidIndex(Input->OutputIndex))
        {
            OutOutputName = Outputs[Input->OutputIndex].OutputName.ToString();
        }
        return true;
    }

    UMaterialExpressionConstant* FindOrCreateTaggedScalarConstant(
        UMaterial* Material,
        const FString& Tag,
        const float DefaultValue,
        const int32 NodePosX,
        const int32 NodePosY)
    {
        if (!Material) return nullptr;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression);
            if (Constant && Constant->Desc == Tag) return Constant;
        }

        UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionConstant::StaticClass(), NodePosX, NodePosY));
        if (Constant)
        {
            Constant->R = DefaultValue;
            Constant->Desc = Tag;
        }
        return Constant;
    }

    bool ConnectChecked(
        UMaterialExpression* FromExpression,
        const FString& FromOutputName,
        UMaterialExpression* ToExpression,
        const FString& ToInputName,
        TArray<FString>& FailureReasons);

    UMaterialExpressionScalarParameter* FindOrCreateScalarParameter(
        UMaterial* Material,
        FName ParameterName,
        float DefaultValue,
        int32 NodePosX,
        int32 NodePosY);

    void RemoveObsoleteDwcSurfaceExpressions(UMaterial* Material)
    {
        if (!Material) return;

        TArray<UMaterialExpression*> ExpressionsToDelete;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            const UMaterialExpressionScalarParameter* ScalarParameter =
                Cast<UMaterialExpressionScalarParameter>(Expression);
            const bool bLegacyInternalRefraction =
                Expression->Desc == TEXT("DWC Clear Coat Internal Refraction Normal") ||
                (ScalarParameter &&
                 ScalarParameter->ParameterName == TEXT("DWC_SurfaceWaterInternalRefractionStrength"));
            const bool bDwcClearCoatNode =
                Expression->Desc.StartsWith(TEXT("DWC Clear Coat")) ||
                Expression->Desc.StartsWith(TEXT("DWC Base Clear Coat"));
            if (bLegacyInternalRefraction || bDwcClearCoatNode)
            {
                ExpressionsToDelete.Add(Expression);
            }
        }

        for (UMaterialExpression* Expression : ExpressionsToDelete)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
    }

    bool RestorePreservedMaterialProperty(
        UMaterial* Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        const EMaterialProperty Property,
        const FString& FunctionInput,
        TArray<FString>& FailureReasons)
    {
        if (UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, Property) != ApplyCall)
        {
            return true;
        }

        FString SourceOutput;
        UMaterialExpression* Source = nullptr;
        ResolveConnectedFunctionInput(ApplyCall, FunctionInput, Source, SourceOutput);
        if (!Source || Source->Desc.StartsWith(TEXT("DWC Base Clear Coat")))
        {
            return UMaterialEditingLibrary::DisconnectMaterialProperty(Material, Property);
        }
        if (UMaterialEditingLibrary::ConnectMaterialProperty(Source, SourceOutput, Property))
        {
            return true;
        }

        FailureReasons.Add(FString::Printf(
            TEXT("Failed to restore preserved material property for MF input '%s'."), *FunctionInput));
        return false;
    }

    bool RemoveLegacyDwcClearCoatRendering(
        UMaterial* Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        TArray<FString>& FailureReasons)
    {
        if (!Material || !ApplyCall) return false;
        bool bRestored = true;
        bRestored &= RestorePreservedMaterialProperty(
            Material, ApplyCall, MP_CustomData0, TEXT("BaseClearCoat"), FailureReasons);
        bRestored &= RestorePreservedMaterialProperty(
            Material, ApplyCall, MP_CustomData1, TEXT("BaseClearCoatRoughness"), FailureReasons);
        RemoveObsoleteDwcSurfaceExpressions(Material);
        return bRestored;
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

    UTexture* LoadDefaultPackedWrinkleTexture()
    {
        if (UTexture* DefaultDiffuse = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultDiffuse.DefaultDiffuse")))
        {
            return DefaultDiffuse;
        }

        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"));
    }

    bool IsDefaultNormalPlaceholder(const UTexture* Texture)
    {
        if (Texture == nullptr)
        {
            return false;
        }

        const FString TexturePath = Texture->GetPathName();
        return TexturePath == TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal") ||
               TexturePath == TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal");
    }

    UMaterialExpressionFunctionInput* FindFunctionInput(UMaterialFunction* Function, const FName InputName)
    {
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(Expression);
            if (Input != nullptr && Input->InputName == InputName)
            {
                return Input;
            }
        }

        return nullptr;
    }

    UMaterialExpressionFunctionInput* FindOrCreateFunctionInput(
        UMaterialFunction* Function,
        const FName        InputName,
        EFunctionInputType InputType,
        const FVector4f&   PreviewValue,
        int32              SortPriority,
        int32              NodePosX,
        int32              NodePosY)
    {
        if (UMaterialExpressionFunctionInput* ExistingInput = FindFunctionInput(Function, InputName))
        {
            ExistingInput->InputType = InputType;
            ExistingInput->PreviewValue = PreviewValue;
            ExistingInput->bUsePreviewValueAsDefault = true;
            ExistingInput->SortPriority = SortPriority;
            ExistingInput->ConditionallyGenerateId(false);
            ExistingInput->ValidateName();
            return ExistingInput;
        }

        UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, UMaterialExpressionFunctionInput::StaticClass(), NodePosX, NodePosY));
        if (Input == nullptr)
        {
            return nullptr;
        }

        Input->InputName = InputName;
        Input->InputType = InputType;
        Input->PreviewValue = PreviewValue;
        Input->bUsePreviewValueAsDefault = true;
        Input->SortPriority = SortPriority;
        Input->ConditionallyGenerateId(true);
        Input->ValidateName();
        return Input;
    }

    UMaterialExpressionFunctionOutput* FindFunctionOutput(UMaterialFunction* Function, const FName OutputName)
    {
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(Expression);
            if (Output != nullptr && Output->OutputName == OutputName)
            {
                return Output;
            }
        }

        return nullptr;
    }

    UMaterialExpressionFunctionOutput* FindOrCreateFunctionOutput(
        UMaterialFunction* Function,
        const FName        OutputName,
        int32              SortPriority,
        int32              NodePosX,
        int32              NodePosY)
    {
        if (UMaterialExpressionFunctionOutput* ExistingOutput = FindFunctionOutput(Function, OutputName))
        {
            ExistingOutput->SortPriority = SortPriority;
            ExistingOutput->ConditionallyGenerateId(false);
            ExistingOutput->ValidateName();
            return ExistingOutput;
        }

        UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, UMaterialExpressionFunctionOutput::StaticClass(), NodePosX, NodePosY));
        if (Output == nullptr)
        {
            return nullptr;
        }

        Output->OutputName = OutputName;
        Output->SortPriority = SortPriority;
        Output->ConditionallyGenerateId(true);
        Output->ValidateName();
        return Output;
    }

    bool ValidateApplyWetnessFunctionContract(
        UMaterialFunctionInterface* ApplyFunctionInterface,
        TArray<FString>&            FailureReasons)
    {
        UMaterialFunction* ApplyFunction = Cast<UMaterialFunction>(ApplyFunctionInterface);
        if (ApplyFunction == nullptr)
        {
            FailureReasons.Add(TEXT("MF_DWC_ApplyWetness is not a UMaterialFunction asset."));
            return false;
        }

        static const FName RequiredInputs[] = {
            TEXT("BaseColor"),
            TEXT("BaseRoughness"),
            TEXT("BaseNormal")
        };
        static const FName RequiredOutputs[] = {
            TEXT("BaseColor"),
            TEXT("Roughness"),
            TEXT("Normal")
        };
        static const FName RequiredScalarParameters[] = {
            TEXT("DWC_WetDarkeningStrength"),
            TEXT("DWC_WetRoughness"),
            TEXT("DWC_SurfaceWaterStrength"),
            TEXT("DWC_UseWrinkleNormalMap"),
            TEXT("DWC_WrinkleStrength"),
            TEXT("DWC_WrinkleWetnessMin"),
            TEXT("DWC_WrinkleWetnessMax"),
            TEXT("DWC_UseTransparencyMap"),
            TEXT("DWC_TransparencyStrength"),
            TEXT("DWC_TransparencyWetnessMin"),
            TEXT("DWC_TransparencyWetnessMax"),
            TEXT("DWC_TransparencyUVChannel"),
            TEXT("DWC_WrinkleSuppressionStrength")
        };
        static const FName RequiredTextureParameters[] = {
            TEXT("DWC_WrinkleNormalMap"),
            TEXT("DWC_TransparencyMap")
        };
        static const FString RequiredCustomDescriptions[] = {
            TEXT("DWC ApplyWetness Wrinkle Normal Blend"),
            TEXT("DWC ApplyWetness Runtime Transparency Blend"),
            TEXT("DWC Transparency UV Selector")
        };

        for (const FName InputName : RequiredInputs)
        {
            if (FindFunctionInput(ApplyFunction, InputName) == nullptr)
            {
                FailureReasons.Add(FString::Printf(TEXT("Missing function input '%s'."), *InputName.ToString()));
            }
        }

        for (const FName OutputName : RequiredOutputs)
        {
            const UMaterialExpressionFunctionOutput* Output = FindFunctionOutput(ApplyFunction, OutputName);
            if (Output == nullptr)
            {
                FailureReasons.Add(FString::Printf(TEXT("Missing function output '%s'."), *OutputName.ToString()));
            }
            else if (Output->A.Expression == nullptr)
            {
                FailureReasons.Add(FString::Printf(TEXT("Function output '%s' is not connected."), *OutputName.ToString()));
            }
        }

        TSet<FName> FoundScalarParameters;
        TSet<FName> FoundTextureParameters;
        TSet<FString> FoundCustomDescriptions;
        for (UMaterialExpression* Expression : ApplyFunction->GetExpressions())
        {
            if (const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression))
            {
                FoundScalarParameters.Add(ScalarParameter->ParameterName);
            }
            if (const UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
            {
                FoundTextureParameters.Add(TextureParameter->ParameterName);
            }
            if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
            {
                FoundCustomDescriptions.Add(Custom->Description);
            }
        }

        for (const FName ParameterName : RequiredScalarParameters)
        {
            if (!FoundScalarParameters.Contains(ParameterName))
            {
                FailureReasons.Add(FString::Printf(TEXT("Missing scalar parameter '%s'."), *ParameterName.ToString()));
            }
        }
        for (const FName ParameterName : RequiredTextureParameters)
        {
            if (!FoundTextureParameters.Contains(ParameterName))
            {
                FailureReasons.Add(FString::Printf(TEXT("Missing texture parameter '%s'."), *ParameterName.ToString()));
            }
        }
        for (const FString& Description : RequiredCustomDescriptions)
        {
            if (!FoundCustomDescriptions.Contains(Description))
            {
                FailureReasons.Add(FString::Printf(TEXT("Missing required graph node '%s'."), *Description));
            }
        }

        return FailureReasons.Num() == 0;
    }

    UMaterialExpressionCustom* FindOrCreateApplyWetnessFunctionWrinkleNormalBlend(UMaterialFunction* Function, int32 NodePosX, int32 NodePosY)
    {
        static const FString BlendDescription = TEXT("DWC ApplyWetness Wrinkle Normal Blend");
        static const TCHAR*  BlendCode = TEXT(
            "float3 Base = normalize(BaseNormal);\n"
            "// Match the editor accumulated-preview path: baked wrinkle Z is intentionally ignored.\n"
            "// The preview reconstructs its tangent-space normal from XY with Z = 1 before blending.\n"
            "float2 WrinkleXY = PackedWrinkle.rg * 2.0 - 1.0;\n"
            "float WetnessRange = max(WrinkleWetnessMax - WrinkleWetnessMin, 0.0001);\n"
            "float WetnessT = saturate((Wetness - WrinkleWetnessMin) / WetnessRange);\n"
            "float Strength = max(WrinkleStrength, 0.0) * saturate(UseWrinkleNormalMap) * WetnessT;\n"
            "float3 WeightedWrinkle = normalize(float3(WrinkleXY * Strength, 1.0));\n"
            "return normalize(float3(Base.xy + WeightedWrinkle.xy, Base.z * WeightedWrinkle.z));");

        auto ConfigureBlendNode = [](UMaterialExpressionCustom* Custom)
        {
            Custom->Inputs.Reset();

            FCustomInput& BaseNormal = Custom->Inputs.AddDefaulted_GetRef();
            BaseNormal.InputName = TEXT("BaseNormal");

            FCustomInput& PackedWrinkle = Custom->Inputs.AddDefaulted_GetRef();
            PackedWrinkle.InputName = TEXT("PackedWrinkle");

            FCustomInput& Wetness = Custom->Inputs.AddDefaulted_GetRef();
            Wetness.InputName = TEXT("Wetness");

            FCustomInput& UseWrinkleNormalMap = Custom->Inputs.AddDefaulted_GetRef();
            UseWrinkleNormalMap.InputName = TEXT("UseWrinkleNormalMap");

            FCustomInput& WrinkleStrength = Custom->Inputs.AddDefaulted_GetRef();
            WrinkleStrength.InputName = TEXT("WrinkleStrength");

            FCustomInput& WrinkleWetnessMin = Custom->Inputs.AddDefaulted_GetRef();
            WrinkleWetnessMin.InputName = TEXT("WrinkleWetnessMin");

            FCustomInput& WrinkleWetnessMax = Custom->Inputs.AddDefaulted_GetRef();
            WrinkleWetnessMax.InputName = TEXT("WrinkleWetnessMax");

            Custom->OutputType = CMOT_Float3;
            Custom->Code = BlendCode;
        };

        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom != nullptr && Custom->Description == BlendDescription)
            {
                ConfigureBlendNode(Custom);
                return Custom;
            }
        }

        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, UMaterialExpressionCustom::StaticClass(), NodePosX, NodePosY));
        if (Custom == nullptr)
        {
            return nullptr;
        }

        Custom->Description = BlendDescription;
        ConfigureBlendNode(Custom);
        return Custom;
    }

    UMaterialExpressionCustom* FindLegacyApplyWetnessFunctionTransparencyPreviewBlend(UMaterialFunction* Function)
    {
        static const FString BlendDescription = TEXT("DWC ApplyWetness Transparency Preview Blend");
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom != nullptr && Custom->Description == BlendDescription)
            {
                return Custom;
            }
        }
        return nullptr;
    }

    void RemoveLegacyApplyWetnessFunctionTransparencyPreviewGraph(UMaterialFunction* Function)
    {
        if (Function == nullptr)
        {
            return;
        }

        static const TSet<FName> LegacyScalarParameters = {
            TEXT("DWC_UseTransparencyPreview"),
            TEXT("DWC_TransparencyPreviewStrength"),
            TEXT("DWC_TransparencyPreviewWetness"),
            TEXT("DWC_TransparencyPreviewDebug")
        };

        TArray<UMaterialExpression*> ExpressionsToRemove;
        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression);
            const UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            if ((Custom != nullptr && Custom->Description == TEXT("DWC ApplyWetness Transparency Preview Blend")) ||
                (Scalar != nullptr && LegacyScalarParameters.Contains(Scalar->ParameterName)) ||
                (Texture != nullptr && Texture->ParameterName == TEXT("DWC_TransparencyPreviewMap")))
            {
                ExpressionsToRemove.Add(Expression);
            }
        }

        for (UMaterialExpression* Expression : ExpressionsToRemove)
        {
            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
        }
    }

    UMaterialExpressionCustom* FindApplyWetnessFunctionRuntimeTransparencyBlend(UMaterialFunction* Function)
    {
        static const FString BlendDescription = TEXT("DWC ApplyWetness Runtime Transparency Blend");
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom != nullptr && Custom->Description == BlendDescription)
            {
                return Custom;
            }
        }
        return nullptr;
    }

    UMaterialExpressionCustom* FindOrCreateApplyWetnessFunctionRuntimeTransparencyBlend(
        UMaterialFunction* Function,
        int32 NodePosX,
        int32 NodePosY)
    {
        static const FString BlendDescription = TEXT("DWC ApplyWetness Runtime Transparency Blend");
        static const TCHAR* BlendCode = TEXT(
            "float WetnessRange = max(TransparencyWetnessMax - TransparencyWetnessMin, 0.0001);\n"
            "float WetnessT = saturate((Wetness - TransparencyWetnessMin) / WetnessRange);\n"
            "float WrinkleSuppression = saturate(WrinkleCoverage * max(WrinkleSuppressionStrength, 0.0) * saturate(UseWrinkleNormalMap));\n"
            "float FinalTransparency = saturate(TransparencyAlpha * max(TransparencyStrength, 0.0) * WetnessT * "
            "saturate(UseTransparencyMap) * (1.0 - WrinkleSuppression));\n"
            "return lerp(BaseColor, TransparencyColor, FinalTransparency);");

        auto ConfigureBlendNode = [](UMaterialExpressionCustom* Custom)
        {
            Custom->Inputs.Reset();
            static const FName InputNames[] =
            {
                TEXT("BaseColor"),
                TEXT("TransparencyColor"),
                TEXT("TransparencyAlpha"),
                TEXT("Wetness"),
                TEXT("WrinkleCoverage"),
                TEXT("UseWrinkleNormalMap"),
                TEXT("UseTransparencyMap"),
                TEXT("TransparencyStrength"),
                TEXT("TransparencyWetnessMin"),
                TEXT("TransparencyWetnessMax"),
                TEXT("WrinkleSuppressionStrength")
            };
            for (const FName InputName : InputNames)
            {
                FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
                Input.InputName = InputName;
            }
            Custom->OutputType = CMOT_Float3;
            Custom->Code = BlendCode;
        };

        if (Function == nullptr)
        {
            return nullptr;
        }

        if (UMaterialExpressionCustom* Existing = FindApplyWetnessFunctionRuntimeTransparencyBlend(Function))
        {
            ConfigureBlendNode(Existing);
            return Existing;
        }

        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionCustom::StaticClass(),
                NodePosX,
                NodePosY));
        if (Custom != nullptr)
        {
            Custom->Description = BlendDescription;
            ConfigureBlendNode(Custom);
        }
        return Custom;
    }

    UMaterialExpressionCustom* FindOrCreateApplyWetnessFunctionTransparencyUVSelector(
        UMaterialFunction* Function,
        int32 NodePosX,
        int32 NodePosY)
    {
        static const FString SelectorDescription = TEXT("DWC Transparency UV Selector");
        static const TCHAR* SelectorCode = TEXT(
            "float Channel = clamp(floor(UVChannel + 0.5), 0.0, 3.0);\n"
            "if (Channel < 0.5) return UV0;\n"
            "if (Channel < 1.5) return UV1;\n"
            "if (Channel < 2.5) return UV2;\n"
            "return UV3;");

        auto ConfigureSelector = [](UMaterialExpressionCustom* Custom)
        {
            Custom->Inputs.Reset();
            static const FName InputNames[] = { TEXT("UV0"), TEXT("UV1"), TEXT("UV2"), TEXT("UV3"), TEXT("UVChannel") };
            for (const FName InputName : InputNames)
            {
                FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
                Input.InputName = InputName;
            }
            Custom->OutputType = CMOT_Float2;
            Custom->Code = SelectorCode;
        };

        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (Custom != nullptr && Custom->Description == SelectorDescription)
            {
                ConfigureSelector(Custom);
                return Custom;
            }
        }

        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionCustom::StaticClass(),
                NodePosX,
                NodePosY));
        if (Custom != nullptr)
        {
            Custom->Description = SelectorDescription;
            ConfigureSelector(Custom);
        }
        return Custom;
    }

    UMaterialExpressionScalarParameter* FindOrCreateFunctionScalarParameter(
        UMaterialFunction* Function,
        const FName        ParameterName,
        float              DefaultValue,
        int32              NodePosX,
        int32              NodePosY)
    {
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                Parameter->DefaultValue = DefaultValue;
                return Parameter;
            }
        }

        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionScalarParameter::StaticClass(),
                NodePosX,
                NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UMaterialExpressionTextureSampleParameter2D* FindOrCreateFunctionWrinkleTextureParameter(
        UMaterialFunction* Function,
        int32              NodePosX,
        int32              NodePosY)
    {
        static const FName ParameterName(TEXT("DWC_WrinkleNormalMap"));
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                Parameter->SamplerType = SAMPLERTYPE_Color;
                if (Parameter->Texture == nullptr || IsDefaultNormalPlaceholder(Parameter->Texture))
                {
                    Parameter->Texture = LoadDefaultPackedWrinkleTexture();
                }
                Parameter->Desc = TEXT("DWC baked wrinkle packed normal/coverage texture. RGB is decoded as tangent-space normal; alpha is reserved for coverage.");
                return Parameter;
            }
        }

        UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionTextureSampleParameter2D::StaticClass(),
                NodePosX,
                NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadDefaultPackedWrinkleTexture();
            Parameter->Desc = TEXT("DWC baked wrinkle packed normal/coverage texture. RGB is decoded as tangent-space normal; alpha is reserved for coverage.");
        }
        return Parameter;
    }

    UMaterialExpressionTextureSampleParameter2D* FindOrCreateFunctionRuntimeTransparencyTextureParameter(
        UMaterialFunction* Function,
        int32 NodePosX,
        int32 NodePosY)
    {
        static const FName ParameterName(TEXT("DWC_TransparencyMap"));
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                Parameter->SamplerType = SAMPLERTYPE_Color;
                if (Parameter->Texture == nullptr)
                {
                    Parameter->Texture = LoadDefaultPackedWrinkleTexture();
                }
                Parameter->Desc = TEXT("DWC runtime packed transparency map. RGB is inner-surface color and A is edited transparency.");
                return Parameter;
            }
        }

        UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionTextureSampleParameter2D::StaticClass(),
                NodePosX,
                NodePosY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = ParameterName;
            Parameter->SamplerType = SAMPLERTYPE_Color;
            Parameter->Texture = LoadDefaultPackedWrinkleTexture();
            Parameter->Desc = TEXT("DWC runtime packed transparency map. RGB is inner-surface color and A is edited transparency.");
        }
        return Parameter;
    }

    UMaterialExpressionTextureCoordinate* FindOrCreateFunctionTransparencyUV(
        UMaterialFunction* Function,
        const int32 CoordinateIndex,
        int32 NodePosX,
        int32 NodePosY)
    {
        const FString Description = FString::Printf(TEXT("DWC Transparency UV%d"), CoordinateIndex);
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            if (TextureCoordinate != nullptr && TextureCoordinate->Desc == Description)
            {
                TextureCoordinate->CoordinateIndex = CoordinateIndex;
                return TextureCoordinate;
            }
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                NodePosX,
                NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = CoordinateIndex;
            TextureCoordinate->Desc = Description;
        }
        return TextureCoordinate;
    }

    UMaterialExpressionTextureCoordinate* FindOrCreateFunctionWrinkleUV0(
        UMaterialFunction* Function,
        int32              NodePosX,
        int32              NodePosY)
    {
        static const FString Description = TEXT("DWC Wrinkle UV0");
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            if (TextureCoordinate != nullptr && TextureCoordinate->Desc == Description)
            {
                TextureCoordinate->CoordinateIndex = 0;
                return TextureCoordinate;
            }
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                NodePosX,
                NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = 0;
            TextureCoordinate->Desc = Description;
        }
        return TextureCoordinate;
    }

    UMaterialExpressionFrac* FindOrCreateFunctionWrinkleUVWrap(
        UMaterialFunction* Function,
        int32              NodePosX,
        int32              NodePosY)
    {
        static const FString Description = TEXT("DWC Wrinkle UV Wrap");
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionFrac* Frac = Cast<UMaterialExpressionFrac>(Expression);
            if (Frac != nullptr && Frac->Desc == Description)
            {
                return Frac;
            }
        }

        UMaterialExpressionFrac* Frac = Cast<UMaterialExpressionFrac>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionFrac::StaticClass(),
                NodePosX,
                NodePosY));
        if (Frac != nullptr)
        {
            Frac->Desc = Description;
        }
        return Frac;
    }

    UMaterialExpressionVertexColor* FindOrCreateFunctionVertexColor(
        UMaterialFunction* Function,
        int32              NodePosX,
        int32              NodePosY)
    {
        if (Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            if (UMaterialExpressionVertexColor* VertexColor = Cast<UMaterialExpressionVertexColor>(Expression))
            {
                return VertexColor;
            }
        }

        return Cast<UMaterialExpressionVertexColor>(
            UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
                Function,
                UMaterialExpressionVertexColor::StaticClass(),
                NodePosX,
                NodePosY));
    }

    void ReplaceFunctionExpressionReferences(
        UMaterialFunction*   Function,
        UMaterialExpression* OldExpression,
        UMaterialExpression* NewExpression)
    {
        if (Function == nullptr || OldExpression == nullptr || NewExpression == nullptr)
        {
            return;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            if (Expression == OldExpression)
            {
                continue;
            }

            for (FExpressionInputIterator It{Expression}; It; ++It)
            {
                FExpressionInput* Input = It.Input;
                if (Input != nullptr && Input->Expression == OldExpression)
                {
                    Input->Connect(0, NewExpression);
                }
            }
        }
    }

    void ReplaceFunctionScalarInputWithParameter(
        UMaterialFunction* Function,
        const FName        InputName,
        const FName        ParameterName,
        float              DefaultValue,
        int32              NodePosX,
        int32              NodePosY)
    {
        UMaterialExpressionFunctionInput* FunctionInput = FindFunctionInput(Function, InputName);
        UMaterialExpressionScalarParameter* Parameter = FindOrCreateFunctionScalarParameter(
            Function,
            ParameterName,
            DefaultValue,
            NodePosX,
            NodePosY);
        if (FunctionInput == nullptr || Parameter == nullptr)
        {
            return;
        }

        ReplaceFunctionExpressionReferences(Function, FunctionInput, Parameter);
        UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, FunctionInput);
    }

    void RemoveObsoleteFunctionInputs(UMaterialFunction* Function, const TSet<FName>& InputNames)
    {
        if (Function == nullptr)
        {
            return;
        }

        TArray<UMaterialExpression*> InputsToRemove;
        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(Expression);
            if (FunctionInput != nullptr && InputNames.Contains(FunctionInput->InputName))
            {
                InputsToRemove.Add(FunctionInput);
            }
        }

        for (UMaterialExpression* Expression : InputsToRemove)
        {
            UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Function, Expression);
        }
    }

    bool EnsureApplyWetnessFunctionInternalGraph(
        UMaterialFunctionInterface* ApplyFunctionInterface,
        TArray<FString>&            FailureReasons)
    {
        UMaterialFunction* ApplyFunction = Cast<UMaterialFunction>(ApplyFunctionInterface);
        if (ApplyFunction == nullptr)
        {
            FailureReasons.Add(TEXT("MF_DWC_ApplyWetness is not an editable UMaterialFunction asset."));
            return false;
        }

        ApplyFunction->Modify();

        ReplaceFunctionScalarInputWithParameter(
            ApplyFunction,
            TEXT("WetDarkeningStrength"),
            TEXT("DWC_WetDarkeningStrength"),
            0.35f,
            -930,
            80);
        ReplaceFunctionScalarInputWithParameter(
            ApplyFunction,
            TEXT("WetRoughness"),
            TEXT("DWC_WetRoughness"),
            0.12f,
            -930,
            170);
        ReplaceFunctionScalarInputWithParameter(
            ApplyFunction,
            TEXT("SurfaceWaterStrength"),
            TEXT("DWC_SurfaceWaterStrength"),
            1.0f,
            -930,
            260);
        RemoveObsoleteFunctionInputs(
            ApplyFunction,
            {
                TEXT("WrinkleNormal"),
                TEXT("WrinkleWetness"),
                TEXT("UseWrinkleNormalMap"),
                TEXT("WrinkleStrength"),
                TEXT("WrinkleWetnessMin"),
                TEXT("WrinkleWetnessMax")
            });

        UMaterialExpressionFunctionInput* BaseNormal = FindOrCreateFunctionInput(
            ApplyFunction,
            TEXT("BaseNormal"),
            FunctionInput_Vector3,
            FVector4f(0.0f, 0.0f, 1.0f, 0.0f),
            100,
            -900,
            500);
        UMaterialExpressionTextureSampleParameter2D* WrinkleNormalMap = FindOrCreateFunctionWrinkleTextureParameter(
            ApplyFunction,
            -900,
            620);
        UMaterialExpressionTextureCoordinate* WrinkleUV0 = FindOrCreateFunctionWrinkleUV0(
            ApplyFunction,
            -1130,
            620);
        UMaterialExpressionFrac* WrinkleUVWrap = FindOrCreateFunctionWrinkleUVWrap(
            ApplyFunction,
            -1020,
            620);
        UMaterialExpressionVertexColor* VertexColor = FindOrCreateFunctionVertexColor(
            ApplyFunction,
            -900,
            760);
        UMaterialExpressionScalarParameter* UseWrinkleNormalMap = FindOrCreateFunctionScalarParameter(
            ApplyFunction,
            TEXT("DWC_UseWrinkleNormalMap"),
            0.0f,
            -900,
            880);
        UMaterialExpressionScalarParameter* WrinkleStrength = FindOrCreateFunctionScalarParameter(
            ApplyFunction,
            TEXT("DWC_WrinkleStrength"),
            1.0f,
            -900,
            970);
        UMaterialExpressionScalarParameter* WrinkleWetnessMin = FindOrCreateFunctionScalarParameter(
            ApplyFunction,
            TEXT("DWC_WrinkleWetnessMin"),
            0.25f,
            -900,
            1060);
        UMaterialExpressionScalarParameter* WrinkleWetnessMax = FindOrCreateFunctionScalarParameter(
            ApplyFunction,
            TEXT("DWC_WrinkleWetnessMax"),
            1.0f,
            -900,
            1150);
        UMaterialExpressionCustom* WrinkleBlend = FindOrCreateApplyWetnessFunctionWrinkleNormalBlend(
            ApplyFunction,
            -520,
            710);
        UMaterialExpressionFunctionOutput* NormalOutput = FindOrCreateFunctionOutput(
            ApplyFunction,
            TEXT("Normal"),
            2,
            -140,
            710);
        UMaterialExpressionFunctionOutput* BaseColorOutput = FindFunctionOutput(ApplyFunction, TEXT("BaseColor"));
        FExpressionInput WetBaseColorInput;
        if (BaseColorOutput != nullptr)
        {
            WetBaseColorInput = BaseColorOutput->A;
            for (int32 UnwrapIndex = 0; UnwrapIndex < 2; ++UnwrapIndex)
            {
                UMaterialExpressionCustom* ExistingBlend = Cast<UMaterialExpressionCustom>(WetBaseColorInput.Expression);
                if (ExistingBlend == nullptr ||
                    (ExistingBlend != FindLegacyApplyWetnessFunctionTransparencyPreviewBlend(ApplyFunction) &&
                     ExistingBlend != FindApplyWetnessFunctionRuntimeTransparencyBlend(ApplyFunction)))
                {
                    break;
                }

                const FCustomInput* ExistingBaseColor = ExistingBlend->Inputs.FindByPredicate(
                    [](const FCustomInput& Input) { return Input.InputName == TEXT("BaseColor"); });
                if (ExistingBaseColor == nullptr || ExistingBaseColor->Input.Expression == nullptr)
                {
                    break;
                }
                WetBaseColorInput = ExistingBaseColor->Input;
            }
        }
        RemoveLegacyApplyWetnessFunctionTransparencyPreviewGraph(ApplyFunction);
        UMaterialExpressionTextureSampleParameter2D* RuntimeTransparencyMap =
            FindOrCreateFunctionRuntimeTransparencyTextureParameter(ApplyFunction, -900, 1270);
        UMaterialExpressionTextureCoordinate* TransparencyUV0 = FindOrCreateFunctionTransparencyUV(ApplyFunction, 0, -1250, 1220);
        UMaterialExpressionTextureCoordinate* TransparencyUV1 = FindOrCreateFunctionTransparencyUV(ApplyFunction, 1, -1250, 1280);
        UMaterialExpressionTextureCoordinate* TransparencyUV2 = FindOrCreateFunctionTransparencyUV(ApplyFunction, 2, -1250, 1340);
        UMaterialExpressionTextureCoordinate* TransparencyUV3 = FindOrCreateFunctionTransparencyUV(ApplyFunction, 3, -1250, 1400);
        UMaterialExpressionScalarParameter* TransparencyUVChannel = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_TransparencyUVChannel"), 0.0f, -1250, 1470);
        UMaterialExpressionCustom* TransparencyUVSelector = FindOrCreateApplyWetnessFunctionTransparencyUVSelector(
            ApplyFunction, -1050, 1320);
        UMaterialExpressionScalarParameter* UseTransparencyMap = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_UseTransparencyMap"), 0.0f, -900, 1380);
        UMaterialExpressionScalarParameter* TransparencyStrength = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_TransparencyStrength"), 0.4f, -900, 1470);
        UMaterialExpressionScalarParameter* TransparencyWetnessMin = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_TransparencyWetnessMin"), 0.0f, -900, 1560);
        UMaterialExpressionScalarParameter* TransparencyWetnessMax = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_TransparencyWetnessMax"), 1.0f, -900, 1650);
        UMaterialExpressionScalarParameter* WrinkleSuppressionStrength = FindOrCreateFunctionScalarParameter(
            ApplyFunction, TEXT("DWC_WrinkleSuppressionStrength"), 0.6f, -900, 1740);
        UMaterialExpressionCustom* RuntimeTransparencyBlend = FindOrCreateApplyWetnessFunctionRuntimeTransparencyBlend(
            ApplyFunction, -420, 1400);

        if (BaseNormal == nullptr ||
            WrinkleNormalMap == nullptr ||
            WrinkleUV0 == nullptr ||
            WrinkleUVWrap == nullptr ||
            VertexColor == nullptr ||
            UseWrinkleNormalMap == nullptr ||
            WrinkleStrength == nullptr ||
            WrinkleWetnessMin == nullptr ||
            WrinkleWetnessMax == nullptr ||
            WrinkleBlend == nullptr ||
            NormalOutput == nullptr ||
            BaseColorOutput == nullptr ||
            WetBaseColorInput.Expression == nullptr ||
            RuntimeTransparencyMap == nullptr ||
            TransparencyUV0 == nullptr ||
            TransparencyUV1 == nullptr ||
            TransparencyUV2 == nullptr ||
            TransparencyUV3 == nullptr ||
            TransparencyUVChannel == nullptr ||
            TransparencyUVSelector == nullptr ||
            UseTransparencyMap == nullptr ||
            TransparencyStrength == nullptr ||
            TransparencyWetnessMin == nullptr ||
            TransparencyWetnessMax == nullptr ||
            WrinkleSuppressionStrength == nullptr ||
            RuntimeTransparencyBlend == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create required wrinkle/transparency nodes inside MF_DWC_ApplyWetness."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseNormal, FString(), WrinkleBlend, TEXT("BaseNormal"), FailureReasons);
        WrinkleUVWrap->Input.Connect(0, WrinkleUV0);
        bConnected &= ConnectTextureCoordinateChecked(WrinkleUVWrap, WrinkleNormalMap, FailureReasons);
        bConnected &= ConnectChecked(WrinkleNormalMap, TEXT("RGB"), WrinkleBlend, TEXT("PackedWrinkle"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("R"), WrinkleBlend, TEXT("Wetness"), FailureReasons);
        bConnected &= ConnectChecked(UseWrinkleNormalMap, FString(), WrinkleBlend, TEXT("UseWrinkleNormalMap"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleStrength, FString(), WrinkleBlend, TEXT("WrinkleStrength"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMin, FString(), WrinkleBlend, TEXT("WrinkleWetnessMin"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMax, FString(), WrinkleBlend, TEXT("WrinkleWetnessMax"), FailureReasons);
        bConnected &= ConnectFunctionOutputChecked(WrinkleBlend, FString(), NormalOutput, FailureReasons);
        bConnected &= ConnectChecked(TransparencyUV0, FString(), TransparencyUVSelector, TEXT("UV0"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyUV1, FString(), TransparencyUVSelector, TEXT("UV1"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyUV2, FString(), TransparencyUVSelector, TEXT("UV2"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyUV3, FString(), TransparencyUVSelector, TEXT("UV3"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyUVChannel, FString(), TransparencyUVSelector, TEXT("UVChannel"), FailureReasons);
        bConnected &= ConnectTextureCoordinateChecked(TransparencyUVSelector, RuntimeTransparencyMap, FailureReasons);
        RuntimeTransparencyBlend->Inputs[0].Input = WetBaseColorInput;
        bConnected &= ConnectChecked(RuntimeTransparencyMap, TEXT("RGB"), RuntimeTransparencyBlend, TEXT("TransparencyColor"), FailureReasons);
        bConnected &= ConnectChecked(RuntimeTransparencyMap, TEXT("A"), RuntimeTransparencyBlend, TEXT("TransparencyAlpha"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("R"), RuntimeTransparencyBlend, TEXT("Wetness"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleNormalMap, TEXT("A"), RuntimeTransparencyBlend, TEXT("WrinkleCoverage"), FailureReasons);
        bConnected &= ConnectChecked(UseWrinkleNormalMap, FString(), RuntimeTransparencyBlend, TEXT("UseWrinkleNormalMap"), FailureReasons);
        bConnected &= ConnectChecked(UseTransparencyMap, FString(), RuntimeTransparencyBlend, TEXT("UseTransparencyMap"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyStrength, FString(), RuntimeTransparencyBlend, TEXT("TransparencyStrength"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyWetnessMin, FString(), RuntimeTransparencyBlend, TEXT("TransparencyWetnessMin"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyWetnessMax, FString(), RuntimeTransparencyBlend, TEXT("TransparencyWetnessMax"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleSuppressionStrength, FString(), RuntimeTransparencyBlend, TEXT("WrinkleSuppressionStrength"), FailureReasons);
        bConnected &= ConnectFunctionOutputChecked(RuntimeTransparencyBlend, FString(), BaseColorOutput, FailureReasons);

        if (!bConnected)
        {
            return false;
        }

        UMaterialEditingLibrary::UpdateMaterialFunction(ApplyFunction);
        ApplyFunction->MarkPackageDirty();
        return true;
    }

    UMaterial* LoadExistingDwcMaterialForSource(const UMaterial* SourceMaterial, bool* bOutExpectedDwcPackageExists = nullptr)
    {
        if (bOutExpectedDwcPackageExists != nullptr)
        {
            *bOutExpectedDwcPackageExists = false;
        }

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
        if (bOutExpectedDwcPackageExists != nullptr)
        {
            *bOutExpectedDwcPackageExists = FPackageName::DoesPackageExist(DwcPackageName);
        }
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

    bool ConnectFunctionOutputChecked(
        UMaterialExpression*               FromExpression,
        const FString&                     FromOutputName,
        UMaterialExpressionFunctionOutput* FunctionOutput,
        TArray<FString>&                   FailureReasons)
    {
        if (FromExpression == nullptr || FunctionOutput == nullptr)
        {
            FailureReasons.Add(TEXT("Function output connection requires a source expression and function output node."));
            return false;
        }

        FunctionOutput->A.Connect(0, FromExpression);
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

    bool ResolveConnectedFunctionInput(
        UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const FName                              InputName,
        UMaterialExpression*&                    OutExpression,
        FString&                                 OutOutputName)
    {
        OutExpression = nullptr;
        OutOutputName.Reset();
        if (FunctionCall == nullptr)
        {
            return false;
        }

        FunctionCall->UpdateFromFunctionResource();
        for (const FFunctionExpressionInput& FunctionInput : FunctionCall->FunctionInputs)
        {
            if (FunctionInput.ExpressionInput == nullptr || FunctionInput.ExpressionInput->InputName != InputName)
            {
                continue;
            }

            OutExpression = FunctionInput.Input.Expression;
            if (OutExpression == nullptr)
            {
                return false;
            }

            const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(OutExpression);
            if (OutputNames.IsValidIndex(FunctionInput.Input.OutputIndex))
            {
                OutOutputName = OutputNames[FunctionInput.Input.OutputIndex];
            }
            return true;
        }

        return false;
    }

    void RemoveMaterialLevelDwcWetnessNodes(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return;
        }

        static const TSet<FString> DwcCustomDescriptions = {
            TEXT("DWC Decode Packed Wrinkle Normal"),
            TEXT("DWC Blend Runtime Wrinkle Normal"),
            TEXT("DWC Compute Wrinkle Wetness")
        };
        static const TSet<FName> DwcScalarParameters = {
            TEXT("DWC_WetDarkeningStrength"),
            TEXT("DWC_WetRoughness"),
            TEXT("DWC_SurfaceWaterStrength"),
            TEXT("DWC_UseWrinkleNormalMap"),
            TEXT("DWC_WrinkleStrength"),
            TEXT("DWC_WrinkleWetnessMin"),
            TEXT("DWC_WrinkleWetnessMax")
        };

        TArray<UMaterialExpression*> ExpressionsToRemove;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            const UMaterialExpressionScalarParameter* ScalarParameter = Cast<UMaterialExpressionScalarParameter>(Expression);
            const UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            const UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            const bool bIsDwcCustom = Custom != nullptr && DwcCustomDescriptions.Contains(Custom->Description);
            const bool bIsDwcScalar = ScalarParameter != nullptr && DwcScalarParameters.Contains(ScalarParameter->ParameterName);
            const bool bIsDwcWrinkleTexture = TextureParameter != nullptr && TextureParameter->ParameterName == TEXT("DWC_WrinkleNormalMap");
            const bool bIsDwcWrinkleUv = TextureCoordinate != nullptr && TextureCoordinate->Desc == TEXT("DWC Wrinkle UV");
            if (bIsDwcCustom || bIsDwcScalar || bIsDwcWrinkleTexture || bIsDwcWrinkleUv)
            {
                ExpressionsToRemove.Add(Expression);
            }
        }

        for (UMaterialExpression* Expression : ExpressionsToRemove)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
    }

    bool ConnectDwcApplyWetnessNormalGraph(
        UMaterial*                               Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        TArray<FString>&                         FailureReasons)
    {
        if (Material == nullptr || ApplyCall == nullptr)
        {
            FailureReasons.Add(TEXT("Normal setup requires a material and MF_DWC_ApplyWetness call."));
            return false;
        }

        ApplyCall->UpdateFromFunctionResource();
        FString              BaseNormalOutputName;
        UMaterialExpression* BaseNormalInput = nullptr;
        if (!ResolveConnectedFunctionInput(ApplyCall, FName(TEXT("BaseNormal")), BaseNormalInput, BaseNormalOutputName))
        {
            BaseNormalInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Normal, FVector2D(-900.0f, 500.0f), BaseNormalOutputName);
        }

        const UMaterialExpressionCustom* DwcWrinkleBlend = Cast<UMaterialExpressionCustom>(BaseNormalInput);
        if (BaseNormalInput == ApplyCall ||
            (DwcWrinkleBlend != nullptr && DwcWrinkleBlend->Description == TEXT("DWC Blend Runtime Wrinkle Normal")))
        {
            BaseNormalInput = nullptr;
            BaseNormalOutputName.Reset();
        }

        if (BaseNormalInput == nullptr)
        {
            UMaterialExpressionConstant3Vector* FlatNormal = nullptr;
            for (UMaterialExpression* Expression : Material->GetExpressions())
            {
                UMaterialExpressionConstant3Vector* Candidate = Cast<UMaterialExpressionConstant3Vector>(Expression);
                if (Candidate != nullptr && Candidate->Desc == TEXT("DWC Base Normal Fallback"))
                {
                    FlatNormal = Candidate;
                    break;
                }
            }

            if (FlatNormal == nullptr)
            {
                FlatNormal = Cast<UMaterialExpressionConstant3Vector>(
                    UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant3Vector::StaticClass(), -900, 500));
            }

            if (FlatNormal != nullptr)
            {
                FlatNormal->Constant = FLinearColor(0.0f, 0.0f, 1.0f);
                FlatNormal->Desc = TEXT("DWC Base Normal Fallback");
                BaseNormalInput = FlatNormal;
                BaseNormalOutputName.Reset();
            }
        }

        if (BaseNormalInput == nullptr)
        {
            FailureReasons.Add(TEXT("DWC material setup could not resolve the source material Normal input."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseNormalInput, BaseNormalOutputName, ApplyCall, TEXT("BaseNormal"), FailureReasons);

        FString ApplyNormalOutput;
        if (!ResolveRequiredOutputName(ApplyCall, TEXT("Normal"), ApplyNormalOutput))
        {
            FailureReasons.Add(FString::Printf(TEXT("Missing output 'Normal' on MF_DWC_ApplyWetness. Available outputs: %s"),
                                               *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
            return false;
        }

        if (!UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyNormalOutput, MP_Normal))
        {
            FailureReasons.Add(TEXT("Failed to connect MF_DWC_ApplyWetness output 'Normal' to Material Normal."));
            bConnected = false;
        }

        RemoveMaterialLevelDwcWetnessNodes(Material);

        return bConnected;
    }

    bool ConnectDwcSurfaceWaterUV(
        UMaterial* Material,
        UMaterialExpressionMaterialFunctionCall* ApplyCall,
        const int32 SurfaceWaterUVChannelIndex,
        TArray<FString>& FailureReasons)
    {
        if (!Material || !ApplyCall || !HasInput(ApplyCall, TEXT("SurfaceWaterUV")))
        {
            FailureReasons.Add(TEXT("MF_DWC_ApplyWetness is missing the SurfaceWaterUV input."));
            return false;
        }

        const int32 SafeUVChannelIndex = FMath::Clamp(SurfaceWaterUVChannelIndex, 0, 7);
        UMaterialExpressionTextureCoordinate* SurfaceUV = nullptr;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionTextureCoordinate* Candidate = Cast<UMaterialExpressionTextureCoordinate>(Expression);
            if (Candidate && Candidate->Desc == TEXT("DWC Surface Water UV"))
            {
                SurfaceUV = Candidate;
                break;
            }
        }

        if (!SurfaceUV)
        {
            SurfaceUV = Cast<UMaterialExpressionTextureCoordinate>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1150, 1240));
        }
        if (!SurfaceUV)
        {
            FailureReasons.Add(TEXT("Could not create the DWC Surface Water TextureCoordinate node."));
            return false;
        }

        SurfaceUV->CoordinateIndex = SafeUVChannelIndex;
        SurfaceUV->Desc = TEXT("DWC Surface Water UV");
        return ConnectChecked(SurfaceUV, FString(), ApplyCall, TEXT("SurfaceWaterUV"), FailureReasons);
    }

    void ConfigureDwcMaterialNormalSettings(UMaterial* Material)
    {
        if (Material == nullptr)
        {
            return;
        }

        // DWC wrinkle maps are baked as tangent-space normals, so generated wet materials must
        // evaluate the Normal input in tangent space even if the source material used another mode.
        Material->bTangentSpaceNormal = true;
    }

    bool ConfigureExistingDwcMaterial(
        UMaterial*                  Material,
        UMaterialFunctionInterface* ApplyFunction,
        UMaterialFunctionInterface* DebugFunction,
        int32                       WrinkleUVChannelIndex,
        int32                       SurfaceWaterUVChannelIndex,
        TArray<FString>&            FailureReasons)
    {
        (void)WrinkleUVChannelIndex;

        UMaterialExpressionMaterialFunctionCall* ApplyCall = FindFunctionCall(Material, ApplyFunction);
        UMaterialExpressionMaterialFunctionCall* DebugCall = FindFunctionCall(Material, DebugFunction);
        if (ApplyCall == nullptr || DebugCall == nullptr)
        {
            FailureReasons.Add(TEXT("Existing DWC material is missing MF_DWC_ApplyWetness or MF_DWC_WetPartDebug."));
            return false;
        }

        ConfigureDwcMaterialNormalSettings(Material);

        bool bConnected = true;
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);
        bConnected &= ConnectDwcSurfaceWaterUV(Material, ApplyCall, SurfaceWaterUVChannelIndex, FailureReasons);
        bConnected &= RemoveLegacyDwcClearCoatRendering(Material, ApplyCall, FailureReasons);

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
        int32                       SurfaceWaterUVChannelIndex,
        TArray<FString>&            FailureReasons)
    {
        (void)WrinkleUVChannelIndex;

        FString              BaseColorOutputName;
        UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(Material, MP_BaseColor, FVector2D(-900.0f, -120.0f), BaseColorOutputName);
        FString              RoughnessOutputName;
        UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Roughness, FVector2D(-900.0f, 160.0f), RoughnessOutputName);

        UMaterialExpressionMaterialFunctionCall* ApplyCall = CreateFunctionCall(Material, ApplyFunction, -360, -70);
        UMaterialExpressionMaterialFunctionCall* DebugCall = CreateFunctionCall(Material, DebugFunction, 60, -95);

        const bool bCreatedRequiredNodes = ApplyCall != nullptr && DebugCall != nullptr &&
                                           BaseColorInput != nullptr && RoughnessInput != nullptr;
        if (!bCreatedRequiredNodes)
        {
            FailureReasons.Add(TEXT("DWC material setup could not create one or more required nodes."));
            return false;
        }

        bool bConnected = true;
        bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, ApplyCall, TEXT("BaseColor"), FailureReasons);
        bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, ApplyCall, TEXT("BaseRoughness"), FailureReasons);
        bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);
        bConnected &= ConnectDwcSurfaceWaterUV(Material, ApplyCall, SurfaceWaterUVChannelIndex, FailureReasons);
        bConnected &= RemoveLegacyDwcClearCoatRendering(Material, ApplyCall, FailureReasons);

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

FWetClothingMaterialSetupResult FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
    UMaterialInterface* MaterialInterface,
    int32 WrinkleUVChannelIndex,
    int32 SurfaceWaterUVChannelIndex)
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
                FWetClothingMaterialSetupResult ParentRefreshResult = DuplicateAndApplyToMaterialInterface(
                    ParentMaterial, WrinkleUVChannelIndex, SurfaceWaterUVChannelIndex);
                if (!ParentRefreshResult.bSucceeded)
                {
                    Result.Message = FString::Printf(
                        TEXT("'%s' is already backed by a DWC material, but the parent material output connections could not be refreshed.\n%s"),
                        *MaterialInterface->GetName(),
                        *ParentRefreshResult.Message);
                    return Result;
                }
            }

            Result.bSucceeded = true;
            Result.bAlreadyConfigured = true;
            Result.ConfiguredMaterial = MaterialInterface;
            Result.Message = FString::Printf(
                TEXT("'%s' is already backed by a DWC material. Refreshed its parent material output connections."),
                *MaterialInterface->GetName());
            return Result;
        }

        if (ParentMaterial == nullptr)
        {
            Result.Message = FString::Printf(TEXT("'%s' has no editable parent material."), *MaterialInterface->GetName());
            return Result;
        }

        FWetClothingMaterialSetupResult ParentResult = DuplicateAndApplyToMaterialInterface(
            ParentMaterial, WrinkleUVChannelIndex, SurfaceWaterUVChannelIndex);
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

    TArray<FString> ApplyFunctionFailureReasons;
    if (!ValidateApplyWetnessFunctionContract(ApplyFunction, ApplyFunctionFailureReasons))
    {
        Result.Message = TEXT("The fixed MF_DWC_ApplyWetness asset is incompatible with this DWCEditor build.\n");
        Result.Message += FString::Join(ApplyFunctionFailureReasons, TEXT("\n"));
        Result.Message += TEXT("\nRun 'DWC.RepairApplyWetnessFunction' explicitly, then retry Material Setup.");
        return Result;
    }

    if (HasFunctionCall(Material, ApplyFunction) || HasFunctionCall(Material, DebugFunction))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "RepairWetnessMaterialSetup", "Repair Dynamic Wet Clothes Material Setup"));
        Material->Modify();

        TArray<FString> FailureReasons;
        const bool bConfigured = ConfigureExistingDwcMaterial(
            Material, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, SurfaceWaterUVChannelIndex, FailureReasons);
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

    bool bExpectedDwcPackageExists = false;
    if (UMaterial* ExistingDwcMaterial = LoadExistingDwcMaterialForSource(Material, &bExpectedDwcPackageExists))
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWC", "ReuseWetnessMaterialSetup", "Reuse Dynamic Wet Clothes Material Setup"));
        ExistingDwcMaterial->Modify();

        TArray<FString> FailureReasons;
        const bool            bHasDwcFunctionCall = HasFunctionCall(ExistingDwcMaterial, ApplyFunction) || HasFunctionCall(ExistingDwcMaterial, DebugFunction);
        const bool            bConfigured = bHasDwcFunctionCall
                                                ? ConfigureExistingDwcMaterial(ExistingDwcMaterial, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, SurfaceWaterUVChannelIndex, FailureReasons)
                                                : CreateDwcMaterialGraph(ExistingDwcMaterial, ApplyFunction, DebugFunction, WrinkleUVChannelIndex, SurfaceWaterUVChannelIndex, FailureReasons);
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

    if (bExpectedDwcPackageExists)
    {
        const FString SourcePackageName = Material->GetOutermost()->GetName();
        const FString DwcPackageName = SourcePackageName.EndsWith(TEXT("_DWC")) ? SourcePackageName : SourcePackageName + TEXT("_DWC");
        Result.Message = FString::Printf(
            TEXT("Expected existing DWC material package '%s' exists but could not be loaded. Fix or remove that material before generating a new DWC override."),
            *DwcPackageName);
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
    ConfigureDwcMaterialNormalSettings(Material);

    FString              BaseColorOutputName;
    UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(Material, MP_BaseColor, FVector2D(-900.0f, -120.0f), BaseColorOutputName);
    FString              RoughnessOutputName;
    UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Roughness, FVector2D(-900.0f, 160.0f), RoughnessOutputName);

    UMaterialExpressionMaterialFunctionCall* ApplyCall = CreateFunctionCall(Material, ApplyFunction, -360, -70);
    UMaterialExpressionMaterialFunctionCall* DebugCall = CreateFunctionCall(Material, DebugFunction, 60, -95);

    const bool bCreatedRequiredNodes = ApplyCall != nullptr && DebugCall != nullptr &&
                                       BaseColorInput != nullptr && RoughnessInput != nullptr;
    if (!bCreatedRequiredNodes)
    {
        Result.Message = TEXT("DWC material setup could not create one or more required nodes.");
        return Result;
    }

    TArray<FString> FailureReasons;
    bool            bConnected = true;
    bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, ApplyCall, TEXT("BaseColor"), FailureReasons);
    bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, ApplyCall, TEXT("BaseRoughness"), FailureReasons);
    bConnected &= ConnectDwcApplyWetnessNormalGraph(Material, ApplyCall, FailureReasons);
    bConnected &= ConnectDwcSurfaceWaterUV(Material, ApplyCall, SurfaceWaterUVChannelIndex, FailureReasons);
    bConnected &= RemoveLegacyDwcClearCoatRendering(Material, ApplyCall, FailureReasons);

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

bool FWetClothingMaterialSetup::ValidateSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    FString ApplyFunctionPath;
    UMaterialFunctionInterface* ApplyFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_ApplyWetness"), &ApplyFunctionPath);
    if (ApplyFunction == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not load MF_DWC_ApplyWetness at '%s'."),
            ApplyFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *ApplyFunctionPath);
        return false;
    }

    TArray<FString> FailureReasons;
    if (!ValidateApplyWetnessFunctionContract(ApplyFunction, FailureReasons))
    {
        OutErrorMessage = FString::Join(FailureReasons, TEXT("\n"));
        return false;
    }
    return true;
}

bool FWetClothingMaterialSetup::RepairOrUpgradeSharedApplyWetnessFunction(FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    FString ApplyFunctionPath;
    UMaterialFunctionInterface* ApplyFunction = LoadDwcMaterialFunction(TEXT("MF_DWC_ApplyWetness"), &ApplyFunctionPath);
    if (ApplyFunction == nullptr)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not load MF_DWC_ApplyWetness at '%s'."),
            ApplyFunctionPath.IsEmpty() ? TEXT("<plugin not mounted>") : *ApplyFunctionPath);
        return false;
    }

    TArray<FString> FailureReasons;
    if (!EnsureApplyWetnessFunctionInternalGraph(ApplyFunction, FailureReasons))
    {
        OutErrorMessage = FString::Join(FailureReasons, TEXT("\n"));
        return false;
    }

    FailureReasons.Reset();
    if (!ValidateApplyWetnessFunctionContract(ApplyFunction, FailureReasons))
    {
        OutErrorMessage = TEXT("MF_DWC_ApplyWetness was rebuilt but still does not satisfy the fixed asset contract.\n");
        OutErrorMessage += FString::Join(FailureReasons, TEXT("\n"));
        return false;
    }

    return true;
}
