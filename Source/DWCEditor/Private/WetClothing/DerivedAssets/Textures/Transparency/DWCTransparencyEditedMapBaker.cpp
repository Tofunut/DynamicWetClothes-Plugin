//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionProcessor.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"

namespace
{
    FString BuildTransparencyMapAssetName(const UWetClothingAsset& Asset, const FDWCTransparencyAutoBakeResult& AutoResult)
    {
        return FString::Printf(
            TEXT("T_%s_Slot%d_UV%d_LOD%d_TransparencyMap"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName()),
            AutoResult.MaterialSlotIndex,
            AutoResult.UVChannelIndex,
            AutoResult.LODIndex);
    }

    UTexture2D* CreateOrUpdateTransparencyMapAsset(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<FColor>& Pixels,
        FString& OutErrorMessage)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(Asset, TEXT("Textures/Transparency"));
        const FString AssetName = BuildTransparencyMapAssetName(Asset, AutoResult);
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
            [&AutoResult](const FWetClothingBakedTransparencyMap& Candidate)
            {
                return Candidate.MaterialSlotIndex == AutoResult.MaterialSlotIndex;
            });
        if (ExistingMap != nullptr && IsValid(ExistingMap->TransparencyMap.Get()))
        {
            Texture = ExistingMap->TransparencyMap.Get();
            bFromSerializedReference = true;
        }

        if (Texture == nullptr)
        {
            UObject* ExistingObject = LoadObject<UObject>(nullptr, *ObjectPath);
            if (ExistingObject != nullptr && !ExistingObject->IsA<UTexture2D>())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated Transparency output path '%s' is occupied by an incompatible asset of type '%s'. Move or rename that asset and try again."),
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
                    TEXT("The transparency texture '%s' belongs to another Wet Clothing Asset (%s). It will not be overwritten."),
                    *GetPathNameSafe(Texture),
                    *ExistingOwnerGuid.ToString(EGuidFormats::DigitsWithHyphens));
                return nullptr;
            }
            if (!bHasOwnerGuid && !bFromSerializedReference)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated Transparency output path '%s' contains an unowned texture. It will not be overwritten automatically. Move or rename the existing texture, or restore the original WCA reference."),
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
            AutoResult.Resolution.X,
            AutoResult.Resolution.Y,
            1,
            1,
            TSF_BGRA8,
            reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->CompressionSettings = TC_Default;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->SRGB = true;
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

    FString MakeFinalBuildSignature(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FWetClothingTransparencyLayerData& Layer,
        const FString& WrinkleMaskBuildSignature,
        const FString& SuppressionSettingsSignature,
        const int32 PaddingPixels,
        const float EdgeFeatherPixels)
    {
        FString Canonical = FString::Printf(
            TEXT("DWC.Transparency.Edited.v3|Auto=%s|WrinkleMask=%s|Suppression=%s|Padding=%d|EdgeFeather=%.9g"),
            *AutoResult.BuildSignature,
            *WrinkleMaskBuildSignature,
            *SuppressionSettingsSignature,
            PaddingPixels,
            EdgeFeatherPixels);
        const int32 FirstStrokeIndex = FMath::Clamp(
            AutoResult.BaselineStrokeCount,
            0,
            Layer.EditableStrokes.Num());
        for (int32 StrokeIndex = FirstStrokeIndex; StrokeIndex < Layer.EditableStrokes.Num(); ++StrokeIndex)
        {
            const FDWCTransparencyBrushStroke& Stroke = Layer.EditableStrokes[StrokeIndex];
            Canonical += FString::Printf(
                TEXT("|Stroke=%s,%d,%d,%d,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits),
                Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex,
                static_cast<int32>(Stroke.BrushMode),
                Stroke.Falloff,
                Stroke.TargetAlpha,
                Stroke.Spacing,
                Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                Canonical += FString::Printf(
                    TEXT(";%.9g,%.9g,%.9g,%.9g"),
                    Sample.PositionUV.X,
                    Sample.PositionUV.Y,
                    Sample.RadiusUV,
                    Sample.Strength);
            }
        }
        return FMD5::HashAnsiString(*Canonical);
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
}

struct FDWCTransparencyEditedMapBakeSnapshot::FImpl
{
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoResult;
    FGuid LayerGuid;
    FWetClothingTransparencyTargetSurface TargetSurface;
    FWetClothingTransparencyManualColorSource ManualColorSource;
    TArray<FDWCTransparencyBrushStroke> EditableStrokes;
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;
    TArray<uint8> WrinkleSuppressionBuffer;
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
    bool bValid = false;
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
    return Impl.IsValid() && Impl->AutoResult.IsValid()
        ? Impl->AutoResult->MaterialSlotIndex
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

bool FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FString& OutReason)
{
    OutReason.Reset();
    if (AutoResult.LayerGuid != Layer.LayerGuid ||
        AutoResult.MaterialSlotIndex != Layer.TargetSurface.OuterMaterialSlotIndex)
    {
        OutReason = TEXT("The generated transparency map no longer matches the selected layer.");
        return false;
    }

    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    if (AutoResult.Resolution.X <= 0 || AutoResult.Resolution.Y <= 0 ||
        AutoResult.InnerColorBuffer.Num() != PixelCount ||
        AutoResult.AutoAlphaBuffer.Num() != PixelCount ||
        AutoResult.OuterCoverageBuffer.Num() != PixelCount ||
        AutoResult.ValidHitBuffer.Num() != PixelCount)
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

    FDWCTransparencyAutoBakeResult AutoResult;
    FString SignatureError;
    if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
            WetClothingAsset,
            Layer,
            AutoResult,
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
        [&AutoResult](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == AutoResult.MaterialSlotIndex;
        });
    if (BakedMap == nullptr || !BakedMap->IsRuntimeUsable())
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("Transparency map is missing or not runtime-usable.");
        }
        return false;
    }

    const FWetClothingTransparencyData& TransparencyData = WetClothingAsset.Authored.TransparencyData;
    const FDWCWrinkleSuppressionSource SuppressionSource =
        FDWCWrinkleSuppressionProcessor::FindExactSource(
            &WetClothingAsset,
            AutoResult.MaterialSlotIndex);
    const bool bHasWrinkleSuppression = SuppressionSource.IsValid() &&
        SuppressionSource.MaskTexture->Source.IsValid() &&
        SuppressionSource.MaskTexture->Source.GetSizeX() > 0 &&
        SuppressionSource.MaskTexture->Source.GetSizeY() > 0;
    const FString SourceWrinkleMaskBuildSignature =
        bHasWrinkleSuppression && SuppressionSource.BakedMap != nullptr
            ? SuppressionSource.BakedMap->BuildSignature
            : FString();
    const FString SuppressionSettingsSignature =
        FDWCWrinkleSuppressionProcessor::MakeSettingsSignature(
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            TransparencyData.WrinkleSuppressionStrength,
            TransparencyData.TransparencyPreviewStrength);
    const FString ExpectedSignature = MakeFinalBuildSignature(
        AutoResult,
        Layer,
        SourceWrinkleMaskBuildSignature,
        SuppressionSettingsSignature,
        TransparencyData.TransparencyPaddingPixels,
        TransparencyData.TransparencyEdgeFeatherPixels);

    const bool bCurrent =
        BakedMap->Resolution == AutoResult.Resolution.X &&
        BakedMap->PaddingPixels == TransparencyData.TransparencyPaddingPixels &&
        BakedMap->SourceWrinkleMaskBuildSignature == SourceWrinkleMaskBuildSignature &&
        BakedMap->WrinkleSuppressionSettingsSignature == SuppressionSettingsSignature &&
        BakedMap->bWrinkleSuppressionBakedIntoAlpha == bHasWrinkleSuppression &&
        BakedMap->BuildSignature == ExpectedSignature;
    if (!bCurrent && OutReason != nullptr)
    {
        *OutReason = TEXT("Transparency map was built from old authored data or bake settings.");
    }
    return bCurrent;
}

