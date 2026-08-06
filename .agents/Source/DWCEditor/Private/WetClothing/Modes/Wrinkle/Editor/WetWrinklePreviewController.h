#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleHitData.h"

class SWetWrinkleViewport;
enum class EWetWrinkleElementType : uint8;

class FWetWrinklePreviewController
{
  public:
    void AttachViewport(const TSharedPtr<SWetWrinkleViewport>& InViewport);
    void DetachViewport();

    void SynchronizeBrushSettings(const FWetWrinkleBrushSettings& Settings) const;
    void UpdateBrushTopology(const FWetWrinkleBrushSettings& Settings) const;
    void UpdateBrushPreview(const FWetWrinkleBrushSettings& Settings) const;
    void UpdatePreviewWetness(float PreviewWetness) const;
    void UpdateElementSelection(
        EWetWrinkleElementType ElementType,
        const FGuid& SelectedGuid,
        int32 SelectedProceduralPointIndex) const;
    void RefreshStoredElements(bool bRebuildAccumulatedPreview) const;

  private:
    TWeakPtr<SWetWrinkleViewport> Viewport;
};
