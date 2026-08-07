//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 * Declares the Slate controls used by Auto Partition.
 */

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

class SWetPartAutoPartitionControls : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetPartAutoPartitionControls)
        : _IsAutoPartitionEnabled(true)
    {
    }
    SLATE_ATTRIBUTE(bool, IsAutoPartitionEnabled)
    SLATE_EVENT(FOnClicked, OnAutoPartitionClicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
