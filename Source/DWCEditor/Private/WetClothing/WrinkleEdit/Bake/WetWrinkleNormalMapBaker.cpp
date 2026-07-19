#include "WetClothing/WrinkleEdit/Bake/WetWrinkleNormalMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinklePreset.h"
#include "DataAssets/WetWrinklePresetBuilder.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/WrinkleEdit/Stroke/WetProceduralRidgeRasterizer.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

namespace
{
    struct FWetWrinkleNormalSource
    {
        explicit FWetWrinkleNormalSource(UTexture2D* InTexture)
            : Texture(InTexture)
        {
            if (Texture == nullptr || !Texture->Source.IsValid())
            {
                return;
            }

            SizeX = Texture->Source.GetSizeX();
            SizeY = Texture->Source.GetSizeY();
            SourceFormat = Texture->Source.GetFormat();
            if (SizeX <= 0 || SizeY <= 0 ||
                (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_BGRE8 && SourceFormat != TSF_G8 && SourceFormat != TSF_G16))
            {
                return;
            }

            bFlipGreenChannel = Texture->bFlipGreenChannel;
            MipData = Texture->Source.LockMipReadOnly(0);
        }

        ~FWetWrinkleNormalSource()
        {
            if (Texture != nullptr && MipData != nullptr)
            {
                Texture->Source.UnlockMip(0);
            }
        }

        bool IsValid() const
        {
            return Texture != nullptr && MipData != nullptr && SizeX > 0 && SizeY > 0;
        }

        FVector SampleNormalTS(const FVector2D& UV) const
        {
            if (!IsValid())
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            if (SourceFormat == TSF_G8)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            if (SourceFormat == TSF_G16)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            // Match Texture2DSampleLevel(..., 0) in the preview material. Nearest-neighbor
            // source reads made a 1010px preset visibly stair-step after it was stamped into
            // the larger baked map.
            const float SampleX = FMath::Clamp(static_cast<float>(UV.X), 0.0f, 1.0f) * static_cast<float>(SizeX - 1);
            const float SampleY = FMath::Clamp(static_cast<float>(UV.Y), 0.0f, 1.0f) * static_cast<float>(SizeY - 1);
            const int32 X0 = FMath::FloorToInt(SampleX);
            const int32 Y0 = FMath::FloorToInt(SampleY);
            const int32 X1 = FMath::Min(X0 + 1, SizeX - 1);
            const int32 Y1 = FMath::Min(Y0 + 1, SizeY - 1);
            const float FracX = SampleX - static_cast<float>(X0);
            const float FracY = SampleY - static_cast<float>(Y0);

            const FColor* ColorData = reinterpret_cast<const FColor*>(MipData);
            const FColor& Color00 = ColorData[Y0 * SizeX + X0];
            const FColor& Color10 = ColorData[Y0 * SizeX + X1];
            const FColor& Color01 = ColorData[Y1 * SizeX + X0];
            const FColor& Color11 = ColorData[Y1 * SizeX + X1];
            const auto BilinearChannel = [FracX, FracY](const uint8 C00, const uint8 C10, const uint8 C01, const uint8 C11)
            {
                return FMath::Lerp(
                    FMath::Lerp(static_cast<float>(C00), static_cast<float>(C10), FracX),
                    FMath::Lerp(static_cast<float>(C01), static_cast<float>(C11), FracX),
                    FracY) / 255.0f;
            };

            const float DecodedX = BilinearChannel(Color00.R, Color10.R, Color01.R, Color11.R) * 2.0f - 1.0f;
            float DecodedY = -(BilinearChannel(Color00.G, Color10.G, Color01.G, Color11.G) * 2.0f - 1.0f);
            if (bFlipGreenChannel)
            {
                DecodedY = -DecodedY;
            }

            FVector DecodedNormal(
                DecodedX,
                DecodedY,
                BilinearChannel(Color00.B, Color10.B, Color01.B, Color11.B) * 2.0f - 1.0f);
            if (DecodedNormal.Z <= UE_SMALL_NUMBER)
            {
                const float XYLengthSq = FMath::Min(DecodedNormal.X * DecodedNormal.X + DecodedNormal.Y * DecodedNormal.Y, 1.0f);
                DecodedNormal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
            }

            return DecodedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        }

        UTexture2D* Texture = nullptr;
        const uint8* MipData = nullptr;
        int32 SizeX = 0;
        int32 SizeY = 0;
        ETextureSourceFormat SourceFormat = TSF_Invalid;
        bool bFlipGreenChannel = false;
    };

    float WrapUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    FVector2D WrapUV(const FVector2D& UV)
    {
        return FVector2D(WrapUnit(UV.X), WrapUnit(UV.Y));
    }

