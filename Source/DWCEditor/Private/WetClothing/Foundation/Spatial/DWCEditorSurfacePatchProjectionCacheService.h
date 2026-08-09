//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartTypes.h"
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
    uint64 GeometryUsedBytes = 0;
    uint64 ChartUsedBytes = 0;
    uint64 BudgetBytes = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    uint64 AdmissionRejectCount = 0;
    uint64 EphemeralBuildCount = 0;
    uint64 ChartHitCount = 0;
    uint64 ChartMissCount = 0;
    uint64 ReadOnlyHitCount = 0;
    uint64 ReadOnlyMissCount = 0;
    uint64 GeometryEvictionCount = 0;
    uint64 ChartEvictionCount = 0;
    int32 ChartEntryCount = 0;
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

    bool Resolve(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        EDWCEditorSurfacePatchCachePolicy Policy,
        FDWCEditorSurfacePatchProjectionHandle& OutGeometry,
        FString* OutError = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    void Reset();
    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void ResetDiagnosticCounters();

    uint64 GetUsedBytes() const;
    uint64 GetBudgetBytes() const { return BudgetBytes; }
    int32 GetEntryCount() const;
    FDWCEditorSurfacePatchProjectionCacheDiagnostics GetDiagnostics() const;
    static uint64 EstimateResidentBytes(
        const FDWCEditorSurfacePatchProjectionGeometry& Geometry);

  private:
    enum class ECacheEntryClass : uint8
    {
        Any,
        Geometry,
        Chart
    };

    struct FKey
    {
        const FDWCEditorSpatialData* SpatialIdentity = nullptr;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 AnchorTriangleID = INDEX_NONE;
        uint32 Values[23] = {};
        uint32 AlgorithmVersion = 0;

        bool operator==(const FKey& Other) const
        {
            return SpatialIdentity == Other.SpatialIdentity &&
                MaterialSlotIndex == Other.MaterialSlotIndex &&
                AnchorTriangleID == Other.AnchorTriangleID &&
                AlgorithmVersion == Other.AlgorithmVersion &&
                FMemory::Memcmp(Values, Other.Values, sizeof(Values)) == 0;
        }

        friend uint32 GetTypeHash(const FKey& Key)
        {
            uint32 Hash = PointerHash(Key.SpatialIdentity);
            Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AnchorTriangleID));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AlgorithmVersion));
            for (const uint32 Value : Key.Values)
            {
                Hash = HashCombine(Hash, ::GetTypeHash(Value));
            }
            return Hash;
        }
    };

    struct FEntry
    {
        FDWCEditorSurfacePatchProjectionHandle Geometry;
        FDWCEditorMemoryLease MemoryLease;
        uint64 ResidentBytes = 0;
        uint64 LastUsedSerial = 0;
    };

    struct FChartKey
    {
        const FDWCEditorSpatialData* SpatialIdentity = nullptr;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 AnchorTriangleID = INDEX_NONE;
        uint32 Values[13] = {};
        uint32 AlgorithmVersion = 0;

        bool operator==(const FChartKey& Other) const
        {
            return SpatialIdentity == Other.SpatialIdentity &&
                MaterialSlotIndex == Other.MaterialSlotIndex &&
                AnchorTriangleID == Other.AnchorTriangleID &&
                AlgorithmVersion == Other.AlgorithmVersion &&
                FMemory::Memcmp(Values, Other.Values, sizeof(Values)) == 0;
        }

        friend uint32 GetTypeHash(const FChartKey& Key)
        {
            uint32 Hash = PointerHash(Key.SpatialIdentity);
            Hash = HashCombine(Hash, ::GetTypeHash(Key.MaterialSlotIndex));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AnchorTriangleID));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.AlgorithmVersion));
            for (const uint32 Value : Key.Values)
            {
                Hash = HashCombine(Hash, ::GetTypeHash(Value));
            }
            return Hash;
        }
    };

    struct FChartEntry
    {
        FDWCEditorIslandLocalChartHandle Chart;
        FDWCEditorMemoryLease MemoryLease;
        uint64 ResidentBytes = 0;
        uint64 LastUsedSerial = 0;
    };

    static FKey MakeKey(const FDWCEditorSurfacePatchProjectionRequest& Request);
    static FChartKey MakeChartKey(const FDWCEditorIslandLocalChartRequest& Request);
    bool ResolveChart(
        FDWCEditorIslandLocalChartRequest Request,
        EDWCEditorSurfacePatchCachePolicy Policy,
        FDWCEditorIslandLocalChartHandle& OutChart,
        FString* OutError,
        const FDWCEditorCancellationToken* CancellationToken);
    bool EvictOldestUnleased_Locked(ECacheEntryClass PreferredClass = ECacheEntryClass::Any);
    ECacheEntryClass ResolvePressureClass_Locked() const;

    mutable FCriticalSection Mutex;
    TMap<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>> Entries;
    TMap<FChartKey, TSharedPtr<FChartEntry, ESPMode::ThreadSafe>> ChartEntries;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity MemoryOwner;
    uint64 BudgetBytes = DefaultBudgetBytes;
    uint64 UsedBytes = 0;
    uint64 GeometryUsedBytes = 0;
    uint64 ChartUsedBytes = 0;
    uint64 UseSerial = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    uint64 AdmissionRejectCount = 0;
    uint64 EphemeralBuildCount = 0;
    uint64 ChartHitCount = 0;
    uint64 ChartMissCount = 0;
    uint64 ReadOnlyHitCount = 0;
    uint64 ReadOnlyMissCount = 0;
    uint64 GeometryEvictionCount = 0;
    uint64 ChartEvictionCount = 0;
};
