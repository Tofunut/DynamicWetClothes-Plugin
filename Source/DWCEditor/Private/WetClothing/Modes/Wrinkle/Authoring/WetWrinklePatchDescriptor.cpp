//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"

#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"
#include "WetClothing/Modes/Wrinkle/Authoring/DWCEditorWrinkleTextureResolver.h"

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

FDWCEditorSurfacePatchProjectionSettings
FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionSettings(
    const EWetWrinklePatchProjectionMode AuthoredMode,
    const float ProjectionDepthLocal,
    const float MaxSurfaceAngleDegrees,
    const float ProjectionDepthSoftness,
    const float ProjectionAngleSoftness)
{
    FDWCEditorSurfacePatchProjectionSettings Settings;
    Settings.BoundaryPolicy = AuthoredMode == EWetWrinklePatchProjectionMode::SurfaceDecal
        ? EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams
        : EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
    Settings.ProjectionDepthLocal = ProjectionDepthLocal;
    Settings.MaxSurfaceAngleDegrees = MaxSurfaceAngleDegrees;
    Settings.ProjectionDepthSoftness = ProjectionDepthSoftness;
    Settings.ProjectionAngleSoftness = ProjectionAngleSoftness;
    Settings.Normalize();
    return Settings;
}

EWetWrinklePatchProjectionMode
FDWCEditorWrinklePatchDescriptorBuilder::ResolveAuthoredProjectionMode(
    const EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy)
{
    return BoundaryPolicy == EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams
        ? EWetWrinklePatchProjectionMode::SurfaceDecal
        : EWetWrinklePatchProjectionMode::NonUVSeam;
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
        ProjectionSettings.IsValid() &&
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
    Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(ProjectionSettings.BoundaryPolicy)));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionSettings.ProjectionDepthLocal));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionSettings.MaxSurfaceAngleDegrees));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionSettings.ProjectionDepthSoftness));
    Hash = HashCombine(Hash, GetTypeHash(ProjectionSettings.ProjectionAngleSoftness));
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
    OutDescriptor.ProjectionSettings = Brush.PatchProjection;
    OutDescriptor.ProjectionSettings.Normalize();
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
        !Placement.HasValidSurfaceFrame() || !Placement.HasWrinkleNormalTexture())
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
    OutDescriptor.ProjectionSettings = BuildProjectionSettings(
        Placement.ProjectionMode,
        Placement.ProjectionDepthLocal,
        Placement.MaxSurfaceAngleDegrees,
        Placement.ProjectionDepthSoftness,
        Placement.ProjectionAngleSoftness);
    OutDescriptor.AnchorUV = FVector2f(Placement.PositionUV);
    OutDescriptor.DisplayRadiusUV = Placement.BrushRadiusUV;
    OutDescriptor.RotationRadians = Placement.RotationRadians;
    OutDescriptor.Scale = FVector2f(Placement.Scale);
    OutDescriptor.Strength = Placement.Strength;
    OutDescriptor.Falloff = Placement.Falloff;
    const FDWCEditorWrinkleTextureReferenceSnapshot SourceReference =
        FDWCEditorWrinkleTextureResolver::ResolveSource(Placement);
    if (!SourceReference.IsReady())
    {
        SetDescriptorError(
            OutError,
            SourceReference.Detail.IsEmpty()
                ? TEXT("The authored patch wrinkle source could not be resolved.")
                : *SourceReference.Detail);
        return false;
    }
    OutDescriptor.NormalTexture = SourceReference.Texture;
    OutDescriptor.NormalTextureSourceId = SourceReference.SourceId;
    if (!OutDescriptor.IsValid())
    {
        SetDescriptorError(OutError, TEXT("The authored patch descriptor is invalid."));
        return false;
    }
    return true;
}

