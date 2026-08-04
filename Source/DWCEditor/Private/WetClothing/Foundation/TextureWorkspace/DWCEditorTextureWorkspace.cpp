#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"

FDWCEditorTextureWorkspace::FDWCEditorTextureWorkspace(
    TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
    const uint64 InCPUBudgetBytes,
    const uint64 InGPUBudgetBytes)
    : UploadQueue(MoveTemp(InUploadQueue))
    , LeaseState(MakeShared<FDWCEditorTextureLeaseState>())
    , CPUBudgetBytes(FMath::Max<uint64>(InCPUBudgetBytes, 1))
    , GPUBudgetBytes(FMath::Max<uint64>(InGPUBudgetBytes, 1))
{
    LeaseState->ReleaseCallback = [this](const FDWCEditorTextureHandle& Entry, const uint64 LeaseId)
    {
        ReleaseTextureLease(Entry, LeaseId);
    };
}

FDWCEditorTextureWorkspace::~FDWCEditorTextureWorkspace()
{
    check(IsInGameThread());
    LeaseState->bAcceptReleases = false;
    LeaseState->ReleaseCallback = nullptr;
    Reset();
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::Acquire(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor)
{
    check(IsInGameThread());
    return FindOrCreateEntry(Key, Descriptor, true);
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::FindOrCreateEntry(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    const bool bInitializeBuffers)
{
    if (!Descriptor.IsValid())
    {
        return nullptr;
    }

    FDWCEditorTextureHandle* Existing = Entries.Find(Key);
    if (Existing != nullptr && Existing->IsValid() && (*Existing)->Descriptor == Descriptor)
    {
        ++AcquireHitCount;
        (*Existing)->LastUsedSerial = ++UseSerial;
        return *Existing;
    }

    if (Existing != nullptr)
    {
        ++TextureRecreateCount;
        RetireEntry(Key);
    }
    else
    {
        ++AcquireMissCount;
    }

    // Make room before publishing a new entry. Trimming after insertion can
    // evict the just-created, not-yet-leased entry and leave the caller with
    // an untracked transient resource.
    TrimToBudget(FDWCEditorTextureHandle());

    FDWCEditorTextureHandle Entry = MakeShared<FDWCEditorTextureWorkspaceEntry>();
    Entry->Key = Key;
    Entry->Descriptor = Descriptor;
    Entry->LastUsedSerial = ++UseSerial;
    if (bInitializeBuffers)
    {
        InitializeBuffers(Entry);
    }
    Entries.Add(Key, Entry);
    return Entry;
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::AcquireLease(const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    FDWCEditorTextureLease Lease;
    if (!Entry.IsValid())
    {
        return Lease;
    }

    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return Lease;
    }
    if (bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
    }

    ++Entry->ActiveLeaseCount;
    Lease.State = LeaseState;
    Lease.Entry = Entry;
    Lease.LeaseId = NextLeaseId++;
    return Lease;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishBGRA8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_B8G8R8A8 ||
        Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->BGRA8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    return Entry;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishG8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<uint8>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_G8 || Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->G8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    return Entry;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishNormalBGRA8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    FDWCEditorNormalRasterSurface&& WorkingSurface,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_B8G8R8A8 ||
        Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != Descriptor.WorkingSize)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->BGRA8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = MoveTemp(WorkingSurface);
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    return Entry;
}

void FDWCEditorTextureWorkspace::MarkDirty(
    const FDWCEditorTextureHandle& Entry,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    bool bDeferredInitialUpload = false;
    if (!Entry.IsValid() || DirtyRect.IsEmpty() ||
        !EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return;
    }
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    UploadQueue->Enqueue(
        Entry,
        bDeferredInitialUpload ? FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y) : DirtyRect,
        bDeferredInitialUpload ? false : bWrap,
        Priority);
}

void FDWCEditorTextureWorkspace::MarkDirty(
    const FDWCEditorTextureLease& Lease,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    MarkDirty(Lease.GetHandle(), DirtyRect, bWrap, Priority);
}