    float WrappedDelta(float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    float SmoothStep(float Edge0, float Edge1, float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }

        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    FColor EncodeNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    FColor EncodeNormalWithCoverage(const FVector& Normal, const float Coverage)
    {
        FColor EncodedNormal = EncodeNormal(Normal);
        EncodedNormal.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Coverage, 0.0f, 1.0f) * 255.0f));
        return EncodedNormal;
    }

    uint8 EncodeUnit(float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    float SignedTriangleArea2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
    }

    bool BuildIslandMask(
        const UWetClothingAsset& WetClothingAsset,
        const int32 LODIndex,
        const int32 MaterialSlotIndex,
        const int32 UVChannelIndex,
        const int32 Width,
        const int32 Height,
        TArray<uint8>& OutIslandMask)
    {
        OutIslandMask.Init(0, Width * Height);
        if (WetClothingAsset.GetDWCSkeletalMesh() == nullptr || Width <= 0 || Height <= 0)
        {
            return false;
        }

        TArray<FWetClothingAssetUVIsland> Islands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                WetClothingAsset.GetDWCSkeletalMesh(),
                LODIndex,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                nullptr))
        {
            return false;
        }

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                const FVector2D WrappedUV0 = WrapUV(Triangle.UVs[0]);
                const FVector2D WrappedUV1 = WrappedUV0 + FVector2D(
                    WrappedDelta(Triangle.UVs[1].X - Triangle.UVs[0].X),
                    WrappedDelta(Triangle.UVs[1].Y - Triangle.UVs[0].Y));
                const FVector2D WrappedUV2 = WrappedUV0 + FVector2D(
                    WrappedDelta(Triangle.UVs[2].X - Triangle.UVs[0].X),
                    WrappedDelta(Triangle.UVs[2].Y - Triangle.UVs[0].Y));

                for (int32 TileY = -1; TileY <= 1; ++TileY)
                {
                    for (int32 TileX = -1; TileX <= 1; ++TileX)
                    {
                        const FVector2D TileOffset(static_cast<float>(TileX), static_cast<float>(TileY));
                        const FVector2D A = WrappedUV0 + TileOffset;
                        const FVector2D B = WrappedUV1 + TileOffset;
                        const FVector2D C = WrappedUV2 + TileOffset;
                        const float TriangleArea = SignedTriangleArea2D(A, B, C);
                        if (FMath::Abs(TriangleArea) <= UE_SMALL_NUMBER)
                        {
                            continue;
                        }

                        const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X) * Width), 0, Width - 1);
                        const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X) * Width), 0, Width - 1);
                        const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);
                        const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y) * Height), 0, Height - 1);
                        for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
                        {
                            for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                            {
                                const FVector2D PixelUV(
                                    (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(Width),
                                    (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(Height));
                                const float EdgeAB = SignedTriangleArea2D(A, B, PixelUV);
                                const float EdgeBC = SignedTriangleArea2D(B, C, PixelUV);
                                const float EdgeCA = SignedTriangleArea2D(C, A, PixelUV);
                                const bool bInside = TriangleArea > 0.0f
                                    ? EdgeAB >= -UE_SMALL_NUMBER && EdgeBC >= -UE_SMALL_NUMBER && EdgeCA >= -UE_SMALL_NUMBER
                                    : EdgeAB <= UE_SMALL_NUMBER && EdgeBC <= UE_SMALL_NUMBER && EdgeCA <= UE_SMALL_NUMBER;
                                if (bInside)
                                {
                                    OutIslandMask[PixelY * Width + PixelX] = 1;
                                }
                            }
                        }
                    }
                }
            }
        }

        return OutIslandMask.Contains(1);
    }


    void DilateNormalCoverageIntoIslandPadding(
        TArray<FVector>& InOutNormalBuffer,
        TArray<float>& InOutCoverageBuffer,
        const TArray<uint8>& IslandMask,
        const int32 Width,
        const int32 Height,
        const int32 PaddingPixels)
    {
        const int32 ClampedPadding = FMath::Clamp(PaddingPixels, 0, 64);
        if (ClampedPadding <= 0 || Width <= 0 || Height <= 0 ||
            InOutNormalBuffer.Num() != Width * Height ||
            InOutCoverageBuffer.Num() != Width * Height ||
            IslandMask.Num() != Width * Height)
        {
            return;
        }

        TArray<uint8> CurrentMask = IslandMask;
        for (int32 Iteration = 0; Iteration < ClampedPadding; ++Iteration)
        {
            TArray<FVector> NextNormalBuffer = InOutNormalBuffer;
            TArray<float> NextCoverageBuffer = InOutCoverageBuffer;
            TArray<uint8> NextMask = CurrentMask;
            bool bChanged = false;

            for (int32 PixelY = 0; PixelY < Height; ++PixelY)
            {
                for (int32 PixelX = 0; PixelX < Width; ++PixelX)
                {
                    const int32 PixelIndex = PixelY * Width + PixelX;
                    if (CurrentMask[PixelIndex] != 0)
                    {
                        continue;
                    }

                    int32 BestNeighborIndex = INDEX_NONE;
                    float BestNeighborCoverage = -1.0f;
                    for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                    {
                        for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                        {
                            if (OffsetX == 0 && OffsetY == 0)
                            {
                                continue;
                            }

                            const int32 NeighborX = PixelX + OffsetX;
                            const int32 NeighborY = PixelY + OffsetY;
                            if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
                            {
                                continue;
                            }

                            const int32 NeighborIndex = NeighborY * Width + NeighborX;
                            if (CurrentMask[NeighborIndex] == 0)
                            {
                                continue;
                            }

                            const float NeighborCoverage = InOutCoverageBuffer[NeighborIndex];
                            if (NeighborCoverage > BestNeighborCoverage)
                            {
                                BestNeighborCoverage = NeighborCoverage;
                                BestNeighborIndex = NeighborIndex;
                            }
                        }
                    }

                    if (BestNeighborIndex != INDEX_NONE)
                    {
                        NextNormalBuffer[PixelIndex] = InOutNormalBuffer[BestNeighborIndex];
                        NextCoverageBuffer[PixelIndex] = InOutCoverageBuffer[BestNeighborIndex];
                        NextMask[PixelIndex] = 1;
                        bChanged = true;
                    }
                }
            }

            if (!bChanged)
            {
                break;
            }

            InOutNormalBuffer = MoveTemp(NextNormalBuffer);
            InOutCoverageBuffer = MoveTemp(NextCoverageBuffer);
            CurrentMask = MoveTemp(NextMask);
        }
    }

    void EncodeNormalCoveragePixels(
        const TArray<FVector>& NormalBuffer,
        const TArray<float>& CoverageBuffer,
        TArray<FColor>& OutNormalPixels)
    {
        const int32 PixelCount = NormalBuffer.Num();
        OutNormalPixels.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const float Coverage = CoverageBuffer.IsValidIndex(PixelIndex) ? CoverageBuffer[PixelIndex] : 0.0f;
            OutNormalPixels[PixelIndex] = EncodeNormalWithCoverage(NormalBuffer[PixelIndex], Coverage);
        }
    }

    void EncodeCoverageMaskPixels(const TArray<float>& CoverageBuffer, TArray<FColor>& OutMaskPixels)
    {
        const int32 PixelCount = CoverageBuffer.Num();
        OutMaskPixels.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            const uint8 MaskValue = EncodeUnit(CoverageBuffer[PixelIndex]);
            OutMaskPixels[PixelIndex] = FColor(MaskValue, MaskValue, MaskValue, 255);
        }
    }
}

