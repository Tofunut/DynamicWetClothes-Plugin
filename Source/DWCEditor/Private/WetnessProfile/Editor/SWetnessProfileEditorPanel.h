#pragma once

#include "CoreMinimal.h"
#include "Types/SlateEnums.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class USkeletalMesh;
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
    TSharedRef<SWidget> BuildPreviewDetailSizeSection();

    FString GetCurrentPreviewMeshObjectPath() const;
    void HandleCurrentPreviewMeshChanged(const FAssetData& AssetData);
    FReply HandleUseReferenceMeshClicked();
    FReply HandleSaveCurrentMeshAsReferenceClicked();
    FReply HandleUseSphereMeshClicked();

    float GetPreviewAbsorbedWaterPercent() const;
    void HandlePreviewAbsorbedWaterPercentChanged(float InPercent);
    FText GetPreviewAbsorbedWaterPercentText() const;

    float GetPreviewDropletDetailSize() const;
    void HandlePreviewDropletDetailSizeChanged(float InValue);
    FText GetPreviewDropletDetailSizeText() const;

    TSharedRef<SWidget> GeneratePreviewModeWidget(TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode) const;
    void HandlePreviewModeChanged(TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode, ESelectInfo::Type SelectInfo);
    FText GetPreviewModeText() const;
    FText GetPreviewModeText(SWetnessProfileViewport::EPreviewMode InMode) const;

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

    float PreviewDropletDetailSize = 1.0f;
};
