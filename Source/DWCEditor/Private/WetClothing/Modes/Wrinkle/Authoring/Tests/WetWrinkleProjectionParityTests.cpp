//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationFieldBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationResolver.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"

namespace WetWrinkleProjectionParityTestsPrivate
{
    constexpr int32 MaterialSlotIndex = 4;
    constexpr int32 UVChannelIndex = 2;
    constexpr int32 QuadsX = 8;
    constexpr int32 QuadsY = 4;
    constexpr int32 ColumnsPerIsland = 4;
    constexpr int32 FirstTriangleID = 100;

    uint64 MakeTriangleLookupKey(const int32 SlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(SlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    void FinalizeTriangle(FDWCEditorSpatialTriangle& Triangle)
    {
        Triangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
        Triangle.LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
        Triangle.LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
        Triangle.LocalSurfaceAxisU = Triangle.LocalTangent;
        Triangle.LocalSurfaceAxisV = Triangle.LocalBitangent;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.LocalNormals[CornerIndex] = Triangle.LocalNormal;
            Triangle.LocalTangents[CornerIndex] = Triangle.LocalTangent;
            Triangle.LocalBitangents[CornerIndex] = Triangle.LocalBitangent;
            Triangle.LocalBounds += Triangle.LocalPositions[CornerIndex];
            Triangle.UVBounds += Triangle.UVs[CornerIndex];
        }
    }

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> BuildSeamedSurface()
    {
        TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> Data =
            MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
        Data->LODIndex = 0;
        Data->UVChannelIndex = UVChannelIndex;
        Data->MaterialSlotIndex = MaterialSlotIndex;
        Data->Triangles.Reserve(QuadsX * QuadsY * 2);

        auto VertexID = [](const int32 X, const int32 Y)
        {
            return static_cast<int64>(Y * (QuadsX + 1) + X);
        };
        auto Position = [](const int32 X, const int32 Y)
        {
            return FVector3f(
                static_cast<float>(X) / static_cast<float>(QuadsX),
                static_cast<float>(Y) / static_cast<float>(QuadsY),
                0.0f);
        };

        int32 TriangleID = FirstTriangleID;
        for (int32 Y = 0; Y < QuadsY; ++Y)
        {
            for (int32 X = 0; X < QuadsX; ++X)
            {
                const int32 IslandID = X / ColumnsPerIsland;
                const int32 IslandStartX = IslandID * ColumnsPerIsland;
                auto UV = [IslandID, IslandStartX](const int32 VertexX, const int32 VertexY)
                {
                    constexpr float IslandWidth = 0.42f;
                    constexpr float IslandGap = 0.08f;
                    const float IslandStartU = 0.04f +
                        static_cast<float>(IslandID) * (IslandWidth + IslandGap);
                    const float LocalU = static_cast<float>(VertexX - IslandStartX) /
                        static_cast<float>(ColumnsPerIsland);
                    return FVector2f(
                        IslandStartU + LocalU * IslandWidth,
                        0.05f + static_cast<float>(VertexY) /
                            static_cast<float>(QuadsY) * 0.90f);
                };
                auto AddTriangle = [&](const int32 X0, const int32 Y0,
                                       const int32 X1, const int32 Y1,
                                       const int32 X2, const int32 Y2)
                {
                    FDWCEditorSpatialTriangle& Triangle = Data->Triangles.AddDefaulted_GetRef();
                    Triangle.MaterialSlotIndex = MaterialSlotIndex;
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

    UTexture2D* MakeNormalTexture()
    {
        UTexture2D* Texture = NewObject<UTexture2D>(GetTransientPackage());
        const FColor Pixel(128, 128, 255, 255);
        Texture->Source.Init(
            1, 1, 1, 1, TSF_BGRA8,
            reinterpret_cast<const uint8*>(&Pixel));
        return Texture;
    }

    int32 GetAnchorTriangleID()
    {
        constexpr int32 AnchorX = ColumnsPerIsland - 1;
        constexpr int32 AnchorY = QuadsY / 2;
        return FirstTriangleID + (AnchorY * QuadsX + AnchorX) * 2;
    }

    bool AreFragmentsEquivalent(
        const TArray<FDWCEditorSurfacePatchFragment>& A,
        const TArray<FDWCEditorSurfacePatchFragment>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 FragmentIndex = 0; FragmentIndex < A.Num(); ++FragmentIndex)
        {
            const FDWCEditorSurfacePatchFragment& Left = A[FragmentIndex];
            const FDWCEditorSurfacePatchFragment& Right = B[FragmentIndex];
            if (Left.TriangleID != Right.TriangleID ||
                Left.UVIslandID != Right.UVIslandID)
            {
                return false;
            }
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                if (!Left.PatchCoordinates[CornerIndex].Equals(
                        Right.PatchCoordinates[CornerIndex], UE_KINDA_SMALL_NUMBER) ||
                    !Left.TargetUVs[CornerIndex].Equals(
                        Right.TargetUVs[CornerIndex], UE_KINDA_SMALL_NUMBER) ||
                    !Left.PatchAxisUInTargetTangent[CornerIndex].Equals(
                        Right.PatchAxisUInTargetTangent[CornerIndex], UE_KINDA_SMALL_NUMBER) ||
                    !Left.PatchAxisVInTargetTangent[CornerIndex].Equals(
                        Right.PatchAxisVInTargetTangent[CornerIndex], UE_KINDA_SMALL_NUMBER) ||
                    !FMath::IsNearlyEqual(
                        Left.SignedProjectionDepth[CornerIndex],
                        Right.SignedProjectionDepth[CornerIndex]) ||
                    !Left.SurfaceNormalInProjectorSpace[CornerIndex].Equals(
                        Right.SurfaceNormalInProjectorSpace[CornerIndex], UE_KINDA_SMALL_NUMBER))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool RunPreviewCommitBakeParityScenario(
        FAutomationTestBase& Test,
        const EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy)
    {
        const TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> SpatialData =
            BuildSeamedSurface();
        FDWCEditorSurfaceOrientationPolicy OrientationPolicy;
        OrientationPolicy.Normalize();
        FString OrientationWarning;
        if (!Test.TestTrue(
                TEXT("The synthetic seam surface builds its canonical orientation field"),
                FDWCEditorSurfaceOrientationFieldBuilder::Build(
                    SpatialData->Triangles,
                    OrientationPolicy,
                    SpatialData->SurfaceOrientationField,
                    &OrientationWarning)))
        {
            if (!OrientationWarning.IsEmpty())
            {
                Test.AddError(OrientationWarning);
            }
            return false;
        }
        UTexture2D* NormalTexture = MakeNormalTexture();

        FWetWrinkleBrushSettings Brush;
        Brush.MaterialSlotIndex = MaterialSlotIndex;
        Brush.UVChannelIndex = UVChannelIndex;
        Brush.WrinkleNormalTexture = NormalTexture;
        Brush.PatchDiameterLocal = 0.72f;
        Brush.BrushRadiusUV = 0.08f;
        Brush.RotationRadians = 0.37f;
        Brush.Strength = 0.85f;
        Brush.Falloff = 0.2f;
        Brush.PatchProjection.BoundaryPolicy = BoundaryPolicy;
        Brush.PatchProjection.ProjectionDepthLocal = 0.3f;
        Brush.PatchProjection.MaxSurfaceAngleDegrees = 63.0f;
        Brush.PatchProjection.ProjectionDepthSoftness = 0.35f;
        Brush.PatchProjection.ProjectionAngleSoftness = 0.25f;

        FWetWrinkleSurfaceHit Hit;
        Hit.bHit = true;
        Hit.MaterialSlotIndex = MaterialSlotIndex;
        Hit.UVChannelIndex = UVChannelIndex;
        Hit.TriangleID = GetAnchorTriangleID();
        Hit.UVIslandID = 0;
        Hit.Barycentric = FVector(0.1, 0.45, 0.45);
        Hit.UV = FVector2D(0.44, 0.55);
        Hit.LocalNormal = FVector::UpVector;
        const uint64 AnchorLookupKey = MakeTriangleLookupKey(
            MaterialSlotIndex,
            Hit.TriangleID);
        const int32* AnchorTriangleIndex = SpatialData->TriangleLookup.Find(AnchorLookupKey);
        FDWCEditorResolvedSurfaceOrientation ResolvedOrientation;
        if (!Test.TestTrue(
                TEXT("The hover anchor resolves its cached canonical orientation"),
                AnchorTriangleIndex != nullptr &&
                    FDWCEditorSurfaceOrientationResolver::Resolve(
                        *SpatialData,
                        *AnchorTriangleIndex,
                        FVector3f(Hit.Barycentric),
                        FVector3f(Hit.LocalNormal),
                        OrientationPolicy,
                        ResolvedOrientation)))
        {
            return false;
        }
        Hit.LocalSurfaceFrameU = FVector(ResolvedOrientation.FrameU);
        Hit.LocalSurfaceFrameV = FVector(ResolvedOrientation.FrameV);

        FDWCEditorWrinklePatchDescriptor HoverDescriptor;
        FString Error;
        if (!Test.TestTrue(
                TEXT("The hover hit builds a canonical patch descriptor"),
                FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
                    Hit, Brush, 17, HoverDescriptor, &Error)))
        {
            Test.AddError(Error);
            return false;
        }

        const uint32 HoverStableHash = HoverDescriptor.GetStableHash();
        FWetWrinklePatchPlacement Placement;
        if (!Test.TestTrue(
                TEXT("The presented hover descriptor commits to authored placement"),
                FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(
                    HoverDescriptor, Placement, &Error)))
        {
            Test.AddError(Error);
            return false;
        }

        FDWCEditorWrinklePatchDescriptor BakeDescriptor;
        if (!Test.TestTrue(
                TEXT("The committed placement rebuilds the bake descriptor"),
                FDWCEditorWrinklePatchDescriptorBuilder::BuildFromPlacement(
                    Placement, UVChannelIndex, BakeDescriptor, &Error)))
        {
            Test.AddError(Error);
            return false;
        }

        Test.TestEqual(
            TEXT("Hover and bake descriptors preserve the same stable contract"),
            BakeDescriptor.GetStableHash(), HoverStableHash);
        Test.TestEqual(
            TEXT("The committed boundary policy survives the authored round trip"),
            BakeDescriptor.ProjectionSettings.BoundaryPolicy, BoundaryPolicy);
        Test.TestTrue(
            TEXT("The committed anchor is identical to the presented anchor"),
            BakeDescriptor.AnchorBarycentric.Equals(
                HoverDescriptor.AnchorBarycentric, UE_KINDA_SMALL_NUMBER));
        Test.TestTrue(
            TEXT("The committed physical footprint is identical to the presented footprint"),
            BakeDescriptor.SurfaceHalfExtentLocal.Equals(
                HoverDescriptor.SurfaceHalfExtentLocal, UE_KINDA_SMALL_NUMBER));
        Test.TestTrue(
            TEXT("The canonical orientation survives hover, commit, and authored rebuild"),
            BakeDescriptor.SurfaceFrameU.Equals(
                ResolvedOrientation.FrameU, UE_KINDA_SMALL_NUMBER) &&
            BakeDescriptor.SurfaceFrameV.Equals(
                ResolvedOrientation.FrameV, UE_KINDA_SMALL_NUMBER));

        const FDWCEditorNormalSourceSnapshot NormalSource = MakeNormalSource();
        const FDWCEditorScalarSourceSnapshot CoverageSource = MakeCoverageSource();
        FDWCEditorSurfaceNormalPatchInput PreviewInput;
        FDWCEditorSurfaceNormalPatchInput BakeInput;
        if (!Test.TestTrue(
                TEXT("The hover descriptor builds the preview raster input"),
                FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
                    HoverDescriptor,
                    SpatialData,
                    NormalSource,
                    CoverageSource,
                    PreviewInput,
                    &Error)) ||
            !Test.TestTrue(
                TEXT("The committed descriptor builds the bake raster input"),
                FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
                    BakeDescriptor,
                    SpatialData,
                    NormalSource,
                    CoverageSource,
                    BakeInput,
                    &Error)))
        {
            Test.AddError(Error);
            return false;
        }

        Test.TestEqual(TEXT("Preview and bake keep the same boundary policy"),
            BakeInput.Projection.BoundaryPolicy, PreviewInput.Projection.BoundaryPolicy);
        Test.TestTrue(TEXT("Preview and bake keep the same projector frame"),
            BakeInput.Projection.SurfaceFrameU.Equals(
                PreviewInput.Projection.SurfaceFrameU, UE_KINDA_SMALL_NUMBER) &&
            BakeInput.Projection.SurfaceFrameV.Equals(
                PreviewInput.Projection.SurfaceFrameV, UE_KINDA_SMALL_NUMBER));
        Test.TestTrue(TEXT("Preview and bake keep the same rotation and scale"),
            FMath::IsNearlyEqual(
                BakeInput.Projection.RotationRadians,
                PreviewInput.Projection.RotationRadians) &&
            BakeInput.Projection.Scale.Equals(
                PreviewInput.Projection.Scale, UE_KINDA_SMALL_NUMBER));

        FDWCEditorSurfacePatchProjectionCacheService PreviewCache;
        FDWCEditorSurfacePatchProjectionCacheService BakeCache;
        FDWCEditorProjectedNormalPatchCommand PreviewCommand;
        FDWCEditorProjectedNormalPatchCommand BakeCommand;
        if (!Test.TestTrue(
                TEXT("The preview projected command builds"),
                FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
                    PreviewInput,
                    PreviewCommand,
                    &Error,
                    nullptr,
                    &PreviewCache,
                    EDWCEditorSurfacePatchCachePolicy::Persistent)) ||
            !Test.TestTrue(
                TEXT("The bake projected command builds from an independent cache"),
                FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
                    BakeInput,
                    BakeCommand,
                    &Error,
                    nullptr,
                    &BakeCache,
                    EDWCEditorSurfacePatchCachePolicy::Persistent)))
        {
            Test.AddError(Error);
            return false;
        }

        Test.TestTrue(TEXT("Preview and bake own independent projection leases"),
            PreviewCommand.ProjectionLease.Get() != BakeCommand.ProjectionLease.Get());
        Test.TestTrue(TEXT("Independent preview and bake projections are deterministic"),
            AreFragmentsEquivalent(
                PreviewCommand.GetFragments(), BakeCommand.GetFragments()));

        const FDWCEditorSurfacePatchProjectionGeometry& Projection =
            *PreviewCommand.ProjectionLease.Get();
        if (BoundaryPolicy == EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly)
        {
            Test.TestEqual(TEXT("Non UV Seam affects only the anchor island"),
                Projection.AffectedUVIslandIDs.Num(), 1);
            Test.TestEqual(TEXT("Non UV Seam traverses no UV seam"),
                Projection.TraversedSeamCount, 0);
        }
        else
        {
            Test.TestTrue(TEXT("UV Seam reaches the connected neighboring island"),
                Projection.AffectedUVIslandIDs.Num() > 1);
            Test.TestTrue(TEXT("UV Seam traverses at least one UV seam"),
                Projection.TraversedSeamCount > 0);
        }

        FDWCEditorNormalRasterSurface PreviewSurface;
        FDWCEditorNormalRasterSurface BakeSurface;
        if (!Test.TestTrue(TEXT("The preview raster surface initializes"),
                PreviewSurface.Initialize(FIntPoint(256, 256), true)) ||
            !Test.TestTrue(TEXT("The bake raster surface initializes"),
                BakeSurface.Initialize(FIntPoint(256, 256), true)))
        {
            return false;
        }

        const FDWCEditorRasterResult PreviewResult =
            FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
                PreviewCommand, PreviewSurface);
        const FDWCEditorRasterResult BakeResult =
            FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
                BakeCommand, BakeSurface);
        Test.TestTrue(TEXT("The preview raster affects pixels"),
            PreviewResult.bSucceeded && PreviewResult.bAffectedPixels);
        Test.TestTrue(TEXT("The bake raster affects pixels"),
            BakeResult.bSucceeded && BakeResult.bAffectedPixels);
        Test.TestEqual(TEXT("Preview and bake affect the same pixel count"),
            BakeResult.AffectedPixelCount, PreviewResult.AffectedPixelCount);
        Test.TestTrue(TEXT("Preview and bake normal buffers are identical"),
            PreviewSurface.PackedNormalXY == BakeSurface.PackedNormalXY);
        Test.TestTrue(TEXT("Preview and bake coverage buffers are identical"),
            PreviewSurface.Coverage == BakeSurface.Coverage);

        TArray<FColor> PreviewPixels;
        TArray<FColor> BakePixels;
        FDWCEditorRasterPostProcess::EncodeNormalPixels(PreviewSurface, PreviewPixels);
        FDWCEditorRasterPostProcess::EncodeNormalPixels(BakeSurface, BakePixels);
        Test.TestTrue(TEXT("Preview and bake encoded RGBA pixels are identical"),
            PreviewPixels == BakePixels);
        return !Test.HasAnyErrors();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleNonUvSeamPreviewCommitBakeParityTest,
    "DWC.Editor.Wrinkle.ProjectionParity.NonUVSeam.PreviewCommitBake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleNonUvSeamPreviewCommitBakeParityTest::RunTest(const FString&)
{
    return WetWrinkleProjectionParityTestsPrivate::RunPreviewCommitBakeParityScenario(
        *this,
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleUvSeamPreviewCommitBakeParityTest,
    "DWC.Editor.Wrinkle.ProjectionParity.UVSeam.PreviewCommitBake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleUvSeamPreviewCommitBakeParityTest::RunTest(const FString&)
{
    return WetWrinkleProjectionParityTestsPrivate::RunPreviewCommitBakeParityScenario(
        *this,
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams);
}

#endif
