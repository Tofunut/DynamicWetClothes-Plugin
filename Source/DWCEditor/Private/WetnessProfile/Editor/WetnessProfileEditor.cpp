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
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WetnessProfileDetailsCustomization.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
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
    AbsorbedDetailsView = CreateChannelDetailsView(true);
    SurfaceDetailsView = CreateChannelDetailsView(false);

    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(
        this,
        &FWetnessProfileEditor::HandleObjectPropertyChanged);

    const TSharedRef<FTabManager::FLayout> Layout =
        FTabManager::NewLayout(TEXT("Standalone_WetnessProfileEditor_Layout_v3"))
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

    return SNew(SDockTab)
        .Label(LOCTEXT("SettingsTabLabel", "Water Settings"))
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(8.0f, 8.0f, 8.0f, 6.0f)
                   [BuildChannelSelector()]

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(4.0f, 0.0f, 4.0f, 4.0f)
                   [SAssignNew(ChannelSwitcher, SWidgetSwitcher)
                        .WidgetIndex(this, &FWetnessProfileEditor::GetActiveChannelIndex)

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
                                         .Text(LOCTEXT("MissingSurfaceDetails", "Surface Water details are unavailable.")))]]];
}

TSharedRef<SDockTab> FWetnessProfileEditor::SpawnPreviewTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == PreviewTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("PreviewTabLabel", "Preview"))
        [SAssignNew(PreviewPanel, SWetnessProfileEditorPanel)
             .WetnessProfile(WetnessProfile.Get())
             .AbsorbedDetailsView(AbsorbedDetailsView)
             .SurfaceDetailsView(SurfaceDetailsView)];
}

TSharedRef<SWidget> FWetnessProfileEditor::BuildChannelSelector()
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                  [BuildChannelCard(
                      EWaterChannel::AbsorbedWater,
                      LOCTEXT("AbsorbedWaterChannelTitle", "Absorbed Water"),
                      LOCTEXT("AbsorbedWaterChannelDescription", "Water held inside the material"))]

        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                  [BuildChannelCard(
                      EWaterChannel::SurfaceWater,
                      LOCTEXT("SurfaceWaterChannelTitle", "Surface Water"),
                      LOCTEXT("SurfaceWaterChannelDescription", "Visible droplets on the material surface"))];
}

TSharedRef<SWidget> FWetnessProfileEditor::BuildChannelCard(
    const EWaterChannel Channel,
    const FText& Title,
    const FText& Description)
{
    const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15);
    const FSlateFontInfo SelectedFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9);

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(this, &FWetnessProfileEditor::GetChannelCardOutlineTint, Channel)
        .Padding(2.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                 .BorderBackgroundColor(this, &FWetnessProfileEditor::GetChannelCardTint, Channel)
                 .Padding(0.0f)
                     [SNew(SBox)
                          .HeightOverride(66.0f)
                              [SNew(SHorizontalBox)

                               + SHorizontalBox::Slot()
                                     .FillWidth(1.0f)
                                         [SNew(SButton)
                                              .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                                              .ContentPadding(FMargin(14.0f, 8.0f))
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
                                                         .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(Description)
                                                                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]]

                               + SHorizontalBox::Slot()
                                     .AutoWidth()
                                     .VAlign(VAlign_Center)
                                     .Padding(8.0f, 0.0f, 14.0f, 0.0f)
                                         [SNew(SVerticalBox)

                                          + SVerticalBox::Slot()
                                                .AutoHeight()
                                                .HAlign(HAlign_Right)
                                                .Padding(0.0f, 0.0f, 0.0f, 7.0f)
                                                    [SNew(SBorder)
                                                         .Visibility(this, &FWetnessProfileEditor::GetChannelSelectedVisibility, Channel)
                                                         .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                         .BorderBackgroundColor(FStyleColors::Primary)
                                                         .Padding(FMargin(7.0f, 2.0f))
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("SelectedChannelBadge", "SELECTED"))
                                                                  .Font(SelectedFont)
                                                                  .ColorAndOpacity(FLinearColor::White)]]

                                          + SVerticalBox::Slot()
                                                .AutoHeight()
                                                .HAlign(HAlign_Right)
                                                    [SNew(SCheckBox)
                                                         .IsChecked(this, &FWetnessProfileEditor::GetChannelEnabledState, Channel)
                                                         .OnCheckStateChanged(this, &FWetnessProfileEditor::HandleChannelEnabledChanged, Channel)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("ChannelEnabledLabel", "Enabled"))]]]]]];
}

FReply FWetnessProfileEditor::HandleSelectChannel(const EWaterChannel Channel)
{
    ActiveChannel = Channel;
    return FReply::Handled();
}

int32 FWetnessProfileEditor::GetActiveChannelIndex() const
{
    return ActiveChannel == EWaterChannel::AbsorbedWater ? 0 : 1;
}

ECheckBoxState FWetnessProfileEditor::GetChannelEnabledState(const EWaterChannel Channel) const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return ECheckBoxState::Unchecked;
    }

    const bool bEnabled = Channel == EWaterChannel::AbsorbedWater
        ? Profile->Parameters.AbsorbedWetness.bEnabled
        : Profile->Parameters.SurfaceWater.bEnabled;
    return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FWetnessProfileEditor::HandleChannelEnabledChanged(
    const ECheckBoxState NewState,
    const EWaterChannel Channel)
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return;
    }

    const bool bEnabled = NewState == ECheckBoxState::Checked;
    const bool bCurrentValue = Channel == EWaterChannel::AbsorbedWater
        ? Profile->Parameters.AbsorbedWetness.bEnabled
        : Profile->Parameters.SurfaceWater.bEnabled;
    if (bCurrentValue == bEnabled)
    {
        return;
    }

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
}

FSlateColor FWetnessProfileEditor::GetChannelCardTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FStyleColors::Primary);
    }
    return FSlateColor(FLinearColor(0.055f, 0.055f, 0.055f, 1.0f));
}

FSlateColor FWetnessProfileEditor::GetChannelCardOutlineTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FStyleColors::Primary);
    }
    return FSlateColor(FLinearColor(0.13f, 0.13f, 0.13f, 1.0f));
}

FSlateColor FWetnessProfileEditor::GetChannelTitleTint(const EWaterChannel Channel) const
{
    if (Channel == ActiveChannel)
    {
        return FSlateColor(FStyleColors::Foreground);
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
