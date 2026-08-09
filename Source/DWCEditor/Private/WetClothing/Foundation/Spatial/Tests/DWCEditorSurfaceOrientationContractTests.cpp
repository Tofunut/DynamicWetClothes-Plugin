//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationPolicyContractTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.PolicyContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationPolicyContractTest::RunTest(const FString&)
{
    FDWCEditorSurfaceOrientationPolicy DefaultPolicy;
    DefaultPolicy.Normalize();
    TestTrue(TEXT("The default surface orientation policy is valid"), DefaultPolicy.IsValid());
    const uint32 DefaultSignature = DefaultPolicy.BuildSignature();
    TestTrue(TEXT("A normalized policy has a non-zero signature"), DefaultSignature != 0);

    FDWCEditorSurfaceOrientationPolicy RepeatedPolicy;
    RepeatedPolicy.Normalize();
    TestEqual(
        TEXT("Equivalent policies produce the same signature"),
        RepeatedPolicy.BuildSignature(),
        DefaultSignature);

    FDWCEditorSurfaceOrientationPolicy InvalidPolicy;
    InvalidPolicy.PrimaryAxis = FVector3f::ZeroVector;
    InvalidPolicy.SecondaryAxis = FVector3f(0.0f, 0.0f, 5.0f);
    InvalidPolicy.FallbackFullQuality = 0.9f;
    InvalidPolicy.FallbackBeginQuality = 0.2f;
    InvalidPolicy.Normalize();
    TestTrue(TEXT("Invalid axes and thresholds normalize to a valid policy"), InvalidPolicy.IsValid());
    TestTrue(
        TEXT("Normalized policy axes are orthogonal"),
        FMath::IsNearlyZero(
            FVector3f::DotProduct(InvalidPolicy.PrimaryAxis, InvalidPolicy.SecondaryAxis),
            0.001f));

    FDWCEditorSurfaceOrientationPolicy NonFinitePolicy;
    NonFinitePolicy.PrimaryAxis.X = std::numeric_limits<float>::quiet_NaN();
    NonFinitePolicy.FallbackBeginQuality = std::numeric_limits<float>::quiet_NaN();
    NonFinitePolicy.Normalize();
    TestTrue(TEXT("Non-finite policy values normalize to a valid contract"), NonFinitePolicy.IsValid());

    FDWCEditorSurfaceOrientationPolicy ChangedPolicy = DefaultPolicy;
    ChangedPolicy.FallbackBeginQuality = 0.35f;
    ChangedPolicy.Normalize();
    TestNotEqual(
        TEXT("A policy change invalidates its signature"),
        ChangedPolicy.BuildSignature(),
        DefaultSignature);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSurfaceOrientationSparseFieldContractTest,
    "DWC.Editor.Foundation.Spatial.SurfaceOrientation.SparseFieldContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSurfaceOrientationSparseFieldContractTest::RunTest(const FString&)
{
    FDWCEditorSurfaceOrientationPolicy Policy;
    Policy.Normalize();

    FDWCEditorSurfaceOrientationField Field;
    TestTrue(TEXT("A default sparse field is empty"), Field.IsEmpty());
    TestNull(TEXT("An empty sparse field has no triangle entry"), Field.FindByTriangleIndex(0));
    TestTrue(TEXT("An empty sparse field has a valid empty contract"), Field.ValidateContract(4));

    Field.PolicySignature = Policy.BuildSignature();
    Field.FieldLayoutVersion = DWCEditorSurfaceOrientationVersion::FieldLayout;
    Field.BuildStatus = EDWCEditorSurfaceOrientationFieldBuildStatus::Ready;
    Field.EntryIndexByTriangle.Init(INDEX_NONE, 4);
    FDWCEditorSurfaceOrientationFieldEntry& Entry = Field.Entries.AddDefaulted_GetRef();
    Entry.TriangleIndex = 2;
    Entry.CornerFallbackV[0] = FPackedNormal(FVector3f(1.0f, 0.0f, 0.0f));
    Entry.CornerFallbackV[1] = FPackedNormal(FVector3f(0.98f, 0.2f, 0.0f).GetSafeNormal());
    Entry.CornerFallbackV[2] = FPackedNormal(FVector3f(0.98f, -0.2f, 0.0f).GetSafeNormal());
    Field.EntryIndexByTriangle[2] = 0;

    FString ContractError;
    TestTrue(
        TEXT("A sparse triangle entry satisfies the field contract"),
        Field.ValidateContract(4, &ContractError));
    TestTrue(TEXT("The sparse field matches its policy"), Field.IsCompatible(Policy.BuildSignature()));
    TestNotNull(TEXT("The authored triangle resolves its sparse entry"), Field.FindByTriangleIndex(2));
    TestNull(TEXT("A triangle without fallback data resolves no entry"), Field.FindByTriangleIndex(1));
    TestTrue(TEXT("Sparse field allocations are included in its memory estimate"), Field.GetAllocatedSizeBytes() > 0);

    FDWCEditorSpatialData SpatialData;
    const uint64 EmptySpatialBytes = SpatialData.GetAllocatedSizeBytes();
    SpatialData.SurfaceOrientationField = Field;
    const uint64 CopiedFieldBytes = SpatialData.SurfaceOrientationField.GetAllocatedSizeBytes();
    TestEqual(
        TEXT("Spatial cache memory includes the copied orientation field allocation"),
        SpatialData.GetAllocatedSizeBytes() - EmptySpatialBytes,
        CopiedFieldBytes);

    FDWCEditorSurfaceOrientationField BrokenField = Field;
    BrokenField.EntryIndexByTriangle[2] = INDEX_NONE;
    TestFalse(
        TEXT("A mismatched sparse lookup is rejected"),
        BrokenField.ValidateContract(4, &ContractError));

    Field.Reset();
    TestTrue(TEXT("Reset releases sparse field arrays"), Field.GetAllocatedSizeBytes() == 0);
    TestTrue(TEXT("Reset clears the policy contract"), Field.PolicySignature == 0);
    return true;
}

#endif
