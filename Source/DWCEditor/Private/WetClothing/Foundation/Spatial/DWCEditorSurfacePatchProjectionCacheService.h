//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionTypes.h"

class FDWCEditorCancellationToken;
struct FDWCEditorPreviewMemoryBucket;

enum class EDWCEditorSurfacePatchCachePolicy : uint8
{
    Ephemeral,
    /** Reuse an existing persistent entry, but do not admit a miss. */
    ReadOnlyThenEphemeral,
    Persistent
};

struct FDWCEditorSurfacePatchProjectionCacheDiagnostics
{
    uint64 UsedBytes = 0;
    uint64 ActiveBytes = 0;
    uint64 RetiredPinnedBytes = 0;
    uint64 HighWaterBytes = 0;
    uint64 BudgetBytes = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    uint64 AdmissionRejectCount = 0;
    uint64 EphemeralBuildCount = 0;
    uint64 ReadOnlyHitCount = 0;
    uint64 ReadOnlyMissCount = 0;
    uint64 RetireCount = 0;
    uint64 RetiredSweepCount = 0;
    int32 PinnedEntryCount = 0;
    int32 RetiredEntryCount = 0;
    int32 EntryCount = 0;
};

/** Thread-safe, byte-bounded LRU for immutable surface projection geometry. */
class FDWCEditorSurfacePatchProjectionCacheService final
{
  public:
    static constexpr uint64 DefaultBudgetBytes = 32ull * 1024ull * 1024ull;

    explicit FDWCEditorSurfacePatchProjectionCacheService(uint64 InBudgetBytes = DefaultBudgetBytes);
    FDWCEditorSurfacePatchProjectionCacheService(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
        const FGuid& InSessionEpoch,
        uint64 InBudgetBytes = DefaultBudgetBytes);
    ~FDWCEditorSurfacePatchProjectionCacheService();

