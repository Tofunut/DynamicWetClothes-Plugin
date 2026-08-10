// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewLayerOrderTest,
    "DWC.Editor.Preview.Orchestration.LayerOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewLayerOrderTest::RunTest(const FString& Parameters)
{
    const FName SharedParameter(TEXT("DWC_TestSharedParameter"));

    FDWCEditorPreviewLayer Transparency;
    Transparency.Kind = EDWCEditorPreviewLayerKind::SavedTransparency;
    Transparency.MaterialSlotIndex = 3;
    Transparency.AddScalar(SharedParameter, 2.0f);

    FDWCEditorPreviewLayer Wrinkle;
    Wrinkle.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleAccumulated;
    Wrinkle.MaterialSlotIndex = 3;
    Wrinkle.AddScalar(SharedParameter, 1.0f);

    FDWCEditorPreviewLayerStack Stack;
    Stack.MaterialSlotIndex = 3;
    Stack.AddOrReplace(MoveTemp(Transparency));
    Stack.AddOrReplace(MoveTemp(Wrinkle));

    FDWCEditorPreviewParameterSet ParametersOut;
    Stack.BuildParameterSet(ParametersOut);
    TestEqual(TEXT("Duplicate bindings follow semantic layer order"), ParametersOut.Scalars.Num(), 1);
    TestEqual(TEXT("Transparency is composed after wrinkle"), ParametersOut.Scalars[0].Value, 2.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewLayerReplaceTest,
    "DWC.Editor.Preview.Orchestration.LayerReplace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewLayerReplaceTest::RunTest(const FString& Parameters)
{
    const FName EnableParameter(TEXT("DWC_TestEnable"));

    FDWCEditorPreviewLayerStack Stack;
    Stack.MaterialSlotIndex = 7;

    FDWCEditorPreviewLayer Initial;
    Initial.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleHover;
    Initial.MaterialSlotIndex = 7;
    Initial.AddScalar(EnableParameter, 0.0f);
    Stack.AddOrReplace(MoveTemp(Initial));

    FDWCEditorPreviewLayer Updated;
    Updated.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleHover;
    Updated.MaterialSlotIndex = 7;
    Updated.AddScalar(EnableParameter, 1.0f);
    Stack.AddOrReplace(MoveTemp(Updated));

    FDWCEditorPreviewParameterSet ParametersOut;
    Stack.BuildParameterSet(ParametersOut);
    TestEqual(TEXT("A semantic layer is replaced instead of duplicated"), Stack.Layers.Num(), 1);
    TestEqual(TEXT("The latest layer value is retained"), ParametersOut.Scalars[0].Value, 1.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewLayerEquivalenceTest,
    "DWC.Editor.Preview.Orchestration.LayerEquivalence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewLayerEquivalenceTest::RunTest(const FString& Parameters)
{
    FDWCEditorPreviewLayer Initial;
    Initial.Kind = EDWCEditorPreviewLayerKind::LiveWrinkleHover;
    Initial.MaterialSlotIndex = 4;
    Initial.AuthoringRevision = 12;
    Initial.ResourceRevision = 8;
    Initial.AddScalar(TEXT("DWC_TestStrength"), 0.75f, 0.0f);
    Initial.AddVector(TEXT("DWC_TestTint"), FLinearColor(0.2f, 0.4f, 0.6f, 1.0f));
    Initial.AddTexture(TEXT("DWC_TestTexture"), nullptr);

    FDWCEditorPreviewLayer Equivalent = Initial;
    TestTrue(TEXT("Matching layer is equivalent"), Initial.IsEquivalentTo(Equivalent));

    Equivalent.ResourceRevision++;
    TestFalse(TEXT("Resource revision invalidates equivalence"), Initial.IsEquivalentTo(Equivalent));
    Equivalent = Initial;
    Equivalent.Scalars[0].ResetValue = 0.1f;
    TestFalse(TEXT("Reset value invalidates equivalence"), Initial.IsEquivalentTo(Equivalent));
    Equivalent = Initial;
    Equivalent.Vectors[0].Value.G = 0.5f;
    TestFalse(TEXT("Vector value invalidates equivalence"), Initial.IsEquivalentTo(Equivalent));
    Equivalent = Initial;
    Equivalent.bEnabled = false;
    TestFalse(TEXT("Enabled state invalidates equivalence"), Initial.IsEquivalentTo(Equivalent));
    Equivalent = Initial;
    Equivalent.AddScalar(TEXT("DWC_TestAdditionalScalar"), 1.0f);
    Swap(Equivalent.Scalars[0], Equivalent.Scalars[1]);
    Initial.AddScalar(TEXT("DWC_TestAdditionalScalar"), 1.0f);
    TestFalse(TEXT("Parameter ordering remains part of the layer contract"),
              Initial.IsEquivalentTo(Equivalent));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewIdenticalLiveLayerSkipTest,
    "DWC.Editor.Preview.Orchestration.IdenticalLiveLayerSkip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewIdenticalLiveLayerSkipTest::RunTest(const FString& Parameters)
{
    FDWCEditorPreviewOrchestrator Orchestrator;

    FDWCEditorPreviewLayer Layer;
    Layer.Kind = EDWCEditorPreviewLayerKind::LiveTransparencyHover;
    Layer.MaterialSlotIndex = 6;
    Layer.AuthoringRevision = 21;
    Layer.ResourceRevision = 3;
    Layer.AddScalar(TEXT("DWC_TestHoverEnabled"), 1.0f);

    TestTrue(TEXT("Initial live layer updates the orchestration state"), Orchestrator.SetLiveLayer(6, Layer));
    TestFalse(TEXT("Identical live layer bypasses recomposition"), Orchestrator.SetLiveLayer(6, Layer));
    TestEqual(TEXT("Only initial layer updated state"), Orchestrator.GetLiveLayerUpdateCount(), 1ull);
    TestEqual(TEXT("Identical layer was skipped before recomposition"), Orchestrator.GetIdenticalLiveLayerSkipCount(), 1ull);

    Layer.ResourceRevision++;
    TestTrue(TEXT("Changed resource revision updates orchestration state"), Orchestrator.SetLiveLayer(6, Layer));
    TestEqual(TEXT("Changed resource increments update count"), Orchestrator.GetLiveLayerUpdateCount(), 2ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewTransparencyHoverLayerOrderTest,
    "DWC.Editor.Preview.Orchestration.TransparencyHoverLayerOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewTransparencyHoverLayerOrderTest::RunTest(const FString& Parameters)
{
    const FName HoverEnable(TEXT("DWC_TestTransparencyHoverEnable"));

    FDWCEditorPreviewLayer StableTransparency;
    StableTransparency.Kind = EDWCEditorPreviewLayerKind::LiveTransparency;
    StableTransparency.MaterialSlotIndex = 2;
    StableTransparency.AddScalar(HoverEnable, 0.0f);

    FDWCEditorPreviewLayer Hover;
    Hover.Kind = EDWCEditorPreviewLayerKind::LiveTransparencyHover;
    Hover.MaterialSlotIndex = 2;
    Hover.AddScalar(HoverEnable, 1.0f);

    FDWCEditorPreviewLayerStack Stack;
    Stack.MaterialSlotIndex = 2;
    Stack.AddOrReplace(MoveTemp(Hover));
    Stack.AddOrReplace(MoveTemp(StableTransparency));

    FDWCEditorPreviewParameterSet ParametersOut;
    Stack.BuildParameterSet(ParametersOut);
    TestEqual(TEXT("The ephemeral hover layer overrides the stable transparency layer"),
              ParametersOut.Scalars.Num(), 1);
    TestEqual(TEXT("The hover enable value wins regardless of insertion order"),
              ParametersOut.Scalars[0].Value, 1.0f);

    Stack.Remove(EDWCEditorPreviewLayerKind::LiveTransparencyHover);
    Stack.BuildParameterSet(ParametersOut);
    TestEqual(TEXT("Removing hover restores the stable transparency value"),
              ParametersOut.Scalars[0].Value, 0.0f);
    return true;
}

#endif