void FDWCEditorTextureWorkspace::RecreateWithAddressMode(
    const FDWCEditorTextureHandle& Entry,
    const TextureAddress AddressX,
    const TextureAddress AddressY)
{
    check(IsInGameThread());
    if (!Entry.IsValid() ||
        (Entry->Descriptor.AddressX == AddressX && Entry->Descriptor.AddressY == AddressY))
    {
        return;
    }
    UploadQueue->Cancel(Entry->Key);
    Entry->Descriptor.AddressX = AddressX;
    Entry->Descriptor.AddressY = AddressY;
    ++TextureRecreateCount;

    // Keep the UTexture2D object stable. Preview MIDs already point at this
    // object, so replacing it would require every consumer to rebind after the
    // release fence. UpdateResource recreates its RHI resource in place; the
    // full queued upload below restores the current CPU pixels afterwards.
    if (Entry->IsGPUResident() && Entry->Texture != nullptr)
    {
        Entry->Texture->AddressX = AddressX;
        Entry->Texture->AddressY = AddressY;
        Entry->Texture->UpdateResource();
        ++Entry->ResourceGeneration;
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
        return;
    }

    bool bDeferredInitialUpload = false;
    if (EnsureTexture(Entry, true, &bDeferredInitialUpload) && bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
    }
}

void FDWCEditorTextureWorkspace::RecreateWithAddressMode(
    const FDWCEditorTextureLease& Lease,
    const TextureAddress AddressX,
    const TextureAddress AddressY)
{
    RecreateWithAddressMode(Lease.GetHandle(), AddressX, AddressY);
}

void FDWCEditorTextureWorkspace::Invalidate(const FDWCEditorTextureKey& Key)
{
    check(IsInGameThread());
    RetireEntry(Key);
}

void FDWCEditorTextureWorkspace::Discard(const FDWCEditorTextureLease& Lease)
{
    check(IsInGameThread());
    if (!Lease.IsValid())
    {
        return;
    }

    // Retire by entry identity rather than by key. A new preview may already
    // have reused the same key while an older lease is being released.
    RetireEntry(Lease.GetHandle());
}

void FDWCEditorTextureWorkspace::InvalidateOwner(const UObject* Owner)
{
    check(IsInGameThread());
    if (Owner == nullptr)
    {
        return;
    }
    const FObjectKey OwnerKey(Owner);
    UploadQueue->CancelOwner(Owner);
    TArray<FDWCEditorTextureKey> KeysToRetire;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Key.Owner == OwnerKey)
        {
            KeysToRetire.Add(Pair.Key);
        }
    }
    for (const FDWCEditorTextureKey& Key : KeysToRetire)
    {
        RetireEntry(Key);
    }
}

void FDWCEditorTextureWorkspace::Reset()
{
    check(IsInGameThread());
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        UploadQueue->Cancel(Pair.Key);
    }
    TArray<FDWCEditorTextureKey> Keys;
    Entries.GetKeys(Keys);
    for (const FDWCEditorTextureKey& Key : Keys)
    {
        RetireEntry(Key);
    }
    ProcessRetiredGPUResources();
}

void FDWCEditorTextureWorkspace::TrimToBudget()
{
    check(IsInGameThread());
    TrimToBudget(FDWCEditorTextureHandle());
}

void FDWCEditorTextureWorkspace::TrimToBudget(const FDWCEditorTextureHandle& ProtectedEntry)
{
    check(IsInGameThread());
    ProcessRetiredGPUResources();

    uint64 UsedGPUBytes = CalculateGPUUsedBytes();
    while (UsedGPUBytes > GPUBudgetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value != ProtectedEntry &&
                Pair.Value->ActiveLeaseCount == 0 && Pair.Value->IsGPUResident() &&
                Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value->LastUsedSerial;
                OldestKey = &Pair.Key;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }
        const FDWCEditorTextureHandle Entry = Entries.FindRef(*OldestKey);
        if (!BeginGPUResourceRetire(Entry))
        {
            break;
        }
        UsedGPUBytes = CalculateGPUUsedBytes();
    }

    uint64 UsedCPUBytes = CalculateCPUUsedBytes();
    while (UsedCPUBytes > CPUBudgetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value != ProtectedEntry &&
                Pair.Value->ActiveLeaseCount == 0 && Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value->LastUsedSerial;
                OldestKey = &Pair.Key;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }
        RemoveEntry(*OldestKey, true);
        UsedCPUBytes = CalculateCPUUsedBytes();
    }
}

