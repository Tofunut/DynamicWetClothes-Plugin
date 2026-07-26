#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleElementListPanel.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SWetWrinkleElementListPanel"

void SWetWrinkleElementListPanel::Construct(const FArguments& InArgs)
{
    OnGenerateRow = InArgs._OnGenerateRow;
    OnSelectionChanged = InArgs._OnSelectionChanged;

    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
    ChildSlot
        [SNew(SBorder)
             .Padding(10.0f)
                 [SNew(SVerticalBox)

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                            [SNew(SHorizontalBox)

                             + SHorizontalBox::Slot()
                                   .FillWidth(1.0f)
                                   .VAlign(VAlign_Center)
                                       [SNew(STextBlock)
                                            .Text(LOCTEXT("ElementListHeading", "Wrinkle Elements"))
                                            .Font(SectionHeadingFont)]

                             + SHorizontalBox::Slot()
                                   .AutoWidth()
                                       [SNew(SButton)
                                            .Text(LOCTEXT("ClearElementListButton", "Clear"))
                                            .IsEnabled(InArgs._CanClear)
                                            .OnClicked(InArgs._OnClear)]]

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                            [SNew(STextBlock)
                                 .AutoWrapText(true)
                                 .Text(InArgs._SummaryText)]

                  + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                            [SAssignNew(ListView, SListView<FWetWrinkleElementListItemPtr>)
                                 .ListItemsSource(&Items)
                                 .OnGenerateRow(this, &SWetWrinkleElementListPanel::GenerateRow)
                                 .OnSelectionChanged(this, &SWetWrinkleElementListPanel::HandleSelectionChanged)]]];
}

void SWetWrinkleElementListPanel::SetItems(
    TArray<FWetWrinkleElementListItemPtr>&& InItems,
    const FGuid& SelectedGuid,
    const EWetWrinkleElementType SelectedType)
{
    Items = MoveTemp(InItems);
    if (!ListView.IsValid())
    {
        return;
    }

    ListView->RequestListRefresh();
    TGuardValue<bool> SelectionGuard(bSynchronizingSelection, true);
    ListView->ClearSelection();
    for (const FWetWrinkleElementListItemPtr& Item : Items)
    {
        if (Item.IsValid() && Item->StrokeGuid == SelectedGuid && Item->ElementType == SelectedType)
        {
            ListView->SetSelection(Item, ESelectInfo::Direct);
            break;
        }
    }
}

void SWetWrinkleElementListPanel::RequestRefresh()
{
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
}

TSharedRef<ITableRow> SWetWrinkleElementListPanel::GenerateRow(
    FWetWrinkleElementListItemPtr Item,
    const TSharedRef<STableViewBase>& OwnerTable) const
{
    check(OnGenerateRow.IsBound());
    return OnGenerateRow.Execute(Item, OwnerTable);
}

void SWetWrinkleElementListPanel::HandleSelectionChanged(
    FWetWrinkleElementListItemPtr Item,
    const ESelectInfo::Type SelectInfo) const
{
    if (!bSynchronizingSelection)
    {
        OnSelectionChanged.ExecuteIfBound(Item, SelectInfo);
    }
}

#undef LOCTEXT_NAMESPACE
