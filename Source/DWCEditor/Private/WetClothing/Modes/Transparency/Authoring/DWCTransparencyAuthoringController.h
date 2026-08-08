// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"

class FDWCEditorAuthoringDocument;
class FDWCEditorSessionStore;
class SWetClothingTransparencyPreviewViewport;
class UWetClothingAsset;

/** Owns transparency paint transactions and converts surface drags into authored UV samples. */
class FDWCTransparencyAuthoringController final
    : public TSharedFromThis<FDWCTransparencyAuthoringController>
{
  public:
    FDWCTransparencyAuthoringController(
        UWetClothingAsset*                      InAsset,
        TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument,
        TSharedPtr<FDWCEditorSessionStore>      InSessionStore);
    ~FDWCTransparencyAuthoringController();

    void AttachViewport(const TSharedPtr<SWetClothingTransparencyPreviewViewport>& InViewport);
    void DetachViewport();
    void HandleSessionStateChanged(const FDWCEditorSessionState& State);

    bool CanBeginSurfaceInteraction(const FDWCEditorSurfaceHit& SurfaceHit) const;
    void HandleSurfaceHitChanged(const FDWCEditorSurfaceHit& SurfaceHit);
    void BeginSurfaceInteraction(const FDWCEditorSurfaceHit& SurfaceHit);
    void UpdateSurfaceInteraction(const FDWCEditorSurfaceHit& SurfaceHit);
    void EndSurfaceInteraction();
    void CancelSurfaceInteraction();
    bool CancelActiveInteraction(bool bRefreshPreview = true);
    bool IsInteracting() const { return ActiveStrokeGuid.IsValid(); }

  private:
    const FDWCEditorTransparencySessionState& GetTransparencyState() const;
    bool                                      AppendPaintSample(const FVector2D& PositionUV, int32 UVIslandID);
    void                                      ResetInteractionState();

    TWeakObjectPtr<UWetClothingAsset>                 Asset;
    TSharedPtr<FDWCEditorAuthoringDocument>           AuthoringDocument;
    TSharedPtr<FDWCEditorSessionStore>                SessionStore;
    TWeakPtr<SWetClothingTransparencyPreviewViewport> Viewport;

    FDWCTransparencyEditContext   ActiveContext;
    FDWCTransparencyPaintSettings ActivePaintSettings;
    FGuid                         ActiveStrokeGuid;
    // The asset is deliberately untouched until mouse-up. These transient
    // strokes are the source for live preview and a single commit mutation.
    TOptional<FDWCTransparencyBrushStroke>       ActiveBrushStroke;
    TOptional<FDWCTransparencyRevealColorStroke> ActiveRevealColorStroke;
    bool                                         bCommitMutationApplied = false;
    FVector2D                                    LastPointerUV = FVector2D::ZeroVector;
    int32                                        LastPointerUVIslandID = INDEX_NONE;
    float                                        DistanceToNextStamp = 0.0f;
};