bool FDWCEditorTextureWorkspace::EnsureTexture(
    const FDWCEditorTextureHandle& Entry,
    const bool bDeferLargeInitialUpload,
    bool* const bOutDeferredInitialUpload)
{
    check(IsInGameThread());
    if (bOutDeferredInitialUpload != nullptr)
    {
        *bOutDeferredInitialUpload = false;
    }
    if (!Entry.IsValid() || !Entry->Descriptor.IsValid())
    {
        return false;
    }
    if (Entry->IsGPUResident())
    {
        return true;
    }
    if (Entry->IsGPURetiring())
    {
        return false;
    }

    TrimToBudget(Entry);
    const uint64 NewResourceBytes = static_cast<uint64>(Entry->Descriptor.Size.X) *
        static_cast<uint64>(Entry->Descriptor.Size.Y) *
        static_cast<uint64>(Entry->Descriptor.GetBytesPerPixel());
    if (CalculateGPUUsedBytes() + NewResourceBytes > GPUBudgetBytes)
    {
        ++GPUBudgetRejectCount;
        return false;
    }

    Entry->Texture = UTexture2D::CreateTransient(
        Entry->Descriptor.Size.X,
        Entry->Descriptor.Size.Y,
        Entry->Descriptor.PixelFormat);
    if (Entry->Texture == nullptr || Entry->Texture->GetPlatformData() == nullptr ||
        !Entry->Texture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        Entry->Texture = nullptr;
        return false;
    }

    UTexture2D* Texture = Entry->Texture;
    Texture->SRGB = Entry->Descriptor.bSRGB;
    Texture->CompressionSettings = Entry->Descriptor.CompressionSettings;
    Texture->MipGenSettings = Entry->Descriptor.MipGenSettings;
    Texture->Filter = Entry->Descriptor.Filter;
    Texture->AddressX = Entry->Descriptor.AddressX;
    Texture->AddressY = Entry->Descriptor.AddressY;
    Texture->LODGroup = Entry->Descriptor.LODGroup;
    Texture->NeverStream = true;

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    const int64 ExpectedBytes = static_cast<int64>(Entry->Descriptor.Size.X) *
        Entry->Descriptor.Size.Y * Entry->Descriptor.GetBytesPerPixel();
    if (MipData == nullptr || Entry->GetPixelDataBytes() != ExpectedBytes)
    {
        Mip.BulkData.Unlock();
        Entry->Texture = nullptr;
        return false;
    }
    const bool bShouldDeferUpload = bDeferLargeInitialUpload &&
        ExpectedBytes > static_cast<int64>(FDWCEditorRenderUploadQueue::DefaultPerFlushBudgetBytes);
    if (bShouldDeferUpload)
    {
        // Resource creation must happen on the game thread, but copying a 4K
        // CPU image into its initial mip does not. Initialize a descriptor-
        // appropriate neutral resource and let the budgeted upload queue fill
        // it in rows. A zeroed normal map is not a flat tangent normal.
        if (Entry->Descriptor.PixelFormat == PF_B8G8R8A8)
        {
            FColor* InitialPixels = static_cast<FColor*>(MipData);
            const int64 PixelCount = static_cast<int64>(Entry->Descriptor.Size.X) * Entry->Descriptor.Size.Y;
            for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
            {
                InitialPixels[PixelIndex] = Entry->Descriptor.InitialBGRA8;
            }
        }
        else
        {
            FMemory::Memset(MipData, Entry->Descriptor.InitialG8, ExpectedBytes);
        }
        if (bOutDeferredInitialUpload != nullptr)
        {
            *bOutDeferredInitialUpload = true;
        }
    }
    else
    {
        FMemory::Memcpy(MipData, Entry->GetPixelData(), ExpectedBytes);
    }
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    Entry->GPUState = EDWCEditorTextureGPUState::Resident;
    ++Entry->ResourceGeneration;
    ++TextureCreateCount;
    UpdateGPUHighWaterMark();
    return true;
}

void FDWCEditorTextureWorkspace::InitializeBuffers(const FDWCEditorTextureHandle& Entry)
{
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    const int32 PixelCount = Entry->Descriptor.Size.X * Entry->Descriptor.Size.Y;
    if (Entry->Descriptor.PixelFormat == PF_G8)
    {
        Entry->G8Pixels.Init(Entry->Descriptor.InitialG8, PixelCount);
    }
    else
    {
        Entry->BGRA8Pixels.Init(Entry->Descriptor.InitialBGRA8, PixelCount);
    }
}

