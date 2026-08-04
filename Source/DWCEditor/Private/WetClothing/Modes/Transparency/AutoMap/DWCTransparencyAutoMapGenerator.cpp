#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

#include "DataAssets/DWCBakeLayer.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSourceResolver.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

namespace
{
    FDWCBakeResolvedLayer MakeResolvedLayer(
        USkeletalMesh* Mesh,
        const FName LayerId,
        const int32 LayerOrder,
        const float MaxRevealDistance)
    {
        FDWCBakeResolvedLayer Result;
        Result.LayerId = LayerId;
        Result.LayerOrder = LayerOrder;
        Result.ComponentDisplayName = Mesh != nullptr ? Mesh->GetFName() : NAME_None;
        Result.ComponentPath = GetPathNameSafe(Mesh);
        Result.SkeletalMesh = Mesh;
        Result.BakeTransform = FTransform::Identity;
        Result.bCanBeRevealSource = true;
        Result.bCanBeWetOuterLayer = true;
        Result.bBlocksReveal = false;
        Result.MaxRevealDistance = MaxRevealDistance;
        if (Mesh != nullptr)
        {
            for (const FSkeletalMaterial& Material : Mesh->GetMaterials())
            {
                Result.Materials.Add(Material.MaterialInterface);
            }
        }
        return Result;
    }

    FName MakeInnerSourceLayerId(const int32 PriorityIndex)
    {
        return FName(*FString::Printf(TEXT("DWCTransparencyInner_%d"), PriorityIndex));
    }

