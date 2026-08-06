#include "WetnessProfileEditor.h"

#include "DataAssets/WetnessProfile.h"
#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "SWetnessProfileEditorPanel.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WetnessProfileDetailsCustomization.h"
#include "WetnessProfileEditorPolicy.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditor"

const FName FWetnessProfileEditor::EditorAppDisplayName(TEXT("WetnessProfileEditorApp"));
const FName FWetnessProfileEditor::SettingsTabId(TEXT("WetnessProfileEditor_Settings"));
const FName FWetnessProfileEditor::PreviewTabId(TEXT("WetnessProfileEditor_Preview"));

FWetnessProfileEditor::~FWetnessProfileEditor()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
}

void FWetnessProfileEditor::Initialize(
    const EToolkitMode::Type Mode,
    const TSharedPtr<IToolkitHost>& InitToolkitHost,
    UWetnessProfile* InProfile)
{
    check(InProfile != nullptr);

    WetnessProfile = InProfile;
#if WITH_EDITORONLY_DATA
    InProfile->CaptureEditorSavedParametersSnapshot();
#endif
    AbsorbedDetailsView = CreateChannelDetailsView(true);
    SurfaceDetailsView = CreateChannelDetailsView(false);

    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
        this,
        &FWetnessProfileEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout =
        FTabManager::NewLayout(TEXT("Standalone_WetnessProfileEditor_Layout_v4"))
            ->AddArea(
                FTabManager::NewPrimaryArea()
                    ->SetOrientation(Orient_Horizontal)
                    ->Split(
                        FTabManager::NewStack()
                            ->SetSizeCoefficient(0.62f)
                            ->SetHideTabWell(true)
                            ->AddTab(SettingsTabId, ETabState::OpenedTab))
                    ->Split(
                        FTabManager::NewStack()
                            ->SetSizeCoefficient(0.38f)
                            ->SetHideTabWell(true)
                            ->AddTab(PreviewTabId, ETabState::OpenedTab)));

    FAssetEditorToolkit::InitAssetEditor(
        Mode,
        InitToolkitHost,
        EditorAppDisplayName,
        Layout,
        true,
        false,
        InProfile);
}

TSharedPtr<IDetailsView> FWetnessProfileEditor::CreateChannelDetailsView(const bool bAbsorbedWater) const
{
    FPropertyEditorModule& PropertyEditorModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = true;
    DetailsViewArgs.ViewIdentifier = bAbsorbedWater
        ? FName(TEXT("WetnessProfileEditor_AbsorbedDetails"))
        : FName(TEXT("WetnessProfileEditor_SurfaceDetails"));

    const TSharedPtr<IDetailsView> NewDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    NewDetailsView->RegisterInstancedCustomPropertyLayout(
        UWetnessProfile::StaticClass(),
        FOnGetDetailCustomizationInstance::CreateLambda(
            [bAbsorbedWater]()
            {
                return FWetnessProfileDetailsCustomization::MakeInstance(
                    bAbsorbedWater
                        ? EWetnessProfileDetailsMode::AbsorbedWater
                        : EWetnessProfileDetailsMode::SurfaceWater);
            }));
    NewDetailsView->SetObject(WetnessProfile.Get());
    return NewDetailsView;
}

void FWetnessProfileEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
        LOCTEXT("WorkspaceMenu", "Wetness Profile Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(
            SettingsTabId,
            FOnSpawnTab::CreateSP(this, &FWetnessProfileEditor::SpawnSettingsTab))
        .SetDisplayName(LOCTEXT("SettingsTab", "Water Settings"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());

    InTabManager->RegisterTabSpawner(
            PreviewTabId,
            FOnSpawnTab::CreateSP(this, &FWetnessProfileEditor::SpawnPreviewTab))
        .SetDisplayName(LOCTEXT("PreviewTab", "Preview"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetnessProfileEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(SettingsTabId);
    InTabManager->UnregisterTabSpawner(PreviewTabId);
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

void FWetnessProfileEditor::SaveAsset_Execute()
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile != nullptr)
    {
        TArray<FString> ClampedValues;
        FWetnessProfileEditorPolicy::SanitizeProfile(Profile, &ClampedValues);
    }

    FAssetEditorToolkit::SaveAsset_Execute();

#if WITH_EDITORONLY_DATA
    if (Profile != nullptr && Profile->GetOutermost() != nullptr && !Profile->GetOutermost()->IsDirty())
    {
        Profile->CaptureEditorSavedParametersSnapshot();
        RefreshEditorViews();
    }
#endif
}

void FWetnessProfileEditor::HandleObjectPropertyChanged(
    UObject* ObjectBeingModified,
    FPropertyChangedEvent& PropertyChangedEvent)
{
    if (ObjectBeingModified == WetnessProfile.Get())
    {
        if (PreviewPanel.IsValid())
        {
            PreviewPanel->RefreshFromProfile();
        }
    }
}

TSharedRef<SDockTab> FWetnessProfileEditor::SpawnSettingsTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == SettingsTabId);

    const FSlateFontInfo EmptyStateFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13);

    return SNew(SDockTab)
        .Label(LOCTEXT("SettingsTabLabel", "Water Settings"))
        [SNew(SBorder)
             .Padding(0.0f)
             .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
             .BorderBackgroundColor(FLinearColor(0.018f, 0.018f, 0.018f, 1.0f))
         [SNew(SVerticalBox)

          + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(8.0f, 8.0f, 8.0f, 7.0f)
                    [BuildChannelSelector()]

          + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(8.0f, 0.0f, 8.0f, 8.0f)
                    [SNew(SBorder)
                         .Padding(0.0f)
                         .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                         .BorderBackgroundColor(FLinearColor(0.028f, 0.028f, 0.028f, 1.0f))
                     [SAssignNew(ChannelSwitcher, SWidgetSwitcher)
                          .WidgetIndex(this, &FWetnessProfileEditor::GetActiveChannelIndex)

                      + SWidgetSwitcher::Slot()
                            [SNew(SBox)
                                 .HAlign(HAlign_Center)
                                 .VAlign(VAlign_Center)
                             [SNew(SVerticalBox)
                              + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .HAlign(HAlign_Center)
                                    .Padding(20.0f, 20.0f, 20.0f, 7.0f)
                                        [SNew(STextBlock)
                                             .Text(FText::FromString(TEXT("◇")))
                                             .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 28))
                                             .ColorAndOpacity(FSlateColor(FLinearColor(0.40f, 0.45f, 0.52f, 0.42f)))]
                              + SVerticalBox::Slot()
                                    .AutoHeight()
                                    .HAlign(HAlign_Center)
                                    .Padding(20.0f, 0.0f, 20.0f, 20.0f)
                                        [SNew(STextBlock)
                                             .Text(LOCTEXT("SelectWaterTypeEmptyState", "Select a water type to begin."))
                                             .Font(EmptyStateFont)
                                             .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.74f, 0.78f, 0.62f)))]]]

                      + SWidgetSwitcher::Slot()
                            [AbsorbedDetailsView.IsValid()
                                 ? StaticCastSharedRef<SWidget>(AbsorbedDetailsView.ToSharedRef())
                                 : StaticCastSharedRef<SWidget>(
                                       SNew(STextBlock)
                                           .Text(LOCTEXT("MissingAbsorbedDetails", "Absorbed Water details are unavailable.")))]

                      + SWidgetSwitcher::Slot()
                            [SurfaceDetailsView.IsValid()
                                 ? StaticCastSharedRef<SWidget>(SurfaceDetailsView.ToSharedRef())
                                 : StaticCastSharedRef<SWidget>(
                                       SNew(STextBlock)
                                           .Text(LOCTEXT("MissingSurfaceDetails", "Surface Water details are unavailable.")))]]]]];
}

TSharedRef<SDockTab> FWetnessProfileEditor::SpawnPreviewTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == PreviewTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("PreviewTabLabel", "Preview"))
        [SAssignNew(PreviewPanel, SWetnessProfileEditorPanel)
             .WetnessProfile(WetnessProfile.Get())
             .AbsorbedDetailsView(AbsorbedDetailsView)
             .SurfaceDetailsView(SurfaceDetailsView)
             .HasWaterChannelSelection(this, &FWetnessProfileEditor::HasWaterChannelSelection)
             .IsSurfaceWaterSelected(this, &FWetnessProfileEditor::IsSurfaceWaterSelected)];
}

