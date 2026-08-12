//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
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

    int32 WrapCoordinate(const int32 Value, const int32 Size)
    {
        return Size > 0 ? (Value % Size + Size) % Size : 0;
    }

    bool MatchesResolvedOutputResolution(
        const UTexture2D* Texture,
        const FIntPoint Resolution)
    {
        return Texture != nullptr && Resolution.X > 0 && Resolution.Y > 0 &&
            Texture->Source.GetSizeX() == Resolution.X &&
            Texture->Source.GetSizeY() == Resolution.Y;
    }
}

TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot>
FDWCTransparencyAlphaDomainSnapshot::Create(
    const FDWCTransparencySourcePayload& SourcePayload,
    FString* OutError,
    const bool bIncludeOuterIslandIDs)
{
    TSharedPtr<FDWCTransparencyAlphaDomainSnapshot> Result =
        MakeShared<FDWCTransparencyAlphaDomainSnapshot>();
    Result->LayerGuid = SourcePayload.LayerGuid;
    Result->MaterialSlotIndex = SourcePayload.MaterialSlotIndex;
    Result->Resolution = SourcePayload.Resolution;
    Result->OutputResolutionIdentity = SourcePayload.OutputResolutionIdentity;
    Result->SourceSignature = SourcePayload.BuildSignature;
    Result->BaseAlpha = SourcePayload.AutoAlphaBuffer;
    Result->OuterCoverage = SourcePayload.OuterCoverageBuffer;
    if (bIncludeOuterIslandIDs)
    {
        Result->OuterIslandIDs = SourcePayload.OuterIslandIDBuffer;
    }
    Result->ValidSource = SourcePayload.ValidHitBuffer;
    if (!Result->IsValid(OutError))
    {
        return nullptr;
    }
    return Result;
}

bool FDWCTransparencyAlphaDomainSnapshot::IsValid(FString* OutError) const
{
    const int64 PixelCount = static_cast<int64>(Resolution.X) * Resolution.Y;
    if (!LayerGuid.IsValid() || MaterialSlotIndex == INDEX_NONE ||
        Resolution.X <= 0 || Resolution.Y <= 0 || OutputResolutionIdentity.IsEmpty() ||
        SourceSignature.IsEmpty() ||
        BaseAlpha.Num() != PixelCount || OuterCoverage.Num() != PixelCount ||
        (OuterIslandIDs.Num() != 0 && OuterIslandIDs.Num() != PixelCount) ||
        ValidSource.Num() != PixelCount)
    {
        return Fail(OutError, TEXT("The Stage 4 alpha domain is missing identity or alpha-domain buffers."));
    }
    return true;
}

uint64 FDWCTransparencyAlphaDomainSnapshot::GetAllocatedBytes() const
{
    return static_cast<uint64>(sizeof(FDWCTransparencyAlphaDomainSnapshot)) +
        static_cast<uint64>(OutputResolutionIdentity.GetAllocatedSize()) +
        static_cast<uint64>(SourceSignature.GetAllocatedSize()) +
        static_cast<uint64>(BaseAlpha.GetAllocatedSize()) +
        static_cast<uint64>(OuterCoverage.GetAllocatedSize()) +
        static_cast<uint64>(OuterIslandIDs.GetAllocatedSize()) +
        static_cast<uint64>(ValidSource.GetAllocatedSize());
}

int32 FDWCTransparencyAlphaDomainSnapshot::ResolveOuterIslandIDAtUV(
    const FVector2D& PositionUV,
    const int32 FallbackUVIslandID,
    const bool bWrap) const
{
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return FallbackUVIslandID;
    }
    int32 X = FMath::FloorToInt(PositionUV.X * Resolution.X);
    int32 Y = FMath::FloorToInt(PositionUV.Y * Resolution.Y);
    if (bWrap)
    {
        X = WrapCoordinate(X, Resolution.X);
        Y = WrapCoordinate(Y, Resolution.Y);
    }
    else
    {
        X = FMath::Clamp(X, 0, Resolution.X - 1);
        Y = FMath::Clamp(Y, 0, Resolution.Y - 1);
    }
    const int32 PixelIndex = Y * Resolution.X + X;
    return OuterIslandIDs.IsValidIndex(PixelIndex)
        ? FDWCTransparencySourcePayload::DecodeOuterIslandID(OuterIslandIDs[PixelIndex])
        : FallbackUVIslandID;
}

