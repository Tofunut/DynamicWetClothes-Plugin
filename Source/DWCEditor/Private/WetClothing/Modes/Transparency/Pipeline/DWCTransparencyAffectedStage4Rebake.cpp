//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
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
        constexpr EDWCTransparencyTempArtifactKind RequiredKinds[] = {
            EDWCTransparencyTempArtifactKind::BaseRevealColor,
            EDWCTransparencyTempArtifactKind::BaseRevealSurface,
            EDWCTransparencyTempArtifactKind::ValidHit,
            EDWCTransparencyTempArtifactKind::OuterCoverage,
            EDWCTransparencyTempArtifactKind::OuterIslandID,
            EDWCTransparencyTempArtifactKind::HitSource,
            EDWCTransparencyTempArtifactKind::HitDistance
        };
        for (const EDWCTransparencyTempArtifactKind Kind : RequiredKinds)
        {
            if (!FDWCTransparencyTempAssetStore::HasCurrentArtifact(
                    Layer, Kind, SourceSignature))
            {
                return false;
            }
        }
        return true;
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
        if (BakedMap == nullptr || !BakedMap->IsRuntimeUsableForLayer(Layer.RequiresRevealSurface()))
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
            SignatureOnly.BuildSignature, Layer);
        const FString RevealSurfaceSignature =
            FDWCTransparencySignatureService::BuildRevealSurfaceSignature(
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
                Settings.EdgeFeatherPixels);

        if (BakedMap->Resolution != SignatureOnly.Resolution.X ||
            BakedMap->PaddingPixels != Settings.PaddingPixels ||
            BakedMap->WrinkleSuppressionSettingsSignature != SuppressionSignature ||
            BakedMap->BuildSignature != ExpectedUsingPreviousWrinkle ||
            (Layer.RequiresRevealSurface() &&
             BakedMap->RevealSurfaceBuildSignature != RevealSurfaceSignature))
        {
            Candidate.Status = EDWCTransparencyAffectedRebakeStatus::NonWrinkleInputsChanged;
            Candidate.Detail =
                TEXT("Stage 2/3 inputs, alpha edits, output settings, or resolution changed; run the full Transparency bake workflow.");
            return Candidate;
        }

        const FDWCWrinkleSuppressionDependencySnapshot CurrentWrinkle =
            FDWCWrinkleSuppressionCoverageService::ResolveDependency(
                &Asset, Candidate.MaterialSlotIndex);
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
    FString& OutError)
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

    UTexture2D* BaseReveal = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::BaseRevealColor, OutResult.BuildSignature, true);
    UTexture2D* BaseRevealSurface = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::BaseRevealSurface, OutResult.BuildSignature, true);
    UTexture2D* ValidHit = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::ValidHit, OutResult.BuildSignature, true);
    UTexture2D* OuterCoverage = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::OuterCoverage, OutResult.BuildSignature, true);
    UTexture2D* OuterIslandID = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::OuterIslandID, OutResult.BuildSignature, true);
    UTexture2D* HitSource = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::HitSource, OutResult.BuildSignature, true);
    UTexture2D* HitDistance = FDWCTransparencyTempAssetStore::FindCurrentArtifact(
        Layer, EDWCTransparencyTempArtifactKind::HitDistance, OutResult.BuildSignature, true);

    TArray64<uint8> BaseBytes;
    TArray64<uint8> SurfaceBytes;
    TArray64<uint8> ValidBytes;
    TArray64<uint8> CoverageBytes;
    TArray64<uint8> IslandBytes;
    TArray64<uint8> SourceBytes;
    TArray64<uint8> DistanceBytes;
    if (!ReadArtifact(BaseReveal, TSF_BGRA8, OutResult.Resolution, BaseBytes, OutError) ||
        !ReadArtifact(BaseRevealSurface, TSF_BGRA8, OutResult.Resolution, SurfaceBytes, OutError) ||
        !ReadArtifact(ValidHit, TSF_G8, OutResult.Resolution, ValidBytes, OutError) ||
        !ReadArtifact(OuterCoverage, TSF_G8, OutResult.Resolution, CoverageBytes, OutError) ||
        !ReadArtifact(OuterIslandID, TSF_G16, OutResult.Resolution, IslandBytes, OutError) ||
        !ReadArtifact(HitSource, TSF_G16, OutResult.Resolution, SourceBytes, OutError) ||
        !ReadArtifact(HitDistance, TSF_R16F, OutResult.Resolution, DistanceBytes, OutError))
    {
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }

    const int32 PixelCount = OutResult.Resolution.X * OutResult.Resolution.Y;
    if (BaseBytes.Num() != static_cast<int64>(PixelCount) * sizeof(FColor) ||
        SurfaceBytes.Num() != static_cast<int64>(PixelCount) * sizeof(FColor) ||
        ValidBytes.Num() != PixelCount ||
        CoverageBytes.Num() != PixelCount ||
        IslandBytes.Num() != static_cast<int64>(PixelCount) * sizeof(uint16) ||
        SourceBytes.Num() != static_cast<int64>(PixelCount) * sizeof(uint16) ||
        DistanceBytes.Num() != static_cast<int64>(PixelCount) * sizeof(FFloat16))
    {
        OutError = TEXT("A canonical Stage 2 Temp artifact has an invalid payload size.");
        OutResult = FDWCTransparencySourcePayload();
        return false;
    }

    OutResult.InnerColorBuffer.SetNumUninitialized(PixelCount);
    OutResult.RevealSurfaceBuffer.SetNumUninitialized(PixelCount);
    OutResult.AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    OutResult.OuterCoverageBuffer.SetNumUninitialized(PixelCount);
    OutResult.OuterIslandIDBuffer.SetNumUninitialized(PixelCount);
    OutResult.ValidHitBuffer.Init(false, PixelCount);
    OutResult.HitDistanceBuffer.SetNumUninitialized(PixelCount);
    OutResult.SourcePriorityBuffer.SetNumUninitialized(PixelCount);
    FMemory::Memcpy(
        OutResult.InnerColorBuffer.GetData(),
        BaseBytes.GetData(),
        static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
    FMemory::Memcpy(
        OutResult.RevealSurfaceBuffer.GetData(),
        SurfaceBytes.GetData(),
        static_cast<SIZE_T>(PixelCount) * sizeof(FColor));
    FMemory::Memcpy(
        OutResult.OuterCoverageBuffer.GetData(),
        CoverageBytes.GetData(),
        static_cast<SIZE_T>(PixelCount));
    FMemory::Memcpy(
        OutResult.OuterIslandIDBuffer.GetData(),
        IslandBytes.GetData(),
        static_cast<SIZE_T>(PixelCount) * sizeof(uint16));

    const uint16* EncodedSourcePriorities = reinterpret_cast<const uint16*>(SourceBytes.GetData());
    const FFloat16* EncodedHitDistances = reinterpret_cast<const FFloat16*>(DistanceBytes.GetData());

    for (int32 Index = 0; Index < PixelCount; ++Index)
    {
        const FColor Packed = OutResult.InnerColorBuffer[Index];
        OutResult.AutoAlphaBuffer[Index] = Packed.A;
        OutResult.InnerColorBuffer[Index].A = 255;
        const bool bValidHit = ValidBytes[Index] != 0;
        OutResult.ValidHitBuffer[Index] = bValidHit;
        OutResult.ValidHitCount += bValidHit ? 1 : 0;
        const bool bCovered = OutResult.OuterCoverageBuffer[Index] != 0;
        OutResult.OuterSampleCount += bCovered ? 1 : 0;
        OutResult.NoHitCount += bCovered && !bValidHit ? 1 : 0;

        const uint16 EncodedPriority = EncodedSourcePriorities[Index];
        OutResult.SourcePriorityBuffer[Index] = EncodedPriority > 0
            ? static_cast<int16>(FMath::Min<int32>(static_cast<int32>(EncodedPriority) - 1, MAX_int16))
            : INDEX_NONE;
        OutResult.HitDistanceBuffer[Index] = static_cast<float>(EncodedHitDistances[Index]);
    }
    return true;
}
