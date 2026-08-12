// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBrokerTypes.h"

class FDWCEditorResourceBroker;

/** Move-only process-wide Build ownership token. */
class FDWCEditorExclusiveBuildLease final
{
public:
    FDWCEditorExclusiveBuildLease() = default;
    ~FDWCEditorExclusiveBuildLease();

    FDWCEditorExclusiveBuildLease(const FDWCEditorExclusiveBuildLease&) = delete;
    FDWCEditorExclusiveBuildLease& operator=(const FDWCEditorExclusiveBuildLease&) = delete;
    FDWCEditorExclusiveBuildLease(FDWCEditorExclusiveBuildLease&& Other) noexcept;
    FDWCEditorExclusiveBuildLease& operator=(FDWCEditorExclusiveBuildLease&& Other) noexcept;

    bool IsValid() const { return ScopeId.IsValid() && Broker.IsValid(); }
    const FGuid& GetScopeId() const { return ScopeId; }
    void Reset();

private:
    friend class FDWCEditorResourceBroker;
    FDWCEditorExclusiveBuildLease(
        TWeakPtr<FDWCEditorResourceBroker> InBroker,
        FGuid InScopeId);

    TWeakPtr<FDWCEditorResourceBroker> Broker;
    FGuid ScopeId;
};

/** Process-wide WCA editor memory admission and pressure-reclaim coordinator. */
class FDWCEditorResourceBroker final
    : public TSharedFromThis<FDWCEditorResourceBroker>
{
    friend class FDWCEditorExclusiveBuildLease;

public:
    using FExclusiveBuildBarrierChanged = TFunction<void(bool)>;
    using FHasOutstandingInteractiveWork = TFunction<bool()>;

    static TSharedRef<FDWCEditorResourceBroker> Get();
    static TSharedRef<FDWCEditorResourceBroker> Create(
        const FDWCEditorResourceBudgetConfig& InConfig);

    ~FDWCEditorResourceBroker();

    TSharedRef<FDWCEditorResourceGovernor> GetResourceGovernor() const
    {
        return ResourceGovernor;
    }
    const FDWCEditorResourceBudgetConfig& GetBudgetConfig() const { return BudgetConfig; }

    FGuid OpenSession(const FString& DebugName);
    void CloseSession(const FGuid& SessionId);
    void SetSessionActive(const FGuid& SessionId, bool bActive);
    void SetSessionBuildBarrierHooks(
        const FGuid& SessionId,
        FExclusiveBuildBarrierChanged BarrierChanged,
        FHasOutstandingInteractiveWork HasOutstandingInteractiveWork);
    bool HasOutstandingInteractiveWork() const;

    TUniquePtr<FDWCEditorExclusiveBuildLease> TryBeginExclusiveBuild(
        const FDWCEditorExclusiveBuildRequest& Request,
        FString* OutError = nullptr);
    bool CanAdmitWork(
        const FGuid& SessionId,
        EDWCEditorWorkClass WorkClass,
        const FGuid& ExclusiveBuildScopeId,
        FString* OutReason = nullptr);
    void SetExclusiveBuildState(
        const FGuid& ScopeId,
        EDWCEditorExclusiveBuildState State);
    FDWCEditorExclusiveBuildSnapshot GetExclusiveBuildSnapshot() const;

    uint64 RegisterParticipant(FDWCEditorReclaimParticipantDescriptor Descriptor);
    void UnregisterParticipant(uint64 ParticipantId);
    void UnregisterSessionParticipants(const FGuid& SessionId);

    FDWCEditorResourceBrokerDiagnostics GetDiagnostics() const;
    void ResetDiagnosticCounters();

private:
    explicit FDWCEditorResourceBroker(const FDWCEditorResourceBudgetConfig& InConfig);
    void InitializePressureHandler();
    bool HandleAdmissionPressure(const FDWCEditorResourceReservationRequest& Request);
    void EndExclusiveBuild(const FGuid& ScopeId);

    struct FSessionState
    {
        FString DebugName;
        bool bActive = true;
        uint64 OpenSerial = 0;
        FExclusiveBuildBarrierChanged BarrierChanged;
        FHasOutstandingInteractiveWork HasOutstandingInteractiveWork;
    };

    struct FParticipantState
    {
        uint64 Id = 0;
        uint64 RegistrationSerial = 0;
        FDWCEditorReclaimParticipantDescriptor Descriptor;
    };

    mutable FCriticalSection Mutex;
    FDWCEditorResourceBudgetConfig BudgetConfig;
    TSharedRef<FDWCEditorResourceGovernor> ResourceGovernor;
    TMap<FGuid, FSessionState> Sessions;
    TMap<uint64, FParticipantState> Participants;
    uint64 NextParticipantId = 1;
    uint64 NextSerial = 1;
    bool bReclaimInProgress = false;
    FDWCEditorExclusiveBuildSnapshot ExclusiveBuild;
    FDWCEditorResourceBrokerDiagnostics Diagnostics;
};
