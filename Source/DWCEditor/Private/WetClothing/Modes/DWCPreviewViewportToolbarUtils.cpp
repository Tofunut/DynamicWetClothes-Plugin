// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"

#include "EditorViewportClient.h"
#include "SEditorViewport.h"
#include "Settings/EditorViewportSettings.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ViewportToolbar/UnrealEdViewportToolbar.h"

#define LOCTEXT_NAMESPACE "DWCPreviewViewportToolbarUtils"

namespace
{
    constexpr float DWCPreviewCameraSpeed = 0.25f;
    constexpr float DWCPreviewCameraMinUISpeed = 0.01f;
    constexpr float DWCPreviewCameraMaxUISpeed = 32.0f;

    void AddBufferVisualizationEntry(
        FToolMenuSection&                     Section,
        const TWeakPtr<FEditorViewportClient> WeakViewportClient,
        const FName                           ModeName,
        const FText&                          Label,
        const FText&                          Tooltip)
    {
        const FName EntryName(*FString::Printf(TEXT("DWCBufferVisualization_%s"), *ModeName.ToString()));
        Section.AddMenuEntry(
            EntryName,
            Label,
            Tooltip,
            FSlateIcon(),
            FUIAction(
                FExecuteAction::CreateLambda(
                    [WeakViewportClient, ModeName]()
                    {
                        if (const TSharedPtr<FEditorViewportClient> ViewportClient = WeakViewportClient.Pin())
                        {
                            ViewportClient->ChangeBufferVisualizationMode(ModeName);
                            ViewportClient->Invalidate();
                        }
                    }),
                FCanExecuteAction::CreateLambda(
                    [WeakViewportClient]()
                    {
                        return WeakViewportClient.IsValid();
                    }),
                FIsActionChecked::CreateLambda(
                    [WeakViewportClient, ModeName]()
                    {
                        const TSharedPtr<FEditorViewportClient> ViewportClient = WeakViewportClient.Pin();
                        return ViewportClient.IsValid() && ViewportClient->IsBufferVisualizationModeSelected(ModeName);
                    })),
            EUserInterfaceActionType::RadioButton);
    }

    void PopulateDWCBufferVisualizationMenu(UToolMenu* Menu, const TWeakPtr<FEditorViewportClient> WeakViewportClient)
    {
        FToolMenuSection& Section = Menu->AddSection(
            TEXT("DWCBufferVisualizationTargets"),
            LOCTEXT("DWCBufferVisualizationTargets", "Buffer Visualization"));

        AddBufferVisualizationEntry(
            Section,
            WeakViewportClient,
            TEXT("WorldNormal"),
            LOCTEXT("DWCBufferVisualizationWorldNormal", "World Normal"),
            LOCTEXT("DWCBufferVisualizationWorldNormalTooltip", "Show the World Normal buffer visualization."));

        AddBufferVisualizationEntry(
            Section,
            WeakViewportClient,
            TEXT("BaseColor"),
            LOCTEXT("DWCBufferVisualizationBaseColor", "Base Color"),
            LOCTEXT("DWCBufferVisualizationBaseColorTooltip", "Show the Base Color buffer visualization."));

        AddBufferVisualizationEntry(
            Section,
            WeakViewportClient,
            TEXT("Roughness"),
            LOCTEXT("DWCBufferVisualizationRoughness", "Roughness"),
            LOCTEXT("DWCBufferVisualizationRoughnessTooltip", "Show the Roughness buffer visualization."));

        AddBufferVisualizationEntry(
            Section,
            WeakViewportClient,
            TEXT("Metallic"),
            LOCTEXT("DWCBufferVisualizationMetallic", "Metallic"),
            LOCTEXT("DWCBufferVisualizationMetallicTooltip", "Show the Metallic buffer visualization."));
    }

