//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace DWCEditorSurfacePatchProjectionCachePrivate
{
    uint32 FloatBits(const float Value)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }
}

FDWCEditorSurfacePatchProjectionCacheService::FDWCEditorSurfacePatchProjectionCacheService(
    const uint64 InBudgetBytes)
    : BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
{
}

FDWCEditorSurfacePatchProjectionCacheService::FDWCEditorSurfacePatchProjectionCacheService(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
    const FGuid& InSessionEpoch,
    const uint64 InBudgetBytes)
    : ResourceGovernor(MoveTemp(InResourceGovernor))
    , BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
{
    MemoryOwner.Key.Namespace = TEXT("DWC.SurfacePatchProjectionCache");
    MemoryOwner.SessionEpoch = InSessionEpoch.IsValid() ? InSessionEpoch : FGuid::NewGuid();
    MemoryOwner.OperationId = 1;
    MemoryOwner.Generation = 1;
}

FDWCEditorSurfacePatchProjectionCacheService::~FDWCEditorSurfacePatchProjectionCacheService()
{
    Reset();
}

FDWCEditorSurfacePatchProjectionCacheService::FKey
FDWCEditorSurfacePatchProjectionCacheService::MakeKey(
    const FDWCEditorSurfacePatchProjectionRequest& Request)
{
    using namespace DWCEditorSurfacePatchProjectionCachePrivate;
    FKey Key;
    Key.SpatialIdentity = Request.SpatialHandle.Get();
    Key.MaterialSlotIndex = Request.MaterialSlotIndex;
    Key.AnchorTriangleID = Request.AnchorTriangleID;
    Key.AnchorBarycentric = {
        FloatBits(Request.AnchorBarycentric.X),
        FloatBits(Request.AnchorBarycentric.Y),
        FloatBits(Request.AnchorBarycentric.Z)};
    Key.SurfaceHalfExtentLocal = {
        FloatBits(Request.SurfaceHalfExtentLocal.X),
        FloatBits(Request.SurfaceHalfExtentLocal.Y)};
    Key.SurfaceFrameU = {
        FloatBits(Request.SurfaceFrameU.X),
        FloatBits(Request.SurfaceFrameU.Y),
        FloatBits(Request.SurfaceFrameU.Z)};
    Key.SurfaceFrameV = {
        FloatBits(Request.SurfaceFrameV.X),
        FloatBits(Request.SurfaceFrameV.Y),
        FloatBits(Request.SurfaceFrameV.Z)};
    Key.RotationRadians = FloatBits(Request.RotationRadians);
    Key.Scale = {FloatBits(Request.Scale.X), FloatBits(Request.Scale.Y)};
    Key.UVChannelIndex = Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->UVChannelIndex
        : INDEX_NONE;
    Key.LODIndex = Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->LODIndex
        : INDEX_NONE;
    Key.ProjectionDepthLocal = FloatBits(Request.ProjectionDepthLocal);
    Key.BoundaryPolicy = Request.BoundaryPolicy;
    Key.AlgorithmVersion = DWCEditorSurfacePatchProjectionVersion::SurfaceProjection;
    return Key;
}

