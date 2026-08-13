//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "WetClothing/Foundation/Assets/DWCEditorArtifactStore.h"
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
    bool MatchesResolvedOutputResolution(
        const UTexture2D* Texture,
        const FIntPoint Resolution)
    {
        return Texture != nullptr && Resolution.X > 0 && Resolution.Y > 0 &&
            Texture->Source.GetSizeX() == Resolution.X &&
            Texture->Source.GetSizeY() == Resolution.Y;
    }

    struct FResolvedStage4Reveal
    {
        EDWCTransparencyStage4RevealSource Source =
            EDWCTransparencyStage4RevealSource::CanonicalReplay;
        TArray<FColor> CorrectedPixels;
        FString Warning;

        uint64 GetAllocatedBytes() const
        {
            return static_cast<uint64>(CorrectedPixels.GetAllocatedSize()) +
                static_cast<uint64>(Warning.GetAllocatedSize());
        }
    };

    FResolvedStage4Reveal ResolveStage4RevealCheckpoint(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& CanonicalSource,
        const float RevealMetallicDarkeningStrength)
    {
        FResolvedStage4Reveal Result;
        FString RestoreError;
        const EDWCTransparencyCorrectedRevealRestoreResult RestoreResult =
            FDWCTransparencyTempAssetStore::RestoreCurrentCorrectedReveal(
                Layer,
                CanonicalSource,
                RevealMetallicDarkeningStrength,
                Result.CorrectedPixels,
                RestoreError);
        if (RestoreResult != EDWCTransparencyCorrectedRevealRestoreResult::Restored)
        {
            if (RestoreResult == EDWCTransparencyCorrectedRevealRestoreResult::Invalid)
            {
                Result.Warning = FString::Printf(
                    TEXT("The Corrected Reveal Color checkpoint for slot %d was invalid and was rebuilt from canonical Stage 2/3 authoring: %s"),
                    Layer.TargetSurface.OuterMaterialSlotIndex,
                    *RestoreError);
            }
            Result.CorrectedPixels.Reset();
            return Result;
        }

        const int32 PixelCount = CanonicalSource.Resolution.X * CanonicalSource.Resolution.Y;
        if (Result.CorrectedPixels.Num() != PixelCount)
        {
            Result.Warning = FString::Printf(
                TEXT("The Corrected Reveal Color checkpoint for slot %d had an invalid pixel count and was rebuilt from canonical Stage 2/3 authoring."),
                Layer.TargetSurface.OuterMaterialSlotIndex);
            Result.CorrectedPixels.Reset();
            return Result;
        }

        Result.Source = EDWCTransparencyStage4RevealSource::CorrectedCheckpoint;
        return Result;
    }

    TSharedRef<const FDWCTransparencySourcePayload> BuildRetainedStage4Source(
        const FDWCTransparencySourcePayload& Source,
        const EDWCTransparencyStage4RevealSource RevealSource,
        const bool bRequiresRevealSurface)
    {
        TSharedRef<FDWCTransparencySourcePayload> Result =
            MakeShared<FDWCTransparencySourcePayload>();
        Result->LayerGuid = Source.LayerGuid;
        Result->MaterialSlotIndex = Source.MaterialSlotIndex;
        Result->UVChannelIndex = Source.UVChannelIndex;
        Result->LODIndex = Source.LODIndex;
        Result->Resolution = Source.Resolution;
        Result->OutputResolutionIdentity = Source.OutputResolutionIdentity;
        Result->BuildSignature = Source.BuildSignature;
        Result->OuterSampleCount = Source.OuterSampleCount;
        Result->ValidHitCount = Source.ValidHitCount;
        Result->NoHitCount = Source.NoHitCount;
        Result->OverlappedUVPixelCount = Source.OverlappedUVPixelCount;

        if (bRequiresRevealSurface)
        {
            Result->RevealSurfaceAuthoring = Source.RevealSurfaceAuthoring;
        }
        if (RevealSource == EDWCTransparencyStage4RevealSource::CanonicalReplay)
        {
            Result->InnerColorBuffer = Source.InnerColorBuffer;
            Result->AutoAlphaBuffer = Source.AutoAlphaBuffer;
            Result->OuterCoverageBuffer = Source.OuterCoverageBuffer;
            Result->OuterIslandIDBuffer = Source.OuterIslandIDBuffer;
            Result->ValidHitBuffer = Source.ValidHitBuffer;
        }
        return Result;
    }

    uint64 SaturatingAdd(const uint64 A, const uint64 B)
    {
        return B > MAX_uint64 - A ? MAX_uint64 : A + B;
    }

    uint64 SaturatingMultiply(const uint64 A, const uint64 B)
    {
        return A != 0 && B > MAX_uint64 / A ? MAX_uint64 : A * B;
    }

    FString BuildTransparencyGeneratedTextureAssetBaseName(const UWetClothingAsset& Asset)
    {
        FString AssetToken = FDWCRevealBakeUtilities::SanitizeAssetToken(Asset.GetName());
        AssetToken.ReplaceInline(TEXT("_Transparency_"), TEXT("_"));
        AssetToken.RemoveFromEnd(TEXT("_Transparency"));
        return FString::Printf(TEXT("T_%s"), *AssetToken);
    }

    FString BuildTransparencyMapAssetName(const UWetClothingAsset& Asset, const int32 MaterialSlotIndex)
    {
        const FString BaseName = BuildTransparencyGeneratedTextureAssetBaseName(Asset);
        return FString::Printf(
            TEXT("%s_Slot%d_TransparencyMap"),
            *BaseName,
            MaterialSlotIndex);
    }

    FString BuildRevealNormalMapAssetName(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex)
    {
        const FString BaseName = BuildTransparencyGeneratedTextureAssetBaseName(Asset);
        return FString::Printf(
            TEXT("%s_Slot%d_RevealNormalMap"),
            *BaseName,
            MaterialSlotIndex);
    }

    bool BuildFinalTransparencyTextureRequest(
        UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const int32 MaterialSlotIndex,
        const FIntPoint Resolution,
        const TArray<FColor>& Pixels,
        const bool bRevealNormal,
        FDWCEditorArtifactTextureRequest& OutRequest,
        FString& OutErrorMessage)
    {
        const FString PackagePath = FDWCRevealBakeUtilities::GetGeneratedPackagePath(Asset, TEXT("Textures/Transparency"));
        const FString AssetName = bRevealNormal
            ? BuildRevealNormalMapAssetName(Asset, MaterialSlotIndex)
            : BuildTransparencyMapAssetName(Asset, MaterialSlotIndex);
        const TCHAR* OutputLabel = bRevealNormal ? TEXT("Reveal Normal") : TEXT("Transparency");
        if (PackagePath.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not resolve the generated Transparency Textures package path.");
            return false;
        }

        const FString PackageName = PackagePath / AssetName;
        const FString ObjectPath = PackageName + TEXT(".") + AssetName;
        UTexture2D* Texture = nullptr;
        bool bFromSerializedReference = false;

        const FWetClothingBakedTransparencyMap* ExistingMap = Layer.BakedMaps.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingBakedTransparencyMap& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
        if (ExistingMap != nullptr &&
            IsValid(bRevealNormal ? ExistingMap->RevealNormalMap.Get() : ExistingMap->TransparencyMap.Get()) &&
            (bRevealNormal ? ExistingMap->RevealNormalMap->GetPathName() : ExistingMap->TransparencyMap->GetPathName()) == ObjectPath)
        {
            Texture = bRevealNormal ? ExistingMap->RevealNormalMap.Get() : ExistingMap->TransparencyMap.Get();
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
                return false;
            }
            Texture = Cast<UTexture2D>(ExistingObject);
        }

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
                return false;
            }
            if (!bHasOwnerGuid && !bFromSerializedReference)
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The generated %s output path '%s' contains an unowned texture. It will not be overwritten automatically. Move or rename the existing texture, or restore the original WCA reference."),
                    OutputLabel,
                    *ObjectPath);
                return false;
            }
        }

        OutRequest = FDWCEditorArtifactTextureRequest();
        OutRequest.OwnerAsset = &Asset;
        OutRequest.PackageName = PackageName;
        OutRequest.AssetName = AssetName;
        OutRequest.ExistingTexture = Texture;
        OutRequest.bExistingReferenceIsTrusted = bFromSerializedReference;
        OutRequest.Resolution = Resolution;
        OutRequest.SourceFormat = TSF_BGRA8;
        OutRequest.PixelData = reinterpret_cast<const uint8*>(Pixels.GetData());
        OutRequest.PixelBytes = static_cast<uint64>(Pixels.Num()) * sizeof(FColor);
        OutRequest.Settings.CompressionSettings = bRevealNormal ? TC_Normalmap : TC_Default;
        OutRequest.Settings.bSRGB = !bRevealNormal;
        OutRequest.Settings.LODGroup = bRevealNormal
            ? TEXTUREGROUP_WorldNormalMap
            : TEXTUREGROUP_Pixels2D;
        const TextureAddress Address = Layer.TargetSurface.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap
            ? TA_Wrap
            : TA_Clamp;
        OutRequest.Settings.AddressX = Address;
        OutRequest.Settings.AddressY = Address;
        OutRequest.DebugName = FString::Printf(TEXT("Transparency final %s"), OutputLabel);
        OutErrorMessage.Reset();
        return true;
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

    void DilateOutsideCoverageBounded(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const int32 PaddingPixels,
        TArray<FColor>& InOutPixels,
        const bool bDirectionalNormal)
    {
        const int32 SafePadding = FMath::Max(PaddingPixels, 0);
        const int32 PixelCount = Resolution.X * Resolution.Y;
        if (SafePadding <= 0 || OuterCoverage.Num() != PixelCount || InOutPixels.Num() != PixelCount)
        {
            return;
        }

        constexpr uint8 UnfilledDistance = MAX_uint8;
        TArray<uint8> Distance;
        Distance.Init(UnfilledDistance, PixelCount);
        TArray<int32> Frontier;
        Frontier.Reserve(FMath::Min(PixelCount, 64 * 1024));
        for (int32 Y = 0; Y < Resolution.Y; ++Y)
        {
            for (int32 X = 0; X < Resolution.X; ++X)
            {
                const int32 PixelIndex = Y * Resolution.X + X;
                if (OuterCoverage[PixelIndex] == 0)
                {
                    continue;
                }
                Distance[PixelIndex] = 0;
                bool bBoundary = false;
                for (int32 OffsetY = -1; OffsetY <= 1 && !bBoundary; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        if (OffsetX == 0 && OffsetY == 0)
                        {
                            continue;
                        }
                        const int32 NeighborX = X + OffsetX;
                        const int32 NeighborY = Y + OffsetY;
                        bBoundary = NeighborX >= 0 && NeighborY >= 0 &&
                            NeighborX < Resolution.X && NeighborY < Resolution.Y &&
                            OuterCoverage[NeighborY * Resolution.X + NeighborX] == 0;
                        if (bBoundary)
                        {
                            break;
                        }
                    }
                }
                if (bBoundary)
                {
                    Frontier.Add(PixelIndex);
                }
            }
        }

        TArray<int32> NextFrontier;
        for (int32 Step = 1; Step <= SafePadding && !Frontier.IsEmpty(); ++Step)
        {
            NextFrontier.Reset();
            NextFrontier.Reserve(Frontier.Num());
            for (const int32 FrontierIndex : Frontier)
            {
                const int32 SourceX = FrontierIndex % Resolution.X;
                const int32 SourceY = FrontierIndex / Resolution.X;
                for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        if (OffsetX == 0 && OffsetY == 0)
                        {
                            continue;
                        }
                        const int32 X = SourceX + OffsetX;
                        const int32 Y = SourceY + OffsetY;
                        if (X < 0 || Y < 0 || X >= Resolution.X || Y >= Resolution.Y)
                        {
                            continue;
                        }
                        const int32 PixelIndex = Y * Resolution.X + X;
                        if (Distance[PixelIndex] != UnfilledDistance)
                        {
                            continue;
                        }

                        int32 SampleCount = 0;
                        int32 SumR = 0;
                        int32 SumG = 0;
                        int32 SumB = 0;
                        int32 SumA = 0;
                        int32 FirstNeighborIndex = INDEX_NONE;
                        for (int32 SampleY = -1; SampleY <= 1; ++SampleY)
                        {
                            for (int32 SampleX = -1; SampleX <= 1; ++SampleX)
                            {
                                if (SampleX == 0 && SampleY == 0)
                                {
                                    continue;
                                }
                                const int32 NeighborX = X + SampleX;
                                const int32 NeighborY = Y + SampleY;
                                if (NeighborX < 0 || NeighborY < 0 ||
                                    NeighborX >= Resolution.X || NeighborY >= Resolution.Y)
                                {
                                    continue;
                                }
                                const int32 NeighborIndex = NeighborY * Resolution.X + NeighborX;
                                if (Distance[NeighborIndex] >= Step)
                                {
                                    continue;
                                }
                                if (FirstNeighborIndex == INDEX_NONE)
                                {
                                    FirstNeighborIndex = NeighborIndex;
                                }
                                const FColor& Neighbor = InOutPixels[NeighborIndex];
                                SumR += Neighbor.R;
                                SumG += Neighbor.G;
                                SumB += Neighbor.B;
                                SumA += Neighbor.A;
                                ++SampleCount;
                            }
                        }
                        if (SampleCount == 0)
                        {
                            continue;
                        }
                        InOutPixels[PixelIndex] = bDirectionalNormal
                            ? InOutPixels[FirstNeighborIndex]
                            : FColor(
                                SumR / SampleCount,
                                SumG / SampleCount,
                                SumB / SampleCount,
                                SumA / SampleCount);
                        Distance[PixelIndex] = static_cast<uint8>(Step);
                        NextFrontier.Add(PixelIndex);
                    }
                }
            }
            Frontier = MoveTemp(NextFrontier);
        }
    }

    void DilateOutsideCoverage(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const int32 PaddingPixels,
        TArray<FColor>& InOutPixels)
    {
        DilateOutsideCoverageBounded(
            Resolution, OuterCoverage, PaddingPixels, InOutPixels, false);
    }

    void DilateRevealNormalOutsideCoverage(
        const FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        const int32 PaddingPixels,
        TArray<FColor>& InOutPixels)
    {
        DilateOutsideCoverageBounded(
            Resolution, OuterCoverage, PaddingPixels, InOutPixels, true);
    }
}

