#pragma once

#include "CoreMinimal.h"
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
    TSharedRef<SWidget> BuildPreviewSettingsSection();

    float GetPreviewAbsorbedWaterPercent() const;
    void HandlePreviewAbsorbedWaterPercentChanged(float InPercent);
    FText GetPreviewAbsorbedWaterPercentText() const;

    float GetPreviewSurfaceWaterPercent() const;
    void HandlePreviewSurfaceWaterPercentChanged(float InPercent);
    FText GetPreviewSurfaceWaterPercentText() const;

  private:
    TWeakObjectPtr<UWetnessProfile>     WetnessProfile;
    TSharedPtr<IDetailsView>            DetailsView;
    TSharedPtr<SWetnessProfileViewport> PreviewViewport;
};