    FString MakeSameMeshBuildSignature(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        const USkeletalMesh* RuntimeMesh,
        const int32 Resolution,
        const int32 DataUVChannelIndex,
        const int32 LODIndex)
    {
        const FDWCDataUVLODMetadata* DataUVMetadata =
            WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
        FString SignatureSource = FString::Printf(
            TEXT("DWCTransparencyAutoMap_v5|Mesh=%s|Layer=%s|Slot=%d|UV=%d|LOD=%d|Resolution=%d|Address=%d|RayStart=%.9g|Min=%.9g|Full=%.9g|None=%.9g|Max=%.9g|DataUV=%s"),
            *GetPathNameSafe(RuntimeMesh),
            *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
            Layer.TargetSurface.OuterMaterialSlotIndex,
            DataUVChannelIndex,
            LODIndex,
            Resolution,
            static_cast<int32>(Layer.TargetSurface.UVAddressMode),
            Layer.RaySettings.RayStartOffset,
            Layer.RaySettings.MinHitDistance,
            Layer.RaySettings.FullTransparencyDistance,
            Layer.RaySettings.NoTransparencyDistance,
            Layer.RaySettings.MaxRayDistance,
            DataUVMetadata != nullptr ? *DataUVMetadata->DataUVOutputSignature : TEXT("Missing"));
        for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
        {
            const FWetClothingTransparencyInnerSlot& InnerSlot =
                Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
            SignatureSource += FString::Printf(
                TEXT("|Inner=%d,%d,%d,%s"),
                PriorityIndex,
                InnerSlot.MaterialSlotIndex,
                InnerSlot.SourceUVChannel,
                *InnerSlot.MaterialSlotName.ToString());
            if (RuntimeMesh != nullptr && RuntimeMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex))
            {
                SignatureSource += FString::Printf(
                    TEXT(",%s"),
                    *GetPathNameSafe(RuntimeMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface));
            }
        }
        return FMD5::HashAnsiString(*SignatureSource);
    }

    FString MakeBaseRevealColorBuildSignature(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        const USkeletalMesh* RuntimeMesh,
        const int32 Resolution,
        const int32 DataUVChannelIndex,
        const int32 LODIndex)
    {
        const FDWCDataUVLODMetadata* DataUVMetadata =
            WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
        const FLinearColor& Color = Layer.ManualColorSource.BaseRevealColor;
        FString SignatureSource = FString::Printf(
            TEXT("DWCTransparencyBaseRevealColor_v5|Mesh=%s|Layer=%s|Slot=%d|UV=%d|LOD=%d|Resolution=%d|Address=%d|Color=%.9g,%.9g,%.9g|Alpha=%.9g|DataUV=%s"),
            *GetPathNameSafe(RuntimeMesh),
            *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
            Layer.TargetSurface.OuterMaterialSlotIndex,
            DataUVChannelIndex,
            LODIndex,
            Resolution,
            static_cast<int32>(Layer.TargetSurface.UVAddressMode),
            Color.R,
            Color.G,
            Color.B,
            Layer.ManualColorSource.InitialTransparencyAlpha,
            DataUVMetadata != nullptr ? *DataUVMetadata->DataUVOutputSignature : TEXT("Missing"));
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer.RevealColorPaintStrokes)
        {
            SignatureSource += FString::Printf(
                TEXT("|Paint=%s,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits),
                Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex,
                static_cast<int32>(Stroke.BrushMode),
                Stroke.PaintColor.R,
                Stroke.PaintColor.G,
                Stroke.PaintColor.B,
                Stroke.Falloff,
                Stroke.Spacing,
                Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                SignatureSource += FString::Printf(
                    TEXT(",%.9g,%.9g,%.9g,%.9g,%d"),
                    Sample.PositionUV.X,
                    Sample.PositionUV.Y,
                    Sample.RadiusUV,
                    Sample.Strength,
                    Sample.UVIslandID);
            }
        }
        return FMD5::HashAnsiString(*SignatureSource);
    }

    class FSameMeshSurfaceCache
    {
      public:
        FSameMeshSurfaceCache(USkeletalMesh* InMesh, const int32 InLODIndex)
            : Mesh(InMesh)
            , LODIndex(InLODIndex)
        {
        }

        bool BuildSlotSurface(
            const FDWCBakeResolvedLayer& Layer,
            const int32 UVChannelIndex,
            const int32 MaterialSlotIndex,
            FDWCRevealBakeSurface& OutSurface,
            FString& OutErrorMessage)
        {
            const FDWCRevealBakeSurface* BaseSurface =
                FindOrBuildBaseSurface(UVChannelIndex, OutErrorMessage);
            if (BaseSurface == nullptr)
            {
                return false;
            }

            const TArray<int32>& SlotTriangleIndices =
                FindOrBuildSlotTriangleIndices(
                    UVChannelIndex,
                    MaterialSlotIndex,
                    *BaseSurface);
            if (SlotTriangleIndices.IsEmpty())
            {
                OutErrorMessage = FString::Printf(
                    TEXT("Material slot %d has no triangles at LOD %d."),
                    MaterialSlotIndex,
                    LODIndex);
                return false;
            }

            OutSurface.Reset();
            OutSurface.LayerId = Layer.LayerId;
            OutSurface.LayerOrder = Layer.LayerOrder;
            OutSurface.LODIndex = LODIndex;
            OutSurface.UVChannelIndex = UVChannelIndex;
            OutSurface.SkeletalMesh = Mesh;
            OutSurface.bCanBeRevealSource = Layer.bCanBeRevealSource;
            OutSurface.bCanBeWetOuterLayer = Layer.bCanBeWetOuterLayer;
            OutSurface.bBlocksReveal = Layer.bBlocksReveal;
            OutSurface.MaxRevealDistance = Layer.MaxRevealDistance;
            OutSurface.Triangles.Reserve(SlotTriangleIndices.Num());
            for (const int32 TriangleIndex : SlotTriangleIndices)
            {
                const FDWCRevealBakeSurfaceTriangle& Triangle =
                    BaseSurface->Triangles[TriangleIndex];
                OutSurface.Bounds += Triangle.Bounds;
                OutSurface.Triangles.Add(Triangle);
            }
            OutErrorMessage.Reset();
            return true;
        }

      private:
        const FDWCRevealBakeSurface* FindOrBuildBaseSurface(
            const int32 UVChannelIndex,
            FString& OutErrorMessage)
        {
            if (const FDWCRevealBakeSurface* CachedSurface =
                    BaseSurfacesByUVChannel.Find(UVChannelIndex))
            {
                return CachedSurface;
            }

            const FDWCBakeResolvedLayer GeometryLayer = MakeResolvedLayer(
                Mesh,
                FName(TEXT("DWCTransparencyGeometryCache")),
                0,
                0.0f);
            FDWCRevealBakeSurface BuiltSurface;
            if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
                    GeometryLayer,
                    LODIndex,
                    UVChannelIndex,
                    BuiltSurface,
                    &OutErrorMessage))
            {
                return nullptr;
            }
            return &BaseSurfacesByUVChannel.Add(UVChannelIndex, MoveTemp(BuiltSurface));
        }

        const TArray<int32>& FindOrBuildSlotTriangleIndices(
            const int32 UVChannelIndex,
            const int32 MaterialSlotIndex,
            const FDWCRevealBakeSurface& BaseSurface)
        {
            const FIntPoint Key(UVChannelIndex, MaterialSlotIndex);
            if (const TArray<int32>* CachedIndices =
                    SlotTriangleIndicesByUVAndSlot.Find(Key))
            {
                return *CachedIndices;
            }

            TArray<int32> TriangleIndices;
            for (int32 TriangleIndex = 0;
                 TriangleIndex < BaseSurface.Triangles.Num();
                 ++TriangleIndex)
            {
                if (BaseSurface.Triangles[TriangleIndex].MaterialSlotIndex ==
                    MaterialSlotIndex)
                {
                    TriangleIndices.Add(TriangleIndex);
                }
            }
            return SlotTriangleIndicesByUVAndSlot.Add(Key, MoveTemp(TriangleIndices));
        }

        USkeletalMesh* Mesh = nullptr;
        int32 LODIndex = 0;
        TMap<int32, FDWCRevealBakeSurface> BaseSurfacesByUVChannel;
        TMap<FIntPoint, TArray<int32>> SlotTriangleIndicesByUVAndSlot;
    };

    float ApplyTextureAddress(const float Coordinate, const TextureAddress AddressMode)
    {
        switch (AddressMode)
        {
        case TA_Wrap:
            return FMath::Frac(Coordinate);
        case TA_Mirror:
        {
            const float Wrapped = FMath::Frac(Coordinate * 0.5f) * 2.0f;
            return Wrapped <= 1.0f ? Wrapped : 2.0f - Wrapped;
        }
        case TA_Clamp:
        default:
            return FMath::Clamp(Coordinate, 0.0f, 1.0f);
        }
    }

    FLinearColor SampleTextureBilinear(const FWetClothingTextureReadback& TextureData, const FVector2D& UV)
    {
        if (!TextureData.IsValid())
        {
            return FLinearColor::White;
        }

        const float U = ApplyTextureAddress(static_cast<float>(UV.X), TextureData.AddressX);
        const float V = ApplyTextureAddress(static_cast<float>(UV.Y), TextureData.AddressY);
        const float X = U * static_cast<float>(TextureData.Width - 1);
        const float Y = V * static_cast<float>(TextureData.Height - 1);
        const int32 X0 = FMath::FloorToInt(X);
        const int32 Y0 = FMath::FloorToInt(Y);
        const int32 X1 = FMath::Min(X0 + 1, TextureData.Width - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, TextureData.Height - 1);
        const float AlphaX = X - static_cast<float>(X0);
        const float AlphaY = Y - static_cast<float>(Y0);
        const FLinearColor C0 = FMath::Lerp(TextureData.GetLinearColor(X0, Y0), TextureData.GetLinearColor(X1, Y0), AlphaX);
        const FLinearColor C1 = FMath::Lerp(TextureData.GetLinearColor(X0, Y1), TextureData.GetLinearColor(X1, Y1), AlphaX);
        return FMath::Lerp(C0, C1, AlphaY);
    }

    float CalculateAutoAlpha(const FWetClothingTransparencyRaySettings& Settings, const FDWCRevealBakeRayHit& Hit)
    {
        const float FullDistance = FMath::Max(Settings.FullTransparencyDistance, 0.0f);
        const float NoTransparencyDistance = FMath::Max(Settings.NoTransparencyDistance, FullDistance + UE_SMALL_NUMBER);
        const float DistanceAlpha = 1.0f - FMath::Clamp(
            (Hit.Distance - FullDistance) / (NoTransparencyDistance - FullDistance),
            0.0f,
            1.0f);
        return FMath::Clamp(DistanceAlpha * Hit.Confidence, 0.0f, 1.0f);
    }

    int32 ResolveOuterSampleUVIslandID(
        const FDWCDataUVLODMetadata* DataUVMetadata,
        const FDWCRevealBakeTexelSample& Sample)
    {
        if (Sample.UVIslandID != INDEX_NONE)
        {
            return Sample.UVIslandID;
        }
        if (DataUVMetadata != nullptr &&
            DataUVMetadata->DataUVIslandIDByTriangleID.IsValidIndex(Sample.TriangleIndex))
        {
            return DataUVMetadata->DataUVIslandIDByTriangleID[Sample.TriangleIndex];
        }
        return INDEX_NONE;
    }

    bool TryEncodeOuterSampleUVIslandID(
        const FDWCDataUVLODMetadata* DataUVMetadata,
        const FDWCRevealBakeTexelSample& Sample,
        uint16& OutEncodedIslandID,
        FString& OutErrorMessage)
    {
        const int32 IslandID = ResolveOuterSampleUVIslandID(DataUVMetadata, Sample);
        if (IslandID != INDEX_NONE && !FDWCTransparencyAutoBakeResult::CanEncodeOuterIslandID(IslandID))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Transparency target UV island ID %d exceeds the supported range [0, %u]."),
                IslandID,
                static_cast<uint32>(FDWCTransparencyAutoBakeResult::InvalidOuterIslandID) - 1u);
            return false;
        }
        OutEncodedIslandID = FDWCTransparencyAutoBakeResult::EncodeOuterIslandID(IslandID);
        return true;
    }

    bool PassesTransparencyIslandClip(
        const FDWCTransparencyAutoBakeResult& Result,
        const int32 PixelIndex,
        const int32 UVIslandID)
    {
        if (UVIslandID == INDEX_NONE)
        {
            return true;
        }
        return Result.OuterIslandIDBuffer.IsValidIndex(PixelIndex) &&
            FDWCTransparencyAutoBakeResult::MatchesOuterIslandID(
                Result.OuterIslandIDBuffer[PixelIndex],
                UVIslandID);
    }

    int32 ResolveTransparencySampleIslandID(
        const FDWCTransparencyAutoBakeResult& Result,
        const FVector2D& PositionUV,
        const int32 UVIslandID,
        const int32 Width,
        const int32 Height,
        const bool bWrap)
    {
        if (UVIslandID != INDEX_NONE)
        {
            return UVIslandID;
        }
        if (Result.OuterIslandIDBuffer.Num() != Width * Height)
        {
            return INDEX_NONE;
        }

        int32 X = FMath::FloorToInt(PositionUV.X * Width);
        int32 Y = FMath::FloorToInt(PositionUV.Y * Height);
        if (bWrap)
        {
            X = (X % Width + Width) % Width;
            Y = (Y % Height + Height) % Height;
        }
        else if (X < 0 || X >= Width || Y < 0 || Y >= Height)
        {
            return INDEX_NONE;
        }
        else
        {
            X = FMath::Clamp(X, 0, Width - 1);
            Y = FMath::Clamp(Y, 0, Height - 1);
        }
        return FDWCTransparencyAutoBakeResult::DecodeOuterIslandID(
            Result.OuterIslandIDBuffer[Y * Width + X]);
    }
}

