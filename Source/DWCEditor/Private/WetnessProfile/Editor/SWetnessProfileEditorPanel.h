#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SWetnessProfileViewport;
class UWetnessProfile;

class SWetnessProfileEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetnessProfileEditorPanel) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromProfile();

  private:
    FReply HandleSaveClicked();
    TSharedRef<SWidget> BuildSimulationModeButton(EDWCSimulationMode Mode, const FText& Label);
    ECheckBoxState IsSimulationModeChecked(EDWCSimulationMode Mode) const;
    void HandleSimulationModeChanged(ECheckBoxState NewState, EDWCSimulationMode Mode);
    float GetPreviewRainRadius() const;
    void HandlePreviewRainRadiusChanged(float InRadius);
    float GetPreviewRainAmountPercent() const;
    void HandlePreviewRainAmountPercentChanged(float InAmountPercent);
    ECheckBoxState IsPreviewWetnessDebugColorChecked() const;
    void HandlePreviewWetnessDebugColorChanged(ECheckBoxState NewState);

  private:
    TWeakObjectPtr<UWetnessProfile>     WetnessProfile;
    TSharedPtr<IDetailsView>            DetailsView;
    TSharedPtr<SWetnessProfileViewport> PreviewViewport;
};
