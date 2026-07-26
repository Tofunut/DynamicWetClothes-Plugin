#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"

DECLARE_DELEGATE(FOnWetWrinkleUVViewSettingsChanged);

class SWetWrinkleUVPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleUVPanel) {}
    SLATE_ATTRIBUTE(FText, ChannelText)
    SLATE_EVENT(FOnWetWrinkleUVViewSettingsChanged, OnViewSettingsChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    TSharedPtr<SWCAUVView> GetView() const { return UVView; }
    EWCAUVDisplayMode GetDisplayMode() const { return DisplayMode; }
    float GetIslandLineOpacity() const { return IslandLineOpacity; }
    float GetIslandLineThicknessScale() const { return IslandLineThicknessScale; }

  private:
    using FDisplayModeItemPtr = TSharedPtr<EWCAUVDisplayMode>;

    FText GetSelectedDisplayModeText() const;
    void HandleDisplayModeChanged(FDisplayModeItemPtr Item);
    void HandleIslandLineOpacityChanged(float NewValue);
    void HandleIslandLineThicknessScaleChanged(float NewValue);
    void ApplyViewSettings();

    TSharedPtr<SWCAUVView> UVView;
    TArray<FDisplayModeItemPtr> DisplayModeItems;
    FDisplayModeItemPtr SelectedDisplayModeItem;
    EWCAUVDisplayMode DisplayMode = EWCAUVDisplayMode::Normal;
    float IslandLineOpacity = 1.0f;
    float IslandLineThicknessScale = 1.0f;
    FOnWetWrinkleUVViewSettingsChanged OnViewSettingsChanged;
};