bool FDWCEditorWrinklePatchDescriptorBuilder::ValidatePlacement(
    const FWetWrinklePatchPlacement& Placement,
    const int32 UVChannelIndex,
    FDWCEditorWrinklePatchValidationResult& OutResult)
{
    OutResult = {};
    if (!Placement.HasValidSurfaceAnchor() || !Placement.HasValidSurfaceFootprint() ||
        !Placement.HasValidSurfaceFrame() || !Placement.HasWrinkleNormalTexture())
    {
        OutResult.Status = EDWCEditorWrinklePatchValidationStatus::InvalidSurfaceContract;
        OutResult.Error = TEXT("The authored patch has no valid physical surface contract.");
        return false;
    }

    if (!BuildFromPlacement(Placement, UVChannelIndex, OutResult.Descriptor, &OutResult.Error))
    {
        OutResult.Status = EDWCEditorWrinklePatchValidationStatus::InvalidDescriptor;
        if (OutResult.Error.IsEmpty())
        {
            OutResult.Error = TEXT("The authored patch descriptor is invalid.");
        }
        return false;
    }

    OutResult.Status = EDWCEditorWrinklePatchValidationStatus::Valid;
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
    if (NormalTexture == nullptr)
    {
        SetDescriptorError(OutError, TEXT("The patch descriptor has no normal texture."));
        return false;
    }
    FDWCEditorNormalSourceSnapshot NormalSource;
    FString ReadError;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
            NormalTexture, NormalSource.Texture, ReadError))
    {
        if (OutError != nullptr)
        {
            *OutError = MoveTemp(ReadError);
        }
        return false;
    }
    NormalSource.bFlipGreenChannel = NormalTexture->bFlipGreenChannel;
    return BuildRasterInputFromSources(
        Descriptor,
        SpatialHandle,
        NormalSource,
        FDWCEditorScalarSourceSnapshot(),
        OutInput,
        OutError);
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
    const FDWCEditorWrinklePatchDescriptor& Descriptor,
    const FDWCEditorSpatialHandle& SpatialHandle,
    const FDWCEditorNormalSourceSnapshot& NormalSource,
    const FDWCEditorScalarSourceSnapshot& CoverageSource,
    FDWCEditorSurfaceNormalPatchInput& OutInput,
    FString* OutError)
{
    OutInput = {};
    if (!NormalSource.IsValid())
    {
        SetDescriptorError(OutError, TEXT("The patch command input has no readable normal source."));
        return false;
    }
    if (!BuildProjectionRequest(Descriptor, SpatialHandle, OutInput.Projection, OutError))
    {
        return false;
    }
    OutInput.NormalSource = NormalSource;
    OutInput.CoverageSource = CoverageSource;
    OutInput.Strength = Descriptor.Strength;
    OutInput.Falloff = Descriptor.Falloff;
    if (!OutInput.IsValid())
    {
        SetDescriptorError(OutError, TEXT("The canonical surface patch command input is invalid."));
        return false;
    }
    return true;
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
    OutRequest.ApplySettings(Descriptor.ProjectionSettings);
    OutRequest.bCollectDetailedDiagnostics =
        CVarDWCWrinkleProjectionDiagnostics.GetValueOnAnyThread() > 0;
    return FDWCEditorSurfacePatchProjector::ValidateProjectionContract(OutRequest, OutError);
}

bool FDWCEditorWrinklePatchDescriptorBuilder::BuildPlacement(
    const FDWCEditorWrinklePatchDescriptor& Descriptor,
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
    OutPlacement.ProjectionMode = ResolveAuthoredProjectionMode(
        Descriptor.ProjectionSettings.BoundaryPolicy);
    OutPlacement.ProjectionDepthLocal = Descriptor.ProjectionSettings.ProjectionDepthLocal;
    OutPlacement.MaxSurfaceAngleDegrees = Descriptor.ProjectionSettings.MaxSurfaceAngleDegrees;
    OutPlacement.ProjectionDepthSoftness = Descriptor.ProjectionSettings.ProjectionDepthSoftness;
    OutPlacement.ProjectionAngleSoftness = Descriptor.ProjectionSettings.ProjectionAngleSoftness;
    OutPlacement.bHasSurfaceAnchor = true;
    OutPlacement.AnchorTriangleID = Descriptor.AnchorTriangleID;
    OutPlacement.AnchorBarycentric = Descriptor.AnchorBarycentric;
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
    OutPlacement.SetWrinkleNormalTexture(NormalTexture);
    OutPlacement.bEnabled = true;
    return true;
}
