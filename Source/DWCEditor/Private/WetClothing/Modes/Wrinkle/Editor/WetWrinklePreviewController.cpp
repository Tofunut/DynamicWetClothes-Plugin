// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Wrinkle/Editor/WetWrinklePreviewController.h"

#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleElementListPanel.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleViewport.h"

void FWetWrinklePreviewController::AttachViewport(const TSharedPtr<SWetWrinkleViewport>& InViewport)
{
    Viewport = InViewport;
}

void FWetWrinklePreviewController::DetachViewport()
{
    Viewport.Reset();
}

void FWetWrinklePreviewController::SynchronizeBrushSettings(
    const FWetWrinkleBrushSettings& Settings) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->SynchronizeBrushSettings(Settings);
    }
}

void FWetWrinklePreviewController::UpdateBrushTopology(
    const FWetWrinkleBrushSettings& Settings) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->SetBrushTopology(Settings.MaterialSlotIndex, Settings.UVChannelIndex);
        PinnedViewport->UpdateBrushPreviewSettings(Settings);
        PinnedViewport->SetPreviewWetness(Settings.PreviewWetness);
    }
}

void FWetWrinklePreviewController::UpdateBrushPreview(
    const FWetWrinkleBrushSettings& Settings) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->UpdateBrushPreviewSettings(Settings);
    }
}

void FWetWrinklePreviewController::UpdatePreviewWetness(const float PreviewWetness) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->SetPreviewWetness(PreviewWetness);
    }
}

void FWetWrinklePreviewController::UpdateElementSelection(
    const EWetWrinkleElementType ElementType,
    const FGuid&                 SelectedGuid,
    const int32                  SelectedProceduralPointIndex) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        const bool bProceduralStroke = ElementType == EWetWrinkleElementType::ProceduralRidgeStroke;
        PinnedViewport->SetSelectedProceduralStrokeGuid(bProceduralStroke ? SelectedGuid : FGuid());
        PinnedViewport->SetSelectedProceduralStrokePointIndex(
            bProceduralStroke ? SelectedProceduralPointIndex : INDEX_NONE);
    }
}

void FWetWrinklePreviewController::RefreshStoredElements(
    const bool bRebuildAccumulatedPreview) const
{
    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = Viewport.Pin())
    {
        PinnedViewport->RefreshStoredStampOverlay(bRebuildAccumulatedPreview);
    }
}
