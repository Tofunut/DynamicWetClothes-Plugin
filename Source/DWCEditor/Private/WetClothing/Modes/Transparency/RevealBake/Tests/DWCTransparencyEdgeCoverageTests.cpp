// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FDWCRevealBakeSurfaceTriangle MakeTriangle(
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C,
        const int32 TriangleIndex,
        const int32 VertexA,
        const int32 VertexB,
        const int32 VertexC)
    {
        FDWCRevealBakeSurfaceTriangle Triangle;
        Triangle.TriangleIndex = TriangleIndex;
        Triangle.MaterialSlotIndex = 0;
        Triangle.UVIslandID = 0;
        Triangle.VertexIndices[0] = VertexA;
        Triangle.VertexIndices[1] = VertexB;
        Triangle.VertexIndices[2] = VertexC;
        Triangle.UVs[0] = A;
        Triangle.UVs[1] = B;
        Triangle.UVs[2] = C;
        Triangle.Positions[0] = FVector(A, 0.0);
        Triangle.Positions[1] = FVector(B, 0.0);
        Triangle.Positions[2] = FVector(C, 0.0);
        return Triangle;
    }

    const FDWCRevealBakeTexelSample* FindSample(
        const TArray<FDWCRevealBakeTexelSample>& Samples,
        const FIntPoint Pixel)
    {
        return Samples.FindByPredicate(
            [Pixel](const FDWCRevealBakeTexelSample& Sample)
            {
                return Sample.Pixel == Pixel;
            });
    }

    FDWCRevealBakeSurface MakeProjectionSurface(
        const FName LayerId,
        const float Z,
        const bool bRevealSource,
        const bool bBlocker)
    {
        FDWCRevealBakeSurface Surface;
        Surface.LayerId = LayerId;
        Surface.MaxRevealDistance = 5.0f;
        Surface.bCanBeRevealSource = bRevealSource;
        Surface.bBlocksReveal = bBlocker;
        FDWCRevealBakeSurfaceTriangle Triangle;
        Triangle.TriangleIndex = 0;
        Triangle.Positions[0] = FVector(-10.0, -10.0, Z);
        Triangle.Positions[1] = FVector(10.0, -10.0, Z);
        Triangle.Positions[2] = FVector(0.0, 10.0, Z);
        Triangle.UVs[0] = FVector2D(0.0, 0.0);
        Triangle.UVs[1] = FVector2D(1.0, 0.0);
        Triangle.UVs[2] = FVector2D(0.5, 1.0);
        Triangle.Bounds = FBox(ForceInit);
        for (const FVector& Position : Triangle.Positions)
        {
            Triangle.Bounds += Position;
            Surface.Bounds += Position;
        }
        Surface.Triangles.Add(MoveTemp(Triangle));
        return Surface;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyFractionalEdgeCoverageTest,
    "DWC.Transparency.RevealBake.FractionalEdgeCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyFractionalEdgeCoverageTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface Surface;
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0),
        FVector2D(1.0, 0.0),
        FVector2D(0.0, 1.0),
        0, 0, 1, 2));

    FDWCRevealBakeTexelSamplingSettings Settings;
    Settings.Resolution = FIntPoint(1, 1);
    Settings.MaterialSlotIndex = 0;
    TArray<FDWCRevealBakeTexelSample> Samples;
    FString Error;
    TestTrue(TEXT("The half-texel triangle rasterizes."),
        FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Surface, Settings, Samples, &Error));
    TestEqual(TEXT("One target texel is emitted."), Samples.Num(), 1);
    if (Samples.Num() == 1)
    {
        TestEqual(TEXT("Three of four subpixels produce fractional coverage."),
            Samples[0].Coverage, static_cast<uint8>(191));
    }

    TArray<uint8> Coverage = { 64, 128, 255 };
    TArray<uint8> EdgeWeights;
    TestTrue(TEXT("Zero-distance feathering preserves fractional coverage."),
        FDWCTransparencyComposite::BuildCoverageEdgeFeatherBuffer(
            FIntPoint(3, 1), Coverage, 0.0f, EdgeWeights));
    TestEqual(TEXT("Fractional edge weights remain unchanged."), EdgeWeights, Coverage);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencySubpixelSliverCoverageTest,
    "DWC.Transparency.RevealBake.SubpixelSliverCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencySubpixelSliverCoverageTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface Surface;
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0),
        FVector2D(0.13, 0.0),
        FVector2D(0.0, 0.13),
        0, 0, 1, 2));

    FDWCRevealBakeTexelSamplingSettings Settings;
    Settings.Resolution = FIntPoint(4, 4);
    Settings.MaterialSlotIndex = 0;
    TArray<FDWCRevealBakeTexelSample> Samples;
    FString Error;
    TestTrue(TEXT("A triangle missed by the texel center is conservatively retained."),
        FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Surface, Settings, Samples, &Error));
    const FDWCRevealBakeTexelSample* Sample = FindSample(Samples, FIntPoint(0, 0));
    TestNotNull(TEXT("The covered edge texel exists."), Sample);
    if (Sample != nullptr)
    {
        TestEqual(TEXT("Only the covered subpixel contributes."),
            Sample->Coverage, static_cast<uint8>(64));
        TestTrue(TEXT("The representative ray point remains inside the triangle."),
            Sample->Barycentric.X >= 0.0 &&
            Sample->Barycentric.Y >= 0.0 &&
            Sample->Barycentric.Z >= 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencySharedEdgeCoverageTest,
    "DWC.Transparency.RevealBake.SharedEdgeCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencySharedEdgeCoverageTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface Surface;
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0),
        0, 0, 1, 2));
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0),
        1, 0, 2, 3));

    FDWCRevealBakeTexelSamplingSettings Settings;
    Settings.Resolution = FIntPoint(4, 4);
    Settings.MaterialSlotIndex = 0;
    TArray<FDWCRevealBakeTexelSample> Samples;
    FString Error;
    int32 OverlapCount = 0;
    TestTrue(TEXT("The split quad rasterizes."),
        FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Surface, Settings, Samples, &Error, &OverlapCount));
    TestEqual(TEXT("Every quad texel is emitted exactly once."), Samples.Num(), 16);
    TestEqual(TEXT("A shared mesh edge is not reported as UV overlap."), OverlapCount, 0);
    for (const FDWCRevealBakeTexelSample& Sample : Samples)
    {
        TestEqual(TEXT("Shared triangle masks union to full texel coverage."),
            Sample.Coverage, static_cast<uint8>(255));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyUVOverlapCoverageTest,
    "DWC.Transparency.RevealBake.UVOverlapCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyUVOverlapCoverageTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface Surface;
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0),
        0, 0, 1, 2));
    Surface.Triangles.Add(MakeTriangle(
        FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0),
        1, 3, 4, 5));

    FDWCRevealBakeTexelSamplingSettings Settings;
    Settings.Resolution = FIntPoint(1, 1);
    Settings.MaterialSlotIndex = 0;
    TArray<FDWCRevealBakeTexelSample> Samples;
    FString Error;
    int32 OverlapCount = 0;
    TestTrue(TEXT("The first UV island remains usable when another island overlaps it."),
        FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Surface, Settings, Samples, &Error, &OverlapCount));
    TestEqual(TEXT("The overlapped texel is emitted once."), Samples.Num(), 1);
    TestEqual(TEXT("The overlapped texel is reported once."), OverlapCount, 1);
    TestEqual(TEXT("The first deterministic owner retains its own coverage."),
        Samples[0].Coverage, static_cast<uint8>(191));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyProjectionSampleSubsetTest,
    "DWC.Transparency.RevealBake.ProjectionSampleSubset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyProjectionSampleSubsetTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface OuterSurface;
    OuterSurface.MaxRevealDistance = 5.0f;
    OuterSurface.LayerOrder = 100;
    TArray<FDWCRevealBakeSurface> Sources;
    Sources.Add(MakeProjectionSurface(TEXT("Reveal"), -1.0f, true, false));
    TArray<FDWCRevealBakeTexelSample> Samples;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FDWCRevealBakeTexelSample& Sample = Samples.AddDefaulted_GetRef();
        Sample.Pixel = FIntPoint(Index, 0);
        Sample.Position = FVector(static_cast<double>(Index), 0.0, 0.0);
        Sample.Normal = FVector::UpVector;
    }
    const TArray<int32> RequestedSamples = { 1 };
    TArray<FDWCRevealBakeRayHit> Hits;
    FDWCRevealBakeRayProjectionSettings ProjectionSettings;
    ProjectionSettings.RayStartOffset = 0.0f;
    FString Error;
    TestTrue(
        TEXT("A projection can evaluate only the unresolved sample subset."),
        FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            Sources,
            Samples,
            ProjectionSettings,
            [&Hits](const FDWCRevealBakeRayHit& Hit) { Hits.Add(Hit); },
            &Error,
            nullptr,
            RequestedSamples));
    TestEqual(TEXT("Only one callback is produced."), Hits.Num(), 1);
    if (Hits.Num() == 1)
    {
        TestEqual(TEXT("The requested sample pixel is preserved."), Hits[0].Pixel, FIntPoint(1, 0));
        TestTrue(TEXT("The selected sample hits the reveal surface."), Hits[0].bHit);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyProjectionBlockerResultTest,
    "DWC.Transparency.RevealBake.ProjectionBlockerResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyProjectionBlockerResultTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface OuterSurface;
    OuterSurface.MaxRevealDistance = 5.0f;
    OuterSurface.LayerOrder = 100;
    TArray<FDWCRevealBakeSurface> Sources;
    Sources.Add(MakeProjectionSurface(TEXT("Blocker"), -0.5f, false, true));
    FDWCRevealBakeTexelSample Sample;
    Sample.Pixel = FIntPoint::ZeroValue;
    Sample.Position = FVector::ZeroVector;
    Sample.Normal = FVector::UpVector;
    TArray<FDWCRevealBakeTexelSample> Samples = { Sample };
    FDWCRevealBakeRayProjectionSettings Settings;
    Settings.RayStartOffset = 0.0f;
    Settings.bRespectBlockers = true;
    FDWCRevealBakeRayHit ResultHit;
    FString Error;
    TestTrue(
        TEXT("A blocker-only priority layer projects successfully."),
        FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            Sources,
            Samples,
            Settings,
            [&ResultHit](const FDWCRevealBakeRayHit& Hit) { ResultHit = Hit; },
            &Error));
    TestFalse(TEXT("A blocker is not reported as a reveal hit."), ResultHit.bHit);
    TestTrue(TEXT("The terminal blocker state is explicit."), ResultHit.bBlocked);
    TestTrue(TEXT("The blocker distance is retained for same-priority comparison."),
        FMath::IsNearlyEqual(ResultHit.Distance, 0.5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyProjectionProgressAndCancellationTest,
    "DWC.Transparency.RevealBake.ProjectionProgressAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyProjectionProgressAndCancellationTest::RunTest(const FString&)
{
    FDWCRevealBakeSurface OuterSurface;
    OuterSurface.MaxRevealDistance = 5.0f;
    OuterSurface.LayerOrder = 100;
    TArray<FDWCRevealBakeSurface> Sources;
    Sources.Add(MakeProjectionSurface(TEXT("Reveal"), -1.0f, true, false));

    constexpr int32 SampleCount = 4097;
    TArray<FDWCRevealBakeTexelSample> Samples;
    Samples.SetNum(SampleCount);
    for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
    {
        Samples[SampleIndex].Pixel = FIntPoint(SampleIndex, 0);
        Samples[SampleIndex].Position = FVector::ZeroVector;
        Samples[SampleIndex].Normal = FVector::UpVector;
    }

    FDWCRevealBakeRayProjectionSettings Settings;
    Settings.RayStartOffset = 0.0f;
    TArray<int32> ReportedCompletedSamples;
    const FDWCRevealBakeProjectionProgressCallback ProgressCallback =
        [&ReportedCompletedSamples](const int32 CompletedSamples, const int32 TotalSamples)
    {
        check(TotalSamples == SampleCount);
        ReportedCompletedSamples.Add(CompletedSamples);
    };
    FString Error;
    TestTrue(
        TEXT("Projection succeeds while reporting bounded progress."),
        FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            Sources,
            Samples,
            Settings,
            [](const FDWCRevealBakeRayHit&) {},
            &Error,
            nullptr,
            {},
            &ProgressCallback));
    TestTrue(TEXT("Projection reports at least its start and completion."),
        ReportedCompletedSamples.Num() >= 2);
    if (!ReportedCompletedSamples.IsEmpty())
    {
        TestEqual(TEXT("Projection progress starts at zero."),
            ReportedCompletedSamples[0], 0);
        TestEqual(TEXT("Projection progress ends at the exact sample count."),
            ReportedCompletedSamples.Last(), SampleCount);
        for (int32 Index = 1; Index < ReportedCompletedSamples.Num(); ++Index)
        {
            TestTrue(TEXT("Projection progress never moves backwards."),
                ReportedCompletedSamples[Index] >= ReportedCompletedSamples[Index - 1]);
        }
    }

    FDWCEditorCancellationToken CancellationToken;
    const FDWCRevealBakeProjectionProgressCallback CancelingProgressCallback =
        [&CancellationToken](const int32 CompletedSamples, const int32)
    {
        if (CompletedSamples >= 2048)
        {
            CancellationToken.Cancel();
        }
    };
    Error.Reset();
    TestFalse(
        TEXT("Projection stops after the progress consumer requests cancellation."),
        FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            OuterSurface,
            Sources,
            Samples,
            Settings,
            [](const FDWCRevealBakeRayHit&) {},
            &Error,
            &CancellationToken,
            {},
            &CancelingProgressCallback));
    TestTrue(TEXT("Canceled projection returns an explicit cancellation error."),
        Error.Contains(TEXT("canceled")));
    return true;
}

#endif
