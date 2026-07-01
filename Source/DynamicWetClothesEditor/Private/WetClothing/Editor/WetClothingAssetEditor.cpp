#include "WetClothingAssetEditor.h"

#include "WetClothingAsset.h"
#include "SWetClothingAssetEditorPanel.h"
#include "DetailsViewArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Styling/AppStyle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditor"

namespace
{
    enum class EWetClothingPendingCloseChoice : uint8
    {
        BuildAndSave,
        CloseAnyway,
        Cancel
    };

    EWetClothingPendingCloseChoice ShowPendingWetSetupCloseDialog(const FString& PendingSummary)
    {
        EWetClothingPendingCloseChoice Choice = EWetClothingPendingCloseChoice::Cancel;

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
                .Title(LOCTEXT("PendingWetSetupCloseTitle", "Pending Wet Setup"))
                .SizingRule(ESizingRule::Autosized)
                .SupportsMaximize(false)
                .SupportsMinimize(false);

        DialogWindow->SetContent(
            SNew(SBorder)
                .Padding(16.0f)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                    [SNew(SVerticalBox)

                     + SVerticalBox::Slot()
                           .AutoHeight()
                           .Padding(0.0f, 0.0f, 0.0f, 14.0f)
                               [SNew(SBox)
                                    .WidthOverride(440.0f)
                                        [SNew(STextBlock)
                                             .AutoWrapText(true)
                                             .Text(FText::FromString(PendingSummary))]]

                     + SVerticalBox::Slot()
                           .AutoHeight()
                           .HAlign(HAlign_Right)
                               [SNew(SHorizontalBox)

                                + SHorizontalBox::Slot()
                                      .AutoWidth()
                                      .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                          [SNew(SButton)
                                               .Text(LOCTEXT("PendingWetSetupBuildAndSave", "Build & Save"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                               {
                                                   Choice = EWetClothingPendingCloseChoice::BuildAndSave;
                                                   DialogWindow->RequestDestroyWindow();
                                                   return FReply::Handled();
                                               })]

                                + SHorizontalBox::Slot()
                                      .AutoWidth()
                                      .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                          [SNew(SButton)
                                               .Text(LOCTEXT("PendingWetSetupCloseAnyway", "Close Anyway"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                               {
                                                   Choice = EWetClothingPendingCloseChoice::CloseAnyway;
                                                   DialogWindow->RequestDestroyWindow();
                                                   return FReply::Handled();
                                               })]

                                + SHorizontalBox::Slot()
                                      .AutoWidth()
                                          [SNew(SButton)
                                               .Text(LOCTEXT("PendingWetSetupCancel", "Cancel"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                               {
                                                   Choice = EWetClothingPendingCloseChoice::Cancel;
                                                   DialogWindow->RequestDestroyWindow();
                                                   return FReply::Handled();
                                               })]]]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
        return Choice;
    }
} // namespace

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
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda(
        [](const FPropertyAndParent& PropertyAndParent)
        {
            return PropertyAndParent.Property.GetFName() == GET_MEMBER_NAME_CHECKED(UWetClothingAsset, TargetMesh);
        }));
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

bool FWetClothingAssetEditor::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
    if (EditorPanel.IsValid())
    {
        FString PendingSummary;
        if (EditorPanel->HasPendingWetSetupTasks(&PendingSummary))
        {
            const EWetClothingPendingCloseChoice Choice = ShowPendingWetSetupCloseDialog(PendingSummary);
            if (Choice == EWetClothingPendingCloseChoice::Cancel)
            {
                return false;
            }

            if (Choice == EWetClothingPendingCloseChoice::BuildAndSave)
            {
                FString BuildSummary;
                if (!EditorPanel->BuildWetSetup(BuildSummary))
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(BuildSummary), LOCTEXT("BuildWetSetupFailedTitle", "Build Wet Setup"));
                    return false;
                }

                if (WetClothingAsset.IsValid())
                {
                    if (!EditorPanel->SaveWetSetupAssets())
                    {
                        return false;
                    }
                }
            }
        }
    }

    return FAssetEditorToolkit::OnRequestClose(InCloseReason);
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
