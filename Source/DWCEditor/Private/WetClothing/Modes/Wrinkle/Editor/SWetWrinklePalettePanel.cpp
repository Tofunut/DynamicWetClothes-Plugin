//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinklePalettePanel.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SWetWrinklePalettePanel"

void SWetWrinklePalettePanel::Construct(const FArguments& InArgs)
{
    OnGenerateTile = InArgs._OnGenerateTile;
    ButtonStyle = FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 6.0f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.15f, 1.15f, 1.15f, 1.0f), 6.0f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), 6.0f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White, 6.0f))
        .SetNormalPadding(FMargin(0.0f))
        .SetPressedPadding(FMargin(0.0f));

    ChildSlot
        [SNew(SBox)
             .HeightOverride(332.0f)
                 [SNew(SOverlay)

                  + SOverlay::Slot()
                        [SAssignNew(TileView, STileView<FWetWrinkleTexturePaletteItemPtr>)
                             .ListItemsSource(&VisibleItems)
                             .OnGenerateTile(this, &SWetWrinklePalettePanel::GenerateTile)
                             .ItemWidth(160.0f)
                             .ItemHeight(160.0f)
                             .SelectionMode(ESelectionMode::None)]

                  + SOverlay::Slot()
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                            [SNew(STextBlock)
                                 .AutoWrapText(true)
                                 .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                                 .Text(LOCTEXT(
                                     "NoPaletteItems",
                                     "No wrinkle normal textures were found in the configured paths."))
                                 .Visibility_Lambda([this]()
                                 {
                                     return AllItems.IsEmpty()
                                         ? EVisibility::HitTestInvisible
                                         : EVisibility::Collapsed;
                                 })]]];
}

void SWetWrinklePalettePanel::RequestRefresh()
{
    if (TileView.IsValid())
    {
        TileView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SWetWrinklePalettePanel::GenerateTile(
    FWetWrinkleTexturePaletteItemPtr Item,
    const TSharedRef<STableViewBase>& OwnerTable) const
{
    check(OnGenerateTile.IsBound());
    return OnGenerateTile.Execute(Item, OwnerTable);
}

#undef LOCTEXT_NAMESPACE
