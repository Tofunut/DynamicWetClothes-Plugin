// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewMaterialBinding.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewMaterialBindingCacheTest,
    "DWC.Editor.Preview.MaterialBinding.CacheDecision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewMaterialBindingCacheTest::RunTest(const FString& Parameters)
{
    UMaterialInterface* SourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
    UMaterialInstanceDynamic* PreviewMaterial =
        UMaterialInstanceDynamic::Create(SourceMaterial, GetTransientPackage());
    USkeletalMesh* FirstMesh = NewObject<USkeletalMesh>(
        GetTransientPackage(),
        TEXT("DWCPreviewMaterialBindingFirstMesh"));
    USkeletalMesh* SecondMesh = NewObject<USkeletalMesh>(
        GetTransientPackage(),
        TEXT("DWCPreviewMaterialBindingSecondMesh"));

    TestNotNull(TEXT("A source material is available"), SourceMaterial);
    TestNotNull(TEXT("A transient preview material is created"), PreviewMaterial);
    TestNotNull(TEXT("First transient mesh is created"), FirstMesh);
    TestNotNull(TEXT("Second transient mesh is created"), SecondMesh);
    TestNotEqual(TEXT("The transient mesh identities differ"), FirstMesh, SecondMesh);
    if (SourceMaterial == nullptr || PreviewMaterial == nullptr || FirstMesh == nullptr || SecondMesh == nullptr)
    {
        return false;
    }

    FDWCEditorPreviewMaterialBindingCache Cache;
    const FDWCEditorPreviewMaterialBindingDecision InitialDecision =
        Cache.Evaluate(FirstMesh, 2, 0, SourceMaterial, SourceMaterial);
    TestFalse(TEXT("A matching source material does not need assignment"), InitialDecision.bNeedsAssignment);
    TestFalse(TEXT("First observation is not a cache hit"), InitialDecision.bCacheMatched);

    const FDWCEditorPreviewMaterialBindingDecision RepeatedDecision =
        Cache.Evaluate(FirstMesh, 2, 0, SourceMaterial, SourceMaterial);
    TestFalse(TEXT("The identical binding remains a no-op"), RepeatedDecision.bNeedsAssignment);
    TestTrue(TEXT("The repeated binding hits the cache"), RepeatedDecision.bCacheMatched);

    const FDWCEditorPreviewMaterialBindingDecision PreviewDecision =
        Cache.Evaluate(FirstMesh, 2, 0, PreviewMaterial, SourceMaterial);
    TestTrue(TEXT("Changing to a preview MID needs one assignment"), PreviewDecision.bNeedsAssignment);
    Cache.RecordApplied(FirstMesh, 2, 0, PreviewMaterial);

    const FDWCEditorPreviewMaterialBindingDecision PreviewRepeatedDecision =
        Cache.Evaluate(FirstMesh, 2, 0, PreviewMaterial, PreviewMaterial);
    TestFalse(TEXT("A retained preview MID does not need reassignment"), PreviewRepeatedDecision.bNeedsAssignment);
    TestTrue(TEXT("The retained preview MID hits the cache"), PreviewRepeatedDecision.bCacheMatched);

    const FDWCEditorPreviewMaterialBindingDecision ExternalOverrideDecision =
        Cache.Evaluate(FirstMesh, 2, 0, PreviewMaterial, SourceMaterial);
    TestTrue(TEXT("An external material override is detected despite a cache hit"),
             ExternalOverrideDecision.bNeedsAssignment);
    TestTrue(TEXT("The cache still records the expected preview MID"),
             ExternalOverrideDecision.bCacheMatched);

    const FDWCEditorPreviewMaterialBindingDecision MeshChangedDecision =
        Cache.Evaluate(SecondMesh, 2, 0, PreviewMaterial, PreviewMaterial);
    TestFalse(TEXT("A matching binding on the new mesh does not need assignment"),
              MeshChangedDecision.bNeedsAssignment);
    TestFalse(TEXT("Changing the mesh invalidates the prior cache entry"),
              MeshChangedDecision.bCacheMatched);
    return true;
}

#endif
