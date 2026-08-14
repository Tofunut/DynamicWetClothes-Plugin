// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Assets/DWCEditorAssetResidency.h"

FDWCEditorAssetResidencyLease::FDWCEditorAssetResidencyLease(
    FDWCEditorAssetResidencyLease&& Other) noexcept
    : State(MoveTemp(Other.State))
    , Key(MoveTemp(Other.Key))
    , LeaseId(Other.LeaseId)
{
    Other.LeaseId = 0;
}

FDWCEditorAssetResidencyLease& FDWCEditorAssetResidencyLease::operator=(
    FDWCEditorAssetResidencyLease&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        State = MoveTemp(Other.State);
        Key = MoveTemp(Other.Key);
        LeaseId = Other.LeaseId;
        Other.LeaseId = 0;
    }
    return *this;
}

void FDWCEditorAssetResidencyLease::Reset()
{
    if (LeaseId == 0)
    {
        return;
    }
    check(IsInGameThread());
    if (const TSharedPtr<FDWCEditorAssetResidencyLeaseState> PinnedState = State.Pin();
        PinnedState.IsValid() && PinnedState->bAcceptReleases && PinnedState->ReleaseCallback)
    {
        PinnedState->ReleaseCallback(Key, LeaseId);
    }
    State.Reset();
    LeaseId = 0;
}

FDWCEditorAssetResidencyRegistry::FDWCEditorAssetResidencyRegistry()
    : LeaseState(MakeShared<FDWCEditorAssetResidencyLeaseState>())
{
    LeaseState->ReleaseCallback =
        [this](const FDWCEditorAssetResidencyKey& Key, const uint64 LeaseId)
        {
            Release(Key, LeaseId);
        };
}

FDWCEditorAssetResidencyRegistry::~FDWCEditorAssetResidencyRegistry()
{
    Shutdown();
}

FDWCEditorAssetResidencyLease FDWCEditorAssetResidencyRegistry::Acquire(
    UObject* Object,
    const EDWCEditorAssetResidencyDomain Domain,
    const FName Purpose)
{
    check(IsInGameThread());
    FDWCEditorAssetResidencyLease Lease;
    if (bShuttingDown || Object == nullptr)
    {
        return Lease;
    }

    FDWCEditorAssetResidencyKey Key;
    Key.Object = FObjectKey(Object);
    Key.Domain = Domain;
    Key.Purpose = Purpose;

    FEntry& Entry = Entries.FindOrAdd(Key);
    Entry.Object = Object;
    const uint64 LeaseId = NextLeaseId++;
    Entry.LeaseIds.Add(LeaseId);

    Lease.State = LeaseState;
    Lease.Key = Key;
    Lease.LeaseId = LeaseId;
    ++AcquireCount;
    return Lease;
}

void FDWCEditorAssetResidencyRegistry::ReleaseDomain(
    const EDWCEditorAssetResidencyDomain Domain)
{
    check(IsInGameThread());
    for (auto It = Entries.CreateIterator(); It; ++It)
    {
        if (It.Key().Domain == Domain)
        {
            ReleaseCount += static_cast<uint64>(It.Value().LeaseIds.Num());
            It.RemoveCurrent();
        }
    }
}

void FDWCEditorAssetResidencyRegistry::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    ++ShutdownCount;
    LeaseState->bAcceptReleases = false;
    LeaseState->ReleaseCallback = nullptr;
    Entries.Reset();
}

FDWCEditorAssetResidencyDiagnostics FDWCEditorAssetResidencyRegistry::GetDiagnostics() const
{
    FDWCEditorAssetResidencyDiagnostics Result;
    Result.ResidentObjectCount = Entries.Num();
    for (const TPair<FDWCEditorAssetResidencyKey, FEntry>& Pair : Entries)
    {
        Result.ActiveLeaseCount += Pair.Value.LeaseIds.Num();
    }
    Result.AcquireCount = AcquireCount;
    Result.ReleaseCount = ReleaseCount;
    Result.ShutdownCount = ShutdownCount;
    return Result;
}

void FDWCEditorAssetResidencyRegistry::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (TPair<FDWCEditorAssetResidencyKey, FEntry>& Pair : Entries)
    {
        Collector.AddReferencedObject(Pair.Value.Object);
    }
}

void FDWCEditorAssetResidencyRegistry::Release(
    const FDWCEditorAssetResidencyKey& Key,
    const uint64 LeaseId)
{
    check(IsInGameThread());
    FEntry* Entry = Entries.Find(Key);
    if (Entry == nullptr || Entry->LeaseIds.Remove(LeaseId) == 0)
    {
        return;
    }
    ++ReleaseCount;
    if (Entry->LeaseIds.IsEmpty())
    {
        Entries.Remove(Key);
    }
}

