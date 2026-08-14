// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCTransparencySourcePayload;

/**
 * Byte- and entry-bounded retention for canonical Stage 2 working payloads.
 * Payloads keep their own PreviewWorkspaceCPU account; this cache only owns
 * references and therefore never reserves the same pixels twice.
 */
class FDWCTransparencyWorkingPayloadCache final
{
public:
    static constexpr uint64 DefaultBudgetBytes = 512ull * 1024ull * 1024ull;
    static constexpr int32 DefaultMaxEntries = 2;

    explicit FDWCTransparencyWorkingPayloadCache(
        uint64 InBudgetBytes = DefaultBudgetBytes,
        int32 InMaxEntries = DefaultMaxEntries);

    TSharedPtr<FDWCTransparencySourcePayload>* Find(const FGuid& LayerGuid);
    const TSharedPtr<FDWCTransparencySourcePayload>* Find(const FGuid& LayerGuid) const;
    bool Contains(const FGuid& LayerGuid) const;
    TSharedPtr<FDWCTransparencySourcePayload>& operator[](const FGuid& LayerGuid);

    void Add(
        const FGuid& LayerGuid,
        TSharedPtr<FDWCTransparencySourcePayload> Payload);
    int32 Remove(const FGuid& LayerGuid);
    void Reset();

    uint64 GetUsedBytes() const { return UsedBytes; }
    uint64 GetBudgetBytes() const { return BudgetBytes; }
    int32 Num() const { return Entries.Num(); }
    uint64 GetReclaimableBytes(const FGuid& ProtectedLayerGuid = FGuid()) const;
    uint64 Reclaim(uint64 TargetBytes, const FGuid& ProtectedLayerGuid = FGuid());

private:
    struct FEntry
    {
        TSharedPtr<FDWCTransparencySourcePayload> Payload;
        uint64 Bytes = 0;
        mutable uint64 LastUsedSerial = 0;
    };

    void Touch(FEntry& Entry) const;
    bool EvictOldest(const FGuid* ProtectedLayerGuid = nullptr);
    void EnforceBounds(const FGuid& ProtectedLayerGuid);

    TMap<FGuid, FEntry> Entries;
    uint64 BudgetBytes = DefaultBudgetBytes;
    int32 MaxEntries = DefaultMaxEntries;
    uint64 UsedBytes = 0;
    mutable uint64 UseSerial = 0;
};
