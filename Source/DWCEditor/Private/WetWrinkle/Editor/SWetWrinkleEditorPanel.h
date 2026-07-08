#pragma once

#include "CoreMinimal.h"
#include "ScopedTransaction.h"
#include "Widgets/SCompoundWidget.h"
#include "WetWrinkle/Viewport/WetWrinkleHitData.h"

class IDetailsView;
class ITableRow;
class SInlineEditableTextBlock;
class SWetWrinkleTexturePreview;
class SWetWrinkleViewport;
class STableViewBase;
class UWetClothingAsset;
class UTexture;
class UTexture2D;
struct FAssetData;
struct FWetWrinklePatchPlacement;
struct FWetWrinklePatchStroke;

struct FWetWrinklePatchStrokeListItem
{
    FGuid StrokeGuid;
};

struct FWetWrinkleBrushPresetOption
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
};

class SWetWrinkleEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromAsset();

  private:
    using FStrokeListItemPtr = TSharedPtr<FWetWrinklePatchStrokeListItem>;

    FReply HandleSaveClicked();
    FReply HandleFocusClicked();
    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeEnded();
    void PushBrushSettingsToViewport();
    void RefreshStrokeList();
    void RefreshStrokeOverlay();
    void RefreshMaterialSlotOptions();
    void RefreshBrushPresetOptions();
    void RefreshTexturePreview();

    FText GetHitInfoText() const;
    FText GetStrokeSummaryText() const;
    TSharedRef<SWidget> GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const;
    FText GetSelectedMaterialSlotText() const;
    void HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget> GenerateBrushPresetComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const;
    FText GetSelectedBrushPresetText() const;
    void HandleBrushPresetChanged(TSharedPtr<FWetWrinkleBrushPresetOption> Item, ESelectInfo::Type SelectInfo);
    FString GetBrushHeightTextureObjectPath() const;
    void HandleBrushHeightTextureChanged(const FAssetData& AssetData);
    TSharedRef<ITableRow> GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleClearStrokesClicked();
    bool IsClearStrokesEnabled() const;
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item);
    void HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item);
    FReply HandleDeleteStrokeClicked(FStrokeListItemPtr Item);

    void HandleUVChannelChanged(int32 NewValue);
    void HandleMaterialSlotChanged(int32 NewValue);
    void HandleBrushRadiusChanged(float NewValue);
    void HandleStrengthChanged(float NewValue);
    void HandleFalloffChanged(float NewValue);
    void HandleRotationChanged(float NewValue);
    void HandlePreviewToggleChanged(ECheckBoxState NewState);
    ECheckBoxState GetPreviewToggleState() const;
    FWetWrinklePatchStroke* FindMutableStroke(const FGuid& StrokeGuid) const;
    const FWetWrinklePatchStroke* FindStroke(const FGuid& StrokeGuid) const;
    FWetWrinklePatchPlacement MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    UTexture* ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    UTexture2D* ResolveDefaultBrushHeightTexture() const;
    FText GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const;
    TSharedPtr<int32> FindMaterialSlotOption(int32 MaterialSlotIndex) const;
    TSharedPtr<FWetWrinkleBrushPresetOption> FindBrushPresetOption(UTexture2D* Texture) const;
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
    TSharedPtr<SWetWrinkleTexturePreview> TexturePreview;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MaterialSlotComboBox;
    TSharedPtr<class SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>> BrushPresetComboBox;
    TSharedPtr<class SListView<FStrokeListItemPtr>> StrokeListView;
    TArray<FStrokeListItemPtr> StrokeListItems;
    TArray<TSharedPtr<int32>> MaterialSlotOptions;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BrushPresetOptions;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentHit;
    FGuid ActiveStrokeGuid;
    FGuid SelectedStrokeGuid;
    FVector2D LastStampUV = FVector2D::ZeroVector;
    int32 LastStampMaterialSlotIndex = INDEX_NONE;
    int32 LastStampUVChannelIndex = INDEX_NONE;
    bool bHasLastStamp = false;
    bool bAllowImmediateNextStrokeStamp = false;
    TUniquePtr<FScopedTransaction> ActivePaintTransaction;
};
