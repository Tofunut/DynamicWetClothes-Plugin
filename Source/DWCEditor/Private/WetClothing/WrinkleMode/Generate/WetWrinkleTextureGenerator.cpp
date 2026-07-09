#include "WetClothing/WrinkleMode/Generate/WetWrinkleTextureGenerator.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"

namespace
{
    struct FWetWrinkleGeneratorNormalSource
    {
        explicit FWetWrinkleGeneratorNormalSource(UTexture2D* InTexture)
            : Texture(InTexture)
        {
#if WITH_EDITORONLY_DATA
            if (Texture == nullptr || !Texture->Source.IsValid())
            {
                return;
            }

            SizeX = Texture->Source.GetSizeX();
            SizeY = Texture->Source.GetSizeY();
            SourceFormat = Texture->Source.GetFormat();
            if (SizeX <= 0 || SizeY <= 0 || (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_BGRE8))
            {
                return;
            }

            bFlipGreenChannel = Texture->bFlipGreenChannel;
            MipData = Texture->Source.LockMipReadOnly(0);
#endif
        }

        ~FWetWrinkleGeneratorNormalSource()
        {
#if WITH_EDITORONLY_DATA
            if (Texture != nullptr && MipData != nullptr)
            {
                Texture->Source.UnlockMip(0);
            }
#endif
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

            const float ClampedU = FMath::Clamp(static_cast<float>(UV.X), 0.0f, 1.0f);
            const float ClampedV = FMath::Clamp(static_cast<float>(UV.Y), 0.0f, 1.0f);
            const int32 PixelX = FMath::Clamp(FMath::FloorToInt(ClampedU * static_cast<float>(SizeX)), 0, SizeX - 1);
            const int32 PixelY = FMath::Clamp(FMath::FloorToInt(ClampedV * static_cast<float>(SizeY)), 0, SizeY - 1);

            const FColor* ColorData = reinterpret_cast<const FColor*>(MipData);
            const FColor Color = ColorData[PixelY * SizeX + PixelX];

            const float DecodedX = static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f;
            float DecodedY = -(static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f);
            if (bFlipGreenChannel)
            {
                DecodedY = -DecodedY;
            }

            FVector DecodedNormal(
                DecodedX,
                DecodedY,
                static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
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

    float WetWrinkleWrapUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    FVector2D WetWrinkleWrapUV(const FVector2D& UV)
    {
        return FVector2D(WetWrinkleWrapUnit(UV.X), WetWrinkleWrapUnit(UV.Y));
    }

    float WetWrinkleStableUnitRandom(uint32 Seed)
    {
        Seed ^= Seed >> 16;
        Seed *= 0x7feb352dU;
        Seed ^= Seed >> 15;
        Seed *= 0x846ca68bU;
        Seed ^= Seed >> 16;
        return static_cast<float>(Seed & 0x00ffffffU) / static_cast<float>(0x00ffffffU);
    }

    FVector WetWrinkleComputeBarycentric2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const FVector2D V0 = B - A;
        const FVector2D V1 = C - A;
        const FVector2D V2 = Point - A;
        const double D00 = FVector2D::DotProduct(V0, V0);
        const double D01 = FVector2D::DotProduct(V0, V1);
        const double D11 = FVector2D::DotProduct(V1, V1);
        const double D20 = FVector2D::DotProduct(V2, V0);
        const double D21 = FVector2D::DotProduct(V2, V1);
        const double Denom = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denom))
        {
            return FVector(-1.0, -1.0, -1.0);
        }

        const double V = (D11 * D20 - D01 * D21) / Denom;
        const double W = (D00 * D21 - D01 * D20) / Denom;
        return FVector(1.0 - V - W, V, W);
    }

    FVector2D WetWrinkleComputeTriangleTileOffset(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const FVector2D Center = (A + B + C) * (1.0 / 3.0);
        return FVector2D(FMath::FloorToDouble(Center.X), FMath::FloorToDouble(Center.Y));
    }

    struct FWetWrinklePreviewEdgeKey
    {
        FIntPoint A = FIntPoint::ZeroValue;
        FIntPoint B = FIntPoint::ZeroValue;

        FWetWrinklePreviewEdgeKey() = default;

