#include "WetClothing/ProfileMap/WetClothingProfileMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothingAsset.h"
#include "WetnessProfile.h"

namespace
{
    bool IsProfileMapUVPointInsideTriangle(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool   bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool   bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }

    uint8 PackUnitFloat(float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    FColor EncodeProfileParameters(const FWetnessProfileParameters& Parameters)
    {
        return FColor(
            PackUnitFloat(Parameters.GetWetVisualStrength()),
            PackUnitFloat(Parameters.GetTransparencyStrength()),
            PackUnitFloat(Parameters.GetSurfaceWaterStrength()),
            255);
    }

    void NormalizeIslandsToPrimaryUVTile(TArray<FWetClothingAssetUVIsland>& Islands)
    {
        for (FWetClothingAssetUVIsland& Island : Islands)
        {
            FBox2D SourceBounds(ForceInit);
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                SourceBounds += Triangle.UVs[0];
                SourceBounds += Triangle.UVs[1];
                SourceBounds += Triangle.UVs[2];
            }

            if (!SourceBounds.bIsValid)
            {
                continue;
            }

            const FVector2D SourceCenter = (SourceBounds.Min + SourceBounds.Max) * 0.5f;
            const FVector2D TileOffset(
                FMath::FloorToDouble(SourceCenter.X),
                FMath::FloorToDouble(SourceCenter.Y));

            Island.UVBounds.Init();
            for (FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
                {
                    Triangle.UVs[VertexIndex] -= TileOffset;
                    Island.UVBounds += Triangle.UVs[VertexIndex];
                }
            }
        }
    }

    bool ResolveWetPartParameters(const FWetClothingAssetWetPartEntry& WetPartEntry, FWetnessProfileParameters& OutParameters)
    {
        if (WetPartEntry.ProfileAssignment.SourceProfile.IsValid())
        {
            if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(WetPartEntry.ProfileAssignment.SourceProfile.TryLoad()))
            {
                OutParameters = SourceProfile->GetParameters();
                return true;
            }
        }

