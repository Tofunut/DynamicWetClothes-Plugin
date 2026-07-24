#include "WetWrinkleViewport.h"

#include "AdvancedPreviewScene.h"
#include "Algo/Sort.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PrimitiveDrawInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "RHITypes.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Modes/DWCEditorPreviewSlotUtils.h"
#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewMaterialBuilder.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "WetWrinkleViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetWrinklePreviewViewport, Log, All);

namespace
{
    constexpr int32 WetWrinkleUVGridResolution = 64;
    constexpr int32 WetWrinkleBVHLeafTriangleCount = 8;

    uint64 MakeWetWrinkleTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    const FName EditorPreviewWetnessProfileMap0ParameterName(TEXT("DWC_WetnessProfileMap0"));
    const FName EditorPreviewUseWetnessProfileMap0ParameterName(TEXT("DWC_UseWetnessProfileMap0"));

    UMaterialInterface* ResolveSourceMeshMaterialForPreviewSlot(
        const USkeletalMesh* PreparedMesh,
        const USkeletalMesh* SourceMesh,
        const int32 MaterialSlotIndex)
    {
        if (PreparedMesh == nullptr || SourceMesh == nullptr || !PreparedMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            return nullptr;
        }

        const TArray<FSkeletalMaterial>& SourceMaterials = SourceMesh->GetMaterials();
        if (SourceMaterials.IsValidIndex(MaterialSlotIndex) && SourceMaterials[MaterialSlotIndex].MaterialInterface != nullptr)
        {
            return SourceMaterials[MaterialSlotIndex].MaterialInterface;
        }

        const FSkeletalMaterial& PreparedMaterial = PreparedMesh->GetMaterials()[MaterialSlotIndex];
        if (PreparedMaterial.MaterialSlotName.IsNone() && PreparedMaterial.ImportedMaterialSlotName.IsNone())
        {
            return nullptr;
        }

        for (const FSkeletalMaterial& SourceMaterial : SourceMaterials)
        {
            const bool bSlotNameMatches =
                !PreparedMaterial.MaterialSlotName.IsNone() &&
                (SourceMaterial.MaterialSlotName == PreparedMaterial.MaterialSlotName ||
                 SourceMaterial.ImportedMaterialSlotName == PreparedMaterial.MaterialSlotName);
            const bool bImportedNameMatches =
                !PreparedMaterial.ImportedMaterialSlotName.IsNone() &&
                (SourceMaterial.MaterialSlotName == PreparedMaterial.ImportedMaterialSlotName ||
                 SourceMaterial.ImportedMaterialSlotName == PreparedMaterial.ImportedMaterialSlotName);
            if ((bSlotNameMatches || bImportedNameMatches) && SourceMaterial.MaterialInterface != nullptr)
            {
                return SourceMaterial.MaterialInterface;
            }
        }

        return nullptr;
    }

    FVector MakeWetWrinkleAnyPerpendicular(const FVector& Direction)
    {
        FVector Perpendicular = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector::ForwardVector : Perpendicular;
    }

    FVector ComputeWetWrinkleBarycentric(const FVector& Point, const FVector& A, const FVector& B, const FVector& C)
    {
        const FVector V0 = B - A;
        const FVector V1 = C - A;
        const FVector V2 = Point - A;
        const double D00 = FVector::DotProduct(V0, V0);
        const double D01 = FVector::DotProduct(V0, V1);
        const double D11 = FVector::DotProduct(V1, V1);
        const double D20 = FVector::DotProduct(V2, V0);
        const double D21 = FVector::DotProduct(V2, V1);
        const double Denom = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denom))
        {
            return FVector(1.0, 0.0, 0.0);
        }

        const double V = (D11 * D20 - D01 * D21) / Denom;
        const double W = (D00 * D21 - D01 * D20) / Denom;
        return FVector(1.0 - V - W, V, W);
    }

    FVector ComputeWetWrinkleBarycentric2D(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
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

    bool IsWetWrinkleBarycentricInside(const FVector& Barycentric)
    {
        constexpr double Tolerance = 0.0001;
        return Barycentric.X >= -Tolerance &&
               Barycentric.Y >= -Tolerance &&
               Barycentric.Z >= -Tolerance &&
               Barycentric.X <= 1.0 + Tolerance &&
               Barycentric.Y <= 1.0 + Tolerance &&
               Barycentric.Z <= 1.0 + Tolerance;
    }


    bool DoesWetWrinkleSegmentIntersectBox(const FBox& Box, const FVector& SegmentStart, const FVector& SegmentEnd)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        const FVector SegmentDelta = SegmentEnd - SegmentStart;
        double TMin = 0.0;
        double TMax = 1.0;

        auto ClipAxis = [&TMin, &TMax](double Start, double Delta, double MinValue, double MaxValue) -> bool
        {
            if (FMath::Abs(Delta) <= UE_SMALL_NUMBER)
            {
                return Start >= MinValue && Start <= MaxValue;
            }

            double AxisT0 = (MinValue - Start) / Delta;
            double AxisT1 = (MaxValue - Start) / Delta;
            if (AxisT0 > AxisT1)
            {
                const double Temp = AxisT0;
                AxisT0 = AxisT1;
                AxisT1 = Temp;
            }

            TMin = FMath::Max(TMin, AxisT0);
            TMax = FMath::Min(TMax, AxisT1);
            return TMin <= TMax;
        };

        return ClipAxis(SegmentStart.X, SegmentDelta.X, Box.Min.X, Box.Max.X) &&
               ClipAxis(SegmentStart.Y, SegmentDelta.Y, Box.Min.Y, Box.Max.Y) &&
               ClipAxis(SegmentStart.Z, SegmentDelta.Z, Box.Min.Z, Box.Max.Z);
    }

    bool IsWetWrinkleLinkedSurface(const FVector& PrimaryWorldPosition, const FVector& CandidateWorldPosition, float Radius)
    {
        const float MinLinkedDistance = FMath::Max(Radius * 0.5f, 1.0f);
        return FVector::DistSquared(PrimaryWorldPosition, CandidateWorldPosition) > FMath::Square(MinLinkedDistance);
    }

    float WrapWetWrinkleRasterPreviewUV(float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    FVector2D WrapWetWrinkleRasterPreviewUV(const FVector2D& UV)
    {
        return FVector2D(WrapWetWrinkleRasterPreviewUV(UV.X), WrapWetWrinkleRasterPreviewUV(UV.Y));
    }

    float ComputeWrappedWetWrinkleDelta(float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    FIntPoint ComputeWetWrinklePreviewTextureSize(const UWetClothingAsset* Asset)
    {
        const int32 Resolution = Asset != nullptr
            ? Asset->Authored.WrinkleData.BakeSettings.DefaultResolution
            : WetWrinkleTextureRaster::InternalBakeResolution;
        return WetWrinkleTextureRaster::ResolveFinalTextureSize(Resolution);
    }

    float ComputeWetWrinkleSmoothStep(float Edge0, float Edge1, float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }

        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }



    bool AreWetWrinkleSurfaceHitsEquivalentForPreview(const FWetWrinkleSurfaceHit& A, const FWetWrinkleSurfaceHit& B)
    {
        if (A.bHit != B.bHit)
        {
            return false;
        }

        if (!A.bHit)
        {
            return true;
        }

        constexpr double UVToleranceSq = 1.0e-8;
        return A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.UVChannelIndex == B.UVChannelIndex &&
               A.TriangleID == B.TriangleID &&
               (A.UV - B.UV).SizeSquared() <= UVToleranceSq;
    }

    FVector DecodeWetWrinkleNormal(const FColor& Color)
    {
        FVector DecodedNormal(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
        if (DecodedNormal.Z <= UE_SMALL_NUMBER)
        {
            const float XYLengthSq = FMath::Min(DecodedNormal.X * DecodedNormal.X + DecodedNormal.Y * DecodedNormal.Y, 1.0f);
            DecodedNormal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
        }

        return DecodedNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
    }

    FColor EncodeWetWrinkleNormal(const FVector& Normal)
    {
        const FVector SafeNormal = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FColor(
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.X * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Y * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(SafeNormal.Z * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)),
            255);
    }

    struct FWetWrinkleBrushNormalSource
    {
        explicit FWetWrinkleBrushNormalSource(UTexture2D* InTexture)
            : Texture(InTexture)
        {
            if (Texture == nullptr || !Texture->Source.IsValid())
            {
                return;
            }

            SizeX = Texture->Source.GetSizeX();
            SizeY = Texture->Source.GetSizeY();
            SourceFormat = Texture->Source.GetFormat();
            if (SizeX <= 0 ||
                SizeY <= 0 ||
                (SourceFormat != TSF_BGRA8 && SourceFormat != TSF_BGRE8 && SourceFormat != TSF_G8 && SourceFormat != TSF_G16))
            {
                return;
            }

            bFlipGreenChannel = Texture->bFlipGreenChannel;
            MipData = Texture->Source.LockMipReadOnly(0);
        }

        ~FWetWrinkleBrushNormalSource()
        {
            if (Texture != nullptr && MipData != nullptr)
            {
                Texture->Source.UnlockMip(0);
            }
        }

        bool IsValid() const
        {
            return MipData != nullptr;
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

            // Keep CPU accumulated-stamp sampling consistent with the level-0 bilinear
            // texture sampling used by the hover preview material and the baker.
            const float SampleX = FMath::Clamp(UV.X, 0.0f, 1.0f) * static_cast<float>(SizeX - 1);
            const float SampleY = FMath::Clamp(UV.Y, 0.0f, 1.0f) * static_cast<float>(SizeY - 1);
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

    FIntRect RasterizeWetWrinkleAccumulatedStamp(const FWetWrinklePatchPlacement& Stamp, const FIntPoint& TextureSize, TArray<FColor>& InOutPixels)
    {
        FIntRect DirtyRect;
        bool bHasDirtyRect = false;
        UTexture2D* CorrectedNormalTexture = Stamp.WrinkleNormalTexture;
        if (TextureSize.X <= 0 || TextureSize.Y <= 0 || InOutPixels.Num() != TextureSize.X * TextureSize.Y || CorrectedNormalTexture == nullptr ||
            Stamp.BrushRadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            return DirtyRect;
        }

        FWetWrinkleBrushNormalSource NormalSource(CorrectedNormalTexture);
        if (!NormalSource.IsValid())
        {
            return DirtyRect;
        }

        const FVector2D WrappedCenter = WrapWetWrinkleRasterPreviewUV(Stamp.PositionUV);
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
                const int32 MinX = FMath::Clamp(
                    FMath::FloorToInt((TileCenter.X - Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.X)),
                    0,
                    TextureSize.X - 1);
                const int32 MaxX = FMath::Clamp(
                    FMath::CeilToInt((TileCenter.X + Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.X)),
                    0,
                    TextureSize.X - 1);
                const int32 MinY = FMath::Clamp(
                    FMath::FloorToInt((TileCenter.Y - Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.Y)),
                    0,
                    TextureSize.Y - 1);
                const int32 MaxY = FMath::Clamp(
                    FMath::CeilToInt((TileCenter.Y + Stamp.BrushRadiusUV) * static_cast<float>(TextureSize.Y)),
                    0,
                    TextureSize.Y - 1);
                if (MinX > MaxX || MinY > MaxY)
                {
                    continue;
                }

                for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
                {
                    for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                    {
                        const FVector2D PixelUV(
                            (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(TextureSize.X),
                            (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(TextureSize.Y));
                        const FVector2D DeltaUV(
                            ComputeWrappedWetWrinkleDelta(PixelUV.X - TileCenter.X),
                            ComputeWrappedWetWrinkleDelta(PixelUV.Y - TileCenter.Y));
                        const FVector2D Local = DeltaUV / FMath::Max(Stamp.BrushRadiusUV, UE_SMALL_NUMBER);
                        const float DistanceFromCenter = Local.Size();
                        if (DistanceFromCenter > 1.0f)
                        {
                            continue;
                        }

                        const float EdgeFade = 1.0f - ComputeWetWrinkleSmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
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

                        FColor& Pixel = InOutPixels[PixelY * TextureSize.X + PixelX];
                        const FVector ExistingNormalTS = DecodeWetWrinkleNormal(Pixel);
                        const FVector BlendedNormalTS =
                            FVector(
                                ExistingNormalTS.X + StampNormalTS.X,
                                ExistingNormalTS.Y + StampNormalTS.Y,
                                ExistingNormalTS.Z * StampNormalTS.Z)
                                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
                        Pixel = EncodeWetWrinkleNormal(BlendedNormalTS);
                        if (!bHasDirtyRect)
                        {
                            DirtyRect = FIntRect(PixelX, PixelY, PixelX + 1, PixelY + 1);
                            bHasDirtyRect = true;
                        }
                        else
                        {
                            DirtyRect.Min.X = FMath::Min(DirtyRect.Min.X, PixelX);
                            DirtyRect.Min.Y = FMath::Min(DirtyRect.Min.Y, PixelY);
                            DirtyRect.Max.X = FMath::Max(DirtyRect.Max.X, PixelX + 1);
                            DirtyRect.Max.Y = FMath::Max(DirtyRect.Max.Y, PixelY + 1);
                        }
                    }
                }
            }
        }
        return DirtyRect;
    }

    bool InitializeWetWrinklePreviewTexture(
        TObjectPtr<UTexture2D>& InOutTexture,
        const FIntPoint& TextureSize,
        const TArray<FColor>& Pixels)
    {
        if (TextureSize.X <= 0 || TextureSize.Y <= 0 || Pixels.Num() != TextureSize.X * TextureSize.Y)
        {
            return false;
        }

        InOutTexture = UTexture2D::CreateTransient(TextureSize.X, TextureSize.Y, PF_B8G8R8A8);
        if (InOutTexture == nullptr || InOutTexture->GetPlatformData() == nullptr ||
            !InOutTexture->GetPlatformData()->Mips.IsValidIndex(0))
        {
            InOutTexture = nullptr;
            return false;
        }

        InOutTexture->SRGB = false;
        InOutTexture->CompressionSettings = TC_Normalmap;
        InOutTexture->MipGenSettings = TMGS_NoMipmaps;
        InOutTexture->Filter = TF_Bilinear;
        InOutTexture->AddressX = TA_Wrap;
        InOutTexture->AddressY = TA_Wrap;
        InOutTexture->LODGroup = TEXTUREGROUP_WorldNormalMap;
        InOutTexture->NeverStream = true;

        FTexture2DMipMap& Mip = InOutTexture->GetPlatformData()->Mips[0];
        void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
        Mip.BulkData.Unlock();
        InOutTexture->UpdateResource();
        return true;
    }

    void UploadWetWrinklePreviewTextureRegion(
        UTexture2D* Texture,
        const FIntPoint& TextureSize,
        const TArray<FColor>& Pixels,
        const FIntRect& DirtyRect)
    {
        if (Texture == nullptr || Texture->GetResource() == nullptr ||
            Pixels.Num() != TextureSize.X * TextureSize.Y || DirtyRect.Width() <= 0 || DirtyRect.Height() <= 0)
        {
            return;
        }

        const int32 RegionWidth = DirtyRect.Width();
        const int32 RegionHeight = DirtyRect.Height();
        const uint32 RegionPitch = static_cast<uint32>(RegionWidth * sizeof(FColor));
        uint8* RegionData = static_cast<uint8*>(FMemory::Malloc(static_cast<SIZE_T>(RegionPitch) * RegionHeight));
        for (int32 Row = 0; Row < RegionHeight; ++Row)
        {
            const FColor* SourceRow = Pixels.GetData() + (DirtyRect.Min.Y + Row) * TextureSize.X + DirtyRect.Min.X;
            FMemory::Memcpy(RegionData + static_cast<SIZE_T>(Row) * RegionPitch, SourceRow, RegionPitch);
        }

        FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
            DirtyRect.Min.X,
            DirtyRect.Min.Y,
            0,
            0,
            RegionWidth,
            RegionHeight);
        Texture->UpdateTextureRegions(
            0,
            1,
            Region,
            RegionPitch,
            sizeof(FColor),
            RegionData,
            [](uint8* Data, const FUpdateTextureRegion2D* Regions)
            {
                FMemory::Free(Data);
                delete Regions;
            });
    }

    void IncludeWetWrinkleRect(FIntRect& InOutRect, bool& bHasRect, const FIntRect& Rect)
    {
        if (Rect.Width() <= 0 || Rect.Height() <= 0)
        {
            return;
        }

        if (!bHasRect)
        {
            InOutRect = Rect;
            bHasRect = true;
            return;
        }

        InOutRect.Min.X = FMath::Min(InOutRect.Min.X, Rect.Min.X);
        InOutRect.Min.Y = FMath::Min(InOutRect.Min.Y, Rect.Min.Y);
        InOutRect.Max.X = FMath::Max(InOutRect.Max.X, Rect.Max.X);
        InOutRect.Max.Y = FMath::Max(InOutRect.Max.Y, Rect.Max.Y);
    }
} // namespace

void SWetWrinkleViewport::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    bUseDefaultPreviewMaterial = InArgs._UseDefaultPreviewMaterial;
    bUseOriginalMeshMaterialForPreview = InArgs._UseOriginalMeshMaterialForPreview;
    OnSurfaceHitChanged = InArgs._OnSurfaceHitChanged;
    OnPaintStrokeStarted = InArgs._OnPaintStrokeStarted;
    OnPaintStampRequested = InArgs._OnPaintStampRequested;
    OnPaintStrokeEnded = InArgs._OnPaintStrokeEnded;
    OnPaintStrokeCanceled = InArgs._OnPaintStrokeCanceled;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);

    RefreshPreviewMesh();
}