        FWetWrinklePreviewEdgeKey(const FVector2D& InA, const FVector2D& InB)
        {
            constexpr double QuantizeScale = 100000.0;
            FIntPoint QuantizedA(
                FMath::RoundToInt(InA.X * QuantizeScale),
                FMath::RoundToInt(InA.Y * QuantizeScale));
            FIntPoint QuantizedB(
                FMath::RoundToInt(InB.X * QuantizeScale),
                FMath::RoundToInt(InB.Y * QuantizeScale));

            if (QuantizedB.X < QuantizedA.X || (QuantizedB.X == QuantizedA.X && QuantizedB.Y < QuantizedA.Y))
            {
                Swap(QuantizedA, QuantizedB);
            }

            A = QuantizedA;
            B = QuantizedB;
        }

        bool operator==(const FWetWrinklePreviewEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    uint32 GetTypeHash(const FWetWrinklePreviewEdgeKey& Key)
    {
        const uint32 HashA = HashCombine(::GetTypeHash(Key.A.X), ::GetTypeHash(Key.A.Y));
        const uint32 HashB = HashCombine(::GetTypeHash(Key.B.X), ::GetTypeHash(Key.B.Y));
        return HashCombine(HashA, HashB);
    }

    void WetWrinkleAddPreviewEdge(
        const FVector2D& A,
        const FVector2D& B,
        TMap<FWetWrinklePreviewEdgeKey, int32>& EdgeUseCounts,
        TMap<FWetWrinklePreviewEdgeKey, TPair<FVector2D, FVector2D>>& EdgeSegments)
    {
        const FWetWrinklePreviewEdgeKey Key(A, B);
        EdgeUseCounts.FindOrAdd(Key)++;
        EdgeSegments.FindOrAdd(Key, TPair<FVector2D, FVector2D>(A, B));
    }

    bool WetWrinkleIsBarycentricInside(const FVector& Barycentric)
    {
        constexpr double Tolerance = 0.0001;
        return Barycentric.X >= -Tolerance &&
               Barycentric.Y >= -Tolerance &&
               Barycentric.Z >= -Tolerance &&
               Barycentric.X <= 1.0 + Tolerance &&
               Barycentric.Y <= 1.0 + Tolerance &&
               Barycentric.Z <= 1.0 + Tolerance;
    }

    FColor WetWrinkleEncodeNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    void WetWrinkleDrawPreviewPixel(TArray<FColor>& Pixels, int32 Width, int32 Height, int32 X, int32 Y, const FColor& Color)
    {
        if (Width <= 0 || Height <= 0 || !Pixels.IsValidIndex(Y * Width + X))
        {
            return;
        }

        Pixels[Y * Width + X] = Color;
    }

    void WetWrinkleDrawPreviewLine(
        TArray<FColor>& Pixels,
        const int32 Width,
        const int32 Height,
        const FVector2D& StartUV,
        const FVector2D& EndUV,
        const FColor& Color,
        const int32 ThicknessPixels)
    {
        if (Width <= 0 || Height <= 0)
        {
            return;
        }

        const int32 X0 = FMath::Clamp(FMath::RoundToInt(StartUV.X * static_cast<double>(Width - 1)), 0, Width - 1);
        const int32 Y0 = FMath::Clamp(FMath::RoundToInt(StartUV.Y * static_cast<double>(Height - 1)), 0, Height - 1);
        const int32 X1 = FMath::Clamp(FMath::RoundToInt(EndUV.X * static_cast<double>(Width - 1)), 0, Width - 1);
        const int32 Y1 = FMath::Clamp(FMath::RoundToInt(EndUV.Y * static_cast<double>(Height - 1)), 0, Height - 1);
        const int32 Steps = FMath::Max(FMath::Abs(X1 - X0), FMath::Abs(Y1 - Y0));
        if (Steps <= 0)
        {
            WetWrinkleDrawPreviewPixel(Pixels, Width, Height, X0, Y0, Color);
            return;
        }

        const int32 Radius = FMath::Max(0, ThicknessPixels / 2);
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const float Alpha = static_cast<float>(Step) / static_cast<float>(Steps);
            const int32 X = FMath::RoundToInt(FMath::Lerp(static_cast<float>(X0), static_cast<float>(X1), Alpha));
            const int32 Y = FMath::RoundToInt(FMath::Lerp(static_cast<float>(Y0), static_cast<float>(Y1), Alpha));
            for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
            {
                for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
                {
                    const int32 PixelX = FMath::Clamp(X + OffsetX, 0, Width - 1);
                    const int32 PixelY = FMath::Clamp(Y + OffsetY, 0, Height - 1);
                    WetWrinkleDrawPreviewPixel(Pixels, Width, Height, PixelX, PixelY, Color);
                }
            }
        }
    }

