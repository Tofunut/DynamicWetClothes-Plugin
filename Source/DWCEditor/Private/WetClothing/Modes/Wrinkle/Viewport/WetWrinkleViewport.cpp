#include "WetWrinkleViewport.h"

#include "AdvancedPreviewScene.h"
#include "Algo/Sort.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PrimitiveDrawInterface.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
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
#include "WetClothing/WCAEditor/UI/UVView/WCAUVPreviewTriangleReader.h"
#include "WetWrinkleViewportClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/SRichTextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleViewport"

DEFINE_LOG_CATEGORY_STATIC(LogWetWrinklePreviewViewport, Log, All);

namespace
{
    constexpr int32 WetWrinkleUVGridResolution = 64;
    constexpr int32 WetWrinkleBVHLeafTriangleCount = 8;
    constexpr int32 ForceRenderLOD0 = 1; // USkinnedMeshComponent forced LOD is 1-based; 0 means automatic.

    TAutoConsoleVariable<int32> CVarWetWrinklePreviewCacheBudgetMB(
        TEXT("DWC.WrinkleEditor.PreviewCacheBudgetMB"),
        128,
        TEXT("Maximum retained memory for wrinkle accumulated preview states, in MiB. ")
        TEXT("The active state is protected and may exceed this budget by itself."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarWetWrinkleHitCacheBudgetMB(
        TEXT("DWC.WrinkleEditor.HitCacheBudgetMB"),
        64,
        TEXT("Maximum retained memory for wrinkle hit triangle and BVH caches, in MiB. ")
        TEXT("The active cache is protected and may exceed this budget by itself."),
        ECVF_Default);

    uint64 ResolveCacheBudgetBytes(const TAutoConsoleVariable<int32>& BudgetCVar)
    {
        constexpr uint64 BytesPerMiB = 1024ull * 1024ull;
        return static_cast<uint64>(FMath::Max(BudgetCVar.GetValueOnGameThread(), 1)) * BytesPerMiB;
    }

    uint64 EstimatePreviewTextureBytes(const UTexture2D* Texture)
    {
        if (Texture == nullptr)
        {
            return 0;
        }

        uint64 BulkDataBytes = 0;
        if (const FTexturePlatformData* PlatformData = Texture->GetPlatformData())
        {
            for (const FTexture2DMipMap& Mip : PlatformData->Mips)
            {
                BulkDataBytes += static_cast<uint64>(FMath::Max<int64>(Mip.BulkData.GetBulkDataSize(), 0));
            }
        }

        const uint64 MinimumTextureBytes =
            static_cast<uint64>(FMath::Max(Texture->GetSizeX(), 0)) *
            static_cast<uint64>(FMath::Max(Texture->GetSizeY(), 0)) *
            sizeof(FColor);
        const uint64 ResidentResourceBytes =
            static_cast<uint64>(Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips));
        return BulkDataBytes + FMath::Max(ResidentResourceBytes, MinimumTextureBytes);
    }

    uint64 EstimateAccumulatedPreviewStateDynamicBytes(const FWetWrinkleAccumulatedPreviewState& State)
    {
        return static_cast<uint64>(State.Pixels.GetAllocatedSize()) +
               static_cast<uint64>(State.WorkingPixels.GetAllocatedSize()) +
               EstimatePreviewTextureBytes(State.AccumulatedNormalTexture);
    }

    uint64 EstimateAccumulatedPreviewCacheBytes(const TArray<FWetWrinkleAccumulatedPreviewState>& States)
    {
        uint64 TotalBytes = static_cast<uint64>(States.GetAllocatedSize());
        for (const FWetWrinkleAccumulatedPreviewState& State : States)
        {
            TotalBytes += EstimateAccumulatedPreviewStateDynamicBytes(State);
        }
        return TotalBytes;
    }

    uint64 EstimateHitCacheDataBytes(
        const TArray<FWetWrinkleCachedHitTriangle>& Triangles,
        const TMap<uint64, int32>& TriangleLookup,
        const TArray<int32>& BVHTriangleIndices,
        const TArray<FWetWrinkleHitBVHNode>& BVHNodes,
        const TArray<TArray<int32>>& UVTriangleGrid)
    {
        uint64 TotalBytes =
            static_cast<uint64>(Triangles.GetAllocatedSize()) +
            static_cast<uint64>(TriangleLookup.GetAllocatedSize()) +
            static_cast<uint64>(BVHTriangleIndices.GetAllocatedSize()) +
            static_cast<uint64>(BVHNodes.GetAllocatedSize()) +
            static_cast<uint64>(UVTriangleGrid.GetAllocatedSize());
        for (const TArray<int32>& GridCell : UVTriangleGrid)
        {
            TotalBytes += static_cast<uint64>(GridCell.GetAllocatedSize());
        }
        return TotalBytes;
    }

    uint64 EstimateHitCacheEntryDynamicBytes(
        const FWetWrinkleHitCacheKey& Key,
        const FWetWrinkleHitCacheEntry& Entry)
    {
        return static_cast<uint64>(Key.TopologySignature.GetAllocatedSize()) +
               EstimateHitCacheDataBytes(
                   Entry.Triangles,
                   Entry.TriangleLookup,
                   Entry.BVHTriangleIndices,
                   Entry.BVHNodes,
                   Entry.UVTriangleGrid);
    }

    uint64 MakeWetWrinkleTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

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

    FVector3f MakeWetWrinkleAnyPerpendicular(const FVector3f& Direction)
    {
        FVector3f Perpendicular = FVector3f::CrossProduct(Direction, FVector3f(0.0f, 0.0f, 1.0f)).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector3f::CrossProduct(Direction, FVector3f(0.0f, 1.0f, 0.0f)).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector3f(1.0f, 0.0f, 0.0f) : Perpendicular;
    }

    FVector3f ComputeWetWrinkleBarycentric2D(
        const FVector2f& Point,
        const FVector2f& A,
        const FVector2f& B,
        const FVector2f& C)
    {
        const FVector2f V0 = B - A;
        const FVector2f V1 = C - A;
        const FVector2f V2 = Point - A;
        const float D00 = FVector2f::DotProduct(V0, V0);
        const float D01 = FVector2f::DotProduct(V0, V1);
        const float D11 = FVector2f::DotProduct(V1, V1);
        const float D20 = FVector2f::DotProduct(V2, V0);
        const float D21 = FVector2f::DotProduct(V2, V1);
        const float Denom = D00 * D11 - D01 * D01;
        if (FMath::IsNearlyZero(Denom))
        {
            return FVector3f(-1.0f, -1.0f, -1.0f);
        }

        const float V = (D11 * D20 - D01 * D21) / Denom;
        const float W = (D00 * D21 - D01 * D20) / Denom;
        return FVector3f(1.0f - V - W, V, W);
    }

    bool IsWetWrinkleBarycentricInside(const FVector3f& Barycentric)
    {
        constexpr float Tolerance = 0.0001f;
        return Barycentric.X >= -Tolerance &&
               Barycentric.Y >= -Tolerance &&
               Barycentric.Z >= -Tolerance &&
               Barycentric.X <= 1.0f + Tolerance &&
               Barycentric.Y <= 1.0f + Tolerance &&
               Barycentric.Z <= 1.0f + Tolerance;
    }

    bool DoesWetWrinkleSegmentIntersectBox(
        const FBox3f& Box,
        const FVector3f& SegmentStart,
        const FVector3f& SegmentEnd)
    {
        if (!Box.IsValid)
        {
            return true;
        }

        const FVector3f SegmentDelta = SegmentEnd - SegmentStart;
        float TMin = 0.0f;
        float TMax = 1.0f;

        auto ClipAxis = [&TMin, &TMax](float Start, float Delta, float MinValue, float MaxValue) -> bool
        {
            if (FMath::Abs(Delta) <= UE_SMALL_NUMBER)
            {
                return Start >= MinValue && Start <= MaxValue;
            }

            float AxisT0 = (MinValue - Start) / Delta;
            float AxisT1 = (MaxValue - Start) / Delta;
            if (AxisT0 > AxisT1)
            {
                const float Temp = AxisT0;
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

    bool IntersectWetWrinkleLocalTriangle(
        const FVector3f& SegmentStart,
        const FVector3f& SegmentEnd,
        const FVector3f& A,
        const FVector3f& B,
        const FVector3f& C,
        float& OutSegmentT,
        FVector3f& OutBarycentric)
    {
        const FVector3f SegmentDelta = SegmentEnd - SegmentStart;
        const FVector3f EdgeAB = B - A;
        const FVector3f EdgeAC = C - A;
        const FVector3f P = FVector3f::CrossProduct(SegmentDelta, EdgeAC);
        const float Determinant = FVector3f::DotProduct(EdgeAB, P);
        if (FMath::Abs(Determinant) <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const float InverseDeterminant = 1.0f / Determinant;
        const FVector3f T = SegmentStart - A;
        const float BarycentricB = FVector3f::DotProduct(T, P) * InverseDeterminant;
        if (BarycentricB < -UE_KINDA_SMALL_NUMBER || BarycentricB > 1.0f + UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const FVector3f Q = FVector3f::CrossProduct(T, EdgeAB);
        const float BarycentricC = FVector3f::DotProduct(SegmentDelta, Q) * InverseDeterminant;
        if (BarycentricC < -UE_KINDA_SMALL_NUMBER ||
            BarycentricB + BarycentricC > 1.0f + UE_KINDA_SMALL_NUMBER)
        {
            return false;
        }

        const float SegmentT = FVector3f::DotProduct(EdgeAC, Q) * InverseDeterminant;
        if (SegmentT < 0.0f || SegmentT > 1.0f)
        {
            return false;
        }

        OutSegmentT = SegmentT;
        OutBarycentric = FVector3f(1.0f - BarycentricB - BarycentricC, BarycentricB, BarycentricC);
        return true;
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
    PreviewMeshComponent->SetForcedLOD(ForceRenderLOD0);
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
        ClearAllHitCaches();
        PreviewMeshComponent->SetSkeletalMeshAsset(TargetMesh);
    }
    else if (bForceMaterialRebuild)
    {
        // A full asset refresh can regenerate UV metadata without replacing the mesh UObject.
        ClearAllHitCaches();
    }
    PreviewMeshComponent->SetForcedLOD(ForceRenderLOD0);

    const bool bMaterialSourcesChanged = bMeshChanged || !ArePreviewMaterialSlotsCurrent();
    if (bForceMaterialRebuild || bMaterialSourcesChanged)
    {
        RebuildPreviewMaterialSlots();
    }
    ApplyMaterialSlotVisibility();
    if (bMeshChanged || bForceMaterialRebuild)
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

void SWetWrinkleViewport::SynchronizeBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_SynchronizeBrushSettings);

    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        InBrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;
    const bool bNeedsTriangleRebuild =
        BrushSettings.UVChannelIndex != InBrushSettings.UVChannelIndex ||
        BrushSettings.MaterialSlotIndex != InBrushSettings.MaterialSlotIndex;

    if (bNeedsTriangleRebuild)
    {
        PrepareAccumulatedPreviewStatesForSlot(
            InBrushSettings.MaterialSlotIndex,
            InBrushSettings.UVChannelIndex);
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
        CurrentSurfaceHit = FWetWrinkleSurfaceHit();
        ClearBrushCursor();
        RebuildHitTriangles();
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

void SWetWrinkleViewport::SetBrushTopology(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_SetBrushTopology);

    if (BrushSettings.MaterialSlotIndex == MaterialSlotIndex &&
        BrushSettings.UVChannelIndex == UVChannelIndex)
    {
        return;
    }

    PrepareAccumulatedPreviewStatesForSlot(MaterialSlotIndex, UVChannelIndex);
    ReleaseTransientProceduralPreviewState();
    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    BrushSettings.UVChannelIndex = UVChannelIndex;
    ApplyMaterialSlotVisibility();

    CurrentSurfaceHit = FWetWrinkleSurfaceHit();
    ClearBrushCursor();
    RebuildHitTriangles();
    RefreshWrinklePreviewMaterials();
    Invalidate();
}

void SWetWrinkleViewport::UpdateBrushPreviewSettings(
    const FWetWrinkleBrushSettings& InBrushSettings)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_UpdateBrushPreviewSettings);

    ensureMsgf(
        BrushSettings.MaterialSlotIndex == InBrushSettings.MaterialSlotIndex &&
            BrushSettings.UVChannelIndex == InBrushSettings.UVChannelIndex,
        TEXT("UpdateBrushPreviewSettings cannot change wrinkle topology. Use SetBrushTopology first."));

    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = BrushSettings.UVChannelIndex;
    const float PreviewWetness = BrushSettings.PreviewWetness;
    const bool bLeavingProceduralRidgeMode =
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        InBrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke;

    BrushSettings = InBrushSettings;
    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    BrushSettings.UVChannelIndex = UVChannelIndex;
    BrushSettings.PreviewWetness = PreviewWetness;

    if (bLeavingProceduralRidgeMode)
    {
        ReleaseTransientProceduralPreviewState();
        RefreshWrinklePreviewTransientParameters();
    }
    else if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
             TransientProceduralStrokeHits.Num() >= 2)
    {
        SetTransientProceduralStroke(
            TransientProceduralStrokeHits,
            bTransientProceduralStartJunction,
            bTransientProceduralEndJunction);
    }

    RefreshBrushCursor();
    RefreshWrinklePreviewHoverParameters();
    Invalidate();
}

void SWetWrinkleViewport::SetPreviewWetness(const float PreviewWetness)
{
    const float ClampedWetness = FMath::Clamp(PreviewWetness, 0.0f, 1.0f);
    if (FMath::IsNearlyEqual(BrushSettings.PreviewWetness, ClampedWetness))
    {
        return;
    }

    BrushSettings.PreviewWetness = ClampedWetness;
    RefreshWrinklePreviewWetnessParameter();
    Invalidate();
}

void SWetWrinkleViewport::SetGeneratedNormalPreviewTexture(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    UTexture2D* GeneratedNormalTexture,
    const bool bRefreshPreview)
{
    if (bGeneratedNormalPreviewOverrideActive &&
        GeneratedNormalPreviewMaterialSlotIndex == MaterialSlotIndex &&
        GeneratedNormalPreviewUVChannelIndex == UVChannelIndex &&
        GeneratedNormalPreviewTexture == GeneratedNormalTexture)
    {
        return;
    }

    bGeneratedNormalPreviewOverrideActive = true;
    GeneratedNormalPreviewMaterialSlotIndex = MaterialSlotIndex;
    GeneratedNormalPreviewUVChannelIndex = UVChannelIndex;
    GeneratedNormalPreviewTexture = GeneratedNormalTexture;
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
}

void SWetWrinkleViewport::ClearGeneratedNormalPreviewTexture(const bool bRefreshPreview)
{
    if (!bGeneratedNormalPreviewOverrideActive &&
        GeneratedNormalPreviewMaterialSlotIndex == INDEX_NONE &&
        GeneratedNormalPreviewUVChannelIndex == INDEX_NONE &&
        GeneratedNormalPreviewTexture == nullptr)
    {
        return;
    }

    bGeneratedNormalPreviewOverrideActive = false;
    GeneratedNormalPreviewMaterialSlotIndex = INDEX_NONE;
    GeneratedNormalPreviewUVChannelIndex = INDEX_NONE;
    GeneratedNormalPreviewTexture = nullptr;
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
}

void SWetWrinkleViewport::RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview)
{
    if (bRebuildAccumulatedPreview)
    {
        MarkAccumulatedPreviewStatesDirty();
    }

    RefreshWrinklePreviewAccumulatedParameters();
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
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
    if (bTransientProceduralPreviewBound)
    {
        bTransientProceduralPreviewBound = false;
        RefreshWrinklePreviewTransientParameters();
    }
    Invalidate();
}

void SWetWrinkleViewport::PreviewEditedProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    EditedProceduralStrokePreview = Stroke;
    PendingTransientProceduralStroke = Stroke;
    Invalidate();
}

bool SWetWrinkleViewport::SetEditingProceduralStrokeGuid(
    const FGuid& InStrokeGuid,
    const bool bRefreshPreview)
{
    if (EditingProceduralStrokeGuid == InStrokeGuid)
    {
        return false;
    }

    EditingProceduralStrokeGuid = InStrokeGuid;
    if (!EditingProceduralStrokeGuid.IsValid() ||
        (EditedProceduralStrokePreview.IsSet() &&
         EditedProceduralStrokePreview->StrokeGuid != EditingProceduralStrokeGuid))
    {
        EditedProceduralStrokePreview.Reset();
    }
    MarkAccumulatedPreviewStatesDirty();
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewAccumulatedParameters();
        Invalidate();
    }
    return true;
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

bool SWetWrinkleViewport::ClearTransientProceduralStroke(const bool bRefreshPreview)
{
    const bool bHadVisibleTransientPreview =
        !TransientProceduralStrokeHits.IsEmpty() ||
        EditedProceduralStrokePreview.IsSet() ||
        PendingTransientProceduralStroke.IsSet() ||
        bTransientProceduralPreviewBound;
    if (!bHadVisibleTransientPreview)
    {
        return false;
    }

    TransientProceduralStrokeHits.Reset();
    bTransientProceduralStartJunction = false;
    bTransientProceduralEndJunction = false;
    bTransientProceduralPreviewBound = false;
    EditedProceduralStrokePreview.Reset();
    PendingTransientProceduralStroke.Reset();
    if (bRefreshPreview)
    {
        RefreshWrinklePreviewTransientParameters();
        Invalidate();
    }
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

    if (BrushSettings.MaterialSlotIndex == INDEX_NONE ||
        BrushSettings.UVChannelIndex == INDEX_NONE ||
        PreviewMeshComponent == nullptr ||
        PreviewMeshComponent->GetSkeletalMeshAsset() == nullptr ||
        CachedHitTriangles.Num() == 0)
    {
        return false;
    }

    const FVector SafeRayDirection = RayDirection.GetSafeNormal();
    if (SafeRayDirection.IsNearlyZero())
    {
        return false;
    }

    const FVector RayEnd = RayOrigin + SafeRayDirection * 1000000.0;
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
    const FVector3f LocalRayOrigin(ComponentTransform.InverseTransformPosition(RayOrigin));
    const FVector3f LocalRayEnd(ComponentTransform.InverseTransformPosition(RayEnd));
    float BestSegmentT = TNumericLimits<float>::Max();

    auto TestTriangle = [
        this,
        &OutHit,
        &RayOrigin,
        &SafeRayDirection,
        &ComponentTransform,
        &LocalRayOrigin,
        &LocalRayEnd,
        &BestSegmentT](const FWetWrinkleCachedHitTriangle& Triangle)
    {
        if (Triangle.LocalBounds.IsValid &&
            !DoesWetWrinkleSegmentIntersectBox(Triangle.LocalBounds.ExpandBy(0.1f), LocalRayOrigin, LocalRayEnd))
        {
            return;
        }

        float SegmentT = 0.0f;
        FVector3f Barycentric3f = FVector3f::ZeroVector;
        if (!IntersectWetWrinkleLocalTriangle(
                LocalRayOrigin,
                LocalRayEnd,
                Triangle.LocalPositions[0],
                Triangle.LocalPositions[1],
                Triangle.LocalPositions[2],
                SegmentT,
                Barycentric3f) ||
            SegmentT >= BestSegmentT)
        {
            return;
        }

        const FVector3f LocalIntersectionPoint =
            Triangle.LocalPositions[0] * Barycentric3f.X +
            Triangle.LocalPositions[1] * Barycentric3f.Y +
            Triangle.LocalPositions[2] * Barycentric3f.Z;
        const FVector IntersectionPoint = ComponentTransform.TransformPosition(FVector(LocalIntersectionPoint));
        const double DistanceSq = FVector::DistSquared(RayOrigin, IntersectionPoint);
        FVector Normal = ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalNormal)).GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = FVector::UpVector;
        }
        if (FVector::DotProduct(Normal, SafeRayDirection) > 0.0)
        {
            Normal *= -1.0;
        }

        const FVector CachedWorldTangent =
            ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalTangent)).GetSafeNormal();
        FVector Tangent = (CachedWorldTangent - Normal * FVector::DotProduct(CachedWorldTangent, Normal)).GetSafeNormal();
        if (Tangent.IsNearlyZero())
        {
            Tangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal();
        if (Bitangent.IsNearlyZero())
        {
            Bitangent = MakeWetWrinkleAnyPerpendicular(Normal);
        }

        const FVector Barycentric(Barycentric3f);
        const FVector2f UV2f =
            Triangle.UVs[0] * Barycentric3f.X +
            Triangle.UVs[1] * Barycentric3f.Y +
            Triangle.UVs[2] * Barycentric3f.Z;

        BestSegmentT = SegmentT;
        OutHit.bHit = true;
        OutHit.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        OutHit.TriangleID = Triangle.TriangleID;
        OutHit.UVIslandID = Triangle.UVIslandID;
        OutHit.UVChannelIndex = HitTriangleUVChannelIndex != INDEX_NONE ? HitTriangleUVChannelIndex : BrushSettings.UVChannelIndex;
        OutHit.WorldPosition = IntersectionPoint;
        OutHit.WorldNormal = Normal;
        OutHit.WorldTangent = Tangent;
        OutHit.WorldBitangent = Bitangent;
        OutHit.LocalPosition = FVector(LocalIntersectionPoint);
        OutHit.LocalNormal = ComponentTransform.InverseTransformVectorNoScale(Normal).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        OutHit.LocalTangent = ComponentTransform.InverseTransformVectorNoScale(Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        OutHit.LocalBitangent = ComponentTransform.InverseTransformVectorNoScale(Bitangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
        OutHit.UV = FVector2D(UV2f);
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
            if (!Node.Bounds.IsValid ||
                !DoesWetWrinkleSegmentIntersectBox(Node.Bounds.ExpandBy(0.1f), LocalRayOrigin, LocalRayEnd))
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

UTexture* SWetWrinkleViewport::ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* SourceWetClothingAsset = ResolveSourceWetClothingAsset();
    if (SourceWetClothingAsset != nullptr)
    {
#if WITH_EDITORONLY_DATA
        const FWetClothingAuthoredMaterialSlot* SlotData =
            SourceWetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
        if (SlotData != nullptr && SlotData->bHasSourceTextureSelection)
        {
            return SlotData->SourceTexture.Get();
        }
#endif
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
    }

    RefreshWrinklePreviewWetnessParameter();
    RefreshWrinklePreviewAccumulatedParameters();
    RefreshWrinklePreviewTransientParameters();
    RefreshWrinklePreviewHoverParameters();
}

void SWetWrinkleViewport::RefreshWrinklePreviewWetnessParameter()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != INDEX_NONE)
    {
        EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
        if (PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
        {
            FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
            if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
            {
                SlotState.PreviewMID->SetScalarParameterValue(
                    WetWrinklePreviewMaterialParameters::PreviewWetness,
                    FMath::Clamp(BrushSettings.PreviewWetness, 0.0f, 1.0f));
            }
        }
    }

    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}

void SWetWrinkleViewport::RefreshWrinklePreviewAccumulatedParameters()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != INDEX_NONE)
    {
        EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
        if (PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
        {
            FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
            if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
            {
                SlotState.PreviewMID->SetTextureParameterValue(
                    WetWrinklePreviewMaterialParameters::AccumulatedNormal,
                    nullptr);
                SlotState.PreviewMID->SetScalarParameterValue(
                    WetWrinklePreviewMaterialParameters::AccumulatedEnabled,
                    0.0f);
                SlotState.PreviewMID->SetScalarParameterValue(
                    WetWrinklePreviewMaterialParameters::AccumulatedStrength,
                    1.0f);

                UTexture2D* PreviewNormalTexture = nullptr;
                if (GeneratedNormalPreviewTexture != nullptr &&
                    GeneratedNormalPreviewMaterialSlotIndex == ActiveMaterialSlotIndex &&
                    GeneratedNormalPreviewUVChannelIndex == BrushSettings.UVChannelIndex)
                {
                    PreviewNormalTexture = GeneratedNormalPreviewTexture.Get();
                }
                else
                {
                    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(ActiveMaterialSlotIndex);
                    PreviewNormalTexture = ResolveAccumulatedPreviewTexture(SourceTexture, ActiveMaterialSlotIndex, BrushSettings.UVChannelIndex);
                }

                if (PreviewNormalTexture != nullptr)
                {
                    SlotState.PreviewMID->SetTextureParameterValue(
                        WetWrinklePreviewMaterialParameters::AccumulatedNormal,
                        PreviewNormalTexture);
                    SlotState.PreviewMID->SetScalarParameterValue(
                        WetWrinklePreviewMaterialParameters::AccumulatedEnabled,
                        1.0f);
                }
            }
        }
    }

    if (bPreviewMaterialsNeedReapply || LastAppliedActivePreviewMaterialSlot != ActiveMaterialSlotIndex)
    {
        ApplyPreviewMaterialsToMesh();
    }
}

