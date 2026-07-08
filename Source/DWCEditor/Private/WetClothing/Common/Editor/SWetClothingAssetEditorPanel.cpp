#include "SWetClothingAssetEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
#include "WetClothing/PartMode/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/TransparencyMode/Editor/SWetClothingTransparencyEditorPanel.h"
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
               [SAssignNew(TransparencyEditorPanel, SWetClothingTransparencyEditorPanel)
                    .WetClothingAsset(WetClothingAsset.Get())
                    .DetailsView(DetailsView)]];

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
    if (TransparencyEditorPanel.IsValid())
    {
        TransparencyEditorPanel->RefreshFromAsset();
    }
}

bool SWetClothingAssetEditorPanel::HasPendingWetSetupTasks(FString* OutSummary) const
{
    TArray<FString> PendingSections;
    FString PartSummary;
    if (PartEditorPanel.IsValid() && PartEditorPanel->HasPendingWetSetupTasks(&PartSummary))
    {
        PendingSections.Add(PartSummary);
    }

    FString TransparencySummary;
    if (TransparencyEditorPanel.IsValid() && TransparencyEditorPanel->HasPendingTransparencySetup(&TransparencySummary))
    {
        PendingSections.Add(TransparencySummary);
    }

    if (OutSummary != nullptr)
    {
        *OutSummary = PendingSections.Num() == 0
            ? TEXT("Wet setup is up to date.")
            : FString::Join(PendingSections, TEXT("\n\n"));
    }

    return PendingSections.Num() > 0;
}

bool SWetClothingAssetEditorPanel::BuildWetSetup(FString& OutSummary, bool* OutHadWarnings)
{
    bool bHadWarnings = false;
    TArray<FString> Sections;

    if (PartEditorPanel.IsValid())
    {
        FString PartSummary;
        bool bPartHadWarnings = false;
        if (!PartEditorPanel->BuildWetSetup(PartSummary, &bPartHadWarnings))
        {
            OutSummary = PartSummary;
            return false;
        }
        bHadWarnings |= bPartHadWarnings;
        Sections.Add(PartSummary);
    }

    if (TransparencyEditorPanel.IsValid() && WetClothingAsset.IsValid() && !WetClothingAsset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        FString TransparencySummary;
        bool bTransparencyHadWarnings = false;
        if (!TransparencyEditorPanel->BuildTransparencySetup(TransparencySummary, &bTransparencyHadWarnings))
        {
            OutSummary = TransparencySummary;
            return false;
        }
        bHadWarnings |= bTransparencyHadWarnings;
        Sections.Add(TransparencySummary);
    }

    OutSummary = Sections.Num() > 0 ? FString::Join(Sections, TEXT("\n\n")) : TEXT("Wet setup is already up to date.");
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = bHadWarnings;
    }
    return true;
}

bool SWetClothingAssetEditorPanel::SaveWetSetupAssets() const
{
    bool bSaved = true;
    if (PartEditorPanel.IsValid())
    {
        bSaved &= PartEditorPanel->SaveWetSetupAssets();
    }
    if (TransparencyEditorPanel.IsValid())
    {
        bSaved &= TransparencyEditorPanel->SaveTransparencySetupAssets();
    }
    return bSaved;
}

FReply SWetClothingAssetEditorPanel::ExecuteBakeWrinkleNormalMap()
{
    if (!WrinkleEditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    return WrinkleEditorPanel->ExecuteBakeWrinkleNormalMap();
}

FReply SWetClothingAssetEditorPanel::ExecuteBakeWrinkleMask()
{
    if (!WrinkleEditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    return WrinkleEditorPanel->ExecuteBakeWrinkleMask();
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
    else if (NewMode == EWetClothingEditorMode::Transparency && TransparencyEditorPanel.IsValid())
    {
        TransparencyEditorPanel->RefreshFromAsset();
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
