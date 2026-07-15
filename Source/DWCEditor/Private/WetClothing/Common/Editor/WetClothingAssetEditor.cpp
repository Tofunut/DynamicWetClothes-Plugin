#include "WetClothingAssetEditor.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DWCEditorStyle.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "SWetClothingAssetEditorPanel.h"
#include "DetailsViewArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Styling/ToolBarStyle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/SurfaceWater/WetClothingSurfaceWaterFlowMapBaker.h"
#include "WetClothing/SurfaceWater/WetClothingSurfaceWaterUVGenerator.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditor"

namespace
{
    const FCheckBoxStyle& GetWetClothingModeToggleStyle()
    {
        static const FSlateRoundedBoxBrush UncheckedBrush(FStyleColors::Header, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedHoveredBrush(FStyleColors::Hover, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedPressedBrush(FStyleColors::Recessed, 4.0f);
        static const FSlateRoundedBoxBrush CheckedBrush(FStyleColors::AccentBlue, 4.0f);
        static const FSlateRoundedBoxBrush CheckedHoveredBrush(FStyleColors::PrimaryHover, 4.0f);

        static const FCheckBoxStyle Style =
            FCheckBoxStyle(FAppStyle::Get().GetWidgetStyle<FToolBarStyle>(TEXT("AssetEditorToolbar")).ToggleButton)
                .SetUncheckedImage(UncheckedBrush)
                .SetUncheckedHoveredImage(UncheckedHoveredBrush)
                .SetUncheckedPressedImage(UncheckedPressedBrush)
                .SetCheckedImage(CheckedBrush)
                .SetCheckedHoveredImage(CheckedHoveredBrush)
                .SetCheckedPressedImage(CheckedBrush)
                .SetPadding(FMargin(0.0f));

        return Style;
    }

    enum class EWetClothingPendingCloseChoice : uint8
    {
        Save,
        CloseAnyway,
        Cancel
    };

    EWetClothingPendingCloseChoice ShowPendingVisualBakeCloseDialog(const FString& PendingSummary)
    {
        EWetClothingPendingCloseChoice Choice = EWetClothingPendingCloseChoice::Cancel;

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
                .Title(LOCTEXT("PendingVisualBakeCloseTitle", "Pending Visual Bake"))
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
                                               .Text(LOCTEXT("PendingVisualBakeSave", "Bake & Save"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                                                 {
                                                    Choice = EWetClothingPendingCloseChoice::Save;
                                                    DialogWindow->RequestDestroyWindow();
                                                    return FReply::Handled(); })]

                                + SHorizontalBox::Slot()
                                      .AutoWidth()
                                      .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                          [SNew(SButton)
                                               .Text(LOCTEXT("PendingVisualBakeCloseAnyway", "Close Anyway"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                                                 {
                                                   Choice = EWetClothingPendingCloseChoice::CloseAnyway;
                                                   DialogWindow->RequestDestroyWindow();
                                                   return FReply::Handled(); })]

                                + SHorizontalBox::Slot()
                                      .AutoWidth()
                                          [SNew(SButton)
                                               .Text(LOCTEXT("PendingVisualBakeCancel", "Cancel"))
                                               .OnClicked_Lambda([&Choice, DialogWindow]()
                                                                 {
                                                   Choice = EWetClothingPendingCloseChoice::Cancel;
                                                   DialogWindow->RequestDestroyWindow();
                                                   return FReply::Handled(); })]]]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
        return Choice;
    }
} // namespace

const FName FWetClothingAssetEditor::EditorAppDisplayName(TEXT("WetClothingAssetEditorApp"));
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
            const FName PropertyName = PropertyAndParent.Property.GetFName();
            if (PropertyName == GET_MEMBER_NAME_CHECKED(UWetClothingAsset, TargetMesh) ||
                PropertyName == GET_MEMBER_NAME_CHECKED(UWetClothingAsset, SurfaceWaterSettings))
            {
                return true;
            }

