#include "WetWrinkleAssetViewport.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "WetClothing/Texture/WetClothingMaterialTextureResolver.h"
#include "WetWrinkleAssetViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAssetViewport"

namespace
{
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

    FIntPoint ComputeWetWrinklePreviewTextureSize(const UTexture* SourceTexture)
    {
        constexpr int32 MaxPreviewDimension = 256;
        constexpr int32 MinPreviewDimension = 128;

        const int32 SourceSizeX = SourceTexture != nullptr ? FMath::Max(1, SourceTexture->GetSurfaceWidth()) : 512;
        const int32 SourceSizeY = SourceTexture != nullptr ? FMath::Max(1, SourceTexture->GetSurfaceHeight()) : 512;
        const int32 LargestDimension = FMath::Max(SourceSizeX, SourceSizeY);
        if (LargestDimension <= MaxPreviewDimension)
        {
            return FIntPoint(SourceSizeX, SourceSizeY);
        }

        const float Scale = static_cast<float>(MaxPreviewDimension) / static_cast<float>(LargestDimension);
        return FIntPoint(
            FMath::Max(MinPreviewDimension, FMath::RoundToInt(static_cast<float>(SourceSizeX) * Scale)),
            FMath::Max(MinPreviewDimension, FMath::RoundToInt(static_cast<float>(SourceSizeY) * Scale)));
    }

    struct FWetWrinklePreviewStampData
    {
        FVector2D CenterUV = FVector2D::ZeroVector;
        float RadiusUV = 0.025f;
        float RotationRadians = 0.0f;
        FVector2D Scale = FVector2D(1.0f, 1.0f);
        float Strength = 1.0f;
        float Falloff = 0.5f;
        TObjectPtr<UTexture2D> BrushHeightTexture = nullptr;
    };

    void AppendWetWrinkleRingMesh(
        TArray<FVector>& Vertices,
        TArray<int32>& Indices,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UVs,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents,
        const FVector& Center,
        const FVector& Normal,
        const FVector& Tangent,
        const FVector& Bitangent,
        float Radius,
        float InnerRadius,
        const FLinearColor& Color)
    {
        constexpr int32 SegmentCount = 32;
        const int32 BaseVertexIndex = Vertices.Num();

        for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const float Angle = (static_cast<float>(SegmentIndex) / static_cast<float>(SegmentCount)) * UE_TWO_PI;
            const FVector Direction = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);

