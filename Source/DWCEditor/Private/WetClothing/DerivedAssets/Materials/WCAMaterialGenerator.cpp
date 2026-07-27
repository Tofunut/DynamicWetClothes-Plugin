#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "MaterialGraph/MaterialGraph.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialShared.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
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
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    constexpr const TCHAR* DynamicWetClothesPluginName = TEXT("DynamicWetClothes");
    constexpr const TCHAR* GeneratedDwcUnifiedMaterialSuffix = TEXT("_DWC");
    constexpr const TCHAR* GeneratedDwcUnifiedCpuInstanceSuffix = TEXT("_DWC_CPU");
    constexpr const TCHAR* GeneratedDwcUnifiedGpuInstanceSuffix = TEXT("_DWC_GPU");
    constexpr const TCHAR* DwcEvaluateSurfaceAppearanceFunction = TEXT("MF_DWC_EvaluateSurfaceAppearance");
    constexpr const TCHAR* DwcGetRenderProfileFunction = TEXT("MF_DWC_GetRenderProfile");
    constexpr const TCHAR* DwcSampleSurfaceWaterNormalsFunction = TEXT("MF_DWC_SampleSurfaceWaterNormals");
    constexpr const TCHAR* DwcDebugWetPartColorFunction = TEXT("MF_DWC_DebugWetPartColor");
    constexpr int32        DwcSourceGraphTargetMinX = -3600;
    constexpr int32        DwcSourceGraphTargetMinY = -420;
    constexpr int32        DwcSourceGraphMaxWidth = 1100;

    FString BuildPluginDwcMaterialFunctionPath(const FString& FunctionName)
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

    UMaterialFunctionInterface* LoadPluginDwcMaterialFunction(
        const FString& FunctionName,
        FString*       OutObjectPath = nullptr)
    {
        const FString ObjectPath = BuildPluginDwcMaterialFunctionPath(FunctionName);
        if (OutObjectPath != nullptr)
        {
            *OutObjectPath = ObjectPath;
        }
        return ObjectPath.IsEmpty() ? nullptr : LoadObject<UMaterialFunctionInterface>(nullptr, *ObjectPath);
    }

    bool HasMaterialFunctionInput(const UMaterialFunctionInterface* FunctionInterface, const FName InputName)
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

    bool HasMaterialFunctionOutput(const UMaterialFunctionInterface* FunctionInterface, const FName OutputName)
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

    bool HasMaterialFunctionStaticSwitchParameter(
        const UMaterialFunctionInterface* FunctionInterface,
        const FName                       ParameterName)
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

    bool ValidateMaterialFunctionContract(
        const UMaterialFunctionInterface* Function,
        const TCHAR*                      FunctionName,
        const TArray<FName>&              RequiredInputs,
        const TArray<FName>&              RequiredOutputs,
        TArray<FString>&                  OutFailureReasons)
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
            if (!HasMaterialFunctionInput(Function, InputName))
            {
                OutFailureReasons.Add(FString::Printf(
                    TEXT("%s is missing required input '%s'."),
                    FunctionName,
                    *InputName.ToString()));
                bValid = false;
            }
        }
        for (const FName OutputName : RequiredOutputs)
        {
            if (!HasMaterialFunctionOutput(Function, OutputName))
            {
                OutFailureReasons.Add(FString::Printf(
                    TEXT("%s is missing required output '%s'."),
                    FunctionName,
                    *OutputName.ToString()));
                bValid = false;
            }
        }
        return bValid;
    }

    bool IsExpectedMaterialFunctionCall(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const UMaterialFunctionInterface*              ExpectedFunction,
        const TCHAR*                                   ExpectedFunctionName)
    {
        return FunctionCall != nullptr &&
               FunctionCall->MaterialFunction != nullptr &&
               (FunctionCall->MaterialFunction == ExpectedFunction ||
                FunctionCall->MaterialFunction->GetName().Equals(ExpectedFunctionName, ESearchCase::CaseSensitive));
    }

    bool HasMaterialFunctionCall(
        const UMaterialFunction*          MaterialFunction,
        const UMaterialFunctionInterface* CalledFunction,
        const TCHAR*                      ExpectedFunctionName)
    {
        if (MaterialFunction == nullptr || CalledFunction == nullptr)
        {
            return false;
        }
        for (UMaterialExpression* Expression : MaterialFunction->GetExpressions())
        {
            const UMaterialExpressionMaterialFunctionCall* FunctionCall =
                Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (IsExpectedMaterialFunctionCall(FunctionCall, CalledFunction, ExpectedFunctionName))
            {
                return true;
            }
        }
        return false;
    }

    bool ValidateDwcMaterialFunctionSet(
        TArray<FString>&             OutFailureReasons,
        UMaterialFunctionInterface** OutEvaluateFunction = nullptr)
    {
        if (OutEvaluateFunction != nullptr)
        {
            *OutEvaluateFunction = nullptr;
        }

        UMaterialFunctionInterface* RenderProfileFunction =
            LoadPluginDwcMaterialFunction(DwcGetRenderProfileFunction);
        UMaterialFunctionInterface* SurfaceNormalFunction =
            LoadPluginDwcMaterialFunction(DwcSampleSurfaceWaterNormalsFunction);
        UMaterialFunctionInterface* DebugWetPartFunction =
            LoadPluginDwcMaterialFunction(DwcDebugWetPartColorFunction);
        UMaterialFunctionInterface* EvaluateFunction =
            LoadPluginDwcMaterialFunction(DwcEvaluateSurfaceAppearanceFunction);

        static const TArray<FName> RenderProfileInputs = { TEXT("DWCDataUV") };
        static const TArray<FName> RenderProfileOutputs = {
            TEXT("AbsorbedDarkeningStrength"),
            TEXT("AbsorbedGlossinessStrength"),
            TEXT("DropletMaskSlice"),
            TEXT("DropletNormalSlice"),
            TEXT("RivuletMaskSlice"),
            TEXT("RivuletNormalSlice"),
            TEXT("SurfaceWaterNormalStrength"),
            TEXT("SurfaceWaterRoughnessStrength"),
            TEXT("SurfaceVisibilityThreshold"),
            TEXT("RivuletUVScrollSpeed"),
            TEXT("DropletDetailSize"),
            TEXT("RivuletDetailSize")
        };
        static const TArray<FName> SurfaceNormalInputs = {
            TEXT("SurfaceWaterNormalUV"), TEXT("SurfaceTime"),
            TEXT("DropletMaskSlice"), TEXT("DropletNormalSlice"),
            TEXT("RivuletMaskSlice"), TEXT("RivuletNormalSlice"),
            TEXT("DropletDetailSize"), TEXT("RivuletDetailSize"),
            TEXT("RivuletEncodedFlowAngle"), TEXT("RivuletUVScrollSpeed")
        };
        static const TArray<FName> SurfaceNormalOutputs = {
            TEXT("DropletMask"), TEXT("DropletNormal"),
            TEXT("RivuletMask"), TEXT("RivuletNormal")
        };
        static const TArray<FName> DebugWetPartInputs = {
            TEXT("BaseColor"), TEXT("VertexColorRGB"), TEXT("VertexColorAlpha"),
            TEXT("WetnessMask"), TEXT("WetPartDebugStrength"),
            TEXT("DropletCoverage"), TEXT("RivuletCoverage"),
            TEXT("SurfaceWaterDebugStrength"),
            TEXT("DropletDebugColor"), TEXT("RivuletDebugColor")
        };
        static const TArray<FName> DebugWetPartOutputs = {
            TEXT("BaseColor"), TEXT("DebugColor"), TEXT("DebugAlpha")
        };
        static const TArray<FName> EvaluateInputs = {
            TEXT("BaseColor"), TEXT("BaseRoughness"), TEXT("BaseNormal"),
            TEXT("Wetness"), TEXT("DWCDataUV"), TEXT("SurfaceWaterNormalUV"), TEXT("SurfaceTime"),
            TEXT("WetDarkeningStrength"), TEXT("WetRoughness"),
            TEXT("SurfaceWaterTargetRoughness"),
            TEXT("WrinkleNormal"), TEXT("UseWrinkleNormalMap"), TEXT("WrinkleStrength"),
            TEXT("WrinkleWetnessMin"), TEXT("WrinkleWetnessMax"),
            TEXT("TransparencyColor"), TEXT("TransparencyAlpha"), TEXT("UseTransparencyMap"),
            TEXT("TransparencyWetnessMin"), TEXT("TransparencyWetnessMax")
        };
        static const TArray<FName> EvaluateOutputs = {
            TEXT("BaseColor"), TEXT("Roughness"), TEXT("Normal"),
            TEXT("DropletCoverage"), TEXT("RivuletCoverage")
        };

        bool bValid = true;
        bValid &= ValidateMaterialFunctionContract(
            RenderProfileFunction,
            DwcGetRenderProfileFunction,
            RenderProfileInputs,
            RenderProfileOutputs,
            OutFailureReasons);
        bValid &= ValidateMaterialFunctionContract(
            SurfaceNormalFunction,
            DwcSampleSurfaceWaterNormalsFunction,
            SurfaceNormalInputs,
            SurfaceNormalOutputs,
            OutFailureReasons);
        bValid &= ValidateMaterialFunctionContract(
            DebugWetPartFunction,
            DwcDebugWetPartColorFunction,
            DebugWetPartInputs,
            DebugWetPartOutputs,
            OutFailureReasons);
        bValid &= ValidateMaterialFunctionContract(
            EvaluateFunction,
            DwcEvaluateSurfaceAppearanceFunction,
            EvaluateInputs,
            EvaluateOutputs,
            OutFailureReasons);

        if (EvaluateFunction != nullptr &&
            !HasMaterialFunctionStaticSwitchParameter(
                EvaluateFunction,
                DWCWetMaterialParameters::UseSurfaceWater()))
        {
            OutFailureReasons.Add(TEXT(
                "MF_DWC_EvaluateSurfaceAppearance is missing static switch 'DWC_UseSurfaceWater'."));
            bValid = false;
        }

        if (bValid)
        {
            const UMaterialFunction* EvaluateMaterialFunction = Cast<UMaterialFunction>(EvaluateFunction);
            if (EvaluateMaterialFunction == nullptr ||
                !HasMaterialFunctionCall(EvaluateMaterialFunction, RenderProfileFunction, DwcGetRenderProfileFunction) ||
                !HasMaterialFunctionCall(EvaluateMaterialFunction, SurfaceNormalFunction, DwcSampleSurfaceWaterNormalsFunction))
            {
                OutFailureReasons.Add(TEXT(
                    "MF_DWC_EvaluateSurfaceAppearance must call both MF_DWC_GetRenderProfile and MF_DWC_SampleSurfaceWaterNormals. Run create_mf_dwc_evaluate_surface_appearance.py last, after recreating the helper functions."));
                bValid = false;
            }
        }

        if (bValid && OutEvaluateFunction != nullptr)
        {
            *OutEvaluateFunction = EvaluateFunction;
        }
        return bValid;
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
        for (const FWetClothingAuthoredMaterialSlot& SlotState : WetClothingAsset.Authored.PartData.EditableWetPartData.MaterialSlots)
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

    UMaterialInterface* ResolveOriginalSourceMaterial(
        UMaterialInterface*                    CandidateMaterial,
        const FWCAMaterialGenerator::FOptions& Options)
    {
        if (CandidateMaterial == nullptr || Options.OwningWetClothingAsset == nullptr)
        {
            return CandidateMaterial;
        }

        UMaterial* CandidateBase = CandidateMaterial->GetMaterial();
        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
             Options.OwningWetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
        {
            UMaterialInterface* SourceMaterial = MaterialOverride.SourceMaterial.Get();
            UMaterial*          GeneratedMaterial = MaterialOverride.GeneratedMaterial.Get();
            UMaterialInterface* CPUMaterialInstance = MaterialOverride.CPUMaterialInstance.Get();
            UMaterialInterface* GPUMaterialInstance = MaterialOverride.GPUMaterialInstance.Get();
            if (SourceMaterial != nullptr &&
                (CandidateMaterial == GeneratedMaterial ||
                 CandidateMaterial == CPUMaterialInstance ||
                 CandidateMaterial == GPUMaterialInstance ||
                 CandidateBase == GeneratedMaterial))
            {
                return SourceMaterial;
            }
        }

        return CandidateMaterial;
    }

    UMaterialExpressionMaterialFunctionCall* FindFunctionCall(
        UMaterial*                        Material,
        const UMaterialFunctionInterface* Function,
        const TCHAR*                      ExpectedFunctionName = nullptr)
    {
        if (Material == nullptr || Function == nullptr)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression);
            if (ExpectedFunctionName != nullptr
                    ? IsExpectedMaterialFunctionCall(FunctionCall, Function, ExpectedFunctionName)
                    : FunctionCall != nullptr && FunctionCall->MaterialFunction == Function)
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

    UMaterialExpressionComment* CreateGraphComment(
        UMaterial*          Material,
        const TCHAR*        Text,
        const int32         NodePosX,
        const int32         NodePosY,
        const int32         Width,
        const int32         Height,
        const FLinearColor& Color)
    {
        if (Material == nullptr)
        {
            return nullptr;
        }

        UMaterialExpressionComment* Comment = nullptr;
        for (UMaterialExpressionComment* ExistingComment : Material->GetEditorComments())
        {
            if (ExistingComment != nullptr &&
                ExistingComment->Text.Equals(Text, ESearchCase::CaseSensitive))
            {
                Comment = ExistingComment;
                break;
            }
        }

        if (Comment == nullptr)
        {
            Comment = NewObject<UMaterialExpressionComment>(Material, NAME_None, RF_Transactional);
            if (Comment != nullptr)
            {
                Material->GetExpressionCollection().AddComment(Comment);
                Comment->Material = Material;
                Comment->UpdateMaterialExpressionGuid(true, true);
            }
        }

        if (Comment != nullptr)
        {
            Comment->Material = Material;
            Comment->MaterialExpressionEditorX = NodePosX;
            Comment->MaterialExpressionEditorY = NodePosY;
            Comment->Text = Text;
            Comment->SizeX = Width;
            Comment->SizeY = Height;
            Comment->CommentColor = Color;
            Comment->bGroupMode = false;
            Comment->MarkPackageDirty();

            if (Material->MaterialGraph != nullptr && Comment->GraphNode == nullptr)
            {
                Material->MaterialGraph->AddComment(Comment, false);
            }
        }
        return Comment;
    }

    FIntRect GetExpressionBounds(const TArray<UMaterialExpression*>& Expressions)
    {
        bool     bHasAnyExpression = false;
        FIntRect Bounds(0, 0, 0, 0);
        for (const UMaterialExpression* Expression : Expressions)
        {
            if (Expression == nullptr)
            {
                continue;
            }

            const int32 X = Expression->MaterialExpressionEditorX;
            const int32 Y = Expression->MaterialExpressionEditorY;
            if (!bHasAnyExpression)
            {
                Bounds = FIntRect(X, Y, X, Y);
                bHasAnyExpression = true;
            }
            else
            {
                Bounds.Min.X = FMath::Min(Bounds.Min.X, X);
                Bounds.Min.Y = FMath::Min(Bounds.Min.Y, Y);
                Bounds.Max.X = FMath::Max(Bounds.Max.X, X);
                Bounds.Max.Y = FMath::Max(Bounds.Max.Y, Y);
            }
        }
        return Bounds;
    }

    void NormalizeSourceMaterialGraphLayout(UMaterial* Material, const TSet<UMaterialExpression*>& DwcExpressions)
    {
        if (Material == nullptr)
        {
            return;
        }

        TArray<UMaterialExpression*> SourceExpressions;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression != nullptr && !DwcExpressions.Contains(Expression))
            {
                SourceExpressions.Add(Expression);
            }
        }
        if (SourceExpressions.IsEmpty())
        {
            return;
        }

        const FIntRect Bounds = GetExpressionBounds(SourceExpressions);
        const int32    Width = FMath::Max(1, Bounds.Max.X - Bounds.Min.X);
        const int32    Height = FMath::Max(1, Bounds.Max.Y - Bounds.Min.Y);
        const float    Scale = Width > DwcSourceGraphMaxWidth
                                   ? static_cast<float>(DwcSourceGraphMaxWidth) / static_cast<float>(Width)
                                   : 1.0f;

        for (UMaterialExpression* Expression : SourceExpressions)
        {
            const int32 RelativeX = Expression->MaterialExpressionEditorX - Bounds.Min.X;
            const int32 RelativeY = Expression->MaterialExpressionEditorY - Bounds.Min.Y;
            Expression->MaterialExpressionEditorX = DwcSourceGraphTargetMinX + FMath::RoundToInt(static_cast<float>(RelativeX) * Scale);
            Expression->MaterialExpressionEditorY = DwcSourceGraphTargetMinY + FMath::RoundToInt(static_cast<float>(RelativeY) * Scale);
        }

        CreateGraphComment(
            Material,
            TEXT("Source Material Graph"),
            DwcSourceGraphTargetMinX - 120,
            DwcSourceGraphTargetMinY - 120,
            FMath::Max(760, FMath::RoundToInt(static_cast<float>(Width) * Scale) + 260),
            FMath::Max(360, FMath::RoundToInt(static_cast<float>(Height) * Scale) + 260),
            FLinearColor(0.18f, 0.22f, 0.26f, 1.0f));
    }

    void EnsureUnifiedDwcGraphComments(UMaterial* Material)
    {
        CreateGraphComment(
            Material,
            TEXT("DWC Source Material Inputs"),
            -2720,
            -620,
            620,
            1120,
            FLinearColor(0.20f, 0.28f, 0.38f, 1.0f));
        CreateGraphComment(
            Material,
            TEXT("DWC Runtime and Slot Parameters"),
            -2080,
            -500,
            700,
            1760,
            FLinearColor(0.20f, 0.36f, 0.30f, 1.0f));
        CreateGraphComment(
            Material,
            TEXT("DWC Data and Surface Water Normal UVs"),
            -2240,
            720,
            720,
            660,
            FLinearColor(0.22f, 0.30f, 0.46f, 1.0f));
        CreateGraphComment(
            Material,
            TEXT("DWC Wetness Backend Selection"),
            -1640,
            -840,
            560,
            520,
            FLinearColor(0.35f, 0.25f, 0.40f, 1.0f));
        CreateGraphComment(
            Material,
            TEXT("DWC Surface Appearance Evaluation"),
            -760,
            -360,
            720,
            900,
            FLinearColor(0.30f, 0.32f, 0.20f, 1.0f));
        CreateGraphComment(
            Material,
            TEXT("DWC Wet Part Debug Overlay"),
            80,
            -560,
            920,
            560,
            FLinearColor(0.40f, 0.24f, 0.25f, 1.0f));
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

    bool MaterialInterfaceHasTextureParameter(UMaterialInterface* MaterialInterface, const FName ParameterName)
    {
        if (MaterialInterface == nullptr || ParameterName.IsNone())
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        MaterialInterface->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
        return ParameterInfos.ContainsByPredicate(
            [ParameterName](const FMaterialParameterInfo& ParameterInfo)
            {
                return ParameterInfo.Name == ParameterName;
            });
    }

    bool MaterialInterfaceHasScalarParameter(UMaterialInterface* MaterialInterface, const FName ParameterName)
    {
        if (MaterialInterface == nullptr || ParameterName.IsNone())
        {
            return false;
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        MaterialInterface->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
        return ParameterInfos.ContainsByPredicate(
            [ParameterName](const FMaterialParameterInfo& ParameterInfo)
            {
                return ParameterInfo.Name == ParameterName;
            });
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

    void AppendMissingGpuRuntimeMaterialParameters(
        UMaterialInterface* MaterialInterface,
        const bool       bRequireSurfaceWater,
        TArray<FString>& OutMissingParameters)
    {
        if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::WetnessMap()))
        {
            OutMissingParameters.Add(DWCWetMaterialParameters::WetnessMap().ToString());
        }
        if (bRequireSurfaceWater)
        {
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::SurfaceDropletRT()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::SurfaceDropletRT().ToString());
            }
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::SurfaceRivuletRT()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::SurfaceRivuletRT().ToString());
            }
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::DropletMaskTextureArray()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::DropletMaskTextureArray().ToString());
            }
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::DropletNormalTextureArray()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::DropletNormalTextureArray().ToString());
            }
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::RivuletMaskTextureArray()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::RivuletMaskTextureArray().ToString());
            }
            if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::RivuletNormalTextureArray()))
            {
                OutMissingParameters.Add(DWCWetMaterialParameters::RivuletNormalTextureArray().ToString());
            }
        }
        if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::WetPartDataTexture()))
        {
            OutMissingParameters.Add(DWCWetMaterialParameters::WetPartDataTexture().ToString());
        }
        if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::ProfileRemapLUT()))
        {
            OutMissingParameters.Add(DWCWetMaterialParameters::ProfileRemapLUT().ToString());
        }
        if (!MaterialInterfaceHasTextureParameter(MaterialInterface, DWCWetMaterialParameters::GlobalRenderProfileLUT()))
        {
            OutMissingParameters.Add(DWCWetMaterialParameters::GlobalRenderProfileLUT().ToString());
        }
        if (!MaterialInterfaceHasScalarParameter(MaterialInterface, DWCWetMaterialParameters::GlobalRenderProfileTexelSize()))
        {
            OutMissingParameters.Add(DWCWetMaterialParameters::GlobalRenderProfileTexelSize().ToString());
        }
    }

    bool HasRequiredGpuRuntimeMaterialParameters(UMaterialInterface* MaterialInterface, const bool bRequireSurfaceWater)
    {
        TArray<FString> MissingParameters;
        AppendMissingGpuRuntimeMaterialParameters(MaterialInterface, bRequireSurfaceWater, MissingParameters);
        return MissingParameters.IsEmpty();
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

    UMaterialExpressionTextureSampleParameter2D* FindOrCreateTransparencyMapParameter(
        UMaterial* Material,
        int32      NodePosX,
        int32      NodePosY)
    {
        const FName ParameterName = DWCWetMaterialParameters::TransparencyMap();
        if (UMaterialExpressionTextureSampleParameter2D* ExistingParameter = FindTextureSampleParameter(Material, ParameterName))
        {
            ExistingParameter->SamplerType = SAMPLERTYPE_Color;
            ExistingParameter->Desc = TEXT("DWC baked transparency map. RGB stores inner color; alpha stores transparency amount.");
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
            Parameter->Desc = TEXT("DWC baked transparency map. RGB stores inner color; alpha stores transparency amount.");
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

    UMaterialExpressionTextureCoordinate* FindOrCreateSurfaceWaterNormalTextureCoordinate(
        UMaterial*  Material,
        const int32 SurfaceWaterNormalUVChannelIndex,
        const int32 NodePosX,
        const int32 NodePosY)
    {
        if (Material == nullptr || SurfaceWaterNormalUVChannelIndex < 0)
        {
            return nullptr;
        }

        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(Expression))
            {
                if (TextureCoordinate->Desc == TEXT("DWC Surface Water Normal UV"))
                {
                    TextureCoordinate->CoordinateIndex = SurfaceWaterNormalUVChannelIndex;
                    return TextureCoordinate;
                }
            }
        }

        UMaterialExpressionTextureCoordinate* TextureCoordinate = Cast<UMaterialExpressionTextureCoordinate>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                Material,
                UMaterialExpressionTextureCoordinate::StaticClass(),
                NodePosX,
                NodePosY));
        if (TextureCoordinate != nullptr)
        {
            TextureCoordinate->CoordinateIndex = SurfaceWaterNormalUVChannelIndex;
            TextureCoordinate->Desc = TEXT("DWC Surface Water Normal UV");
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

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;

        SourceInstance->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            float Value = 0.0f;
            if (SourceInstance->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value))
            {
                TargetInstance->SetScalarParameterValueEditorOnly(ParameterInfo, Value);
            }
        }

        ParameterInfos.Reset();
        ParameterIds.Reset();
        SourceInstance->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            FLinearColor Value = FLinearColor::White;
            if (SourceInstance->GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value))
            {
                TargetInstance->SetVectorParameterValueEditorOnly(ParameterInfo, Value);
            }
        }

        ParameterInfos.Reset();
        ParameterIds.Reset();
        SourceInstance->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
        for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
        {
            UTexture* Value = nullptr;
            if (SourceInstance->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value))
            {
                TargetInstance->SetTextureParameterValueEditorOnly(ParameterInfo, Value);
            }
        }

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

        Material->UpdateCachedExpressionData();
        Material->PostEditChange();
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

} // namespace