void SWetWrinkleViewport::RefreshWrinklePreviewTransientParameters()
{
    const int32 ActiveMaterialSlotIndex = ResolveActivePreviewMaterialSlot();
    if (ActiveMaterialSlotIndex != INDEX_NONE)
    {
        EnsurePreviewMaterialForSlot(ActiveMaterialSlotIndex);
        if (PreviewMaterialSlots.IsValidIndex(ActiveMaterialSlotIndex))
        {
            FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[ActiveMaterialSlotIndex];
            if (SlotState.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready && SlotState.PreviewMID != nullptr)
            {
                SlotState.PreviewMID->SetTextureParameterValue(
                    WetWrinklePreviewMaterialParameters::TransientRidgeNormal,
                    nullptr);
                SlotState.PreviewMID->SetScalarParameterValue(
                    WetWrinklePreviewMaterialParameters::TransientRidgeEnabled,
                    0.0f);

                if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
                    (!TransientProceduralStrokeHits.IsEmpty() || EditedProceduralStrokePreview.IsSet()) &&
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
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_EnsurePreviewMaterialForSlot);

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

    const FWetWrinklePreviewMaterialSlotState* SharedParentState =
        PreviewMaterialSlots.FindByPredicate(
            [&SlotState, RequiredUVChannelIndex](const FWetWrinklePreviewMaterialSlotState& Candidate)
            {
                return &Candidate != &SlotState &&
                       Candidate.PreviewStatus == EWetWrinklePreviewMaterialStatus::Ready &&
                       Candidate.PreviewSourceMaterial == SlotState.PreviewSourceMaterial &&
                       Candidate.PreviewUVChannelIndex == RequiredUVChannelIndex &&
                       Candidate.bUsesDwcWetMaterial == SlotState.bUsesDwcWetMaterial &&
                       Candidate.TransientPreviewParent != nullptr;
            });
    if (SharedParentState != nullptr)
    {
        SlotState.TransientPreviewMaterial = SharedParentState->TransientPreviewMaterial;
        SlotState.TransientPreviewParent = SharedParentState->TransientPreviewParent;
        SlotState.PreviewMID = UMaterialInstanceDynamic::Create(
            SlotState.TransientPreviewParent,
            GetTransientPackage());
        if (SlotState.PreviewMID != nullptr)
        {
            SlotState.PreviewMID->SetFlags(RF_Transient);
            SlotState.PreviewStatus = EWetWrinklePreviewMaterialStatus::Ready;
            SlotState.PreviewBuildError.Reset();
            ResetPreviewMaterialParameters(MaterialSlotIndex);
            return true;
        }
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
    DWCEditorPreviewSlotUtils::ApplyRenderProfileResources(
        WetClothingAsset.Get(),
        MaterialSlotIndex,
        PreviewMaterialSlots.Num(),
        SlotState.PreviewMID,
        PreviewScene.IsValid() ? PreviewScene->GetWorld() : nullptr);
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
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
        return;
    }

    PreviewState->TextureSize = ComputeWetWrinklePreviewTextureSize(WetClothingAsset.Get());
    PreviewState->WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(PreviewState->TextureSize);
    if (PreviewState->TextureSize.X <= 0 || PreviewState->TextureSize.Y <= 0 ||
        PreviewState->AccumulatedNormalTexture->GetSizeX() != PreviewState->TextureSize.X ||
        PreviewState->AccumulatedNormalTexture->GetSizeY() != PreviewState->TextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
        return;
    }

    if (PreviewState->Pixels.Num() != PreviewState->TextureSize.X * PreviewState->TextureSize.Y ||
        PreviewState->WorkingPixels.Num() != PreviewState->WorkingTextureSize.X * PreviewState->WorkingTextureSize.Y)
    {
        RebuildAccumulatedPreviewTexture(*PreviewState);
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
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
    PruneAccumulatedPreviewStates(Stamp.MaterialSlotIndex, Stamp.UVChannelIndex);
}

void SWetWrinkleViewport::AppendAccumulatedPreviewProceduralStroke(const FWetProceduralRidgeStroke& Stroke)
{
    if (!Stroke.bEnabled || Stroke.MaterialSlotIndex == INDEX_NONE || Stroke.UVChannelIndex < 0 || Stroke.Points.Num() < 2)
    {
        return;
    }

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(Stroke.MaterialSlotIndex);
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
        RefreshWrinklePreviewAccumulatedParameters();
        PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, Stroke.UVChannelIndex);
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
    PruneAccumulatedPreviewStates(Stroke.MaterialSlotIndex, Stroke.UVChannelIndex);
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStates()
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        ReleaseAccumulatedPreviewStateResources(PreviewState, true);
    }
    AccumulatedPreviewStates.Reset();
    AccumulatedPreviewUseSerial = 0;
}

void SWetWrinkleViewport::ReleaseAccumulatedPreviewStateResources(
    FWetWrinkleAccumulatedPreviewState& PreviewState,
    const bool bClearMaterialBinding)
{
    if (bClearMaterialBinding && PreviewMaterialSlots.IsValidIndex(PreviewState.MaterialSlotIndex))
    {
        FWetWrinklePreviewMaterialSlotState& SlotState = PreviewMaterialSlots[PreviewState.MaterialSlotIndex];
        if (SlotState.PreviewMID != nullptr)
        {
            SlotState.PreviewMID->SetTextureParameterValue(
                WetWrinklePreviewMaterialParameters::AccumulatedNormal,
                nullptr);
            SlotState.PreviewMID->SetScalarParameterValue(
                WetWrinklePreviewMaterialParameters::AccumulatedEnabled,
                0.0f);
        }
    }

    PreviewState.AccumulatedNormalTexture = nullptr;
    PreviewState.Pixels.Empty();
    PreviewState.WorkingPixels.Empty();
}

void SWetWrinkleViewport::PrepareAccumulatedPreviewStatesForSlot(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    for (FWetWrinkleAccumulatedPreviewState& PreviewState : AccumulatedPreviewStates)
    {
        if (PreviewState.MaterialSlotIndex == MaterialSlotIndex &&
            PreviewState.UVChannelIndex == UVChannelIndex)
        {
            PreviewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
            continue;
        }

        // The high-resolution working buffer is only useful while editing this slot.
        // Preserve the smaller final pixels/texture so revisiting a clean slot is instant.
        PreviewState.WorkingPixels.Empty();
    }

    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
}

void SWetWrinkleViewport::PruneAccumulatedPreviewStates(
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex)
{
    const uint64 BudgetBytes = ResolveCacheBudgetBytes(CVarWetWrinklePreviewCacheBudgetMB);
    uint64 ResidentBytes = EstimateAccumulatedPreviewCacheBytes(AccumulatedPreviewStates);
    while (ResidentBytes > BudgetBytes)
    {
        int32 OldestIndex = INDEX_NONE;
        uint64 OldestSerial = MAX_uint64;
        for (int32 StateIndex = 0; StateIndex < AccumulatedPreviewStates.Num(); ++StateIndex)
        {
            const FWetWrinkleAccumulatedPreviewState& PreviewState = AccumulatedPreviewStates[StateIndex];
            if (PreviewState.MaterialSlotIndex == MaterialSlotIndex &&
                PreviewState.UVChannelIndex == UVChannelIndex)
            {
                continue;
            }

            if (PreviewState.LastUsedSerial < OldestSerial)
            {
                OldestSerial = PreviewState.LastUsedSerial;
                OldestIndex = StateIndex;
            }
        }

        if (OldestIndex == INDEX_NONE)
        {
            break;
        }

        const FWetWrinkleAccumulatedPreviewState& EvictedState = AccumulatedPreviewStates[OldestIndex];
        const uint64 EvictedBytes = EstimateAccumulatedPreviewStateDynamicBytes(EvictedState);
        UE_LOG(
            LogWetWrinklePreviewViewport,
            VeryVerbose,
            TEXT("Evicting accumulated preview cache for slot %d UV %d (%llu bytes, resident=%llu, budget=%llu)."),
            EvictedState.MaterialSlotIndex,
            EvictedState.UVChannelIndex,
            EvictedBytes,
            ResidentBytes,
            BudgetBytes);
        const bool bClearMaterialBinding =
            EvictedState.MaterialSlotIndex != ResolveActivePreviewMaterialSlot();
        ReleaseAccumulatedPreviewStateResources(
            AccumulatedPreviewStates[OldestIndex],
            bClearMaterialBinding);
        AccumulatedPreviewStates.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
        ResidentBytes = EstimateAccumulatedPreviewCacheBytes(AccumulatedPreviewStates);
    }
}

void SWetWrinkleViewport::ReleaseTransientProceduralPreviewState()
{
    TransientProceduralPreviewState = FWetProceduralRidgeTransientPreviewState();
    bTransientProceduralPreviewBound = false;
    EditedProceduralStrokePreview.Reset();
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

    UTexture* SourceTexture = ResolveSourceTextureForMaterialSlot(MaterialSlotIndex);
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
        bTransientProceduralPreviewBound = true;
        RefreshWrinklePreviewTransientParameters();
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
            PreviewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
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
    NewState.LastUsedSerial = ++AccumulatedPreviewUseSerial;
    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
    return AccumulatedPreviewStates.FindByPredicate(
        [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleAccumulatedPreviewState& PreviewState)
        {
            return PreviewState.MaterialSlotIndex == MaterialSlotIndex &&
                   PreviewState.UVChannelIndex == UVChannelIndex;
        });
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
        PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
        return nullptr;
    }

    UTexture2D* Result = PreviewState->AccumulatedNormalTexture;
    PruneAccumulatedPreviewStates(MaterialSlotIndex, UVChannelIndex);
    return Result;
}

