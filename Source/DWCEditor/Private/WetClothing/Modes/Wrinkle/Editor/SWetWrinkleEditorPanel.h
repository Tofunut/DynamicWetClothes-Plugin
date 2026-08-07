//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "EditorUndoClient.h"
#include "Styling/SlateTypes.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Widgets/SCompoundWidget.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleElementListPanel.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinklePalettePanel.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleUVPanel.h"
#include "WetClothing/Modes/Wrinkle/Editor/WetWrinklePreviewController.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"
#include "WetClothing/Foundation/Preview/Slots/DWCEditorPreviewSlotState.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class FDWCEditorAuthoringDocument;
class FDWCEditorBakeCoordinator;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorRenderUploadQueue;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorTextureWorkspace;
class FDWCEditorWorkerJobScheduler;
class FWetWrinkleAuthoringController;
using FDWCEditorWorkerJobSchedulerPtr = TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>;
class IDetailsView;
class ITableRow;
class SInlineEditableTextBlock;
class SWidgetSwitcher;
class SWetWrinkleCustomNormalPanel;
class SWetWrinkleViewport;
class STableViewBase;
class USkeletalMesh;
class UWetClothingAsset;
class UTexture;
class UTexture2D;
enum class EDWCEditorPreviewSuspendReason : uint8;
struct FAssetData;
struct FWetWrinklePatchPlacement;
struct FWetProceduralRidgeStroke;
struct FWetProceduralRidgeStrokePoint;

struct FWetWrinkleBrushPresetOption
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
};

