// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewLayerStack.h"

class FDWCEditorPreviewSession;
class FDWCEditorSessionStore;
class UWetClothingAsset;
struct FDWCEditorSessionState;
enum class EDWCEditorSessionEffect : uint32;

/**
 * Composes persisted cross-domain data and active-mode live layers, then sends
 * one declarative parameter stack to a preview session.
 */
class FDWCEditorPreviewOrchestrator final
{
  public:
    void Initialize(
        UWetClothingAsset*                 WetClothingAsset,
        FDWCEditorPreviewSession*          PreviewSession,
        EDWCEditorAuthoringDomain          ActiveDomain,
        TSharedPtr<FDWCEditorSessionStore> SessionStore = nullptr);
    void Shutdown();

    void SetShowSavedCrossLayer(bool bShow);
    void SetPreviewWetness(float PreviewWetness);

    void SetLiveLayers(int32 MaterialSlotIndex, TArray<FDWCEditorPreviewLayer> Layers);
    void SetLiveLayer(int32 MaterialSlotIndex, FDWCEditorPreviewLayer Layer);
    void ClearLiveLayer(int32 MaterialSlotIndex, EDWCEditorPreviewLayerKind LayerKind);
    void ClearLiveLayers(int32 MaterialSlotIndex);
    void ClearAllLiveLayers();

    void PreparePreviewMaterials(TConstArrayView<int32> MaterialSlotIndices);
    void RecomposeSlot(int32 MaterialSlotIndex);
    void RecomposeBuiltSlots();

    uint64 GetComposeCount() const { return ComposeCount; }
    uint64 GetNoChangeCount() const { return NoChangeCount; }

  private:
    FDWCEditorPreviewLayerStack BuildStack(int32 MaterialSlotIndex) const;
    FDWCEditorPreviewLayer      BuildSavedCrossLayer(int32 MaterialSlotIndex) const;
    void                        HandleSessionStateChanged(
                               const FDWCEditorSessionState& State,
                               EDWCEditorSessionEffect       Effects,
                               uint64                        SessionRevision);

    TWeakObjectPtr<UWetClothingAsset>           WetClothingAsset;
    FDWCEditorPreviewSession*                   PreviewSession = nullptr;
    TSharedPtr<FDWCEditorSessionStore>          SessionStore;
    TMap<int32, TArray<FDWCEditorPreviewLayer>> LiveLayersBySlot;
    EDWCEditorAuthoringDomain                   ActiveDomain = EDWCEditorAuthoringDomain::None;
    FDelegateHandle                             SessionStateChangedHandle;
    bool                                        bShowSavedCrossLayer = true;
    uint64                                      ComposeCount = 0;
    uint64                                      NoChangeCount = 0;
};
