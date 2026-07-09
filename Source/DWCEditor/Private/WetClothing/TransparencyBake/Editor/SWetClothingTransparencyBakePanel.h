#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class IDetailsView;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SWetClothingTransparencyPreviewViewport;
class UWetClothingAsset;
class UObject;
class UTexture2D;
struct FAssetData;
enum class EWetClothingTransparencyPreviewMode : uint8;

enum class EDWCTransparencyRevealSourceType : uint8
{
    MeshRaycast,
    ManualInnerTexture,
    FallbackColor
};

enum class EDWCTransparencyRevealMapType : uint8
{
    Color,
    Mask,
    Confidence,
    Lookup
};

class SWetClothingTransparencyBakePanel : public SCompoundWidget
{
  public:
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
    FReply HandleBakeAndSaveClicked();
    FReply HandleFocusPreviewClicked();
    FReply HandleRevealMapPreviewClicked(UObject* Texture, FText Label, FText Detail);
    FText GetStatusText() const;
    FText GetBakeButtonText() const;
    FText GetTargetMeshText() const;
    FText GetInnerSourceStatusText() const;
    float GetWetnessPreviewPercent() const;
    void HandleWetnessPreviewChanged(float InValue);
    ECheckBoxState IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const;
    void HandlePreviewModeChanged(ECheckBoxState NewState, EWetClothingTransparencyPreviewMode Mode);
    ECheckBoxState IsRevealMapTypeChecked(EDWCTransparencyRevealMapType MapType) const;
    void HandleRevealMapTypeChanged(ECheckBoxState NewState, EDWCTransparencyRevealMapType MapType);
    bool IsBakeEnabled() const;
    void UpdateInnerSourceStatus();

    FText GetRevealSourceTypeText() const;
    FText GetRevealSourceTypeLabel(EDWCTransparencyRevealSourceType SourceType) const;
    TSharedRef<SWidget> GenerateRevealSourceTypeComboItem(TSharedPtr<EDWCTransparencyRevealSourceType> Item) const;
    void HandleRevealSourceTypeChanged(TSharedPtr<EDWCTransparencyRevealSourceType> Item, ESelectInfo::Type SelectInfo);

    FString GetManualInnerColorTexturePath() const;
    FString GetManualInnerMaskTexturePath() const;
    void HandleManualInnerColorTextureChanged(const FAssetData& AssetData);
    void HandleManualInnerMaskTextureChanged(const FAssetData& AssetData);

    TSharedRef<SWidget> BuildControlPanel();
    TSharedRef<SWidget> BuildTargetSection();
    TSharedRef<SWidget> BuildInnerSourceSection();
    TSharedRef<SWidget> BuildGeneratedOutputsSection();
    TSharedRef<SWidget> BuildBakeSection();
    TSharedRef<SWidget> BuildTransparencyPreviewSection();
    TSharedRef<SWidget> BuildPreviewSettingsSection();
    TSharedRef<SWidget> BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label);
    TSharedRef<SWidget> BuildRevealMapTypeButton(EDWCTransparencyRevealMapType MapType, const FText& Label);
    TSharedRef<SWidget> BuildTargetMeshSection();
    TSharedRef<SWidget> BuildRevealMaterialSection();
    TSharedRef<SWidget> BuildRevealTextureSection();
    TSharedRef<SWidget> BuildSelectedRevealMapPreview(UObject* Texture, const FText& Label, const FText& Detail);
    TSharedRef<SWidget> BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail = FText::GetEmpty());
    TSharedRef<SWidget> BuildEmptyAssetRow(const FText& Label) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetClothingTransparencyPreviewViewport> PreviewViewport;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> ActiveThumbnails;
    FString StatusMessage;
    FString InnerSourceStatusMessage;
    TArray<TSharedPtr<EDWCTransparencyRevealSourceType>> RevealSourceTypeItems;
    TSharedPtr<EDWCTransparencyRevealSourceType> SelectedRevealSourceTypeItem;
    EDWCTransparencyRevealSourceType RevealSourceType = EDWCTransparencyRevealSourceType::MeshRaycast;
    EDWCTransparencyRevealMapType SelectedRevealMapType = EDWCTransparencyRevealMapType::Color;
    TWeakObjectPtr<UTexture2D> ManualInnerColorTexture;
    TWeakObjectPtr<UTexture2D> ManualInnerMaskTexture;
    FLinearColor FallbackRevealColor = FLinearColor(0.72f, 0.60f, 0.52f, 1.0f);
    float WetnessPreviewPercent = 100.0f;
};
