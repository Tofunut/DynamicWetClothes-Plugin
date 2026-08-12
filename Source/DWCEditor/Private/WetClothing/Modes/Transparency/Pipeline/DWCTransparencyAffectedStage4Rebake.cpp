//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageArtifactContract.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"

namespace
{
    const FWetClothingTransparencyLayerData* FindLayer(
        const UWetClothingAsset& Asset,
        const FGuid& LayerGuid)
    {
        return Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
            [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
            {
                return Candidate.LayerGuid == LayerGuid;
            });
    }

    bool ReadArtifact(
        UTexture2D* Texture,
        const ETextureSourceFormat ExpectedFormat,
        const FIntPoint ExpectedResolution,
        TArray64<uint8>& OutBytes,
        FString& OutError)
    {
#if WITH_EDITORONLY_DATA
        OutBytes.Reset();
        if (Texture == nullptr || !Texture->Source.IsValid())
        {
            OutError = TEXT("A required Stage 2 Temp artifact is missing or has no source data.");
            return false;
        }
        if (Texture->Source.GetFormat() != ExpectedFormat ||
            Texture->Source.GetSizeX() != ExpectedResolution.X ||
            Texture->Source.GetSizeY() != ExpectedResolution.Y)
        {
            OutError = FString::Printf(
                TEXT("Stage 2 Temp artifact '%s' has an unexpected format or resolution."),
                *Texture->GetName());
            return false;
        }
        if (!Texture->Source.GetMipData(OutBytes, 0))
        {
            OutError = FString::Printf(
                TEXT("Could not read Stage 2 Temp artifact '%s'."),
                *Texture->GetName());
            return false;
        }
        return true;
#else
        OutError = TEXT("Stage 2 Temp artifact restore requires editor source data.");
        return false;
#endif
    }

