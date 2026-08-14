// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"

enum class EDWCEditorAssetResidencyDomain : uint8
{
    Session,
    WetPart,
    Wrinkle,
    Transparency,
    Build
};

struct FDWCEditorAssetResidencyDiagnostics
{
    int32 ResidentObjectCount = 0;
    int32 ActiveLeaseCount = 0;
    uint64 AcquireCount = 0;
    uint64 ReleaseCount = 0;
    uint64 ShutdownCount = 0;
};

struct FDWCEditorAssetResidencyKey
{
    FObjectKey Object;
    EDWCEditorAssetResidencyDomain Domain = EDWCEditorAssetResidencyDomain::Session;
    FName Purpose;

    bool operator==(const FDWCEditorAssetResidencyKey& Other) const
    {
        return Object == Other.Object && Domain == Other.Domain && Purpose == Other.Purpose;
    }

    friend uint32 GetTypeHash(const FDWCEditorAssetResidencyKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.Object);
        Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.Domain)));
        return HashCombine(Hash, GetTypeHash(Key.Purpose));
    }
};

class FDWCEditorAssetResidencyLeaseState final
{
  public:
    bool bAcceptReleases = true;
    TFunction<void(const FDWCEditorAssetResidencyKey&, uint64)> ReleaseCallback;
};

/** Game-thread RAII token that keeps one editor asset resident for a declared purpose. */
class FDWCEditorAssetResidencyLease final
{
  public:
    FDWCEditorAssetResidencyLease() = default;
    ~FDWCEditorAssetResidencyLease() { Reset(); }

    FDWCEditorAssetResidencyLease(const FDWCEditorAssetResidencyLease&) = delete;
    FDWCEditorAssetResidencyLease& operator=(const FDWCEditorAssetResidencyLease&) = delete;
    FDWCEditorAssetResidencyLease(FDWCEditorAssetResidencyLease&& Other) noexcept;
    FDWCEditorAssetResidencyLease& operator=(FDWCEditorAssetResidencyLease&& Other) noexcept;

    bool IsValid() const { return LeaseId != 0 && State.IsValid(); }
    void Reset();

  private:
    friend class FDWCEditorAssetResidencyRegistry;

    TWeakPtr<FDWCEditorAssetResidencyLeaseState> State;
    FDWCEditorAssetResidencyKey Key;
    uint64 LeaseId = 0;
};

/** WCA editor-session GC owner for loaded source assets and transient snapshots. */
class FDWCEditorAssetResidencyRegistry final : public FGCObject
{
  public:
    FDWCEditorAssetResidencyRegistry();
    virtual ~FDWCEditorAssetResidencyRegistry() override;

    FDWCEditorAssetResidencyLease Acquire(
        UObject* Object,
        EDWCEditorAssetResidencyDomain Domain,
        FName Purpose);
    void ReleaseDomain(EDWCEditorAssetResidencyDomain Domain);
    void Shutdown();

    FDWCEditorAssetResidencyDiagnostics GetDiagnostics() const;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("FDWCEditorAssetResidencyRegistry");
    }

  private:
    struct FEntry
    {
        TObjectPtr<UObject> Object = nullptr;
        TSet<uint64> LeaseIds;
    };

    void Release(const FDWCEditorAssetResidencyKey& Key, uint64 LeaseId);

    TMap<FDWCEditorAssetResidencyKey, FEntry> Entries;
    TSharedRef<FDWCEditorAssetResidencyLeaseState> LeaseState;
    uint64 NextLeaseId = 1;
    uint64 AcquireCount = 0;
    uint64 ReleaseCount = 0;
    uint64 ShutdownCount = 0;
    bool bShuttingDown = false;
};