uint64 FDWCTransparencyStage4MemoryPlan::GetTotalBytes() const
{
    uint64 Total = 0;
    Total = SaturatingAdd(Total, ResidentSharedBytes);
    Total = SaturatingAdd(Total, PrepareInputBytes);
    Total = SaturatingAdd(Total, SnapshotBytes);
    Total = SaturatingAdd(Total, OutputBytes);
    return SaturatingAdd(Total, ScratchBytes);
}

uint64 FDWCTransparencyStage4MemoryPlan::GetPreparePeakBytes() const
{
    return SaturatingAdd(
        SaturatingAdd(ResidentSharedBytes, PrepareInputBytes),
        SnapshotBytes);
}

uint64 FDWCTransparencyStage4MemoryPlan::GetWorkerPeakBytes() const
{
    const uint64 TransferBytes = FMath::Min(TransferableSnapshotBytes, OutputBytes);
    const uint64 SourcePhasePeak = SaturatingAdd(
        SaturatingAdd(ResidentSharedBytes, SnapshotBytes),
        OutputBytes - TransferBytes);
    const uint64 PostSourcePeak = SaturatingAdd(
        SaturatingAdd(
            SnapshotBytes >= TransferBytes ? SnapshotBytes - TransferBytes : 0,
            OutputBytes),
        ScratchBytes);
    return FMath::Max(SourcePhasePeak, PostSourcePeak);
}

