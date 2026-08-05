#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class USkeletalMesh;
struct FSlateBrush;
class UWetnessProfile;
struct FAssetData;

class SWetnessProfileEditorPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SWetnessProfileEditorPanel) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, AbsorbedDetailsView)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, SurfaceDetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromProfile();

private:
    FReply HandleSaveClicked();
    void RefreshDetailsViews();

    TSharedRef<SWidget> BuildPreviewToolbar();
    TSharedRef<SWidget> BuildPreviewControlsSection();
    TSharedRef<SWidget> BuildPreviewModeSection();
    TSharedRef<SWidget> BuildPreviewWaterSection();
    TSharedRef<SWidget> BuildPreviewSimulationSection();
    TSharedRef<SWidget> BuildPreviewDetailSizeSection();

    FString GetCurrentPreviewMeshObjectPath() const;
    void HandleCurrentPreviewMeshChanged(const FAssetData& AssetData);
    FReply HandleUseReferenceMeshClicked();
    FReply HandleSaveCurrentMeshAsReferenceClicked();
    FReply HandleUseSphereMeshClicked();

    float GetPreviewAbsorbedWaterPercent() const;
    void HandlePreviewAbsorbedWaterPercentChanged(float InPercent);
    FText GetPreviewAbsorbedWaterPercentText() const;

    float GetPreviewDroplet1DetailSize() const;
    void HandlePreviewDroplet1DetailSizeChanged(float InValue);
    FText GetPreviewDroplet1DetailSizeText() const;
    float GetPreviewDroplet2DetailSize() const;
    void HandlePreviewDroplet2DetailSizeChanged(float InValue);
    FText GetPreviewDroplet2DetailSizeText() const;

    TSharedRef<SWidget> GeneratePreviewModeWidget(TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode) const;
    void HandlePreviewModeChanged(TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode, ESelectInfo::Type SelectInfo);
    FText GetPreviewModeText() const;
    FText GetPreviewModeText(SWetnessProfileViewport::EPreviewMode InMode) const;

    TSharedRef<SWidget> GeneratePreviewBehaviorWidget(TSharedPtr<SWetnessProfileViewport::EPreviewBehavior> InBehavior) const;
    void HandlePreviewBehaviorChanged(TSharedPtr<SWetnessProfileViewport::EPreviewBehavior> InBehavior, ESelectInfo::Type SelectInfo);
    FText GetPreviewBehaviorText() const;
    FText GetPreviewBehaviorText(SWetnessProfileViewport::EPreviewBehavior InBehavior) const;
    EVisibility GetManualControlsVisibility() const;
    EVisibility GetSimulationControlsVisibility() const;
    FReply HandlePlayPauseClicked();
    FReply HandleRestartSimulationClicked();
    const FSlateBrush* GetPlayPauseBrush() const;
    FText GetPlayPauseToolTip() const;
    FText GetSimulationTimeText() const;
    ECheckBoxState GetAbsorbedLayerCheckState() const;
    void HandleAbsorbedLayerCheckStateChanged(ECheckBoxState NewState);
    ECheckBoxState GetSurfaceLayerCheckState() const;
    void HandleSurfaceLayerCheckStateChanged(ECheckBoxState NewState);
    ECheckBoxState GetDroplet1CheckState() const;
    void HandleDroplet1CheckStateChanged(ECheckBoxState NewState);
    ECheckBoxState GetDroplet2CheckState() const;
    void HandleDroplet2CheckStateChanged(ECheckBoxState NewState);
    EVisibility GetDroplet1ControlsVisibility() const;
    EVisibility GetDroplet2ControlsVisibility() const;
    TSharedRef<SWidget> GeneratePreviewSpeedWidget(TSharedPtr<float> InSpeed) const;
    void HandlePreviewSpeedChanged(TSharedPtr<float> InSpeed, ESelectInfo::Type SelectInfo);
    FText GetPreviewSpeedText() const;
    ECheckBoxState GetLoopCheckState() const;
    void HandleLoopCheckStateChanged(ECheckBoxState NewState);

    void LoadPersistedPreviewSettings();
    void PersistPreviewDetailSizes();
    void ApplyPreviewSettingsToViewport();

private:
    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<IDetailsView> AbsorbedDetailsView;
    TSharedPtr<IDetailsView> SurfaceDetailsView;
    TSharedPtr<SWetnessProfileViewport> PreviewViewport;
    TArray<TSharedPtr<SWetnessProfileViewport::EPreviewMode>> PreviewModeItems;
    TSharedPtr<SWetnessProfileViewport::EPreviewMode> SelectedPreviewModeItem;
    TArray<TSharedPtr<SWetnessProfileViewport::EPreviewBehavior>> PreviewBehaviorItems;
    TSharedPtr<SWetnessProfileViewport::EPreviewBehavior> SelectedPreviewBehaviorItem;
    TArray<TSharedPtr<float>> PreviewSpeedItems;
    TSharedPtr<float> SelectedPreviewSpeedItem;

    bool bPreviewAbsorbedLayerEnabled = true;
    bool bPreviewSurfaceLayerEnabled = true;
    bool bPreviewDroplet1Enabled = true;
    bool bPreviewDroplet2Enabled = false;
    float PreviewDroplet1DetailSize = 1.0f;
    float PreviewDroplet2DetailSize = 1.0f;
};