    UTexture2D* WetWrinkleCreateTransientDisplayTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return nullptr;
        }

        UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || !Texture->GetPlatformData()->Mips.IsValidIndex(0))
        {
            return nullptr;
        }

        Texture->SRGB = true;
        Texture->NeverStream = true;
        Texture->CompressionSettings = TC_Default;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
    }

    UTexture* WetWrinkleResolveMaterialReferenceTexture(const UWetClothingAsset& WetClothingAsset, int32 MaterialSlotIndex)
    {
        const USkeletalMesh* TargetMesh = WetClothingAsset.TargetMesh.Get();
        if (TargetMesh == nullptr || !TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            return nullptr;
        }

        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface);
    }

    UTexture2D* WetWrinkleCreateTransientNormalTexture(int32 Width, int32 Height, const TArray<FColor>& Pixels)
    {
        if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
        {
            return nullptr;
        }

        UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || !Texture->GetPlatformData()->Mips.IsValidIndex(0))
        {
            return nullptr;
        }

        Texture->SRGB = false;
        Texture->NeverStream = true;
        Texture->CompressionSettings = TC_Normalmap;
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->Filter = TF_Bilinear;
        Texture->AddressX = TA_Wrap;
        Texture->AddressY = TA_Wrap;
        Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
        return Texture;
    }
}

