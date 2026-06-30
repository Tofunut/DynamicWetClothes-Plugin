/*
 *  Auto Partitioning 컨트롤 Slate 위젯을 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

class SWetClothingAutoPartitionControls : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingAutoPartitionControls)
        : _IsAutoPartitionEnabled(true)
    {
    }
    SLATE_ATTRIBUTE(bool, IsAutoPartitionEnabled)
    SLATE_EVENT(FOnClicked, OnAutoPartitionClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
