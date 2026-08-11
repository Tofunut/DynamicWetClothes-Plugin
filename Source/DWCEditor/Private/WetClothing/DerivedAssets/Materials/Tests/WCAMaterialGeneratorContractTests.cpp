// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"

#include "DataAssets/WetClothingAsset.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "WetRendering/WetMaterialParameters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAMaterialGeneratorMetadataContractTest,
    "DWC.Editor.Materials.GeneratedMetadataContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAMaterialGeneratorMetadataContractTest::RunTest(const FString&)
{
    UMaterial* SourceMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    SourceMaterial->StateId = FGuid(1, 2, 3, 4);

    const FString FirstSourceSignature = FWCAMaterialGenerator::BuildSourceMaterialSignature(SourceMaterial);
    TestFalse(TEXT("Source signature is generated"), FirstSourceSignature.IsEmpty());
    TestEqual(
        TEXT("Source signature is deterministic"),
        FWCAMaterialGenerator::BuildSourceMaterialSignature(SourceMaterial),
        FirstSourceSignature);

    SourceMaterial->StateId = FGuid(5, 6, 7, 8);
    const FString ChangedSourceSignature = FWCAMaterialGenerator::BuildSourceMaterialSignature(SourceMaterial);
    TestNotEqual(
        TEXT("Editing the source graph at the same object path changes its signature"),
        ChangedSourceSignature,
        FirstSourceSignature);

    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage(), NAME_None, RF_Transient);
    UMaterial* GeneratedMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    UMaterialInstanceConstant* RuntimeInstance =
        NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
    RuntimeInstance->SetParentEditorOnly(GeneratedMaterial);

    FWetClothingUnifiedMaterialSetupResult MaterialSet;
    MaterialSet.bSucceeded = true;
    MaterialSet.GeneratedMaterial = GeneratedMaterial;
    MaterialSet.GeneratedMaterialInstance = RuntimeInstance;

    FString CommitError;
    TestTrue(
        TEXT("Complete generated set commits metadata"),
        FWCAMaterialGenerator::CommitGeneratedMaterialOverride(
            Asset,
            0,
            SourceMaterial,
            MaterialSet,
            &CommitError));
    TestTrue(TEXT("Commit has no error"), CommitError.IsEmpty());
    TestEqual(TEXT("One override is stored"), Asset->Derived.Inline.GeneratedWetMaterialOverrides.Num(), 1);
    if (Asset->Derived.Inline.GeneratedWetMaterialOverrides.Num() == 1)
    {
        const FWetClothingGeneratedWetMaterialOverride& Override =
            Asset->Derived.Inline.GeneratedWetMaterialOverrides[0];
        TestEqual(
            TEXT("Current generator version is recorded"),
            Override.GeneratorVersion,
            FWCAMaterialGenerator::GeneratedMaterialGeneratorVersion);
        TestFalse(TEXT("Source signature is recorded"), Override.SourceMaterialSignature.IsEmpty());
        TestFalse(TEXT("Generation signature is recorded"), Override.GenerationSignature.IsEmpty());
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWCAMaterialGeneratorWetnessGraphContractTest,
    "DWC.Editor.Materials.WetnessGraphContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWCAMaterialGeneratorWetnessGraphContractTest::RunTest(const FString&)
{
    UMaterial* SourceMaterial = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
    FWCAMaterialGenerator::FOptions Options;
    Options.DWCDataUVChannelIndex = 0;
    Options.OriginalUVChannelIndex = 0;
    Options.SurfaceWaterNormalUVChannelIndex = 0;

    const FWetClothingUnifiedMaterialSetupResult MaterialSet =
        FWCAMaterialGenerator::CreateTransientUnifiedPreviewMaterial(SourceMaterial, Options);
    if (!TestTrue(TEXT("Transient v7 graph is generated"), MaterialSet.bSucceeded))
    {
        AddError(MaterialSet.Message);
        return false;
    }
    TestTrue(
        TEXT("Generated graph satisfies the CPU/GPU wetness contract"),
        FWCAMaterialGenerator::IsMaterialConfiguredForDwc(
            MaterialSet.GeneratedMaterialInstance,
            Options));

    UMaterialExpressionIf* Selector = nullptr;
    UMaterialExpressionVertexColor* VertexColor = nullptr;
    for (UMaterialExpression* Expression : MaterialSet.GeneratedMaterial->GetExpressions())
    {
        if (UMaterialExpressionVertexColor* CandidateVertexColor = Cast<UMaterialExpressionVertexColor>(Expression))
        {
            VertexColor = CandidateVertexColor;
        }
        if (UMaterialExpressionIf* CandidateSelector = Cast<UMaterialExpressionIf>(Expression))
        {
            const FExpressionInput Control = CandidateSelector->A.GetTracedInput();
            const UMaterialExpressionScalarParameter* Parameter =
                Cast<UMaterialExpressionScalarParameter>(Control.Expression);
            if (Parameter != nullptr && Parameter->ParameterName == DWCWetMaterialParameters::UseGPUBackend())
            {
                Selector = CandidateSelector;
            }
        }
    }
    if (!TestNotNull(TEXT("Generated graph contains the backend selector"), Selector) ||
        !TestNotNull(TEXT("Generated graph contains VertexColor"), VertexColor))
    {
        return false;
    }

    int32 AlphaOutputIndex = INDEX_NONE;
    const TArray<FExpressionOutput>& VertexOutputs = VertexColor->GetOutputs();
    for (int32 OutputIndex = 0; OutputIndex < VertexOutputs.Num(); ++OutputIndex)
    {
        const FExpressionOutput& Output = VertexOutputs[OutputIndex];
        if (Output.OutputName == TEXT("A") ||
            (Output.OutputName.IsNone() &&
             !Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA))
        {
            AlphaOutputIndex = OutputIndex;
            break;
        }
    }
    if (!TestTrue(TEXT("VertexColor exposes an Alpha output"), AlphaOutputIndex != INDEX_NONE))
    {
        return false;
    }

    Selector->ALessThanB.Connect(AlphaOutputIndex, VertexColor);
    Selector->AEqualsB.Connect(AlphaOutputIndex, VertexColor);
    TestFalse(
        TEXT("A stored graph wired to VertexColor.A is rejected"),
        FWCAMaterialGenerator::IsMaterialConfiguredForDwc(
            MaterialSet.GeneratedMaterialInstance,
            Options));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
