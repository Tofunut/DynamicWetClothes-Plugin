// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "UObject/GCObject.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

class SWindow;
class SWetWrinkleViewport;
class UTexture2D;
class UWetClothingAsset;
class SWetWrinkleZoomableImage;

struct FWetWrinkleBrushPresetOption
{
    FText           DisplayName;
    FSoftObjectPath TexturePath;
};

class SWetWrinkleTextureGeneratorDialog : public SCompoundWidget, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleTextureGeneratorDialog) {}
    SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(int32, MaterialSlotIndex)
    SLATE_ARGUMENT(int32, UVChannelIndex)
    SLATE_ARGUMENT(int32, Resolution)
    SLATE_ARGUMENT(TArray<TSharedPtr<FWetWrinkleBrushPresetOption>>, BaseNormalOptions)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override;

    bool        WasApplied() const;
    UTexture2D* GetGeneratedNormalTexture() const;

  private:
    TSharedRef<SWidget> BuildFloatControl(
        const FText& Label,
        TAttribute<float> ValueAttribute,
        TFunction<void(float)> OnValueChanged,
        float MinValue,
        float MaxValue);
    TSharedRef<SWidget> GenerateBaseNormalComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const;
    FText               GetSelectedBaseNormalText() const;
    float               GetIntensity() const;
    float               GetDirectionDegrees() const;
    float               GetPatternScale() const;
    float               GetPatternOffsetX() const;
    float               GetPatternOffsetY() const;
    float               GetNoise() const;
    FText               GetGenerationStatusText() const;
    void                HandleBaseNormalChanged(TSharedPtr<FWetWrinkleBrushPresetOption> Item, ESelectInfo::Type SelectInfo);
    void                HandleIntensityChanged(float NewValue);
    void                HandleDirectionDegreesChanged(float NewValue);
    void                HandlePatternScaleChanged(float NewValue);
    void                HandlePatternOffsetXChanged(float NewValue);
    void                HandlePatternOffsetYChanged(float NewValue);
    void                HandleNoiseChanged(float NewValue);
    UTexture2D*         ResolveSelectedBaseNormalTexture() const;
    void                ConfigurePreviewViewport();
    void                RequestPreviewTextureRebuild();
    EActiveTimerReturnType HandlePreviewRebuildTimer(double CurrentTime, float DeltaTime);
    void                RebuildPreviewTexture();
    FReply              HandleCancelClicked();
    FReply              HandleApplyClicked();

    TWeakPtr<SWindow> ParentWindow;
    TObjectPtr<UWetClothingAsset> WetClothingAsset = nullptr;
    TSharedPtr<SWetWrinkleViewport> PreviewViewport;
    TSharedPtr<SWetWrinkleZoomableImage> GeneratedNormalImage;
    TSharedPtr<SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>> BaseNormalComboBox;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BaseNormalOptions;
    TSharedPtr<FWetWrinkleBrushPresetOption> SelectedBaseNormalOption;
    FSlateBrush GeneratedNormalBrush;
    TObjectPtr<UTexture2D> GeneratedNormalTexture = nullptr;
    TObjectPtr<UTexture2D> DisplayPreviewTexture = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    int32 Resolution = 1024;
    int32 GeneratedTextureWidth = 0;
    int32 GeneratedTextureHeight = 0;
    float Intensity = 1.0f;
    float PatternScale = 1.0f;
    FVector2D PatternOffset = FVector2D::ZeroVector;
    float DirectionDegrees = 0.0f;
    float Noise = 0.0f;
    FString LastGenerationError;
    bool bGeneratedPreviewValid = false;
    bool bPendingPreviewRebuild = false;
    bool bPreviewRebuildTimerActive = false;
    bool bApplied = false;
};
