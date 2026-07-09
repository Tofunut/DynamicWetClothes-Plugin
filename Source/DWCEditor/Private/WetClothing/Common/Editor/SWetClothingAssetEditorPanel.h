#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Common/Editor/WetClothingEditorMode.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SWetClothingPartEditorPanel;
class SWetClothingTransparencyBakePanel;
class SWetWrinkleEditorPanel;
class SWidgetSwitcher;
class UWetClothingAsset;

class SWetClothingAssetEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingAssetEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakePendingVisualAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveBakedVisualAssets() const;
    bool SaveTransparencySetupAssets() const;
    void SetEditorMode(EWetClothingEditorMode NewMode);

  private:
    int32 GetModeIndex(EWetClothingEditorMode Mode) const;
    EVisibility GetRuntimeReadyWarningVisibility() const;
    FText GetRuntimeReadyWarningText() const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWidgetSwitcher> ModeContentSwitcher;
    TSharedPtr<SWetClothingPartEditorPanel> PartEditorPanel;
    TSharedPtr<SWetWrinkleEditorPanel> WrinkleEditorPanel;
    TSharedPtr<SWetClothingTransparencyBakePanel> TransparencyBakePanel;
};
