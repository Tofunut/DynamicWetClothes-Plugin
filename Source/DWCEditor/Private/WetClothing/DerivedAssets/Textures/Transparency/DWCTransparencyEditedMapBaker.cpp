//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAlphaSnapshotMaterializer.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"

namespace
{
    TSharedRef<const FDWCTransparencySourcePayload> ResolveStage4RevealCheckpoint(
        const FWetClothingTransparencyLayerData& Layer,
        const TSharedRef<const FDWCTransparencySourcePayload>& CanonicalSource)
    {
        TArray<FColor> CorrectedPixels;
        FString RestoreError;
        const EDWCTransparencyCorrectedRevealRestoreResult RestoreResult =
            FDWCTransparencyTempAssetStore::RestoreCurrentCorrectedReveal(
                Layer,
                *CanonicalSource,
                CorrectedPixels,
                RestoreError);
        if (RestoreResult != EDWCTransparencyCorrectedRevealRestoreResult::Restored)
        {
            if (RestoreResult == EDWCTransparencyCorrectedRevealRestoreResult::Invalid)
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DWC transparency: ignoring invalid Corrected Reveal Color for slot %d: %s"),
                    Layer.TargetSurface.OuterMaterialSlotIndex,
                    *RestoreError);
            }
            return CanonicalSource;
        }

        const int32 PixelCount = CanonicalSource->Resolution.X * CanonicalSource->Resolution.Y;
        if (CorrectedPixels.Num() != PixelCount)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC transparency: Corrected Reveal Color pixel count did not match slot %d."),
                Layer.TargetSurface.OuterMaterialSlotIndex);
            return CanonicalSource;
        }

        TSharedRef<FDWCTransparencySourcePayload> ResolvedSource =
            MakeShared<FDWCTransparencySourcePayload>(*CanonicalSource);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            ResolvedSource->InnerColorBuffer[PixelIndex] = CorrectedPixels[PixelIndex];
            ResolvedSource->InnerColorBuffer[PixelIndex].A = 255;
            ResolvedSource->AutoAlphaBuffer[PixelIndex] = CorrectedPixels[PixelIndex].A;
        }
        ResolvedSource->bUsesCorrectedRevealCheckpoint = true;
        return ResolvedSource;
    }

    FString BuildTransparencyGeneratedTextureAssetBaseName(const UWetClothingAsset& Asset)
    {
        FString AssetToken = FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName());
        AssetToken.ReplaceInline(TEXT("_Transparency_"), TEXT("_"));
        AssetToken.RemoveFromEnd(TEXT("_Transparency"));
        return FString::Printf(TEXT("T_%s"), *AssetToken);
    }

    FString BuildTransparencyMapAssetName(const UWetClothingAsset& Asset, const FDWCTransparencySourcePayload& SourcePayload)
    {
        const FString BaseName = BuildTransparencyGeneratedTextureAssetBaseName(Asset);
        return FString::Printf(
            TEXT("%s_Slot%d_TransparencyMap"),
            *BaseName,
            SourcePayload.MaterialSlotIndex);
    }

    FString BuildRevealSurfaceMapAssetName(
        const UWetClothingAsset& Asset,
        const FDWCTransparencySourcePayload& SourcePayload)
    {
        const FString BaseName = BuildTransparencyGeneratedTextureAssetBaseName(Asset);
        return FString::Printf(
            TEXT("%s_Slot%d_RevealSurfaceMap"),
            *BaseName,
            SourcePayload.MaterialSlotIndex);
    }

    UTexture2D* CreateOrUpdateFinalTransparencyTextureAsset(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& SourcePayload,
        const TArray<FColor>& Pixels,
        const bool bRevealSurface,
        FString& OutErrorMessage)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(Asset, TEXT("Textures/Transparency"));
        const FString AssetName = bRevealSurface
            ? BuildRevealSurfaceMapAssetName(Asset, SourcePayload)
            : BuildTransparencyMapAssetName(Asset, SourcePayload);
        const TCHAR* OutputLabel = bRevealSurface ? TEXT("Reveal Surface") : TEXT("Transparency");
        if (PackagePath.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not resolve the generated Transparency Textures package path.");
            return nullptr;
        }

        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;
        UTexture2D* Texture = nullptr;
        bool bFromSerializedReference = false;

        const FWetClothingBakedTransparencyMap* ExistingMap = Layer.BakedMaps.FindByPredicate(
            [&SourcePayload](const FWetClothingBakedTransparencyMap& Candidate)
            {
                return Candidate.MaterialSlotIndex == SourcePayload.MaterialSlotIndex;
            });
        if (ExistingMap != nullptr &&
            IsValid(bRevealSurface ? ExistingMap->RevealSurfaceMap.Get() : ExistingMap->TransparencyMap.Get()) &&
            (bRevealSurface ? ExistingMap->RevealSurfaceMap->GetPathName() : ExistingMap->TransparencyMap->GetPathName()) == ObjectPath)
        {
            Texture = bRevealSurface ? ExistingMap->RevealSurfaceMap.Get() : ExistingMap->TransparencyMap.Get();
            bFromSerializedReference = true;
        }

        if (Texture == nullptr)
        {
            UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath);
            if (ExistingObject != nullptr && !ExistingObject->IsA<UTexture2D>())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated %s output path '%s' is occupied by an incompatible asset of type '%s'. Move or rename that asset and try again."),
                    OutputLabel,
                    *ObjectPath,
                    *GetNameSafe(ExistingObject->GetClass()));
                return nullptr;
            }
            Texture = Cast<UTexture2D>(ExistingObject);
        }

        UPackage* Package = nullptr;
        bool bCreatedAsset = false;
        if (Texture != nullptr)
        {
            FGuid ExistingOwnerGuid;
            const bool bHasOwnerGuid = Asset.TryGetGeneratedAssetOwnerGuid(Texture, ExistingOwnerGuid);
            if (bHasOwnerGuid && ExistingOwnerGuid != Asset.GetAssetGuid())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The %s texture '%s' belongs to another Wet Clothing Asset (%s). It will not be overwritten."),
                    OutputLabel,
                    *GetPathNameSafe(Texture),
                    *ExistingOwnerGuid.ToString(EGuidFormats::DigitsWithHyphens));
                return nullptr;
            }
            if (!bHasOwnerGuid && !bFromSerializedReference)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated %s output path '%s' contains an unowned texture. It will not be overwritten automatically. Move or rename the existing texture, or restore the original WCA reference."),
                    OutputLabel,
                    *ObjectPath);
                return nullptr;
            }
            if (!Asset.TagGeneratedAsset(Texture))
            {
                OutErrorMessage = TEXT("Could not associate the existing transparency texture with this Wet Clothing Asset.");
                return nullptr;
            }

            Package = Texture->GetOutermost();
            Texture->Modify();
        }
        else
        {
            Package = CreatePackage(*PackageName);
            if (Package == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Could not create package '%s'."), *PackageName);
                return nullptr;
            }

            Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
            if (Texture == nullptr)
            {
                OutErrorMessage = FString::Printf(TEXT("Could not create transparency map '%s'."), *ObjectPath);
                return nullptr;
            }

            if (!Asset.TagGeneratedAsset(Texture))
            {
                OutErrorMessage = TEXT("Could not associate the new transparency texture with this Wet Clothing Asset.");
                return nullptr;
            }
            bCreatedAsset = true;
        }

        Texture->Source.Init(
            SourcePayload.Resolution.X,
            SourcePayload.Resolution.Y,
            1,
            1,
            TSF_BGRA8,
            reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->CompressionSettings = TC_Default;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->SRGB = !bRevealSurface;
        Texture->LODGroup = TEXTUREGROUP_Pixels2D;
        const TextureAddress Address = Layer.TargetSurface.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap
            ? TA_Wrap
            : TA_Clamp;
        Texture->AddressX = Address;
        Texture->AddressY = Address;
        Texture->PostEditChange();
        Texture->MarkPackageDirty();
        Package->MarkPackageDirty();

        if (bCreatedAsset)
        {
            FAssetRegistryModule::AssetCreated(Texture);
        }
        return Texture;
    }

    void ApplyCoverageEdgeFeather(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const float FeatherPixels,
        TArray<FColor>& InOutPixels)
    {
        const int32 PixelCount = Resolution.X * Resolution.Y;
        TArray<uint8> EdgeFeatherBuffer;
        if (InOutPixels.Num() != PixelCount ||
            !FDWCTransparencyComposite::BuildCoverageEdgeFeatherBuffer(
                Resolution,
                OuterCoverage,
                FeatherPixels,
                EdgeFeatherBuffer))
        {
            return;
        }
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            InOutPixels[PixelIndex].A = static_cast<uint8>(
                (static_cast<uint32>(InOutPixels[PixelIndex].A) *
                 EdgeFeatherBuffer[PixelIndex] + 127u) / 255u);
        }
    }

    void DilateOutsideCoverage(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const int32 PaddingPixels,
        TArray<FColor>& InOutPixels)
    {
        const int32 SafePadding = FMath::Max(PaddingPixels, 0);
        const int32 PixelCount = Resolution.X * Resolution.Y;
        if (SafePadding <= 0 || OuterCoverage.Num() != PixelCount || InOutPixels.Num() != PixelCount)
        {
            return;
        }

        TArray<uint8> Filled = OuterCoverage;
        TArray<uint8> NextFilled;
        TArray<FColor> NextPixels;
        for (int32 Step = 0; Step < SafePadding; ++Step)
        {
            NextFilled = Filled;
            NextPixels = InOutPixels;
            bool bExpanded = false;
            for (int32 Y = 0; Y < Resolution.Y; ++Y)
            {
                for (int32 X = 0; X < Resolution.X; ++X)
                {
                    const int32 PixelIndex = Y * Resolution.X + X;
                    if (Filled[PixelIndex] != 0)
                    {
                        continue;
                    }

                    int32 SampleCount = 0;
                    int32 SumR = 0;
                    int32 SumG = 0;
                    int32 SumB = 0;
                    int32 SumA = 0;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            if (OffsetX == 0 && OffsetY == 0)
                            {
                                continue;
                            }
                            const int32 NeighborX = X + OffsetX;
                            const int32 NeighborY = Y + OffsetY;
                            if (NeighborX < 0 || NeighborY < 0 ||
                                NeighborX >= Resolution.X || NeighborY >= Resolution.Y)
                            {
                                continue;
                            }
                            const int32 NeighborIndex = NeighborY * Resolution.X + NeighborX;
                            if (Filled[NeighborIndex] == 0)
                            {
                                continue;
                            }
                            const FColor& Neighbor = InOutPixels[NeighborIndex];
                            SumR += Neighbor.R;
                            SumG += Neighbor.G;
                            SumB += Neighbor.B;
                            SumA += Neighbor.A;
                            ++SampleCount;
                        }
                    }
                    if (SampleCount > 0)
                    {
                        NextPixels[PixelIndex] = FColor(
                            SumR / SampleCount,
                            SumG / SampleCount,
                            SumB / SampleCount,
                            SumA / SampleCount);
                        NextFilled[PixelIndex] = 1;
                        bExpanded = true;
                    }
                }
            }
            InOutPixels = MoveTemp(NextPixels);
            Filled = MoveTemp(NextFilled);
            if (!bExpanded)
            {
                break;
            }
        }
    }

    void DilateRevealSurfaceOutsideCoverage(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const int32 PaddingPixels,
        TArray<FColor>& InOutPixels)
    {
        const int32 SafePadding = FMath::Max(PaddingPixels, 0);
        const int32 PixelCount = Resolution.X * Resolution.Y;
        if (SafePadding <= 0 || OuterCoverage.Num() != PixelCount || InOutPixels.Num() != PixelCount)
        {
            return;
        }

        // A surface normal is directional data. Padding copies the nearest
        // payload intact instead of averaging RG normal components or B/A.
        TArray<uint8> Filled = OuterCoverage;
        TArray<uint8> NextFilled;
        TArray<FColor> NextPixels;
        for (int32 Step = 0; Step < SafePadding; ++Step)
        {
            NextFilled = Filled;
            NextPixels = InOutPixels;
            bool bExpanded = false;
            for (int32 Y = 0; Y < Resolution.Y; ++Y)
            {
                for (int32 X = 0; X < Resolution.X; ++X)
                {
                    const int32 PixelIndex = Y * Resolution.X + X;
                    if (Filled[PixelIndex] != 0)
                    {
                        continue;
                    }

                    int32 BestNeighborIndex = INDEX_NONE;
                    uint8 BestCoverage = 0;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            if (OffsetX == 0 && OffsetY == 0)
                            {
                                continue;
                            }
                            const int32 NeighborX = X + OffsetX;
                            const int32 NeighborY = Y + OffsetY;
                            if (NeighborX < 0 || NeighborY < 0 ||
                                NeighborX >= Resolution.X || NeighborY >= Resolution.Y)
                            {
                                continue;
                            }
                            const int32 NeighborIndex = NeighborY * Resolution.X + NeighborX;
                            if (Filled[NeighborIndex] == 0)
                            {
                                continue;
                            }
                            const uint8 NeighborCoverage = InOutPixels[NeighborIndex].A;
                            if (BestNeighborIndex == INDEX_NONE || NeighborCoverage > BestCoverage)
                            {
                                BestNeighborIndex = NeighborIndex;
                                BestCoverage = NeighborCoverage;
                            }
                        }
                    }
                    if (BestNeighborIndex != INDEX_NONE)
                    {
                        NextPixels[PixelIndex] = InOutPixels[BestNeighborIndex];
                        NextFilled[PixelIndex] = 1;
                        bExpanded = true;
                    }
                }
            }
            InOutPixels = MoveTemp(NextPixels);
            Filled = MoveTemp(NextFilled);
            if (!bExpanded)
            {
                break;
            }
        }
    }
}