            Vertices.Add(Center + Direction * Radius);
            Vertices.Add(Center + Direction * InnerRadius);
            Normals.Add(Normal);
            Normals.Add(Normal);
            UVs.Add(FVector2D(1.0f, 0.0f));
            UVs.Add(FVector2D(0.0f, 0.0f));
            VertexColors.Add(Color);
            VertexColors.Add(Color);
            Tangents.Add(FProcMeshTangent(Tangent, false));
            Tangents.Add(FProcMeshTangent(Tangent, false));
        }

        for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
        {
            const int32 NextSegmentIndex = (SegmentIndex + 1) % SegmentCount;
            const int32 OuterA = BaseVertexIndex + SegmentIndex * 2;
            const int32 InnerA = OuterA + 1;
            const int32 OuterB = BaseVertexIndex + NextSegmentIndex * 2;
            const int32 InnerB = OuterB + 1;

            Indices.Add(OuterA);
            Indices.Add(OuterB);
            Indices.Add(InnerB);

            Indices.Add(OuterA);
            Indices.Add(InnerB);
            Indices.Add(InnerA);
        }
    }

    struct FWetWrinkleBrushHeightSource
    {
        explicit FWetWrinkleBrushHeightSource(UTexture2D* InTexture)
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

            MipData = Texture->Source.LockMipReadOnly(0);
        }

        ~FWetWrinkleBrushHeightSource()
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

        float SampleHeight(const FVector2D& UV) const
        {
            if (!IsValid())
            {
                return 0.5f;
            }

            const float ClampedU = FMath::Clamp(UV.X, 0.0f, 0.999f);
            const float ClampedV = FMath::Clamp(UV.Y, 0.0f, 0.999f);
            const int32 PixelX = FMath::Clamp(FMath::FloorToInt(ClampedU * static_cast<float>(SizeX)), 0, SizeX - 1);
            const int32 PixelY = FMath::Clamp(FMath::FloorToInt(ClampedV * static_cast<float>(SizeY)), 0, SizeY - 1);

            if (SourceFormat == TSF_G8)
            {
                const uint8 Value = MipData[PixelY * SizeX + PixelX];
                return static_cast<float>(Value) / 255.0f;
            }

            if (SourceFormat == TSF_G16)
            {
                const uint16* HeightData = reinterpret_cast<const uint16*>(MipData);
                const uint16 Value = HeightData[PixelY * SizeX + PixelX];
                return static_cast<float>(Value) / 65535.0f;
            }

            const FColor* ColorData = reinterpret_cast<const FColor*>(MipData);
            const FColor Color = ColorData[PixelY * SizeX + PixelX];
            return (static_cast<float>(Color.R) + static_cast<float>(Color.G) + static_cast<float>(Color.B)) / (3.0f * 255.0f);
        }

        FVector2D SampleGradient(const FVector2D& UV, float SampleDistanceUV) const
        {
            const FVector2D OffsetU(SampleDistanceUV, 0.0f);
            const FVector2D OffsetV(0.0f, SampleDistanceUV);
            return FVector2D(
                SampleHeight(UV + OffsetU) - SampleHeight(UV - OffsetU),
                SampleHeight(UV + OffsetV) - SampleHeight(UV - OffsetV));
        }

        UTexture2D* Texture = nullptr;
        const uint8* MipData = nullptr;
        int32 SizeX = 0;
        int32 SizeY = 0;
        ETextureSourceFormat SourceFormat = TSF_Invalid;
    };

    bool EnsureWetWrinkleTransientNormalTexture(UTexture2D*& InOutTexture, int32 SizeX, int32 SizeY)
    {
        if (InOutTexture != nullptr && InOutTexture->GetSizeX() == SizeX && InOutTexture->GetSizeY() == SizeY)
        {
            return true;
        }

        InOutTexture = UTexture2D::CreateTransient(SizeX, SizeY, PF_B8G8R8A8);
        if (InOutTexture == nullptr)
        {
            return false;
        }

        InOutTexture->SRGB = false;
        InOutTexture->CompressionSettings = TC_Normalmap;
        InOutTexture->MipGenSettings = TMGS_NoMipmaps;
        InOutTexture->AddressX = TA_Wrap;
        InOutTexture->AddressY = TA_Wrap;
        InOutTexture->Filter = TF_Bilinear;
        InOutTexture->NeverStream = true;
        InOutTexture->UpdateResource();
        return true;
    }

    void UpdateWetWrinkleTransientNormalTexture(UTexture2D* Texture, const TArray<FColor>& Pixels)
    {
        if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
        {
            return;
        }

        FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
        void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
        if (TextureData != nullptr)
        {
            const SIZE_T CopySize = static_cast<SIZE_T>(Pixels.Num()) * sizeof(FColor);
            FMemory::Memcpy(TextureData, Pixels.GetData(), CopySize);
        }
        Mip.BulkData.Unlock();
        Texture->UpdateResource();
    }

    void RasterizeWetWrinklePreviewStamp(
        const FWetWrinklePreviewStampData& Stamp,
        TArray<float>& HeightBuffer,
        int32 SizeX,
        int32 SizeY,
        bool& bOutTouched)
    {
        if (Stamp.BrushHeightTexture == nullptr || Stamp.RadiusUV <= 0.0f || Stamp.Strength <= 0.0f)
        {
            return;
        }

        FWetWrinkleBrushHeightSource HeightSource(Stamp.BrushHeightTexture);
        if (!HeightSource.IsValid() || HeightBuffer.Num() != SizeX * SizeY)
        {
            return;
        }

        const FVector2D WrappedCenterUV = WrapWetWrinkleRasterPreviewUV(Stamp.CenterUV);
        const FVector2D SafeScale(
            FMath::Max(FMath::Abs(Stamp.Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Stamp.Scale.Y), UE_SMALL_NUMBER));
        const float CosRotation = FMath::Cos(Stamp.RotationRadians);
        const float SinRotation = FMath::Sin(Stamp.RotationRadians);
        const float EdgeFadeStart = FMath::Clamp(1.0f - Stamp.Falloff, 0.0f, 0.98f);

        for (int32 PixelY = 0; PixelY < SizeY; ++PixelY)
        {
            const float SampleV = (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(SizeY);
            for (int32 PixelX = 0; PixelX < SizeX; ++PixelX)
            {
                const float SampleU = (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(SizeX);
                FVector2D DeltaUV(SampleU - WrappedCenterUV.X, SampleV - WrappedCenterUV.Y);
                DeltaUV.X = ComputeWrappedWetWrinkleDelta(DeltaUV.X);
                DeltaUV.Y = ComputeWrappedWetWrinkleDelta(DeltaUV.Y);

                const float LocalX = (CosRotation * DeltaUV.X + SinRotation * DeltaUV.Y) / (Stamp.RadiusUV * SafeScale.X);
                const float LocalY = (-SinRotation * DeltaUV.X + CosRotation * DeltaUV.Y) / (Stamp.RadiusUV * SafeScale.Y);
                const float DistanceFromCenter = FMath::Sqrt(LocalX * LocalX + LocalY * LocalY);
                if (DistanceFromCenter > 1.0f)
                {
                    continue;
                }

                const float EdgeFade = 1.0f - FMath::SmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
                if (EdgeFade <= UE_SMALL_NUMBER)
                {
                    continue;
                }

                const FVector2D BrushUV(LocalX * 0.5f + 0.5f, LocalY * 0.5f + 0.5f);
                const float HeightSample = HeightSource.SampleHeight(BrushUV);
                const int32 BufferIndex = PixelY * SizeX + PixelX;
                HeightBuffer[BufferIndex] += (HeightSample - 0.5f) * Stamp.Strength * EdgeFade;
                bOutTouched = true;
            }
        }
    }

    void ConvertWetWrinkleHeightBufferToNormalPixels(
        const TArray<float>& HeightBuffer,
        int32 SizeX,
        int32 SizeY,
        TArray<FColor>& OutPixels)
    {
        constexpr float PreviewNormalStrength = 14.0f;

        OutPixels.SetNumUninitialized(SizeX * SizeY);
        auto SampleHeight = [&HeightBuffer, SizeX, SizeY](int32 X, int32 Y) -> float
        {
            const int32 WrappedX = (X % SizeX + SizeX) % SizeX;
            const int32 WrappedY = (Y % SizeY + SizeY) % SizeY;
            return FMath::Clamp(0.5f + HeightBuffer[WrappedY * SizeX + WrappedX], 0.0f, 1.0f);
        };

        for (int32 PixelY = 0; PixelY < SizeY; ++PixelY)
        {
            for (int32 PixelX = 0; PixelX < SizeX; ++PixelX)
            {
                const float HeightLeft = SampleHeight(PixelX - 1, PixelY);
                const float HeightRight = SampleHeight(PixelX + 1, PixelY);
                const float HeightDown = SampleHeight(PixelX, PixelY - 1);
                const float HeightUp = SampleHeight(PixelX, PixelY + 1);

                const float GradientX = HeightRight - HeightLeft;
                const float GradientY = HeightUp - HeightDown;
                const FVector PreviewNormal = FVector(-GradientX * PreviewNormalStrength, -GradientY * PreviewNormalStrength, 1.0f).GetSafeNormal();

                OutPixels[PixelY * SizeX + PixelX] = FColor(
                    static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((PreviewNormal.X * 0.5f + 0.5f) * 255.0f), 0, 255)),
                    static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((PreviewNormal.Y * 0.5f + 0.5f) * 255.0f), 0, 255)),
                    static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((PreviewNormal.Z * 0.5f + 0.5f) * 255.0f), 0, 255)),
                    255);
            }
        }
    }

    struct FWetWrinkleProceduralMeshBuffers
    {
        TArray<FVector> Vertices;
        TArray<int32> Indices;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> VertexColors;
        TArray<FProcMeshTangent> Tangents;
    };

    void AppendWetWrinkleDisplacedPatchMesh(
        TArray<FVector>& Vertices,
        TArray<int32>& Indices,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UVs,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents,
        UTexture2D* BrushHeightTexture,
        const FVector& Center,
        const FVector& Normal,
        const FVector& Tangent,
        const FVector& Bitangent,
        const FVector2D& CenterMeshUV,
        float RadiusWorld,
        float RadiusUV,
        float RotationRadians,
        const FVector2D& Scale,
        float Strength,
        float Falloff,
        float SurfaceOffset,
        const FLinearColor& VertexColor)
    {
        if (BrushHeightTexture == nullptr || RadiusWorld <= 0.0f || RadiusUV <= 0.0f || Strength <= 0.0f)
        {
            return;
        }

        FWetWrinkleBrushHeightSource HeightSource(BrushHeightTexture);
        if (!HeightSource.IsValid())
        {
            return;
        }

        constexpr int32 GridSize = 20;
        constexpr float GradientSampleDistanceUV = 1.0f / (GridSize * 2.0f);
        const float HeightAmplitude = FMath::Max(RadiusWorld * (0.12f + Strength * 0.28f), 0.2f);
        const float EdgeFadeStart = FMath::Clamp(1.0f - Falloff, 0.0f, 0.98f);
        const FVector2D SafeScale(
            FMath::Max(FMath::Abs(Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Scale.Y), UE_SMALL_NUMBER));
        const float CosRotation = FMath::Cos(RotationRadians);
        const float SinRotation = FMath::Sin(RotationRadians);
        const FVector RotatedTangent = (Tangent * CosRotation + Bitangent * SinRotation).GetSafeNormal(UE_SMALL_NUMBER, Tangent);
        const FVector RotatedBitangent = (-Tangent * SinRotation + Bitangent * CosRotation).GetSafeNormal(UE_SMALL_NUMBER, Bitangent);
        const int32 VertexGridSize = GridSize + 1;
        TArray<int32> VertexIndexGrid;
        VertexIndexGrid.Init(INDEX_NONE, VertexGridSize * VertexGridSize);

        auto GetVertexGridIndex = [VertexGridSize](int32 X, int32 Y)
        {
            return Y * VertexGridSize + X;
        };

        for (int32 GridY = 0; GridY <= GridSize; ++GridY)
        {
            for (int32 GridX = 0; GridX <= GridSize; ++GridX)
            {
                const FVector2D BrushUV(
                    static_cast<float>(GridX) / static_cast<float>(GridSize),
                    static_cast<float>(GridY) / static_cast<float>(GridSize));
                const FVector2D Local = (BrushUV - FVector2D(0.5f, 0.5f)) * 2.0f;
                const float DistanceFromCenter = Local.Size();
                if (DistanceFromCenter > 1.0f)
                {
                    continue;
                }

                const float EdgeFade = 1.0f - FMath::SmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
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

                const FVector2D HeightBrushUV(BrushLocalX * 0.5f + 0.5f, BrushLocalY * 0.5f + 0.5f);
                const float Height = HeightSource.SampleHeight(HeightBrushUV);
                const float SignedHeight = (Height - 0.5f) * 2.0f;
                const FVector2D HeightGradient = HeightSource.SampleGradient(HeightBrushUV, GradientSampleDistanceUV);
                const FVector PreviewNormal =
                    (Normal -
                     RotatedTangent * (HeightGradient.X * Strength * 3.0f) -
                     RotatedBitangent * (HeightGradient.Y * Strength * 3.0f))
                        .GetSafeNormal(UE_SMALL_NUMBER, Normal);
                const FVector SurfacePoint =
                    Center +
                    Tangent * (Local.X * RadiusWorld) +
                    Bitangent * (Local.Y * RadiusWorld) +
                    Normal * (SurfaceOffset + SignedHeight * HeightAmplitude * EdgeFade);
                const FVector2D MeshUV = CenterMeshUV + FVector2D(Local.X * RadiusUV, Local.Y * RadiusUV);

                VertexIndexGrid[GetVertexGridIndex(GridX, GridY)] = Vertices.Num();
                Vertices.Add(SurfacePoint);
                Normals.Add(PreviewNormal);
                UVs.Add(MeshUV);
                VertexColors.Add(VertexColor);
                Tangents.Add(FProcMeshTangent(Tangent, false));
            }
        }

        for (int32 GridY = 0; GridY < GridSize; ++GridY)
        {
            for (int32 GridX = 0; GridX < GridSize; ++GridX)
            {
                const int32 Vertex00 = VertexIndexGrid[GetVertexGridIndex(GridX, GridY)];
                const int32 Vertex10 = VertexIndexGrid[GetVertexGridIndex(GridX + 1, GridY)];
                const int32 Vertex01 = VertexIndexGrid[GetVertexGridIndex(GridX, GridY + 1)];
                const int32 Vertex11 = VertexIndexGrid[GetVertexGridIndex(GridX + 1, GridY + 1)];

                if (Vertex00 != INDEX_NONE && Vertex10 != INDEX_NONE && Vertex11 != INDEX_NONE)
                {
                    Indices.Add(Vertex00);
                    Indices.Add(Vertex10);
                    Indices.Add(Vertex11);
                }

                if (Vertex00 != INDEX_NONE && Vertex11 != INDEX_NONE && Vertex01 != INDEX_NONE)
                {
                    Indices.Add(Vertex00);
                    Indices.Add(Vertex11);
                    Indices.Add(Vertex01);
                }
            }
        }
    }

    void AppendWetWrinkleRaisedPatchMesh(
        TArray<FVector>& Vertices,
        TArray<int32>& Indices,
        TArray<FVector>& Normals,
        TArray<FVector2D>& UVs,
        TArray<FLinearColor>& VertexColors,
        TArray<FProcMeshTangent>& Tangents,
        const FVector& Center,
        const FVector& Normal,
        const FVector& Tangent,
        const FVector& Bitangent,
        const FVector2D& CenterMeshUV,
        float RadiusWorld,
        float RadiusUV,
        float Strength,
        float Falloff,
        float SurfaceOffset,
        const FLinearColor& VertexColor)
    {
        if (RadiusWorld <= 0.0f || Strength <= 0.0f)
        {
            return;
        }

        constexpr int32 GridSize = 24;
        const float EdgeFadeStart = FMath::Clamp(1.0f - Falloff, 0.0f, 0.98f);
        const float HeightAmplitude = FMath::Max(RadiusWorld * (0.14f + Strength * 0.30f), 0.5f);
        const int32 VertexGridSize = GridSize + 1;
        TArray<int32> VertexIndexGrid;
        VertexIndexGrid.Init(INDEX_NONE, VertexGridSize * VertexGridSize);

        auto GetVertexGridIndex = [VertexGridSize](int32 X, int32 Y)
        {
            return Y * VertexGridSize + X;
        };

        const int32 BaseVertexIndex = Vertices.Num();
        const int32 BaseIndexIndex = Indices.Num();

        for (int32 GridY = 0; GridY <= GridSize; ++GridY)
        {
            for (int32 GridX = 0; GridX <= GridSize; ++GridX)
            {
                const FVector2D BrushUV(
                    static_cast<float>(GridX) / static_cast<float>(GridSize),
                    static_cast<float>(GridY) / static_cast<float>(GridSize));
                const FVector2D Local = (BrushUV - FVector2D(0.5f, 0.5f)) * 2.0f;
                const float DistanceFromCenter = Local.Size();
                if (DistanceFromCenter > 1.0f)
                {
                    continue;
                }

                const float HeightMask = 1.0f - FMath::SmoothStep(EdgeFadeStart, 1.0f, DistanceFromCenter);
                if (HeightMask <= UE_SMALL_NUMBER)
                {
                    continue;
                }

                const FVector SurfacePoint =
                    Center +
                    Tangent * (Local.X * RadiusWorld) +
                    Bitangent * (Local.Y * RadiusWorld) +
                    Normal * (SurfaceOffset + HeightAmplitude * HeightMask);
                const FVector2D MeshUV = CenterMeshUV + FVector2D(Local.X * RadiusUV, Local.Y * RadiusUV);

                VertexIndexGrid[GetVertexGridIndex(GridX, GridY)] = Vertices.Num();
                Vertices.Add(SurfacePoint);
                Normals.Add(FVector::ZeroVector);
                UVs.Add(MeshUV);
                VertexColors.Add(VertexColor);
                Tangents.Add(FProcMeshTangent(Tangent, false));
            }
        }

        for (int32 GridY = 0; GridY < GridSize; ++GridY)
        {
            for (int32 GridX = 0; GridX < GridSize; ++GridX)
            {
                const int32 Vertex00 = VertexIndexGrid[GetVertexGridIndex(GridX, GridY)];
                const int32 Vertex10 = VertexIndexGrid[GetVertexGridIndex(GridX + 1, GridY)];
                const int32 Vertex01 = VertexIndexGrid[GetVertexGridIndex(GridX, GridY + 1)];
                const int32 Vertex11 = VertexIndexGrid[GetVertexGridIndex(GridX + 1, GridY + 1)];

                if (Vertex00 != INDEX_NONE && Vertex10 != INDEX_NONE && Vertex11 != INDEX_NONE)
                {
                    Indices.Add(Vertex00);
                    Indices.Add(Vertex10);
                    Indices.Add(Vertex11);
                }

                if (Vertex00 != INDEX_NONE && Vertex11 != INDEX_NONE && Vertex01 != INDEX_NONE)
                {
                    Indices.Add(Vertex00);
                    Indices.Add(Vertex11);
                    Indices.Add(Vertex01);
                }
            }
        }

        for (int32 VertexIndex = BaseVertexIndex; VertexIndex < Normals.Num(); ++VertexIndex)
        {
            Normals[VertexIndex] = FVector::ZeroVector;
        }

        for (int32 IndexIndex = BaseIndexIndex; IndexIndex + 2 < Indices.Num(); IndexIndex += 3)
        {
            const int32 VertexAIndex = Indices[IndexIndex];
            const int32 VertexBIndex = Indices[IndexIndex + 1];
            const int32 VertexCIndex = Indices[IndexIndex + 2];

            const FVector FaceNormal = FVector::CrossProduct(
                                           Vertices[VertexBIndex] - Vertices[VertexAIndex],
                                           Vertices[VertexCIndex] - Vertices[VertexAIndex])
                                           .GetSafeNormal();
            if (FaceNormal.IsNearlyZero())
            {
                continue;
            }

            Normals[VertexAIndex] += FaceNormal;
            Normals[VertexBIndex] += FaceNormal;
            Normals[VertexCIndex] += FaceNormal;
        }

        for (int32 VertexIndex = BaseVertexIndex; VertexIndex < Normals.Num(); ++VertexIndex)
        {
            Normals[VertexIndex] = Normals[VertexIndex].GetSafeNormal(UE_SMALL_NUMBER, Normal);
        }
    }
} // namespace