    bool HasCurrentCanonicalArtifacts(
        const FWetClothingTransparencyLayerData& Layer,
        const FString& SourceSignature)
    {
        const FDWCTransparencyTempArtifactReference* BaseReveal =
            FDWCTransparencyStageArtifactContract::FindReference(
                Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor);
        FString ArtifactError;
        return BaseReveal != nullptr &&
            FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
                Layer,
                SourceSignature,
                BaseReveal->Resolution,
                false,
                ArtifactError);
    }

    FDWCTransparencyAffectedRebakeCandidate EvaluateLayer(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer)
    {
        FDWCTransparencyAffectedRebakeCandidate Candidate;
        Candidate.LayerGuid = Layer.LayerGuid;
        Candidate.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;

        const FWetClothingBakedTransparencyMap* BakedMap =
            Asset.Authored.TransparencyData.FindBakedTransparencyMap(Candidate.MaterialSlotIndex);
        if (BakedMap == nullptr || !BakedMap->IsRuntimeUsableForLayer(Layer.RequiresRuntimeRevealNormal()))
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::MissingBakedMap;
            Candidate.Detail = TEXT("No previous Stage 4 Transparency Map is available.");
            return Candidate;
        }

        FDWCTransparencySourcePayload SignatureOnly;
        FString SignatureError;
        if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
                Asset, Layer, SignatureOnly, SignatureError))
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::NonWrinkleInputsChanged;
            Candidate.Detail = SignatureError;
            return Candidate;
        }

        const FDWCTransparencyFinalSettingsSnapshot Settings =
            FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(Asset.Authored.TransparencyData);
        const FString RevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
            SignatureOnly.BuildSignature,
            Layer,
            Asset.Authored.TransparencyData.RevealMetallicDarkeningStrength);
        const FString RevealNormalSignature =
            FDWCTransparencySignatureService::BuildRevealNormalSignature(
                SignatureOnly.BuildSignature);
        const FString SuppressionSignature =
            FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
                Settings.WrinkleMaskThreshold,
                Settings.WrinkleMaskSoftness,
                Settings.WrinkleSuppressionStrength,
                Settings.TransparencyStrength);
        const FString ExpectedUsingPreviousWrinkle =
            FDWCTransparencySignatureService::BuildFinalSignature(
                RevealSignature,
                Layer,
                BakedMap->SourceWrinkleMaskBuildSignature,
                SuppressionSignature,
                Settings.PaddingPixels,
                Settings.EdgeFeatherPixels,
                SignatureOnly.BuildSignature);

        if (BakedMap->Resolution != SignatureOnly.Resolution.X ||
            BakedMap->PaddingPixels != Settings.PaddingPixels ||
            BakedMap->WrinkleSuppressionSettingsSignature != SuppressionSignature ||
            BakedMap->BuildSignature != ExpectedUsingPreviousWrinkle ||
            (Layer.RequiresRevealSurface() &&
             BakedMap->RevealNormalBuildSignature != RevealNormalSignature))
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::NonWrinkleInputsChanged;
            Candidate.Detail =
                TEXT("Stage 2/3 inputs, alpha edits, output settings, or resolution changed; run the full Transparency bake workflow.");
            return Candidate;
        }

        const FDWCWrinkleSuppressionDependencySnapshot CurrentWrinkle =
            FDWCWrinkleSuppressionCoverageService::ResolveDependency(
                &Asset, Candidate.MaterialSlotIndex, true);
        const FString CurrentWrinkleSignature = CurrentWrinkle.IsAvailable()
            ? CurrentWrinkle.BuildSignature
            : FString();
        const FGuid CurrentWrinkleGuid = CurrentWrinkle.IsAvailable()
            ? CurrentWrinkle.BakeGuid
            : FGuid();
        if (BakedMap->SourceWrinkleMaskBuildSignature == CurrentWrinkleSignature &&
            BakedMap->SourceWrinkleMaskBakeGuid == CurrentWrinkleGuid)
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::AlreadyCurrent;
            Candidate.Detail = TEXT("The Stage 4 Transparency Map already uses the current wrinkle dependency.");
            return Candidate;
        }

        if (!HasCurrentCanonicalArtifacts(Layer, SignatureOnly.BuildSignature))
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::MissingCanonicalArtifact;
            Candidate.Detail =
                TEXT("The canonical Stage 2 Temp artifacts are missing or stale; regenerate Stage 2 before rebaking Stage 4.");
            return Candidate;
        }

        Candidate.Status = EDWCTransparencyAffectedRebakeStatus::Eligible;
        Candidate.Detail = TEXT("Only the wrinkle suppression dependency changed.");
        return Candidate;
    }
}

void FDWCTransparencyAffectedRebakeSequence::Initialize(TArray<FGuid> InLayerGuids)
{
    LayerGuids = MoveTemp(InLayerGuids);
    NextIndex = 0;
    bActive = false;
    ActivePayloadBytes = 0;
    PeakPayloadBytes = 0;
}

bool FDWCTransparencyAffectedRebakeSequence::TryBeginNext(FGuid& OutLayerGuid)
{
    OutLayerGuid.Invalidate();
    if (bActive || !LayerGuids.IsValidIndex(NextIndex))
    {
        return false;
    }
    OutLayerGuid = LayerGuids[NextIndex++];
    bActive = true;
    ActivePayloadBytes = 0;
    return true;
}

void FDWCTransparencyAffectedRebakeSequence::SetActivePayloadBytes(const uint64 InBytes)
{
    check(bActive);
    ActivePayloadBytes = InBytes;
    PeakPayloadBytes = FMath::Max(PeakPayloadBytes, ActivePayloadBytes);
}

void FDWCTransparencyAffectedRebakeSequence::CompleteActive()
{
    bActive = false;
    ActivePayloadBytes = 0;
}

void FDWCTransparencyAffectedRebakeSequence::DiscardRemaining()
{
    NextIndex = LayerGuids.Num();
    CompleteActive();
}

