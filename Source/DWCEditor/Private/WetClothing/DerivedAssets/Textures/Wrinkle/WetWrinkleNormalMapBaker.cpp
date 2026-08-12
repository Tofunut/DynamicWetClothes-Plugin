//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "Core/DWCGeneratedAssetPaths.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleNormalTextureBuilder.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Assets/DWCEditorArtifactStore.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinklePatchDescriptor.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "ObjectTools.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCWrinkleBake, Log, All);

namespace
{
    FString BuildWrinkleGeneratedTextureAssetBaseName(const UWetClothingAsset& WetClothingAsset)
    {
        FString AssetToken = ObjectTools::SanitizeObjectName(WetClothingAsset.GetName());
        AssetToken.ReplaceInline(TEXT("_Wrinkle_"), TEXT("_"));
        AssetToken.RemoveFromEnd(TEXT("_Wrinkle"));
        return FString::Printf(TEXT("T_%s"), *AssetToken);
    }

    struct FWetWrinkleBakeSourceCacheEntry final : IDWCEditorCacheValue
    {
        FWetWrinkleTexturePixelBuffer Pixels;
        FWetClothingTextureReadback Readback;
        FString Error;
        FGuid SourceId;
        bool bFlipGreenChannel = false;
        bool bScalarSource = false;
        bool bValid = false;

        static FName StaticCacheTypeName()
        {
            static const FName Name(TEXT("WrinkleBakeSource"));
            return Name;
        }
        virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
        virtual uint64 GetAllocatedSizeBytes() const override
        {
            // Texture readback payloads own their broker reservation independently.
            return Pixels.Pixels.GetAllocatedSize() + Error.GetAllocatedSize();
        }
    };

    struct FWetWrinkleBakeSeparationCacheKey
    {
        const UTexture2D* Texture = nullptr;
        int32 BlurRadius = 0;
        float ConvexityThreshold = 0.0f;
        int32 MinimumComponentPixels = 0;
        bool bInvertConvexity = false;

        bool operator==(const FWetWrinkleBakeSeparationCacheKey& Other) const
        {
            return Texture == Other.Texture &&
                   BlurRadius == Other.BlurRadius &&
                   ConvexityThreshold == Other.ConvexityThreshold &&
                   MinimumComponentPixels == Other.MinimumComponentPixels &&
                   bInvertConvexity == Other.bInvertConvexity;
        }

        friend uint32 GetTypeHash(const FWetWrinkleBakeSeparationCacheKey& Key)
        {
            uint32 Hash = GetTypeHash(Key.Texture);
            Hash = HashCombineFast(Hash, GetTypeHash(Key.BlurRadius));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.ConvexityThreshold));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.MinimumComponentPixels));
            return HashCombineFast(Hash, GetTypeHash(Key.bInvertConvexity));
        }
    };

    struct FWetWrinkleBakeSeparationCacheEntry final : IDWCEditorCacheValue
    {
        FWetWrinkleTextureScalarBuffer Buffer;
        TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> SharedValues;
        FString Error;
        FGuid SourceId;
        bool bFlipGreenChannel = false;
        bool bValid = false;

        static FName StaticCacheTypeName()
        {
            static const FName Name(TEXT("WrinkleBakeSeparation"));
            return Name;
        }
        virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
        virtual uint64 GetAllocatedSizeBytes() const override
        {
            return (SharedValues.IsValid() ? SharedValues->GetAllocatedSize() : 0) +
                Error.GetAllocatedSize();
        }
    };

    struct FWetWrinkleBakeIslandCacheKey
    {
        const UWetClothingAsset* Asset = nullptr;
        const UObject* Mesh = nullptr;
        int32 LODIndex = 0;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = 0;
        int32 Width = 0;
        int32 Height = 0;

        bool operator==(const FWetWrinkleBakeIslandCacheKey& Other) const
        {
            return Asset == Other.Asset &&
                   Mesh == Other.Mesh &&
                   LODIndex == Other.LODIndex &&
                   MaterialSlotIndex == Other.MaterialSlotIndex &&
                   UVChannelIndex == Other.UVChannelIndex &&
                   Width == Other.Width &&
                   Height == Other.Height;
        }

        friend uint32 GetTypeHash(const FWetWrinkleBakeIslandCacheKey& Key)
        {
            uint32 Hash = HashCombineFast(GetTypeHash(Key.Asset), GetTypeHash(Key.Mesh));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.LODIndex));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.MaterialSlotIndex));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.UVChannelIndex));
            Hash = HashCombineFast(Hash, GetTypeHash(Key.Width));
            return HashCombineFast(Hash, GetTypeHash(Key.Height));
        }
    };

    struct FWetWrinkleBakeIslandCacheEntry final : IDWCEditorCacheValue
    {
        TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> SharedMask;
        FString TopologySignature;
        bool bValid = false;

        static FName StaticCacheTypeName()
        {
            static const FName Name(TEXT("WrinkleBakeIslandMask"));
            return Name;
        }
        virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
        virtual uint64 GetAllocatedSizeBytes() const override
        {
            return (SharedMask.IsValid() ? SharedMask->GetAllocatedSize() : 0) +
                TopologySignature.GetAllocatedSize();
        }
    };

    FDWCEditorCacheKey MakeWrinkleBakeSourceCacheKey(
        UTexture2D* Texture,
        const FGuid& SourceId,
        const bool bFlipGreenChannel)
    {
        FDWCEditorCacheKey Key;
        Key.Namespace = TEXT("WrinkleBake.Source");
        Key.Owner = FObjectKey(Texture);
        Key.ResourceIdentity = Texture;
        Key.Signature = FString::Printf(
            TEXT("%s|FlipGreen=%d"),
            *SourceId.ToString(EGuidFormats::Digits),
            bFlipGreenChannel ? 1 : 0);
        return Key;
    }

    bool BuildWrinkleArtifactRequest(
        UWetClothingAsset& WetClothingAsset,
        const FString& ObjectSuffix,
        const FIntPoint Resolution,
        const ETextureSourceFormat SourceFormat,
        const void* PixelData,
        const uint64 PixelBytes,
        UTexture2D* ExistingTexture,
        const bool bNormalMap,
        FDWCEditorArtifactTextureRequest& OutRequest,
        FString& OutErrorMessage)
    {
        const FString WcaFolder = FPackageName::GetLongPackagePath(
            WetClothingAsset.GetOutermost()->GetName());
        const FString PackagePath =
            DWCGeneratedAssetPaths::MakeAssetRoot(WcaFolder, WetClothingAsset.GetName()) /
            TEXT("Textures") / TEXT("Wrinkles");
        if (PackagePath.IsEmpty() || Resolution.X <= 0 || Resolution.Y <= 0)
        {
            OutErrorMessage = TEXT("Could not resolve a valid wrinkle texture output contract.");
            return false;
        }
        const FString ObjectName = ObjectTools::SanitizeObjectName(FString::Printf(
            TEXT("%s_%s"),
            *BuildWrinkleGeneratedTextureAssetBaseName(WetClothingAsset),
            *ObjectSuffix));
        const FString PackageName = PackagePath / ObjectName;
        const FString ObjectPath = PackageName + TEXT(".") + ObjectName;
        const bool bExistingTextureMatchesTarget =
            IsValid(ExistingTexture) && ExistingTexture->GetPathName() == ObjectPath;

        OutRequest = FDWCEditorArtifactTextureRequest();
        OutRequest.OwnerAsset = &WetClothingAsset;
        OutRequest.PackageName = PackageName;
        OutRequest.AssetName = ObjectName;
        OutRequest.ExistingTexture = bExistingTextureMatchesTarget ? ExistingTexture : nullptr;
        OutRequest.bExistingReferenceIsTrusted = bExistingTextureMatchesTarget;
        OutRequest.Resolution = Resolution;
        OutRequest.SourceFormat = SourceFormat;
        OutRequest.PixelData = static_cast<const uint8*>(PixelData);
        OutRequest.PixelBytes = PixelBytes;
        OutRequest.Settings.CompressionSettings = bNormalMap ? TC_Normalmap : TC_Grayscale;
        OutRequest.Settings.Filter = TF_Bilinear;
        OutRequest.DebugName = bNormalMap
            ? TEXT("Wrinkle normal texture")
            : TEXT("Wrinkle mask texture");
        OutErrorMessage.Reset();
        return true;
    }

    FDWCEditorCacheKey MakeWrinkleBakeSeparationCacheKey(
        UTexture2D* Texture,
        const FGuid& SourceId,
        const bool bFlipGreenChannel,
        const FWetWrinkleCoverageExtractionSettings& Settings)
    {
        FDWCEditorCacheKey Key;
        Key.Namespace = TEXT("WrinkleBake.Separation");
        Key.Owner = FObjectKey(Texture);
        Key.ResourceIdentity = Texture;
        Key.Signature = FString::Printf(
            TEXT("%s|FlipGreen=%d|Blur=%d|Threshold=%.9g|Min=%d|Invert=%d"),
            *SourceId.ToString(EGuidFormats::Digits),
            bFlipGreenChannel ? 1 : 0,
            Settings.InputBlurRadiusPixels,
            Settings.ConvexityThreshold,
            Settings.MinimumComponentPixels,
            Settings.bInvertConvexity ? 1 : 0);
        return Key;
    }

    FDWCEditorCacheKey MakeWrinkleBakeIslandCacheKey(
        const UWetClothingAsset& Asset,
        const UObject* Mesh,
        const int32 LODIndex,
        const int32 MaterialSlotIndex,
        const int32 UVChannelIndex,
        const FIntPoint TextureSize,
        const FString& TopologySignature)
    {
        FDWCEditorCacheKey Key;
        Key.Namespace = TEXT("WrinkleBake.IslandMask");
        Key.Owner = FObjectKey(&Asset);
        Key.ResourceIdentity = Mesh;
        Key.LODIndex = LODIndex;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        Key.UVChannelIndex = UVChannelIndex;
        Key.Signature = FString::Printf(
            TEXT("%s|%dx%d"),
            *TopologySignature,
            TextureSize.X,
            TextureSize.Y);
        return Key;
    }

    struct FWetWrinkleBakePatchCommandInput
    {
        FGuid PatchGuid;
        FString DisplayName;
        EWetWrinklePatchProjectionMode AuthoredMode =
            EWetWrinklePatchProjectionMode::NonUVSeam;
        EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy =
            EDWCEditorSurfacePatchBoundaryPolicy::Invalid;
        FDWCEditorSurfaceNormalPatchInput RasterInput;

        uint64 GetAllocatedSizeBytes() const
        {
            return DisplayName.GetAllocatedSize();
        }
    };

    const TCHAR* GetAuthoredProjectionModeName(const EWetWrinklePatchProjectionMode Mode)
    {
        return Mode == EWetWrinklePatchProjectionMode::SurfaceDecal
            ? TEXT("UVSeam")
            : TEXT("NonUVSeam");
    }

    const TCHAR* GetBoundaryPolicyName(const EDWCEditorSurfacePatchBoundaryPolicy Policy)
    {
        switch (Policy)
        {
        case EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly:
            return TEXT("AnchorUVIslandOnly");
        case EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams:
            return TEXT("CrossUVSeams");
        default:
            return TEXT("Invalid");
        }
    }

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

    float SignedTriangleArea2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
    }

    bool BuildIslandMaskUncached(
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

}

