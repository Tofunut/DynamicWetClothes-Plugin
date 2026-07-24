#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"
#include "Subsystems/WorldSubsystem.h"
#include "RenderGraphResources.h"
#include "UObject/ObjectKey.h"
#include "DWCGPUResourceSubsystem.generated.h"

class UMaterialInstanceDynamic;
class UTexture2D;
class UTexture2DArray;
/** Cache identity for immutable GPU lookup resources baked from one WCA material slot. */
struct DWC_API FDWCGPUStaticResourceKey
{
    FObjectKey AssetKey;
    int32 MaterialSlotIndex = INDEX_NONE;
    FString BuildSignature;

    FDWCGPUStaticResourceKey() = default;
    FDWCGPUStaticResourceKey(
        const UWetClothingAsset* Asset,
        const int32 InMaterialSlotIndex,
        FString InBuildSignature)
        : AssetKey(const_cast<UWetClothingAsset*>(Asset))
        , MaterialSlotIndex(InMaterialSlotIndex)
        , BuildSignature(MoveTemp(InBuildSignature))
    {
    }

    friend bool operator==(const FDWCGPUStaticResourceKey& A, const FDWCGPUStaticResourceKey& B)
    {
        return A.AssetKey == B.AssetKey &&
               A.MaterialSlotIndex == B.MaterialSlotIndex &&
               A.BuildSignature == B.BuildSignature;
    }

    friend uint32 GetTypeHash(const FDWCGPUStaticResourceKey& Key)
    {
        return HashCombine(
            HashCombine(GetTypeHash(Key.AssetKey), GetTypeHash(Key.MaterialSlotIndex)),
            GetTypeHash(Key.BuildSignature));
    }
};


struct DWC_API FDWCGPUStaticSectionResources
{
    uint32 TriangleCount = 0;
    TRefCountPtr<FRDGPooledBuffer> TriangleIndices;
    TRefCountPtr<FRDGPooledBuffer> TriangleUV01;
    TRefCountPtr<FRDGPooledBuffer> TriangleUV2RestArea;
};

/**
 * Immutable pooled buffers shared by every GPU backend using the same WCA, slot and bake signature.
 * Buffer extraction is performed lazily by the first render graph that consumes the resource.
 */
struct DWC_API FDWCGPUStaticSlotResources
{
    FDWCGPUStaticResourceKey Key;
    FIntPoint LookupExtent = FIntPoint::ZeroValue;
    uint32 TexelCount = 0;
    uint32 TriangleCount = 0;
    uint32 SeamDestinationCount = 0;
    uint32 SeamIncomingCount = 0;

    TRefCountPtr<FRDGPooledBuffer> TriangleProfileIndices;
    TRefCountPtr<FRDGPooledBuffer> TriangleDataToSurfaceWaterNormalUV;
    TArray<FDWCGPUStaticSectionResources> Sections;

    TRefCountPtr<FRDGPooledBuffer> TexelLookup;
    TRefCountPtr<FRDGPooledBuffer> SeamDestinations;
    TRefCountPtr<FRDGPooledBuffer> SeamIncoming;
};

struct DWC_API FDWCGPUResourceSubsystemStats
{
    uint32 AssetResourceCount = 0;
    uint32 StaticSlotResourceCount = 0;
    uint32 RuntimeProfileCount = 0;
    uint32 TextureArrayCount = 0;
    uint64 CPUBytes = 0;
    uint64 StaticBufferGPUBytes = 0;
    uint64 RenderProfileLUTGPUBytes = 0;
    uint64 ProfileIDRemapGPUBytes = 0;
    uint64 SurfaceNormalArrayGPUBytes = 0;

    uint64 GetGPUBytes() const
    {
        return StaticBufferGPUBytes +
               RenderProfileLUTGPUBytes +
               ProfileIDRemapGPUBytes +
               SurfaceNormalArrayGPUBytes;
    }
};

USTRUCT()
struct DWC_API FDWCAssetRenderProfileResources
{
    GENERATED_BODY()

    /** Slot-local Profile ID textures. Every texture is sampled with that slot's DWC Data UV. */
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UTexture2D>> ProfileIDTexturesByMaterialSlot;

    /** WCA-wide Local Profile ID -> global runtime profile row mapping. */
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> ProfileRemapLUT = nullptr;

    /** Bake identity used to invalidate this WCA's slot texture/remap cache after an editor rebake. */
    FGuid SourceBakeGuid;

    int32 RegistryRevision = INDEX_NONE;