void FDWCTransparencyAffectedStage4Rebake::CollectCandidates(
    const UWetClothingAsset& Asset,
    const TConstArrayView<int32> MaterialSlotIndices,
    TArray<FDWCTransparencyAffectedRebakeCandidate>& OutCandidates)
{
    check(IsInGameThread());
    OutCandidates.Reset();
    TSet<int32> RequestedSlots;
    RequestedSlots.Append(MaterialSlotIndices);
    for (const FWetClothingTransparencyLayerData& Layer :
         Asset.Authored.TransparencyData.TransparencyLayers)
    {
        if (!Layer.IsRuntimeEnabled())
        {
            continue;
        }
        const int32 Slot = Layer.TargetSurface.OuterMaterialSlotIndex;
        if (!RequestedSlots.IsEmpty() && !RequestedSlots.Contains(Slot))
        {
            continue;
        }
        OutCandidates.Add(EvaluateLayer(Asset, Layer));
    }
}

bool FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalSource(
    const UWetClothingAsset& Asset,
    const FGuid& LayerGuid,
    FDWCTransparencySourcePayload& OutResult,
    FString& OutError)
{
    check(IsInGameThread());
    OutResult = FDWCTransparencySourcePayload();
    OutError.Reset();
    const FWetClothingTransparencyLayerData* Layer = FindLayer(Asset, LayerGuid);
    if (Layer == nullptr)
    {
        OutError = TEXT("The affected transparency layer no longer exists.");
        return false;
    }
    FDWCTransparencySourcePayload Identity;
    if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
            Asset, *Layer, Identity, OutError))
    {
        return false;
    }

    return RestoreCanonicalArtifacts(*Layer, Identity, OutResult, OutError);
}

