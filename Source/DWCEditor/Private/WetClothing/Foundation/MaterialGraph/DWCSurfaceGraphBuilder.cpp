#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"

#include "Engine/Texture.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "WetRendering/WetMaterialParameters.h"

namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");
    constexpr const TCHAR* EvaluateSurfaceAppearanceFunctionName = TEXT("MF_DWC_EvaluateSurfaceAppearance");
    constexpr const TCHAR* GetRenderProfileFunctionName = TEXT("MF_DWC_GetRenderProfile");
    constexpr const TCHAR* SampleSurfaceWaterNormalsFunctionName = TEXT("MF_DWC_SampleSurfaceWaterNormals");

    FString BuildPluginMaterialFunctionPath(const FString& FunctionName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(DynamicWetClothesPluginName);
        if (!Plugin.IsValid())
        {
            return FString();
        }

        FString MountedAssetPath = Plugin->GetMountedAssetPath();
        MountedAssetPath.RemoveFromEnd(TEXT("/"));
        return FString::Printf(TEXT("%s/Materials/Functions/%s.%s"), *MountedAssetPath, *FunctionName, *FunctionName);
    }

    UMaterialFunctionInterface* LoadPluginMaterialFunction(const FString& FunctionName)
    {
        const FString ObjectPath = BuildPluginMaterialFunctionPath(FunctionName);
        return ObjectPath.IsEmpty() ? nullptr : LoadObject<UMaterialFunctionInterface>(nullptr, *ObjectPath);
    }

    bool HasFunctionInput(const UMaterialFunctionInterface* FunctionInterface, const FName InputName)
    {
        const UMaterialFunction* Function = Cast<UMaterialFunction>(FunctionInterface);
        if (Function == nullptr)
        {
            return false;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            const UMaterialExpressionFunctionInput* Input = Cast<UMaterialExpressionFunctionInput>(Expression);
            if (Input != nullptr && Input->InputName == InputName)
            {
                return true;
            }
        }
        return false;
    }

    bool HasFunctionOutput(const UMaterialFunctionInterface* FunctionInterface, const FName OutputName)
    {
        const UMaterialFunction* Function = Cast<UMaterialFunction>(FunctionInterface);
        if (Function == nullptr)
        {
            return false;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            const UMaterialExpressionFunctionOutput* Output = Cast<UMaterialExpressionFunctionOutput>(Expression);
            if (Output != nullptr && Output->OutputName == OutputName)
            {
                return true;
            }
        }
        return false;
    }

    bool HasStaticSwitchParameter(
        const UMaterialFunctionInterface* FunctionInterface,
        const FName ParameterName)
    {
        const UMaterialFunction* Function = Cast<UMaterialFunction>(FunctionInterface);
        if (Function == nullptr)
        {
            return false;
        }

        for (UMaterialExpression* Expression : Function->GetExpressions())
        {
            const UMaterialExpressionStaticSwitchParameter* Switch =
                Cast<UMaterialExpressionStaticSwitchParameter>(Expression);
            if (Switch != nullptr && Switch->ParameterName == ParameterName)
            {
                return true;
            }
        }
        return false;
    }

    bool IsExpectedFunctionCall(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const UMaterialFunctionInterface* ExpectedFunction,
        const TCHAR* ExpectedFunctionName)
    {
        return FunctionCall != nullptr && FunctionCall->MaterialFunction != nullptr &&
               (FunctionCall->MaterialFunction == ExpectedFunction ||
                FunctionCall->MaterialFunction->GetName().Equals(ExpectedFunctionName, ESearchCase::CaseSensitive));
    }

    bool HasFunctionCall(
        const UMaterialFunction* MaterialFunction,
        const UMaterialFunctionInterface* CalledFunction,
        const TCHAR* ExpectedFunctionName)
    {
        if (MaterialFunction == nullptr || CalledFunction == nullptr)
        {
            return false;
        }

        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            const UMaterialExpressionMaterialFunctionCall* FunctionCall =
                Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (IsExpectedFunctionCall(FunctionCall, CalledFunction, ExpectedFunctionName))
            {
                return true;
            }
        }
        return false;
    }

    bool ValidateFunctionContract(
        const UMaterialFunctionInterface* Function,
        const TCHAR* FunctionName,
        const TArray<FName>& RequiredInputs,
        const TArray<FName>& RequiredOutputs,
        TArray<FString>& OutFailureReasons)
    {
        if (Function == nullptr)
        {
            OutFailureReasons.Add(FString::Printf(
                TEXT("Could not load %s. Run the corresponding DWC Python creation script once before Generate Materials."),
                FunctionName));
            return false;
        }

        bool bValid = true;
        for (const FName InputName : RequiredInputs)
        {
            if (!HasFunctionInput(Function, InputName))
            {
                OutFailureReasons.Add(FString::Printf(
                    TEXT("%s is missing required input '%s'."), FunctionName, *InputName.ToString()));
                bValid = false;
            }
        }
        for (const FName OutputName : RequiredOutputs)
        {
            if (!HasFunctionOutput(Function, OutputName))
            {
                OutFailureReasons.Add(FString::Printf(
                    TEXT("%s is missing required output '%s'."), FunctionName, *OutputName.ToString()));
                bValid = false;
            }
        }
        return bValid;
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

    TArray<FString> GetOutputNames(UMaterialExpression* Expression)
    {
        TArray<FString> OutputNames;
        if (Expression != nullptr)
        {
            for (const FExpressionOutput& Output : Expression->GetOutputs())
            {
                OutputNames.Add(Output.OutputName.ToString());
            }
        }
        return OutputNames;
    }

    bool Connect(
        const FDWCMaterialGraphPin& From,
        UMaterialExpression* ToExpression,
        const FString& ToInputName,
        TArray<FString>& FailureReasons)
    {
        if (!From.IsValid() || ToExpression == nullptr)
        {
            FailureReasons.Add(FString::Printf(TEXT("Cannot connect an invalid graph pin to '%s'."), *ToInputName));
            return false;
        }

        const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(ToExpression);
        if (!InputNames.Contains(ToInputName))
        {
            FailureReasons.Add(FString::Printf(
                TEXT("Missing input '%s' on %s. Available inputs: %s"),
                *ToInputName,
                *GetNameSafe(ToExpression),
                *JoinPinNames(InputNames)));
            return false;
        }

        if (!UMaterialEditingLibrary::ConnectMaterialExpressions(
                From.Expression, From.OutputName, ToExpression, ToInputName))
        {
            FailureReasons.Add(FString::Printf(
                TEXT("Failed to connect %s output '%s' to %s input '%s'."),
                *GetNameSafe(From.Expression),
                From.OutputName.IsEmpty() ? TEXT("<first>") : *From.OutputName,
                *GetNameSafe(ToExpression),
                *ToInputName));
            return false;
        }
        return true;
    }

    bool ConnectTextureCoordinate(
        UMaterialExpressionTextureCoordinate* TextureCoordinate,
        UMaterialExpressionTextureSampleParameter2D* TextureSample,
        TArray<FString>& FailureReasons)
    {
        if (TextureCoordinate == nullptr || TextureSample == nullptr)
        {
            FailureReasons.Add(TEXT("DWC Data UV connection requires a texture coordinate and texture sample."));
            return false;
        }

        const TArray<FString> InputNames = UMaterialEditingLibrary::GetMaterialExpressionInputNames(TextureSample);
        static const FString CandidateInputNames[] = { TEXT("UVs"), TEXT("Coordinates") };
        for (const FString& CandidateInputName : CandidateInputNames)
        {
            if (InputNames.Contains(CandidateInputName) &&
                UMaterialEditingLibrary::ConnectMaterialExpressions(
                    TextureCoordinate, FString(), TextureSample, CandidateInputName))
            {
                return true;
            }
        }

        FailureReasons.Add(FString::Printf(
            TEXT("Failed to connect DWC Data UV to %s. Available inputs: %s"),
            *GetNameSafe(TextureSample),
            *JoinPinNames(InputNames)));
        return false;
    }

    UMaterialExpressionMaterialFunctionCall* CreateFunctionCall(
        UMaterial* Material,
        UMaterialFunctionInterface* Function,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionMaterialFunctionCall* FunctionCall =
            Cast<UMaterialExpressionMaterialFunctionCall>(UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionMaterialFunctionCall::StaticClass(), NodeX, NodeY));
        if (FunctionCall == nullptr || !FunctionCall->SetMaterialFunction(Function))
        {
            return nullptr;
        }
        FunctionCall->UpdateFromFunctionResource();
        return FunctionCall;
    }

    FDWCMaterialGraphPin ResolvePropertyInputOrFallback(
        UMaterial* Material,
        const EMaterialProperty Property,
        const FVector2D& NodePosition)
    {
        if (UMaterialExpression* Existing = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, Property))
        {
            return { Existing, UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(Material, Property) };
        }

        if (Property == MP_BaseColor || Property == MP_Normal)
        {
            UMaterialExpressionConstant3Vector* Fallback = Cast<UMaterialExpressionConstant3Vector>(
                UMaterialEditingLibrary::CreateMaterialExpression(
                    Material, UMaterialExpressionConstant3Vector::StaticClass(), NodePosition.X, NodePosition.Y));
            if (Fallback != nullptr)
            {
                Fallback->Constant = Property == MP_Normal
                    ? FLinearColor(0.0f, 0.0f, 1.0f)
                    : FLinearColor::White;
            }
            return { Fallback, FString() };
        }

        UMaterialExpressionConstant* Fallback = Cast<UMaterialExpressionConstant>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionConstant::StaticClass(), NodePosition.X, NodePosition.Y));
        if (Fallback != nullptr)
        {
            Fallback->R = Property == MP_Metallic ? 0.0f : 0.5f;
        }
        return { Fallback, FString() };
    }

    UMaterialExpressionScalarParameter* CreateScalarParameter(
        UMaterial* Material,
        const FName Name,
        const float DefaultValue,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionScalarParameter::StaticClass(), NodeX, NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = Name;
            Parameter->DefaultValue = DefaultValue;
        }
        return Parameter;
    }

    UTexture* LoadDefaultNormalTexture()
    {
        if (UTexture* Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal")))
        {
            return Texture;
        }
        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineMaterials/T_Default_Normal.T_Default_Normal"));
    }

    UTexture* LoadDefaultBlackTexture()
    {
        if (UTexture* Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black")))
        {
            return Texture;
        }
        return LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
    }

    UMaterialExpressionTextureSampleParameter2D* CreateTextureParameter(
        UMaterial* Material,
        const FName Name,
        const EMaterialSamplerType SamplerType,
        UTexture* DefaultTexture,
        const TCHAR* Description,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureSampleParameter2D* Parameter = Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), NodeX, NodeY));
        if (Parameter != nullptr)
        {
            Parameter->ParameterName = Name;
            Parameter->SamplerType = SamplerType;
            Parameter->Texture = DefaultTexture;
            Parameter->Desc = Description;
        }
        return Parameter;
    }

    UMaterialExpressionTextureCoordinate* CreateTextureCoordinate(
        UMaterial* Material,
        const int32 CoordinateIndex,
        const TCHAR* Description,
        const int32 NodeX,
        const int32 NodeY)
    {
        UMaterialExpressionTextureCoordinate* Coordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material, UMaterialExpressionTextureCoordinate::StaticClass(), NodeX, NodeY));
        if (Coordinate != nullptr)
        {
            Coordinate->CoordinateIndex = CoordinateIndex;
            Coordinate->Desc = Description;
        }
        return Coordinate;
    }

    bool ResolveOutputPin(
        UMaterialExpression* Expression,
        const FString& OutputName,
        FDWCMaterialGraphPin& OutPin,
        TArray<FString>& FailureReasons)
    {
        if (!GetOutputNames(Expression).Contains(OutputName))
        {
            FailureReasons.Add(FString::Printf(
                TEXT("Missing output '%s' on %s. Available outputs: %s"),
                *OutputName,
                *GetNameSafe(Expression),
                *JoinPinNames(GetOutputNames(Expression))));
            return false;
        }
        OutPin = { Expression, OutputName };
        return true;
    }

    void DeleteExpressionsCreatedAfter(
        UMaterial* Material,
        const TSet<UMaterialExpression*>& PreExistingExpressions)
    {
        if (Material == nullptr)
        {
            return;
        }

        TArray<UMaterialExpression*> CreatedExpressions;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression != nullptr && !PreExistingExpressions.Contains(Expression))
            {
                CreatedExpressions.Add(Expression);
            }
        }
        for (UMaterialExpression* Expression : CreatedExpressions)
        {
            UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
        }
    }
}

