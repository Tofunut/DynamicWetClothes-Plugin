//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionAction.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleAuthoringControllerCommitTest,
    "DWC.Editor.Wrinkle.Authoring.ControllerCommit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleAuthoringControllerCommitTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    UTexture2D* NormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    const FColor InitialNormalPixel(128, 128, 255, 255);
    NormalTexture->Source.Init(
        1, 1, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(&InitialNormalPixel));
    TSharedRef<FDWCEditorAuthoringDocument> Document =
        MakeShared<FDWCEditorAuthoringDocument>(Asset);
    TSharedRef<FDWCEditorSessionStore> Store = MakeShared<FDWCEditorSessionStore>();
    TSharedRef<FWetWrinkleAuthoringController> Controller =
        MakeShared<FWetWrinkleAuthoringController>(Asset, Document, Store);

    FDWCSetWrinkleBrushAction BrushAction;
    BrushAction.Brush.MaterialSlotIndex = 0;
    BrushAction.Brush.UVChannelIndex = 0;
    BrushAction.Brush.WrinkleNormalTexture = NormalTexture;
    BrushAction.Brush.ToolMode = EWetWrinkleToolMode::Patch;
    BrushAction.Brush.BrushRadiusUV = 0.05f;
    BrushAction.BrushSizeCm = 8.0f;
    BrushAction.BrushSizeUV = 0.05f;
    Store->Dispatch(BrushAction);

    FWetWrinkleSurfaceHit Hit;
    Hit.bHit = true;
    Hit.MaterialSlotIndex = 0;
    Hit.UVChannelIndex = 0;
    Hit.UVIslandID = 3;
    Hit.TriangleID = 7;
    Hit.UV = FVector2D(0.25, 0.5);
    Hit.Barycentric = FVector(0.2, 0.3, 0.5);
    Hit.LocalSurfaceAxisU = FVector::ForwardVector;
    Hit.LocalSurfaceAxisV = FVector::RightVector;
    Hit.SurfaceUnitsPerUV = FVector2f(100.0f, 50.0f);
    FDWCEditorWrinklePatchDescriptor PresentedDescriptor;
    TestTrue(
        TEXT("The Patch hit builds the descriptor presented by the viewport"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
            Hit, BrushAction.Brush, 0, PresentedDescriptor));
    TestTrue(
        TEXT("A newly built descriptor matches the current normal texture content"),
        PresentedDescriptor.HasCurrentNormalTextureContent());
    const FColor RebuiltNormalPixel(129, 128, 255, 255);
    NormalTexture->Source.Init(
        1, 1, 1, 1, TSF_BGRA8,
        reinterpret_cast<const uint8*>(&RebuiltNormalPixel));
    TestFalse(
        TEXT("Regenerating the same normal texture invalidates the old descriptor"),
        PresentedDescriptor.HasCurrentNormalTextureContent());
    TestTrue(
        TEXT("The descriptor can be rebuilt against regenerated texture content"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
            Hit, BrushAction.Brush, 0, PresentedDescriptor));
    const FWetWrinklePatchCommitResult CommitResult =
        Controller->CommitPresentedPatch(PresentedDescriptor);
    TestTrue(TEXT("The presented Patch commits successfully"), CommitResult.bSucceeded);
    TestTrue(TEXT("The commit result returns the exact authored placement"),
        CommitResult.Placement.PatchGuid == CommitResult.PatchGuid &&
        CommitResult.Placement.PatchGuid.IsValid());

    TestEqual(
        TEXT("A Patch click commits one authored patch"),
        Asset->Authored.WrinkleData.EditablePatches.Num(),
        1);
    TestTrue(
        TEXT("The committed Patch becomes the session selection"),
        Store->GetState().Wrinkle.SelectedElementGuid ==
            Asset->Authored.WrinkleData.EditablePatches[0].PatchGuid);
    const FWetWrinklePatchPlacement& Patch = Asset->Authored.WrinkleData.EditablePatches[0];
    TestTrue(TEXT("The returned placement matches the placement stored in the WCA"),
        CommitResult.Placement.PatchGuid == Patch.PatchGuid &&
        CommitResult.Placement.AnchorTriangleID == Patch.AnchorTriangleID &&
        CommitResult.Placement.SurfaceHalfExtentLocal.Equals(
            Patch.SurfaceHalfExtentLocal, UE_KINDA_SMALL_NUMBER));
    TestTrue(TEXT("A new Patch stores a canonical surface anchor"), Patch.bHasSurfaceAnchor);
    TestEqual(TEXT("The Patch anchor preserves the hit triangle"), Patch.AnchorTriangleID, 7);
    TestTrue(TEXT("A new Patch stores a Data UV-independent surface frame"), Patch.HasValidSurfaceFrame());
    TestTrue(TEXT("A new Patch stores a local-space surface footprint"), Patch.bHasSurfaceFootprint);
    TestTrue(
        TEXT("The local footprint stores half of the authored physical brush diameter"),
        Patch.SurfaceHalfExtentLocal.Equals(FVector2f(4.0f, 4.0f), UE_KINDA_SMALL_NUMBER));

    BrushAction.Brush.ToolMode = EWetWrinkleToolMode::ProceduralRidgeStroke;
    BrushAction.Brush.RidgeEditMode = EWetProceduralRidgeEditMode::Draw;
    Store->Dispatch(BrushAction);
    Controller->BeginSurfaceInteraction(Hit);
    FWetWrinkleSurfaceHit EndHit = Hit;
    EndHit.WorldPosition = FVector(20.0, 0.0, 0.0);
    EndHit.UV = FVector2D(0.45, 0.5);
    Controller->UpdateSurfaceInteraction(EndHit);
    Controller->CancelSurfaceInteraction();
    TestEqual(
        TEXT("Canceling a Ridge interaction does not mutate the asset"),
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num(),
        0);

    Controller->BeginSurfaceInteraction(Hit);
    Controller->UpdateSurfaceInteraction(EndHit);
    Controller->EndSurfaceInteraction();
    TestEqual(
        TEXT("Mouse-up commits one Ridge stroke"),
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num(),
        1);
    TestFalse(TEXT("The committed interaction is no longer active"), Controller->IsInteracting());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinklePresentedPatchDescriptorParityTest,
    "DWC.Editor.Wrinkle.Authoring.PresentedPatchDescriptorParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinklePresentedPatchDescriptorParityTest::RunTest(const FString&)
{
    UTexture2D* NormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    FWetWrinkleBrushSettings Brush;
    Brush.MaterialSlotIndex = 4;
    Brush.UVChannelIndex = 2;
    Brush.WrinkleNormalTexture = NormalTexture;
    Brush.PatchDiameterLocal = 12.0f;
    Brush.RotationRadians = 0.37f;
    Brush.Strength = 0.8f;
    Brush.Falloff = 0.25f;

    FWetWrinkleSurfaceHit Hit;
    Hit.bHit = true;
    Hit.MaterialSlotIndex = 4;
    Hit.UVChannelIndex = 2;
    Hit.TriangleID = 19;
    Hit.Barycentric = FVector(0.2, 0.3, 0.5);
    Hit.UV = FVector2D(0.6, 0.4);
    Hit.LocalNormal = FVector::UpVector;
    Hit.LocalSurfaceFrameU = FVector::ForwardVector;
    Hit.LocalSurfaceFrameV = FVector::RightVector;

    FDWCEditorWrinklePatchDescriptor Presented;
    TestTrue(
        TEXT("A valid hover hit builds one immutable patch descriptor"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
            Hit, Brush, 42, Presented));

    Brush.PatchDiameterLocal = 40.0f;
    Brush.RotationRadians = 1.5f;
    Hit.Barycentric = FVector(0.8, 0.1, 0.1);

    FWetWrinklePatchPlacement Committed;
    TestTrue(
        TEXT("The presented descriptor converts directly into authored data"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(Presented, Committed));
    TestEqual(TEXT("Commit keeps the presented triangle"), Committed.AnchorTriangleID, 19);
    TestTrue(
        TEXT("Commit keeps the presented anchor instead of the newer mouse hit"),
        Committed.AnchorBarycentric.Equals(FVector3f(0.2f, 0.3f, 0.5f), UE_KINDA_SMALL_NUMBER));
    TestTrue(
        TEXT("Commit keeps the presented physical footprint"),
        Committed.SurfaceHalfExtentLocal.Equals(FVector2f(6.0f, 6.0f), UE_KINDA_SMALL_NUMBER));
    TestTrue(
        TEXT("Commit preserves the UV-panel display radius without using it for projection"),
        FMath::IsNearlyEqual(Committed.BrushRadiusUV, 0.025f));
    TestTrue(
        TEXT("Commit keeps the presented rotation"),
        FMath::IsNearlyEqual(Committed.RotationRadians, 0.37f));

    FDWCEditorWrinklePatchDescriptor RoundTrip;
    TestTrue(
        TEXT("Authored data rebuilds the same projector descriptor"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromPlacement(
            Committed, 2, RoundTrip));
    TestEqual(
        TEXT("Hover and authored projector inputs have identical stable hashes"),
        RoundTrip.GetStableHash(),
        Presented.GetStableHash());

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> SpatialData =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    SpatialData->MaterialSlotIndex = Presented.MaterialSlotIndex;
    SpatialData->UVChannelIndex = Presented.UVChannelIndex;
    FDWCEditorSurfacePatchProjectionRequest HoverRequest;
    FDWCEditorSurfacePatchProjectionRequest AuthoredRequest;
    TestTrue(
        TEXT("The presented descriptor builds a projector request"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionRequest(
            Presented, SpatialData, HoverRequest));
    TestTrue(
        TEXT("The authored descriptor builds a projector request"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionRequest(
            RoundTrip, SpatialData, AuthoredRequest));
    TestEqual(TEXT("Projector requests keep the same material slot"),
        AuthoredRequest.MaterialSlotIndex, HoverRequest.MaterialSlotIndex);
    TestEqual(TEXT("Projector requests keep the same anchor triangle"),
        AuthoredRequest.AnchorTriangleID, HoverRequest.AnchorTriangleID);
    TestTrue(TEXT("Projector requests keep the same barycentric anchor"),
        AuthoredRequest.AnchorBarycentric.Equals(
            HoverRequest.AnchorBarycentric, UE_KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Projector requests keep the same stable surface frame"),
        AuthoredRequest.SurfaceFrameU.Equals(HoverRequest.SurfaceFrameU, UE_KINDA_SMALL_NUMBER) &&
            AuthoredRequest.SurfaceFrameV.Equals(HoverRequest.SurfaceFrameV, UE_KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Projector requests keep the same physical footprint"),
        AuthoredRequest.SurfaceHalfExtentLocal.Equals(
            HoverRequest.SurfaceHalfExtentLocal, UE_KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Projector requests keep the same rotation and scale"),
        FMath::IsNearlyEqual(AuthoredRequest.RotationRadians, HoverRequest.RotationRadians) &&
            AuthoredRequest.Scale.Equals(HoverRequest.Scale, UE_KINDA_SMALL_NUMBER));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleProjectionModeCanonicalizationTest,
    "DWC.Editor.Wrinkle.Authoring.ProjectionModeCanonicalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleProjectionModeCanonicalizationTest::RunTest(const FString&)
{
    UTexture2D* NormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    FWetWrinkleBrushSettings Brush;
    Brush.MaterialSlotIndex = 2;
    Brush.UVChannelIndex = 1;
    Brush.WrinkleNormalTexture = NormalTexture;
    Brush.PatchDiameterLocal = 10.0f;
    Brush.Strength = 1.0f;
    Brush.PatchProjection.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;

    FWetWrinkleSurfaceHit Hit;
    Hit.bHit = true;
    Hit.MaterialSlotIndex = 2;
    Hit.UVChannelIndex = 1;
    Hit.TriangleID = 12;
    Hit.Barycentric = FVector(0.2, 0.3, 0.5);
    Hit.UV = FVector2D(0.4, 0.6);
    Hit.LocalNormal = FVector::UpVector;
    Hit.LocalSurfaceFrameU = FVector::ForwardVector;
    Hit.LocalSurfaceFrameV = FVector::RightVector;

    FDWCEditorWrinklePatchDescriptor NonUvDescriptor;
    TestTrue(
        TEXT("A Non UV Seam descriptor builds from a valid surface hit"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
            Hit, Brush, 1, NonUvDescriptor));

    FDWCEditorWrinklePatchDescriptor DifferentDecalSettings = NonUvDescriptor;
    DifferentDecalSettings.ProjectionSettings.ProjectionDepthLocal = -1.0f;
    DifferentDecalSettings.ProjectionSettings.MaxSurfaceAngleDegrees = 120.0f;
    DifferentDecalSettings.ProjectionSettings.ProjectionDepthSoftness = -0.5f;
    DifferentDecalSettings.ProjectionSettings.ProjectionAngleSoftness = 2.0f;
    TestFalse(
        TEXT("Non UV Seam rejects invalid shared decal projection settings"),
        DifferentDecalSettings.IsValid());

    FDWCEditorWrinklePatchDescriptor ChangedNonUvSettings = NonUvDescriptor;
    ChangedNonUvSettings.ProjectionSettings.ProjectionDepthLocal *= 2.0f;
    TestNotEqual(
        TEXT("Non UV Seam stable hashes include shared decal projection settings"),
        ChangedNonUvSettings.GetStableHash(),
        NonUvDescriptor.GetStableHash());

    TSharedRef<FDWCEditorSpatialData, ESPMode::ThreadSafe> SpatialData =
        MakeShared<FDWCEditorSpatialData, ESPMode::ThreadSafe>();
    SpatialData->MaterialSlotIndex = NonUvDescriptor.MaterialSlotIndex;
    SpatialData->UVChannelIndex = NonUvDescriptor.UVChannelIndex;
    FDWCEditorSurfacePatchProjectionRequest NonUvRequest;
    TestTrue(
        TEXT("The Non UV Seam descriptor builds a projection request"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionRequest(
            NonUvDescriptor, SpatialData, NonUvRequest));
    TestEqual(
        TEXT("Non UV Seam resolves to the anchor-island boundary policy"),
        NonUvRequest.BoundaryPolicy,
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly);

    FDWCEditorWrinklePatchDescriptor DecalDescriptor = NonUvDescriptor;
    DecalDescriptor.ProjectionSettings.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    TestTrue(TEXT("A UV Seam descriptor with valid decal settings remains valid"),
        DecalDescriptor.IsValid());
    TestNotEqual(
        TEXT("Changing projection mode changes the descriptor stable hash"),
        DecalDescriptor.GetStableHash(),
        NonUvDescriptor.GetStableHash());

    FDWCEditorWrinklePatchDescriptor ChangedDecalSettings = DecalDescriptor;
    ChangedDecalSettings.ProjectionSettings.ProjectionDepthLocal *= 2.0f;
    TestNotEqual(
        TEXT("UV Seam stable hashes include decal projection settings"),
        ChangedDecalSettings.GetStableHash(),
        DecalDescriptor.GetStableHash());

    DifferentDecalSettings.ProjectionSettings.BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    TestFalse(
        TEXT("UV Seam rejects invalid decal projection settings"),
        DifferentDecalSettings.IsValid());

    FWetWrinklePatchPlacement Placement;
    TestTrue(
        TEXT("A UV Seam descriptor converts to authored placement data"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(
            DecalDescriptor, Placement));
    FDWCEditorWrinklePatchDescriptor RoundTrip;
    TestTrue(
        TEXT("The authored UV Seam placement rebuilds a descriptor"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildFromPlacement(
            Placement, DecalDescriptor.UVChannelIndex, RoundTrip));
    TestEqual(
        TEXT("The projection mode survives the descriptor-placement round trip"),
        RoundTrip.ProjectionSettings.BoundaryPolicy,
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams);
    TestEqual(
        TEXT("The round-trip descriptor preserves its stable projection contract"),
        RoundTrip.GetStableHash(),
        DecalDescriptor.GetStableHash());

    FDWCEditorSurfacePatchProjectionRequest DecalRequest;
    TestTrue(
        TEXT("The UV Seam descriptor builds a projection request"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionRequest(
            RoundTrip, SpatialData, DecalRequest));
    TestEqual(
        TEXT("UV Seam resolves to the cross-seam boundary policy"),
        DecalRequest.BoundaryPolicy,
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams);

    FDWCEditorNormalSourceSnapshot NormalSource;
    NormalSource.Texture.Width = 1;
    NormalSource.Texture.Height = 1;
    NormalSource.Texture.BytesPerPixel = 4;
    NormalSource.Texture.Format = TSF_BGRA8;
    NormalSource.Texture.RawData = MakeShared<TArray64<uint8>>();
    NormalSource.Texture.RawData->Append({255, 128, 128, 255});
    FDWCEditorScalarSourceSnapshot CoverageSource;
    CoverageSource.Size = FIntPoint(1, 1);
    TSharedRef<TArray<float>, ESPMode::ThreadSafe> CoverageValues =
        MakeShared<TArray<float>, ESPMode::ThreadSafe>();
    CoverageValues->Add(1.0f);
    CoverageSource.Values = CoverageValues;

    FDWCEditorSurfaceNormalPatchInput NonUvRasterInput;
    TestTrue(
        TEXT("Cached bake sources build the canonical Non UV Seam command input"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
            NonUvDescriptor,
            SpatialData,
            NormalSource,
            CoverageSource,
            NonUvRasterInput));
    TestEqual(
        TEXT("The canonical Non UV Seam command input keeps the anchor-island policy"),
        NonUvRasterInput.Projection.BoundaryPolicy,
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly);

    FDWCEditorSurfaceNormalPatchInput DecalRasterInput;
    TestTrue(
        TEXT("Cached bake sources build the canonical UV Seam command input"),
        FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
            DecalDescriptor,
            SpatialData,
            NormalSource,
            CoverageSource,
            DecalRasterInput));
    TestEqual(
        TEXT("The canonical UV Seam command input keeps the cross-seam policy"),
        DecalRasterInput.Projection.BoundaryPolicy,
        EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams);
    return true;
}

#endif
