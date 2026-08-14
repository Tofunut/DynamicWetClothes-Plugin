// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Modes/Part/Partition/WetPartAutoPartitionTypes.h"
#include "WetClothing/Modes/Part/Presentation/DWCPartPresentationModel.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Modes/Part/Viewport/SDWCPartViewport.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class FDWCEditorCacheStore;
class FDWCEditorPreviewResourceContext;
class FDWCEditorAuthoringDocument;
class FDWCEditorSessionStore;
class FDWCPartAuthoringController;
class FDWCEditorRenderUploadQueue;
class FDWCEditorResourceGovernor;
class FDWCEditorTextureWorkspace;
class FDWCEditorWorkerJobScheduler;
class IDetailsView;
class STableViewBase;
class SInlineEditableTextBlock;
class SWindow;
class UTexture;
class UTexture2D;
struct FSlateBrush;
struct FDWCDataUVBuildResult;
class FDWCDataUVBuildSelection;
enum class EDWCEditorPreviewSuspendReason : uint8;

class SWetClothingPartEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingPartEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorAuthoringDocument>, AuthoringDocument)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSessionStore>, SessionStore)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorCacheStore>, CacheStore)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorPreviewResourceContext>, PreviewResources)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorResourceGovernor>, ResourceGovernor)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorWorkerJobScheduler>, WorkerJobScheduler)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();
    void SuspendPreview(EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewIfNeeded();
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeRenderProfileDataAndUpdateMaterials(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveBakedRenderProfileAssets() const;

  private:
    using FMaterialSlotItemPtr = TSharedPtr<FWCAMaterialSlotItem>;
    using FTextureItemPtr = TSharedPtr<FWCATextureItem>;
    using FUVIslandItemPtr = TSharedPtr<FWetClothingAssetUVIsland>;
    using FUVSelectionToolItemPtr = TSharedPtr<FWCAUVSelectionToolItem>;
    using FAutoPartitionColorModeItemPtr = TSharedPtr<EWetPartAutoPartitionColorMode>;
    using FWetPartEntryPtr = FDWCPartPresentationItemPtr;

    void RefreshMaterialSlotItems();
    void RefreshMaterialTextures(bool bRefreshUVView = true);
    void RefreshTextureToggleWidgets();
    void RefreshOriginalUVChannel();
    void RefreshUVIslandList();
    void RefreshUVView();
    void RefreshPreviewIslandHighlight();
    void RefreshWetPartList(bool bRefreshUVView = true);
    bool RebuildWetPartPresentationSnapshot();
    void RefreshPreviewWetPartOverlay();
    void RefreshIslandSelectionViews();
    void RefreshWetPartAssignmentViews();
    void RebuildAllMaterialSlotPresentation();
    bool RebuildMaterialSlotPresentation(int32 MaterialSlotIndex);
    const FDWCPartSlotPresentationItem* FindMaterialSlotPresentation(int32 MaterialSlotIndex) const;

    int32                           GetOriginalUVChannelIndex() const;
    bool                            HasValidOriginalUVChannel() const;
    const FWetClothingWetPartEntry* FindWetPartEntry(int32 WetPartID) const;
    FWetPartEntryPtr                FindWetPartItemByID(int32 WetPartID) const;
    FMaterialSlotItemPtr            FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    TSet<int32>                     GetUVIslandIDsForWetPart(int32 WetPartID) const;
    int32                           GetEffectiveWetPartForUVIsland(int32 UVIslandID) const;

    void ApplyIslandSelection(const TArray<int32>& HitUVIslandIDs, bool bAppendSelection);
    void SetSelectedUVIslandIDs(const TSet<int32>& InSelectedUVIslandIDs, int32 InPrimarySelectedUVIslandID, bool bSyncListSelection = true);
    void SyncUVIslandListSelectionToState();
    void ResetIslandSelection();

    TSharedRef<ITableRow>     GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<SWidget>       BuildMaterialSlotPreviewWidget(int32 MaterialSlotIndex) const;
    void                      HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type);
    void                      HandleMaterialSlotDoubleClicked(FMaterialSlotItemPtr Item);
    FReply                    HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex);
    TSharedRef<SWidget>       GenerateTextureComboItem(FTextureItemPtr Item);
    void                      HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>       BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout);
    FReply                    HandleApplyMaterialSetupClicked();
    bool                      IsApplyMaterialSetupEnabled() const;
    TSharedRef<ITableRow>     GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                      HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo);
    void                      HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& UVIslandIDs, EWCAUVSelectionOp SelectionOp);
    void                      HandleUVIslandPickedFromPreview(int32 UVIslandID, bool bAppendSelection);
    TSharedRef<SWidget>       GenerateAutoPartitionColorModeComboItem(FAutoPartitionColorModeItemPtr Item) const;
    void                      HandleAutoPartitionColorModeSelectionChanged(FAutoPartitionColorModeItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow>     GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                      HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    void                      HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item);
    void                      HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item);
    FReply                    HandleWetPartColorClicked(FWetPartEntryPtr Item);
    void                      HandleWetPartColorCommitted(FLinearColor NewColor, FWetPartEntryPtr Item);
    FReply                    HandleToggleWetPartViewClicked(FWetPartEntryPtr Item);
    const FSlateBrush*        GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const;
    void                      HandleWetnessProfilePicked(FWetPartEntryPtr Item, const FAssetData& ProfileAssetData);
    TSharedRef<SWidget>       GenerateAssignWetPartComboItem(FWetPartEntryPtr Item);
    void                      HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    FReply                    HandleAddWetPartClicked();
    FReply                    HandleRemoveWetPartClicked();
    FReply                    HandleResetWetPartClicked(FWetPartEntryPtr Item);
    bool                      IsWetPartRemoveEnabled() const;
    bool                      IsAutoPartitionEnabled() const;
    bool                      HasAutoPartitionDataToReplace() const;
    FReply                    HandleAutoPartitionClicked();
    UTexture2D*               ResolveAutoPartitionTexture() const;
    TMap<int32, FLinearColor> BuildAutoPartitionPreviewColorMap(const TArray<FWetPartAutoPartitionCluster>& Clusters) const;
    void                      ApplyAutoPartitionClusters(const TArray<FWetPartAutoPartitionCluster>& Clusters);
    FReply                    HandleAssignSelectedUVIslandToWetPartClicked();
    void                      SetCurrentUVSelectionTool(EWCAUVSelectionTool InTool);
    const FSlateBrush*        GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const;
    FSlateColor               GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const;

    FText                 GetMaterialSlotStatusText(int32 MaterialSlotIndex) const;
    FSlateColor           GetMaterialSlotStatusColor(int32 MaterialSlotIndex) const;
    FText                 GetMaterialSlotStatusTooltip(int32 MaterialSlotIndex) const;
    bool                  ShouldShowMaterialSlotStatusInfo(int32 MaterialSlotIndex) const;
    const FSlateBrush*    GetMaterialSlotStatusInfoBrush(int32 MaterialSlotIndex) const;
    FSlateColor           GetMaterialSlotStatusInfoColor(int32 MaterialSlotIndex) const;
    FReply                HandleMaterialSlotStatusInfoClicked(int32 MaterialSlotIndex);
    FSlateColor           GetMaterialSlotRowBackgroundColor(int32 MaterialSlotIndex) const;
    FSlateColor           GetMaterialSlotRowAccentColor(int32 MaterialSlotIndex) const;
    bool                  IsMaterialSlotIncludedInDataUVLayout(int32 MaterialSlotIndex) const;
    bool                  IsMaterialSlotPartMapComplete(int32 MaterialSlotIndex) const;
    bool                  DoesMaterialSlotNeedPartMapAttention(int32 MaterialSlotIndex) const;
    FText                 GetMaterialSlotPartMapWarningText(int32 MaterialSlotIndex) const;
    TSet<int32>           CollectExistingDataUVSlotIndices() const;
    TSet<int32>           CollectSelectableDataUVOperationSlotIndices() const;
    TSet<int32>           CollectSelectedGenerateDataUVSlotIndices() const;
    bool                  IsMissingPreparedMeshRecoveryRequired() const;
    bool                  ConfirmMissingPreparedMeshRecovery() const;
    bool                  IsDataUVOperationSelectable(int32 MaterialSlotIndex) const;
    void                  HandleDataUVOperationCheckStateChanged(ECheckBoxState NewState, int32 MaterialSlotIndex);
    void                  HandleAllDataUVOperationCheckStateChanged(ECheckBoxState NewState);
    void                  SyncDataUVOperationSelection();
    bool CaptureDataUVBuildSelection(
        const TSet<int32>&        RequestedMaterialSlotIndices,
        FDWCDataUVBuildSelection& OutSelection,
        FString*                  OutErrorMessage = nullptr) const;
    FDWCDataUVBuildResult GenerateDataUVForSelection(
        const FDWCDataUVBuildSelection& Selection,
        const TSet<int32>* ConfirmedVisibleExclusionMaterialSlotIndices = nullptr,
        const TSet<int32>* SkippedMaterialSlotIndices = nullptr);
    void                                RestorePersistedDataUVFailureState();
    void                                PersistDataUVFailureState();
    EVisibility                         GetDataUVUpdateBarVisibility() const;
    FText                               GetDataUVOperationSummaryText() const;
    FText                               GetDataUVOperationButtonText() const;
    FText                               GetDataUVOperationButtonTooltip() const;
    bool                                IsDataUVOperationEnabled() const;
    FReply                              HandleDataUVOperationClicked();
    float                               GetUVViewBackgroundTextureOpacity() const;
    float                               GetUVViewIslandLineOpacity() const;
    float                               GetUVViewIslandLineThicknessScale() const;
    void                                HandleUVViewBackgroundTextureOpacityChanged(float NewValue);
    void                                HandleUVViewIslandLineOpacityChanged(float NewValue);
    void                                HandleUVViewIslandLineThicknessScaleChanged(float NewValue);
    FText                               GetUVIslandCountText() const;
    FText                               GetSelectedUVIslandText() const;
    EVisibility                         GetSelectedUVIslandTextVisibility() const;
    FText                               GetUVIslandAssignmentSummaryText() const;
    FText                               GetUVIslandAssignmentButtonText() const;
    FText                               GetUVIslandAssignmentButtonTooltip() const;
    FText                               GetUVStatusText() const;
    EVisibility                         GetUVStatusOverlayVisibility() const;
    EVisibility                         GetUVEditorContentVisibility() const;
    EVisibility                         GetUVIslandStatusOverlayVisibility() const;
    FText                               GetCommonPartEditingEmptyStateText() const;
    FText                               GetWetPartSectionText() const;
    FText                               GetSelectedAssignWetPartText() const;
    FText                               GetSelectedAssignWetPartNameText() const;
    FText                               GetSelectedAssignWetPartIDText() const;
    EVisibility                         GetSelectedAssignWetPartIDVisibility() const;
    FSlateColor                         GetSelectedAssignWetPartColor() const;
    FText                               GetSelectedWetPartText() const;
    EVisibility                         GetSelectedWetPartTextVisibility() const;
    FText                               GetWetnessProfileLibraryStatusText() const;
    EVisibility                         GetWetnessProfileLibraryStatusVisibility() const;
    bool                                IsSelectedMaterialSlotWettable() const;
    bool                                IsMaterialSlotDataUVReadyForEditing(int32 MaterialSlotIndex) const;
    bool                                IsSelectedMaterialSlotPartEditingReady() const;
    bool                                IsAssignUVIslandToWetPartEnabled() const;
    FString                             GetWetnessProfileObjectPath(FWetPartEntryPtr Item) const;
    bool                                IsWetnessProfileControlEnabled(FWetPartEntryPtr Item) const;
    bool                                IsWetnessProfileBrowseEnabled(FWetPartEntryPtr Item) const;
    void                                HandleUseSelectedWetnessProfileClicked(FWetPartEntryPtr Item);
    void                                HandleBrowseWetnessProfileClicked(FWetPartEntryPtr Item);
    FText                               GetAutoPartitionColorModeText() const;
    float                               GetAutoPartitionTolerance() const;
    void                                HandleAutoPartitionToleranceChanged(float InValue);
    bool                                IsSelectedWetPartSurfaceSettingsEnabled() const;
    bool                                IsSurfaceWaterTilingEnabled(FWetPartEntryPtr Item) const;
    FReply                              HandleOpenSurfaceWaterTilingClicked(FWetPartEntryPtr Item);
    TSharedRef<SWidget>                 BuildSurfaceWaterTilingWindowContent();
    void                                PausePartPreviewForSurfaceWaterTiling();
    void                                ResumePartPreviewAfterSurfaceWaterTiling();
    void                                RefreshSurfaceWaterTilingPreview();
    void                                ResetSurfaceWaterTilingPreviewState();
    void                                UpdateSurfaceWaterTilingWindowTitle();
    bool                                IsSurfaceWaterTilingDirty() const;
    const FWetPartSurfaceWaterSettings* GetSurfaceWaterTilingWorkingSettings() const;
    void                                NotifySurfaceWaterTilingWorkingSettingsChanged();
    FReply                              HandleApplySurfaceWaterTilingChangesClicked();
    FReply                              HandleRevertSurfaceWaterTilingChangesClicked();
    void                                ResetSelectedDropletStampOverrideToApplied();
    void                                ResetSelectedDropletStampScaleToApplied();
    void                                ResetSelectedDropletFlowStampOverrideToApplied();
    void                                ResetSelectedDropletFlowStampScaleToApplied();
    void                                ResetSelectedDropletDetailSizeToApplied();
    void                                ResetSelectedDropletFlowDetailSizeToApplied();
    void                                HandleSurfaceWaterTilingWindowClosed(const TSharedRef<SWindow>& Window);
    void                                HandleSelectedDropletStampSizeOverrideChanged(ECheckBoxState NewState);
    bool                                IsSelectedDropletStampSizeOverrideEnabled() const;
    void                                HandleSelectedDropletFlowStampSizeOverrideChanged(ECheckBoxState NewState);
    bool                                IsSelectedDropletFlowStampSizeOverrideEnabled() const;
    float                               GetSelectedDropletDetailSize() const;
    float                               GetSelectedDropletFlowDetailSize() const;
    void                                HandleSelectedDropletRadiusScaleChanged(float InValue);
    void                                HandleSelectedDropletFlowSizeScaleChanged(float InValue);
    void                                HandleSelectedDropletDetailSizeChanged(float InValue);
    void                                HandleSelectedDropletFlowDetailSizeChanged(float InValue);
    void                                HandleSurfaceWaterPreviewCoverageModeChanged(ECheckBoxState NewState, EDWCSurfaceWaterTilingPreviewCoverageMode Mode);
    void                                HandleSurfaceWaterPreviewDisplayModeChanged(ECheckBoxState NewState, EDWCSurfaceWaterTilingPreviewDisplayMode Mode);
    ECheckBoxState                      GetShowPartColorsCheckState() const;
    void                                HandleShowPartColorsChanged(ECheckBoxState NewState);
    float                               GetPartColorIntensity() const;
    void                                HandlePartColorIntensityChanged(float InValue);
    float                               GetSelectionLineThicknessScale() const;
    void                                HandleSelectionLineThicknessChanged(float InValue);
    TSharedRef<SWidget>                 BuildPartPreviewControlsPanel();
    FReply                              HandleFocusPreviewClicked();
    FReply                              HandleSaveAssetClicked();
    FReply                              HandleBakeAllMapsClicked();
    bool                                IsRenderProfileBakeSourceValid() const;
    FReply                              HandleBakeRenderProfileDataClicked();
    UTexture*                           ResolveSelectedMaterialTexture() const;
    UTexture*                           ResolveTextureAddressTexture() const;
    void                                SaveSelectedTexture();
    void                                SaveTextureSelection(int32 MaterialSlotIndex, UTexture* Texture);
    void                                HandleSessionStateChanged(
        const FDWCEditorSessionState& State,
        EDWCEditorSessionEffect Effects,
        uint64 Revision);
    EActiveTimerReturnType              FlushPendingAuthoringRefresh(double CurrentTime, float DeltaTime);

  private:
    TWeakObjectPtr<UWetClothingAsset>                 WetClothingAsset;
    TSharedPtr<FDWCEditorAuthoringDocument>            AuthoringDocument;
    TSharedPtr<FDWCEditorSessionStore>                 SessionStore;
    TSharedPtr<FDWCPartAuthoringController>            AuthoringController;
    EDWCEditorSessionEffect                            PendingAuthoringEffects = EDWCEditorSessionEffect::None;
    TSet<int32>                                        PendingPartRefreshMaterialSlotIndices;
    uint64                                             LastHandledPartAuthoringRevision = 0;
    bool                                               bPendingGlobalPartRefresh = false;
    bool                                               bPartRefreshDeferredWhileInactive = false;
    bool                                               bAuthoringRefreshScheduled = false;
    TSharedPtr<FDWCEditorCacheStore>                   CacheStore;
    TSharedPtr<FDWCEditorTextureWorkspace>             TextureWorkspace;
    TSharedPtr<FDWCEditorRenderUploadQueue>            RenderUploadQueue;
    TSharedPtr<FDWCEditorResourceGovernor>              ResourceGovernor;
    TSharedPtr<FDWCEditorWorkerJobScheduler>            WorkerJobScheduler;
    FDWCEditorCacheKey                                  ActiveTopologyKey;
    FDWCEditorCacheLease                                ActiveTopologyLease;
    bool                                                bHasActiveTopologyKey = false;
    TSharedPtr<IDetailsView>                          DetailsView;
    TSharedPtr<SDWCPartViewport>                      PreviewViewport;
    TSharedPtr<SDWCPartViewport>                      SurfaceWaterTilingPreviewViewport;
    TArray<FMaterialSlotItemPtr>                      MaterialSlotItems;
    TMap<int32, FMaterialSlotItemPtr>                 MaterialSlotItemByIndex;
    TSharedPtr<FAssetThumbnailPool>                   MaterialThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>>               MaterialSlotThumbnails;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>> MaterialSlotListView;
    int32                                             SelectedMaterialSlotIndex = INDEX_NONE;
    TArray<FTextureItemPtr>                           TextureItems;
    TArray<TSharedPtr<FAssetThumbnail>>               TextureThumbnails;
    FTextureItemPtr                                   SelectedTextureItem;
    TSharedPtr<class SComboBox<FTextureItemPtr>>      TextureComboBox;
    TSharedPtr<class SBox>                            SelectedTextureComboContentBox;
    TSharedPtr<class SBox>                            TextureSelectionContainer;
    bool                                              bShowMaterialTextureInUVView = true;
    TArray<FUVIslandItemPtr>                          UVIslandItems;
    TSharedPtr<class SListView<FUVIslandItemPtr>>     UVIslandListView;
    int32                                             SelectedUVIslandID = INDEX_NONE;
    TSet<int32>                                       SelectedUVIslandIDs;
    bool                                              bSyncingUVIslandListSelection = false;
    TSharedPtr<SWCAUVView>                            UVView;
    EWCAUVSelectionTool                               CurrentUVSelectionTool = EWCAUVSelectionTool::BoxSelect;
    float                                             UVViewBackgroundTextureOpacity = 0.70f;
    float                                             UVViewIslandLineOpacity = 1.0f;
    float                                             UVViewIslandLineThicknessScale = 1.0f;
    TArray<FUVSelectionToolItemPtr>                   UVSelectionToolItems;
    FUVSelectionToolItemPtr                           SelectedUVSelectionToolItem;
    TArray<FAutoPartitionColorModeItemPtr>            AutoPartitionColorModeItems;
    FAutoPartitionColorModeItemPtr                    SelectedAutoPartitionColorModeItem;
    EWetPartAutoPartitionColorMode                    AutoPartitionColorMode = EWetPartAutoPartitionColorMode::DominantColor;
    FString                                           UVStatusMessage;
    FDWCPartSlotPresentationSnapshot                  MaterialSlotPresentationSnapshot;
    FDWCPartPresentationSnapshot                      WetPartPresentationSnapshot;
    TArray<FWetPartEntryPtr>                          CurrentWetPartItems;
    TSharedPtr<class SListView<FWetPartEntryPtr>>     WetPartListView;
    TSharedPtr<class SComboBox<FWetPartEntryPtr>>     AssignWetPartComboBox;
    TMap<int32, TWeakPtr<SInlineEditableTextBlock>>   WetPartInlineRenameWidgets;
    int32                                             SelectedWetPartID = INDEX_NONE;
    int32                                             SelectedAssignWetPartID = INDEX_NONE;
    bool                                              bShowPartColorsInPreview = true;
    float                                             PartColorIntensity = 1.0f;
    TSet<int32>                                       FailedDataUVSlotIndices;
    TSet<int32>                                       SelectedDataUVOperationSlotIndices;
    FString                                           LastDataUVUpdateError;
    bool                                              bSynchronizingMaterialSlotSelection = false;
    EDWCSurfaceWaterTilingPreviewCoverageMode         SurfaceWaterPreviewCoverageMode = EDWCSurfaceWaterTilingPreviewCoverageMode::FullPart;
    EDWCSurfaceWaterTilingPreviewDisplayMode          SurfaceWaterPreviewDisplayMode = EDWCSurfaceWaterTilingPreviewDisplayMode::Lit;
    TWeakPtr<SWindow>                                 SurfaceWaterTilingWindow;
    bool                                              bSurfaceWaterTilingEditSessionActive = false;
    bool                                              bSurfaceWaterTilingHasPendingChanges = false;
    int32                                             SurfaceWaterTilingEditMaterialSlotIndex = INDEX_NONE;
    int32                                             SurfaceWaterTilingEditWetPartID = INDEX_NONE;
    FString                                           SurfaceWaterTilingEditPartDisplayName;
    FWetPartSurfaceWaterSettings                      SurfaceWaterTilingOriginalSettings;
    FWetPartSurfaceWaterSettings                      SurfaceWaterTilingEditedSettings;
    float                                             AutoPartitionTolerancePercent = 20.0f;
};