bool FDWCEditorSurfacePatchProjectionCacheService::Resolve(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const EDWCEditorSurfacePatchCachePolicy Policy,
    FDWCEditorSurfacePatchProjectionLease& OutLease,
    FString* OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    OutLease.Reset();
    if (OutError != nullptr)
    {
        OutError->Reset();
    }

    if (!FDWCEditorSurfacePatchProjector::ValidateProjectionContract(Request, OutError))
    {
        return false;
    }

    const FKey Key = MakeKey(Request);
    uint64 ResolveGeneration = 0;
    if (Policy != EDWCEditorSurfacePatchCachePolicy::Ephemeral)
    {
        FScopeLock Lock(&Mutex);
        ResolveGeneration = CacheGeneration;
        if (TSharedPtr<FEntry, ESPMode::ThreadSafe>* Existing = Entries.Find(Key))
        {
            const TSharedPtr<FEntry, ESPMode::ThreadSafe> Entry = *Existing;
            if (Entry.IsValid() && Entry->Lease.IsValid() && Entry->Lease.IsCacheResident())
            {
                const uint64 ResultBytes = Entry->Lease->GetAllocatedSizeBytes();
                const bool bDiagnosticsSatisfied =
                    !Request.bCollectDetailedDiagnostics || Entry->Lease->Diagnostics.bDetailed;
                if (bDiagnosticsSatisfied &&
                    Entry->Lease->VisitedTriangleCount <=
                        (Request.MaxVisitedTriangles > 0
                            ? Request.MaxVisitedTriangles
                            : TNumericLimits<int32>::Max()) &&
                    Entry->Lease->PeakWorkingSetBytes <= Request.MaxWorkingSetBytes &&
                    ResultBytes <= Request.MaxResultBytes)
                {
                    Entry->LastUsedSerial = ++UseSerial;
                    ++HitCount;
                    if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
                    {
                        ++ReadOnlyHitCount;
                    }
                    OutLease = Entry->Lease;
                    return true;
                }
            }
        }
        ++MissCount;
        if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
        {
            ++ReadOnlyMissCount;
            ++EphemeralBuildCount;
        }
    }
    else
    {
        FScopeLock Lock(&Mutex);
        ++EphemeralBuildCount;
    }

    FDWCEditorSurfacePatchProjectionResult Projection =
        FDWCEditorSurfacePatchProjector::Project(Request, CancellationToken);
    if (!Projection.IsSuccess() || Projection.Fragments.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = Projection.Error.IsEmpty()
                ? TEXT("The surface patch did not project onto any target triangles.")
                : MoveTemp(Projection.Error);
        }
        return false;
    }

    TSharedRef<FDWCEditorSurfacePatchProjectionGeometry, ESPMode::ThreadSafe> Geometry =
        MakeShared<FDWCEditorSurfacePatchProjectionGeometry, ESPMode::ThreadSafe>();
    Geometry->Fragments = MoveTemp(Projection.Fragments);
    Geometry->AffectedUVIslandIDs = MoveTemp(Projection.AffectedUVIslandIDs);
    Geometry->VisitedTriangleCount = Projection.VisitedTriangleCount;
    Geometry->TraversedSeamCount = Projection.TraversedSeamCount;
    Geometry->PeakWorkingSetBytes = Projection.PeakWorkingSetBytes;
    Geometry->Diagnostics = Projection.Diagnostics;
    OutLease.Geometry = Geometry;

    if (Policy != EDWCEditorSurfacePatchCachePolicy::Persistent)
    {
        return true;
    }

    const uint64 ResidentBytes = EstimateResidentBytes(*Geometry);
    FScopeLock Lock(&Mutex);
    if (CacheGeneration != ResolveGeneration)
    {
        return true;
    }
    if (TSharedPtr<FEntry, ESPMode::ThreadSafe>* Existing = Entries.Find(Key))
    {
        if (Existing->IsValid() && (*Existing)->Lease.IsValid() &&
            (*Existing)->Lease.IsCacheResident() &&
            (!Request.bCollectDetailedDiagnostics ||
             (*Existing)->Lease->Diagnostics.bDetailed))
        {
            (*Existing)->LastUsedSerial = ++UseSerial;
            OutLease = (*Existing)->Lease;
            ++HitCount;
            return true;
        }

        // A detailed request may replace a geometry-equivalent basic entry.
        // Existing consumer leases remain valid through residency ownership.
        Entries.Remove(Key);
    }

    while (AccountingState->Snapshot().UsedBytes + ResidentBytes > BudgetBytes &&
        EvictOldestUnleased_Locked())
    {
    }
    if (ResidentBytes > BudgetBytes ||
        AccountingState->Snapshot().UsedBytes + ResidentBytes > BudgetBytes)
    {
        ++AdmissionRejectCount;
        return true;
    }

    FDWCEditorMemoryLease MemoryLease;
    if (ResourceGovernor.IsValid())
    {
        FDWCEditorResourceReservationRequest Reservation;
        Reservation.Pool = EDWCEditorResourcePool::SharedCacheCPU;
        Reservation.Bytes = ResidentBytes;
        Reservation.Owner = MemoryOwner;
        Reservation.DebugName = TEXT("Surface patch projection geometry");
        MemoryLease = ResourceGovernor->TryAcquire(Reservation);
        while (!MemoryLease.IsValid() && EvictOldestUnleased_Locked())
        {
            MemoryLease = ResourceGovernor->TryAcquire(Reservation);
        }
        if (!MemoryLease.IsValid())
        {
            ++AdmissionRejectCount;
            return true;
        }
    }

    TSharedPtr<FEntry, ESPMode::ThreadSafe> Entry = MakeShared<FEntry, ESPMode::ThreadSafe>();
    Entry->Lease.Geometry = Geometry;
    Entry->Lease.Residency = MakeShared<FProjectionResidency, ESPMode::ThreadSafe>(
        AccountingState,
        MoveTemp(MemoryLease),
        ResidentBytes);
    Entry->SpatialLease = Request.SpatialHandle;
    Entry->LastUsedSerial = ++UseSerial;
    Entries.Add(Key, Entry);
    OutLease = Entry->Lease;
    return true;
}

