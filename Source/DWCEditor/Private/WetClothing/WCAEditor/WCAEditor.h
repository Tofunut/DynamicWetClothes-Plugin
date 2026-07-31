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
    void                 HandleEditorPanelStatusChanged();
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);
    void                 FillAssetToolbar(FToolBarBuilder& ToolbarBuilder);
    void                 HandleAssetSetupClicked();
    void                 HandleInitializeGeneratedDataUVClicked();
    void                 InitializeGeneratedDataUV(bool bAllowOverwriteExistingDataUVChannel, bool bUsePreferredDataUVChannel = false);
    void                 HandleValidationClicked();
    void                 RefreshValidationDialogContent(const TSharedRef<SWindow>& DialogWindow);
    FReply               HandleValidationRefreshClicked(TWeakPtr<SWindow> DialogWindow);
    FReply               HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow);
    TSharedRef<SWidget>  BuildRuntimeBuildMenu();
    FReply               HandleBuildAllRequiredClicked();
    FReply               HandleBuildCPURuntimeDataClicked();
    FReply               HandleBuildGPURuntimeDataClicked();
    FReply               HandleBakeRenderProfileDataClicked();
    FReply               HandleBakeWrinkleNormalMapClicked();
    FReply               HandleBakeTransparencyMapsClicked();
    FReply               HandleGenerateMaterialsClicked();
    FReply               GenerateWetMaterials();
    bool                 HasMaterialGenerationPrerequisites(FText* OutFailureReason = nullptr) const;
    bool                 IsMaterialGenerationRequired() const;
    bool                 CanGenerateMaterials() const;
    FText                GetGenerateMaterialsTooltip() const;
    bool                 CanBuildAllRequired() const;
    bool                 CanBuildCPURuntimeData() const;
    bool                 CanBuildGPURuntimeData() const;
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