bool FDWCTransparencyAutoMapGenerator::BuildTargetSurfaceBuffers(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyTargetSurface& TargetSurface,
    const int32 LODIndex,
    const FIntPoint Resolution,
    TArray<uint8>& OutCoverageBuffer,
    TArray<uint16>& OutIslandIDBuffer,
    int32* OutOuterSampleCount,
    int32* OutOverlappedPixelCount,
    FString& OutErrorMessage)
{
    OutCoverageBuffer.Reset();
    OutIslandIDBuffer.Reset();
    if (OutOuterSampleCount != nullptr)
    {
        *OutOuterSampleCount = 0;
    }
    if (OutOverlappedPixelCount != nullptr)
    {
        *OutOverlappedPixelCount = 0;
    }
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Manual transparency generation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }
    if (Resolution.X <= 0 || Resolution.Y <= 0)
    {
        OutErrorMessage = TEXT("Transparency target surface buffers require a valid resolution.");
        return false;
    }

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyTargetSurface")) ,
        MAX_int32 / 2,
        0.0f);
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex);
    FDWCRevealBakeSurface OuterSurface;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            TargetSurface.OuterMaterialSlotIndex,
            OuterSurface,
            OutErrorMessage))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *OutErrorMessage);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = Resolution;
    SamplingSettings.MaterialSlotIndex = TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            OuterSurface,
            SamplingSettings,
            OuterSamples,
            &OutErrorMessage,
            &OverlappedPixelCount))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *OutErrorMessage);
        return false;
    }
    if (OuterSamples.IsEmpty())
    {
        OutErrorMessage = TEXT("No target texel samples were generated. Check the selected target slot and DWC UV Channel.");
        return false;
    }

    const int32 PixelCount = Resolution.X * Resolution.Y;
    OutCoverageBuffer.Init(0, PixelCount);
    OutIslandIDBuffer.Init(FDWCTransparencyAutoBakeResult::InvalidOuterIslandID, PixelCount);
    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    for (const FDWCRevealBakeTexelSample& Sample : OuterSamples)
    {
        if (Sample.Pixel.X < 0 || Sample.Pixel.Y < 0 ||
            Sample.Pixel.X >= Resolution.X || Sample.Pixel.Y >= Resolution.Y)
        {
            continue;
        }

        const int32 PixelIndex = Sample.Pixel.Y * Resolution.X + Sample.Pixel.X;
        OutCoverageBuffer[PixelIndex] = 1;
        uint16 EncodedIslandID = FDWCTransparencyAutoBakeResult::InvalidOuterIslandID;
        if (!TryEncodeOuterSampleUVIslandID(
                DataUVMetadata,
                Sample,
                EncodedIslandID,
                OutErrorMessage))
        {
            OutCoverageBuffer.Reset();
            OutIslandIDBuffer.Reset();
            return false;
        }
        OutIslandIDBuffer[PixelIndex] = EncodedIslandID;
    }

    if (OutOuterSampleCount != nullptr)
    {
        *OutOuterSampleCount = OuterSamples.Num();
    }
    if (OutOverlappedPixelCount != nullptr)
    {
        *OutOverlappedPixelCount = OverlappedPixelCount;
    }
    return true;
}

void FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
    const FDWCTransparencyAutoBakeResult& AutoResult,
    const TArray<FDWCTransparencyRevealColorStroke>& Strokes,
    const int32 MaterialSlotIndex,
    const FLinearColor& BaseRevealColor,
    TArray<FColor>& InOutRevealColorBuffer)
{
    const int32 Width = AutoResult.Resolution.X;
    const int32 Height = AutoResult.Resolution.Y;
    if (Width <= 0 || Height <= 0 || InOutRevealColorBuffer.Num() != Width * Height)
    {
        return;
    }

    const auto WrapIndex = [](int32 Value, int32 Size) { return (Value % Size + Size) % Size; };
    for (const FDWCTransparencyRevealColorStroke& Stroke : Strokes)
    {
        if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        const bool bWrap = Stroke.UVAddressMode == EDWCTransparencyUVAddressMode::Wrap;
    const FLinearColor BaseColor = BaseRevealColor.CopyWithNewOpacity(1.0f);
        const FLinearColor PaintColor = Stroke.PaintColor.CopyWithNewOpacity(1.0f);
        for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
        {
            const float RadiusX = FMath::Max(Sample.RadiusUV * Width, 1.0f);
            const float RadiusY = FMath::Max(Sample.RadiusUV * Height, 1.0f);
            const FVector2D Center(Sample.PositionUV.X * Width, Sample.PositionUV.Y * Height);
            const int32 MinX = FMath::FloorToInt(Center.X - RadiusX - 1.0f);
            const int32 MaxX = FMath::CeilToInt(Center.X + RadiusX + 1.0f);
            const int32 MinY = FMath::FloorToInt(Center.Y - RadiusY - 1.0f);
            const int32 MaxY = FMath::CeilToInt(Center.Y + RadiusY + 1.0f);
            const int32 ClipUVIslandID = ResolveTransparencySampleIslandID(
                AutoResult,
                Sample.PositionUV,
                Sample.UVIslandID,
                Width,
                Height,
                bWrap);
            const float InnerRadius = 1.0f - FMath::Clamp(Stroke.Falloff, 0.0f, 1.0f);
            const bool bSmooth = Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth;
            const int32 SnapshotMinX = MinX - 1;
            const int32 SnapshotMinY = MinY - 1;
            const int32 SnapshotWidth = MaxX - MinX + 3;
            const int32 SnapshotHeight = MaxY - MinY + 3;
            TArray<FColor> SmoothSnapshot;
            if (bSmooth)
            {
                SmoothSnapshot.SetNumUninitialized(SnapshotWidth * SnapshotHeight);
                for (int32 SnapshotY = 0; SnapshotY < SnapshotHeight; ++SnapshotY)
                {
                    for (int32 SnapshotX = 0; SnapshotX < SnapshotWidth; ++SnapshotX)
                    {
                        int32 SourceX = SnapshotMinX + SnapshotX;
                        int32 SourceY = SnapshotMinY + SnapshotY;
                        if (bWrap)
                        {
                            SourceX = WrapIndex(SourceX, Width);
                            SourceY = WrapIndex(SourceY, Height);
                        }
                        else
                        {
                            SourceX = FMath::Clamp(SourceX, 0, Width - 1);
                            SourceY = FMath::Clamp(SourceY, 0, Height - 1);
                        }
                        SmoothSnapshot[SnapshotY * SnapshotWidth + SnapshotX] =
                            InOutRevealColorBuffer[SourceY * Width + SourceX];
                    }
                }
            }
            auto ReadColorAt = [Width, Height, bWrap, SnapshotMinX, SnapshotMinY, SnapshotWidth,
                                &WrapIndex, &SmoothSnapshot, &AutoResult, &InOutRevealColorBuffer](int32 RawX, int32 RawY)
            {
                const int32 SnapshotIndex = (RawY - SnapshotMinY) * SnapshotWidth + (RawX - SnapshotMinX);
                if (SmoothSnapshot.IsValidIndex(SnapshotIndex))
                {
                    return FLinearColor(SmoothSnapshot[SnapshotIndex]);
                }
                if (bWrap)
                {
                    RawX = WrapIndex(RawX, Width);
                    RawY = WrapIndex(RawY, Height);
                }
                else
                {
                    RawX = FMath::Clamp(RawX, 0, Width - 1);
                    RawY = FMath::Clamp(RawY, 0, Height - 1);
                }
                return FLinearColor(InOutRevealColorBuffer[RawY * Width + RawX]);
            };
            for (int32 RawY = MinY; RawY <= MaxY; ++RawY)
            {
                for (int32 RawX = MinX; RawX <= MaxX; ++RawX)
                {
                    if (!bWrap && (RawX < 0 || RawX >= Width || RawY < 0 || RawY >= Height)) continue;
                    const float DX = (RawX + 0.5f - Center.X) / RadiusX;
                    const float DY = (RawY + 0.5f - Center.Y) / RadiusY;
                    const float Distance = FMath::Sqrt(DX * DX + DY * DY);
                    if (Distance > 1.0f) continue;
                    const float RadialWeight = Distance <= InnerRadius || Stroke.Falloff <= KINDA_SMALL_NUMBER
                        ? 1.0f : 1.0f - FMath::SmoothStep(InnerRadius, 1.0f, Distance);
                    const float Weight = FMath::Clamp(RadialWeight * Sample.Strength, 0.0f, 1.0f);
                    if (Weight <= 0.0f) continue;
                    const int32 X = bWrap ? WrapIndex(RawX, Width) : RawX;
                    const int32 Y = bWrap ? WrapIndex(RawY, Height) : RawY;
                    const int32 PixelIndex = Y * Width + X;
                    if (!AutoResult.OuterCoverageBuffer.IsValidIndex(PixelIndex) || AutoResult.OuterCoverageBuffer[PixelIndex] == 0) continue;
                    if (!PassesTransparencyIslandClip(AutoResult, PixelIndex, ClipUVIslandID)) continue;
                    FLinearColor TargetColor = PaintColor;
                    if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::EraseToBase)
                    {
                        TargetColor = BaseColor;
                    }
                    else if (Stroke.BrushMode == EDWCTransparencyRevealColorBrushMode::Smooth)
                    {
                        TargetColor = FLinearColor::Black;
                        for (int32 OffsetY = -1; OffsetY <= 1; ++OffsetY)
                        {
                            for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)
                            {
                                int32 NeighborX = RawX + OffsetX;
                                int32 NeighborY = RawY + OffsetY;
                                if (bWrap)
                                {
                                    NeighborX = WrapIndex(NeighborX, Width);
                                    NeighborY = WrapIndex(NeighborY, Height);
                                }
                                else
                                {
                                    NeighborX = FMath::Clamp(NeighborX, 0, Width - 1);
                                    NeighborY = FMath::Clamp(NeighborY, 0, Height - 1);
                                }
                                const int32 NeighborIndex = NeighborY * Width + NeighborX;
                                if (PassesTransparencyIslandClip(AutoResult, NeighborIndex, ClipUVIslandID))
                                {
                                    TargetColor += ReadColorAt(RawX + OffsetX, RawY + OffsetY);
                                }
                                else
                                {
                                    TargetColor += FLinearColor(InOutRevealColorBuffer[PixelIndex]);
                                }
                            }
                        }
                        TargetColor /= 9.0f;
                        TargetColor.A = 1.0f;
                    }
                    InOutRevealColorBuffer[PixelIndex] = FMath::Lerp(
                        FLinearColor(InOutRevealColorBuffer[PixelIndex]),
                        TargetColor.CopyWithNewOpacity(1.0f),
                        Weight).ToFColor(true);
                }
            }
        }
    }
}
bool FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoBakeResult& OutResult,
    FString& OutSummary,
    TArray<FString>& OutWarnings)
{
    OutResult = FDWCTransparencyAutoBakeResult();
    OutSummary.Reset();
    OutWarnings.Reset();

    if (Layer.SourceType != EDWCTransparencySourceType::ManualColorOrTexture)
    {
        OutSummary = TEXT("The selected Transparency Target Part is not configured for the manual base-color workflow.");
        return false;
    }

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutSummary = TEXT("Manual transparency generation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutSummary = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyManualOuter")),
        MAX_int32 / 2,
        0.0f);
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex);
    FDWCRevealBakeSurface OuterSurface;
    FString BuildError;
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            OuterSurface,
            BuildError))
    {
        OutSummary = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *BuildError);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            OuterSurface,
            SamplingSettings,
            OuterSamples,
            &BuildError,
            &OverlappedPixelCount))
    {
        OutSummary = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *BuildError);
        return false;
    }
    if (OuterSamples.IsEmpty())
    {
        OutSummary = TEXT("No target texel samples were generated. Check the selected target slot and DWC UV Channel.");
        return false;
    }

    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = DataUVChannelIndex;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = BakeResolution;
    OutResult.OuterSampleCount = OuterSamples.Num();
    OutResult.OverlappedUVPixelCount = OverlappedPixelCount;
    OutResult.InnerColorBuffer.Init(FColor::Black, PixelCount);
    OutResult.AutoAlphaBuffer.Init(0, PixelCount);
    OutResult.OuterCoverageBuffer.Init(0, PixelCount);
    OutResult.ValidHitBuffer.Init(false, PixelCount);
    OutResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    OutResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    OutResult.OuterIslandIDBuffer.Init(FDWCTransparencyAutoBakeResult::InvalidOuterIslandID, PixelCount);

    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    const FColor BaseRevealColor = Layer.ManualColorSource.BaseRevealColor
        .CopyWithNewOpacity(1.0f)
        .ToFColor(true);
    const uint8 InitialAlpha = static_cast<uint8>(FMath::RoundToInt(
        FMath::Clamp(Layer.ManualColorSource.InitialTransparencyAlpha, 0.0f, 1.0f) * 255.0f));
    for (const FDWCRevealBakeTexelSample& Sample : OuterSamples)
    {
        if (Sample.Pixel.X < 0 || Sample.Pixel.Y < 0 ||
            Sample.Pixel.X >= Resolution || Sample.Pixel.Y >= Resolution)
        {
            continue;
        }

        const int32 PixelIndex = Sample.Pixel.Y * Resolution + Sample.Pixel.X;
        OutResult.OuterCoverageBuffer[PixelIndex] = 1;
        uint16 EncodedIslandID = FDWCTransparencyAutoBakeResult::InvalidOuterIslandID;
        FString IslandError;
        if (!TryEncodeOuterSampleUVIslandID(
                DataUVMetadata,
                Sample,
                EncodedIslandID,
                IslandError))
        {
            OutSummary = MoveTemp(IslandError);
            return false;
        }
        OutResult.OuterIslandIDBuffer[PixelIndex] = EncodedIslandID;
        OutResult.ValidHitBuffer[PixelIndex] = true;
        OutResult.InnerColorBuffer[PixelIndex] = BaseRevealColor;
        OutResult.AutoAlphaBuffer[PixelIndex] = InitialAlpha;
        ++OutResult.ValidHitCount;
    }

    OutResult.BuildSignature = MakeBaseRevealColorBuildSignature(
        WetClothingAsset,
        Layer,
        RuntimeMesh,
        Resolution,
        DataUVChannelIndex,
        LODIndex);

    if (OverlappedPixelCount > 0)
    {
        OutWarnings.Add(FString::Printf(
            TEXT("The target DWC UV Channel contains %d genuinely overlapping rasterized pixel(s). Rebuild and inspect the DWC UV Channel before editing transparency."),
            OverlappedPixelCount));
    }

    OutSummary = FString::Printf(
        TEXT("Generated %d target texels with Base Reveal Color and Initial Transparency Alpha. No ray projection was used."),
        OutResult.OuterSampleCount);
    return true;
}

