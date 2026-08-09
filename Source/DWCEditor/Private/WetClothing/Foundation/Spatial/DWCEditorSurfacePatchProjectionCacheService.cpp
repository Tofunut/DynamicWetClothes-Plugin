//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace DWCEditorSurfacePatchProjectionCachePrivate
{
    constexpr float IslandChartRadiusBucketLocal = 1.0f;

    uint32 FloatBits(const float Value)
    {
        uint32 Bits = 0;
        FMemory::Memcpy(&Bits, &Value, sizeof(Bits));
        return Bits;
    }
}

FDWCEditorSurfacePatchProjectionCacheService::FChartKey
FDWCEditorSurfacePatchProjectionCacheService::MakeChartKey(
    const FDWCEditorIslandLocalChartRequest& Request)
{
    using namespace DWCEditorSurfacePatchProjectionCachePrivate;
    FChartKey Key;
    Key.SpatialIdentity = Request.SpatialHandle.Get();
    Key.MaterialSlotIndex = Request.MaterialSlotIndex;
    Key.AnchorTriangleID = Request.AnchorTriangleID;
    Key.Values[0] = FloatBits(Request.AnchorBarycentric.X);
    Key.Values[1] = FloatBits(Request.AnchorBarycentric.Y);
    Key.Values[2] = FloatBits(Request.AnchorBarycentric.Z);
    Key.Values[3] = FloatBits(Request.SurfaceFrameU.X);
    Key.Values[4] = FloatBits(Request.SurfaceFrameU.Y);
    Key.Values[5] = FloatBits(Request.SurfaceFrameU.Z);
    Key.Values[6] = FloatBits(Request.SurfaceFrameV.X);
    Key.Values[7] = FloatBits(Request.SurfaceFrameV.Y);
    Key.Values[8] = FloatBits(Request.SurfaceFrameV.Z);
    Key.Values[9] = FloatBits(Request.GeodesicRadiusLocal);
    Key.Values[10] = FloatBits(Request.NeighborhoodMarginLocal);
    Key.Values[11] = static_cast<uint32>(Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->UVChannelIndex
        : INDEX_NONE);
    Key.Values[12] = static_cast<uint32>(Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->LODIndex
        : INDEX_NONE);
    Key.AlgorithmVersion = DWCEditorSurfacePatchProjectionVersion::IslandLocalChart;
    return Key;
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

FDWCEditorSurfacePatchProjectionCacheService::FKey
FDWCEditorSurfacePatchProjectionCacheService::MakeKey(
    const FDWCEditorSurfacePatchProjectionRequest& Request)
{
    using namespace DWCEditorSurfacePatchProjectionCachePrivate;
    FKey Key;
    Key.SpatialIdentity = Request.SpatialHandle.Get();
    Key.MaterialSlotIndex = Request.MaterialSlotIndex;
    Key.AnchorTriangleID = Request.AnchorTriangleID;
    Key.Values[0] = FloatBits(Request.AnchorBarycentric.X);
    Key.Values[1] = FloatBits(Request.AnchorBarycentric.Y);
    Key.Values[2] = FloatBits(Request.AnchorBarycentric.Z);
    Key.Values[3] = FloatBits(Request.SurfaceHalfExtentLocal.X);
    Key.Values[4] = FloatBits(Request.SurfaceHalfExtentLocal.Y);
    Key.Values[5] = FloatBits(Request.SurfaceFrameU.X);
    Key.Values[6] = FloatBits(Request.SurfaceFrameU.Y);
    Key.Values[7] = FloatBits(Request.SurfaceFrameU.Z);
    Key.Values[8] = FloatBits(Request.SurfaceFrameV.X);
    Key.Values[9] = FloatBits(Request.SurfaceFrameV.Y);
    Key.Values[10] = FloatBits(Request.SurfaceFrameV.Z);
    Key.Values[11] = FloatBits(Request.RotationRadians);
    Key.Values[12] = FloatBits(Request.Scale.X);
    Key.Values[13] = FloatBits(Request.Scale.Y);
    Key.Values[14] = static_cast<uint32>(Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->UVChannelIndex
        : INDEX_NONE);
    Key.Values[15] = static_cast<uint32>(Request.SpatialHandle.IsValid()
        ? Request.SpatialHandle->LODIndex
        : INDEX_NONE);
    Key.Values[16] = FloatBits(Request.ProjectionDepthLocal);
    Key.Values[17] = FloatBits(Request.MaxSurfaceAngleDegrees);
    Key.Values[18] = FloatBits(Request.ProjectionDepthSoftness);
    Key.Values[19] = FloatBits(Request.ProjectionAngleSoftness);
    Key.Values[20] = Request.bUseSurfaceDecalProjection ? 1u : 0u;
    Key.Values[21] = Request.bAllowUVSeamTraversal ? 1u : 0u;
    Key.Values[22] = Request.bCollectDetailedDiagnostics ? 1u : 0u;
    Key.AlgorithmVersion = DWCEditorSurfacePatchProjectionVersion::SurfaceProjection;
    return Key;
}

bool FDWCEditorSurfacePatchProjectionCacheService::ResolveChart(
    FDWCEditorIslandLocalChartRequest Request,
    const EDWCEditorSurfacePatchCachePolicy Policy,
    FDWCEditorIslandLocalChartHandle& OutChart,
    FString* OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorSurfacePatchProjectionCachePrivate;
    OutChart.Reset();

    // Round upward so every request sharing a key is covered by the cached chart.
    Request.GeodesicRadiusLocal = FMath::CeilToFloat(
        Request.GeodesicRadiusLocal / IslandChartRadiusBucketLocal) *
        IslandChartRadiusBucketLocal;
    const FChartKey Key = MakeChartKey(Request);
    if (Policy != EDWCEditorSurfacePatchCachePolicy::Ephemeral)
    {
        FScopeLock Lock(&Mutex);
        if (TSharedPtr<FChartEntry, ESPMode::ThreadSafe>* Existing = ChartEntries.Find(Key))
        {
            const TSharedPtr<FChartEntry, ESPMode::ThreadSafe> Entry = *Existing;
            if (Entry.IsValid() && Entry->Chart.IsValid() &&
                Entry->Chart->Diagnostics.PeakWorkingSetBytes <= Request.MaxWorkingSetBytes &&
                Entry->Chart->GetAllocatedSizeBytes() <= Request.MaxResultBytes)
            {
                Entry->LastUsedSerial = ++UseSerial;
                ++ChartHitCount;
                if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
                {
                    ++ReadOnlyHitCount;
                }
                OutChart = Entry->Chart;
                return true;
            }
        }
        ++ChartMissCount;
        if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
        {
            ++ReadOnlyMissCount;
        }
    }

    const FDWCEditorIslandLocalChartResult BuildResult =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(Request, CancellationToken);
    if (!BuildResult.IsSuccess())
    {
        if (OutError != nullptr)
        {
            *OutError = BuildResult.Error.IsEmpty()
                ? TEXT("The island-local chart build failed.")
                : BuildResult.Error;
        }
        return false;
    }
    OutChart = BuildResult.Chart;
    if (Policy != EDWCEditorSurfacePatchCachePolicy::Persistent)
    {
        return true;
    }

    const uint64 ResidentBytes = OutChart->GetAllocatedSizeBytes() +
        sizeof(FChartEntry) + sizeof(FChartKey);
    FScopeLock Lock(&Mutex);
    if (TSharedPtr<FChartEntry, ESPMode::ThreadSafe>* Existing = ChartEntries.Find(Key))
    {
        if (Existing->IsValid() && (*Existing)->Chart.IsValid())
        {
            (*Existing)->LastUsedSerial = ++UseSerial;
            OutChart = (*Existing)->Chart;
            ++ChartHitCount;
            return true;
        }
    }
    while (UsedBytes + ResidentBytes > BudgetBytes &&
        EvictOldestUnleased_Locked(ResolvePressureClass_Locked()))
    {
    }
    if (ResidentBytes > BudgetBytes || UsedBytes + ResidentBytes > BudgetBytes)
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
        Reservation.DebugName = TEXT("Island-local surface chart");
        MemoryLease = ResourceGovernor->TryAcquire(Reservation);
        while (!MemoryLease.IsValid() &&
            EvictOldestUnleased_Locked(ResolvePressureClass_Locked()))
        {
            MemoryLease = ResourceGovernor->TryAcquire(Reservation);
        }
        if (!MemoryLease.IsValid())
        {
            ++AdmissionRejectCount;
            return true;
        }
    }

    TSharedPtr<FChartEntry, ESPMode::ThreadSafe> Entry =
        MakeShared<FChartEntry, ESPMode::ThreadSafe>();
    Entry->Chart = OutChart;
    Entry->MemoryLease = MoveTemp(MemoryLease);
    Entry->ResidentBytes = ResidentBytes;
    Entry->LastUsedSerial = ++UseSerial;
    ChartEntries.Add(Key, Entry);
    UsedBytes += ResidentBytes;
    ChartUsedBytes += ResidentBytes;
    return true;
}

