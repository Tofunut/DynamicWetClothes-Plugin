#include "SWetClothingAssetEditorPanel.h"

#include "DataAssets/WetClothingAsset.h"
#include "IDetailsView.h"
#include "WetClothing/PartEdit/Editor/SWetClothingPartEditorPanel.h"
#include "WetClothing/TransparencyBake/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/WrinkleEdit/Editor/SWetWrinkleEditorPanel.h"
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
               [SAssignNew(TransparencyBakePanel, SWetClothingTransparencyBakePanel)
                    .WetClothingAsset(WetClothingAsset.Get())
                    .DetailsView(DetailsView)]];

    SetEditorMode(EWetClothingEditorMode::PartEdit);
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
    if (TransparencyBakePanel.IsValid())
    {
        TransparencyBakePanel->RefreshFromAsset();
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
    if (TransparencyBakePanel.IsValid() && TransparencyBakePanel->HasPendingTransparencySetup(&TransparencySummary))
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

    if (TransparencyBakePanel.IsValid() && WetClothingAsset.IsValid() && !WetClothingAsset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        FString TransparencySummary;
        bool bTransparencyHadWarnings = false;
        if (!TransparencyBakePanel->BuildTransparencySetup(TransparencySummary, &bTransparencyHadWarnings))
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

bool SWetClothingAssetEditorPanel::BuildPendingWetSetup(FString& OutSummary, bool* OutHadWarnings)
{
    bool bHadWarnings = false;
    TArray<FString> Sections;

    if (PartEditorPanel.IsValid())
    {
        FString PartPendingSummary;
        if (PartEditorPanel->HasPendingWetSetupTasks(&PartPendingSummary))
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
    }

    if (TransparencyBakePanel.IsValid())
    {
        FString TransparencyPendingSummary;
        if (TransparencyBakePanel->HasPendingTransparencySetup(&TransparencyPendingSummary))
        {
            FString TransparencySummary;
            bool bTransparencyHadWarnings = false;
            if (!TransparencyBakePanel->BuildTransparencySetup(TransparencySummary, &bTransparencyHadWarnings))
            {
                OutSummary = TransparencySummary;
                return false;
            }
            bHadWarnings |= bTransparencyHadWarnings;
            Sections.Add(TransparencySummary);
        }
    }

    OutSummary = Sections.Num() > 0 ? FString::Join(Sections, TEXT("\n\n")) : TEXT("Wet setup is already up to date.");
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = bHadWarnings;
    }
    return true;
}

bool SWetClothingAssetEditorPanel::BuildTransparencySetup(FString& OutSummary, bool* OutHadWarnings)
{
    if (!TransparencyBakePanel.IsValid())
    {
        OutSummary = TEXT("Transparency editor is not available.");
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = false;
        }
        return false;
    }

    return TransparencyBakePanel->BuildTransparencySetup(OutSummary, OutHadWarnings);
}

bool SWetClothingAssetEditorPanel::SaveTransparencySetupAssets() const
{
    return TransparencyBakePanel.IsValid() ? TransparencyBakePanel->SaveTransparencySetupAssets() : true;
}

bool SWetClothingAssetEditorPanel::SaveWetSetupAssets() const
{
    bool bSaved = true;
    if (PartEditorPanel.IsValid())
    {
        bSaved &= PartEditorPanel->SaveWetSetupAssets();
    }
    if (TransparencyBakePanel.IsValid())
    {
        bSaved &= TransparencyBakePanel->SaveTransparencySetupAssets();
    }
    return bSaved;
}

void SWetClothingAssetEditorPanel::SetEditorMode(EWetClothingEditorMode NewMode)
{
    if (NewMode == EWetClothingEditorMode::PartEdit && PartEditorPanel.IsValid())
    {
        PartEditorPanel->RefreshFromAsset();
    }
    else if (NewMode == EWetClothingEditorMode::WrinkleEdit && WrinkleEditorPanel.IsValid())
    {
        WrinkleEditorPanel->RefreshFromAsset();
    }
    else if (NewMode == EWetClothingEditorMode::TransparencyBake && TransparencyBakePanel.IsValid())
    {
        TransparencyBakePanel->RefreshFromAsset();
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
    case EWetClothingEditorMode::PartEdit:
        return 0;
    case EWetClothingEditorMode::WrinkleEdit:
        return 1;
    case EWetClothingEditorMode::TransparencyBake:
        return 2;
    default:
        return 0;
    }
}

#undef LOCTEXT_NAMESPACE
