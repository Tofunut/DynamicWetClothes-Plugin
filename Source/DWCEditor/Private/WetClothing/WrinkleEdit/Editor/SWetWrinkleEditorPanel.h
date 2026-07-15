#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "EditorUndoClient.h"
#include "ScopedTransaction.h"
#include "Styling/SlateTypes.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Widgets/SCompoundWidget.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditorTypes.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Widgets/SWetClothingAssetUVView.h"
#include "WetClothing/WrinkleEdit/Viewport/WetWrinkleHitData.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class ITableRow;
class SInlineEditableTextBlock;
class SWrapBox;
class SWetWrinkleViewport;
class STableViewBase;
class UWetClothingAsset;
class UTexture;
class UTexture2D;
class UWetWrinklePreset;
struct FAssetData;
struct FWetClothingWetPartEntry;
struct FWetWrinklePatchPlacement;
struct FWetWrinklePatchStroke;
struct FWetProceduralRidgeStroke;
struct FWetProceduralRidgeStrokePoint;

enum class EWetWrinkleElementType : uint8
{
    PatchStroke,
    ProceduralRidgeStroke
};

struct FWetWrinklePatchStrokeListItem
{
    FGuid StrokeGuid;
    EWetWrinkleElementType ElementType = EWetWrinkleElementType::PatchStroke;
};

struct FWetWrinkleBrushPresetOption
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
};

struct FWetWrinklePresetPaletteItem
{
    FText DisplayName;
    FSoftObjectPath PresetPath;
    FSoftObjectPath ThumbnailTexturePath;
    TWeakObjectPtr<UWetWrinklePreset> Preset;
    FSlateBrush ThumbnailBrush;
    bool bRemoved = false;
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
    FReply ExecuteBakeWrinkleNormalMap();
    FReply ExecuteBakeAllWrinkleNormalMaps();

  private:
    using FStrokeListItemPtr = TSharedPtr<FWetWrinklePatchStrokeListItem>;
    using FMaterialSlotItemPtr = TSharedPtr<FWetClothingMaterialSlotItem>;
    using FTextureItemPtr = TSharedPtr<FWetClothingTextureItem>;
    using FWetPartEntryPtr = TSharedPtr<FWetClothingWetPartEntry>;
    using FUVDisplayModeItemPtr = TSharedPtr<EWetClothingAssetUVDisplayMode>;

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
    void RefreshStrokeOverlay(bool bRebuildAccumulatedPreview = true);
    void RefreshMaterialSlotOptions();
    void RefreshBrushPresetOptions();
    void RefreshWrinklePresetPalette(bool bForceAssetScan = false);
    void RebuildWrinklePresetPaletteWidget();
    void RefreshWrinklePresetPaletteState();
    void RefreshWrinklePresetPaletteItemState(const TSharedPtr<FWetWrinklePresetPaletteItem>& Item);
    EActiveTimerReturnType HandleWrinklePresetPaletteRefreshTimer(double InCurrentTime, float InDeltaTime);
    void HandleWrinklePresetPaletteAssetRemoved(const FAssetData& AssetData);
    void HandleWrinklePresetPaletteAssetUpdated(const FAssetData& AssetData);
    void RefreshPartMapItems();
    void RefreshMaterialTextures();
    void RefreshTextureToggleWidgets();
    void EnsureWrinkleUVChannelForModeEntry();
    bool HasUsableWrinkleUVChannel() const;
    bool HasGeneratedWrinkleUVForMaterialSlot(int32 MaterialSlotIndex) const;
    bool EnsureWrinkleUVChannelForMaterialSlot(int32 MaterialSlotIndex, bool bShowFailureDialog);
    void InvalidateWrinkleUVViewCache();
    void RefreshUVChannelOptions();
    void RefreshWrinkleUVView();
    void RebuildWrinkleUVViewPatchMarkerCache();
    void RefreshWrinkleUVViewMarkersOnly();
    TSharedRef<SWidget> BuildWrinkleUVViewSection();