struct FWetWrinkleNormalMapBaker::FBakeGroup
{
    int32 LODIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    UTexture* SourceTexture = nullptr;
    TArray<const FWetWrinklePatchPlacement*> Stamps;
    TArray<const FWetProceduralRidgeStroke*> ProceduralRidgeStrokes;
};

bool FWetWrinkleNormalMapBaker::BakeMaterialSlot(
    UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FWetWrinkleNormalMapBakeResult();

    if (WetClothingAsset == nullptr)
    {
        OutErrorMessage = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a material slot before baking a wrinkle normal map.");
        return false;
    }

    const int32 WrinkleUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();

    TSet<int32> TargetSlots;
    TargetSlots.Add(MaterialSlotIndex);

    TArray<int32> LODIndices = WetClothingAsset->WrinkleData.BakeSettings.TargetLODIndices;
    if (LODIndices.Num() == 0)
    {
        LODIndices.Add(0);
    }

    int32 MatchingSlotUVStampCount = 0;
    int32 MissingPresetStampCount = 0;
    int32 MissingCorrectedNormalStampCount = 0;
    int32 MissingSourceTextureStampCount = 0;
    int32 MatchingProceduralStrokeCount = 0;
    int32 InvalidProceduralStrokeCount = 0;

    for (const int32 LODIndex : LODIndices)
    {
        FBakeGroup Group;
        Group.LODIndex = FMath::Max(0, LODIndex);
        Group.MaterialSlotIndex = MaterialSlotIndex;
        Group.UVChannelIndex = WrinkleUVChannelIndex;

        for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset->WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes)
            {
                continue;
            }

            for (const FWetWrinklePatchPlacement& Stamp : Stroke.PatchPlacements)
            {
                if (Stamp.MaterialSlotIndex != MaterialSlotIndex ||
                    Stamp.UVChannelIndex != Group.UVChannelIndex)
                {
                    continue;
                }

                ++MatchingSlotUVStampCount;

                if (Stamp.WrinklePreset == nullptr)
                {
                    ++MissingPresetStampCount;
                    continue;
                }

                if (Stamp.WrinklePreset->GetNormalTextureForBrush() == nullptr)
                {
                    ++MissingCorrectedNormalStampCount;
                    continue;
                }

                if (Stamp.SourceTexture == nullptr)
                {
                    ++MissingSourceTextureStampCount;
                    continue;
                }

                if (Group.SourceTexture == nullptr)
                {
                    Group.SourceTexture = Stamp.SourceTexture;
                }

                if (Group.SourceTexture == Stamp.SourceTexture)
                {
                    Group.Stamps.Add(&Stamp);
                }
            }
        }

        for (const FWetProceduralRidgeStroke& Stroke : WetClothingAsset->WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes) ||
                Stroke.MaterialSlotIndex != MaterialSlotIndex ||
                Stroke.UVChannelIndex != Group.UVChannelIndex ||
                Stroke.LODIndex != Group.LODIndex)
            {
                continue;
            }

            ++MatchingProceduralStrokeCount;
            if (Stroke.Points.Num() < 2 || Stroke.WidthUV <= 0.0f || Stroke.Strength <= 0.0f)
            {
                ++InvalidProceduralStrokeCount;
                continue;
            }

            Group.ProceduralRidgeStrokes.Add(&Stroke);
        }

        if (Group.SourceTexture == nullptr && Group.ProceduralRidgeStrokes.Num() > 0)
        {
            Group.SourceTexture = FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(
                WetClothingAsset,
                Group.MaterialSlotIndex,
                Group.UVChannelIndex);
        }

        if ((Group.Stamps.Num() > 0 || Group.ProceduralRidgeStrokes.Num() > 0) &&
            !BakeGroup(*WetClothingAsset, Group, Settings, OutResult, OutErrorMessage))
        {
            return false;
        }
    }

    if (OutResult.BakedMapCount == 0)
    {
        if (MatchingSlotUVStampCount == 0 && MatchingProceduralStrokeCount == 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("No wrinkle patches or procedural ridge strokes were found for the selected material slot on UV channel %d."),
                WrinkleUVChannelIndex);
        }
        else if (MissingPresetStampCount > 0 || MissingCorrectedNormalStampCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wrinkle patches were found, but none could be baked. Missing presets: %d, missing generated corrected normals: %d. Rebuild Wet Wrinkle Presets before baking."),
                MissingPresetStampCount,
                MissingCorrectedNormalStampCount);
        }
        else if (MissingSourceTextureStampCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wrinkle patches were found, but %d patch(es) are missing source texture mapping data."),
                MissingSourceTextureStampCount);
        }
        else if (InvalidProceduralStrokeCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Procedural ridge strokes were found, but %d stroke(s) do not contain a bakeable centerline, width, or strength."),
                InvalidProceduralStrokeCount);
        }
        else if (MatchingProceduralStrokeCount > 0)
        {
            OutErrorMessage = TEXT("Procedural ridge strokes were found, but the material slot has no source texture mapping data for the bake canvas.");
        }
        else
        {
            OutErrorMessage = TEXT("The selected material slot did not receive any wrinkle pixels during bake.");
        }
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

