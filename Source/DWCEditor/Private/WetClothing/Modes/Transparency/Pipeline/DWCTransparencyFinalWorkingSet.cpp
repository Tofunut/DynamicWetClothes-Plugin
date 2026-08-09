//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"

namespace
{
    bool Fail(FString* OutError, const TCHAR* Message)
    {
        if (OutError != nullptr)
        {
            *OutError = Message;
        }
        return false;
    }
}

FDWCTransparencyFinalSettingsSnapshot FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(
    const FWetClothingTransparencyData& Data)
{
    FDWCTransparencyFinalSettingsSnapshot Result;
    Result.TransparencyStrength = Data.TransparencyPreviewStrength;
    Result.WrinkleSuppressionStrength = Data.WrinkleSuppressionStrength;
    Result.WrinkleMaskThreshold = Data.WrinkleSuppressionCoverageThreshold;
    Result.WrinkleMaskSoftness = Data.WrinkleSuppressionMaskSoftness;
    Result.PaddingPixels = Data.TransparencyPaddingPixels;
    Result.EdgeFeatherPixels = Data.TransparencyEdgeFeatherPixels;
    return Result;
}

bool FDWCTransparencyFinalSettingsSnapshot::IsValid(FString* OutError) const
{
    if (!FMath::IsFinite(TransparencyStrength) || TransparencyStrength < 0.0f ||
        !FMath::IsFinite(WrinkleSuppressionStrength) || WrinkleSuppressionStrength < 0.0f ||
        !FMath::IsFinite(WrinkleMaskThreshold) || !FMath::IsWithinInclusive(WrinkleMaskThreshold, 0.0f, 1.0f) ||
        !FMath::IsFinite(WrinkleMaskSoftness) || !FMath::IsWithinInclusive(WrinkleMaskSoftness, 0.0f, 1.0f) ||
        PaddingPixels < 0 || PaddingPixels > 64 ||
        !FMath::IsFinite(EdgeFeatherPixels) || EdgeFeatherPixels < 0.0f)
    {
        return Fail(OutError, TEXT("Stage 4 settings contain an invalid or out-of-range value."));
    }
    return true;
}

bool FDWCTransparencyAlphaWorkingSnapshot::IsValid(FString* OutError) const
{
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return Fail(OutError, TEXT("The Stage 4 alpha snapshot has an invalid resolution."));
    }
    if (BaselineStrokeCount < 0 || AuthoredStrokeCount < BaselineStrokeCount || AppliedSampleCount < 0)
    {
        return Fail(OutError, TEXT("The Stage 4 alpha snapshot has an invalid stroke range."));
    }
    if (Mode == EDWCTransparencyAlphaSnapshotMode::SparseTiles)
    {
        if (!FallbackStrokes.IsEmpty())
        {
            return Fail(OutError, TEXT("A sparse alpha snapshot cannot also own fallback strokes."));
        }
        TSet<FIntPoint> Coordinates;
        for (const FDWCTransparencyAlphaTilePayload& Tile : ModifiedTiles)
        {
            if (!Tile.IsValidFor(Resolution, FDWCTransparencyAlphaTileStore::TileSize) ||
                Coordinates.Contains(Tile.TileCoordinate))
            {
                return Fail(OutError, TEXT("The Stage 4 alpha snapshot contains an invalid or duplicate tile."));
            }
            Coordinates.Add(Tile.TileCoordinate);
        }
    }
    else if (!ModifiedTiles.IsEmpty())
    {
        return Fail(OutError, TEXT("A stroke-replay alpha snapshot cannot also own sparse tiles."));
    }
    else if (AuthoredStrokeCount != FallbackStrokes.Num())
    {
        return Fail(OutError, TEXT("A stroke-replay alpha snapshot must own the complete authored stroke range."));
    }
    return true;
}

uint64 FDWCTransparencyAlphaWorkingSnapshot::GetAllocatedBytes() const
{
    uint64 Bytes = ModifiedTiles.GetAllocatedSize() + FallbackStrokes.GetAllocatedSize();
    for (const FDWCTransparencyAlphaTilePayload& Tile : ModifiedTiles)
    {
        Bytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCTransparencyBrushStroke& Stroke : FallbackStrokes)
    {
        Bytes += Stroke.DisplayName.GetAllocatedSize() + Stroke.Samples.GetAllocatedSize();
    }
    return Bytes;
}

bool FDWCTransparencyFinalWorkingSet::IsValid(FString* OutError) const
{
    if (!Identity.IsValid() || !SourcePayload.IsValid() || SourceSignature.IsEmpty() ||
        RevealSignature.IsEmpty() || AlphaAuthoringSignature.IsEmpty() ||
        SuppressionSettingsSignature.IsEmpty() || FinalSignature.IsEmpty())
    {
        return Fail(OutError, TEXT("The Stage 4 working set is missing identity, payload, or signatures."));
    }
    if (!Settings.IsValid(OutError) || !Alpha.IsValid(OutError) || !WrinkleDependency.IsValid(OutError))
    {
        return false;
    }
    return Identity.LayerGuid == SourcePayload->LayerGuid &&
        Identity.MaterialSlotIndex == SourcePayload->MaterialSlotIndex &&
        Identity.DataUVChannelIndex == SourcePayload->UVChannelIndex &&
        Identity.LODIndex == SourcePayload->LODIndex &&
        Identity.Resolution == SourcePayload->Resolution &&
        Alpha.Resolution == Identity.Resolution;
}

