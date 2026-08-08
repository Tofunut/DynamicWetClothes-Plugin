// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SkeletalMeshComponent.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSpatialQueryTest,
    "DWC.Editor.Foundation.Spatial.RaycastUvAndAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSpatialQueryTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorCacheStore>                       Store = MakeShared<FDWCEditorCacheStore>();
    FDWCEditorSpatialQueryService                          Service(Store);
    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    Data->UVChannelIndex = 2;
    Data->MaterialSlotIndex = 4;

    FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
    Triangle.MaterialSlotIndex = 4;
    Triangle.TriangleID = 9;
    Triangle.UVIslandID = 3;
    Triangle.LocalPositions[0] = FVector3f(0.0f, 0.0f, 0.0f);
    Triangle.LocalPositions[1] = FVector3f(1.0f, 0.0f, 0.0f);
    Triangle.LocalPositions[2] = FVector3f(0.0f, 1.0f, 0.0f);
    Triangle.UVs[0] = FVector2f(0.0f, 0.0f);
    Triangle.UVs[1] = FVector2f(1.0f, 0.0f);
    Triangle.UVs[2] = FVector2f(0.0f, 1.0f);
    Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
    Triangle.LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
    Triangle.LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
    {
        Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
        Triangle.UVBounds += Triangle.UVs[CornerIndex];
    }
    const uint64 LookupKey = (static_cast<uint64>(4) << 32) | static_cast<uint32>(9);
    Data->TriangleLookup.Add(LookupKey, 0);

    FDWCEditorSpatialHandle Handle = Data;
    USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>();

    FDWCEditorSurfaceHit Hit;
    TestTrue(
        TEXT("Ray intersects the shared local-space triangle"),
        Service.TraceSurface(
            Handle,
            Component,
            FVector(0.25, 0.25, 10.0),
            FVector(0.0, 0.0, -1.0),
            Hit));
    TestEqual(TEXT("Ray hit preserves triangle ID"), Hit.TriangleID, 9);
    TestEqual(TEXT("Ray hit preserves Data UV channel"), Hit.UVChannelIndex, 2);

    TArray<FDWCEditorProjectedSurface> Surfaces;
    Service.FindSurfacesAtUV(Handle, Component, FVector2D(0.25, 0.25), Surfaces);
    TestEqual(TEXT("UV projection finds one surface"), Surfaces.Num(), 1);

    FDWCEditorProjectedSurface Anchor;
    TestTrue(
        TEXT("Stored triangle anchor resolves through the shared lookup"),
        Service.ResolveTriangleAnchor(
            Handle,
            Component,
            4,
            9,
            FVector3f(0.5f, 0.25f, 0.25f),
            Anchor));
    TestEqual(TEXT("Anchor preserves island ID"), Anchor.UVIslandID, 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSpatialLeaseLifetimeTest,
    "DWC.Editor.Foundation.Spatial.LeaseSurvivesInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSpatialLeaseLifetimeTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorCacheStore> Store = MakeShared<FDWCEditorCacheStore>();
    FDWCEditorSpatialQueryService    Service(Store);
    UTexture2D*                      Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey               Key;
    Key.Namespace = TEXT("Spatial");
    Key.Owner = FObjectKey(Owner);
    Key.LODIndex = 0;
    Key.UVChannelIndex = 2;
    Key.MaterialSlotIndex = 4;

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    Data->LODIndex = Key.LODIndex;
    Data->UVChannelIndex = Key.UVChannelIndex;
    Data->MaterialSlotIndex = Key.MaterialSlotIndex;

    FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
    Triangle.MaterialSlotIndex = Key.MaterialSlotIndex;
    Triangle.TriangleID = 12;
    Triangle.UVIslandID = 8;
    Triangle.LocalPositions[0] = FVector3f(0.0f, 0.0f, 0.0f);
    Triangle.LocalPositions[1] = FVector3f(1.0f, 0.0f, 0.0f);
    Triangle.LocalPositions[2] = FVector3f(0.0f, 1.0f, 0.0f);
    Triangle.UVs[0] = FVector2f(0.0f, 0.0f);
    Triangle.UVs[1] = FVector2f(1.0f, 0.0f);
    Triangle.UVs[2] = FVector2f(0.0f, 1.0f);
    Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
    Triangle.LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
    Triangle.LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
    {
        Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
        Triangle.UVBounds += Triangle.UVs[CornerIndex];
    }
    const uint64 LookupKey = (static_cast<uint64>(Key.MaterialSlotIndex) << 32) |
                             static_cast<uint32>(Triangle.TriangleID);
    Data->TriangleLookup.Add(LookupKey, 0);

    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = Data;
    Store->Put(Key, CacheValue);
    FDWCEditorSpatialLease Lease = Store->FindLease<FDWCEditorSpatialData>(Key);
    TestTrue(TEXT("Spatial cache entry can be leased"), Lease.IsValid());

    FDWCEditorSpatialHandle Handle =
        StaticCastSharedPtr<const FDWCEditorSpatialData>(Lease.GetSharedValue());
    Store->InvalidateOwner(Owner);
    TestEqual(TEXT("Invalidation removes the spatial cache index"), Store->GetEntryCount(), 0);
    TestTrue(TEXT("The spatial lease keeps the query payload alive"), Lease.IsValid());

    USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>();
    FDWCEditorSurfaceHit    Hit;
    TestTrue(
        TEXT("Spatial queries remain valid after cache invalidation while leased"),
        Service.TraceSurface(
            Handle,
            Component,
            FVector(0.25, 0.25, 10.0),
            FVector(0.0, 0.0, -1.0),
            Hit));
    TestEqual(TEXT("Leased spatial query preserves triangle ID"), Hit.TriangleID, 12);

    Lease.Reset();
    TestEqual(TEXT("Released spatial lease retires no entry"), Store->GetRetiredEntryCount(), 0);
    return true;
}

#endif