void SWetWrinkleAssetViewport::Construct(const FArguments& InArgs)
{
    WetWrinkleAsset = InArgs._WetWrinkleAsset;
    OnSurfaceHitChanged = InArgs._OnSurfaceHitChanged;
    OnPaintStrokeStarted = InArgs._OnPaintStrokeStarted;
    OnPaintStampRequested = InArgs._OnPaintStampRequested;
    OnPaintStrokeEnded = InArgs._OnPaintStrokeEnded;
    PreviewScene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());

    SEditorViewport::Construct(SEditorViewport::FArguments());

    PreviewMeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    PreviewMeshComponent->SetMobility(EComponentMobility::Movable);
    PreviewMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreviewScene->AddComponent(PreviewMeshComponent, FTransform::Identity);

    BrushCursorComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    BrushCursorComponent->SetMobility(EComponentMobility::Movable);
    BrushCursorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BrushCursorComponent->SetCastShadow(false);
    BrushCursorComponent->SetVisibility(false, true);
    BrushCursorComponent->bUseAsyncCooking = false;
    BrushCursorComponent->SetMaterial(0, ResolveCursorMaterial());
    PreviewScene->AddComponent(BrushCursorComponent, FTransform::Identity);

    StoredStampOverlayComponent = NewObject<UProceduralMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    StoredStampOverlayComponent->SetMobility(EComponentMobility::Movable);
    StoredStampOverlayComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StoredStampOverlayComponent->SetCastShadow(false);
    StoredStampOverlayComponent->bUseAsyncCooking = false;
    StoredStampOverlayComponent->SetMaterial(0, ResolveCursorMaterial());
    PreviewScene->AddComponent(StoredStampOverlayComponent, FTransform::Identity);

    RefreshPreviewMesh();
}

