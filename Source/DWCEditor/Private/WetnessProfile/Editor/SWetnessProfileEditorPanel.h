#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class SWetnessProfileViewport;
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

    float GetPreviewSurfaceWaterPercent() const;
    void HandlePreviewSurfaceWaterPercentChanged(float InPercent);
    FText GetPreviewSurfaceWaterPercentText() const;

    float GetPreviewDropletDetailSize() const;
    void HandlePreviewDropletDetailSizeChanged(float InValue);
    FText GetPreviewDropletDetailSizeText() const;

    void LoadPersistedPreviewSettings();
    void PersistPreviewDetailSizes();
    void ApplyPreviewSettingsToViewport();

private:
    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<IDetailsView> AbsorbedDetailsView;
    TSharedPtr<IDetailsView> SurfaceDetailsView;
    TSharedPtr<SWetnessProfileViewport> PreviewViewport;

    float PreviewDropletDetailSize = 1.0f;
};
