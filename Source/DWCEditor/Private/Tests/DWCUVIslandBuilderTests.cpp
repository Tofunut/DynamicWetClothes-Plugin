#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Common/UV/DWCUVIslandBuilder.h"

namespace DWCUVIslandBuilderTestsPrivate
{
    FDWCUVIslandBuildTriangle MakeTriangle(
        const int32 TriangleID,
        const int32 MaterialSlotIndex,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C)
    {
        FDWCUVIslandBuildTriangle Triangle;
        Triangle.TriangleID = TriangleID;
        Triangle.MaterialSlotIndex = MaterialSlotIndex;
        Triangle.UVs[0] = A;
        Triangle.UVs[1] = B;
        Triangle.UVs[2] = C;
        return Triangle;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCUVIslandBuilderSharedEdgeTest,
    "DWC.UV.OriginalIslandBuilder.SharedEdgeConnects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCUVIslandBuilderSharedEdgeTest::RunTest(const FString& Parameters)
{
    using namespace DWCUVIslandBuilderTestsPrivate;

    TArray<FDWCUVIslandBuildTriangle> Triangles;
    Triangles.Add(MakeTriangle(0, 0, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}));
    Triangles.Add(MakeTriangle(1, 0, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}));

    TArray<FDWCOriginalUVIslandBuildResult> Islands;
    FDWCUVIslandBuilder::Build(Triangles, Islands);

    TestEqual(TEXT("Shared UV edge forms one island"), Islands.Num(), 1);
    if (Islands.Num() == 1)
    {
        TestEqual(TEXT("Island contains both triangles"), Islands[0].TriangleIDs.Num(), 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCUVIslandBuilderStackedUVTest,
    "DWC.UV.OriginalIslandBuilder.StackedUVConnectsAll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCUVIslandBuilderStackedUVTest::RunTest(const FString& Parameters)
{
    using namespace DWCUVIslandBuilderTestsPrivate;

    TArray<FDWCUVIslandBuildTriangle> Triangles;
    // Two fully stacked copies, such as left/right sleeves sharing the same UV coordinates.
    for (int32 CopyIndex = 0; CopyIndex < 2; ++CopyIndex)
    {
        const int32 BaseID = CopyIndex * 2;
        Triangles.Add(MakeTriangle(BaseID, 0, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}));
        Triangles.Add(MakeTriangle(BaseID + 1, 0, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}));
    }

    TArray<FDWCOriginalUVIslandBuildResult> Islands;
    FDWCUVIslandBuilder::Build(Triangles, Islands);

    TestEqual(TEXT("Stacked UV copies remain one logical Original UV island"), Islands.Num(), 1);
    if (Islands.Num() == 1)
    {
        TestEqual(TEXT("Stacked island contains all triangles"), Islands[0].TriangleIDs.Num(), 4);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCUVIslandBuilderMaterialSlotIsolationTest,
    "DWC.UV.OriginalIslandBuilder.MaterialSlotsAreIsolated",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCUVIslandBuilderMaterialSlotIsolationTest::RunTest(const FString& Parameters)
{
    using namespace DWCUVIslandBuilderTestsPrivate;

    TArray<FDWCUVIslandBuildTriangle> Triangles;
    Triangles.Add(MakeTriangle(0, 0, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}));
    Triangles.Add(MakeTriangle(1, 1, {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}));

    TArray<FDWCOriginalUVIslandBuildResult> Islands;
    FDWCUVIslandBuilder::Build(Triangles, Islands);

    TestEqual(TEXT("Identical UVs in different material slots remain separate"), Islands.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCUVIslandBuilderSeparatedUVTest,
    "DWC.UV.OriginalIslandBuilder.SeparatedUVsRemainSeparate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCUVIslandBuilderSeparatedUVTest::RunTest(const FString& Parameters)
{
    using namespace DWCUVIslandBuilderTestsPrivate;

    TArray<FDWCUVIslandBuildTriangle> Triangles;
    Triangles.Add(MakeTriangle(0, 0, {0.0, 0.0}, {0.2, 0.0}, {0.0, 0.2}));
    Triangles.Add(MakeTriangle(1, 0, {0.8, 0.8}, {1.0, 0.8}, {0.8, 1.0}));

    TArray<FDWCOriginalUVIslandBuildResult> Islands;
    FDWCUVIslandBuilder::Build(Triangles, Islands);

    TestEqual(TEXT("UV triangles without a shared edge remain separate"), Islands.Num(), 2);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