bool FDWCSurfaceGraphBuilder::ValidateDependencies(
    TArray<FString>& OutFailureReasons,
    UMaterialFunctionInterface** OutEvaluateFunction)
{
    if (OutEvaluateFunction != nullptr)
    {
        *OutEvaluateFunction = nullptr;
    }

    UMaterialFunctionInterface* RenderProfileFunction = LoadPluginMaterialFunction(GetRenderProfileFunctionName);
    UMaterialFunctionInterface* SurfaceNormalFunction = LoadPluginMaterialFunction(SampleSurfaceWaterNormalsFunctionName);
    UMaterialFunctionInterface* EvaluateFunction = LoadPluginMaterialFunction(EvaluateSurfaceAppearanceFunctionName);

    static const TArray<FName> RenderProfileInputs = { TEXT("DWCDataUV") };
    static const TArray<FName> RenderProfileOutputs = {
        TEXT("AbsorbedDarkeningStrength"), TEXT("AbsorbedGlossinessStrength"),
        TEXT("Droplet1NormalSlice"), TEXT("Droplet1NormalStrength"),
        TEXT("Droplet1RoughnessBlend"), TEXT("Droplet1Specular"),
        TEXT("Droplet1MaskSlice"), TEXT("Droplet1TargetRoughness"),
        TEXT("Droplet1TotalStrength"), TEXT("Droplet2NormalSlice"),
        TEXT("Droplet2MaskSlice"), TEXT("Droplet2TotalStrength"),
        TEXT("Droplet2TargetRoughness"), TEXT("Droplet2RoughnessBlend"),
        TEXT("Droplet2Specular"), TEXT("Droplet1ColorBlend"),
        TEXT("Droplet2ColorBlend"), TEXT("Droplet2NormalStrength"),
        TEXT("Droplet1DetailSize"), TEXT("Droplet2DetailSize")
    };
    static const TArray<FName> SurfaceNormalInputs = {
        TEXT("SurfaceWaterNormalUV"),
        TEXT("Droplet1DetailSize"), TEXT("Droplet2DetailSize"),
        TEXT("Droplet1MaskSlice"), TEXT("Droplet1NormalSlice"),
        TEXT("Droplet2MaskSlice"), TEXT("Droplet2NormalSlice")
    };
    static const TArray<FName> SurfaceNormalOutputs = {
        TEXT("Droplet1Mask"), TEXT("Droplet1Normal"),
        TEXT("Droplet2Mask"), TEXT("Droplet2Normal")
    };
    static const TArray<FName> EvaluateInputs = {
        TEXT("BaseColor"), TEXT("BaseRoughness"), TEXT("BaseSpecular"), TEXT("BaseMetallic"), TEXT("BaseNormal"),
        TEXT("Wetness"), TEXT("DWCDataUV"), TEXT("SurfaceWaterNormalUV"),
        TEXT("WetDarkeningStrength"), TEXT("WetRoughness"),
        TEXT("WrinkleNormal"), TEXT("UseWrinkleNormalMap"), TEXT("WrinkleStrength"),
        TEXT("WrinkleWetnessMin"), TEXT("WrinkleWetnessMax"),
        TEXT("TransparencyColor"), TEXT("TransparencyAlpha"), TEXT("UseTransparencyMap"),
        TEXT("TransparencyWetnessMin"), TEXT("TransparencyWetnessMax")
    };
    static const TArray<FName> EvaluateOutputs = {
        TEXT("BaseColor"), TEXT("Roughness"), TEXT("Specular"), TEXT("Normal"),
        TEXT("SurfaceCoverage"), TEXT("DropletCoverage"),
        TEXT("DropletWetness"), TEXT("DropletBrush")
    };

    bool bValid = true;
    bValid &= ValidateFunctionContract(
        RenderProfileFunction, GetRenderProfileFunctionName,
        RenderProfileInputs, RenderProfileOutputs, OutFailureReasons);
    bValid &= ValidateFunctionContract(
        SurfaceNormalFunction, SampleSurfaceWaterNormalsFunctionName,
        SurfaceNormalInputs, SurfaceNormalOutputs, OutFailureReasons);
    bValid &= ValidateFunctionContract(
        EvaluateFunction, EvaluateSurfaceAppearanceFunctionName,
        EvaluateInputs, EvaluateOutputs, OutFailureReasons);

    if (EvaluateFunction != nullptr &&
        !HasStaticSwitchParameter(EvaluateFunction, DWCWetMaterialParameters::UseSurfaceWater()))
    {
        OutFailureReasons.Add(TEXT(
            "MF_DWC_EvaluateSurfaceAppearance is missing static switch 'DWC_UseSurfaceWater'."));
        bValid = false;
    }

    if (bValid)
    {
        const UMaterialFunction* EvaluateMaterialFunction = Cast<UMaterialFunction>(EvaluateFunction);
        if (EvaluateMaterialFunction == nullptr ||
            !HasFunctionCall(EvaluateMaterialFunction, RenderProfileFunction, GetRenderProfileFunctionName) ||
            !HasFunctionCall(EvaluateMaterialFunction, SurfaceNormalFunction, SampleSurfaceWaterNormalsFunctionName))
        {
            OutFailureReasons.Add(TEXT(
                "MF_DWC_EvaluateSurfaceAppearance must call both MF_DWC_GetRenderProfile and "
                "MF_DWC_SampleSurfaceWaterNormals."));
            bValid = false;
        }
    }

    if (bValid && OutEvaluateFunction != nullptr)
    {
        *OutEvaluateFunction = EvaluateFunction;
    }
    return bValid;
}