SWetWrinkleAssetViewport::~SWetWrinkleAssetViewport()
{
    if (PreviewScene.IsValid() && PreviewMeshComponent != nullptr)
    {
        PreviewScene->RemoveComponent(PreviewMeshComponent);
    }

    if (PreviewScene.IsValid() && BrushCursorComponent != nullptr)
    {
        PreviewScene->RemoveComponent(BrushCursorComponent);
    }

    if (PreviewScene.IsValid() && StoredStampOverlayComponent != nullptr)
    {
        PreviewScene->RemoveComponent(StoredStampOverlayComponent);
    }
}

void SWetWrinkleAssetViewport::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(PreviewMeshComponent);
    Collector.AddReferencedObject(BrushCursorComponent);
    Collector.AddReferencedObject(StoredStampOverlayComponent);
    Collector.AddReferencedObject(CursorMaterial);
    for (TObjectPtr<UMaterialInterface>& Material : PreviewBaseMaterials)
    {
        Collector.AddReferencedObject(Material);
    }
    for (TObjectPtr<UMaterialInstanceDynamic>& MID : PreviewMaterialInstances)
    {
        Collector.AddReferencedObject(MID);
    }
    for (TObjectPtr<UTexture2D>& Texture : PreviewWrinkleNormalTextures)
    {
        Collector.AddReferencedObject(Texture);
    }
    Collector.AddReferencedObject(BrushSettings.BrushHeightTexture);
}

void SWetWrinkleAssetViewport::RefreshPreviewMesh()
{
    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    InitializePreviewMaterialInstances();
    ApplyPreviewWetVertexColors();
    ApplyMaterialSlotVisibility();
    RebuildHitTriangles();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    RefreshStoredStampOverlay();
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
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, true);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
        ViewportClient->Invalidate();
    }
    else
    {
        Invalidate();
    }
}

