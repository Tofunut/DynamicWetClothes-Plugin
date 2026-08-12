// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorResourceBroker, Log, All);

FDWCEditorExclusiveBuildLease::FDWCEditorExclusiveBuildLease(
    TWeakPtr<FDWCEditorResourceBroker> InBroker,
    const FGuid InScopeId)
    : Broker(MoveTemp(InBroker))
    , ScopeId(InScopeId)
{
}

FDWCEditorExclusiveBuildLease::~FDWCEditorExclusiveBuildLease()
{
    Reset();
}

FDWCEditorExclusiveBuildLease::FDWCEditorExclusiveBuildLease(
    FDWCEditorExclusiveBuildLease&& Other) noexcept
    : Broker(MoveTemp(Other.Broker))
    , ScopeId(Other.ScopeId)
{
    Other.ScopeId.Invalidate();
}

FDWCEditorExclusiveBuildLease& FDWCEditorExclusiveBuildLease::operator=(
    FDWCEditorExclusiveBuildLease&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        Broker = MoveTemp(Other.Broker);
        ScopeId = Other.ScopeId;
        Other.ScopeId.Invalidate();
    }
    return *this;
}

void FDWCEditorExclusiveBuildLease::Reset()
{
    const FGuid ScopeToRelease = ScopeId;
    ScopeId.Invalidate();
    if (ScopeToRelease.IsValid())
    {
        if (const TSharedPtr<FDWCEditorResourceBroker> PinnedBroker = Broker.Pin())
        {
            PinnedBroker->EndExclusiveBuild(ScopeToRelease);
        }
    }
    Broker.Reset();
}

namespace
{
    uint64 GetPoolUsedBytes(
        const FDWCEditorResourceGovernorDiagnostics& Diagnostics,
        const EDWCEditorResourcePool Pool)
    {
        for (const FDWCEditorResourcePoolDiagnostics& PoolDiagnostics : Diagnostics.Pools)
        {
            if (PoolDiagnostics.Pool == Pool)
            {
                return PoolDiagnostics.UsedBytes;
            }
        }
        return 0;
    }

    uint64 GetPoolBudgetBytes(
        const FDWCEditorResourceGovernorDiagnostics& Diagnostics,
        const EDWCEditorResourcePool Pool)
    {
        for (const FDWCEditorResourcePoolDiagnostics& PoolDiagnostics : Diagnostics.Pools)
        {
            if (PoolDiagnostics.Pool == Pool)
            {
                return PoolDiagnostics.BudgetBytes;
            }
        }
        return 0;
    }

    bool PoolsCanRelieveEachOther(
        const EDWCEditorResourcePool RequestedPool,
        const EDWCEditorResourcePool ParticipantPool)
    {
        if (RequestedPool == EDWCEditorResourcePool::PreviewGPU)
        {
            return ParticipantPool == EDWCEditorResourcePool::PreviewGPU;
        }
        return FDWCEditorAsyncOperationContract::IsCPUResourcePool(RequestedPool) &&
            FDWCEditorAsyncOperationContract::IsCPUResourcePool(ParticipantPool);
    }

