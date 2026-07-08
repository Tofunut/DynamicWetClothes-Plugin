#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class FAssetThumbnail;
class FAssetThumbnailPool;
class SWetClothingTransparencyPreviewViewport;
class UWetClothingAsset;
class UObject;
enum class EWetClothingTransparencyPreviewMode : uint8;

class SWetClothingTransparencyEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingTransparencyEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromAsset();
    bool HasPendingTransparencySetup(FString* OutSummary = nullptr) const;
    bool BuildTransparencySetup(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveTransparencySetupAssets() const;

  private:
    const UClass* GetSelectedSourceClass() const;
    void HandleSourceClassChanged(const UClass* NewClass);
    FReply HandleBuildAndSaveClicked();
    FReply HandleFocusPreviewClicked();
    FText GetStatusText() const;
    FText GetBuildButtonText() const;
    FText GetTargetMeshText() const;
    float GetWetnessPreviewPercent() const;
    void HandleWetnessPreviewChanged(float InValue);
    ECheckBoxState IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const;
    void HandlePreviewModeChanged(ECheckBoxState NewState, EWetClothingTransparencyPreviewMode Mode);
    bool IsBuildEnabled() const;
    TSharedRef<SWidget> BuildControlPanel();
    TSharedRef<SWidget> BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label);
    TSharedRef<SWidget> BuildTargetMeshSection();
    TSharedRef<SWidget> BuildRevealMaterialSection();
    TSharedRef<SWidget> BuildRevealTextureSection();
    TSharedRef<SWidget> BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail = FText::GetEmpty());
    TSharedRef<SWidget> BuildEmptyAssetRow(const FText& Label) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetClothingTransparencyPreviewViewport> PreviewViewport;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> ActiveThumbnails;
    FString StatusMessage;
    float WetnessPreviewPercent = 100.0f;
};