struct FWetWrinkleNormalMapBakeSession::FImpl
{
    FImpl(
        TSharedRef<FDWCEditorSpatialQueryService> InSpatialQueryService,
        TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> InSurfacePatchProjectionCache,
        TSharedPtr<FDWCEditorCacheStore> InCacheStore)
        : SpatialQueryService(MoveTemp(InSpatialQueryService))
        , SurfacePatchProjectionCache(MoveTemp(InSurfacePatchProjectionCache))
        , CacheStore(MoveTemp(InCacheStore))
    {
    }

    TSharedRef<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache;
    TSharedPtr<FDWCEditorCacheStore> CacheStore;
    TMap<const UTexture2D*, TSharedPtr<const FWetWrinkleBakeSourceCacheEntry, ESPMode::ThreadSafe>> SourceTextures;
    TMap<FWetWrinkleBakeSeparationCacheKey, TSharedPtr<const FWetWrinkleBakeSeparationCacheEntry, ESPMode::ThreadSafe>> SeparationBuffers;
    TMap<FWetWrinkleBakeIslandCacheKey, TSharedPtr<const FWetWrinkleBakeIslandCacheEntry, ESPMode::ThreadSafe>> IslandMasks;
};

struct FWetWrinkleNormalMapBakeSnapshot::FImpl
{
    int32 LODIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    int32 PaddingPixels = 0;
    FIntPoint FinalTextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FWetWrinkleBakePatchCommandInput> PatchCommandInputs;
    TArray<FWetProceduralRidgeStroke> ProceduralRidgeStrokes;
    FDWCEditorSpatialLease SpatialLease;
    TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache;
    TArray<FDWCEditorCacheLease> CacheLeases;
    TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> IslandMask;
    FString BaseSuffix;
    FString BuildSignature;
    uint32 SurfaceProjectionAlgorithmVersion =
        DWCEditorSurfacePatchProjectionVersion::SurfaceProjection;
    uint32 ProjectedRasterAlgorithmVersion =
        DWCEditorSurfacePatchProjectionVersion::ProjectedRaster;
    uint64 EstimatedSnapshotBytes = 0;
    uint64 EstimatedWorkingBytes = 0;
    uint64 EstimatedResultBytes = 0;
    uint64 EstimatedCommitBytes = 0;
    FWetWrinkleNormalMapBakeMemoryPlan MemoryPlan;
    bool bValid = false;
};

FWetWrinkleNormalMapBakeSession::FWetWrinkleNormalMapBakeSession(
    TSharedRef<FDWCEditorSpatialQueryService> InSpatialQueryService,
    TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> InSurfacePatchProjectionCache,
    TSharedPtr<FDWCEditorCacheStore> InCacheStore)
    : Impl(MakeUnique<FImpl>(
        MoveTemp(InSpatialQueryService),
        MoveTemp(InSurfacePatchProjectionCache),
        MoveTemp(InCacheStore)))
{
}

FWetWrinkleNormalMapBakeSession::~FWetWrinkleNormalMapBakeSession() = default;
FWetWrinkleNormalMapBakeSession::FWetWrinkleNormalMapBakeSession(FWetWrinkleNormalMapBakeSession&&) = default;
FWetWrinkleNormalMapBakeSession& FWetWrinkleNormalMapBakeSession::operator=(FWetWrinkleNormalMapBakeSession&&) = default;

FWetWrinkleNormalMapBakeSnapshot::FWetWrinkleNormalMapBakeSnapshot()
    : Impl(MakeUnique<FImpl>())
{
}

FWetWrinkleNormalMapBakeSnapshot::~FWetWrinkleNormalMapBakeSnapshot() = default;
FWetWrinkleNormalMapBakeSnapshot::FWetWrinkleNormalMapBakeSnapshot(FWetWrinkleNormalMapBakeSnapshot&&) = default;
FWetWrinkleNormalMapBakeSnapshot& FWetWrinkleNormalMapBakeSnapshot::operator=(FWetWrinkleNormalMapBakeSnapshot&&) = default;

bool FWetWrinkleNormalMapBakeSnapshot::IsValid() const
{
    return Impl.IsValid() && Impl->bValid;
}

int32 FWetWrinkleNormalMapBakeSnapshot::GetMaterialSlotIndex() const
{
    return Impl.IsValid() ? Impl->MaterialSlotIndex : INDEX_NONE;
}

uint64 FWetWrinkleNormalMapBakeSnapshot::GetEstimatedSnapshotBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedSnapshotBytes : 0;
}

uint64 FWetWrinkleNormalMapBakeSnapshot::GetEstimatedWorkingBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedWorkingBytes : 0;
}

uint64 FWetWrinkleNormalMapBakeSnapshot::GetEstimatedResultBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedResultBytes : 0;
}

uint64 FWetWrinkleNormalMapBakeSnapshot::GetEstimatedCommitBytes() const
{
    return Impl.IsValid() ? Impl->EstimatedCommitBytes : 0;
}

FWetWrinkleNormalMapBakeMemoryPlan FWetWrinkleNormalMapBakeSnapshot::GetMemoryPlan() const
{
    return Impl.IsValid() ? Impl->MemoryPlan : FWetWrinkleNormalMapBakeMemoryPlan();
}

