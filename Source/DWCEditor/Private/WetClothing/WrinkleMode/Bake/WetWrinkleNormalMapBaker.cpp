#include "WetClothing/WrinkleMode/Bake/WetWrinkleNormalMapBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"

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

            const float ClampedU = FMath::Clamp(static_cast<float>(UV.X), 0.0f, 1.0f);
            const float ClampedV = FMath::Clamp(static_cast<float>(UV.Y), 0.0f, 1.0f);
            const int32 PixelX = FMath::Clamp(FMath::FloorToInt(ClampedU * static_cast<float>(SizeX)), 0, SizeX - 1);
            const int32 PixelY = FMath::Clamp(FMath::FloorToInt(ClampedV * static_cast<float>(SizeY)), 0, SizeY - 1);

            if (SourceFormat == TSF_G8)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

            if (SourceFormat == TSF_G16)
            {
                return FVector(0.0f, 0.0f, 1.0f);
            }

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

    FVector DecodeNormal(const FColor& Color)
    {
        FVector Normal(
            static_cast<float>(Color.R) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.G) / 255.0f * 2.0f - 1.0f,
            static_cast<float>(Color.B) / 255.0f * 2.0f - 1.0f);
        if (Normal.Z <= UE_SMALL_NUMBER)
        {
            const float XYLengthSq = FMath::Min(Normal.X * Normal.X + Normal.Y * Normal.Y, 1.0f);
            Normal.Z = FMath::Sqrt(FMath::Max(1.0f - XYLengthSq, 0.0f));
        }

        return Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
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

    FVector3f BlendAngleCorrectedNormals(const FVector3f& BaseNormal, const FVector3f& DetailNormal)
    {
        const FVector Base = FVector(BaseNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        const FVector Detail = FVector(DetailNormal).GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        const FVector Blended =
            FVector(
                Base.X + Detail.X,
                Base.Y + Detail.Y,
                Base.Z * Detail.Z)
                .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f));
        return FVector3f(Blended);
    }

    bool IsUVPointInsideTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }
}

struct FWetWrinkleNormalMapBaker::FBakeGroup
{
    int32 LODIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    UTexture* SourceTexture = nullptr;
    TArray<const FWetWrinklePatchPlacement*> Stamps;
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

    if (WetClothingAsset->TargetMesh == nullptr)
    {
        OutErrorMessage = TEXT("Assign a Target Mesh before baking wrinkle maps.");
        return false;
    }

    if (!WetClothingAsset->TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        OutErrorMessage = FString::Printf(
            TEXT("The selected material slot (%d) is not valid for the current Target Mesh."),
            MaterialSlotIndex);
        return false;
    }

    TArray<int32> BakeUVChannelIndices;
    if (!ResolveBakeUVChannelIndices(*WetClothingAsset, MaterialSlotIndex, Settings, BakeUVChannelIndices, OutErrorMessage))
    {
        return false;
    }

    TArray<int32> LODIndices = WetClothingAsset->WrinkleData.BakeSettings.TargetLODIndices;
    if (LODIndices.Num() == 0)
    {
        LODIndices.Add(0);
    }

    for (const int32 LODIndex : LODIndices)
    {
        for (const int32 BakeUVChannelIndex : BakeUVChannelIndices)
        {
            FBakeGroup Group;
            if (!BuildBakeGroupForMaterialSlot(
                    *WetClothingAsset,
                    MaterialSlotIndex,
                    BakeUVChannelIndex,
                    FMath::Max(0, LODIndex),
                    Settings,
                    Group,
                    OutErrorMessage))
            {
                return false;
            }

            if (Group.Stamps.Num() > 0 && !BakeGroup(*WetClothingAsset, Group, Settings, OutResult, OutErrorMessage))
            {
                return false;
            }
        }
    }

