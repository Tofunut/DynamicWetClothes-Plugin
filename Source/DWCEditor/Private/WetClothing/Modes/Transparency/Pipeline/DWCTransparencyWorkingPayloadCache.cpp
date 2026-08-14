// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyWorkingPayloadCache.h"

#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

FDWCTransparencyWorkingPayloadCache::FDWCTransparencyWorkingPayloadCache(
    const uint64 InBudgetBytes,
    const int32 InMaxEntries)
    : BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
    , MaxEntries(FMath::Max(InMaxEntries, 1))
{
}

TSharedPtr<FDWCTransparencySourcePayload>*
FDWCTransparencyWorkingPayloadCache::Find(const FGuid& LayerGuid)
{
    FEntry* Entry = Entries.Find(LayerGuid);
    if (Entry == nullptr)
    {
        return nullptr;
    }
    Touch(*Entry);
    return &Entry->Payload;
}

const TSharedPtr<FDWCTransparencySourcePayload>*
FDWCTransparencyWorkingPayloadCache::Find(const FGuid& LayerGuid) const
{
    FEntry* Entry = const_cast<TMap<FGuid, FEntry>&>(Entries).Find(LayerGuid);
    if (Entry == nullptr)
    {
        return nullptr;
    }
    Touch(*Entry);
    return &Entry->Payload;
}

bool FDWCTransparencyWorkingPayloadCache::Contains(const FGuid& LayerGuid) const
{
    return Find(LayerGuid) != nullptr;
}

TSharedPtr<FDWCTransparencySourcePayload>&
FDWCTransparencyWorkingPayloadCache::operator[](const FGuid& LayerGuid)
{
    TSharedPtr<FDWCTransparencySourcePayload>* Payload = Find(LayerGuid);
    check(Payload != nullptr);
    return *Payload;
}

void FDWCTransparencyWorkingPayloadCache::Add(
    const FGuid& LayerGuid,
    TSharedPtr<FDWCTransparencySourcePayload> Payload)
{
    if (!LayerGuid.IsValid() || !Payload.IsValid())
    {
        return;
    }

    Remove(LayerGuid);
    FEntry& Entry = Entries.Add(LayerGuid);
    Entry.Payload = MoveTemp(Payload);
    Entry.Bytes = Entry.Payload->GetAllocatedBytes();
    Touch(Entry);
    UsedBytes += Entry.Bytes;
    EnforceBounds(LayerGuid);
}

int32 FDWCTransparencyWorkingPayloadCache::Remove(const FGuid& LayerGuid)
{
    FEntry* Entry = Entries.Find(LayerGuid);
    if (Entry == nullptr)
    {
        return 0;
    }
    UsedBytes = UsedBytes >= Entry->Bytes ? UsedBytes - Entry->Bytes : 0;
    Entries.Remove(LayerGuid);
    return 1;
}

void FDWCTransparencyWorkingPayloadCache::Reset()
{
    Entries.Reset();
    UsedBytes = 0;
    UseSerial = 0;
}

uint64 FDWCTransparencyWorkingPayloadCache::GetReclaimableBytes(
    const FGuid& ProtectedLayerGuid) const
{
    uint64 Bytes = 0;
    for (const TPair<FGuid, FEntry>& Pair : Entries)
    {
        if (Pair.Key != ProtectedLayerGuid)
        {
            Bytes += Pair.Value.Bytes;
        }
    }
    return Bytes;
}

uint64 FDWCTransparencyWorkingPayloadCache::Reclaim(
    const uint64 TargetBytes,
    const FGuid& ProtectedLayerGuid)
{
    const uint64 Before = UsedBytes;
    while (Before - UsedBytes < TargetBytes &&
           EvictOldest(ProtectedLayerGuid.IsValid() ? &ProtectedLayerGuid : nullptr))
    {
    }
    return Before - UsedBytes;
}

void FDWCTransparencyWorkingPayloadCache::Touch(FEntry& Entry) const
{
    Entry.LastUsedSerial = ++UseSerial;
}

bool FDWCTransparencyWorkingPayloadCache::EvictOldest(
    const FGuid* ProtectedLayerGuid)
{
    const FGuid* OldestGuid = nullptr;
    uint64 OldestSerial = MAX_uint64;
    for (const TPair<FGuid, FEntry>& Pair : Entries)
    {
        if (ProtectedLayerGuid != nullptr && Pair.Key == *ProtectedLayerGuid)
        {
            continue;
        }
        if (Pair.Value.LastUsedSerial < OldestSerial)
        {
            OldestSerial = Pair.Value.LastUsedSerial;
            OldestGuid = &Pair.Key;
        }
    }
    if (OldestGuid == nullptr)
    {
        return false;
    }
    const FGuid Guid = *OldestGuid;
    Remove(Guid);
    return true;
}

void FDWCTransparencyWorkingPayloadCache::EnforceBounds(
    const FGuid& ProtectedLayerGuid)
{
    // A single valid payload may exceed the retention target. Its own resource
    // account already passed admission, so keep that one active entry while
    // preventing any additional retained payloads.
    while ((Entries.Num() > MaxEntries || UsedBytes > BudgetBytes) &&
           Entries.Num() > 1 &&
           EvictOldest(&ProtectedLayerGuid))
    {
    }
}