bool SWetWrinkleViewport::RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleViewport_RebuildAccumulatedPreviewTexture);

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

    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
            const bool bShowSection = BrushSettings.MaterialSlotIndex == INDEX_NONE
                                          ? DWCEditorPreviewSlotUtils::IsCpuPreviewReady(WetClothingAsset.Get(), Section.MaterialIndex)
                                          : Section.MaterialIndex == BrushSettings.MaterialSlotIndex;
            PreviewMeshComponent->ShowMaterialSection(Section.MaterialIndex, SectionIndex, bShowSection, LODIndex);
        }
    }
}

void SWetWrinkleViewport::InvalidateAccumulatedPreviewTextures()
{
    MarkAccumulatedPreviewStatesDirty();
}

TOptional<FWetWrinkleHitCacheKey> SWetWrinkleViewport::MakeHitCacheKey(
    const USkeletalMesh* Mesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex) const
{
    if (Mesh == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return {};
    }

    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return {};
    }

    FWetWrinkleHitCacheKey Key;
    Key.Mesh = Mesh;
    Key.LODRenderDataIdentity = &RenderData->LODRenderData[LODIndex];
    Key.LODIndex = LODIndex;
    Key.UVChannelIndex = UVChannelIndex;
    Key.MaterialSlotIndex = MaterialSlotIndex;

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr)
    {
        if (const FDWCDataUVLODMetadata* DataUVMetadata = Asset->FindDataUVMetadataForLOD(LODIndex);
            DataUVMetadata != nullptr && DataUVMetadata->UVChannelIndex == UVChannelIndex)
        {
            Key.TopologySignature = DataUVMetadata->DataUVOutputSignature;
        }
#if WITH_EDITORONLY_DATA
        else if (const FDWCEditorUVTopologyData* OriginalUVTopology = Asset->FindOriginalUVTopologyForLOD(LODIndex);
                 OriginalUVTopology != nullptr && OriginalUVTopology->UVChannelIndex == UVChannelIndex)
        {
            Key.TopologySignature = OriginalUVTopology->BuildSignature;
        }
#endif
        else
        {
            Key.TopologySignature = Asset->GetSourceMeshSignature();
        }
    }

    return Key;
}

