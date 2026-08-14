//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"

struct FPropertyChangedEvent;
struct FSlateColor;
struct FSlateBrush;
class FExtender;
class FDWCEditorSlateHostVisibilityAdapter;
class FToolBarBuilder;
class IDetailsView;
class SDockTab;
class SWidget;
class SWindow;
class SWCAEditorPanel;
class UWetClothingAsset;
struct FWCAEditorCanonicalStateSnapshot;

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
    void                 HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent);
    void                 HandleDWCEditorAssetSaveAttemptFinished(UObject* SavedAsset, bool bSaveSucceeded);
    void                 HandleEditorPanelStatusChanged();
    void                 ShutdownEditorPanel();
    TSharedRef<SDockTab> SpawnMainTab(const FSpawnTabArgs& Args);
    void                 FillAssetToolbar(FToolBarBuilder& ToolbarBuilder);
    void                 HandleAssetSetupClicked();
    void                 HandleInitializeGeneratedDataUVClicked();
    void                 InitializeGeneratedDataUV(bool bAllowOverwriteExistingDataUVChannel, bool bUsePreferredDataUVChannel = false);
    void                 HandleValidationClicked();
    void                 RefreshValidationDialogContent(const TSharedRef<SWindow>& DialogWindow);
    FReply               HandleValidationRefreshClicked(TWeakPtr<SWindow> DialogWindow);
    FReply               HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow);
    void                 ExecuteValidationResolveExclusive();
    void                 HandleValidationWindowClosed(const TSharedRef<SWindow>& Window);
    TSharedRef<SWidget>  BuildRuntimeBuildMenu();
    FWCAEditorCanonicalStateSnapshot BuildCanonicalStateSnapshot(bool bDeepValidation) const;
    FDWCEditorBuildStatusSnapshot BuildBuildStatusSnapshot(bool bDeepValidation = false) const;
    bool                 CanExecuteBuildAction(EDWCEditorBuildAction Action) const;
    void                 ExecuteBuildAction(EDWCEditorBuildAction Action);
    TSharedRef<SWidget>  BuildPreviewDiagnosticsMenu();
    void                 HandleDumpPreviewDiagnostics();
    void                 HandleResetPreviewDiagnostics();
    FReply               HandleBuildAllRequiredClicked();
    void                 ExecuteBuildAllRequiredExclusive();
    bool                 ResolveIssuesAndSave(
        FString& OutFailure,
        FString* OutSuccessSummary = nullptr,
        const TCHAR* BuildTraceName = TEXT("Resolve Required Outputs"),
        EDWCEditorBuildPlanPolicy PlanPolicy = EDWCEditorBuildPlanPolicy::AllRequired,
        TOptional<EDWCEditorBuildAction> ExplicitAction = {});
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
    TWeakPtr<SDockTab>                       MainDockTab;
    TWeakPtr<SWindow>                        ValidationDialogWindow;
    TSharedPtr<FDWCEditorSlateHostVisibilityAdapter> HostVisibilityAdapter;
    TSharedPtr<FWorkspaceItem>               WorkspaceMenuCategory;
    FDelegateHandle                          ObjectPropertyChangedHandle;
    FDelegateHandle                          AssetSavedHandle;
    TSharedPtr<FExtender>                    ToolbarExtender;
    EWCAEditorMode                           CurrentMode = EWCAEditorMode::PartEdit;
    ECloseConfirmationState                  CloseConfirmationState = ECloseConfirmationState::Idle;
    bool                                     bValidationResolveInProgress = false;
    bool                                     bValidationResolveFailed = false;
    FText                                    ValidationResolveStatus;
};
