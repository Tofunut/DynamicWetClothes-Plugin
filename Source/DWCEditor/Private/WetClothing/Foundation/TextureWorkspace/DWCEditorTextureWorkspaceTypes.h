#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "PixelFormat.h"
#include "RenderCommandFence.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class UTexture2D;
class FDWCEditorTextureWorkspace;

enum class EDWCEditorTexturePurpose : uint8
{
    WrinkleAccumulated,
    WrinkleProcedural,
    TransparencyVisualization,
    TransparencyWrinkleSuppression
};

enum class EDWCEditorTextureUploadPriority : uint8
{
    Background,
    Normal,
    Interactive
};

/** GPU residency for a transient workspace texture. CPU pixels may outlive this resource. */
enum class EDWCEditorTextureGPUState : uint8
{
    CPUOnly,
    Resident,
    Retiring
};

struct FDWCEditorTextureKey
{
    FObjectKey Owner;
    EDWCEditorTexturePurpose Purpose = EDWCEditorTexturePurpose::WrinkleAccumulated;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;

    bool operator==(const FDWCEditorTextureKey& Other) const
    {
        return Owner == Other.Owner &&
            Purpose == Other.Purpose &&
            MaterialSlotIndex == Other.MaterialSlotIndex &&
            LayerGuid == Other.LayerGuid;
    }

    friend uint32 GetTypeHash(const FDWCEditorTextureKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.Owner);
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.Purpose)));
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.LayerGuid));
    }
};

struct FDWCEditorTextureDescriptor
{
    FIntPoint Size = FIntPoint::ZeroValue;
    FIntPoint WorkingSize = FIntPoint::ZeroValue;
    EPixelFormat PixelFormat = PF_B8G8R8A8;
    bool bSRGB = false;
    TextureCompressionSettings CompressionSettings = TC_Default;
    TextureMipGenSettings MipGenSettings = TMGS_NoMipmaps;
    TextureFilter Filter = TF_Bilinear;
    TextureAddress AddressX = TA_Clamp;
    TextureAddress AddressY = TA_Clamp;
    TextureGroup LODGroup = TEXTUREGROUP_World;
    FColor InitialBGRA8 = FColor::Black;
    uint8 InitialG8 = 0;

    bool IsValid() const
    {
        return Size.X > 0 && Size.Y > 0 &&
            (PixelFormat == PF_B8G8R8A8 || PixelFormat == PF_G8);
    }

    int32 GetBytesPerPixel() const
    {
        return PixelFormat == PF_G8 ? 1 : static_cast<int32>(sizeof(FColor));
    }

    bool operator==(const FDWCEditorTextureDescriptor& Other) const
    {
        return Size == Other.Size &&
            WorkingSize == Other.WorkingSize &&
            PixelFormat == Other.PixelFormat &&
            bSRGB == Other.bSRGB &&
            CompressionSettings == Other.CompressionSettings &&
            MipGenSettings == Other.MipGenSettings &&
            Filter == Other.Filter &&
            AddressX == Other.AddressX &&
            AddressY == Other.AddressY &&
            LODGroup == Other.LODGroup &&
            InitialBGRA8 == Other.InitialBGRA8 &&
            InitialG8 == Other.InitialG8;
    }
};

/** Mutable game-thread texture state. Workers only publish completed arrays into this entry. */
class FDWCEditorTextureWorkspaceEntry final
{
  public:
    const FDWCEditorTextureKey& GetKey() const { return Key; }
    const FDWCEditorTextureDescriptor& GetDescriptor() const { return Descriptor; }
    UTexture2D* GetTexture() const { return Texture.Get(); }
    uint64 GetContentRevision() const { return ContentRevision; }
    uint64 GetResourceGeneration() const { return ResourceGeneration; }
    uint64 GetLastUsedSerial() const { return LastUsedSerial; }
    uint32 GetActiveLeaseCount() const { return ActiveLeaseCount; }
    EDWCEditorTextureGPUState GetGPUState() const { return GPUState; }
    bool IsGPUResident() const { return GPUState == EDWCEditorTextureGPUState::Resident && Texture != nullptr; }
    bool IsGPURetiring() const { return GPUState == EDWCEditorTextureGPUState::Retiring; }
    bool CanAcceptUploads() const { return IsGPUResident(); }

    TArray<FColor>& GetMutableBGRA8Pixels() { return BGRA8Pixels; }
    const TArray<FColor>& GetBGRA8Pixels() const { return BGRA8Pixels; }
    TArray<uint8>& GetMutableG8Pixels() { return G8Pixels; }
    const TArray<uint8>& GetG8Pixels() const { return G8Pixels; }
    FDWCEditorNormalRasterSurface& GetMutableWorkingNormalSurface() { return WorkingNormalSurface; }
    const FDWCEditorNormalRasterSurface& GetWorkingNormalSurface() const { return WorkingNormalSurface; }