bool FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoBakeResult& OutResult,
    FString& OutSummary,
    TArray<FString>& OutWarnings)
{
    OutResult = FDWCTransparencyAutoBakeResult();
    OutSummary.Reset();
    OutWarnings.Reset();

    FDWCTransparencyAutoMapSnapshot Snapshot;
    FString SnapshotError;
    if (!BuildSameMeshSnapshot(WetClothingAsset, Layer, Snapshot, SnapshotError))
    {
        OutSummary = MoveTemp(SnapshotError);
        return false;
    }

    FDWCTransparencyAutoMapComputedResult Computed = ComputeSameMeshSnapshot(Snapshot);
    OutWarnings = MoveTemp(Computed.Warnings);
    if (!Computed.bSucceeded)
    {
        OutSummary = Computed.Error.IsEmpty()
            ? TEXT("Transparency ray projection failed.")
            : MoveTemp(Computed.Error);
        return false;
    }

    OutResult = MoveTemp(Computed.AutoResult);
    OutSummary = MoveTemp(Computed.Summary);
    return true;
}

bool FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoBakeResult& OutResult,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutResult = FDWCTransparencyAutoBakeResult();
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Transparency validation requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (!RuntimeMesh->GetMaterials().IsValidIndex(Layer.TargetSurface.OuterMaterialSlotIndex))
    {
        OutErrorMessage = TEXT("The Transparency Target Part no longer references a valid material slot.");
        return false;
    }
    if (Layer.SourceType != EDWCTransparencySourceType::ManualColorOrTexture &&
        Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutErrorMessage = TEXT("Add at least one Inner Source Part before validating the transparency map.");
        return false;
    }

    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    if (DataUVChannelIndex == INDEX_NONE || DataUVMetadata == nullptr ||
        DataUVMetadata->DataUVOutputSignature.IsEmpty())
    {
        OutErrorMessage = TEXT("Transparency validation requires valid DWC Data UV metadata for LOD 0.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    OutResult.LayerGuid = Layer.LayerGuid;
    OutResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    OutResult.UVChannelIndex = DataUVChannelIndex;
    OutResult.LODIndex = LODIndex;
    OutResult.Resolution = FIntPoint(Resolution, Resolution);
    OutResult.BuildSignature = Layer.SourceType == EDWCTransparencySourceType::ManualColorOrTexture
        ? MakeBaseRevealColorBuildSignature(
            WetClothingAsset,
            Layer,
            RuntimeMesh,
            Resolution,
            DataUVChannelIndex,
            LODIndex)
        : MakeSameMeshBuildSignature(
            WetClothingAsset,
            Layer,
            RuntimeMesh,
            Resolution,
            DataUVChannelIndex,
            LODIndex);
    return true;
}

struct FDWCTransparencyAutoMapSnapshot::FImpl
{
    FDWCRevealBakeSurface OuterSurface;
    TArray<FDWCRevealBakeSurface> SourceSurfaces;
    TArray<FDWCRevealBakeTexelSample> OuterSamples;
    FDWCRevealBakeRayProjectionSettings ProjectionSettings;
    FWetClothingTransparencyRaySettings RaySettings;
    TMap<FName, int32> PriorityBySourceLayerId;
    TMap<FName, int32> StatsIndexBySourceLayerId;
    TMap<FName, FWetClothingTextureReadback> SourceTextureDataByLayerId;
    FDWCTransparencyAutoBakeResult SeedResult;
    TArray<FString> Warnings;
    uint64 EstimatedBytes = 0;
    bool bValid = false;
};

FDWCTransparencyAutoMapSnapshot::FDWCTransparencyAutoMapSnapshot()
    : Impl(MakeUnique<FImpl>())
{
}

FDWCTransparencyAutoMapSnapshot::~FDWCTransparencyAutoMapSnapshot() = default;
FDWCTransparencyAutoMapSnapshot::FDWCTransparencyAutoMapSnapshot(FDWCTransparencyAutoMapSnapshot&&) = default;
FDWCTransparencyAutoMapSnapshot& FDWCTransparencyAutoMapSnapshot::operator=(FDWCTransparencyAutoMapSnapshot&&) = default;

bool FDWCTransparencyAutoMapSnapshot::IsValid() const
{
    return Impl.IsValid() && Impl->bValid;
}

int32 FDWCTransparencyAutoMapSnapshot::GetMaterialSlotIndex() const
{
    return Impl.IsValid() ? Impl->SeedResult.MaterialSlotIndex : INDEX_NONE;
}

FGuid FDWCTransparencyAutoMapSnapshot::GetLayerGuid() const
{
    return Impl.IsValid() ? Impl->SeedResult.LayerGuid : FGuid();
}

uint64 FDWCTransparencyAutoMapSnapshot::GetEstimatedBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedBytes : 0;
}