            const FString Category = PropertyAndParent.Property.GetMetaData(TEXT("Category"));
            return Category.StartsWith(TEXT("Surface Water"));
        }));
    DetailsView->SetObject(InWetClothingAsset);
    DetailsView->OnFinishedChangingProperties().AddSP(this, &FWetClothingAssetEditor::HandleFinishedChangingProperties);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetClothingAssetEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetClothingAssetEditor_Layout_v4")
                                                        ->AddArea(
                                                            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(MainTabId, ETabState::OpenedTab)));

    const bool bCreateDefaultStandaloneMenu = true;
    const bool bCreateDefaultToolbar = true;
    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppDisplayName, Layout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InWetClothingAsset);

    ToolbarExtender = MakeShared<FExtender>();
    ToolbarExtender->AddToolBarExtension(
        TEXT("Asset"),
        EExtensionHook::After,
        GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateSP(this, &FWetClothingAssetEditor::FillAssetToolbar));
    AddToolbarExtender(ToolbarExtender);

    RegenerateMenusAndToolbars();
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
    return EditorAppDisplayName;
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
        if (EditorPanel->HasPendingVisualBakeTasks(&PendingSummary))
        {
            const EWetClothingPendingCloseChoice Choice = ShowPendingVisualBakeCloseDialog(PendingSummary);
            if (Choice == EWetClothingPendingCloseChoice::Cancel)
            {
                return false;
            }

            if (Choice == EWetClothingPendingCloseChoice::Save)
            {
                FString Summary;
                bool bHadWarnings = false;
                if (!EditorPanel->BakePendingVisualAssets(Summary, &bHadWarnings))
                {
                    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
                    return false;
                }

                EditorPanel->SaveBakedVisualAssets();
                DWCEditorUtils::SaveAsset(WetClothingAsset.Get());
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

void FWetClothingAssetEditor::PostRegenerateMenusAndToolbars()
{
    AddToolbarWidget(BuildModeToolbarWidget());
    GenerateToolbar();
}

void FWetClothingAssetEditor::FillAssetToolbar(FToolBarBuilder& ToolbarBuilder)
{
    ToolbarBuilder.AddSeparator();
    ToolbarBuilder.AddToolBarButton(
        FUIAction(FExecuteAction::CreateSP(this, &FWetClothingAssetEditor::HandleGenerateSurfaceWaterUVClicked)),
        NAME_None,
        LOCTEXT("GenerateSurfaceWaterUVLabel", "Generate Water UV"),
        LOCTEXT("GenerateSurfaceWaterUVTooltip", "Generate independently packed Surface Water UVs for every Wettable Material Slot in the configured UV Channel Index."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.Bake")));
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FWetClothingAssetEditor::BuildBakeMapsMenu),
        LOCTEXT("BakeMapsToolbarLabel", "Bake Maps"),
        LOCTEXT("BakeMapsToolbarTooltip", "Bake generated maps for this Wet Clothing Asset."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.Bake")),
        false);
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildBakeMapsMenu()
{
    FWetClothingBakeMapsMenuArgs Args;
    Args.CurrentMode = CurrentMode;
    Args.OnBakeAllWetnessProfileMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeAllWetnessProfileMapsClicked(); });
    Args.OnBakeSelectedWetnessProfileMap = FSimpleDelegate::CreateLambda([this]() { HandleBakeSelectedWetnessProfileMapClicked(); });
    Args.OnBakeTransparencyRevealMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeTransparencyRevealMapsClicked(); });
    Args.OnBakeAllWrinkleNormalMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeAllWrinkleNormalMapsClicked(); });
    Args.OnBakeSelectedWrinkleNormalMap = FSimpleDelegate::CreateLambda([this]() { HandleBakeSelectedWrinkleNormalMapClicked(); });
    return FWetClothingEditorCommonWidgets::BuildBakeMapsMenu(Args);
}

FReply FWetClothingAssetEditor::HandleBakeAllWetnessProfileMapsClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FString Summary;
    bool    bHadWarnings = false;
    if (!EditorPanel->BakeWetVisualAssets(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    EditorPanel->SaveBakedVisualAssets();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

void FWetClothingAssetEditor::HandleGenerateSurfaceWaterUVClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!Asset) return;

    const FSurfaceWaterSimulationSettings& Settings = Asset->SurfaceWaterSettings;
    const FWetClothingSurfaceWaterUVGenerationResult Result = FWetClothingSurfaceWaterUVGenerator::Generate(
        Asset, // Editing WCA
        0, // Use LOD0 Mesh
        0, // Use UV0 Island Structure
        Settings.UVChannelIndexToConstruct, // Channel to Struct
        Settings.RenderTargetResolution, // Surface Water RT Resolution
        Settings.BakedFlowMap.PaddingPixels, // Padding Between UV Island
        Settings.bAllowOverwriteExistingSurfaceWaterUVChannel,
        Settings.TargetSurfaceWaterTexelsPerCentimeter);

    FMessageDialog::Open(
        Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Error,
        EAppMsgType::Ok,
        FText::FromString(Result.Message));
    if (Result.bSucceeded && EditorPanel.IsValid()) EditorPanel->RefreshFromAsset();
}

