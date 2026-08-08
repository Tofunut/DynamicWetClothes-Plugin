// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FWetWrinkleElementListItem
{
    FGuid                  StrokeGuid;
    EWetWrinkleElementType ElementType = EWetWrinkleElementType::Patch;
    int32                  SourceIndex = INDEX_NONE;
};

using FWetWrinkleElementListItemPtr = TSharedPtr<FWetWrinkleElementListItem>;

DECLARE_DELEGATE_RetVal_TwoParams(
    TSharedRef<ITableRow>,
    FOnGenerateWetWrinkleElementRow,
    FWetWrinkleElementListItemPtr,
    const TSharedRef<STableViewBase>&);
DECLARE_DELEGATE_TwoParams(
    FOnWetWrinkleElementSelectionChanged,
    FWetWrinkleElementListItemPtr,
    ESelectInfo::Type);

class SWetWrinkleElementListPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleElementListPanel) {}
    SLATE_ATTRIBUTE(FText, SummaryText)
    SLATE_ATTRIBUTE(bool, CanClear)
    SLATE_EVENT(FOnClicked, OnClear)
    SLATE_EVENT(FOnGenerateWetWrinkleElementRow, OnGenerateRow)
    SLATE_EVENT(FOnWetWrinkleElementSelectionChanged, OnSelectionChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void SetItems(
        TArray<FWetWrinkleElementListItemPtr>&& InItems,
        const FGuid&                            SelectedGuid,
        EWetWrinkleElementType                  SelectedType);
    void RequestRefresh();

  private:
    TSharedRef<ITableRow> GenerateRow(
        FWetWrinkleElementListItemPtr     Item,
        const TSharedRef<STableViewBase>& OwnerTable) const;
    void HandleSelectionChanged(FWetWrinkleElementListItemPtr Item, ESelectInfo::Type SelectInfo) const;

    TArray<FWetWrinkleElementListItemPtr>                Items;
    TSharedPtr<SListView<FWetWrinkleElementListItemPtr>> ListView;
    FOnGenerateWetWrinkleElementRow                      OnGenerateRow;
    FOnWetWrinkleElementSelectionChanged                 OnSelectionChanged;
    bool                                                 bSynchronizingSelection = false;
};
