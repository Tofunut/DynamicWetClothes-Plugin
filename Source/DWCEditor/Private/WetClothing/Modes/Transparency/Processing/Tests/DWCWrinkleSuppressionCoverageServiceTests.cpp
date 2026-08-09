//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWrinkleSuppressionEvaluationParityTest,
    "DWC.Transparency.Processing.WrinkleSuppressionEvaluationParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWrinkleSuppressionEvaluationParityTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Coverage below threshold is rejected."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.1f, 0.2f, 0.1f),
        0.0f);
    TestTrue(TEXT("Coverage inside softness uses smoothstep before multiplication."),
        FMath::IsNearlyEqual(
            FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.25f, 0.2f, 0.1f),
            0.125f,
            0.0001f));
    TestEqual(TEXT("Coverage above the transition is preserved."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.4f, 0.2f, 0.1f),
        0.4f);
    TestEqual(TEXT("Zero softness is a hard threshold."),
        FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(0.2f, 0.2f, 0.0f),
        0.2f);
    return true;
}

#endif