SWetWrinkleViewport::~SWetWrinkleViewport()
{
    ReleasePreviewMaterialSlots();
    ReleaseAccumulatedPreviewStates();
    ReleaseTransientProceduralPreviewState();

    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

}

void SWetWrinkleViewport::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
    if (PendingTransientProceduralStroke.IsSet())
    {
        FWetProceduralRidgeStroke Stroke = MoveTemp(PendingTransientProceduralStroke.GetValue());
        PendingTransientProceduralStroke.Reset();
        UpdateTransientProceduralPreview(Stroke);
    }
    FlushTransientProceduralPreviewUpload();
}

void SWetWrinkleViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(GeneratedNormalPreviewTexture);
    for (FWetWrinklePreviewMaterialSlotState& SlotState : PreviewMaterialSlots)
    {
        Collector.AddReferencedObject(SlotState.MeshOriginalMaterial);
        Collector.AddReferencedObject(SlotState.DwcWetMaterial);
        Collector.AddReferencedObject(SlotState.PreviewSourceMaterial);
        Collector.AddReferencedObject(SlotState.TransientPreviewMaterial);
        Collector.AddReferencedObject(SlotState.TransientPreviewParent);
        Collector.AddReferencedObject(SlotState.PreviewMID);
    }
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        Collector.AddReferencedObject(PreviewState.SourceTexture);
        Collector.AddReferencedObject(PreviewState.AccumulatedNormalTexture);
    }
    Collector.AddReferencedObject(TransientProceduralPreviewState.SourceTexture);
    Collector.AddReferencedObject(TransientProceduralPreviewState.NormalTexture);
    Collector.AddReferencedObject(BrushSettings.WrinkleNormalTexture);
}