bool FDWCTransparencyFinalWorkingSetBuilder::Build(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload,
    const FDWCTransparencyFinalSettingsSnapshot& Settings,
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
    FDWCWrinkleSuppressionDependencySnapshot WrinkleDependency,
    const uint64 AuthoringRevision,
    FDWCTransparencyFinalWorkingSet& OutWorkingSet,
    FString& OutError)
{
    OutWorkingSet = FDWCTransparencyFinalWorkingSet();
    OutError.Reset();
    if (!SourcePayload.IsValid() || !Settings.IsValid(&OutError) || !AlphaSnapshot.IsValid(&OutError))
    {
        return false;
    }
    const int32 DataUV = Asset.GetDWCDataUVChannelIndex();
    if (SourcePayload->LayerGuid != Layer.LayerGuid ||
        SourcePayload->MaterialSlotIndex != Layer.TargetSurface.OuterMaterialSlotIndex ||
        SourcePayload->UVChannelIndex != DataUV || SourcePayload->LODIndex != 0 ||
        SourcePayload->Resolution != AlphaSnapshot.Resolution)
    {
        OutError = TEXT("The Stage 4 source payload does not match the selected layer, DWC Data UV, LOD 0, or alpha resolution.");
        return false;
    }

    OutWorkingSet.Identity.LayerGuid = Layer.LayerGuid;
    OutWorkingSet.Identity.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutWorkingSet.Identity.DataUVChannelIndex = DataUV;
    OutWorkingSet.Identity.LODIndex = 0;
    OutWorkingSet.Identity.Resolution = SourcePayload->Resolution;
    OutWorkingSet.Identity.Revision = AuthoringRevision;
    OutWorkingSet.SourcePayload = MoveTemp(SourcePayload);
    OutWorkingSet.Settings = Settings;
    OutWorkingSet.Alpha = MoveTemp(AlphaSnapshot);
    OutWorkingSet.WrinkleDependency = MoveTemp(WrinkleDependency);
    OutWorkingSet.AuthoringRevision = AuthoringRevision;
    OutWorkingSet.SourceSignature = OutWorkingSet.SourcePayload->BuildSignature;
    OutWorkingSet.RevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        OutWorkingSet.SourceSignature, Layer);
    OutWorkingSet.AlphaAuthoringSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    OutWorkingSet.SuppressionSettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
            Settings.WrinkleMaskThreshold,
            Settings.WrinkleMaskSoftness,
            Settings.WrinkleSuppressionStrength,
            Settings.TransparencyStrength);

    FDWCTransparencyFinalSignatureInputs SignatureInputs;
    SignatureInputs.RevealSignature = OutWorkingSet.RevealSignature;
    SignatureInputs.AlphaAuthoringSignature = OutWorkingSet.AlphaAuthoringSignature;
    SignatureInputs.WrinkleMaskBuildSignature = OutWorkingSet.WrinkleDependency.BuildSignature;
    SignatureInputs.SuppressionSettingsSignature = OutWorkingSet.SuppressionSettingsSignature;
    SignatureInputs.PaddingPixels = Settings.PaddingPixels;
    SignatureInputs.EdgeFeatherPixels = Settings.EdgeFeatherPixels;
    OutWorkingSet.FinalSignature =
        FDWCTransparencySignatureService::BuildFinalSignature(SignatureInputs);
    OutWorkingSet.OwnedBytes = OutWorkingSet.Alpha.GetAllocatedBytes();
    OutWorkingSet.RetainedBytes = OutWorkingSet.SourcePayload->GetAllocatedBytes();
    return OutWorkingSet.IsValid(&OutError);
}

FDWCTransparencyFinalCurrentness FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(
    const FWetClothingBakedTransparencyMap* BakedMap,
    const FDWCTransparencyFinalWorkingSet& WorkingSet)
{
    FDWCTransparencyFinalCurrentness Result;
    if (BakedMap == nullptr || !BakedMap->IsRuntimeUsable())
    {
        Result.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Result.Detail = TEXT("The final Transparency Map has not been baked.");
    }
    else if (BakedMap->MaterialSlotIndex != WorkingSet.Identity.MaterialSlotIndex ||
        BakedMap->Resolution != WorkingSet.Identity.Resolution.X)
    {
        Result.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Result.Detail = TEXT("The baked map does not match the Stage 4 slot or resolution.");
    }
    else if (BakedMap->SourceWrinkleMaskBuildSignature != WorkingSet.WrinkleDependency.BuildSignature ||
        BakedMap->SourceWrinkleMaskBakeGuid != WorkingSet.WrinkleDependency.BakeGuid)
    {
        Result.Reason = EDWCTransparencyStaleReason::WrinkleDependencyChanged;
        Result.Detail = TEXT("The wrinkle suppression dependency changed.");
    }
    else if (BakedMap->PaddingPixels != WorkingSet.Settings.PaddingPixels ||
        BakedMap->WrinkleSuppressionSettingsSignature != WorkingSet.SuppressionSettingsSignature)
    {
        Result.Reason = EDWCTransparencyStaleReason::OutputSettingsChanged;
        Result.Detail = TEXT("The final transparency output settings changed.");
    }
    else if (BakedMap->BuildSignature != WorkingSet.FinalSignature)
    {
        Result.Reason = EDWCTransparencyStaleReason::AlphaEditsChanged;
        Result.Detail = TEXT("The Stage 4 alpha authoring or reveal input changed.");
    }
    return Result;
}
