//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorPreviewModeLifetime.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialCache.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSessionTypes.h"

struct FPropertyChangedEvent;
class UMaterialInterface;
class UObject;
class UWetClothingAsset;
class UWorld;

/** Why an editor preview session released its transient rendering state. */
enum class EDWCEditorPreviewSuspendReason : uint8
{
    ModeSwitch,
    HostInactive,
    BeginPIE,
    ExclusiveBuild,
    EditorClosing
};

/**
 * Owns common editor-preview material state for one editor viewport/session.
 * Mode-specific render targets, brushes, hit tests, and mesh visibility remain
 * owned by the Wrinkle or Transparency viewport.
 */
class FDWCEditorPreviewSession final
{
  public:
    static constexpr int32 AllWettableSlots = INDEX_NONE;

    FDWCEditorPreviewSession() = default;
    ~FDWCEditorPreviewSession();

    FDWCEditorPreviewSession(const FDWCEditorPreviewSession&) = delete;
    FDWCEditorPreviewSession& operator=(const FDWCEditorPreviewSession&) = delete;

    void Initialize(
        UWetClothingAsset* WetClothingAsset,
        UWorld* PreviewWorld,
        const FDWCEditorPreviewSessionConfig& Config);
    void Shutdown();

    bool IsInitialized() const;
    bool IsSuspended() const { return bSuspended; }
    uint64 GetLifecycleGeneration() const { return LifecycleGeneration; }
    EDWCEditorPreviewSuspendReason GetLastSuspendReason() const { return LastSuspendReason; }
    void BindModeLifetime(const TSharedPtr<FDWCEditorPreviewModeLifetime>& InModeLifetime);
    FDWCEditorPreviewRunToken CaptureRunToken() const;

    /** Releases transient preview materials while retaining slot eligibility and authored state. */
    void Suspend(EDWCEditorPreviewSuspendReason Reason);
    /** Re-enables lazy preview material creation. Does not eagerly rebuild any slot. */
    void Resume();

    bool RefreshSlotStates();
    const FDWCEditorPreviewSlotCollection& GetSlotStates() const;
    const FDWCEditorPreviewSessionSlot* FindSlot(int32 MaterialSlotIndex) const;

    bool SetPreviewMaterialScope(EDWCEditorPreviewMaterialScope Scope, int32 MaterialSlotIndex = INDEX_NONE);
    EDWCEditorPreviewMaterialScope GetPreviewMaterialScope() const { return PreviewMaterialScope; }
    TConstArrayView<int32> GetActivePreviewMaterialSlots() const { return ActivePreviewMaterialSlots; }
    bool SetSelectedMaterialSlot(int32 MaterialSlotIndex);

    void SetPreviewWetness(float PreviewWetness);

    /** Stores the desired layer state and applies only changed MID parameters. */
    bool SetLayerStack(int32 MaterialSlotIndex, const FDWCEditorPreviewLayerStack& LayerStack);

    /** Builds the requested slot MIDs first, then applies shared render resources once. */
    void PreparePreviewMaterials(TConstArrayView<int32> MaterialSlotIndices);
    /** Polls shader compilation and promotes completed slots without blocking. */
    void TickPendingMaterialCompilations();
    bool HasPendingMaterialCompilations() const { return PendingMaterialBuildCount > 0; }
    FDWCEditorPreviewSessionMaterialResult GetOrCreatePreviewMaterial(int32 MaterialSlotIndex);
    void ForEachActiveBuiltMID(TFunctionRef<void(int32, UMaterialInstanceDynamic&)> Visitor) const;

    void NotifySourceMaterialChanged(UMaterialInterface* SourceMaterial);
    void NotifyWCADataChanged();
    void InvalidateMaterialGraphs();
    void FlushRenderResourceBindings();

    void DumpDiagnostics(int32 SessionIndex) const;
    void AppendGlobalMemoryOwners(
        int32 SessionIndex,
        TSet<FString>& SeenOwnerIdentifiers,
        TArray<FDWCEditorMemoryOwnerRecord>& OutOwners) const;
    void ResetDiagnosticCounters();