void SWetWrinkleViewport::RefreshPreviewMesh(const bool bForceMaterialRebuild)
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    const bool bMeshChanged = PreviewMeshComponent->GetSkeletalMeshAsset() != TargetMesh;
    if (bMeshChanged)
    {
        PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    }
    PreviewMeshComponent->SetForcedLOD(1);

    const bool bMaterialSourcesChanged = bMeshChanged || !ArePreviewMaterialSlotsCurrent();
    if (bForceMaterialRebuild || bMaterialSourcesChanged)
    {
        RebuildPreviewMaterialSlots();
    }
    ApplyMaterialSlotVisibility();
    if (bMeshChanged)
    {
        RebuildHitTriangles();
    }
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    if (bMeshChanged)
    {
        ReleaseAccumulatedPreviewStates();
        ReleaseTransientProceduralPreviewState();
        RefreshStoredStampOverlay();
    }
    RefreshWrinklePreviewMaterials();

    if (TargetMesh != nullptr)
    {
        const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(FTransform::Identity);
        PreviewScene->SetFloorOffset(-Bounds.Origin.Z + Bounds.BoxExtent.Z);
    }
    else
    {
        PreviewScene->SetFloorOffset(0.0f);
    }

    if (OverlayText.IsValid())
    {
        OverlayText->SetText(GetViewportHintText());
    }

    if (ViewportClient.IsValid())
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        if (bMeshChanged)
        {
            ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
            ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
        }
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SWetWrinkleViewport::SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        InBrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    if (bNeedsTriangleRebuild)
    {
        // Preview canvases are editor caches for the active slot only. Keeping one full
        // working canvas per visited slot makes memory scale with navigation history.
        ReleaseAccumulatedPreviewStates();
        ReleaseTransientProceduralPreviewState();
    }
    else if (bLeavingProceduralRidgeMode)
    {
        ReleaseTransientProceduralPreviewState();
    }

    BrushSettings = InBrushSettings;
    ApplyMaterialSlotVisibility();

    if (bNeedsTriangleRebuild)
    {
        RebuildHitTriangles();
        RefreshStoredStampOverlay(false);
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke && TransientProceduralStrokeHits.Num() >= 2)
    {
        SetTransientProceduralStroke(
            TransientProceduralStrokeHits,
            bTransientProceduralStartJunction,
            bTransientProceduralEndJunction);
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::SetGeneratedNormalPreviewTexture(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    UTexture2D* GeneratedNormalTexture)
{
    bGeneratedNormalPreviewOverrideActive = true;
    GeneratedNormalPreviewMaterialSlotIndex = MaterialSlotIndex;
    GeneratedNormalPreviewUVChannelIndex = UVChannelIndex;
    GeneratedNormalPreviewTexture = GeneratedNormalTexture;
    MarkPreviewMaterialsNeedReapply();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::ClearGeneratedNormalPreviewTexture()
{
    bGeneratedNormalPreviewOverrideActive = false;
    GeneratedNormalPreviewMaterialSlotIndex = INDEX_NONE;
    GeneratedNormalPreviewUVChannelIndex = INDEX_NONE;
    GeneratedNormalPreviewTexture = nullptr;
    MarkPreviewMaterialsNeedReapply();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview)
{
    if (bRebuildAccumulatedPreview)
    {
        MarkAccumulatedPreviewStatesDirty();
    }

    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedStrokeGuid(const FGuid& InStrokeGuid)
{
    if (SelectedStrokeGuid == InStrokeGuid)
    {
        return;
    }

    SelectedStrokeGuid = InStrokeGuid;
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedProceduralStrokeGuid(const FGuid& InStrokeGuid)
{
    if (SelectedProceduralStrokeGuid == InStrokeGuid)
    {
        return;
    }

    SelectedProceduralStrokeGuid = InStrokeGuid;
    Invalidate();
}

void SWetWrinkleViewport::SetSelectedProceduralStrokePointIndex(const int32 InPointIndex)
{
    if (SelectedProceduralStrokePointIndex == InPointIndex)
    {
        return;
    }
    SelectedProceduralStrokePointIndex = InPointIndex;
    Invalidate();
}

void SWetWrinkleViewport::SetTransientProceduralStroke(
    const TArray<FWetWrinkleSurfaceHit>& SurfaceHits,
    const bool bStartJunction,
    const bool bEndJunction)
{
    TransientProceduralStrokeHits = SurfaceHits;
    bTransientProceduralStartJunction = bStartJunction;
    bTransientProceduralEndJunction = bEndJunction;
    PendingTransientProceduralStroke.Reset();
    if (bTransientProceduralPreviewBound)
    {
        bTransientProceduralPreviewBound = false;
        RefreshWrinklePreviewMaterials();
    }
    Invalidate();
}

void SWetWrinkleViewport::PreviewEditedProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    PendingTransientProceduralStroke = Stroke;
    Invalidate();
}

void SWetWrinkleViewport::SetEditingProceduralStrokeGuid(const FGuid& InStrokeGuid)
{
    if (EditingProceduralStrokeGuid == InStrokeGuid)
    {
        return;
    }

    EditingProceduralStrokeGuid = InStrokeGuid;
    MarkAccumulatedPreviewStatesDirty();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

int32 SWetWrinkleViewport::FindNearestProceduralStrokePoint(
    const FWetProceduralRidgeStroke& Stroke,
    const FVector& WorldPosition,
    const float MaxDistance) const
{
    int32 NearestPointIndex = INDEX_NONE;
    double NearestDistanceSq = FMath::Square(FMath::Max(static_cast<double>(MaxDistance), 0.0));
    for (int32 PointIndex = 0; PointIndex < Stroke.Points.Num(); ++PointIndex)
    {
        FVector PointWorldPosition = FVector::ZeroVector;
        FVector PointWorldNormal = FVector::UpVector;
        if (!ResolveProceduralStrokePointWorld(
                Stroke.Points[PointIndex],
                Stroke.MaterialSlotIndex,
                PointWorldPosition,
                PointWorldNormal))
        {
            continue;
        }

        const double DistanceSq = FVector::DistSquared(PointWorldPosition, WorldPosition);
        if (DistanceSq <= NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestPointIndex = PointIndex;
        }
    }
    return NearestPointIndex;
}

void SWetWrinkleViewport::ClearTransientProceduralStroke()
{
    if (TransientProceduralStrokeHits.IsEmpty() && TransientProceduralPreviewState.NormalTexture == nullptr)
    {
        return;
    }

    TransientProceduralStrokeHits.Reset();
    bTransientProceduralStartJunction = false;
    bTransientProceduralEndJunction = false;
    bTransientProceduralPreviewBound = false;
    PendingTransientProceduralStroke.Reset();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV)
{
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        ClearBrushCursor();
        Invalidate();
        return;
    }

    const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[0];
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    CurrentSurfaceHit.bHit = true;
    CurrentSurfaceHit.MaterialSlotIndex = Surface.MaterialSlotIndex;
    CurrentSurfaceHit.TriangleID = Surface.TriangleID;
    CurrentSurfaceHit.UVIslandID = Surface.UVIslandID;
    CurrentSurfaceHit.UVChannelIndex = UVChannelIndex;
    CurrentSurfaceHit.WorldPosition = Surface.WorldPosition;
    CurrentSurfaceHit.WorldNormal = Surface.WorldNormal;
    CurrentSurfaceHit.WorldTangent = Surface.WorldTangent;
    CurrentSurfaceHit.WorldBitangent = Surface.WorldBitangent;
    CurrentSurfaceHit.UV = UV;
    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::ClearExternalBrushPreview()
{
    ClearBrushCursor();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

bool SWetWrinkleViewport::TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.UV = UV;

    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0 || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[0];
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = Surface.MaterialSlotIndex;
    OutHit.TriangleID = Surface.TriangleID;
    OutHit.UVIslandID = Surface.UVIslandID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = Surface.WorldPosition;
    OutHit.WorldNormal = Surface.WorldNormal;
    OutHit.WorldTangent = Surface.WorldTangent;
    OutHit.WorldBitangent = Surface.WorldBitangent;
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface.WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldTangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Surface.WorldBitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    OutHit.UV = UV;
    OutHit.Barycentric = Surface.Barycentric;
    OutHit.DistanceSq = 0.0;
    return true;
}

bool SWetWrinkleViewport::TryBuildSurfaceHitAtUVNearWorldPosition(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    const FVector& ReferenceWorldPosition,
    FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.UV = UV;

    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.IsEmpty() || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FWetWrinkleProjectedSurface* Surface = &ProjectedSurfaces[0];
    double BestDistanceSq = FVector::DistSquared(Surface->WorldPosition, ReferenceWorldPosition);
    for (int32 SurfaceIndex = 1; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const double DistanceSq = FVector::DistSquared(ProjectedSurfaces[SurfaceIndex].WorldPosition, ReferenceWorldPosition);
        if (DistanceSq < BestDistanceSq)
        {
            Surface = &ProjectedSurfaces[SurfaceIndex];
            BestDistanceSq = DistanceSq;
        }
    }

    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = Surface->MaterialSlotIndex;
    OutHit.TriangleID = Surface->TriangleID;
    OutHit.UVIslandID = Surface->UVIslandID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = Surface->WorldPosition;
    OutHit.WorldNormal = Surface->WorldNormal;
    OutHit.WorldTangent = Surface->WorldTangent;
    OutHit.WorldBitangent = Surface->WorldBitangent;
    OutHit.LocalPosition = ComponentTransform.InverseTransformPosition(Surface->WorldPosition);
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldTangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Surface->WorldBitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    OutHit.UV = UV;
    OutHit.Barycentric = Surface->Barycentric;
    OutHit.DistanceSq = BestDistanceSq;
    return true;
}

bool SWetWrinkleViewport::TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = HitTriangleUVChannelIndex != INDEX_NONE ? HitTriangleUVChannelIndex : BrushSettings.UVChannelIndex;

    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr || CachedHitTriangles.Num() == 0)
    {
        return false;
    }

    const FVector SafeRayDirection = RayDirection.GetSafeNormal();
    if (SafeRayDirection.IsNearlyZero())
    {
        return false;
    }

    const FVector RayEnd = RayOrigin + SafeRayDirection * 1000000.0;

    auto TestTriangle = [this, &OutHit, &RayOrigin, &RayEnd, &SafeRayDirection](const FWetWrinkleCachedHitTriangle& Triangle)
    {
        if (Triangle.WorldBounds.IsValid && !DoesWetWrinkleSegmentIntersectBox(Triangle.WorldBounds.ExpandBy(0.1f), RayOrigin, RayEnd))
        {
            return;
        }

        FVector IntersectionPoint = FVector::ZeroVector;
        FVector TriangleNormal = FVector::ZeroVector;
        if (!FMath::SegmentTriangleIntersection(
                RayOrigin,
                RayEnd,
                Triangle.WorldPositions[0],
                Triangle.WorldPositions[1],
                Triangle.WorldPositions[2],
                IntersectionPoint,
                TriangleNormal))
        {
            return;
        }

        const double DistanceSq = FVector::DistSquared(RayOrigin, IntersectionPoint);
        if (DistanceSq >= OutHit.DistanceSq)
        {
            return;
        }

        FVector Normal = Triangle.WorldNormal;
        if (Normal.IsNearlyZero())
        {
            Normal = TriangleNormal.GetSafeNormal();
        }
        if (Normal.IsNearlyZero())
        {
            Normal = FVector::UpVector;
        }
        if (FVector::DotProduct(Normal, SafeRayDirection) > 0.0)
        {
            Normal *= -1.0;
        }

        FVector Tangent = (Triangle.WorldTangent - Normal * FVector::DotProduct(Triangle.WorldTangent, Normal)).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();
        if (Bitangent.IsNearlyZero())
        {
            Bitangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric(
            IntersectionPoint,
            Triangle.WorldPositions[0],
            Triangle.WorldPositions[1],
            Triangle.WorldPositions[2]);
        const FVector LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        const FVector2D UV = Triangle.UVs[0] * Barycentric.X + Triangle.UVs[1] * Barycentric.Y + Triangle.UVs[2] * Barycentric.Z;

        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVIslandID = Triangle.UVIslandID;
        OutHit.UVChannelIndex = HitTriangleUVChannelIndex != INDEX_NONE ? HitTriangleUVChannelIndex : BrushSettings.UVChannelIndex;
        OutHit.WorldPosition = IntersectionPoint;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.WorldBitangent = Bitangent;
        OutHit.LocalPosition = LocalPosition;
        OutHit.LocalNormal = PreviewMeshComponent->GetComponentTransform().InverseTransformVectorNoScale(Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutHit.LocalTangent = PreviewMeshComponent->GetComponentTransform().InverseTransformVectorNoScale(Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutHit.LocalBitangent = PreviewMeshComponent->GetComponentTransform().InverseTransformVectorNoScale(Bitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
        OutHit.UV = UV;
        OutHit.Barycentric = Barycentric;
        OutHit.DistanceSq = DistanceSq;
    };

    if (!HitBVHNodes.IsEmpty())
    {
        TArray<int32, TInlineAllocator<64>> NodeStack;
        NodeStack.Add(0);
        while (!NodeStack.IsEmpty())
        {
            const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
            if (!HitBVHNodes.IsValidIndex(NodeIndex))
            {
                continue;
            }

            const FWetWrinkleHitBVHNode& Node = HitBVHNodes[NodeIndex];
            if (!Node.Bounds.IsValid || !DoesWetWrinkleSegmentIntersectBox(Node.Bounds.ExpandBy(0.1f), RayOrigin, RayEnd))
            {
                continue;
            }

            if (Node.IsLeaf())
            {
                for (int32 Offset = 0; Offset < Node.TriangleCount; ++Offset)
                {
                    const int32 OrderedIndex = Node.FirstTriangleIndex + Offset;
                    if (HitBVHTriangleIndices.IsValidIndex(OrderedIndex) &&
                        CachedHitTriangles.IsValidIndex(HitBVHTriangleIndices[OrderedIndex]))
                    {
                        TestTriangle(CachedHitTriangles[HitBVHTriangleIndices[OrderedIndex]]);
                    }
                }
            }
            else
            {
                NodeStack.Add(Node.LeftChildIndex);
                NodeStack.Add(Node.RightChildIndex);
            }
        }
    }
    else
    {
        for (const FWetWrinkleCachedHitTriangle& Triangle : CachedHitTriangles)
        {
            TestTriangle(Triangle);
        }
    }

    return OutHit.bHit;
}

void SWetWrinkleViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetWrinkleViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetWrinkleViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetWrinkleViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetWrinkleEditor.ViewportToolbar");

    if (!UToolMenus::Get()->IsMenuRegistered(ViewportToolbarName))
    {
        UToolMenu* const ViewportToolbarMenu = UToolMenus::Get()->RegisterMenu(ViewportToolbarName, NAME_None, EMultiBoxType::SlimHorizontalToolBar);
        ViewportToolbarMenu->StyleName = TEXT("ViewportToolbar");

        ViewportToolbarMenu->AddSection(TEXT("Left"));

        FToolMenuSection& RightSection = ViewportToolbarMenu->AddSection(TEXT("Right"));
        RightSection.Alignment = EToolMenuSectionAlign::Last;
        RightSection.AddEntry(UE::UnrealEd::CreateCameraSubmenu(UE::UnrealEd::FViewportCameraMenuOptions().ShowAll()));
        RightSection.AddEntry(UE::UnrealEd::CreateViewModesSubmenu());
    }

    FToolMenuContext ViewportToolbarContext;
    ViewportToolbarContext.AppendCommandList(GetCommandList());
    ViewportToolbarContext.AddObject(UE::UnrealEd::CreateViewportToolbarDefaultContext(SharedThis(this)));

    return UToolMenus::Get()->GenerateWidget(ViewportToolbarName, ViewportToolbarContext);
}

void SWetWrinkleViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
    SEditorViewport::PopulateViewportOverlays(Overlay);

    Overlay->AddSlot()
        .VAlign(VAlign_Top)
        .HAlign(HAlign_Left)
        .Padding(8.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::Get().GetBrush("FloatingBorder"))
                 .Padding(6.0f)
                     [SAssignNew(OverlayText, SRichTextBlock)
                          .Text(GetViewportHintText())]];
}

void SWetWrinkleViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

USkeletalMesh* SWetWrinkleViewport::ResolveTargetMesh() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    return Asset->GetDWCSkeletalMesh() != nullptr
               ? Asset->GetDWCSkeletalMesh()
               : Asset->GetSourceSkeletalMesh();
}

const UWetClothingAsset* SWetWrinkleViewport::ResolveSourceWetClothingAsset() const
{
    return WetClothingAsset.Get();
}

UTexture* SWetWrinkleViewport::ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset != nullptr)
    {
        for (const FWetClothingSourceTextureSelection& TextureSelection : SourceWetClothingAsset->Authored.PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (TextureSelection.MaterialSlotIndex == MaterialSlotIndex &&
                TextureSelection.UVChannelIndex == UVChannelIndex &&
                TextureSelection.Texture != nullptr)
            {
                return TextureSelection.Texture;
            }
        }
    }

    const USkeletalMesh* TargetMesh = ResolveTargetMesh();
    UMaterialInterface* SourceMaterial =
        TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex)
            ? TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface
            : nullptr;
    if (SourceMaterial == nullptr && SourceWetClothingAsset != nullptr)
    {
        SourceMaterial = ResolveSourceMeshMaterialForPreviewSlot(
            TargetMesh,
            SourceWetClothingAsset->GetSourceSkeletalMesh(),
            MaterialSlotIndex);
    }
    if (SourceMaterial != nullptr)
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(SourceMaterial);
    }

    return nullptr;
}

UMaterialInterface* SWetWrinkleViewport::ResolveDwcWetMaterialForSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset == nullptr)
    {
        return nullptr;
    }

    return DWCEditorPreviewSlotUtils::ResolveCpuPreviewMaterial(SourceWetClothingAsset, MaterialSlotIndex);
}

void SWetWrinkleViewport::ReleasePreviewMaterialSlots()
{
    PreviewMaterialSlots.Reset();
    MarkPreviewMaterialsNeedReapply();
}

bool SWetWrinkleViewport::ArePreviewMaterialSlotsCurrent() const
{
    if (PreviewMeshComponent == nullptr)
    {
        return PreviewMaterialSlots.IsEmpty();
    }

    const USkeletalMesh* TargetMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (TargetMesh == nullptr)
    {
        return PreviewMaterialSlots.IsEmpty();
    }

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    if (PreviewMaterialSlots.Num() != MaterialCount)
    {
        return false;
    }

    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    const USkeletalMesh* SourceMesh = SourceWetClothingAsset != nullptr
                                          ? SourceWetClothingAsset->GetSourceSkeletalMesh()
                                          : nullptr;
    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        const FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialIndex];
        UMaterialInterface* ExpectedOriginalMaterial = TargetMesh->GetMaterials().IsValidIndex(MaterialIndex)
                                                           ? TargetMesh->GetMaterials()[MaterialIndex].MaterialInterface
                                                           : nullptr;
        if (ExpectedOriginalMaterial == nullptr)
        {
            ExpectedOriginalMaterial = ResolveSourceMeshMaterialForPreviewSlot(TargetMesh, SourceMesh, MaterialIndex);
        }

        UMaterialInterface* ExpectedDwcMaterial = ResolveDwcWetMaterialForSlot(MaterialIndex);
        UMaterialInterface* ExpectedPreviewSource = ExpectedDwcMaterial != nullptr
                                                        ? ExpectedDwcMaterial
                                                        : (bUseDefaultPreviewMaterial
                                                               ? UMaterial::GetDefaultMaterial(MD_Surface)
                                                               : ExpectedOriginalMaterial);
        if (ExpectedPreviewSource == nullptr)
        {
            ExpectedPreviewSource = UMaterial::GetDefaultMaterial(MD_Surface);
        }

        if (SlotState.MaterialSlotIndex != MaterialIndex ||
            SlotState.MeshOriginalMaterial != ExpectedOriginalMaterial ||
            SlotState.DwcWetMaterial != ExpectedDwcMaterial ||
            SlotState.PreviewSourceMaterial != ExpectedPreviewSource)
        {
            return false;
        }
    }

    return true;
}

