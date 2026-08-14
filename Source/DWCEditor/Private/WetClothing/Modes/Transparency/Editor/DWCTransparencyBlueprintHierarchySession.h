//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"

struct FStreamableHandle;
class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;
struct FWetClothingTransparencyBlueprintSource;

enum class EDWCTransparencyBlueprintHierarchyState : uint8
{
    Unloaded,
    Loading,
    Ready,
    Error
};

struct FDWCTransparencyBlueprintHierarchySnapshot
{
    EDWCTransparencyBlueprintHierarchyState State =
        EDWCTransparencyBlueprintHierarchyState::Unloaded;
    FGuid LayerGuid;
    FSoftObjectPath BlueprintClassPath;
    TObjectPtr<UClass> LoadedClass = nullptr;
    FDWCTransparencyBlueprintHierarchyMetadata Hierarchy;
    FString Error;
    uint64 Revision = 0;

    bool Matches(const FGuid& InLayerGuid, const FSoftObjectPath& InClassPath) const
    {
        return LayerGuid == InLayerGuid && BlueprintClassPath == InClassPath;
    }

    bool IsReadyFor(const FGuid& InLayerGuid, const FSoftObjectPath& InClassPath) const
    {
        return State == EDWCTransparencyBlueprintHierarchyState::Ready &&
            Matches(InLayerGuid, InClassPath);
    }
};

struct FDWCTransparencyType2Readiness
{
    bool bReady = false;
    bool bTargetResolved = false;
    FString DisabledReason;
};

struct FDWCTransparencyType2BindingReconcileResult
{
    bool bChanged = false;
    bool bTargetResolved = false;
    bool bTargetAmbiguous = false;
    FString Status;
};

/**
 * Panel-session owner for the Type 2 Blueprint hierarchy. A class load reads
 * default component metadata without spawning an actor or snapshotting
 * materials. Full projection objects are created only by an explicit Generate.
 */
class FDWCTransparencyBlueprintHierarchySession final : public FGCObject,
    public TSharedFromThis<FDWCTransparencyBlueprintHierarchySession>
{
  public:
    DECLARE_MULTICAST_DELEGATE(FOnChanged);

    FDWCTransparencyBlueprintHierarchySession() = default;
    virtual ~FDWCTransparencyBlueprintHierarchySession() override;

    void Request(
        const FGuid& LayerGuid,
        const TSoftClassPtr<AActor>& BlueprintClass,
        bool bForceReload = false);
    void Reset();
    void CancelPendingRequest();

    const FDWCTransparencyBlueprintHierarchySnapshot& GetSnapshot() const
    {
        return Snapshot;
    }

    FOnChanged& OnChanged()
    {
        return Changed;
    }

    static FDWCTransparencyType2Readiness EvaluateReadiness(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot);
    static FDWCTransparencyType2Readiness EvaluateReadiness(
        const USkeletalMesh* RuntimeMesh,
        const USkeletalMesh* SourceMesh,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot);
    static FDWCTransparencyType2BindingReconcileResult ReconcileBindings(
        const USkeletalMesh* RuntimeMesh,
        const USkeletalMesh* SourceMesh,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyBlueprintHierarchySnapshot& Snapshot,
        FWetClothingTransparencyBlueprintSource& InOutSource);

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("FDWCTransparencyBlueprintHierarchySession");
    }

  private:
    void CompleteRequest(uint64 RequestGeneration);
    void PublishState(EDWCTransparencyBlueprintHierarchyState NewState, FString Error = FString());

    FDWCTransparencyBlueprintHierarchySnapshot Snapshot;
    TSharedPtr<FStreamableHandle> PendingLoad;
    uint64 RequestGeneration = 0;
    uint64 SnapshotRevision = 0;
    FOnChanged Changed;
};