    bool Resolve(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        EDWCEditorSurfacePatchCachePolicy Policy,
        FDWCEditorSurfacePatchProjectionLease& OutLease,
        FString* OutError = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    void Reset();
    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void ResetDiagnosticCounters();

    uint64 GetUsedBytes() const;
    uint64 GetReclaimableBytes() const;
    uint64 ReclaimUnleasedBytes(uint64 TargetBytes);
    uint64 GetBudgetBytes() const { return BudgetBytes; }
    int32 GetEntryCount() const;
    FDWCEditorSurfacePatchProjectionCacheDiagnostics GetDiagnostics() const;
    static uint64 EstimateResidentBytes(
        const FDWCEditorSurfacePatchProjectionGeometry& Geometry);

  private:
    struct FResidencyAccountingSnapshot
    {
        uint64 UsedBytes = 0;
        uint64 HighWaterBytes = 0;
    };

    struct FResidencyAccountingState
    {
        void Add(uint64 Bytes)
        {
            FScopeLock Lock(&Mutex);
            UsedBytes += Bytes;
            HighWaterBytes = FMath::Max(HighWaterBytes, UsedBytes);
        }

        void Release(uint64 Bytes)
        {
            FScopeLock Lock(&Mutex);
            UsedBytes = Bytes >= UsedBytes ? 0ull : UsedBytes - Bytes;
        }

        FResidencyAccountingSnapshot Snapshot() const
        {
            FScopeLock Lock(&Mutex);
            FResidencyAccountingSnapshot Result;
            Result.UsedBytes = UsedBytes;
            Result.HighWaterBytes = HighWaterBytes;
            return Result;
        }

      private:
        mutable FCriticalSection Mutex;
        uint64 UsedBytes = 0;
        uint64 HighWaterBytes = 0;
    };

    struct FProjectionResidency final : IDWCEditorSurfacePatchProjectionResidency
    {
        FProjectionResidency(
            TSharedRef<FResidencyAccountingState, ESPMode::ThreadSafe> InAccounting,
            FDWCEditorMemoryLease&& InMemoryLease,
            uint64 InResidentBytes)
            : Accounting(MoveTemp(InAccounting))
            , MemoryLease(MoveTemp(InMemoryLease))
            , ResidentBytes(InResidentBytes)
        {
            Accounting->Add(ResidentBytes);
        }

        virtual ~FProjectionResidency() override
        {
            Accounting->Release(ResidentBytes);
        }

        virtual uint64 GetResidentBytes() const override { return ResidentBytes; }

        TSharedRef<FResidencyAccountingState, ESPMode::ThreadSafe> Accounting;
        FDWCEditorMemoryLease MemoryLease;
        uint64 ResidentBytes = 0;
    };

    struct FFloat2Bits
    {
        uint32 X = 0;
        uint32 Y = 0;

        bool operator==(const FFloat2Bits& Other) const
        {
            return X == Other.X && Y == Other.Y;
        }

        friend uint32 GetTypeHash(const FFloat2Bits& Value)
        {
            return HashCombine(::GetTypeHash(Value.X), ::GetTypeHash(Value.Y));
        }
    };

    struct FFloat3Bits
    {
        uint32 X = 0;
        uint32 Y = 0;
        uint32 Z = 0;

        bool operator==(const FFloat3Bits& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }

        friend uint32 GetTypeHash(const FFloat3Bits& Value)
        {
            uint32 Hash = HashCombine(::GetTypeHash(Value.X), ::GetTypeHash(Value.Y));
            return HashCombine(Hash, ::GetTypeHash(Value.Z));
        }
    };

    struct FKey
    {
        const FDWCEditorSpatialData* SpatialIdentity = nullptr;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 AnchorTriangleID = INDEX_NONE;
        FFloat3Bits AnchorBarycentric;
        FFloat2Bits SurfaceHalfExtentLocal;
        FFloat3Bits SurfaceFrameU;
        FFloat3Bits SurfaceFrameV;
        uint32 RotationRadians = 0;
        FFloat2Bits Scale;
        int32 UVChannelIndex = INDEX_NONE;
        int32 LODIndex = INDEX_NONE;
        uint32 ProjectionDepthLocal = 0;
        EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy =
            EDWCEditorSurfacePatchBoundaryPolicy::Invalid;
        uint32 AlgorithmVersion = 0;

        bool operator==(const FKey& Other) const
        {
            return SpatialIdentity == Other.SpatialIdentity &&
                MaterialSlotIndex == Other.MaterialSlotIndex &&
                AnchorTriangleID == Other.AnchorTriangleID &&
                AnchorBarycentric == Other.AnchorBarycentric &&
                SurfaceHalfExtentLocal == Other.SurfaceHalfExtentLocal &&
                SurfaceFrameU == Other.SurfaceFrameU &&
                SurfaceFrameV == Other.SurfaceFrameV &&
                RotationRadians == Other.RotationRadians &&
                Scale == Other.Scale &&
                UVChannelIndex == Other.UVChannelIndex &&
                LODIndex == Other.LODIndex &&
                ProjectionDepthLocal == Other.ProjectionDepthLocal &&
                BoundaryPolicy == Other.BoundaryPolicy &&
                AlgorithmVersion == Other.AlgorithmVersion;
        }

        friend uint32 GetTypeHash(const FKey& Key)
        {
            uint32 Hash = PointerHash(Key.SpatialIdentity);
            Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AnchorTriangleID));
            Hash = HashCombine(Hash, GetTypeHash(Key.AnchorBarycentric));
            Hash = HashCombine(Hash, GetTypeHash(Key.SurfaceHalfExtentLocal));
            Hash = HashCombine(Hash, GetTypeHash(Key.SurfaceFrameU));
            Hash = HashCombine(Hash, GetTypeHash(Key.SurfaceFrameV));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.RotationRadians));
            Hash = HashCombine(Hash, GetTypeHash(Key.Scale));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.UVChannelIndex));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.LODIndex));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.ProjectionDepthLocal));
            Hash = HashCombine(Hash, ::GetTypeHash(static_cast<uint8>(Key.BoundaryPolicy)));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AlgorithmVersion));
            return Hash;
        }
    };

    struct FEntry
    {
        FDWCEditorSurfacePatchProjectionLease Lease;
        FDWCEditorSpatialHandle SpatialLease;
        uint64 LastUsedSerial = 0;
    };

    static FKey MakeKey(const FDWCEditorSurfacePatchProjectionRequest& Request);
    bool EvictOldestUnleased_Locked();
    uint64 GetReclaimableBytes_Locked() const;
    void RetireActiveEntries_Locked();
    void SweepRetired_Locked() const;
    uint64 GetActiveBytes_Locked() const;

    mutable FCriticalSection Mutex;
    TMap<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>> Entries;
    mutable TArray<TWeakPtr<const IDWCEditorSurfacePatchProjectionResidency, ESPMode::ThreadSafe>>
        RetiredResidencies;
    TSharedRef<FResidencyAccountingState, ESPMode::ThreadSafe> AccountingState =
        MakeShared<FResidencyAccountingState, ESPMode::ThreadSafe>();
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity MemoryOwner;
    uint64 BudgetBytes = DefaultBudgetBytes;
    uint64 CacheGeneration = 1;
    uint64 UseSerial = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    uint64 AdmissionRejectCount = 0;
    uint64 EphemeralBuildCount = 0;
    uint64 ReadOnlyHitCount = 0;
    uint64 ReadOnlyMissCount = 0;
    uint64 RetireCount = 0;
    mutable uint64 RetiredSweepCount = 0;
};