struct FDWCTransparencyEditedMapBakeSnapshot::FImpl
{
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload;
    FDWCTransparencyFinalWorkingSet WorkingSet;
    FGuid LayerGuid;
    FWetClothingTransparencyTargetSurface TargetSurface;
    FWetClothingTransparencyManualColorSource ManualColorSource;
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService;
    FDWCEditorCacheLease WrinkleCoverageLease;
    FString SuppressionWarning;
    FString SourceWrinkleMaskBuildSignature;
    FString SuppressionSettingsSignature;
    FString FinalBuildSignature;
    FGuid SourceWrinkleMaskBakeGuid;
    float TransparencyStrength = 0.0f;
    float SuppressionStrength = 0.0f;
    float EdgeFeatherPixels = 0.0f;
    int32 PaddingPixels = 0;
    int32 BakedStrokeCount = 0;
    bool bHasWrinkleSuppression = false;
    bool bRequiresRevealSurface = false;
    bool bValid = false;
    uint64 EstimatedPrivateBytes = 0;
    uint64 EstimatedOutputBytes = 0;
    uint64 EstimatedScratchBytes = 0;
    uint64 EstimatedBytes = 0;
};

FDWCTransparencyEditedMapBakeSnapshot::FDWCTransparencyEditedMapBakeSnapshot()
    : Impl(MakeUnique<FImpl>())
{
}

