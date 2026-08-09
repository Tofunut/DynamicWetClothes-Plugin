//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"

#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"

namespace
{
    TAutoConsoleVariable<int32> CVarDWCWrinkleProjectionDiagnostics(
        TEXT("DWC.Wrinkle.ProjectionDiagnostics"),
        0,
        TEXT("Collect detailed wrinkle surface projection diagnostics. 0=off, 1=issues only."),
        ECVF_Default);

    void SetDescriptorError(FString* OutError, const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
    }

    uint32 HashQuantizedVector(const FVector3f& Value)
    {
        constexpr float Quantization = 4096.0f;
        uint32 Hash = GetTypeHash(FMath::RoundToInt(Value.X * Quantization));
        Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Value.Y * Quantization)));
        return HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Value.Z * Quantization)));
    }
}

bool FDWCEditorWrinklePatchDescriptor::IsValid() const
{
    FVector3f NormalizedBarycentric;
    FVector3f StableU;
    FVector3f StableV;
    const FVector3f SurfaceNormal = FVector3f::CrossProduct(SurfaceFrameU, SurfaceFrameV).GetSafeNormal();
    return MaterialSlotIndex != INDEX_NONE && UVChannelIndex >= 0 && AnchorTriangleID != INDEX_NONE &&
        FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(AnchorBarycentric, NormalizedBarycentric) &&
        FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            SurfaceNormal, SurfaceFrameU, SurfaceFrameV, StableU, StableV) &&
        FMath::IsFinite(SurfaceHalfExtentLocal.X) && FMath::IsFinite(SurfaceHalfExtentLocal.Y) &&
        SurfaceHalfExtentLocal.X > UE_SMALL_NUMBER && SurfaceHalfExtentLocal.Y > UE_SMALL_NUMBER &&
        FMath::IsFinite(DisplayRadiusUV) && DisplayRadiusUV >= 0.0f &&
        FMath::IsFinite(ProjectionDepthLocal) && ProjectionDepthLocal > 0.0f &&
        FMath::IsFinite(MaxSurfaceAngleDegrees) && MaxSurfaceAngleDegrees > 0.0f && MaxSurfaceAngleDegrees < 90.0f &&
        FMath::IsFinite(ProjectionDepthSoftness) && ProjectionDepthSoftness >= 0.0f && ProjectionDepthSoftness <= 1.0f &&
        FMath::IsFinite(ProjectionAngleSoftness) && ProjectionAngleSoftness >= 0.0f && ProjectionAngleSoftness <= 1.0f &&
        FMath::IsFinite(RotationRadians) && FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y) &&
        FMath::Abs(Scale.X) > UE_SMALL_NUMBER && FMath::Abs(Scale.Y) > UE_SMALL_NUMBER &&
        FMath::IsFinite(Strength) && Strength > 0.0f && FMath::IsFinite(Falloff) &&
        NormalTexture.IsValid();
}

bool FDWCEditorWrinklePatchDescriptor::HasCurrentNormalTextureContent() const
{
    const UTexture2D* Texture = NormalTexture.Get();
    return Texture != nullptr && Texture->Source.GetId() == NormalTextureSourceId;
}