    FAutoConsoleCommand DumpResourceBrokerCommand(
        TEXT("dwc.Editor.ResourceBroker.Dump"),
        TEXT("Dumps the process-wide WCA editor resource broker state."),
        FConsoleCommandDelegate::CreateStatic([]
        {
            const TSharedRef<FDWCEditorResourceBroker> Broker = FDWCEditorResourceBroker::Get();
            const FDWCEditorResourceBrokerDiagnostics BrokerDiagnostics = Broker->GetDiagnostics();
            const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
                Broker->GetResourceGovernor()->GetDiagnostics();
            UE_LOG(
                LogDWCEditorResourceBroker,
                Display,
                TEXT("WCA resource broker: sessions=%d participants=%d CPU=%.2f/%.2f MiB "
                     "pressure=%llu successful=%llu reclaimed=%.2f MiB retiringGPU=%.2f MiB reentrant=%llu."),
                BrokerDiagnostics.SessionCount,
                BrokerDiagnostics.ParticipantCount,
                static_cast<double>(GovernorDiagnostics.GlobalCPUUsedBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                static_cast<double>(GovernorDiagnostics.GlobalCPUBudgetBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                BrokerDiagnostics.PressureRequestCount,
                BrokerDiagnostics.SuccessfulReclaimCount,
                static_cast<double>(BrokerDiagnostics.ImmediateReclaimedBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                static_cast<double>(BrokerDiagnostics.RetiringGPUBytes) /
                    FDWCEditorResourceBudgetConfig::MiB,
                BrokerDiagnostics.ReentrantPressureRejectCount);
            if (BrokerDiagnostics.PressureRequestCount > 0)
            {
                UE_LOG(
                    LogDWCEditorResourceBroker,
                    Display,
                    TEXT("  Last pressure: pool=%s requested=%.2f MiB target=%.2f MiB reclaimed=%.2f MiB candidates=%d reclaimable=%d ownerExcluded=%d."),
                    FDWCEditorAsyncOperationContract::LexToString(BrokerDiagnostics.LastRequestedPool),
                    static_cast<double>(BrokerDiagnostics.LastRequestedBytes) /
                        FDWCEditorResourceBudgetConfig::MiB,
                    static_cast<double>(BrokerDiagnostics.LastTargetBytes) /
                        FDWCEditorResourceBudgetConfig::MiB,
                    static_cast<double>(BrokerDiagnostics.LastImmediateReclaimedBytes) /
                        FDWCEditorResourceBudgetConfig::MiB,
                    BrokerDiagnostics.LastCandidateCount,
                    BrokerDiagnostics.LastReclaimableParticipantCount,
                    BrokerDiagnostics.LastOwnerExcludedCount);
            }
            if (BrokerDiagnostics.ExclusiveBuild.IsActive())
            {
                UE_LOG(
                    LogDWCEditorResourceBroker,
                    Display,
                    TEXT("  Exclusive Build: state=%d scope=%s asset='%s' action='%s' blockedPreview=%llu blockedBuild=%llu."),
                    static_cast<int32>(BrokerDiagnostics.ExclusiveBuild.State),
                    *BrokerDiagnostics.ExclusiveBuild.ScopeId.ToString(),
                    *BrokerDiagnostics.ExclusiveBuild.AssetPath,
                    *BrokerDiagnostics.ExclusiveBuild.DebugName,
                    BrokerDiagnostics.ExclusiveBuild.BlockedPreviewRequestCount,
                    BrokerDiagnostics.ExclusiveBuild.BlockedBuildRequestCount);
            }
        }));
}

TSharedRef<FDWCEditorResourceBroker> FDWCEditorResourceBroker::Get()
{
    static TSharedRef<FDWCEditorResourceBroker> Instance = []
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.bAllowCPUPoolBorrowing = true;
        Config.bEnableAdmissionFailureDiagnostics = true;
        return FDWCEditorResourceBroker::Create(Config);
    }();
    return Instance;
}

TSharedRef<FDWCEditorResourceBroker> FDWCEditorResourceBroker::Create(
    const FDWCEditorResourceBudgetConfig& InConfig)
{
    TSharedPtr<FDWCEditorResourceBroker> BrokerPtr =
        MakeShareable(new FDWCEditorResourceBroker(InConfig));
    TSharedRef<FDWCEditorResourceBroker> Broker = BrokerPtr.ToSharedRef();
    Broker->InitializePressureHandler();
    return Broker;
}

FDWCEditorResourceBroker::FDWCEditorResourceBroker(
    const FDWCEditorResourceBudgetConfig& InConfig)
    : BudgetConfig(InConfig)
    , ResourceGovernor(MakeShared<FDWCEditorResourceGovernor>(InConfig))
{
}

FDWCEditorResourceBroker::~FDWCEditorResourceBroker()
{
    ResourceGovernor->SetPressureHandler(nullptr);
}

void FDWCEditorResourceBroker::InitializePressureHandler()
{
    const TWeakPtr<FDWCEditorResourceBroker> WeakBroker = AsShared();
    ResourceGovernor->SetPressureHandler(
        [WeakBroker](const FDWCEditorResourceReservationRequest& Request)
        {
            const TSharedPtr<FDWCEditorResourceBroker> Broker = WeakBroker.Pin();
            return Broker.IsValid() && Broker->HandleAdmissionPressure(Request);
        });
}

FGuid FDWCEditorResourceBroker::OpenSession(const FString& DebugName)
{
    check(IsInGameThread());
    FScopeLock Lock(&Mutex);
    const FGuid SessionId = FGuid::NewGuid();
    FSessionState& Session = Sessions.Add(SessionId);
    Session.DebugName = DebugName;
    Session.OpenSerial = NextSerial++;
    Diagnostics.SessionCount = Sessions.Num();
    return SessionId;
}

void FDWCEditorResourceBroker::CloseSession(const FGuid& SessionId)
{
    check(IsInGameThread());
    TArray<FExclusiveBuildBarrierChanged> ResumeHandlers;
    {
        FScopeLock Lock(&Mutex);
        for (auto It = Participants.CreateIterator(); It; ++It)
        {
            if (It.Value().Descriptor.SessionId == SessionId)
            {
                It.RemoveCurrent();
            }
        }
        Sessions.Remove(SessionId);
        if (ExclusiveBuild.SessionId == SessionId)
        {
            ExclusiveBuild = FDWCEditorExclusiveBuildSnapshot();
            Diagnostics.ExclusiveBuild = ExclusiveBuild;
            for (const TPair<FGuid, FSessionState>& Pair : Sessions)
            {
                if (Pair.Value.BarrierChanged)
                {
                    ResumeHandlers.Add(Pair.Value.BarrierChanged);
                }
            }
        }
        Diagnostics.SessionCount = Sessions.Num();
        Diagnostics.ParticipantCount = Participants.Num();
    }
    for (FExclusiveBuildBarrierChanged& Handler : ResumeHandlers)
    {
        Handler(false);
    }
}

void FDWCEditorResourceBroker::SetSessionActive(const FGuid& SessionId, const bool bActive)
{
    FScopeLock Lock(&Mutex);
    if (FSessionState* Session = Sessions.Find(SessionId))
    {
        Session->bActive = bActive;
    }
}

void FDWCEditorResourceBroker::SetSessionBuildBarrierHooks(
    const FGuid& SessionId,
    FExclusiveBuildBarrierChanged BarrierChanged,
    FHasOutstandingInteractiveWork HasOutstandingInteractiveWork)
{
    check(IsInGameThread());
    FScopeLock Lock(&Mutex);
    if (FSessionState* Session = Sessions.Find(SessionId))
    {
        Session->BarrierChanged = MoveTemp(BarrierChanged);
        Session->HasOutstandingInteractiveWork = MoveTemp(HasOutstandingInteractiveWork);
    }
}

bool FDWCEditorResourceBroker::HasOutstandingInteractiveWork() const
{
    check(IsInGameThread());
    TArray<FHasOutstandingInteractiveWork> Queries;
    {
        FScopeLock Lock(&Mutex);
        for (const TPair<FGuid, FSessionState>& Pair : Sessions)
        {
            if (Pair.Value.HasOutstandingInteractiveWork)
            {
                Queries.Add(Pair.Value.HasOutstandingInteractiveWork);
            }
        }
    }
    return Queries.ContainsByPredicate(
        [](const FHasOutstandingInteractiveWork& Query)
        {
            return Query();
        });
}

TUniquePtr<FDWCEditorExclusiveBuildLease> FDWCEditorResourceBroker::TryBeginExclusiveBuild(
    const FDWCEditorExclusiveBuildRequest& Request,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    TArray<FExclusiveBuildBarrierChanged> SuspendHandlers;
    FGuid ScopeId;
    {
        FScopeLock Lock(&Mutex);
        if (!Request.SessionId.IsValid() || !Sessions.Contains(Request.SessionId) ||
            Request.DebugName.IsEmpty())
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("The exclusive Build request does not identify a live WCA editor session.");
            }
            return nullptr;
        }
        if (ExclusiveBuild.IsActive())
        {
            ++Diagnostics.ExclusiveBuildRejectCount;
            if (OutError != nullptr)
            {
                *OutError = FString::Printf(
                    TEXT("'%s' is already preparing or building '%s'. Wait for it to finish before starting another Build."),
                    *ExclusiveBuild.DebugName,
                    *ExclusiveBuild.AssetPath);
            }
            return nullptr;
        }

        ExclusiveBuild.ScopeId = FGuid::NewGuid();
        ExclusiveBuild.SessionId = Request.SessionId;
        ExclusiveBuild.AssetPath = Request.AssetPath;
        ExclusiveBuild.DebugName = Request.DebugName;
        ExclusiveBuild.State = EDWCEditorExclusiveBuildState::Draining;
        ExclusiveBuild.StartedSeconds = FPlatformTime::Seconds();
        ScopeId = ExclusiveBuild.ScopeId;
        ++Diagnostics.ExclusiveBuildAcquireCount;
        Diagnostics.ExclusiveBuild = ExclusiveBuild;
        for (const TPair<FGuid, FSessionState>& Pair : Sessions)
        {
            if (Pair.Value.BarrierChanged)
            {
                SuspendHandlers.Add(Pair.Value.BarrierChanged);
            }
        }
    }
    for (FExclusiveBuildBarrierChanged& Handler : SuspendHandlers)
    {
        Handler(true);
    }
    return TUniquePtr<FDWCEditorExclusiveBuildLease>(
        new FDWCEditorExclusiveBuildLease(AsShared(), ScopeId));
}