FDWCSurfaceGraphBuildResult FDWCSurfaceGraphBuilder::Build(const FDWCSurfaceGraphBuildRequest& Request)
{
    FDWCSurfaceGraphBuildResult Result;
    if (Request.Material == nullptr)
    {
        Result.FailureReasons.Add(TEXT("Cannot build a DWC surface graph in a null material."));
        return Result;
    }
    if (!Request.WetnessInput.IsValid())
    {
        Result.FailureReasons.Add(TEXT("The DWC surface graph requires a caller-owned wetness input."));
        return Result;
    }
    if (Request.DWCDataUVChannelIndex < 0 || Request.SurfaceWaterNormalUVChannelIndex < 0)
    {
        Result.FailureReasons.Add(TEXT("The DWC surface graph requires valid Data UV and surface-normal UV channels."));
        return Result;
    }

    TSet<UMaterialExpression*> PreExistingExpressions;
    for (UMaterialExpression* Expression : Request.Material->GetExpressions())
    {
        if (Expression != nullptr)
        {
            PreExistingExpressions.Add(Expression);
        }
    }

    UMaterialFunctionInterface* EvaluateFunction = Request.EvaluateFunction;
    if (EvaluateFunction == nullptr && !ValidateDependencies(Result.FailureReasons, &EvaluateFunction))
    {
        return Result;
    }

    Result.BaseInputs.BaseColor = ResolvePropertyInputOrFallback(
        Request.Material, MP_BaseColor, FVector2D(-2600.0f, -280.0f));
    Result.BaseInputs.Roughness = ResolvePropertyInputOrFallback(
        Request.Material, MP_Roughness, FVector2D(-2600.0f, -40.0f));
    Result.BaseInputs.Normal = ResolvePropertyInputOrFallback(
        Request.Material, MP_Normal, FVector2D(-2600.0f, 200.0f));
    Result.BaseInputs.Metallic = ResolvePropertyInputOrFallback(
        Request.Material, MP_Metallic, FVector2D(-2600.0f, 440.0f));
    Result.BaseInputs.Specular = ResolvePropertyInputOrFallback(
        Request.Material, MP_Specular, FVector2D(-2600.0f, 680.0f));

    Result.EvaluateExpression = CreateFunctionCall(Request.Material, EvaluateFunction, -620, -100);
    Result.DWCDataUVExpression = CreateTextureCoordinate(
        Request.Material, Request.DWCDataUVChannelIndex, TEXT("DWC Data UV"), -2140, 820);
    Result.SurfaceWaterNormalUVExpression = CreateTextureCoordinate(
        Request.Material, Request.SurfaceWaterNormalUVChannelIndex,
        TEXT("DWC Surface Water Normal UV"), -2140, 980);

    UMaterialExpressionScalarParameter* WetDarkeningStrength = CreateScalarParameter(
        Request.Material, TEXT("DWC_WetDarkeningStrength"), 0.35f, -2140, -420);
    UMaterialExpressionScalarParameter* WetRoughness = CreateScalarParameter(
        Request.Material, TEXT("DWC_WetRoughness"), 0.12f, -2140, -320);
    UMaterialExpressionTextureSampleParameter2D* WrinkleNormalMap = CreateTextureParameter(
        Request.Material, TEXT("DWC_WrinkleNormalMap"), SAMPLERTYPE_Normal, LoadDefaultNormalTexture(),
        TEXT("DWC baked wrinkle tangent-space normal map."), -1900, 260);
    UMaterialExpressionScalarParameter* UseWrinkleNormalMap = CreateScalarParameter(
        Request.Material, TEXT("DWC_UseWrinkleNormalMap"), 0.0f, -1660, 260);
    UMaterialExpressionScalarParameter* WrinkleStrength = CreateScalarParameter(
        Request.Material, TEXT("DWC_WrinkleStrength"), 1.0f, -1660, 350);
    UMaterialExpressionScalarParameter* WrinkleWetnessMin = CreateScalarParameter(
        Request.Material, TEXT("DWC_WrinkleWetnessMin"), 0.25f, -1660, 440);
    UMaterialExpressionScalarParameter* WrinkleWetnessMax = CreateScalarParameter(
        Request.Material, TEXT("DWC_WrinkleWetnessMax"), 1.0f, -1660, 530);
    UMaterialExpressionTextureSampleParameter2D* TransparencyMap = CreateTextureParameter(
        Request.Material, DWCWetMaterialParameters::TransparencyMap(), SAMPLERTYPE_Color, LoadDefaultBlackTexture(),
        TEXT("DWC baked transparency map. RGB stores inner color; alpha stores transparency amount."), -1900, 1360);
    UMaterialExpressionScalarParameter* UseTransparencyMap = CreateScalarParameter(
        Request.Material, DWCWetMaterialParameters::UseTransparencyMap(), 0.0f, -1660, 1360);
    UMaterialExpressionScalarParameter* TransparencyWetnessMin = CreateScalarParameter(
        Request.Material, DWCWetMaterialParameters::TransparencyWetnessMin(),
        DWCWetMaterialParameters::DefaultTransparencyWetnessMin(), -1660, 1450);
    UMaterialExpressionScalarParameter* TransparencyWetnessMax = CreateScalarParameter(
        Request.Material, DWCWetMaterialParameters::TransparencyWetnessMax(),
        DWCWetMaterialParameters::DefaultTransparencyWetnessMax(), -1660, 1540);

    if (!Result.BaseInputs.IsValid() || Result.EvaluateExpression == nullptr ||
        Result.DWCDataUVExpression == nullptr || Result.SurfaceWaterNormalUVExpression == nullptr ||
        WetDarkeningStrength == nullptr || WetRoughness == nullptr ||
        WrinkleNormalMap == nullptr ||
        UseWrinkleNormalMap == nullptr || WrinkleStrength == nullptr || WrinkleWetnessMin == nullptr ||
        WrinkleWetnessMax == nullptr || TransparencyMap == nullptr || UseTransparencyMap == nullptr ||
        TransparencyWetnessMin == nullptr || TransparencyWetnessMax == nullptr)
    {
        Result.FailureReasons.Add(TEXT("Could not create one or more nodes for the common DWC surface graph."));
        DeleteExpressionsCreatedAfter(Request.Material, PreExistingExpressions);
        return Result;
    }

    bool bConnected = true;
    bConnected &= ConnectTextureCoordinate(Result.DWCDataUVExpression, WrinkleNormalMap, Result.FailureReasons);
    bConnected &= ConnectTextureCoordinate(Result.DWCDataUVExpression, TransparencyMap, Result.FailureReasons);
    bConnected &= Connect(Result.BaseInputs.BaseColor, Result.EvaluateExpression, TEXT("BaseColor"), Result.FailureReasons);
    bConnected &= Connect(Result.BaseInputs.Roughness, Result.EvaluateExpression, TEXT("BaseRoughness"), Result.FailureReasons);
    bConnected &= Connect(Result.BaseInputs.Specular, Result.EvaluateExpression, TEXT("BaseSpecular"), Result.FailureReasons);
    bConnected &= Connect(Result.BaseInputs.Metallic, Result.EvaluateExpression, TEXT("BaseMetallic"), Result.FailureReasons);
    bConnected &= Connect(Result.BaseInputs.Normal, Result.EvaluateExpression, TEXT("BaseNormal"), Result.FailureReasons);
    bConnected &= Connect(Request.WetnessInput, Result.EvaluateExpression, TEXT("Wetness"), Result.FailureReasons);
    bConnected &= Connect({ WetDarkeningStrength, FString() }, Result.EvaluateExpression, TEXT("WetDarkeningStrength"), Result.FailureReasons);
    bConnected &= Connect({ WetRoughness, FString() }, Result.EvaluateExpression, TEXT("WetRoughness"), Result.FailureReasons);
    bConnected &= Connect({ Result.DWCDataUVExpression, FString() }, Result.EvaluateExpression, TEXT("DWCDataUV"), Result.FailureReasons);
    bConnected &= Connect({ Result.SurfaceWaterNormalUVExpression, FString() }, Result.EvaluateExpression, TEXT("SurfaceWaterNormalUV"), Result.FailureReasons);
    bConnected &= Connect({ WrinkleNormalMap, TEXT("RGB") }, Result.EvaluateExpression, TEXT("WrinkleNormal"), Result.FailureReasons);
    bConnected &= Connect({ UseWrinkleNormalMap, FString() }, Result.EvaluateExpression, TEXT("UseWrinkleNormalMap"), Result.FailureReasons);
    bConnected &= Connect({ WrinkleStrength, FString() }, Result.EvaluateExpression, TEXT("WrinkleStrength"), Result.FailureReasons);
    bConnected &= Connect({ WrinkleWetnessMin, FString() }, Result.EvaluateExpression, TEXT("WrinkleWetnessMin"), Result.FailureReasons);
    bConnected &= Connect({ WrinkleWetnessMax, FString() }, Result.EvaluateExpression, TEXT("WrinkleWetnessMax"), Result.FailureReasons);
    bConnected &= Connect({ TransparencyMap, TEXT("RGB") }, Result.EvaluateExpression, TEXT("TransparencyColor"), Result.FailureReasons);
    bConnected &= Connect({ TransparencyMap, TEXT("A") }, Result.EvaluateExpression, TEXT("TransparencyAlpha"), Result.FailureReasons);
    bConnected &= Connect({ UseTransparencyMap, FString() }, Result.EvaluateExpression, TEXT("UseTransparencyMap"), Result.FailureReasons);
    bConnected &= Connect({ TransparencyWetnessMin, FString() }, Result.EvaluateExpression, TEXT("TransparencyWetnessMin"), Result.FailureReasons);
    bConnected &= Connect({ TransparencyWetnessMax, FString() }, Result.EvaluateExpression, TEXT("TransparencyWetnessMax"), Result.FailureReasons);

    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("BaseColor"), Result.Outputs.BaseColor, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("Roughness"), Result.Outputs.Roughness, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("Specular"), Result.Outputs.Specular, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("Normal"), Result.Outputs.Normal, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("SurfaceCoverage"), Result.Outputs.SurfaceCoverage, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("DropletCoverage"), Result.Outputs.DropletCoverage, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("DropletWetness"), Result.Outputs.DropletWetness, Result.FailureReasons);
    bConnected &= ResolveOutputPin(Result.EvaluateExpression, TEXT("DropletBrush"), Result.Outputs.DropletBrush, Result.FailureReasons);

    if (!bConnected || !Result.Outputs.IsValid())
    {
        DeleteExpressionsCreatedAfter(Request.Material, PreExistingExpressions);
        Result.EvaluateExpression = nullptr;
        Result.DWCDataUVExpression = nullptr;
        Result.SurfaceWaterNormalUVExpression = nullptr;
        return Result;
    }

    Result.bSucceeded = true;
    return Result;
}