void FWetWrinkleNormalMapBakeSnapshot::ReleaseWorkerResources()
{
    if (!Impl.IsValid())
    {
        return;
    }

    Impl->PatchCommandInputs.Reset();
    Impl->ProceduralRidgeStrokes.Reset();
    Impl->SpatialLease.Reset();
    Impl->SurfacePatchProjectionCache.Reset();
    Impl->CacheLeases.Reset();
    Impl->IslandMask.Reset();
    Impl->EstimatedSnapshotBytes = Impl->EstimatedCommitBytes;
    Impl->MemoryPlan.SnapshotBytes = Impl->EstimatedCommitBytes;
    Impl->MemoryPlan.RasterBytes = 0;
    Impl->MemoryPlan.PostProcessBytes = 0;
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
    FWetWrinkleNormalMapBakeSession& Session,
    FWetWrinkleNormalMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    FWetWrinkleNormalMapBakeSnapshot Snapshot;
    if (!BuildMaterialSlotSnapshot(
            WetClothingAsset,
            MaterialSlotIndex,
            Settings,
            Session,
            Snapshot,
            OutErrorMessage))
    {
        return false;
    }

    FWetWrinkleNormalMapComputedResult ComputedResult = ComputeSnapshot(Snapshot);
    return CommitComputedResult(
        WetClothingAsset,
        Snapshot,
        MoveTemp(ComputedResult),
        OutResult,
        OutErrorMessage);
}

bool FWetWrinkleNormalMapBaker::BuildMaterialSlotSnapshot(
    UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeSession& Session,
    FWetWrinkleNormalMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutSnapshot = FWetWrinkleNormalMapBakeSnapshot();
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

    const int32 DataUVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
    int32 MatchingStampCount = 0;
    int32 MissingTextureCount = 0;
    int32 MatchingStrokeCount = 0;
    int32 InvalidStrokeCount = 0;
    FBakeGroup Group;
    Group.LODIndex = 0;
    Group.MaterialSlotIndex = MaterialSlotIndex;
    Group.UVChannelIndex = DataUVChannelIndex;

    for (const FWetWrinklePatchPlacement& Stamp : WetClothingAsset->Authored.WrinkleData.EditablePatches)
    {
        if ((!Stamp.bEnabled && !Settings.bIncludeDisabledPatches) ||
            Stamp.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }
        ++MatchingStampCount;
        if (Stamp.WrinkleNormalTexture == nullptr)
        {
            ++MissingTextureCount;
            continue;
        }
        Group.Stamps.Add(&Stamp);
    }

    for (const FWetProceduralRidgeStroke& Stroke :
         WetClothingAsset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
    {
        if ((!Stroke.bEnabled && !Settings.bIncludeDisabledPatches) ||
            Stroke.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }
        ++MatchingStrokeCount;
        if (Stroke.Points.Num() < 2 || Stroke.WidthUV <= 0.0f || Stroke.Strength <= 0.0f)
        {
            ++InvalidStrokeCount;
            continue;
        }
        Group.ProceduralRidgeStrokes.Add(&Stroke);
    }

    if (Group.Stamps.IsEmpty() && Group.ProceduralRidgeStrokes.IsEmpty())
    {
        if (MatchingStampCount == 0 && MatchingStrokeCount == 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("No wrinkle patches or procedural ridge strokes were found for the selected material slot on UV channel %d."),
                DataUVChannelIndex);
        }
        else if (MissingTextureCount > 0)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wrinkle patches were found, but %d patch(es) do not reference a wrinkle normal texture."),
                MissingTextureCount);
        }
        else
        {
            OutErrorMessage = FString::Printf(
                TEXT("Procedural ridge strokes were found, but %d stroke(s) do not contain a bakeable centerline, width, or strength."),
                InvalidStrokeCount);
        }
        return false;
    }

    return BuildGroupSnapshot(
        *WetClothingAsset,
        Group,
        Settings,
        Session,
        OutSnapshot,
        OutErrorMessage);
}

bool FWetWrinkleNormalMapBaker::IsMaterialSlotBakeCurrent(
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex)
{
    return EvaluateMaterialSlotBakeState(
        WetClothingAsset, MaterialSlotIndex, true).IsCurrent();
}

FWetWrinkleMaterialSlotBakeState FWetWrinkleNormalMapBaker::EvaluateMaterialSlotBakeState(
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex,
    const bool bExactSignature)
{
    FWetWrinkleMaterialSlotBakeState Result;
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        Result.Detail = TEXT("The WCA or material slot is unavailable.");
        return Result;
    }

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = WetClothingAsset->GetWrinkleMapResolution();
    Settings.PaddingPixels = WetClothingAsset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatches =
        WetClothingAsset->Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches;

    const int32 UVChannelIndex = WetClothingAsset->GetDWCDataUVChannelIndex();
    const FIntPoint TextureSize = WetWrinkleTextureRaster::ResolveFinalTextureSize(Settings.Resolution);
    FBakeGroup Group;
    Group.LODIndex = 0;
    Group.MaterialSlotIndex = MaterialSlotIndex;
    Group.UVChannelIndex = UVChannelIndex;

        for (const FWetWrinklePatchPlacement& Stamp : WetClothingAsset->Authored.WrinkleData.EditablePatches)
        {
            if ((!Stamp.bEnabled && !Settings.bIncludeDisabledPatches) ||
                Stamp.MaterialSlotIndex != MaterialSlotIndex ||
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
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::NoBakeableContent;
        Result.Detail = TEXT("No valid authored wrinkle content requires a baked output.");
        return Result;
    }
    Result.bHasBakeableContent = true;

    const FWetWrinkleBakedMapSet* BakedMap =
        WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    Result.bNormalExists = BakedMap != nullptr && BakedMap->BakedWrinkleNormalMap != nullptr;
    Result.bCoverageMaskExists = BakedMap != nullptr && BakedMap->BakedWrinkleMask != nullptr;
    if (!Result.bNormalExists)
    {
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::NormalMissing;
        Result.Detail = TEXT("The baked wrinkle normal texture is missing.");
        return Result;
    }
    if (!Result.bCoverageMaskExists)
    {
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::CoverageMissing;
        Result.Detail = TEXT("The editor-only wrinkle coverage mask is missing.");
        return Result;
    }

    Result.bResolutionMatches = BakedMap->Resolution == TextureSize.X;
    if (!Result.bResolutionMatches)
    {
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::ResolutionMismatch;
        Result.Detail = TEXT("The baked wrinkle output resolution no longer matches Asset Setup.");
        return Result;
    }
    Result.bPaddingMatches =
        BakedMap->PaddingPixels == FMath::Clamp(Settings.PaddingPixels, 0, 64);
    if (!Result.bPaddingMatches)
    {
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::PaddingMismatch;
        Result.Detail = TEXT("The baked wrinkle padding no longer matches the authored settings.");
        return Result;
    }

    Result.bSignatureMatches = !BakedMap->BuildSignature.IsEmpty() &&
        BakedMap->BakeGuid.IsValid() &&
        (!bExactSignature ||
         BakedMap->BuildSignature == MakeBuildSignature(
            *WetClothingAsset,
            Group,
            TextureSize.X,
            TextureSize.Y,
            Settings));
    if (!Result.bSignatureMatches)
    {
        Result.Issue = EWetWrinkleBakeCurrentnessIssue::SignatureMismatch;
        Result.Detail = TEXT("The baked wrinkle outputs were built from older authored data.");
        return Result;
    }

    Result.Issue = EWetWrinkleBakeCurrentnessIssue::None;
    Result.Detail = TEXT("The baked wrinkle normal and coverage outputs are current.");
    return Result;
}