uint64 FDWCTransparencyEditedMapBaker::EstimateCanonicalSourcePayloadBytes(
    const FIntPoint Resolution)
{
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return MAX_uint64;
    }
    const uint64 PixelCount = SaturatingMultiply(
        static_cast<uint64>(Resolution.X),
        static_cast<uint64>(Resolution.Y));
    if (PixelCount == MAX_uint64)
    {
        return MAX_uint64;
    }

    // Inner color, reveal surface, alpha, coverage, island, hit-distance,
    // source-priority, and the packed valid-hit bitset.
    uint64 Bytes = sizeof(FDWCTransparencySourcePayload) + 64ull * 1024ull;
    Bytes = SaturatingAdd(Bytes, SaturatingMultiply(PixelCount, 18ull));
    Bytes = SaturatingAdd(Bytes, (PixelCount + 7ull) / 8ull);
    return Bytes;
}

uint64 FDWCTransparencyEditedMapBaker::EstimateStage4SourcePayloadBytes(
    const FIntPoint Resolution,
    const bool bRequiresRevealSurface,
    const bool bRequiresOuterIslandID)
{
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        return MAX_uint64;
    }
    const uint64 PixelCount = SaturatingMultiply(
        static_cast<uint64>(Resolution.X),
        static_cast<uint64>(Resolution.Y));
    if (PixelCount == MAX_uint64)
    {
        return MAX_uint64;
    }

    // Base reveal RGBA, auto alpha, target coverage, and packed valid-hit bits.
    uint64 Bytes = sizeof(FDWCTransparencySourcePayload) + 64ull * 1024ull;
    Bytes = SaturatingAdd(Bytes, SaturatingMultiply(PixelCount, 6ull));
    Bytes = SaturatingAdd(Bytes, (PixelCount + 7ull) / 8ull);
    if (bRequiresOuterIslandID)
    {
        Bytes = SaturatingAdd(Bytes, SaturatingMultiply(PixelCount, sizeof(uint16)));
    }
    if (bRequiresRevealSurface)
    {
        Bytes = SaturatingAdd(Bytes, SaturatingMultiply(PixelCount, sizeof(FColor)));
    }
    return Bytes;
}