bool FDWCTransparencyAutoMapGenerator::BuildSameMeshSnapshot(
    const UWetClothingAsset& WetClothingAsset,
    const FWetClothingTransparencyLayerData& Layer,
    FDWCTransparencyAutoMapSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutSnapshot = FDWCTransparencyAutoMapSnapshot();
    FDWCTransparencyAutoMapSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    OutErrorMessage.Reset();

    USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        OutErrorMessage = TEXT("Transparency auto-map requires a DWC runtime skeletal mesh.");
        return false;
    }
    if (Layer.TargetSurface.OuterMaterialSlotIndex == INDEX_NONE)
    {
        OutErrorMessage = TEXT("Select a Transparency Target Part before generating the transparency map.");
        return false;
    }
    if (Layer.SameMeshSource.InnerSlotPriority.IsEmpty())
    {
        OutErrorMessage = TEXT("Add at least one Inner Source Part before generating the transparency map.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        WetClothingAsset.Authored.TransparencyData.TransparencyBakeResolution,
        16,
        4096);
    constexpr int32 LODIndex = 0;
    const int32 DataUVChannelIndex = WetClothingAsset.GetDWCDataUVChannelIndex();
    const FIntPoint BakeResolution(Resolution, Resolution);
    const int32 PixelCount = Resolution * Resolution;

    const FDWCBakeResolvedLayer OuterLayer = MakeResolvedLayer(
        RuntimeMesh,
        FName(TEXT("DWCTransparencyOuter")),
        MAX_int32 / 2,
        Layer.RaySettings.MaxRayDistance);
    FSameMeshSurfaceCache SurfaceCache(RuntimeMesh, LODIndex);
    FString BuildError;
    if (!SurfaceCache.BuildSlotSurface(
            OuterLayer,
            DataUVChannelIndex,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            Snapshot.OuterSurface,
            BuildError))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to build the transparency target surface: %s"),
            *BuildError);
        return false;
    }

    FDWCRevealBakeTexelSamplingSettings SamplingSettings;
    SamplingSettings.Resolution = BakeResolution;
    SamplingSettings.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    int32 OverlappedPixelCount = 0;
    if (!FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
            Snapshot.OuterSurface,
            SamplingSettings,
            Snapshot.OuterSamples,
            &BuildError,
            &OverlappedPixelCount))
    {
        OutErrorMessage = FString::Printf(
            TEXT("Failed to rasterize the transparency target UVs: %s"),
            *BuildError);
        return false;
    }
    if (Snapshot.OuterSamples.IsEmpty())
    {
        OutErrorMessage = TEXT("No outer texel samples were generated. Check the selected target slot and DWC Data UV channel.");
        return false;
    }

    Snapshot.SourceSurfaces.Reserve(Layer.SameMeshSource.InnerSlotPriority.Num());
    for (int32 PriorityIndex = 0; PriorityIndex < Layer.SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
    {
        const FWetClothingTransparencyInnerSlot& InnerSlot =
            Layer.SameMeshSource.InnerSlotPriority[PriorityIndex];
        if (InnerSlot.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }
        if (!RuntimeMesh->GetMaterials().IsValidIndex(InnerSlot.MaterialSlotIndex))
        {
            Snapshot.Warnings.Add(FString::Printf(
                TEXT("Inner Source Part priority %d references an unavailable material slot."),
                PriorityIndex));
            continue;
        }

        const FName SourceLayerId = MakeInnerSourceLayerId(PriorityIndex);
        const FDWCBakeResolvedLayer SourceLayer = MakeResolvedLayer(
            RuntimeMesh,
            SourceLayerId,
            PriorityIndex,
            Layer.RaySettings.MaxRayDistance);
        FDWCRevealBakeSurface SourceSurface;
        if (!SurfaceCache.BuildSlotSurface(
                SourceLayer,
                InnerSlot.SourceUVChannel,
                InnerSlot.MaterialSlotIndex,
                SourceSurface,
                BuildError))
        {
            Snapshot.Warnings.Add(FString::Printf(
                TEXT("Inner Source Part '%s' was skipped: %s"),
                *InnerSlot.MaterialSlotName.ToString(),
                *BuildError));
            continue;
        }

        FDWCTransparencySourceHitStats& Stats = Snapshot.SeedResult.SourceStats.AddDefaulted_GetRef();
        Stats.PriorityIndex = PriorityIndex;
        Stats.MaterialSlotIndex = InnerSlot.MaterialSlotIndex;
        Stats.MaterialSlotName = InnerSlot.MaterialSlotName;
        Snapshot.StatsIndexBySourceLayerId.Add(SourceLayerId, Snapshot.SeedResult.SourceStats.Num() - 1);
        Snapshot.PriorityBySourceLayerId.Add(SourceLayerId, PriorityIndex);

        if (UMaterialInterface* SourceMaterial =
                RuntimeMesh->GetMaterials()[InnerSlot.MaterialSlotIndex].MaterialInterface)
        {
            if (UTexture2D* SourceTexture =
                    FDWCRevealBakeSourceResolver::ResolveRevealSourceBaseColorTexture(SourceMaterial))
            {
                FWetClothingTextureReadback TextureData;
                FString TextureError;
                if (FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                        SourceTexture,
                        TextureData,
                        TextureError))
                {
                    Snapshot.SourceTextureDataByLayerId.Add(SourceLayerId, MoveTemp(TextureData));
                }
                else
                {
                    Snapshot.Warnings.Add(FString::Printf(
                        TEXT("Inner Source Part '%s' has no readable base-color source texture: %s"),
                        *InnerSlot.MaterialSlotName.ToString(),
                        *TextureError));
                }
            }
            else
            {
                Snapshot.Warnings.Add(FString::Printf(
                    TEXT("Inner Source Part '%s' has no resolvable base-color texture; white will be used for its valid hits."),
                    *InnerSlot.MaterialSlotName.ToString()));
            }
        }
        SourceSurface.SkeletalMesh = nullptr;
        Snapshot.SourceSurfaces.Add(MoveTemp(SourceSurface));
    }
    if (Snapshot.SourceSurfaces.IsEmpty())
    {
        OutErrorMessage = TEXT("No eligible Inner Source Part surface could be built for transparency ray projection.");
        return false;
    }

    Snapshot.OuterSurface.SkeletalMesh = nullptr;
    Snapshot.ProjectionSettings.RayStartOffset = Layer.RaySettings.RayStartOffset;
    Snapshot.ProjectionSettings.RayLengthScale = 1.0f;
    Snapshot.ProjectionSettings.MinHitDistance = Layer.RaySettings.MinHitDistance;
    Snapshot.ProjectionSettings.bRespectSourceLayerOrder = true;
    Snapshot.ProjectionSettings.bRespectBlockers = false;
    Snapshot.ProjectionSettings.bPreferLowerSourceLayerOrder = true;
    Snapshot.ProjectionSettings.bRespectPerSourceMaxDistance = true;
    Snapshot.ProjectionSettings.bUseNormalAlignmentConfidence = false;
    Snapshot.RaySettings = Layer.RaySettings;

    FDWCTransparencyAutoBakeResult& SeedResult = Snapshot.SeedResult;
    SeedResult.LayerGuid = Layer.LayerGuid;
    SeedResult.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
    SeedResult.UVChannelIndex = DataUVChannelIndex;
    SeedResult.LODIndex = LODIndex;
    SeedResult.Resolution = BakeResolution;
    SeedResult.OuterSampleCount = Snapshot.OuterSamples.Num();
    SeedResult.OverlappedUVPixelCount = OverlappedPixelCount;
    SeedResult.InnerColorBuffer.Init(FColor::Black, PixelCount);
    SeedResult.AutoAlphaBuffer.Init(0, PixelCount);
    SeedResult.OuterCoverageBuffer.Init(0, PixelCount);
    SeedResult.ValidHitBuffer.Init(false, PixelCount);
    SeedResult.HitDistanceBuffer.Init(0.0f, PixelCount);
    SeedResult.SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    SeedResult.OuterIslandIDBuffer.Init(FDWCTransparencyAutoBakeResult::InvalidOuterIslandID, PixelCount);

    const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
    for (const FDWCRevealBakeTexelSample& Sample : Snapshot.OuterSamples)
    {
        if (Sample.Pixel.X >= 0 && Sample.Pixel.Y >= 0 &&
            Sample.Pixel.X < Resolution && Sample.Pixel.Y < Resolution)
        {
            const int32 PixelIndex = Sample.Pixel.Y * Resolution + Sample.Pixel.X;
            SeedResult.OuterCoverageBuffer[PixelIndex] = 1;
            uint16 EncodedIslandID = FDWCTransparencyAutoBakeResult::InvalidOuterIslandID;
            if (!TryEncodeOuterSampleUVIslandID(
                    DataUVMetadata,
                    Sample,
                    EncodedIslandID,
                    OutErrorMessage))
            {
                return false;
            }
            SeedResult.OuterIslandIDBuffer[PixelIndex] = EncodedIslandID;
        }
    }

    SeedResult.BuildSignature = MakeSameMeshBuildSignature(
        WetClothingAsset,
        Layer,
        RuntimeMesh,
        Resolution,
        DataUVChannelIndex,
        LODIndex);
    if (OverlappedPixelCount > 0)
    {
        Snapshot.Warnings.Add(FString::Printf(
            TEXT("The target DWC Data UV contains %d genuinely overlapping rasterized pixel(s). Rebuild and inspect the DWC Data UV before editing transparency."),
            OverlappedPixelCount));
    }

    Snapshot.EstimatedBytes =
        Snapshot.OuterSurface.Triangles.GetAllocatedSize() +
        Snapshot.OuterSamples.GetAllocatedSize() +
        SeedResult.InnerColorBuffer.GetAllocatedSize() +
        SeedResult.AutoAlphaBuffer.GetAllocatedSize() +
        SeedResult.OuterCoverageBuffer.GetAllocatedSize() +
        SeedResult.OuterIslandIDBuffer.GetAllocatedSize() +
        SeedResult.ValidHitBuffer.GetAllocatedSize() +
        SeedResult.HitDistanceBuffer.GetAllocatedSize() +
        SeedResult.SourcePriorityBuffer.GetAllocatedSize();
    for (const FDWCRevealBakeSurface& SourceSurface : Snapshot.SourceSurfaces)
    {
        Snapshot.EstimatedBytes += SourceSurface.Triangles.GetAllocatedSize();
    }
    for (const TPair<FName, FWetClothingTextureReadback>& Pair : Snapshot.SourceTextureDataByLayerId)
    {
        if (Pair.Value.RawData.IsValid())
        {
            Snapshot.EstimatedBytes += Pair.Value.RawData->GetAllocatedSize();
        }
    }
    Snapshot.bValid = true;
    return true;
}