bool FDWCTransparencyEditedMapBaker::Bake(
    UWetClothingAsset& WetClothingAsset,
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FDWCTransparencyEditedMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    FDWCTransparencyEditedMapBakeSnapshot Snapshot;
    if (!BuildSnapshot(WetClothingAsset, Layer, AutoResult, Snapshot, OutErrorMessage))
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
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    return BuildSnapshot(
        WetClothingAsset,
        Layer,
        MakeShared<FDWCTransparencyAutoBakeResult>(AutoResult),
        OutSnapshot,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResultRef,
    FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutSnapshot = FDWCTransparencyEditedMapBakeSnapshot();
    const FDWCTransparencyAutoBakeResult& AutoResult = *AutoResultRef;
    FString CompatibilityReason;
    if (!IsAutoResultCompatible(Layer, AutoResult, CompatibilityReason))
    {
        OutErrorMessage = CompatibilityReason;
        return false;
    }

    FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    Snapshot.AutoResult = MoveTemp(AutoResultRef);
    Snapshot.LayerGuid = Layer.LayerGuid;
    Snapshot.TargetSurface = Layer.TargetSurface;
    Snapshot.ManualColorSource = Layer.ManualColorSource;
    Snapshot.EditableStrokes = Layer.EditableStrokes;
    Snapshot.RevealColorPaintStrokes = Layer.RevealColorPaintStrokes;
    Snapshot.BakedStrokeCount = Layer.EditableStrokes.Num();

    const FWetClothingTransparencyData& TransparencyData = WetClothingAsset.Authored.TransparencyData;
    const FDWCWrinkleSuppressionSource SuppressionSource = AutoResult.bIsFinalBakedBaseline
        ? FDWCWrinkleSuppressionSource()
        : FDWCWrinkleSuppressionProcessor::FindExactSource(
            &WetClothingAsset,
            AutoResult.MaterialSlotIndex);
    Snapshot.bHasWrinkleSuppression = !AutoResult.bIsFinalBakedBaseline &&
        SuppressionSource.IsValid() &&
        FDWCWrinkleSuppressionProcessor::BuildProcessedBuffer(
            SuppressionSource,
            AutoResult.Resolution,
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            Snapshot.WrinkleSuppressionBuffer,
            Snapshot.SuppressionWarning);
    if (AutoResult.bIsFinalBakedBaseline)
    {
        Snapshot.SuppressionWarning.Reset();
    }
    else if (!SuppressionSource.IsValid())
    {
        Snapshot.SuppressionWarning =
            TEXT("No exact baked wrinkle mask matched this slot, UV channel, and LOD. Transparency was baked without wrinkle suppression.");
    }
    else if (!Snapshot.bHasWrinkleSuppression && Snapshot.SuppressionWarning.IsEmpty())
    {
        Snapshot.SuppressionWarning =
            TEXT("The baked wrinkle mask could not be processed. Transparency was baked without wrinkle suppression.");
    }

    Snapshot.SourceWrinkleMaskBakeGuid =
        Snapshot.bHasWrinkleSuppression && SuppressionSource.BakedMap != nullptr
            ? SuppressionSource.BakedMap->BakeGuid
            : FGuid();
    Snapshot.SourceWrinkleMaskBuildSignature =
        Snapshot.bHasWrinkleSuppression && SuppressionSource.BakedMap != nullptr
            ? SuppressionSource.BakedMap->BuildSignature
            : FString();
    Snapshot.SuppressionSettingsSignature =
        FDWCWrinkleSuppressionProcessor::MakeSettingsSignature(
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            TransparencyData.WrinkleSuppressionStrength,
            TransparencyData.TransparencyPreviewStrength);
    Snapshot.TransparencyStrength = FMath::Max(TransparencyData.TransparencyPreviewStrength, 0.0f);
    Snapshot.SuppressionStrength = FMath::Max(TransparencyData.WrinkleSuppressionStrength, 0.0f);
    Snapshot.EdgeFeatherPixels = TransparencyData.TransparencyEdgeFeatherPixels;
    Snapshot.PaddingPixels = TransparencyData.TransparencyPaddingPixels;
    Snapshot.FinalBuildSignature = MakeFinalBuildSignature(
        AutoResult,
        Layer,
        Snapshot.SourceWrinkleMaskBuildSignature,
        Snapshot.SuppressionSettingsSignature,
        Snapshot.PaddingPixels,
        Snapshot.EdgeFeatherPixels);
    Snapshot.EstimatedBytes =
        AutoResult.InnerColorBuffer.GetAllocatedSize() +
        AutoResult.AutoAlphaBuffer.GetAllocatedSize() +
        AutoResult.OuterCoverageBuffer.GetAllocatedSize() +
        AutoResult.OuterIslandIDBuffer.GetAllocatedSize() +
        AutoResult.ValidHitBuffer.GetAllocatedSize() +
        Snapshot.WrinkleSuppressionBuffer.GetAllocatedSize() +
        static_cast<uint64>(AutoResult.Resolution.X) * AutoResult.Resolution.Y *
            (sizeof(FColor) + sizeof(uint8) * 2);
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
    const FDWCTransparencyAutoBakeResult& AutoResult = *Snapshot.AutoResult;
    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;

    FWetClothingTransparencyLayerData WorkerLayer;
    WorkerLayer.LayerGuid = Snapshot.LayerGuid;
    WorkerLayer.TargetSurface = Snapshot.TargetSurface;
    WorkerLayer.ManualColorSource = Snapshot.ManualColorSource;
    WorkerLayer.EditableStrokes = Snapshot.EditableStrokes;
    WorkerLayer.RevealColorPaintStrokes = Snapshot.RevealColorPaintStrokes;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        AutoResult,
        WorkerLayer.EditableStrokes,
        AutoResult.BaselineStrokeCount,
        AutoResult.MaterialSlotIndex,
        AutoResult.UVChannelIndex,
        ManualPremultipliedBuffer,
        ManualWeightBuffer);

    Result.bAppliedWrinkleSuppression = Snapshot.bHasWrinkleSuppression;
    Result.WarningMessage = Snapshot.SuppressionWarning;
    Result.FinalPixels = AutoResult.InnerColorBuffer;
    FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
        AutoResult,
        WorkerLayer.RevealColorPaintStrokes,
        AutoResult.MaterialSlotIndex,
        WorkerLayer.ManualColorSource.BaseRevealColor,
        Result.FinalPixels);
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (CancellationToken != nullptr && (PixelIndex & 4095) == 0 && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The transparency bake was canceled.");
            return Result;
        }
        const bool bHasValidInnerColor = AutoResult.ValidHitBuffer.IsValidIndex(PixelIndex) &&
            AutoResult.ValidHitBuffer[PixelIndex] != 0;
        const float EditedAlpha = FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
            AutoResult,
            ManualPremultipliedBuffer,
            ManualWeightBuffer,
            PixelIndex);
        if (!bHasValidInnerColor)
        {
            Result.IgnoredNoHitOverridePixelCount += EditedAlpha > KINDA_SMALL_NUMBER ? 1 : 0;
            Result.FinalPixels[PixelIndex].A = 0;
            continue;
        }
        if (AutoResult.bIsFinalBakedBaseline)
        {
            Result.FinalPixels[PixelIndex].A = static_cast<uint8>(FMath::RoundToInt(
                FMath::Clamp(EditedAlpha, 0.0f, 1.0f) * 255.0f));
        }
        else
        {
            const uint8 WrinkleSuppression = Snapshot.bHasWrinkleSuppression &&
                Snapshot.WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
                ? Snapshot.WrinkleSuppressionBuffer[PixelIndex]
                : 0;
            Result.FinalPixels[PixelIndex].A = FDWCTransparencyComposite::ResolveFinalAlpha8(
                EditedAlpha,
                Snapshot.TransparencyStrength,
                WrinkleSuppression,
                Snapshot.SuppressionStrength);
        }
    }

    if (!AutoResult.bIsFinalBakedBaseline)
    {
        ApplyCoverageEdgeFeather(
            AutoResult.Resolution,
            AutoResult.OuterCoverageBuffer,
            Snapshot.EdgeFeatherPixels,
            Result.FinalPixels);
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The transparency bake was canceled.");
            return Result;
        }
        DilateOutsideCoverage(
            AutoResult.Resolution,
            AutoResult.OuterCoverageBuffer,
            Snapshot.PaddingPixels,
            Result.FinalPixels);
    }

    const int32 FirstAppliedStrokeIndex = FMath::Clamp(
        AutoResult.BaselineStrokeCount,
        0,
        Snapshot.EditableStrokes.Num());
    Result.AppliedStrokeCount = Snapshot.EditableStrokes.Num() - FirstAppliedStrokeIndex;
    for (int32 StrokeIndex = FirstAppliedStrokeIndex; StrokeIndex < Snapshot.EditableStrokes.Num(); ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = Snapshot.EditableStrokes[StrokeIndex];
        Result.AppliedSampleCount += Stroke.bEnabled ? Stroke.Samples.Num() : 0;
    }
    Result.ResultBytes = Result.FinalPixels.GetAllocatedSize();
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
        !Snapshot.AutoResult.IsValid() ||
        Layer->TargetSurface.OuterMaterialSlotIndex != Snapshot.AutoResult->MaterialSlotIndex)
    {
        OutErrorMessage = TEXT("The transparency target changed before the bake result could be committed.");
        return false;
    }
    const int32 ExpectedPixelCount = Snapshot.AutoResult->Resolution.X * Snapshot.AutoResult->Resolution.Y;
    if (ComputedResult.FinalPixels.Num() != ExpectedPixelCount)
    {
        OutErrorMessage = TEXT("The transparency bake result has an unexpected pixel count.");
        return false;
    }

    UTexture2D* Texture = CreateOrUpdateTransparencyMapAsset(
        WetClothingAsset,
        *Layer,
        *Snapshot.AutoResult,
        ComputedResult.FinalPixels,
        OutErrorMessage);
    if (Texture == nullptr)
    {
        return false;
    }

    WetClothingAsset.Modify();
    FWetClothingBakedTransparencyMap* BakedMap = Layer->BakedMaps.FindByPredicate(
        [&Snapshot](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Snapshot.AutoResult->MaterialSlotIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &Layer->BakedMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = Snapshot.AutoResult->MaterialSlotIndex;
    BakedMap->TransparencyMap = Texture;
    BakedMap->Resolution = Snapshot.AutoResult->Resolution.X;
    BakedMap->PaddingPixels = Snapshot.PaddingPixels;
    BakedMap->BakedStrokeCount = Snapshot.BakedStrokeCount;
    BakedMap->BakeGuid = FGuid::NewGuid();
    if (!Snapshot.AutoResult->bIsFinalBakedBaseline)
    {
        BakedMap->SourceWrinkleMaskBakeGuid = Snapshot.SourceWrinkleMaskBakeGuid;
        BakedMap->SourceWrinkleMaskBuildSignature = Snapshot.SourceWrinkleMaskBuildSignature;
        BakedMap->WrinkleSuppressionSettingsSignature = Snapshot.SuppressionSettingsSignature;
        BakedMap->bWrinkleSuppressionBakedIntoAlpha = Snapshot.bHasWrinkleSuppression;
    }
    BakedMap->BuildSignature = Snapshot.FinalBuildSignature;
    BakedMap->bContainsColorRGB = true;
    BakedMap->bContainsTransparencyAlpha = true;

    OutResult.TransparencyMap = Texture;
    OutResult.AppliedStrokeCount = ComputedResult.AppliedStrokeCount;
    OutResult.AppliedSampleCount = ComputedResult.AppliedSampleCount;
    OutResult.IgnoredNoHitOverridePixelCount = ComputedResult.IgnoredNoHitOverridePixelCount;
    OutResult.bAppliedWrinkleSuppression = ComputedResult.bAppliedWrinkleSuppression;
    OutResult.WarningMessage = MoveTemp(ComputedResult.WarningMessage);
    WetClothingAsset.MarkPackageDirty();
    return true;
}