void FDWCEditorTextureWorkspace::RemoveEntry(
    const FDWCEditorTextureKey& Key,
    const bool bCountEviction)
{
    const FDWCEditorTextureHandle Entry = Entries.FindRef(Key);
    if (!Entry.IsValid())
    {
        return;
    }

    RetireEntry(Entry);
    if (bCountEviction)
    {
        ++EvictionCount;
    }
}

void FDWCEditorTextureWorkspace::RetireEntry(const FDWCEditorTextureKey& Key)
{
    // Take a strong local copy before RetireEntry(handle) erases the map key.
    // Passing the TMap value by reference here would leave that reference
    // dangling as soon as Entries.Remove() destroys the map element.
    const FDWCEditorTextureHandle Entry = Entries.FindRef(Key);
    if (!Entry.IsValid())
    {
        return;
    }

    RetireEntry(Entry);
}

void FDWCEditorTextureWorkspace::RetireEntry(const FDWCEditorTextureHandle& Entry)
{
    if (!Entry.IsValid() || Entry->bRetired)
    {
        return;
    }

    const FDWCEditorTextureHandle* CurrentEntry = Entries.Find(Entry->Key);
    if (CurrentEntry != nullptr && *CurrentEntry == Entry)
    {
        UploadQueue->Cancel(Entry->Key);
        Entries.Remove(Entry->Key);
    }

    Entry->bRetired = true;
    if (Entry->ActiveLeaseCount == 0)
    {
        ReleaseEntryCPUStorage(Entry);
    }
    BeginGPUResourceRetire(Entry);
    if (Entry->ActiveLeaseCount > 0 || Entry->IsGPURetiring())
    {
        RetiredEntries.AddUnique(Entry);
    }
}

bool FDWCEditorTextureWorkspace::BeginGPUResourceRetire(const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    if (!Entry.IsValid() || Entry->IsGPURetiring())
    {
        return false;
    }
    if (!Entry->IsGPUResident() || Entry->Texture == nullptr)
    {
        Entry->GPUState = EDWCEditorTextureGPUState::CPUOnly;
        return false;
    }

    UploadQueue->Cancel(Entry->Key);
    Entry->GPUState = EDWCEditorTextureGPUState::Retiring;
    Entry->GPUReleaseFence = MakeUnique<FRenderCommandFence>();
    Entry->Texture->ReleaseResource();
    Entry->GPUReleaseFence->BeginFence();
    ++GPUResourceRetireCount;
    return true;
}

void FDWCEditorTextureWorkspace::ReleaseEntryCPUStorage(const FDWCEditorTextureHandle& Entry)
{
    if (!Entry.IsValid())
    {
        return;
    }

    Entry->BGRA8Pixels.Empty();
    Entry->G8Pixels.Empty();
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
}

void FDWCEditorTextureWorkspace::ProcessRetiredGPUResources()
{
    check(IsInGameThread());
    const auto ProcessEntry = [this](const FDWCEditorTextureHandle& Entry)
    {
        if (!Entry.IsValid() || !Entry->IsGPURetiring() || !Entry->GPUReleaseFence.IsValid() ||
            !Entry->GPUReleaseFence->IsFenceComplete())
        {
            return;
        }

        Entry->Texture = nullptr;
        Entry->GPUReleaseFence.Reset();
        Entry->GPUState = EDWCEditorTextureGPUState::CPUOnly;
        ++GPUResourceReleaseCompleteCount;
    };

    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        ProcessEntry(Pair.Value);
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        ProcessEntry(Entry);
    }
    RemoveCompletedRetiredEntries();
}

void FDWCEditorTextureWorkspace::RemoveCompletedRetiredEntries()
{
    RetiredEntries.RemoveAllSwap(
        [](const FDWCEditorTextureHandle& Entry)
        {
            return !Entry.IsValid() ||
                (Entry->bRetired && Entry->ActiveLeaseCount == 0 && !Entry->IsGPURetiring());
        },
        EAllowShrinking::No);
}

