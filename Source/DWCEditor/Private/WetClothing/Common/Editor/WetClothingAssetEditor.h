#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WetClothing/Common/Editor/WetClothingEditorMode.h"

struct FPropertyChangedEvent;
struct FSlateColor;
struct FSlateBrush;
class FExtender;
class FToolBarBuilder;
class IDetailsView;
class SDockTab;
class SWidget;
class SWetClothingAssetEditorPanel;
class UWetClothingAsset;
enum class ECheckBoxState : uint8;

class FWetClothingAssetEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWetClothingAssetEditor() override;

    void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingAsset* InWetClothingAsset);

    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;

    virtual FName        GetToolkitFName() const override;
    virtual FText        GetBaseToolkitName() const override;
    virtual FString      GetWorldCentricTabPrefix() const override;
    virtual FLinearColor GetWorldCentricTabColorScale() const override;
    virtual bool         OnRequestClose(EAssetEditorCloseReason InCloseReason) override;

  private:
    virtual void         PostRegenerateMenusAndToolbars() override;
    void                 HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
    void                 HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);
    void                 FillAssetToolbar(FToolBarBuilder& ToolbarBuilder);
    TSharedRef<SWidget>  BuildBakeMapsMenu();
    FReply               HandleBakeAllMapsClicked();
    void                 HandleGenerateSurfaceWaterUVClicked();
    FReply               HandleBakeWetnessProfileMapsClicked();
    FReply               HandleBakeTransparencyRevealMapsClicked();
    FReply               HandleBakeWrinkleNormalMapClicked();
    FReply               HandleBakeWrinkleMaskClicked();
    TSharedRef<SWidget>  BuildModeToolbarWidget();
    TSharedRef<SWidget>  BuildModeToggleButton(EWetClothingEditorMode Mode, FName IconName, const FText& ToolTipText);
    void                 SetEditorMode(EWetClothingEditorMode NewMode);
    ECheckBoxState       IsModeChecked(EWetClothingEditorMode Mode) const;
    void                 HandleModeCheckStateChanged(ECheckBoxState NewState, EWetClothingEditorMode Mode);
    FSlateColor          GetModeIconColor(EWetClothingEditorMode Mode) const;

  private:
    static const FName EditorAppDisplayName;
    static const FName MainTabId;

    TWeakObjectPtr<UWetClothingAsset>        WetClothingAsset;
    TSharedPtr<IDetailsView>                 DetailsView;
    TSharedPtr<SWetClothingAssetEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem>               WorkspaceMenuCategory;
    FDelegateHandle                          ObjectPropertyChangedHandle;
    TSharedPtr<FExtender>                    ToolbarExtender;
    EWetClothingEditorMode                   CurrentMode = EWetClothingEditorMode::PartEdit;
};
