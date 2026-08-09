//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "WetClothing/Foundation/Preview/Slots/DWCEditorPreviewSlotState.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

class IDetailsView;
class FAssetThumbnail;
class FAssetThumbnailPool;
class FDWCEditorAuthoringDocument;
class FDWCEditorBakeCoordinator;
class FDWCWrinkleSuppressionCoverageService;
class FDWCTransparencyAuthoringController;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorRenderUploadQueue;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorTextureWorkspace;
class FDWCEditorWorkerJobScheduler;
using FDWCEditorWorkerJobSchedulerPtr = TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>;
class SWetClothingTransparencyPreviewViewport;
class USkeletalMesh;
class UWetClothingAsset;
class UObject;
enum class EDWCEditorPreviewSuspendReason : uint8;
struct FDWCTransparencySourcePayload;
struct FAssetData;
namespace DWCTransparencyWorkflow
{
struct FDWCTransparencyTypeChangeImpact;
}

enum class EDWCTransparencyPanelStatus : uint8
{
    Info,
    Ready,
    Warning,
    Error
};

enum class EDWCTransparencyPanelRefreshFlags : uint8
{
    None = 0,
    Model = 1 << 0,
    StageContent = 1 << 1,
    Viewport = 1 << 2,
    Details = 1 << 3
};
ENUM_CLASS_FLAGS(EDWCTransparencyPanelRefreshFlags);

enum class EDWCTransparencyFinalPreviewRefresh : uint8
{
    None,
    WrinkleSuppression,
    OuterEdgeFeather
};

enum class EDWCTransparencyBrushSizeTarget : uint8
{
    TransparencyBrush,
    RevealColorPaint
};

struct FDWCTransparencyLayerListItem
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    FName MaterialSlotName;
};

struct FDWCTransparencyMaterialSlotItem
{
    int32 SlotIndex = INDEX_NONE;
    FName SlotName;
};

class SWetClothingTransparencyBakePanel : public SCompoundWidget
{
  public:
    using FLayerItemPtr = TSharedPtr<FDWCTransparencyLayerListItem>;
    using FMaterialSlotItemPtr = TSharedPtr<FDWCTransparencyMaterialSlotItem>;

