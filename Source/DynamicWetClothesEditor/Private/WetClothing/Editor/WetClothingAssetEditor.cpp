#include "WetClothingAssetEditor.h"

#include "WetClothingAsset.h"
#include "SWetClothingAssetEditorPanel.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditor"

const FName FWetClothingAssetEditor::EditorAppName(TEXT("WetClothingAssetEditorApp"));
const FName FWetClothingAssetEditor::MainTabId(TEXT("WetClothingAssetEditor_Main"));

FWetClothingAssetEditor::~FWetClothingAssetEditor()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FWetClothingAssetEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingAsset* InWetClothingAsset)
{
    check(InWetClothingAsset != nullptr);

    WetClothingAsset = InWetClothingAsset;

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(InWetClothingAsset);
    DetailsView->OnFinishedChangingProperties().AddSP(this, &FWetClothingAssetEditor::HandleFinishedChangingProperties);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetClothingAssetEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetClothingAssetEditor_Layout_v3")
                                                        ->AddArea(
                                                            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(MainTabId, ETabState::OpenedTab)));

    const bool bCreateDefaultStandaloneMenu = true;
    const bool bCreateDefaultToolbar = false;
    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppName, Layout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InWetClothingAsset);
}

void FWetClothingAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wet Clothing Asset Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWetClothingAssetEditor::SpawnMainTab))
        .SetDisplayName(LOCTEXT("MainTab", "Wet Clothing Asset"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetClothingAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWetClothingAssetEditor::GetToolkitFName() const
{
    return EditorAppName;
}

FText FWetClothingAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("AppLabel", "Wet Clothing Asset Editor");
}

FString FWetClothingAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("Wet Clothing Asset ");
}

FLinearColor FWetClothingAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
}

void FWetClothingAssetEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromAsset();
    }
}

void FWetClothingAssetEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (ObjectBeingModified == WetClothingAsset.Get() && EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromAsset();
    }
}

TSharedRef<SDockTab> FWetClothingAssetEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == MainTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("MainTabLabel", "Wet Clothing Asset Editor"))
            [SAssignNew(EditorPanel, SWetClothingAssetEditorPanel)
                 .DetailsView(DetailsView)
                 .WetClothingAsset(WetClothingAsset.Get())];
}

#undef LOCTEXT_NAMESPACE
