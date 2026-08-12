// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/MaterialBake/DWCTransparencyMaterialBakeResolutionResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyMaterialBakeResolutionPolicyTest,
    "DynamicWetClothes.Transparency.MaterialBake.ResolutionPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyMaterialBakeResolutionPolicyTest::RunTest(const FString& Parameters)
{
    TArray<FIntPoint> Dimensions;
    TestEqual(
        TEXT("Procedural source materials use the deterministic 2K fallback."),
        FDWCTransparencyMaterialBakeResolutionResolver::
            ResolveAutomaticResolutionFromDimensions(Dimensions),
        2048);

    Dimensions = {FIntPoint(1024, 512), FIntPoint(2048, 1024), FIntPoint(512, 512)};
    TestEqual(
        TEXT("The largest participating surface-property texture determines the source bake."),
        FDWCTransparencyMaterialBakeResolutionResolver::
            ResolveAutomaticResolutionFromDimensions(Dimensions),
        2048);

    Dimensions.Add(FIntPoint(4096, 2048));
    TestEqual(
        TEXT("A 4K Base Color, Normal, or Metallic dependency retains a 4K source bake."),
        FDWCTransparencyMaterialBakeResolutionResolver::
            ResolveAutomaticResolutionFromDimensions(Dimensions),
        4096);

    Dimensions = {FIntPoint(8192, 8192)};
    TestEqual(
        TEXT("Source material evaluation is bounded at 4K."),
        FDWCTransparencyMaterialBakeResolutionResolver::
            ResolveAutomaticResolutionFromDimensions(Dimensions),
        4096);

    const FDWCTransparencyResolvedMaterialBakeResolution MissingMaterial =
        FDWCTransparencyMaterialBakeResolutionResolver::Resolve(nullptr);
    TestEqual(
        TEXT("A missing effective material resolves to the deterministic fallback."),
        MissingMaterial.Resolution,
        2048);
    TestTrue(
        TEXT("The missing-material decision is marked as fallback."),
        MissingMaterial.bUsedFallback);
    TestFalse(
        TEXT("Every source bake resolution decision has an identity."),
        MissingMaterial.Identity.IsEmpty());
    return true;
}

#endif
