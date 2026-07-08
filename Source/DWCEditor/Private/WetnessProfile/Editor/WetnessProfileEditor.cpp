#include "WetnessProfileEditor.h"

#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SWetnessProfileEditorPanel.h"
#include "UObject/UObjectGlobals.h"
#include "DataAssets/WetnessProfile.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditor"

const FName FWetnessProfileEditor::EditorAppDisplayName(TEXT("WetnessProfileEditorApp"));
const FName FWetnessProfileEditor::MainTabId(TEXT("WetnessProfileEditor_Main"));

FWetnessProfileEditor::~FWetnessProfileEditor()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FWetnessProfileEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetnessProfile* InProfile)
{
    check(InProfile != nullptr);

    WetnessProfile = InProfile;

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetObject(InProfile);
    DetailsView->OnFinishedChangingProperties().AddSP(this, &FWetnessProfileEditor::HandleFinishedChangingProperties);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetnessProfileEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetnessProfileEditor_Layout_v1")
                                                        ->AddArea(
                                                            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(MainTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppDisplayName, Layout, true, false, InProfile);
}

void FWetnessProfileEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wetness Profile Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWetnessProfileEditor::SpawnMainTab))
        .SetDisplayName(LOCTEXT("MainTab", "Wetness Profile"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetnessProfileEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWetnessProfileEditor::GetToolkitFName() const
{
    return EditorAppDisplayName;
}

FText FWetnessProfileEditor::GetBaseToolkitName() const
{
    return LOCTEXT("AppLabel", "Wetness Profile Editor");
}

FString FWetnessProfileEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("Wetness Profile ");
}

FLinearColor FWetnessProfileEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.0f, 0.2f, 0.2f, 0.5f);
}

void FWetnessProfileEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromProfile();
    }
}

void FWetnessProfileEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (ObjectBeingModified == WetnessProfile.Get() && EditorPanel.IsValid())
    {
        EditorPanel->RefreshFromProfile();
    }
}

TSharedRef<SDockTab> FWetnessProfileEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == MainTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("MainTabLabel", "Wetness Profile Editor"))
            [SAssignNew(EditorPanel, SWetnessProfileEditorPanel)
                 .DetailsView(DetailsView)
                 .WetnessProfile(WetnessProfile.Get())];
}

#undef LOCTEXT_NAMESPACE
