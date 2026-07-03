#include "WetClothing/Material/WetClothingMaterialSetup.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

namespace
{
    constexpr const TCHAR* ApplyWetnessFunctionPath = TEXT("/DWC/Materials/Functions/MF_DWC_ApplyWetness.MF_DWC_ApplyWetness");
    constexpr const TCHAR* WetPartDebugFunctionPath = TEXT("/DWC/Materials/Functions/MF_DWC_WetPartDebug.MF_DWC_WetPartDebug");

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

    UMaterialFunctionInterface* ApplyFunction = LoadObject<UMaterialFunctionInterface>(nullptr, ApplyWetnessFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadObject<UMaterialFunctionInterface>(nullptr, WetPartDebugFunctionPath);
    if (ApplyFunction == nullptr || DebugFunction == nullptr)
    {
        Result.Message = TEXT("Could not load MF_DWC_ApplyWetness or MF_DWC_WetPartDebug.");
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

    UMaterialFunctionInterface* ApplyFunction = LoadObject<UMaterialFunctionInterface>(nullptr, ApplyWetnessFunctionPath);
    UMaterialFunctionInterface* DebugFunction = LoadObject<UMaterialFunctionInterface>(nullptr, WetPartDebugFunctionPath);
    return HasFunctionCall(Material, ApplyFunction) && HasFunctionCall(Material, DebugFunction);
}
