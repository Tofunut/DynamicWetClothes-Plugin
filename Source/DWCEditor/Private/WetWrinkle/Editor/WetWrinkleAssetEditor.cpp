#include "WetWrinkleAssetEditor.h"

#include "DataAssets/WetWrinkleAsset.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SWetWrinkleAssetEditorPanel.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAssetEditor"

const FName FWetWrinkleAssetEditor::EditorAppName(TEXT("WetWrinkleAssetEditorApp"));
const FName FWetWrinkleAssetEditor::MainTabId(TEXT("WetWrinkleAssetEditor_Main"));

FWetWrinkleAssetEditor::~FWetWrinkleAssetEditor()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FWetWrinkleAssetEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetWrinkleAsset* InWetWrinkleAsset)
{
    check(InWetWrinkleAsset != nullptr);

    WetWrinkleAsset = InWetWrinkleAsset;

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(InWetWrinkleAsset);
    DetailsView->OnFinishedChangingProperties().AddSP(this, &FWetWrinkleAssetEditor::HandleFinishedChangingProperties);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetWrinkleAssetEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetWrinkleAssetEditor_Layout_v1")
                                                        ->AddArea(
                                                            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(MainTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppName, Layout, true, false, InWetWrinkleAsset);
}

void FWetWrinkleAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wet Wrinkle Asset Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWetWrinkleAssetEditor::SpawnMainTab))
        .SetDisplayName(LOCTEXT("MainTab", "Wet Wrinkle Asset"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetWrinkleAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWetWrinkleAssetEditor::GetToolkitFName() const
{
    return EditorAppName;
}

FText FWetWrinkleAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("AppLabel", "Wet Wrinkle Asset Editor");
}

FString FWetWrinkleAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("Wet Wrinkle ");
}

FLinearColor FWetWrinkleAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.05f, 0.18f, 0.32f, 0.5f);
}

void FWetWrinkleAssetEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromAsset();
    }
}

void FWetWrinkleAssetEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (ObjectBeingModified == WetWrinkleAsset.Get() && EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromAsset();
    }
}

TSharedRef<SDockTab> FWetWrinkleAssetEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == MainTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("MainTabLabel", "Wet Wrinkle Asset Editor"))
            [SAssignNew(EditorPanel, SWetWrinkleAssetEditorPanel)
                 .DetailsView(DetailsView)
                 .WetWrinkleAsset(WetWrinkleAsset.Get())];
}

#undef LOCTEXT_NAMESPACE
