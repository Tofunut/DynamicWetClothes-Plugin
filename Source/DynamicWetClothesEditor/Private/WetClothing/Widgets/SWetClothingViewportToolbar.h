/*
 *  Wet Clothing 3D Viewport Toolbar Slate 위젯을 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"

class SWetClothingViewportToolbar : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingViewportToolbar)
        : _SelectionLineThicknessScale(1.0f)
    {
    }
    SLATE_ATTRIBUTE(float, SelectionLineThicknessScale)
    SLATE_EVENT(SSpinBox<float>::FOnValueChanged, OnSelectionLineThicknessChanged)
    SLATE_EVENT(FOnClicked, OnFocusPreviewClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
