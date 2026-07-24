#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"

struct FPropertyChangedEvent;
struct FSlateColor;
struct FSlateBrush;
class FExtender;
class FToolBarBuilder;
class IDetailsView;
class SDockTab;
class SWidget;
class SWindow;
class SWCAEditorPanel;
class UWetClothingAsset;

enum class ECheckBoxState : uint8;

class FWCAEditor : public FAssetEditorToolkit
{
  public:
    virtual ~FWCAEditor() override;

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
    FReply               HandleBakeRenderProfileDataClicked();
    FReply               HandleBakeGPUWetnessMapDataClicked();
    FReply               HandleBakeWrinkleNormalMapClicked();
    FReply               HandleBakeTransparencyMapsClicked();
    FReply               HandleGenerateMaterialsClicked();
    FReply               GenerateWetMaterials();
    bool                 CanBakeAnyMaps() const;
    bool                 CanBakeCurrentModeMaps() const;
    bool                 CanBakeGPUMaps() const;
    bool                 CanBakeRenderProfileData() const;
    bool                 CanBakeWrinkleMaps() const;
    bool                 CanBakeTransparencyMaps() const;
    bool                 ResolveIssuesAndSave(FString& OutFailure, FString* OutSuccessSummary = nullptr);
    void                 RefreshAssetStateAndEditor(
        bool bRunDeepValidation = false,
        bool bRebuildActiveModePreview = true);
    TSharedRef<SWidget>  BuildModeToolbarWidget();
    TSharedRef<SWidget>  BuildModeToggleButton(EWCAEditorMode Mode, FName IconName, const FText& ToolTipText);
    void                 SetEditorMode(EWCAEditorMode NewMode);
    ECheckBoxState       IsModeChecked(EWCAEditorMode Mode) const;
    void                 HandleModeCheckStateChanged(ECheckBoxState NewState, EWCAEditorMode Mode);
    FSlateColor          GetModeIconColor(EWCAEditorMode Mode) const;

  private:
    enum class ECloseConfirmationState : uint8
    {
        Idle,
        PromptOpen,
        Confirmed
    };

    static const FName EditorAppDisplayName;
    static const FName MainTabId;

    TWeakObjectPtr<UWetClothingAsset>        WetClothingAsset;
    TSharedPtr<IDetailsView>                 DetailsView;
    TSharedPtr<SWCAEditorPanel> EditorPanel;
    TSharedPtr<FWorkspaceItem>               WorkspaceMenuCategory;
    FDelegateHandle                          ObjectPropertyChangedHandle;
    FDelegateHandle                          AssetSavedHandle;
    TSharedPtr<FExtender>                    ToolbarExtender;
    EWCAEditorMode                           CurrentMode = EWCAEditorMode::PartEdit;
    ECloseConfirmationState                  CloseConfirmationState = ECloseConfirmationState::Idle;
};
