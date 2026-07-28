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

/** Runtime render resources required by a receiver backend. */
enum class EDWCRenderResourceUsage : uint8
{
    AbsorbedOnly,
    FullGPU
};
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
    FIntPoint SurfaceLookupExtent = FIntPoint::ZeroValue;
    uint32 SurfaceTexelCount = 0;
    uint32 TriangleCount = 0;
    uint32 SeamDestinationCount = 0;
    uint32 SeamIncomingCount = 0;

    TRefCountPtr<FRDGPooledBuffer> TriangleProfileIndices;
    TRefCountPtr<FRDGPooledBuffer> TriangleDataToSurfaceWaterNormalUV;
    TArray<FDWCGPUStaticSectionResources> Sections;

    TRefCountPtr<FRDGPooledBuffer> TexelLookup;
    TRefCountPtr<FRDGPooledBuffer> SurfaceTexelLookup;
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
    uint64 WetPartDataRemapGPUBytes = 0;
    uint64 SurfaceNormalArrayGPUBytes = 0;

    uint64 GetGPUBytes() const
    {
        return StaticBufferGPUBytes +
               RenderProfileLUTGPUBytes +
               WetPartDataRemapGPUBytes +
               SurfaceNormalArrayGPUBytes;
    }
};

USTRUCT()
struct DWC_API FDWCAssetRenderProfileResources
{
    GENERATED_BODY()

    /** Slot-local Wet Part data textures sampled with each slot's DWC Data UV. */
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UTexture2D>> WetPartDataTexturesByMaterialSlot;

    /** WCA-wide Local Profile ID -> global runtime profile row mapping. */
    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> ProfileRemapLUT = nullptr;

    /** Slot fallback render profiles resolved with the asset resource cache, not every material apply. */
    UPROPERTY(Transient)
    TMap<int32, FWetClothingLocalRenderProfile> FallbackRenderProfilesByMaterialSlot;

    /** Bake identity used to invalidate this WCA's slot texture/remap cache after an editor rebake. */
    FGuid SourceBakeGuid;

    /** Resolved profile parameter identity used to invalidate remaps when source Wetness Profiles change. */
    FString ResolvedProfileSignature;

    int32 RegistryRevision = INDEX_NONE;
    bool bSurfaceResourcesResolved = false;

    bool IsValid() const
    {
        return !WetPartDataTexturesByMaterialSlot.IsEmpty() && ProfileRemapLUT != nullptr;
    }