bool FDWCEditorSurfacePatchProjectionCacheService::Resolve(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const EDWCEditorSurfacePatchCachePolicy Policy,
    FDWCEditorSurfacePatchProjectionHandle& OutGeometry,
    FString* OutError,
    const FDWCEditorCancellationToken* CancellationToken)
{
    OutGeometry.Reset();
    if (OutError != nullptr)
    {
        OutError->Reset();
    }

    if (!FDWCEditorSurfacePatchProjector::ValidateProjectionModeContract(Request, OutError))
    {
        return false;
    }

    const FKey Key = MakeKey(Request);
    if (Policy != EDWCEditorSurfacePatchCachePolicy::Ephemeral)
    {
        FScopeLock Lock(&Mutex);
        if (TSharedPtr<FEntry, ESPMode::ThreadSafe>* Existing = Entries.Find(Key))
        {
            const TSharedPtr<FEntry, ESPMode::ThreadSafe> Entry = *Existing;
            if (Entry.IsValid() && Entry->Geometry.IsValid())
            {
                const uint64 ResultBytes = Entry->Geometry->GetAllocatedSizeBytes();
                if (Entry->Geometry->VisitedTriangleCount <=
                        (Request.MaxVisitedTriangles > 0
                            ? Request.MaxVisitedTriangles
                            : TNumericLimits<int32>::Max()) &&
                    Entry->Geometry->PeakWorkingSetBytes <= Request.MaxWorkingSetBytes &&
                    ResultBytes <= Request.MaxResultBytes)
                {
                    Entry->LastUsedSerial = ++UseSerial;
                    ++HitCount;
                    if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
                    {
                        ++ReadOnlyHitCount;
                    }
                    OutGeometry = Entry->Geometry;
                    return true;
                }
            }
        }
        ++MissCount;
        if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
        {
            ++ReadOnlyMissCount;
        }
    }
    else
    {
        FScopeLock Lock(&Mutex);
        ++EphemeralBuildCount;
    }
    if (Policy == EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral)
    {
        FScopeLock Lock(&Mutex);
        ++EphemeralBuildCount;
    }

    FDWCEditorSurfacePatchProjectionResult Projection;
    if (Request.bUseSurfaceDecalProjection)
    {
        Projection = FDWCEditorSurfacePatchProjector::Project(Request, CancellationToken);
    }
    else
    {
        FDWCEditorIslandLocalChartRequest ChartRequest;
        FString ChartError;
        if (!FDWCEditorSurfacePatchProjector::BuildIslandLocalChartRequest(
                Request, ChartRequest, &ChartError))
        {
            if (OutError != nullptr)
            {
                *OutError = MoveTemp(ChartError);
            }
            return false;
        }
        FDWCEditorIslandLocalChartHandle Chart;
        if (!ResolveChart(
                MoveTemp(ChartRequest), Policy, Chart, &ChartError, CancellationToken))
        {
            if (OutError != nullptr)
            {
                *OutError = MoveTemp(ChartError);
            }
            return false;
        }
        Projection = FDWCEditorSurfacePatchProjector::ProjectFromIslandLocalChart(
            Request, Chart, CancellationToken);
    }
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
    Geometry->bTouchesUVSeam = Projection.bTouchesUVSeam;
    Geometry->PeakWorkingSetBytes = Projection.PeakWorkingSetBytes;
    Geometry->Diagnostics = Projection.Diagnostics;
    OutGeometry = Geometry;

    if (Policy != EDWCEditorSurfacePatchCachePolicy::Persistent)
    {
        return true;
    }

    const uint64 ResidentBytes = EstimateResidentBytes(*Geometry);
    FScopeLock Lock(&Mutex);
    if (TSharedPtr<FEntry, ESPMode::ThreadSafe>* Existing = Entries.Find(Key))
    {
        if (Existing->IsValid() && (*Existing)->Geometry.IsValid())
        {
            (*Existing)->LastUsedSerial = ++UseSerial;
            OutGeometry = (*Existing)->Geometry;
            ++HitCount;
            return true;
        }
    }

    // Final projection geometry is rotation/scale-specific. Prefer retiring an
    // older geometry entry before evicting its reusable island chart.
    while (UsedBytes + ResidentBytes > BudgetBytes &&
        EvictOldestUnleased_Locked(ECacheEntryClass::Geometry))
    {
    }
    if (ResidentBytes > BudgetBytes || UsedBytes + ResidentBytes > BudgetBytes)
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
        while (!MemoryLease.IsValid() &&
            EvictOldestUnleased_Locked(ECacheEntryClass::Geometry))
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
    Entry->Geometry = Geometry;
    Entry->MemoryLease = MoveTemp(MemoryLease);
    Entry->ResidentBytes = ResidentBytes;
    Entry->LastUsedSerial = ++UseSerial;
    Entries.Add(Key, Entry);
    UsedBytes += ResidentBytes;
    GeometryUsedBytes += ResidentBytes;
    return true;
}