bool FDWCTransparencyEditedMapBaker::BuildMemoryPlan(
    const FIntPoint Resolution,
    const uint64 SourcePayloadBytes,
    const uint64 AuthoringInputBytes,
    const bool bRestoresCanonicalArtifacts,
    FDWCTransparencyStage4MemoryPlan& OutPlan,
    FString& OutErrorMessage)
{
    return BuildMemoryPlan(
        Resolution,
        SourcePayloadBytes,
        AuthoringInputBytes,
        bRestoresCanonicalArtifacts,
        true,
        true,
        OutPlan,
        OutErrorMessage);
}

bool FDWCTransparencyEditedMapBaker::BuildMemoryPlan(
    const FIntPoint Resolution,
    const uint64 SourcePayloadBytes,
    const uint64 AuthoringInputBytes,
    const bool bRestoresCanonicalArtifacts,
    const bool bRequiresRevealNormal,
    const bool bRequiresOuterIslandID,
    FDWCTransparencyStage4MemoryPlan& OutPlan,
    FString& OutErrorMessage)
{
    OutPlan = FDWCTransparencyStage4MemoryPlan();
    OutErrorMessage.Reset();
    if (Resolution.X <= 0 || Resolution.Y <= 0 || SourcePayloadBytes == 0 ||
        SourcePayloadBytes == MAX_uint64)
    {
        OutErrorMessage = TEXT("Stage 4 memory planning received an invalid source resolution or payload size.");
        return false;
    }

    const uint64 PixelCount = SaturatingMultiply(
        static_cast<uint64>(Resolution.X),
        static_cast<uint64>(Resolution.Y));
    if (PixelCount == MAX_uint64 || PixelCount > static_cast<uint64>(MAX_int32))
    {
        OutErrorMessage = TEXT("Stage 4 memory planning overflowed the supported pixel count.");
        return false;
    }

    const uint64 FixedAllowance = 64ull * 1024ull;
    const uint64 AlphaDomainBytes = SaturatingAdd(
        SaturatingMultiply(PixelCount, bRequiresOuterIslandID ? 4ull : 2ull),
        SaturatingAdd((PixelCount + 7ull) / 8ull, FixedAllowance));
    const uint64 CorrectedCheckpointBytes = SaturatingMultiply(PixelCount, sizeof(FColor));
    uint64 OutputBytes = SaturatingMultiply(PixelCount, sizeof(FColor));
    if (bRequiresRevealNormal)
    {
        OutputBytes = SaturatingAdd(
            OutputBytes,
            SaturatingMultiply(PixelCount, sizeof(FColor)));
    }
    OutputBytes = SaturatingAdd(
        OutputBytes,
        SaturatingMultiply(PixelCount, sizeof(uint8)));
    // The largest post-source phase is bounded frontier dilation.
    const uint64 WorkerScratchBytes = SaturatingMultiply(PixelCount, 9ull);
    const uint64 RestoreScratchBytes = bRestoresCanonicalArtifacts
        ? SaturatingMultiply(PixelCount, sizeof(FColor))
        : 0ull;

    OutPlan.ResidentSharedBytes = SourcePayloadBytes;
    OutPlan.PrepareInputBytes = AuthoringInputBytes;
    OutPlan.SnapshotBytes = SaturatingAdd(
        SaturatingAdd(AlphaDomainBytes, CorrectedCheckpointBytes),
        SaturatingAdd(AuthoringInputBytes, FixedAllowance));
    OutPlan.OutputBytes = OutputBytes;
    OutPlan.ScratchBytes = FMath::Max(WorkerScratchBytes, RestoreScratchBytes);
    OutPlan.TransferableSnapshotBytes = CorrectedCheckpointBytes;
    if (OutPlan.GetTotalBytes() == MAX_uint64)
    {
        OutErrorMessage = TEXT("Stage 4 memory planning overflowed the supported reservation size.");
        OutPlan = FDWCTransparencyStage4MemoryPlan();
        return false;
    }
    return true;
}

struct FDWCTransparencyEditedMapBakeSnapshot::FImpl
{
    // Read-only Stage 3/final packaging dependency. Stage 4 brush replay,
    // validity, feathering, and padding use WorkingSet.AlphaDomain instead.
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload;
    int32 MaterialSlotIndex = INDEX_NONE;
    FIntPoint SourceResolution = FIntPoint::ZeroValue;
    FString SourceBuildSignature;
    int32 SourceValidHitCount = 0;
    int32 SourceNoHitCount = 0;
    EDWCTransparencyStage4RevealSource RevealSource =
        EDWCTransparencyStage4RevealSource::CanonicalReplay;
    TArray<FColor> CorrectedRevealPixels;
    FString RevealFallbackWarning;
    FDWCTransparencyFinalWorkingSet WorkingSet;
    FGuid LayerGuid;
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
    float RevealMetallicDarkeningStrength = 0.0f;
    float SuppressionStrength = 0.0f;
    float EdgeFeatherPixels = 0.0f;
    int32 PaddingPixels = 0;
    int32 BakedStrokeCount = 0;
    bool bHasWrinkleSuppression = false;
    bool bRequiresRevealSurface = false;
    bool bValid = false;
    uint64 EstimatedPrivateBytes = 0;
    uint64 EstimatedOutputBytes = 0;
    uint64 EstimatedTransferableBytes = 0;
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
    return Impl.IsValid() ? Impl->MaterialSlotIndex : INDEX_NONE;
}

FGuid FDWCTransparencyEditedMapBakeSnapshot::GetLayerGuid() const
{
    return Impl.IsValid() ? Impl->LayerGuid : FGuid();
}

FIntPoint FDWCTransparencyEditedMapBakeSnapshot::GetSourceResolution() const
{
    return Impl.IsValid() ? Impl->SourceResolution : FIntPoint::ZeroValue;
}

