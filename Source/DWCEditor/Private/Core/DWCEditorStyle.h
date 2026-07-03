#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;
struct FSlateBrush;
class ISlateStyle;

class FDWCEditorStyle
{
  public:
    static void Initialize();
    static void Shutdown();

    static FName              GetStyleSetName();
    static const ISlateStyle& Get();
    static const FSlateBrush* GetBrush(const FName BrushName);

  private:
    static TSharedPtr<FSlateStyleSet> StyleSet;
};