uint32 FDWCEditorWrinklePatchDescriptor::GetStableHash() const
{
    uint32 Hash = GetTypeHash(MaterialSlotIndex);
    Hash = HashCombine(Hash, GetTypeHash(UVChannelIndex));
    Hash = HashCombine(Hash, GetTypeHash(AnchorTriangleID));
    Hash = HashCombine(Hash, HashQuantizedVector(AnchorBarycentric));
    Hash = HashCombine(Hash, HashQuantizedVector(SurfaceFrameU));
    Hash = HashCombine(Hash, HashQuantizedVector(SurfaceFrameV));
    Hash = HashCombine(Hash, GetTypeHash(SurfaceHalfExtentLocal));
    Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(ProjectionMode)));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionDepthLocal));
    Hash = HashCombine(Hash, GetTypeHash(MaxSurfaceAngleDegrees));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionDepthSoftness));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionAngleSoftness));
    Hash = HashCombine(Hash, GetTypeHash(DisplayRadiusUV));
    Hash = HashCombine(Hash, GetTypeHash(RotationRadians));
    Hash = HashCombine(Hash, GetTypeHash(Scale));
    Hash = HashCombine(Hash, GetTypeHash(Strength));
    Hash = HashCombine(Hash, GetTypeHash(Falloff));
    Hash = HashCombine(Hash, PointerHash(NormalTexture.Get()));
    return HashCombine(Hash, GetTypeHash(NormalTextureSourceId));
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildFromHit(
    const FWetWrinkleSurfaceHit& Hit,
    const FWetWrinkleBrushSettings& Brush,
    const uint64 RequestId,
    FDWCEditorWrinklePatchDescriptor& OutDescriptor,
    FString* OutError)
{
    OutDescriptor = {};
    FVector3f Barycentric;
    FVector3f FrameU;
    FVector3f FrameV;
    if (!Hit.bHit || Hit.TriangleID == INDEX_NONE || Brush.WrinkleNormalTexture == nullptr ||
        Brush.PatchDiameterLocal <= 0.0f || Brush.Strength <= 0.0f ||
        !FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(FVector3f(Hit.Barycentric), Barycentric) ||
        !FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            FVector3f(Hit.LocalNormal),
            FVector3f(Hit.LocalSurfaceFrameU),
            FVector3f(Hit.LocalSurfaceFrameV),
            FrameU,
            FrameV))
    {
        SetDescriptorError(OutError, TEXT("The hover hit or brush cannot define a stable physical patch."));
        return false;
    }

    OutDescriptor.RequestId = RequestId;
    OutDescriptor.MaterialSlotIndex = Hit.MaterialSlotIndex;
    OutDescriptor.UVChannelIndex = Hit.UVChannelIndex;
    OutDescriptor.AnchorTriangleID = Hit.TriangleID;
    OutDescriptor.AnchorBarycentric = Barycentric;
    OutDescriptor.SurfaceFrameU = FrameU;
    OutDescriptor.SurfaceFrameV = FrameV;
    const float HalfExtent = Brush.PatchDiameterLocal * 0.5f;
    OutDescriptor.SurfaceHalfExtentLocal = FVector2f(HalfExtent, HalfExtent);
    OutDescriptor.ProjectionMode = Brush.PatchProjectionMode;
    OutDescriptor.ProjectionDepthLocal = Brush.PatchProjectionDepthLocal;
    OutDescriptor.MaxSurfaceAngleDegrees = Brush.PatchMaxSurfaceAngleDegrees;
    OutDescriptor.ProjectionDepthSoftness = Brush.PatchProjectionDepthSoftness;
    OutDescriptor.ProjectionAngleSoftness = Brush.PatchProjectionAngleSoftness;
    OutDescriptor.AnchorUV = FVector2f(Hit.UV);
    OutDescriptor.DisplayRadiusUV = Brush.BrushRadiusUV;
    OutDescriptor.RotationRadians = Brush.RotationRadians;
    OutDescriptor.Strength = FMath::Clamp(Brush.Strength, 0.0f, 4.0f);
    OutDescriptor.Falloff = FMath::Clamp(Brush.Falloff, 0.0f, 1.0f);
    OutDescriptor.NormalTexture = Brush.WrinkleNormalTexture.Get();
    OutDescriptor.NormalTextureSourceId = Brush.WrinkleNormalTexture->Source.GetId();
    return OutDescriptor.IsValid();
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildFromPlacement(
    const FWetWrinklePatchPlacement& Placement,
    const int32 UVChannelIndex,
    FDWCEditorWrinklePatchDescriptor& OutDescriptor,
    FString* OutError)
{
    OutDescriptor = {};
    if (!Placement.HasValidSurfaceAnchor() || !Placement.HasValidSurfaceFootprint() ||
        !Placement.HasValidSurfaceFrame() || Placement.WrinkleNormalTexture == nullptr)
    {
        SetDescriptorError(OutError, TEXT("The authored patch has no valid physical surface contract."));
        return false;
    }
    OutDescriptor.MaterialSlotIndex = Placement.MaterialSlotIndex;
    OutDescriptor.UVChannelIndex = UVChannelIndex;
    OutDescriptor.AnchorTriangleID = Placement.AnchorTriangleID;
    OutDescriptor.AnchorBarycentric = Placement.AnchorBarycentric;
    OutDescriptor.SurfaceFrameU = Placement.SurfaceFrameU;
    OutDescriptor.SurfaceFrameV = Placement.SurfaceFrameV;
    OutDescriptor.SurfaceHalfExtentLocal = Placement.SurfaceHalfExtentLocal;
    OutDescriptor.ProjectionMode = Placement.ProjectionMode;
    OutDescriptor.ProjectionDepthLocal = Placement.ProjectionDepthLocal;
    OutDescriptor.MaxSurfaceAngleDegrees = Placement.MaxSurfaceAngleDegrees;
    OutDescriptor.ProjectionDepthSoftness = Placement.ProjectionDepthSoftness;
    OutDescriptor.ProjectionAngleSoftness = Placement.ProjectionAngleSoftness;
    OutDescriptor.AnchorUV = FVector2f(Placement.PositionUV);
    OutDescriptor.DisplayRadiusUV = Placement.BrushRadiusUV;
    OutDescriptor.RotationRadians = Placement.RotationRadians;
    OutDescriptor.Scale = FVector2f(Placement.Scale);
    OutDescriptor.Strength = Placement.Strength;
    OutDescriptor.Falloff = Placement.Falloff;
    OutDescriptor.NormalTexture = Placement.WrinkleNormalTexture.Get();
    OutDescriptor.NormalTextureSourceId = Placement.WrinkleNormalTexture->Source.GetId();
    if (!OutDescriptor.IsValid())
    {
        SetDescriptorError(OutError, TEXT("The authored patch descriptor is invalid."));
        return false;
    }
    return true;
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInput(
    const FDWCEditorWrinklePatchDescriptor& Descriptor,
    const FDWCEditorSpatialHandle& SpatialHandle,
    FDWCEditorSurfaceNormalPatchInput& OutInput,
    FString* OutError)
{
    OutInput = {};
    UTexture2D* NormalTexture = Descriptor.NormalTexture.Get();
    if (!BuildProjectionRequest(Descriptor, SpatialHandle, OutInput.Projection, OutError) ||
        NormalTexture == nullptr)
    {
        SetDescriptorError(OutError, TEXT("The patch descriptor does not match the active spatial payload."));
        return false;
    }
    FString ReadError;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            NormalTexture, OutInput.NormalSource.Texture, ReadError))
    {
        if (OutError != nullptr)
        {
            *OutError = MoveTemp(ReadError);
        }
        return false;
    }
    OutInput.NormalSource.bFlipGreenChannel = NormalTexture->bFlipGreenChannel;
    OutInput.Strength = Descriptor.Strength;
    OutInput.Falloff = Descriptor.Falloff;
    return OutInput.IsValid();
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionRequest(
    const FDWCEditorWrinklePatchDescriptor& Descriptor,
    const FDWCEditorSpatialHandle& SpatialHandle,
    FDWCEditorSurfacePatchProjectionRequest& OutRequest,
    FString* OutError)
{
    OutRequest = {};
    if (!Descriptor.IsValid() || !SpatialHandle.IsValid() ||
        SpatialHandle->MaterialSlotIndex != Descriptor.MaterialSlotIndex ||
        SpatialHandle->UVChannelIndex != Descriptor.UVChannelIndex)
    {
        SetDescriptorError(OutError, TEXT("The patch descriptor does not match the active spatial payload."));
        return false;
    }
    OutRequest.SpatialHandle = SpatialHandle;
    OutRequest.MaterialSlotIndex = Descriptor.MaterialSlotIndex;
    OutRequest.AnchorTriangleID = Descriptor.AnchorTriangleID;
    OutRequest.AnchorBarycentric = Descriptor.AnchorBarycentric;
    OutRequest.SurfaceFrameU = Descriptor.SurfaceFrameU;
    OutRequest.SurfaceFrameV = Descriptor.SurfaceFrameV;
    OutRequest.SurfaceHalfExtentLocal = Descriptor.SurfaceHalfExtentLocal;
    OutRequest.RotationRadians = Descriptor.RotationRadians;
    OutRequest.Scale = Descriptor.Scale;
    OutRequest.ProjectionDepthLocal = Descriptor.ProjectionDepthLocal;
    OutRequest.MaxSurfaceAngleDegrees = Descriptor.MaxSurfaceAngleDegrees;
    OutRequest.ProjectionDepthSoftness = Descriptor.ProjectionDepthSoftness;
    OutRequest.ProjectionAngleSoftness = Descriptor.ProjectionAngleSoftness;
    OutRequest.bUseSurfaceDecalProjection =
        Descriptor.ProjectionMode == EWetWrinklePatchProjectionMode::SurfaceDecal;
    OutRequest.bAllowUVSeamTraversal =
        Descriptor.ProjectionMode == EWetWrinklePatchProjectionMode::SurfaceDecal;
    OutRequest.bCollectDetailedDiagnostics =
        CVarDWCWrinkleProjectionDiagnostics.GetValueOnAnyThread() > 0;
    return true;
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(
    const FDWCEditorWrinklePatchDescriptor& Descriptor,
    UTexture* SourceTexture,
    FWetWrinklePatchPlacement& OutPlacement,
    FString* OutError)
{
    OutPlacement = {};
    UTexture2D* NormalTexture = Descriptor.NormalTexture.Get();
    if (!Descriptor.IsValid() || NormalTexture == nullptr)
    {
        SetDescriptorError(OutError, TEXT("Only a valid presented patch can be committed."));
        return false;
    }
    OutPlacement.PatchGuid = FGuid::NewGuid();
    OutPlacement.MaterialSlotIndex = Descriptor.MaterialSlotIndex;
    OutPlacement.ProjectionMode = Descriptor.ProjectionMode;
    OutPlacement.ProjectionDepthLocal = Descriptor.ProjectionDepthLocal;
    OutPlacement.MaxSurfaceAngleDegrees = Descriptor.MaxSurfaceAngleDegrees;
    OutPlacement.ProjectionDepthSoftness = Descriptor.ProjectionDepthSoftness;
    OutPlacement.ProjectionAngleSoftness = Descriptor.ProjectionAngleSoftness;
    OutPlacement.bHasSurfaceAnchor = true;
    OutPlacement.AnchorTriangleID = Descriptor.AnchorTriangleID;
    OutPlacement.AnchorBarycentric = Descriptor.AnchorBarycentric;
    OutPlacement.SourceTexture = SourceTexture;
    OutPlacement.PositionUV = FVector2D(Descriptor.AnchorUV);
    OutPlacement.BrushRadiusUV = Descriptor.DisplayRadiusUV;
    OutPlacement.bHasSurfaceFrame = true;
    OutPlacement.SurfaceFrameU = Descriptor.SurfaceFrameU;
    OutPlacement.SurfaceFrameV = Descriptor.SurfaceFrameV;
    OutPlacement.bHasSurfaceFootprint = true;
    OutPlacement.SurfaceHalfExtentLocal = Descriptor.SurfaceHalfExtentLocal;
    OutPlacement.RotationRadians = Descriptor.RotationRadians;
    OutPlacement.Scale = FVector2D(Descriptor.Scale);
    OutPlacement.Strength = Descriptor.Strength;
    OutPlacement.Falloff = Descriptor.Falloff;
    OutPlacement.WrinkleNormalTexture = NormalTexture;
    OutPlacement.bEnabled = true;
#if WITH_EDITORONLY_DATA
    OutPlacement.bHasEditorSurface = true;
    OutPlacement.EditorSurfaceLocalTangent = FVector(Descriptor.SurfaceFrameU);
    OutPlacement.EditorSurfaceLocalBitangent = FVector(Descriptor.SurfaceFrameV);
    OutPlacement.EditorSurfaceLocalNormal = FVector(FVector3f::CrossProduct(
        Descriptor.SurfaceFrameU, Descriptor.SurfaceFrameV).GetSafeNormal());
#endif
    return true;
}
