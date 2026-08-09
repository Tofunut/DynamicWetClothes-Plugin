//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationResolver.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace
{
    FDWCEditorSurfaceOrientationPolicy MakeResolverTestPolicy()
    {
        FDWCEditorSurfaceOrientationPolicy Policy;
        Policy.Normalize();
        return Policy;
    }

    FDWCEditorSpatialData MakeResolverTestData(
        const FDWCEditorSurfaceOrientationPolicy& Policy,
        const FVector3f& FallbackV,
        const bool bCompatibleField = true)
    {
        FDWCEditorSpatialData Data;
        Data.Triangles.AddDefaulted();
        Data.SurfaceOrientationField.BuildStatus =
            EDWCEditorSurfaceOrientationFieldBuildStatus::Ready;
        Data.SurfaceOrientationField.PolicySignature = bCompatibleField
            ? Policy.BuildSignature()
            : Policy.BuildSignature() + 1u;
        Data.SurfaceOrientationField.FieldLayoutVersion =
            DWCEditorSurfaceOrientationVersion::FieldLayout;
        Data.SurfaceOrientationField.EntryIndexByTriangle.Init(INDEX_NONE, 1);
        FDWCEditorSurfaceOrientationFieldEntry& Entry =
            Data.SurfaceOrientationField.Entries.AddDefaulted_GetRef();
        Entry.TriangleIndex = 0;
        for (FPackedNormal& Corner : Entry.CornerFallbackV)
        {
            Corner = FPackedNormal(FallbackV.GetSafeNormal());
        }
        Data.SurfaceOrientationField.EntryIndexByTriangle[0] = 0;
        return Data;
    }

    bool IsRightHandedResolverFrame(
        const FDWCEditorResolvedSurfaceOrientation& Orientation,
        const FVector3f& Normal)
    {
        return Orientation.IsValid() &&
            FMath::IsNearlyZero(FVector3f::DotProduct(Orientation.FrameU, Normal), 0.002f) &&
            FMath::IsNearlyZero(FVector3f::DotProduct(Orientation.FrameV, Normal), 0.002f) &&
            FVector3f::DotProduct(
                FVector3f::CrossProduct(Orientation.FrameU, Orientation.FrameV),
                Normal) > 0.998f;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverStablePrimaryTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.StablePrimaryAxis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverStablePrimaryTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    FDWCEditorSpatialData Data;
    Data.Triangles.AddDefaulted();
    const FVector3f Normal(1.0f, 0.0f, 0.0f);
    FDWCEditorResolvedSurfaceOrientation Orientation;
    TestTrue(
        TEXT("A stable surface resolves from the primary garment axis"),
        FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.2f, 0.3f, 0.5f),
            Normal,
            Policy,
            Orientation));
    TestEqual(
        TEXT("The stable source is the primary axis"),
        Orientation.Source,
        EDWCEditorSurfaceOrientationSource::PrimaryAxis);
    TestTrue(
        TEXT("The primary garment direction is mesh-local up"),
        FVector3f::DotProduct(Orientation.FrameV, Policy.PrimaryAxis) > 0.999f);
    TestTrue(TEXT("The stable frame is orthonormal"), IsRightHandedResolverFrame(Orientation, Normal));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverTopologyFallbackTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.TopologyFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverTopologyFallbackTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    const FDWCEditorSpatialData Data = MakeResolverTestData(
        Policy,
        FVector3f(0.0f, 1.0f, 0.0f));
    const FVector3f Normal(0.0f, 0.0f, 1.0f);
    FDWCEditorResolvedSurfaceOrientation Orientation;
    TestTrue(
        TEXT("A horizontal surface resolves from its cached topology field"),
        FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.5f, 0.25f, 0.25f),
            Normal,
            Policy,
            Orientation));
    TestEqual(
        TEXT("The horizontal source is the topology fallback"),
        Orientation.Source,
        EDWCEditorSurfaceOrientationSource::TopologyFallback);
    TestTrue(
        TEXT("The cached topology direction is preserved"),
        FVector3f::DotProduct(Orientation.FrameV, FVector3f(0.0f, 1.0f, 0.0f)) > 0.995f);
    TestTrue(TEXT("The fallback frame is orthonormal"), IsRightHandedResolverFrame(Orientation, Normal));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverBlendContinuityTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.BlendContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverBlendContinuityTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    const FDWCEditorSpatialData Data = MakeResolverTestData(
        Policy,
        FVector3f(0.0f, 1.0f, 0.0f));
    const auto MakeTransitionNormal = [](const float Quality)
    {
        return FVector3f(Quality, 0.0f, FMath::Sqrt(1.0f - Quality * Quality));
    };
    FDWCEditorResolvedSurfaceOrientation Before;
    FDWCEditorResolvedSurfaceOrientation After;
    TestTrue(
        TEXT("The lower transition sample resolves"),
        FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.34f, 0.33f, 0.33f),
            MakeTransitionNormal(0.174f),
            Policy,
            Before));
    TestTrue(
        TEXT("The upper transition sample resolves"),
        FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.34f, 0.33f, 0.33f),
            MakeTransitionNormal(0.176f),
            Policy,
            After));
    TestEqual(
        TEXT("Transition samples use blended topology fallback"),
        Before.Source,
        EDWCEditorSurfaceOrientationSource::BlendedTopologyFallback);
    TestTrue(
        TEXT("A small normal change cannot cause an orientation flip"),
        FVector3f::DotProduct(Before.FrameV, After.FrameV) > 0.999f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverContractFallbackTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.IncompatibleFieldFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverContractFallbackTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    const FDWCEditorSpatialData Data = MakeResolverTestData(
        Policy,
        FVector3f(0.0f, 1.0f, 0.0f),
        false);
    FDWCEditorResolvedSurfaceOrientation Orientation;
    TestTrue(
        TEXT("An incompatible field still has a deterministic finite fallback"),
        FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f),
            Policy,
            Orientation));
    TestEqual(
        TEXT("An incompatible field cannot be consumed as topology data"),
        Orientation.Source,
        EDWCEditorSurfaceOrientationSource::DeterministicSecondaryFallback);
    TestTrue(
        TEXT("The policy secondary axis is used as the final fallback"),
        FVector3f::DotProduct(Orientation.FrameV, Policy.SecondaryAxis) > 0.999f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverThresholdSweepTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.ThresholdSweepContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverThresholdSweepTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    FDWCEditorSpatialData Data = MakeResolverTestData(
        Policy,
        FVector3f(0.0f, 1.0f, 0.0f));
    const uint64 InitialAllocatedBytes = Data.GetAllocatedSizeBytes();
    const int32* InitialLookupData = Data.SurfaceOrientationField.EntryIndexByTriangle.GetData();
    const FDWCEditorSurfaceOrientationFieldEntry* InitialEntryData =
        Data.SurfaceOrientationField.Entries.GetData();

    FDWCEditorResolvedSurfaceOrientation Previous;
    bool bHasPrevious = false;
    for (int32 SampleIndex = 0; SampleIndex <= 70; ++SampleIndex)
    {
        const float Quality = static_cast<float>(SampleIndex) * 0.005f;
        const FVector3f Normal(
            Quality,
            0.0f,
            FMath::Sqrt(FMath::Max(1.0f - Quality * Quality, 0.0f)));
        FDWCEditorResolvedSurfaceOrientation First;
        FDWCEditorResolvedSurfaceOrientation Repeated;
        const bool bFirstResolved = FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.2f, 0.3f, 0.5f),
            Normal,
            Policy,
            First);
        const bool bRepeatedResolved = FDWCEditorSurfaceOrientationResolver::Resolve(
            Data,
            0,
            FVector3f(0.2f, 0.3f, 0.5f),
            Normal,
            Policy,
            Repeated);
        if (!bFirstResolved || !bRepeatedResolved)
        {
            AddError(FString::Printf(TEXT("Orientation sample %d did not resolve."), SampleIndex));
            return false;
        }
        if (FVector3f::DotProduct(First.FrameU, Repeated.FrameU) <= 0.99999f ||
            FVector3f::DotProduct(First.FrameV, Repeated.FrameV) <= 0.99999f)
        {
            AddError(FString::Printf(
                TEXT("Orientation sample %d was not deterministic."),
                SampleIndex));
            return false;
        }
        if (bHasPrevious &&
            FVector3f::DotProduct(Previous.FrameV, First.FrameV) <= 0.99f)
        {
            AddError(FString::Printf(
                TEXT("Orientation flipped between threshold samples %d and %d."),
                SampleIndex - 1,
                SampleIndex));
            return false;
        }
        Previous = First;
        bHasPrevious = true;
    }

    TestEqual(
        TEXT("Repeated resolver calls do not change spatial allocation bytes"),
        Data.GetAllocatedSizeBytes(),
        InitialAllocatedBytes);
    TestTrue(
        TEXT("Repeated resolver calls do not replace the sparse lookup allocation"),
        Data.SurfaceOrientationField.EntryIndexByTriangle.GetData() == InitialLookupData);
    TestTrue(
        TEXT("Repeated resolver calls do not replace the sparse entry allocation"),
        Data.SurfaceOrientationField.Entries.GetData() == InitialEntryData);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationResolverBarycentricContinuityTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.Resolver.BarycentricContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationResolverBarycentricContinuityTest::RunTest(const FString&)
{
    const FDWCEditorSurfaceOrientationPolicy Policy = MakeResolverTestPolicy();
    FDWCEditorSpatialData Data = MakeResolverTestData(
        Policy,
        FVector3f(1.0f, 0.0f, 0.0f));
    FDWCEditorSurfaceOrientationFieldEntry& Entry = Data.SurfaceOrientationField.Entries[0];
    Entry.CornerFallbackV[0] = FPackedNormal(FVector3f(1.0f, 0.0f, 0.0f));
    Entry.CornerFallbackV[1] = FPackedNormal(
        FVector3f(1.0f, 0.25f, 0.0f).GetSafeNormal());
    Entry.CornerFallbackV[2] = FPackedNormal(
        FVector3f(1.0f, -0.25f, 0.0f).GetSafeNormal());

    const FVector3f Normal(0.0f, 0.0f, 1.0f);
    FDWCEditorResolvedSurfaceOrientation Previous;
    bool bHasPrevious = false;
    for (int32 SampleIndex = 0; SampleIndex <= 100; ++SampleIndex)
    {
        const float T = static_cast<float>(SampleIndex) / 100.0f;
        FDWCEditorResolvedSurfaceOrientation Current;
        if (!FDWCEditorSurfaceOrientationResolver::Resolve(
                Data,
                0,
                FVector3f(0.0f, 1.0f - T, T),
                Normal,
                Policy,
                Current))
        {
            AddError(FString::Printf(TEXT("Barycentric sample %d did not resolve."), SampleIndex));
            return false;
        }
        if (bHasPrevious &&
            FVector3f::DotProduct(Previous.FrameV, Current.FrameV) <= 0.999f)
        {
            AddError(FString::Printf(
                TEXT("Fallback orientation flipped between barycentric samples %d and %d."),
                SampleIndex - 1,
                SampleIndex));
            return false;
        }
        Previous = Current;
        bHasPrevious = true;
    }
    return true;
}

#endif
