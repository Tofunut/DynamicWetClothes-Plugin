#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "Styling/SlateTypes.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Widgets/SCompoundWidget.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class ITableRow;
class SInlineEditableTextBlock;
class SWrapBox;
class SWidgetSwitcher;
class SWetWrinkleCustomNormalPanel;
class SWetWrinkleViewport;
class STableViewBase;
class UWetClothingAsset;
class UTexture;
class UTexture2D;
struct FAssetData;
struct FWetWrinklePatchPlacement;
struct FWetProceduralRidgeStroke;
struct FWetProceduralRidgeStrokePoint;

enum class EWetWrinkleElementType : uint8
{
    Patch,
    ProceduralRidgeStroke
};

struct FWetWrinkleElementListItem
{
    FGuid StrokeGuid;
    EWetWrinkleElementType ElementType = EWetWrinkleElementType::Patch;
};

struct FWetWrinkleBrushPresetOption
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
};

struct FWetWrinkleTexturePaletteItem
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
    TWeakObjectPtr<UTexture2D> Texture;
    TSharedPtr<FAssetThumbnail> AssetThumbnail;
    bool bAssetAvailable = true;
    bool bRemoved = false;
    bool bHidden = false;
};

class SWetWrinkleEditorPanel : public SCompoundWidget, public FEditorUndoClient
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetWrinkleEditorPanel() override;
    virtual void PostUndo(bool bSuccess) override;
    virtual void PostRedo(bool bSuccess) override;
    void RefreshFromAsset();
    void RefreshFromAssetLightweight();
    FReply BakeSelectedWrinkleNormalMap();
    FReply ExecuteBakeWrinkleNormalMap();
    FReply ExecuteBakeAllWrinkleNormalMaps();

  private:
    void RefreshFromAssetInternal(bool bForcePreviewMaterialRebuild, bool bRebuildAccumulatedPreview);
    using FStrokeListItemPtr = TSharedPtr<FWetWrinkleElementListItem>;
    using FMaterialSlotItemPtr = TSharedPtr<FWCAMaterialSlotItem>;
    using FUVDisplayModeItemPtr = TSharedPtr<EWCAUVDisplayMode>;

    FReply HandleSaveClicked();
    FReply BakeWrinkleNormalMapsForSlots(const TArray<int32>& MaterialSlotIndices);
    FReply HandleFocusClicked();
    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeEnded();
    void HandlePaintStrokeCanceled();
    TSharedRef<SWidget> BuildPatchBrushSection();
    TSharedRef<SWidget> BuildPatchListSection();
    void PushBrushSettingsToViewport();
    void RefreshStrokeList();
    bool IsPatchVisibleForCurrentMaterialSlot(const FWetWrinklePatchPlacement& Patch) const;
    bool IsProceduralRidgeStrokeVisibleForCurrentMaterialSlot(const FWetProceduralRidgeStroke& Stroke) const;
    void RefreshStrokeOverlay(bool bRebuildAccumulatedPreview = true);
    void RefreshMaterialSlotOptions();
    void RefreshBrushPresetOptions();
    void RefreshWrinkleTexturePalette(bool bForceAssetScan = false);
    void RebuildWrinkleTexturePaletteWidget();
    void RefreshWrinkleTexturePaletteState();
    void RefreshWrinkleTexturePaletteItemState(const TSharedPtr<FWetWrinkleTexturePaletteItem>& Item);
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
    void RefreshRuntimeNormalUI();
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
    TSharedRef<SWidget> GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item) const;
    void HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo);
    FText GetSelectedUVDisplayModeText() const;
    float GetUVViewIslandLineOpacity() const;
    float GetUVViewIslandLineThicknessScale() const;
    void HandleUVViewIslandLineOpacityChanged(float NewValue);
    void HandleUVViewIslandLineThicknessScaleChanged(float NewValue);
    FReply HandleAutoGenerateClicked();
    FText GetHitInfoText() const;
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
    TSharedRef<SWidget> GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const;
    FText GetSelectedMaterialSlotText() const;
    void HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow> GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);

    FString GetWrinkleNormalTextureObjectPath() const;
    void HandleWrinkleNormalTextureChanged(const FAssetData& AssetData);
    TSharedRef<SWidget> BuildWrinkleTexturePalette();
    TSharedRef<SWidget> GenerateWrinkleTexturePaletteTile(TSharedPtr<FWetWrinkleTexturePaletteItem> Item);
    FReply HandleWrinkleTexturePaletteClicked(TSharedPtr<FWetWrinkleTexturePaletteItem> Item);
    FReply HandleWrinkleTexturePaletteContextMenu(const FPointerEvent& MouseEvent, TSharedPtr<FWetWrinkleTexturePaletteItem> Item);
    FReply HandleRefreshWrinkleTexturePaletteClicked();
    FSlateColor GetWrinkleTexturePaletteTileColor(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const;
    EVisibility GetWrinkleTexturePaletteTileVisibility(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const;
    FText GetWrinkleTexturePaletteTooltipText(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const;
    EVisibility GetWrinkleTexturePaletteThumbnailVisibility(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const;
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
    void HandleSetWrinkleTextureHidden(TSharedPtr<FWetWrinkleTexturePaletteItem> Item, bool bHidden);
    void HandleCorrectWrinkleTexture(TSharedPtr<FWetWrinkleTexturePaletteItem> Item);
    void HandleCorrectedWrinkleTextureCreated(UTexture2D* CorrectedTexture, bool bHideOriginal, FSoftObjectPath OriginalPath);
    bool IsAssetInsideWrinkleTextureSearchPaths(const FAssetData& AssetData) const;
    TSharedRef<ITableRow> GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleClearStrokesClicked();
    bool IsClearStrokesEnabled() const;
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item);
    void HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item);
    FReply HandleDeleteStrokeClicked(FStrokeListItemPtr Item);

    void HandleMaterialSlotChanged(int32 NewValue);
    float GetBrushSizeCm() const;
    FText GetBrushSizeDisplayText() const;
    TSharedRef<SWidget> BuildBrushSizeMenu();
    void HandleBrushRadiusChanged(float NewValue);
    FReply HandleBrushSizePresetClicked(float NewValue);
    void HandleStrengthChanged(float NewValue);
    void HandleFalloffChanged(float NewValue);
    void HandleRotationChanged(float NewValue);
    void HandlePreviewWetnessChanged(float NewValue);
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
    FWetProceduralRidgeStroke* FindMutableProceduralRidgeStroke(const FGuid& StrokeGuid) const;
    const FWetProceduralRidgeStroke* FindProceduralRidgeStroke(const FGuid& StrokeGuid) const;
    FWetWrinklePatchPlacement MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    FWetProceduralRidgeStrokePoint MakeProceduralRidgePointFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    void BeginProceduralRidgeStroke(const FWetWrinkleSurfaceHit& SurfaceHit);
    void AppendProceduralRidgeStrokePoint(const FWetWrinkleSurfaceHit& SurfaceHit);
    void CommitProceduralRidgeStroke();
    void CancelProceduralRidgeStroke();
    void BeginProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void UpdateProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndProceduralRidgePointEdit(bool bCancel);
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
    UTexture* ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    bool IsCurrentWrinkleNormalUsable(FString* OutReason = nullptr) const;
    FText GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const;
    TSharedPtr<int32> FindMaterialSlotOption(int32 MaterialSlotIndex) const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    void HandleTextureUVHovered(const FVector2D& UV);
    void HandleTextureUVHoverEnded();
    void HandleTexturePaintStrokeStarted(const FVector2D& UV);
    void HandleTexturePaintStampRequested(const FVector2D& UV);
    void HandleTexturePaintStrokeEnded();
    bool TryBuildTextureSurfaceHit(const FVector2D& UV, FWetWrinkleSurfaceHit& OutSurfaceHit) const;
    FString MakeDefaultPatchName() const;
    void MarkAssetEdited();
    void MarkWrinkleAuthoringEdited();

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleViewport> PreviewViewport;
    TSharedPtr<SWetWrinkleCustomNormalPanel> CustomNormalPanel;
    TSharedPtr<SWidgetSwitcher> RightPanelSwitcher;
    TSharedPtr<SWCAUVView> WrinkleUVView;
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> WrinkleUVIslandItems;
    TArray<FWCAUVViewCircleMarker> CachedWrinkleUVViewPatchMarkers;
    int32 CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerMaterialSlotIndex = INDEX_NONE;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MaterialSlotComboBox;
    TSharedPtr<class SComboBox<FUVDisplayModeItemPtr>> UVDisplayModeComboBox;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>> MaterialSlotListView;
    TSharedPtr<class SComboButton> BrushSizeComboButton;
    TSharedPtr<class SListView<FStrokeListItemPtr>> StrokeListView;
    TArray<FStrokeListItemPtr> StrokeListItems;
    TArray<TSharedPtr<int32>> MaterialSlotOptions;
    TArray<FUVDisplayModeItemPtr> UVDisplayModeItems;
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BrushPresetOptions;
    TArray<TSharedPtr<FWetWrinkleTexturePaletteItem>> WrinkleTexturePaletteItems;
    TSharedPtr<SWrapBox> WrinkleTexturePaletteWrapBox;
    FButtonStyle WrinkleTexturePaletteButtonStyle;
    TSharedPtr<FAssetThumbnailPool> MaterialThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> MaterialSlotThumbnails;
    FSlateBrush SelectedWrinkleNormalThumbnailBrush;
    TSharedPtr<class SEditableTextBox> WrinkleTextureSearchPathTextBox;
    FWetWrinkleBrushSettings BrushSettings;
    float SizeCm = 8.0f;
    float SizeUV = 0.0677f;
    FWetWrinkleSurfaceHit CurrentHit;
    FGuid SelectedStrokeGuid;
    EWetWrinkleElementType SelectedElementType = EWetWrinkleElementType::Patch;
    FUVDisplayModeItemPtr SelectedUVDisplayModeItem;
    EWCAUVDisplayMode CurrentUVDisplayMode = EWCAUVDisplayMode::Normal;
    float UVViewIslandLineOpacity = 1.0f;
    float UVViewIslandLineThicknessScale = 1.0f;
    int32 ActiveProceduralRidgeMaterialSlotIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVChannelIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVIslandID = INDEX_NONE;
    bool bCapturingProceduralRidgeStroke = false;
    bool bProceduralRidgeCaptureBlocked = false;
    TArray<FWetWrinkleSurfaceHit> CapturedProceduralRidgeHits;
    TArray<FWetWrinkleSurfaceHit> SmoothedProceduralRidgeHits;
    FWetWrinkleSurfaceHit LiveProceduralRidgeHit;
    TUniquePtr<FScopedTransaction> ActiveRidgeEditTransaction;
    TUniquePtr<FScopedTransaction> ActiveRidgePropertyTransaction;
    int32 SelectedProceduralRidgePointIndex = INDEX_NONE;
    int32 EditingProceduralRidgePointIndex = INDEX_NONE;
    int32 EditingProceduralRidgeUVIslandID = INDEX_NONE;
    FWetProceduralRidgeStrokePoint OriginalEditedProceduralRidgePoint;
    FWetProceduralRidgeEndpoint OriginalEditedStartEndpoint;
    FWetProceduralRidgeEndpoint OriginalEditedEndEndpoint;
    bool bEditingProceduralRidgePoint = false;
    bool bInsertedEditedProceduralRidgePoint = false;
    FGuid PendingStartConnectionStrokeGuid;
    int32 PendingStartConnectionSegmentIndex = INDEX_NONE;
    float PendingStartConnectionSegmentT = 0.0f;
};
