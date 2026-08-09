//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture.h"
#include "PixelFormat.h"
#include "RenderCommandFence.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

class UTexture2D;
class FDWCEditorTextureWorkspace;
class FDWCEditorTextureWorkspaceEntry;

enum class EDWCEditorTexturePurpose : uint8
{
    WrinkleAccumulated,
    WrinkleProcedural,
    WrinkleHover,
    TransparencyVisualization,
    TransparencyHoverBaseline,
    TransparencyHoverIslandMask
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

class FDWCEditorTextureUploadTelemetryState;
class FDWCEditorTextureUploadState;

/**
 * Non-blocking timing snapshot for one texture content revision.
 *
 * RenderEnqueued means that all UpdateTextureRegions calls were issued. It
 * does not imply that the render thread finished consuming the update.
 */
struct FDWCEditorTextureUploadTiming
{
    double QueuedSeconds = 0.0;
    double SelectedSeconds = 0.0;
    double SubmittedSeconds = 0.0;
    double RenderCallbackSeconds = 0.0;
    double ObservedSeconds = 0.0;
    double QueueWaitMs = 0.0;
    double SliceDelayMs = 0.0;
    double StagingCopyMs = 0.0;
    double SubmitCallMs = 0.0;
    double SubmittedToObservedMs = 0.0;
    double RenderCallbackLatencyMs = 0.0;
    uint64 PreparedPayloadBytes = 0;
    uint64 AvoidedStagingCopyBytes = 0;
    uint64 RequestedPixels = 0;
    uint64 SubmittedBytes = 0;
    uint32 RequestedRegionCount = 0;
    uint32 SubmittedRegionCount = 0;
    uint32 CompletedRegionCount = 0;
    uint32 CoalescedRequestCount = 0;
    uint32 QueueDepthAtSelection = 0;
    EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal;
    bool bFullTextureUpload = false;
    bool bStale = false;
    bool bCanceled = false;
    bool bUsedPreparedPayload = false;

    bool WasSelected() const { return SelectedSeconds > 0.0; }
    bool WasSubmitted() const { return SubmittedSeconds > 0.0; }
    bool WasObserved() const { return ObservedSeconds > 0.0; }
};

/** Tightly packed immutable BGRA8 region whose storage may be adopted by the render upload queue. */
struct FDWCEditorPreparedBGRA8Region
{
    FIntRect Rect;
    TArray<FColor> Pixels;
};

/** Identifies one CPU content revision waiting to be submitted to its GPU texture. */
struct FDWCEditorTextureUploadTicket
{
    FDWCEditorTextureKey Key;
    TWeakPtr<FDWCEditorTextureWorkspaceEntry> Entry;
    uint64 ResourceGeneration = 0;
    uint64 ContentRevision = 0;
    TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe> Telemetry;
    TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe> State;

    bool IsValid() const
    {
        return ResourceGeneration > 0 && ContentRevision > 0 && State.IsValid();
    }
};

enum class EDWCEditorTextureUploadStatus : uint8
{
    Invalid,
    Queued,
    RenderEnqueued,
    Stale
};

/** Registration token for one game-thread upload state observer. */
struct FDWCEditorTextureUploadObserverHandle
{
    uint64 ObserverId = 0;
    TWeakPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe> State;

    bool IsValid() const
    {
        return ObserverId != 0 && State.IsValid();
    }

    void Reset()
    {
        ObserverId = 0;
        State.Reset();
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
    uint64 GetDataRevision() const { return DataRevision; }
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
    // Persistent derived working data revision. Presentation-only changes,
    // such as hover overlays, must not invalidate an admitted region job.
    uint64 DataRevision = 0;
    uint64 ContentRevision = 0;
    uint64 ResourceGeneration = 0;
    uint64 LastUsedSerial = 0;
    uint32 ActiveLeaseCount = 0;
    EDWCEditorTextureGPUState GPUState = EDWCEditorTextureGPUState::CPUOnly;
    TUniquePtr<FRenderCommandFence> GPUReleaseFence;
    FDWCEditorMemoryLease CPUResourceLease;
    FDWCEditorMemoryLease GPUResourceLease;
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