    SLATE_BEGIN_ARGS(SWetClothingTransparencyBakePanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorAuthoringDocument>, AuthoringDocument)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSessionStore>, SessionStore)
    SLATE_ARGUMENT(FDWCEditorWorkerJobSchedulerPtr, WorkerJobScheduler)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorBakeCoordinator>, BakeCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCWrinkleSuppressionCoverageService>, WrinkleSuppressionCoverageService)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSpatialQueryService>, SpatialQueryService)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorTextureWorkspace>, TextureWorkspace)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorPreviewCommitCoordinator>, PreviewCommitCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorRenderUploadQueue>, RenderUploadQueue)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetClothingTransparencyBakePanel() override;
    void RefreshFromAsset();
    void SuspendPreview(EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewIfNeeded();
    bool SaveTransparencySetupAssets() const;
    void RebuildEditorLayout();

  private:
    void DispatchTransparencyPreviewState();
    void DispatchTransparencyPaintState(EDWCEditorSessionEffect Effects);
    FDWCTransparencyPaintSettings GetRevealPaintSettingsFromSession() const;
    void DispatchRevealPaintState(
        FDWCTransparencyPaintSettings Settings,
        EDWCEditorSessionEffect Effects = EDWCEditorSessionEffect::None);
    void DisableRevealPaintInSession();
    void DispatchTransparencyEditContext();
    void HandleSessionStateChanged(
        const FDWCEditorSessionState& State,
        EDWCEditorSessionEffect Effects,
        uint64 Revision);
    const UClass* GetSelectedSourceClass() const;
    void HandleSourceClassChanged(const UClass* NewClass);
    FString GetExternalSourceMeshPath() const;
    void HandleExternalSourceMeshChanged(const FAssetData& AssetData);
    FReply HandleStageClicked(EDWCTransparencyEditorStage Stage);
    FReply HandleSourceTypeCardClicked(EDWCTransparencySourceType SourceType);
    FReply HandleContinueToGenerationClicked();
    FReply HandleCancelSourceTypeDraftClicked();
    ECheckBoxState IsStageChecked(EDWCTransparencyEditorStage Stage) const;
    ECheckBoxState IsSourceTypeCardChecked(EDWCTransparencySourceType SourceType) const;
    FText GetSourceTypeCardStatusText(EDWCTransparencySourceType SourceType, FText Availability) const;
    EVisibility GetSourceTypeDraftStatusVisibility() const;
    FText GetSourceTypeDraftStatusText() const;
    bool HasDirtySourceTypeDraft() const;
    bool IsSourceTypeAvailable(EDWCTransparencySourceType SourceType) const;
    bool CanContinueToGeneration() const;
    void InitializeCharacterTypeSessionState();
    void ReconcileCharacterTypeSessionState();
    DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact EvaluateCharacterTypeChangeImpact() const;
    EDWCTransparencyEditorStage GetCurrentStage() const;
    void SetCurrentStage(EDWCTransparencyEditorStage Stage);
    bool CanEnterRevealEditingStage() const;
    bool CanEnterFinalEditingStage() const;
    void EnsureStageForSelectedLayer();
    bool RefreshModelState();
    void RefreshStageContent();
    void RefreshMapGenerationSettings();
    void RefreshRevealEditingContent();
    void RefreshFinalEditingContent();
    void RefreshInnerSourceSlotItems();
    void RequestRefresh(EDWCTransparencyPanelRefreshFlags Flags);
    EActiveTimerReturnType HandleDeferredRefresh(double CurrentTime, float DeltaTime);
    // Stage 4 owns final alpha; Stage 3 owns reveal-color authoring over the
    // Stage 2 source result.
    bool EnsureFinalEditingWorkingMap();
    bool EnsureRevealEditingWorkingMap();
    bool LoadBakedMapAsWorkingResult(
        const FWetClothingBakedTransparencyMap& BakedMap,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedPtr<FDWCTransparencySourcePayload>& OutResult,
        FString& OutError) const;
    int32 GetCurrentBaselineStrokeCount() const;
    FReply HandleGenerateTransparencyMapClicked();
    FReply HandleContinueToFinalEditingClicked();
    FReply HandleBakeEditedTransparencyMapClicked();
    FReply HandleFocusPreviewClicked();
    FText GetStatusText() const;
    FSlateColor GetStatusColor() const;
    FText GetGenerateTooltipText() const;
    FText GetBakeEditedTooltipText() const;
    float GetWetnessPreviewPercent() const;
    void HandleWetnessPreviewChanged(float InValue);
    TOptional<float> GetTransparencyPreviewStrength() const;
    void HandleTransparencyPreviewStrengthChanged(float InValue);
    void HandleTransparencyPreviewStrengthCommitted(float InValue, ETextCommit::Type CommitType);
    ECheckBoxState GetShowSavedWrinkleState() const;
    void HandleShowSavedWrinkleChanged(ECheckBoxState NewState);
    TOptional<float> GetWrinkleSuppressionStrength() const;
    void HandleWrinkleSuppressionStrengthChanged(float InValue);
    void HandleWrinkleSuppressionStrengthCommitted(float InValue, ETextCommit::Type CommitType);
    TOptional<float> GetWrinkleMaskThreshold() const;
    void HandleWrinkleMaskThresholdChanged(float InValue);
    void HandleWrinkleMaskThresholdCommitted(float InValue, ETextCommit::Type CommitType);
    TOptional<float> GetWrinkleMaskSoftness() const;
    void HandleWrinkleMaskSoftnessChanged(float InValue);
    void HandleWrinkleMaskSoftnessCommitted(float InValue, ETextCommit::Type CommitType);
    ECheckBoxState IsBrushModeChecked(EDWCTransparencyBrushMode Mode) const;
    void HandleBrushModeChanged(ECheckBoxState NewState, EDWCTransparencyBrushMode Mode);
    float GetBrushSizeCm() const;
    FText GetBrushSizeDisplayText() const;
    TOptional<float> GetBrushStrength() const;
    TOptional<float> GetBrushFalloff() const;
    TOptional<float> GetBrushSpacing() const;
    TOptional<float> GetBrushTargetAlpha() const;
    void HandleBrushSizeChanged(float Value, EDWCTransparencyBrushSizeTarget Target);
    void HandleBrushSizeCommitted(float Value, ETextCommit::Type CommitType, EDWCTransparencyBrushSizeTarget Target);
    FReply HandleBrushSizePresetClicked(float Value, EDWCTransparencyBrushSizeTarget Target);
    void HandleBrushStrengthCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushFalloffCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushSpacingCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushTargetAlphaCommitted(float Value, ETextCommit::Type CommitType);
    void PushPaintSettingsToViewport();
    void RefreshTransparencyStrokeList();
    void RefreshRevealColorStrokeList();
    FReply HandleUndoLastStrokeClicked();
    FReply HandleClearStrokesClicked();
    FReply HandleDeleteStrokeClicked(FGuid StrokeGuid);
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FGuid StrokeGuid);
    TSharedRef<SWidget> GenerateVisualizationModeComboItem(TSharedPtr<EDWCTransparencyVisualizationMode> Item) const;
    void HandleVisualizationModeChanged(TSharedPtr<EDWCTransparencyVisualizationMode> Item, ESelectInfo::Type SelectInfo);
    FText GetVisualizationModeLabel(EDWCTransparencyVisualizationMode Mode) const;
    bool IsVisualizationModeAvailable(EDWCTransparencyVisualizationMode Mode) const;
    TSharedPtr<EDWCTransparencyVisualizationMode> FindVisualizationModeItem(EDWCTransparencyVisualizationMode Mode) const;
    ECheckBoxState IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const;
    void HandlePreviewModeChanged(ECheckBoxState NewState, EWetClothingTransparencyPreviewMode Mode);
    bool IsGenerateEnabled() const;
    bool IsBakeEditedEnabled() const;
    bool CanUseFullBlueprintPreview() const;
    void UpdateInnerSourceStatus();

    bool RefreshOptionItems();
    void RepairInvalidLayerIdentities();
    void RefreshLayerItems();
    void RefreshViewportContext();
    FWetClothingTransparencyLayerData* GetSelectedLayer();
    const FWetClothingTransparencyLayerData* GetSelectedLayer() const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 SlotIndex) const;
    const FDWCEditorPreviewSlotState* FindPreviewSlotState(int32 SlotIndex) const;
    int32 GetTransparencyDataUVChannel() const;
    bool HasUsableTransparencyDataUV() const;
    TSharedPtr<int32> FindUVChannelItem(int32 UVChannelIndex) const;
    void EditSelectedLayer(const FText& TransactionText, TFunctionRef<void(FWetClothingTransparencyLayerData&)> Edit, bool bRebuildLayout);
    bool EditSelectedLayerFinal(
        const FText& TransactionText,
        const FGuid& ElementGuid,
        TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit);
    void EditGlobalSettings(const FText& TransactionText, TFunctionRef<void(FWetClothingTransparencyData&)> Edit);
    void EditFinalBakeSettings(
        const FText& TransactionText,
        TFunctionRef<bool(FWetClothingTransparencyData&)> Edit,
        EDWCTransparencyFinalPreviewRefresh PreviewRefresh);
    void CommitTransparencyPreviewSettings(
        const FText& TransactionText,
        const FDWCTransparencyPreviewSettings& Settings);
    FDWCTransparencyPreviewSettings GetTransparencyPreviewSettings() const;
    void DispatchTransparencyPreviewSettings(FDWCTransparencyPreviewSettings Settings);

    TSharedRef<ITableRow> GenerateLayerRow(FLayerItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<ITableRow> GenerateInnerSourceRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleLayerSelectionChanged(FLayerItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleRemoveLayerClicked();
    bool CanRemoveSelectedLayer() const;

    TSharedRef<SWidget> GenerateUVChannelComboItem(TSharedPtr<int32> Item) const;
    FReply HandleManualBaseColorClicked();
    FReply HandleManualPickBaseColorFromUVIslandClicked();
    void HandleManualBaseColorCommitted(FLinearColor NewColor);
    FReply HandleRevealPaintColorClicked();
    void HandleRevealPaintColorCommitted(FLinearColor NewColor);
    ECheckBoxState IsRevealColorPaintEnabledChecked() const;
    void HandleRevealColorPaintEnabledChanged(ECheckBoxState NewState);
    ECheckBoxState IsRevealColorPaintModeChecked(EDWCTransparencyRevealColorBrushMode Mode) const;
    void HandleRevealColorPaintModeChanged(ECheckBoxState NewState, EDWCTransparencyRevealColorBrushMode Mode);
    float GetRevealPaintSizeCm() const;
    FText GetRevealPaintSizeDisplayText() const;
    TOptional<float> GetRevealPaintStrength() const;
    TOptional<float> GetRevealPaintFalloff() const;
    void HandleRevealPaintStrengthCommitted(float Value, ETextCommit::Type CommitType);
    void HandleRevealPaintFalloffCommitted(float Value, ETextCommit::Type CommitType);
    FReply HandleClearRevealColorPaintClicked();
    bool EditRevealColorStrokeHistory(
        const FText& TransactionText,
        FGuid StrokeGuid,
        TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit);
    FReply HandleUndoLastRevealColorStrokeClicked();
    FReply HandleDeleteRevealColorStrokeClicked(FGuid StrokeGuid);
    void HandleRevealColorStrokeEnabledChanged(ECheckBoxState NewState, FGuid StrokeGuid);
    TSharedRef<ITableRow> GenerateRevealColorStrokeRow(
        TSharedPtr<FGuid> Item,
        const TSharedRef<STableViewBase>& OwnerTable);
    void PushRevealColorPaintSettingsToViewport();
    TOptional<float> GetManualInitialTransparencyAlpha() const;
    void HandleManualInitialTransparencyAlphaCommitted(float NewValue, ETextCommit::Type CommitType);

    FReply HandleAddInnerSlotClicked();
    FReply HandleRemoveInnerSlotClicked(int32 PriorityIndex);
    FReply HandleMoveInnerSlotClicked(int32 PriorityIndex, int32 Direction);
    void HandleInnerMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo, int32 PriorityIndex);
    void HandleInnerUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo, int32 PriorityIndex);
    void HandleTransparencyTargetPartsResized(float NewHeight);

    TSharedRef<SWidget> BuildControlPanel();
    TSharedRef<SWidget> BuildStageNavigation();
    TSharedRef<SWidget> BuildStructureSetupStage();
    TSharedRef<SWidget> BuildMapGenerationStage();
    TSharedRef<SWidget> BuildRevealEditingStage();
    TSharedRef<SWidget> BuildFinalEditingStage();
    TSharedRef<SWidget> BuildFinalEditingNotice();
    TSharedRef<SWidget> BuildSourceTypeCard(
        EDWCTransparencySourceType SourceType,
        const FText& Title,
        const FText& Description,
        const FText& Availability);
    TSharedRef<SWidget> BuildTargetMeshSection();
    TSharedRef<SWidget> BuildTransparencyLayersSection();
    TSharedRef<SWidget> BuildSameMeshSourceSection();
    TSharedRef<SWidget> BuildOtherMeshSourceSection();
    TSharedRef<SWidget> BuildExternalMeshSourceSection();
    TSharedRef<SWidget> BuildManualSourceSection();
    TSharedRef<SWidget> BuildRevealColorEditingSection();
    TSharedRef<SWidget> BuildRaySettingsSection();
    TSharedRef<SWidget> BuildBakeSettingsSection(bool bShowResolution);
    TSharedRef<SWidget> BuildTransparencyBrushSection();
    TSharedRef<SWidget> BuildBrushSizeControl(const FText& Label, EDWCTransparencyBrushSizeTarget Target);
    TSharedRef<SWidget> BuildBrushSizeMenu(EDWCTransparencyBrushSizeTarget Target);
    TSharedRef<SWidget> BuildTransparencyStrokeList();
    TSharedRef<SWidget> BuildGeneratedOutputsSection();
    TSharedRef<SWidget> BuildPackedTransparencyMapSection();
    TSharedRef<SWidget> BuildTransparencyPreviewSection();
    TSharedRef<SWidget> BuildPreviewSettingsSection();
    TSharedRef<SWidget> BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label);
    TSharedRef<SWidget> BuildAssetSummaryRow(
        UObject* Asset,
        const FText& Label,
        const FText& Detail,
        TArray<TSharedPtr<FAssetThumbnail>>& ThumbnailStorage);
    TSharedRef<SWidget> BuildEmptyAssetRow(const FText& Label) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
    TSharedPtr<FDWCTransparencyAuthoringController> AuthoringController;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    FDWCEditorWorkerJobSchedulerPtr WorkerJobScheduler;
    TSharedPtr<FDWCEditorBakeCoordinator> BakeCoordinator;
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> WrinkleSuppressionCoverageService;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<class SBox> ControlPanelContainer;
    TSharedPtr<class SWidgetSwitcher> StageContentSwitcher;
    TSharedPtr<class SWidgetSwitcher> MapGenerationSettingsSwitcher;
    TSharedPtr<class SBox> RevealEditingContentContainer;
    TSharedPtr<class SBox> FinalEditingNoticeContainer;
    TSharedPtr<class SBox> FinalEditingPreviewSettingsContainer;
    TSharedPtr<class SBox> FinalEditingGeneratedOutputsContainer;
    TSharedPtr<class SBox> TransparencyStrokeListContainer;
    TArray<TSharedPtr<FGuid>> RevealColorStrokeItems;
    TSharedPtr<class SListView<TSharedPtr<FGuid>>> RevealColorStrokeListView;
    TSharedPtr<class SScrollBox> ControlPanelScrollBox;
    TSharedPtr<class SComboButton> TransparencyBrushSizeComboButton;
    TSharedPtr<class SComboButton> RevealPaintSizeComboButton;
    TSharedPtr<SWetClothingTransparencyPreviewViewport> PreviewViewport;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> TargetMeshThumbnails;
    TArray<TSharedPtr<FAssetThumbnail>> GeneratedOutputThumbnails;
    // Shared by target and inner-source rows. The thumbnail pool owns rendering;
    // this map only keeps one lightweight thumbnail wrapper per material slot.
    TMap<int32, TSharedPtr<FAssetThumbnail>> MaterialSlotThumbnails;
    FString StatusMessage;
    EDWCTransparencyPanelStatus PanelStatus = EDWCTransparencyPanelStatus::Info;
    FString InnerSourceStatusMessage;
    FGuid SelectedLayerGuid;
    TArray<FLayerItemPtr> LayerItems;
    TSharedPtr<class SListView<FLayerItemPtr>> LayerListView;
    TArray<TSharedPtr<int32>> InnerSourceSlotItems;
    TSharedPtr<class SListView<TSharedPtr<int32>>> InnerSourceListView;
    // Inner source slots remain unrestricted. Target slots are limited to Wettable slots.
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    TArray<FMaterialSlotItemPtr> TargetMaterialSlotItems;
    FDWCEditorPreviewSlotCollection PreviewSlotStates;
    TArray<TSharedPtr<int32>> UVChannelItems;
    TArray<TSharedPtr<EDWCTransparencyVisualizationMode>> VisualizationModeItems;
    TWeakObjectPtr<USkeletalMesh> OptionItemsTargetMesh;
    int32 OptionItemsMaterialSlotCount = INDEX_NONE;
    int32 OptionItemsUVChannelCount = INDEX_NONE;
    uint32 OptionItemsPreviewStateSignature = 0;
    bool bPreviewSlotStateRefreshRequested = true;
    EDWCTransparencyVisualizationMode SelectedVisualizationMode = static_cast<EDWCTransparencyVisualizationMode>(0);
    float WetnessPreviewPercent = 100.0f;
    bool bShowSavedWrinkle = true;
    EDWCTransparencyBrushMode BrushMode = EDWCTransparencyBrushMode::Apply;
    float BrushRadiusUV = 0.0677f;
    float BrushStrength = 0.5f;
    float BrushFalloff = 0.5f;
    float BrushSpacing = 0.25f;
    float BrushTargetAlpha = 1.0f;
    EDWCTransparencyRevealColorBrushMode RevealPaintMode = EDWCTransparencyRevealColorBrushMode::Paint;
    FLinearColor RevealPaintColor = FLinearColor::White;
    float RevealPaintRadiusUV = 0.0677f;
    float RevealPaintStrength = 1.0f;
    float RevealPaintFalloff = 0.5f;
    bool bRevealColorPaintEnabled = false;
    float TransparencyTargetPartsListHeight = 170.0f;
    bool bRefreshingLayerSelection = false;
    bool bRefreshTimerRegistered = false;
    bool bApplyingSessionState = false;
    bool bPreviewSuspended = false;
    EDWCTransparencyPanelRefreshFlags PendingRefreshFlags = EDWCTransparencyPanelRefreshFlags::None;
    TMap<FGuid, EDWCTransparencyEditorStage> StageByLayer;
    TMap<FGuid, TSharedPtr<FDWCTransparencySourcePayload>> AutoBakeResults;
    FDWCEditorWorkerJobTicket PendingRevealCommitTicket;
    uint64 RevealCommitEpoch = 1;
    bool bRevealCommitInFlight = false;
};