bool FWetWrinkleNormalMapBaker::BakeGroup(
    UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeResult& InOutResult,
    FString& OutErrorMessage)
{
    if (!Settings.bBakeNormalMap && !Settings.bBakeMask)
    {
        OutErrorMessage = TEXT("No wrinkle bake outputs are enabled.");
        return false;
    }

    if (Group.SourceTexture == nullptr || (Group.Stamps.Num() == 0 && Group.ProceduralRidgeStrokes.Num() == 0))
    {
        return true;
    }

    const int32 MaxResolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    const int32 SourceWidth = FMath::Max(Group.SourceTexture->GetSurfaceWidth(), 1);
    const int32 SourceHeight = FMath::Max(Group.SourceTexture->GetSurfaceHeight(), 1);
    const double ResolutionScale = static_cast<double>(MaxResolution) / FMath::Max(SourceWidth, SourceHeight);
    const int32 Width = FMath::Clamp(FMath::RoundToInt(SourceWidth * ResolutionScale), 1, 8192);
    const int32 Height = FMath::Clamp(FMath::RoundToInt(SourceHeight * ResolutionScale), 1, 8192);

    TArray<FVector> NormalBuffer;
    TArray<float> CoverageBuffer;
    NormalBuffer.Init(FVector(0.0f, 0.0f, 1.0f), Width * Height);
    CoverageBuffer.Init(0.0f, Width * Height);

    int32 BakedStampCount = 0;
    TMap<const UWetWrinklePreset*, FWetWrinklePresetScalarBuffer> SeparationSources;
    for (const FWetWrinklePatchPlacement* StampPtr : Group.Stamps)
    {
        const FWetWrinklePatchPlacement& Stamp = *StampPtr;
        UTexture2D* CorrectedNormalTexture = Stamp.WrinklePreset != nullptr ? Stamp.WrinklePreset->GetNormalTextureForBrush() : nullptr;
        FWetWrinkleNormalSource NormalSource(CorrectedNormalTexture);
        if (!NormalSource.IsValid() || Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            continue;
        }

        const UWetWrinklePreset* Preset = Stamp.WrinklePreset.Get();
        FWetWrinklePresetScalarBuffer* SeparationSource = SeparationSources.Find(Preset);
        if (SeparationSource == nullptr)
        {
            FWetWrinklePresetScalarBuffer NewSeparationSource;
            FString SeparationError;
            if (!FWetWrinklePresetBuilder::BuildConvexSeparationBuffer(
                    CorrectedNormalTexture,
                    Preset->SeparationSettings,
                    NewSeparationSource,
                    SeparationError))
            {
                UE_LOG(LogTemp, Warning, TEXT("DWC wrinkle bake skipped preset '%s': %s"), *GetNameSafe(Preset), *SeparationError);
                continue;
            }
            SeparationSource = &SeparationSources.Add(Preset, MoveTemp(NewSeparationSource));
        }

        ++BakedStampCount;

        const FVector2D WrappedCenter = WrapUV(Stamp.PositionUV);
        const FVector2D SafeScale(
            FMath::Max(FMath::Abs(Stamp.Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Stamp.Scale.Y), UE_SMALL_NUMBER));
        const float EdgeFadeStart = FMath::Clamp(1.0f - Stamp.Falloff, 0.0f, 0.98f);
        const float CosRotation = FMath::Cos(Stamp.RotationRadians);
        const float SinRotation = FMath::Sin(Stamp.RotationRadians);

        for (int32 TileOffsetY = -1; TileOffsetY <= 1; ++TileOffsetY)
        {
            for (int32 TileOffsetX = -1; TileOffsetX <= 1; ++TileOffsetX)
            {
                const FVector2D TileCenter = WrappedCenter + FVector2D(static_cast<float>(TileOffsetX), static_cast<float>(TileOffsetY));
                const int32 MinX = FMath::Clamp(FMath::FloorToInt((TileCenter.X - Stamp.BrushRadiusUV) * Width), 0, Width - 1);
                const int32 MaxX = FMath::Clamp(FMath::CeilToInt((TileCenter.X + Stamp.BrushRadiusUV) * Width), 0, Width - 1);
                const int32 MinY = FMath::Clamp(FMath::FloorToInt((TileCenter.Y - Stamp.BrushRadiusUV) * Height), 0, Height - 1);
                const int32 MaxY = FMath::Clamp(FMath::CeilToInt((TileCenter.Y + Stamp.BrushRadiusUV) * Height), 0, Height - 1);
                if (MinX > MaxX || MinY > MaxY)
                {
                    continue;
                }

                for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
                {
                    for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                    {
                        const FVector2D PixelUV(
                            (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(Width),
                            (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(Height));
                        const FVector2D DeltaUV(
                            WrappedDelta(PixelUV.X - TileCenter.X),
                            WrappedDelta(PixelUV.Y - TileCenter.Y));
                        const FVector2D Local = DeltaUV / FMath::Max(Stamp.BrushRadiusUV, UE_SMALL_NUMBER);
                        const float DistanceFromCenter = Local.Size();
                        if (DistanceFromCenter > 1.0f)
                        {
                            continue;
                        }

                        const float EdgeFade = 1.0f - SmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
                        if (EdgeFade <= UE_SMALL_NUMBER)
                        {
                            continue;
                        }

                        const float BrushLocalX = (CosRotation * Local.X + SinRotation * Local.Y) / SafeScale.X;
                        const float BrushLocalY = (-SinRotation * Local.X + CosRotation * Local.Y) / SafeScale.Y;
                        if (FMath::Abs(BrushLocalX) > 1.0f || FMath::Abs(BrushLocalY) > 1.0f)
                        {
                            continue;
                        }

                        const FVector2D BrushTextureUV(BrushLocalX * 0.5f + 0.5f, BrushLocalY * 0.5f + 0.5f);
                        const FVector BrushNormalTS = NormalSource.SampleNormalTS(BrushTextureUV);
                        const float SeparationCoverage = SeparationSource->SampleBilinear(BrushTextureUV);
                        const FVector RotatedBrushNormalTS(
                            BrushNormalTS.X * CosRotation - BrushNormalTS.Y * SinRotation,
                            BrushNormalTS.X * SinRotation + BrushNormalTS.Y * CosRotation,
                            BrushNormalTS.Z);
                        const float StrengthScale = FMath::Max(Stamp.Strength * EdgeFade, 0.0f);
                        const FVector StampNormalTS =
                            FVector(
                                RotatedBrushNormalTS.X * StrengthScale,
                                RotatedBrushNormalTS.Y * StrengthScale,
                                RotatedBrushNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));

                        const int32 PixelIndex = PixelY * Width + PixelX;
                        const FVector ExistingNormalTS = NormalBuffer[PixelIndex];
                        const FVector BlendedNormalTS =
                            FVector(
                                ExistingNormalTS.X + StampNormalTS.X,
                                ExistingNormalTS.Y + StampNormalTS.Y,
                                ExistingNormalTS.Z * StampNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
                        NormalBuffer[PixelIndex] = BlendedNormalTS;

                        const float Coverage = FMath::Max(CoverageBuffer[PixelIndex], EdgeFade * SeparationCoverage);
                        CoverageBuffer[PixelIndex] = Coverage;
                    }
                }
            }
        }
    }

    int32 BakedProceduralStrokeCount = 0;
    for (const FWetProceduralRidgeStroke* Stroke : Group.ProceduralRidgeStrokes)
    {
        if (Stroke == nullptr)
        {
            continue;
        }

        const FWetProceduralRidgeRasterResult RasterResult =
            FWetProceduralRidgeRasterizer::RasterizeToNormalCoverageBuffers(
                *Stroke,
                FIntPoint(Width, Height),
                NormalBuffer,
                CoverageBuffer);
        if (RasterResult.bAffectedPixels)
        {
            ++BakedProceduralStrokeCount;
        }
    }

    if (BakedStampCount == 0 && BakedProceduralStrokeCount == 0)
    {
        return true;
    }

    // Keep the extracted convex core narrow. Bilinear preset sampling supplies the
    // short antialiased transition; no semantic radius or feather is added around it.
    // Padding below only copies edge texels outside UV islands for seam-safe sampling.
    TArray<uint8> IslandMask;
    if (BuildIslandMask(
            WetClothingAsset,
            Group.LODIndex,
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            Width,
            Height,
            IslandMask))
    {
        DilateNormalCoverageIntoIslandPadding(
            NormalBuffer,
            CoverageBuffer,
            IslandMask,
            Width,
            Height,
            Settings.PaddingPixels);
    }

    TArray<FColor> NormalPixels;
    TArray<FColor> MaskPixels;
    EncodeNormalCoveragePixels(NormalBuffer, CoverageBuffer, NormalPixels);
    EncodeCoverageMaskPixels(CoverageBuffer, MaskPixels);

    const FString BaseSuffix = FString::Printf(
        TEXT("Slot%d_UV%d_LOD%d"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex);

    UTexture2D* NormalTexture = nullptr;
    if (Settings.bBakeNormalMap)
    {
        NormalTexture = CreateOrUpdateTextureAsset(
            WetClothingAsset,
            BaseSuffix + TEXT("_WrinkleNormalMap"),
            Width,
            Height,
            NormalPixels,
            true,
            OutErrorMessage);
        if (NormalTexture == nullptr)
        {
            return false;
        }
    }

    UTexture2D* MaskTexture = nullptr;
    if (Settings.bBakeMask)
    {
        MaskTexture = CreateOrUpdateTextureAsset(
            WetClothingAsset,
            BaseSuffix + TEXT("_WrinkleMask"),
            Width,
            Height,
            MaskPixels,
            false,
            OutErrorMessage);
        if (MaskTexture == nullptr)
        {
            return false;
        }
    }

    WetClothingAsset.Modify();
    FWetWrinkleBakedMapSet* BakedMap = WetClothingAsset.WrinkleData.BakedWrinkleMaps.FindByPredicate(
        [&Group](const FWetWrinkleBakedMapSet& ExistingMap)
        {
            return ExistingMap.LODIndex == Group.LODIndex &&
                   ExistingMap.MaterialSlotIndex == Group.MaterialSlotIndex &&
                   ExistingMap.UVChannelIndex == Group.UVChannelIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &WetClothingAsset.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    }

    BakedMap->LODIndex = Group.LODIndex;
    BakedMap->MaterialSlotIndex = Group.MaterialSlotIndex;
    BakedMap->UVChannelIndex = Group.UVChannelIndex;
    if (Settings.bBakeNormalMap)
    {
        BakedMap->BakedWrinkleNormalMap = NormalTexture;
    }
    if (Settings.bBakeMask)
    {
        BakedMap->BakedWrinkleMask = MaskTexture;
    }
    BakedMap->Resolution = MaxResolution;
    BakedMap->PaddingPixels = FMath::Clamp(Settings.PaddingPixels, 0, 64);
    BakedMap->bHasCoverageAlpha = Settings.bBakeNormalMap && NormalTexture != nullptr;
    BakedMap->AlphaSemantic = NormalTexture != nullptr
        ? EDWCWrinkleAlphaSemantic::ConvexSeparation
        : EDWCWrinkleAlphaSemantic::None;
    BakedMap->AlphaBuildVersion = NormalTexture != nullptr ? 4 : 0;
    BakedMap->BuildSignature = MakeBuildSignature(WetClothingAsset, Group, Width, Height, Settings);
    BakedMap->BakeGuid = FGuid::NewGuid();
    WetClothingAsset.MarkPackageDirty();

    InOutResult.BakedMapCount++;
    InOutResult.BakedStampCount += BakedStampCount;
    InOutResult.BakedProceduralStrokeCount += BakedProceduralStrokeCount;
    if (NormalTexture != nullptr)
    {
        InOutResult.BakedNormalMaps.Add(NormalTexture);
        InOutResult.bBakedCoverageAlpha = true;
    }
    if (MaskTexture != nullptr)
    {
        InOutResult.BakedMasks.Add(MaskTexture);
    }

    OutErrorMessage.Reset();
    return true;
}

FString FWetWrinkleNormalMapBaker::MakeBuildSignature(
    const UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const int32 Width,
    const int32 Height,
    const FWetWrinkleNormalMapBakeSettings& Settings)
{
    FString Canonical;
    Canonical.Reserve(4096);
    Canonical += TEXT("DWC.WrinkleNormalMap.v15.FlaredEndpoint|");
    Canonical += WetClothingAsset.GetPathName();
    Canonical += FString::Printf(
        TEXT("|Slot=%d|UV=%d|LOD=%d|Size=%dx%d|Padding=%d|Source=%s"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        Width,
        Height,
        FMath::Clamp(Settings.PaddingPixels, 0, 64),
        *GetPathNameSafe(Group.SourceTexture));

    for (const FWetWrinklePatchPlacement* Stamp : Group.Stamps)
    {
        const UWetWrinklePreset* Preset = Stamp != nullptr ? Stamp->WrinklePreset.Get() : nullptr;
        Canonical += FString::Printf(
            TEXT("|Stamp:%s;UV=%.9g,%.9g;Radius=%.9g;Rot=%.9g;Scale=%.9g,%.9g;Strength=%.9g;Falloff=%.9g;Preset=%s;PresetBuild=%s;SepBlur=%d;SepThreshold=%.9g;SepMinComponent=%d;SepInvert=%d"),
            *Stamp->PatchGuid.ToString(EGuidFormats::Digits),
            Stamp->PositionUV.X,
            Stamp->PositionUV.Y,
            Stamp->BrushRadiusUV,
            Stamp->RotationRadians,
            Stamp->Scale.X,
            Stamp->Scale.Y,
            Stamp->Strength,
            Stamp->Falloff,
            *GetPathNameSafe(Preset),
            Preset != nullptr ? *Preset->BuildSignature : TEXT(""),
            Preset != nullptr ? Preset->SeparationSettings.InputBlurRadiusPixels : 0,
            Preset != nullptr ? Preset->SeparationSettings.ConvexityThreshold : 0.0f,
            Preset != nullptr ? Preset->SeparationSettings.MinimumComponentPixels : 0,
            Preset != nullptr && Preset->SeparationSettings.bInvertConvexity ? 1 : 0);
    }

    for (const FWetProceduralRidgeStroke* Stroke : Group.ProceduralRidgeStrokes)
    {
        if (Stroke == nullptr)
        {
            continue;
        }

        Canonical += FString::Printf(
            TEXT("|Ridge:%s;Enabled=%d;Slot=%d;UV=%d;LOD=%d;Shape=%d;FlipFold=%d;Width=%.9g;Strength=%.9g;Falloff=%.9g;StartTaper=%.9g;EndTaper=%.9g;Flare=%.9g,%.9g,%.9g,%.9g;Variation=%d,%.9g,%.9g,%.9g,%.9g,%d;StartMode=%d;StartLink=%s,%d,%.9g;EndMode=%d;EndLink=%s,%d,%.9g;Points=%d"),
            *Stroke->StrokeGuid.ToString(EGuidFormats::Digits),
            Stroke->bEnabled ? 1 : 0,
            Stroke->MaterialSlotIndex,
            Stroke->UVChannelIndex,
            Stroke->LODIndex,
            static_cast<int32>(Stroke->Shape),
            Stroke->bFlipFoldSide ? 1 : 0,
            Stroke->WidthUV,
            Stroke->Strength,
            Stroke->Falloff,
            Stroke->StartTaper,
            Stroke->EndTaper,
            Stroke->FlareSettings.Length,
            Stroke->FlareSettings.WidthScale,
            Stroke->FlareSettings.EndStrength,
            Stroke->FlareSettings.Softness,
            Stroke->NaturalVariation.bEnabled ? 1 : 0,
            Stroke->NaturalVariation.CenterlineAmount,
            Stroke->NaturalVariation.CenterlineFrequency,
            Stroke->NaturalVariation.WidthVariation,
            Stroke->NaturalVariation.WidthFrequency,
            Stroke->NaturalVariation.NoiseSeed,
            static_cast<int32>(Stroke->StartEndpoint.Mode),
            *Stroke->StartEndpoint.ConnectedStrokeGuid.ToString(EGuidFormats::Digits),
            Stroke->StartEndpoint.ConnectedSegmentIndex,
            Stroke->StartEndpoint.ConnectedSegmentT,
            static_cast<int32>(Stroke->EndEndpoint.Mode),
            *Stroke->EndEndpoint.ConnectedStrokeGuid.ToString(EGuidFormats::Digits),
            Stroke->EndEndpoint.ConnectedSegmentIndex,
            Stroke->EndEndpoint.ConnectedSegmentT,
            Stroke->Points.Num());
        for (const FWetProceduralRidgeStrokePoint& Point : Stroke->Points)
        {
            Canonical += FString::Printf(
                TEXT(";Point=%.9g,%.9g,%d,%.9g,%.9g,%.9g"),
                Point.PositionUV.X,
                Point.PositionUV.Y,
                Point.AnchorTriangleID,
                Point.AnchorBarycentric.X,
                Point.AnchorBarycentric.Y,
                Point.AnchorBarycentric.Z);
        }
    }

    return FMD5::HashAnsiString(*Canonical);
}

UTexture2D* FWetWrinkleNormalMapBaker::CreateOrUpdateTextureAsset(
    UWetClothingAsset& WetClothingAsset,
    const FString& ObjectSuffix,
    const int32 Width,
    const int32 Height,
    const TArray<FColor>& Pixels,
    const bool bNormalMap,
    FString& OutErrorMessage)
{
#if WITH_EDITORONLY_DATA
    const FString AssetPackageName = WetClothingAsset.GetOutermost()->GetName();
    const FString WcaFolder = FPackageName::GetLongPackagePath(AssetPackageName);
    const FString PackagePath = WcaFolder / TEXT("Generated") / WetClothingAsset.GetName() / TEXT("Maps") / TEXT("Wrinkles");
    if (PackagePath.IsEmpty())
    {
        OutErrorMessage = TEXT("Could not resolve a package path for the Wet Clothing Asset.");
        return nullptr;
    }

    const FString ObjectName = ObjectTools::SanitizeObjectName(
        FString::Printf(TEXT("T_%s_%s"), *WetClothingAsset.GetName(), *ObjectSuffix));
    const FString TexturePackageName = PackagePath / ObjectName;
    const FString TextureObjectPath = TexturePackageName + TEXT(".") + ObjectName;

    UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TextureObjectPath);
    if (Texture == nullptr)
    {
        UPackage* Package = CreatePackage(*TexturePackageName);
        if (Package == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to create package '%s'."), *TexturePackageName);
            return nullptr;
        }

        Texture = NewObject<UTexture2D>(Package, *ObjectName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Texture);
    }
    else
    {
        Texture->Modify();
    }

    Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    Texture->SRGB = false;
    // Baked wrinkle normal maps pack coverage into alpha; TC_Normalmap/BC5 would drop it.
    Texture->CompressionSettings = bNormalMap ? TC_VectorDisplacementmap : TC_Grayscale;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    Texture->UpdateResource();
    Texture->MarkPackageDirty();

    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wrinkle normal map baking requires editor-only texture source data.");
    return nullptr;
#endif
}