bool FDWCEditorResourceBroker::CanAdmitWork(
    const FGuid& SessionId,
    const EDWCEditorWorkClass WorkClass,
    const FGuid& ExclusiveBuildScopeId,
    FString* OutReason)
{
    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    FScopeLock Lock(&Mutex);
    if (!ExclusiveBuild.IsActive())
    {
        return true;
    }
    if (WorkClass == EDWCEditorWorkClass::ExclusiveBuild &&
        SessionId == ExclusiveBuild.SessionId &&
        ExclusiveBuildScopeId == ExclusiveBuild.ScopeId)
    {
        return true;
    }

    if (WorkClass == EDWCEditorWorkClass::InteractivePreview)
    {
        ++ExclusiveBuild.BlockedPreviewRequestCount;
        ++Diagnostics.ExclusiveBuildBlockedPreviewCount;
    }
    else
    {
        ++ExclusiveBuild.BlockedBuildRequestCount;
        ++Diagnostics.ExclusiveBuildBlockedActionCount;
    }
    Diagnostics.ExclusiveBuild = ExclusiveBuild;
    if (OutReason != nullptr)
    {
        *OutReason = FString::Printf(
            TEXT("The exclusive Build '%s' is using WCA editor resources for '%s'."),
            *ExclusiveBuild.DebugName,
            *ExclusiveBuild.AssetPath);
    }
    return false;
}