void SWetWrinkleViewport::RebuildPreviewMaterialSlots()
{
    ReleasePreviewMaterialSlots();

    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* TargetMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (TargetMesh == nullptr)
    {
        return;
    }

    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    PreviewMaterialSlots.SetNum(MaterialCount);

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialIndex];
        SlotState.MaterialSlotIndex = MaterialIndex;

        if (TargetMesh->GetMaterials().IsValidIndex(MaterialIndex))
        {
            SlotState.MeshOriginalMaterial = TargetMesh->GetMaterials()[MaterialIndex].MaterialInterface;
        }
        if (SlotState.MeshOriginalMaterial == nullptr)
        {
            SlotState.MeshOriginalMaterial = ResolveSourceMeshMaterialForPreviewSlot(
                TargetMesh,
                ResolveSourceWetClothingAsset() != nullptr ? ResolveSourceWetClothingAsset()->GetSourceSkeletalMesh() : nullptr,
                MaterialIndex);
        }

        SlotState.DwcWetMaterial = ResolveDwcWetMaterialForSlot(MaterialIndex);
        SlotState.bUsesDwcWetMaterial = SlotState.DwcWetMaterial != nullptr;
        // A DWC-ready slot always previews from its CPU material instance. The fallback flags
        // are only meaningful for slots that do not have a generated CPU DWC material.
        SlotState.PreviewSourceMaterial = SlotState.bUsesDwcWetMaterial
                                              ? SlotState.DwcWetMaterial
                                              : (bUseDefaultPreviewMaterial
                                                     ? UMaterial::GetDefaultMaterial(MD_Surface)
                                                     : SlotState.MeshOriginalMaterial);
        if (SlotState.PreviewSourceMaterial == nullptr)
        {
            UE_LOG(
                LogWetWrinklePreviewViewport,
                Warning,
                TEXT("Wrinkle preview slot %d has no DWC override or source material. Falling back to the engine default material."),
                MaterialIndex);
            SlotState.PreviewSourceMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
        }
    }

    ApplyPreviewMaterialsToMesh();
}

void SWetWrinkleViewport::ApplyPreviewMaterialsToMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    for (const FWetWrinklePreviewMaterialSlotState& SlotState : PreviewMaterialSlots)
    {
        UMaterialInterface* MaterialToApply =
            (SlotState.MaterialSlotIndex == ActiveMaterialSlotIndex && SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
             SlotState.PreviewMID != nullptr)
                ? static_cast<UMaterialInterface*>(SlotState.PreviewMID.Get())
                : SlotState.PreviewSourceMaterial.Get();
        PreviewMeshComponent->SetMaterial(SlotState.MaterialSlotIndex, MaterialToApply);
    }

    LastAppliedActivePreviewMaterialSlot = ActiveMaterialSlotIndex;
    bPreviewMaterialsNeedReapply = false;
}

void SWetWrinkleViewport::MarkPreviewMaterialsNeedReapply()
{
    LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    bPreviewMaterialsNeedReapply = true;
}

UMaterialInterface* SWetWrinkleViewport::GetPreviewSourceMaterial(int32 MaterialSlotIndex) const
{
    return PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex)
               ? PreviewMaterialSlots[MaterialSlotIndex].PreviewSourceMaterial.Get()
               : nullptr;
}

UTexture2D* SWetWrinkleViewport::ResolveWetnessProfileMapForSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset == nullptr)
    {
        return nullptr;
    }

    const UTexture* DesiredSourceTexture = ResolveSourceTextureForMaterialSlot(MaterialSlotIndex, BrushSettings.UVChannelIndex);

    const FWetClothingBakedWetnessProfileMap* ExactMatch = SourceWetClothingAsset->Derived.Inline.BakedWetnessProfileMaps.FindByPredicate(
        [MaterialSlotIndex, DesiredSourceTexture, this](const FWetClothingBakedWetnessProfileMap& Entry)
        {
            return Entry.WetnessProfileMap0 != nullptr &&
                   Entry.SourceTexture == DesiredSourceTexture &&
                   Entry.UVChannelIndex == BrushSettings.UVChannelIndex &&
                   Entry.MaterialSlotIndices.Contains(MaterialSlotIndex);
        });
    if (ExactMatch != nullptr)
    {
        return ExactMatch->WetnessProfileMap0.Get();
    }

    const FWetClothingBakedWetnessProfileMap* SlotMatch = SourceWetClothingAsset->Derived.Inline.BakedWetnessProfileMaps.FindByPredicate(
        [MaterialSlotIndex](const FWetClothingBakedWetnessProfileMap& Entry)
        {
            return Entry.WetnessProfileMap0 != nullptr &&
                   Entry.MaterialSlotIndices.Contains(MaterialSlotIndex);
        });
    return SlotMatch != nullptr ? SlotMatch->WetnessProfileMap0.Get() : nullptr;
}

void SWetWrinkleViewport::RefreshWrinklePreviewMaterials()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != INDEX_NONE)
    {
        const bool bWasReady =
            PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex) &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewMID != nullptr;
        EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
        const bool bIsReady =
            PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex) &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
            PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewMID != nullptr;
        if (!bWasReady && bIsReady)
        {
            bPreviewMaterialsNeedReapply = true;
        }
        ResetPreviewMaterialParameters(ActiveMaterialSlotIndex);

        if (PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
        {
            FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
            if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
            {
                UTexture2D* PreviewNormalTexture = nullptr;
                if (bGeneratedNormalPreviewOverrideActive &&
                    GeneratedNormalPreviewMaterialSlotIndex == ActiveMaterialSlotIndex &&
                    GeneratedNormalPreviewUVChannelIndex == BrushSettings.UVChannelIndex)
                {
                    PreviewNormalTexture = GeneratedNormalPreviewTexture.Get();
                }
                else
                {
                    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(ActiveMaterialSlotIndex, BrushSettings.UVChannelIndex);
                    PreviewNormalTexture = ResolveAccumulatedPreviewTexture(SourceTexture, ActiveMaterialSlotIndex, BrushSettings.UVChannelIndex);
                }

                if (PreviewNormalTexture != nullptr)
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::AccumulatedNormal,
                        PreviewNormalTexture);
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 1.0f);
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f);
                }

                if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
                    !TransientProceduralStrokeHits.IsEmpty() &&
                    TransientProceduralPreviewState.MaterialSlotIndex == ActiveMaterialSlotIndex &&
                    TransientProceduralPreviewState.UVChannelIndex == BrushSettings.UVChannelIndex &&
                    TransientProceduralPreviewState.NormalTexture != nullptr)
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::TransientRidgeNormal,
                        TransientProceduralPreviewState.NormalTexture);
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::TransientRidgeEnabled,
                        1.0f);
                }

                UTexture2D* HoverNormalTexture = BrushSettings.WrinkleNormalTexture;
                if (BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
                    BrushSettings.bShowPreview && CurrentSurfaceHit.bHit && CurrentSurfaceHit.MaterialSlotIndex == ActiveMaterialSlotIndex &&
                    CurrentSurfaceHit.UVChannelIndex == BrushSettings.UVChannelIndex && HoverNormalTexture != nullptr)
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverNormal,
                        HoverNormalTexture);
                    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 1.0f);
                    SlotState.PreviewMID->SetVectorParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverCenterUV,
                        FLinearColor(CurrentSurfaceHit.UV.X, CurrentSurfaceHit.UV.Y, 0.0f, 0.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverRadiusUV,
                        FMath::Max(BrushSettings.BrushRadiusUV, UE_SMALL_NUMBER));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverRotation,
                        BrushSettings.RotationRadians);
                    SlotState.PreviewMID->SetVectorParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverScale,
                        FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverStrength,
                        FMath::Clamp(BrushSettings.Strength, 0.0f, 4.0f));
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::HoverFalloff,
                        FMath::Clamp(BrushSettings.Falloff, 0.0f, 1.0f));
                }
            }
        }
    }

    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}


void SWetWrinkleViewport::RefreshWrinklePreviewHoverParameters()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();

    auto DisableHoverForSlot = [this](int32 SlotIndex)
    {
        if (PreviewMaterialSlots.IsValidIndex(SlotIndex) && PreviewMaterialSlots[SlotIndex].PreviewMID != nullptr)
        {
            PreviewMaterialSlots[SlotIndex].PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f);
            PreviewMaterialSlots[SlotIndex].PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverRadiusUV, 0.0f);
        }
    };

    if (LastHoverPreviewMaterialSlotIndex != INDEX_NONE && LastHoverPreviewMaterialSlotIndex != ActiveMaterialSlotIndex)
    {
        DisableHoverForSlot(LastHoverPreviewMaterialSlotIndex);
    }

    if (!PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
    {
        LastHoverPreviewMaterialSlotIndex = INDEX_NONE;
        return;
    }

    EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
    if (!PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex) || PreviewMaterialSlots[ActiveMaterialSlotIndex].PreviewMID == nullptr)
    {
        LastHoverPreviewMaterialSlotIndex = INDEX_NONE;
        return;
    }

    FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
    const bool bEnableHover =
        SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
        BrushSettings.ToolMode == EWetWrinkleToolMode::Patch &&
        BrushSettings.bShowPreview &&
        CurrentSurfaceHit.bHit &&
        CurrentSurfaceHit.MaterialSlotIndex == ActiveMaterialSlotIndex &&
        CurrentSurfaceHit.UVChannelIndex == BrushSettings.UVChannelIndex &&
        BrushSettings.WrinkleNormalTexture != nullptr;

    if (!bEnableHover)
    {
        DisableHoverForSlot(ActiveMaterialSlotIndex);
        LastHoverPreviewMaterialSlotIndex = INDEX_NONE;
    }
    else
    {
        SlotState.PreviewMID->SetTextureParameterValue(
            WetWrinklePreviewMaterialParameters::HoverNormal,
            BrushSettings.WrinkleNormalTexture);
        SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 1.0f);
        SlotState.PreviewMID->SetVectorParameterValue(
            WetWrinklePreviewMaterialParameters::HoverCenterUV,
            FLinearColor(CurrentSurfaceHit.UV.X, CurrentSurfaceHit.UV.Y, 0.0f, 0.0f));
        SlotState.PreviewMID->SetScalarParameterValue(
            WetWrinklePreviewMaterialParameters::HoverRadiusUV,
            FMath::Max(BrushSettings.BrushRadiusUV, UE_SMALL_NUMBER));
        SlotState.PreviewMID->SetScalarParameterValue(
            WetWrinklePreviewMaterialParameters::HoverRotation,
            BrushSettings.RotationRadians);
        SlotState.PreviewMID->SetVectorParameterValue(
            WetWrinklePreviewMaterialParameters::HoverScale,
            FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));
        SlotState.PreviewMID->SetScalarParameterValue(
            WetWrinklePreviewMaterialParameters::HoverStrength,
            FMath::Clamp(BrushSettings.Strength, 0.0f, 4.0f));
        SlotState.PreviewMID->SetScalarParameterValue(
            WetWrinklePreviewMaterialParameters::HoverFalloff,
            FMath::Clamp(BrushSettings.Falloff, 0.0f, 1.0f));
        LastHoverPreviewMaterialSlotIndex = ActiveMaterialSlotIndex;
    }

    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}

bool SWetWrinkleViewport::EnsurePreviewMaterialForSlot(int32 MaterialSlotIndex)
{
    if (!PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex))
    {
        return false;
    }

    FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialSlotIndex];
    const int32 RequiredUVChannelIndex = BrushSettings.UVChannelIndex;
    if (SlotState.PreviewUVChannelIndex != RequiredUVChannelIndex)
    {
        SlotState.TransientPreviewMaterial = nullptr;
        SlotState.TransientPreviewParent = nullptr;
        SlotState.PreviewMID = nullptr;
        SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Uninitialized;
        SlotState.PreviewBuildError.Reset();
        SlotState.PreviewUVChannelIndex = RequiredUVChannelIndex;
        MarkPreviewMaterialsNeedReapply();
    }

    if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
    {
        return true;
    }

    if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Failed ||
        SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Unsupported)
    {
        return false;
    }

    if (SlotState.PreviewSourceMaterial == nullptr)
    {
        SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Unsupported;
        SlotState.PreviewBuildError = TEXT("Preview source material is not available.");
        return false;
    }

    FWetWrinklePreviewMaterialBuildArgs BuildArgs;
    BuildArgs.SourceMaterial = SlotState.PreviewSourceMaterial.Get();
    BuildArgs.UVChannelIndex = RequiredUVChannelIndex;
    BuildArgs.bOverrideCpuWetnessInput = SlotState.bUsesDwcWetMaterial;

    FWetWrinklePreviewMaterialBuildResult BuildResult = FWetWrinklePreviewMaterialBuilder::Build(BuildArgs);
    if (!BuildResult.bSucceeded || BuildResult.PreviewMID == nullptr)
    {
        SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Failed;
        SlotState.PreviewBuildError = BuildResult.ErrorMessage;
        UE_LOG(
            LogWetWrinklePreviewViewport,
            Warning,
            TEXT("Failed to build wrinkle preview material for slot %d (%s): %s"),
            MaterialSlotIndex,
            *GetNameSafe(SlotState.PreviewSourceMaterial),
            *SlotState.PreviewBuildError);
        return false;
    }

    SlotState.TransientPreviewMaterial = BuildResult.TransientBaseMaterial;
    SlotState.TransientPreviewParent = BuildResult.TransientMaterialParent;
    SlotState.PreviewMID = BuildResult.PreviewMID;
    SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Ready;
    SlotState.PreviewBuildError.Reset();
    ResetPreviewMaterialParameters(MaterialSlotIndex);
    return true;
}