class SWetWrinkleEditorPanel : public SCompoundWidget, public FEditorUndoClient
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorAuthoringDocument>, AuthoringDocument)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSessionStore>, SessionStore)
    SLATE_ARGUMENT(FDWCEditorWorkerJobSchedulerPtr, WorkerJobScheduler)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorBakeCoordinator>, BakeCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSpatialQueryService>, SpatialQueryService)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorTextureWorkspace>, TextureWorkspace)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorPreviewCommitCoordinator>, PreviewCommitCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorRenderUploadQueue>, RenderUploadQueue)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetWrinkleEditorPanel() override;
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;
    void RefreshFromAsset();
    void RefreshFromAssetLightweight();
    void SuspendPreview(EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewIfNeeded();
    FReply BakeSelectedWrinkleNormalMap();

  private:
    void RefreshFromAssetInternal(bool bForcePreviewMaterialRebuild, bool bRebuildAccumulatedPreview);
    void DispatchWrinkleBrushState(EDWCEditorSessionEffect Effects);
    void DispatchWrinkleSelectionState();
    void HandleSessionStateChanged(
        const FDWCEditorSessionState& State,
        EDWCEditorSessionEffect Effects,
        uint64 Revision);
    using FStrokeListItemPtr = FWetWrinkleElementListItemPtr;
    using FMaterialSlotItemPtr = TSharedPtr<FWCAMaterialSlotItem>;
    using FWrinkleTexturePaletteItemPtr = FWetWrinkleTexturePaletteItemPtr;

    struct FWrinkleUVIslandCacheEntry
    {
        const USkeletalMesh* Mesh = nullptr;
        const void* LODRenderDataIdentity = nullptr;
        int32 UVChannelIndex = INDEX_NONE;
        int32 MaterialSlotIndex = INDEX_NONE;
        FString TopologySignature;
        TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
        uint64 LastUsedSerial = 0;
    };

    FReply HandleSaveClicked();
    FReply BakeWrinkleNormalMapsForSlots(const TArray<int32>& MaterialSlotIndices);
    FReply HandleFocusClicked();
    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    TSharedRef<SWidget> BuildPatchBrushSection();
    TSharedRef<SWidget> BuildPatchListSection();
    FWetWrinkleBrushSettings MakeViewportBrushSettings() const;
    void PushBrushSettingsToViewport();
    void PushBrushTopologyToViewport();
    void PushBrushPreviewSettingsToViewport();
    void PushPreviewWetnessToViewport();
    void PushStrokeSelectionToViewport();
    void RefreshStrokeList();
    bool IsPatchVisibleForCurrentMaterialSlot(const FWetWrinklePatchPlacement& Patch) const;
    bool IsProceduralRidgeStrokeVisibleForCurrentMaterialSlot(const FWetProceduralRidgeStroke& Stroke) const;
    void RefreshStrokeOverlay(bool bRebuildAccumulatedPreview = true);
    void RefreshMaterialSlotOptions();
    void RefreshBrushPresetOptions();
    void RefreshWrinkleTexturePalette(bool bForceAssetScan = false);
    void RefreshWrinkleTexturePaletteView();
    void RefreshWrinkleTexturePaletteItemState(const FWrinkleTexturePaletteItemPtr& Item);
    FWrinkleTexturePaletteItemPtr UpsertWrinkleTexturePaletteItem(const FAssetData& AssetData);
    bool RemoveWrinkleTexturePaletteItem(const FSoftObjectPath& TexturePath);
    void SortWrinkleTexturePaletteItems();
    void HandleWrinkleTextureAssetAdded(const FAssetData& AssetData);
    void HandleWrinkleTextureAssetRemoved(const FAssetData& AssetData);
    void HandleWrinkleTextureAssetUpdated(const FAssetData& AssetData);
    TSharedRef<SWidget> BuildRuntimeWrinkleNormalStatusSection();
    TSharedRef<SWidget> BuildAuthoringRightPanel();
    TSharedRef<SWidget> BuildCustomNormalRightPanel();
    TSharedRef<SWidget> BuildCustomWrinkleMapToggle(int32 MaterialSlotIndex);
    ECheckBoxState GetCustomWrinkleMapCheckState(int32 MaterialSlotIndex) const;
    void HandleCustomWrinkleMapCheckStateChanged(ECheckBoxState NewState, int32 MaterialSlotIndex);
    bool IsUsingCustomWrinkleMap(int32 MaterialSlotIndex = INDEX_NONE) const;
    void HandleCustomNormalSettingsChanged();
    void RefreshRuntimeNormalUI(bool bRebuildAccumulatedPreview = true, bool bPushViewportSettings = true);
    FText GetRuntimeNormalSourceText() const;
    FText GetRuntimeNormalTextureText() const;
    FText GetRuntimeNormalUVText() const;
    FText GetRuntimeNormalCoverageText() const;
    FText GetRuntimeNormalStatusText() const;
    FSlateColor GetRuntimeNormalStatusColor() const;
    bool HasUsableWrinkleUVChannel() const;
    bool EnsureWrinkleUVChannelForMaterialSlot(int32 MaterialSlotIndex, bool bShowFailureDialog);
    void InvalidateWrinkleUVViewCache();
    void RefreshDWCDataUVChannel();
    void RefreshWrinkleUVView();
    void RebuildWrinkleUVViewPatchMarkerCache();
    void RefreshWrinkleUVViewMarkersOnly();
    TSharedRef<SWidget> BuildWrinkleUVViewSection();

    int32 GetWrinkleUVViewChannelIndex() const;
    FText GetDWCDataUVChannelText() const;
    FReply HandleAutoGenerateClicked();
    FText GetPatchListSummaryText() const;
    FText GetBrushSectionHeadingText() const;
    FText GetBrushSizeLabelText() const;
    ECheckBoxState GetToolModeCheckState(EWetWrinkleToolMode ToolMode) const;
    void HandleToolModeChanged(ECheckBoxState NewState, EWetWrinkleToolMode ToolMode);
    ECheckBoxState GetRidgeEditModeCheckState(EWetProceduralRidgeEditMode EditMode) const;
    void HandleRidgeEditModeChanged(ECheckBoxState NewState, EWetProceduralRidgeEditMode EditMode);
    ECheckBoxState GetRidgeJunctionModeCheckState() const;
    void HandleRidgeJunctionModeChanged(ECheckBoxState NewState);
    ECheckBoxState GetRidgeShapeCheckState(EWetProceduralRidgeShape Shape) const;
    void HandleRidgeShapeChanged(ECheckBoxState NewState, EWetProceduralRidgeShape Shape);
    EVisibility GetFoldOptionsVisibility() const;
    ECheckBoxState GetFlipFoldSideCheckState() const;
    void HandleFlipFoldSideChanged(ECheckBoxState NewState);
    EVisibility GetPatchToolVisibility() const;
    EVisibility GetProceduralRidgeToolVisibility() const;
    EVisibility GetProceduralRidgeEditVisibility() const;
    FText GetMaterialSlotCountText() const;
    TSharedRef<ITableRow> GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);
    void ApplyMaterialSlotSelection(int32 MaterialSlotIndex, bool bShowFailureDialog);

    FString GetWrinkleNormalTextureObjectPath() const;
    void HandleWrinkleNormalTextureChanged(const FAssetData& AssetData);
    TSharedRef<SWidget> BuildWrinkleTexturePalette();
    TSharedRef<ITableRow> GenerateWrinkleTexturePaletteTileRow(
        FWrinkleTexturePaletteItemPtr Item,
        const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<SWidget> GenerateWrinkleTexturePaletteTile(FWrinkleTexturePaletteItemPtr Item);
    FReply HandleWrinkleTexturePaletteClicked(FWrinkleTexturePaletteItemPtr Item);
    FReply HandleWrinkleTexturePaletteContextMenu(const FPointerEvent& MouseEvent, FWrinkleTexturePaletteItemPtr Item);
    FReply HandleRefreshWrinkleTexturePaletteClicked();
    FSlateColor GetWrinkleTexturePaletteTileColor(FWrinkleTexturePaletteItemPtr Item) const;
    FText GetWrinkleTexturePaletteTooltipText(FWrinkleTexturePaletteItemPtr Item) const;
    void RefreshWrinkleNormalThumbnail();
    const FSlateBrush* GetWrinkleNormalThumbnailBrush() const;
    EVisibility GetWrinkleNormalThumbnailVisibility() const;
    FText GetWrinkleNormalStatusText() const;
    FSlateColor GetWrinkleNormalStatusColor() const;
    FReply HandleOpenWrinkleNormalTextureClicked();
    bool CanOpenWrinkleNormalTexture() const;
    FReply HandleAddWrinkleTextureSearchPathClicked();
    TSharedRef<SWidget> BuildWrinkleTextureSearchPathMenu();
    void HandleRemoveWrinkleTextureSearchPath(FString Path);
    ECheckBoxState GetShowHiddenWrinkleTexturesState() const;
    void HandleShowHiddenWrinkleTexturesChanged(ECheckBoxState NewState);
    void HandleSetWrinkleTextureHidden(FWrinkleTexturePaletteItemPtr Item, bool bHidden);
    void HandleCorrectWrinkleTexture(FWrinkleTexturePaletteItemPtr Item);
    void HandleCorrectedWrinkleTextureCreated(UTexture2D* CorrectedTexture, bool bHideOriginal, FSoftObjectPath OriginalPath);
    bool IsAssetInsideWrinkleTextureSearchPaths(const FAssetData& AssetData) const;
    TSharedRef<ITableRow> GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleClearStrokesClicked();
    bool IsClearStrokesEnabled() const;
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item);
    void HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item);
    FReply HandleDeleteStrokeClicked(FStrokeListItemPtr Item);

    float GetBrushSizeCm() const;
    FText GetBrushSizeDisplayText() const;
    TSharedRef<SWidget> BuildBrushSizeMenu();
    void HandleBrushRadiusChanged(float NewValue);
    FReply HandleBrushSizePresetClicked(float NewValue);
    void HandleStrengthChanged(float NewValue);
    void HandleFalloffChanged(float NewValue);
    void HandleRotationChanged(float NewValue);
    void HandlePreviewWetnessChanged(float NewValue);
    ECheckBoxState GetShowBakedTransparencyState() const;
    void HandleShowBakedTransparencyChanged(ECheckBoxState NewState);
    void HandleRidgeStartTaperChanged(float NewValue);
    void HandleRidgeEndTaperChanged(float NewValue);
    void HandleRidgePointSpacingChanged(float NewValue);
    ECheckBoxState GetRidgeNaturalVariationEnabledState() const;
    void HandleRidgeNaturalVariationEnabledChanged(ECheckBoxState NewState);
    float GetRidgeCenterlineVariationValue() const;
    void HandleRidgeCenterlineVariationChanged(float NewValue);
    float GetRidgeCenterlineFrequencyValue() const;
    void HandleRidgeCenterlineFrequencyChanged(float NewValue);
    float GetRidgeWidthVariationValue() const;
    void HandleRidgeWidthVariationChanged(float NewValue);
    float GetRidgeWidthFrequencyValue() const;
    void HandleRidgeWidthFrequencyChanged(float NewValue);
    int32 GetRidgeNoiseSeedValue() const;
    void HandleRidgeNoiseSeedChanged(int32 NewValue);
    FReply HandleRandomizeRidgeNoiseSeedClicked();
    void HandleRidgePropertySliderBegin();
    void HandleRidgePropertySliderEnd(float NewValue);
    void HandleRidgePropertyCommitted(float NewValue, ETextCommit::Type CommitType);
    float GetRidgeStrengthValue() const;
    float GetRidgeFalloffPercentValue() const;
    float GetRidgeStartTaperValue() const;
    float GetRidgeEndTaperValue() const;
    void ApplyBrushSettingsToSelectedProceduralStroke();
    const FWetProceduralRidgeStroke* GetSelectedProceduralRidgeEditState() const;
    FWetProceduralRidgeStroke* BeginTransientSelectedRidgeEdit();
    bool CommitTransientSelectedRidgeEdit(bool bRefreshPreview = true);
    bool DiscardTransientSelectedRidgeEdit(bool bRefreshPreview = true);
    void CommitRidgePropertyEdit();
    FReply HandleDeleteSelectedRidgePointClicked();
    bool CanDeleteSelectedRidgePoint() const;
    ECheckBoxState GetSelectedRidgeEndpointPointedState(bool bStartEndpoint) const;
    void HandleSelectedRidgeEndpointPointedChanged(ECheckBoxState NewState, bool bStartEndpoint);
    ECheckBoxState GetSelectedRidgeEndpointFlaredState(bool bStartEndpoint) const;
    void HandleSelectedRidgeEndpointFlaredChanged(ECheckBoxState NewState, bool bStartEndpoint);
    EVisibility GetFlareOptionsVisibility() const;
    float GetRidgeFlareLengthValue() const;
    void HandleRidgeFlareLengthChanged(float NewValue);
    float GetRidgeFlareWidthValue() const;
    void HandleRidgeFlareWidthChanged(float NewValue);
    float GetRidgeFlareEndStrengthValue() const;
    void HandleRidgeFlareEndStrengthChanged(float NewValue);
    float GetRidgeFlareSoftnessValue() const;
    void HandleRidgeFlareSoftnessChanged(float NewValue);
    FText GetSelectedRidgeEndpointStatusText(bool bStartEndpoint) const;
    FSlateColor GetSelectedRidgeEndpointStatusColor(bool bStartEndpoint) const;
    void HandlePreviewToggleChanged(ECheckBoxState NewState);
    ECheckBoxState GetPreviewToggleState() const;
    FWetWrinklePatchPlacement* FindMutablePatch(const FGuid& PatchGuid) const;
    const FWetWrinklePatchPlacement* FindPatch(const FGuid& PatchGuid) const;
    const FWetWrinklePatchPlacement* ResolvePatchListItem(const FStrokeListItemPtr& Item) const;
    FWetProceduralRidgeStroke* FindMutableProceduralRidgeStroke(const FGuid& StrokeGuid) const;
    const FWetProceduralRidgeStroke* FindProceduralRidgeStroke(const FGuid& StrokeGuid) const;
    const FWetProceduralRidgeStroke* ResolveProceduralRidgeListItem(const FStrokeListItemPtr& Item) const;
    FWetWrinklePatchPlacement MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    FWetProceduralRidgeStrokePoint MakeProceduralRidgePointFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    void BeginProceduralRidgeStroke(const FWetWrinkleSurfaceHit& SurfaceHit);
    void AppendProceduralRidgeStrokePoint(const FWetWrinkleSurfaceHit& SurfaceHit);
    void CommitProceduralRidgeStroke();
    void CancelProceduralRidgeStroke();
    void BeginProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void UpdateProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndProceduralRidgePointEdit(bool bCancel, bool bRefreshPreview = true);
    int32 FindNearestProceduralRidgeSegment(const FWetProceduralRidgeStroke& Stroke, const FVector2D& UV, float& OutSegmentT) const;
    bool FindProceduralRidgeJunctionSnap(
        const FWetWrinkleSurfaceHit& SurfaceHit,
        const FGuid& ExcludedStrokeGuid,
        FWetWrinkleSurfaceHit& OutSnappedHit,
        FGuid& OutConnectedStrokeGuid,
        int32& OutConnectedSegmentIndex,
        float& OutConnectedSegmentT) const;
    void ClearConnectionsToStroke(const FGuid& DeletedStrokeGuid);
    bool ShouldAddProceduralRidgePoint(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    TArray<FWetWrinkleSurfaceHit> BuildSmoothedProceduralRidgeHits() const;
    bool TrySmoothProceduralRidgeInteriorHit(
        const FWetWrinkleSurfaceHit& Previous,
        const FWetWrinkleSurfaceHit& Current,
        const FWetWrinkleSurfaceHit& Next,
        FWetWrinkleSurfaceHit& OutSmoothedHit) const;
    UTexture* ResolveSourceTextureForStamp(int32 MaterialSlotIndex) const;
    bool IsCurrentWrinkleNormalUsable(FString* OutReason = nullptr) const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    const FDWCEditorPreviewSlotState* FindPreviewSlotState(int32 MaterialSlotIndex) const;
    FString MakeDefaultPatchName() const;
    bool EditWrinkleData(
        const FText& TransactionText,
        EDWCEditorAuthoringImpact Impact,
        int32 MaterialSlotIndex,
        const FGuid& ElementGuid,
        TFunctionRef<bool(FWetClothingWrinkleData&)> Mutation);
    void MarkAssetEdited();

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    FDWCEditorWorkerJobSchedulerPtr WorkerJobScheduler;
    TSharedPtr<FDWCEditorBakeCoordinator> BakeCoordinator;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleViewport> PreviewViewport;
    TSharedPtr<FWetWrinkleAuthoringController> AuthoringController;
    TUniquePtr<FWetWrinklePreviewController> PreviewController;
    TSharedPtr<SWetWrinkleCustomNormalPanel> CustomNormalPanel;
    TSharedPtr<SWidgetSwitcher> RightPanelSwitcher;
    TSharedPtr<SWetWrinkleUVPanel> WrinkleUVPanel;
    TSharedPtr<SWCAUVView> WrinkleUVView;
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> WrinkleUVIslandItems;
    TArray<FWrinkleUVIslandCacheEntry> WrinkleUVIslandCache;
    uint64 WrinkleUVIslandCacheUseSerial = 0;
    TArray<FWCAUVViewCircleMarker> CachedWrinkleUVViewPatchMarkers;
    const USkeletalMesh* CachedWrinkleUVViewMesh = nullptr;
    const void* CachedWrinkleUVViewLODRenderDataIdentity = nullptr;
    FString CachedWrinkleUVViewTopologySignature;
    int32 CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerMaterialSlotIndex = INDEX_NONE;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>> MaterialSlotListView;
    TSharedPtr<class SComboButton> BrushSizeComboButton;
    TSharedPtr<SWetWrinkleElementListPanel> ElementListPanel;
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    FDWCEditorPreviewSlotCollection PreviewSlotStates;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BrushPresetOptions;
    TSharedPtr<SWetWrinklePalettePanel> WrinklePalettePanel;
    TSharedPtr<FAssetThumbnailPool> MaterialThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> MaterialSlotThumbnails;
    FSlateBrush SelectedWrinkleNormalThumbnailBrush;
    TSharedPtr<class SEditableTextBox> WrinkleTextureSearchPathTextBox;
    FWetWrinkleBrushSettings BrushSettings;
    float SizeCm = 8.0f;
    float SizeUV = 0.0677f;
    bool bShowBakedTransparency = true;
    FWetWrinkleSurfaceHit CurrentHit;
    FGuid SelectedStrokeGuid;
    EWetWrinkleElementType SelectedElementType = EWetWrinkleElementType::Patch;
    int32 ActiveProceduralRidgeMaterialSlotIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVChannelIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVIslandID = INDEX_NONE;
    bool bCapturingProceduralRidgeStroke = false;
    bool bProceduralRidgeCaptureBlocked = false;
    TArray<FWetWrinkleSurfaceHit> CapturedProceduralRidgeHits;
    TArray<FWetWrinkleSurfaceHit> SmoothedProceduralRidgeHits;
    FWetWrinkleSurfaceHit LiveProceduralRidgeHit;
    bool bRidgePointEditActive = false;
    bool bRidgePropertyEditActive = false;
    int32 SelectedProceduralRidgePointIndex = INDEX_NONE;
    int32 EditingProceduralRidgePointIndex = INDEX_NONE;
    int32 EditingProceduralRidgeUVIslandID = INDEX_NONE;
    TOptional<FWetProceduralRidgeStroke> TransientEditedProceduralRidgeStroke;
    bool bEditingProceduralRidgePoint = false;
    bool bSynchronizingMaterialSlotSelection = false;
    bool bApplyingSessionState = false;
    bool bPreviewSuspended = false;
    FGuid PendingStartConnectionStrokeGuid;
    int32 PendingStartConnectionSegmentIndex = INDEX_NONE;
    float PendingStartConnectionSegmentT = 0.0f;
};