void SWetWrinkleAssetViewport::SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    BrushSettings = InBrushSettings;
    ApplyMaterialSlotVisibility();

    if (bNeedsTriangleRebuild)
    {
        RebuildHitTriangles();
        RefreshStoredStampOverlay();
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleAssetViewport::RefreshStoredStampOverlay()
{
    if (StoredStampOverlayComponent == nullptr)
    {
        return;
    }

    StoredStampOverlayComponent->ClearAllMeshSections();

    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || HitTriangles.Num() == 0)
    {
        return;
    }

    if (PreviewMaterialInstances.Num() != PreviewMeshComponent->GetNumMaterials())
    {
        InitializePreviewMaterialInstances();
    }

    TMap<int32, int32> MaterialSlotToBufferIndex;
    TArray<FWetWrinkleProceduralMeshBuffers> MaterialBuffers;
    auto GetOrAddMaterialBuffer = [&MaterialSlotToBufferIndex, &MaterialBuffers](int32 MaterialSlotIndex) -> FWetWrinkleProceduralMeshBuffers&
    {
        if (const int32* ExistingIndex = MaterialSlotToBufferIndex.Find(MaterialSlotIndex))
        {
            return MaterialBuffers[*ExistingIndex];
        }

        const int32 NewIndex = MaterialBuffers.AddDefaulted();
        MaterialSlotToBufferIndex.Add(MaterialSlotIndex, NewIndex);
        return MaterialBuffers[NewIndex];
    };

    const FBoxSphereBounds Bounds = PreviewMeshComponent != nullptr
                                        ? PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform())
                                        : FBoxSphereBounds(FSphere(FVector::ZeroVector, 1.0f));
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    const FTransform ComponentTransform = PreviewMeshComponent != nullptr
                                              ? PreviewMeshComponent->GetComponentTransform()
                                              : FTransform::Identity;
    const FVector ViewLocation = ViewportClient.IsValid() ? ViewportClient->GetViewLocation() : FVector::ZeroVector;
    const bool bHasViewLocation = ViewportClient.IsValid();

    for (const FWetWrinkleStroke& Stroke : Asset->Strokes)
    {
        if (!Stroke.bEnabled)
        {
            continue;
        }

        for (const FWetWrinkleStamp& Stamp : Stroke.Stamps)
        {
            if (Stamp.UVChannelIndex != BrushSettings.UVChannelIndex)
            {
                continue;
            }

            if (BrushSettings.MaterialSlotIndex != INDEX_NONE && Stamp.MaterialSlotIndex != BrushSettings.MaterialSlotIndex)
            {
                continue;
            }

            TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
            FindProjectedSurfacesAtUV(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex, Stamp.PositionUV, ProjectedSurfaces);
            if (ProjectedSurfaces.Num() == 0)
            {
                continue;
            }

            int32 PrimarySurfaceIndex = 0;
#if WITH_EDITORONLY_DATA
            if (Stamp.bHasEditorSurface)
            {
                const FVector PrimaryWorldPosition = ComponentTransform.TransformPosition(Stamp.EditorSurfaceLocalPosition);
                double ClosestDistanceSq = TNumericLimits<double>::Max();
                for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
                {
                    const double DistanceSq = FVector::DistSquared(PrimaryWorldPosition, ProjectedSurfaces[SurfaceIndex].WorldPosition);
                    if (DistanceSq < ClosestDistanceSq)
                    {
                        ClosestDistanceSq = DistanceSq;
                        PrimarySurfaceIndex = SurfaceIndex;
                    }
                }
            }
#endif

            const float Radius = FMath::Clamp(MeshRadius * Stamp.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
            const float SurfaceOffset = FMath::Max(Radius * 0.005f, 0.05f);
            const FVector PrimarySurfacePosition = ProjectedSurfaces[PrimarySurfaceIndex].WorldPosition;
            FWetWrinkleProceduralMeshBuffers& MaterialBuffer = GetOrAddMaterialBuffer(Stamp.MaterialSlotIndex);

            for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
            {
                const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[SurfaceIndex];
                const bool bPrimarySurface = SurfaceIndex == PrimarySurfaceIndex;
                if (!bPrimarySurface && !IsWetWrinkleLinkedSurface(PrimarySurfacePosition, Surface.WorldPosition, Radius))
                {
                    continue;
                }

                FVector SurfaceNormal = Surface.WorldNormal.GetSafeNormal();
                if (SurfaceNormal.IsNearlyZero())
                {
                    SurfaceNormal = FVector::UpVector;
                }
                if (bHasViewLocation && FVector::DotProduct(SurfaceNormal, ViewLocation - Surface.WorldPosition) < 0.0)
                {
                    SurfaceNormal *= -1.0;
                }

                FVector SurfaceTangent = Surface.WorldTangent.GetSafeNormal();
                SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
                if (SurfaceTangent.IsNearlyZero())
                {
                    SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
                }

                FVector SurfaceBitangent = Surface.WorldBitangent.GetSafeNormal();
                SurfaceBitangent = (SurfaceBitangent - SurfaceNormal * FVector::DotProduct(SurfaceBitangent, SurfaceNormal)).GetSafeNormal();
                if (SurfaceBitangent.IsNearlyZero())
                {
                    SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
                }
                if (SurfaceBitangent.IsNearlyZero())
                {
                    SurfaceBitangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
                }

                AppendWetWrinkleDisplacedPatchMesh(
                    MaterialBuffer.Vertices,
                    MaterialBuffer.Indices,
                    MaterialBuffer.Normals,
                    MaterialBuffer.UVs,
                    MaterialBuffer.VertexColors,
                    MaterialBuffer.Tangents,
                    Stamp.BrushHeightTexture,
                    Surface.WorldPosition,
                    SurfaceNormal,
                    SurfaceTangent,
                    SurfaceBitangent,
                    Stamp.PositionUV,
                    Radius,
                    Stamp.BrushRadiusUV,
                    Stamp.RotationRadians,
                    Stamp.Scale,
                    Stamp.Strength,
                    Stamp.Falloff,
                    SurfaceOffset,
                    FLinearColor::White);
            }
        }
    }

    int32 SectionIndex = 0;
    for (const TPair<int32, int32>& Pair : MaterialSlotToBufferIndex)
    {
        const int32 MaterialSlotIndex = Pair.Key;
        const FWetWrinkleProceduralMeshBuffers& MaterialBuffer = MaterialBuffers[Pair.Value];
        if (MaterialBuffer.Vertices.Num() == 0)
        {
            continue;
        }

        UMaterialInterface* SectionMaterial = nullptr;
        if (PreviewBaseMaterials.IsValidIndex(MaterialSlotIndex))
        {
            SectionMaterial = PreviewBaseMaterials[MaterialSlotIndex];
        }

        if (SectionMaterial != nullptr)
        {
            StoredStampOverlayComponent->SetMaterial(SectionIndex, SectionMaterial);
        }

        StoredStampOverlayComponent->CreateMeshSection_LinearColor(
            SectionIndex,
            MaterialBuffer.Vertices,
            MaterialBuffer.Indices,
            MaterialBuffer.Normals,
            MaterialBuffer.UVs,
            MaterialBuffer.VertexColors,
            MaterialBuffer.Tangents,
            false,
            false);
        ++SectionIndex;
    }
}

void SWetWrinkleAssetViewport::SetSelectedStrokeGuid(const FGuid& InStrokeGuid)
{
    SelectedStrokeGuid = InStrokeGuid;
    RefreshStoredStampOverlay();
    Invalidate();
}

void SWetWrinkleAssetViewport::PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV)
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

void SWetWrinkleAssetViewport::ClearExternalBrushPreview()
{
    ClearBrushCursor();
    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

bool SWetWrinkleAssetViewport::TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const
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
    OutHit.DistanceSq = 0.0;
    return true;
}

bool SWetWrinkleAssetViewport::TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const
{
    OutHit = FWetWrinkleSurfaceHit();
    OutHit.UVChannelIndex = BrushSettings.UVChannelIndex;

    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr || HitTriangles.Num() == 0)
    {
        return false;
    }

    const FVector SafeRayDirection = RayDirection.GetSafeNormal();
    const FVector RayEnd = RayOrigin + SafeRayDirection * 1000000.0;
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    for (const FWetClothingAssetUVTriangle& Triangle : HitTriangles)
    {
        const FVector WorldA = ComponentTransform.TransformPosition(Triangle.LocalPositions[0]);
        const FVector WorldB = ComponentTransform.TransformPosition(Triangle.LocalPositions[1]);
        const FVector WorldC = ComponentTransform.TransformPosition(Triangle.LocalPositions[2]);

        FVector IntersectionPoint = FVector::ZeroVector;
        FVector TriangleNormal = FVector::ZeroVector;
        if (!FMath::SegmentTriangleIntersection(RayOrigin, RayEnd, WorldA, WorldB, WorldC, IntersectionPoint, TriangleNormal))
        {
            continue;
        }

        const double DistanceSq = FVector::DistSquared(RayOrigin, IntersectionPoint);
        if (DistanceSq >= OutHit.DistanceSq)
        {
            continue;
        }

        FVector Normal = FVector::CrossProduct(WorldB - WorldA, WorldC - WorldA).GetSafeNormal();
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

        FVector Tangent = (WorldB - WorldA).GetSafeNormal();
        Tangent = (Tangent - Normal * FVector::DotProduct(Tangent, Normal)).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();
        if (Bitangent.IsNearlyZero())
        {
            Bitangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric(IntersectionPoint, WorldA, WorldB, WorldC);
        const FVector LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;
        const FVector2D UV = Triangle.UVs[0] * Barycentric.X + Triangle.UVs[1] * Barycentric.Y + Triangle.UVs[2] * Barycentric.Z;

        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVChannelIndex = BrushSettings.UVChannelIndex;
        OutHit.WorldPosition = IntersectionPoint;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.WorldBitangent = Bitangent;
        OutHit.LocalPosition = LocalPosition;
        OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Bitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
        OutHit.UV = UV;
        OutHit.Barycentric = Barycentric;
        OutHit.DistanceSq = DistanceSq;
    }

    return OutHit.bHit;
}