void FDWCEditorResourceBroker::SetExclusiveBuildState(
    const FGuid& ScopeId,
    const EDWCEditorExclusiveBuildState State)
{
    FScopeLock Lock(&Mutex);
    if (ExclusiveBuild.ScopeId == ScopeId)
    {
        ExclusiveBuild.State = State;
        Diagnostics.ExclusiveBuild = ExclusiveBuild;
    }
}

FDWCEditorExclusiveBuildSnapshot FDWCEditorResourceBroker::GetExclusiveBuildSnapshot() const
{
    FScopeLock Lock(&Mutex);
    return ExclusiveBuild;
}

void FDWCEditorResourceBroker::EndExclusiveBuild(const FGuid& ScopeId)
{
    TArray<FExclusiveBuildBarrierChanged> ResumeHandlers;
    {
        FScopeLock Lock(&Mutex);
        if (ExclusiveBuild.ScopeId != ScopeId)
        {
            return;
        }
        ExclusiveBuild = FDWCEditorExclusiveBuildSnapshot();
        Diagnostics.ExclusiveBuild = ExclusiveBuild;
        for (const TPair<FGuid, FSessionState>& Pair : Sessions)
        {
            if (Pair.Value.BarrierChanged)
            {
                ResumeHandlers.Add(Pair.Value.BarrierChanged);
            }
        }
    }
    for (FExclusiveBuildBarrierChanged& Handler : ResumeHandlers)
    {
        Handler(false);
    }
}

uint64 FDWCEditorResourceBroker::RegisterParticipant(
    FDWCEditorReclaimParticipantDescriptor Descriptor)
{
    check(IsInGameThread());
    if (!Descriptor.IsValid())
    {
        return 0;
    }

    FScopeLock Lock(&Mutex);
    const uint64 ParticipantId = NextParticipantId++;
    FParticipantState& Participant = Participants.Add(ParticipantId);
    Participant.Id = ParticipantId;
    Participant.RegistrationSerial = NextSerial++;
    Participant.Descriptor = MoveTemp(Descriptor);
    Diagnostics.ParticipantCount = Participants.Num();
    return ParticipantId;
}

void FDWCEditorResourceBroker::UnregisterParticipant(const uint64 ParticipantId)
{
    FScopeLock Lock(&Mutex);
    Participants.Remove(ParticipantId);
    Diagnostics.ParticipantCount = Participants.Num();
}

