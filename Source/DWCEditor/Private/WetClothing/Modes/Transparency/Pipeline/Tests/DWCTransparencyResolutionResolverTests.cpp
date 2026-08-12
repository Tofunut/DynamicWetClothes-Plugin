// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyResolutionPolicyTest,
    "DynamicWetClothes.Transparency.Resolution.Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyResolutionPolicyTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("The minimum supported resolution is retained."),
        FDWCTransparencyResolutionResolver::NormalizeResolution(128), 256);
    TestEqual(TEXT("Unsupported values round up to avoid losing source detail."),
        FDWCTransparencyResolutionResolver::NormalizeResolution(513), 1024);
    TestEqual(TEXT("The maximum is bounded at 4096."),
        FDWCTransparencyResolutionResolver::NormalizeResolution(8192), 4096);

    TArray<FIntPoint> Dimensions;
    TestEqual(TEXT("Procedural materials use the deterministic fallback."),
        FDWCTransparencyResolutionResolver::ResolveAutomaticResolutionFromDimensions(Dimensions),
        2048);
    Dimensions = {FIntPoint(1024, 512), FIntPoint(512, 512)};
    TestEqual(TEXT("The largest Base Color axis determines a square output."),
        FDWCTransparencyResolutionResolver::ResolveAutomaticResolutionFromDimensions(Dimensions),
        1024);
    Dimensions.Add(FIntPoint(2048, 4096));
    TestEqual(TEXT("A 4K participating texture resolves a 4K output."),
        FDWCTransparencyResolutionResolver::ResolveAutomaticResolutionFromDimensions(Dimensions),
        4096);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyResolutionContractTest,
    "DynamicWetClothes.Transparency.Resolution.LayerContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyResolutionContractTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FWetClothingTransparencyLayerData Layer;
    Layer.TargetSurface.OuterMaterialSlotIndex = 3;
    Layer.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Override;
    Layer.OutputResolutionOverride = 1500;

    const FDWCTransparencyResolvedOutputResolution OverrideResult =
        FDWCTransparencyResolutionResolver::Resolve(*Asset, Layer);
    TestEqual(TEXT("Override mode is normalized through the common policy."),
        OverrideResult.Size, 2048);
    TestFalse(TEXT("Override mode is not reported as a fallback."),
        OverrideResult.bUsedFallback);

    Layer.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Auto;
    const FDWCTransparencyResolvedOutputResolution AutoResult =
        FDWCTransparencyResolutionResolver::Resolve(*Asset, Layer);
    TestEqual(TEXT("A missing target material uses the automatic fallback."),
        AutoResult.Size, 2048);
    TestTrue(TEXT("The missing target material is reported as a fallback."),
        AutoResult.bUsedFallback);
    TestFalse(TEXT("Every resolution decision has a signature identity."),
        AutoResult.Identity.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyPerSlotResolutionIsolationTest,
    "DynamicWetClothes.Transparency.Resolution.PerSlotIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyPerSlotResolutionIsolationTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FWetClothingTransparencyLayerData LayerA;
    LayerA.LayerGuid = FGuid::NewGuid();
    LayerA.TargetSurface.OuterMaterialSlotIndex = 2;
    LayerA.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Override;
    LayerA.OutputResolutionOverride = 1024;

    FWetClothingTransparencyLayerData LayerB;
    LayerB.LayerGuid = FGuid::NewGuid();
    LayerB.TargetSurface.OuterMaterialSlotIndex = 7;
    LayerB.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Override;
    LayerB.OutputResolutionOverride = 4096;

    const FDWCTransparencyResolvedOutputResolution ResolutionA =
        FDWCTransparencyResolutionResolver::Resolve(*Asset, LayerA);
    const FDWCTransparencyResolvedOutputResolution ResolutionB =
        FDWCTransparencyResolutionResolver::Resolve(*Asset, LayerB);
    TestEqual(TEXT("Slot A retains its 1K output extent."),
        ResolutionA.GetExtent(), FIntPoint(1024, 1024));
    TestEqual(TEXT("Slot B retains its 4K output extent."),
        ResolutionB.GetExtent(), FIntPoint(4096, 4096));
    TestNotEqual(TEXT("Different resolved extents cannot share a resolution identity."),
        ResolutionA.Identity, ResolutionB.Identity);

    LayerA.OutputResolutionOverride = 2048;
    const FDWCTransparencyResolvedOutputResolution ChangedResolutionA =
        FDWCTransparencyResolutionResolver::Resolve(*Asset, LayerA);
    TestNotEqual(TEXT("Changing slot A invalidates slot A's resolution identity."),
        ResolutionA.Identity, ChangedResolutionA.Identity);
    TestEqual(TEXT("Changing slot A does not mutate slot B's captured identity."),
        ResolutionB.Identity,
        FDWCTransparencyResolutionResolver::Resolve(*Asset, LayerB).Identity);
    return true;
}

#endif