void SWetWrinkleAssetViewport::FocusOnPreviewMesh(bool bInstant)
{
    if (ViewportClient.IsValid())
    {
        ViewportClient->FocusOnPreviewMesh(PreviewMeshComponent, bInstant);
    }
}

TSharedRef<FEditorViewportClient> SWetWrinkleAssetViewport::MakeEditorViewportClient()
{
    check(PreviewScene.IsValid());
    ViewportClient = MakeShared<FWetWrinkleAssetViewportClient>(PreviewScene.Get(), SharedThis(this));

    if (PreviewMeshComponent != nullptr)
    {
        ViewportClient->SetPreviewMeshComponent(PreviewMeshComponent);
        ViewportClient->RequestFocusOnPreviewMeshNextTick(PreviewMeshComponent);
    }

    return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SWetWrinkleAssetViewport::BuildViewportToolbar()
{
    const FName ViewportToolbarName = TEXT("WetWrinkleAssetEditor.ViewportToolbar");

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

void SWetWrinkleAssetViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
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

void SWetWrinkleAssetViewport::OnFocusViewportToSelection()
{
    FocusOnPreviewMesh(false);
}

USkeletalMesh* SWetWrinkleAssetViewport::ResolveTargetMesh() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (Asset->TargetMesh != nullptr)
    {
        return Asset->TargetMesh;
    }

    if (Asset->SourceWetClothingAsset != nullptr)
    {
        return Asset->SourceWetClothingAsset->TargetMesh;
    }

    return nullptr;
}

const UWetClothingAsset* SWetWrinkleAssetViewport::ResolveSourceWetClothingAsset() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    return Asset != nullptr ? Asset->SourceWetClothingAsset.Get() : nullptr;
}

UTexture* SWetWrinkleAssetViewport::ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset != nullptr)
    {
        for (const FWetClothingAssetTextureSelection& TextureSelection : SourceWetClothingAsset->TextureSelections)
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
    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface);
    }

    return nullptr;
}

void SWetWrinkleAssetViewport::InitializePreviewMaterialInstances()
{
    PreviewBaseMaterials.Reset();
    PreviewMaterialInstances.Reset();
    PreviewWrinkleNormalTextures.Reset();

    if (PreviewMeshComponent == nullptr)
    {
        return;
    }

    const USkeletalMesh* TargetMesh = PreviewMeshComponent->GetSkeletalMeshAsset();
    if (TargetMesh == nullptr)
    {
        return;
    }

    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    const int32 MaterialCount = PreviewMeshComponent->GetNumMaterials();
    PreviewBaseMaterials.SetNum(MaterialCount);
    PreviewMaterialInstances.SetNum(MaterialCount);
    PreviewWrinkleNormalTextures.SetNum(MaterialCount);

    for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
    {
        UMaterialInterface* BaseMaterial = nullptr;
        if (SourceWetClothingAsset != nullptr)
        {
            if (const FWetClothingAssetWetMaterialOverride* WetOverride = SourceWetClothingAsset->WetMaterialOverrides.FindByPredicate(
                    [MaterialIndex](const FWetClothingAssetWetMaterialOverride& Entry)
                    {
                        return Entry.MaterialSlotIndex == MaterialIndex;
                    }))
            {
                if (WetOverride->SourceMaterial != nullptr)
                {
                    BaseMaterial = WetOverride->SourceMaterial;
                }
                else if (WetOverride->WetMaterial != nullptr)
                {
                    BaseMaterial = WetOverride->WetMaterial;
                }
            }
        }

        if (BaseMaterial == nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialIndex))
        {
            BaseMaterial = TargetMesh->GetMaterials()[MaterialIndex].MaterialInterface;
        }

        PreviewBaseMaterials[MaterialIndex] = BaseMaterial;
        PreviewMeshComponent->SetMaterial(MaterialIndex, BaseMaterial);
        PreviewMaterialInstances[MaterialIndex] = nullptr;
    }
}

void SWetWrinkleAssetViewport::ApplyPreviewWetVertexColors()
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const FSkeletalMeshRenderData* RenderData = PreviewMeshComponent->GetSkeletalMeshAsset()->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(0))
    {
        return;
    }

    const int32 VertexCount = RenderData->LODRenderData[0].GetNumVertices();
    if (VertexCount <= 0)
    {
        return;
    }

    TArray<FLinearColor> PreviewVertexColors;
    PreviewVertexColors.Init(FLinearColor::White, VertexCount);
    PreviewMeshComponent->SetVertexColorOverride_LinearColor(0, PreviewVertexColors);
    PreviewMeshComponent->MarkRenderStateDirty();
}

void SWetWrinkleAssetViewport::ResetPreviewWrinkleTextures()
{
    PreviewWrinkleNormalTextures.Reset();
}

void SWetWrinkleAssetViewport::RefreshWrinklePreviewMaterials()
{
    // Brush preview is currently shown by a localized displaced patch mesh instead of
    // mutating the preview mesh material graph or MID parameters.
}

void SWetWrinkleAssetViewport::ApplyMaterialSlotVisibility()
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

    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
        {
            PreviewMeshComponent->ShowAllMaterialSections(LODIndex);
            continue;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
            const bool bShowSection = Section.MaterialIndex == BrushSettings.MaterialSlotIndex;
            PreviewMeshComponent->ShowMaterialSection(Section.MaterialIndex, SectionIndex, bShowSection, LODIndex);
        }
    }
}

void SWetWrinkleAssetViewport::RebuildHitTriangles()
{
    HitTriangles.Reset();

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    if (TargetMesh == nullptr)
    {
        return;
    }

    const int32 MaterialCount = TargetMesh->GetMaterials().Num();
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        if (BrushSettings.MaterialSlotIndex != INDEX_NONE && BrushSettings.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        TArray<FWetClothingAssetUVIsland> Islands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, BrushSettings.UVChannelIndex, MaterialSlotIndex, Islands, nullptr))
        {
            continue;
        }

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            HitTriangles.Append(Island.UVTriangles);
        }
    }
}

