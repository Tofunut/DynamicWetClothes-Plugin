#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STransparencyPlaceholderPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(STransparencyPlaceholderPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
};
