#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleNormalTextureBuilder.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCWrinkleBake, Log, All);

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
        TArray<FVector3f>& InOutNormalBuffer,
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

        constexpr uint8 UnvisitedDistance = MAX_uint8;
        TArray<uint8> DistanceFromIsland;
        DistanceFromIsland.Init(UnvisitedDistance, Width * Height);

        TArray<int32> CurrentFrontier;
        CurrentFrontier.Reserve(Width + Height);
        for (int32 PixelY = 0; PixelY < Height; ++PixelY)
        {
            for (int32 PixelX = 0; PixelX < Width; ++PixelX)
            {
                const int32 PixelIndex = PixelY * Width + PixelX;
                if (IslandMask[PixelIndex] == 0)
                {
                    continue;
                }

                DistanceFromIsland[PixelIndex] = 0;
                bool bIsBoundaryPixel = false;
                for (int32 OffsetY = -1; OffsetY <= 1 && !bIsBoundaryPixel; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        if (OffsetX == 0 && OffsetY == 0)
                        {
                            continue;
                        }

                        const int32 NeighborX = PixelX + OffsetX;
                        const int32 NeighborY = PixelY + OffsetY;
                        bIsBoundaryPixel = NeighborX >= 0 && NeighborX < Width &&
                            NeighborY >= 0 && NeighborY < Height &&
                            IslandMask[NeighborY * Width + NeighborX] == 0;
                        if (bIsBoundaryPixel)
                        {
                            break;
                        }
                    }
                }

                if (bIsBoundaryPixel)
                {
                    CurrentFrontier.Add(PixelIndex);
                }
            }
        }

        TArray<int32> NextFrontier;
        NextFrontier.Reserve(CurrentFrontier.Num());
        for (int32 Iteration = 1; Iteration <= ClampedPadding && CurrentFrontier.Num() > 0; ++Iteration)
        {
            NextFrontier.Reset();
            const uint8 NextDistance = static_cast<uint8>(Iteration);

            for (const int32 SourceIndex : CurrentFrontier)
            {
                const int32 SourceX = SourceIndex % Width;
                const int32 SourceY = SourceIndex / Width;
                for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                {
                    for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                    {
                        if (OffsetX == 0 && OffsetY == 0)
                        {
                            continue;
                        }

                        const int32 NeighborX = SourceX + OffsetX;
                        const int32 NeighborY = SourceY + OffsetY;
                        if (NeighborX < 0 || NeighborX >= Width || NeighborY < 0 || NeighborY >= Height)
                        {
                            continue;
                        }

                        const int32 NeighborIndex = NeighborY * Width + NeighborX;
                        if (IslandMask[NeighborIndex] != 0 || DistanceFromIsland[NeighborIndex] < NextDistance)
                        {
                            continue;
                        }

                        if (DistanceFromIsland[NeighborIndex] == UnvisitedDistance)
                        {
                            DistanceFromIsland[NeighborIndex] = NextDistance;
                            InOutNormalBuffer[NeighborIndex] = InOutNormalBuffer[SourceIndex];
                            InOutCoverageBuffer[NeighborIndex] = InOutCoverageBuffer[SourceIndex];
                            NextFrontier.Add(NeighborIndex);
                        }
                        else if (DistanceFromIsland[NeighborIndex] == NextDistance &&
                                 InOutCoverageBuffer[SourceIndex] > InOutCoverageBuffer[NeighborIndex])
                        {
                            InOutNormalBuffer[NeighborIndex] = InOutNormalBuffer[SourceIndex];
                            InOutCoverageBuffer[NeighborIndex] = InOutCoverageBuffer[SourceIndex];
                        }
                    }
                }
            }

            Swap(CurrentFrontier, NextFrontier);
        }
    }

    void EncodeNormalPixels(
        const TArray<FVector3f>& NormalBuffer,
        TArray<FColor>& OutNormalPixels)
    {
        const int32 PixelCount = NormalBuffer.Num();
        OutNormalPixels.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            OutNormalPixels[PixelIndex] = EncodeNormal(FVector(NormalBuffer[PixelIndex]));
        }
    }

    void EncodeCoverageMaskPixels(const TArray<float>& CoverageBuffer, TArray<uint8>& OutMaskPixels)
    {
        const int32 PixelCount = CoverageBuffer.Num();
        OutMaskPixels.SetNumUninitialized(PixelCount);
        for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
        {
            OutMaskPixels[PixelIndex] = EncodeUnit(CoverageBuffer[PixelIndex]);
        }
    }
}