    void PopulateDWCViewModesMenu(UToolMenu* Menu, const TWeakPtr<SEditorViewport> WeakViewport)
    {
        UE::UnrealEd::PopulateViewModesMenu(Menu);

        TWeakPtr<FEditorViewportClient> WeakViewportClient;
        if (const TSharedPtr<SEditorViewport> Viewport = WeakViewport.Pin())
        {
            WeakViewportClient = Viewport->GetViewportClient();
        }
        else if (Menu != nullptr)
        {
            WeakViewportClient = UUnrealEdViewportToolbarContext::GetEditorViewportClient(Menu->Context);
        }

        FToolMenuSection& Section = Menu->AddSection(
            TEXT("DWCBufferVisualization"),
            LOCTEXT("DWCBufferVisualizationSection", "Buffer Visualization"));

        Section.AddSubMenu(
            TEXT("DWCBufferVisualizationSubmenu"),
            LOCTEXT("DWCBufferVisualizationSubmenu", "Buffer Visualization"),
            LOCTEXT("DWCBufferVisualizationSubmenuTooltip", "Inspect material buffer visualizations in the WCA preview viewport."),
            FNewToolMenuDelegate::CreateLambda(
                [WeakViewportClient](UToolMenu* SubMenu)
                {
                    PopulateDWCBufferVisualizationMenu(SubMenu, WeakViewportClient);
                }),
            FUIAction(
                FExecuteAction(),
                FCanExecuteAction::CreateLambda(
                    [WeakViewportClient]()
                    {
                        return WeakViewportClient.IsValid();
                    }),
                FIsActionChecked::CreateLambda(
                    [WeakViewportClient]()
                    {
                        const TSharedPtr<FEditorViewportClient> ViewportClient = WeakViewportClient.Pin();
                        return ViewportClient.IsValid() && ViewportClient->IsViewModeEnabled(VMI_VisualizeBuffer);
                    })),
            EUserInterfaceActionType::RadioButton,
            false,
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("EditorViewport.VisualizeBufferMode")));
    }
} // namespace

void UE::DWCEditor::ApplyDWCPreviewCameraSpeedSettings(FEditorViewportClient& ViewportClient)
{
    ViewportClient.SetCameraSpeedSettings(FEditorViewportCameraSpeedSettings::FromUIRange(
        DWCPreviewCameraSpeed,
        DWCPreviewCameraMinUISpeed,
        DWCPreviewCameraMaxUISpeed));
}

FToolMenuEntry UE::DWCEditor::CreateDWCViewModesSubmenu()
{
    return FToolMenuEntry::InitDynamicEntry(
        TEXT("DWCDynamicViewModes"),
        FNewToolMenuSectionDelegate::CreateLambda(
            [](FToolMenuSection& InDynamicSection)
            {
                TAttribute<FText>         LabelAttribute = UE::UnrealEd::GetViewModesSubmenuLabel(nullptr);
                TAttribute<FSlateIcon>    IconAttribute;
                TWeakPtr<SEditorViewport> WeakViewport;

                if (UUnrealEdViewportToolbarContext* const Context = InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>())
                {
                    WeakViewport = Context->Viewport;

                    LabelAttribute = TAttribute<FText>::CreateLambda(
                        [WeakViewport = Context->Viewport]()
                        {
                            return UE::UnrealEd::GetViewModesSubmenuLabel(WeakViewport);
                        });

                    IconAttribute = TAttribute<FSlateIcon>::CreateLambda(
                        [WeakViewport = Context->Viewport]()
                        {
                            return UE::UnrealEd::GetViewModesSubmenuIcon(WeakViewport);
                        });
                }

                FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
                    TEXT("ViewModes"),
                    LabelAttribute,
                    LOCTEXT("DWCViewModesSubmenuTooltip", "View mode settings for the current viewport."),
                    FNewToolMenuDelegate::CreateLambda(
                        [WeakViewport](UToolMenu* Menu)
                        {
                            PopulateDWCViewModesMenu(Menu, WeakViewport);
                        }),
                    false,
                    IconAttribute);
                Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
            }));
}

#undef LOCTEXT_NAMESPACE