FDWCTransparencyEditedMapBakeSnapshot::~FDWCTransparencyEditedMapBakeSnapshot() = default;
FDWCTransparencyEditedMapBakeSnapshot::FDWCTransparencyEditedMapBakeSnapshot(
    FDWCTransparencyEditedMapBakeSnapshot&&) = default;
FDWCTransparencyEditedMapBakeSnapshot& FDWCTransparencyEditedMapBakeSnapshot::operator=(
    FDWCTransparencyEditedMapBakeSnapshot&&) = default;

bool FDWCTransparencyEditedMapBakeSnapshot::IsValid() const
{
    return Impl.IsValid() && Impl->bValid;
}

int32 FDWCTransparencyEditedMapBakeSnapshot::GetMaterialSlotIndex() const
{
    return Impl.IsValid() && Impl->SourcePayload.IsValid()
        ? Impl->SourcePayload->MaterialSlotIndex
        : INDEX_NONE;
}

FGuid FDWCTransparencyEditedMapBakeSnapshot::GetLayerGuid() const
{
    return Impl.IsValid() ? Impl->LayerGuid : FGuid();
}

uint64 FDWCTransparencyEditedMapBakeSnapshot::GetEstimatedBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedBytes : 0;
}

uint64 FDWCTransparencyEditedMapBakeSnapshot::GetEstimatedPrivateBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedPrivateBytes : 0;
}

uint64 FDWCTransparencyEditedMapBakeSnapshot::GetEstimatedOutputBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedOutputBytes : 0;
}

uint64 FDWCTransparencyEditedMapBakeSnapshot::GetEstimatedScratchBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedScratchBytes : 0;
}

bool FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& SourcePayload,
    FString& OutReason)
{
    OutReason.Reset();
    if (SourcePayload.LayerGuid != Layer.LayerGuid ||
        SourcePayload.MaterialSlotIndex != Layer.TargetSurface.OuterMaterialSlotIndex)
    {
        OutReason = TEXT("The generated transparency map no longer matches the selected layer.");
        return false;
    }

    const int32 PixelCount = SourcePayload.Resolution.X * SourcePayload.Resolution.Y;
    if (SourcePayload.Resolution.X <= 0 || SourcePayload.Resolution.Y <= 0 ||
        SourcePayload.InnerColorBuffer.Num() != PixelCount ||
        SourcePayload.RevealSurfaceBuffer.Num() != PixelCount ||
        SourcePayload.AutoAlphaBuffer.Num() != PixelCount ||
        SourcePayload.OuterCoverageBuffer.Num() != PixelCount ||
        SourcePayload.ValidHitBuffer.Num() != PixelCount)
    {
        OutReason = TEXT("The generated transparency map buffers are invalid.");
        return false;
    }

    return true;
}

