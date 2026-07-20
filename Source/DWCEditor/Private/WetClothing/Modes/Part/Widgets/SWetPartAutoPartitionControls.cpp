/*
 *  Auto Partitioning 버튼과 Color Tolerance 입력 UI를 구현합니다.
 */

#include "WetClothing/Modes/Part/Widgets/SWetPartAutoPartitionControls.h"

#include "Core/DWCEditorStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetPartAutoPartitionControls"

void SWetPartAutoPartitionControls::Construct(const FArguments& InArgs)
{
    ChildSlot
        [SNew(SHorizontalBox)

         + SHorizontalBox::Slot()
               .AutoWidth()
                   [SNew(SButton)
                        .IsEnabled(InArgs._IsAutoPartitionEnabled)
                        .OnClicked(InArgs._OnAutoPartitionClicked)
                        .ContentPadding(FMargin(0.0f, 2.0f, 6.0f, 2.0f))
                            [SNew(SBox)
                                 .HeightOverride(24.0f)
                                 .VAlign(VAlign_Center)
                                     [SNew(SHorizontalBox)

                                      + SHorizontalBox::Slot()
                                            .AutoWidth()
                                            .VAlign(VAlign_Center)
                                            .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                                                [SNew(SBox)
                                                     .WidthOverride(16.0f)
                                                     .HeightOverride(16.0f)
                                                         [SNew(SImage)
                                                              .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.MagicWandTool")))]]

                                      + SHorizontalBox::Slot()
                                            .AutoWidth()
                                            .VAlign(VAlign_Center)
                                                [SNew(STextBlock)
                                                     .Text(LOCTEXT("AutoPartitionButton", "Auto Partition"))]]]]];
}

#undef LOCTEXT_NAMESPACE