    if (OutResult.BakedMapCount == 0)
    {
        OutErrorMessage = TEXT("No wrinkle patches were found for the selected material slot on the active wrinkle UV channel. Add at least one patch to this slot before baking.");
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

bool FWetWrinkleNormalMapBaker::ResolveBakeUVChannelIndices(
    const UWetClothingAsset& WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    TArray<int32>& OutUVChannelIndices,
    FString& OutErrorMessage)
{
    TSet<int32> UniqueUVChannels;

    for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset.WrinkleData.EditablePatchStrokes)
    {
        if (!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes)
        {
            continue;
        }

        for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
        {
            if (Patch.MaterialSlotIndex != MaterialSlotIndex || Patch.NormalPatchTexture == nullptr)
            {
                continue;
            }

            UniqueUVChannels.Add(Patch.UVChannelIndex);
        }
    }

    if (UniqueUVChannels.Num() == 0)
    {
        OutErrorMessage = TEXT("No wrinkle patches were found for the selected material slot.");
        return false;
    }

    if (Settings.PreferredUVChannelIndex != INDEX_NONE)
    {
        OutUVChannelIndices = {Settings.PreferredUVChannelIndex};
        OutErrorMessage.Reset();
        return true;
    }

    if (WetClothingAsset.WrinkleData.WrinkleUVChannelIndex != INDEX_NONE)
    {
        OutUVChannelIndices = {WetClothingAsset.WrinkleData.WrinkleUVChannelIndex};
        OutErrorMessage.Reset();
        return true;
    }

    OutUVChannelIndices.Reset();
    OutUVChannelIndices.Reserve(UniqueUVChannels.Num());
    for (const int32 UVChannelIndex : UniqueUVChannels)
    {
        OutUVChannelIndices.Add(UVChannelIndex);
    }
    OutUVChannelIndices.Sort();
    OutErrorMessage.Reset();
    return true;
}

bool FWetWrinkleNormalMapBaker::BuildBakeGroupForMaterialSlot(
    const UWetClothingAsset& WetClothingAsset,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    const int32 LODIndex,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FBakeGroup& OutGroup,
    FString& OutErrorMessage)
{
    OutGroup = FBakeGroup();
    OutGroup.LODIndex = FMath::Max(0, LODIndex);
    OutGroup.MaterialSlotIndex = MaterialSlotIndex;
    OutGroup.UVChannelIndex = UVChannelIndex;

    auto TryAppendPatch = [&OutGroup, MaterialSlotIndex, UVChannelIndex, &OutErrorMessage](const FWetWrinklePatchPlacement& Patch) -> bool
    {
        if (Patch.MaterialSlotIndex != MaterialSlotIndex || Patch.NormalPatchTexture == nullptr)
        {
            return true;
        }

        if (OutGroup.SourceTexture == nullptr)
        {
            OutGroup.SourceTexture = Patch.SourceTexture.Get();
        }
        else if (Patch.SourceTexture != nullptr && OutGroup.SourceTexture != Patch.SourceTexture)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Material slot %d / UV %d contains wrinkle patches that reference multiple source textures. This is not yet supported by the wrinkle normal map baker."),
                MaterialSlotIndex,
                UVChannelIndex);
            return false;
        }

        OutGroup.Stamps.Add(&Patch);
        return true;
    };

    bool bFoundExactUVMatch = false;
    for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset.WrinkleData.EditablePatchStrokes)
    {
        if (!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes)
        {
            continue;
        }

        for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
        {
            if (Patch.MaterialSlotIndex != MaterialSlotIndex || Patch.UVChannelIndex != UVChannelIndex)
            {
                continue;
            }

            bFoundExactUVMatch = true;
            if (!TryAppendPatch(Patch))
            {
                return false;
            }
        }
    }

    const bool bAllowLegacyUV0Fallback =
        !bFoundExactUVMatch &&
        OutGroup.Stamps.Num() == 0 &&
        UVChannelIndex != 0 &&
        WetClothingAsset.WrinkleData.WrinkleUVChannelIndex == UVChannelIndex;

    if (bAllowLegacyUV0Fallback)
    {
        for (const FWetWrinklePatchStroke& Stroke : WetClothingAsset.WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.bEnabled && !Settings.bIncludeDisabledPatchStrokes)
            {
                continue;
            }

            for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
            {
                if (Patch.MaterialSlotIndex != MaterialSlotIndex || Patch.UVChannelIndex != 0)
                {
                    continue;
                }

                if (!TryAppendPatch(Patch))
                {
                    return false;
                }
            }
        }
    }

    OutErrorMessage.Reset();
    return true;
}

