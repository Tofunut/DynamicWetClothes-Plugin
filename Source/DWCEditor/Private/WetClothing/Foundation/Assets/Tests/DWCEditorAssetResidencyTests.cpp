// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "UObject/GarbageCollection.h"
#include "WetClothing/Foundation/Assets/DWCEditorAssetResidency.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAssetResidencyGCLifetimeTest,
    "DWC.Editor.Foundation.Assets.Residency.GCLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAssetResidencyGCLifetimeTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorAssetResidencyRegistry> Registry =
        MakeShared<FDWCEditorAssetResidencyRegistry>();
    UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
    TWeakObjectPtr<UTexture2D> WeakTexture(Texture);

    FDWCEditorAssetResidencyLease FirstLease = Registry->Acquire(
        Texture,
        EDWCEditorAssetResidencyDomain::Wrinkle,
        TEXT("Selected brush texture"));
    FDWCEditorAssetResidencyLease SecondLease = Registry->Acquire(
        Texture,
        EDWCEditorAssetResidencyDomain::Wrinkle,
        TEXT("Selected brush texture"));
    Texture = nullptr;

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("An active residency lease protects the transient asset across GC"),
        WeakTexture.IsValid());
    FDWCEditorAssetResidencyDiagnostics Diagnostics = Registry->GetDiagnostics();
    TestEqual(TEXT("Duplicate purpose leases share one resident object"),
        Diagnostics.ResidentObjectCount, 1);
    TestEqual(TEXT("Both lease owners remain visible"), Diagnostics.ActiveLeaseCount, 2);

    FirstLease.Reset();
    Diagnostics = Registry->GetDiagnostics();
    TestEqual(TEXT("Releasing one owner preserves the shared residency"),
        Diagnostics.ActiveLeaseCount, 1);

    SecondLease.Reset();
    Diagnostics = Registry->GetDiagnostics();
    TestEqual(TEXT("The last release removes the GC owner"),
        Diagnostics.ResidentObjectCount, 0);
    Registry->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAssetResidencyShutdownTest,
    "DWC.Editor.Foundation.Assets.Residency.Shutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAssetResidencyShutdownTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorAssetResidencyRegistry> Registry =
        MakeShared<FDWCEditorAssetResidencyRegistry>();
    FDWCEditorAssetResidencyLease Lease = Registry->Acquire(
        NewObject<UTexture2D>(GetTransientPackage()),
        EDWCEditorAssetResidencyDomain::Session,
        TEXT("Shutdown fixture"));

    Registry->Shutdown();
    TestEqual(TEXT("Shutdown releases every resident object"),
        Registry->GetDiagnostics().ResidentObjectCount, 0);
    Lease.Reset();
    TestEqual(TEXT("A late lease release remains harmless"),
        Registry->GetDiagnostics().ResidentObjectCount, 0);
    return true;
}

#endif