FReply FWetClothingAssetEditor::HandleBakeSelectedWetnessProfileMapClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FString Summary;
    bool bHadWarnings = false;
    if (!EditorPanel->BakeSelectedWetnessProfileMap(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    EditorPanel->SaveBakedVisualAssets();
    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeTransparencyRevealMapsClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FString Summary;
    bool bHadWarnings = false;
    if (!EditorPanel->BakeTransparencyRevealAssets(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    EditorPanel->SaveTransparencySetupAssets();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeAllWrinkleNormalMapsClicked()
{
    return EditorPanel.IsValid() ? EditorPanel->ExecuteBakeAllWrinkleNormalMaps() : FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeSelectedWrinkleNormalMapClicked()
{
    return EditorPanel.IsValid() ? EditorPanel->ExecuteBakeWrinkleNormalMap() : FReply::Handled();
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildModeToolbarWidget()
{
    return SNew(SBox)
        .Padding(FMargin(12.0f, 0.0f))
            [SNew(SHorizontalBox)
             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::PartEdit,
                           TEXT("DWCEditor.Mode.Part"),
                           LOCTEXT("PartEditModeTooltip", "Part Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::WrinkleEdit,
                           TEXT("DWCEditor.Mode.Wrinkle"),
                           LOCTEXT("WrinkleEditModeTooltip", "Wrinkle Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::TransparencyBake,
                           TEXT("DWCEditor.Mode.Transparency"),
                           LOCTEXT("TransparencyBakeModeTooltip", "Transparency Bake Mode"))]];
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildModeToggleButton(EWetClothingEditorMode Mode, FName IconName, const FText& ToolTipText)
{
    return SNew(SCheckBox)
        .Style(&GetWetClothingModeToggleStyle())
        .Type(ESlateCheckBoxType::ToggleButton)
        .ToolTipText(ToolTipText)
        .IsChecked(this, &FWetClothingAssetEditor::IsModeChecked, Mode)
        .OnCheckStateChanged(this, &FWetClothingAssetEditor::HandleModeCheckStateChanged, Mode)
            [SNew(SBox)
                 .WidthOverride(76.0f)
                 .HeightOverride(32.0f)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(SImage)
                          .DesiredSizeOverride(FVector2D(24.0f, 24.0f))
                          .Image(FDWCEditorStyle::GetBrush(IconName))
                          .ColorAndOpacity(this, &FWetClothingAssetEditor::GetModeIconColor, Mode)]];
}

void FWetClothingAssetEditor::SetEditorMode(EWetClothingEditorMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    CurrentMode = NewMode;

    if (EditorPanel.IsValid())
    {
        EditorPanel->SetEditorMode(NewMode);
    }
}

ECheckBoxState FWetClothingAssetEditor::IsModeChecked(EWetClothingEditorMode Mode) const
{
    return CurrentMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FWetClothingAssetEditor::HandleModeCheckStateChanged(ECheckBoxState NewState, EWetClothingEditorMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SetEditorMode(Mode);
    }
}

FSlateColor FWetClothingAssetEditor::GetModeIconColor(EWetClothingEditorMode Mode) const
{
    if (CurrentMode == Mode)
    {
        return FSlateColor(FLinearColor::White);
    }

    switch (Mode)
    {
    case EWetClothingEditorMode::PartEdit:
        return FSlateColor(FLinearColor(1.0f, 0.66f, 0.78f, 1.0f));
    case EWetClothingEditorMode::WrinkleEdit:
        return FSlateColor(FLinearColor(0.62f, 0.95f, 0.62f, 1.0f));
    case EWetClothingEditorMode::TransparencyBake:
        return FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 1.0f));
    default:
        return FSlateColor::UseForeground();
    }
}

#undef LOCTEXT_NAMESPACE
