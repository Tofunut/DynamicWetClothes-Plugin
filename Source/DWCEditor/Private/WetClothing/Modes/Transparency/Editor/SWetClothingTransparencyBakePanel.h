#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class IDetailsView;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SWetClothingTransparencyPreviewViewport;
class USkeletalMesh;
class UWetClothingAsset;
class UObject;
enum class EWetClothingTransparencyPreviewMode : uint8;
enum class EDWCTransparencyVisualizationMode : uint8;
struct FDWCTransparencyAutoBakeResult;

enum class EDWCTransparencyRevealMapType : uint8
{
    Color,
    Mask,
    Confidence,
    Lookup
};

enum class EDWCTransparencyPanelStatus : uint8
{
    Info,
    Ready,
    Warning,
    Error
};

struct FDWCTransparencyLayerListItem
{
    FGuid LayerGuid;
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
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromAsset();
    bool HasPendingTransparencySetup(FString* OutSummary = nullptr) const;
    bool BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveTransparencySetupAssets() const;
    void RebuildEditorLayout();

  private:
    const UClass* GetSelectedSourceClass() const;
    void HandleSourceClassChanged(const UClass* NewClass);
    FReply HandleGenerateTransparencyMapClicked();
    FReply HandleBakeEditedTransparencyMapClicked();
    FReply HandleFocusPreviewClicked();
    FText GetStatusText() const;
    FSlateColor GetStatusColor() const;
    FText GetGenerateTooltipText() const;
    FText GetBakeEditedTooltipText() const;
    FText GetInnerSourceStatusText() const;
    float GetWetnessPreviewPercent() const;
    void HandleWetnessPreviewChanged(float InValue);
    TOptional<float> GetTransparencyPreviewStrength() const;
    void HandleTransparencyPreviewStrengthCommitted(float InValue, ETextCommit::Type CommitType);
    TOptional<float> GetWrinkleSuppressionStrength() const;
    void HandleWrinkleSuppressionStrengthCommitted(float InValue, ETextCommit::Type CommitType);
    ECheckBoxState IsBrushModeChecked(EDWCTransparencyBrushMode Mode) const;
    void HandleBrushModeChanged(ECheckBoxState NewState, EDWCTransparencyBrushMode Mode);
    TOptional<float> GetBrushRadius() const;
    TOptional<float> GetBrushStrength() const;
    TOptional<float> GetBrushFalloff() const;
    TOptional<float> GetBrushSpacing() const;
    TOptional<float> GetBrushTargetAlpha() const;
    void HandleBrushRadiusCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushStrengthCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushFalloffCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushSpacingCommitted(float Value, ETextCommit::Type CommitType);
    void HandleBrushTargetAlphaCommitted(float Value, ETextCommit::Type CommitType);
    void PushPaintSettingsToViewport();
    void HandleViewportStrokesChanged();
    FReply HandleUndoLastStrokeClicked();
    FReply HandleClearStrokesClicked();
    FReply HandleDeleteStrokeClicked(FGuid StrokeGuid);
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FGuid StrokeGuid);
    TSharedRef<SWidget> GenerateVisualizationModeComboItem(TSharedPtr<EDWCTransparencyVisualizationMode> Item) const;
    void HandleVisualizationModeChanged(TSharedPtr<EDWCTransparencyVisualizationMode> Item, ESelectInfo::Type SelectInfo);
    FText GetVisualizationModeLabel(EDWCTransparencyVisualizationMode Mode) const;
    TSharedPtr<EDWCTransparencyVisualizationMode> FindVisualizationModeItem(EDWCTransparencyVisualizationMode Mode) const;
    ECheckBoxState IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const;
    void HandlePreviewModeChanged(ECheckBoxState NewState, EWetClothingTransparencyPreviewMode Mode);
    ECheckBoxState IsRevealMapTypeChecked(EDWCTransparencyRevealMapType MapType) const;
    void HandleRevealMapTypeChanged(ECheckBoxState NewState, EDWCTransparencyRevealMapType MapType);
    bool IsGenerateEnabled() const;
    bool IsBakeEditedEnabled() const;
    bool CanUseFullBlueprintPreview() const;
    void UpdateInnerSourceStatus();

    bool RefreshOptionItems();
    void RefreshLayerItems();
    void RefreshViewportContext();
    FWetClothingTransparencyLayerData* GetSelectedLayer();
    const FWetClothingTransparencyLayerData* GetSelectedLayer() const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 SlotIndex) const;
    TSharedPtr<int32> FindUVChannelItem(int32 UVChannelIndex) const;
    TSharedPtr<EDWCTransparencySourceType> FindSourceTypeItem(EDWCTransparencySourceType SourceType) const;
    TSharedPtr<EDWCTransparencyUVAddressMode> FindAddressModeItem(EDWCTransparencyUVAddressMode AddressMode) const;
    void EditSelectedLayer(const FText& TransactionText, TFunctionRef<void(FWetClothingTransparencyLayerData&)> Edit, bool bRebuildLayout);
    void EditGlobalSettings(const FText& TransactionText, TFunctionRef<void(FWetClothingTransparencyData&)> Edit);

    TSharedRef<ITableRow> GenerateLayerRow(FLayerItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleLayerSelectionChanged(FLayerItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleAddLayerClicked();
    FReply HandleRemoveLayerClicked();
    bool CanRemoveSelectedLayer() const;

    TSharedRef<SWidget> GenerateMaterialSlotComboItem(FMaterialSlotItemPtr Item) const;
    TSharedRef<SWidget> GenerateUVChannelComboItem(TSharedPtr<int32> Item) const;
    TSharedRef<SWidget> GenerateSourceTypeComboItem(TSharedPtr<EDWCTransparencySourceType> Item) const;
    TSharedRef<SWidget> GenerateAddressModeComboItem(TSharedPtr<EDWCTransparencyUVAddressMode> Item) const;
    FText GetMaterialSlotLabel(int32 SlotIndex) const;
    FText GetSourceTypeLabel(EDWCTransparencySourceType SourceType) const;
    FText GetAddressModeLabel(EDWCTransparencyUVAddressMode AddressMode) const;
    void HandleOuterMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);
    void HandleOuterUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    void HandleSourceTypeChanged(TSharedPtr<EDWCTransparencySourceType> Item, ESelectInfo::Type SelectInfo);
    void HandleAddressModeChanged(TSharedPtr<EDWCTransparencyUVAddressMode> Item, ESelectInfo::Type SelectInfo);

    FReply HandleAddInnerSlotClicked();
    FReply HandleRemoveInnerSlotClicked(int32 PriorityIndex);
    FReply HandleMoveInnerSlotClicked(int32 PriorityIndex, int32 Direction);
    void HandleInnerSlotEnabledChanged(ECheckBoxState NewState, int32 PriorityIndex);
    void HandleInnerMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo, int32 PriorityIndex);
    void HandleInnerUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo, int32 PriorityIndex);

    TSharedRef<SWidget> BuildControlPanel();
    TSharedRef<SWidget> BuildTargetMeshSection();
    TSharedRef<SWidget> BuildTransparencyLayersSection();
    TSharedRef<SWidget> BuildTargetSurfaceSection();
    TSharedRef<SWidget> BuildInnerSourceSection();
    TSharedRef<SWidget> BuildSameMeshSourceSection();
    TSharedRef<SWidget> BuildOtherMeshSourceSection();
    TSharedRef<SWidget> BuildManualSourceSection();
    TSharedRef<SWidget> BuildRaySettingsSection();
    TSharedRef<SWidget> BuildBakeSettingsSection();
    TSharedRef<SWidget> BuildTransparencyBrushSection();
    TSharedRef<SWidget> BuildGeneratedOutputsSection();
    TSharedRef<SWidget> BuildPackedTransparencyMapSection();
    TSharedRef<SWidget> BuildBakeSection();
    TSharedRef<SWidget> BuildTransparencyPreviewSection();
    TSharedRef<SWidget> BuildPreviewSettingsSection();
    TSharedRef<SWidget> BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label);
    TSharedRef<SWidget> BuildRevealMapTypeButton(EDWCTransparencyRevealMapType MapType, const FText& Label);
    TSharedRef<SWidget> BuildRevealMaterialSection();
    TSharedRef<SWidget> BuildRevealTextureSection();
    TSharedRef<SWidget> BuildSelectedRevealMapPreview(UObject* Texture, const FText& Label, const FText& Detail);
    TSharedRef<SWidget> BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail = FText::GetEmpty());
    TSharedRef<SWidget> BuildEmptyAssetRow(const FText& Label) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<class SBox> ControlPanelContainer;
    TSharedPtr<class SScrollBox> ControlPanelScrollBox;
    TSharedPtr<SWetClothingTransparencyPreviewViewport> PreviewViewport;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> ActiveThumbnails;
    FString StatusMessage;
    EDWCTransparencyPanelStatus PanelStatus = EDWCTransparencyPanelStatus::Info;
    FString InnerSourceStatusMessage;
    FGuid SelectedLayerGuid;
    TArray<FLayerItemPtr> LayerItems;
    TSharedPtr<class SListView<FLayerItemPtr>> LayerListView;
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    TArray<TSharedPtr<int32>> UVChannelItems;
    TArray<TSharedPtr<EDWCTransparencySourceType>> SourceTypeItems;
    TArray<TSharedPtr<EDWCTransparencyUVAddressMode>> AddressModeItems;
    TArray<TSharedPtr<EDWCTransparencyVisualizationMode>> VisualizationModeItems;
    TWeakObjectPtr<USkeletalMesh> OptionItemsTargetMesh;
    int32 OptionItemsMaterialSlotCount = INDEX_NONE;
    int32 OptionItemsUVChannelCount = INDEX_NONE;
    EDWCTransparencyRevealMapType SelectedRevealMapType = EDWCTransparencyRevealMapType::Color;
    EDWCTransparencyVisualizationMode SelectedVisualizationMode = static_cast<EDWCTransparencyVisualizationMode>(0);
    float WetnessPreviewPercent = 100.0f;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    EDWCTransparencyBrushMode BrushMode = EDWCTransparencyBrushMode::Apply;
    float BrushRadiusUV = 0.025f;
    float BrushStrength = 0.5f;
    float BrushFalloff = 0.5f;
    float BrushSpacing = 0.25f;
    float BrushTargetAlpha = 1.0f;
    bool bRefreshingLayerSelection = false;
    TMap<FGuid, TSharedPtr<FDWCTransparencyAutoBakeResult>> AutoBakeResults;
};
