//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WCAEditor.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DWCEditorStyle.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/ToolBarStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "WCAEditor"

namespace
{
    const FCheckBoxStyle& GetWetClothingModeToggleStyle()
    {
        static const FSlateRoundedBoxBrush UncheckedBrush(FStyleColors::Header, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedHoveredBrush(FStyleColors::Hover, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedPressedBrush(FStyleColors::Recessed, 4.0f);
        static const FSlateRoundedBoxBrush CheckedBrush(FStyleColors::Primary, 4.0f);
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
}

TSharedRef<SWidget> FWCAEditor::BuildModeToolbarWidget()
{
    return SNew(SBox)
        .Padding(FMargin(12.0f, 0.0f))
            [SNew(SHorizontalBox)
             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::PartEdit,
                           TEXT("DWCEditor.Mode.Part"),
                           LOCTEXT("PartEditModeTooltip", "Part Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::WrinkleEdit,
                           TEXT("DWCEditor.Mode.Wrinkle"),
                           LOCTEXT("WrinkleEditModeTooltip", "Wrinkle Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::TransparencyBake,
                           TEXT("DWCEditor.Mode.Transparency"),
                           LOCTEXT("TransparencyBakeModeTooltip", "Transparency Bake Mode"))]];
}

TSharedRef<SWidget> FWCAEditor::BuildModeToggleButton(
    const EWCAEditorMode Mode,
    const FName IconName,
    const FText& ToolTipText)
{
    return SNew(SCheckBox)
        .Style(&GetWetClothingModeToggleStyle())
        .Type(ESlateCheckBoxType::ToggleButton)
        .ToolTipText(ToolTipText)
        .IsChecked(this, &FWCAEditor::IsModeChecked, Mode)
        .OnCheckStateChanged(this, &FWCAEditor::HandleModeCheckStateChanged, Mode)
            [SNew(SBox)
                 .WidthOverride(76.0f)
                 .HeightOverride(32.0f)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(SImage)
                          .DesiredSizeOverride(FVector2D(24.0f, 24.0f))
                          .Image(FDWCEditorStyle::GetBrush(IconName))
                          .ColorAndOpacity(this, &FWCAEditor::GetModeIconColor, Mode)]];
}

void FWCAEditor::SetEditorMode(const EWCAEditorMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    FDWCEditorAuthoringOperationScope DiagnosticScope(TEXT("WCAEditor.SetEditorMode"), WetClothingAsset.Get());
    CurrentMode = NewMode;

    if (EditorPanel.IsValid())
    {
        EditorPanel->SetEditorMode(NewMode);
    }

    RegenerateMenusAndToolbars();
}

ECheckBoxState FWCAEditor::IsModeChecked(const EWCAEditorMode Mode) const
{
    return CurrentMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FWCAEditor::HandleModeCheckStateChanged(
    const ECheckBoxState NewState,
    const EWCAEditorMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SetEditorMode(Mode);
    }
}

FSlateColor FWCAEditor::GetModeIconColor(const EWCAEditorMode Mode) const
{
    if (CurrentMode == Mode)
    {
        return FSlateColor(FLinearColor::White);
    }

    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        return FSlateColor(FLinearColor(1.0f, 0.66f, 0.78f, 1.0f));
    case EWCAEditorMode::WrinkleEdit:
        return FSlateColor(FLinearColor(0.62f, 0.95f, 0.62f, 1.0f));
    case EWCAEditorMode::TransparencyBake:
        return FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 1.0f));
    default:
        return FSlateColor::UseForeground();
    }
}

#undef LOCTEXT_NAMESPACE