void SWetWrinkleViewport::ResetPreviewMaterialParameters(int32 MaterialSlotIndex)
{
    if (!PreviewMaterialSlots.IsValidIndex(MaterialSlotIndex))
    {
        return;
    }

    FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[MaterialSlotIndex];
    if (SlotState.PreviewMID == nullptr)
    {
        return;
    }

    SlotState.PreviewMID->SetScalarParameterValue(
        WetWrinklePreviewMaterialParameters::PreviewWetness,
        FMath::Clamp(BrushSettings.PreviewWetness, 0.0f, 1.0f));
    SlotState.PreviewMID->SetTextureParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedNormal, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedEnabled, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::AccumulatedStrength, 1.0f);
    SlotState.PreviewMID->SetTextureParameterValue(WetWrinklePreviewMaterialParameters::TransientRidgeNormal, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::TransientRidgeEnabled, 0.0f);
    SlotState.PreviewMID->SetTextureParameterValue(WetWrinklePreviewMaterialParameters::HoverNormal, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverEnabled, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverRotation, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverRadiusUV, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverStrength, 0.0f);
    SlotState.PreviewMID->SetScalarParameterValue(WetWrinklePreviewMaterialParameters::HoverFalloff, 0.5f);
    SlotState.PreviewMID->SetVectorParameterValue(WetWrinklePreviewMaterialParameters::HoverCenterUV, FLinearColor::Black);
    SlotState.PreviewMID->SetVectorParameterValue(
        WetWrinklePreviewMaterialParameters::HoverScale,
        FLinearColor(1.0f, 1.0f, 0.0f, 0.0f));

    SlotState.PreviewMID->SetTextureParameterValue(EditorPreviewWetnessProfileMap0ParameterName, nullptr);
    SlotState.PreviewMID->SetScalarParameterValue(EditorPreviewUseWetnessProfileMap0ParameterName, 0.0f);

    if (SlotState.bUsesDwcWetMaterial)
    {
        if (UTexture2D* WetnessProfileMap0 = ResolveWetnessProfileMapForSlot(MaterialSlotIndex))
        {
            SlotState.PreviewMID->SetTextureParameterValue(EditorPreviewWetnessProfileMap0ParameterName, WetnessProfileMap0);
            SlotState.PreviewMID->SetScalarParameterValue(EditorPreviewUseWetnessProfileMap0ParameterName, 1.0f);
        }
    }

}

void SWetWrinkleViewport::AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp)
{
    if (Stamp.MaterialSlotIndex == INDEX_NONE || Stamp.UVChannelIndex < 0)
    {
        return;
    }

    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(Stamp.SourceTexture.Get(), Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return;
    }

    if (PreviewState->bDirty || PreviewState->AccumulatedNormalTexture == nullptr ||
        PreviewState->AccumulatedNormalTexture->GetPlatformData() == nullptr ||
        !PreviewState->AccumulatedNormalTexture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    PreviewState->TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState->WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState->TextureSize);
    if (PreviewState->TextureSize.X <= 0 || PreviewState->TextureSize.Y <= 0 ||
        PreviewState->AccumulatedNormalTexture->GetSizeX() != PreviewState->TextureSize.X ||
        PreviewState->AccumulatedNormalTexture->GetSizeY() != PreviewState->TextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    if (PreviewState->Pixels.Num() != PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        PreviewState->WorkingPixels.Num() != PreviewState->WorkingTextureSize.X * PreviewState->WorkingTextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    const FIntRect WorkingDirtyRect = RasterizeWetWrinkleAccumulatedStamp(
        Stamp,
        PreviewState->WorkingTextureSize,
        PreviewState->WorkingPixels);
    if (WorkingDirtyRect.IsEmpty())
    {
        return;
    }

    const FIntRect FinalDirtyRect = WetWrinkleTextureRaster::MapWorkingRectToFinal(
        WorkingDirtyRect,
        PreviewState->WorkingTextureSize,
        PreviewState->TextureSize);
    WetWrinkleTextureRaster::DownsampleNormalPixels(
        PreviewState->WorkingPixels,
        PreviewState->WorkingTextureSize,
        PreviewState->TextureSize,
        PreviewState->Pixels,
        &FinalDirtyRect);
    UploadWetWrinklePreviewTextureRegion(
        PreviewState->AccumulatedNormalTexture,
        PreviewState->TextureSize,
        PreviewState->Pixels,
        FinalDirtyRect);
    PreviewState->bDirty = false;
}

void SWetWrinkleViewport::AppendAccumulatedPreviewProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    if (!Stroke.bEnabled || Stroke.MaterialSlotIndex == INDEX_NONE || Stroke.UVChannelIndex < 0 || Stroke.Points.Num() < 2)
    {
        return;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(Stroke.MaterialSlotIndex, Stroke.UVChannelIndex);
    FWetWrinkleAccumulatedPreviewState* PreviewState =
        FindOrAddAccumulatedPreviewState(SourceTexture, Stroke.MaterialSlotIndex, Stroke.UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return;
    }

    if (PreviewState->bDirty || PreviewState->AccumulatedNormalTexture == nullptr ||
        PreviewState->Pixels.Num() != PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        PreviewState->WorkingPixels.Num() != PreviewState->WorkingTextureSize.X * PreviewState->WorkingTextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewMaterials();
        return;
    }

    const FWetProceduralRidgeRasterResult RasterResult = FWetProceduralRidgeRasterizer::Rasterize(
        Stroke,
        PreviewState->WorkingTextureSize,
        PreviewState->WorkingPixels);
    if (RasterResult.bAffectedPixels)
    {
        const FIntRect FinalDirtyRect = WetWrinkleTextureRaster::MapWorkingRectToFinal(
            RasterResult.DirtyRect,
            PreviewState->WorkingTextureSize,
            PreviewState->TextureSize);
        WetWrinkleTextureRaster::DownsampleNormalPixels(
            PreviewState->WorkingPixels,
            PreviewState->WorkingTextureSize,
            PreviewState->TextureSize,
            PreviewState->Pixels,
            &FinalDirtyRect);
        UploadWetWrinklePreviewTextureRegion(
            PreviewState->AccumulatedNormalTexture,
            PreviewState->TextureSize,
            PreviewState->Pixels,
            FinalDirtyRect);
    }
    PreviewState->bDirty = false;
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStates()
{
    AccumulatedPreviewStates.Reset();
}

void SWetWrinkleViewport::ReleaseTransientProceduralPreviewState()
{
    TransientProceduralPreviewState = FWetProceduralRidgeTransientPreviewState();
    bTransientProceduralPreviewBound = false;
    PendingTransientProceduralStroke.Reset();
    PendingTransientProceduralUploadRect = FIntRect();
    bHasPendingTransientProceduralUpload = false;
}

void SWetWrinkleViewport::FlushTransientProceduralPreviewUpload()
{
    if (!bHasPendingTransientProceduralUpload ||
        TransientProceduralPreviewState.NormalTexture == nullptr ||
        PendingTransientProceduralUploadRect.IsEmpty())
    {
        return;
    }

    UploadWetWrinklePreviewTextureRegion(
        TransientProceduralPreviewState.NormalTexture,
        TransientProceduralPreviewState.TextureSize,
        TransientProceduralPreviewState.Pixels,
        PendingTransientProceduralUploadRect);
    PendingTransientProceduralUploadRect = FIntRect();
    bHasPendingTransientProceduralUpload = false;
}

bool SWetWrinkleViewport::EnsureTransientProceduralPreviewState(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0)
    {
        return false;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(MaterialSlotIndex, UVChannelIndex);
    const FIntPoint TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    const FIntPoint WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(TextureSize);
    const int32 PixelCount = TextureSize.X * TextureSize.Y;
    const int32 WorkingPixelCount = WorkingTextureSize.X * WorkingTextureSize.Y;
    if (TextureSize.X <= 0 || TextureSize.Y <= 0 || PixelCount <= 0 || WorkingPixelCount <= 0)
    {
        return false;
    }

    const bool bNeedsNewState =
        TransientProceduralPreviewState.NormalTexture == nullptr ||
        TransientProceduralPreviewState.SourceTexture.Get() != SourceTexture ||
        TransientProceduralPreviewState.MaterialSlotIndex != MaterialSlotIndex ||
        TransientProceduralPreviewState.UVChannelIndex != UVChannelIndex ||
        TransientProceduralPreviewState.TextureSize != TextureSize ||
        TransientProceduralPreviewState.WorkingTextureSize != WorkingTextureSize ||
        TransientProceduralPreviewState.Pixels.Num() != PixelCount ||
        TransientProceduralPreviewState.WorkingPixels.Num() != WorkingPixelCount;
    if (!bNeedsNewState)
    {
        return true;
    }

    const FColor FlatNormal = EncodeWetWrinkleNormal(FVector(0.0f, 0.0f, 1.0f));
    ReleaseTransientProceduralPreviewState();
    TransientProceduralPreviewState.SourceTexture = SourceTexture;
    TransientProceduralPreviewState.MaterialSlotIndex = MaterialSlotIndex;
    TransientProceduralPreviewState.UVChannelIndex = UVChannelIndex;
    TransientProceduralPreviewState.TextureSize = TextureSize;
    TransientProceduralPreviewState.WorkingTextureSize = WorkingTextureSize;
    TransientProceduralPreviewState.Pixels.Init(FlatNormal, PixelCount);
    TransientProceduralPreviewState.WorkingPixels.Init(FlatNormal, WorkingPixelCount);
    if (!InitializeWetWrinklePreviewTexture(
            TransientProceduralPreviewState.NormalTexture,
            TextureSize,
            TransientProceduralPreviewState.Pixels))
    {
        ReleaseTransientProceduralPreviewState();
        return false;
    }

    return true;
}

bool SWetWrinkleViewport::UpdateTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke)
{
    if (Stroke.Points.Num() < 2 || Stroke.MaterialSlotIndex == INDEX_NONE || Stroke.UVChannelIndex < 0)
    {
        return false;
    }

    if (!EnsureTransientProceduralPreviewState(Stroke.MaterialSlotIndex, Stroke.UVChannelIndex))
    {
        return false;
    }

    const FIntPoint TextureSize = TransientProceduralPreviewState.TextureSize;
    const FIntPoint WorkingTextureSize = TransientProceduralPreviewState.WorkingTextureSize;
    const FColor FlatNormal = EncodeWetWrinkleNormal(FVector(0.0f, 0.0f, 1.0f));

    FWetProceduralRidgeStroke PreviousStroke;
    PreviousStroke.MaterialSlotIndex = TransientProceduralPreviewState.MaterialSlotIndex;
    PreviousStroke.UVChannelIndex = TransientProceduralPreviewState.UVChannelIndex;
    PreviousStroke.Shape = static_cast<EWetProceduralRidgeShape>(TransientProceduralPreviewState.PreviousShape);
    PreviousStroke.bFlipFoldSide = TransientProceduralPreviewState.bPreviousFlipFoldSide;
    PreviousStroke.WidthUV = TransientProceduralPreviewState.PreviousWidthUV;
    PreviousStroke.Strength = TransientProceduralPreviewState.PreviousStrength;
    PreviousStroke.Falloff = TransientProceduralPreviewState.PreviousFalloff;
    PreviousStroke.StartTaper = TransientProceduralPreviewState.PreviousStartTaper;
    PreviousStroke.EndTaper = TransientProceduralPreviewState.PreviousEndTaper;
    PreviousStroke.StartEndpoint.Mode = static_cast<EWetProceduralRidgeEndpointMode>(TransientProceduralPreviewState.PreviousStartEndpointMode);
    PreviousStroke.EndEndpoint.Mode = static_cast<EWetProceduralRidgeEndpointMode>(TransientProceduralPreviewState.PreviousEndEndpointMode);
    PreviousStroke.FlareSettings = TransientProceduralPreviewState.PreviousFlareSettings;
    PreviousStroke.NaturalVariation = TransientProceduralPreviewState.PreviousNaturalVariation;
    for (const FVector2D& UV : TransientProceduralPreviewState.PreviousPointUVs)
    {
        FWetProceduralRidgeStrokePoint& Point = PreviousStroke.Points.AddDefaulted_GetRef();
        Point.PositionUV = UV;
    }

    int32 CommonPointCount = 0;
    while (CommonPointCount < PreviousStroke.Points.Num() && CommonPointCount < Stroke.Points.Num() &&
           PreviousStroke.Points[CommonPointCount].PositionUV.Equals(Stroke.Points[CommonPointCount].PositionUV, 1.0e-6))
    {
        ++CommonPointCount;
    }

    const auto VariationsEqual = [](const FWetProceduralRidgeVariationSettings& A, const FWetProceduralRidgeVariationSettings& B)
    {
        return A.bEnabled == B.bEnabled &&
            FMath::IsNearlyEqual(A.CenterlineAmount, B.CenterlineAmount) &&
            FMath::IsNearlyEqual(A.CenterlineFrequency, B.CenterlineFrequency) &&
            FMath::IsNearlyEqual(A.WidthVariation, B.WidthVariation) &&
            FMath::IsNearlyEqual(A.WidthFrequency, B.WidthFrequency) &&
            A.NoiseSeed == B.NoiseSeed;
    };
    const bool bSettingsChanged =
        PreviousStroke.Shape != Stroke.Shape ||
        PreviousStroke.bFlipFoldSide != Stroke.bFlipFoldSide ||
        !FMath::IsNearlyEqual(PreviousStroke.WidthUV, Stroke.WidthUV) ||
        !FMath::IsNearlyEqual(PreviousStroke.Strength, Stroke.Strength) ||
        !FMath::IsNearlyEqual(PreviousStroke.Falloff, Stroke.Falloff) ||
        !FMath::IsNearlyEqual(PreviousStroke.StartTaper, Stroke.StartTaper) ||
        !FMath::IsNearlyEqual(PreviousStroke.EndTaper, Stroke.EndTaper) ||
        PreviousStroke.StartEndpoint.Mode != Stroke.StartEndpoint.Mode ||
        PreviousStroke.EndEndpoint.Mode != Stroke.EndEndpoint.Mode ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.Length, Stroke.FlareSettings.Length) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.WidthScale, Stroke.FlareSettings.WidthScale) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.EndStrength, Stroke.FlareSettings.EndStrength) ||
        !FMath::IsNearlyEqual(PreviousStroke.FlareSettings.Softness, Stroke.FlareSettings.Softness) ||
        !VariationsEqual(PreviousStroke.NaturalVariation, Stroke.NaturalVariation);
    const int32 FirstChangedPoint = bSettingsChanged ? 0 : FMath::Max(CommonPointCount - 2, 0);

    FIntRect DirtyRect;
    bool bHasDirtyRect = false;
    if (PreviousStroke.Points.Num() >= 2)
    {
        IncludeWetWrinkleRect(
            DirtyRect,
            bHasDirtyRect,
            FWetProceduralRidgeRasterizer::ComputeBounds(PreviousStroke, WorkingTextureSize, FirstChangedPoint));
    }
    IncludeWetWrinkleRect(
        DirtyRect,
        bHasDirtyRect,
        FWetProceduralRidgeRasterizer::ComputeBounds(Stroke, WorkingTextureSize, FirstChangedPoint));
    if (!bHasDirtyRect)
    {
        return false;
    }

    for (int32 PixelY = DirtyRect.Min.Y; PixelY < DirtyRect.Max.Y; ++PixelY)
    {
        FColor* Row = TransientProceduralPreviewState.WorkingPixels.GetData() + PixelY * WorkingTextureSize.X;
        for (int32 PixelX = DirtyRect.Min.X; PixelX < DirtyRect.Max.X; ++PixelX)
        {
            Row[PixelX] = FlatNormal;
        }
    }

    FWetProceduralRidgeRasterizer::Rasterize(
        Stroke,
        WorkingTextureSize,
        TransientProceduralPreviewState.WorkingPixels,
        &DirtyRect,
        false);
    const FIntRect FinalDirtyRect = WetWrinkleTextureRaster::MapWorkingRectToFinal(
        DirtyRect,
        WorkingTextureSize,
        TextureSize);
    WetWrinkleTextureRaster::DownsampleNormalPixels(
        TransientProceduralPreviewState.WorkingPixels,
        WorkingTextureSize,
        TextureSize,
        TransientProceduralPreviewState.Pixels,
        &FinalDirtyRect);
    IncludeWetWrinkleRect(
        PendingTransientProceduralUploadRect,
        bHasPendingTransientProceduralUpload,
        FinalDirtyRect);

    TransientProceduralPreviewState.PreviousPointUVs.Reset(Stroke.Points.Num());
    for (const FWetProceduralRidgeStrokePoint& Point : Stroke.Points)
    {
        TransientProceduralPreviewState.PreviousPointUVs.Add(Point.PositionUV);
    }
    TransientProceduralPreviewState.PreviousShape = static_cast<uint8>(Stroke.Shape);
    TransientProceduralPreviewState.bPreviousFlipFoldSide = Stroke.bFlipFoldSide;
    TransientProceduralPreviewState.PreviousWidthUV = Stroke.WidthUV;
    TransientProceduralPreviewState.PreviousStrength = Stroke.Strength;
    TransientProceduralPreviewState.PreviousFalloff = Stroke.Falloff;
    TransientProceduralPreviewState.PreviousStartTaper = Stroke.StartTaper;
    TransientProceduralPreviewState.PreviousEndTaper = Stroke.EndTaper;
    TransientProceduralPreviewState.PreviousStartEndpointMode = static_cast<uint8>(Stroke.StartEndpoint.Mode);
    TransientProceduralPreviewState.PreviousEndEndpointMode = static_cast<uint8>(Stroke.EndEndpoint.Mode);
    TransientProceduralPreviewState.PreviousFlareSettings = Stroke.FlareSettings;
    TransientProceduralPreviewState.PreviousNaturalVariation = Stroke.NaturalVariation;
    if (!bTransientProceduralPreviewBound)
    {
        RefreshWrinklePreviewMaterials();
        bTransientProceduralPreviewBound = true;
    }
    return true;
}