bool FDWCTransparencyEditedMapBaker::IsLayerBakeCurrent(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FString* OutReason)
{
    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }

    FDWCTransparencySourcePayload SourcePayload;
    FString SignatureError;
    if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
            WetClothingAsset,
            Layer,
            SourcePayload,
            SignatureError))
    {
        if (OutReason != nullptr)
        {
            *OutReason = SignatureError.IsEmpty()
                ? TEXT("Transparency build signature could not be generated for validation.")
                : SignatureError;
        }
        return false;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = Layer.BakedMaps.FindByPredicate(
        [&SourcePayload](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == SourcePayload.MaterialSlotIndex;
        });
    if (BakedMap == nullptr || !BakedMap->IsRuntimeUsableForLayer(Layer.RequiresRevealSurface()))
    {
        if (OutReason != nullptr)
        {
            *OutReason = Layer.RequiresRevealSurface()
                ? TEXT("Transparency map or its required Reveal Surface payload is missing or not runtime-usable.")
                : TEXT("Transparency map is missing or not runtime-usable.");
        }
        return false;
    }

    const FWetClothingTransparencyData& TransparencyData = WetClothingAsset.Authored.TransparencyData;
    const FDWCWrinkleSuppressionDependencySnapshot SuppressionDependency =
        FDWCWrinkleSuppressionCoverageService::ResolveDependency(
            &WetClothingAsset,
            SourcePayload.MaterialSlotIndex);
    const bool bHasWrinkleSuppression = SuppressionDependency.IsAvailable();
    const FString SourceWrinkleMaskBuildSignature =
        bHasWrinkleSuppression
            ? SuppressionDependency.BuildSignature
            : FString();
    const FString SuppressionSettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            TransparencyData.WrinkleSuppressionStrength,
            TransparencyData.TransparencyPreviewStrength);
    const FString RevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        SourcePayload.BuildSignature,
        Layer);
    const FString RevealSurfaceSignature =
        FDWCTransparencySignatureService::BuildRevealSurfaceSignature(
            SourcePayload.BuildSignature);
    const FString ExpectedSignature = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealSignature,
        Layer,
        SourceWrinkleMaskBuildSignature,
        SuppressionSettingsSignature,
        TransparencyData.TransparencyPaddingPixels,
        TransparencyData.TransparencyEdgeFeatherPixels);

    TArray<FString> Mismatches;
    if (BakedMap->Resolution != SourcePayload.Resolution.X)
    {
        Mismatches.Add(TEXT("the output resolution changed"));
    }
    if (BakedMap->PaddingPixels != TransparencyData.TransparencyPaddingPixels)
    {
        Mismatches.Add(TEXT("the padding setting changed"));
    }
    if (BakedMap->SourceWrinkleMaskBuildSignature != SourceWrinkleMaskBuildSignature ||
        BakedMap->SourceWrinkleMaskBakeGuid !=
            (bHasWrinkleSuppression ? SuppressionDependency.BakeGuid : FGuid()) ||
        BakedMap->bWrinkleSuppressionBakedIntoAlpha != bHasWrinkleSuppression)
    {
        Mismatches.Add(TEXT("the baked wrinkle-mask dependency changed"));
    }
    if (BakedMap->WrinkleSuppressionSettingsSignature != SuppressionSettingsSignature)
    {
        Mismatches.Add(TEXT("the wrinkle-suppression or transparency settings changed"));
    }
    if (BakedMap->BuildSignature != ExpectedSignature)
    {
        Mismatches.Add(TEXT("the authored transparency data changed"));
    }
    if (Layer.RequiresRevealSurface() &&
        BakedMap->RevealSurfaceBuildSignature != RevealSurfaceSignature)
    {
        Mismatches.Add(TEXT("the source Reveal Surface data changed or was not baked"));
    }
    if (Layer.RequiresRevealSurface() && !BakedMap->HasCompleteRevealSurfacePayload())
    {
        Mismatches.Add(TEXT("the required Reveal Surface runtime artifact is missing"));
    }
    if (BakedMap->bContainsRevealNormalRG != BakedMap->bContainsInnerMetallicB ||
        BakedMap->bContainsRevealNormalRG != BakedMap->bContainsRevealSurfaceCoverageAlpha ||
        (BakedMap->bContainsRevealNormalRG && BakedMap->RevealSurfaceMap == nullptr) ||
        (!BakedMap->bContainsRevealNormalRG && BakedMap->RevealSurfaceMap != nullptr))
    {
        Mismatches.Add(TEXT("the Reveal Surface runtime artifact is inconsistent"));
    }
    if (OutReason != nullptr && !Mismatches.IsEmpty())
    {
        *OutReason = FString::Printf(
            TEXT("Transparency map is out of date because %s."),
            *FString::Join(Mismatches, TEXT(", ")));
    }
    return Mismatches.IsEmpty();
}