bool SWetWrinkleViewport::RestoreHitCache(const FWetWrinkleHitCacheKey& Key)
{
    if (ActiveHitCacheKey.IsSet() && ActiveHitCacheKey.GetValue() == Key)
    {
        return true;
    }

    StoreActiveHitCache();

    FWetWrinkleHitCacheEntry* Entry = InactiveHitCaches.Find(Key);
    if (Entry == nullptr)
    {
        PruneInactiveHitCaches();
        return false;
    }

    CachedHitTriangles = MoveTemp(Entry->Triangles);
    CachedHitTriangleLookup = MoveTemp(Entry->TriangleLookup);
    HitBVHTriangleIndices = MoveTemp(Entry->BVHTriangleIndices);
    HitBVHNodes = MoveTemp(Entry->BVHNodes);
    UVTriangleGrid = MoveTemp(Entry->UVTriangleGrid);
    HitTriangleUVChannelIndex = Entry->UVChannelIndex;
    InactiveHitCaches.Remove(Key);
    ActiveHitCacheKey = Key;
    ++HitCacheUseSerial;
    PruneInactiveHitCaches();
    return true;
}

void SWetWrinkleViewport::StoreActiveHitCache()
{
    if (!ActiveHitCacheKey.IsSet())
    {
        ClearActiveHitCache();
        return;
    }

    FWetWrinkleHitCacheEntry Entry;
    Entry.Triangles = MoveTemp(CachedHitTriangles);
    Entry.TriangleLookup = MoveTemp(CachedHitTriangleLookup);
    Entry.BVHTriangleIndices = MoveTemp(HitBVHTriangleIndices);
    Entry.BVHNodes = MoveTemp(HitBVHNodes);
    Entry.UVTriangleGrid = MoveTemp(UVTriangleGrid);
    Entry.UVChannelIndex = HitTriangleUVChannelIndex;
    Entry.LastUsedSerial = ++HitCacheUseSerial;
    InactiveHitCaches.Add(ActiveHitCacheKey.GetValue(), MoveTemp(Entry));
    ActiveHitCacheKey.Reset();
    HitTriangleUVChannelIndex = INDEX_NONE;
}