void SWetWrinkleViewport::MarkAccumulatedPreviewStatesDirty()
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        PreviewState.bDirty = true;
    }
}

FWetWrinkleAccumulatedPreviewState* SWetWrinkleViewport::FindOrAddAccumulatedPreviewState(
    UTexture* SourceTexture,
    int32 MaterialSlotIndex,
    int32 UVChannelIndex)
{
    if (MaterialSlotIndex == INDEX_NONE || UVChannelIndex < 0)
    {
        return nullptr;
    }

    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.MaterialSlotIndex == MaterialSlotIndex && PreviewState.UVChannelIndex == UVChannelIndex)
        {
            const FIntPoint ExpectedTextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
            const FIntPoint ExpectedWorkingTextureSize =
                WetWrinkleTextureRaster::ResolveWorkingTextureSize(ExpectedTextureSize);
            if (PreviewState.SourceTexture.Get() != SourceTexture)
            {
                PreviewState.SourceTexture = SourceTexture;
                PreviewState.bDirty = true;
            }
            if (PreviewState.TextureSize != ExpectedTextureSize ||
                PreviewState.WorkingTextureSize != ExpectedWorkingTextureSize)
            {
                PreviewState.TextureSize = ExpectedTextureSize;
                PreviewState.WorkingTextureSize = ExpectedWorkingTextureSize;
                PreviewState.bDirty = true;
            }
            return &PreviewState;
        }
    }

    FWetWrinkleAccumulatedPreviewState& NewState = AccumulatedPreviewStates.AddDefaulted_GetRef();
    NewState.SourceTexture = SourceTexture;
    NewState.MaterialSlotIndex = MaterialSlotIndex;
    NewState.UVChannelIndex = UVChannelIndex;
    NewState.TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    NewState.WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(NewState.TextureSize);
    NewState.bDirty = true;
    return &NewState;
}

UTexture2D* SWetWrinkleViewport::ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    FWetWrinkleAccumulatedPreviewState* PreviewState = FindOrAddAccumulatedPreviewState(SourceTexture, MaterialSlotIndex, UVChannelIndex);
    if (PreviewState == nullptr)
    {
        return nullptr;
    }

    if (PreviewState->bDirty && !RebuildAccumulatedPreviewTexture(*PreviewState))
    {
        return nullptr;
    }

    return PreviewState->AccumulatedNormalTexture;
}

bool SWetWrinkleViewport::RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    PreviewState.bDirty = false;

    if (PreviewState.MaterialSlotIndex == INDEX_NONE || PreviewState.UVChannelIndex < 0)
    {
        PreviewState.AccumulatedNormalTexture = nullptr;
        return false;
    }

    PreviewState.TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState.WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState.TextureSize);
    if (PreviewState.TextureSize.X <= 0 || PreviewState.TextureSize.Y <= 0)
    {
        PreviewState.AccumulatedNormalTexture = nullptr;
        return false;
    }

    TArray<FColor> WorkingPixels;
    WorkingPixels.Init(
        EncodeWetWrinkleNormal(FVector(0.0f, 0.0f, 1.0f)),
        PreviewState.WorkingTextureSize.X * PreviewState.WorkingTextureSize.Y);

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr)
    {
        for (const FWetWrinklePatchPlacement& Stamp : Asset->Authored.WrinkleData.EditablePatches)
        {
            if (!Stamp.bEnabled)
            {
                continue;
            }

            if (Stamp.MaterialSlotIndex != PreviewState.MaterialSlotIndex || Stamp.UVChannelIndex != PreviewState.UVChannelIndex)
            {
                continue;
            }

            RasterizeWetWrinkleAccumulatedStamp(Stamp, PreviewState.WorkingTextureSize, WorkingPixels);
        }

        for (const FWetProceduralRidgeStroke& Stroke : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
        {
            if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != PreviewState.MaterialSlotIndex ||
                Stroke.UVChannelIndex != PreviewState.UVChannelIndex ||
                Stroke.StrokeGuid == EditingProceduralStrokeGuid)
            {
                continue;
            }

            FWetProceduralRidgeRasterizer::Rasterize(Stroke, PreviewState.WorkingTextureSize, WorkingPixels);
        }
    }

    PreviewState.WorkingPixels = MoveTemp(WorkingPixels);
    WetWrinkleTextureRaster::DownsampleNormalPixels(
        PreviewState.WorkingPixels,
        PreviewState.WorkingTextureSize,
        PreviewState.TextureSize,
        PreviewState.Pixels);

    const bool bNeedsNewTexture = PreviewState.AccumulatedNormalTexture == nullptr ||
                                  PreviewState.AccumulatedNormalTexture->GetSizeX() != PreviewState.TextureSize.X ||
                                  PreviewState.AccumulatedNormalTexture->GetSizeY() != PreviewState.TextureSize.Y;
    if (bNeedsNewTexture)
    {
        return InitializeWetWrinklePreviewTexture(
            PreviewState.AccumulatedNormalTexture,
            PreviewState.TextureSize,
            PreviewState.Pixels);
    }

    FTexture2DMipMap& Mip = PreviewState.AccumulatedNormalTexture->GetPlatformData()->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(MipData, PreviewState.Pixels.GetData(), PreviewState.Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    PreviewState.AccumulatedNormalTexture->UpdateResource();
    return true;
}

int32 SWetWrinkleViewport::ResolveActivePreviewMaterialSlot() const
{
    if (PreviewMaterialSlots.IsValidIndex(BrushSettings.MaterialSlotIndex))
    {
        return BrushSettings.MaterialSlotIndex;
    }

    return CurrentSurfaceHit.bHit && PreviewMaterialSlots.IsValidIndex(CurrentSurfaceHit.MaterialSlotIndex)
               ? CurrentSurfaceHit.MaterialSlotIndex
               : INDEX_NONE;
}

void SWetWrinkleViewport::ApplyMaterialSlotVisibility()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* SkeletalMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr)
    {
        return;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        return;
    }

    constexpr int32 PreviewLODIndex = 0;
    if (!RenderData->LODRenderData.IsValidIndex(PreviewLODIndex))
    {
        return;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[PreviewLODIndex];
    for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
    {
        const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
        const bool bShowSection = BrushSettings.MaterialSlotIndex == INDEX_NONE
                                      ? DWCEditorPreviewSlotUtils::IsCpuPreviewReady(WetClothingAsset.Get(), Section.MaterialIndex)
                                      : Section.MaterialIndex == BrushSettings.MaterialSlotIndex;
        PreviewMeshComponent->ShowMaterialSection(Section.MaterialIndex, SectionIndex, bShowSection, PreviewLODIndex);
    }
}

