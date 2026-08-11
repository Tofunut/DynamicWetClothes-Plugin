// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetRendering/DWCGeneratedMaterialContract.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCGeneratedMaterialRuntimeContractTest,
    "DWC.Runtime.WetRendering.GeneratedMaterialContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCGeneratedMaterialRuntimeContractTest::RunTest(const FString&)
{
    TestTrue(
        TEXT("Complete v7 metadata is runtime compatible"),
        DWCGeneratedMaterialContract::IsRuntimeCompatible(
            DWCGeneratedMaterialContract::CurrentGeneratorVersion,
            TEXT("Generation"),
            TEXT("Source")));
    TestFalse(
        TEXT("v6 metadata is rejected"),
        DWCGeneratedMaterialContract::IsRuntimeCompatible(6, TEXT("Generation"), TEXT("Source")));
    TestFalse(
        TEXT("Missing generation signature is rejected"),
        DWCGeneratedMaterialContract::IsRuntimeCompatible(
            DWCGeneratedMaterialContract::CurrentGeneratorVersion,
            FString(),
            TEXT("Source")));
    TestFalse(
        TEXT("Missing source signature is rejected"),
        DWCGeneratedMaterialContract::IsRuntimeCompatible(
            DWCGeneratedMaterialContract::CurrentGeneratorVersion,
            TEXT("Generation"),
            FString()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
