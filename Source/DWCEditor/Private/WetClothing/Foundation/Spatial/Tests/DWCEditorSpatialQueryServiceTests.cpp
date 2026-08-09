//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/SkeletalMeshComponent.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSpatialQueryTest,
    "DWC.Editor.Foundation.Spatial.RaycastUvAndAnchor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSpatialQueryTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorCacheStore> Store = MakeShared<FDWCEditorCacheStore>();
    FDWCEditorSpatialQueryService Service(Store);
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
        Triangle.LocalNormals[CornerIndex] = Triangle.LocalNormal;
        Triangle.LocalTangents[CornerIndex] = FVector3f(0.0f, 1.0f, 0.0f);
        Triangle.LocalBitangents[CornerIndex] = FVector3f(-1.0f, 0.0f, 0.0f);
    }
    const uint64 LookupKey = (static_cast<uint64>(4) << 32) | static_cast<uint32>(9);
    Data->TriangleLookup.Add(LookupKey, 0);
    FDWCEditorSurfaceOrientationPolicy OrientationPolicy;
    OrientationPolicy.Normalize();
    Data->SurfaceOrientationField.BuildStatus =
        EDWCEditorSurfaceOrientationFieldBuildStatus::Ready;
    Data->SurfaceOrientationField.PolicySignature = OrientationPolicy.BuildSignature();
    Data->SurfaceOrientationField.FieldLayoutVersion =
        DWCEditorSurfaceOrientationVersion::FieldLayout;
    Data->SurfaceOrientationField.EntryIndexByTriangle.Init(INDEX_NONE, 1);
    FDWCEditorSurfaceOrientationFieldEntry& OrientationEntry =
        Data->SurfaceOrientationField.Entries.AddDefaulted_GetRef();
    OrientationEntry.TriangleIndex = 0;
    for (FPackedNormal& CornerFallback : OrientationEntry.CornerFallbackV)
    {
        CornerFallback = FPackedNormal(FVector3f(1.0f, 0.0f, 0.0f));
    }
    Data->SurfaceOrientationField.EntryIndexByTriangle[0] = 0;

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
    TestTrue(
        TEXT("Ray hit preserves the render tangent independently"),
        FVector::DotProduct(Hit.LocalTangent, FVector::RightVector) > 0.999);
    TestTrue(
        TEXT("Ray hit resolves the topology-backed authoring frame"),
        FVector::DotProduct(Hit.LocalSurfaceFrameV, FVector::ForwardVector) > 0.999);

    FDWCEditorSurfaceHit ObliqueHit;
    const FVector TargetPoint(0.25, 0.25, 0.0);
    const FVector ObliqueOrigin(2.0, -1.0, 5.0);
    TestTrue(
        TEXT("An oblique ray reaches the same physical point"),
        Service.TraceSurface(
            Handle,
            Component,
            ObliqueOrigin,
            (TargetPoint - ObliqueOrigin).GetSafeNormal(),
            ObliqueHit));
    TestEqual(
        TEXT("Ray approach direction does not change the selected triangle"),
        ObliqueHit.TriangleID,
        Hit.TriangleID);
    TestTrue(
        TEXT("Ray approach direction does not change the local authoring frame"),
        FVector::DotProduct(ObliqueHit.LocalSurfaceFrameU, Hit.LocalSurfaceFrameU) > 0.9999 &&
            FVector::DotProduct(ObliqueHit.LocalSurfaceFrameV, Hit.LocalSurfaceFrameV) > 0.9999);

    TArray<FDWCEditorProjectedSurface> Surfaces;
    Service.FindSurfacesAtUV(Handle, Component, FVector2D(0.25, 0.25), Surfaces);
    TestEqual(TEXT("UV projection finds one surface"), Surfaces.Num(), 1);
    TestTrue(
        TEXT("Ray and UV projection share the same authoring frame"),
        Surfaces.Num() == 1 &&
            FVector::DotProduct(
                Hit.LocalSurfaceFrameU,
                Surfaces[0].LocalSurfaceFrameU) > 0.999);

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
    TestTrue(
        TEXT("Stored anchors resolve the same authoring frame as hover"),
        FVector::DotProduct(Hit.LocalSurfaceFrameU, Anchor.LocalSurfaceFrameU) > 0.999);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSpatialTopologyContractTest,
    "DWC.Editor.Foundation.Spatial.SurfaceTopologyContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSpatialTopologyContractTest::RunTest(const FString& Parameters)
{
    FDWCEditorSpatialData Data;
    Data.Triangles.Reserve(2);
    FDWCEditorSpatialTriangle& TriangleA = Data.Triangles.AddDefaulted_GetRef();
    TriangleA.MaterialSlotIndex = 4;
    TriangleA.TriangleID = 10;
    TriangleA.UVIslandID = 1;
    TriangleA.TopologyVertexIDs[0] = 0;
    TriangleA.TopologyVertexIDs[1] = 1;
    TriangleA.TopologyVertexIDs[2] = 2;
    TriangleA.UVs[0] = FVector2f(0.0f, 0.0f);
    TriangleA.UVs[1] = FVector2f(0.5f, 0.0f);
    TriangleA.UVs[2] = FVector2f(0.5f, 0.5f);

    FDWCEditorSpatialTriangle& TriangleB = Data.Triangles.AddDefaulted_GetRef();
    TriangleB.MaterialSlotIndex = 4;
    TriangleB.TriangleID = 11;
    TriangleB.UVIslandID = 2;
    TriangleB.TopologyVertexIDs[0] = 2;
    TriangleB.TopologyVertexIDs[1] = 1;
    TriangleB.TopologyVertexIDs[2] = 3;
    TriangleB.UVs[0] = FVector2f(0.8f, 0.8f);
    TriangleB.UVs[1] = FVector2f(0.8f, 0.3f);
    TriangleB.UVs[2] = FVector2f(1.0f, 0.8f);

    FDWCEditorSpatialQueryService::BuildTriangleTopology(Data);
    TestEqual(
        TEXT("A physical edge split across UV islands is classified as a seam"),
        TriangleA.EdgeTypes[1],
        EDWCEditorSpatialEdgeType::UVSeam);
    TestEqual(
        TEXT("Seam adjacency points at the connected triangle"),
        TriangleA.AdjacentTriangleIndices[1],
        1);
    TestEqual(
        TEXT("The reverse seam edge points back to the source triangle"),
        TriangleB.AdjacentTriangleIndices[0],
        0);
    TestEqual(
        TEXT("An unshared physical edge remains a boundary"),
        TriangleA.EdgeTypes[0],
        EDWCEditorSpatialEdgeType::Boundary);

    FDWCEditorSpatialData RegularData;
    RegularData.Triangles = Data.Triangles;
    RegularData.Triangles[1].UVIslandID = RegularData.Triangles[0].UVIslandID;
    RegularData.Triangles[1].UVs[0] = RegularData.Triangles[0].UVs[2];
    RegularData.Triangles[1].UVs[1] = RegularData.Triangles[0].UVs[1];
    FDWCEditorSpatialQueryService::BuildTriangleTopology(RegularData);
    TestEqual(
        TEXT("A physically and UV-contiguous edge is classified as regular"),
        RegularData.Triangles[0].EdgeTypes[1],
        EDWCEditorSpatialEdgeType::Regular);

    FDWCEditorSpatialData NonManifoldData;
    NonManifoldData.Triangles.Reserve(3);
    for (int32 TriangleIndex = 0; TriangleIndex < 3; ++TriangleIndex)
    {
        FDWCEditorSpatialTriangle& Triangle = NonManifoldData.Triangles.AddDefaulted_GetRef();
        Triangle.TopologyVertexIDs[0] = 10;
        Triangle.TopologyVertexIDs[1] = 11;
        Triangle.TopologyVertexIDs[2] = 20 + TriangleIndex;
    }
    FDWCEditorSpatialQueryService::BuildTriangleTopology(NonManifoldData);
    TestEqual(
        TEXT("A non-manifold edge is blocked instead of choosing an arbitrary neighbor"),
        NonManifoldData.Triangles[0].EdgeTypes[0],
        EDWCEditorSpatialEdgeType::Blocked);
    TestEqual(
        TEXT("Blocked non-manifold edges have no adjacency"),
        NonManifoldData.Triangles[0].AdjacentTriangleIndices[0],
        INDEX_NONE);

    FVector3f NormalizedBarycentric;
    TestTrue(
        TEXT("Small barycentric edge error is normalized"),
        FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
            FVector3f(-0.0001f, 0.4f, 0.6001f),
            NormalizedBarycentric));
    TestTrue(
        TEXT("Normalized barycentric coordinates sum to one"),
        FMath::IsNearlyEqual(
            NormalizedBarycentric.X + NormalizedBarycentric.Y + NormalizedBarycentric.Z,
            1.0f));
    TestFalse(
        TEXT("A materially invalid barycentric anchor is rejected"),
        FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
            FVector3f(-0.2f, 0.6f, 0.6f),
            NormalizedBarycentric));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorSpatialLeaseLifetimeTest,
    "DWC.Editor.Foundation.Spatial.LeaseSurvivesInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorSpatialLeaseLifetimeTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorCacheStore> Store = MakeShared<FDWCEditorCacheStore>();
    FDWCEditorSpatialQueryService Service(Store);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
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
    FDWCEditorSurfaceHit Hit;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorStableSurfaceFrameTest,
    "DWC.Editor.Foundation.Spatial.StableSurfaceFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorStableSurfaceFrameTest::RunTest(const FString& Parameters)
{
    FVector3f FrameU;
    FVector3f FrameV;
    TestTrue(
        TEXT("A valid normal and preferred tangent build a surface frame"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(0.0f, 0.0f, 1.0f),
            FVector3f(2.0f, 0.0f, 0.5f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FrameU,
            FrameV));
    TestTrue(TEXT("Surface frame U is normalized"), FMath::IsNearlyEqual(FrameU.Size(), 1.0f));
    TestTrue(TEXT("Surface frame V is normalized"), FMath::IsNearlyEqual(FrameV.Size(), 1.0f));
    TestTrue(TEXT("Surface frame axes are orthogonal"),
        FMath::IsNearlyZero(FVector3f::DotProduct(FrameU, FrameV), 0.001f));
    TestTrue(TEXT("Surface frame U is tangent to the surface"),
        FMath::IsNearlyZero(FVector3f::DotProduct(FrameU, FVector3f(0.0f, 0.0f, 1.0f)), 0.001f));

    FVector3f ContinuedU;
    FVector3f ContinuedV;
    TestTrue(
        TEXT("The previous frame transports onto a neighboring smooth normal"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(0.0f, 0.2f, 0.98f).GetSafeNormal(),
            FrameU,
            FrameV,
            ContinuedU,
            ContinuedV));
    TestTrue(TEXT("Transport preserves frame sign continuity"),
        FVector3f::DotProduct(FrameU, ContinuedU) > 0.0f);

    FVector3f ParallelFallbackU;
    FVector3f ParallelFallbackV;
    TestTrue(
        TEXT("A preferred direction parallel to the normal uses a deterministic tangent fallback"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(0.0f, 0.0f, 1.0f),
            FVector3f(0.0f, 0.0f, 4.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            ParallelFallbackU,
            ParallelFallbackV));
    TestTrue(
        TEXT("The fallback frame honors the preferred bitangent sign"),
        FVector3f::DotProduct(ParallelFallbackV, FVector3f(0.0f, 1.0f, 0.0f)) > 0.999f);

    FVector3f RepeatedFallbackU;
    FVector3f RepeatedFallbackV;
    TestTrue(
        TEXT("Repeated fallback construction succeeds"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(0.0f, 0.0f, 1.0f),
            FVector3f(0.0f, 0.0f, 4.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            RepeatedFallbackU,
            RepeatedFallbackV));
    TestTrue(
        TEXT("Repeated fallback construction cannot rotate or flip the frame"),
        ParallelFallbackU.Equals(RepeatedFallbackU, UE_KINDA_SMALL_NUMBER) &&
            ParallelFallbackV.Equals(RepeatedFallbackV, UE_KINDA_SMALL_NUMBER));

    FVector3f MirroredU;
    FVector3f MirroredV;
    TestTrue(
        TEXT("A mirrored preferred bitangent still builds a stable frame"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(0.0f, 0.0f, 1.0f),
            FrameU,
            -FrameV,
            MirroredU,
            MirroredV));
    TestTrue(
        TEXT("The mirrored bitangent flips both axes together and preserves handedness"),
        FVector3f::DotProduct(MirroredV, -FrameV) > 0.999f &&
            FVector3f::DotProduct(
                FVector3f::CrossProduct(MirroredU, MirroredV),
                FVector3f(0.0f, 0.0f, 1.0f)) > 0.999f);

    TestFalse(
        TEXT("A degenerate normal cannot build a surface frame"),
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f::ZeroVector,
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FrameU,
            FrameV));
    return true;
}

#endif