bool FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& Identity,
    FDWCTransparencySourcePayload& OutResult,
    FString& OutError,
    const bool bRequireOuterIslandID)
{
    check(IsInGameThread());
    OutResult = Identity;
    OutError.Reset();
    if (OutResult.BuildSignature.IsEmpty() || OutResult.Resolution.X <= 0 ||
        OutResult.Resolution.Y <= 0)
    {
        OutError = TEXT("The canonical Stage 2 identity is invalid.");
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }

    const auto FindSourceArtifact = [&Layer, &OutResult](
        const EDWCTransparencyTempArtifactKind Kind)
    {
        return FDWCTransparencyTempAssetStore::FindCurrentArtifact(
            Layer,
            Kind,
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                Kind, OutResult.BuildSignature),
            true);
    };
    FString ArtifactError;
    const FDWCTransparencySourceArtifactSelection ArtifactSelection =
        FDWCTransparencySourceArtifactSelection::Stage4(
            Layer.RequiresRevealSurface(),
            bRequireOuterIslandID);
    if (!FDWCTransparencyStageArtifactContract::InspectSourceArtifactSet(
            Layer,
            OutResult.BuildSignature,
            OutResult.Resolution,
            ArtifactSelection,
            true,
            ArtifactError))
    {
        OutError = ArtifactError;
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }

    UTexture2D* BaseReveal = FindSourceArtifact(
        EDWCTransparencyTempArtifactKind::BaseRevealColor);
    UTexture2D* BaseRevealSurface = Layer.RequiresRevealSurface()
        ? FDWCTransparencyTempAssetStore::FindCurrentArtifact(
            Layer,
            EDWCTransparencyTempArtifactKind::BaseRevealSurface,
            FDWCTransparencyStageArtifactContract::BuildExpectedSignature(
                EDWCTransparencyTempArtifactKind::BaseRevealSurface,
                OutResult.BuildSignature),
            true)
        : nullptr;
    UTexture2D* ValidHit = FindSourceArtifact(EDWCTransparencyTempArtifactKind::ValidHit);
    UTexture2D* OuterCoverage = FindSourceArtifact(EDWCTransparencyTempArtifactKind::OuterCoverage);
    UTexture2D* OuterIslandID = bRequireOuterIslandID
        ? FindSourceArtifact(EDWCTransparencyTempArtifactKind::OuterIslandID)
        : nullptr;

    const int32 PixelCount = OutResult.Resolution.X * OutResult.Resolution.Y;
    if (PixelCount <= 0)
    {
        OutError = TEXT("The canonical Stage 2 Temp artifacts have an invalid pixel count.");
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }

    OutResult.InnerColorBuffer.SetNumUninitialized(PixelCount);
    if (Layer.RequiresRevealSurface())
    {
        OutResult.RevealSurfaceAuthoring.SetNumUninitialized(OutResult.Resolution);
    }
    OutResult.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    OutResult.OuterCoverageBuffer.SetNumUninitialized(PixelCount);
    if (bRequireOuterIslandID)
    {
        OutResult.OuterIslandIDBuffer.SetNumUninitialized(PixelCount);
    }
    OutResult.ValidHitBuffer.Init(false, PixelCount);
    OutResult.ValidHitCount = 0;
    OutResult.OuterSampleCount = 0;
    OutResult.NoHitCount = 0;

    TArray64<uint8> ArtifactBytes;
    const auto ReadExpected = [&ArtifactBytes, &OutResult, &OutError](
        UTexture2D* Texture,
        const ETextureSourceFormat Format,
        const int64 ExpectedBytes)
    {
        ArtifactBytes.Reset();
        if (!ReadArtifact(Texture, Format, OutResult.Resolution, ArtifactBytes, OutError))
        {
            return false;
        }
        if (ArtifactBytes.Num() != ExpectedBytes)
        {
            OutError = TEXT("A canonical Stage 2 Temp artifact has an invalid payload size.");
            return false;
        }
        return true;
    };

    const int64 ColorBytes = static_cast<int64>(PixelCount) * sizeof(FColor);
    const int64 UInt16Bytes = static_cast<int64>(PixelCount) * sizeof(uint16);
    if (!ReadExpected(BaseReveal, TSF_BGRA8, ColorBytes))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }
    FMemory::Memcpy(
        OutResult.InnerColorBuffer.GetData(),
        ArtifactBytes.GetData(),
        static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        const FColor Packed = OutResult.InnerColorBuffer[Index];
        OutResult.AutoAlphaBuffer[Index] = Packed.A;
        OutResult.InnerColorBuffer[Index].A = 255;
    }

    if (Layer.RequiresRevealSurface() &&
        !ReadExpected(BaseRevealSurface, TSF_BGRA8, ColorBytes))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }
    if (Layer.RequiresRevealSurface())
    {
        FMemory::Memcpy(
            OutResult.RevealSurfaceAuthoring.GetData(),
            ArtifactBytes.GetData(),
            static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
    }

    if (!ReadExpected(ValidHit, TSF_G8, PixelCount))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        const bool bValidHit = ArtifactBytes[Index] != 0;
        OutResult.ValidHitBuffer[Index] = bValidHit;
        OutResult.ValidHitCount += bValidHit ? 1 : 0;
    }

    if (!ReadExpected(OuterCoverage, TSF_G8, PixelCount))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }
    FMemory::Memcpy(
        OutResult.OuterCoverageBuffer.GetData(),
        ArtifactBytes.GetData(),
        static_cast<SIZE_T>(PixelCount));
    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        const bool bCovered = OutResult.OuterCoverageBuffer[Index] != 0;
        const bool bValidHit = OutResult.ValidHitBuffer[Index];
        OutResult.OuterSampleCount += bCovered ? 1 : 0;
        OutResult.NoHitCount += bCovered && !bValidHit ? 1 : 0;
    }

    if (bRequireOuterIslandID &&
        !ReadExpected(OuterIslandID, TSF_G16, UInt16Bytes))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }
    if (bRequireOuterIslandID)
    {
        FMemory::Memcpy(
            OutResult.OuterIslandIDBuffer.GetData(),
            ArtifactBytes.GetData(),
            static_cast<SIZE_T>(PixelCount) * sizeof(uint16));
    }
    return true;
}
