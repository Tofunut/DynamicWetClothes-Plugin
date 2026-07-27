#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class IDetailsView;
class SDockTab;
class SWetnessProfileEditorPanel;
class SWidget;
class SWidgetSwitcher;
class UWetnessProfile;
struct FPropertyChangedEvent;

class FWetnessProfileEditor : public FAssetEditorToolkit
{
public:
    virtual ~FWetnessProfileEditor() override;

    void Initialize(
        EToolkitMode::Type Mode,
        const TSharedPtr<IToolkitHost>& InitToolkitHost,
        UWetnessProfile* InProfile);

    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

    virtual FName GetToolkitFName() const override;
    virtual FText GetBaseToolkitName() const override;
    virtual FString GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;

private:
    enum class EWaterChannel : uint8
    {
        AbsorbedWater,
        SurfaceWater
    };

    TSharedPtr<IDetailsView> CreateChannelDetailsView(bool bAbsorbedWater) const;
    void HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);

    TSharedRef<SDockTab> SpawnSettingsTab(const FSpawnTabArgs& Args);
    TSharedRef<SDockTab> SpawnPreviewTab(const FSpawnTabArgs& Args);

    TSharedRef<SWidget> BuildChannelSelector();
    TSharedRef<SWidget> BuildChannelCard(
        EWaterChannel Channel,
        const FText& Title,
        const FText& Description);
    FReply HandleSelectChannel(EWaterChannel Channel);
    int32 GetActiveChannelIndex() const;
    ECheckBoxState GetChannelEnabledState(EWaterChannel Channel) const;
    void HandleChannelEnabledChanged(ECheckBoxState NewState, EWaterChannel Channel);
    FSlateColor GetChannelCardTint(EWaterChannel Channel) const;
    FSlateColor GetChannelCardOutlineTint(EWaterChannel Channel) const;
    FSlateColor GetChannelTitleTint(EWaterChannel Channel) const;
    EVisibility GetChannelSelectedVisibility(EWaterChannel Channel) const;
    void RefreshEditorViews();

private:
    static const FName EditorAppDisplayName;
    static const FName SettingsTabId;
    static const FName PreviewTabId;

    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<IDetailsView> AbsorbedDetailsView;
    TSharedPtr<IDetailsView> SurfaceDetailsView;
    TSharedPtr<SWidgetSwitcher> ChannelSwitcher;
    TSharedPtr<SWetnessProfileEditorPanel> PreviewPanel;
    TSharedPtr<FWorkspaceItem> WorkspaceMenuCategory;
    FDelegateHandle ObjectPropertyChangedHandle;
    EWaterChannel ActiveChannel = EWaterChannel::SurfaceWater;
};
