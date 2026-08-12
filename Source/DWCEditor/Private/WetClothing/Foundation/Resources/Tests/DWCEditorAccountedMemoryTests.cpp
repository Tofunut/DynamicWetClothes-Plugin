//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Resources/DWCEditorAccountedMemory.h"

namespace
{
    FDWCEditorResourceBudgetConfig MakeAccountedMemoryBudget(const uint64 Bytes)
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = Bytes;
        Config.WorkerPrivateCPUBytes = Bytes;
        Config.PreviewWorkspaceCPUBytes = Bytes;
        Config.SharedCacheCPUBytes = Bytes;
        Config.UploadStagingCPUBytes = Bytes;
        Config.PreviewGPUBytes = Bytes;
        return Config;
    }

    FDWCEditorAsyncOperationIdentity MakeAccountedMemoryOwner()
    {
        FDWCEditorAsyncOperationIdentity Owner;
        Owner.Key.Namespace = TEXT("DWC.Test.AccountedMemory");
        Owner.SessionEpoch = FGuid::NewGuid();
        Owner.OperationId = 1;
        Owner.Generation = 1;
        return Owner;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAccountedMemoryLifecycleTest,
    "DWC.Editor.Foundation.Resources.AccountedMemory.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAccountedMemoryLifecycleTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 1024;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeAccountedMemoryBudget(BudgetBytes));
    FDWCEditorAccountedMemory Account;
    Account.Configure(
        Governor,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        MakeAccountedMemoryOwner(),
        TEXT("Accounted memory lifecycle test"));

    TestTrue(TEXT("Initial allocation is adopted"), Account.TryAdoptActualBytes(400));
    TestEqual(TEXT("Actual bytes are tracked"), Account.GetActualBytes(), 400ull);
    TestEqual(TEXT("Governor reservation matches allocation"), Account.GetReservedBytes(), 400ull);
    TestTrue(TEXT("Account reports a valid ownership contract"), Account.IsAccounted());

    TestTrue(TEXT("Allocation can shrink"), Account.TryAdoptActualBytes(160));
    TestEqual(TEXT("Shrunk reservation follows allocation"), Account.GetReservedBytes(), 160ull);
    Account.Reset();
    TestEqual(TEXT("Reset releases governor memory"), Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAccountedMemoryAtomicFailureTest,
    "DWC.Editor.Foundation.Resources.AccountedMemory.AtomicFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAccountedMemoryAtomicFailureTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 512;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeAccountedMemoryBudget(BudgetBytes));
    FDWCEditorAccountedMemory Account;
    Account.Configure(
        Governor,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        MakeAccountedMemoryOwner(),
        TEXT("Accounted memory failure test"));

    TestTrue(TEXT("Baseline allocation is admitted"), Account.TryAdoptActualBytes(256));
    FString Error;
    TestFalse(TEXT("Oversized replacement is rejected"), Account.TryAdoptActualBytes(768, &Error));
    TestEqual(TEXT("Failed replacement preserves actual bytes"), Account.GetActualBytes(), 256ull);
    TestEqual(TEXT("Failed replacement preserves reservation"), Account.GetReservedBytes(), 256ull);
    TestFalse(TEXT("Failure explains the rejected admission"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAccountedMemorySharedLifetimeTest,
    "DWC.Editor.Foundation.Resources.AccountedMemory.SharedLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAccountedMemorySharedLifetimeTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 1024;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeAccountedMemoryBudget(BudgetBytes));
    TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Owner =
        MakeShared<FDWCEditorAccountedMemory, ESPMode::ThreadSafe>();
    Owner->Configure(
        Governor,
        EDWCEditorResourcePool::SharedCacheCPU,
        MakeAccountedMemoryOwner(),
        TEXT("Accounted memory shared lifetime test"));
    TestTrue(TEXT("Shared allocation is admitted"), Owner->TryAdoptActualBytes(384));

    TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Consumer = Owner;
    Owner.Reset();
    TestEqual(
        TEXT("A remaining consumer keeps the reservation alive"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes,
        384ull);
    Consumer.Reset();
    TestEqual(
        TEXT("The last consumer releases the reservation"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes,
        0ull);
    return true;
}

#endif
