// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FDWCRevealBakeSurfaceTriangle MakeImportedBasisTriangle(const int8 BitangentSign = 1)
    {
        FDWCRevealBakeSurfaceTriangle Triangle;
        Triangle.bHasValidImportedTangentBasis = true;
        Triangle.Positions[0] = FVector(0.0, 0.0, 0.0);
        Triangle.Positions[1] = FVector(1.0, 0.0, 0.0);
        Triangle.Positions[2] = FVector(0.0, 1.0, 0.0);
        Triangle.UVs[0] = FVector2D(0.0, 0.0);
        Triangle.UVs[1] = FVector2D(1.0, 0.0);
        Triangle.UVs[2] = FVector2D(0.0, 1.0);
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.Normals[CornerIndex] = FVector3f(0.0f, 0.0f, 1.0f);
            Triangle.Tangents[CornerIndex] = FVector3f(1.0f, 0.0f, 0.0f);
            Triangle.BitangentSigns[CornerIndex] = BitangentSign;
        }
        return Triangle;
    }

    bool FramesMatch(
        const FDWCRevealBakeSurfaceFrame& A,
        const FDWCRevealBakeSurfaceFrame& B,
        const double Tolerance = 1.0e-6)
    {
        return A.Tangent.Equals(B.Tangent, Tolerance) &&
            A.Bitangent.Equals(B.Bitangent, Tolerance) &&
            A.Normal.Equals(B.Normal, Tolerance);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRevealBakeImportedBasisIgnoresRasterUVTest,
    "DWC.Transparency.RevealBake.ImportedBasisIgnoresRasterUV",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRevealBakeImportedBasisIgnoresRasterUVTest::RunTest(const FString&)
{
    FDWCRevealBakeSurfaceTriangle Triangle = MakeImportedBasisTriangle();
    const FVector Barycentric(0.2, 0.3, 0.5);
    FDWCRevealBakeSurfaceFrame OriginalFrame;
    TestTrue(
        TEXT("The imported frame builds for the original raster UV."),
        FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
            Triangle,
            Barycentric,
            OriginalFrame));

    Triangle.UVs[0] = FVector2D(8.0, -4.0);
    Triangle.UVs[1] = FVector2D(-3.0, 9.0);
    Triangle.UVs[2] = FVector2D(12.0, 7.0);
    FDWCRevealBakeSurfaceFrame RepackedFrame;
    TestTrue(
        TEXT("The imported frame builds after the DWC raster UV is repacked."),
        FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
            Triangle,
            Barycentric,
            RepackedFrame));
    TestTrue(
        TEXT("DWC raster UV changes do not rotate the imported surface frame."),
        FramesMatch(OriginalFrame, RepackedFrame));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRevealBakeImportedHandednessTest,
    "DWC.Transparency.RevealBake.ImportedHandedness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRevealBakeImportedHandednessTest::RunTest(const FString&)
{
    const FDWCRevealBakeSurfaceTriangle Triangle = MakeImportedBasisTriangle(-1);
    FDWCRevealBakeSurfaceFrame Frame;
    TestTrue(
        TEXT("A mirrored imported basis remains valid."),
        FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
            Triangle,
            FVector(1.0 / 3.0),
            Frame));
    TestTrue(
        TEXT("Imported handedness controls the reconstructed bitangent."),
        Frame.Bitangent.Equals(FVector(0.0, -1.0, 0.0), 1.0e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRevealBakeImportedFrameReorientationTest,
    "DWC.Transparency.RevealBake.ImportedFrameReorientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRevealBakeImportedFrameReorientationTest::RunTest(const FString&)
{
    FDWCRevealBakeSurfaceFrame SourceFrame;
    SourceFrame.Tangent = FVector::ForwardVector;
    SourceFrame.Bitangent = FVector::RightVector;
    SourceFrame.Normal = FVector::UpVector;

    FDWCRevealBakeSurfaceFrame TargetFrame;
    TargetFrame.Tangent = FVector::RightVector;
    TargetFrame.Bitangent = -FVector::ForwardVector;
    TargetFrame.Normal = FVector::UpVector;

    const FVector3f Reoriented =
        FDWCRevealBakeSurfaceFrameBuilder::ReorientTangentNormal(
            FVector3f(1.0f, 0.0f, 0.0f),
            SourceFrame,
            TargetFrame);
    TestTrue(
        TEXT("A source +X tangent normal is expressed in the target imported frame."),
        Reoriented.Equals(FVector3f(0.0f, -1.0f, 0.0f), 1.0e-5f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRevealBakeImportedBasisTransformTest,
    "DWC.Transparency.RevealBake.ImportedBasisTransform",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRevealBakeImportedBasisTransformTest::RunTest(const FString&)
{
    FVector3f Tangent;
    FVector3f Normal;
    int8 BitangentSign = 0;
    const FTransform NonUniformTransform(
        FQuat(FVector::UpVector, UE_HALF_PI),
        FVector::ZeroVector,
        FVector(2.0, 3.0, 0.5));
    TestTrue(
        TEXT("A rotated non-uniform placement produces a valid imported basis."),
        FDWCRevealBakeSurfaceFrameBuilder::TransformImportedBasis(
            NonUniformTransform,
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f),
            Tangent,
            Normal,
            BitangentSign));
    TestTrue(TEXT("The tangent follows the placement rotation."),
        Tangent.Equals(FVector3f(0.0f, 1.0f, 0.0f), 1.0e-5f));
    TestTrue(TEXT("The inverse-transposed normal remains orthogonal."),
        Normal.Equals(FVector3f(0.0f, 0.0f, 1.0f), 1.0e-5f));
    TestEqual(TEXT("A positive-scale transform preserves handedness."), BitangentSign, static_cast<int8>(1));

    TestTrue(
        TEXT("A mirrored placement still produces a valid imported basis."),
        FDWCRevealBakeSurfaceFrameBuilder::TransformImportedBasis(
            FTransform(FQuat::Identity, FVector::ZeroVector, FVector(-1.0, 1.0, 1.0)),
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f),
            Tangent,
            Normal,
            BitangentSign));
    TestEqual(TEXT("A mirrored placement flips imported handedness."), BitangentSign, static_cast<int8>(-1));

    TestFalse(
        TEXT("A singular placement is rejected."),
        FDWCRevealBakeSurfaceFrameBuilder::TransformImportedBasis(
            FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.0, 0.0, 1.0)),
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f),
            Tangent,
            Normal,
            BitangentSign));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRevealBakeInvalidImportedBasisFallbackTest,
    "DWC.Transparency.RevealBake.InvalidImportedBasisFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRevealBakeInvalidImportedBasisFallbackTest::RunTest(const FString&)
{
    FDWCRevealBakeSurfaceTriangle Triangle = MakeImportedBasisTriangle();
    Triangle.bHasValidImportedTangentBasis = false;
    FDWCRevealBakeSurfaceFrame Frame;
    TestFalse(
        TEXT("An invalid imported basis is rejected instead of deriving a frame from the raster UV."),
        FDWCRevealBakeSurfaceFrameBuilder::BuildInterpolatedFrame(
            Triangle,
            FVector(1.0 / 3.0),
            Frame));
    TestEqual(
        TEXT("The imported basis producer version is explicit."),
        FDWCTransparencySignatureService::RevealSurfaceBasisVersion,
        2);
    return true;
}

#endif
