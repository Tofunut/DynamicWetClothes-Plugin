// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Misc/AutomationTest.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/Foundation/MaterialGraph/DWCRevealSurfaceMaterialGraph.h"
#include "WetClothing/Foundation/MaterialGraph/DWCSurfaceGraphBuilder.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialParameters.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialCache.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewGraphExtension.h"
#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialParameters.h"

namespace
{
    template <typename TExpression>
    TExpression* CreateExpression(UMaterial* Material, const int32 X, const int32 Y)
    {
        return Cast<TExpression>(UMaterialEditingLibrary::CreateMaterialExpression(
            Material,
            TExpression::StaticClass(),
            X,
            Y));
    }

    bool HasScalarParameter(const UMaterial& Material, const FName ParameterName)
    {
        for (UMaterialExpression* Expression : Material.GetExpressions())
        {
            const UMaterialExpressionScalarParameter* Parameter =
                Cast<UMaterialExpressionScalarParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return true;
            }
        }
        return false;
    }

    bool HasVectorParameter(const UMaterial& Material, const FName ParameterName)
    {
        for (UMaterialExpression* Expression : Material.GetExpressions())
        {
            const UMaterialExpressionVectorParameter* Parameter =
                Cast<UMaterialExpressionVectorParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return true;
            }
        }
        return false;
    }

    bool HasTextureParameter(const UMaterial& Material, const FName ParameterName)
    {
        for (UMaterialExpression* Expression : Material.GetExpressions())
        {
            const UMaterialExpressionTextureObjectParameter* Parameter =
                Cast<UMaterialExpressionTextureObjectParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return true;
            }
        }
        return false;
    }

    UMaterialExpressionScalarParameter* FindScalarParameter(
        const UMaterial& Material,
        const FName      ParameterName)
    {
        for (UMaterialExpression* Expression : Material.GetExpressions())
        {
            UMaterialExpressionScalarParameter* Parameter =
                Cast<UMaterialExpressionScalarParameter>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }
        return nullptr;
    }

    UMaterialExpressionTextureSampleParameter2D* FindTextureSampleParameter(
        const UMaterial& Material,
        const FName      ParameterName)
    {
        for (UMaterialExpression* Expression : Material.GetExpressions())
        {
            UMaterialExpressionTextureSampleParameter2D* Parameter =
                Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
            if (Parameter != nullptr && Parameter->ParameterName == ParameterName)
            {
                return Parameter;
            }
        }
        return nullptr;
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

    UMaterialExpression* FindFunctionInputExpression(
        const UMaterialExpressionMaterialFunctionCall* FunctionCall,
        const FName                                    InputName)
    {
        if (FunctionCall == nullptr)
        {
            return nullptr;
        }

        for (const FFunctionExpressionInput& FunctionInput : FunctionCall->FunctionInputs)
        {
            if (FunctionInput.ExpressionInput != nullptr &&
                FunctionInput.ExpressionInput->InputName == InputName)
            {
                return FunctionInput.Input.Expression;
            }
        }
        return nullptr;
    }

    int32 FindExpressionOutputIndex(
        UMaterialExpression* Expression,
        const FName          OutputName)
    {
        if (Expression == nullptr)
        {
            return INDEX_NONE;
        }

        const TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
        for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
        {
            const FExpressionOutput& Output = Outputs[OutputIndex];
            const bool bNamedMatch = Output.OutputName == OutputName;
            const bool bMaskMatch = Output.OutputName.IsNone() &&
                ((OutputName == TEXT("R") && Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA) ||
                 (OutputName == TEXT("G") && !Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA) ||
                 (OutputName == TEXT("B") && !Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA) ||
                 (OutputName == TEXT("A") && !Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA));
            if (bNamedMatch || bMaskMatch)
            {
                return OutputIndex;
            }
        }
        return INDEX_NONE;
    }

    FDWCEditorPreviewMaterialRequest MakeTransparencyHoverMaterialRequest(
        UMaterialInterface* SourceMaterial)
    {
        FDWCEditorPreviewMaterialRequest Request;
        Request.SourceMaterial = SourceMaterial;
        Request.SlotOwner = SourceMaterial;
        Request.MaterialSlotIndex = 0;
        Request.DWCDataUVChannelIndex = 0;
        Request.SurfaceWaterNormalUVChannelIndex = 0;
        Request.FeatureMask = EDWCEditorPreviewMaterialFeature::Transparency;
        Request.FeatureSchemaVersion = FWetTransparencyPreviewGraphExtension::GraphSchemaVersion;
        Request.ExtendGraph = &FWetTransparencyPreviewGraphExtension::ExtendGraph;
        return Request;
    }

    class FWaitForTransparencyHoverMaterialReady final : public IAutomationLatentCommand
    {
      public:
        FWaitForTransparencyHoverMaterialReady(
            FAutomationTestBase*                       InTest,
            TSharedRef<FDWCEditorPreviewMaterialCache> InCache,
            UMaterialInterface*                        InSourceMaterial)
            : Test(InTest), Cache(MoveTemp(InCache)), SourceMaterial(InSourceMaterial), DeadlineSeconds(FPlatformTime::Seconds() + 60.0)
        {
        }

        virtual bool Update() override
        {
            if (Test == nullptr || SourceMaterial == nullptr)
            {
                return true;
            }

            const FDWCEditorPreviewMaterialResult Result = Cache->GetOrCreate(
                MakeTransparencyHoverMaterialRequest(SourceMaterial));
            if (Result.State == EDWCEditorPreviewMaterialState::Ready)
            {
                Test->TestTrue(TEXT("The material-driven hover graph compiles successfully"), Result.bSucceeded);
                Test->TestNotNull(TEXT("The compiled hover graph creates a preview MID"), Result.PreviewMID);
                Cache->Reset();
                return true;
            }
            if (Result.State == EDWCEditorPreviewMaterialState::Failed)
            {
                Test->AddError(FString::Printf(
                    TEXT("The material-driven hover graph failed to compile: %s"),
                    *Result.Message));
                Cache->Reset();
                return true;
            }
            if (FPlatformTime::Seconds() >= DeadlineSeconds)
            {
                Test->AddError(TEXT("The material-driven hover graph did not finish compiling within 60 seconds."));
                Cache->Reset();
                return true;
            }
            return false;
        }

      private:
        FAutomationTestBase*                       Test = nullptr;
        TSharedRef<FDWCEditorPreviewMaterialCache> Cache;
        TObjectPtr<UMaterialInterface>             SourceMaterial = nullptr;
        double                                     DeadlineSeconds = 0.0;
    };
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPreviewMaterialHoverGraphTest,
    "DWC.Editor.Transparency.MaterialPreview.HoverGraphContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPreviewMaterialHoverGraphTest::RunTest(const FString&)
{
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    TestNotNull(TEXT("A transient material is available for the hover graph fixture"), Material);
    if (Material == nullptr)
    {
        return false;
    }

    UMaterialExpressionConstant3Vector* BaseColor =
        CreateExpression<UMaterialExpressionConstant3Vector>(Material, -1200, 0);
    UMaterialExpressionConstant3Vector* BaseNormal =
        CreateExpression<UMaterialExpressionConstant3Vector>(Material, -1200, 50);
    UMaterialExpressionTextureCoordinate* DataUV =
        CreateExpression<UMaterialExpressionTextureCoordinate>(Material, -1200, 100);
    UMaterialExpressionScalarParameter* PreviewWetness =
        CreateExpression<UMaterialExpressionScalarParameter>(Material, -1200, 200);
    TestNotNull(TEXT("The fixture creates a Base Color expression"), BaseColor);
    TestNotNull(TEXT("The fixture creates a Base Normal expression"), BaseNormal);
    TestNotNull(TEXT("The fixture creates a Data UV expression"), DataUV);
    TestNotNull(TEXT("The fixture creates a Preview Wetness expression"), PreviewWetness);
    if (BaseColor == nullptr || BaseNormal == nullptr || DataUV == nullptr || PreviewWetness == nullptr)
    {
        return false;
    }
    PreviewWetness->ParameterName = DWCEditorPreviewMaterialParameters::PreviewWetness();
    PreviewWetness->DefaultValue = 1.0f;

    FDWCSurfaceGraphBuildResult SurfaceGraph;
    SurfaceGraph.Outputs.BaseColor = { BaseColor, FString() };
    SurfaceGraph.Outputs.Normal = { BaseNormal, FString() };
    SurfaceGraph.DWCDataUVExpression = DataUV;

    FString ErrorMessage;
    TestTrue(
        TEXT("The Transparency preview extension accepts the hover graph contract"),
        FWetTransparencyPreviewGraphExtension::ExtendGraph(Material, SurfaceGraph, ErrorMessage));
    TestTrue(TEXT("The hover graph reports no construction error"), ErrorMessage.IsEmpty());

    TestTrue(TEXT("Hover state 0 is exposed as a vector parameter"),
             HasVectorParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverState0()));
    TestTrue(TEXT("Hover state 1 is exposed as a vector parameter"),
             HasVectorParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverState1()));
    TestTrue(TEXT("Hover color is exposed as a vector parameter"),
             HasVectorParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverColor()));
    TestTrue(TEXT("Hover target is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverTarget()));
    TestTrue(TEXT("Hover wrapping is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverWrap()));
    TestTrue(TEXT("Hover texel size is exposed as a vector parameter"),
             HasVectorParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverTexelSize()));
    TestTrue(TEXT("Hover visualization mode is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverVisualizationMode()));
    TestTrue(TEXT("Hover baseline is exposed as a texture parameter"),
             HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverBaselineMap()));
    TestTrue(TEXT("Hover baseline use is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseHoverBaselineMap()));
    TestTrue(TEXT("Hover island identity is exposed as a texture parameter"),
             HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverIslandIDMap()));
    TestTrue(TEXT("Hover island identity use is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseHoverIslandIDMap()));
    TestTrue(TEXT("The active hover island is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverIslandID()));
    TestTrue(TEXT("Hover edge feather is exposed as a texture parameter"),
             HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap()));
    TestTrue(TEXT("Hover edge feather use is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap()));
    TestTrue(TEXT("Wrinkle coverage is exposed as a texture parameter"),
             HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::WrinkleCoverageMap()));
    TestTrue(TEXT("Wrinkle coverage use is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseWrinkleCoverageMap()));
    TestTrue(TEXT("Wrinkle threshold is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::WrinkleMaskThreshold()));
    TestTrue(TEXT("Wrinkle softness is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::WrinkleMaskSoftness()));
    TestTrue(TEXT("Transparency visualization mode is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::VisualizationMode()));
    TestTrue(TEXT("Reveal Surface map is exposed as a texture parameter"),
             HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::RevealSurfaceMap()));
    TestTrue(TEXT("Reveal Surface enable is exposed as a scalar parameter"),
             HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseRevealSurfaceMap()));

    const UMaterialExpressionCustom* PreviewState = nullptr;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        const UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression);
        if (Candidate != nullptr && Candidate->Description.Contains(TEXT("Transparency Live Preview")))
        {
            PreviewState = Candidate;
            break;
        }
    }
    TestNotNull(TEXT("The graph owns the Transparency preview state expression"), PreviewState);
    if (PreviewState != nullptr)
    {
        TestTrue(TEXT("Disabled hover has an explicit fast path"),
                 PreviewState->Code.Contains(TEXT("HoverState1.x > 0.0")));
        TestTrue(TEXT("Reveal and alpha hover use separate targets"),
                 PreviewState->Code.Contains(TEXT("HoverTarget < 1.5")));
        TestTrue(TEXT("Smooth hover samples only inside the active branch"),
                 PreviewState->Code.Contains(TEXT("SelectedHoverOperation == 3")));
        TestTrue(TEXT("Hover is clipped to the active UV island"),
                 PreviewState->Code.Contains(TEXT("HoverIslandEligibility")));
        TestTrue(TEXT("Hover island identity is sampled directly in the material"),
                 PreviewState->Code.Contains(TEXT("HoverIslandIDMapTex.Load")));
        TestTrue(TEXT("Hover island selection is parameter-driven"),
                 PreviewState->Code.Contains(TEXT("SampledHoverIslandID - HoverIslandID")));
        TestTrue(TEXT("Auto-alpha hover updates its grayscale visualization"),
                 PreviewState->Code.Contains(TEXT("TransparencySample.rgb = TransparencySample.aaa")));
        TestTrue(TEXT("Wrinkle coverage is sampled directly by the preview material"),
                 PreviewState->Code.Contains(TEXT("WrinkleCoverageMapTex")));
        TestTrue(TEXT("Wrinkle threshold and softness are evaluated by the preview material"),
                 PreviewState->Code.Contains(TEXT("smoothstep(SafeThreshold, TransitionEnd, Coverage)")));
        TestTrue(TEXT("Wrinkle suppression is applied to final alpha in the preview material"),
                 PreviewState->Code.Contains(TEXT("(1.0 - SuppressionWeight)")));
        TestTrue(TEXT("Reveal Surface visibility follows final transparency alpha"),
                 PreviewState->Code.Contains(TEXT("FinalRevealVisibility = FinalAlpha")));
        bool bConsumesBaseColor = false;
        for (const FCustomInput& Input : PreviewState->Inputs)
        {
            bConsumesBaseColor |= Input.InputName == TEXT("BaseColor");
        }
        TestFalse(TEXT("Preview state never consumes source Base Color"), bConsumesBaseColor);
    }

    const UMaterialExpressionCustom* RevealSurfaceNormal = nullptr;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        const UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression);
        if (Candidate != nullptr && Candidate->Description.Contains(TEXT("Reveal Surface Preview Normal")))
        {
            RevealSurfaceNormal = Candidate;
            break;
        }
    }
    TestNotNull(TEXT("The graph owns the isolated Reveal Surface normal expression"), RevealSurfaceNormal);
    if (RevealSurfaceNormal != nullptr)
    {
        TestTrue(TEXT("Reveal Surface decodes its packed normal channels"),
                 RevealSurfaceNormal->Code.Contains(TEXT("float2 RevealXY")));
        TestTrue(TEXT("Editor Reveal Surface consumes packed source coverage"),
                 RevealSurfaceNormal->Code.Contains(TEXT("RevealSample.a")));
        TestTrue(TEXT("Reveal Surface returns the composed normal"),
                 RevealSurfaceNormal->Code.Contains(TEXT("return normalize")));
        TestFalse(TEXT("Reveal Surface does not apply runtime metallic darkening"),
                  RevealSurfaceNormal->Code.Contains(TEXT("MetallicDarkening")));
        bool bConsumesBaseColor = false;
        for (const FCustomInput& Input : RevealSurfaceNormal->Inputs)
        {
            bConsumesBaseColor |= Input.InputName == TEXT("BaseColor");
        }
        TestFalse(TEXT("Reveal Normal never consumes source Base Color"), bConsumesBaseColor);
    }

    const UMaterialExpressionLinearInterpolate* ColorCompose = Cast<UMaterialExpressionLinearInterpolate>(
        UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_BaseColor));
    TestNotNull(TEXT("Base Color is composed through an isolated Lerp"), ColorCompose);

    TestEqual(TEXT("The material hover target enum has a stable disabled value"),
              static_cast<uint8>(EDWCTransparencyMaterialHoverTarget::None), static_cast<uint8>(0));
    TestEqual(TEXT("The material hover operation enum has a stable smooth value"),
              static_cast<uint8>(EDWCTransparencyMaterialHoverOperation::Smooth), static_cast<uint8>(3));
    TestTrue(TEXT("The feature schema invalidates graphs built before separated Reveal preview paths"),
             FWetTransparencyPreviewGraphExtension::GraphSchemaVersion >= 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRuntimeRevealNormalGraphContractTest,
    "DWC.Editor.Transparency.MaterialPreview.RuntimeRevealNormalGraphContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRuntimeRevealNormalGraphContractTest::RunTest(const FString&)
{
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    TestNotNull(TEXT("A transient material is available for the runtime Reveal Normal fixture"), Material);
    if (Material == nullptr)
    {
        return false;
    }

    UMaterialExpressionScalarParameter* Wetness =
        CreateExpression<UMaterialExpressionScalarParameter>(Material, -2800, -600);
    if (Wetness == nullptr)
    {
        AddError(TEXT("Could not create the runtime Reveal Normal wetness fixture input."));
        return false;
    }
    Wetness->ParameterName = DWCEditorPreviewMaterialParameters::PreviewWetness();
    Wetness->DefaultValue = 1.0f;

    FDWCSurfaceGraphBuildRequest Request;
    Request.Material = Material;
    Request.DWCDataUVChannelIndex = 0;
    Request.SurfaceWaterNormalUVChannelIndex = 0;
    Request.WetnessInput = { Wetness, FString() };

    const FDWCSurfaceGraphBuildResult Result = FDWCSurfaceGraphBuilder::Build(Request);
    TestTrue(TEXT("The common DWC surface graph builds successfully"), Result.bSucceeded);
    TestTrue(TEXT("The common DWC surface graph reports no failure"), Result.FailureReasons.IsEmpty());
    TestNotNull(TEXT("The common graph owns MF_DWC_EvaluateSurfaceAppearance"), Result.EvaluateExpression);
    if (!Result.bSucceeded || Result.EvaluateExpression == nullptr)
    {
        return false;
    }

    UMaterialExpressionTextureSampleParameter2D* RevealNormal = FindTextureSampleParameter(
        *Material,
        DWCWetMaterialParameters::RevealNormalMap());
    TestNotNull(TEXT("The common graph owns the Reveal Normal texture sample"), RevealNormal);
    if (RevealNormal != nullptr)
    {
        TestEqual(
            TEXT("Runtime Reveal Normal uses a normal sampler"),
            RevealNormal->SamplerType,
            SAMPLERTYPE_Normal);
    }

    TestTrue(
        TEXT("Reveal Normal is connected to MF_DWC_EvaluateSurfaceAppearance"),
        IsFunctionInputConnected(Result.EvaluateExpression, TEXT("RevealNormal")));
    TestTrue(
        TEXT("Reveal Normal enable is connected to MF_DWC_EvaluateSurfaceAppearance"),
        IsFunctionInputConnected(Result.EvaluateExpression, TEXT("UseRevealNormalMap")));
    TestTrue(
        TEXT("Reveal Normal strength is connected to MF_DWC_EvaluateSurfaceAppearance"),
        IsFunctionInputConnected(Result.EvaluateExpression, TEXT("RevealNormalStrength")));
    TestNotNull(
        TEXT("The common graph owns the Reveal Normal strength parameter"),
        FindScalarParameter(*Material, DWCWetMaterialParameters::RevealNormalStrength()));
    TestTrue(
        TEXT("The final runtime Normal comes directly from MF_DWC_EvaluateSurfaceAppearance"),
        Result.Outputs.Normal.Expression == Result.EvaluateExpression &&
            Result.Outputs.Normal.OutputName == TEXT("Normal"));

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
        TestFalse(
            TEXT("The runtime graph no longer owns an external Reveal Normal composite"),
            Custom != nullptr &&
                Custom->Description.Contains(TEXT("Runtime Coverage-Weighted Normal")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCUnifiedMaterialRevealGraphContractTest,
    "DWC.Editor.Transparency.MaterialPreview.UnifiedMaterialRevealGraphContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCUnifiedMaterialRevealGraphContractTest::RunTest(const FString&)
{
    UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    TestNotNull(TEXT("The default material is available for the unified graph fixture"), SourceMaterial);
    if (SourceMaterial == nullptr)
    {
        return false;
    }

    FWCAMaterialGenerator::FOptions Options;
    Options.DWCDataUVChannelIndex = 0;
    Options.OriginalUVChannelIndex = 0;
    Options.SurfaceWaterNormalUVChannelIndex = 0;

    const FWetClothingUnifiedMaterialSetupResult Result =
        FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(SourceMaterial, Options);
    TestTrue(TEXT("The unified DWC preview graph is generated successfully"), Result.bSucceeded);
    TestTrue(TEXT("The unified DWC preview graph reports no failure"), Result.Message.IsEmpty() || Result.bSucceeded);
    TestNotNull(TEXT("The unified graph owns a transient material"), Result.GeneratedMaterial);
    if (!Result.bSucceeded || Result.GeneratedMaterial == nullptr)
    {
        AddError(Result.Message);
        return false;
    }

    const UMaterialExpressionCustom* PreviewDebug = nullptr;
    for (UMaterialExpression* Expression : Result.GeneratedMaterial->GetExpressions())
    {
        const UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression);
        if (Candidate != nullptr &&
            Candidate->Description == TEXT("DWC Wetness Profile Preview Debug BaseColor"))
        {
            PreviewDebug = Candidate;
            break;
        }
    }
    TestNotNull(TEXT("The unified graph owns its preview debug expression"), PreviewDebug);
    if (PreviewDebug != nullptr)
    {
        for (const FCustomInput& Input : PreviewDebug->Inputs)
        {
            TestNotNull(
                *FString::Printf(TEXT("Preview debug input '%s' is connected"), *Input.InputName.ToString()),
                Input.Input.Expression);
        }
    }

    const UMaterialExpressionMaterialFunctionCall* FinalNormal =
        Cast<UMaterialExpressionMaterialFunctionCall>(
            UMaterialEditingLibrary::GetMaterialPropertyInputNode(
                Result.GeneratedMaterial,
                MP_Normal));
    TestNotNull(TEXT("The unified graph takes its final Normal from a material function"), FinalNormal);
    TestEqual(
        TEXT("The unified graph takes the named Normal output from MF_DWC_EvaluateSurfaceAppearance"),
        UMaterialEditingLibrary::GetMaterialPropertyInputNodeOutputName(
            Result.GeneratedMaterial,
            MP_Normal),
        FString(TEXT("Normal")));

    UMaterialExpressionScalarParameter* UseGPUBackend = FindScalarParameter(
        *Result.GeneratedMaterial,
        DWCWetMaterialParameters::UseGPUBackend());
    UMaterialExpressionTextureSampleParameter2D* WetnessMap = FindTextureSampleParameter(
        *Result.GeneratedMaterial,
        DWCWetMaterialParameters::WetnessMap());
    UMaterialExpressionVertexColor* VertexColor = nullptr;
    UMaterialExpressionIf* WetnessSelector = nullptr;
    for (UMaterialExpression* Expression : Result.GeneratedMaterial->GetExpressions())
    {
        if (VertexColor == nullptr)
        {
            VertexColor = Cast<UMaterialExpressionVertexColor>(Expression);
        }
        if (UMaterialExpressionIf* Candidate = Cast<UMaterialExpressionIf>(Expression))
        {
            if (Candidate->A.Expression == UseGPUBackend)
            {
                WetnessSelector = Candidate;
            }
        }
    }

    TestNotNull(TEXT("The unified graph owns the GPU backend selector parameter"), UseGPUBackend);
    TestNotNull(TEXT("The unified graph owns the GPU wetness map"), WetnessMap);
    TestNotNull(TEXT("The unified graph owns a runtime Vertex Color expression"), VertexColor);
    TestNotNull(TEXT("The unified graph owns a CPU/GPU wetness selector"), WetnessSelector);
    if (WetnessSelector != nullptr && WetnessMap != nullptr && VertexColor != nullptr)
    {
        const int32 WetnessMapR = FindExpressionOutputIndex(WetnessMap, TEXT("R"));
        const int32 VertexColorR = FindExpressionOutputIndex(VertexColor, TEXT("R"));
        TestTrue(TEXT("The GPU wetness map exposes an R output"), WetnessMapR != INDEX_NONE);
        TestTrue(TEXT("Vertex Color exposes an R output"), VertexColorR != INDEX_NONE);
        TestTrue(
            TEXT("The GPU selector branch reads DWC_WetnessMap.R"),
            WetnessSelector->AGreaterThanB.Expression == WetnessMap &&
                WetnessSelector->AGreaterThanB.OutputIndex == WetnessMapR);
        TestTrue(
            TEXT("The CPU selector branch reads VertexColor.R"),
            WetnessSelector->ALessThanB.Expression == VertexColor &&
                WetnessSelector->ALessThanB.OutputIndex == VertexColorR);
        TestTrue(
            TEXT("The selector equality branch also reads VertexColor.R"),
            WetnessSelector->AEqualsB.Expression == VertexColor &&
                WetnessSelector->AEqualsB.OutputIndex == VertexColorR);
    }

    TestTrue(
        TEXT("The selected CPU/GPU wetness drives MF_DWC_EvaluateSurfaceAppearance"),
        FindFunctionInputExpression(FinalNormal, TEXT("Wetness")) == WetnessSelector);
    TestTrue(
        TEXT("Reveal Normal remains inside MF_DWC_EvaluateSurfaceAppearance"),
        IsFunctionInputConnected(FinalNormal, TEXT("RevealNormal")) &&
            IsFunctionInputConnected(FinalNormal, TEXT("UseRevealNormalMap")));
    TestTrue(
        TEXT("Transparency remains inside MF_DWC_EvaluateSurfaceAppearance"),
        IsFunctionInputConnected(FinalNormal, TEXT("TransparencyColor")) &&
            IsFunctionInputConnected(FinalNormal, TEXT("TransparencyAlpha")) &&
            IsFunctionInputConnected(FinalNormal, TEXT("UseTransparencyMap")));

    if (Result.GeneratedMaterialInstance != nullptr)
    {
        Result.GeneratedMaterialInstance->MarkAsGarbage();
    }
    Result.GeneratedMaterial->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPreviewMaterialHoverCompileTest,
    "DWC.Editor.Transparency.MaterialPreview.HoverGraphCompile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
        EAutomationTestFlags::NonNullRHI)

bool FDWCTransparencyPreviewMaterialHoverCompileTest::RunTest(const FString&)
{
    UMaterialInterface* SourceMaterial = LoadObject<UMaterialInterface>(
        nullptr,
        TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
    TestNotNull(TEXT("The engine default material is available for the hover compile fixture"), SourceMaterial);
    if (SourceMaterial == nullptr)
    {
        return false;
    }

    TSharedRef<FDWCEditorPreviewMaterialCache> Cache = MakeShared<FDWCEditorPreviewMaterialCache>();
    const FDWCEditorPreviewMaterialResult      Initial = Cache->GetOrCreate(
        MakeTransparencyHoverMaterialRequest(SourceMaterial));
    const bool bEnteredCompileLifecycle = Initial.State == EDWCEditorPreviewMaterialState::Compiling ||
                                          Initial.State == EDWCEditorPreviewMaterialState::Ready;
    TestTrue(TEXT("The hover graph enters the material compile lifecycle"), bEnteredCompileLifecycle);
    if (!bEnteredCompileLifecycle)
    {
        AddError(FString::Printf(TEXT("Hover graph construction failed: %s"), *Initial.Message));
        Cache->Reset();
        return false;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FWaitForTransparencyHoverMaterialReady(this, Cache, SourceMaterial));
    return true;
}

#endif