void FDWCEditorResourceBroker::UnregisterSessionParticipants(const FGuid& SessionId)
{
    FScopeLock Lock(&Mutex);
    for (auto It = Participants.CreateIterator(); It; ++It)
    {
        if (It.Value().Descriptor.SessionId == SessionId)
        {
            It.RemoveCurrent();
        }
    }
    Diagnostics.ParticipantCount = Participants.Num();
}

bool FDWCEditorResourceBroker::HandleAdmissionPressure(
    const FDWCEditorResourceReservationRequest& Request)
{
    check(IsInGameThread());

    TArray<FParticipantState> Candidates;
    TMap<FGuid, bool> SessionActivity;
    {
        FScopeLock Lock(&Mutex);
        ++Diagnostics.PressureRequestCount;
        if (bReclaimInProgress)
        {
            ++Diagnostics.ReentrantPressureRejectCount;
            return false;
        }
        bReclaimInProgress = true;
        for (const TPair<FGuid, FSessionState>& Pair : Sessions)
        {
            SessionActivity.Add(Pair.Key, Pair.Value.bActive);
        }
        Participants.GenerateValueArray(Candidates);
    }

    const auto EndReclaim = [this]
    {
        FScopeLock Lock(&Mutex);
        bReclaimInProgress = false;
    };

    const FDWCEditorResourceGovernorDiagnostics Before = ResourceGovernor->GetDiagnostics();
    uint64 TargetBytes = 0;
    if (Request.Pool == EDWCEditorResourcePool::PreviewGPU)
    {
        const uint64 Used = GetPoolUsedBytes(Before, Request.Pool);
        const uint64 Budget = GetPoolBudgetBytes(Before, Request.Pool);
        if (Request.Bytes > Budget)
        {
            EndReclaim();
            return false;
        }
        TargetBytes = Used <= Budget && Request.Bytes <= Budget - Used
            ? 0
            : Used + Request.Bytes - Budget;
    }
    else
    {
        if (Request.Bytes > Before.GlobalCPUBudgetBytes)
        {
            EndReclaim();
            return false;
        }
        TargetBytes = Before.GlobalCPUUsedBytes <= Before.GlobalCPUBudgetBytes &&
                Request.Bytes <= Before.GlobalCPUBudgetBytes - Before.GlobalCPUUsedBytes
            ? 0
            : Before.GlobalCPUUsedBytes + Request.Bytes - Before.GlobalCPUBudgetBytes;
    }

    if (TargetBytes == 0)
    {
        EndReclaim();
        return false;
    }

    int32 OwnerExcludedCount = 0;
    Candidates.RemoveAll(
        [&Request, &OwnerExcludedCount](const FParticipantState& Participant)
        {
            const bool bOwnerExcluded =
                !Request.Owner.Key.Namespace.IsNone() &&
                Participant.Descriptor.ReservationOwnerNamespace == Request.Owner.Key.Namespace &&
                Participant.Descriptor.ReservationSessionEpoch == Request.Owner.SessionEpoch;
            OwnerExcludedCount += bOwnerExcluded ? 1 : 0;
            return !Participant.Descriptor.IsValid() ||
                !PoolsCanRelieveEachOther(Request.Pool, Participant.Descriptor.Pool) ||
                bOwnerExcluded;
        });
    Candidates.Sort(
        [&SessionActivity](const FParticipantState& A, const FParticipantState& B)
        {
            const bool bAActive = SessionActivity.FindRef(A.Descriptor.SessionId);
            const bool bBActive = SessionActivity.FindRef(B.Descriptor.SessionId);
            if (bAActive != bBActive)
            {
                return !bAActive;
            }
            if (A.Descriptor.Priority != B.Descriptor.Priority)
            {
                return static_cast<uint8>(A.Descriptor.Priority) <
                    static_cast<uint8>(B.Descriptor.Priority);
            }
            return A.RegistrationSerial < B.RegistrationSerial;
        });

    uint64 TotalRetiringGPUBytes = 0;
    int32 ReclaimableParticipantCount = 0;
    for (const FParticipantState& Participant : Candidates)
    {
        const uint64 ReclaimableBytes = Participant.Descriptor.QueryReclaimableBytes();
        if (ReclaimableBytes == 0)
        {
            continue;
        }
        ++ReclaimableParticipantCount;

        const FDWCEditorResourceGovernorDiagnostics Current = ResourceGovernor->GetDiagnostics();
        const uint64 CurrentUsed = Request.Pool == EDWCEditorResourcePool::PreviewGPU
            ? GetPoolUsedBytes(Current, Request.Pool)
            : Current.GlobalCPUUsedBytes;
        const uint64 OriginalUsed = Request.Pool == EDWCEditorResourcePool::PreviewGPU
            ? GetPoolUsedBytes(Before, Request.Pool)
            : Before.GlobalCPUUsedBytes;
        const uint64 FreedSoFar = OriginalUsed >= CurrentUsed ? OriginalUsed - CurrentUsed : 0;
        if (FreedSoFar >= TargetBytes)
        {
            break;
        }

        FDWCEditorResourceReclaimRequest ReclaimRequest;
        ReclaimRequest.RequestedPool = Request.Pool;
        ReclaimRequest.TargetBytes = FMath::Min(TargetBytes - FreedSoFar, ReclaimableBytes);
        ReclaimRequest.RequestingSessionId = Request.Owner.SessionEpoch;
        ReclaimRequest.RequestOwnerNamespace = Request.Owner.Key.Namespace;
        const FDWCEditorResourceReclaimResult Result =
            Participant.Descriptor.Reclaim(ReclaimRequest);
        TotalRetiringGPUBytes += Result.RetiringGPUBytes;
    }

    const FDWCEditorResourceGovernorDiagnostics After = ResourceGovernor->GetDiagnostics();
    const uint64 BeforeUsed = Request.Pool == EDWCEditorResourcePool::PreviewGPU
        ? GetPoolUsedBytes(Before, Request.Pool)
        : Before.GlobalCPUUsedBytes;
    const uint64 AfterUsed = Request.Pool == EDWCEditorResourcePool::PreviewGPU
        ? GetPoolUsedBytes(After, Request.Pool)
        : After.GlobalCPUUsedBytes;
    const uint64 ImmediateFreedBytes = BeforeUsed >= AfterUsed ? BeforeUsed - AfterUsed : 0;
    {
        FScopeLock Lock(&Mutex);
        Diagnostics.ImmediateReclaimedBytes += ImmediateFreedBytes;
        Diagnostics.RetiringGPUBytes += TotalRetiringGPUBytes;
        Diagnostics.LastRequestedPool = Request.Pool;
        Diagnostics.LastRequestedBytes = Request.Bytes;
        Diagnostics.LastTargetBytes = TargetBytes;
        Diagnostics.LastImmediateReclaimedBytes = ImmediateFreedBytes;
        Diagnostics.LastCandidateCount = Candidates.Num();
        Diagnostics.LastReclaimableParticipantCount = ReclaimableParticipantCount;
        Diagnostics.LastOwnerExcludedCount = OwnerExcludedCount;
        if (ImmediateFreedBytes >= TargetBytes)
        {
            ++Diagnostics.SuccessfulReclaimCount;
        }
    }
    EndReclaim();
    return ImmediateFreedBytes >= TargetBytes;
}