struct FWetWrinkleNormalMapBaker::FBakeGroup
{
    int32 LODIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
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

    TArray<int32> LODIndices = WetClothingAsset->Authored.WrinkleData.BakeSettings.TargetLODIndices;
    if (LODIndices.Num() == 0)
    {
        LODIndices.Add(0);
    }

    int32 MatchingSlotUVStampCount = 0;
    int32 MissingNormalTextureStampCount = 0;
    int32 MatchingProceduralStrokeCount = 0;
    int32 InvalidProceduralStrokeCount = 0;

    for (const int32 LODIndex : LODIndices)
    {
        FBakeGroup Group;
        Group.LODIndex = FMath::Max(0, LODIndex);
        Group.MaterialSlotIndex = MaterialSlotIndex;
        Group.UVChannelIndex = WrinkleUVChannelIndex;

        for (const FWetWrinklePatchPlacement& Stamp : WetClothingAsset->Authored.WrinkleData.EditablePatches)
        {
            if (!Stamp.bEnabled && !Settings.bIncludeDisabledPatches)
            {
                continue;
            }

            if (Stamp.MaterialSlotIndex != MaterialSlotIndex ||
                Stamp.UVChannelIndex != Group.UVChannelIndex)
            {
                continue;
            }

            ++MatchingSlotUVStampCount;

            if (Stamp.WrinkleNormalTexture == nullptr)
            {
                ++MissingNormalTextureStampCount;
                continue;
            }

            Group.Stamps.Add(&Stamp);
        }

        for (const FWetProceduralRidgeStroke& Stroke : WetClothingAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !Settings.bIncludeDisabledPatches) ||
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
        else if (MissingNormalTextureStampCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wrinkle patches were found, but %d patch(es) do not reference a wrinkle normal texture."),
                MissingNormalTextureStampCount);
        }
        else if (InvalidProceduralStrokeCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Procedural ridge strokes were found, but %d stroke(s) do not contain a bakeable centerline, width, or strength."),
                InvalidProceduralStrokeCount);
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

bool FWetWrinkleNormalMapBaker::IsMaterialSlotBakeCurrent(
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = WetClothingAsset->Authored.WrinkleData.BakeSettings.DefaultResolution;
    Settings.PaddingPixels = WetClothingAsset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatches =
        WetClothingAsset->Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches;

    const int32 UVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
    TArray<int32> LODIndices = WetClothingAsset->Authored.WrinkleData.BakeSettings.TargetLODIndices;
    if (LODIndices.IsEmpty())
    {
        LODIndices.Add(0);
    }

    const FIntPoint TextureSize = WetWrinkleTextureRaster::ResolveFinalTextureSize(Settings.Resolution);
    bool bFoundBakeableGroup = false;
    for (const int32 RequestedLODIndex : LODIndices)
    {
        FBakeGroup Group;
        Group.LODIndex = FMath::Max(0, RequestedLODIndex);
        Group.MaterialSlotIndex = MaterialSlotIndex;
        Group.UVChannelIndex = UVChannelIndex;

        for (const FWetWrinklePatchPlacement& Stamp : WetClothingAsset->Authored.WrinkleData.EditablePatches)
        {
            if ((!Stamp.bEnabled && !Settings.bIncludeDisabledPatches) ||
                Stamp.MaterialSlotIndex != MaterialSlotIndex ||
                Stamp.UVChannelIndex != UVChannelIndex ||
                Stamp.WrinkleNormalTexture == nullptr)
            {
                continue;
            }
            Group.Stamps.Add(&Stamp);
        }

        for (const FWetProceduralRidgeStroke& Stroke :
             WetClothingAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            if ((!Stroke.bEnabled && !Settings.bIncludeDisabledPatches) ||
                Stroke.MaterialSlotIndex != MaterialSlotIndex ||
                Stroke.UVChannelIndex != UVChannelIndex ||
                Stroke.LODIndex != Group.LODIndex ||
                Stroke.Points.Num() < 2 ||
                Stroke.WidthUV <= 0.0f ||
                Stroke.Strength <= 0.0f)
            {
                continue;
            }
            Group.ProceduralRidgeStrokes.Add(&Stroke);
        }

        if (Group.Stamps.IsEmpty() && Group.ProceduralRidgeStrokes.IsEmpty())
        {
            continue;
        }
        bFoundBakeableGroup = true;

        const FWetWrinkleBakedMapSet* BakedMap =
            WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
                [&Group](const FWetWrinkleBakedMapSet& Candidate)
                {
                    return Candidate.MaterialSlotIndex == Group.MaterialSlotIndex &&
                           Candidate.UVChannelIndex == Group.UVChannelIndex &&
                           Candidate.LODIndex == Group.LODIndex;
                });
        if (BakedMap == nullptr ||
            BakedMap->BakedWrinkleNormalMap == nullptr ||
            BakedMap->BakedWrinkleMask == nullptr ||
            BakedMap->Resolution != TextureSize.X ||
            BakedMap->PaddingPixels != FMath::Clamp(Settings.PaddingPixels, 0, 64) ||
            BakedMap->BuildSignature != MakeBuildSignature(
                *WetClothingAsset,
                Group,
                TextureSize.X,
                TextureSize.Y,
                Settings))
        {
            return false;
        }
    }

