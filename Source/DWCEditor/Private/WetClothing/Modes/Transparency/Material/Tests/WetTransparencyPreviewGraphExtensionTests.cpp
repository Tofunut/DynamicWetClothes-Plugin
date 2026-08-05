#if WITH_DEV_AUTOMATION_TESTS

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/AutomationTest.h"
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
            FAutomationTestBase* InTest,
            TSharedRef<FDWCEditorPreviewMaterialCache> InCache,
            UMaterialInterface* InSourceMaterial)
            : Test(InTest)
            , Cache(MoveTemp(InCache))
            , SourceMaterial(InSourceMaterial)
            , DeadlineSeconds(FPlatformTime::Seconds() + 60.0)
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
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FDWCEditorPreviewMaterialCache> Cache;
        TObjectPtr<UMaterialInterface> SourceMaterial = nullptr;
        double DeadlineSeconds = 0.0;
    };
}

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
    UMaterialExpressionTextureCoordinate* DataUV =
        CreateExpression<UMaterialExpressionTextureCoordinate>(Material, -1200, 100);
    UMaterialExpressionScalarParameter* PreviewWetness =
        CreateExpression<UMaterialExpressionScalarParameter>(Material, -1200, 200);
    TestNotNull(TEXT("The fixture creates a Base Color expression"), BaseColor);
    TestNotNull(TEXT("The fixture creates a Data UV expression"), DataUV);
    TestNotNull(TEXT("The fixture creates a Preview Wetness expression"), PreviewWetness);
    if (BaseColor == nullptr || DataUV == nullptr || PreviewWetness == nullptr)
    {
        return false;
    }
    PreviewWetness->ParameterName = DWCEditorPreviewMaterialParameters::PreviewWetness();
    PreviewWetness->DefaultValue = 1.0f;

    FDWCSurfaceGraphBuildResult SurfaceGraph;
    SurfaceGraph.Outputs.BaseColor = {BaseColor, FString()};
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
    TestTrue(TEXT("Hover edge feather is exposed as a texture parameter"),
        HasTextureParameter(*Material, DWCTransparencyPreviewMaterialParameters::HoverEdgeFeatherMap()));
    TestTrue(TEXT("Hover edge feather use is exposed as a scalar parameter"),
        HasScalarParameter(*Material, DWCTransparencyPreviewMaterialParameters::UseHoverEdgeFeatherMap()));

    const UMaterialExpressionCustom* HoverBlend = nullptr;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        const UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression);
        if (Candidate != nullptr && Candidate->Description.Contains(TEXT("Transparency Live Preview")))
        {
            HoverBlend = Candidate;
            break;
        }
    }
    TestNotNull(TEXT("The graph owns the Transparency custom blend"), HoverBlend);
    if (HoverBlend != nullptr)
    {
        TestTrue(TEXT("Disabled hover has an explicit fast path"),
            HoverBlend->Code.Contains(TEXT("HoverState1.x > 0.0")));
        TestTrue(TEXT("Reveal and alpha hover use separate targets"),
            HoverBlend->Code.Contains(TEXT("HoverTarget < 1.5")));
        TestTrue(TEXT("Smooth hover samples only inside the active branch"),
            HoverBlend->Code.Contains(TEXT("SelectedHoverOperation == 3")));
        TestTrue(TEXT("Hover is clipped to the active UV island"),
            HoverBlend->Code.Contains(TEXT("HoverIslandEligibility")));
        TestTrue(TEXT("Auto-alpha hover updates its grayscale visualization"),
            HoverBlend->Code.Contains(TEXT("TransparencySample.rgb = TransparencySample.aaa")));
    }

    TestEqual(TEXT("The material hover target enum has a stable disabled value"),
        static_cast<uint8>(EDWCTransparencyMaterialHoverTarget::None), static_cast<uint8>(0));
    TestEqual(TEXT("The material hover operation enum has a stable smooth value"),
        static_cast<uint8>(EDWCTransparencyMaterialHoverOperation::Smooth), static_cast<uint8>(3));
    TestTrue(TEXT("The feature schema invalidates pre-hover cached graphs"),
        FWetTransparencyPreviewGraphExtension::GraphSchemaVersion >= 4);
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
    const FDWCEditorPreviewMaterialResult Initial = Cache->GetOrCreate(
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