TSharedRef<SWidget> FWetnessProfileEditor::BuildChannelSelector()
{
    return SNew(SBorder)
        .Padding(FMargin(10.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(FLinearColor(0.038f, 0.042f, 0.050f, 1.0f))
        [SNew(SHorizontalBox)

         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                   [BuildChannelCard(
                       EWaterChannel::AbsorbedWater,
                       LOCTEXT("AbsorbedWaterChannelTitle", "Absorbed Water"),
                       LOCTEXT("AbsorbedWaterChannelDescription", "Spreading and darkening inside the material"))]

         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                   [BuildChannelCard(
                       EWaterChannel::SurfaceWater,
                       LOCTEXT("SurfaceWaterChannelTitle", "Surface Water"),
                       LOCTEXT("SurfaceWaterChannelDescription", "Visible droplets on the material surface"))]];
}

TSharedRef<SWidget> FWetnessProfileEditor::BuildChannelCard(
    const EWaterChannel Channel,
    const FText& Title,
    const FText& Description)
{
    const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
    const FSlateFontInfo DescriptionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(this, &FWetnessProfileEditor::GetChannelCardOutlineTint, Channel)
        .Padding(2.0f)
        [SNew(SBorder)
             .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
             .BorderBackgroundColor(this, &FWetnessProfileEditor::GetChannelCardTint, Channel)
             .Padding(0.0f)
         [SNew(SBox)
              .HeightOverride(66.0f)
          [SNew(SHorizontalBox)

           + SHorizontalBox::Slot()
                 .FillWidth(1.0f)
                     [SNew(SButton)
                          .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                          .ContentPadding(FMargin(12.0f, 8.0f))
                          .HAlign(HAlign_Left)
                          .OnClicked(this, &FWetnessProfileEditor::HandleSelectChannel, Channel)
                      [SNew(SVerticalBox)
                       + SVerticalBox::Slot()
                             .AutoHeight()
                                 [SNew(STextBlock)
                                      .Text(Title)
                                      .Font(TitleFont)
                                      .ColorAndOpacity(this, &FWetnessProfileEditor::GetChannelTitleTint, Channel)]
                       + SVerticalBox::Slot()
                             .AutoHeight()
                             .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                                 [SNew(STextBlock)
                                      .Text(Description)
                                      .Font(DescriptionFont)
                                      .ColorAndOpacity(FSlateColor(FLinearColor(0.74f, 0.77f, 0.82f, 0.72f)))
                                      .AutoWrapText(true)]]]

           + SHorizontalBox::Slot()
                 .AutoWidth()
                 .VAlign(VAlign_Center)
                 .Padding(2.0f, 0.0f, 4.0f, 0.0f)
                     [SNew(SButton)
                          .Visibility(this, &FWetnessProfileEditor::GetChannelEnabledRevertVisibility, Channel)
                          .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                          .ContentPadding(FMargin(4.0f))
                          .ToolTipText(LOCTEXT("RevertChannelEnabledTooltip", "Revert to the value loaded from the saved asset."))
                          .OnClicked(this, &FWetnessProfileEditor::HandleRevertChannelEnabled, Channel)
                      [SNew(SImage)
                           .Image(FAppStyle::GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))]]

           + SHorizontalBox::Slot()
                 .AutoWidth()
                 .VAlign(VAlign_Center)
                 .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                     [SNew(SCheckBox)
                          .IsChecked(this, &FWetnessProfileEditor::GetChannelEnabledState, Channel)
                          .ToolTipText(LOCTEXT("ToggleWaterTypeEnabledTooltip", "Enable or disable this water type without changing the current editor selection."))
                          .OnCheckStateChanged(this, &FWetnessProfileEditor::HandleChannelEnabledStateChanged, Channel)
                      [SNew(STextBlock)
                           .Text(LOCTEXT("WaterTypeEnabledCheckbox", "Enabled"))
                           .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
                           .ColorAndOpacity(FSlateColor::UseForeground())]]]]];
}

FReply FWetnessProfileEditor::HandleSelectChannel(const EWaterChannel Channel)
{
    ActiveChannel = ActiveChannel == Channel ? EWaterChannel::None : Channel;
    RefreshEditorViews();
    return FReply::Handled();
}

FReply FWetnessProfileEditor::HandleToggleChannelEnabled(const EWaterChannel Channel)
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || Channel == EWaterChannel::None)
    {
        return FReply::Handled();
    }

    const bool bCurrentValue = Channel == EWaterChannel::AbsorbedWater
        ? Profile->Parameters.AbsorbedWetness.bEnabled
        : Profile->Parameters.SurfaceWater.bEnabled;
    const bool bEnabled = !bCurrentValue;

    const FScopedTransaction Transaction(
        Channel == EWaterChannel::AbsorbedWater
            ? LOCTEXT("ToggleAbsorbedWater", "Toggle Absorbed Water")
            : LOCTEXT("ToggleSurfaceWater", "Toggle Surface Water"));
    Profile->Modify();
    if (Channel == EWaterChannel::AbsorbedWater)
    {
        Profile->Parameters.AbsorbedWetness.bEnabled = bEnabled;
    }
    else
    {
        Profile->Parameters.SurfaceWater.bEnabled = bEnabled;
    }
    Profile->MarkPackageDirty();

    if (FProperty* ParametersProperty = FindFProperty<FProperty>(
            UWetnessProfile::StaticClass(),
            FName(TEXT("Parameters"))))
    {
        FPropertyChangedEvent ChangedEvent(ParametersProperty, EPropertyChangeType::ValueSet);
        Profile->PostEditChangeProperty(ChangedEvent);
    }

    RefreshEditorViews();
    return FReply::Handled();
}

