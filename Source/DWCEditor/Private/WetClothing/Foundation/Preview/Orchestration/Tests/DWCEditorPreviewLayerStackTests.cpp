// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"

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