void FDWCEditorTextureWorkspace::ReleaseTextureLease(
    const FDWCEditorTextureHandle& Entry,
    const uint64 LeaseId)
{
    check(IsInGameThread());
    (void)LeaseId;
    if (!Entry.IsValid() || Entry->ActiveLeaseCount == 0)
    {
        return;
    }

    --Entry->ActiveLeaseCount;
    if (Entry->ActiveLeaseCount == 0 && Entry->bRetired)
    {
        ReleaseEntryCPUStorage(Entry);
        RemoveCompletedRetiredEntries();
    }
    TrimToBudget();
}

uint64 FDWCEditorTextureWorkspace::CalculateCPUUsedBytes() const
{
    uint64 UsedBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            UsedBytes += Pair.Value->GetAllocatedSizeBytes();
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            UsedBytes += Entry->GetAllocatedSizeBytes();
        }
    }
    return UsedBytes;
}

uint64 FDWCEditorTextureWorkspace::CalculateGPUUsedBytes() const
{
    uint64 UsedBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            UsedBytes += Pair.Value->GetEstimatedGPUBytes();
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            UsedBytes += Entry->GetEstimatedGPUBytes();
        }
    }
    return UsedBytes;
}

FDWCEditorTextureWorkspace::FGPUResidencyStats FDWCEditorTextureWorkspace::CollectGPUResidencyStats() const
{
    FGPUResidencyStats Stats;
    const auto AccumulateEntry = [&Stats](const FDWCEditorTextureHandle& Entry)
    {
        if (!Entry.IsValid())
        {
            return;
        }

        switch (Entry->GetGPUState())
        {
        case EDWCEditorTextureGPUState::Resident:
            ++Stats.ResidentEntryCount;
            Stats.ResidentBytes += Entry->GetEstimatedGPUBytes();
            Stats.ResidentLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
            break;
        case EDWCEditorTextureGPUState::Retiring:
            ++Stats.RetiringEntryCount;
            Stats.RetiringBytes += Entry->GetEstimatedGPUBytes();
            Stats.RetiringLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
            break;
        case EDWCEditorTextureGPUState::CPUOnly:
        default:
            ++Stats.CPUOnlyEntryCount;
            break;
        }
    };

    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        AccumulateEntry(Pair.Value);
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        AccumulateEntry(Entry);
    }
    return Stats;
}

void FDWCEditorTextureWorkspace::UpdateGPUHighWaterMark()
{
    GPUHighWaterBytes = FMath::Max(GPUHighWaterBytes, CalculateGPUUsedBytes());
}

void FDWCEditorTextureWorkspace::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    auto AddBucket = [this, &OutBuckets](
                         const FString& Name,
                         const uint64 UsedBytes,
                         const uint64 BudgetBytes,
                         const int32 EntryCount,
                         const int32 ActiveLeaseCount,
                         const int32 RetiredEntryCount)
    {
        FDWCEditorPreviewMemoryBucket& Bucket = OutBuckets.AddDefaulted_GetRef();
        Bucket.Name = Name;
        Bucket.UsedBytes = UsedBytes;
        Bucket.BudgetBytes = BudgetBytes;
        Bucket.EntryCount = EntryCount;
        Bucket.ActiveLeaseCount = ActiveLeaseCount;
        Bucket.RetiredEntryCount = RetiredEntryCount;
        Bucket.HitCount = AcquireHitCount;
        Bucket.MissCount = AcquireMissCount;
        Bucket.EvictionCount = EvictionCount;
    };

    const int32 TotalEntryCount = Entries.Num() + RetiredEntries.Num();
    int32 TotalLeaseCount = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            TotalLeaseCount += static_cast<int32>(Pair.Value->GetActiveLeaseCount());
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            TotalLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
        }
    }

    const FGPUResidencyStats GPUStats = CollectGPUResidencyStats();
    const uint64 CPUUsedBytes = CalculateCPUUsedBytes();
    const uint64 GPUUsedBytes = GPUStats.ResidentBytes + GPUStats.RetiringBytes;

    // Keep aggregate buckets for existing diagnostics consumers, then add the
    // GPU lifecycle split used to investigate VRAM pressure after PIE.
    AddBucket(
        TEXT("Editor texture workspace"),
        CPUUsedBytes + GPUUsedBytes,
        CPUBudgetBytes + GPUBudgetBytes,
        TotalEntryCount,
        TotalLeaseCount,
        RetiredEntries.Num());
    AddBucket(TEXT("Editor texture workspace CPU"), CPUUsedBytes, CPUBudgetBytes, TotalEntryCount, TotalLeaseCount, RetiredEntries.Num());
    AddBucket(TEXT("Editor texture workspace GPU"), GPUUsedBytes, GPUBudgetBytes, TotalEntryCount, TotalLeaseCount, GPUStats.RetiringEntryCount);
    AddBucket(
        TEXT("Preview GPU resident resources"),
        GPUStats.ResidentBytes,
        GPUBudgetBytes,
        GPUStats.ResidentEntryCount,
        GPUStats.ResidentLeaseCount,
        0);
    AddBucket(
        TEXT("Preview GPU retiring resources"),
        GPUStats.RetiringBytes,
        GPUBudgetBytes,
        GPUStats.RetiringEntryCount,
        GPUStats.RetiringLeaseCount,
        GPUStats.RetiringEntryCount);
    AddBucket(
        TEXT("Preview GPU CPU-only workspace entries"),
        0,
        0,
        GPUStats.CPUOnlyEntryCount,
        0,
        0);
    AddBucket(TEXT("Preview GPU residency high-water"), GPUHighWaterBytes, GPUBudgetBytes, 0, 0, 0);
}