FDWCEditorSurfacePatchProjectionCacheService::ECacheEntryClass
FDWCEditorSurfacePatchProjectionCacheService::ResolvePressureClass_Locked() const
{
    const uint64 ChartSoftBudget = BudgetBytes * 5ull / 8ull;
    const uint64 GeometrySoftBudget = BudgetBytes - ChartSoftBudget;
    if (GeometryUsedBytes > GeometrySoftBudget)
    {
        return ECacheEntryClass::Geometry;
    }
    if (ChartUsedBytes > ChartSoftBudget)
    {
        return ECacheEntryClass::Chart;
    }
    return ECacheEntryClass::Any;
}

bool FDWCEditorSurfacePatchProjectionCacheService::EvictOldestUnleased_Locked(
    const ECacheEntryClass PreferredClass)
{
    const FKey* OldestKey = nullptr;
    const FChartKey* OldestChartKey = nullptr;
    uint64 OldestSerial = TNumericLimits<uint64>::Max();
    const auto FindOldest = [this](
        const ECacheEntryClass EntryClass,
        const FKey*& OutGeometryKey,
        const FChartKey*& OutChartKey,
        uint64& OutOldestSerial)
    {
        if (EntryClass == ECacheEntryClass::Any || EntryClass == ECacheEntryClass::Geometry)
        {
            for (const TPair<FKey, TSharedPtr<FEntry, ESPMode::ThreadSafe>>& Pair : Entries)
            {
                if (!Pair.Value.IsValid() || !Pair.Value->Geometry.IsValid() ||
                    !Pair.Value->Geometry.IsUnique())
                {
                    continue;
                }
                if (Pair.Value->LastUsedSerial < OutOldestSerial)
                {
                    OutOldestSerial = Pair.Value->LastUsedSerial;
                    OutGeometryKey = &Pair.Key;
                    OutChartKey = nullptr;
                }
            }
        }
        if (EntryClass == ECacheEntryClass::Any || EntryClass == ECacheEntryClass::Chart)
        {
            for (const TPair<FChartKey, TSharedPtr<FChartEntry, ESPMode::ThreadSafe>>& Pair : ChartEntries)
            {
                if (!Pair.Value.IsValid() || !Pair.Value->Chart.IsValid() ||
                    !Pair.Value->Chart.IsUnique())
                {
                    continue;
                }
                if (Pair.Value->LastUsedSerial < OutOldestSerial)
                {
                    OutOldestSerial = Pair.Value->LastUsedSerial;
                    OutGeometryKey = nullptr;
                    OutChartKey = &Pair.Key;
                }
            }
        }
    };
    FindOldest(PreferredClass, OldestKey, OldestChartKey, OldestSerial);
    if (OldestKey == nullptr && OldestChartKey == nullptr &&
        PreferredClass != ECacheEntryClass::Any)
    {
        OldestSerial = TNumericLimits<uint64>::Max();
        FindOldest(ECacheEntryClass::Any, OldestKey, OldestChartKey, OldestSerial);
    }
    if (OldestKey == nullptr && OldestChartKey == nullptr)
    {
        return false;
    }
    if (OldestKey != nullptr)
    {
        const uint64 ResidentBytes = Entries[*OldestKey]->ResidentBytes;
        UsedBytes -= ResidentBytes;
        GeometryUsedBytes -= ResidentBytes;
        Entries.Remove(*OldestKey);
        ++GeometryEvictionCount;
    }
    else
    {
        const uint64 ResidentBytes = ChartEntries[*OldestChartKey]->ResidentBytes;
        UsedBytes -= ResidentBytes;
        ChartUsedBytes -= ResidentBytes;
        ChartEntries.Remove(*OldestChartKey);
        ++ChartEvictionCount;
    }
    ++EvictionCount;
    return true;
}