void SWetWrinkleAssetViewport::HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    CurrentSurfaceHit = SurfaceHit;
    RefreshBrushCursor();
    RefreshWrinklePreviewMaterials();

    if (OnSurfaceHitChanged.IsBound())
    {
        OnSurfaceHitChanged.Execute(CurrentSurfaceHit);
    }
}

void SWetWrinkleAssetViewport::BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStrokeStarted.IsBound())
    {
        OnPaintStrokeStarted.Execute(SurfaceHit);
    }
}

void SWetWrinkleAssetViewport::RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (OnPaintStampRequested.IsBound())
    {
        OnPaintStampRequested.Execute(SurfaceHit);
    }
}

void SWetWrinkleAssetViewport::EndPaintStrokeFromClient()
{
    if (OnPaintStrokeEnded.IsBound())
    {
        OnPaintStrokeEnded.Execute();
    }
}

void SWetWrinkleAssetViewport::RefreshBrushCursor()
{
    if (BrushCursorComponent == nullptr)
    {
        return;
    }

    BrushCursorComponent->SetVisibility(false, true);
    BrushCursorComponent->ClearAllMeshSections();
    BrushCursorComponent->MarkRenderStateDirty();

    if (!BrushSettings.bShowPreview || !CurrentSurfaceHit.bHit)
    {
        return;
    }

    const float Radius = CalculateBrushCursorWorldRadius();
    const float InnerRadius = Radius * 0.92f;
    const float NormalOffset = FMath::Max(Radius * 0.12f, 1.0f);
    const FVector ViewLocation = ViewportClient.IsValid() ? ViewportClient->GetViewLocation() : FVector::ZeroVector;
    const bool bHasViewLocation = ViewportClient.IsValid();

    TArray<FVector> PatchVertices;
    TArray<int32> PatchIndices;
    TArray<FVector> PatchNormals;
    TArray<FVector2D> PatchUVs;
    TArray<FLinearColor> PatchVertexColors;
    TArray<FProcMeshTangent> PatchTangents;

    TArray<FVector> RingVertices;
    TArray<int32> RingIndices;
    TArray<FVector> RingNormals;
    TArray<FVector2D> RingUVs;
    TArray<FLinearColor> RingVertexColors;
    TArray<FProcMeshTangent> RingTangents;

    const FLinearColor CursorColor(0.12f, 0.82f, 1.0f, 1.0f);
    TArray<FWetWrinkleProjectedSurface> ProjectedSurfaces;
    FindProjectedSurfacesAtUV(CurrentSurfaceHit.MaterialSlotIndex, CurrentSurfaceHit.UVChannelIndex, CurrentSurfaceHit.UV, ProjectedSurfaces);
    if (ProjectedSurfaces.Num() == 0)
    {
        FWetWrinkleProjectedSurface FallbackSurface;
        FallbackSurface.MaterialSlotIndex = CurrentSurfaceHit.MaterialSlotIndex;
        FallbackSurface.TriangleID = CurrentSurfaceHit.TriangleID;
        FallbackSurface.WorldPosition = CurrentSurfaceHit.WorldPosition;
        FallbackSurface.WorldNormal = CurrentSurfaceHit.WorldNormal;
        FallbackSurface.WorldTangent = CurrentSurfaceHit.WorldTangent;
        FallbackSurface.WorldBitangent = CurrentSurfaceHit.WorldBitangent;
        ProjectedSurfaces.Add(FallbackSurface);
    }

    int32 PrimarySurfaceIndex = 0;
    double ClosestPrimaryDistanceSq = TNumericLimits<double>::Max();
    for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const double DistanceSq = FVector::DistSquared(CurrentSurfaceHit.WorldPosition, ProjectedSurfaces[SurfaceIndex].WorldPosition);
        if (DistanceSq < ClosestPrimaryDistanceSq)
        {
            ClosestPrimaryDistanceSq = DistanceSq;
            PrimarySurfaceIndex = SurfaceIndex;
        }
    }

    const FVector PrimarySurfacePosition = ProjectedSurfaces[PrimarySurfaceIndex].WorldPosition;
    for (int32 SurfaceIndex = 0; SurfaceIndex < ProjectedSurfaces.Num(); ++SurfaceIndex)
    {
        const FWetWrinkleProjectedSurface& Surface = ProjectedSurfaces[SurfaceIndex];
        const bool bPrimarySurface = SurfaceIndex == PrimarySurfaceIndex;
        if (!bPrimarySurface && !IsWetWrinkleLinkedSurface(PrimarySurfacePosition, Surface.WorldPosition, Radius))
        {
            continue;
        }

        const FVector SurfacePosition = bPrimarySurface ? CurrentSurfaceHit.WorldPosition : Surface.WorldPosition;
        FVector SurfaceNormal = (bPrimarySurface ? CurrentSurfaceHit.WorldNormal : Surface.WorldNormal).GetSafeNormal();
        if (SurfaceNormal.IsNearlyZero())
        {
            SurfaceNormal = FVector::UpVector;
        }
        if (!bPrimarySurface && bHasViewLocation && FVector::DotProduct(SurfaceNormal, ViewLocation - SurfacePosition) < 0.0)
        {
            SurfaceNormal *= -1.0;
        }

        FVector SurfaceTangent = (bPrimarySurface ? CurrentSurfaceHit.WorldTangent : Surface.WorldTangent).GetSafeNormal();
        FVector SurfaceBitangent = (bPrimarySurface ? CurrentSurfaceHit.WorldBitangent : Surface.WorldBitangent).GetSafeNormal();
        SurfaceTangent = (SurfaceTangent - SurfaceNormal * FVector::DotProduct(SurfaceTangent, SurfaceNormal)).GetSafeNormal();
        if (SurfaceTangent.IsNearlyZero())
        {
            SurfaceTangent = MakeWetWrinkleAnyPerpendicular(SurfaceNormal);
        }
        SurfaceBitangent = (SurfaceBitangent - SurfaceNormal * FVector::DotProduct(SurfaceBitangent, SurfaceNormal)).GetSafeNormal();
        if (SurfaceBitangent.IsNearlyZero())
        {
            SurfaceBitangent = FVector::CrossProduct(SurfaceNormal, SurfaceTangent).GetSafeNormal();
        }

        FLinearColor SurfaceColor = CursorColor;
        if (!bPrimarySurface)
        {
            SurfaceColor = FLinearColor(1.0f, 0.55f, 0.08f, 1.0f);
        }

        if (BrushSettings.BrushHeightTexture != nullptr)
        {
            AppendWetWrinkleDisplacedPatchMesh(
                PatchVertices,
                PatchIndices,
                PatchNormals,
                PatchUVs,
                PatchVertexColors,
                PatchTangents,
                BrushSettings.BrushHeightTexture.Get(),
                SurfacePosition,
                SurfaceNormal,
                SurfaceTangent,
                SurfaceBitangent,
                CurrentSurfaceHit.UV,
                Radius,
                BrushSettings.BrushRadiusUV,
                BrushSettings.RotationRadians,
                FVector2D(1.0f, 1.0f),
                BrushSettings.Strength,
                BrushSettings.Falloff,
                FMath::Max(Radius * 0.01f, 0.05f),
                FLinearColor::White);
        }
        else
        {
            AppendWetWrinkleRaisedPatchMesh(
                PatchVertices,
                PatchIndices,
                PatchNormals,
                PatchUVs,
                PatchVertexColors,
                PatchTangents,
                SurfacePosition,
                SurfaceNormal,
                SurfaceTangent,
                SurfaceBitangent,
                CurrentSurfaceHit.UV,
                Radius,
                BrushSettings.BrushRadiusUV,
                BrushSettings.Strength,
                BrushSettings.Falloff,
                FMath::Max(Radius * 0.01f, 0.05f),
                FLinearColor::White);
        }

        AppendWetWrinkleRingMesh(
            RingVertices,
            RingIndices,
            RingNormals,
            RingUVs,
            RingVertexColors,
            RingTangents,
            SurfacePosition + SurfaceNormal * NormalOffset,
            SurfaceNormal,
            SurfaceTangent,
            SurfaceBitangent,
            Radius,
            InnerRadius,
            SurfaceColor);

    }

    UMaterialInterface* PatchMaterial = nullptr;
    if (PreviewBaseMaterials.IsValidIndex(CurrentSurfaceHit.MaterialSlotIndex))
    {
        PatchMaterial = PreviewBaseMaterials[CurrentSurfaceHit.MaterialSlotIndex];
    }

    if (PatchVertices.Num() > 0)
    {
        BrushCursorComponent->SetMaterial(0, PatchMaterial != nullptr ? PatchMaterial : ResolveCursorMaterial());
        BrushCursorComponent->CreateMeshSection_LinearColor(
            0,
            PatchVertices,
            PatchIndices,
            PatchNormals,
            PatchUVs,
            PatchVertexColors,
            PatchTangents,
            false,
            false);
    }

    if (RingVertices.Num() > 0)
    {
        BrushCursorComponent->SetMaterial(1, ResolveCursorMaterial());
        BrushCursorComponent->CreateMeshSection_LinearColor(
            1,
            RingVertices,
            RingIndices,
            RingNormals,
            RingUVs,
            RingVertexColors,
            RingTangents,
            false,
            false);
    }

    BrushCursorComponent->SetVisibility(true, true);
    BrushCursorComponent->MarkRenderStateDirty();
}