bool FWetWrinkleTextureGenerator::GeneratePreviewMaterialSlotTexture(
    UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetWrinkleTextureGenerationSettings& Settings,
    FWetWrinkleTextureGenerationResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FWetWrinkleTextureGenerationResult();

#if WITH_EDITORONLY_DATA
    if (WetClothingAsset == nullptr)
    {
        OutErrorMessage = TEXT("Wet Clothing Asset is unavailable.");
        return false;
    }

    if (WetClothingAsset->TargetMesh == nullptr)
    {
        OutErrorMessage = TEXT("Assign a TargetMesh before generating wrinkle textures.");
        return false;
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a single material slot before generating wrinkle textures. All Slots is preview-only.");
        return false;
    }

    if (!WetClothingAsset->TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        OutErrorMessage = TEXT("Selected material slot is not valid for the TargetMesh.");
        return false;
    }

    if (Settings.BaseNormalTexture == nullptr)
    {
        OutErrorMessage = TEXT("Select a base normal texture.");
        return false;
    }

    FWetWrinkleGeneratorNormalSource BaseNormalSource(Settings.BaseNormalTexture);
    if (!BaseNormalSource.IsValid())
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not read base normal texture '%s'. Use a texture with BGRA8 or BGRE8 editor source data."),
            *GetNameSafe(Settings.BaseNormalTexture));
        return false;
    }

    const int32 UVChannelIndex = Settings.UVChannelIndex != INDEX_NONE
                                    ? Settings.UVChannelIndex
                                    : WetClothingAsset->WrinkleData.WrinkleUVChannelIndex;
    if (UVChannelIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select or generate a wrinkle UV channel before generating wrinkle textures.");
        return false;
    }

    TArray<FWetClothingAssetUVIsland> Islands;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            WetClothingAsset->TargetMesh.Get(),
            FMath::Max(0, Settings.LODIndex),
            UVChannelIndex,
            MaterialSlotIndex,
            Islands,
            &OutErrorMessage) || Islands.Num() == 0)
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = TEXT("No UV islands were found for the selected material slot.");
        }
        return false;
    }

    const int32 MaxResolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    const int32 Width = MaxResolution;
    const int32 Height = MaxResolution;

    TArray<FColor> Pixels;
    Pixels.Init(FColor(128, 128, 255, 255), Width * Height);
    TArray<bool> bCoveredPixels;
    bCoveredPixels.Init(false, Width * Height);

    const float Intensity = FMath::Clamp(Settings.Intensity, 0.0f, 4.0f);
    const float PatternScale = FMath::Clamp(Settings.PatternScale, 0.25f, 4.0f);
    const FVector2D PatternOffset(
        WetWrinkleWrapUnit(static_cast<float>(Settings.PatternOffset.X)),
        WetWrinkleWrapUnit(static_cast<float>(Settings.PatternOffset.Y)));
    const float Noise = FMath::Clamp(Settings.Noise, 0.0f, 1.0f);
    const float DirectionRadians = Settings.DirectionRadians;

    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        if (Island.UVTriangles.Num() == 0 || !Island.UVBounds.bIsValid)
        {
            continue;
        }

        const FVector2D IslandMin = Island.UVBounds.Min;
        const FVector2D IslandSize(
            FMath::Max(static_cast<float>(Island.UVBounds.Max.X - Island.UVBounds.Min.X), UE_SMALL_NUMBER),
            FMath::Max(static_cast<float>(Island.UVBounds.Max.Y - Island.UVBounds.Min.Y), UE_SMALL_NUMBER));

        const uint32 IslandSeed = HashCombine(::GetTypeHash(MaterialSlotIndex), ::GetTypeHash(Island.UVIslandID));
        const float TwoPi = 2.0f * UE_PI;
        const float PhaseA = WetWrinkleStableUnitRandom(IslandSeed * 1664525u + 1013904223u) * TwoPi;
        const float PhaseB = WetWrinkleStableUnitRandom(IslandSeed * 22695477u + 1u) * TwoPi;
        const float PhaseC = WetWrinkleStableUnitRandom(IslandSeed * 1103515245u + 12345u) * TwoPi;
        const float FrequencyA = 1.35f + WetWrinkleStableUnitRandom(IslandSeed * 747796405u + 2891336453u) * 1.65f;
        const float FrequencyB = 1.35f + WetWrinkleStableUnitRandom(IslandSeed * 277803737u + 1013904223u) * 1.65f;
        const float FrequencyC = 0.85f + WetWrinkleStableUnitRandom(IslandSeed * 3266489917u + 668265263u) * 1.25f;
        const float WarpAmplitude = Noise * 0.075f;
        const float CosRotation = FMath::Cos(DirectionRadians);
        const float SinRotation = FMath::Sin(DirectionRadians);

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector2D TriangleTileOffset = WetWrinkleComputeTriangleTileOffset(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
            const FVector2D UV0 = Triangle.UVs[0] - TriangleTileOffset;
            const FVector2D UV1 = Triangle.UVs[1] - TriangleTileOffset;
            const FVector2D UV2 = Triangle.UVs[2] - TriangleTileOffset;
            const float MinU = FMath::Min(FMath::Min(static_cast<float>(UV0.X), static_cast<float>(UV1.X)), static_cast<float>(UV2.X));
            const float MaxU = FMath::Max(FMath::Max(static_cast<float>(UV0.X), static_cast<float>(UV1.X)), static_cast<float>(UV2.X));
            const float MinV = FMath::Min(FMath::Min(static_cast<float>(UV0.Y), static_cast<float>(UV1.Y)), static_cast<float>(UV2.Y));
            const float MaxV = FMath::Max(FMath::Max(static_cast<float>(UV0.Y), static_cast<float>(UV1.Y)), static_cast<float>(UV2.Y));
            const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Width), 0, Width - 1);
            const int32 MaxX = FMath::Clamp(FMath::CeilToInt(MaxU * Width), 0, Width - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Height), 0, Height - 1);
            const int32 MaxY = FMath::Clamp(FMath::CeilToInt(MaxV * Height), 0, Height - 1);
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
                    const FVector Barycentric = WetWrinkleComputeBarycentric2D(PixelUV, UV0, UV1, UV2);
                    if (!WetWrinkleIsBarycentricInside(Barycentric))
                    {
                        continue;
                    }

                    const FVector2D OriginalPixelUV = PixelUV + TriangleTileOffset;
                    FVector2D LocalUV(
                        (OriginalPixelUV.X - IslandMin.X) / IslandSize.X - 0.5f,
                        (OriginalPixelUV.Y - IslandMin.Y) / IslandSize.Y - 0.5f);
                    LocalUV /= PatternScale;
                    const FVector2D RotatedLocal(
                        CosRotation * LocalUV.X + SinRotation * LocalUV.Y,
                        -SinRotation * LocalUV.X + CosRotation * LocalUV.Y);
                    const float WaveX = FMath::Sin(RotatedLocal.Y * FrequencyA * TwoPi + PhaseA);
                    const float WaveY = FMath::Sin(RotatedLocal.X * FrequencyB * TwoPi + PhaseB);
                    const float CrossWave = FMath::Sin((RotatedLocal.X + RotatedLocal.Y) * FrequencyC * TwoPi + PhaseC);
                    const FVector2D WarpedLocal = RotatedLocal + FVector2D(
                        WaveX + CrossWave * 0.35f,
                        WaveY - CrossWave * 0.35f) * WarpAmplitude;
                    const FVector2D BaseSampleUV = WetWrinkleWrapUV(WarpedLocal + FVector2D(0.5f, 0.5f) + PatternOffset);

                    const FVector SampledNormalTS = BaseNormalSource.SampleNormalTS(BaseSampleUV);
                    const FVector FinalNormalTS = FVector(
                        SampledNormalTS.X * Intensity,
                        SampledNormalTS.Y * Intensity,
                        SampledNormalTS.Z)
                        .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));

                    const int32 PixelIndex = PixelY * Width + PixelX;
                    Pixels[PixelIndex] = WetWrinkleEncodeNormal(FinalNormalTS);
                    bCoveredPixels[PixelIndex] = true;
                }
            }
        }
    }

    UTexture2D* GeneratedTexture = WetWrinkleCreateTransientNormalTexture(Width, Height, Pixels);
    if (GeneratedTexture == nullptr)
    {
        OutErrorMessage = TEXT("Failed to create a transient generated normal texture.");
        return false;
    }

    TArray<FColor> DisplayPixels;
    DisplayPixels.Init(FColor(0, 0, 0, 0), Width * Height);
    for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); ++PixelIndex)
    {
        if (bCoveredPixels.IsValidIndex(PixelIndex) && bCoveredPixels[PixelIndex])
        {
            DisplayPixels[PixelIndex] = Pixels[PixelIndex];
        }
    }
    constexpr int32 IslandOutlineShadowThickness = 5;
    constexpr int32 IslandOutlineThickness = 2;
    const FColor IslandOutlineShadow(8, 8, 16, 255);
    const FColor IslandOutlineColor(255, 244, 96, 255);
    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        TMap<FWetWrinklePreviewEdgeKey, int32> EdgeUseCounts;
        TMap<FWetWrinklePreviewEdgeKey, TPair<FVector2D, FVector2D>> EdgeSegments;

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector2D TriangleTileOffset = WetWrinkleComputeTriangleTileOffset(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
            const FVector2D UV0 = Triangle.UVs[0] - TriangleTileOffset;
            const FVector2D UV1 = Triangle.UVs[1] - TriangleTileOffset;
            const FVector2D UV2 = Triangle.UVs[2] - TriangleTileOffset;

            WetWrinkleAddPreviewEdge(UV0, UV1, EdgeUseCounts, EdgeSegments);
            WetWrinkleAddPreviewEdge(UV1, UV2, EdgeUseCounts, EdgeSegments);
            WetWrinkleAddPreviewEdge(UV2, UV0, EdgeUseCounts, EdgeSegments);
        }

        for (const TPair<FWetWrinklePreviewEdgeKey, int32>& EdgeUseCount : EdgeUseCounts)
        {
            if (EdgeUseCount.Value != 1)
            {
                continue;
            }

            const TPair<FVector2D, FVector2D>* EdgeSegment = EdgeSegments.Find(EdgeUseCount.Key);
            if (EdgeSegment == nullptr)
            {
                continue;
            }

            WetWrinkleDrawPreviewLine(DisplayPixels, Width, Height, EdgeSegment->Key, EdgeSegment->Value, IslandOutlineShadow, IslandOutlineShadowThickness);
            WetWrinkleDrawPreviewLine(DisplayPixels, Width, Height, EdgeSegment->Key, EdgeSegment->Value, IslandOutlineColor, IslandOutlineThickness);
        }
    }

    OutResult.GeneratedNormalMap = GeneratedTexture;
    OutResult.PreviewDisplayMap = WetWrinkleCreateTransientDisplayTexture(Width, Height, DisplayPixels);
    OutResult.Width = Width;
    OutResult.Height = Height;
    OutErrorMessage.Reset();
    return true;
#else
    OutErrorMessage = TEXT("Wrinkle texture generation requires editor-only texture source data.");
    return false;
#endif
}
