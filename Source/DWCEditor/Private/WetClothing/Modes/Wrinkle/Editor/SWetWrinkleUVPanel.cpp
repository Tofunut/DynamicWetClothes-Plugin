//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleUVPanel.h"

#include "Styling/AppStyle.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SWetWrinkleUVPanel"

void SWetWrinkleUVPanel::Construct(const FArguments& InArgs)
{
    OnViewSettingsChanged = InArgs._OnViewSettingsChanged;

    ChildSlot
        [SNew(SBorder)
             .Padding(8.0f)
             .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                 [SNew(SVerticalBox)

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                            [FWCAEditorWidgets::BuildSectionHeader(
                                LOCTEXT("WrinkleUVViewLabel", "Wrinkle UV View"),
                                InArgs._ChannelText)]

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                            [SNew(SSeparator)
                                 .Orientation(Orient_Horizontal)]

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                            [SNew(SBox)
                                 .HAlign(HAlign_Right)
                                     [FWCAEditorWidgets::BuildUVViewOptionsButton(
                                         TAttribute<float>(),
                                         TFunction<void(float)>(),
                                         TAttribute<float>::CreateLambda([this]()
                                         {
                                             return IslandLineOpacity;
                                         }),
                                         [this](const float NewValue)
                                         {
                                             HandleIslandLineOpacityChanged(NewValue);
                                         },
                                         TAttribute<float>::CreateLambda([this]()
                                         {
                                             return IslandLineThicknessScale;
                                         }),
                                         [this](const float NewValue)
                                         {
                                             HandleIslandLineThicknessScaleChanged(NewValue);
                                         },
                                         false)]]

                  + SVerticalBox::Slot()
                        .FillHeight(1.0f)
                            [SAssignNew(UVView, SWCAUVView)]]];

    ApplyViewSettings();
}

void SWetWrinkleUVPanel::HandleIslandLineOpacityChanged(const float NewValue)
{
    IslandLineOpacity = FMath::Clamp(NewValue, 0.0f, 1.0f);
    ApplyViewSettings();
    OnViewSettingsChanged.ExecuteIfBound();
}

void SWetWrinkleUVPanel::HandleIslandLineThicknessScaleChanged(const float NewValue)
{
    IslandLineThicknessScale = FMath::Clamp(NewValue, 0.25f, 6.0f);
    ApplyViewSettings();
    OnViewSettingsChanged.ExecuteIfBound();
}

void SWetWrinkleUVPanel::ApplyViewSettings()
{
    if (UVView.IsValid())
    {
        UVView->SetDisplayMode(EWCAUVDisplayMode::Normal);
        UVView->SetUVIslandLineOpacity(IslandLineOpacity);
        UVView->SetUVIslandLineThicknessScale(IslandLineThicknessScale);
    }
}

#undef LOCTEXT_NAMESPACE