bool FDWCEditorSurfacePatchProjectionCacheService::EvictOldestUnleased_Locked()
{
    const FKey* OldestKey = nullptr;
    uint64 OldestSerial = TNumericLimits<uint64>::Max();
    for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
    {
        if (!Pair.Value.IsValid() || !Pair.Value->Lease.IsCacheResident() ||
            !Pair.Value->Lease.IsResidencyUnique())
        {
            continue;
        }
        if (Pair.Value->LastUsedSerial < OldestSerial)
        {
            OldestSerial = Pair.Value->LastUsedSerial;
            OldestKey = &Pair.Key;
        }
    }
    if (OldestKey == nullptr)
    {
        return false;
    }
    Entries.Remove(*OldestKey);
    ++EvictionCount;
    return true;
}

void FDWCEditorSurfacePatchProjectionCacheService::Reset()
{
    FScopeLock Lock(&Mutex);
    ++CacheGeneration;
    if (CacheGeneration == 0)
    {
        CacheGeneration = 1;
    }
    RetireActiveEntries_Locked();
    Entries.Reset();
    SweepRetired_Locked();
}

void FDWCEditorSurfacePatchProjectionCacheService::RetireActiveEntries_Locked()
{
    for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->Lease.IsCacheResident() &&
            !Pair.Value->Lease.IsResidencyUnique())
        {
            RetiredResidencies.Add(Pair.Value->Lease.Residency);
            ++RetireCount;
        }
    }
}

void FDWCEditorSurfacePatchProjectionCacheService::SweepRetired_Locked() const
{
    const int32 Before = RetiredResidencies.Num();
    RetiredResidencies.RemoveAllSwap(
        [](const TWeakPtr<const IDWCEditorSurfacePatchProjectionResidency, ESPMode::ThreadSafe>& Weak)
        {
            return !Weak.IsValid();
        }, EAllowShrinking::No);
    RetiredSweepCount += static_cast<uint64>(Before - RetiredResidencies.Num());
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::GetActiveBytes_Locked() const
{
    uint64 ActiveBytes = 0;
    for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            ActiveBytes += Pair.Value->Lease.GetSharedResidentBytes();
        }
    }
    return ActiveBytes;
}

void FDWCEditorSurfacePatchProjectionCacheService::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    FScopeLock Lock(&Mutex);
    SweepRetired_Locked();
    const FResidencyAccountingSnapshot Accounting = AccountingState->Snapshot();
    FDWCEditorPreviewMemoryBucket& Bucket = OutBuckets.AddDefaulted_GetRef();
    Bucket.Name = TEXT("Surface patch projection geometry");
    Bucket.UsedBytes = Accounting.UsedBytes;
    Bucket.BudgetBytes = BudgetBytes;
    Bucket.EntryCount = Entries.Num() + RetiredResidencies.Num();
    Bucket.HitCount = HitCount;
    Bucket.MissCount = MissCount;
    Bucket.EvictionCount = EvictionCount;
    Bucket.GlobalOwnerIdentifier = FString::Printf(
        TEXT("SurfacePatchProjectionCache/%p"),
        static_cast<const void*>(this));
    Bucket.GlobalCategory = EDWCEditorMemoryCategory::SharedCacheCPU;
    Bucket.bIncludeInGlobalSnapshot = true;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::GetReclaimableBytes_Locked() const
{
    uint64 ReclaimableBytes = 0;
    for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->Lease.IsCacheResident() &&
            Pair.Value->Lease.IsResidencyUnique())
        {
            ReclaimableBytes += Pair.Value->Lease.GetSharedResidentBytes();
        }
    }
    return ReclaimableBytes;
}