void FWetWrinkleNormalMapBaker::InitializeIntermediateBuffers(
    FIntermediateBakeResult& OutIntermediateResult,
    const int32 Width,
    const int32 Height,
    const int32 Resolution)
{
    OutIntermediateResult.Width = Width;
    OutIntermediateResult.Height = Height;
    OutIntermediateResult.Resolution = Resolution;
    OutIntermediateResult.NormalBuffer.Init(FVector3f(0.0f, 0.0f, 1.0f), Width * Height);
    OutIntermediateResult.IslandMask.Init(false, Width * Height);
    OutIntermediateResult.PatchCoverageBuffer.Init(0.0f, Width * Height);
}

bool FWetWrinkleNormalMapBaker::BuildTrianglesAndIslands(
    const UWetClothingAsset& WetClothingAsset,
    const int32 LODIndex,
    const int32 MaterialSlotIndex,
    const int32 UVChannelIndex,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles,
    FString& OutErrorMessage)
{
    OutIslands.Reset();
    OutTriangles.Reset();

    if (WetClothingAsset.TargetMesh == nullptr)
    {
        OutErrorMessage = TEXT("Assign a TargetMesh before baking a wrinkle normal map.");
        return false;
    }

    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            WetClothingAsset.TargetMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            OutIslands,
            &OutErrorMessage))
    {
        if (OutErrorMessage.IsEmpty())
        {
            OutErrorMessage = FString::Printf(
                TEXT("Failed to build UV triangles for material slot %d on UV channel %d (LOD %d)."),
                MaterialSlotIndex,
                UVChannelIndex,
                LODIndex);
        }
        return false;
    }

    for (const FWetClothingAssetUVIsland& Island : OutIslands)
    {
        OutTriangles.Append(Island.UVTriangles);
    }

    if (OutTriangles.Num() == 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("The selected material slot does not contain any usable UV triangles on UV channel %d (LOD %d)."),
            UVChannelIndex,
            LODIndex);
        return false;
    }

    OutErrorMessage.Reset();
    return true;
}

int32 FWetWrinkleNormalMapBaker::RasterizeTriangleMask(
    TArray<bool>& OutMask,
    const int32 Width,
    const int32 Height,
    const FWetClothingAssetUVTriangle& Triangle)
{
    const FVector2D& A = Triangle.UVs[0];
    const FVector2D& B = Triangle.UVs[1];
    const FVector2D& C = Triangle.UVs[2];

    const double MinU = FMath::Min3(A.X, B.X, C.X);
    const double MaxU = FMath::Max3(A.X, B.X, C.X);
    const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
    const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

    const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * Width), 0, Width - 1);
    const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * Width), 0, Width - 1);
    const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * Height), 0, Height - 1);
    const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxV * Height), 0, Height - 1);

    int32 PaintedPixelCount = 0;
    for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
    {
        for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
        {
            const FVector2D SampleUV(
                (static_cast<double>(PixelX) + 0.5) / Width,
                (static_cast<double>(PixelY) + 0.5) / Height);

            if (!IsUVPointInsideTriangle(SampleUV, A, B, C))
            {
                continue;
            }

            const int32 PixelIndex = PixelY * Width + PixelX;
            if (!OutMask[PixelIndex])
            {
                OutMask[PixelIndex] = true;
                ++PaintedPixelCount;
            }
        }
    }

    if (PaintedPixelCount == 0)
    {
        const FVector2D CenterUV = (A + B + C) / 3.0f;
        const int32 FallbackX = FMath::Clamp(FMath::FloorToInt(CenterUV.X * Width), 0, Width - 1);
        const int32 FallbackY = FMath::Clamp(FMath::FloorToInt(CenterUV.Y * Height), 0, Height - 1);
        const int32 PixelIndex = FallbackY * Width + FallbackX;
        if (!OutMask[PixelIndex])
        {
            OutMask[PixelIndex] = true;
            ++PaintedPixelCount;
        }
    }

    return PaintedPixelCount;
}

void FWetWrinkleNormalMapBaker::RasterizeIslandMask(FIntermediateBakeResult& InOutIntermediateResult)
{
    for (const FWetClothingAssetUVTriangle& Triangle : InOutIntermediateResult.UVTriangles)
    {
        RasterizeTriangleMask(
            InOutIntermediateResult.IslandMask,
            InOutIntermediateResult.Width,
            InOutIntermediateResult.Height,
            Triangle);
    }
}