void FDWCEditorSurfacePatchProjectionCacheService::Reset()
{
    FScopeLock Lock(&Mutex);
    for (auto Iterator = Entries.CreateIterator(); Iterator; ++Iterator)
    {
        if (!Iterator.Value().IsValid() || !Iterator.Value()->Geometry.IsValid() ||
            Iterator.Value()->Geometry.IsUnique())
        {
            UsedBytes -= Iterator.Value().IsValid() ? Iterator.Value()->ResidentBytes : 0;
            GeometryUsedBytes -= Iterator.Value().IsValid() ? Iterator.Value()->ResidentBytes : 0;
            Iterator.RemoveCurrent();
        }
    }
    for (auto Iterator = ChartEntries.CreateIterator(); Iterator; ++Iterator)
    {
        if (!Iterator.Value().IsValid() || !Iterator.Value()->Chart.IsValid() ||
            Iterator.Value()->Chart.IsUnique())
        {
            UsedBytes -= Iterator.Value().IsValid() ? Iterator.Value()->ResidentBytes : 0;
            ChartUsedBytes -= Iterator.Value().IsValid() ? Iterator.Value()->ResidentBytes : 0;
            Iterator.RemoveCurrent();
        }
    }
}

void FDWCEditorSurfacePatchProjectionCacheService::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    FScopeLock Lock(&Mutex);
    FDWCEditorPreviewMemoryBucket& GeometryBucket = OutBuckets.AddDefaulted_GetRef();
    GeometryBucket.Name = TEXT("Surface patch projection geometry");
    GeometryBucket.UsedBytes = GeometryUsedBytes;
    GeometryBucket.BudgetBytes = BudgetBytes - BudgetBytes * 5ull / 8ull;
    GeometryBucket.EntryCount = Entries.Num();
    GeometryBucket.HitCount = HitCount;
    GeometryBucket.MissCount = MissCount;
    GeometryBucket.EvictionCount = GeometryEvictionCount;

    FDWCEditorPreviewMemoryBucket& ChartBucket = OutBuckets.AddDefaulted_GetRef();
    ChartBucket.Name = TEXT("Island-local geodesic charts");
    ChartBucket.UsedBytes = ChartUsedBytes;
    ChartBucket.BudgetBytes = BudgetBytes * 5ull / 8ull;
    ChartBucket.EntryCount = ChartEntries.Num();
    ChartBucket.HitCount = ChartHitCount;
    ChartBucket.MissCount = ChartMissCount;
    ChartBucket.EvictionCount = ChartEvictionCount;
}

