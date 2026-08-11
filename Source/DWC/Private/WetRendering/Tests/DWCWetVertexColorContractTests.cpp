// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetRendering/DWCWetVertexColorContract.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWetVertexColorContractTest,
    "DWC.Runtime.WetRendering.CPUVertexColorContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWetVertexColorContractTest::RunTest(const FString&)
{
    TestEqual(
        TEXT("CPU material wetness is read from VertexColor.R"),
        DWCWetVertexColorContract::CPUWetnessOutput(),
        FName(TEXT("R")));

    const FLinearColor Encoded = DWCWetVertexColorContract::Encode(
        0.25f,
        FLinearColor(0.2f, 0.4f, 0.6f, 1.0f));
    TestEqual(TEXT("Wetness is encoded in R"), Encoded.R, 0.25f);
    TestEqual(TEXT("Wet Part debug R is encoded in G"), Encoded.G, 0.2f);
    TestEqual(TEXT("Wet Part debug G is encoded in B"), Encoded.B, 0.4f);
    TestEqual(TEXT("Wet Part debug B is encoded in A"), Encoded.A, 0.6f);

    const FLinearColor Clamped = DWCWetVertexColorContract::Encode(
        2.0f,
        FLinearColor(-1.0f, 0.5f, 3.0f, 1.0f));
    TestEqual(TEXT("Wetness is clamped"), Clamped.R, 1.0f);
    TestEqual(TEXT("Debug red is clamped"), Clamped.G, 0.0f);
    TestEqual(TEXT("Debug green remains unchanged"), Clamped.B, 0.5f);
    TestEqual(TEXT("Debug blue is clamped"), Clamped.A, 1.0f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