    FDWCEditorPreviewSessionSlotsChanged& OnSlotsChanged();
    FDWCEditorPreviewSessionMaterialReady& OnMaterialReady();

  private:
    FDWCEditorPreviewSessionMaterialResult GetOrCreatePreviewMaterialInternal(
        int32 MaterialSlotIndex,
        bool bFlushRenderResourceBindings);
    FDWCEditorPreviewSessionSlot* FindMutableSlot(int32 MaterialSlotIndex);
    uint32 GetSourceParameterRevision(UMaterialInterface* SourceMaterial) const;
    void ApplyCommonParameters(UMaterialInstanceDynamic& MID) const;
    bool ApplyLayerParameterDiff(FDWCEditorPreviewSessionSlot& Slot, UMaterialInstanceDynamic& MID);
    void ClearBuiltMaterials();
    void TrimIdlePreviewMaterials(bool bReleaseAllIdle = false);
    void RebuildActivePreviewMaterialSlots();
    bool IsPreviewMaterialSlotActive(int32 MaterialSlotIndex) const;
    void HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);
    bool IsSourceOrBaseMaterial(const UObject* Object, UMaterialInterface* SourceMaterial) const;
    uint64 CalculateSessionContainerBytes() const;
    void SetMaterialBuildPending(FDWCEditorPreviewSessionSlot& Slot, bool bPending);
    void RebuildPendingMaterialBuildCount();

    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TWeakObjectPtr<UWorld> PreviewWorld;
    TWeakPtr<FDWCEditorPreviewModeLifetime> ModeLifetime;
    FDWCEditorPreviewSessionConfig SessionConfig;
    FDWCEditorPreviewSlotCollection SlotCollection;
    TArray<FDWCEditorPreviewSessionSlot> RuntimeSlots;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> ResourceBindingMIDs;
    TArray<int32, TInlineAllocator<16>> ActivePreviewMaterialSlots;
    TMap<FObjectKey, uint32> SourceParameterRevisions;
    FDWCEditorPreviewMaterialCache MaterialCache;

    int32 SelectedMaterialSlotIndex = AllWettableSlots;
    EDWCEditorPreviewMaterialScope PreviewMaterialScope = EDWCEditorPreviewMaterialScope::AllWettableSlots;
    int32 CachedDataUVChannelIndex = INDEX_NONE;
    float PreviewWetness = 1.0f;
    bool bInitialized = false;
    bool bSuspended = false;
    EDWCEditorPreviewSuspendReason LastSuspendReason = EDWCEditorPreviewSuspendReason::ModeSwitch;
    uint64 LifecycleGeneration = 1;
    bool bRenderResourcesDirty = false;
    int32 PendingMaterialBuildCount = 0;
    uint64 SlotRefreshCount = 0;
    uint64 SlotStateChangeCount = 0;
    uint64 MaterialRequestCount = 0;
    uint64 ExistingMIDReuseCount = 0;
    uint64 IdleMIDTrimCount = 0;
    uint64 IdleMIDTrimmedEntryCount = 0;
    uint64 MaterialUseSerial = 0;
    uint64 ObjectPropertyChangeCount = 0;
    uint64 RelevantObjectPropertyChangeCount = 0;
    uint64 WCAInvalidationCount = 0;
    uint64 SourceMaterialInvalidationCount = 0;
    uint64 GraphInvalidationCount = 0;
    uint64 RenderBindingFlushCount = 0;
    uint64 LayerStackUpdateCount = 0;
    uint64 LayerStackNoChangeCount = 0;
    uint64 LayerParameterWriteCount = 0;
    static constexpr int32 MaxIdleSlotMIDCount = 8;
    FDelegateHandle ObjectPropertyChangedHandle;

    FDWCEditorPreviewSessionSlotsChanged SlotsChangedDelegate;
    FDWCEditorPreviewSessionMaterialReady MaterialReadyDelegate;
};
