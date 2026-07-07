#pragma once

#include "CoreMinimal.h"
#include "Bake/DWCBakeProjection.h"
#include "Runtime/Engine/Classes/Engine/Texture.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"

class UTexture2D;

struct FDWCRevealBakeTextureSet
{
    TObjectPtr<UTexture2D> LookupMap = nullptr;
    TObjectPtr<UTexture2D> ColorMap = nullptr;
    TObjectPtr<UTexture2D> MaskMap = nullptr;
    TObjectPtr<UTexture2D> ConfidenceMap = nullptr;
};

struct FDWCRevealBakeTextureWriteSettings
{
    FIntPoint Resolution = FIntPoint(512, 512);
    FString PackagePath;
    FString AssetNamePrefix = TEXT("T_DWC_Reveal");
    TArray<FName> SourceLayerIds;
    TMap<FName, UTexture2D*> SourceLayerTextures;
    float MaskFeatherRadiusPixels = 4.0f;
};

class FDWCRevealBakeTextureWriter
{
  public:
    static bool WriteTextures(
        const TArray<FDWCBakeRayHit>&              Hits,
        const FDWCRevealBakeTextureWriteSettings& Settings,
        FDWCRevealBakeTextureSet&                 OutTextures,
        FString*                                  OutErrorMessage = nullptr);

  private:
    static UTexture2D* CreateOrUpdateTextureAsset(
        const FString&        PackagePath,
        const FString&        AssetName,
        int32                 Width,
        int32                 Height,
        const TArray<FColor>& Pixels,
        bool                  bSRGB);

    static UTexture2D* CreateOrUpdateFloatTextureAsset(
        const FString&              PackagePath,
        const FString&              AssetName,
        int32                       Width,
        int32                       Height,
        const TArray<FFloat16Color>& Pixels);

    static void BuildFeatheredMaskPixels(
        const TArray<uint8>& BinaryMask,
        int32                Width,
        int32                Height,
        float                FeatherRadiusPixels,
        TArray<FColor>&      OutMaskPixels);

    static float ApplyTextureAddress(float Coordinate, TextureAddress AddressMode);
    static FLinearColor SampleTextureBilinear(const FWetClothingTextureReadback& TextureData, const FVector2D& UV);

    static void BuildSourceTextureReadbacks(
        const FDWCRevealBakeTextureWriteSettings& Settings,
        TMap<FName, FWetClothingTextureReadback>& OutTextureDataByLayerId);

    static uint8 EncodeUnitFloat(float Value);
    static uint8 EncodeLayerIndex(FName LayerId, const TArray<FName>& SourceLayerIds);
    static float EncodeLayerIndexFloat(FName LayerId, const TArray<FName>& SourceLayerIds);
    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