bool FDWCTransparencyAlphaDomainSnapshot::MatchesOuterIslandID(
    const int32 PixelIndex,
    const int32 UVIslandID) const
{
    return OuterCoverage.IsValidIndex(PixelIndex) && OuterCoverage[PixelIndex] != 0 &&
        (UVIslandID == INDEX_NONE ||
         (OuterIslandIDs.IsValidIndex(PixelIndex) &&
          FDWCTransparencySourcePayload::MatchesOuterIslandID(
              OuterIslandIDs[PixelIndex], UVIslandID)));
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
    if (!Identity.IsValid() || !AlphaDomain.IsValid() || SourceSignature.IsEmpty() ||
        RevealSignature.IsEmpty() || RevealNormalSignature.IsEmpty() || AlphaAuthoringSignature.IsEmpty() ||
        SuppressionSettingsSignature.IsEmpty() || FinalAlphaSignature.IsEmpty() || FinalSignature.IsEmpty())
    {
        return Fail(OutError, TEXT("The Stage 4 working set is missing identity, payload, or signatures."));
    }
    if (!Settings.IsValid(OutError) || !Alpha.IsValid(OutError) || !WrinkleDependency.IsValid(OutError))
    {
        return false;
    }
    return Identity.LayerGuid == AlphaDomain->LayerGuid &&
        Identity.MaterialSlotIndex == AlphaDomain->MaterialSlotIndex &&
        Identity.LODIndex == 0 &&
        Identity.Resolution == AlphaDomain->Resolution &&
        Identity.OutputResolutionIdentity == AlphaDomain->OutputResolutionIdentity &&
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
        SourcePayload->Resolution != AlphaSnapshot.Resolution ||
        SourcePayload->OutputResolutionIdentity.IsEmpty())
    {
        OutError = TEXT("The Stage 4 source payload does not match the selected layer, DWC Data UV, LOD 0, or alpha resolution.");
        return false;
    }

    OutWorkingSet.Identity.LayerGuid = Layer.LayerGuid;
    OutWorkingSet.Identity.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutWorkingSet.Identity.DataUVChannelIndex = DataUV;
    OutWorkingSet.Identity.LODIndex = 0;
    OutWorkingSet.Identity.Resolution = SourcePayload->Resolution;
    OutWorkingSet.Identity.OutputResolutionIdentity =
        SourcePayload->OutputResolutionIdentity;
    OutWorkingSet.Identity.Revision = AuthoringRevision;
    const bool bRequiresOuterIslandIDs =
        AlphaSnapshot.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    OutWorkingSet.AlphaDomain = FDWCTransparencyAlphaDomainSnapshot::Create(
        *SourcePayload,
        &OutError,
        bRequiresOuterIslandIDs);
    if (!OutWorkingSet.AlphaDomain.IsValid())
    {
        return false;
    }
    OutWorkingSet.Settings = Settings;
    OutWorkingSet.Alpha = MoveTemp(AlphaSnapshot);
    OutWorkingSet.WrinkleDependency = MoveTemp(WrinkleDependency);
    OutWorkingSet.AuthoringRevision = AuthoringRevision;
    OutWorkingSet.bRequiresRevealSurface = Layer.RequiresRevealSurface();
    OutWorkingSet.bRequiresRuntimeRevealNormal = Layer.RequiresRuntimeRevealNormal();
    OutWorkingSet.SourceSignature = OutWorkingSet.AlphaDomain->SourceSignature;
    OutWorkingSet.RevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        OutWorkingSet.SourceSignature,
        Layer,
        Asset.Authored.TransparencyData.RevealMetallicDarkeningStrength);
    OutWorkingSet.RevealNormalSignature =
        FDWCTransparencySignatureService::BuildRevealNormalSignature(
            OutWorkingSet.SourceSignature);
    OutWorkingSet.AlphaAuthoringSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    OutWorkingSet.SuppressionSettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
            Settings.WrinkleMaskThreshold,
            Settings.WrinkleMaskSoftness,
            Settings.WrinkleSuppressionStrength,
            Settings.TransparencyStrength);

    FDWCTransparencyFinalSignatureInputs SignatureInputs;
    SignatureInputs.SourceSignature = OutWorkingSet.SourceSignature;
    SignatureInputs.RevealSignature = OutWorkingSet.RevealSignature;
    SignatureInputs.AlphaAuthoringSignature = OutWorkingSet.AlphaAuthoringSignature;
    SignatureInputs.WrinkleMaskBuildSignature = OutWorkingSet.WrinkleDependency.BuildSignature;
    SignatureInputs.SuppressionSettingsSignature = OutWorkingSet.SuppressionSettingsSignature;
    SignatureInputs.PaddingPixels = Settings.PaddingPixels;
    SignatureInputs.EdgeFeatherPixels = Settings.EdgeFeatherPixels;
    OutWorkingSet.FinalAlphaSignature =
        FDWCTransparencySignatureService::BuildFinalAlphaSignature(SignatureInputs);
    OutWorkingSet.FinalSignature =
        FDWCTransparencySignatureService::BuildFinalSignature(SignatureInputs);
    OutWorkingSet.OwnedBytes = OutWorkingSet.Alpha.GetAllocatedBytes();
    OutWorkingSet.RetainedBytes = OutWorkingSet.AlphaDomain->GetAllocatedBytes();
    return OutWorkingSet.IsValid(&OutError);
}