void FDWCEditorSurfacePatchProjectionCacheService::ResetDiagnosticCounters()
{
    FScopeLock Lock(&Mutex);
    HitCount = 0;
    MissCount = 0;
    EvictionCount = 0;
    AdmissionRejectCount = 0;
    EphemeralBuildCount = 0;
    ReadOnlyHitCount = 0;
    ReadOnlyMissCount = 0;
    RetireCount = 0;
    RetiredSweepCount = 0;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::GetUsedBytes() const
{
    return AccountingState->Snapshot().UsedBytes;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::GetReclaimableBytes() const
{
    FScopeLock Lock(&Mutex);
    return GetReclaimableBytes_Locked();
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::ReclaimUnleasedBytes(
    const uint64 TargetBytes)
{
    FScopeLock Lock(&Mutex);
    const uint64 BeforeBytes = AccountingState->Snapshot().UsedBytes;
    while (BeforeBytes >= AccountingState->Snapshot().UsedBytes &&
           BeforeBytes - AccountingState->Snapshot().UsedBytes < TargetBytes &&
           EvictOldestUnleased_Locked())
    {
    }
    const uint64 AfterBytes = AccountingState->Snapshot().UsedBytes;
    return BeforeBytes >= AfterBytes ? BeforeBytes - AfterBytes : 0;
}

int32 FDWCEditorSurfacePatchProjectionCacheService::GetEntryCount() const
{
    FScopeLock Lock(&Mutex);
    return Entries.Num();
}

FDWCEditorSurfacePatchProjectionCacheDiagnostics
FDWCEditorSurfacePatchProjectionCacheService::GetDiagnostics() const
{
    FScopeLock Lock(&Mutex);
    SweepRetired_Locked();
    const FResidencyAccountingSnapshot Accounting = AccountingState->Snapshot();
    FDWCEditorSurfacePatchProjectionCacheDiagnostics Result;
    Result.UsedBytes = Accounting.UsedBytes;
    Result.ActiveBytes = GetActiveBytes_Locked();
    Result.HighWaterBytes = Accounting.HighWaterBytes;
    Result.BudgetBytes = BudgetBytes;
    Result.HitCount = HitCount;
    Result.MissCount = MissCount;
    Result.EvictionCount = EvictionCount;
    Result.AdmissionRejectCount = AdmissionRejectCount;
    Result.EphemeralBuildCount = EphemeralBuildCount;
    Result.ReadOnlyHitCount = ReadOnlyHitCount;
    Result.ReadOnlyMissCount = ReadOnlyMissCount;
    Result.RetireCount = RetireCount;
    Result.RetiredSweepCount = RetiredSweepCount;
    for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->Lease.IsCacheResident() &&
            !Pair.Value->Lease.IsResidencyUnique())
        {
            ++Result.PinnedEntryCount;
        }
    }
    for (const TWeakPtr<const IDWCEditorSurfacePatchProjectionResidency, ESPMode::ThreadSafe>& Weak :
        RetiredResidencies)
    {
        if (const TSharedPtr<const IDWCEditorSurfacePatchProjectionResidency, ESPMode::ThreadSafe> Pinned = Weak.Pin())
        {
            Result.RetiredPinnedBytes += Pinned->GetResidentBytes();
            ++Result.PinnedEntryCount;
        }
    }
    Result.RetiredEntryCount = RetiredResidencies.Num();
    Result.EntryCount = Entries.Num();
    return Result;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::EstimateResidentBytes(
    const FDWCEditorSurfacePatchProjectionGeometry& Geometry)
{
    return Geometry.GetAllocatedSizeBytes() + sizeof(FEntry) + sizeof(FKey);
}