const FString& FDWCTransparencyEditedMapBakeSnapshot::GetSourceBuildSignature() const
{
    static const FString Empty;
    return Impl.IsValid() ? Impl->SourceBuildSignature : Empty;
}

int32 FDWCTransparencyEditedMapBakeSnapshot::GetSourceValidHitCount() const
{
    return Impl.IsValid() ? Impl->SourceValidHitCount : 0;
}

int32 FDWCTransparencyEditedMapBakeSnapshot::GetSourceNoHitCount() const
{
    return Impl.IsValid() ? Impl->SourceNoHitCount : 0;
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

uint64 FDWCTransparencyEditedMapBakeSnapshot::GetEstimatedTransferableBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedTransferableBytes : 0;
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
        SourcePayload.OutputResolutionIdentity.IsEmpty() ||
        SourcePayload.InnerColorBuffer.Num() != PixelCount ||
        (Layer.RequiresRevealSurface() &&
         !SourcePayload.RevealSurfaceAuthoring.IsValidForResolution(SourcePayload.Resolution)) ||
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
    if (BakedMap == nullptr || !BakedMap->IsRuntimeUsableForLayer(Layer.RequiresRuntimeRevealNormal()))
    {
        if (OutReason != nullptr)
        {
            *OutReason = Layer.RequiresRuntimeRevealNormal()
                ? TEXT("Transparency map or its required Reveal Normal is missing or not runtime-usable.")
                : TEXT("Transparency map is missing or not runtime-usable.");
        }
        return false;
    }

    const FWetClothingTransparencyData& TransparencyData = WetClothingAsset.Authored.TransparencyData;
    const FDWCWrinkleSuppressionDependencySnapshot SuppressionDependency =
        FDWCWrinkleSuppressionCoverageService::ResolveDependency(
            &WetClothingAsset,
            SourcePayload.MaterialSlotIndex,
            true);
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
        Layer,
        TransparencyData.RevealMetallicDarkeningStrength);
    const FString RevealNormalSignature =
        FDWCTransparencySignatureService::BuildRevealNormalSignature(
            SourcePayload.BuildSignature);
    const FString ExpectedSignature = FDWCTransparencySignatureService::BuildFinalSignature(
        RevealSignature,
        Layer,
        SourceWrinkleMaskBuildSignature,
        SuppressionSettingsSignature,
        TransparencyData.TransparencyPaddingPixels,
        TransparencyData.TransparencyEdgeFeatherPixels,
        SourcePayload.BuildSignature);
    FDWCTransparencyFinalSignatureInputs AlphaSignatureInputs;
    AlphaSignatureInputs.SourceSignature = SourcePayload.BuildSignature;
    AlphaSignatureInputs.AlphaAuthoringSignature =
        FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(Layer);
    AlphaSignatureInputs.WrinkleMaskBuildSignature = SourceWrinkleMaskBuildSignature;
    AlphaSignatureInputs.SuppressionSettingsSignature = SuppressionSettingsSignature;
    AlphaSignatureInputs.PaddingPixels = TransparencyData.TransparencyPaddingPixels;
    AlphaSignatureInputs.EdgeFeatherPixels = TransparencyData.TransparencyEdgeFeatherPixels;
    const FString ExpectedFinalAlphaSignature =
        FDWCTransparencySignatureService::BuildFinalAlphaSignature(AlphaSignatureInputs);

    TArray<FString> Mismatches;
    if (BakedMap->Resolution != SourcePayload.Resolution.X)
    {
        Mismatches.Add(TEXT("the output resolution changed"));
    }
    if (!MatchesResolvedOutputResolution(
            BakedMap->TransparencyMap.Get(), SourcePayload.Resolution))
    {
        Mismatches.Add(TEXT("the Transparency Map dimensions do not match the resolved output resolution"));
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
    if (BakedMap->FinalAlphaBuildSignature != ExpectedFinalAlphaSignature)
    {
        Mismatches.Add(TEXT("the Stage 4 alpha working set changed"));
    }
    if (BakedMap->BuildSignature != ExpectedSignature)
    {
        Mismatches.Add(TEXT("the authored transparency data changed"));
    }
    if (Layer.RequiresRuntimeRevealNormal() &&
        BakedMap->RevealNormalBuildSignature != RevealNormalSignature)
    {
        Mismatches.Add(TEXT("the source Reveal Normal data changed or was not baked"));
    }
    if (Layer.RequiresRuntimeRevealNormal() && !BakedMap->HasRuntimeRevealNormalPayload())
    {
        Mismatches.Add(TEXT("the required coverage-weighted Reveal Normal runtime artifact is missing"));
    }
    if (Layer.RequiresRuntimeRevealNormal() &&
        !MatchesResolvedOutputResolution(
            BakedMap->RevealNormalMap.Get(), SourcePayload.Resolution))
    {
        Mismatches.Add(TEXT("the Reveal Normal dimensions do not match the resolved output resolution"));
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
    const TArray<FDWCTransparencyBrushStroke>& EditableStrokes = Layer.GetEditableStrokes();
    AlphaSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoResultRef->BaselineStrokeCount, 0, EditableStrokes.Num());
    AlphaSnapshot.AuthoredStrokeCount = EditableStrokes.Num();
    AlphaSnapshot.FallbackStrokes = EditableStrokes;
    for (int32 StrokeIndex = AlphaSnapshot.BaselineStrokeCount;
         StrokeIndex < EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = EditableStrokes[StrokeIndex];
        AlphaSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.GetSampleCount() : 0;
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
    const TArray<FDWCTransparencyBrushStroke>& EditableStrokes = Layer.GetEditableStrokes();
    AlphaSnapshot.BaselineStrokeCount = FMath::Clamp(
        AutoResultRef->BaselineStrokeCount, 0, EditableStrokes.Num());
    AlphaSnapshot.AuthoredStrokeCount = EditableStrokes.Num();
    AlphaSnapshot.FallbackStrokes = EditableStrokes;
    for (int32 StrokeIndex = AlphaSnapshot.BaselineStrokeCount;
         StrokeIndex < EditableStrokes.Num();
         ++StrokeIndex)
    {
        const FDWCTransparencyBrushStroke& Stroke = EditableStrokes[StrokeIndex];
        AlphaSnapshot.AppliedSampleCount += Stroke.bEnabled ? Stroke.GetSampleCount() : 0;
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

    // Corrected reveal color is a compact Stage 3 checkpoint. Keep it separate
    // from the immutable Stage 2 source so selecting the checkpoint never
    // clones the full hit/surface payload.
    FResolvedStage4Reveal Stage4Reveal = ResolveStage4RevealCheckpoint(
        Layer,
        CanonicalSourcePayload,
        WetClothingAsset.Authored.TransparencyData.RevealMetallicDarkeningStrength);
    const FDWCTransparencySourcePayload& SourcePayload = CanonicalSourcePayload;

    FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    Snapshot.SourcePayload = MoveTemp(AutoResultRef);
    Snapshot.MaterialSlotIndex = SourcePayload.MaterialSlotIndex;
    Snapshot.SourceResolution = SourcePayload.Resolution;
    Snapshot.SourceBuildSignature = SourcePayload.BuildSignature;
    Snapshot.SourceValidHitCount = SourcePayload.ValidHitCount;
    Snapshot.SourceNoHitCount = SourcePayload.NoHitCount;
    Snapshot.RevealSource = Stage4Reveal.Source;
    Snapshot.CorrectedRevealPixels = MoveTemp(Stage4Reveal.CorrectedPixels);
    Snapshot.RevealFallbackWarning = MoveTemp(Stage4Reveal.Warning);
    Snapshot.LayerGuid = Layer.LayerGuid;
    Snapshot.bRequiresRevealSurface = Layer.RequiresRevealSurface();
    Snapshot.ManualColorSource = Layer.ManualColorSource;
    if (Snapshot.RevealSource == EDWCTransparencyStage4RevealSource::CanonicalReplay)
    {
        Snapshot.RevealColorPaintStrokes = Layer.GetRevealColorPaintStrokes();
    }
    Snapshot.RevealMetallicDarkeningStrength =
        WetClothingAsset.Authored.TransparencyData.RevealMetallicDarkeningStrength;
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
            SourcePayload.MaterialSlotIndex,
            true);
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
    if (!Snapshot.RevealFallbackWarning.IsEmpty())
    {
        Snapshot.SuppressionWarning = Snapshot.SuppressionWarning.IsEmpty()
            ? Snapshot.RevealFallbackWarning
            : Snapshot.SuppressionWarning + TEXT("\n") + Snapshot.RevealFallbackWarning;
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
    Snapshot.SourcePayload = BuildRetainedStage4Source(
        SourcePayload,
        Snapshot.RevealSource,
        Snapshot.bRequiresRevealSurface);
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
    Snapshot.EstimatedPrivateBytes =
        Snapshot.SourcePayload->GetAllocatedBytes() +
        Snapshot.WorkingSet.OwnedBytes +
        Snapshot.WorkingSet.RetainedBytes +
        static_cast<uint64>(Snapshot.RevealColorPaintStrokes.GetAllocatedSize()) +
        static_cast<uint64>(Snapshot.CorrectedRevealPixels.GetAllocatedSize()) +
        static_cast<uint64>(Snapshot.RevealFallbackWarning.GetAllocatedSize());
    for (const FDWCTransparencyRevealColorStroke& Stroke : Snapshot.RevealColorPaintStrokes)
    {
        Snapshot.EstimatedPrivateBytes +=
            Stroke.GetSampleAllocatedSize();
    }
    Snapshot.EstimatedOutputBytes = PixelCount * sizeof(FColor) *
        (Snapshot.bRequiresRevealSurface ? 2ull : 1ull) +
        (Snapshot.RevealSource == EDWCTransparencyStage4RevealSource::CanonicalReplay
            ? PixelCount * sizeof(uint8)
            : 0ull);
    Snapshot.EstimatedTransferableBytes =
        Snapshot.RevealSource == EDWCTransparencyStage4RevealSource::CorrectedCheckpoint
            ? static_cast<uint64>(Snapshot.CorrectedRevealPixels.GetAllocatedSize())
            : 0ull;
    // Feathering and the two directional dilation passes use bounded scratch
    // arrays; count their peak rather than reserving them as persistent data.
    Snapshot.EstimatedScratchBytes = PixelCount * 9ull;
    Snapshot.EstimatedBytes = Snapshot.EstimatedPrivateBytes +
        Snapshot.EstimatedOutputBytes + Snapshot.EstimatedScratchBytes;
    Snapshot.bValid = true;
    OutErrorMessage.Reset();
    return true;
}

FDWCTransparencyEditedMapComputedResult FDWCTransparencyEditedMapBaker::ComputeSnapshot(
    FDWCTransparencyEditedMapBakeSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FDWCTransparencyEditedMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The transparency bake snapshot is invalid.");
        return Result;
    }
    FDWCTransparencyEditedMapBakeSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    const FDWCTransparencySourcePayload& SourcePayload = *Snapshot.SourcePayload;
    const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain = *Snapshot.WorkingSet.AlphaDomain;
    const int32 PixelCount = AlphaDomain.Resolution.X * AlphaDomain.Resolution.Y;

    FDWCTransparencyAlphaWorkingSnapshot MaterializedAlphaSnapshot;
    const FDWCTransparencyAlphaWorkingSnapshot* SparseAlphaSnapshot = &Snapshot.WorkingSet.Alpha;
    FString AlphaError;
    if (Snapshot.WorkingSet.Alpha.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay &&
        !FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
            AlphaDomain,
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
    AlphaContext.AlphaDomain = &AlphaDomain;
    AlphaContext.AlphaSnapshotView = &AlphaView;
    const FDWCWrinkleCoverageCacheValue* WrinkleCoverage =
        Snapshot.WrinkleCoverageLease.GetAs<FDWCWrinkleCoverageCacheValue>();

    Result.bAppliedWrinkleSuppression = Snapshot.bHasWrinkleSuppression && WrinkleCoverage != nullptr;
    Result.WarningMessage = Snapshot.SuppressionWarning;
    const bool bUsesCorrectedReveal =
        Snapshot.RevealSource == EDWCTransparencyStage4RevealSource::CorrectedCheckpoint;
    if (bUsesCorrectedReveal)
    {
        Result.FinalPixels = MoveTemp(Snapshot.CorrectedRevealPixels);
        Snapshot.EstimatedPrivateBytes =
            Snapshot.EstimatedPrivateBytes >= Snapshot.EstimatedTransferableBytes
                ? Snapshot.EstimatedPrivateBytes - Snapshot.EstimatedTransferableBytes
                : 0;
        Snapshot.EstimatedTransferableBytes = 0;
    }
    else
    {
        Result.FinalPixels = SourcePayload.InnerColorBuffer;
    }
    Result.bContainsRevealNormal = Snapshot.bRequiresRevealSurface;
    if (Result.bContainsRevealNormal)
    {
        Result.FinalRevealNormalPixels.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            Result.FinalRevealNormalPixels[PixelIndex] =
                FDWCTransparencyRevealSurfaceAuthoringPayload::EncodeRuntimeRevealNormal(
                    SourcePayload.RevealSurfaceAuthoring[PixelIndex]);
        }
    }
    if (!bUsesCorrectedReveal)
    {
        // Missing or stale Stage 3 checkpoints are reconstructed from the
        // canonical Stage 2 result and serialized Reveal Color strokes.
        FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
            SourcePayload,
            Snapshot.RevealColorPaintStrokes,
            SourcePayload.MaterialSlotIndex,
            Snapshot.ManualColorSource.BaseRevealColor,
            Result.FinalPixels);
        if (!FDWCTransparencyComposite::ApplyRevealMetallicDarkening(
                Result.FinalPixels,
                SourcePayload,
                Snapshot.RevealMetallicDarkeningStrength,
                CancellationToken))
        {
            Result.bCanceled = CancellationToken != nullptr && CancellationToken->IsCanceled();
            Result.Error = Result.bCanceled
                ? TEXT("The transparency bake was canceled.")
                : TEXT("Metallic reveal-color correction could not be applied.");
            return Result;
        }
        Result.RebuiltCorrectedRevealAlpha.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const uint8 SourceAlpha = SourcePayload.AutoAlphaBuffer[PixelIndex];
            Result.FinalPixels[PixelIndex].A = SourceAlpha;
            Result.RebuiltCorrectedRevealAlpha[PixelIndex] = SourceAlpha;
        }
        Result.bRebuiltCorrectedRevealCheckpoint = true;
    }

    // Everything below operates on the independent alpha domain and output
    // arrays. Release the large canonical Stage 2 payload before scratch-heavy
    // feathering and dilation begin.
    Snapshot.SourcePayload.Reset();
    Snapshot.RevealColorPaintStrokes.Reset();
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        if (CancellationToken != nullptr && (PixelIndex & 4095) == 0 && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The transparency bake was canceled.");
            return Result;
        }
        const bool bHasValidInnerColor = AlphaDomain.ValidSource.IsValidIndex(PixelIndex) &&
            AlphaDomain.ValidSource[PixelIndex];
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
            const int32 PixelX = PixelIndex % AlphaDomain.Resolution.X;
            const int32 PixelY = PixelIndex / AlphaDomain.Resolution.X;
            const FVector2f UV(
                (static_cast<float>(PixelX) + 0.5f) / AlphaDomain.Resolution.X,
                (static_cast<float>(PixelY) + 0.5f) / AlphaDomain.Resolution.Y);
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
        AlphaDomain.Resolution,
        AlphaDomain.OuterCoverage,
        Snapshot.EdgeFeatherPixels,
        Result.FinalPixels);
    if (CancellationToken != nullptr && CancellationToken->IsCanceled())
    {
        Result.bCanceled = true;
        Result.Error = TEXT("The transparency bake was canceled.");
        return Result;
    }
    DilateOutsideCoverage(
        AlphaDomain.Resolution,
        AlphaDomain.OuterCoverage,
        Snapshot.PaddingPixels,
        Result.FinalPixels);
    if (Result.bContainsRevealNormal)
    {
        DilateRevealNormalOutsideCoverage(
            AlphaDomain.Resolution,
            AlphaDomain.OuterCoverage,
            Snapshot.PaddingPixels,
            Result.FinalRevealNormalPixels);
    }

    Result.AppliedStrokeCount = FMath::Max(
        Snapshot.WorkingSet.Alpha.AuthoredStrokeCount -
        Snapshot.WorkingSet.Alpha.BaselineStrokeCount,
        0);
    Result.AppliedSampleCount = Snapshot.WorkingSet.Alpha.AppliedSampleCount;
    Result.ResultBytes = Result.FinalPixels.GetAllocatedSize() +
        Result.FinalRevealNormalPixels.GetAllocatedSize() +
        Result.RebuiltCorrectedRevealAlpha.GetAllocatedSize();
    Snapshot.WorkingSet.AlphaDomain.Reset();
    Snapshot.WorkingSet.Alpha.ModifiedTiles.Reset();
    Snapshot.WorkingSet.Alpha.FallbackStrokes.Reset();
    Snapshot.WrinkleCoverageLease.Reset();
    Snapshot.EstimatedPrivateBytes = 1024ull * 1024ull;
    Snapshot.EstimatedScratchBytes = 0;
    Snapshot.EstimatedBytes = Snapshot.EstimatedPrivateBytes;
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
        Layer->TargetSurface.OuterMaterialSlotIndex != Snapshot.MaterialSlotIndex)
    {
        OutErrorMessage = TEXT("The transparency target changed before the bake result could be committed.");
        return false;
    }
    const int32 ExpectedPixelCount = Snapshot.SourceResolution.X * Snapshot.SourceResolution.Y;
    if (ComputedResult.FinalPixels.Num() != ExpectedPixelCount ||
        (ComputedResult.bContainsRevealNormal &&
         ComputedResult.FinalRevealNormalPixels.Num() != ExpectedPixelCount))
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
        *Layer,
        CurrentData.RevealMetallicDarkeningStrength);
    const FString CurrentRevealNormalSignature =
        FDWCTransparencySignatureService::BuildRevealNormalSignature(
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
        CurrentRevealNormalSignature != Snapshot.WorkingSet.RevealNormalSignature ||
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
                Layer->TargetSurface.OuterMaterialSlotIndex,
                true);
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
                ComputedResult.FinalPixels,
                ComputedResult.RebuiltCorrectedRevealAlpha,
                Snapshot.SourceResolution,
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
        ComputedResult.RebuiltCorrectedRevealAlpha.Reset();
    }

    TArray<FDWCEditorArtifactTextureRequest> ArtifactRequests;
    ArtifactRequests.Reserve(ComputedResult.bContainsRevealNormal ? 2 : 1);
    FDWCEditorArtifactTextureRequest TransparencyRequest;
    if (!BuildFinalTransparencyTextureRequest(
            WetClothingAsset,
            *Layer,
            Snapshot.MaterialSlotIndex,
            Snapshot.SourceResolution,
            ComputedResult.FinalPixels,
            false,
            TransparencyRequest,
            OutErrorMessage))
    {
        return false;
    }
    ArtifactRequests.Add(MoveTemp(TransparencyRequest));

    if (ComputedResult.bContainsRevealNormal)
    {
        FDWCEditorArtifactTextureRequest RevealNormalRequest;
        if (!BuildFinalTransparencyTextureRequest(
                WetClothingAsset,
                *Layer,
                Snapshot.MaterialSlotIndex,
                Snapshot.SourceResolution,
                ComputedResult.FinalRevealNormalPixels,
                true,
                RevealNormalRequest,
                OutErrorMessage))
        {
            return false;
        }
        ArtifactRequests.Add(MoveTemp(RevealNormalRequest));
    }

    TArray<FDWCEditorArtifactCommitReceipt> ArtifactReceipts;
    if (!FDWCEditorArtifactStore::Get()->CommitTextureBatch(
            ArtifactRequests, ArtifactReceipts, OutErrorMessage) ||
        ArtifactReceipts.Num() != ArtifactRequests.Num())
    {
        return false;
    }
    UTexture2D* Texture = ArtifactReceipts[0].Texture;
    UTexture2D* RevealNormalTexture = ComputedResult.bContainsRevealNormal
        ? ArtifactReceipts[1].Texture
        : nullptr;
    ComputedResult.FinalPixels.Reset();
    ComputedResult.FinalRevealNormalPixels.Reset();

    if (!MatchesResolvedOutputResolution(Texture, Snapshot.SourceResolution) ||
        (ComputedResult.bContainsRevealNormal &&
         !MatchesResolvedOutputResolution(
             RevealNormalTexture, Snapshot.SourceResolution)))
    {
        OutErrorMessage = TEXT("A committed Transparency runtime texture does not match the resolved output resolution.");
        return false;
    }

    WetClothingAsset.Modify();
    FWetClothingBakedTransparencyMap* BakedMap = Layer->BakedMaps.FindByPredicate(
        [&Snapshot](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Snapshot.MaterialSlotIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &Layer->BakedMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = Snapshot.MaterialSlotIndex;
    BakedMap->TransparencyMap = Texture;
    BakedMap->RevealNormalMap = RevealNormalTexture;
    BakedMap->RevealNormalBuildSignature = ComputedResult.bContainsRevealNormal
        ? Snapshot.WorkingSet.RevealNormalSignature
        : FString();
    BakedMap->bSourceCoverageBakedIntoRevealNormal = ComputedResult.bContainsRevealNormal;
    BakedMap->RevealSurfaceMap = nullptr;
    BakedMap->RevealSurfaceBuildSignature.Reset();
    BakedMap->bContainsRevealNormalRG = false;
    BakedMap->bContainsInnerMetallicB = false;
    BakedMap->bContainsRevealSurfaceCoverageAlpha = false;
    BakedMap->bMetallicDarkeningBakedIntoColor = true;
    BakedMap->Resolution = Snapshot.SourceResolution.X;
    BakedMap->PaddingPixels = Snapshot.PaddingPixels;
    BakedMap->BakedStrokeCount = Snapshot.BakedStrokeCount;
    BakedMap->BakeGuid = FGuid::NewGuid();
    BakedMap->SourceWrinkleMaskBakeGuid = Snapshot.SourceWrinkleMaskBakeGuid;
    BakedMap->SourceWrinkleMaskBuildSignature = Snapshot.SourceWrinkleMaskBuildSignature;
    BakedMap->WrinkleSuppressionSettingsSignature = Snapshot.SuppressionSettingsSignature;
    BakedMap->bWrinkleSuppressionBakedIntoAlpha = Snapshot.bHasWrinkleSuppression;
    BakedMap->BuildSignature = Snapshot.FinalBuildSignature;
    BakedMap->FinalAlphaBuildSignature = Snapshot.WorkingSet.FinalAlphaSignature;
    BakedMap->bContainsColorRGB = true;
    BakedMap->bContainsTransparencyAlpha = true;

    OutResult.TransparencyMap = Texture;
    OutResult.RevealNormalMap = RevealNormalTexture;
    OutResult.AppliedStrokeCount = ComputedResult.AppliedStrokeCount;
    OutResult.AppliedSampleCount = ComputedResult.AppliedSampleCount;
    OutResult.IgnoredNoHitOverridePixelCount = ComputedResult.IgnoredNoHitOverridePixelCount;
    OutResult.bAppliedWrinkleSuppression = ComputedResult.bAppliedWrinkleSuppression;
    OutResult.WarningMessage = MoveTemp(ComputedResult.WarningMessage);
    WetClothingAsset.MarkPackageDirty();
    return true;
}