void SWetWrinkleViewport::ClearActiveHitCache()
{
    CachedHitTriangles.Reset();
    CachedHitTriangleLookup.Reset();
    HitBVHTriangleIndices.Reset();
    HitBVHNodes.Reset();
    UVTriangleGrid.Reset();
    HitTriangleUVChannelIndex = INDEX_NONE;
    ActiveHitCacheKey.Reset();
}

void SWetWrinkleViewport::ClearAllHitCaches()
{
    ClearActiveHitCache();
    InactiveHitCaches.Reset();
    HitCacheUseSerial = 0;
}

void SWetWrinkleViewport::PruneInactiveHitCaches()
{
    const uint64 BudgetBytes = ResolveCacheBudgetBytes(CVarWetWrinkleHitCacheBudgetMB);
    const auto EstimateResidentBytes = [this]()
    {
        uint64 TotalBytes = static_cast<uint64>(InactiveHitCaches.GetAllocatedSize());
        if (ActiveHitCacheKey.IsSet())
        {
            TotalBytes += static_cast<uint64>(
                ActiveHitCacheKey.GetValue().TopologySignature.GetAllocatedSize());
        }
        TotalBytes += EstimateHitCacheDataBytes(
            CachedHitTriangles,
            CachedHitTriangleLookup,
            HitBVHTriangleIndices,
            HitBVHNodes,
            UVTriangleGrid);
        for (const TPair<FWetWrinkleHitCacheKey, FWetWrinkleHitCacheEntry>& Pair : InactiveHitCaches)
        {
            TotalBytes += EstimateHitCacheEntryDynamicBytes(Pair.Key, Pair.Value);
        }
        return TotalBytes;
    };

    uint64 ResidentBytes = EstimateResidentBytes();
    while (ResidentBytes > BudgetBytes && !InactiveHitCaches.IsEmpty())
    {
        TOptional<FWetWrinkleHitCacheKey> OldestKey;
        uint64 OldestSerial = TNumericLimits<uint64>::Max();
        for (const TPair<FWetWrinkleHitCacheKey, FWetWrinkleHitCacheEntry>& Pair : InactiveHitCaches)
        {
            if (Pair.Value.LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value.LastUsedSerial;
                OldestKey = Pair.Key;
            }
        }

        if (!OldestKey.IsSet())
        {
            break;
        }

        const FWetWrinkleHitCacheEntry* EvictedEntry = InactiveHitCaches.Find(OldestKey.GetValue());
        const uint64 EvictedBytes = EvictedEntry != nullptr
            ? EstimateHitCacheEntryDynamicBytes(OldestKey.GetValue(), *EvictedEntry)
            : 0;
        UE_LOG(
            LogWetWrinklePreviewViewport,
            VeryVerbose,
            TEXT("Evicting hit cache for slot %d UV %d (%llu bytes, resident=%llu, budget=%llu)."),
            OldestKey.GetValue().MaterialSlotIndex,
            OldestKey.GetValue().UVChannelIndex,
            EvictedBytes,
            ResidentBytes,
            BudgetBytes);
        InactiveHitCaches.Remove(OldestKey.GetValue());
        ResidentBytes = EstimateResidentBytes();
    }
}