void FDWCEditorTextureWorkspace::AppendDiagnosticOperationCounters(
    TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const
{
    FDWCEditorPreviewOperationCounter& Creates = OutCounters.AddDefaulted_GetRef();
    Creates.Name = TEXT("Transient preview texture creates");
    Creates.Count = TextureCreateCount;

    FDWCEditorPreviewOperationCounter& Recreates = OutCounters.AddDefaulted_GetRef();
    Recreates.Name = TEXT("Transient preview texture recreates");
    Recreates.Count = TextureRecreateCount;

    FDWCEditorPreviewOperationCounter& Retires = OutCounters.AddDefaulted_GetRef();
    Retires.Name = TEXT("Transient GPU resource retires");
    Retires.Count = GPUResourceRetireCount;

    FDWCEditorPreviewOperationCounter& RetireCompletions = OutCounters.AddDefaulted_GetRef();
    RetireCompletions.Name = TEXT("Transient GPU resource retire completions");
    RetireCompletions.Count = GPUResourceReleaseCompleteCount;

    FDWCEditorPreviewOperationCounter& GPURejects = OutCounters.AddDefaulted_GetRef();
    GPURejects.Name = TEXT("Preview GPU budget rejects");
    GPURejects.Count = GPUBudgetRejectCount;

    const FGPUResidencyStats GPUStats = CollectGPUResidencyStats();
    FDWCEditorPreviewOperationCounter& ResidentEntries = OutCounters.AddDefaulted_GetRef();
    ResidentEntries.Name = TEXT("Preview GPU resident entries");
    ResidentEntries.Count = GPUStats.ResidentEntryCount;
    ResidentEntries.Bytes = GPUStats.ResidentBytes;

    FDWCEditorPreviewOperationCounter& RetiringEntries = OutCounters.AddDefaulted_GetRef();
    RetiringEntries.Name = TEXT("Preview GPU retiring entries");
    RetiringEntries.Count = GPUStats.RetiringEntryCount;
    RetiringEntries.Bytes = GPUStats.RetiringBytes;

    FDWCEditorPreviewOperationCounter& HighWater = OutCounters.AddDefaulted_GetRef();
    HighWater.Name = TEXT("Preview GPU residency high-water");
    HighWater.Count = 1;
    HighWater.Bytes = GPUHighWaterBytes;
}

void FDWCEditorTextureWorkspace::ResetDiagnosticCounters()
{
    AcquireHitCount = 0;
    AcquireMissCount = 0;
    EvictionCount = 0;
    TextureCreateCount = 0;
    TextureRecreateCount = 0;
    GPUResourceRetireCount = 0;
    GPUResourceReleaseCompleteCount = 0;
    GPUBudgetRejectCount = 0;
    GPUHighWaterBytes = CalculateGPUUsedBytes();
}

void FDWCEditorTextureWorkspace::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            Collector.AddReferencedObject(Pair.Value->Texture);
        }
    }
    for (FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            Collector.AddReferencedObject(Entry->Texture);
        }
    }
}
