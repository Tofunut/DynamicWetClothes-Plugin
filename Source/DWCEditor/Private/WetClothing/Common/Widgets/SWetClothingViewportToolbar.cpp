/*
 *  3D Viewport 상단 툴바의 Selection Line 조절과 Focus Mesh 버튼 UI를 구현합니다.
 */

#include "WetClothing/Common/Widgets/SWetClothingViewportToolbar.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingViewportToolbar"

void SWetClothingViewportToolbar::Construct(const FArguments& InArgs)
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    ChildSlot
        [SNew(SHorizontalBox)

         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .VAlign(VAlign_Center)
                   [SNew(STextBlock)
                         .Text(LOCTEXT("PreviewLabel", "Preview"))
                        .Font(SectionHeadingFont)]

         + SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(0.0f, 0.0f, 6.0f, 0.0f)
               .VAlign(VAlign_Center)
                   [SNew(STextBlock)
                        .Text(LOCTEXT("SelectionLineThicknessLabel", "Selection Line"))]

         + SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(0.0f, 0.0f, 10.0f, 0.0f)
               .VAlign(VAlign_Center)
                   [SNew(SBox)
                        .WidthOverride(88.0f)
                            [SNew(SSpinBox<float>)
                                 .MinValue(0.25f)
                                 .MaxValue(4.0f)
                                 .MinSliderValue(0.25f)
                                 .MaxSliderValue(4.0f)
                                 .Delta(0.05f)
                                 .Value(InArgs._SelectionLineThicknessScale)
                                 .OnValueChanged(InArgs._OnSelectionLineThicknessChanged)]]

         + SHorizontalBox::Slot()
               .AutoWidth()
                   [SNew(SButton)
                        .Text(LOCTEXT("FocusMeshButton", "Focus Mesh"))
                        .OnClicked(InArgs._OnFocusPreviewClicked)]];
}

#undef LOCTEXT_NAMESPACE