void SWetWrinkleViewport::RebuildHitTriangles()
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        // All Slots is an overview-only state. Preserve the previous slot cache, but do not
        // build a combined all-section triangle set or BVH.
        StoreActiveHitCache();
        PruneInactiveHitCaches();
        return;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh();
    constexpr int32 HitTestLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    const TOptional<FWetWrinkleHitCacheKey> RequestedKey = MakeHitCacheKey(
        TargetMesh,
        HitTestLODIndex,
        BrushSettings.UVChannelIndex,
        BrushSettings.MaterialSlotIndex);
    if (!RequestedKey.IsSet())
    {
        StoreActiveHitCache();
        PruneInactiveHitCaches();
        return;
    }
    if (RestoreHitCache(RequestedKey.GetValue()))
    {
        return;
    }

    ClearActiveHitCache();
    ActiveHitCacheKey = RequestedKey;

    TArray<int32> RequestedMaterialSlots;
    const int32 MaterialCount = TargetMesh->GetMaterials().Num();
    RequestedMaterialSlots.Reserve(BrushSettings.MaterialSlotIndex == INDEX_NONE ? MaterialCount : 1);
    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
    {
        if (BrushSettings.MaterialSlotIndex == INDEX_NONE || BrushSettings.MaterialSlotIndex == MaterialSlotIndex)
        {
            RequestedMaterialSlots.Add(MaterialSlotIndex);
        }
    }

    if (RequestedMaterialSlots.IsEmpty())
    {
        ClearActiveHitCache();
        return;
    }

    HitTriangleUVChannelIndex = BrushSettings.UVChannelIndex;

    auto AppendCachedTriangle = [this](
        const int32 MaterialSlotIndex,
        const int32 TriangleID,
        const int32 UVIslandID,
        const auto* LocalPositions,
        const auto* UVs)
    {
        FWetWrinkleCachedHitTriangle& CachedTriangle = CachedHitTriangles.AddDefaulted_GetRef();
        CachedTriangle.MaterialSlotIndex = MaterialSlotIndex;
        CachedTriangle.TriangleID = TriangleID;
        CachedTriangle.UVIslandID = UVIslandID;

        CachedTriangle.LocalBounds = FBox3f(ForceInit);
        CachedTriangle.UVBounds = FBox2f(ForceInit);
        for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
        {
            CachedTriangle.LocalPositions[VertexIndex] = FVector3f(LocalPositions[VertexIndex]);
            CachedTriangle.UVs[VertexIndex] = FVector2f(UVs[VertexIndex]);
            CachedTriangle.LocalBounds += CachedTriangle.LocalPositions[VertexIndex];
            CachedTriangle.UVBounds += CachedTriangle.UVs[VertexIndex];
        }

        CachedTriangle.LocalNormal = FVector3f::CrossProduct(
            CachedTriangle.LocalPositions[1] - CachedTriangle.LocalPositions[0],
            CachedTriangle.LocalPositions[2] - CachedTriangle.LocalPositions[0]).GetSafeNormal();
        if (CachedTriangle.LocalNormal.IsNearlyZero())
        {
            CachedTriangle.LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
        }

        CachedTriangle.LocalTangent =
            (CachedTriangle.LocalPositions[1] - CachedTriangle.LocalPositions[0]).GetSafeNormal();
        CachedTriangle.LocalTangent =
            (CachedTriangle.LocalTangent -
             CachedTriangle.LocalNormal * FVector3f::DotProduct(CachedTriangle.LocalTangent, CachedTriangle.LocalNormal))
                .GetSafeNormal();
        if (CachedTriangle.LocalTangent.IsNearlyZero())
        {
            CachedTriangle.LocalTangent = MakeWetWrinkleAnyPerpendicular(CachedTriangle.LocalNormal);
        }

        CachedTriangle.LocalBitangent =
            FVector3f::CrossProduct(CachedTriangle.LocalNormal, CachedTriangle.LocalTangent).GetSafeNormal();
        if (CachedTriangle.LocalBitangent.IsNearlyZero())
        {
            CachedTriangle.LocalBitangent = MakeWetWrinkleAnyPerpendicular(CachedTriangle.LocalNormal);
        }
    };

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FDWCDataUVLODMetadata* DataUVMetadata =
        Asset != nullptr ? Asset->FindDataUVMetadataForLOD(HitTestLODIndex) : nullptr;
    const bool bHasDirectHitTopology =
        Asset != nullptr &&
        TargetMesh == Asset->GetDWCSkeletalMesh() &&
        DataUVMetadata != nullptr &&
        DataUVMetadata->bIsValid &&
        DataUVMetadata->UVChannelIndex == BrushSettings.UVChannelIndex &&
        !DataUVMetadata->DataUVIslandIDByTriangleID.IsEmpty();

    bool bBuiltDirectly = false;
    if (bHasDirectHitTopology)
    {
        TArray<FWCAUVPreviewSourceTriangle> SourceTriangles;
        FString TriangleReadError;
        if (FWCAUVPreviewTriangleReader::ReadFromSkeletalMesh(
                TargetMesh,
                HitTestLODIndex,
                BrushSettings.UVChannelIndex,
                RequestedMaterialSlots,
                SourceTriangles,
                &TriangleReadError))
        {
            bool bTopologyComplete = !SourceTriangles.IsEmpty();
            for (const FWCAUVPreviewSourceTriangle& Triangle : SourceTriangles)
            {
                if (!DataUVMetadata->DataUVIslandIDByTriangleID.IsValidIndex(Triangle.TriangleID) ||
                    DataUVMetadata->DataUVIslandIDByTriangleID[Triangle.TriangleID] == INDEX_NONE)
                {
                    bTopologyComplete = false;
                    break;
                }
            }

            if (bTopologyComplete)
            {
                CachedHitTriangles.Reserve(SourceTriangles.Num());
                for (const FWCAUVPreviewSourceTriangle& Triangle : SourceTriangles)
                {
                    AppendCachedTriangle(
                        Triangle.MaterialSlotIndex,
                        Triangle.TriangleID,
                        DataUVMetadata->DataUVIslandIDByTriangleID[Triangle.TriangleID],
                        Triangle.LocalPositions,
                        Triangle.UVs);
                }
                bBuiltDirectly = true;
            }
        }
    }

    if (!bBuiltDirectly)
    {
        for (const int32 MaterialSlotIndex : RequestedMaterialSlots)
        {
            TArray<FWetClothingAssetUVIsland> Islands;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                    TargetMesh,
                    HitTestLODIndex,
                    BrushSettings.UVChannelIndex,
                    MaterialSlotIndex,
                    Islands,
                    nullptr))
            {
                continue;
            }

            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                CachedHitTriangles.Reserve(CachedHitTriangles.Num() + Island.UVTriangles.Num());
                for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
                {
                    AppendCachedTriangle(
                        Triangle.MaterialSlotIndex,
                        Triangle.TriangleID,
                        Triangle.UVIslandID,
                        Triangle.LocalPositions,
                        Triangle.UVs);
                }
            }
        }
    }

    if (CachedHitTriangles.IsEmpty())
    {
        // Do not cache a failed/temporarily unavailable build. A later refresh should retry it.
        ClearActiveHitCache();
        return;
    }

    RebuildHitTriangleAccelerationStructures();
    PruneInactiveHitCaches();
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
        FBox3f Bounds(ForceInit);
        FBox3f CenterBounds(ForceInit);
        for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
        {
            const FWetWrinkleCachedHitTriangle& Triangle = CachedHitTriangles[HitBVHTriangleIndices[FirstIndex + Offset]];
            Bounds += Triangle.LocalBounds;
            CenterBounds += Triangle.LocalBounds.GetCenter();
        }

        HitBVHNodes[NodeIndex].Bounds = Bounds;
        HitBVHNodes[NodeIndex].FirstTriangleIndex = FirstIndex;
        HitBVHNodes[NodeIndex].TriangleCount = TriangleCount;
        if (TriangleCount <= WetWrinkleBVHLeafTriangleCount)
        {
            return NodeIndex;
        }

        const FVector3f Extent = CenterBounds.GetExtent();
        const int32 SplitAxis = Extent.Y > Extent.X
            ? (Extent.Z > Extent.Y ? 2 : 1)
            : (Extent.Z > Extent.X ? 2 : 0);
        TArrayView<int32> TriangleRange(HitBVHTriangleIndices.GetData() + FirstIndex, TriangleCount);
        Algo::Sort(
            TriangleRange,
            [this, SplitAxis](const int32 A, const int32 B)
            {
                return CachedHitTriangles[A].LocalBounds.GetCenter()[SplitAxis] <
                    CachedHitTriangles[B].LocalBounds.GetCenter()[SplitAxis];
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
    const FVector2f QueryUV2f(QueryUV);
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();

    auto TestTriangle = [
        &OutSurfaces,
        MaterialSlotIndex,
        &QueryUV,
        &QueryUV2f,
        &ComponentTransform](const FWetWrinkleCachedHitTriangle& Triangle)
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

        const FVector3f Barycentric3f =
            ComputeWetWrinkleBarycentric2D(QueryUV2f, Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        if (!IsWetWrinkleBarycentricInside(Barycentric3f))
        {
            return;
        }

        const FVector3f LocalPosition =
            Triangle.LocalPositions[0] * Barycentric3f.X +
            Triangle.LocalPositions[1] * Barycentric3f.Y +
            Triangle.LocalPositions[2] * Barycentric3f.Z;
        FWetWrinkleProjectedSurface ProjectedSurface;
        ProjectedSurface.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        ProjectedSurface.TriangleID = Triangle.TriangleID;
        ProjectedSurface.UVIslandID = Triangle.UVIslandID;
        ProjectedSurface.Barycentric = FVector(Barycentric3f);
        ProjectedSurface.WorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
        ProjectedSurface.WorldNormal =
            ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalNormal))
                .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        ProjectedSurface.WorldTangent =
            ComponentTransform.TransformVectorNoScale(FVector(Triangle.LocalTangent))
                .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        ProjectedSurface.WorldBitangent =
            FVector::CrossProduct(ProjectedSurface.WorldNormal, ProjectedSurface.WorldTangent)
                .GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
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
    if (Triangle == nullptr || PreviewMeshComponent == nullptr)
    {
        return false;
    }

    const FVector3f Barycentric(Point.AnchorBarycentric);
    const FVector3f LocalPosition =
        Triangle->LocalPositions[0] * Barycentric.X +
        Triangle->LocalPositions[1] * Barycentric.Y +
        Triangle->LocalPositions[2] * Barycentric.Z;
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
    OutWorldPosition = ComponentTransform.TransformPosition(FVector(LocalPosition));
    OutWorldNormal =
        ComponentTransform.TransformVectorNoScale(FVector(Triangle->LocalNormal))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
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

    const FVector3f Barycentric3f(Point.AnchorBarycentric);
    const FTransform ComponentTransform = PreviewMeshComponent->GetComponentTransform();
    FVector Normal =
        ComponentTransform.TransformVectorNoScale(FVector(Triangle->LocalNormal))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    const FVector CachedWorldTangent =
        ComponentTransform.TransformVectorNoScale(FVector(Triangle->LocalTangent))
            .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    FVector Tangent = (CachedWorldTangent - Normal * FVector::DotProduct(CachedWorldTangent, Normal))
                          .GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    FVector Bitangent = FVector::CrossProduct(Normal, Tangent).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
    const FVector3f LocalPosition3f =
        Triangle->LocalPositions[0] * Barycentric3f.X +
        Triangle->LocalPositions[1] * Barycentric3f.Y +
        Triangle->LocalPositions[2] * Barycentric3f.Z;
    const FVector LocalPosition(LocalPosition3f);
    const FVector WorldPosition = ComponentTransform.TransformPosition(LocalPosition);
    const FVector Barycentric(Barycentric3f);

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
    OutHit.LocalNormal = FVector(Triangle->LocalNormal);
    OutHit.LocalTangent = FVector(Triangle->LocalTangent);
    OutHit.LocalBitangent = FVector(Triangle->LocalBitangent);
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

    const FWetProceduralRidgeStroke* StrokeToDraw = nullptr;
    if (EditedProceduralStrokePreview.IsSet() &&
        EditedProceduralStrokePreview->StrokeGuid == SelectedProceduralStrokeGuid)
    {
        StrokeToDraw = &EditedProceduralStrokePreview.GetValue();
    }
    else if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        StrokeToDraw = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
            [this](const FWetProceduralRidgeStroke& Candidate)
            {
                return Candidate.StrokeGuid == SelectedProceduralStrokeGuid;
            });
    }

    if (StrokeToDraw != nullptr)
    {
        const FWetProceduralRidgeStroke* Stroke = StrokeToDraw;
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
