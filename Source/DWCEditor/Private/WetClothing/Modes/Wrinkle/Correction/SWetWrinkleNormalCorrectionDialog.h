//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "DataAssets/WetWrinkleNormalTextureData.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;
class UTexture2D;
class UWetClothingAsset;

DECLARE_DELEGATE_TwoParams(FOnWetWrinkleCorrectedTextureCreated, UTexture2D*, bool);

class SWetWrinkleNormalCorrectionDialog : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleNormalCorrectionDialog) {}
    SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_ARGUMENT(UTexture2D*, SourceTexture)
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_EVENT(FOnWetWrinkleCorrectedTextureCreated, OnCorrectedTextureCreated)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

  private:
    void RebuildPreview();
    bool CreateTransientPreviewTexture(
        TStrongObjectPtr<UTexture2D>& OutTexture,
        const FWetWrinkleTexturePixelBuffer& PixelBuffer,
        bool bNormalMap) const;
    TSharedRef<SWidget> BuildPreviewCell(const FText& Label, const FSlateBrush* Brush) const;
    FReply HandleCreateClicked();
    FReply HandleRefreshPreviewClicked();
    FReply HandleCancelClicked();
    void SaveLastSettings() const;
    void CloseWindow() const;

  private:
    TWeakPtr<SWindow> ParentWindow;
    TWeakObjectPtr<UTexture2D> SourceTexture;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    FOnWetWrinkleCorrectedTextureCreated OnCorrectedTextureCreated;
    bool bUseCorrection = true;
    bool bHideOriginal = false;
    FWetWrinkleNormalCorrectionSettings CorrectionSettings;
    FWetWrinkleCoverageExtractionSettings CoverageSettings;
    FWetWrinkleNormalBuildOutput LastBuildOutput;
    bool bPreviewValid = false;

    TStrongObjectPtr<UTexture2D> SourceDeviationTexture;
    TStrongObjectPtr<UTexture2D> CorrectedNormalPreviewTexture;
    TStrongObjectPtr<UTexture2D> CorrectedDeviationTexture;
    TStrongObjectPtr<UTexture2D> ConvexSeparationTexture;
    FSlateBrush SourceNormalBrush;
    FSlateBrush SourceDeviationBrush;
    FSlateBrush CorrectedNormalBrush;
    FSlateBrush CorrectedDeviationBrush;
    FSlateBrush ConvexSeparationBrush;
    FText StatusText;
    FSlateColor StatusColor = FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f));
};
