//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationFieldBuilder.h"

namespace
{
    FDWCEditorSpatialTriangle& AddOrientationTestTriangle(
        FDWCEditorSpatialData& Data,
        const int32 TriangleID,
        const FVector3f& Normal,
        const int64 Vertex0,
        const int64 Vertex1,
        const int64 Vertex2,
        const int32 IslandID,
        const FVector2f& UVOffset)
    {
        FDWCEditorSpatialTriangle& Triangle = Data.Triangles.AddDefaulted_GetRef();
        Triangle.TriangleID = TriangleID;
        Triangle.MaterialSlotIndex = 0;
        Triangle.UVIslandID = IslandID;
        Triangle.TopologyVertexIDs[0] = Vertex0;
        Triangle.TopologyVertexIDs[1] = Vertex1;
        Triangle.TopologyVertexIDs[2] = Vertex2;
        Triangle.LocalPositions[0] = FVector3f(0.0f, 0.0f, 0.0f);
        Triangle.LocalPositions[1] = FVector3f(0.0f, 1.0f, 0.0f);
        Triangle.LocalPositions[2] = FVector3f(1.0f, 0.0f, 0.0f);
        Triangle.UVs[0] = UVOffset + FVector2f(0.0f, 0.0f);
        Triangle.UVs[1] = UVOffset + FVector2f(0.0f, 0.5f);
        Triangle.UVs[2] = UVOffset + FVector2f(0.5f, 0.0f);
        Triangle.LocalNormal = Normal.GetSafeNormal();
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.LocalNormals[CornerIndex] = Triangle.LocalNormal;
        }
        return Triangle;
    }

    void ConnectOrientationTestTriangles(
        FDWCEditorSpatialTriangle& A,
        const int32 EdgeA,
        const int32 IndexB,
        FDWCEditorSpatialTriangle& B,
        const int32 EdgeB,
        const int32 IndexA,
        const EDWCEditorSpatialEdgeType EdgeType)
    {
        A.AdjacentTriangleIndices[EdgeA] = IndexB;
        A.EdgeTypes[EdgeA] = EdgeType;
        B.AdjacentTriangleIndices[EdgeB] = IndexA;
        B.EdgeTypes[EdgeB] = EdgeType;
    }

    FDWCEditorSurfaceOrientationPolicy MakeOrientationTestPolicy()
    {
        FDWCEditorSurfaceOrientationPolicy Policy;
        Policy.Normalize();
        return Policy;
    }

    bool DirectionsNearlyEqual(const FPackedNormal& A, const FPackedNormal& B)
    {
        return FVector3f::DotProduct(
            A.ToFVector3f().GetSafeNormal(),
            B.ToFVector3f().GetSafeNormal()) > 0.995f;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationStableFieldTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.StableMeshIsSparse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationStableFieldTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    AddOrientationTestTriangle(
        Data, 10, FVector3f(1.0f, 0.0f, 0.0f), 0, 1, 2, 0, FVector2f::ZeroVector);

    FDWCEditorSurfaceOrientationField Field;
    FString Warning;
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeOrientationTestPolicy();
    TestTrue(
        TEXT("A stable mesh builds an orientation field"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(Data.Triangles, Policy, Field, &Warning));
    TestTrue(TEXT("A stable mesh needs no sparse entries"), Field.IsEmpty());
    TestEqual(
        TEXT("A stable empty field is a completed build"),
        Field.BuildStatus,
        EDWCEditorSurfaceOrientationFieldBuildStatus::Ready);
    TestTrue(TEXT("A stable empty field preserves its policy contract"), Field.IsCompatible(Policy.BuildSignature()));
    TestTrue(TEXT("A stable build has no warning"), Warning.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationSeedPropagationTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.SeedPropagationAcrossUVSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationSeedPropagationTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    Data.Triangles.Reserve(3);
    FDWCEditorSpatialTriangle& Stable = AddOrientationTestTriangle(
        Data, 10, FVector3f(1.0f, 0.0f, 0.0f), 0, 1, 2, 0, FVector2f::ZeroVector);
    FDWCEditorSpatialTriangle& FallbackA = AddOrientationTestTriangle(
        Data, 20, FVector3f(0.0f, 0.0f, 1.0f), 2, 1, 3, 1, FVector2f(0.5f, 0.0f));
    FDWCEditorSpatialTriangle& FallbackB = AddOrientationTestTriangle(
        Data, 30, FVector3f(0.0f, 0.1f, 0.995f), 3, 1, 4, 1, FVector2f(0.5f, 0.5f));
    ConnectOrientationTestTriangles(Stable, 1, 1, FallbackA, 0, 0, EDWCEditorSpatialEdgeType::UVSeam);
    ConnectOrientationTestTriangles(FallbackA, 1, 2, FallbackB, 0, 1, EDWCEditorSpatialEdgeType::Regular);

    FDWCEditorSurfaceOrientationField Field;
    TestTrue(
        TEXT("A fallback component connected across a UV seam builds"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            Data.Triangles,
            MakeOrientationTestPolicy(),
            Field));
    TestEqual(TEXT("Only fallback triangles receive sparse entries"), Field.Entries.Num(), 2);
    TestNotNull(TEXT("The first fallback triangle has an entry"), Field.FindByTriangleIndex(1));
    TestNotNull(TEXT("The propagated fallback triangle has an entry"), Field.FindByTriangleIndex(2));
    TestEqual(TEXT("The component uses its stable neighbor"), Field.Diagnostics.FullyDegenerateComponentCount, 0);
    TestEqual(TEXT("The physical propagation records its UV seam crossing"), Field.Diagnostics.CrossedUVSeamEdgeCount, 1);
    TestTrue(
        TEXT("Directions remain continuous after topology propagation"),
        Field.Diagnostics.MaxAdjacentDirectionAngleDegrees < 5.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationBoundaryIsolationTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.BoundaryAndBlockedIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationBoundaryIsolationTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    Data.Triangles.Reserve(2);
    FDWCEditorSpatialTriangle& A = AddOrientationTestTriangle(
        Data, 50, FVector3f(0.0f, 0.0f, 1.0f), 0, 1, 2, 0, FVector2f::ZeroVector);
    FDWCEditorSpatialTriangle& B = AddOrientationTestTriangle(
        Data, 40, FVector3f(0.0f, 0.0f, 1.0f), 2, 1, 3, 0, FVector2f(0.5f, 0.0f));
    ConnectOrientationTestTriangles(A, 1, 1, B, 0, 0, EDWCEditorSpatialEdgeType::Blocked);

    FDWCEditorSurfaceOrientationField Field;
    TestTrue(
        TEXT("Blocked fallback components build independently"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            Data.Triangles,
            MakeOrientationTestPolicy(),
            Field));
    TestEqual(TEXT("A blocked edge creates two components"), Field.Diagnostics.FallbackComponentCount, 2);
    TestEqual(TEXT("Both isolated components use deterministic roots"), Field.Diagnostics.FullyDegenerateComponentCount, 2);
    TestEqual(TEXT("Both fallback triangles are represented"), Field.Entries.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationCornerSmoothingTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.SharedCornerSmoothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationCornerSmoothingTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    Data.Triangles.Reserve(2);
    FDWCEditorSpatialTriangle& A = AddOrientationTestTriangle(
        Data, 10, FVector3f(0.0f, 0.0f, 1.0f), 0, 1, 2, 0, FVector2f::ZeroVector);
    FDWCEditorSpatialTriangle& B = AddOrientationTestTriangle(
        Data, 20, FVector3f(0.0f, 0.2f, 0.98f), 2, 1, 3, 1, FVector2f(0.6f, 0.0f));
    ConnectOrientationTestTriangles(A, 1, 1, B, 0, 0, EDWCEditorSpatialEdgeType::UVSeam);

    FDWCEditorSurfaceOrientationField Field;
    TestTrue(
        TEXT("A horizontal seam component builds"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            Data.Triangles,
            MakeOrientationTestPolicy(),
            Field));
    const FDWCEditorSurfaceOrientationFieldEntry* EntryA = Field.FindByTriangleIndex(0);
    const FDWCEditorSurfaceOrientationFieldEntry* EntryB = Field.FindByTriangleIndex(1);
    TestNotNull(TEXT("First seam triangle has fallback data"), EntryA);
    TestNotNull(TEXT("Second seam triangle has fallback data"), EntryB);
    if (EntryA != nullptr && EntryB != nullptr)
    {
        TestTrue(
            TEXT("The shared topology vertex receives a continuous corner direction"),
            DirectionsNearlyEqual(EntryA->CornerFallbackV[2], EntryB->CornerFallbackV[0]));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationDeterminismTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.DeterministicOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationDeterminismTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    Data.Triangles.Reserve(2);
    FDWCEditorSpatialTriangle& A = AddOrientationTestTriangle(
        Data, 20, FVector3f(0.0f, 0.0f, 1.0f), 0, 1, 2, 0, FVector2f::ZeroVector);
    FDWCEditorSpatialTriangle& B = AddOrientationTestTriangle(
        Data, 10, FVector3f(0.0f, 0.0f, 1.0f), 2, 1, 3, 0, FVector2f(0.5f, 0.0f));
    ConnectOrientationTestTriangles(A, 1, 1, B, 0, 0, EDWCEditorSpatialEdgeType::Regular);

    FDWCEditorSurfaceOrientationField First;
    FDWCEditorSurfaceOrientationField Second;
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeOrientationTestPolicy();
    TestTrue(TEXT("The first deterministic field builds"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(Data.Triangles, Policy, First));
    TestTrue(TEXT("The repeated deterministic field builds"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(Data.Triangles, Policy, Second));
    TestEqual(TEXT("Repeated builds have equal entry counts"), First.Entries.Num(), Second.Entries.Num());
    for (int32 EntryIndex = 0; EntryIndex < First.Entries.Num() && EntryIndex < Second.Entries.Num(); ++EntryIndex)
    {
        TestEqual(
            TEXT("Repeated builds preserve triangle ordering"),
            First.Entries[EntryIndex].TriangleIndex,
            Second.Entries[EntryIndex].TriangleIndex);
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            TestTrue(
                TEXT("Repeated builds preserve packed corner direction"),
                DirectionsNearlyEqual(
                    First.Entries[EntryIndex].CornerFallbackV[CornerIndex],
                    Second.Entries[EntryIndex].CornerFallbackV[CornerIndex]));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationMalformedTopologyTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.MalformedTopologyDegrades",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationMalformedTopologyTest::RunTest(const FString&)
{
    FDWCEditorSpatialData Data;
    FDWCEditorSpatialTriangle& Triangle = AddOrientationTestTriangle(
        Data, 10, FVector3f::ZeroVector, INDEX_NONE, 1, 2, 0, FVector2f::ZeroVector);
    Triangle.EdgeTypes[0] = EDWCEditorSpatialEdgeType::Regular;
    Triangle.AdjacentTriangleIndices[0] = 99;

    FDWCEditorSurfaceOrientationField Field;
    FString Warning;
    TestTrue(
        TEXT("Malformed optional orientation input does not fail the spatial payload"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            Data.Triangles,
            MakeOrientationTestPolicy(),
            Field,
            &Warning));
    TestEqual(
        TEXT("Malformed optional data is explicitly degraded"),
        Field.BuildStatus,
        EDWCEditorSurfaceOrientationFieldBuildStatus::Degraded);
    TestFalse(TEXT("A degraded build reports one warning"), Warning.IsEmpty());
    TestTrue(TEXT("The degraded result still satisfies the immutable field contract"), Field.ValidateContract(1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationCacheLeaseTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.CacheLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationCacheLeaseTest::RunTest(const FString&)
{
    FDWCEditorSpatialData SourceData;
    AddOrientationTestTriangle(
        SourceData, 10, FVector3f(0.0f, 0.0f, 1.0f), 0, 1, 2, 0, FVector2f::ZeroVector);

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> CachedData =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    CachedData->Triangles = SourceData.Triangles;
    TestTrue(
        TEXT("The leased orientation field builds"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            CachedData->Triangles,
            MakeOrientationTestPolicy(),
            CachedData->SurfaceOrientationField));

    FDWCEditorCacheStore Store(1024ull * 1024ull);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("SpatialOrientationLeaseTest");
    Key.Owner = FObjectKey(Owner);
    Key.MaterialSlotIndex = 0;
    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = CachedData;
    TestTrue(TEXT("The spatial payload enters the byte-budget cache"), Store.Put(Key, CacheValue));

    FDWCEditorCacheLease Lease = Store.FindLease<FDWCEditorSpatialData>(Key);
    TestTrue(TEXT("The spatial payload can be leased"), Lease.IsValid());
    Store.InvalidateNamespace(Key.Namespace);
    TestFalse(
        TEXT("Invalidation removes the spatial payload from the cache index"),
        Store.Find<FDWCEditorSpatialData>(Key).IsValid());
    const FDWCEditorSpatialData* LeasedData = Lease.GetAs<FDWCEditorSpatialData>();
    TestNotNull(TEXT("The lease keeps the orientation payload alive"), LeasedData);
    if (LeasedData != nullptr)
    {
        TestNotNull(
            TEXT("The leased sparse orientation entry remains readable"),
            LeasedData->SurfaceOrientationField.FindByTriangleIndex(0));
    }
    Lease.Reset();
    TestEqual(TEXT("Releasing the lease retires the invalidated payload"), Store.GetRetiredEntryCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationSparseMemoryBoundTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.FieldBuilder.SparseMemoryBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationSparseMemoryBoundTest::RunTest(const FString&)
{
    constexpr int32 TriangleCount = 256;
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeOrientationTestPolicy();

    FDWCEditorSpatialData StableData;
    StableData.Triangles.Reserve(TriangleCount);
    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        AddOrientationTestTriangle(
            StableData,
            TriangleIndex,
            FVector3f(1.0f, 0.0f, 0.0f),
            TriangleIndex * 3,
            TriangleIndex * 3 + 1,
            TriangleIndex * 3 + 2,
            TriangleIndex,
            FVector2f::ZeroVector);
    }
    TestTrue(
        TEXT("A large stable mesh builds its orientation contract"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            StableData.Triangles,
            Policy,
            StableData.SurfaceOrientationField));
    TestTrue(
        TEXT("Stable triangles allocate no sparse fallback payload"),
        StableData.SurfaceOrientationField.IsEmpty() &&
            StableData.SurfaceOrientationField.GetAllocatedSizeBytes() == 0);

    FDWCEditorSpatialData FallbackData;
    FallbackData.Triangles.Reserve(TriangleCount);
    for (int32 TriangleIndex = 0; TriangleIndex < TriangleCount; ++TriangleIndex)
    {
        AddOrientationTestTriangle(
            FallbackData,
            TriangleIndex,
            FVector3f(0.0f, 0.0f, 1.0f),
            TriangleIndex * 3,
            TriangleIndex * 3 + 1,
            TriangleIndex * 3 + 2,
            TriangleIndex,
            FVector2f::ZeroVector);
    }
    TestTrue(
        TEXT("A horizontal mesh builds bounded fallback data"),
        FDWCEditorSurfaceOrientationFieldBuilder::Build(
            FallbackData.Triangles,
            Policy,
            FallbackData.SurfaceOrientationField));
    TestEqual(
        TEXT("Only fallback triangles receive sparse field entries"),
        FallbackData.SurfaceOrientationField.Entries.Num(),
        TriangleCount);
    TestEqual(
        TEXT("The sparse lookup has exactly one index per source triangle"),
        FallbackData.SurfaceOrientationField.EntryIndexByTriangle.Num(),
        TriangleCount);

    const uint64 SparseBytes = FallbackData.SurfaceOrientationField.GetAllocatedSizeBytes();
    const uint64 DenseFloatBaseline = static_cast<uint64>(TriangleCount) *
        (sizeof(int32) + sizeof(FVector3f) * 3);
    TestTrue(TEXT("Packed sparse orientation data is smaller than dense float directions"),
        SparseBytes > 0 && SparseBytes < DenseFloatBaseline);
    const uint64 SpatialBytesWithField = FallbackData.GetAllocatedSizeBytes();
    FDWCEditorSurfaceOrientationField DetachedField =
        MoveTemp(FallbackData.SurfaceOrientationField);
    const uint64 SpatialBytesWithoutField = FallbackData.GetAllocatedSizeBytes();
    FallbackData.SurfaceOrientationField = MoveTemp(DetachedField);
    TestEqual(
        TEXT("Spatial memory accounting includes the exact sparse field allocation"),
        SpatialBytesWithField - SpatialBytesWithoutField,
        SparseBytes);
    return true;
}

#endif