    UTexture2D* FindWetPartDataTexture(const int32 MaterialSlotIndex) const
    {
        if (const TObjectPtr<UTexture2D>* Found = WetPartDataTexturesByMaterialSlot.Find(MaterialSlotIndex))
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
    static constexpr int32 TexelsPerProfile = 3;
    static constexpr int32 GlobalLUTWidth = MaxRuntimeProfileCount * TexelsPerProfile;
    static constexpr int32 LocalRemapWidth = 256;

    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Deinitialize() override;

    const FDWCAssetRenderProfileResources* AcquireAssetResources(
        UWetClothingAsset* Asset,
        EDWCRenderResourceUsage Usage);

    TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe> AcquireStaticSlotResources(
        const UWetClothingAsset* Asset,
        int32 MaterialSlotIndex,
        const FString& BuildSignature,
        FIntPoint LookupExtent,
        uint32 TexelCount,
        FIntPoint SurfaceLookupExtent,
        uint32 SurfaceTexelCount,
        uint32 TriangleCount,
        int32 SectionCount);

    void InvalidateAssetResources(const UWetClothingAsset* Asset);
    void InvalidateStaticResources(const UWetClothingAsset* Asset);

    void ApplyResourcesToMaterials(
        UWetClothingAsset* Asset,
        const TArray<TObjectPtr<UMaterialInstanceDynamic>>& MaterialInstances,
        EDWCRenderResourceUsage Usage);

    bool ApplyPreviewRenderProfileFallback(
        UWetClothingAsset* Asset,
        int32 MaterialSlotIndex,
        int32 LocalProfileID,
        UMaterialInstanceDynamic& MID);

    bool ApplyPreviewRenderProfileFallbackProfile(
        const UWetClothingAsset* Asset,
        int32 MaterialSlotIndex,
        const FWetClothingLocalRenderProfile& LocalProfile,
        UMaterialInstanceDynamic& MID);

    UTexture2D* GetGlobalRenderProfileLUT() const { return GlobalRenderProfileLUT; }
    UTexture2DArray* GetDropletNormalArray() const { return DropletNormalArray; }
    UTexture2DArray* GetDropletMaskArray() const { return DropletMaskArray; }
    int32 GetRegistryRevision() const { return RegistryRevision; }
    FDWCGPUResourceSubsystemStats GetStats() const;

private:
    struct FRuntimeProfileRecord
    {
        FString StableKey;
        TArray<FLinearColor> PackedTexels;
        bool bSurfaceResourcesResolved = false;
    };

    struct FTextureArrayRegistry
    {
        TArray<TObjectPtr<UTexture2D>> SourceTextures;
        TMap<FString, int32> SliceByPath;
        int32 SizeX = 0;
        int32 SizeY = 0;
        int32 PixelFormat = INDEX_NONE;
        int32 AllocatedCapacity = 0;
        TSet<int32> DirtySlices;

        void ReserveNeutralSlice(bool& bOutChanged);
        void SetNeutral(UTexture2D* Texture, bool& bOutChanged);
        int32 FindOrAdd(UTexture2D* Texture, bool& bOutChanged);
        void Reset();
    };

    int32 FindOrAddRuntimeProfile(
        const struct FWetClothingLocalRenderProfile& LocalProfile,
        EDWCRenderResourceUsage Usage);

    void EnsureNeutralResources();
    void EnsureMaskRegistryNeutral(
        FTextureArrayRegistry& Registry,
        UTexture2D* ReferenceTexture,
        bool& bOutChanged);
    void RebuildGlobalRenderProfileLUT();
    void UpdateGlobalRenderProfileLUT(int32 RuntimeProfileIndex);
    void FlushDirtyRuntimeProfiles();
    void RebindGlobalRenderProfileLUT();
    bool EnsureTextureArraysUpToDate();
    bool EnsureTextureArray(
        const TCHAR* DebugName,
        FTextureArrayRegistry& Registry,
        TObjectPtr<UTexture2DArray>& Array,
        bool bNormalArray);
    void UploadTextureArraySlices(
        UTexture2DArray* Array,
        const FTextureArrayRegistry& Registry,
        const TSet<int32>& SliceIndices);
    void RebindGPUTextureArrays();
    UTexture2D* BuildAssetRemapLUT(
        UWetClothingAsset* Asset,
        const TArray<int32>& LocalToRuntimeProfileIndices);

    void ApplyFallbackRenderProfileParameters(
        UMaterialInstanceDynamic& MID,
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        const FWetClothingLocalRenderProfile* CachedProfile,
        EDWCRenderResourceUsage Usage);

    UTexture2DArray* BuildTextureArray(
        const TCHAR* DebugName,
        const TArray<TObjectPtr<UTexture2D>>& SourceTextures,
        int32 SliceCapacity,
        bool bNormalArray);

    void BindGlobalResources(UMaterialInstanceDynamic& MID, EDWCRenderResourceUsage Usage) const;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> NeutralWetPartDataTexture = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> NeutralProfileRemapLUT = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2D> GlobalRenderProfileLUT = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DArray> DropletMaskArray = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DArray> DropletNormalArray = nullptr;

    UPROPERTY(Transient)
    TMap<TObjectPtr<UWetClothingAsset>, FDWCAssetRenderProfileResources> AssetResources;

    TMap<FDWCGPUStaticResourceKey, TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>> StaticSlotResources;

    TArray<FRuntimeProfileRecord> RuntimeProfiles;
    TMap<FString, int32> RuntimeProfileIndexByKey;
    FTextureArrayRegistry DropletMaskRegistry;
    FTextureArrayRegistry DropletNormalRegistry;
    TSet<int32> DirtyRuntimeProfileIndices;
    TSet<TWeakObjectPtr<UMaterialInstanceDynamic>> RegisteredMaterialInstances;
    TSet<TWeakObjectPtr<UMaterialInstanceDynamic>> GPUMaterialInstances;
    bool bTextureArraysDirty = false;
    int32 RegistryRevision = 0;
};
