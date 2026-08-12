// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Modes/Part/Topology/DWCPartTopologyCache.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartTopologyKeyContractTest,
    "DWC.Editor.WetPart.Topology.LOD0KeyContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartTopologyKeyContractTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    const FDWCEditorCacheKey SlotThree = FDWCPartTopologyCache::BuildKey(*Asset, 2, 3);
    const FDWCEditorCacheKey SlotFour = FDWCPartTopologyCache::BuildKey(*Asset, 2, 4);

    TestEqual(TEXT("Wet Part authoring topology is fixed to LOD0"),
        SlotThree.LODIndex, FDWCPartTopologyCache::AuthoringLODIndex);
    TestEqual(TEXT("The selected UV channel participates in the cache key"),
        SlotThree.UVChannelIndex, 2);
    TestEqual(TEXT("The material slot participates in the cache key"),
        SlotThree.MaterialSlotIndex, 3);
    TestFalse(TEXT("Different material slots cannot share topology entries"), SlotThree == SlotFour);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCPartTopologyLeaseLifetimeTest,
    "DWC.Editor.WetPart.Topology.SharedLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCPartTopologyLeaseLifetimeTest::RunTest(const FString&)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    FDWCEditorCacheStore Store(1024ull * 1024ull);
    const FDWCEditorCacheKey Key = FDWCPartTopologyCache::BuildKey(*Asset, 0, 1);

    TSharedRef<FDWCPartTopologyCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FDWCPartTopologyCacheValue, ESPMode::ThreadSafe>();
    TSharedPtr<FWetClothingAssetUVIsland> Island = MakeShared<FWetClothingAssetUVIsland>();
    Island->UVIslandID = 17;
    Island->TriangleIDs.Add(4);
    Island->UVTriangles.AddDefaulted();
    Value->Islands.Add(Island);
    Value->PickTriangles.AddDefaulted();
    Value->PickTriangleIndices.Add(0);
    Value->PickBVHNodes.AddDefaulted();

    const uint64 PayloadBytes = Value->GetAllocatedSizeBytes();
    TestTrue(TEXT("Topology accounting includes island and compact pick payloads"), PayloadBytes > 0);
    TestTrue(TEXT("Topology value is admitted by the shared cache"), Store.Put(Key, Value));

    FDWCEditorCacheLease Lease = Store.FindLease<FDWCPartTopologyCacheValue>(Key);
    TestTrue(TEXT("Wet Part consumers acquire a typed topology lease"), Lease.IsValid());
    Store.InvalidateOwner(Asset);
    TestTrue(TEXT("Invalidation cannot free topology while a viewport lease is active"), Lease.IsValid());
    const FDWCPartTopologyCacheValue* LeasedValue = Lease.GetAs<FDWCPartTopologyCacheValue>();
    TestNotNull(TEXT("The leased topology payload remains readable"), LeasedValue);
    if (LeasedValue != nullptr)
    {
        TestEqual(TEXT("The leased island identity is preserved"), LeasedValue->Islands[0]->UVIslandID, 17);
    }
    return true;
}

#endif