    int32 GetWrinkleUVViewChannelIndex() const;
    int32 GetProtectedBaseUVChannelCount() const;
    bool IsUVChannelDeleteAllowed(int32 UVChannelIndex) const;
    FText GetWrinkleUVChannelText() const;
    FText GetSelectedMeshUVChannelText() const;
    FText GetMeshUVChannelDisplayText(int32 UVChannelIndex) const;
    TSharedRef<SWidget> GenerateMeshUVChannelComboRow(TSharedPtr<int32> Item) const;
    TSharedRef<SWidget> GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item) const;
    void HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo);
    FText GetSelectedUVDisplayModeText() const;
    float GetUVViewBackgroundTextureOpacity() const;
    float GetUVViewIslandLineOpacity() const;
    float GetUVViewIslandLineThicknessScale() const;
    void HandleUVViewBackgroundTextureOpacityChanged(float NewValue);
    void HandleUVViewIslandLineOpacityChanged(float NewValue);
    void HandleUVViewIslandLineThicknessScaleChanged(float NewValue);
    void HandleMeshUVChannelComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    FReply HandleDeleteMeshUVChannelClicked();
    bool IsDeleteMeshUVChannelEnabled() const;
    FReply HandleGenerateWrinkleUVChannelClicked();
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
    FText GetPartMapSectionText() const;
    TSharedRef<SWidget> GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const;
    FText GetSelectedMaterialSlotText() const;
    FText GetMaterialSlotStatusText(int32 MaterialSlotIndex) const;
    void HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow> GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex);
    TSharedRef<ITableRow> GeneratePartMapRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<SWidget> GenerateTextureComboItem(FTextureItemPtr Item);
    void HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo);
    UTexture* ResolveSelectedMaterialTexture() const;
    UTexture* ResolveTextureAddressTexture() const;
    void SaveSelectedTexture();

    FString GetWrinklePresetObjectPath() const;
    void HandleWrinklePresetChanged(const FAssetData& AssetData);
    TSharedRef<SWidget> BuildWrinklePresetPalette();
    TSharedRef<SWidget> GenerateWrinklePresetPaletteTile(TSharedPtr<FWetWrinklePresetPaletteItem> Item);
    FReply HandleWrinklePresetPaletteClicked(TSharedPtr<FWetWrinklePresetPaletteItem> Item);
    FReply HandleRefreshWrinklePresetPaletteClicked();
    FSlateColor GetWrinklePresetPaletteTileColor(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const;
    EVisibility GetWrinklePresetPaletteTileVisibility(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const;
    FText GetWrinklePresetPaletteTooltipText(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const;
    EVisibility GetWrinklePresetPaletteThumbnailVisibility(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const;
    void RefreshWrinklePresetThumbnail();
    const FSlateBrush* GetWrinklePresetThumbnailBrush() const;
    EVisibility GetWrinklePresetThumbnailVisibility() const;
    FText GetWrinklePresetStatusText() const;
    FSlateColor GetWrinklePresetStatusColor() const;
    FReply HandleOpenWrinklePresetClicked();
    bool CanOpenWrinklePreset() const;
    TSharedRef<ITableRow> GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleClearStrokesClicked();
    bool IsClearStrokesEnabled() const;
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item);
    void HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item);
    FReply HandleDeleteStrokeClicked(FStrokeListItemPtr Item);

    void HandleUVChannelChanged(int32 NewValue);
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
    FWetWrinklePatchStroke* FindMutableStroke(const FGuid& StrokeGuid) const;
    const FWetWrinklePatchStroke* FindStroke(const FGuid& StrokeGuid) const;
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
    UTexture* ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    bool IsCurrentWrinklePresetUsable(FString* OutReason = nullptr) const;
    FText GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const;
    TSharedPtr<int32> FindMaterialSlotOption(int32 MaterialSlotIndex) const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    void HandleTextureUVHovered(const FVector2D& UV);
    void HandleTextureUVHoverEnded();
    void HandleTexturePaintStrokeStarted(const FVector2D& UV);
    void HandleTexturePaintStampRequested(const FVector2D& UV);
    void HandleTexturePaintStrokeEnded();
    bool TryBuildTextureSurfaceHit(const FVector2D& UV, FWetWrinkleSurfaceHit& OutSurfaceHit) const;
    bool ShouldAddStampForHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    FString MakeDefaultStrokeName() const;
    void MarkAssetEdited();

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleViewport> PreviewViewport;
    TSharedPtr<SWetClothingAssetUVView> WrinkleUVView;
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> WrinkleUVIslandItems;
    TArray<FWetClothingAssetUVViewCircleMarker> CachedWrinkleUVViewPatchMarkers;
    int32 CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewPatchMarkerMaterialSlotIndex = INDEX_NONE;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MaterialSlotComboBox;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MeshUVChannelComboBox;
    TSharedPtr<class SComboBox<FUVDisplayModeItemPtr>> UVDisplayModeComboBox;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>> MaterialSlotListView;
    TSharedPtr<class SComboButton> BrushSizeComboButton;
    TSharedPtr<class SListView<FStrokeListItemPtr>> StrokeListView;
    TArray<FStrokeListItemPtr> StrokeListItems;
    TArray<TSharedPtr<int32>> MaterialSlotOptions;
    TArray<TSharedPtr<int32>> MeshUVChannelOptions;
    TArray<FUVDisplayModeItemPtr> UVDisplayModeItems;
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    TArray<FWetPartEntryPtr> PartMapItems;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BrushPresetOptions;
    TArray<TSharedPtr<FWetWrinklePresetPaletteItem>> WrinklePresetPaletteItems;
    TSharedPtr<SWrapBox> WrinklePresetPaletteWrapBox;
    FButtonStyle WrinklePresetPaletteButtonStyle;
    TSharedPtr<FAssetThumbnailPool> MaterialThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> MaterialSlotThumbnails;
    TArray<FTextureItemPtr> TextureItems;
    TArray<TSharedPtr<FAssetThumbnail>> TextureThumbnails;
    FTextureItemPtr SelectedTextureItem;
    TSharedPtr<class SComboBox<FTextureItemPtr>> TextureComboBox;
    TSharedPtr<class SBox> SelectedTextureComboContentBox;
    TSharedPtr<class SBox> TextureSelectionContainer;
    FSlateBrush SelectedWrinklePresetThumbnailBrush;
    bool bShowMaterialTextureInUVView = true;
    TSharedPtr<class SListView<FWetPartEntryPtr>> PartMapListView;
    FWetWrinkleBrushSettings BrushSettings;
    float SizeCm = 8.0f;
    float SizeUV = 0.0677f;
    FWetWrinkleSurfaceHit CurrentHit;
    FGuid ActiveStrokeGuid;
    FGuid SelectedStrokeGuid;
    EWetWrinkleElementType SelectedElementType = EWetWrinkleElementType::PatchStroke;
    FVector2D LastStampUV = FVector2D::ZeroVector;
    int32 LastStampMaterialSlotIndex = INDEX_NONE;
    int32 LastStampUVChannelIndex = INDEX_NONE;
    int32 SelectedMeshUVChannelIndex = INDEX_NONE;
    FUVDisplayModeItemPtr SelectedUVDisplayModeItem;
    EWetClothingAssetUVDisplayMode CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;
    float UVViewBackgroundTextureOpacity = 0.70f;
    float UVViewIslandLineOpacity = 1.0f;
    float UVViewIslandLineThicknessScale = 1.0f;
    bool bHasLastStamp = false;
    bool bAllowImmediateNextStrokeStamp = false;
    int32 ActiveProceduralRidgeMaterialSlotIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVChannelIndex = INDEX_NONE;
    int32 ActiveProceduralRidgeUVIslandID = INDEX_NONE;
    bool bCapturingProceduralRidgeStroke = false;
    bool bProceduralRidgeCaptureBlocked = false;
    TArray<FWetWrinkleSurfaceHit> CapturedProceduralRidgeHits;
    TUniquePtr<FScopedTransaction> ActivePaintTransaction;
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