void FDWCEditorSurfacePatchProjectionCacheService::ResetDiagnosticCounters()
{
    FScopeLock Lock(&Mutex);
    HitCount = 0;
    MissCount = 0;
    EvictionCount = 0;
    AdmissionRejectCount = 0;
    EphemeralBuildCount = 0;
    ChartHitCount = 0;
    ChartMissCount = 0;
    ReadOnlyHitCount = 0;
    ReadOnlyMissCount = 0;
    GeometryEvictionCount = 0;
    ChartEvictionCount = 0;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::GetUsedBytes() const
{
    FScopeLock Lock(&Mutex);
    return UsedBytes;
}

int32 FDWCEditorSurfacePatchProjectionCacheService::GetEntryCount() const
{
    FScopeLock Lock(&Mutex);
    return Entries.Num() + ChartEntries.Num();
}

FDWCEditorSurfacePatchProjectionCacheDiagnostics
FDWCEditorSurfacePatchProjectionCacheService::GetDiagnostics() const
{
    FScopeLock Lock(&Mutex);
    FDWCEditorSurfacePatchProjectionCacheDiagnostics Result;
    Result.UsedBytes = UsedBytes;
    Result.GeometryUsedBytes = GeometryUsedBytes;
    Result.ChartUsedBytes = ChartUsedBytes;
    Result.BudgetBytes = BudgetBytes;
    Result.HitCount = HitCount;
    Result.MissCount = MissCount;
    Result.EvictionCount = EvictionCount;
    Result.AdmissionRejectCount = AdmissionRejectCount;
    Result.EphemeralBuildCount = EphemeralBuildCount;
    Result.ChartHitCount = ChartHitCount;
    Result.ChartMissCount = ChartMissCount;
    Result.ReadOnlyHitCount = ReadOnlyHitCount;
    Result.ReadOnlyMissCount = ReadOnlyMissCount;
    Result.GeometryEvictionCount = GeometryEvictionCount;
    Result.ChartEvictionCount = ChartEvictionCount;
    Result.ChartEntryCount = ChartEntries.Num();
    Result.EntryCount = Entries.Num() + ChartEntries.Num();
    return Result;
}

uint64 FDWCEditorSurfacePatchProjectionCacheService::EstimateResidentBytes(
    const FDWCEditorSurfacePatchProjectionGeometry& Geometry)
{
    return Geometry.GetAllocatedSizeBytes() + sizeof(FEntry) + sizeof(FKey);
}