    return bFoundBakeableGroup;
}

bool FWetWrinkleNormalMapBaker::BakeGroup(
    UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeResult& InOutResult,
    FString& OutErrorMessage)
{
    if (Group.Stamps.Num() == 0 && Group.ProceduralRidgeStrokes.Num() == 0)
    {
        return true;
    }

    const double BakeStartTime = FPlatformTime::Seconds();
    const FIntPoint FinalTextureSize = WetWrinkleTextureRaster::ResolveFinalTextureSize(Settings.Resolution);
    const FIntPoint WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(FinalTextureSize);
    const int32 Width = WorkingTextureSize.X;
    const int32 Height = WorkingTextureSize.Y;

    TArray<FVector3f> NormalBuffer;
    TArray<float> CoverageBuffer;
    NormalBuffer.Init(FVector3f(0.0f, 0.0f, 1.0f), Width * Height);
    CoverageBuffer.Init(0.0f, Width * Height);

    int32 BakedStampCount = 0;
    const FWetWrinkleCoverageExtractionSettings& CoverageSettings =
        WetClothingAsset.Authored.WrinkleData.CoverageExtractionSettings;
    TMap<const UTexture2D*, FWetWrinkleTextureScalarBuffer> SeparationSources;
    for (const FWetWrinklePatchPlacement* StampPtr : Group.Stamps)
    {
        const FWetWrinklePatchPlacement& Stamp = *StampPtr;
        UTexture2D* CorrectedNormalTexture = Stamp.WrinkleNormalTexture;
        FWetWrinkleNormalSource NormalSource(CorrectedNormalTexture);
        if (!NormalSource.IsValid() || Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            continue;
        }

        FWetWrinkleTextureScalarBuffer* SeparationSource = SeparationSources.Find(CorrectedNormalTexture);
        if (SeparationSource == nullptr)
        {
            FWetWrinkleTextureScalarBuffer NewSeparationSource;
            FString SeparationError;
            if (!FWetWrinkleNormalTextureBuilder::BuildConvexSeparationBuffer(
                    CorrectedNormalTexture,
                    CoverageSettings,
                    NewSeparationSource,
                    SeparationError))
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DWC wrinkle bake skipped normal texture '%s': %s"),
                    *GetNameSafe(CorrectedNormalTexture),
                    *SeparationError);
                continue;
            }
            SeparationSource = &SeparationSources.Add(CorrectedNormalTexture, MoveTemp(NewSeparationSource));
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
                        const FVector ExistingNormalTS(NormalBuffer[PixelIndex]);
                        const FVector BlendedNormalTS =
                            FVector(
                                ExistingNormalTS.X + StampNormalTS.X,
                                ExistingNormalTS.Y + StampNormalTS.Y,
                                ExistingNormalTS.Z * StampNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
                        NormalBuffer[PixelIndex] = FVector3f(BlendedNormalTS);

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
    const double RasterEndTime = FPlatformTime::Seconds();

    TArray<FVector3f> FinalNormalBuffer;
    TArray<float> FinalCoverageBuffer;
    if (WorkingTextureSize == FinalTextureSize)
    {
        FinalNormalBuffer = MoveTemp(NormalBuffer);
        FinalCoverageBuffer = MoveTemp(CoverageBuffer);
    }
    else
    {
        WetWrinkleTextureRaster::DownsampleNormalCoverage(
            NormalBuffer,
            CoverageBuffer,
            WorkingTextureSize,
            FinalTextureSize,
            FinalNormalBuffer,
            FinalCoverageBuffer);
    }
    if (FinalNormalBuffer.Num() != FinalTextureSize.X * FinalTextureSize.Y ||
        FinalCoverageBuffer.Num() != FinalNormalBuffer.Num())
    {
        OutErrorMessage = TEXT("Failed to downsample the wrinkle normal bake buffer.");
        return false;
    }
    const double ResampleEndTime = FPlatformTime::Seconds();

    // Clip authored data to the selected material-slot islands before expanding
    // into padding. This keeps neighboring islands free from brush-shape spill.
    TArray<uint8> IslandMask;
    if (!BuildIslandMask(
            WetClothingAsset,
            Group.LODIndex,
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            FinalTextureSize.X,
            FinalTextureSize.Y,
            IslandMask))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not build the UV island mask for material slot %d, UV channel %d, LOD %d."),
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            Group.LODIndex);
        return false;
    }

    for (int32 PixelIndex = 0; PixelIndex < IslandMask.Num(); ++PixelIndex)
    {
        if (IslandMask[PixelIndex] == 0)
        {
            FinalNormalBuffer[PixelIndex] = FVector3f(0.0f, 0.0f, 1.0f);
            FinalCoverageBuffer[PixelIndex] = 0.0f;
        }
    }
    DilateNormalCoverageIntoIslandPadding(
        FinalNormalBuffer,
        FinalCoverageBuffer,
        IslandMask,
        FinalTextureSize.X,
        FinalTextureSize.Y,
        Settings.PaddingPixels);
    const double PaddingEndTime = FPlatformTime::Seconds();

    TArray<FColor> NormalPixels;
    TArray<uint8> MaskPixels;
    EncodeNormalPixels(FinalNormalBuffer, NormalPixels);
    EncodeCoverageMaskPixels(FinalCoverageBuffer, MaskPixels);
    const double EncodeEndTime = FPlatformTime::Seconds();

    const FString BaseSuffix = FString::Printf(
        TEXT("Slot%d_UV%d_LOD%d"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex);

    UTexture2D* NormalTexture = CreateOrUpdateNormalTextureAsset(
        WetClothingAsset,
        BaseSuffix + TEXT("_WrinkleNormalMap"),
        FinalTextureSize.X,
        FinalTextureSize.Y,
        NormalPixels,
        OutErrorMessage);
    if (NormalTexture == nullptr)
    {
        return false;
    }

    UTexture2D* MaskTexture = CreateOrUpdateMaskTextureAsset(
        WetClothingAsset,
        BaseSuffix + TEXT("_WrinkleMask"),
        FinalTextureSize.X,
        FinalTextureSize.Y,
        MaskPixels,
        OutErrorMessage);
    if (MaskTexture == nullptr)
    {
        return false;
    }
    const double TextureEndTime = FPlatformTime::Seconds();

    WetClothingAsset.Modify();
    FWetWrinkleBakedMapSet* BakedMap = WetClothingAsset.Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
        [&Group](const FWetWrinkleBakedMapSet& ExistingMap)
        {
            return ExistingMap.LODIndex == Group.LODIndex &&
                   ExistingMap.MaterialSlotIndex == Group.MaterialSlotIndex &&
                   ExistingMap.UVChannelIndex == Group.UVChannelIndex;
        });
    if (BakedMap == nullptr)
    {
        BakedMap = &WetClothingAsset.Authored.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    }

    BakedMap->LODIndex = Group.LODIndex;
    BakedMap->MaterialSlotIndex = Group.MaterialSlotIndex;
    BakedMap->UVChannelIndex = Group.UVChannelIndex;
    BakedMap->BakedWrinkleNormalMap = NormalTexture;
    BakedMap->BakedWrinkleMask = MaskTexture;
    BakedMap->Resolution = FinalTextureSize.X;
    BakedMap->PaddingPixels = FMath::Clamp(Settings.PaddingPixels, 0, 64);
    BakedMap->bHasCoverageAlpha = false;
    BakedMap->AlphaSemantic = EDWCWrinkleAlphaSemantic::None;
    BakedMap->AlphaBuildVersion = 0;
    BakedMap->BuildSignature = MakeBuildSignature(
        WetClothingAsset,
        Group,
        FinalTextureSize.X,
        FinalTextureSize.Y,
        Settings);
    BakedMap->BakeGuid = FGuid::NewGuid();

    for (FWetClothingTransparencyLayerData& TransparencyLayer :
         WetClothingAsset.Authored.TransparencyData.TransparencyLayers)
    {
        if (TransparencyLayer.TargetSurface.OuterMaterialSlotIndex == Group.MaterialSlotIndex &&
            TransparencyLayer.TargetSurface.OuterUVChannel == Group.UVChannelIndex)
        {
            TransparencyLayer.MarkFinalBakeStale();
        }
    }
    WetClothingAsset.MarkPackageDirty();

    InOutResult.BakedMapCount++;
    InOutResult.BakedStampCount += BakedStampCount;
    InOutResult.BakedProceduralStrokeCount += BakedProceduralStrokeCount;
    InOutResult.BakedNormalMaps.Add(NormalTexture);
    InOutResult.BakedMasks.Add(MaskTexture);

    OutErrorMessage.Reset();
    const double BakeEndTime = FPlatformTime::Seconds();
    UE_LOG(
        LogDWCWrinkleBake,
        Log,
        TEXT("Wrinkle map bake completed for slot %d UV %d LOD %d at %dx%d in %.1f ms "
             "(raster %.1f, resample %.1f, island+padding %.1f, encode %.1f, texture update %.1f, metadata %.1f)."),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        FinalTextureSize.X,
        FinalTextureSize.Y,
        (BakeEndTime - BakeStartTime) * 1000.0,
        (RasterEndTime - BakeStartTime) * 1000.0,
        (ResampleEndTime - RasterEndTime) * 1000.0,
        (PaddingEndTime - ResampleEndTime) * 1000.0,
        (EncodeEndTime - PaddingEndTime) * 1000.0,
        (TextureEndTime - EncodeEndTime) * 1000.0,
        (BakeEndTime - TextureEndTime) * 1000.0);
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
    Canonical += TEXT("DWC.WrinkleNormalMap.v19.FixedNormalAndMask|");
    Canonical += WetClothingAsset.GetPathName();
    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(Group.LODIndex);
    Canonical += FString::Printf(
        TEXT("|Slot=%d|UV=%d|LOD=%d|Size=%dx%d|Internal=%d|Padding=%d|DwcMesh=%s|SourceMeshSignature=%s|DataUVSignature=%s|DataUVGenerator=%d"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        Width,
        Height,
        FMath::Max(WetWrinkleTextureRaster::InternalBakeResolution, FMath::Max(Width, Height)),
        FMath::Clamp(Settings.PaddingPixels, 0, 64),
        *GetPathNameSafe(WetClothingAsset.GetDWCSkeletalMesh()),
        *WetClothingAsset.GetSourceMeshSignature(),
        DataUVMetadata != nullptr ? *DataUVMetadata->DataUVOutputSignature : TEXT(""),
        DataUVMetadata != nullptr ? DataUVMetadata->GeneratorVersion : INDEX_NONE);
    const FWetWrinkleCoverageExtractionSettings& CoverageSettings =
        WetClothingAsset.Authored.WrinkleData.CoverageExtractionSettings;
    Canonical += FString::Printf(
        TEXT("|CoverageBlur=%d|CoverageThreshold=%.9g|CoverageMinComponent=%d|CoverageInvert=%d"),
        CoverageSettings.InputBlurRadiusPixels,
        CoverageSettings.ConvexityThreshold,
        CoverageSettings.MinimumComponentPixels,
        CoverageSettings.bInvertConvexity ? 1 : 0);

    for (const FWetWrinklePatchPlacement* Stamp : Group.Stamps)
    {
        if (Stamp == nullptr)
        {
            continue;
        }

        const UTexture2D* NormalTexture = Stamp->WrinkleNormalTexture;
        Canonical += FString::Printf(
            TEXT("|Stamp:%s;UV=%.9g,%.9g;Radius=%.9g;Rot=%.9g;Scale=%.9g,%.9g;Strength=%.9g;Falloff=%.9g;NormalTexture=%s;NormalSource=%s"),
            *Stamp->PatchGuid.ToString(EGuidFormats::Digits),
            Stamp->PositionUV.X,
            Stamp->PositionUV.Y,
            Stamp->BrushRadiusUV,
            Stamp->RotationRadians,
            Stamp->Scale.X,
            Stamp->Scale.Y,
            Stamp->Strength,
            Stamp->Falloff,
            *GetPathNameSafe(NormalTexture),
            NormalTexture != nullptr ? *NormalTexture->Source.GetId().ToString(EGuidFormats::Digits) : TEXT(""));
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

UTexture2D* FWetWrinkleNormalMapBaker::CreateOrUpdateNormalTextureAsset(
    UWetClothingAsset& WetClothingAsset,
    const FString& ObjectSuffix,
    const int32 Width,
    const int32 Height,
    const TArray<FColor>& Pixels,
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
    if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
    {
        OutErrorMessage = TEXT("The wrinkle normal pixel buffer size does not match the requested texture size.");
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
    Texture->CompressionSettings = TC_Normalmap;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    // UTexture::PostEditChangeProperty already calls UpdateResource. Calling it again
    // queues redundant texture resource work and can force an extra compile wait after PIE.
    Texture->PostEditChange();
    Texture->MarkPackageDirty();

    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wrinkle normal map baking requires editor-only texture source data.");
    return nullptr;
#endif
}

UTexture2D* FWetWrinkleNormalMapBaker::CreateOrUpdateMaskTextureAsset(
    UWetClothingAsset& WetClothingAsset,
    const FString& ObjectSuffix,
    const int32 Width,
    const int32 Height,
    const TArray<uint8>& Pixels,
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
    if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
    {
        OutErrorMessage = TEXT("The wrinkle mask pixel buffer size does not match the requested texture size.");
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

    Texture->Source.Init(Width, Height, 1, 1, TSF_G8, Pixels.GetData());
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_Grayscale;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();

    OutErrorMessage.Reset();
    return Texture;
#else
    OutErrorMessage = TEXT("Wrinkle mask baking requires editor-only texture source data.");
    return nullptr;
#endif
}
