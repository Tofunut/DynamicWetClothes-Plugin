// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyWorkingPayloadCache.h"

namespace
{
    TSharedPtr<FDWCTransparencySourcePayload> MakePayload(const int32 PixelCount)
    {
        TSharedPtr<FDWCTransparencySourcePayload> Payload =
            MakeShared<FDWCTransparencySourcePayload>();
        Payload->InnerColorBuffer.SetNumUninitialized(PixelCount);
        Payload->AutoAlphaBuffer.SetNumUninitialized(PixelCount);
        Payload->OuterCoverageBuffer.SetNumUninitialized(PixelCount);
        return Payload;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkingPayloadCacheLRUTest,
    "DWC.Editor.Transparency.WorkingPayloadCache.LRU",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkingPayloadCacheLRUTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyWorkingPayloadCache Cache(MAX_uint64, 2);
    const FGuid First = FGuid::NewGuid();
    const FGuid Second = FGuid::NewGuid();
    const FGuid Third = FGuid::NewGuid();

    Cache.Add(First, MakePayload(8));
    Cache.Add(Second, MakePayload(8));
    TestNotNull(TEXT("Reading the first payload refreshes its LRU age."), Cache.Find(First));
    Cache.Add(Third, MakePayload(8));

    TestTrue(TEXT("The recently used payload remains retained."), Cache.Contains(First));
    TestFalse(TEXT("The least recently used payload is evicted."), Cache.Contains(Second));
    TestTrue(TEXT("The newly inserted payload remains retained."), Cache.Contains(Third));
    TestEqual(TEXT("The entry bound is respected."), Cache.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkingPayloadCacheByteBudgetTest,
    "DWC.Editor.Transparency.WorkingPayloadCache.ByteBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkingPayloadCacheByteBudgetTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<FDWCTransparencySourcePayload> FirstPayload = MakePayload(64);
    const TSharedPtr<FDWCTransparencySourcePayload> SecondPayload = MakePayload(64);
    const uint64 OnePayloadBytes = FirstPayload->GetAllocatedBytes();
    FDWCTransparencyWorkingPayloadCache Cache(OnePayloadBytes + OnePayloadBytes / 2, 4);
    const FGuid First = FGuid::NewGuid();
    const FGuid Second = FGuid::NewGuid();

    Cache.Add(First, FirstPayload);
    Cache.Add(Second, SecondPayload);

    TestFalse(TEXT("The older payload is evicted when the byte budget is exceeded."), Cache.Contains(First));
    TestTrue(TEXT("The current payload remains available."), Cache.Contains(Second));
    TestTrue(TEXT("Retained bytes return below the byte budget."), Cache.GetUsedBytes() <= Cache.GetBudgetBytes());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkingPayloadCacheOversizedActiveTest,
    "DWC.Editor.Transparency.WorkingPayloadCache.OversizedActivePayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkingPayloadCacheOversizedActiveTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyWorkingPayloadCache Cache(1, 2);
    const FGuid ActiveLayer = FGuid::NewGuid();
    Cache.Add(ActiveLayer, MakePayload(64));

    TestTrue(TEXT("One admitted active payload remains usable even above the retention target."),
        Cache.Contains(ActiveLayer));
    TestEqual(TEXT("An oversized payload does not cause an empty working set."), Cache.Num(), 1);
    TestTrue(TEXT("The cache reports the oversized retained size honestly."),
        Cache.GetUsedBytes() > Cache.GetBudgetBytes());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyWorkingPayloadCacheProtectedReclaimTest,
    "DWC.Editor.Transparency.WorkingPayloadCache.ProtectedReclaim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyWorkingPayloadCacheProtectedReclaimTest::RunTest(const FString& Parameters)
{
    FDWCTransparencyWorkingPayloadCache Cache(MAX_uint64, 4);
    const FGuid ActiveLayer = FGuid::NewGuid();
    const FGuid InactiveLayer = FGuid::NewGuid();
    Cache.Add(ActiveLayer, MakePayload(64));
    Cache.Add(InactiveLayer, MakePayload(128));
    const uint64 ReclaimableBytes = Cache.GetReclaimableBytes(ActiveLayer);

    const uint64 ReclaimedBytes = Cache.Reclaim(MAX_uint64, ActiveLayer);

    TestEqual(TEXT("Only inactive retained bytes are advertised as reclaimable."),
        ReclaimedBytes, ReclaimableBytes);
    TestTrue(TEXT("The protected active layer remains retained."), Cache.Contains(ActiveLayer));
    TestFalse(TEXT("The inactive layer is released."), Cache.Contains(InactiveLayer));
    return true;
}

#endif
