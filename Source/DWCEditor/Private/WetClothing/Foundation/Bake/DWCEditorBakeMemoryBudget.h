//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/**
 * Bounded-memory admission state for one editor bake batch.
 *
 * This is deliberately independent from the worker scheduler: the scheduler
 * controls global concurrency, while this object controls snapshots retained
 * by one authoring request before workers consume them.
 */
class FDWCEditorBakeMemoryBudget final
{
  public:
    void Configure(const int32 InMaxInFlightJobs, const uint64 InMaxInFlightBytes)
    {
        MaxInFlightJobs = FMath::Max(InMaxInFlightJobs, 1);
        MaxInFlightBytes = FMath::Max<uint64>(InMaxInFlightBytes, 1);
        InFlightJobs = 0;
        InFlightBytes = 0;
        PeakInFlightJobs = 0;
        PeakInFlightBytes = 0;
        LargestReservedSnapshotBytes = 0;
    }

    bool IsSingleSnapshotAllowed(const uint64 SnapshotBytes) const
    {
        return SnapshotBytes <= MaxInFlightBytes;
    }

    bool HasJobCapacity() const
    {
        return InFlightJobs < MaxInFlightJobs;
    }

    bool CanReserve(const uint64 SnapshotBytes) const
    {
        return IsSingleSnapshotAllowed(SnapshotBytes) &&
            HasJobCapacity() &&
            InFlightBytes <= MaxInFlightBytes - SnapshotBytes;
    }

    bool TryReserve(const uint64 SnapshotBytes)
    {
        if (!CanReserve(SnapshotBytes))
        {
            return false;
        }

        ++InFlightJobs;
        InFlightBytes += SnapshotBytes;
        PeakInFlightJobs = FMath::Max(PeakInFlightJobs, InFlightJobs);
        PeakInFlightBytes = FMath::Max(PeakInFlightBytes, InFlightBytes);
        LargestReservedSnapshotBytes = FMath::Max(LargestReservedSnapshotBytes, SnapshotBytes);
        return true;
    }

    void Release(const uint64 SnapshotBytes)
    {
        InFlightJobs = FMath::Max(InFlightJobs - 1, 0);
        InFlightBytes = InFlightBytes >= SnapshotBytes ? InFlightBytes - SnapshotBytes : 0;
    }

    int32 GetInFlightJobs() const { return InFlightJobs; }
    uint64 GetInFlightBytes() const { return InFlightBytes; }
    int32 GetMaxInFlightJobs() const { return MaxInFlightJobs; }
    uint64 GetMaxInFlightBytes() const { return MaxInFlightBytes; }
    int32 GetPeakInFlightJobs() const { return PeakInFlightJobs; }
    uint64 GetPeakInFlightBytes() const { return PeakInFlightBytes; }
    uint64 GetLargestReservedSnapshotBytes() const { return LargestReservedSnapshotBytes; }

  private:
    int32 MaxInFlightJobs = 1;
    uint64 MaxInFlightBytes = 1;
    int32 InFlightJobs = 0;
    uint64 InFlightBytes = 0;
    int32 PeakInFlightJobs = 0;
    uint64 PeakInFlightBytes = 0;
    uint64 LargestReservedSnapshotBytes = 0;
};
