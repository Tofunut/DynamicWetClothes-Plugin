//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Resources/DWCEditorAccountedMemory.h"

void FDWCEditorAccountedMemory::Configure(
    TSharedPtr<FDWCEditorResourceGovernor> InGovernor,
    const EDWCEditorResourcePool InPool,
    const FDWCEditorAsyncOperationIdentity& InOwner,
    FString InDebugName)
{
    check(ActualBytes == 0 && !MemoryLease.IsValid());
    ResourceGovernor = MoveTemp(InGovernor);
    Pool = InPool;
    Owner = InOwner;
    DebugName = MoveTemp(InDebugName);
}

bool FDWCEditorAccountedMemory::TryAdoptActualBytes(
    const uint64 NewActualBytes,
    FString* OutError)
{
    if (NewActualBytes == ActualBytes)
    {
        return true;
    }

    if (!ResourceGovernor.IsValid())
    {
        ActualBytes = NewActualBytes;
        return true;
    }

    if (NewActualBytes == 0)
    {
        MemoryLease.Reset();
        ActualBytes = 0;
        return true;
    }

    if (MemoryLease.IsValid())
    {
        if (!MemoryLease.TryResize(NewActualBytes, OutError))
        {
            return false;
        }
        ActualBytes = NewActualBytes;
        return true;
    }

    FDWCEditorResourceReservationRequest Request;
    Request.Pool = Pool;
    Request.Bytes = NewActualBytes;
    Request.Owner = Owner;
    Request.DebugName = DebugName.IsEmpty() ? TEXT("Accounted editor buffer") : DebugName;
    FDWCEditorMemoryLease NewLease = ResourceGovernor->TryAcquire(Request, OutError);
    if (!NewLease.IsValid())
    {
        return false;
    }

    MemoryLease = MoveTemp(NewLease);
    ActualBytes = NewActualBytes;
    return true;
}

bool FDWCEditorAccountedMemory::AdoptExistingLease(
    FDWCEditorMemoryLease&& InLease,
    const uint64 InActualBytes,
    FString* OutError)
{
    if (ActualBytes != 0 || MemoryLease.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("An accounted allocation cannot replace an existing reservation.");
        }
        return false;
    }
    if (InActualBytes == 0)
    {
        InLease.Reset();
        return true;
    }
    if (!InLease.IsValid() || InLease.GetPool() != Pool ||
        InLease.GetReservedBytes() < InActualBytes)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The transferred reservation does not cover the accounted allocation.");
        }
        return false;
    }

    MemoryLease = MoveTemp(InLease);
    if (MemoryLease.GetReservedBytes() != InActualBytes &&
        !MemoryLease.TryResize(InActualBytes, OutError))
    {
        MemoryLease.Reset();
        return false;
    }
    ActualBytes = InActualBytes;
    return true;
}

void FDWCEditorAccountedMemory::Reset()
{
    MemoryLease.Reset();
    ActualBytes = 0;
}
