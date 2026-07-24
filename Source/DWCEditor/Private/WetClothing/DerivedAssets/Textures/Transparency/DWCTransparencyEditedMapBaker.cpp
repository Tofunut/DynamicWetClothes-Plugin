#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "TextureResource.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionProcessor.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"

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
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<FColor>& Pixels,
        FString& OutErrorMessage)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(Asset, TEXT("Textures/Transparency"));
        const FString AssetName = BuildTransparencyMapAssetName(Asset, AutoResult);
        if (PackagePath.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not resolve the generated Transparency Maps package path.");
            return nullptr;
        }

        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;
        UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
        UPackage* Package = nullptr;
        bool bCreatedAsset = false;

        if (Texture != nullptr)
        {
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
            bCreatedAsset = true;
        }

        if (Texture == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Could not create transparency map '%s'."), *ObjectPath);
            return nullptr;
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
        Texture->UpdateResource();
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
        for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
        {
            Canonical += FString::Printf(
                TEXT("|Stroke=%s,%d,%d,%d,%d,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits),
                Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex,
                Stroke.UVChannelIndex,
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

bool FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(
    const FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FString& OutReason)
{
    OutReason.Reset();
    if (AutoResult.LayerGuid != Layer.LayerGuid ||
        AutoResult.MaterialSlotIndex != Layer.TargetSurface.OuterMaterialSlotIndex ||
        AutoResult.UVChannelIndex != Layer.TargetSurface.OuterUVChannel)
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

bool FDWCTransparencyEditedMapBaker::Bake(
    UWetClothingAsset& WetClothingAsset,
    FWetClothingTransparencyLayerData& Layer,
    const FDWCTransparencyAutoBakeResult& AutoResult,
    FDWCTransparencyEditedMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FDWCTransparencyEditedMapBakeResult();
    OutErrorMessage.Reset();

    FString CompatibilityReason;
    if (!IsAutoResultCompatible(Layer, AutoResult, CompatibilityReason))
    {
        OutErrorMessage = CompatibilityReason;
        return false;
    }

    const int32 PixelCount = AutoResult.Resolution.X * AutoResult.Resolution.Y;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
        AutoResult,
        Layer,
        AutoResult.MaterialSlotIndex,
        AutoResult.UVChannelIndex,
        ManualPremultipliedBuffer,
        ManualWeightBuffer);

    const FWetClothingTransparencyData& TransparencyData = WetClothingAsset.Authored.TransparencyData;
    const FDWCWrinkleSuppressionSource SuppressionSource =
        FDWCWrinkleSuppressionProcessor::FindExactSource(
            &WetClothingAsset,
            AutoResult.MaterialSlotIndex,
            AutoResult.UVChannelIndex,
            AutoResult.LODIndex);
    TArray<uint8> WrinkleSuppressionBuffer;
    FString SuppressionWarning;
    const bool bHasWrinkleSuppression = SuppressionSource.IsValid() &&
        FDWCWrinkleSuppressionProcessor::BuildProcessedBuffer(
            SuppressionSource,
            AutoResult.Resolution,
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            WrinkleSuppressionBuffer,
            SuppressionWarning);
    if (!SuppressionSource.IsValid())
    {
        SuppressionWarning =
            TEXT("No exact baked wrinkle mask matched this slot, UV channel, and LOD. Transparency was baked without wrinkle suppression.");
    }
    else if (!bHasWrinkleSuppression && SuppressionWarning.IsEmpty())
    {
        SuppressionWarning =
            TEXT("The baked wrinkle mask could not be processed. Transparency was baked without wrinkle suppression.");
    }
    OutResult.bAppliedWrinkleSuppression = bHasWrinkleSuppression;
    OutResult.WarningMessage = SuppressionWarning;

    const float TransparencyStrength = FMath::Max(TransparencyData.TransparencyPreviewStrength, 0.0f);
    const float SuppressionStrength = FMath::Max(TransparencyData.WrinkleSuppressionStrength, 0.0f);

    TArray<FColor> FinalPixels = AutoResult.InnerColorBuffer;
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        const bool bHasValidInnerColor = AutoResult.ValidHitBuffer.IsValidIndex(PixelIndex) &&
            AutoResult.ValidHitBuffer[PixelIndex] != 0;
        const float EditedAlpha = FDWCTransparencyBrushRasterizer::ResolveEditedAlpha(
            AutoResult,
            ManualPremultipliedBuffer,
            ManualWeightBuffer,
            PixelIndex);

        if (!bHasValidInnerColor)
        {
            if (EditedAlpha > KINDA_SMALL_NUMBER)
            {
                ++OutResult.IgnoredNoHitOverridePixelCount;
            }
            FinalPixels[PixelIndex].A = 0;
            continue;
        }

        const uint8 WrinkleSuppression = bHasWrinkleSuppression &&
            WrinkleSuppressionBuffer.IsValidIndex(PixelIndex)
            ? WrinkleSuppressionBuffer[PixelIndex]
            : 0;
        FinalPixels[PixelIndex].A = FDWCTransparencyComposite::ResolveFinalAlpha8(
            EditedAlpha,
            TransparencyStrength,
            WrinkleSuppression,
            SuppressionStrength);
    }

    ApplyCoverageEdgeFeather(
        AutoResult.Resolution,
        AutoResult.OuterCoverageBuffer,
        TransparencyData.TransparencyEdgeFeatherPixels,
        FinalPixels);
    DilateOutsideCoverage(
        AutoResult.Resolution,
        AutoResult.OuterCoverageBuffer,
        TransparencyData.TransparencyPaddingPixels,
        FinalPixels);

    UTexture2D* Texture = CreateOrUpdateTransparencyMapAsset(
        WetClothingAsset,
        Layer,
        AutoResult,
        FinalPixels,
        OutErrorMessage);
    if (Texture == nullptr)
    {
        return false;
    }

    FWetClothingBakedTransparencyMap* BakedMap = Layer.BakedMaps.FindByPredicate(
        [&AutoResult](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == AutoResult.MaterialSlotIndex &&
                   Candidate.UVChannelIndex == AutoResult.UVChannelIndex &&
                   Candidate.LODIndex == AutoResult.LODIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &Layer.BakedMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = AutoResult.MaterialSlotIndex;
    BakedMap->UVChannelIndex = AutoResult.UVChannelIndex;
    BakedMap->LODIndex = AutoResult.LODIndex;
    BakedMap->TransparencyMap = Texture;
    BakedMap->Resolution = AutoResult.Resolution.X;
    BakedMap->PaddingPixels = WetClothingAsset.Authored.TransparencyData.TransparencyPaddingPixels;
    BakedMap->BakeGuid = FGuid::NewGuid();
    BakedMap->SourceWrinkleMaskBakeGuid = bHasWrinkleSuppression && SuppressionSource.BakedMap != nullptr
        ? SuppressionSource.BakedMap->BakeGuid
        : FGuid();
    BakedMap->SourceWrinkleMaskBuildSignature =
        bHasWrinkleSuppression && SuppressionSource.BakedMap != nullptr
            ? SuppressionSource.BakedMap->BuildSignature
            : FString();
    BakedMap->WrinkleSuppressionSettingsSignature =
        FDWCWrinkleSuppressionProcessor::MakeSettingsSignature(
            TransparencyData.WrinkleSuppressionCoverageThreshold,
            TransparencyData.WrinkleSuppressionMaskSoftness,
            TransparencyData.WrinkleSuppressionStrength,
            TransparencyData.TransparencyPreviewStrength);
    BakedMap->BuildSignature = MakeFinalBuildSignature(
        AutoResult,
        Layer,
        BakedMap->SourceWrinkleMaskBuildSignature,
        BakedMap->WrinkleSuppressionSettingsSignature,
        TransparencyData.TransparencyPaddingPixels,
        TransparencyData.TransparencyEdgeFeatherPixels);
    BakedMap->bContainsColorRGB = true;
    BakedMap->bContainsTransparencyAlpha = true;
    BakedMap->bWrinkleSuppressionBakedIntoAlpha = bHasWrinkleSuppression;

    OutResult.TransparencyMap = Texture;
    OutResult.AppliedStrokeCount = Layer.EditableStrokes.Num();
    for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
    {
        if (Stroke.bEnabled)
        {
            OutResult.AppliedSampleCount += Stroke.Samples.Num();
        }
    }
    return true;
}
