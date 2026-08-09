//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class UTexture2D;
struct FWetWrinkleBrushSettings;
struct FWetWrinklePatchPlacement;
struct FWetWrinkleSurfaceHit;

/**
 * Immutable editor contract shared by hover presentation and authored patch commit.
 * It deliberately contains no transient world-space values or Data-UV-derived size.
 */
struct FDWCEditorWrinklePatchDescriptor
{
    uint64 RequestId = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    int32 AnchorTriangleID = INDEX_NONE;
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
    FVector2f SurfaceHalfExtentLocal = FVector2f::ZeroVector;
    FDWCEditorSurfacePatchProjectionSettings ProjectionSettings;
    FVector2f AnchorUV = FVector2f::ZeroVector;
    // Retained only for the UV-panel marker. Projection size comes from SurfaceHalfExtentLocal.
    float DisplayRadiusUV = 0.0f;
    float RotationRadians = 0.0f;
    FVector2f Scale = FVector2f(1.0f, 1.0f);
    float Strength = 0.0f;
    float Falloff = 0.0f;
    TWeakObjectPtr<UTexture2D> NormalTexture;
    FGuid NormalTextureSourceId;

    bool IsValid() const;
    bool HasCurrentNormalTextureContent() const;
    uint32 GetStableHash() const;
};

class FDWCEditorWrinklePatchDescriptorBuilder final
{
  public:
    static FDWCEditorSurfacePatchProjectionSettings BuildProjectionSettings(
        EWetWrinklePatchProjectionMode AuthoredMode,
        float ProjectionDepthLocal,
        float MaxSurfaceAngleDegrees,
        float ProjectionDepthSoftness,
        float ProjectionAngleSoftness);

    static EWetWrinklePatchProjectionMode ResolveAuthoredProjectionMode(
        EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy);

    static bool BuildFromHit(
        const FWetWrinkleSurfaceHit& Hit,
        const FWetWrinkleBrushSettings& Brush,
        uint64 RequestId,
        FDWCEditorWrinklePatchDescriptor& OutDescriptor,
        FString* OutError = nullptr);

    static bool BuildFromPlacement(
        const FWetWrinklePatchPlacement& Placement,
        int32 UVChannelIndex,
        FDWCEditorWrinklePatchDescriptor& OutDescriptor,
        FString* OutError = nullptr);

    static bool BuildRasterInput(
        const FDWCEditorWrinklePatchDescriptor& Descriptor,
        const FDWCEditorSpatialHandle& SpatialHandle,
        FDWCEditorSurfaceNormalPatchInput& OutInput,
        FString* OutError = nullptr);

    static bool BuildRasterInputFromSources(
        const FDWCEditorWrinklePatchDescriptor& Descriptor,
        const FDWCEditorSpatialHandle& SpatialHandle,
        const FDWCEditorNormalSourceSnapshot& NormalSource,
        const FDWCEditorScalarSourceSnapshot& CoverageSource,
        FDWCEditorSurfaceNormalPatchInput& OutInput,
        FString* OutError = nullptr);

    static bool BuildProjectionRequest(
        const FDWCEditorWrinklePatchDescriptor& Descriptor,
        const FDWCEditorSpatialHandle& SpatialHandle,
        FDWCEditorSurfacePatchProjectionRequest& OutRequest,
        FString* OutError = nullptr);

    static bool BuildPlacement(
        const FDWCEditorWrinklePatchDescriptor& Descriptor,
        FWetWrinklePatchPlacement& OutPlacement,
        FString* OutError = nullptr);
};
