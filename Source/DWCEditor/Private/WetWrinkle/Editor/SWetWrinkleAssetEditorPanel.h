#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "WetWrinkle/Viewport/WetWrinkleHitData.h"

class IDetailsView;
class SWetWrinkleAssetViewport;
class UWetWrinkleAsset;

class SWetWrinkleAssetEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleAssetEditorPanel) {}
    SLATE_ARGUMENT(UWetWrinkleAsset*, WetWrinkleAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromAsset();

  private:
    FReply HandleSaveClicked();
    FReply HandleFocusClicked();
    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    void PushBrushSettingsToViewport();

    FText GetHitInfoText() const;
    FText GetStrokeSummaryText() const;

    void HandleUVChannelChanged(int32 NewValue);
    void HandleMaterialSlotChanged(int32 NewValue);
    void HandleBrushRadiusChanged(float NewValue);
    void HandleStrengthChanged(float NewValue);
    void HandleFalloffChanged(float NewValue);
    void HandleRotationChanged(float NewValue);
    void HandlePreviewToggleChanged(ECheckBoxState NewState);
    ECheckBoxState GetPreviewToggleState() const;

  private:
    TWeakObjectPtr<UWetWrinkleAsset> WetWrinkleAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleAssetViewport> PreviewViewport;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentHit;
};