        OutParameters = WetPartEntry.ProfileAssignment.Parameters;
        return true;
    }

    const FWetClothingAssetWetPartEntry* FindWetPartEntryForIsland(
        const UWetClothingAsset& WetClothingAsset,
        int32                    MaterialSlotIndex,
        int32                    UVChannelIndex,
        int32                    IslandID)
    {
        for (const FWetClothingAssetWetPartEntry& Entry : WetClothingAsset.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == MaterialSlotIndex &&
                Entry.UVChannelIndex == UVChannelIndex &&
                Entry.AssignedIslandIDs.Contains(IslandID))
            {
                return &Entry;
            }
        }

        return nullptr;
    }

    int32 PaintTriangle(TArray<FColor>& Pixels, TArray<bool>& PaintedMask, int32 Resolution, const FWetClothingAssetUVTriangle& Triangle, const FColor& Color)
    {
        const FVector2D& A = Triangle.UVs[0];
        const FVector2D& B = Triangle.UVs[1];
        const FVector2D& C = Triangle.UVs[2];

        const double MinU = FMath::Min3(A.X, B.X, C.X);
        const double MaxU = FMath::Max3(A.X, B.X, C.X);
        const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
        const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

        const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Resolution), 0, Resolution - 1);
        const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * Resolution), 0, Resolution - 1);
        const int32 MinY = FMath::Clamp(FMath::FloorToInt((1.0 - MaxV) * Resolution), 0, Resolution - 1);
        const int32 MaxY = FMath::Clamp(FMath::FloorToInt((1.0 - MinV) * Resolution), 0, Resolution - 1);

        int32 PaintedPixelCount = 0;
        for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
        {
            for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
            {
                const FVector2D SampleUV(
                    (static_cast<double>(PixelX) + 0.5) / Resolution,
                    1.0 - ((static_cast<double>(PixelY) + 0.5) / Resolution));

                if (!IsProfileMapUVPointInsideTriangle(SampleUV, A, B, C))
                {
                    continue;
                }

                const int32 PixelIndex = PixelY * Resolution + PixelX;
                if (!PaintedMask[PixelIndex])
                {
                    ++PaintedPixelCount;
                }

                Pixels[PixelIndex] = Color;
                PaintedMask[PixelIndex] = true;
            }
        }

        if (PaintedPixelCount == 0)
        {
            const FVector2D CenterUV = (A + B + C) / 3.0f;
            const int32     FallbackX = FMath::Clamp(FMath::FloorToInt(CenterUV.X * Resolution), 0, Resolution - 1);
            const int32     FallbackY = FMath::Clamp(FMath::FloorToInt((1.0 - CenterUV.Y) * Resolution), 0, Resolution - 1);
            const int32     PixelIndex = FallbackY * Resolution + FallbackX;
            if (!PaintedMask[PixelIndex])
            {
                ++PaintedPixelCount;
            }
            Pixels[PixelIndex] = Color;
            PaintedMask[PixelIndex] = true;
        }

        return PaintedPixelCount;
    }

    void DilatePaintedPixels(TArray<FColor>& Pixels, TArray<bool>& PaintedMask, int32 Resolution, int32 PaddingPixels)
    {
        const int32 ClampedPaddingPixels = FMath::Clamp(PaddingPixels, 0, 64);
        for (int32 PaddingStep = 0; PaddingStep < ClampedPaddingPixels; ++PaddingStep)
        {
            TArray<FColor> PreviousPixels = Pixels;
            TArray<bool>   PreviousMask = PaintedMask;
            bool           bWrotePixel = false;

            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    const int32 PixelIndex = Y * Resolution + X;
                    if (PreviousMask[PixelIndex])
                    {
                        continue;
                    }

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
                            if (NeighborX < 0 || NeighborY < 0 || NeighborX >= Resolution || NeighborY >= Resolution)
                            {
                                continue;
                            }

                            const int32 NeighborIndex = NeighborY * Resolution + NeighborX;
                            if (!PreviousMask[NeighborIndex])
                            {
                                continue;
                            }

                            Pixels[PixelIndex] = PreviousPixels[NeighborIndex];
                            PaintedMask[PixelIndex] = true;
                            bWrotePixel = true;
                            OffsetX = 2;
                            OffsetY = 2;
                        }
                    }
                }
            }

            if (!bWrotePixel)
            {
                break;
            }
        }
    }

    FString BuildProfileMapObjectName(const UWetClothingAsset& WetClothingAsset, const UTexture& SourceTexture, int32 UVChannelIndex)
    {
        const FString RawName = FString::Printf(
            TEXT("T_%s_%s_UV%d_ProfileMap0"),
            *WetClothingAsset.GetName(),
            *SourceTexture.GetName(),
            UVChannelIndex);
        return ObjectTools::SanitizeObjectName(RawName);
    }

    UTexture2D* CreateOrUpdateTextureAsset(
        UWetClothingAsset& WetClothingAsset,
        UTexture&          SourceTexture,
        int32              UVChannelIndex,
        int32              Resolution,
        const TArray<FColor>& Pixels,
        FString&           OutErrorMessage)
    {
#if WITH_EDITORONLY_DATA
        const FString AssetPackageName = WetClothingAsset.GetOutermost()->GetName();
        const FString PackagePath = FPackageName::GetLongPackagePath(AssetPackageName);
        if (PackagePath.IsEmpty())
        {
            OutErrorMessage = TEXT("Could not resolve a package path for the Wet Clothing Asset.");
            return nullptr;
        }

        const FString ObjectName = BuildProfileMapObjectName(WetClothingAsset, SourceTexture, UVChannelIndex);
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

        Texture->Source.Init(Resolution, Resolution, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
        Texture->SRGB = false;
        Texture->CompressionSettings = TC_VectorDisplacementmap;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Nearest;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->PostEditChange();
        Texture->UpdateResource();
        Texture->MarkPackageDirty();

        OutErrorMessage.Reset();
        return Texture;
#else
        OutErrorMessage = TEXT("ProfileMap baking requires editor-only texture source data.");
        return nullptr;
#endif
    }
} // namespace

