//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"

namespace DWCEditorSurfacePatchProjectorTestsPrivate
{
    uint64 MakeTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    void FinalizeTriangle(FDWCEditorSpatialTriangle& Triangle)
    {
        Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
        Triangle.LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
        Triangle.LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
        Triangle.LocalSurfaceAxisU = FVector3f(1.0f, 0.0f, 0.0f);
        Triangle.LocalSurfaceAxisV = FVector3f(0.0f, 1.0f, 0.0f);
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.LocalTangents[CornerIndex] = Triangle.LocalTangent;
            Triangle.LocalBitangents[CornerIndex] = Triangle.LocalBitangent;
            Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
            Triangle.UVBounds += Triangle.UVs[CornerIndex];
        }
    }

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuildTwoTriangleSurface(const bool bWithNeighbor)
    {
        TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
            MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
        Data->LODIndex = 0;
        Data->UVChannelIndex = 2;
        Data->MaterialSlotIndex = 4;

        FDWCEditorSpatialTriangle& TriangleA = Data->Triangles.AddDefaulted_GetRef();
        TriangleA.MaterialSlotIndex = 4;
        TriangleA.TriangleID = 10;
        TriangleA.UVIslandID = 1;
        TriangleA.LocalPositions[0] = FVector3f(0.0f, 0.0f, 0.0f);
        TriangleA.LocalPositions[1] = FVector3f(1.0f, 0.0f, 0.0f);
        TriangleA.LocalPositions[2] = FVector3f(0.0f, 1.0f, 0.0f);
        TriangleA.UVs[0] = FVector2f(0.0f, 0.0f);
        TriangleA.UVs[1] = FVector2f(0.4f, 0.0f);
        TriangleA.UVs[2] = FVector2f(0.0f, 0.4f);
        TriangleA.TopologyVertexIDs[0] = 0;
        TriangleA.TopologyVertexIDs[1] = 1;
        TriangleA.TopologyVertexIDs[2] = 2;
        FinalizeTriangle(TriangleA);

        if (bWithNeighbor)
        {
            FDWCEditorSpatialTriangle& TriangleB = Data->Triangles.AddDefaulted_GetRef();
            TriangleB.MaterialSlotIndex = 4;
            TriangleB.TriangleID = 11;
            TriangleB.UVIslandID = 2;
            TriangleB.LocalPositions[0] = FVector3f(0.0f, 1.0f, 0.0f);
            TriangleB.LocalPositions[1] = FVector3f(1.0f, 0.0f, 0.0f);
            TriangleB.LocalPositions[2] = FVector3f(1.0f, 1.0f, 0.0f);
            TriangleB.UVs[0] = FVector2f(0.6f, 1.0f);
            TriangleB.UVs[1] = FVector2f(1.0f, 0.6f);
            TriangleB.UVs[2] = FVector2f(1.0f, 1.0f);
            TriangleB.TopologyVertexIDs[0] = 2;
            TriangleB.TopologyVertexIDs[1] = 1;
            TriangleB.TopologyVertexIDs[2] = 3;
            FinalizeTriangle(TriangleB);
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

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuildDepthCrossingTriangle()
    {
        TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
            MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
        Data->LODIndex = 0;
        Data->UVChannelIndex = 2;
        Data->MaterialSlotIndex = 4;

        FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
        Triangle.MaterialSlotIndex = 4;
        Triangle.TriangleID = 20;
        Triangle.UVIslandID = 1;
        Triangle.LocalPositions[0] = FVector3f(-1.0f, -1.0f, -1.0f);
        Triangle.LocalPositions[1] = FVector3f(1.0f, -1.0f, 1.0f);
        Triangle.LocalPositions[2] = FVector3f(0.0f, 1.0f, 1.0f);
        Triangle.UVs[0] = FVector2f(0.05f, 0.05f);
        Triangle.UVs[1] = FVector2f(0.95f, 0.05f);
        Triangle.UVs[2] = FVector2f(0.5f, 0.95f);
        Triangle.TopologyVertexIDs[0] = 20;
        Triangle.TopologyVertexIDs[1] = 21;
        Triangle.TopologyVertexIDs[2] = 22;
        FinalizeTriangle(Triangle);

        FDWCEditorSpatialQueryService::BuildTriangleTopology(*Data);
        Data->TriangleLookup.Add(MakeTriangleLookupKey(4, 20), 0);
        return Data;
    }

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuildLargeSeamedGrid(
        const int32 QuadsPerAxis = 24,
        const int32 ColumnsPerIsland = 4)
    {
        TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
            MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
        Data->LODIndex = 0;
        Data->UVChannelIndex = 2;
        Data->MaterialSlotIndex = 4;
        Data->Triangles.Reserve(QuadsPerAxis * QuadsPerAxis * 2);

        const int32 IslandCount = FMath::DivideAndRoundUp(QuadsPerAxis, ColumnsPerIsland);
        const float IslandPitch = 1.0f / static_cast<float>(IslandCount);
        const float IslandWidth = IslandPitch * 0.75f;
        const float VMin = 0.05f;
        const float VRange = 0.90f;
        auto VertexID = [QuadsPerAxis](const int32 X, const int32 Y)
        {
            return static_cast<int64>(Y * (QuadsPerAxis + 1) + X);
        };
        auto Position = [QuadsPerAxis](const int32 X, const int32 Y)
        {
            return FVector3f(
                static_cast<float>(X) / static_cast<float>(QuadsPerAxis),
                static_cast<float>(Y) / static_cast<float>(QuadsPerAxis),
                0.0f);
        };

        int32 TriangleID = 1000;
        for (int32 Y = 0; Y < QuadsPerAxis; ++Y)
        {
            for (int32 X = 0; X < QuadsPerAxis; ++X)
            {
                const int32 IslandID = X / ColumnsPerIsland;
                const int32 IslandStartX = IslandID * ColumnsPerIsland;
                const int32 IslandColumnCount =
                    FMath::Min(ColumnsPerIsland, QuadsPerAxis - IslandStartX);
                auto UV = [=](const int32 VertexX, const int32 VertexY)
                {
                    const float LocalU = static_cast<float>(VertexX - IslandStartX) /
                        static_cast<float>(IslandColumnCount);
                    return FVector2f(
                        IslandPitch * static_cast<float>(IslandID) + LocalU * IslandWidth,
                        VMin + static_cast<float>(VertexY) /
                            static_cast<float>(QuadsPerAxis) * VRange);
                };
                auto AddTriangle = [&](const int32 X0, const int32 Y0,
                                       const int32 X1, const int32 Y1,
                                       const int32 X2, const int32 Y2)
                {
                    FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
                    Triangle.MaterialSlotIndex = 4;
                    Triangle.TriangleID = TriangleID++;
                    Triangle.UVIslandID = IslandID;
                    Triangle.LocalPositions[0] = Position(X0, Y0);
                    Triangle.LocalPositions[1] = Position(X1, Y1);
                    Triangle.LocalPositions[2] = Position(X2, Y2);
                    Triangle.UVs[0] = UV(X0, Y0);
                    Triangle.UVs[1] = UV(X1, Y1);
                    Triangle.UVs[2] = UV(X2, Y2);
                    Triangle.TopologyVertexIDs[0] = VertexID(X0, Y0);
                    Triangle.TopologyVertexIDs[1] = VertexID(X1, Y1);
                    Triangle.TopologyVertexIDs[2] = VertexID(X2, Y2);
                    FinalizeTriangle(Triangle);
                };
                AddTriangle(X, Y, X + 1, Y, X, Y + 1);
                AddTriangle(X, Y + 1, X + 1, Y, X + 1, Y + 1);
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

    FDWCEditorSurfacePatchProjectionRequest MakeLargeRequest(
        const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe>& Data,
        const float RotationRadians = 0.2f)
    {
        FDWCEditorSurfacePatchProjectionRequest Request;
        Request.SpatialHandle = Data;
        Request.MaterialSlotIndex = 4;
        const int32 CenterQuad = 12 * 24 + 12;
        Request.AnchorTriangleID = 1000 + CenterQuad * 2;
        Request.AnchorBarycentric = FVector3f(0.34f, 0.33f, 0.33f);
        Request.SurfaceHalfExtentLocal = FVector2f(0.49f, 0.46f);
        Request.RotationRadians = RotationRadians;
        Request.BoundaryPolicy =
            EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
        return Request;
    }

    FDWCEditorSurfacePatchProjectionRequest MakeRequest(
        const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe>& Data)
    {
        FDWCEditorSurfacePatchProjectionRequest Request;
        Request.SpatialHandle = Data;
        Request.MaterialSlotIndex = 4;
        Request.AnchorTriangleID = 10;
        Request.AnchorBarycentric = FVector3f(0.1f, 0.45f, 0.45f);
        // An anisotropic footprint verifies that physical unfolding happens before
        // source-coordinate normalization.
        Request.SurfaceHalfExtentLocal = FVector2f(0.6f, 0.3f);
        Request.BoundaryPolicy =
            EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
        return Request;
    }

    const FDWCEditorSurfacePatchFragment* FindFragment(
        const FDWCEditorSurfacePatchProjectionResult& Result,
        const int32 TriangleID)
    {
        return Result.Fragments.FindByPredicate(
            [TriangleID](const FDWCEditorSurfacePatchFragment& Fragment)
            {
                return Fragment.TriangleID == TriangleID;
            });
    }

    FVector2f FindCoordinateForTopologyVertex(
        const FDWCEditorSpatialTriangle& Triangle,
        const FDWCEditorSurfacePatchFragment& Fragment,
        const int64 TopologyVertexID)
    {
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            if (Triangle.TopologyVertexIDs[CornerIndex] == TopologyVertexID)
            {
                return Fragment.PatchCoordinates[CornerIndex];
            }
        }
        return FVector2f(TNumericLimits<float>::Max(), TNumericLimits<float>::Max());
    }

    float FindDepthForTopologyVertex(
        const FDWCEditorSpatialTriangle& Triangle,
        const FDWCEditorSurfacePatchFragment& Fragment,
        const int64 TopologyVertexID)
    {
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            if (Triangle.TopologyVertexIDs[CornerIndex] == TopologyVertexID)
            {
                return Fragment.SignedProjectionDepth[CornerIndex];
            }
        }
        return TNumericLimits<float>::Max();
    }

    FDWCEditorNormalSourceSnapshot MakeNormalSource()
    {
        FDWCEditorNormalSourceSnapshot Source;
        Source.Texture.Width = 1;
        Source.Texture.Height = 1;
        Source.Texture.BytesPerPixel = sizeof(FColor);
        Source.Texture.bSRGB = false;
        Source.Texture.Format = TSF_BGRA8;
        Source.Texture.RawData = MakeShared<TArray64<uint8>>();
        Source.Texture.RawData->SetNumUninitialized(sizeof(FColor));
        const FColor Pixel(220, 128, 220, 255);
        FMemory::Memcpy(Source.Texture.RawData->GetData(), &Pixel, sizeof(FColor));
        return Source;
    }

    FDWCEditorScalarSourceSnapshot MakeCoverageSource()
    {
        FDWCEditorScalarSourceSnapshot Source;
        Source.Size = FIntPoint(1, 1);
        TArray<float> Values;
        Values.Add(0.75f);
        Source.Values = MakeShared<const TArray<float>, ESPMode::ThreadSafe>(MoveTemp(Values));
        return Source;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceDecalProjectionTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.DecalCrossesUvSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceDecalProjectionTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(true);
    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);
    Request.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    Request.ProjectionDepthLocal = 0.25f;
    Request.MaxSurfaceAngleDegrees = 70.0f;
    Request.ProjectionDepthSoftness = 0.2f;
    Request.ProjectionAngleSoftness = 0.1f;

    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);
    TestTrue(TEXT("The surface decal projection succeeds"), Result.IsSuccess());
    TestEqual(TEXT("The decal emits one fragment on each UV island"), Result.Fragments.Num(), 2);
    TestEqual(TEXT("The decal records both affected UV islands"), Result.AffectedUVIslandIDs.Num(), 2);
    TestTrue(TEXT("The decal crosses the physical UV seam"), Result.TraversedSeamCount > 0);

    const FDWCEditorSurfacePatchFragment* FragmentA = FindFragment(Result, 10);
    const FDWCEditorSurfacePatchFragment* FragmentB = FindFragment(Result, 11);
    TestNotNull(TEXT("The decal anchor fragment exists"), FragmentA);
    TestNotNull(TEXT("The decal seam fragment exists"), FragmentB);
    if (FragmentA != nullptr && FragmentB != nullptr)
    {
        for (const int64 SharedVertexID : { int64(1), int64(2) })
        {
            const FVector2f CoordinateA =
                FindCoordinateForTopologyVertex(Data->Triangles[0], *FragmentA, SharedVertexID);
            const FVector2f CoordinateB =
                FindCoordinateForTopologyVertex(Data->Triangles[1], *FragmentB, SharedVertexID);
            TestTrue(TEXT("Decal coordinates are continuous across the physical seam"),
                CoordinateA.Equals(CoordinateB, UE_KINDA_SMALL_NUMBER));
            TestTrue(TEXT("Decal depth is continuous across the physical seam"),
                FMath::IsNearlyEqual(
                    FindDepthForTopologyVertex(Data->Triangles[0], *FragmentA, SharedVertexID),
                    FindDepthForTopologyVertex(Data->Triangles[1], *FragmentB, SharedVertexID),
                    UE_KINDA_SMALL_NUMBER));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceDecalInteriorIntersectionTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.DecalInteriorIntersection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceDecalInteriorIntersectionTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildDepthCrossingTriangle();
    FDWCEditorSurfaceNormalPatchInput Input;
    Input.Projection.SpatialHandle = Data;
    Input.Projection.MaterialSlotIndex = 4;
    Input.Projection.AnchorTriangleID = 20;
    Input.Projection.AnchorBarycentric = FVector3f(0.5f, 0.25f, 0.25f);
    Input.Projection.SurfaceHalfExtentLocal = FVector2f(0.35f, 0.35f);
    Input.Projection.ProjectionDepthLocal = 0.25f;
    Input.Projection.MaxSurfaceAngleDegrees = 70.0f;
    Input.Projection.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    Input.Projection.bCollectDetailedDiagnostics = true;
    Input.NormalSource = MakeNormalSource();
    Input.CoverageSource = MakeCoverageSource();
    Input.Strength = 1.0f;

    const FDWCEditorSurfacePatchProjectionResult Projection =
        FDWCEditorSurfacePatchProjector::Project(Input.Projection);
    TestTrue(TEXT("A triangle whose interior crosses the decal volume is projected"),
        Projection.IsSuccess());
    TestEqual(TEXT("The interior-only triangle emits one fragment"),
        Projection.Fragments.Num(), 1);
    TestEqual(TEXT("The diagnostic records an interior footprint candidate"),
        Projection.Diagnostics.InteriorFootprintCandidateCount, 1);
    TestEqual(TEXT("The diagnostic records an interior depth candidate"),
        Projection.Diagnostics.InteriorDepthCandidateCount, 1);

    FDWCEditorProjectedNormalPatchCommand Command;
    FString Error;
    TestTrue(TEXT("The interior projection builds a shared raster command"),
        FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            Input, Command, &Error));
    TestTrue(TEXT("The raster command enables exact surface filtering"),
        Command.bUseSurfaceProjectionFilter);
    TestTrue(TEXT("The raster command preserves projection depth"),
        FMath::IsNearlyEqual(Command.ProjectionDepthLocal, 0.25f));

    FDWCEditorNormalRasterSurface Surface;
    TestTrue(TEXT("The exact-filter raster surface initializes"),
        Surface.Initialize(FIntPoint(128, 128), true));
    FDWCEditorProjectedRasterDiagnostics RasterDiagnostics;
    const FDWCEditorRasterResult RasterResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
            Command, Surface, nullptr, nullptr, &RasterDiagnostics);
    TestTrue(TEXT("The triangle interior contributes pixels"), RasterResult.bAffectedPixels);
    TestTrue(TEXT("Per-texel depth filtering rejects pixels outside the depth slab"),
        RasterResult.AffectedPixelCount > 0 &&
        static_cast<uint64>(RasterResult.AffectedPixelCount) <
            RasterDiagnostics.CandidatePixelCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceDecalPerTexelAngleTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.DecalPerTexelAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceDecalPerTexelAngleTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    FDWCEditorProjectedNormalPatchCommand Command;
    FDWCEditorSurfacePatchFragment& Fragment = Command.Fragments.AddDefaulted_GetRef();
    Fragment.TargetUVs[0] = FVector2f(0.0f, 0.0f);
    Fragment.TargetUVs[1] = FVector2f(1.0f, 0.0f);
    Fragment.TargetUVs[2] = FVector2f(0.0f, 1.0f);
    Fragment.PatchCoordinates[0] = FVector2f(-0.5f, -0.5f);
    Fragment.PatchCoordinates[1] = FVector2f(0.5f, -0.5f);
    Fragment.PatchCoordinates[2] = FVector2f(-0.5f, 0.5f);
    Fragment.TargetUVBounds += Fragment.TargetUVs[0];
    Fragment.TargetUVBounds += Fragment.TargetUVs[1];
    Fragment.TargetUVBounds += Fragment.TargetUVs[2];
    const float TiltRadians = FMath::DegreesToRadians(80.0f);
    const FVector3f TiltedNormal(FMath::Sin(TiltRadians), 0.0f, FMath::Cos(TiltRadians));
    Fragment.SurfaceNormalInProjectorSpace[0] = FVector3f(0.0f, 0.0f, 1.0f);
    Fragment.SurfaceNormalInProjectorSpace[1] = TiltedNormal;
    Fragment.SurfaceNormalInProjectorSpace[2] = TiltedNormal;

    Command.NormalSource = MakeNormalSource();
    Command.CoverageSource = MakeCoverageSource();
    Command.Strength = 1.0f;
    Command.bUseSurfaceProjectionFilter = true;
    Command.ProjectionDepthLocal = 1.0f;
    Command.MaxSurfaceAngleRadians = FMath::DegreesToRadians(45.0f);
    Command.ProjectionAngleSoftness = 0.0f;

    FDWCEditorNormalRasterSurface Surface;
    TestTrue(TEXT("The angle-filter raster surface initializes"),
        Surface.Initialize(FIntPoint(64, 64), true));
    const FDWCEditorRasterResult Result =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(Command, Surface);
    TestTrue(TEXT("The aligned part of the triangle is rasterized"), Result.bAffectedPixels);
    TestTrue(TEXT("A texel near the aligned corner keeps wrinkle coverage"),
        Surface.Coverage[8 * Surface.Size.X + 8] > 0.0f);
    TestTrue(TEXT("A texel near the over-angle corner is rejected"),
        Surface.Coverage[5 * Surface.Size.X + 50] <= UE_SMALL_NUMBER);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchSeamProjectionTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.CrossesUvSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchSeamProjectionTest::RunTest(const FString& Parameters)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(true);
    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);
    Request.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);
    const FDWCEditorSurfacePatchProjectionResult RepeatedResult =
        FDWCEditorSurfacePatchProjector::Project(Request);

    TestTrue(TEXT("Projection succeeds across a physical UV seam"), Result.IsSuccess());
    TestEqual(TEXT("Both physical triangles produce fragments"), Result.Fragments.Num(), 2);
    TestEqual(TEXT("Both UV islands are reported"), Result.AffectedUVIslandIDs.Num(), 2);
    TestTrue(TEXT("The traversal records crossing a UV seam"), Result.TraversedSeamCount > 0);
    TestEqual(
        TEXT("Repeated projection keeps deterministic fragment count"),
        RepeatedResult.Fragments.Num(),
        Result.Fragments.Num());
    for (int32 FragmentIndex = 0;
         FragmentIndex < FMath::Min(Result.Fragments.Num(), RepeatedResult.Fragments.Num());
         ++FragmentIndex)
    {
        TestEqual(
            TEXT("Repeated projection keeps deterministic triangle order"),
            RepeatedResult.Fragments[FragmentIndex].TriangleID,
            Result.Fragments[FragmentIndex].TriangleID);
    }

    const FDWCEditorSurfacePatchFragment* FragmentA = FindFragment(Result, 10);
    const FDWCEditorSurfacePatchFragment* FragmentB = FindFragment(Result, 11);
    TestNotNull(TEXT("The anchor triangle fragment exists"), FragmentA);
    TestNotNull(TEXT("The adjacent seam triangle fragment exists"), FragmentB);
    if (FragmentA != nullptr && FragmentB != nullptr)
    {
        for (const int64 SharedVertexID : { int64(1), int64(2) })
        {
            const FVector2f CoordinateA =
                FindCoordinateForTopologyVertex(Data->Triangles[0], *FragmentA, SharedVertexID);
            const FVector2f CoordinateB =
                FindCoordinateForTopologyVertex(Data->Triangles[1], *FragmentB, SharedVertexID);
            TestTrue(
                TEXT("Shared physical vertices keep continuous patch coordinates across the seam"),
                CoordinateA.Equals(CoordinateB, UE_KINDA_SMALL_NUMBER));
        }
        TestTrue(
            TEXT("The seam fragment keeps a usable tangent-space U transform"),
            !FragmentB->PatchAxisUInTargetTangent[0].IsNearlyZero());
        TestTrue(
            TEXT("The seam fragment keeps a usable tangent-space V transform"),
            !FragmentB->PatchAxisVInTargetTangent[0].IsNearlyZero());
        const FVector2f AdjacentThirdCoordinate =
            FindCoordinateForTopologyVertex(Data->Triangles[1], *FragmentB, 3);
        TestTrue(
            TEXT("Anisotropic footprint normalization preserves the physically unfolded third vertex"),
            AdjacentThirdCoordinate.Equals(FVector2f(0.55f / 0.6f, 0.55f / 0.3f), 0.001f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchStopsAtUvSeamTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.StopsAtUvSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchStopsAtUvSeamTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(true);
    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);

    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);
    TestTrue(TEXT("Non-UV-seam unfolding succeeds on the anchor island"), Result.IsSuccess());
    TestEqual(TEXT("Non-UV-seam unfolding emits only the anchor triangle"), Result.Fragments.Num(), 1);
    TestEqual(TEXT("Non-UV-seam unfolding reports only the anchor island"), Result.AffectedUVIslandIDs.Num(), 1);
    TestEqual(TEXT("Non-UV-seam unfolding does not traverse a UV seam"), Result.TraversedSeamCount, 0);
    TestNotNull(TEXT("The anchor triangle remains projected"), FindFragment(Result, 10));
    TestNull(TEXT("The seam-adjacent triangle is excluded"), FindFragment(Result, 11));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceDecalBoundaryPolicyTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.DecalBoundaryPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceDecalBoundaryPolicyTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildTwoTriangleSurface(true);

    FDWCEditorSurfacePatchProjectionRequest AnchorIslandRequest = MakeRequest(Data);
    AnchorIslandRequest.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
    const FDWCEditorSurfacePatchProjectionResult AnchorIslandResult =
        FDWCEditorSurfacePatchProjector::Project(AnchorIslandRequest);
    TestTrue(TEXT("The anchor-island decal projection succeeds"),
        AnchorIslandResult.IsSuccess());
    TestEqual(TEXT("The anchor-island decal emits only the anchor triangle"),
        AnchorIslandResult.Fragments.Num(), 1);
    TestEqual(TEXT("The anchor-island decal reports one affected island"),
        AnchorIslandResult.AffectedUVIslandIDs.Num(), 1);
    TestEqual(TEXT("The anchor-island decal traverses no UV seam"),
        AnchorIslandResult.TraversedSeamCount, 0);
    TestNull(TEXT("The seam-adjacent triangle is excluded by policy"),
        FindFragment(AnchorIslandResult, 11));

    FDWCEditorSurfacePatchProjectionRequest CrossSeamRequest = AnchorIslandRequest;
    CrossSeamRequest.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    const FDWCEditorSurfacePatchProjectionResult CrossSeamResult =
        FDWCEditorSurfacePatchProjector::Project(CrossSeamRequest);
    TestTrue(TEXT("The cross-seam decal projection succeeds"), CrossSeamResult.IsSuccess());
    TestEqual(TEXT("The cross-seam decal emits both physical triangles"),
        CrossSeamResult.Fragments.Num(), 2);
    TestEqual(TEXT("The cross-seam decal reports both UV islands"),
        CrossSeamResult.AffectedUVIslandIDs.Num(), 2);
    TestTrue(TEXT("The cross-seam decal records a seam traversal"),
        CrossSeamResult.TraversedSeamCount > 0);

    FDWCEditorSurfacePatchProjectionCacheService Cache(8ull * 1024ull * 1024ull);
    FDWCEditorSurfacePatchProjectionLease AnchorLease;
    FDWCEditorSurfacePatchProjectionLease CrossLease;
    FString Error;
    TestTrue(TEXT("The anchor-island decal enters the projection cache"),
        Cache.Resolve(
            AnchorIslandRequest,
            EDWCEditorSurfacePatchCachePolicy::Persistent,
            AnchorLease,
            &Error));
    TestTrue(TEXT("The cross-seam decal enters a distinct projection cache entry"),
        Cache.Resolve(
            CrossSeamRequest,
            EDWCEditorSurfacePatchCachePolicy::Persistent,
            CrossLease,
            &Error));
    TestTrue(TEXT("Boundary policies do not alias immutable projection geometry"),
        AnchorLease.Get() != CrossLease.Get());
    TestEqual(TEXT("The cache owns one geometry entry per boundary policy"),
        Cache.GetDiagnostics().EntryCount, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionContractTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.BoundaryContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionContractTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildTwoTriangleSurface(true);

    FDWCEditorSurfacePatchProjectionRequest InvalidPolicyRequest = MakeRequest(Data);
    InvalidPolicyRequest.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::Invalid;
    FString Error;
    TestFalse(TEXT("An unspecified boundary policy is rejected"),
        FDWCEditorSurfacePatchProjector::ValidateProjectionContract(
            InvalidPolicyRequest, &Error));
    TestFalse(TEXT("An unspecified boundary policy cannot project"),
        FDWCEditorSurfacePatchProjector::Project(InvalidPolicyRequest).IsSuccess());

    FDWCEditorSurfacePatchProjectionRequest CrossSeamRequest = MakeRequest(Data);
    CrossSeamRequest.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    TestTrue(TEXT("The decal contract accepts cross-seam traversal"),
        FDWCEditorSurfacePatchProjector::ValidateProjectionContract(
            CrossSeamRequest, &Error));
    TestTrue(TEXT("The cross-seam boundary contract projects"),
        FDWCEditorSurfacePatchProjector::Project(CrossSeamRequest).IsSuccess());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchRenderTangentReorientationTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.RenderTangentReorientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchRenderTangentReorientationTest::RunTest(const FString& Parameters)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildTwoTriangleSurface(true);

    FDWCEditorSpatialTriangle& RotatedTangentTriangle = Data->Triangles[1];
    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
    {
        RotatedTangentTriangle.LocalTangents[CornerIndex] = FVector3f(0.0f, 1.0f, 0.0f);
        RotatedTangentTriangle.LocalBitangents[CornerIndex] = FVector3f(-1.0f, 0.0f, 0.0f);
    }

    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);
    Request.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);
    const FDWCEditorSurfacePatchFragment* FragmentA = FindFragment(Result, 10);
    const FDWCEditorSurfacePatchFragment* FragmentB = FindFragment(Result, 11);
    TestNotNull(TEXT("The anchor fragment exists"), FragmentA);
    TestNotNull(TEXT("The rotated-tangent seam fragment exists"), FragmentB);
    if (FragmentA == nullptr || FragmentB == nullptr)
    {
        return false;
    }

    const FVector2f AxisA = FragmentA->PatchAxisUInTargetTangent[0].GetSafeNormal();
    const FVector2f AxisB = FragmentB->PatchAxisUInTargetTangent[0].GetSafeNormal();
    const FVector3f PhysicalAxisA =
        Data->Triangles[0].LocalTangents[0] * AxisA.X +
        Data->Triangles[0].LocalBitangents[0] * AxisA.Y;
    const FVector3f PhysicalAxisB =
        Data->Triangles[1].LocalTangents[0] * AxisB.X +
        Data->Triangles[1].LocalBitangents[0] * AxisB.Y;
    TestTrue(
        TEXT("Patch normal direction stays continuous when render tangent rotates across a UV seam"),
        PhysicalAxisA.Equals(PhysicalAxisB, UE_KINDA_SMALL_NUMBER));
    TestFalse(
        TEXT("The stored tangent-space transform changes for the rotated render tangent"),
        AxisA.Equals(AxisB, UE_KINDA_SMALL_NUMBER));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchSharedRasterCommandTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.SharedRasterCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchSharedRasterCommandTest::RunTest(const FString& Parameters)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(true);
    FDWCEditorSurfaceNormalPatchInput Input;
    Input.Projection = MakeRequest(Data);
    Input.Projection.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    Input.NormalSource = MakeNormalSource();
    Input.CoverageSource = MakeCoverageSource();
    Input.Strength = 1.0f;
    Input.Falloff = 0.2f;

    FDWCEditorProjectedNormalPatchCommand Command;
    FString Error;
    TestTrue(
        TEXT("The shared preview/bake builder creates a projected command"),
        FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(Input, Command, &Error));
    TestTrue(TEXT("The projected command has no builder error"), Error.IsEmpty());
    TestEqual(TEXT("The command preserves both seam fragments"), Command.GetFragments().Num(), 2);
    TestTrue(TEXT("The command preserves the bake coverage source"), Command.CoverageSource.IsValid());

    FDWCEditorNormalRasterSurface Surface;
    TestTrue(TEXT("The bake-style raster surface initializes"), Surface.Initialize(FIntPoint(64, 64), true));
    const FDWCEditorRasterResult RasterResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(Command, Surface);
    TestTrue(TEXT("The seam-aware command affects bake pixels"), RasterResult.bAffectedPixels);

    bool bLeftIslandCovered = false;
    bool bRightIslandCovered = false;
    for (int32 Y = 0; Y < Surface.Size.Y; ++Y)
    {
        for (int32 X = 0; X < Surface.Size.X; ++X)
        {
            if (Surface.Coverage[Y * Surface.Size.X + X] <= 0.0f)
            {
                continue;
            }
            bLeftIslandCovered |= X < Surface.Size.X / 2;
            bRightIslandCovered |= X >= Surface.Size.X / 2;
        }
    }
    TestTrue(TEXT("Coverage reaches the anchor UV island"), bLeftIslandCovered);
    TestTrue(TEXT("Coverage reaches the seam-adjacent UV island"), bRightIslandCovered);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchBoundaryProjectionTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.StopsAtBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchBoundaryProjectionTest::RunTest(const FString& Parameters)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(false);
    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);
    Request.SurfaceHalfExtentLocal = FVector2f(10.0f, 10.0f);
    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);

    TestTrue(TEXT("Projection remains valid at an open mesh boundary"), Result.IsSuccess());
    TestEqual(TEXT("A boundary does not invent an adjacent surface"), Result.Fragments.Num(), 1);
    TestEqual(TEXT("Only the anchor triangle is visited"), Result.VisitedTriangleCount, 1);

    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BlockedData =
        BuildTwoTriangleSurface(true);
    BlockedData->Triangles[0].AdjacentTriangleIndices[1] = INDEX_NONE;
    BlockedData->Triangles[0].EdgeTypes[1] = EDWCEditorSpatialEdgeType::Blocked;
    BlockedData->Triangles[1].AdjacentTriangleIndices[0] = INDEX_NONE;
    BlockedData->Triangles[1].EdgeTypes[0] = EDWCEditorSpatialEdgeType::Blocked;
    const FDWCEditorSurfacePatchProjectionResult BlockedResult =
        FDWCEditorSurfacePatchProjector::Project(MakeRequest(BlockedData));
    TestTrue(TEXT("Projection remains valid beside a blocked edge"), BlockedResult.IsSuccess());
    TestEqual(TEXT("A blocked edge is never traversed"), BlockedResult.Fragments.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchBudgetAndCancellationTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.BudgetAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchBudgetAndCancellationTest::RunTest(const FString& Parameters)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildTwoTriangleSurface(true);
    FDWCEditorSurfacePatchProjectionRequest BudgetRequest = MakeRequest(Data);
    BudgetRequest.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    BudgetRequest.MaxVisitedTriangles = 1;
    const FDWCEditorSurfacePatchProjectionResult BudgetResult =
        FDWCEditorSurfacePatchProjector::Project(BudgetRequest);
    TestEqual(
        TEXT("A large seam projection fails explicitly instead of returning a partial result"),
        BudgetResult.Status,
        EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded);
    TestEqual(TEXT("Budget failure discards partial fragments"), BudgetResult.Fragments.Num(), 0);

    FDWCEditorSurfacePatchProjectionRequest ResultBudgetRequest = MakeRequest(Data);
    ResultBudgetRequest.MaxResultBytes = 1;
    const FDWCEditorSurfacePatchProjectionResult ResultBudgetResult =
        FDWCEditorSurfacePatchProjector::Project(ResultBudgetRequest);
    TestEqual(
        TEXT("Result memory is bounded before publishing a fragment"),
        ResultBudgetResult.Status,
        EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded);
    TestEqual(TEXT("Result budget failure discards partial fragments"), ResultBudgetResult.Fragments.Num(), 0);

    FDWCEditorSurfacePatchProjectionRequest InvalidRequest = MakeRequest(Data);
    InvalidRequest.AnchorBarycentric = FVector3f(-0.25f, 0.5f, 0.75f);
    const FDWCEditorSurfacePatchProjectionResult InvalidResult =
        FDWCEditorSurfacePatchProjector::Project(InvalidRequest);
    TestEqual(
        TEXT("A materially invalid surface anchor is rejected"),
        InvalidResult.Status,
        EDWCEditorSurfacePatchProjectionStatus::InvalidRequest);

    FDWCEditorCancellationToken CancellationToken;
    CancellationToken.Cancel();
    const FDWCEditorSurfacePatchProjectionResult CanceledResult =
        FDWCEditorSurfacePatchProjector::Project(MakeRequest(Data), &CancellationToken);
    TestEqual(
        TEXT("Canceled projection reports its lifecycle state"),
        CanceledResult.Status,
        EDWCEditorSurfacePatchProjectionStatus::Canceled);
    TestEqual(TEXT("Canceled projection publishes no fragments"), CanceledResult.Fragments.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchDegenerateTangentFrameTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.DegenerateTangentFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchDegenerateTangentFrameTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildTwoTriangleSurface(false);
    Data->Triangles[0].LocalTangents[1] = FVector3f::ZeroVector;
    const FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(MakeRequest(Data));

    TestEqual(
        TEXT("A degenerate target tangent frame fails instead of substituting identity axes"),
        Result.Status,
        EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface);
    TestEqual(TEXT("A degenerate tangent frame publishes no partial fragments"), Result.Fragments.Num(), 0);
    TestTrue(
        TEXT("The failure diagnostics identify the rejected target tangent frame"),
        Result.Diagnostics.DegenerateTangentFrameCount > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorLargeSurfacePatchProjectionTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.LargeMultiSeamDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorLargeSurfacePatchProjectionTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildLargeSeamedGrid();
    FDWCEditorSurfacePatchProjectionRequest Request = MakeLargeRequest(Data);
    Request.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    const FDWCEditorSurfacePatchProjectionResult First =
        FDWCEditorSurfacePatchProjector::Project(Request);
    const FDWCEditorSurfacePatchProjectionResult Second =
        FDWCEditorSurfacePatchProjector::Project(Request);

    TestTrue(TEXT("A large patch projects across the grid"), First.IsSuccess());
    TestTrue(TEXT("The large patch visits more than 256 triangles"), First.VisitedTriangleCount > 256);
    TestTrue(TEXT("The large patch emits more than 256 fragments"), First.Fragments.Num() > 256);
    TestTrue(TEXT("The large patch crosses multiple UV seams"), First.TraversedSeamCount > 1);
    TestTrue(TEXT("The large patch covers multiple UV islands"), First.AffectedUVIslandIDs.Num() > 2);
    TestEqual(TEXT("Repeated large projection keeps fragment count"), Second.Fragments.Num(), First.Fragments.Num());
    for (int32 Index = 0; Index < FMath::Min(First.Fragments.Num(), Second.Fragments.Num()); ++Index)
    {
        if (First.Fragments[Index].TriangleID != Second.Fragments[Index].TriangleID)
        {
            AddError(FString::Printf(TEXT("Large projection order differs at fragment %d."), Index));
            break;
        }
    }
    TestTrue(TEXT("The sparse traversal reports a bounded working set"), First.PeakWorkingSetBytes > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionCachePolicyTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.CachePolicyAndLease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionCachePolicyTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildLargeSeamedGrid();
    const FDWCEditorSurfacePatchProjectionRequest FirstRequest = MakeLargeRequest(Data, 0.1f);
    FDWCEditorSurfacePatchProjectionCacheService Cache;
    FDWCEditorSurfacePatchProjectionLease FirstGeometry;
    TestTrue(TEXT("First persistent projection succeeds"), Cache.Resolve(
        FirstRequest, EDWCEditorSurfacePatchCachePolicy::Persistent, FirstGeometry));
    TestEqual(TEXT("The first projection caches one geometry"),
        Cache.GetEntryCount(), 1);

    FDWCEditorSurfacePatchProjectionLease ReusedGeometry;
    TestTrue(TEXT("Identical persistent projection resolves"), Cache.Resolve(
        FirstRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        ReusedGeometry));
    TestTrue(TEXT("Identical request reuses immutable geometry"),
        FirstGeometry.Get() == ReusedGeometry.Get());
    TestTrue(TEXT("A reused geometry reports cache residency"), ReusedGeometry.IsCacheResident());
    ReusedGeometry.Reset();

    FDWCEditorSurfacePatchProjectionRequest SecondRequest = MakeLargeRequest(Data, 0.7f);
    FDWCEditorSurfacePatchProjectionLease SecondGeometry;
    TestTrue(TEXT("A rotated request resolves as distinct geometry"), Cache.Resolve(
        SecondRequest, EDWCEditorSurfacePatchCachePolicy::Persistent, SecondGeometry));
    FDWCEditorSurfacePatchProjectionCacheDiagnostics Diagnostics = Cache.GetDiagnostics();
    TestEqual(TEXT("Two rotations own two final geometries"),
        Diagnostics.EntryCount, 2);

    FDWCEditorSurfacePatchProjectionRequest ReadOnlyRequest = MakeLargeRequest(Data, 1.1f);
    FDWCEditorSurfacePatchProjectionLease ReadOnlyGeometry;
    const int32 EntryCountBeforeReadOnly = Cache.GetEntryCount();
    TestTrue(TEXT("Read-only hover projection succeeds"), Cache.Resolve(
        ReadOnlyRequest,
        EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral,
        ReadOnlyGeometry));
    TestEqual(TEXT("Read-only hover does not admit final geometry"),
        Cache.GetEntryCount(), EntryCountBeforeReadOnly);
    Diagnostics = Cache.GetDiagnostics();
    TestTrue(TEXT("Read-only hover records its uncached geometry miss"),
        Diagnostics.ReadOnlyMissCount >= 1);
    TestFalse(TEXT("An ephemeral read-only miss is not reported as cache resident"),
        ReadOnlyGeometry.IsCacheResident());
    TestEqual(TEXT("Active geometry byte accounting matches the hard total"),
        Diagnostics.ActiveBytes, Diagnostics.UsedBytes);

    FDWCEditorSurfacePatchProjectionLease HoverGeometry;
    const int32 EntryCountBeforeHover = Cache.GetEntryCount();
    TestTrue(TEXT("Ephemeral hover projection succeeds"), Cache.Resolve(
        MakeLargeRequest(Data, 1.1f), EDWCEditorSurfacePatchCachePolicy::Ephemeral, HoverGeometry));
    TestEqual(TEXT("Ephemeral hover does not consume persistent cache entries"),
        Cache.GetEntryCount(), EntryCountBeforeHover);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionCanonicalCacheKeyTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.CanonicalGeometryCacheKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionCanonicalCacheKeyTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildLargeSeamedGrid();
    FDWCEditorSurfacePatchProjectionRequest BaseRequest = MakeLargeRequest(Data, 0.2f);
    BaseRequest.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;

    FDWCEditorSurfacePatchProjectionCacheService Cache;
    FDWCEditorSurfacePatchProjectionLease BaseGeometry;
    TestTrue(TEXT("The base surface decal projection resolves"), Cache.Resolve(
        BaseRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        BaseGeometry));

    FDWCEditorSurfacePatchProjectionRequest RasterOnlyRequest = BaseRequest;
    RasterOnlyRequest.MaxSurfaceAngleDegrees = 45.0f;
    RasterOnlyRequest.ProjectionDepthSoftness = 0.75f;
    RasterOnlyRequest.ProjectionAngleSoftness = 0.6f;
    FDWCEditorSurfacePatchProjectionLease RasterOnlyGeometry;
    TestTrue(TEXT("Raster-only projection settings resolve"), Cache.Resolve(
        RasterOnlyRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        RasterOnlyGeometry));
    TestTrue(TEXT("Raster-only settings reuse the same projected geometry"),
        BaseGeometry.Get() == RasterOnlyGeometry.Get());

    FDWCEditorSurfacePatchProjectionRequest DepthRequest = BaseRequest;
    DepthRequest.ProjectionDepthLocal *= 1.5f;
    FDWCEditorSurfacePatchProjectionLease DepthGeometry;
    TestTrue(TEXT("A different projection depth resolves"), Cache.Resolve(
        DepthRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        DepthGeometry));
    TestTrue(TEXT("Projection depth owns a distinct geometry key"),
        BaseGeometry.Get() != DepthGeometry.Get());

    FDWCEditorSurfacePatchProjectionRequest BoundaryRequest = BaseRequest;
    BoundaryRequest.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    FDWCEditorSurfacePatchProjectionLease BoundaryGeometry;
    TestTrue(TEXT("A different boundary policy resolves"), Cache.Resolve(
        BoundaryRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        BoundaryGeometry));
    TestTrue(TEXT("Boundary policy owns a distinct geometry key"),
        BaseGeometry.Get() != BoundaryGeometry.Get());

    FDWCEditorSurfacePatchProjectionRequest OrientationRequest = BaseRequest;
    OrientationRequest.SurfaceFrameU = BaseRequest.SurfaceFrameV;
    OrientationRequest.SurfaceFrameV = -BaseRequest.SurfaceFrameU;
    FDWCEditorSurfacePatchProjectionLease OrientationGeometry;
    TestTrue(TEXT("A different canonical orientation resolves"), Cache.Resolve(
        OrientationRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        OrientationGeometry));
    TestTrue(TEXT("Canonical orientation owns a distinct geometry key"),
        BaseGeometry.Get() != OrientationGeometry.Get());

    FDWCEditorSurfacePatchProjectionRequest DetailedRequest = BaseRequest;
    DetailedRequest.bCollectDetailedDiagnostics = true;
    FDWCEditorSurfacePatchProjectionLease DetailedGeometry;
    TestTrue(TEXT("A detailed request upgrades the geometry-equivalent cache entry"), Cache.Resolve(
        DetailedRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        DetailedGeometry));
    TestTrue(TEXT("The upgraded entry contains detailed diagnostics"),
        DetailedGeometry.IsValid() && DetailedGeometry->Diagnostics.bDetailed);

    FDWCEditorSurfacePatchProjectionLease ReusedDetailedGeometry;
    TestTrue(TEXT("A basic request reuses the detailed geometry entry"), Cache.Resolve(
        BaseRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        ReusedDetailedGeometry));
    TestTrue(TEXT("Detailed geometry satisfies the basic request"),
        DetailedGeometry.Get() == ReusedDetailedGeometry.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionCacheLifetimeTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.CacheLifetimeContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionCacheLifetimeTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildLargeSeamedGrid();
    const FDWCEditorSurfacePatchProjectionRequest Request = MakeLargeRequest(Data, 0.25f);
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>();

    FDWCEditorSurfacePatchProjectionLease ConsumerLease;
    {
        TUniquePtr<FDWCEditorSurfacePatchProjectionCacheService> Cache =
            MakeUnique<FDWCEditorSurfacePatchProjectionCacheService>(
                Governor,
                FGuid::NewGuid());
        TestTrue(TEXT("A persistent projection returns a lease"), Cache->Resolve(
            Request,
            EDWCEditorSurfacePatchCachePolicy::Persistent,
            ConsumerLease));
        TestTrue(TEXT("The persistent result owns cache residency"),
            ConsumerLease.IsCacheResident());
        const uint64 GeometryResidentBytes = ConsumerLease.GetSharedResidentBytes();
        TestTrue(TEXT("The resident lease reports its reservation"),
            GeometryResidentBytes > 0);

        Cache->Reset();
        const FDWCEditorSurfacePatchProjectionCacheDiagnostics ResetDiagnostics =
            Cache->GetDiagnostics();
        TestEqual(TEXT("Reset immediately removes active cache keys"),
            Cache->GetEntryCount(), 0);
        TestEqual(TEXT("The externally leased projection becomes retired"),
            ResetDiagnostics.RetiredEntryCount, 1);
        TestEqual(TEXT("Retired geometry remains physically budgeted"),
            ResetDiagnostics.RetiredPinnedBytes, GeometryResidentBytes);
        TestTrue(TEXT("The retired lease still exposes immutable geometry"),
            ConsumerLease.IsValid() && !ConsumerLease->Fragments.IsEmpty());
        TestEqual(TEXT("The resource governor retains the retired reservation"),
            Governor->GetDiagnostics().GlobalCPUUsedBytes,
            GeometryResidentBytes);

        FDWCEditorSurfacePatchProjectionLease RebuiltLease;
        TestTrue(TEXT("The invalidated key can be rebuilt"), Cache->Resolve(
            Request,
            EDWCEditorSurfacePatchCachePolicy::Persistent,
            RebuiltLease));
        TestTrue(TEXT("Reset never returns the retired geometry as a cache hit"),
            RebuiltLease.Get() != ConsumerLease.Get());
        RebuiltLease.Reset();
        Cache->Reset();

        Cache.Reset();
        TestTrue(TEXT("The geometry survives cache service destruction"),
            ConsumerLease.IsValid() && !ConsumerLease->Fragments.IsEmpty());
        TestEqual(TEXT("Service destruction does not release a consumer-owned reservation"),
            Governor->GetDiagnostics().GlobalCPUUsedBytes,
            GeometryResidentBytes);
    }

    ConsumerLease.Reset();
    TestEqual(TEXT("The final lease release returns all shared-cache bytes"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes,
        0ull);

    {
        FDWCEditorSurfacePatchProjectionCacheService Cache(
            Governor,
            FGuid::NewGuid());
        FDWCEditorSurfacePatchProjectionLease UnpinnedLease;
        TestTrue(TEXT("A second persistent projection succeeds"), Cache.Resolve(
            Request,
            EDWCEditorSurfacePatchCachePolicy::Persistent,
            UnpinnedLease));
        UnpinnedLease.Reset();
        Cache.Reset();
        TestEqual(TEXT("Reset immediately frees entries with no consumer lease"),
            Governor->GetDiagnostics().GlobalCPUUsedBytes,
            0ull);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionAdmissionMemoryTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.AdmissionMemoryBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionAdmissionMemoryTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildLargeSeamedGrid(24, 24);

    FDWCEditorSurfacePatchProjectionRequest NonSeamRequest = MakeLargeRequest(Data, 0.0f);
    const FDWCEditorSurfacePatchProjectionMemoryEstimate NonSeamEstimate =
        FDWCEditorSurfacePatchProjector::EstimateAdmissionMemory(NonSeamRequest);
    TestEqual(TEXT("Non-seam admission uses the spatial triangle upper bound"),
        NonSeamEstimate.TriangleUpperBound, Data->Triangles.Num());
    TestEqual(TEXT("Non-seam admission has no intermediate chart"),
        NonSeamEstimate.IntermediateBytes, 0ull);
    TestTrue(TEXT("Non-seam working memory remains below its hard limit"),
        NonSeamEstimate.WorkingSetBytes <= NonSeamRequest.MaxWorkingSetBytes);
    TestTrue(TEXT("Non-seam final result remains below its hard limit"),
        NonSeamEstimate.ResultBytes <= NonSeamRequest.MaxResultBytes);

    FDWCEditorSurfacePatchProjectionRequest SeamRequest = NonSeamRequest;
    SeamRequest.BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    SeamRequest.MaxVisitedTriangles = 32;
    SeamRequest.MaxWorkingSetBytes = 4096;
    SeamRequest.MaxResultBytes = 8192;
    const FDWCEditorSurfacePatchProjectionMemoryEstimate SeamEstimate =
        FDWCEditorSurfacePatchProjector::EstimateAdmissionMemory(SeamRequest);
    TestEqual(TEXT("UV-seam admission honors the explicit triangle limit"),
        SeamEstimate.TriangleUpperBound, 32);
    TestEqual(TEXT("UV-seam admission has no island-chart intermediate"),
        SeamEstimate.IntermediateBytes, 0ull);
    TestTrue(TEXT("UV-seam working memory is clamped by the hard limit"),
        SeamEstimate.WorkingSetBytes <= SeamRequest.MaxWorkingSetBytes);
    TestTrue(TEXT("UV-seam result memory is clamped by the hard limit"),
        SeamEstimate.ResultBytes <= SeamRequest.MaxResultBytes);
    TestTrue(TEXT("A bounded UV-seam request reserves less than the non-seam full topology"),
        SeamEstimate.GetTotalBytes() < NonSeamEstimate.GetTotalBytes());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorProjectedHoverWorkerLifecycleTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.HoverWorkerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorProjectedHoverWorkerLifecycleTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildLargeSeamedGrid(24, 24);
    const TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> Cache =
        MakeShared<FDWCEditorSurfacePatchProjectionCacheService>();

    FDWCEditorSurfaceNormalPatchInput SurfaceInput;
    SurfaceInput.Projection = MakeLargeRequest(Data, 0.37f);
    SurfaceInput.NormalSource = MakeNormalSource();
    SurfaceInput.CoverageSource = MakeCoverageSource();
    SurfaceInput.Strength = 0.8f;
    SurfaceInput.Falloff = 0.2f;

    // Seed persistent geometry exactly as a committed preview or bake would.
    FDWCEditorSurfacePatchProjectionRequest SeedRequest = SurfaceInput.Projection;
    SeedRequest.RotationRadians = 0.1f;
    FDWCEditorSurfacePatchProjectionLease PersistentGeometry;
    TestTrue(TEXT("Persistent projection seed succeeds"), Cache->Resolve(
        SeedRequest,
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        PersistentGeometry));
    PersistentGeometry.Reset();
    const int32 PersistentEntries = Cache->GetEntryCount();

    FWetWrinkleProjectedHoverPreviewJobInput HoverInput;
    HoverInput.SurfaceInput = SurfaceInput;
    HoverInput.ProjectionCache = Cache;
    HoverInput.TextureSize = FIntPoint(256, 256);
    HoverInput.WorkingTextureSize = FIntPoint(256, 256);
    HoverInput.bCollectPerformanceDiagnostics = true;
    HoverInput.PerformanceDiagnostics.RequestId = 17;
    HoverInput.PerformanceDiagnostics.RequestStartSeconds = FPlatformTime::Seconds();
    const FDWCEditorWorkerMemoryEstimate HoverEstimate =
        FWetWrinkleIncrementalPreviewWorker::EstimateProjectedHoverMemory(
            HoverInput.SurfaceInput,
            HoverInput.WorkingTextureSize,
            HoverInput.TextureSize);
    const FDWCEditorSurfacePatchProjectionMemoryEstimate ProjectionEstimate =
        FDWCEditorSurfacePatchProjector::EstimateAdmissionMemory(SurfaceInput.Projection);
    TestEqual(TEXT("Hover reserves the topology-bounded projection results"),
        HoverEstimate.WorkingBytes,
        ProjectionEstimate.IntermediateBytes + ProjectionEstimate.ResultBytes);
    TestEqual(TEXT("Hover reserves the topology-bounded projection scratch"),
        HoverEstimate.ScratchBytes,
        ProjectionEstimate.WorkingSetBytes);
    TestEqual(TEXT("Hover does not double count sequential encoded output"),
        HoverEstimate.OutputBytes,
        0ull);
    TestTrue(TEXT("The topology-bound admission is smaller than both hard safety maxima"),
        HoverEstimate.GetTotalBytes() <
            SurfaceInput.Projection.MaxResultBytes +
            SurfaceInput.Projection.MaxWorkingSetBytes);
    TestEqual(TEXT("Shared normal readback is not charged to worker-private admission"),
        HoverEstimate.ResidentSharedBytes,
        0ull);
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
        FWetWrinkleIncrementalPreviewWorker::BuildProjectedHover(MoveTemp(HoverInput), Token);
    TestTrue(TEXT("Admitted hover worker succeeds"), Result.IsValid() && Result->bSucceeded);
    TestTrue(TEXT("Hover worker produces projected dirty rectangles"),
        Result.IsValid() && !Result->ProjectedOutputRects.IsEmpty());
    TestTrue(TEXT("Hover worker produces encoded region payloads"),
        Result.IsValid() && !Result->Regions.IsEmpty());
    TestTrue(TEXT("A successful hover worker retains the exact projected commit payload"),
        Result.IsValid() && Result->PresentedProjectedPatch.IsSet() &&
        Result->PresentedProjectedPatch->IsValid());
    TestTrue(TEXT("Hover worker returns opt-in performance diagnostics"),
        Result.IsValid() && Result->HoverDiagnostics.IsSet());
    if (Result.IsValid() && Result->HoverDiagnostics.IsSet())
    {
        const FWetWrinkleHoverPerformanceDiagnostics& Diagnostics =
            Result->HoverDiagnostics.GetValue();
        TestEqual(TEXT("Diagnostics preserve the request id"), Diagnostics.RequestId, 17ull);
        TestTrue(TEXT("Diagnostics count projected fragments"),
            Diagnostics.ProjectedFragmentCount > 0);
        TestTrue(TEXT("Projection private output stays within its admitted result bound"),
            Diagnostics.ProjectionPrivateResultBytes <= HoverEstimate.WorkingBytes);
        TestTrue(TEXT("Projection working memory stays within its admitted scratch bound"),
            Diagnostics.ProjectionWorkingSetBytes <= HoverEstimate.ScratchBytes);
        TestTrue(TEXT("Diagnostics report externally owned shared residency"),
            Diagnostics.SharedResidentBytes > 0);
        TestTrue(TEXT("Diagnostics count candidate raster pixels"),
            Diagnostics.CandidatePixelCount > 0);
        TestTrue(TEXT("Diagnostics count encoded output pixels"),
            Diagnostics.EncodedOutputPixelCount > 0);
        TestTrue(TEXT("Same-resolution hover uses direct normal encode"),
            Diagnostics.bUsedDirectEncode);
        TestTrue(TEXT("Diagnostics report a non-negative worker duration"),
            Diagnostics.WorkerTotalMs >= 0.0);
    }
    TestEqual(TEXT("Hover worker does not grow the persistent cache"),
        Cache->GetEntryCount(), PersistentEntries);

    FWetWrinkleProjectedHoverPreviewJobInput CanceledInput;
    CanceledInput.SurfaceInput = SurfaceInput;
    CanceledInput.ProjectionCache = Cache;
    CanceledInput.TextureSize = FIntPoint(256, 256);
    CanceledInput.WorkingTextureSize = FIntPoint(256, 256);
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> CanceledToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    CanceledToken->Cancel();
    const TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> CanceledResult =
        FWetWrinkleIncrementalPreviewWorker::BuildProjectedHover(
            MoveTemp(CanceledInput), CanceledToken);
    TestTrue(TEXT("Canceled hover worker returns a terminal result"), CanceledResult.IsValid());
    TestFalse(TEXT("Canceled hover worker never produces a committable payload"),
        CanceledResult.IsValid() && CanceledResult->bSucceeded);
    TestFalse(TEXT("Canceled hover worker does not retain a projected commit payload"),
        CanceledResult.IsValid() && CanceledResult->PresentedProjectedPatch.IsSet());
    TestEqual(TEXT("Cancellation leaves persistent cache ownership unchanged"),
        Cache->GetEntryCount(), PersistentEntries);

    FDWCEditorProjectedNormalPatchCommand ReferenceCommand;
    FString ReferenceError;
    TestTrue(TEXT("Reference bake command resolves"),
        FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            SurfaceInput,
            ReferenceCommand,
            &ReferenceError,
            nullptr,
            &Cache.Get(),
            EDWCEditorSurfacePatchCachePolicy::Persistent));
    FDWCEditorNormalRasterSurface ReferenceSurface;
    TestTrue(TEXT("Reference bake surface initializes"),
        ReferenceSurface.Initialize(FIntPoint(256, 256), false));
    TestTrue(TEXT("Reference bake raster affects pixels"),
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
            ReferenceCommand, ReferenceSurface).bAffectedPixels);
    TArray<FColor> ReferencePixels;
    FDWCEditorRasterPostProcess::EncodeNormalPixels(ReferenceSurface, ReferencePixels);
    TestEqual(TEXT("Reference bake pixels encode"), ReferencePixels.Num(), 256 * 256);
    if (Result.IsValid() && Result->PresentedProjectedPatch.IsSet())
    {
        FDWCEditorNormalRasterSurface PresentedSurface;
        TestTrue(TEXT("Presented payload parity surface initializes"),
            PresentedSurface.Initialize(FIntPoint(256, 256), false));
        TestTrue(TEXT("Presented payload parity raster affects pixels"),
            FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
                Result->PresentedProjectedPatch.GetValue(), PresentedSurface).bAffectedPixels);
        TArray<FColor> PresentedPixels;
        FDWCEditorRasterPostProcess::EncodeNormalPixels(PresentedSurface, PresentedPixels);
        TestTrue(TEXT("Presented hover payload matches the reference bake command"),
            PresentedPixels == ReferencePixels);
    }
    if (Result.IsValid())
    {
        for (const FDWCEditorNormalRegionPayload& Region : Result->Regions)
        {
            for (int32 Y = Region.OutputRect.Min.Y; Y < Region.OutputRect.Max.Y; ++Y)
            {
                for (int32 X = Region.OutputRect.Min.X; X < Region.OutputRect.Max.X; ++X)
                {
                    const int32 RegionIndex =
                        (Y - Region.OutputRect.Min.Y) * Region.OutputRect.Width() +
                        (X - Region.OutputRect.Min.X);
                    const int32 ReferenceIndex = Y * 256 + X;
                    if (!Region.EncodedPixels.IsValidIndex(RegionIndex) ||
                        !ReferencePixels.IsValidIndex(ReferenceIndex) ||
                        Region.EncodedPixels[RegionIndex] != ReferencePixels[ReferenceIndex])
                    {
                        AddError(FString::Printf(
                            TEXT("Hover/bake parity differs at output pixel (%d,%d)."), X, Y));
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchProjectionBoundedCacheTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.BoundedGeometryCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchProjectionBoundedCacheTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildLargeSeamedGrid(24, 24);
    const FDWCEditorSurfacePatchProjectionRequest FirstRequest = MakeLargeRequest(Data, 0.1f);

    FDWCEditorSurfacePatchProjectionCacheService MeasuringCache;
    FDWCEditorSurfacePatchProjectionLease MeasuringGeometry;
    TestTrue(TEXT("Measuring projection succeeds"), MeasuringCache.Resolve(
        FirstRequest, EDWCEditorSurfacePatchCachePolicy::Persistent, MeasuringGeometry));
    MeasuringGeometry.Reset();
    const FDWCEditorSurfacePatchProjectionCacheDiagnostics Measured =
        MeasuringCache.GetDiagnostics();
    TestTrue(TEXT("Measuring cache owns geometry bytes"), Measured.UsedBytes > 0);

    const uint64 TightBudget = Measured.UsedBytes +
        FMath::Max<uint64>(Measured.UsedBytes / 2, 1);
    FDWCEditorSurfacePatchProjectionCacheService TightCache(TightBudget);
    FDWCEditorSurfacePatchProjectionLease FirstGeometry;
    TestTrue(TEXT("First tight-budget projection succeeds"), TightCache.Resolve(
        FirstRequest, EDWCEditorSurfacePatchCachePolicy::Persistent, FirstGeometry));
    FirstGeometry.Reset();

    FDWCEditorSurfacePatchProjectionLease SecondGeometry;
    TestTrue(TEXT("Second tight-budget rotation succeeds"), TightCache.Resolve(
        MakeLargeRequest(Data, 0.8f),
        EDWCEditorSurfacePatchCachePolicy::Persistent,
        SecondGeometry));
    const FDWCEditorSurfacePatchProjectionCacheDiagnostics Diagnostics =
        TightCache.GetDiagnostics();
    TestTrue(TEXT("Projection cache obeys its hard byte budget"),
        Diagnostics.UsedBytes <= Diagnostics.BudgetBytes);
    TestEqual(TEXT("Only the latest geometry remains after pressure"),
        Diagnostics.EntryCount, 1);
    TestTrue(TEXT("Pressure evicts the least-recently-used geometry"),
        Diagnostics.EvictionCount >= 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfacePatchContinuityDiagnosticsTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.ContinuityDiagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfacePatchContinuityDiagnosticsTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildTwoTriangleSurface(true);

    // Put both physical triangles in one continuous UV island so the shared
    // edge is classified as Regular and can be checked for chart continuity.
    FDWCEditorSpatialTriangle& TriangleB = Data->Triangles[1];
    TriangleB.UVIslandID = Data->Triangles[0].UVIslandID;
    TriangleB.UVs[0] = Data->Triangles[0].UVs[2];
    TriangleB.UVs[1] = Data->Triangles[0].UVs[1];
    TriangleB.UVs[2] = FVector2f(0.4f, 0.4f);
    TriangleB.UVBounds = FBox2f(ForceInit);
    for (const FVector2f UV : TriangleB.UVs)
    {
        TriangleB.UVBounds += UV;
    }
    FDWCEditorSpatialQueryService::BuildTriangleTopology(*Data);

    FDWCEditorSurfacePatchProjectionRequest Request = MakeRequest(Data);
    Request.bCollectDetailedDiagnostics = true;
    FDWCEditorSurfacePatchProjectionResult Result =
        FDWCEditorSurfacePatchProjector::Project(Request);
    TestTrue(TEXT("Detailed diagnostic projection succeeds"), Result.IsSuccess());
    TestTrue(TEXT("Detailed diagnostics are populated"), Result.Diagnostics.bDetailed);
    TestEqual(TEXT("Both continuous triangles are emitted"), Result.Fragments.Num(), 2);
    TestEqual(TEXT("The regular shared edge is compared once"),
        Result.Diagnostics.SharedEdgeComparisonCount, 1);
    TestEqual(TEXT("A flat chart has no shared-edge discontinuity"),
        Result.Diagnostics.DiscontinuousSharedEdgeCount, 0);
    TestTrue(TEXT("Projection diagnostics record candidate triangles"),
        Result.Diagnostics.CandidateTriangleCount >= Result.Fragments.Num());

    FDWCEditorSurfacePatchFragment* FragmentB = Result.Fragments.FindByPredicate(
        [](const FDWCEditorSurfacePatchFragment& Fragment)
        {
            return Fragment.TriangleID == 11;
        });
    TestNotNull(TEXT("The adjacent diagnostic fragment exists"), FragmentB);
    if (FragmentB != nullptr)
    {
        FragmentB->PatchCoordinates[0].X += 0.1f;
        FDWCEditorSurfacePatchProjector::AnalyzeContinuityForDiagnostics(Result, *Data);
        TestEqual(TEXT("A corrupted shared coordinate is detected"),
            Result.Diagnostics.DiscontinuousSharedEdgeCount, 1);
        TestTrue(TEXT("The diagnostic reports the coordinate error magnitude"),
            Result.Diagnostics.MaxSharedCoordinateError > 0.09f);
    }

    Data->Triangles[0].EdgeTypes[1] = EDWCEditorSpatialEdgeType::Blocked;
    Data->Triangles[1].EdgeTypes[0] = EDWCEditorSpatialEdgeType::Blocked;
    FDWCEditorSurfacePatchProjector::AnalyzeContinuityForDiagnostics(Result, *Data);
    TestEqual(TEXT("A paired blocked edge inside the emitted island is detected"),
        Result.Diagnostics.InternalBlockedEdgeCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorLargeSurfacePatchPreviewBakeParityTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.LargeSeamPreviewBakeParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorLargeSurfacePatchPreviewBakeParityTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data = BuildLargeSeamedGrid();
    FDWCEditorSurfaceNormalPatchInput Input;
    Input.Projection = MakeLargeRequest(Data);
    Input.Projection.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    Input.NormalSource = MakeNormalSource();
    Input.CoverageSource = MakeCoverageSource();
    Input.Strength = 0.8f;
    Input.Falloff = 0.25f;

    FDWCEditorSurfacePatchProjectionCacheService Cache;
    FDWCEditorProjectedNormalPatchCommand PreviewCommand;
    FDWCEditorProjectedNormalPatchCommand BakeCommand;
    FString Error;
    TestTrue(TEXT("Preview command builds"), FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
        Input, PreviewCommand, &Error, nullptr, &Cache,
        EDWCEditorSurfacePatchCachePolicy::Persistent));
    TestTrue(TEXT("Bake command builds"), FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
        Input, BakeCommand, &Error, nullptr, &Cache,
        EDWCEditorSurfacePatchCachePolicy::Persistent));
    TestTrue(TEXT("Preview and bake share the same projected geometry"),
        PreviewCommand.ProjectionLease.Get() == BakeCommand.ProjectionLease.Get());

    const FIntPoint Size(256, 256);
    FDWCEditorNormalRasterSurface PreviewSurface;
    FDWCEditorNormalRasterSurface BakeSurface;
    PreviewSurface.Initialize(Size, true);
    BakeSurface.Initialize(Size, true);
    const FDWCEditorRasterResult PreviewResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(PreviewCommand, PreviewSurface);
    const FDWCEditorRasterResult BakeResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(BakeCommand, BakeSurface);
    TestTrue(TEXT("Preview raster affects seam pixels"), PreviewResult.bAffectedPixels);
    TestTrue(TEXT("Bake raster affects seam pixels"), BakeResult.bAffectedPixels);
    TestTrue(TEXT("Preview and bake normal storage is identical"),
        PreviewSurface.PackedNormalXY == BakeSurface.PackedNormalXY);
    TestTrue(TEXT("Preview and bake coverage is identical"),
        PreviewSurface.Coverage == BakeSurface.Coverage);

    TArray<FColor> PreviewPixels;
    TArray<FColor> BakePixels;
    FDWCEditorRasterPostProcess::EncodeNormalPixels(PreviewSurface, PreviewPixels);
    FDWCEditorRasterPostProcess::EncodeNormalPixels(BakeSurface, BakePixels);
    TestTrue(
        TEXT("Preview and bake produce identical final RGBA pixels across all UV seams"),
        PreviewPixels == BakePixels);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorNonUvSeamAnchorDecalPreviewBakeParityTest,
    "DWC.Editor.Foundation.Spatial.SurfacePatchProjection.NonUvSeamAnchorDecalPreviewBakeParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorNonUvSeamAnchorDecalPreviewBakeParityTest::RunTest(const FString&)
{
    using namespace DWCEditorSurfacePatchProjectorTestsPrivate;
    const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
        BuildLargeSeamedGrid(24, 24);
    FDWCEditorSurfaceNormalPatchInput Input;
    Input.Projection = MakeLargeRequest(Data, 0.43f);
    Input.Projection.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
    Input.Projection.bCollectDetailedDiagnostics = true;
    Input.NormalSource = MakeNormalSource();
    Input.CoverageSource = MakeCoverageSource();
    Input.Strength = 0.85f;
    Input.Falloff = 0.2f;

    FDWCEditorSurfacePatchProjectionCacheService Cache;
    FDWCEditorProjectedNormalPatchCommand PreviewCommand;
    FDWCEditorProjectedNormalPatchCommand BakeCommand;
    FString Error;
    TestTrue(TEXT("Non UV Seam preview command builds from the anchor-island decal"),
        FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            Input, PreviewCommand, &Error, nullptr, &Cache,
            EDWCEditorSurfacePatchCachePolicy::Persistent));
    TestTrue(TEXT("Non UV Seam bake command resolves from the same contract"),
        FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            Input, BakeCommand, &Error, nullptr, &Cache,
            EDWCEditorSurfacePatchCachePolicy::Persistent));
    if (!PreviewCommand.ProjectionLease.IsValid() ||
        !BakeCommand.ProjectionLease.IsValid())
    {
        AddError(FString::Printf(TEXT("Anchor-decal parity command failed: %s"), *Error));
        return false;
    }
    TestTrue(TEXT("Preview and bake lease identical immutable projection geometry"),
        PreviewCommand.ProjectionLease.Get() == BakeCommand.ProjectionLease.Get());
    TestTrue(TEXT("Non UV Seam preview uses the decal projection filter"),
        PreviewCommand.bUseSurfaceProjectionFilter);
    TestTrue(TEXT("Non UV Seam bake uses the decal projection filter"),
        BakeCommand.bUseSurfaceProjectionFilter);
    TestEqual(TEXT("Non UV Seam projection stays on one UV island"),
        PreviewCommand.ProjectionLease->AffectedUVIslandIDs.Num(), 1);
    TestEqual(TEXT("Non UV Seam projection traverses no UV seam"),
        PreviewCommand.ProjectionLease->TraversedSeamCount, 0);

    FDWCEditorNormalRasterSurface PreviewSurface;
    FDWCEditorNormalRasterSurface BakeSurface;
    TestTrue(TEXT("Preview surface initializes"), PreviewSurface.Initialize(FIntPoint(256, 256), true));
    TestTrue(TEXT("Bake surface initializes"), BakeSurface.Initialize(FIntPoint(256, 256), true));
    const FDWCEditorRasterResult PreviewResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(PreviewCommand, PreviewSurface);
    const FDWCEditorRasterResult BakeResult =
        FDWCEditorNormalRasterCore::RasterizeProjectedPatch(BakeCommand, BakeSurface);
    TestTrue(TEXT("Non UV Seam preview affects pixels"), PreviewResult.bAffectedPixels);
    TestTrue(TEXT("Non UV Seam bake affects pixels"), BakeResult.bAffectedPixels);
    TestTrue(TEXT("Non UV Seam preview and bake normal buffers match"),
        PreviewSurface.PackedNormalXY == BakeSurface.PackedNormalXY);
    TestTrue(TEXT("Non UV Seam preview and bake coverage buffers match"),
        PreviewSurface.Coverage == BakeSurface.Coverage);

    const FDWCEditorSurfacePatchProjectionCacheDiagnostics Diagnostics = Cache.GetDiagnostics();
    TestEqual(TEXT("The parity path owns one final projection geometry"),
        Diagnostics.EntryCount, 1);
    return true;
}

#endif
