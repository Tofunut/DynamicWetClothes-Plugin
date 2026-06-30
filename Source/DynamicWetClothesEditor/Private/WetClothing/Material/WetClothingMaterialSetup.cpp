#include "WetClothing/Material/WetClothingMaterialSetup.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"

namespace
{
    constexpr const TCHAR* ApplyWetnessFunctionPath = TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_ApplyWetness.MF_DWC_ApplyWetness");
    constexpr const TCHAR* WetPartDebugFunctionPath = TEXT("/DynamicWetClothes/Materials/Functions/MF_DWC_WetPartDebug.MF_DWC_WetPartDebug");

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

    bool ResolvePreferredOutputName(UMaterialExpression* Expression, const FString& OutputName, FString& OutResolvedOutputName)
    {
        const TArray<FString> OutputNames = UMaterialEditingLibrary::GetMaterialExpressionOutputNames(Expression);
        if (OutputNames.Contains(OutputName))
        {
            OutResolvedOutputName = OutputName;
            return true;
        }

        if (OutputNames.Num() > 0)
        {
            OutResolvedOutputName.Reset();
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
        Result.Message = MaterialInstance != nullptr
                             ? FString::Printf(TEXT("'%s' is a material instance. Assign or duplicate an editable material asset before running DWC setup."), *MaterialInterface->GetName())
                             : FString::Printf(TEXT("'%s' is not an editable material asset."), *MaterialInterface->GetName());
        return Result;
    }

    UMaterialFunctionInterface* ApplyFunction = LoadObject<UMaterialFunctionInterface>(nullptr, ApplyWetnessFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadObject<UMaterialFunctionInterface>(nullptr, WetPartDebugFunctionPath);
    if (ApplyFunction == nullptr || DebugFunction == nullptr)
    {
        Result.Message = TEXT("Could not load MF_DWC_ApplyWetness or MF_DWC_WetPartDebug.");
        return Result;
    }

    if (HasFunctionCall(Material, ApplyFunction) || HasFunctionCall(Material, DebugFunction))
    {
        Result.bSucceeded = true;
        Result.bAlreadyConfigured = true;
        Result.ConfiguredMaterial = Material;
        Result.Message = FString::Printf(TEXT("'%s' already contains a DWC material function call."), *Material->GetName());
        return Result;
    }

    const FString OriginalPackageName = Material->GetOutermost()->GetName();
    FString       NewPackageName;
    FString       NewAssetName;
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

    const FScopedTransaction Transaction(NSLOCTEXT("DynamicWetClothes", "ApplyWetnessMaterialSetup", "Apply Dynamic Wet Clothes Material Setup"));
    Material->Modify();

    FString              BaseColorOutputName;
    UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(Material, MP_BaseColor, FVector2D(-900.0f, -120.0f), BaseColorOutputName);
    FString              RoughnessOutputName;
    UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(Material, MP_Roughness, FVector2D(-900.0f, 160.0f), RoughnessOutputName);

    UMaterialExpressionMaterialFunctionCall* ApplyCall = CreateFunctionCall(Material, ApplyFunction, -360, -70);
    UMaterialExpressionMaterialFunctionCall* DebugCall = CreateFunctionCall(Material, DebugFunction, 60, -95);

    UMaterialExpressionVectorParameter* WetTintColor = CreateVectorParameter(Material, TEXT("FallbackUnderColor"), FLinearColor(0.8f, 0.56f, 0.48f, 1.0f), -900, -10);
    UMaterialExpressionScalarParameter* WetVisualStrength = CreateScalarParameter(Material, TEXT("WetUnderColorBlendStrength"), 0.35f, -900, 90);
    UMaterialExpressionScalarParameter* WetRoughness = CreateScalarParameter(Material, TEXT("DWC_WetRoughness"), 0.12f, -900, 230);
    UMaterialExpressionScalarParameter* SurfaceWaterStrength = CreateScalarParameter(Material, TEXT("DWC_SurfaceWaterStrength"), 1.0f, -900, 330);

    const bool bCreatedRequiredNodes = ApplyCall != nullptr && DebugCall != nullptr &&
                                       BaseColorInput != nullptr && RoughnessInput != nullptr &&
                                       WetTintColor != nullptr && WetVisualStrength != nullptr && WetRoughness != nullptr && SurfaceWaterStrength != nullptr;
    if (!bCreatedRequiredNodes)
    {
        Result.Message = TEXT("DWC material setup could not create one or more required nodes.");
        return Result;
    }

    TArray<FString> FailureReasons;
    bool            bConnected = true;
    bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, ApplyCall, TEXT("BaseColor"), FailureReasons);
    bConnected &= ConnectChecked(WetTintColor, FString(), ApplyCall, TEXT("WetTintColor"), FailureReasons);
    bConnected &= ConnectChecked(WetVisualStrength, FString(), ApplyCall, TEXT("WetVisualStrength"), FailureReasons);
    bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, ApplyCall, TEXT("BaseRoughness"), FailureReasons);
    bConnected &= ConnectChecked(WetRoughness, FString(), ApplyCall, TEXT("WetRoughness"), FailureReasons);
    bConnected &= ConnectChecked(SurfaceWaterStrength, FString(), ApplyCall, TEXT("SurfaceWaterStrength"), FailureReasons);

    FString ApplyBaseColorOutput;
    FString ApplyRoughnessOutput;
    FString DebugBaseColorOutput;
    if (!ResolvePreferredOutputName(ApplyCall, TEXT("BaseColor"), ApplyBaseColorOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_ApplyWetness. Available outputs: %s"),
            *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ResolvePreferredOutputName(ApplyCall, TEXT("Roughness"), ApplyRoughnessOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'Roughness' on MF_DWC_ApplyWetness. Available outputs: %s"),
            *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(ApplyCall))));
        bConnected = false;
    }
    if (!ResolvePreferredOutputName(DebugCall, TEXT("BaseColor"), DebugBaseColorOutput))
    {
        FailureReasons.Add(FString::Printf(TEXT("Missing output 'BaseColor' on MF_DWC_WetPartDebug. Available outputs: %s"),
            *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
        bConnected = false;
    }
    bConnected &= ConnectChecked(ApplyCall, ApplyBaseColorOutput, DebugCall, TEXT("BaseColor"), FailureReasons);
    if (!UMaterialEditingLibrary::ConnectMaterialProperty(DebugCall, DebugBaseColorOutput, MP_BaseColor))
    {
        FailureReasons.Add(FString::Printf(TEXT("Failed to connect MF_DWC_WetPartDebug output '%s' to Material BaseColor. Available outputs: %s"),
            DebugBaseColorOutput.IsEmpty() ? TEXT("<first>") : *DebugBaseColorOutput,
            *JoinPinNames(UMaterialEditingLibrary::GetMaterialExpressionOutputNames(DebugCall))));
        bConnected = false;
    }
    if (!UMaterialEditingLibrary::ConnectMaterialProperty(ApplyCall, ApplyRoughnessOutput, MP_Roughness))
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
