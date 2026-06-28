#include "WetClothingProfileEditor.h"

#include "WetClothingProfile.h"
#include "SWetClothingProfileEditorPanel.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "WetClothingProfileEditor"

const FName FWetClothingProfileEditor::EditorAppName(TEXT("WetClothingProfileEditorApp"));
const FName FWetClothingProfileEditor::MainTabId(TEXT("WetClothingProfileEditor_Main"));

FWetClothingProfileEditor::~FWetClothingProfileEditor()
{
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
	}
}

void FWetClothingProfileEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingProfile* InWetClothingProfile)
{
	check(InWetClothingProfile != nullptr);

	WetClothingProfile = InWetClothingProfile;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(InWetClothingProfile);
	DetailsView->OnFinishedChangingProperties().AddSP(this, &FWetClothingProfileEditor::HandleFinishedChangingProperties);
	ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetClothingProfileEditor::HandleObjectPropertyChanged);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetClothingProfileEditor_Layout_v3")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
				->AddTab(MainTabId, ETabState::OpenedTab)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppName, Layout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InWetClothingProfile);
}

void FWetClothingProfileEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wet Clothing Asset Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWetClothingProfileEditor::SpawnMainTab))
		.SetDisplayName(LOCTEXT("MainTab", "Wet Clothing Asset"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetClothingProfileEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWetClothingProfileEditor::GetToolkitFName() const
{
	return EditorAppName;
}

FText FWetClothingProfileEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Wet Clothing Asset Editor");
}

FString FWetClothingProfileEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("Wet Clothing Asset ");
}

FLinearColor FWetClothingProfileEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
}

void FWetClothingProfileEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (EditorPanel.IsValid())
	{
		EditorPanel->RefreshFromProfile();
	}
}

void FWetClothingProfileEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (ObjectBeingModified == WetClothingProfile.Get() && EditorPanel.IsValid())
	{
		EditorPanel->RefreshFromProfile();
	}
}

TSharedRef<SDockTab> FWetClothingProfileEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == MainTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("MainTabLabel", "Wet Clothing Asset Editor"))
		[
			SAssignNew(EditorPanel, SWetClothingProfileEditorPanel)
			.DetailsView(DetailsView)
			.WetClothingProfile(WetClothingProfile.Get())
		];
}

#undef LOCTEXT_NAMESPACE