FDWCTransparencyFinalCurrentness FDWCTransparencyFinalWorkingSetBuilder::EvaluateCurrentness(
    const FWetClothingBakedTransparencyMap* BakedMap,
    const FDWCTransparencyFinalWorkingSet& WorkingSet)
{
    FDWCTransparencyFinalCurrentness Result;
    if (BakedMap == nullptr ||
        !BakedMap->IsRuntimeUsableForLayer(WorkingSet.bRequiresRuntimeRevealNormal))
    {
        Result.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Result.Detail = WorkingSet.bRequiresRuntimeRevealNormal
            ? TEXT("The final Transparency Map or its required Reveal Surface payload has not been baked.")
            : TEXT("The final Transparency Map has not been baked.");
    }
    else if (BakedMap->MaterialSlotIndex != WorkingSet.Identity.MaterialSlotIndex ||
        BakedMap->Resolution != WorkingSet.Identity.Resolution.X)
    {
        Result.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Result.Detail = TEXT("The baked map does not match the Stage 4 slot or resolution.");
    }
    else if (!MatchesResolvedOutputResolution(
        BakedMap->TransparencyMap.Get(), WorkingSet.Identity.Resolution))
    {
        Result.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Result.Detail = TEXT("The baked Transparency Map dimensions do not match the resolved output resolution.");
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
    else if (WorkingSet.bRequiresRuntimeRevealNormal &&
        BakedMap->RevealNormalBuildSignature != WorkingSet.RevealNormalSignature)
    {
        Result.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Result.Detail = TEXT("The runtime Reveal Normal needs to be rebuilt from the current Stage 2 source.");
    }
    else if (WorkingSet.bRequiresRuntimeRevealNormal &&
        !BakedMap->HasRuntimeRevealNormalPayload())
    {
        Result.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Result.Detail = TEXT("This raycast transparency layer requires a coverage-weighted runtime Reveal Normal.");
    }
    else if (WorkingSet.bRequiresRuntimeRevealNormal &&
        !MatchesResolvedOutputResolution(
            BakedMap->RevealNormalMap.Get(), WorkingSet.Identity.Resolution))
    {
        Result.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Result.Detail = TEXT("The baked Reveal Normal dimensions do not match the resolved output resolution.");
    }
    else if (BakedMap->FinalAlphaBuildSignature != WorkingSet.FinalAlphaSignature)
    {
        Result.Reason = EDWCTransparencyStaleReason::AlphaEditsChanged;
        Result.Detail = TEXT("The Stage 4 alpha authoring, suppression dependency, or output settings changed.");
    }
    else if (BakedMap->BuildSignature != WorkingSet.FinalSignature)
    {
        Result.Reason = EDWCTransparencyStaleReason::RevealEditsChanged;
        Result.Detail = TEXT("The Stage 3 corrected reveal color changed.");
    }
    return Result;
}