FDWCTransparencyAutoMapComputedResult FDWCTransparencyAutoMapGenerator::ComputeSameMeshSnapshot(
    FDWCTransparencyAutoMapSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FDWCTransparencyAutoMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The transparency projection snapshot is invalid.");
        return Result;
    }
    FDWCTransparencyAutoMapSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    Result.AutoResult = MoveTemp(Snapshot.SeedResult);
    Result.Warnings = Snapshot.Warnings;
    const int32 Resolution = Result.AutoResult.Resolution.X;

    const auto ConsumeHit =
        [&Result, &Snapshot, Resolution](const FDWCRevealBakeRayHit& Hit)
    {
        if (Hit.Pixel.X < 0 || Hit.Pixel.Y < 0 ||
            Hit.Pixel.X >= Resolution || Hit.Pixel.Y >= Resolution)
        {
            return;
        }
        FDWCTransparencyAutoBakeResult& AutoResult = Result.AutoResult;
        const int32 PixelIndex = Hit.Pixel.Y * Resolution + Hit.Pixel.X;
        if (!Hit.bHit)
        {
            ++AutoResult.NoHitCount;
            return;
        }

        ++AutoResult.ValidHitCount;
        AutoResult.ValidHitBuffer[PixelIndex] = true;
        AutoResult.HitDistanceBuffer[PixelIndex] = Hit.Distance;
        AutoResult.AutoAlphaBuffer[PixelIndex] = static_cast<uint8>(FMath::RoundToInt(
            CalculateAutoAlpha(Snapshot.RaySettings, Hit) * 255.0f));
        if (const int32* PriorityIndex = Snapshot.PriorityBySourceLayerId.Find(Hit.SourceLayerId))
        {
            AutoResult.SourcePriorityBuffer[PixelIndex] = static_cast<int16>(
                FMath::Clamp(*PriorityIndex, 0, static_cast<int32>(MAX_int16)));
        }
        if (const int32* StatsIndex = Snapshot.StatsIndexBySourceLayerId.Find(Hit.SourceLayerId);
            StatsIndex != nullptr && AutoResult.SourceStats.IsValidIndex(*StatsIndex))
        {
            ++AutoResult.SourceStats[*StatsIndex].HitCount;
        }
        if (const FWetClothingTextureReadback* SourceTexture =
                Snapshot.SourceTextureDataByLayerId.Find(Hit.SourceLayerId))
        {
            AutoResult.InnerColorBuffer[PixelIndex] =
                SampleTextureBilinear(*SourceTexture, Hit.SourceUV).ToFColor(true);
        }
        else
        {
            AutoResult.InnerColorBuffer[PixelIndex] = FColor::White;
        }
    };

    FString ProjectionError;
    if (!FDWCRevealBakeRayProjector::ProjectSamplesToSources(
            Snapshot.OuterSurface,
            Snapshot.SourceSurfaces,
            Snapshot.OuterSamples,
            Snapshot.ProjectionSettings,
            ConsumeHit,
            &ProjectionError,
            CancellationToken))
    {
        Result.bCanceled = CancellationToken != nullptr && CancellationToken->IsCanceled();
        Result.Error = ProjectionError;
        return Result;
    }

    Result.Summary = FString::Printf(
        TEXT("Generated %d outer samples: %d valid hit(s), %d no-hit sample(s)."),
        Result.AutoResult.OuterSampleCount,
        Result.AutoResult.ValidHitCount,
        Result.AutoResult.NoHitCount);
    Result.ResultBytes =
        Result.AutoResult.InnerColorBuffer.GetAllocatedSize() +
        Result.AutoResult.AutoAlphaBuffer.GetAllocatedSize() +
        Result.AutoResult.OuterCoverageBuffer.GetAllocatedSize() +
        Result.AutoResult.OuterIslandIDBuffer.GetAllocatedSize() +
        Result.AutoResult.ValidHitBuffer.GetAllocatedSize() +
        Result.AutoResult.HitDistanceBuffer.GetAllocatedSize() +
        Result.AutoResult.SourcePriorityBuffer.GetAllocatedSize();
    Result.bSucceeded = true;
    return Result;
}