void SWetWrinkleViewport::RebuildHitTriangles()
{
    CachedHitTriangles.Reset();
    CachedHitTriangleLookup.Reset();
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
    UVTriangleGrid.Reset();
    HitTriangleUVChannelIndex = INDEX_NONE;

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh == nullptr || BrushSettings.UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    TArray<FWetClothingAssetUVTriangle> BuiltHitTriangles;
    auto AppendHitTrianglesForUVChannel = [this, TargetMesh, &BuiltHitTriangles](int32 UVChannelIndex)
    {
        const int32 MaterialCount = TargetMesh->GetMaterials().Num();
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
        {
            if (BrushSettings.MaterialSlotIndex != INDEX_NONE && BrushSettings.MaterialSlotIndex != MaterialSlotIndex)
            {
                continue;
            }

            TArray<FWetClothingAssetUVIsland> Islands;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, UVChannelIndex, MaterialSlotIndex, Islands, nullptr))
            {
                continue;
            }

            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                BuiltHitTriangles.Append(Island.UVTriangles);
            }
        }
    };

    AppendHitTrianglesForUVChannel(BrushSettings.UVChannelIndex);
    if (BuiltHitTriangles.Num() == 0)
    {
        return;
    }

    HitTriangleUVChannelIndex = BrushSettings.UVChannelIndex;

    const FTransform ComponentTransform = PreviewMeshComponent != nullptr
                                            ? PreviewMeshComponent->GetComponentTransform()
                                            : FTransform::Identity;
    CachedHitTriangles.Reserve(BuiltHitTriangles.Num());
    for (const FWetClothingAssetUVTriangle& Triangle : BuiltHitTriangles)
    {
        FWetWrinkleCachedHitTriangle& CachedTriangle = CachedHitTriangles.AddDefaulted_GetRef();
        CachedTriangle.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        CachedTriangle.TriangleID = Triangle.TriangleID;
        CachedTriangle.UVIslandID = Triangle.UVIslandID;

        CachedTriangle.WorldBounds = FBox(ForceInit);
        CachedTriangle.UVBounds = FBox2D(ForceInit);
        for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
        {
            CachedTriangle.LocalPositions[VertexIndex] = Triangle.LocalPositions[VertexIndex];
            CachedTriangle.WorldPositions[VertexIndex] = ComponentTransform.TransformPosition(Triangle.LocalPositions[VertexIndex]);
            CachedTriangle.UVs[VertexIndex] = Triangle.UVs[VertexIndex];
            CachedTriangle.WorldBounds += CachedTriangle.WorldPositions[VertexIndex];
            CachedTriangle.UVBounds += Triangle.UVs[VertexIndex];
        }

        CachedTriangle.WorldNormal = FVector::CrossProduct(
            CachedTriangle.WorldPositions[1] - CachedTriangle.WorldPositions[0],
            CachedTriangle.WorldPositions[2] - CachedTriangle.WorldPositions[0]).GetSafeNormal();
        if (CachedTriangle.WorldNormal.IsNearlyZero())
        {
            CachedTriangle.WorldNormal = FVector::UpVector;
        }

        CachedTriangle.WorldTangent = (CachedTriangle.WorldPositions[1] - CachedTriangle.WorldPositions[0]).GetSafeNormal();
        CachedTriangle.WorldTangent = (CachedTriangle.WorldTangent - CachedTriangle.WorldNormal * FVector::DotProduct(CachedTriangle.WorldTangent, CachedTriangle.WorldNormal)).GetSafeNormal();
        if (CachedTriangle.WorldTangent.IsNearlyZero())
        {
            CachedTriangle.WorldTangent = MakeWetWrinkleAnyPerpendicular(CachedTriangle.WorldNormal);
        }

        CachedTriangle.WorldBitangent = FVector::CrossProduct(CachedTriangle.WorldNormal, CachedTriangle.WorldTangent).GetSafeNormal();
        if (CachedTriangle.WorldBitangent.IsNearlyZero())
        {
            CachedTriangle.WorldBitangent = MakeWetWrinkleAnyPerpendicular(CachedTriangle.WorldNormal);
        }

        CachedTriangle.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(CachedTriangle.WorldNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        CachedTriangle.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(CachedTriangle.WorldTangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        CachedTriangle.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(CachedTriangle.WorldBitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    }

    RebuildHitTriangleAccelerationStructures();
}

void SWetWrinkleViewport::RebuildHitTriangleAccelerationStructures()
{
    CachedHitTriangleLookup.Reset();
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
    UVTriangleGrid.Reset();

    if (CachedHitTriangles.IsEmpty())
    {
        return;
    }

    CachedHitTriangleLookup.Reserve(CachedHitTriangles.Num());
    HitBVHTriangleIndices.Reserve(CachedHitTriangles.Num());
    UVTriangleGrid.SetNum(WetWrinkleUVGridResolution * WetWrinkleUVGridResolution);

    for (int32 TriangleIndex = 0; TriangleIndex < CachedHitTriangles.Num(); ++TriangleIndex)
    {
        const FWetWrinkleCachedHitTriangle& Triangle = CachedHitTriangles[TriangleIndex];
        CachedHitTriangleLookup.Add(
            MakeWetWrinkleTriangleLookupKey(Triangle.MaterialSlotIndex, Triangle.TriangleID),
            TriangleIndex);
        HitBVHTriangleIndices.Add(TriangleIndex);

        if (!Triangle.UVBounds.bIsValid)
        {
            continue;
        }

        const int32 MinCellX = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Min.X * WetWrinkleUVGridResolution),
            0,
            WetWrinkleUVGridResolution - 1);
        const int32 MinCellY = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Min.Y * WetWrinkleUVGridResolution),
            0,
            WetWrinkleUVGridResolution - 1);
        const int32 MaxCellX = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Max.X * WetWrinkleUVGridResolution),
            0,
            WetWrinkleUVGridResolution - 1);
        const int32 MaxCellY = FMath::Clamp(
            FMath::FloorToInt(Triangle.UVBounds.Max.Y * WetWrinkleUVGridResolution),
            0,
            WetWrinkleUVGridResolution - 1);
        for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
        {
            for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
            {
                UVTriangleGrid[CellY * WetWrinkleUVGridResolution + CellX].Add(TriangleIndex);
            }
        }
    }

    TFunction<int32(int32, int32)> BuildNode;
    BuildNode = [this, &BuildNode](const int32 FirstIndex, const int32 TriangleCount)
    {
        const int32 NodeIndex = HitBVHNodes.AddDefaulted();
        FBox Bounds(ForceInit);
        FBox CenterBounds(ForceInit);
        for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
        {
            const FWetWrinkleCachedHitTriangle& Triangle = CachedHitTriangles[HitBVHTriangleIndices[FirstIndex + Offset]];
            Bounds += Triangle.WorldBounds;
            CenterBounds += Triangle.WorldBounds.GetCenter();
        }

        HitBVHNodes[NodeIndex].Bounds = Bounds;
        HitBVHNodes[NodeIndex].FirstTriangleIndex = FirstIndex;
        HitBVHNodes[NodeIndex].TriangleCount = TriangleCount;
        if (TriangleCount <= WetWrinkleBVHLeafTriangleCount)
        {
            return NodeIndex;
        }

        const FVector Extent = CenterBounds.GetExtent();
        const int32 SplitAxis = Extent.Y > Extent.X
            ? (Extent.Z > Extent.Y ? 2 : 1)
            : (Extent.Z > Extent.X ? 2 : 0);
        TArrayView<int32> TriangleRange(HitBVHTriangleIndices.GetData() + FirstIndex, TriangleCount);
        Algo::Sort(
            TriangleRange,
            [this, SplitAxis](const int32 A, const int32 B)
            {
                return CachedHitTriangles[A].WorldBounds.GetCenter()[SplitAxis] <
                    CachedHitTriangles[B].WorldBounds.GetCenter()[SplitAxis];
            });

        const int32 LeftCount = TriangleCount / 2;
        const int32 LeftChild = BuildNode(FirstIndex, LeftCount);
        const int32 RightChild = BuildNode(FirstIndex + LeftCount, TriangleCount - LeftCount);
        HitBVHNodes[NodeIndex].LeftChildIndex = LeftChild;
        HitBVHNodes[NodeIndex].RightChildIndex = RightChild;
        HitBVHNodes[NodeIndex].TriangleCount = 0;
        return NodeIndex;
    };
    BuildNode(0, HitBVHTriangleIndices.Num());
}

void SWetWrinkleViewport::HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (AreWetWrinkleSurfaceHitsEquivalentForPreview(CurrentSurfaceHit, SurfaceHit))
    {
        return;
    }

    CurrentSurfaceHit = SurfaceHit;
    RefreshBrushCursor();
    RefreshWrinklePreviewHoverParameters();

    if (OnSurfaceHitChanged.IsBound())
    {
        OnSurfaceHitChanged.Execute(CurrentSurfaceHit);
    }
}

void SWetWrinkleViewport::BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStrokeStarted.IsBound())
    {
        OnPaintStrokeStarted.Execute(SurfaceHit);
    }
}

void SWetWrinkleViewport::RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStampRequested.IsBound())
    {
        OnPaintStampRequested.Execute(SurfaceHit);
    }
}

void SWetWrinkleViewport::EndPaintStrokeFromClient()
{
    if (OnPaintStrokeEnded.IsBound())
    {
        OnPaintStrokeEnded.Execute();
    }
}

void SWetWrinkleViewport::RefreshBrushCursor()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetWrinkleViewport::ClearBrushCursor()
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->Invalidate();
    }
}

void SWetWrinkleViewport::DrawBrushCursor(FPrimitiveDrawInterface* PDI) const
{
    if (PDI == nullptr || !BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    const float Radius = CalculateBrushCursorWorldRadius();
    if (Radius <= UE_SMALL_NUMBER)
    {
        return;
    }

    FVector SurfaceNormal = CurrentSurfaceHit.WorldNormal.GetSafeNormal();
    if (SurfaceNormal.IsNearlyZero())
    {
        SurfaceNormal = FVector::UpVector;
    }

    FVector SurfaceTangent = CurrentSurfaceHit.WorldTangent.GetSafeNormal();
    SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
    if (SurfaceTangent.IsNearlyZero())
    {
        SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
    }
    const FVector SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
    const FVector Center = CurrentSurfaceHit.WorldPosition + SurfaceNormal * FMath::Max(Radius * 0.01f, 0.15f);
    constexpr float Thickness = 2.0f;
    const FLinearColor CursorColor(1.0f, 0.35f, 0.03f, 1.0f);

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        FVector StrokeDirection = SurfaceTangent;
        if (TransientProceduralStrokeHits.Num() >= 2)
        {
            StrokeDirection = TransientProceduralStrokeHits.Last().WorldPosition -
                TransientProceduralStrokeHits[TransientProceduralStrokeHits.Num() - 2].WorldPosition;
            StrokeDirection = (StrokeDirection - SurfaceNormal * FVector::DotProduct(StrokeDirection, SurfaceNormal)).GetSafeNormal();
            if (StrokeDirection.IsNearlyZero())
            {
                StrokeDirection = SurfaceTangent;
            }
        }

        FVector WidthDirection = FVector::CrossProduct(SurfaceNormal, StrokeDirection).GetSafeNormal();
        if (WidthDirection.IsNearlyZero())
        {
            WidthDirection = SurfaceBitangent;
        }

        const float HalfWidth = FMath::Max(Radius * 0.5f, 0.25f);
        const float EndTickLength = FMath::Clamp(HalfWidth * 0.3f, 0.15f, 1.5f);
        const FVector WidthStart = Center - WidthDirection * HalfWidth;
        const FVector WidthEnd = Center + WidthDirection * HalfWidth;
        PDI->DrawLine(WidthStart, WidthEnd, CursorColor, SDPG_Foreground, Thickness, 0.0f, true);
        PDI->DrawLine(
            WidthStart - StrokeDirection * EndTickLength,
            WidthStart + StrokeDirection * EndTickLength,
            CursorColor,
            SDPG_Foreground,
            Thickness,
            0.0f,
            true);
        PDI->DrawLine(
            WidthEnd - StrokeDirection * EndTickLength,
            WidthEnd + StrokeDirection * EndTickLength,
            CursorColor,
            SDPG_Foreground,
            Thickness,
            0.0f,
            true);
        PDI->DrawPoint(Center, CursorColor, 6.0f, SDPG_Foreground);
        return;
    }

    constexpr int32 SegmentCount = 64;
    FVector Previous = Center + SurfaceTangent * Radius;
    for (int32 SegmentIndex = 1; SegmentIndex <= SegmentCount; ++SegmentIndex)
    {
        const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount)) * UE_TWO_PI;
        const FVector Current = Center +
            (SurfaceTangent * FMath::Cos(Angle) + SurfaceBitangent * FMath::Sin(Angle)) * Radius;
        PDI->DrawLine(Previous, Current, CursorColor, SDPG_Foreground, Thickness, 0.0f, true);
        Previous = Current;
    }
}

float SWetWrinkleViewport::CalculateBrushCursorWorldRadius() const
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return 5.0f;
    }

    const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform());
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    return FMath::Clamp(MeshRadius * BrushSettings.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
}