bool FDWCTransparencyEditedMapBaker::Bake(
    UWetClothingAsset& WetClothingAsset,
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& SourcePayload,
    FDWCTransparencyEditedMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    FDWCTransparencyEditedMapBakeSnapshot Snapshot;
    if (!BuildSnapshot(WetClothingAsset, Layer, SourcePayload, Snapshot, OutErrorMessage))
    {
        return false;
    }
    FDWCTransparencyEditedMapComputedResult ComputedResult = ComputeSnapshot(Snapshot);
    return CommitComputedResult(
        WetClothingAsset,
        Snapshot,
        MoveTemp(ComputedResult),
        OutResult,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencySourcePayload& SourcePayload,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        MakeShared<FDWCTransparencySourcePayload>(SourcePayload),
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencySourcePayload> AutoResultRef,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot;
    AlphaSnapshot.Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    AlphaSnapshot.Resolution = AutoResultRef->Resolution;
    AlphaSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoResultRef->BaselineStrokeCount, 0, Layer.EditableStrokes.Num());
    AlphaSnapshot.AuthoredStrokeCount = Layer.EditableStrokes.Num();
    AlphaSnapshot.FallbackStrokes = Layer.EditableStrokes;
    for (int32 StrokeIndex = AlphaSnapshot.BaselineStrokeCount;
         StrokeIndex < Layer.EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Layer.EditableStrokes[StrokeIndex];
        AlphaSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.Samples.Num() : 0;
    }
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        AutoResultRef,
        MoveTemp(AlphaSnapshot),
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencySourcePayload> AutoResultRef,
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot;
    AlphaSnapshot.Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    AlphaSnapshot.Resolution = AutoResultRef->Resolution;
    AlphaSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoResultRef->BaselineStrokeCount, 0, Layer.EditableStrokes.Num());
    AlphaSnapshot.AuthoredStrokeCount = Layer.EditableStrokes.Num();
    AlphaSnapshot.FallbackStrokes = Layer.EditableStrokes;
    for (int32 StrokeIndex = AlphaSnapshot.BaselineStrokeCount;
         StrokeIndex < Layer.EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Layer.EditableStrokes[StrokeIndex];
        AlphaSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.Samples.Num() : 0;
    }
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        MoveTemp(AutoResultRef),
        MoveTemp(AlphaSnapshot),
        MoveTemp(CoverageService),
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencySourcePayload> AutoResultRef,
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    const TSharedRef<FDWCEditorCacheStore> CacheStore = MakeShared<FDWCEditorCacheStore>();
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        MoveTemp(AutoResultRef),
        MoveTemp(AlphaSnapshot),
        MakeShared<FDWCWrinkleSuppressionCoverageService>(CacheStore),
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencySourcePayload> AutoResultRef,
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    const FDWCTransparencyFinalSettingsSnapshot FinalSettings =
        FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(WetClothingAsset.Authored.TransparencyData);
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        MoveTemp(AutoResultRef),
        MoveTemp(AlphaSnapshot),
        MoveTemp(CoverageService),
        FinalSettings,
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencySourcePayload> AutoResultRef,
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
    const FDWCTransparencyFinalSettingsSnapshot& FinalSettings,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutSnapshot = FDWCTransparencyEditedMapBakeSnapshot();
    if (!FinalSettings.IsValid(&OutErrorMessage))
    {
        return false;
    }
    const FDWCTransparencySourcePayload& CanonicalSourcePayload = *AutoResultRef;
    if (CanonicalSourcePayload.bIsFinalBakedBaseline)
    {
        OutErrorMessage =
            TEXT("A final baked Transparency Map cannot be used as a bake source. Rebuild the canonical working map from the stored layer inputs and strokes first.");
        return false;
    }
    FString CompatibilityReason;
    if (!IsAutoResultCompatible(Layer, CanonicalSourcePayload, CompatibilityReason))
    {
        OutErrorMessage = CompatibilityReason;
        return false;
    }

    FDWCTransparencySourcePayload CurrentAutoSignature;
    FString SignatureError;
    if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
            WetClothingAsset,
            Layer,
            CurrentAutoSignature,
            SignatureError))
    {
        OutErrorMessage = SignatureError.IsEmpty()
            ? TEXT("The canonical transparency working-map signature could not be generated.")
            : MoveTemp(SignatureError);
        return false;
    }
    if (CanonicalSourcePayload.BuildSignature != CurrentAutoSignature.BuildSignature)
    {
        OutErrorMessage =
            TEXT("The transparency working map is out of date. Regenerate the preview map before baking.");
        return false;
    }

    // Stage 4 prefers the committed Stage 3 checkpoint. The canonical source
    // remains the fallback and retains the surface/hit auxiliary buffers.
    TSharedRef<const FDWCTransparencySourcePayload> Stage4Source =
        ResolveStage4RevealCheckpoint(Layer, AutoResultRef);
    const bool bOwnsStage4SourcePayload = &Stage4Source.Get() != &AutoResultRef.Get();
    const FDWCTransparencySourcePayload& SourcePayload = *Stage4Source;

    FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    Snapshot.SourcePayload = MoveTemp(Stage4Source);
    Snapshot.LayerGuid = Layer.LayerGuid;
    Snapshot.TargetSurface = Layer.TargetSurface;
    Snapshot.bRequiresRevealSurface = Layer.RequiresRevealSurface();
    Snapshot.ManualColorSource = Layer.ManualColorSource;
    Snapshot.RevealColorPaintStrokes = Layer.RevealColorPaintStrokes;
    Snapshot.BakedStrokeCount = AlphaSnapshot.AuthoredStrokeCount;
    Snapshot.CoverageService = MoveTemp(CoverageService);
    if (!Snapshot.CoverageService.IsValid())
    {
        OutErrorMessage = TEXT("The shared wrinkle coverage service is unavailable.");
        return false;
    }

    FDWCWrinkleSuppressionDependencySnapshot WrinkleDependency =
        Snapshot.CoverageService->ResolveDependency(
            &WetClothingAsset,
            SourcePayload.MaterialSlotIndex);
    if (WrinkleDependency.IsAvailable())
    {
        Snapshot.bHasWrinkleSuppression = Snapshot.CoverageService->AcquireCoverage(
            WetClothingAsset,
            WrinkleDependency,
            Snapshot.WrinkleCoverageLease,
            Snapshot.SuppressionWarning);
    }
    if (!Snapshot.bHasWrinkleSuppression)
    {
        if (Snapshot.SuppressionWarning.IsEmpty())
        {
            Snapshot.SuppressionWarning = WrinkleDependency.Detail.IsEmpty()
                ? TEXT("No baked wrinkle coverage mask matched this material slot. Transparency was baked without wrinkle suppression.")
                : WrinkleDependency.Detail;
        }
        WrinkleDependency = FDWCWrinkleSuppressionDependencySnapshot();
    }
    if (!FDWCTransparencyFinalWorkingSetBuilder::Build(
            WetClothingAsset,
            Layer,
            Snapshot.SourcePayload,
            FinalSettings,
            MoveTemp(AlphaSnapshot),
            MoveTemp(WrinkleDependency),
            static_cast<uint64>(GetTypeHash(Snapshot.SourcePayload->BuildSignature)),
            Snapshot.WorkingSet,
            OutErrorMessage))
    {
        return false;
    }
    Snapshot.SourceWrinkleMaskBakeGuid =
        Snapshot.bHasWrinkleSuppression
            ? Snapshot.WorkingSet.WrinkleDependency.BakeGuid
            : FGuid();
    Snapshot.SourceWrinkleMaskBuildSignature =
        Snapshot.bHasWrinkleSuppression
            ? Snapshot.WorkingSet.WrinkleDependency.BuildSignature
            : FString();
    Snapshot.SuppressionSettingsSignature = Snapshot.WorkingSet.SuppressionSettingsSignature;
    Snapshot.TransparencyStrength = FMath::Max(FinalSettings.TransparencyStrength, 0.0f);
    Snapshot.SuppressionStrength = FMath::Max(FinalSettings.WrinkleSuppressionStrength, 0.0f);
    Snapshot.EdgeFeatherPixels = FinalSettings.EdgeFeatherPixels;
    Snapshot.PaddingPixels = FinalSettings.PaddingPixels;
    Snapshot.FinalBuildSignature = Snapshot.WorkingSet.FinalSignature;
    const uint64 PixelCount = static_cast<uint64>(SourcePayload.Resolution.X) *
        static_cast<uint64>(SourcePayload.Resolution.Y);
    // The canonical source is already accounted as resident shared memory by
    // the scheduler. A corrected Stage 3 checkpoint creates one private copy;
    // only that copy belongs to the worker snapshot.
    Snapshot.EstimatedPrivateBytes =
        (bOwnsStage4SourcePayload ? SourcePayload.GetAllocatedBytes() : 0ull) +
        Snapshot.WorkingSet.OwnedBytes;
    Snapshot.EstimatedOutputBytes = PixelCount * sizeof(FColor) *
        (SourcePayload.bUsesCorrectedRevealCheckpoint ? 2ull : 3ull);
    // Feathering and the two directional dilation passes use bounded scratch
    // arrays; count their peak rather than reserving them as persistent data.
    Snapshot.EstimatedScratchBytes = PixelCount *
        (sizeof(FColor) * 2ull + sizeof(uint8) * 2ull);
    Snapshot.EstimatedBytes = Snapshot.EstimatedPrivateBytes +
        Snapshot.EstimatedOutputBytes + Snapshot.EstimatedScratchBytes;
    Snapshot.bValid = true;
    OutErrorMessage.Reset();
    return true;
}

