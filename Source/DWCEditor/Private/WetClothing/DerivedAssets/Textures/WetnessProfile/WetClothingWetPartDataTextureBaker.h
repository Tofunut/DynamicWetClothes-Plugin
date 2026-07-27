#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingPartData.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

class UTexture2D;
class UWetClothingAsset;

namespace DWCWetPartDataTextureBake
{
    constexpr int32 Resolution = 256;
    constexpr int32 PaddingPixels = 4;
    constexpr uint8 NeutralProfileID = 0;
    constexpr int32 MaxLocalProfileCount = 254;
    constexpr float MinDetailSize = 0.0f;
    constexpr float MaxDetailSize = 4.0f;

    inline uint8 EncodeDetailSize(const float Value)
    {
        const float Normalized = FMath::GetRangePct(MinDetailSize, MaxDetailSize, FMath::Clamp(Value, MinDetailSize, MaxDetailSize));
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Normalized, 0.0f, 1.0f) * 255.0f));
    }
}

struct FWetClothingWetPartDataSlotBakeResult
{
    int32 MaterialSlotIndex = INDEX_NONE;
    TObjectPtr<UTexture2D> WetPartDataTexture = nullptr;
    int32 PaintedPixelCount = 0;
};

struct FWetClothingWetPartDataTextureBakeResult
{
    TArray<FWetClothingWetPartDataSlotBakeResult> SlotResults;
    int32 LocalProfileCount = 0;
    int32 PaintedPixelCount = 0;
};

/**
 * Builds a WCA-wide local profile table and one Wet Part Data Texture per material slot.
 *
 * Authored membership is resolved from Original UV island IDs. Each assigned
 * TriangleID is then rasterized with the matching triangle's DWC Data UVs, so
 * both the baked lookup and the runtime material sample use the Data UV channel.
 */
class FWetClothingWetPartDataTextureBaker
{
public:
    static FString MakeBuildSignature(const UWetClothingAsset* WetClothingAsset);

    static bool Bake(
        UWetClothingAsset* WetClothingAsset,
        FWetClothingWetPartDataTextureBakeResult& OutResult,
        FString& OutErrorMessage);

    static bool ResolveProfileParameters(
        const FWetPartProfileAssignment* ProfileAssignment,
        FWetnessProfileParameters& OutParameters);

    static FString MakeProfileStableKey(
        const FWetPartProfileAssignment* ProfileAssignment,
        const FWetnessProfileParameters& Parameters);

private:
    static bool IsUVPointInsideTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C);

    static int32 PaintTriangle(
        TArray<FColor>& Pixels,
        TArray<bool>& PaintedMask,
        int32 Width,
        int32 Height,
        const FWetClothingAssetUVTriangle& Triangle,
        const FColor& PackedPartData);

    static void DilatePaintedPixels(
        TArray<FColor>& Pixels,
        TArray<bool>& PaintedMask,
        int32 Width,
        int32 Height,
        int32 PaddingPixels);

    static UTexture2D* CreateOrUpdateTextureAsset(
        UWetClothingAsset& WetClothingAsset,
        int32 MaterialSlotIndex,
        const TArray<FColor>& Pixels,
        int32 Width,
        int32 Height,
        FString& OutErrorMessage);
};