void FWetnessProfileEditor::HandleChannelEnabledStateChanged(
    const ECheckBoxState NewState,
    const EWaterChannel Channel)
{
    const bool bRequestedEnabled = NewState == ECheckBoxState::Checked;
    const bool bCurrentlyEnabled = GetChannelEnabledState(Channel) == ECheckBoxState::Checked;
    if (bRequestedEnabled != bCurrentlyEnabled)
    {
        HandleToggleChannelEnabled(Channel);
    }
}

FReply FWetnessProfileEditor::HandleRevertChannelEnabled(const EWaterChannel Channel)
{
#if WITH_EDITORONLY_DATA
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || !Profile->HasEditorSavedParametersSnapshot() || Channel == EWaterChannel::None)
    {
        return FReply::Handled();
    }

    const bool bSaved = Channel == EWaterChannel::AbsorbedWater
        ? Profile->GetEditorSavedParametersSnapshot().AbsorbedWetness.bEnabled
        : Profile->GetEditorSavedParametersSnapshot().SurfaceWater.bEnabled;

    const FScopedTransaction Transaction(LOCTEXT("RevertWaterTypeEnabled", "Revert Water Type Enabled State"));
    Profile->Modify();
    if (Channel == EWaterChannel::AbsorbedWater)
    {
        Profile->Parameters.AbsorbedWetness.bEnabled = bSaved;
    }
    else
    {
        Profile->Parameters.SurfaceWater.bEnabled = bSaved;
    }
    Profile->MarkPackageDirty();
    Profile->PostEditChange();
    RefreshEditorViews();
#endif
    return FReply::Handled();
}

int32 FWetnessProfileEditor::GetActiveChannelIndex() const
{
    switch (ActiveChannel)
    {
    case EWaterChannel::AbsorbedWater:
        return 1;
    case EWaterChannel::SurfaceWater:
        return 2;
    default:
        return 0;
    }
}

bool FWetnessProfileEditor::HasWaterChannelSelection() const
{
    return ActiveChannel != EWaterChannel::None;
}

bool FWetnessProfileEditor::IsSurfaceWaterSelected() const
{
    return ActiveChannel == EWaterChannel::SurfaceWater;
}

ECheckBoxState FWetnessProfileEditor::GetChannelEnabledState(const EWaterChannel Channel) const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || Channel == EWaterChannel::None)
    {
        return ECheckBoxState::Unchecked;
    }

    const bool bEnabled = Channel == EWaterChannel::AbsorbedWater
        ? Profile->Parameters.AbsorbedWetness.bEnabled
        : Profile->Parameters.SurfaceWater.bEnabled;
    return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

EVisibility FWetnessProfileEditor::GetChannelEnabledRevertVisibility(const EWaterChannel Channel) const
{
#if WITH_EDITORONLY_DATA
    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || !Profile->HasEditorSavedParametersSnapshot() || Channel == EWaterChannel::None)
    {
        return EVisibility::Collapsed;
    }

    const bool bCurrent = GetChannelEnabledState(Channel) == ECheckBoxState::Checked;
    const bool bSaved = Channel == EWaterChannel::AbsorbedWater
        ? Profile->GetEditorSavedParametersSnapshot().AbsorbedWetness.bEnabled
        : Profile->GetEditorSavedParametersSnapshot().SurfaceWater.bEnabled;
    return bCurrent != bSaved ? EVisibility::Visible : EVisibility::Collapsed;
#else
    return EVisibility::Collapsed;
#endif
}

FSlateColor FWetnessProfileEditor::GetChannelCardTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FStyleColors::Select);
    }
    return FSlateColor(FLinearColor(0.075f, 0.078f, 0.086f, 1.0f));
}

FSlateColor FWetnessProfileEditor::GetChannelCardOutlineTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FStyleColors::Primary);
    }
    return FSlateColor(FLinearColor(0.19f, 0.20f, 0.23f, 1.0f));
}

FSlateColor FWetnessProfileEditor::GetChannelTitleTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FLinearColor::White);
    }
    return FSlateColor::UseForeground();
}

EVisibility FWetnessProfileEditor::GetChannelSelectedVisibility(const EWaterChannel Channel) const
{
    return Channel == ActiveChannel ? EVisibility::Visible : EVisibility::Collapsed;
}

void FWetnessProfileEditor::RefreshEditorViews()
{
    if (AbsorbedDetailsView.IsValid())
    {
        AbsorbedDetailsView->ForceRefresh();
    }
    if (SurfaceDetailsView.IsValid())
    {
        SurfaceDetailsView->ForceRefresh();
    }
    if (PreviewPanel.IsValid())
    {
        PreviewPanel->RefreshFromProfile();
    }
}

#undef LOCTEXT_NAMESPACE
