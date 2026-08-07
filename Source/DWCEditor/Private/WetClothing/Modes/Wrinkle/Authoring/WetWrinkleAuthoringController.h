//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"

class FDWCEditorAuthoringDocument;
class FDWCEditorSessionStore;
class SWetWrinkleViewport;
class UWetClothingAsset;

/**
 * Owns wrinkle surface-authoring semantics for one WCA editor session.
 *
 * Slate widgets only dispatch settings and selection. The viewport only supplies
 * surface queries and preview rendering. This controller owns the transient
 * Patch/Ridge interaction lifecycle and commits authored data through the
 * shared authoring document.
 */
class FWetWrinkleAuthoringController final
    : public TSharedFromThis<FWetWrinkleAuthoringController>
{
  public:
    FWetWrinkleAuthoringController(
        UWetClothingAsset* InAsset,
        TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument,
        TSharedPtr<FDWCEditorSessionStore> InSessionStore);
    ~FWetWrinkleAuthoringController();

    void AttachViewport(const TSharedPtr<SWetWrinkleViewport>& InViewport);
    void DetachViewport();

    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    void BeginSurfaceInteraction(const FWetWrinkleSurfaceHit& SurfaceHit);
    void UpdateSurfaceInteraction(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndSurfaceInteraction();
    void CancelSurfaceInteraction();
    bool CancelActiveInteraction(bool bRefreshPreview = true);

    bool IsInteracting() const;
    bool IsEditingRidgePoint() const;

  private:
    const FWetWrinkleBrushSettings& GetBrushSettings() const;
    bool CanAuthorWithCurrentSettings() const;
    bool IsUsingCustomWrinkleMap(int32 MaterialSlotIndex) const;
    const FWetProceduralRidgeStroke* FindProceduralRidgeStroke(const FGuid& StrokeGuid) const;
    FWetWrinklePatchPlacement MakePatchFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    FWetProceduralRidgeStrokePoint MakeRidgePointFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    bool EditWrinkleData(
        const FText& TransactionText,
        EDWCEditorAuthoringImpact Impact,
        int32 MaterialSlotIndex,
        const FGuid& ElementGuid,
        TFunctionRef<bool(FWetClothingWrinkleData&)> Mutation) const;
    void SelectElement(EWetWrinkleElementType ElementType, const FGuid& ElementGuid, int32 RidgePointIndex = INDEX_NONE) const;

    void PlacePatch(const FWetWrinkleSurfaceHit& SurfaceHit);
    void BeginRidgeStroke(const FWetWrinkleSurfaceHit& SurfaceHit);
    void AppendRidgeStrokePoint(const FWetWrinkleSurfaceHit& SurfaceHit);
    void CommitRidgeStroke();
    void CancelRidgeStroke(bool bRefreshPreview);
    bool ShouldAddRidgePoint(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    TArray<FWetWrinkleSurfaceHit> BuildSmoothedRidgeHits() const;
    bool TrySmoothRidgeInteriorHit(
        const FWetWrinkleSurfaceHit& Previous,
        const FWetWrinkleSurfaceHit& Current,
        const FWetWrinkleSurfaceHit& Next,
        FWetWrinkleSurfaceHit& OutSmoothedHit) const;

    void BeginRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void UpdateRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndRidgePointEdit(bool bCancel, bool bRefreshPreview);
    int32 FindNearestRidgeSegment(
        const FWetProceduralRidgeStroke& Stroke,
        const FVector2D& UV,
        float& OutSegmentT) const;
    bool FindRidgeJunctionSnap(
        const FWetWrinkleSurfaceHit& SurfaceHit,
        const FGuid& ExcludedStrokeGuid,
        FWetWrinkleSurfaceHit& OutSnappedHit,
        FGuid& OutConnectedStrokeGuid,
        int32& OutConnectedSegmentIndex,
        float& OutConnectedSegmentT) const;

    TWeakObjectPtr<UWetClothingAsset> Asset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TWeakPtr<SWetWrinkleViewport> Viewport;

    bool bCapturingRidgeStroke = false;
    bool bRidgeCaptureBlocked = false;
    int32 ActiveRidgeMaterialSlotIndex = INDEX_NONE;
    int32 ActiveRidgeUVChannelIndex = INDEX_NONE;
    int32 ActiveRidgeUVIslandID = INDEX_NONE;
    TArray<FWetWrinkleSurfaceHit> CapturedRidgeHits;
    TArray<FWetWrinkleSurfaceHit> SmoothedRidgeHits;
    FWetWrinkleSurfaceHit LiveRidgeHit;
    FGuid PendingStartConnectionStrokeGuid;
    int32 PendingStartConnectionSegmentIndex = INDEX_NONE;
    float PendingStartConnectionSegmentT = 0.0f;

    bool bEditingRidgePoint = false;
    int32 EditingRidgePointIndex = INDEX_NONE;
    int32 EditingRidgeUVIslandID = INDEX_NONE;
    TOptional<FWetProceduralRidgeStroke> EditedRidgeStroke;
};