bool FWetWrinkleNormalMapBaker::BuildGroupSnapshot(
    UWetClothingAsset& WetClothingAsset,
    const FBakeGroup& Group,
    const FWetWrinkleNormalMapBakeSettings& Settings,
    FWetWrinkleNormalMapBakeSession& Session,
    FWetWrinkleNormalMapBakeSnapshot& OutSnapshot,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    if (Group.Stamps.Num() == 0 && Group.ProceduralRidgeStrokes.Num() == 0)
    {
        OutErrorMessage = TEXT("The wrinkle bake group is empty.");
        return false;
    }

    const FIntPoint FinalTextureSize = WetWrinkleTextureRaster::ResolveFinalTextureSize(Settings.Resolution);
    const FIntPoint WorkingTextureSize = WetWrinkleTextureRaster::ResolveWorkingTextureSize(FinalTextureSize);
    if (FinalTextureSize.X <= 0 || FinalTextureSize.Y <= 0 ||
        WorkingTextureSize.X <= 0 || WorkingTextureSize.Y <= 0)
    {
        OutErrorMessage = TEXT("The wrinkle bake resolution is invalid.");
        return false;
    }

    check(OutSnapshot.Impl.IsValid());
    FWetWrinkleNormalMapBakeSnapshot::FImpl& Snapshot = *OutSnapshot.Impl;
    Snapshot.LODIndex = Group.LODIndex;
    Snapshot.MaterialSlotIndex = Group.MaterialSlotIndex;
    Snapshot.UVChannelIndex = Group.UVChannelIndex;
    Snapshot.PaddingPixels = FMath::Clamp(Settings.PaddingPixels, 0, 64);
    Snapshot.FinalTextureSize = FinalTextureSize;
    Snapshot.WorkingTextureSize = WorkingTextureSize;
    Snapshot.SurfacePatchProjectionCache = Session.Impl->SurfacePatchProjectionCache;
    if (!Snapshot.SurfacePatchProjectionCache.IsValid())
    {
        OutErrorMessage = TEXT("The wrinkle bake session has no surface projection cache.");
        return false;
    }

    FDWCEditorSpatialHandle SpatialHandle;
    if (!Group.Stamps.IsEmpty())
    {
        FString SpatialError;
        Snapshot.SpatialLease = Session.Impl->SpatialQueryService->AcquireLease(
            &WetClothingAsset,
            WetClothingAsset.GetDWCSkeletalMesh(),
            Group.UVChannelIndex,
            Group.MaterialSlotIndex,
            &SpatialError);
        if (!Snapshot.SpatialLease.IsValid())
        {
            OutErrorMessage = SpatialError.IsEmpty()
                ? TEXT("Could not lease the material-slot surface topology for wrinkle baking.")
                : MoveTemp(SpatialError);
            return false;
        }
        SpatialHandle = StaticCastSharedPtr<const FDWCEditorSpatialData>(
            Snapshot.SpatialLease.GetSharedValue());
        if (!SpatialHandle.IsValid())
        {
            OutErrorMessage = TEXT("The wrinkle bake spatial lease contains no usable topology payload.");
            return false;
        }
    }

    const FWetWrinkleCoverageExtractionSettings& CoverageSettings =
        WetClothingAsset.Authored.WrinkleData.CoverageExtractionSettings;
    check(Session.Impl.IsValid());
    for (const FWetWrinklePatchPlacement* StampPtr : Group.Stamps)
    {
        const FWetWrinklePatchPlacement& Stamp = *StampPtr;
        FDWCEditorWrinklePatchValidationResult PatchValidation;
        if (!FDWCEditorWrinklePatchDescriptorBuilder::ValidatePlacement(
                Stamp,
                Group.UVChannelIndex,
                PatchValidation))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not validate the physical projection contract for patch '%s' (%s): %s"),
                Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName,
                *Stamp.PatchGuid.ToString(EGuidFormats::Digits),
                PatchValidation.Error.IsEmpty() ? TEXT("invalid descriptor") : *PatchValidation.Error);
            return false;
        }
        const FDWCEditorWrinklePatchDescriptor& PatchDescriptor = PatchValidation.Descriptor;
        UTexture2D* CorrectedNormalTexture = Stamp.WrinkleNormalTexture;
        const FGuid SourceId = CorrectedNormalTexture != nullptr
            ? CorrectedNormalTexture->Source.GetId()
            : FGuid();
        const bool bFlipGreenChannel =
            CorrectedNormalTexture != nullptr && CorrectedNormalTexture->bFlipGreenChannel;
        const FWetWrinkleBakeSourceCacheEntry* NormalSource = nullptr;
        FDWCEditorCacheLease SourceLease;
        const FDWCEditorCacheKey SourceCacheKey = MakeWrinkleBakeSourceCacheKey(
            CorrectedNormalTexture,
            SourceId,
            bFlipGreenChannel);
        if (Session.Impl->CacheStore.IsValid())
        {
            SourceLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeSourceCacheEntry>(
                SourceCacheKey);
            NormalSource = SourceLease.GetAs<FWetWrinkleBakeSourceCacheEntry>();
        }
        else if (const TSharedPtr<const FWetWrinkleBakeSourceCacheEntry, ESPMode::ThreadSafe>* Cached =
                     Session.Impl->SourceTextures.Find(CorrectedNormalTexture))
        {
            if (Cached->IsValid() && (*Cached)->SourceId == SourceId &&
                (*Cached)->bFlipGreenChannel == bFlipGreenChannel)
            {
                NormalSource = Cached->Get();
            }
        }
        if (NormalSource == nullptr)
        {
            TSharedRef<FWetWrinkleBakeSourceCacheEntry, ESPMode::ThreadSafe> NewSource =
                MakeShared<FWetWrinkleBakeSourceCacheEntry, ESPMode::ThreadSafe>();
            NewSource->SourceId = SourceId;
            NewSource->bFlipGreenChannel = bFlipGreenChannel;
            NewSource->bScalarSource =
                CorrectedNormalTexture != nullptr &&
                CorrectedNormalTexture->Source.GetFormat() == TSF_G8;
            NewSource->bValid = FWetWrinkleNormalTextureBuilder::ReadTextureSourcePixels(
                CorrectedNormalTexture,
                NewSource->Pixels,
                NewSource->Error);
            FString ReadbackError;
            NewSource->bValid = NewSource->bValid &&
                FWetClothingTextureReadbackUtils::TryReadTextureSourceData(
                    CorrectedNormalTexture,
                    NewSource->Readback,
                    ReadbackError);
            if (!NewSource->bValid && NewSource->Error.IsEmpty())
            {
                NewSource->Error = MoveTemp(ReadbackError);
            }

            if (Session.Impl->CacheStore.IsValid())
            {
                TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = NewSource;
                if (!Session.Impl->CacheStore->Put(SourceCacheKey, CacheValue))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("The shared editor cache budget cannot retain wrinkle source '%s'."),
                        *GetNameSafe(CorrectedNormalTexture));
                    return false;
                }
                SourceLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeSourceCacheEntry>(
                    SourceCacheKey);
                NormalSource = SourceLease.GetAs<FWetWrinkleBakeSourceCacheEntry>();
            }
            else
            {
                TSharedPtr<const FWetWrinkleBakeSourceCacheEntry, ESPMode::ThreadSafe> Stored = NewSource;
                Session.Impl->SourceTextures.Add(CorrectedNormalTexture, Stored);
                NormalSource = Stored.Get();
            }
        }

        if (NormalSource == nullptr || !NormalSource->bValid || Stamp.Strength <= 0.0f)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Wrinkle patch '%s' (%s) has an unreadable normal source or invalid strength."),
                Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName,
                *Stamp.PatchGuid.ToString(EGuidFormats::Digits));
            return false;
        }

        const FWetWrinkleBakeSeparationCacheKey SeparationKey{
            CorrectedNormalTexture,
            CoverageSettings.InputBlurRadiusPixels,
            CoverageSettings.ConvexityThreshold,
            CoverageSettings.MinimumComponentPixels,
            CoverageSettings.bInvertConvexity};
        const FWetWrinkleBakeSeparationCacheEntry* SeparationEntry = nullptr;
        FDWCEditorCacheLease SeparationLease;
        const FDWCEditorCacheKey SeparationCacheKey = MakeWrinkleBakeSeparationCacheKey(
            CorrectedNormalTexture,
            SourceId,
            bFlipGreenChannel,
            CoverageSettings);
        if (Session.Impl->CacheStore.IsValid())
        {
            SeparationLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeSeparationCacheEntry>(
                SeparationCacheKey);
            SeparationEntry = SeparationLease.GetAs<FWetWrinkleBakeSeparationCacheEntry>();
        }
        else if (const TSharedPtr<const FWetWrinkleBakeSeparationCacheEntry, ESPMode::ThreadSafe>* Cached =
                     Session.Impl->SeparationBuffers.Find(SeparationKey))
        {
            SeparationEntry = Cached->Get();
        }
        if (SeparationEntry == nullptr)
        {
            TSharedRef<FWetWrinkleBakeSeparationCacheEntry, ESPMode::ThreadSafe> NewSeparationEntry =
                MakeShared<FWetWrinkleBakeSeparationCacheEntry, ESPMode::ThreadSafe>();
            NewSeparationEntry->SourceId = SourceId;
            NewSeparationEntry->bFlipGreenChannel = bFlipGreenChannel;
            NewSeparationEntry->bValid =
                FWetWrinkleNormalTextureBuilder::BuildConvexSeparationBufferFromPixels(
                    NormalSource->Pixels,
                    NormalSource->bFlipGreenChannel,
                    CoverageSettings,
                    NewSeparationEntry->Buffer,
                    NewSeparationEntry->Error);
            if (NewSeparationEntry->bValid)
            {
                NewSeparationEntry->SharedValues =
                    MakeShared<const TArray<float>, ESPMode::ThreadSafe>(
                        MoveTemp(NewSeparationEntry->Buffer.Values));
            }
            if (!NewSeparationEntry->bValid)
            {
                UE_LOG(
                    LogDWCWrinkleBake,
                    Warning,
                    TEXT("DWC wrinkle bake skipped normal texture '%s': %s"),
                    *GetNameSafe(CorrectedNormalTexture),
                    *NewSeparationEntry->Error);
            }
            if (Session.Impl->CacheStore.IsValid())
            {
                TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = NewSeparationEntry;
                if (!Session.Impl->CacheStore->Put(SeparationCacheKey, CacheValue))
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("The shared editor cache budget cannot retain wrinkle coverage for '%s'."),
                        *GetNameSafe(CorrectedNormalTexture));
                    return false;
                }
                SeparationLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeSeparationCacheEntry>(
                    SeparationCacheKey);
                SeparationEntry = SeparationLease.GetAs<FWetWrinkleBakeSeparationCacheEntry>();
            }
            else
            {
                TSharedPtr<const FWetWrinkleBakeSeparationCacheEntry, ESPMode::ThreadSafe> Stored =
                    NewSeparationEntry;
                Session.Impl->SeparationBuffers.Add(SeparationKey, Stored);
                SeparationEntry = Stored.Get();
            }
        }
        if (SeparationEntry == nullptr || !SeparationEntry->bValid)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not build wrinkle coverage for patch '%s' (%s): %s"),
                Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName,
                *Stamp.PatchGuid.ToString(EGuidFormats::Digits),
                SeparationEntry == nullptr || SeparationEntry->Error.IsEmpty()
                    ? TEXT("unknown coverage extraction error")
                    : *SeparationEntry->Error);
            return false;
        }

        FWetWrinkleBakePatchCommandInput PatchCommandInput;
        PatchCommandInput.PatchGuid = Stamp.PatchGuid;
        PatchCommandInput.DisplayName = Stamp.DisplayName;
        PatchCommandInput.AuthoredMode = Stamp.ProjectionMode;
        FString DescriptorError;
        FDWCEditorNormalSourceSnapshot NormalSourceSnapshot;
        NormalSourceSnapshot.Texture = NormalSource->Readback;
        NormalSourceSnapshot.bFlipGreenChannel = NormalSource->bFlipGreenChannel;
        FDWCEditorScalarSourceSnapshot CoverageSourceSnapshot;
        CoverageSourceSnapshot.Size = SeparationEntry->Buffer.Size;
        CoverageSourceSnapshot.Values = SeparationEntry->SharedValues;
        if (!FDWCEditorWrinklePatchDescriptorBuilder::BuildRasterInputFromSources(
                PatchDescriptor,
                SpatialHandle,
                NormalSourceSnapshot,
                CoverageSourceSnapshot,
                PatchCommandInput.RasterInput,
                &DescriptorError))
        {
            OutErrorMessage = FString::Printf(
                TEXT("Could not build the boundary-aware decal command input for patch '%s': %s"),
                Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName,
                DescriptorError.IsEmpty() ? TEXT("invalid descriptor") : *DescriptorError);
            return false;
        }
        PatchCommandInput.BoundaryPolicy =
            PatchCommandInput.RasterInput.Projection.BoundaryPolicy;
        if (PatchCommandInput.BoundaryPolicy ==
                EDWCEditorSurfacePatchBoundaryPolicy::Invalid)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Patch '%s' did not resolve to a valid boundary-aware decal command."),
                Stamp.DisplayName.IsEmpty() ? TEXT("Unnamed Patch") : *Stamp.DisplayName);
            return false;
        }
        Snapshot.PatchCommandInputs.Add(MoveTemp(PatchCommandInput));
        if (SourceLease.IsValid())
        {
            Snapshot.CacheLeases.Add(MoveTemp(SourceLease));
        }
        if (SeparationLease.IsValid())
        {
            Snapshot.CacheLeases.Add(MoveTemp(SeparationLease));
        }
    }

    for (const FWetProceduralRidgeStroke* Stroke : Group.ProceduralRidgeStrokes)
    {
        if (Stroke != nullptr)
        {
            Snapshot.ProceduralRidgeStrokes.Add(*Stroke);
        }
    }

    if (Snapshot.PatchCommandInputs.IsEmpty() && Snapshot.ProceduralRidgeStrokes.IsEmpty())
    {
        OutErrorMessage = TEXT("The selected material slot has no readable wrinkle input.");
        return false;
    }

    const FWetWrinkleBakeIslandCacheKey IslandKey{
        &WetClothingAsset,
        WetClothingAsset.GetDWCSkeletalMesh(),
        Group.LODIndex,
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        FinalTextureSize.X,
        FinalTextureSize.Y};
    FString TopologySignature = WetClothingAsset.GetSourceMeshSignature();
    if (const FDWCDataUVLODMetadata* DataUVMetadata =
            WetClothingAsset.FindDataUVMetadataForLOD(Group.LODIndex))
    {
        TopologySignature += TEXT("|");
        TopologySignature += DataUVMetadata->DataUVOutputSignature;
    }
    const FWetWrinkleBakeIslandCacheEntry* IslandEntry = nullptr;
    FDWCEditorCacheLease IslandLease;
    const FDWCEditorCacheKey IslandCacheKey = MakeWrinkleBakeIslandCacheKey(
        WetClothingAsset,
        WetClothingAsset.GetDWCSkeletalMesh(),
        Group.LODIndex,
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        FinalTextureSize,
        TopologySignature);
    if (Session.Impl->CacheStore.IsValid())
    {
        IslandLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeIslandCacheEntry>(
            IslandCacheKey);
        IslandEntry = IslandLease.GetAs<FWetWrinkleBakeIslandCacheEntry>();
    }
    else if (const TSharedPtr<const FWetWrinkleBakeIslandCacheEntry, ESPMode::ThreadSafe>* Cached =
                 Session.Impl->IslandMasks.Find(IslandKey))
    {
        if (Cached->IsValid() && (*Cached)->TopologySignature == TopologySignature)
        {
            IslandEntry = Cached->Get();
        }
    }
    if (IslandEntry == nullptr)
    {
        TSharedRef<FWetWrinkleBakeIslandCacheEntry, ESPMode::ThreadSafe> NewIslandEntry =
            MakeShared<FWetWrinkleBakeIslandCacheEntry, ESPMode::ThreadSafe>();
        NewIslandEntry->TopologySignature = TopologySignature;
        TArray<uint8> BuiltMask;
        NewIslandEntry->bValid = BuildIslandMaskUncached(
            WetClothingAsset,
            Group.LODIndex,
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            FinalTextureSize.X,
            FinalTextureSize.Y,
            BuiltMask);
        if (NewIslandEntry->bValid)
        {
            NewIslandEntry->SharedMask =
                MakeShared<const TArray<uint8>, ESPMode::ThreadSafe>(MoveTemp(BuiltMask));
        }
        if (Session.Impl->CacheStore.IsValid())
        {
            TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> CacheValue = NewIslandEntry;
            if (!Session.Impl->CacheStore->Put(IslandCacheKey, CacheValue))
            {
                OutErrorMessage = FString::Printf(
                    TEXT("The shared editor cache budget cannot retain the wrinkle island mask for slot %d."),
                    Group.MaterialSlotIndex);
                return false;
            }
            IslandLease = Session.Impl->CacheStore->FindLease<FWetWrinkleBakeIslandCacheEntry>(
                IslandCacheKey);
            IslandEntry = IslandLease.GetAs<FWetWrinkleBakeIslandCacheEntry>();
        }
        else
        {
            TSharedPtr<const FWetWrinkleBakeIslandCacheEntry, ESPMode::ThreadSafe> Stored = NewIslandEntry;
            Session.Impl->IslandMasks.Add(IslandKey, Stored);
            IslandEntry = Stored.Get();
        }
    }
    if (IslandEntry == nullptr || !IslandEntry->bValid)
    {
        OutErrorMessage = FString::Printf(
            TEXT("Could not build the UV island mask for material slot %d, UV channel %d, LOD %d."),
            Group.MaterialSlotIndex,
            Group.UVChannelIndex,
            Group.LODIndex);
        return false;
    }

    Snapshot.IslandMask = IslandEntry->SharedMask;
    // Each material slot is snapshotted once per batch. Keeping a second copy
    // in the session turns a 4096 all-slot bake into N additional 16 MiB masks.
    if (IslandLease.IsValid())
    {
        Snapshot.CacheLeases.Add(MoveTemp(IslandLease));
    }
    else
    {
        Session.Impl->IslandMasks.Remove(IslandKey);
    }
    Snapshot.BaseSuffix = FString::Printf(
        TEXT("Slot%d"),
        Group.MaterialSlotIndex);
    Snapshot.BuildSignature = MakeBuildSignature(
        WetClothingAsset,
        Group,
        FinalTextureSize.X,
        FinalTextureSize.Y,
        Settings);
    const uint64 WorkingSurfaceBytes =
        static_cast<uint64>(WorkingTextureSize.X) * WorkingTextureSize.Y *
        (sizeof(uint32) + sizeof(float));
    const uint64 FinalSurfaceBytes = WorkingTextureSize == FinalTextureSize
        ? 0
        : static_cast<uint64>(FinalTextureSize.X) * FinalTextureSize.Y *
            (sizeof(uint32) + sizeof(float));
    const uint64 ResultPixelsBytes =
        static_cast<uint64>(FinalTextureSize.X) * FinalTextureSize.Y * (sizeof(FColor) + sizeof(uint8));
    // Dilation retains an 8-bit distance field and can grow integer frontiers
    // over a substantial part of a 4K island. Budget those temporary arrays so
    // two large bakes cannot be admitted on an optimistic estimate.
    const uint64 DilationWorkingBytes = static_cast<uint64>(FinalTextureSize.X) * FinalTextureSize.Y *
        (sizeof(uint8) + sizeof(int32) * 2);
    const bool bUsesBudgetedCache = Session.Impl->CacheStore.IsValid();
    Snapshot.EstimatedSnapshotBytes =
        (bUsesBudgetedCache || !Snapshot.IslandMask.IsValid()
            ? 0
            : Snapshot.IslandMask->GetAllocatedSize()) +
        Snapshot.PatchCommandInputs.GetAllocatedSize() +
        Snapshot.ProceduralRidgeStrokes.GetAllocatedSize();
    Snapshot.EstimatedWorkingBytes = WorkingSurfaceBytes + FinalSurfaceBytes + DilationWorkingBytes;
    Snapshot.EstimatedResultBytes = ResultPixelsBytes;
    TSet<const void*> CountedSourceBuffers;
    for (const FWetWrinkleBakePatchCommandInput& Patch : Snapshot.PatchCommandInputs)
    {
        Snapshot.EstimatedSnapshotBytes += Patch.GetAllocatedSizeBytes();
        if (bUsesBudgetedCache)
        {
            continue;
        }
        const FDWCEditorScalarSourceSnapshot& CoverageSource = Patch.RasterInput.CoverageSource;
        // NormalSource retains a broker-accounted immutable readback handle and is not job-private memory.
        const void* CoverageBuffer = CoverageSource.Values.Get();
        if (CoverageBuffer != nullptr && !CountedSourceBuffers.Contains(CoverageBuffer))
        {
            CountedSourceBuffers.Add(CoverageBuffer);
            Snapshot.EstimatedSnapshotBytes += CoverageSource.Values->GetAllocatedSize();
        }
    }
    for (const FWetProceduralRidgeStroke& Stroke : Snapshot.ProceduralRidgeStrokes)
    {
        Snapshot.EstimatedSnapshotBytes += Stroke.Points.GetAllocatedSize() + Stroke.DisplayName.GetAllocatedSize();
    }
    if (SpatialHandle.IsValid() && !Snapshot.PatchCommandInputs.IsEmpty())
    {
        uint64 MaximumProjectionBytes = 0;
        for (const FWetWrinkleBakePatchCommandInput& Patch : Snapshot.PatchCommandInputs)
        {
            const FDWCEditorSurfacePatchProjectionMemoryEstimate Estimate =
                FDWCEditorSurfacePatchProjector::EstimateAdmissionMemory(
                    Patch.RasterInput.Projection);
            MaximumProjectionBytes = FMath::Max(
                MaximumProjectionBytes,
                Estimate.GetTotalBytes());
        }
        // Commands are resolved and released serially; the shared cache has its own budget.
        Snapshot.EstimatedWorkingBytes += MaximumProjectionBytes;
    }
    if (!Snapshot.ProceduralRidgeStrokes.IsEmpty())
    {
        Snapshot.EstimatedWorkingBytes += FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound();
    }
    Snapshot.EstimatedCommitBytes = sizeof(FWetWrinkleNormalMapBakeSnapshot::FImpl) +
        Snapshot.BaseSuffix.GetAllocatedSize() + Snapshot.BuildSignature.GetAllocatedSize();
    Snapshot.MemoryPlan.SnapshotBytes = Snapshot.EstimatedSnapshotBytes;
    Snapshot.MemoryPlan.RasterBytes = WorkingSurfaceBytes + FinalSurfaceBytes;
    Snapshot.MemoryPlan.PostProcessBytes = DilationWorkingBytes +
        (Snapshot.ProceduralRidgeStrokes.IsEmpty()
            ? 0
            : FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound());
    Snapshot.MemoryPlan.OutputBytes = Snapshot.EstimatedResultBytes;
    Snapshot.MemoryPlan.CommitMetadataBytes = Snapshot.EstimatedCommitBytes;
    Snapshot.bValid = true;
    OutErrorMessage.Reset();
    return true;
}