FText SWetWrinkleViewport::GetViewportHintText() const
{
    if (ResolveTargetMesh() == nullptr)
    {
        return LOCTEXT("NoTargetMeshHint", "Assign a Target Mesh or Source Wet Clothing Asset.");
    }

    if (CachedHitTriangles.Num() == 0)
    {
        return LOCTEXT("NoHitTrianglesHint", "No triangles available for the selected UV channel/material slot.");
    }

    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        if (BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Draw &&
            (bTransientProceduralStartJunction || bTransientProceduralEndJunction))
        {
            if (bTransientProceduralStartJunction && bTransientProceduralEndJunction)
            {
                return LOCTEXT("RidgeBothJunctionCandidateHint", "Junction candidate: Start + End");
            }
            return bTransientProceduralStartJunction
                ? LOCTEXT("RidgeStartJunctionCandidateHint", "Junction candidate: Start")
                : LOCTEXT("RidgeEndJunctionCandidateHint", "Junction candidate: End");
        }
        if (BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
        {
            return BrushSettings.bRidgeJunctionModeEnabled
                ? LOCTEXT("RidgeEditViewportHint", "Drag a selected ridge control point. Shift-click a segment to insert a point. Endpoints snap to nearby ridges.")
                : LOCTEXT("RidgeEditNoJunctionViewportHint", "Drag a selected ridge control point. Shift-click a segment to insert a point. Junction snapping is off.");
        }
        return BrushSettings.bRidgeJunctionModeEnabled
            ? LOCTEXT("RidgeDrawViewportHint", "Drag on the mesh to draw a ridge. Endpoints snap to nearby ridges to form junctions.")
            : LOCTEXT("RidgeDrawNoJunctionViewportHint", "Drag on the mesh to draw a ridge. Junction snapping is off.");
    }

    return LOCTEXT("ViewportHint", "Move the cursor over the mesh to inspect wrinkle brush UV hits.");
}

void SWetWrinkleViewport::FindProjectedSurfacesAtUV(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();

    if (PreviewMeshComponent == nullptr || UVChannelIndex != HitTriangleUVChannelIndex)
    {
        return;
    }

    const FVector2D QueryUV(
        UV.X >= 0.0 && UV.X <= 1.0 ? UV.X : WrapWetWrinkleRasterPreviewUV(UV.X),
        UV.Y >= 0.0 && UV.Y <= 1.0 ? UV.Y : WrapWetWrinkleRasterPreviewUV(UV.Y));
    const int32 CellX = FMath::Clamp(
        FMath::FloorToInt(QueryUV.X * WetWrinkleUVGridResolution),
        0,
        WetWrinkleUVGridResolution - 1);
    const int32 CellY = FMath::Clamp(
        FMath::FloorToInt(QueryUV.Y * WetWrinkleUVGridResolution),
        0,
        WetWrinkleUVGridResolution - 1);
    const int32 CellIndex = CellY * WetWrinkleUVGridResolution + CellX;
    const TArray<int32>* CandidateIndices = UVTriangleGrid.IsValidIndex(CellIndex)
        ? &UVTriangleGrid[CellIndex]
        : nullptr;

    auto TestTriangle = [&OutSurfaces, MaterialSlotIndex, &QueryUV](const FWetWrinkleCachedHitTriangle& Triangle)
    {
        if (MaterialSlotIndex != INDEX_NONE && Triangle.MaterialSlotIndex != MaterialSlotIndex)
        {
            return;
        }

        if (Triangle.UVBounds.bIsValid &&
            (QueryUV.X < Triangle.UVBounds.Min.X - 0.0001 || QueryUV.X > Triangle.UVBounds.Max.X + 0.0001 ||
             QueryUV.Y < Triangle.UVBounds.Min.Y - 0.0001 || QueryUV.Y > Triangle.UVBounds.Max.Y + 0.0001))
        {
            return;
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric2D(QueryUV, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        if (!IsWetWrinkleBarycentricInside(Barycentric))
        {
            return;
        }

        FWetWrinkleProjectedSurface ProjectedSurface;
        ProjectedSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        ProjectedSurface.TriangleID = Triangle.TriangleID;
        ProjectedSurface.UVIslandID = Triangle.UVIslandID;
        ProjectedSurface.Barycentric = Barycentric;
        ProjectedSurface.WorldPosition =
            Triangle.WorldPositions[0] * Barycentric.X +
            Triangle.WorldPositions[1] * Barycentric.Y +
            Triangle.WorldPositions[2] * Barycentric.Z;
        ProjectedSurface.WorldNormal = Triangle.WorldNormal;
        ProjectedSurface.WorldTangent = Triangle.WorldTangent;
        ProjectedSurface.WorldBitangent = Triangle.WorldBitangent;
        OutSurfaces.Add(ProjectedSurface);
    };

    if (CandidateIndices != nullptr)
    {
        for (const int32 TriangleIndex : *CandidateIndices)
        {
            if (CachedHitTriangles.IsValidIndex(TriangleIndex))
            {
                TestTriangle(CachedHitTriangles[TriangleIndex]);
            }
        }
    }
    else
    {
        for (const FWetWrinkleCachedHitTriangle& Triangle : CachedHitTriangles)
        {
            TestTriangle(Triangle);
        }
    }
}

bool SWetWrinkleViewport::TryProjectUVToWorld(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    FVector& OutWorldPosition,
    FVector& OutWorldNormal,
    FVector& OutWorldTangent,
    FVector& OutWorldBitangent) const
{
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(MaterialSlotIndex, UVChannelIndex, UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        return false;
    }

    OutWorldPosition = ProjectedSurfaces[0].WorldPosition;
    OutWorldNormal = ProjectedSurfaces[0].WorldNormal;
    OutWorldTangent = ProjectedSurfaces[0].WorldTangent;
    OutWorldBitangent = ProjectedSurfaces[0].WorldBitangent;
    return true;
}

bool SWetWrinkleViewport::ResolveProceduralStrokePointWorld(
    const FWetProceduralRidgeStrokePoint& Point,
    int32 MaterialSlotIndex,
    FVector& OutWorldPosition,
    FVector& OutWorldNormal) const
{
    const int32* CachedTriangleIndex = CachedHitTriangleLookup.Find(
        MakeWetWrinkleTriangleLookupKey(MaterialSlotIndex, Point.AnchorTriangleID));
    const FWetWrinkleCachedHitTriangle* Triangle =
        CachedTriangleIndex != nullptr && CachedHitTriangles.IsValidIndex(*CachedTriangleIndex)
            ? &CachedHitTriangles[*CachedTriangleIndex]
            : nullptr;
    if (Triangle == nullptr)
    {
        return false;
    }

    const FVector Barycentric(Point.AnchorBarycentric);
    OutWorldPosition =
        Triangle->WorldPositions[0] * Barycentric.X +
        Triangle->WorldPositions[1] * Barycentric.Y +
        Triangle->WorldPositions[2] * Barycentric.Z;
    OutWorldNormal = Triangle->WorldNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    return true;
}

bool SWetWrinkleViewport::TryBuildSurfaceHitFromProceduralStrokePoint(
    const FWetProceduralRidgeStrokePoint& Point,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    const int32* CachedTriangleIndex = CachedHitTriangleLookup.Find(
        MakeWetWrinkleTriangleLookupKey(MaterialSlotIndex, Point.AnchorTriangleID));
    const FWetWrinkleCachedHitTriangle* Triangle =
        CachedTriangleIndex != nullptr && CachedHitTriangles.IsValidIndex(*CachedTriangleIndex)
            ? &CachedHitTriangles[*CachedTriangleIndex]
            : nullptr;
    if (Triangle == nullptr || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FVector Barycentric(Point.AnchorBarycentric);
    FVector Normal = Triangle->WorldNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    FVector Tangent = (Triangle->WorldTangent - Normal * FVector::DotProduct(Triangle->WorldTangent, Normal))
                          .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    const FVector WorldPosition =
        Triangle->WorldPositions[0] * Barycentric.X +
        Triangle->WorldPositions[1] * Barycentric.Y +
        Triangle->WorldPositions[2] * Barycentric.Z;
    const FVector LocalPosition =
        Triangle->LocalPositions[0] * Barycentric.X +
        Triangle->LocalPositions[1] * Barycentric.Y +
        Triangle->LocalPositions[2] * Barycentric.Z;
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    OutHit.bHit = true;
    OutHit.MaterialSlotIndex = MaterialSlotIndex;
    OutHit.TriangleID = Triangle->TriangleID;
    OutHit.UVIslandID = Triangle->UVIslandID;
    OutHit.UVChannelIndex = UVChannelIndex;
    OutHit.WorldPosition = WorldPosition;
    OutHit.WorldNormal = Normal;
    OutHit.WorldTangent = Tangent;
    OutHit.WorldBitangent = Bitangent;
    OutHit.LocalPosition = LocalPosition;
    OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Bitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    OutHit.UV = Point.PositionUV;
    OutHit.Barycentric = Barycentric;
    OutHit.DistanceSq = 0.0;
    return true;
}

void SWetWrinkleViewport::DrawProceduralStrokeGuides(FPrimitiveDrawInterface* PDI) const
{
    if (PDI == nullptr)
    {
        return;
    }

    constexpr float GuideOffset = 0.35f;
    constexpr float GuideThickness = 2.0f;
    const FLinearColor StoredColor(1.0f, 0.35f, 0.05f, 1.0f);
    const FLinearColor TransientColor(0.0f, 0.85f, 1.0f, 1.0f);
    const FLinearColor JunctionColor(1.0f, 0.72f, 0.05f, 1.0f);
    const FLinearColor FlaredColor(0.85f, 0.45f, 1.0f, 1.0f);

    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        if (const FWetProceduralRidgeStroke* Stroke = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                [this](const FWetProceduralRidgeStroke& Candidate)
                {
                    return Candidate.StrokeGuid == SelectedProceduralStrokeGuid;
                }))
        {
            FVector Previous = FVector::ZeroVector;
            bool bHasPrevious = false;
            for (int32 PointIndex = 0; PointIndex < Stroke->Points.Num(); ++PointIndex)
            {
                const FWetProceduralRidgeStrokePoint& Point = Stroke->Points[PointIndex];
                FVector Position = FVector::ZeroVector;
                FVector Normal = FVector::UpVector;
                if (!ResolveProceduralStrokePointWorld(Point, Stroke->MaterialSlotIndex, Position, Normal))
                {
                    bHasPrevious = false;
                    continue;
                }

                Position += Normal * GuideOffset;
                if (bHasPrevious)
                {
                    PDI->DrawLine(Previous, Position, StoredColor, SDPG_Foreground, GuideThickness, 0.0f, true);
                }
                Previous = Position;
                bHasPrevious = true;

                const bool bStartJunction = PointIndex == 0 && Stroke->StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Junction;
                const bool bEndJunction = PointIndex == Stroke->Points.Num() - 1 && Stroke->EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Junction;
                const bool bJunction = bStartJunction || bEndJunction;
                const bool bStartFlared = PointIndex == 0 && Stroke->StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared;
                const bool bEndFlared = PointIndex == Stroke->Points.Num() - 1 && Stroke->EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared;
                const bool bFlared = bStartFlared || bEndFlared;
                const FLinearColor PointColor = bJunction
                    ? JunctionColor
                    : (bFlared
                           ? FlaredColor
                           : (PointIndex == SelectedProceduralStrokePointIndex ? FLinearColor::White : StoredColor));
                PDI->DrawPoint(
                    Position,
                    PointColor,
                    bJunction || bFlared || PointIndex == SelectedProceduralStrokePointIndex ? 10.0f : 6.0f,
                    SDPG_Foreground);
            }
        }
    }

    for (int32 PointIndex = 1; PointIndex < TransientProceduralStrokeHits.Num(); ++PointIndex)
    {
        const FWetWrinkleSurfaceHit& PreviousHit = TransientProceduralStrokeHits[PointIndex - 1];
        const FWetWrinkleSurfaceHit& CurrentHit = TransientProceduralStrokeHits[PointIndex];
        const FVector Previous = PreviousHit.WorldPosition + PreviousHit.WorldNormal * GuideOffset;
        const FVector Current = CurrentHit.WorldPosition + CurrentHit.WorldNormal * GuideOffset;
        PDI->DrawLine(Previous, Current, TransientColor, SDPG_Foreground, GuideThickness, 0.0f, true);
    }

    if (!TransientProceduralStrokeHits.IsEmpty())
    {
        const FWetWrinkleSurfaceHit& First = TransientProceduralStrokeHits[0];
        const FWetWrinkleSurfaceHit& Last = TransientProceduralStrokeHits.Last();
        PDI->DrawPoint(
            First.WorldPosition + First.WorldNormal * GuideOffset,
            bTransientProceduralStartJunction ? JunctionColor : TransientColor,
            bTransientProceduralStartJunction ? 10.0f : 7.0f,
            SDPG_Foreground);
        if (TransientProceduralStrokeHits.Num() > 1)
        {
            PDI->DrawPoint(
                Last.WorldPosition + Last.WorldNormal * GuideOffset,
                bTransientProceduralEndJunction ? JunctionColor : TransientColor,
                bTransientProceduralEndJunction ? 10.0f : 7.0f,
                SDPG_Foreground);
        }
    }
}

void SWetWrinkleViewport::CancelPaintStrokeFromClient()
{
    if (OnPaintStrokeCanceled.IsBound())
    {
        OnPaintStrokeCanceled.Execute();
    }
}

#undef LOCTEXT_NAMESPACE