bool FWetWrinkleNormalMapBaker::RasterizeSinglePatch(
    FIntermediateBakeResult& InOutIntermediateResult,
    const FWetWrinklePatchPlacement& Patch,
    FString& OutErrorMessage)
{
    if (!InOutIntermediateResult.IsInitialized())
    {
        OutErrorMessage = TEXT("Intermediate wrinkle bake buffers are not initialized.");
        return false;
    }

    FWetWrinkleNormalSource NormalSource(Patch.NormalPatchTexture.Get());
    if (!NormalSource.IsValid())
    {
        OutErrorMessage = FString::Printf(
            TEXT("Patch '%s' uses a normal texture that does not expose readable source data. Reimport or resave the texture with source data before baking."),
            *Patch.PatchGuid.ToString());
        return false;
    }

    if (Patch.BrushRadiusUV <= 0.0f || Patch.Strength <= 0.0f)
    {
        OutErrorMessage.Reset();
        return true;
    }

    const FVector2D WrappedCenter = WrapUV(Patch.PositionUV);
    const FVector2D SafeScale(
        FMath::Max(FMath::Abs(Patch.Scale.X), UE_SMALL_NUMBER),
        FMath::Max(FMath::Abs(Patch.Scale.Y), UE_SMALL_NUMBER));
    const float EdgeFadeStart = FMath::Clamp(1.0f - Patch.Falloff, 0.0f, 0.98f);
    const float CosRotation = FMath::Cos(Patch.RotationRadians);
    const float SinRotation = FMath::Sin(Patch.RotationRadians);

    for (int32 TileOffsetY = -1; TileOffsetY <= 1; ++TileOffsetY)
    {
        for (int32 TileOffsetX = -1; TileOffsetX <= 1; ++TileOffsetX)
        {
            const FVector2D TileCenter = WrappedCenter + FVector2D(static_cast<float>(TileOffsetX), static_cast<float>(TileOffsetY));
            const int32 MinX = FMath::Clamp(FMath::FloorToInt((TileCenter.X - Patch.BrushRadiusUV) * InOutIntermediateResult.Width), 0, InOutIntermediateResult.Width - 1);
            const int32 MaxX = FMath::Clamp(FMath::CeilToInt((TileCenter.X + Patch.BrushRadiusUV) * InOutIntermediateResult.Width), 0, InOutIntermediateResult.Width - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt((TileCenter.Y - Patch.BrushRadiusUV) * InOutIntermediateResult.Height), 0, InOutIntermediateResult.Height - 1);
            const int32 MaxY = FMath::Clamp(FMath::CeilToInt((TileCenter.Y + Patch.BrushRadiusUV) * InOutIntermediateResult.Height), 0, InOutIntermediateResult.Height - 1);
            if (MinX > MaxX || MinY > MaxY)
            {
                continue;
            }

            for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
            {
                for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                {
                    const int32 PixelIndex = PixelY * InOutIntermediateResult.Width + PixelX;
                    if (!InOutIntermediateResult.IslandMask[PixelIndex])
                    {
                        continue;
                    }

                    const FVector2D PixelUV(
                        (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(InOutIntermediateResult.Width),
                        (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(InOutIntermediateResult.Height));
                    const FVector2D DeltaUV(
                        WrappedDelta(PixelUV.X - TileCenter.X),
                        WrappedDelta(PixelUV.Y - TileCenter.Y));
                    const FVector2D Local = DeltaUV / FMath::Max(Patch.BrushRadiusUV, UE_SMALL_NUMBER);
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
                    const FVector RotatedBrushNormalTS(
                        BrushNormalTS.X * CosRotation - BrushNormalTS.Y * SinRotation,
                        BrushNormalTS.X * SinRotation + BrushNormalTS.Y * CosRotation,
                        BrushNormalTS.Z);
                    const float StrengthScale = FMath::Max(Patch.Strength * EdgeFade, 0.0f);
                    const FVector3f WeightedPatchNormalTS = FVector3f(
                        FVector(
                            RotatedBrushNormalTS.X * StrengthScale,
                            RotatedBrushNormalTS.Y * StrengthScale,
                            RotatedBrushNormalTS.Z)
                            .GetSafeNormal(UE_SMALL_NUMBER, FVector(0.0f, 0.0f, 1.0f)));

                    InOutIntermediateResult.NormalBuffer[PixelIndex] =
                        BlendAngleCorrectedNormals(InOutIntermediateResult.NormalBuffer[PixelIndex], WeightedPatchNormalTS);
                    InOutIntermediateResult.PatchCoverageBuffer[PixelIndex] =
                        FMath::Max(InOutIntermediateResult.PatchCoverageBuffer[PixelIndex], EdgeFade);
                }
            }
        }
    }

    OutErrorMessage.Reset();
    return true;
}

bool FWetWrinkleNormalMapBaker::BuildIntermediateBakeResult(
    UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FIntermediateBakeResult& OutIntermediateResult,
    FString& OutErrorMessage)
{
    OutIntermediateResult = FIntermediateBakeResult();
    OutIntermediateResult.LODIndex = Group.LODIndex;
    OutIntermediateResult.MaterialSlotIndex = Group.MaterialSlotIndex;
    OutIntermediateResult.UVChannelIndex = Group.UVChannelIndex;
    OutIntermediateResult.SourceTexture = Group.SourceTexture;
    OutIntermediateResult.FilteredPatchPlacements = Group.Stamps;

    if (Group.Stamps.Num() == 0)
    {
        OutErrorMessage = FString::Printf(
            TEXT("No wrinkle patches were found for material slot %d and UV channel %d."),
            Group.MaterialSlotIndex,
            Group.UVChannelIndex);
        return false;
    }

    const int32 MaxResolution = FMath::Clamp(Settings.Resolution, 16, 8192);
    const int32 SourceWidth = FMath::Max(Group.SourceTexture != nullptr ? Group.SourceTexture->GetSurfaceWidth() : MaxResolution, 1);
    const int32 SourceHeight = FMath::Max(Group.SourceTexture != nullptr ? Group.SourceTexture->GetSurfaceHeight() : MaxResolution, 1);
    const double ResolutionScale = static_cast<double>(MaxResolution) / FMath::Max(SourceWidth, SourceHeight);
    const int32 Width = FMath::Clamp(FMath::RoundToInt(SourceWidth * ResolutionScale), 1, 8192);
    const int32 Height = FMath::Clamp(FMath::RoundToInt(SourceHeight * ResolutionScale), 1, 8192);

    InitializeIntermediateBuffers(OutIntermediateResult, Width, Height, MaxResolution);

    if (!BuildTrianglesAndIslands(
            WetClothingAsset,
            Group.LODIndex,
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            OutIntermediateResult.UVIslands,
            OutIntermediateResult.UVTriangles,
            OutErrorMessage))
    {
        return false;
    }

    RasterizeIslandMask(OutIntermediateResult);

    for (const FWetWrinklePatchPlacement* Patch : Group.Stamps)
    {
        if (Patch == nullptr)
        {
            continue;
        }

        if (!RasterizeSinglePatch(OutIntermediateResult, *Patch, OutErrorMessage))
        {
            return false;
        }
    }

    const bool bAnyPatchPixelsRasterized = OutIntermediateResult.PatchCoverageBuffer.ContainsByPredicate(
        [](const float Coverage)
        {
            return Coverage > UE_SMALL_NUMBER;
        });

    if (!bAnyPatchPixelsRasterized)
    {
        OutErrorMessage = FString::Printf(
            TEXT("The selected material slot did not receive any wrinkle pixels during bake on UV channel %d. Check that the patches were placed on this slot and that the active wrinkle UV channel matches the bake target."),
            Group.UVChannelIndex);
        return false;
    }

    DilateBakedNormals(OutIntermediateResult, Settings.PaddingPixels);

    OutErrorMessage.Reset();
    return true;
}

void FWetWrinkleNormalMapBaker::DilateBakedNormals(
    FIntermediateBakeResult& InOutIntermediateResult,
    const int32 PaddingPixels)
{
    const int32 ClampedPaddingPixels = FMath::Clamp(PaddingPixels, 0, 64);
    if (!InOutIntermediateResult.IsInitialized() || ClampedPaddingPixels <= 0)
    {
        return;
    }

    TArray<bool> FilledMask = InOutIntermediateResult.IslandMask;
    for (int32 PaddingStep = 0; PaddingStep < ClampedPaddingPixels; ++PaddingStep)
    {
        const TArray<FVector3f> PreviousNormals = InOutIntermediateResult.NormalBuffer;
        const TArray<bool> PreviousMask = FilledMask;
        bool bWrotePixel = false;

        for (int32 Y = 0; Y < InOutIntermediateResult.Height; ++Y)
        {
            for (int32 X = 0; X < InOutIntermediateResult.Width; ++X)
            {
                const int32 PixelIndex = Y * InOutIntermediateResult.Width + X;
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
                        if (NeighborX < 0 ||
                            NeighborY < 0 ||
                            NeighborX >= InOutIntermediateResult.Width ||
                            NeighborY >= InOutIntermediateResult.Height)
                        {
                            continue;
                        }

                        const int32 NeighborIndex = NeighborY * InOutIntermediateResult.Width + NeighborX;
                        if (!PreviousMask[NeighborIndex])
                        {
                            continue;
                        }

                        InOutIntermediateResult.NormalBuffer[PixelIndex] = PreviousNormals[NeighborIndex];
                        FilledMask[PixelIndex] = true;
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

void FWetWrinkleNormalMapBaker::ConvertIntermediateToPixels(
    const FIntermediateBakeResult& IntermediateResult,
    TArray<FColor>& OutNormalPixels,
    TArray<FColor>& OutMaskPixels)
{
    const int32 PixelCount = IntermediateResult.Width * IntermediateResult.Height;
    OutNormalPixels.SetNumUninitialized(PixelCount);
    OutMaskPixels.SetNumUninitialized(PixelCount);

    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        OutNormalPixels[PixelIndex] = EncodeNormal(FVector(IntermediateResult.NormalBuffer[PixelIndex]));
        const uint8 MaskValue = EncodeUnit(IntermediateResult.PatchCoverageBuffer[PixelIndex]);
        OutMaskPixels[PixelIndex] = FColor(MaskValue, MaskValue, MaskValue, 255);
    }
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

    if (Group.Stamps.Num() == 0)
    {
        return true;
    }

    FIntermediateBakeResult IntermediateResult;
    if (!BuildIntermediateBakeResult(WetClothingAsset, Group, Settings, IntermediateResult, OutErrorMessage))
    {
        return false;
    }

    if (IntermediateResult.FilteredPatchPlacements.Num() == 0)
    {
        return true;
    }

    TArray<FColor> NormalPixels;
    TArray<FColor> MaskPixels;
    ConvertIntermediateToPixels(IntermediateResult, NormalPixels, MaskPixels);

    const int32 Width = IntermediateResult.Width;
    const int32 Height = IntermediateResult.Height;
    const int32 MaxResolution = IntermediateResult.Resolution;
    const int32 BakedStampCount = IntermediateResult.FilteredPatchPlacements.Num();

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
    BakedMap->BuildSignature = MakeBuildSignature(WetClothingAsset, Group, Width, Height);
    BakedMap->BakeGuid = FGuid::NewGuid();
    WetClothingAsset.MarkPackageDirty();

    InOutResult.BakedMapCount++;
    InOutResult.BakedStampCount += BakedStampCount;
    InOutResult.BakedUVChannelIndices.AddUnique(Group.UVChannelIndex);
    if (NormalTexture != nullptr)
    {
        InOutResult.BakedNormalMaps.Add(NormalTexture);
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
    const int32 Height)
{
    FString Canonical;
    Canonical.Reserve(4096);
    Canonical += TEXT("DWC.WrinkleNormalMap.v2|");
    Canonical += WetClothingAsset.GetPathName();
    Canonical += FString::Printf(
        TEXT("|Slot=%d|UV=%d|LOD=%d|Size=%dx%d|Source=%s"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        Width,
        Height,
        *GetPathNameSafe(Group.SourceTexture));

    for (const FWetWrinklePatchPlacement* Stamp : Group.Stamps)
    {
        Canonical += FString::Printf(
            TEXT("|Stamp:%s;UV=%.9g,%.9g;Radius=%.9g;Rot=%.9g;Scale=%.9g,%.9g;Strength=%.9g;Falloff=%.9g;Normal=%s"),
            *Stamp->PatchGuid.ToString(EGuidFormats::Digits),
            Stamp->PositionUV.X,
            Stamp->PositionUV.Y,
            Stamp->BrushRadiusUV,
            Stamp->RotationRadians,
            Stamp->Scale.X,
            Stamp->Scale.Y,
            Stamp->Strength,
            Stamp->Falloff,
            *GetPathNameSafe(Stamp->NormalPatchTexture));
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
    const FString PackagePath = FPackageName::GetLongPackagePath(AssetPackageName);
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
    Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Grayscale;
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