FWetWrinkleNormalMapComputedResult FWetWrinkleNormalMapBaker::ComputeSnapshot(
    const FWetWrinkleNormalMapBakeSnapshot& SnapshotHandle,
    const FDWCEditorCancellationToken* CancellationToken)
{
    FWetWrinkleNormalMapComputedResult Result;
    if (!SnapshotHandle.IsValid())
    {
        Result.Error = TEXT("The wrinkle bake snapshot is invalid.");
        return Result;
    }

    const FWetWrinkleNormalMapBakeSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    if (Snapshot.SurfaceProjectionAlgorithmVersion !=
            DWCEditorSurfacePatchProjectionVersion::SurfaceProjection ||
        Snapshot.ProjectedRasterAlgorithmVersion !=
            DWCEditorSurfacePatchProjectionVersion::ProjectedRaster)
    {
        Result.Error = TEXT("The wrinkle bake snapshot uses an obsolete surface projection contract.");
        return Result;
    }
    if (!Snapshot.PatchCommandInputs.IsEmpty() && !Snapshot.SurfacePatchProjectionCache.IsValid())
    {
        Result.Error = TEXT("The wrinkle bake snapshot lost its required surface projection cache.");
        return Result;
    }
    FDWCEditorNormalRasterSurface WorkingSurface;
    if (!WorkingSurface.Initialize(Snapshot.WorkingTextureSize, true))
    {
        Result.Error = TEXT("Failed to allocate the wrinkle raster surface.");
        return Result;
    }

    for (const FWetWrinkleBakePatchCommandInput& PatchCommandInput : Snapshot.PatchCommandInputs)
    {
        FDWCEditorRasterResult RasterResult;
        FString ProjectionError;
        const EDWCEditorSurfacePatchBoundaryPolicy ExpectedBoundaryPolicy =
            FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionSettings(
                PatchCommandInput.AuthoredMode,
                PatchCommandInput.RasterInput.Projection.ProjectionDepthLocal,
                PatchCommandInput.RasterInput.Projection.MaxSurfaceAngleDegrees,
                PatchCommandInput.RasterInput.Projection.ProjectionDepthSoftness,
                PatchCommandInput.RasterInput.Projection.ProjectionAngleSoftness)
                .BoundaryPolicy;
        const FDWCEditorSurfacePatchProjectionRequest& ProjectionRequest =
            PatchCommandInput.RasterInput.Projection;
        if (ProjectionRequest.BoundaryPolicy != ExpectedBoundaryPolicy ||
            PatchCommandInput.BoundaryPolicy != ExpectedBoundaryPolicy ||
            !FDWCEditorSurfacePatchProjector::ValidateProjectionContract(
                ProjectionRequest, &ProjectionError))
        {
            Result.Error = FString::Printf(
                TEXT("Wrinkle patch '%s' (%s) in slot %d has an invalid bake command contract. "
                     "AuthoredMode=%s, Boundary=%s. %s"),
                PatchCommandInput.DisplayName.IsEmpty()
                    ? TEXT("Unnamed Patch")
                    : *PatchCommandInput.DisplayName,
                *PatchCommandInput.PatchGuid.ToString(EGuidFormats::Digits),
                Snapshot.MaterialSlotIndex,
                GetAuthoredProjectionModeName(PatchCommandInput.AuthoredMode),
                GetBoundaryPolicyName(ProjectionRequest.BoundaryPolicy),
                ProjectionError.IsEmpty() ? TEXT("") : *ProjectionError);
            return Result;
        }
        FDWCEditorProjectedNormalPatchCommand Command;
        if (!FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            PatchCommandInput.RasterInput,
            Command,
            &ProjectionError,
            CancellationToken,
            Snapshot.SurfacePatchProjectionCache.Get(),
            EDWCEditorSurfacePatchCachePolicy::Persistent))
        {
            if (CancellationToken != nullptr && CancellationToken->IsCanceled())
            {
                Result.bCanceled = true;
                Result.Error = TEXT("The wrinkle bake was canceled.");
            }
            else
            {
                Result.Error = FString::Printf(
                    TEXT("Wrinkle patch '%s' (%s) in slot %d could not build its decal command. "
                         "AuthoredMode=%s, Boundary=%s, AnchorTriangle=%d. %s"),
                    PatchCommandInput.DisplayName.IsEmpty()
                        ? TEXT("Unnamed Patch")
                        : *PatchCommandInput.DisplayName,
                    *PatchCommandInput.PatchGuid.ToString(EGuidFormats::Digits),
                    Snapshot.MaterialSlotIndex,
                    GetAuthoredProjectionModeName(PatchCommandInput.AuthoredMode),
                    GetBoundaryPolicyName(ProjectionRequest.BoundaryPolicy),
                    ProjectionRequest.AnchorTriangleID,
                    ProjectionError.IsEmpty() ? TEXT("unknown surface projection error") : *ProjectionError);
            }
            return Result;
        }
        if (!Command.bUseSurfaceProjectionFilter)
        {
            Result.Error = FString::Printf(
                TEXT("Wrinkle patch '%s' produced a non-decal raster command."),
                PatchCommandInput.DisplayName.IsEmpty()
                    ? TEXT("Unnamed Patch")
                    : *PatchCommandInput.DisplayName);
            return Result;
        }
        RasterResult = FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
            Command, WorkingSurface, CancellationToken);
        if (RasterResult.bCanceled)
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The wrinkle bake was canceled.");
            return Result;
        }
        if (!RasterResult.bAffectedPixels)
        {
            Result.Error = FString::Printf(
                TEXT("Wrinkle patch '%s' (%s) projected successfully but affected no bake pixels."),
                PatchCommandInput.DisplayName.IsEmpty()
                    ? TEXT("Unnamed Patch")
                    : *PatchCommandInput.DisplayName,
                *PatchCommandInput.PatchGuid.ToString(EGuidFormats::Digits));
            return Result;
        }
        ++Result.BakedStampCount;
    }

    for (const FWetProceduralRidgeStroke& Stroke : Snapshot.ProceduralRidgeStrokes)
    {
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The wrinkle bake was canceled.");
            return Result;
        }
        const FWetProceduralRidgeRasterResult RasterResult =
            FWetProceduralRidgeRasterizer::RasterizeToSurface(Stroke, WorkingSurface, nullptr, CancellationToken);
        if (RasterResult.bCanceled)
        {
            Result.bCanceled = true;
            Result.Error = TEXT("The wrinkle bake was canceled.");
            return Result;
        }
        Result.BakedProceduralStrokeCount += RasterResult.bAffectedPixels ? 1 : 0;
    }

    if (Result.BakedStampCount == 0 && Result.BakedProceduralStrokeCount == 0)
    {
        Result.Error = TEXT("The selected material slot did not receive any wrinkle pixels during bake.");
        return Result;
    }

    FDWCEditorNormalRasterSurface FinalSurface;
    FDWCEditorNormalRasterSurface* FinalSurfaceToEncode = &WorkingSurface;
    if (Snapshot.WorkingTextureSize != Snapshot.FinalTextureSize)
    {
        if (!FDWCEditorRasterPostProcess::DownsampleNormalSurface(
                WorkingSurface,
                Snapshot.FinalTextureSize,
                FinalSurface))
        {
            Result.Error = TEXT("Failed to downsample the wrinkle normal bake buffer.");
            return Result;
        }
        FinalSurfaceToEncode = &FinalSurface;
        WorkingSurface = FDWCEditorNormalRasterSurface();
    }
    if (CancellationToken != nullptr && CancellationToken->IsCanceled())
    {
        Result.bCanceled = true;
        Result.Error = TEXT("The wrinkle bake was canceled.");
        return Result;
    }

    if (!Snapshot.IslandMask.IsValid())
    {
        Result.Error = TEXT("The wrinkle bake snapshot lost its island mask lease.");
        return Result;
    }
    FDWCEditorRasterPostProcess::ClipToMask(*FinalSurfaceToEncode, *Snapshot.IslandMask);
    FDWCEditorRasterPostProcess::DilateIntoPadding(
        *FinalSurfaceToEncode,
        *Snapshot.IslandMask,
        Snapshot.PaddingPixels);
    FDWCEditorRasterPostProcess::EncodeNormalPixels(*FinalSurfaceToEncode, Result.NormalPixels);
    FDWCEditorRasterPostProcess::EncodeCoveragePixels(*FinalSurfaceToEncode, Result.MaskPixels);
    Result.ResultBytes = Result.NormalPixels.GetAllocatedSize() + Result.MaskPixels.GetAllocatedSize();
    Result.bSucceeded = true;
    return Result;
}