FDWCTransparencyEditedMapComputedResult FDWCTransparencyEditedMapBaker::ComputeSnapshot(
    const FDWCTransparencyEditedMapBakeSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FDWCTransparencyEditedMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The transparency bake snapshot is invalid.");
        return Result;
    }
    const FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    const FDWCTransparencySourcePayload& SourcePayload = *Snapshot.SourcePayload;
    const int32 PixelCount = SourcePayload.Resolution.X * SourcePayload.Resolution.Y;

    FWetClothingTransparencyLayerData WorkerLayer;
    WorkerLayer.LayerGuid = Snapshot.LayerGuid;
    WorkerLayer.TargetSurface = Snapshot.TargetSurface;
    WorkerLayer.ManualColorSource = Snapshot.ManualColorSource;
    WorkerLayer.RevealColorPaintStrokes = Snapshot.RevealColorPaintStrokes;
    FDWCTransparencyAlphaWorkingSnapshot MaterializedAlphaSnapshot;
    const FDWCTransparencyAlphaWorkingSnapshot* SparseAlphaSnapshot = &Snapshot.WorkingSet.Alpha;
    FString AlphaError;
    if (Snapshot.WorkingSet.Alpha.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay &&
        !FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
            SourcePayload,
            Snapshot.WorkingSet.Alpha,
            MaterializedAlphaSnapshot,
            AlphaError,
            CancellationToken))
    {
        Result.Error = MoveTemp(AlphaError);
        return Result;
    }
    if (Snapshot.WorkingSet.Alpha.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay)
    {
        SparseAlphaSnapshot = &MaterializedAlphaSnapshot;
    }
    FDWCTransparencyAlphaSnapshotView AlphaView;
    if (!AlphaView.Initialize(*SparseAlphaSnapshot, &Result.Error))
    {
        return Result;
    }
    FDWCTransparencyPixelComposeContext AlphaContext;
    AlphaContext.SourcePayload = &SourcePayload;
    AlphaContext.AlphaSnapshotView = &AlphaView;
    const FDWCWrinkleCoverageCacheValue* WrinkleCoverage =
        Snapshot.WrinkleCoverageLease.GetAs<FDWCWrinkleCoverageCacheValue>();

    Result.bAppliedWrinkleSuppression = Snapshot.bHasWrinkleSuppression && WrinkleCoverage != nullptr;
    Result.WarningMessage = Snapshot.SuppressionWarning;
    Result.FinalPixels = SourcePayload.InnerColorBuffer;
    Result.FinalRevealSurfacePixels = SourcePayload.RevealSurfaceBuffer;
    // A raycast source owns the packed Reveal Surface contract even when the
    // current layer has no valid hits. In that case the alpha stays zero, but
    // runtime, validation, and rebake currentness remain unambiguous.
    Result.bContainsRevealSurface = Snapshot.bRequiresRevealSurface;
    if (!SourcePayload.bUsesCorrectedRevealCheckpoint)
    {
        // Missing or stale Stage 3 checkpoints are reconstructed from the
        // canonical Stage 2 result and serialized Reveal Color strokes.
        FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
            SourcePayload,
            WorkerLayer.RevealColorPaintStrokes,
            SourcePayload.MaterialSlotIndex,
            WorkerLayer.ManualColorSource.BaseRevealColor,
            Result.FinalPixels);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            Result.FinalPixels[PixelIndex].A = SourcePayload.AutoAlphaBuffer[PixelIndex];
        }
        Result.RebuiltCorrectedRevealPixels = Result.FinalPixels;
        Result.bRebuiltCorrectedRevealCheckpoint = true;
    }
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (CancellationToken != nullptr && (PixelIndex & 4095) == 0 && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The transparency bake was canceled.");
            return Result;
        }
        const bool bHasValidInnerColor = SourcePayload.ValidHitBuffer.IsValidIndex(PixelIndex) &&
            SourcePayload.ValidHitBuffer[PixelIndex] != 0;
        const float EditedAlpha = FDWCTransparencyComposite::ResolveEditedAlpha(
            AlphaContext,
            PixelIndex);
        if (!bHasValidInnerColor)
        {
            Result.IgnoredNoHitOverridePixelCount += EditedAlpha > KINDA_SMALL_NUMBER ? 1 : 0;
            Result.FinalPixels[PixelIndex].A = 0;
            continue;
        }
        uint8 WrinkleSuppression = 0;
        if (Result.bAppliedWrinkleSuppression)
        {
            const int32 PixelX = PixelIndex % SourcePayload.Resolution.X;
            const int32 PixelY = PixelIndex / SourcePayload.Resolution.X;
            const FVector2f UV(
                (static_cast<float>(PixelX) + 0.5f) / SourcePayload.Resolution.X,
                (static_cast<float>(PixelY) + 0.5f) / SourcePayload.Resolution.Y);
            const float Coverage = WrinkleCoverage->SampleCoverage(UV);
            const float Suppression =
                FDWCWrinkleSuppressionCoverageService::EvaluateSuppression(
                    Coverage,
                    Snapshot.WorkingSet.Settings.WrinkleMaskThreshold,
                    Snapshot.WorkingSet.Settings.WrinkleMaskSoftness);
            WrinkleSuppression = static_cast<uint8>(
                FMath::RoundToInt(FMath::Clamp(Suppression, 0.0f, 1.0f) * 255.0f));
        }
        Result.FinalPixels[PixelIndex].A = FDWCTransparencyComposite::ResolveFinalAlpha8(
            EditedAlpha,
            Snapshot.TransparencyStrength,
            WrinkleSuppression,
            Snapshot.SuppressionStrength);
    }

    ApplyCoverageEdgeFeather(
        SourcePayload.Resolution,
        SourcePayload.OuterCoverageBuffer,
        Snapshot.EdgeFeatherPixels,
        Result.FinalPixels);
    if (CancellationToken != nullptr && CancellationToken->IsCanceled())
    {
        Result.bCanceled = true;
        Result.Error = TEXT("The transparency bake was canceled.");
        return Result;
    }
    DilateOutsideCoverage(
        SourcePayload.Resolution,
        SourcePayload.OuterCoverageBuffer,
        Snapshot.PaddingPixels,
        Result.FinalPixels);
    if (Result.bContainsRevealSurface)
    {
        DilateRevealSurfaceOutsideCoverage(
            SourcePayload.Resolution,
            SourcePayload.OuterCoverageBuffer,
            Snapshot.PaddingPixels,
            Result.FinalRevealSurfacePixels);
    }

    Result.AppliedStrokeCount = FMath::Max(
        Snapshot.WorkingSet.Alpha.AuthoredStrokeCount -
        Snapshot.WorkingSet.Alpha.BaselineStrokeCount,
        0);
    Result.AppliedSampleCount = Snapshot.WorkingSet.Alpha.AppliedSampleCount;
    Result.ResultBytes = Result.FinalPixels.GetAllocatedSize() +
        Result.FinalRevealSurfacePixels.GetAllocatedSize() +
        Result.RebuiltCorrectedRevealPixels.GetAllocatedSize();
    Result.bSucceeded = true;
    return Result;
}