bool FWetClothingProfileMapBaker::BakeProfileMap0(
    UWetClothingAsset*                         WetClothingAsset,
    UTexture*                                  SourceTexture,
    int32                                      UVChannelIndex,
    const TArray<int32>&                       MaterialSlotIndices,
    const FWetClothingProfileMapBakeSettings& Settings,
    FWetClothingProfileMapBakeResult&          OutResult,
    FString&                                   OutErrorMessage)
{
    OutResult = FWetClothingProfileMapBakeResult();

    if (WetClothingAsset == nullptr)
    {
        OutErrorMessage = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    if (WetClothingAsset->TargetMesh == nullptr)
    {
        OutErrorMessage = TEXT("Assign a TargetMesh before baking a ProfileMap.");
        return false;
    }

    if (SourceTexture == nullptr)
    {
        OutErrorMessage = TEXT("Select a source texture before baking a ProfileMap.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    TArray<FColor> Pixels;
    TArray<bool>   PaintedMask;
    Pixels.Init(FColor(0, 0, 0, 0), Resolution * Resolution);
    PaintedMask.Init(false, Resolution * Resolution);

    int32 PaintedPixelCount = 0;
    TArray<int32> SortedMaterialSlotIndices = MaterialSlotIndices;
    SortedMaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : SortedMaterialSlotIndices)
    {
        TArray<FWetClothingAssetUVIsland> Islands;
        FString                           BuildIslandError;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                WetClothingAsset->TargetMesh,
                0,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                &BuildIslandError))
        {
            OutErrorMessage = BuildIslandError;
            return false;
        }

        NormalizeIslandsToPrimaryUVTile(Islands);

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            const FWetClothingAssetWetPartEntry* WetPartEntry =
                FindWetPartEntryForIsland(*WetClothingAsset, MaterialSlotIndex, UVChannelIndex, Island.IslandID);
            if (WetPartEntry == nullptr)
            {
                continue;
            }

            FWetnessProfileParameters Parameters;
            if (!ResolveWetPartParameters(*WetPartEntry, Parameters))
            {
                continue;
            }

            const FColor EncodedParameters = EncodeProfileParameters(Parameters);
            for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
            {
                PaintedPixelCount += PaintTriangle(Pixels, PaintedMask, Resolution, Triangle, EncodedParameters);
            }
        }
    }

    DilatePaintedPixels(Pixels, PaintedMask, Resolution, Settings.PaddingPixels);

    UTexture2D* ProfileMap0 = CreateOrUpdateTextureAsset(
        *WetClothingAsset,
        *SourceTexture,
        UVChannelIndex,
        Resolution,
        Pixels,
        OutErrorMessage);
    if (ProfileMap0 == nullptr)
    {
        return false;
    }

    WetClothingAsset->Modify();

    FWetClothingAssetBakedProfileMap* BakedProfileMap = WetClothingAsset->BakedProfileMaps.FindByPredicate(
        [SourceTexture, UVChannelIndex](const FWetClothingAssetBakedProfileMap& ExistingProfileMap)
        {
            return ExistingProfileMap.SourceTexture == SourceTexture && ExistingProfileMap.UVChannelIndex == UVChannelIndex;
        });

    if (BakedProfileMap == nullptr)
    {
        BakedProfileMap = &WetClothingAsset->BakedProfileMaps.AddDefaulted_GetRef();
    }

    BakedProfileMap->SourceTexture = SourceTexture;
    BakedProfileMap->UVChannelIndex = UVChannelIndex;
    BakedProfileMap->MaterialSlotIndices = SortedMaterialSlotIndices;
    BakedProfileMap->ProfileMap0 = ProfileMap0;
    BakedProfileMap->Resolution = Resolution;
    BakedProfileMap->PaddingPixels = Settings.PaddingPixels;
    BakedProfileMap->BakeGuid = FGuid::NewGuid();

    WetClothingAsset->MarkPackageDirty();

    OutResult.ProfileMap0 = ProfileMap0;
    OutResult.MaterialSlotIndices = SortedMaterialSlotIndices;
    OutResult.PaintedPixelCount = PaintedPixelCount;
    OutErrorMessage.Reset();
    return true;
}