FDWCEditorResourceBrokerDiagnostics FDWCEditorResourceBroker::GetDiagnostics() const
{
    FScopeLock Lock(&Mutex);
    FDWCEditorResourceBrokerDiagnostics Result = Diagnostics;
    Result.SessionCount = Sessions.Num();
    Result.ParticipantCount = Participants.Num();
    Result.ExclusiveBuild = ExclusiveBuild;
    return Result;
}

void FDWCEditorResourceBroker::ResetDiagnosticCounters()
{
    FScopeLock Lock(&Mutex);
    Diagnostics.PressureRequestCount = 0;
    Diagnostics.SuccessfulReclaimCount = 0;
    Diagnostics.ImmediateReclaimedBytes = 0;
    Diagnostics.RetiringGPUBytes = 0;
    Diagnostics.ReentrantPressureRejectCount = 0;
    Diagnostics.LastRequestedBytes = 0;
    Diagnostics.LastTargetBytes = 0;
    Diagnostics.LastImmediateReclaimedBytes = 0;
    Diagnostics.LastCandidateCount = 0;
    Diagnostics.LastReclaimableParticipantCount = 0;
    Diagnostics.LastOwnerExcludedCount = 0;
    Diagnostics.ExclusiveBuildAcquireCount = 0;
    Diagnostics.ExclusiveBuildRejectCount = 0;
    Diagnostics.ExclusiveBuildBlockedPreviewCount = 0;
    Diagnostics.ExclusiveBuildBlockedActionCount = 0;
    Diagnostics.ExclusiveBuild = ExclusiveBuild;
}
