//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"

namespace DWCEditorIslandLocalGeodesicChartBuilderTestsPrivate
{
    uint64 MakeTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    void FinalizeTriangle(FDWCEditorSpatialTriangle& Triangle)
    {
        Triangle.LocalNormal = FVector3f::CrossProduct(
            Triangle.LocalPositions[1] - Triangle.LocalPositions[0],
            Triangle.LocalPositions[2] - Triangle.LocalPositions[0]).GetSafeNormal();
        Triangle.LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
        Triangle.LocalBitangent = FVector3f::CrossProduct(
            Triangle.LocalNormal, Triangle.LocalTangent).GetSafeNormal();
        Triangle.LocalSurfaceAxisU = Triangle.LocalTangent;
        Triangle.LocalSurfaceAxisV = Triangle.LocalBitangent;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Triangle.LocalNormals[Corner] = Triangle.LocalNormal;
            Triangle.LocalTangents[Corner] = Triangle.LocalTangent;
            Triangle.LocalBitangents[Corner] = Triangle.LocalBitangent;
            Triangle.LocalBounds += Triangle.LocalPositions[Corner];
            Triangle.UVBounds += Triangle.UVs[Corner];
        }
    }

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuildGrid(
        const int32 QuadsPerAxis,
        const bool bSplitFirstVerticalEdge,
        const bool bCurved)
    {
        TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
            MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
        Data->LODIndex = 0;
        Data->UVChannelIndex = 2;
        Data->MaterialSlotIndex = 4;
        int32 TriangleID = 100;
        auto Position = [=](const int32 X, const int32 Y)
        {
            const float Z = bCurved
                ? 0.12f * static_cast<float>(X * Y)
                : 0.0f;
            return FVector3f(static_cast<float>(X), static_cast<float>(Y), Z);
        };
        auto VertexID = [=](const int32 X, const int32 Y)
        {
            return static_cast<int64>(Y * (QuadsPerAxis + 1) + X);
        };
        auto AddTriangle = [&](const int32 X0, const int32 Y0,
                               const int32 X1, const int32 Y1,
                               const int32 X2, const int32 Y2,
                               const int32 IslandID,
                               const float UVOffset)
        {
            FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
            Triangle.MaterialSlotIndex = 4;
            Triangle.TriangleID = TriangleID++;
            Triangle.UVIslandID = IslandID;
            const int32 Xs[3] = {X0, X1, X2};
            const int32 Ys[3] = {Y0, Y1, Y2};
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                Triangle.LocalPositions[Corner] = Position(Xs[Corner], Ys[Corner]);
                Triangle.TopologyVertexIDs[Corner] = VertexID(Xs[Corner], Ys[Corner]);
                Triangle.UVs[Corner] = FVector2f(
                    UVOffset + static_cast<float>(Xs[Corner]) /
                        static_cast<float>(QuadsPerAxis) * 0.4f,
                    static_cast<float>(Ys[Corner]) /
                        static_cast<float>(QuadsPerAxis) * 0.4f);
            }
            FinalizeTriangle(Triangle);
        };

        for (int32 Y = 0; Y < QuadsPerAxis; ++Y)
        {
            for (int32 X = 0; X < QuadsPerAxis; ++X)
            {
                const bool bSecondIsland = bSplitFirstVerticalEdge && X > 0;
                const int32 IslandID = bSecondIsland ? 2 : 1;
                const float UVOffset = bSecondIsland ? 0.5f : 0.0f;
                AddTriangle(X, Y, X + 1, Y, X, Y + 1, IslandID, UVOffset);
                AddTriangle(X, Y + 1, X + 1, Y, X + 1, Y + 1, IslandID, UVOffset);
            }
        }
        FDWCEditorSpatialQueryService::BuildTriangleTopology(*Data);
        for (int32 TriangleIndex = 0; TriangleIndex < Data->Triangles.Num(); ++TriangleIndex)
        {
            const FDWCEditorSpatialTriangle& Triangle = Data->Triangles[TriangleIndex];
            Data->TriangleLookup.Add(
                MakeTriangleLookupKey(Triangle.MaterialSlotIndex, Triangle.TriangleID),
                TriangleIndex);
        }
        return Data;
    }

    FDWCEditorIslandLocalChartRequest MakeRequest(
        const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe>& Data)
    {
        FDWCEditorIslandLocalChartRequest Request;
        Request.SpatialHandle = Data;
        Request.MaterialSlotIndex = 4;
        Request.AnchorTriangleID = Data->Triangles[0].TriangleID;
        Request.AnchorBarycentric = FVector3f(0.34f, 0.33f, 0.33f);
        Request.SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
        Request.SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
        Request.GeodesicRadiusLocal = 10.0f;
        Request.NeighborhoodMarginLocal = 1.0f;
        return Request;
    }

    const FDWCEditorIslandLocalChartTriangle* FindTriangle(
        const FDWCEditorIslandLocalGeodesicChart& Chart,
        const int32 TriangleID)
    {
        return Chart.Triangles.FindByPredicate(
            [TriangleID](const FDWCEditorIslandLocalChartTriangle& Triangle)
            {
                return Triangle.TriangleID == TriangleID;
            });
    }

    int32 FindChartVertexIndex(
        const FDWCEditorIslandLocalGeodesicChart& Chart,
        const int64 TopologyVertexID)
    {
        return Chart.Vertices.IndexOfByPredicate(
            [TopologyVertexID](const FDWCEditorIslandLocalChartVertex& Vertex)
            {
                return Vertex.TopologyVertexID == TopologyVertexID;
            });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorIslandLocalChartSharedVertexTest,
    "DWC.Editor.Foundation.Spatial.IslandLocalChart.SharedVertexContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorIslandLocalChartSharedVertexTest::RunTest(const FString&)
{
    using namespace DWCEditorIslandLocalGeodesicChartBuilderTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildGrid(1, false, false);
    const FDWCEditorIslandLocalChartResult Result =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(MakeRequest(Data));
    TestTrue(TEXT("The planar island chart builds"), Result.IsSuccess());
    if (!Result.IsSuccess())
    {
        AddError(Result.Error);
        return false;
    }
    const FDWCEditorIslandLocalGeodesicChart& Chart = *Result.Chart;
    TestEqual(TEXT("Two triangles are emitted"), Chart.Triangles.Num(), 2);
    TestEqual(TEXT("Four physical vertices are stored once"), Chart.Vertices.Num(), 4);
    const FDWCEditorIslandLocalChartTriangle* A = FindTriangle(Chart, 100);
    const FDWCEditorIslandLocalChartTriangle* B = FindTriangle(Chart, 101);
    TestNotNull(TEXT("The anchor triangle exists"), A);
    TestNotNull(TEXT("The adjacent triangle exists"), B);
    if (A != nullptr && B != nullptr)
    {
        for (const int64 SharedID : {int64(1), int64(2)})
        {
            const int32 SharedIndex = FindChartVertexIndex(Chart, SharedID);
            TestTrue(TEXT("The shared topology vertex has one chart entry"), SharedIndex != INDEX_NONE);
            TestTrue(TEXT("The first triangle references the shared entry"),
                A->ChartVertexIndices[0] == SharedIndex ||
                A->ChartVertexIndices[1] == SharedIndex ||
                A->ChartVertexIndices[2] == SharedIndex);
            TestTrue(TEXT("The second triangle references the same shared entry"),
                B->ChartVertexIndices[0] == SharedIndex ||
                B->ChartVertexIndices[1] == SharedIndex ||
                B->ChartVertexIndices[2] == SharedIndex);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorIslandLocalChartSeamBoundaryTest,
    "DWC.Editor.Foundation.Spatial.IslandLocalChart.StopsAtUvSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorIslandLocalChartSeamBoundaryTest::RunTest(const FString&)
{
    using namespace DWCEditorIslandLocalGeodesicChartBuilderTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildGrid(2, true, false);
    const FDWCEditorIslandLocalChartResult Result =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(MakeRequest(Data));
    TestTrue(TEXT("The chart builds on the anchor island"), Result.IsSuccess());
    if (!Result.IsSuccess())
    {
        AddError(Result.Error);
        return false;
    }
    for (const FDWCEditorIslandLocalChartTriangle& Triangle : Result.Chart->Triangles)
    {
        TestEqual(TEXT("Every emitted triangle remains on the anchor island"),
            Data->Triangles[Triangle.SpatialTriangleIndex].UVIslandID, 1);
    }
    TestEqual(TEXT("Only the first grid column is emitted"), Result.Chart->Triangles.Num(), 4);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorIslandLocalChartCurvedLoopDeterminismTest,
    "DWC.Editor.Foundation.Spatial.IslandLocalChart.CurvedLoopDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorIslandLocalChartCurvedLoopDeterminismTest::RunTest(const FString&)
{
    using namespace DWCEditorIslandLocalGeodesicChartBuilderTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildGrid(2, false, true);
    const FDWCEditorIslandLocalChartRequest Request = MakeRequest(Data);
    const FDWCEditorIslandLocalChartResult First =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(Request);
    const FDWCEditorIslandLocalChartResult Second =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(Request);
    TestTrue(TEXT("The first curved chart builds"), First.IsSuccess());
    TestTrue(TEXT("The repeated curved chart builds"), Second.IsSuccess());
    if (!First.IsSuccess() || !Second.IsSuccess())
    {
        return false;
    }
    TestEqual(TEXT("Every grid triangle is represented"), First.Chart->Triangles.Num(), 8);
    TestEqual(TEXT("Every grid vertex is represented once"), First.Chart->Vertices.Num(), 9);
    TestEqual(TEXT("Repeated chart keeps vertex count"),
        Second.Chart->Vertices.Num(), First.Chart->Vertices.Num());
    for (int32 Index = 0; Index < First.Chart->Vertices.Num(); ++Index)
    {
        TestEqual(TEXT("Repeated chart keeps topology vertex order"),
            Second.Chart->Vertices[Index].TopologyVertexID,
            First.Chart->Vertices[Index].TopologyVertexID);
        TestTrue(TEXT("Repeated chart keeps exact shared coordinates"),
            Second.Chart->Vertices[Index].ChartCoordinate.Equals(
                First.Chart->Vertices[Index].ChartCoordinate, UE_SMALL_NUMBER));
    }
    TestTrue(TEXT("Curved loop closure candidates are measured"),
        First.Chart->Diagnostics.LoopClosureComparisonCount > 0);
    TestTrue(TEXT("Chart reports bounded working memory"),
        First.Chart->Diagnostics.PeakWorkingSetBytes > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorIslandLocalChartLifecycleTest,
    "DWC.Editor.Foundation.Spatial.IslandLocalChart.BudgetAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorIslandLocalChartLifecycleTest::RunTest(const FString&)
{
    using namespace DWCEditorIslandLocalGeodesicChartBuilderTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildGrid(2, false, false);
    FDWCEditorIslandLocalChartRequest TriangleBudgetRequest = MakeRequest(Data);
    TriangleBudgetRequest.MaxVisitedTriangles = 1;
    const FDWCEditorIslandLocalChartResult TriangleBudgetResult =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(TriangleBudgetRequest);
    TestEqual(TEXT("Triangle budget failure is explicit"),
        TriangleBudgetResult.Status,
        EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded);
    TestFalse(TEXT("Triangle budget failure publishes no partial chart"),
        TriangleBudgetResult.Chart.IsValid());

    FDWCEditorIslandLocalChartRequest MemoryBudgetRequest = MakeRequest(Data);
    MemoryBudgetRequest.MaxWorkingSetBytes = 1;
    const FDWCEditorIslandLocalChartResult MemoryBudgetResult =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(MemoryBudgetRequest);
    TestEqual(TEXT("Memory budget failure is explicit"),
        MemoryBudgetResult.Status,
        EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded);
    TestFalse(TEXT("Memory budget failure publishes no partial chart"),
        MemoryBudgetResult.Chart.IsValid());

    FDWCEditorCancellationToken CancellationToken;
    CancellationToken.Cancel();
    const FDWCEditorIslandLocalChartResult CanceledResult =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(
            MakeRequest(Data), &CancellationToken);
    TestEqual(TEXT("Canceled chart reports cancellation"),
        CanceledResult.Status, EDWCEditorIslandLocalChartStatus::Canceled);
    TestFalse(TEXT("Canceled chart publishes no partial result"),
        CanceledResult.Chart.IsValid());
    return true;
}

#endif
