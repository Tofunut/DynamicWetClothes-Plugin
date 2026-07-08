#include "STransparencyPlaceholderPanel.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TransparencyPlaceholderPanel"

void STransparencyPlaceholderPanel::Construct(const FArguments& InArgs)
{
    ChildSlot
        [SNew(SBorder)
             .Padding(20.0f)
                 [SNew(STextBlock)
                      .Text(LOCTEXT("PlaceholderText", "Transparency mode is not implemented yet."))]];
}

#undef LOCTEXT_NAMESPACE
