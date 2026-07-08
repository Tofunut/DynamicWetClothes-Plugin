#include "SWetClothingAssetEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
#include "WetClothing/PartMode/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/TransparencyMode/Editor/STransparencyPlaceholderPanel.h"
#include "WetClothing/WrinkleMode/Editor/SWetWrinkleEditorPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

void SWetClothingAssetEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;

    ChildSlot
        [SAssignNew(ModeContentSwitcher, SWidgetSwitcher)

         + SWidgetSwitcher::Slot()
               [SAssignNew(PartEditorPanel, SWetClothingPartEditorPanel)
                    .WetClothingAsset(WetClothingAsset.Get())
                    .DetailsView(DetailsView)]

         + SWidgetSwitcher::Slot()
               [SAssignNew(WrinkleEditorPanel, SWetWrinkleEditorPanel)
                    .WetClothingAsset(WetClothingAsset.Get())
                    .DetailsView(DetailsView)]

         + SWidgetSwitcher::Slot()
               [SNew(STransparencyPlaceholderPanel)]];

    SetEditorMode(EWetClothingEditorMode::Part);
}

void SWetClothingAssetEditorPanel::RefreshFromAsset()
{
    if (PartEditorPanel.IsValid())
    {
        PartEditorPanel->RefreshFromAsset();
    }
    if (WrinkleEditorPanel.IsValid())
    {
        WrinkleEditorPanel->RefreshFromAsset();
    }
}

bool SWetClothingAssetEditorPanel::HasPendingWetSetupTasks(FString* OutSummary) const
{
    return PartEditorPanel.IsValid() && PartEditorPanel->HasPendingWetSetupTasks(OutSummary);
}

bool SWetClothingAssetEditorPanel::BuildWetSetup(FString& OutSummary, bool* OutHadWarnings)
{
    return PartEditorPanel.IsValid() && PartEditorPanel->BuildWetSetup(OutSummary, OutHadWarnings);
}

bool SWetClothingAssetEditorPanel::SaveWetSetupAssets() const
{
    return PartEditorPanel.IsValid() && PartEditorPanel->SaveWetSetupAssets();
}

void SWetClothingAssetEditorPanel::SetEditorMode(EWetClothingEditorMode NewMode)
{
    if (NewMode == EWetClothingEditorMode::Part && PartEditorPanel.IsValid())
    {
        PartEditorPanel->RefreshFromAsset();
    }
    else if (NewMode == EWetClothingEditorMode::Wrinkle && WrinkleEditorPanel.IsValid())
    {
        WrinkleEditorPanel->RefreshFromAsset();
    }

    if (ModeContentSwitcher.IsValid())
    {
        ModeContentSwitcher->SetActiveWidgetIndex(GetModeIndex(NewMode));
    }
}

int32 SWetClothingAssetEditorPanel::GetModeIndex(EWetClothingEditorMode Mode) const
{
    switch (Mode)
    {
    case EWetClothingEditorMode::Part:
        return 0;
    case EWetClothingEditorMode::Wrinkle:
        return 1;
    case EWetClothingEditorMode::Transparency:
        return 2;
    default:
        return 0;
    }
}

#undef LOCTEXT_NAMESPACE
