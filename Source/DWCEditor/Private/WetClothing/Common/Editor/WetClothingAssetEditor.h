#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
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
class SWindow;
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
    virtual void         SaveAsset_Execute() override;

  private:
    virtual void         PostRegenerateMenusAndToolbars() override;
    void                 HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
    void                 HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
    void                 HandleDWCEditorAssetSaveAttemptFinished(UObject* SavedAsset, bool bSaveSucceeded);
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);
    void                 FillAssetToolbar(FToolBarBuilder& ToolbarBuilder);
    void                 HandleAssetSetupClicked();
    void                 HandleGenerateGeneratedDataUVClicked();
    void                 RebuildGeneratedDataUV(bool bAllowOverwriteExistingDataUVChannel);
    void                 HandleValidationClicked();
    FReply               HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow);
    TSharedRef<SWidget>  BuildBakeMapsMenu();
    TSharedRef<SWidget>  BuildGenerateMaterialsMenu();
    FReply               HandleBakeAllMapsClicked();
    FReply               HandleBakeWetnessProfileMapsClicked();
    FReply               HandleBakeGPUWetnessMapDataClicked();
    FReply               HandleBakeTransparencyRevealMapsClicked();
    FReply               HandleBakeWrinkleNormalMapClicked();
    FReply               HandleBakeWrinkleMaskClicked();
    FReply               HandleGenerateMaterialsClicked();
    FReply               GenerateWetMaterials();
    bool                 CanBakeAnyMaps() const;
    bool                 CanBakeGPUMaps() const;
    bool                 CanBakeWetnessProfileMaps() const;
    bool                 CanBakeWrinkleMaps() const;
    bool                 CanBakeTransparencyMaps() const;
    bool                 ResolveIssuesAndSave(FString& OutFailure);
    void                 RefreshAssetStateAndEditor(bool bRunDeepValidation = false);
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
    FDelegateHandle                          AssetSavedHandle;
    TSharedPtr<FExtender>                    ToolbarExtender;
    EWetClothingEditorMode                   CurrentMode = EWetClothingEditorMode::PartEdit;
};