bool FDWCTransparencyEditedMapBaker::CommitComputedResult(
    UWetClothingAsset& WetClothingAsset,
    const FDWCTransparencyEditedMapBakeSnapshot& SnapshotHandle,
    FDWCTransparencyEditedMapComputedResult&& ComputedResult,
    FDWCTransparencyEditedMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutResult = FDWCTransparencyEditedMapBakeResult();
    OutErrorMessage.Reset();
    if (!SnapshotHandle.IsValid() || !ComputedResult.bSucceeded)
    {
        OutErrorMessage = ComputedResult.Error.IsEmpty()
            ? TEXT("The transparency bake calculation failed.")
            : MoveTemp(ComputedResult.Error);
        return false;
    }
    const FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    FWetClothingTransparencyLayerData* Layer =
        WetClothingAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
            [&Snapshot](const FWetClothingTransparencyLayerData& Candidate)
            {
                return Candidate.LayerGuid == Snapshot.LayerGuid;
            });
    if (Layer == nullptr ||
        !Snapshot.SourcePayload.IsValid() ||
        Layer->TargetSurface.OuterMaterialSlotIndex != Snapshot.SourcePayload->MaterialSlotIndex)
    {
        OutErrorMessage = TEXT("The transparency target changed before the bake result could be committed.");
        return false;
    }
    const int32 ExpectedPixelCount = Snapshot.SourcePayload->Resolution.X * Snapshot.SourcePayload->Resolution.Y;
    if (ComputedResult.FinalPixels.Num() != ExpectedPixelCount ||
        ComputedResult.FinalRevealSurfacePixels.Num() != ExpectedPixelCount)
    {
        OutErrorMessage = TEXT("The transparency bake result has an unexpected pixel count.");
        return false;
    }

    FDWCTransparencySourcePayload CurrentSourceSignature;
    FString CurrentSignatureError;
    if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
            WetClothingAsset,
            *Layer,
            CurrentSourceSignature,
            CurrentSignatureError))
    {
        OutErrorMessage = CurrentSignatureError.IsEmpty()
            ? TEXT("The transparency source could not be validated before commit.")
            : MoveTemp(CurrentSignatureError);
        return false;
    }

    const FWetClothingTransparencyData& CurrentData = WetClothingAsset.Authored.TransparencyData;
    const FDWCTransparencyFinalSettingsSnapshot CurrentSettings =
        FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(CurrentData);
    const FString CurrentRevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        CurrentSourceSignature.BuildSignature,
        *Layer);
    const FString CurrentRevealSurfaceSignature =
        FDWCTransparencySignatureService::BuildRevealSurfaceSignature(
            CurrentSourceSignature.BuildSignature);
    const FString CurrentAlphaSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(*Layer);
    const FString CurrentSuppressionSettingsSignature =
        FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
            CurrentSettings.WrinkleMaskThreshold,
            CurrentSettings.WrinkleMaskSoftness,
            CurrentSettings.WrinkleSuppressionStrength,
            CurrentSettings.TransparencyStrength);
    if (CurrentSourceSignature.BuildSignature != Snapshot.WorkingSet.SourceSignature ||
        CurrentRevealSignature != Snapshot.WorkingSet.RevealSignature ||
        CurrentRevealSurfaceSignature != Snapshot.WorkingSet.RevealSurfaceSignature ||
        CurrentAlphaSignature != Snapshot.WorkingSet.AlphaAuthoringSignature ||
        CurrentSuppressionSettingsSignature != Snapshot.WorkingSet.SuppressionSettingsSignature ||
        CurrentSettings.PaddingPixels != Snapshot.WorkingSet.Settings.PaddingPixels ||
        !FMath::IsNearlyEqual(
            CurrentSettings.EdgeFeatherPixels,
            Snapshot.WorkingSet.Settings.EdgeFeatherPixels))
    {
        OutErrorMessage =
            TEXT("The transparency authoring data changed while the bake was running. Run the bake again to commit the latest state.");
        return false;
    }
    if (Snapshot.WorkingSet.WrinkleDependency.IsAvailable())
    {
        const FDWCWrinkleSuppressionDependencySnapshot CurrentWrinkleDependency =
            Snapshot.CoverageService->ResolveDependency(
                &WetClothingAsset,
                Layer->TargetSurface.OuterMaterialSlotIndex);
        if (!CurrentWrinkleDependency.IsAvailable() ||
            CurrentWrinkleDependency.BakeGuid != Snapshot.WorkingSet.WrinkleDependency.BakeGuid ||
            CurrentWrinkleDependency.BuildSignature !=
                Snapshot.WorkingSet.WrinkleDependency.BuildSignature ||
            CurrentWrinkleDependency.TextureSourceId !=
                Snapshot.WorkingSet.WrinkleDependency.TextureSourceId)
        {
            OutErrorMessage =
                TEXT("The wrinkle suppression dependency changed while the transparency bake was running. Run the bake again.");
            return false;
        }
    }

    if (ComputedResult.bRebuiltCorrectedRevealCheckpoint)
    {
        FString CheckpointError;
        if (!FDWCTransparencyTempAssetStore::CommitRevealArtifact(
                WetClothingAsset,
                *Layer,
                ComputedResult.RebuiltCorrectedRevealPixels,
                Snapshot.SourcePayload->Resolution,
                Snapshot.WorkingSet.SourceSignature,
                Snapshot.WorkingSet.RevealSignature,
                CheckpointError))
        {
            const FString CacheWarning = FString::Printf(
                TEXT("Corrected Reveal Color checkpoint could not be restored: %s"),
                *CheckpointError);
            ComputedResult.WarningMessage = ComputedResult.WarningMessage.IsEmpty()
                ? CacheWarning
                : ComputedResult.WarningMessage + TEXT("\n") + CacheWarning;
        }
    }

    UTexture2D* Texture = CreateOrUpdateFinalTransparencyTextureAsset(
        WetClothingAsset,
        *Layer,
        *Snapshot.SourcePayload,
        ComputedResult.FinalPixels,
        false,
        OutErrorMessage);
    if (Texture == nullptr)
    {
        return false;
    }

    UTexture2D* RevealSurfaceTexture = nullptr;
    if (ComputedResult.bContainsRevealSurface)
    {
        RevealSurfaceTexture = CreateOrUpdateFinalTransparencyTextureAsset(
            WetClothingAsset,
            *Layer,
            *Snapshot.SourcePayload,
            ComputedResult.FinalRevealSurfacePixels,
            true,
            OutErrorMessage);
        if (RevealSurfaceTexture == nullptr)
        {
            return false;
        }
    }

    WetClothingAsset.Modify();
    FWetClothingBakedTransparencyMap* BakedMap = Layer->BakedMaps.FindByPredicate(
        [&Snapshot](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Snapshot.SourcePayload->MaterialSlotIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &Layer->BakedMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = Snapshot.SourcePayload->MaterialSlotIndex;
    BakedMap->TransparencyMap = Texture;
    BakedMap->RevealSurfaceMap = RevealSurfaceTexture;
    BakedMap->RevealSurfaceBuildSignature = Snapshot.WorkingSet.RevealSurfaceSignature;
    BakedMap->bContainsRevealNormalRG = ComputedResult.bContainsRevealSurface;
    BakedMap->bContainsInnerMetallicB = ComputedResult.bContainsRevealSurface;
    BakedMap->bContainsRevealSurfaceCoverageAlpha = ComputedResult.bContainsRevealSurface;
    BakedMap->Resolution = Snapshot.SourcePayload->Resolution.X;
    BakedMap->PaddingPixels = Snapshot.PaddingPixels;
    BakedMap->BakedStrokeCount = Snapshot.BakedStrokeCount;
    BakedMap->BakeGuid = FGuid::NewGuid();
    BakedMap->SourceWrinkleMaskBakeGuid = Snapshot.SourceWrinkleMaskBakeGuid;
    BakedMap->SourceWrinkleMaskBuildSignature = Snapshot.SourceWrinkleMaskBuildSignature;
    BakedMap->WrinkleSuppressionSettingsSignature = Snapshot.SuppressionSettingsSignature;
    BakedMap->bWrinkleSuppressionBakedIntoAlpha = Snapshot.bHasWrinkleSuppression;
    BakedMap->BuildSignature = Snapshot.FinalBuildSignature;
    BakedMap->bContainsColorRGB = true;
    BakedMap->bContainsTransparencyAlpha = true;

    OutResult.TransparencyMap = Texture;
    OutResult.RevealSurfaceMap = RevealSurfaceTexture;
    OutResult.AppliedStrokeCount = ComputedResult.AppliedStrokeCount;
    OutResult.AppliedSampleCount = ComputedResult.AppliedSampleCount;
    OutResult.IgnoredNoHitOverridePixelCount = ComputedResult.IgnoredNoHitOverridePixelCount;
    OutResult.bAppliedWrinkleSuppression = ComputedResult.bAppliedWrinkleSuppression;
    OutResult.WarningMessage = MoveTemp(ComputedResult.WarningMessage);
    WetClothingAsset.MarkPackageDirty();
    return true;
}