    bool IsValid() const
    {
        return !ProfileIDTexturesByMaterialSlot.IsEmpty() && ProfileRemapLUT != nullptr;
    }


    UTexture2D* FindProfileIDTexture(const int32 MaterialSlotIndex) const
    {
        if (const TObjectPtr<UTexture2D>* Found = ProfileIDTexturesByMaterialSlot.Find(MaterialSlotIndex))
        {
            return Found->Get();
        }
        return nullptr;
    }
};

/**
 * World-local registry for all immutable/shared DWC GPU resources.
 *
 * It owns render-profile LUT/Texture2DArray resources and a static simulation
 * cache keyed by WCA + material slot + map build signature. Per-character
 * mutable resources such as scaled profile parameters, TriangleFlow,
 * TriangleMetric and wetness RTs stay in FDWCGPUBackend.
 */
UCLASS()
class DWC_API UDWCGPUResourceSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    static constexpr int32 MaxRuntimeProfileCount = 255;
    static constexpr int32 TexelsPerProfile = 2;
    static constexpr int32 GlobalLUTWidth = MaxRuntimeProfileCount * TexelsPerProfile;
    static constexpr int32 LocalRemapWidth = 256;

    virtual void Deinitialize() override;

    const FDWCAssetRenderProfileResources* AcquireAssetResources(UWetClothingAsset* Asset);

    TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe> AcquireStaticSlotResources(
        const UWetClothingAsset* Asset,
        int32 MaterialSlotIndex,
        const FString& BuildSignature,
        FIntPoint LookupExtent,
        uint32 TexelCount,
        uint32 TriangleCount,
        int32 SectionCount);

    void InvalidateStaticResources(const UWetClothingAsset* Asset);

    void ApplyResourcesToMaterials(
        UWetClothingAsset* Asset,
        const TArray<TObjectPtr<UMaterialInstanceDynamic>>& MaterialInstances);

    UTexture2D* GetGlobalRenderProfileLUT() const { return GlobalRenderProfileLUT; }
    UTexture2DArray* GetDropletNormalArray() const { return DropletNormalArray; }
    UTexture2DArray* GetRivuletNormalArray() const { return RivuletNormalArray; }
    int32 GetRegistryRevision() const { return RegistryRevision; }
    FDWCGPUResourceSubsystemStats GetStats() const;

private:
    struct FRuntimeProfileRecord
    {
        FString StableKey;
        TArray<FLinearColor> PackedTexels;
    };

    struct FTextureArrayRegistry
    {
        TArray<TObjectPtr<UTexture2D>> SourceTextures;
        TMap<FString, int32> SliceByPath;
        int32 SizeX = 0;
        int32 SizeY = 0;
        int32 PixelFormat = INDEX_NONE;

        void SetNeutral(UTexture2D* Texture, bool& bOutChanged);
        int32 FindOrAdd(UTexture2D* Texture, bool& bOutChanged);
        void Reset();
    };

    int32 FindOrAddRuntimeProfile(
        const struct FWetClothingLocalRenderProfile& LocalProfile,
        bool& bOutChanged);

    void EnsureNeutralResources();
    void RebuildGlobalRenderProfileLUT();
    void RebuildTextureArrays();
    UTexture2D* BuildAssetRemapLUT(
        UWetClothingAsset* Asset,
        const TArray<int32>& LocalToRuntimeProfileIndices);

    void ApplyFallbackRenderProfileParameters(
        UMaterialInstanceDynamic& MID,
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);

    UTexture2DArray* BuildTextureArray(
        const TCHAR* DebugName,
        const TArray<TObjectPtr<UTexture2D>>& SourceTextures,
        bool bNormalArray);

    void BindGlobalResources(UMaterialInstanceDynamic& MID) const;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> NeutralProfileIDTexture = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> NeutralProfileRemapLUT = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> GlobalRenderProfileLUT = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DArray> DropletNormalArray = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DArray> RivuletNormalArray = nullptr;

    UPROPERTY(Transient)
    TMap<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources> AssetResources;

    TMap<FDWCGPUStaticResourceKey, TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>> StaticSlotResources;

    TArray<FRuntimeProfileRecord> RuntimeProfiles;
    TMap<FString, int32> RuntimeProfileIndexByKey;
    FTextureArrayRegistry DropletNormalRegistry;
    FTextureArrayRegistry RivuletNormalRegistry;
    bool bTextureArraysDirty = false;
    int32 RegistryRevision = 0;
};