bool FWetWrinkleNormalMapBaker::CommitComputedResult(
    UWetClothingAsset* WetClothingAsset,
    const FWetWrinkleNormalMapBakeSnapshot& SnapshotHandle,
    FWetWrinkleNormalMapComputedResult&& ComputedResult,
    FWetWrinkleNormalMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    check(IsInGameThread());
    OutResult = FWetWrinkleNormalMapBakeResult();
    if (WetClothingAsset == nullptr || !SnapshotHandle.IsValid())
    {
        OutErrorMessage = TEXT("The wrinkle bake target or snapshot is unavailable.");
        return false;
    }
    if (!ComputedResult.bSucceeded)
    {
        OutErrorMessage = ComputedResult.Error.IsEmpty()
            ? TEXT("The wrinkle bake calculation failed.")
            : MoveTemp(ComputedResult.Error);
        return false;
    }

    const FWetWrinkleNormalMapBakeSnapshot::FImpl& Snapshot = *SnapshotHandle.Impl;
    const int32 ExpectedPixelCount = Snapshot.FinalTextureSize.X * Snapshot.FinalTextureSize.Y;
    if (ComputedResult.NormalPixels.Num() != ExpectedPixelCount ||
        ComputedResult.MaskPixels.Num() != ExpectedPixelCount)
    {
        OutErrorMessage = TEXT("The wrinkle bake result has an unexpected pixel count.");
        return false;
    }

    FWetWrinkleBakedMapSet* ExistingBakedMap =
        WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.FindByPredicate(
            [&Snapshot](const FWetWrinkleBakedMapSet& ExistingMap)
            {
                return ExistingMap.MaterialSlotIndex == Snapshot.MaterialSlotIndex;
            });

    TArray<FDWCEditorArtifactTextureRequest> ArtifactRequests;
    ArtifactRequests.Reserve(2);
    FDWCEditorArtifactTextureRequest NormalRequest;
    if (!BuildWrinkleArtifactRequest(
            *WetClothingAsset,
            Snapshot.BaseSuffix + TEXT("_WrinkleNormalMap"),
            Snapshot.FinalTextureSize,
            TSF_BGRA8,
            ComputedResult.NormalPixels.GetData(),
            static_cast<uint64>(ComputedResult.NormalPixels.Num()) * sizeof(FColor),
            ExistingBakedMap != nullptr ? ExistingBakedMap->BakedWrinkleNormalMap.Get() : nullptr,
            true,
            NormalRequest,
            OutErrorMessage))
    {
        return false;
    }
    ArtifactRequests.Add(MoveTemp(NormalRequest));

    FDWCEditorArtifactTextureRequest MaskRequest;
    if (!BuildWrinkleArtifactRequest(
            *WetClothingAsset,
            Snapshot.BaseSuffix + TEXT("_WrinkleMask"),
            Snapshot.FinalTextureSize,
            TSF_G8,
            ComputedResult.MaskPixels.GetData(),
            static_cast<uint64>(ComputedResult.MaskPixels.Num()),
            ExistingBakedMap != nullptr ? ExistingBakedMap->BakedWrinkleMask.Get() : nullptr,
            false,
            MaskRequest,
            OutErrorMessage))
    {
        return false;
    }
    ArtifactRequests.Add(MoveTemp(MaskRequest));

    TArray<FDWCEditorArtifactCommitReceipt> ArtifactReceipts;
    if (!FDWCEditorArtifactStore::Get()->CommitTextureBatch(
            ArtifactRequests, ArtifactReceipts, OutErrorMessage) ||
        ArtifactReceipts.Num() != 2)
    {
        return false;
    }
    UTexture2D* NormalTexture = ArtifactReceipts[0].Texture;
    UTexture2D* MaskTexture = ArtifactReceipts[1].Texture;
    FWetClothingTextureReadbackUtils::InvalidateTexture(NormalTexture);
    FWetClothingTextureReadbackUtils::InvalidateTexture(MaskTexture);
    ComputedResult.NormalPixels.Reset();
    ComputedResult.MaskPixels.Reset();
    WetClothingAsset->Modify();
    FWetWrinkleBakedMapSet* BakedMap = ExistingBakedMap;
    if (BakedMap == nullptr)
    {
        BakedMap = &WetClothingAsset->Authored.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    }

    BakedMap->MaterialSlotIndex = Snapshot.MaterialSlotIndex;
    BakedMap->BakedWrinkleNormalMap = NormalTexture;
    BakedMap->BakedWrinkleMask = MaskTexture;
    BakedMap->Resolution = Snapshot.FinalTextureSize.X;
    BakedMap->PaddingPixels = Snapshot.PaddingPixels;
    BakedMap->BuildSignature = Snapshot.BuildSignature;
    BakedMap->BakeGuid = FGuid::NewGuid();

    bool bInvalidatedCurrentTransparencyOutput = false;
    for (FWetClothingTransparencyLayerData& TransparencyLayer :
         WetClothingAsset->Authored.TransparencyData.TransparencyLayers)
    {
        if (TransparencyLayer.TargetSurface.OuterMaterialSlotIndex == Snapshot.MaterialSlotIndex)
        {
            const FWetClothingBakedTransparencyMap* CurrentTransparency =
                WetClothingAsset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(
                    Snapshot.MaterialSlotIndex);
            if (CurrentTransparency != nullptr)
            {
                FWetWrinkleInvalidatedTransparencyOutput& Invalidated =
                    OutResult.InvalidatedTransparencyOutputs.AddDefaulted_GetRef();
                Invalidated.MaterialSlotIndex = Snapshot.MaterialSlotIndex;
                Invalidated.TransparencyTextureName = GetPathNameSafe(CurrentTransparency->TransparencyMap);
                if (const USkeletalMesh* Mesh = WetClothingAsset->GetDWCSkeletalMesh();
                    Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(Snapshot.MaterialSlotIndex))
                {
                    Invalidated.MaterialSlotName =
                        Mesh->GetMaterials()[Snapshot.MaterialSlotIndex].MaterialSlotName.ToString();
                }
                bInvalidatedCurrentTransparencyOutput = true;
            }
            TransparencyLayer.MarkFinalBakeStale();
        }
    }
    if (bInvalidatedCurrentTransparencyOutput)
    {
        WetClothingAsset->SetTransparencyBakeStatus(EDWCBakeStatus::OutOfDate);
    }
    WetClothingAsset->MarkPackageDirty();

    OutResult.BakedMapCount = 1;
    OutResult.BakedStampCount = ComputedResult.BakedStampCount;
    OutResult.BakedProceduralStrokeCount = ComputedResult.BakedProceduralStrokeCount;
    OutResult.BakedNormalMaps.Add(NormalTexture);
    OutResult.BakedMasks.Add(MaskTexture);

    OutErrorMessage.Reset();
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
    Canonical += FString::Printf(
        TEXT("DWC.WrinkleNormalMap.v25.BoundaryAwareDecal")
        TEXT("|SurfaceProjection=%u|ProjectedRaster=%u|"),
        DWCEditorSurfacePatchProjectionVersion::SurfaceProjection,
        DWCEditorSurfacePatchProjectionVersion::ProjectedRaster);
    const FDWCDataUVLODMetadata* DataUVMetadata =
        WetClothingAsset.FindDataUVMetadataForLOD(Group.LODIndex);
    Canonical += FString::Printf(
        TEXT("|Slot=%d|UV=%d|LOD=%d|Size=%dx%d|Internal=%d|Padding=%d|SourceMeshSignature=%s|DataUVSignature=%s|DataUVGenerator=%d"),
        Group.MaterialSlotIndex,
        Group.UVChannelIndex,
        Group.LODIndex,
        Width,
        Height,
        FMath::Max(WetWrinkleTextureRaster::InternalBakeResolution, FMath::Max(Width, Height)),
        FMath::Clamp(Settings.PaddingPixels, 0, 64),
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
        const FDWCEditorSurfacePatchProjectionSettings ProjectionSettings =
            FDWCEditorWrinklePatchDescriptorBuilder::BuildProjectionSettings(
                Stamp->ProjectionMode,
                Stamp->ProjectionDepthLocal,
                Stamp->MaxSurfaceAngleDegrees,
                Stamp->ProjectionDepthSoftness,
                Stamp->ProjectionAngleSoftness);
        Canonical += FString::Printf(
            TEXT("|Stamp:%s;Mode=%d;CoreMode=%d;Boundary=%d;Depth=%.9g;Angle=%.9g;DepthSoft=%.9g;AngleSoft=%.9g;HasAnchor=%d;Anchor=%d,%.9g,%.9g,%.9g;HasFrame=%d;FrameU=%.9g,%.9g,%.9g;FrameV=%.9g,%.9g,%.9g;HasFootprint=%d;HalfExtent=%.9g,%.9g;Rot=%.9g;Scale=%.9g,%.9g;Strength=%.9g;Falloff=%.9g;NormalSource=%s"),
            *Stamp->PatchGuid.ToString(EGuidFormats::Digits),
            static_cast<int32>(Stamp->ProjectionMode),
            DWCEditorSurfacePatchProjectionVersion::SurfaceDecalSignatureId,
            static_cast<int32>(ProjectionSettings.BoundaryPolicy),
            Stamp->ProjectionDepthLocal,
            Stamp->MaxSurfaceAngleDegrees,
            Stamp->ProjectionDepthSoftness,
            Stamp->ProjectionAngleSoftness,
            Stamp->bHasSurfaceAnchor ? 1 : 0,
            Stamp->AnchorTriangleID,
            Stamp->AnchorBarycentric.X,
            Stamp->AnchorBarycentric.Y,
            Stamp->AnchorBarycentric.Z,
            Stamp->bHasSurfaceFrame ? 1 : 0,
            Stamp->SurfaceFrameU.X,
            Stamp->SurfaceFrameU.Y,
            Stamp->SurfaceFrameU.Z,
            Stamp->SurfaceFrameV.X,
            Stamp->SurfaceFrameV.Y,
            Stamp->SurfaceFrameV.Z,
            Stamp->bHasSurfaceFootprint ? 1 : 0,
            Stamp->SurfaceHalfExtentLocal.X,
            Stamp->SurfaceHalfExtentLocal.Y,
            Stamp->RotationRadians,
            Stamp->Scale.X,
            Stamp->Scale.Y,
            Stamp->Strength,
            Stamp->Falloff,
            NormalTexture != nullptr ? *NormalTexture->Source.GetId().ToString(EGuidFormats::Digits) : TEXT(""));
    }

    for (const FWetProceduralRidgeStroke* Stroke : Group.ProceduralRidgeStrokes)
    {
        if (Stroke == nullptr)
        {
            continue;
        }

        Canonical += FString::Printf(
            TEXT("|Ridge:%s;Enabled=%d;Slot=%d;Shape=%d;FlipFold=%d;Width=%.9g;Strength=%.9g;Falloff=%.9g;StartTaper=%.9g;EndTaper=%.9g;Flare=%.9g,%.9g,%.9g,%.9g;Variation=%d,%.9g,%.9g,%.9g,%.9g,%d;StartMode=%d;StartLink=%s,%d,%.9g;EndMode=%d;EndLink=%s,%d,%.9g;Points=%d"),
            *Stroke->StrokeGuid.ToString(EGuidFormats::Digits),
            Stroke->bEnabled ? 1 : 0,
            Stroke->MaterialSlotIndex,
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