    const uint8* GetPixelData() const
    {
        return Descriptor.PixelFormat == PF_G8
            ? G8Pixels.GetData()
            : reinterpret_cast<const uint8*>(BGRA8Pixels.GetData());
    }

    int64 GetPixelDataBytes() const
    {
        return Descriptor.PixelFormat == PF_G8
            ? static_cast<int64>(G8Pixels.Num())
            : static_cast<int64>(BGRA8Pixels.Num()) * sizeof(FColor);
    }

    uint64 GetAllocatedSizeBytes() const
    {
        return static_cast<uint64>(BGRA8Pixels.GetAllocatedSize()) +
            static_cast<uint64>(G8Pixels.GetAllocatedSize()) +
            WorkingNormalSurface.GetAllocatedSizeBytes();
    }

    uint64 GetEstimatedGPUBytes() const
    {
        return (GPUState == EDWCEditorTextureGPUState::Resident ||
                GPUState == EDWCEditorTextureGPUState::Retiring)
            ? static_cast<uint64>(Descriptor.Size.X) * static_cast<uint64>(Descriptor.Size.Y) *
                static_cast<uint64>(Descriptor.GetBytesPerPixel())
            : 0;
    }

  private:
    friend class FDWCEditorTextureWorkspace;

    FDWCEditorTextureKey Key;
    FDWCEditorTextureDescriptor Descriptor;
    TObjectPtr<UTexture2D> Texture = nullptr;
    TArray<FColor> BGRA8Pixels;
    TArray<uint8> G8Pixels;
    FDWCEditorNormalRasterSurface WorkingNormalSurface;
    uint64 ContentRevision = 0;
    uint64 ResourceGeneration = 0;
    uint64 LastUsedSerial = 0;
    uint32 ActiveLeaseCount = 0;
    EDWCEditorTextureGPUState GPUState = EDWCEditorTextureGPUState::CPUOnly;
    TUniquePtr<FRenderCommandFence> GPUReleaseFence;
    bool bRetired = false;
};

using FDWCEditorTextureHandle = TSharedPtr<FDWCEditorTextureWorkspaceEntry>;

/**
 * Game-thread ownership token for a mutable preview texture entry.
 *
 * A handle only keeps an entry alive incidentally. A lease records that the
 * entry is actively bound by a viewport, so the workspace can distinguish an
 * active working buffer from an LRU-retained cache entry.
 */
class FDWCEditorTextureLeaseState final
{
  public:
    bool bAcceptReleases = true;
    TFunction<void(const FDWCEditorTextureHandle&, uint64)> ReleaseCallback;
};

class FDWCEditorTextureLease final
{
  public:
    FDWCEditorTextureLease() = default;
    ~FDWCEditorTextureLease() { Reset(); }

    FDWCEditorTextureLease(const FDWCEditorTextureLease&) = delete;
    FDWCEditorTextureLease& operator=(const FDWCEditorTextureLease&) = delete;

    FDWCEditorTextureLease(FDWCEditorTextureLease&& Other) noexcept;
    FDWCEditorTextureLease& operator=(FDWCEditorTextureLease&& Other) noexcept;

    bool IsValid() const { return Entry.IsValid(); }
    explicit operator bool() const { return IsValid(); }
    FDWCEditorTextureWorkspaceEntry* Get() const { return Entry.Get(); }
    FDWCEditorTextureWorkspaceEntry* operator->() const { return Entry.Get(); }
    const FDWCEditorTextureHandle& GetHandle() const { return Entry; }
    void Reset();

  private:
    friend class FDWCEditorTextureWorkspace;

    TWeakPtr<FDWCEditorTextureLeaseState> State;
    FDWCEditorTextureHandle Entry;
    uint64 LeaseId = 0;
};

/** Small deterministic region set shared by all preview upload paths. */
class FDWCEditorDirtyRegionSet final
{
  public:
    static constexpr int32 MaxRegions = 8;

    void Add(const FIntRect& DirtyRect, const FIntPoint& TextureSize, bool bWrap);
    void Reset() { Regions.Reset(); }
    bool IsEmpty() const { return Regions.IsEmpty(); }
    const TArray<FIntRect, TInlineAllocator<MaxRegions>>& GetRegions() const { return Regions; }
    uint64 GetArea() const;

  private:
    void AddClamped(const FIntRect& DirtyRect, const FIntPoint& TextureSize);
    void ReduceRegionCount();

    TArray<FIntRect, TInlineAllocator<MaxRegions>> Regions;
};