void SWetWrinkleAssetViewport::ClearBrushCursor()
{
    if (BrushCursorComponent != nullptr)
    {
        BrushCursorComponent->SetVisibility(false, true);
        BrushCursorComponent->ClearAllMeshSections();
        BrushCursorComponent->MarkRenderStateDirty();
    }
}

void SWetWrinkleAssetViewport::ClearStoredStampOverlay()
{
    if (StoredStampOverlayComponent != nullptr)
    {
        StoredStampOverlayComponent->ClearAllMeshSections();
    }
}

UMaterialInterface* SWetWrinkleAssetViewport::ResolveCursorMaterial()
{
    if (CursorMaterial != nullptr)
    {
        return CursorMaterial;
    }

    if (GEngine != nullptr)
    {
        if (GEngine->VertexColorMaterial != nullptr)
        {
            CursorMaterial = GEngine->VertexColorMaterial;
            return CursorMaterial;
        }

        if (GEngine->VertexColorViewModeMaterial_ColorOnly != nullptr)
        {
            CursorMaterial = GEngine->VertexColorViewModeMaterial_ColorOnly;
            return CursorMaterial;
        }
    }

    CursorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    return CursorMaterial;
}

FText SWetWrinkleAssetViewport::GetViewportHintText() const
{
    if (ResolveTargetMesh() == nullptr)
    {
        return LOCTEXT("NoTargetMeshHint", "Assign a Target Mesh or Source Wet Clothing Asset.");
    }

    if (HitTriangles.Num() == 0)
    {
        return LOCTEXT("NoHitTrianglesHint", "No triangles available for the selected UV channel/material slot.");
    }

    return LOCTEXT("ViewportHint", "Move the cursor over the mesh to inspect wrinkle brush UV hits.");
}

float SWetWrinkleAssetViewport::CalculateBrushCursorWorldRadius() const
{
    if (PreviewMeshComponent == nullptr || PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return 5.0f;
    }

    const FBoxSphereBounds Bounds = PreviewMeshComponent->CalcBounds(PreviewMeshComponent->GetComponentTransform());
    const float MeshRadius = FMath::Max(1.0f, static_cast<float>(Bounds.SphereRadius));
    return FMath::Clamp(MeshRadius * BrushSettings.BrushRadiusUV, 0.25f, MeshRadius * 0.35f);
}

void SWetWrinkleAssetViewport::FindProjectedSurfacesAtUV(
    int32 MaterialSlotIndex,
    int32 UVChannelIndex,
    const FVector2D& UV,
    TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const
{
    OutSurfaces.Reset();

    if (PreviewMeshComponent == nullptr || UVChannelIndex != BrushSettings.UVChannelIndex)
    {
        return;
    }

    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    for (const FWetClothingAssetUVTriangle& Triangle : HitTriangles)
    {
        if (MaterialSlotIndex != INDEX_NONE && Triangle.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        const FVector Barycentric = ComputeWetWrinkleBarycentric2D(UV, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        if (!IsWetWrinkleBarycentricInside(Barycentric))
        {
            continue;
        }

        const FVector LocalPosition =
            Triangle.LocalPositions[0] * Barycentric.X +
            Triangle.LocalPositions[1] * Barycentric.Y +
            Triangle.LocalPositions[2] * Barycentric.Z;

        const FVector WorldA = ComponentTransform.TransformPosition(Triangle.LocalPositions[0]);
        const FVector WorldB = ComponentTransform.TransformPosition(Triangle.LocalPositions[1]);
        const FVector WorldC = ComponentTransform.TransformPosition(Triangle.LocalPositions[2]);

        FVector WorldNormal = FVector::CrossProduct(WorldB - WorldA, WorldC - WorldA).GetSafeNormal();
        if (WorldNormal.IsNearlyZero())
        {
            WorldNormal = FVector::UpVector;
        }

        FVector WorldTangent = (WorldB - WorldA).GetSafeNormal();
        WorldTangent = (WorldTangent - WorldNormal * FVector::DotProduct(WorldTangent, WorldNormal)).GetSafeNormal();
        if (WorldTangent.IsNearlyZero())
        {
            WorldTangent = MakeWetWrinkleAnyPerpendicular(WorldNormal);
        }

        FVector WorldBitangent = FVector::CrossProduct(WorldNormal, WorldTangent).GetSafeNormal();
        if (WorldBitangent.IsNearlyZero())
        {
            WorldBitangent = MakeWetWrinkleAnyPerpendicular(WorldNormal);
        }

        FWetWrinkleProjectedSurface ProjectedSurface;
        ProjectedSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        ProjectedSurface.TriangleID = Triangle.TriangleID;
        ProjectedSurface.WorldPosition = ComponentTransform.TransformPosition(LocalPosition);
        ProjectedSurface.WorldNormal = WorldNormal;
        ProjectedSurface.WorldTangent = WorldTangent;
        ProjectedSurface.WorldBitangent = WorldBitangent;
        OutSurfaces.Add(ProjectedSurface);
    }
}

bool SWetWrinkleAssetViewport::TryProjectUVToWorld(
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

#undef LOCTEXT_NAMESPACE