namespace
{
    const FName DwcUseGpuBackendParameterName(TEXT("DWC_UseGPUBackend"));

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
        const UObject* /*FallbackSourceAsset*/)
    {
        if (Options.OwningWetClothingAsset == nullptr ||
            Options.OwningWetClothingAsset->GetOutermost() == nullptr)
        {
            return FString();
        }

        const FString WcaPackageName = Options.OwningWetClothingAsset->GetOutermost()->GetName();
        if (!FPackageName::IsValidLongPackageName(WcaPackageName))
        {
            return FString();
        }

        const FString WcaFolder = FPackageName::GetLongPackagePath(WcaPackageName);
        return WcaFolder / TEXT("Generated") / Options.OwningWetClothingAsset->GetName() / TEXT("Materials");
    }

    FString BuildUnifiedBaseMaterialPackageName(
        const UMaterial*                       SourceBaseMaterial,
        const FWCAMaterialGenerator::FOptions& Options)
    {
        if (SourceBaseMaterial == nullptr)
        {
            return FString();
        }

        const FString Folder = BuildUnifiedGeneratedMaterialFolder(Options, SourceBaseMaterial);
        const FString SourceAssetName = FPackageName::GetLongPackageAssetName(
            StripKnownDwcSuffix(SourceBaseMaterial->GetOutermost()->GetName()));
        FString AssetStem = GetDwcGeneratedAssetStem(SourceAssetName);
        if (Options.MaterialSlotIndex != INDEX_NONE)
        {
            AssetStem += FString::Printf(TEXT("_Slot%d"), Options.MaterialSlotIndex);
        }
        return Folder / FString::Printf(TEXT("M_%s%s"), *AssetStem, GeneratedDwcUnifiedMaterialSuffix);
    }

    FString BuildUnifiedBackendInstancePackageName(
        const UMaterialInterface*              SourceMaterial,
        const TCHAR*                           BackendSuffix,
        const FWCAMaterialGenerator::FOptions& Options)
    {
        if (SourceMaterial == nullptr || BackendSuffix == nullptr)
        {
            return FString();
        }

        const FString Folder = BuildUnifiedGeneratedMaterialFolder(Options, SourceMaterial);
        const FString SourceAssetName = FPackageName::GetLongPackageAssetName(
            StripKnownDwcSuffix(SourceMaterial->GetOutermost()->GetName()));
        FString AssetStem = GetDwcGeneratedAssetStem(SourceAssetName);
        if (Options.MaterialSlotIndex != INDEX_NONE)
        {
            AssetStem += FString::Printf(TEXT("_Slot%d"), Options.MaterialSlotIndex);
        }
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

        UMaterialFunctionInterface* EvaluateFunction =
            LoadPluginDwcMaterialFunction(DwcEvaluateSurfaceAppearanceFunction);
        UMaterialFunctionInterface* DebugWetPartFunction =
            LoadPluginDwcMaterialFunction(DwcDebugWetPartColorFunction);
        UMaterialExpressionMaterialFunctionCall* Evaluate =
            FindFunctionCall(
                const_cast<UMaterial*>(Material),
                EvaluateFunction,
                DwcEvaluateSurfaceAppearanceFunction);
        UMaterialExpressionMaterialFunctionCall* DebugWetPart =
            FindFunctionCall(
                const_cast<UMaterial*>(Material),
                DebugWetPartFunction,
                DwcDebugWetPartColorFunction);
        return Evaluate != nullptr &&
               DebugWetPart != nullptr &&
               HasDwcBackendStaticSwitchParameter(Material) &&
               FindTextureSampleParameter(
                   const_cast<UMaterial*>(Material),
                   DWCWetMaterialParameters::TransparencyMap()) != nullptr;
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

    bool RetireExistingGeneratedAssetForReplacement(
        UObject*       ExistingObject,
        const FString& ObjectPath,
        FString&       OutErrorMessage)
    {
        if (ExistingObject == nullptr)
        {
            return true;
        }

        UPackage*     PreviousPackage = ExistingObject->GetOutermost();
        const FString RetiredName = FString::Printf(
            TEXT("%s_Replaced_%s"),
            *ExistingObject->GetName(),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits));

        ExistingObject->Modify();
        FAssetRegistryModule::AssetDeleted(ExistingObject);
        ExistingObject->ClearFlags(RF_Public | RF_Standalone);
        ExistingObject->SetFlags(RF_Transient);

        if (!ExistingObject->Rename(
                *RetiredName,
                GetTransientPackage(),
                REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not retire existing generated asset '%s' before regeneration."),
                *ObjectPath);
            return false;
        }

        if (PreviousPackage != nullptr)
        {
            PreviousPackage->MarkPackageDirty();
        }
        return true;
    }

    bool CreateUnifiedDwcMaterialGraph(
        UMaterial*                             Material,
        const FWCAMaterialGenerator::FOptions& Options,
        TArray<FString>&                       FailureReasons)
    {
        if (Material == nullptr)
        {
            FailureReasons.Add(TEXT("Cannot configure a null generated material."));
            return false;
        }

        TSet<UMaterialExpression*> PreExistingExpressions;
        for (UMaterialExpression* Expression : Material->GetExpressions())
        {
            if (Expression != nullptr)
            {
                PreExistingExpressions.Add(Expression);
            }
        }

        UMaterialFunctionInterface* EvaluateFunction = nullptr;
        if (!ValidateDwcMaterialFunctionSet(FailureReasons, &EvaluateFunction))
        {
            return false;
        }
        UMaterialFunctionInterface* DebugWetPartFunction =
            LoadPluginDwcMaterialFunction(DwcDebugWetPartColorFunction);

        FString              BaseColorOutputName;
        UMaterialExpression* BaseColorInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_BaseColor, FVector2D(-2600.0f, -280.0f), BaseColorOutputName);
        FString              RoughnessOutputName;
        UMaterialExpression* RoughnessInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_Roughness, FVector2D(-2600.0f, -40.0f), RoughnessOutputName);
        FString              BaseNormalOutputName;
        UMaterialExpression* BaseNormalInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_Normal, FVector2D(-2600.0f, 200.0f), BaseNormalOutputName);
        FString              SpecularOutputName;
        UMaterialExpression* SpecularInput = ResolveMaterialPropertyInputOrFallback(
            Material, MP_Specular, FVector2D(-2600.0f, 440.0f), SpecularOutputName);

        UMaterialExpressionMaterialFunctionCall* Evaluate =
            CreateFunctionCall(Material, EvaluateFunction, -620, -100);
        UMaterialExpressionMaterialFunctionCall* DebugWetPart =
            CreateFunctionCall(Material, DebugWetPartFunction, 360, -120);
        UMaterialExpressionVertexColor*       VertexColor = FindOrCreateVertexColor(Material, -2600, -520);
        UMaterialExpressionTextureCoordinate* DWCDataUV = FindOrCreateDWCDataTextureCoordinate(
            Material, Options.DWCDataUVChannelIndex, -2140, 820);
        UMaterialExpressionTextureCoordinate* SurfaceWaterNormalUV =
            FindOrCreateSurfaceWaterNormalTextureCoordinate(
                Material, Options.SurfaceWaterNormalUVChannelIndex, -2140, 980);
        UMaterialExpressionTextureSampleParameter2D* WetnessMap =
            FindOrCreateGPUWetnessMapParameter(Material, -1900, 820);
        UMaterialExpressionStaticSwitchParameter* WetnessSourceSwitch = CreateDwcBackendStaticSwitch(
            Material,
            -1520,
            -720,
            TEXT("Selects the compiled CPU/GPU absorbed-wetness source before the shared appearance evaluation."));

        UMaterialExpressionScalarParameter* WetDarkeningStrength = CreateScalarParameter(
            Material, TEXT("DWC_WetDarkeningStrength"), 0.35f, -2140, -420);
        UMaterialExpressionScalarParameter* WetRoughness = CreateScalarParameter(
            Material, TEXT("DWC_WetRoughness"), 0.12f, -2140, -320);
        UMaterialExpressionScalarParameter* SurfaceWaterTargetRoughness = FindOrCreateScalarParameter(
            Material,
            DWCWetMaterialParameters::SurfaceWaterTargetRoughness(),
            Options.SurfaceWaterTargetRoughness,
            -1900,
            1240);
        UMaterialExpressionScalarParameter* SurfaceTime = FindOrCreateScalarParameter(
            Material,
            DWCWetMaterialParameters::SurfaceWaterTime(),
            0.0f,
            -1660,
            1240);

        UMaterialExpressionTextureSampleParameter2D* WrinkleNormalMap = FindOrCreateTextureSampleParameter(
            Material, TEXT("DWC_WrinkleNormalMap"), -1900, 260);
        UMaterialExpressionScalarParameter* UseWrinkleNormalMap = FindOrCreateScalarParameter(
            Material, TEXT("DWC_UseWrinkleNormalMap"), 0.0f, -1660, 260);
        UMaterialExpressionScalarParameter* WrinkleStrength = FindOrCreateScalarParameter(
            Material, TEXT("DWC_WrinkleStrength"), 1.0f, -1660, 350);
        UMaterialExpressionScalarParameter* WrinkleWetnessMin = FindOrCreateScalarParameter(
            Material, TEXT("DWC_WrinkleWetnessMin"), 0.25f, -1660, 440);
        UMaterialExpressionScalarParameter* WrinkleWetnessMax = FindOrCreateScalarParameter(
            Material, TEXT("DWC_WrinkleWetnessMax"), 1.0f, -1660, 530);

        UMaterialExpressionTextureSampleParameter2D* TransparencyMap =
            FindOrCreateTransparencyMapParameter(Material, -1900, 1360);
        UMaterialExpressionScalarParameter* UseTransparencyMap = FindOrCreateScalarParameter(
            Material, DWCWetMaterialParameters::UseTransparencyMap(), 0.0f, -1660, 1360);
        UMaterialExpressionScalarParameter* TransparencyWetnessMin = FindOrCreateScalarParameter(
            Material,
            DWCWetMaterialParameters::TransparencyWetnessMin(),
            DWCWetMaterialParameters::DefaultTransparencyWetnessMin(),
            -1660,
            1450);
        UMaterialExpressionScalarParameter* TransparencyWetnessMax = FindOrCreateScalarParameter(
            Material,
            DWCWetMaterialParameters::TransparencyWetnessMax(),
            DWCWetMaterialParameters::DefaultTransparencyWetnessMax(),
            -1660,
            1540);

        if (BaseColorInput == nullptr || RoughnessInput == nullptr || BaseNormalInput == nullptr ||
            SpecularInput == nullptr ||
            Evaluate == nullptr || DebugWetPart == nullptr || VertexColor == nullptr || DWCDataUV == nullptr ||
            SurfaceWaterNormalUV == nullptr || WetnessMap == nullptr || WetnessSourceSwitch == nullptr ||
            WetDarkeningStrength == nullptr || WetRoughness == nullptr ||
            SurfaceWaterTargetRoughness == nullptr || SurfaceTime == nullptr || WrinkleNormalMap == nullptr ||
            UseWrinkleNormalMap == nullptr || WrinkleStrength == nullptr ||
            WrinkleWetnessMin == nullptr || WrinkleWetnessMax == nullptr ||
            TransparencyMap == nullptr || UseTransparencyMap == nullptr ||
            TransparencyWetnessMin == nullptr || TransparencyWetnessMax == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create one or more nodes for the unified DWC surface graph."));
            return false;
        }

        EnsureUnifiedDwcGraphComments(Material);
        bool bConnected = true;
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WetnessMap, FailureReasons);
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, WrinkleNormalMap, FailureReasons);
        bConnected &= ConnectTextureCoordinateChecked(DWCDataUV, TransparencyMap, FailureReasons);
        bConnected &= ConnectChecked(WetnessMap, TEXT("R"), WetnessSourceSwitch, TEXT("True"), FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("R"), WetnessSourceSwitch, TEXT("False"), FailureReasons);

        bConnected &= ConnectChecked(BaseColorInput, BaseColorOutputName, Evaluate, TEXT("BaseColor"), FailureReasons);
        bConnected &= ConnectChecked(RoughnessInput, RoughnessOutputName, Evaluate, TEXT("BaseRoughness"), FailureReasons);
        bConnected &= ConnectChecked(BaseNormalInput, BaseNormalOutputName, Evaluate, TEXT("BaseNormal"), FailureReasons);
        bConnected &= ConnectChecked(WetnessSourceSwitch, FString(), Evaluate, TEXT("Wetness"), FailureReasons);
        bConnected &= ConnectChecked(WetDarkeningStrength, FString(), Evaluate, TEXT("WetDarkeningStrength"), FailureReasons);
        bConnected &= ConnectChecked(WetRoughness, FString(), Evaluate, TEXT("WetRoughness"), FailureReasons);
        bConnected &= ConnectChecked(DWCDataUV, FString(), Evaluate, TEXT("DWCDataUV"), FailureReasons);
        bConnected &= ConnectChecked(SurfaceWaterNormalUV, FString(), Evaluate, TEXT("SurfaceWaterNormalUV"), FailureReasons);
        bConnected &= ConnectChecked(SurfaceTime, FString(), Evaluate, TEXT("SurfaceTime"), FailureReasons);
        bConnected &= ConnectChecked(SurfaceWaterTargetRoughness, FString(), Evaluate, TEXT("SurfaceWaterTargetRoughness"), FailureReasons);

        bConnected &= ConnectChecked(WrinkleNormalMap, TEXT("RGB"), Evaluate, TEXT("WrinkleNormal"), FailureReasons);
        bConnected &= ConnectChecked(UseWrinkleNormalMap, FString(), Evaluate, TEXT("UseWrinkleNormalMap"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleStrength, FString(), Evaluate, TEXT("WrinkleStrength"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMin, FString(), Evaluate, TEXT("WrinkleWetnessMin"), FailureReasons);
        bConnected &= ConnectChecked(WrinkleWetnessMax, FString(), Evaluate, TEXT("WrinkleWetnessMax"), FailureReasons);

        bConnected &= ConnectChecked(TransparencyMap, TEXT("RGB"), Evaluate, TEXT("TransparencyColor"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyMap, TEXT("A"), Evaluate, TEXT("TransparencyAlpha"), FailureReasons);
        bConnected &= ConnectChecked(UseTransparencyMap, FString(), Evaluate, TEXT("UseTransparencyMap"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyWetnessMin, FString(), Evaluate, TEXT("TransparencyWetnessMin"), FailureReasons);
        bConnected &= ConnectChecked(TransparencyWetnessMax, FString(), Evaluate, TEXT("TransparencyWetnessMax"), FailureReasons);

        FString BaseColorResultOutput;
        FString RoughnessResultOutput;
        FString NormalResultOutput;
        FString DropletCoverageOutput;
        FString RivuletCoverageOutput;
        bConnected &= ResolveRequiredOutputName(Evaluate, TEXT("BaseColor"), BaseColorResultOutput);
        bConnected &= ResolveRequiredOutputName(Evaluate, TEXT("Roughness"), RoughnessResultOutput);
        bConnected &= ResolveRequiredOutputName(Evaluate, TEXT("Normal"), NormalResultOutput);
        bConnected &= ResolveRequiredOutputName(Evaluate, TEXT("DropletCoverage"), DropletCoverageOutput);
        bConnected &= ResolveRequiredOutputName(Evaluate, TEXT("RivuletCoverage"), RivuletCoverageOutput);
        if (!bConnected)
        {
            FailureReasons.Add(TEXT("MF_DWC_EvaluateSurfaceAppearance does not expose the required contract."));
            return false;
        }

        UMaterialExpressionScalarParameter* WetPartDebugStrength = FindOrCreateScalarParameter(
            Material, DWCWetMaterialParameters::WetPartDebugStrength(), 0.0f, 180, 520);
        UMaterialExpressionScalarParameter* SurfaceWaterDebugStrength = FindOrCreateScalarParameter(
            Material, DWCWetMaterialParameters::SurfaceWaterDebugStrength(), 0.0f, 180, 620);
        UMaterialExpressionVectorParameter* DropletDebugColor = FindOrCreateVectorParameter(
            Material, DWCWetMaterialParameters::SurfaceWaterDebugDropletColor(),
            FLinearColor(1.0f, 0.85f, 0.0f, 1.0f), 180, 720);
        UMaterialExpressionVectorParameter* RivuletDebugColor = FindOrCreateVectorParameter(
            Material, DWCWetMaterialParameters::SurfaceWaterDebugRivuletColor(),
            FLinearColor(0.72f, 0.45f, 1.0f, 1.0f), 180, 820);
        if (WetPartDebugStrength == nullptr || SurfaceWaterDebugStrength == nullptr ||
            DropletDebugColor == nullptr || RivuletDebugColor == nullptr)
        {
            FailureReasons.Add(TEXT("Could not create the unified DWC debug graph."));
            return false;
        }

        const int32 VertexRGB = ResolveExpressionOutputIndex(VertexColor, TEXT("RGB"), 0);
        const int32 VertexAlpha = ResolveExpressionOutputIndex(VertexColor, TEXT("A"), 4);
        if (VertexRGB == INDEX_NONE || VertexAlpha == INDEX_NONE)
        {
            FailureReasons.Add(TEXT("Could not resolve VertexColor outputs for WetPart debug."));
            return false;
        }
        bConnected &= ConnectChecked(Evaluate, BaseColorResultOutput, DebugWetPart, TEXT("BaseColor"), FailureReasons);
        // UMaterialExpressionVertexColor may expose unnamed outputs in editor APIs.
        // Its first/default output is the combined RGB value.
        bConnected &= ConnectChecked(
            VertexColor,
            FString(),
            DebugWetPart,
            TEXT("VertexColorRGB"),
            FailureReasons);
        bConnected &= ConnectChecked(VertexColor, TEXT("A"), DebugWetPart, TEXT("VertexColorAlpha"), FailureReasons);
        bConnected &= ConnectChecked(WetnessSourceSwitch, FString(), DebugWetPart, TEXT("WetnessMask"), FailureReasons);
        bConnected &= ConnectChecked(WetPartDebugStrength, FString(), DebugWetPart, TEXT("WetPartDebugStrength"), FailureReasons);
        bConnected &= ConnectChecked(Evaluate, DropletCoverageOutput, DebugWetPart, TEXT("DropletCoverage"), FailureReasons);
        bConnected &= ConnectChecked(Evaluate, RivuletCoverageOutput, DebugWetPart, TEXT("RivuletCoverage"), FailureReasons);
        bConnected &= ConnectChecked(SurfaceWaterDebugStrength, FString(), DebugWetPart, TEXT("SurfaceWaterDebugStrength"), FailureReasons);
        bConnected &= ConnectChecked(DropletDebugColor, FString(), DebugWetPart, TEXT("DropletDebugColor"), FailureReasons);
        bConnected &= ConnectChecked(RivuletDebugColor, FString(), DebugWetPart, TEXT("RivuletDebugColor"), FailureReasons);

        FString DebugBaseColorOutput;
        bConnected &= ResolveRequiredOutputName(DebugWetPart, TEXT("BaseColor"), DebugBaseColorOutput);

        if (!UMaterialEditingLibrary::ConnectMaterialProperty(DebugWetPart, DebugBaseColorOutput, MP_BaseColor))
        {
            FailureReasons.Add(TEXT("Failed to connect unified DWC BaseColor/debug output."));
            bConnected = false;
        }
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(Evaluate, RoughnessResultOutput, MP_Roughness))
        {
            FailureReasons.Add(TEXT("Failed to connect unified DWC Roughness output."));
            bConnected = false;
        }
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(Evaluate, NormalResultOutput, MP_Normal))
        {
            FailureReasons.Add(TEXT("Failed to connect unified DWC Normal output."));
            bConnected = false;
        }
        if (!UMaterialEditingLibrary::ConnectMaterialProperty(SpecularInput, SpecularOutputName, MP_Specular))
        {
            FailureReasons.Add(TEXT("Failed to preserve the source material Specular output."));
            bConnected = false;
        }

        if (bConnected)
        {
            TSet<UMaterialExpression*> DwcExpressions;
            for (UMaterialExpression* Expression : Material->GetExpressions())
            {
                if (Expression != nullptr && !PreExistingExpressions.Contains(Expression))
                {
                    DwcExpressions.Add(Expression);
                }
            }
            NormalizeSourceMaterialGraphLayout(Material, DwcExpressions);
            Material->EditorX = 1480;
            Material->EditorY = -180;
            EnsureUnifiedDwcGraphComments(Material);
            Material->UpdateCachedExpressionData();
            if (Material->MaterialGraph != nullptr)
            {
                Material->MaterialGraph->RebuildGraph();
            }
        }
        return bConnected;
    }

    UMaterial* CreateOrLoadUnifiedDwcBaseMaterial(
        UMaterial*                             SourceBaseMaterial,
        const FWCAMaterialGenerator::FOptions& Options,
        FString&                               OutErrorMessage,
        bool&                                  bOutReusedExisting)
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
            if (Cast<UMaterial>(ExistingObject) == nullptr)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Generated material path '%s' is occupied by '%s' (%s)."),
                    *GeneratedObjectPath,
                    *GetNameSafe(ExistingObject),
                    *GetNameSafe(ExistingObject->GetClass()));
                return nullptr;
            }

            if (!RetireExistingGeneratedAssetForReplacement(ExistingObject, GeneratedObjectPath, OutErrorMessage))
            {
                return nullptr;
            }
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

    bool SetDwcStaticSwitchOverrides(
        UMaterialInstanceConstant* Instance,
        UMaterialInterface*        GeneratedParent,
        const bool                 bUseGPUBackend,
        const bool                 bUseSurfaceWater,
        FString&                   OutErrorMessage)
    {
        if (Instance == nullptr || GeneratedParent == nullptr)
        {
            OutErrorMessage = TEXT("DWC static permutation setup requires an instance and parent material.");
            return false;
        }

        if (UMaterial* ParentMaterial = Cast<UMaterial>(GeneratedParent))
        {
            ParentMaterial->UpdateCachedExpressionData();
        }

        TArray<FMaterialParameterInfo> ParameterInfos;
        TArray<FGuid>                  ParameterIds;
        GeneratedParent->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterIds);

        struct FDesiredSwitch
        {
            FName Name;
            bool  Value = false;
        };
        const FDesiredSwitch DesiredSwitches[] = {
            { DwcUseGpuBackendParameterName, bUseGPUBackend },
            { DWCWetMaterialParameters::UseSurfaceWater(), bUseSurfaceWater }
        };
        const FName LegacyProfileSwitches[] = {
            DWCWetMaterialParameters::UseDropletNormal(),
            DWCWetMaterialParameters::UseRivuletNormal()
        };

        FStaticParameterSet StaticParameters = Instance->GetStaticParameters();
        StaticParameters.StaticSwitchParameters.RemoveAll(
            [&LegacyProfileSwitches](const FStaticSwitchParameter& Parameter)
            {
                for (const FName& LegacyName : LegacyProfileSwitches)
                {
                    if (Parameter.ParameterInfo.Name == LegacyName)
                    {
                        return true;
                    }
                }
                return false;
            });

        for (const FDesiredSwitch& Desired : DesiredSwitches)
        {
            int32 ParameterIndex = INDEX_NONE;
            for (int32 Index = 0; Index < ParameterInfos.Num(); ++Index)
            {
                if (ParameterInfos[Index].Name == Desired.Name &&
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
                    *Desired.Name.ToString());
                return false;
            }

            FStaticSwitchParameter* ExistingParameter =
                StaticParameters.StaticSwitchParameters.FindByPredicate(
                    [&](const FStaticSwitchParameter& Parameter)
                    {
                        return Parameter.ParameterInfo == ParameterInfos[ParameterIndex];
                    });
            if (ExistingParameter != nullptr)
            {
                ExistingParameter->Value = Desired.Value;
                ExistingParameter->bOverride = true;
                ExistingParameter->ExpressionGUID = ParameterIds[ParameterIndex];
            }
            else
            {
                StaticParameters.StaticSwitchParameters.Add(FStaticSwitchParameter(
                    ParameterInfos[ParameterIndex],
                    Desired.Value,
                    true,
                    ParameterIds[ParameterIndex]));
            }
        }

        Instance->UpdateStaticPermutation(StaticParameters, nullptr);
        Instance->UpdateCachedData();
        return true;
    }

    UMaterialInstanceConstant* CreateOrUpdateBackendMaterialInstance(
        UMaterialInterface*                    SourceMaterial,
        UMaterial*                             GeneratedParent,
        const TCHAR*                           Suffix,
        const FWCAMaterialGenerator::FOptions& Options,
        const bool                             bUseGPUBackend,
        FString&                               OutErrorMessage,
        bool&                                  bOutReusedExisting)
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

        if (ExistingObject != nullptr)
        {
            if (!RetireExistingGeneratedAssetForReplacement(ExistingObject, ObjectPath, OutErrorMessage))
            {
                return nullptr;
            }
            Instance = nullptr;
        }

        UPackage* InstancePackage = CreatePackage(*PackageName);
        Instance = NewObject<UMaterialInstanceConstant>(
            InstancePackage,
            *AssetName,
            RF_Public | RF_Standalone | RF_Transactional);
        if (Instance == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not create generated material instance '%s'."), *ObjectPath);
            return nullptr;
        }
        FAssetRegistryModule::AssetCreated(Instance);

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
        if (!SetDwcStaticSwitchOverrides(
                Instance,
                GeneratedParent,
                bUseGPUBackend,
                bUseGPUBackend && Options.bUseSurfaceWater,
                StaticSwitchError))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not set DWC static switches on '%s'. %s"),
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
    const EDWCSimulationMode SimulationMode,
    const int32              MaterialSlotIndex)
{
    FOptions Options;
    Options.SimulationMode = SimulationMode;
    if (WetClothingAsset != nullptr)
    {
        Options.OwningWetClothingAsset = WetClothingAsset;
        Options.DWCDataUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
        Options.OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
        Options.MaterialSlotIndex = MaterialSlotIndex;
        Options.SurfaceWaterNormalUVChannelIndex = Options.OriginalUVChannelIndex;
        const FWetClothingAuthoredMaterialSlot* AuthoredSlot =
            WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (const FSurfaceWaterMaterialSlotData* SlotData = AuthoredSlot != nullptr ? &AuthoredSlot->SurfaceWater : nullptr)
        {
            if (SlotData->SurfaceWaterNormalUVChannel != INDEX_NONE)
            {
                Options.SurfaceWaterNormalUVChannelIndex = SlotData->SurfaceWaterNormalUVChannel;
            }
        }
        Options.bUseSurfaceWater = false;

        const FWetClothingEditableWetPartData& EditableData = WetClothingAsset->Authored.PartData.EditableWetPartData;
        for (const FWetClothingAuthoredMaterialSlot& SlotData : EditableData.MaterialSlots)
        {
            if (MaterialSlotIndex != INDEX_NONE && SlotData.MaterialSlotIndex != MaterialSlotIndex)
            {
                continue;
            }

            for (const FWetClothingWetPartEntry& Entry : SlotData.WetPartEntries)
            {
                const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
                FWetnessProfileParameters        Parameters = Profile != nullptr ? Profile->Parameters : FWetnessProfileParameters();
                if (Profile != nullptr && Profile->SourceProfile.IsValid())
                {
                    if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(Profile->SourceProfile.TryLoad()))
                    {
                        Parameters = SourceProfile->GetParameters();
                    }
                }

                const bool                            bProfileUsesSurfaceWater = Parameters.SupportsSurfaceWater();
                Options.bUseSurfaceWater |= bProfileUsesSurfaceWater;
            }
        }
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
    if (Options.OwningWetClothingAsset == nullptr ||
        Options.OwningWetClothingAsset->GetOutermost() == nullptr ||
        !FPackageName::IsValidLongPackageName(Options.OwningWetClothingAsset->GetOutermost()->GetName()))
    {
        Result.Message = TEXT("Unified DWC material generation requires a saved Wet Clothing Asset owner so outputs can be written under its Generated folder.");
        return Result;
    }
    SourceMaterial = ResolveOriginalSourceMaterial(SourceMaterial, Options);
    if (SourceMaterial == nullptr)
    {
        Result.Message = TEXT("Could not resolve the original source material for DWC generation.");
        return Result;
    }

    if (Options.DWCDataUVChannelIndex < 0 || Options.DWCDataUVChannelIndex > 7)
    {
        Result.Message = TEXT("Unified DWC material generation requires a valid DWC Data UV channel.");
        return Result;
    }
    if (Options.bUseSurfaceWater &&
        (Options.SurfaceWaterNormalUVChannelIndex < 0 || Options.SurfaceWaterNormalUVChannelIndex > 7))
    {
        Result.Message = TEXT("Unified DWC material generation requires a valid Surface Water Normal UV channel for this slot.");
        return Result;
    }

    // Material functions are manually authored/created by the editor Python scripts.
    // Generate Materials only validates and references them; it never creates or modifies their graphs.
    {
        TArray<FString>             FunctionFailures;
        UMaterialFunctionInterface* EvaluateFunction = nullptr;
        if (!ValidateDwcMaterialFunctionSet(FunctionFailures, &EvaluateFunction))
        {
            Result.Message = TEXT("The DWC material-function set is missing or does not match the required contract.\n") +
                             FString::Join(FunctionFailures, TEXT("\n"));
            return Result;
        }
        Result.EvaluateSurfaceAppearanceFunction = EvaluateFunction;
    }

    UMaterial* SourceBaseMaterial = const_cast<UMaterial*>(SourceMaterial->GetMaterial());
    if (SourceBaseMaterial == nullptr)
    {
        Result.Message = FString::Printf(TEXT("'%s' has no editable base material."), *GetNameSafe(SourceMaterial));
        return Result;
    }
    if (IsUnifiedDwcMaterial(SourceBaseMaterial))
    {
        Result.Message = FString::Printf(
            TEXT("'%s' is already a generated DWC material. Select the original source material or regenerate from the recorded WCA override."),
            *GetNameSafe(SourceMaterial));
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
    if (MaterialInterface == nullptr ||
        !IsUnifiedDwcMaterial(MaterialInterface->GetMaterial()))
    {
        return false;
    }

    UMaterial*                               Material = MaterialInterface->GetMaterial();
    UMaterialExpressionMaterialFunctionCall* Evaluate = FindFunctionCall(
        Material,
        LoadPluginDwcMaterialFunction(DwcEvaluateSurfaceAppearanceFunction),
        DwcEvaluateSurfaceAppearanceFunction);
    UMaterialExpressionMaterialFunctionCall* DebugWetPart = FindFunctionCall(
        Material,
        LoadPluginDwcMaterialFunction(DwcDebugWetPartColorFunction),
        DwcDebugWetPartColorFunction);
    if (Evaluate == nullptr ||
        DebugWetPart == nullptr ||
        FindTextureSampleParameter(Material, DWCWetMaterialParameters::WetnessMap()) == nullptr ||
        FindScalarParameter(Material, DWCWetMaterialParameters::WetPartDebugStrength()) == nullptr ||
        FindScalarParameter(Material, DWCWetMaterialParameters::SurfaceWaterDebugStrength()) == nullptr ||
        FindVectorParameter(Material, DWCWetMaterialParameters::SurfaceWaterDebugDropletColor()) == nullptr ||
        FindVectorParameter(Material, DWCWetMaterialParameters::SurfaceWaterDebugRivuletColor()) == nullptr ||
        FindScalarParameter(Material, TEXT("DWC_WetRoughness")) == nullptr ||
        FindScalarParameter(Material, DWCWetMaterialParameters::SurfaceWaterTargetRoughness()) == nullptr ||
        !IsFunctionInputConnected(Evaluate, TEXT("Wetness")) ||
        !IsFunctionInputConnected(Evaluate, TEXT("DWCDataUV")) ||
        !IsFunctionInputConnected(Evaluate, TEXT("SurfaceWaterNormalUV")))
    {
        return false;
    }
    if (Options.SimulationMode == EDWCSimulationMode::WetnessMapGPU &&
        !HasRequiredGpuRuntimeMaterialParameters(MaterialInterface, Options.bUseSurfaceWater))
    {
        return false;
    }

    const UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(MaterialInterface);
    if (Instance == nullptr)
    {
        return true;
    }

    UMaterialInstanceConstant* MutableInstance = const_cast<UMaterialInstanceConstant*>(Instance);
    const bool                 bConfiguredForGPU = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(
        MutableInstance,
        DwcUseGpuBackendParameterName,
        EMaterialParameterAssociation::GlobalParameter);
    const bool bConfiguredForSurfaceWater = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(
        MutableInstance,
        DWCWetMaterialParameters::UseSurfaceWater(),
        EMaterialParameterAssociation::GlobalParameter);
    const bool bExpectGPU = Options.SimulationMode == EDWCSimulationMode::WetnessMapGPU;
    const bool bExpectSurfaceWater = bExpectGPU && Options.bUseSurfaceWater;
    return bConfiguredForGPU == bExpectGPU &&
           bConfiguredForSurfaceWater == bExpectSurfaceWater;
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

#if WITH_EDITORONLY_DATA
    UMaterialFunctionInterface* ExpectedFunction =
        LoadPluginDwcMaterialFunction(DwcEvaluateSurfaceAppearanceFunction);
    if (WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction == nullptr ||
        WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction != ExpectedFunction)
    {
        OutMessages.Add(TEXT("Generated Materials: MF_DWC_EvaluateSurfaceAppearance is missing or out of date. Use Generate Materials."));
    }
#endif

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

#if WITH_EDITORONLY_DATA
    UMaterialFunctionInterface* ExpectedFunction =
        LoadPluginDwcMaterialFunction(DwcEvaluateSurfaceAppearanceFunction);
    if (WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction == nullptr ||
        WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction != ExpectedFunction)
    {
        OutMessages.Add(TEXT("Generated Materials: MF_DWC_EvaluateSurfaceAppearance is missing or out of date. Use Generate Materials."));
    }
#endif

    const FDWCWetClothingAssetSetupSettings& Setup = WetClothingAsset->GetSetupSettings();
    if (!Setup.bBuildCPUVertexSimulationData && !Setup.bBuildGPUWetnessMapSimulationData)
    {
        return;
    }

    FString SharedFunctionError;
    if (!ValidateSurfaceAppearanceFunctions(SharedFunctionError))
    {
        OutMessages.Add(TEXT("Generated Materials: ") + SharedFunctionError);
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

        const FWCAMaterialGenerator::FOptions CPUOptions = MakeOptionsForAsset(
            WetClothingAsset,
            EDWCSimulationMode::VertexCPU,
            MaterialSlotIndex);
        const FWCAMaterialGenerator::FOptions GPUOptions = MakeOptionsForAsset(
            WetClothingAsset,
            EDWCSimulationMode::WetnessMapGPU,
            MaterialSlotIndex);

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
            TArray<FString> MissingGpuParameters;
            AppendMissingGpuRuntimeMaterialParameters(
                GPUMaterialInstance,
                GPUOptions.bUseSurfaceWater,
                MissingGpuParameters);
            const FString MissingParameterText = MissingGpuParameters.IsEmpty()
                                                     ? FString()
                                                     : FString::Printf(
                                                           TEXT(" Missing runtime parameters: %s."),
                                                           *FString::Join(MissingGpuParameters, TEXT(", ")));
            OutMessages.Add(FString::Printf(
                TEXT("Slot %d: generated GPU material '%s' is missing DWC GPU material setup or wetness-map parameters.%s Use Generate Materials."),
                MaterialSlotIndex,
                *GetNameSafe(GPUMaterialInstance),
                *MissingParameterText));
        }
    }
}

bool FWCAMaterialGenerator::ValidateSurfaceAppearanceFunctions(FString& OutErrorMessage)
{
    OutErrorMessage.Reset();
    TArray<FString> FailureReasons;
    if (!ValidateDwcMaterialFunctionSet(FailureReasons))
    {
        OutErrorMessage = FString::Join(FailureReasons, TEXT("\n"));
        return false;
    }
    return true;
}
