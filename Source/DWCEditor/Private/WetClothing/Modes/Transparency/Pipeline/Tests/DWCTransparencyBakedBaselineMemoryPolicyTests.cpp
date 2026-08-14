// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyBakedBaselineMemoryPolicy.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyBakedBaselineMemoryPolicyTest,
    "DWC.Editor.Transparency.BakedBaseline.MemoryPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyBakedBaselineMemoryPolicyTest::RunTest(const FString&)
{
    FDWCTransparencyBakedBaselineMemoryPlan Plan2K;
    FDWCTransparencyBakedBaselineMemoryPlan Plan4K;
    FString Error;
    TestTrue(TEXT("A 2K baked baseline receives a valid memory plan."),
        FDWCTransparencyBakedBaselineMemoryPolicy::TryBuildPlan(
            FIntPoint(2048, 2048), 64, Plan2K, Error));
    TestTrue(TEXT("A 4K baked baseline receives a valid memory plan."),
        FDWCTransparencyBakedBaselineMemoryPolicy::TryBuildPlan(
            FIntPoint(4096, 4096), 64, Plan4K, Error));
    TestTrue(TEXT("Raw mip bytes are represented in the worker peak."),
        Plan4K.WorkerPeakBytes >= Plan4K.RawMipBytes);
    TestTrue(TEXT("Retained payload growth follows output pixel count."),
        Plan4K.RetainedPayloadBytes > Plan2K.RetainedPayloadBytes * 3);
    TestTrue(TEXT("Bounded mask scratch remains far below rich per-texel surface samples."),
        Plan4K.RasterScratchBytes <
            static_cast<uint64>(4096) * 4096 * sizeof(FDWCRevealBakeTexelSample));

    FDWCTransparencyBakedBaselineMemoryPlan InvalidPlan;
    TestFalse(TEXT("Invalid resolution is rejected before allocation."),
        FDWCTransparencyBakedBaselineMemoryPolicy::TryBuildPlan(
            FIntPoint::ZeroValue, 0, InvalidPlan, Error));
    return true;
}

#endif
