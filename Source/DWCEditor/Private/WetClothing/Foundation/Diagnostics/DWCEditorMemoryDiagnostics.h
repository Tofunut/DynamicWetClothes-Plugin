// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FDWCEditorResourceGovernorDiagnostics;
struct FDWCEditorResourceReservationRequest;

enum class EDWCEditorMemoryCategory : uint8
{
    PersistentEditorCPU,
    SharedCacheCPU,
    OperationPrivateCPU,
    UploadStagingCPU,
    PreviewGPU,
    AssetBuildCPU
};

enum class EDWCEditorMemoryAccounting : uint8
{
    /** Bytes currently retained by an editor owner. */
    Resident,
    /** Bytes admitted by the resource governor. Kept separate from resident bytes to avoid double counting. */
    Reservation,
    /** Best-effort bytes for a short-lived phase that is not governed yet. */
    TransientEstimate
};

struct FDWCEditorMemoryOwnerRecord
{
    FString Identifier;
    FName Subsystem;
    FName Resource;
    EDWCEditorMemoryCategory Category = EDWCEditorMemoryCategory::PersistentEditorCPU;
    EDWCEditorMemoryAccounting Accounting = EDWCEditorMemoryAccounting::Resident;
    uint64 CurrentBytes = 0;
    uint64 PeakBytes = 0;
    int32 EntryCount = 0;
    FString Context;
};

struct FDWCEditorMemorySnapshot
{
    uint64 ResidentCPUBytes = 0;
    uint64 ResidentGPUBytes = 0;
    uint64 ReservedCPUBytes = 0;
    uint64 ReservedGPUBytes = 0;
    uint64 TransientEstimatedCPUBytes = 0;
    uint64 PeakResidentCPUBytes = 0;
    uint64 PeakResidentGPUBytes = 0;
    uint64 PeakReservedCPUBytes = 0;
    uint64 PeakReservedGPUBytes = 0;
    TArray<FDWCEditorMemoryOwnerRecord> Owners;
};

using FDWCEditorMemoryCollector = TFunction<void(TArray<FDWCEditorMemoryOwnerRecord>&)>;

/**
 * Process-local accounting for WCA editor memory owners.
 *
 * This is diagnostics only: it never admits, evicts, allocates, or releases editor resources.
 * Resource-governor reservations and actual resident buffers are intentionally separate totals.
 */
class FDWCEditorMemoryDiagnostics final
{
  public:
    static uint64 RegisterOwner(const FDWCEditorMemoryOwnerRecord& Record);
    static void UpdateOwner(uint64 OwnerId, uint64 CurrentBytes, int32 EntryCount = 0);
    static void UnregisterOwner(uint64 OwnerId);

    static void RegisterCollector(FName CollectorName, FDWCEditorMemoryCollector Collector);
    static void UnregisterCollector(FName CollectorName);

    static FDWCEditorMemorySnapshot CaptureSnapshot();
    static void LogSnapshotSummary(const FDWCEditorMemorySnapshot& Snapshot, const FString& Prefix);
    static void LogTopOwners(
        const FDWCEditorMemorySnapshot& Snapshot,
        int32 MaxOwnerCount,
        const FString& Prefix);
    static void ReportAdmissionFailure(
        const FDWCEditorResourceReservationRequest& Request,
        const FDWCEditorResourceGovernorDiagnostics& GovernorDiagnostics,
        const FString& ErrorMessage);
    static void DumpSnapshot(bool bVerboseOwners = false);
    static void ResetPeaks();
    static int32 GetBuildDiagnosticsVerbosity();

    static const TCHAR* LexToString(EDWCEditorMemoryCategory Category);
    static const TCHAR* LexToString(EDWCEditorMemoryAccounting Accounting);
};

/** Scoped memory trace for a synchronous multi-phase WCA build operation. */
class FDWCEditorBuildMemoryTrace final
{
public:
    FDWCEditorBuildMemoryTrace(
        FString InBuildName,
        FString InAssetPath,
        const FString* InFailureMessage = nullptr);
    ~FDWCEditorBuildMemoryTrace();

    FDWCEditorBuildMemoryTrace(const FDWCEditorBuildMemoryTrace&) = delete;
    FDWCEditorBuildMemoryTrace& operator=(const FDWCEditorBuildMemoryTrace&) = delete;

    void BeginPhase(const TCHAR* PhaseName);
    void Complete();

private:
    void EndCurrentPhase();

    FString BuildName;
    FString AssetPath;
    FString CurrentPhase;
    const FString* FailureMessage = nullptr;
    FDWCEditorMemorySnapshot BuildStartSnapshot;
    FDWCEditorMemorySnapshot PhaseStartSnapshot;
    double BuildStartSeconds = 0.0;
    double PhaseStartSeconds = 0.0;
    uint64 StartPressureRequestCount = 0;
    uint64 StartSuccessfulReclaimCount = 0;
    uint64 StartReclaimedBytes = 0;
    uint64 StartCPURejectionCount = 0;
    bool bEnabled = false;
    bool bComplete = false;
};

/** Move-only owner token for buffers whose lifetime has a concrete begin/end boundary. */
class FDWCEditorMemoryOwner final
{
  public:
    FDWCEditorMemoryOwner() = default;
    explicit FDWCEditorMemoryOwner(const FDWCEditorMemoryOwnerRecord& Record);
    ~FDWCEditorMemoryOwner();

    FDWCEditorMemoryOwner(const FDWCEditorMemoryOwner&) = delete;
    FDWCEditorMemoryOwner& operator=(const FDWCEditorMemoryOwner&) = delete;
    FDWCEditorMemoryOwner(FDWCEditorMemoryOwner&& Other) noexcept;
    FDWCEditorMemoryOwner& operator=(FDWCEditorMemoryOwner&& Other) noexcept;

    bool IsValid() const { return OwnerId != 0; }
    void Update(uint64 CurrentBytes, int32 EntryCount = 0) const;
    void Reset();

  private:
    uint64 OwnerId = 0;
};
