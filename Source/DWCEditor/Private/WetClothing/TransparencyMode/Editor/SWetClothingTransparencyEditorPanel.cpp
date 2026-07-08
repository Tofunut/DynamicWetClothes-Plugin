#include "WetClothing/TransparencyMode/Editor/SWetClothingTransparencyEditorPanel.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/TransparencyMode/RevealBake/DWCTransparencyAssetBakeService.h"
#include "WetClothing/TransparencyMode/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyEditorPanel"

void SWetClothingTransparencyEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    RefreshFromAsset();

    ChildSlot
        [SNew(SSplitter)

         + SSplitter::Slot()
               .Value(0.34f)
                   [BuildControlPanel()]

         + SSplitter::Slot()
               .Value(0.66f)
                   [FWetClothingEditorCommonWidgets::BuildPreviewSection(
                       SAssignNew(PreviewViewport, SWetClothingTransparencyPreviewViewport)
                           .WetClothingAsset(WetClothingAsset.Get()),
                       FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetClothingTransparencyEditorPanel::HandleFocusPreviewClicked))]];
}

void SWetClothingTransparencyEditorPanel::RefreshFromAsset()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        StatusMessage = TEXT("No Wet Clothing Asset.");
        return;
    }

    if (Asset->TargetMesh == nullptr)
    {
        StatusMessage = TEXT("Assign a TargetMesh before building Transparency.");
        return;
    }

    if (Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        StatusMessage = TEXT("Assign a Source Blueprint that contains exactly one DWC Bake Component.");
        return;
    }

    StatusMessage = FString::Printf(
        TEXT("Ready. Existing baked reveal layers: %d"),
        Asset->TransparencyData.BakedRevealLayers.Num());
}

bool SWetClothingTransparencyEditorPanel::HasPendingTransparencySetup(FString* OutSummary) const
{
    return FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(WetClothingAsset.Get(), OutSummary);
}

bool SWetClothingTransparencyEditorPanel::BuildTransparencySetup(FString& OutSummary, bool* OutHadWarnings)
{
    const bool bSucceeded = FDWCTransparencyAssetBakeService::BuildTransparencySetup(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
    StatusMessage = OutSummary;
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
    return bSucceeded;
}

bool SWetClothingTransparencyEditorPanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

const UClass* SWetClothingTransparencyEditorPanel::GetSelectedSourceClass() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        return nullptr;
    }

    return Asset->TransparencyData.SourceBlueprintClass.LoadSynchronous();
}

void SWetClothingTransparencyEditorPanel::HandleSourceClassChanged(const UClass* NewClass)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    Asset->Modify();
    Asset->TransparencyData.SourceBlueprintClass = NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
        ? const_cast<UClass*>(NewClass)
        : nullptr;
    Asset->MarkPackageDirty();
    RefreshFromAsset();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
    Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

FReply SWetClothingTransparencyEditorPanel::HandleBuildAndSaveClicked()
{
    FString Summary;
    bool bHadWarnings = false;
    if (!BuildTransparencySetup(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    SaveTransparencySetupAssets();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply SWetClothingTransparencyEditorPanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
}

FText SWetClothingTransparencyEditorPanel::GetStatusText() const
{
    return FText::FromString(StatusMessage);
}

FText SWetClothingTransparencyEditorPanel::GetBuildButtonText() const
{
    return LOCTEXT("BuildAndSave", "Build & Save");
}

FText SWetClothingTransparencyEditorPanel::GetTargetMeshText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return FText::FromString(Asset != nullptr && Asset->TargetMesh != nullptr ? Asset->TargetMesh->GetName() : TEXT("None"));
}

float SWetClothingTransparencyEditorPanel::GetWetnessPreviewPercent() const
{
    return WetnessPreviewPercent;
}

void SWetClothingTransparencyEditorPanel::HandleWetnessPreviewChanged(const float InValue)
{
    WetnessPreviewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
    }
}

ECheckBoxState SWetClothingTransparencyEditorPanel::IsPreviewModeChecked(const EWetClothingTransparencyPreviewMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewMode() == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyEditorPanel::HandlePreviewModeChanged(const ECheckBoxState NewState, const EWetClothingTransparencyPreviewMode Mode)
{
    if (NewState == ECheckBoxState::Checked && PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(Mode);
    }
}

bool SWetClothingTransparencyEditorPanel::IsBuildEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && Asset->TargetMesh != nullptr && !Asset->TransparencyData.SourceBlueprintClass.IsNull();
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildControlPanel()
{
    ActiveThumbnails.Reset();

    return SNew(SBorder)
        .Padding(12.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SScrollBox)

             + SScrollBox::Slot()
                 [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [FWetClothingEditorCommonWidgets::BuildSectionHeader(LOCTEXT("SourceSection", "Source Blueprint"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SNew(SClassPropertyEntryBox)
                            .MetaClass(AActor::StaticClass())
                            .AllowAbstract(false)
                            .AllowNone(true)
                            .SelectedClass(this, &SWetClothingTransparencyEditorPanel::GetSelectedSourceClass)
                            .OnSetClass(this, &SWetClothingTransparencyEditorPanel::HandleSourceClassChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [BuildTargetMeshSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [BuildRevealMaterialSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [BuildRevealTextureSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("WetnessPreviewLabel", "Wet Preview"))
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(SSlider)
                                       .MinValue(0.0f)
                                       .MaxValue(100.0f)
                                       .Value(this, &SWetClothingTransparencyEditorPanel::GetWetnessPreviewPercent)
                                       .OnValueChanged(this, &SWetClothingTransparencyEditorPanel::HandleWetnessPreviewChanged)]
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text_Lambda([this]()
                                       {
                                           return FText::Format(LOCTEXT("WetnessPreviewPercent", "{0}%"), FText::AsNumber(FMath::RoundToInt(WetnessPreviewPercent)));
                                       })]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewModeLabel", "Preview Mode"))
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                  [BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::TargetMeshOnly, LOCTEXT("TargetOnlyMode", "Target Mesh"))]
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::FullBlueprint, LOCTEXT("FullBlueprintMode", "Full BP"))]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingTransparencyEditorPanel::GetStatusText)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(SButton)
                            .Text(this, &SWetClothingTransparencyEditorPanel::GetBuildButtonText)
                            .IsEnabled(this, &SWetClothingTransparencyEditorPanel::IsBuildEnabled)
                            .OnClicked(this, &SWetClothingTransparencyEditorPanel::HandleBuildAndSaveClicked)]]];
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildPreviewModeButton(
    const EWetClothingTransparencyPreviewMode Mode,
    const FText& Label)
{
    return SNew(SCheckBox)
        .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
        .Type(ESlateCheckBoxType::ToggleButton)
        .IsChecked(this, &SWetClothingTransparencyEditorPanel::IsPreviewModeChecked, Mode)
        .OnCheckStateChanged(this, &SWetClothingTransparencyEditorPanel::HandlePreviewModeChanged, Mode)
            [SNew(SBox)
                 .MinDesiredWidth(86.0f)
                 .HAlign(HAlign_Center)
                 .Padding(FMargin(8.0f, 4.0f))
                     [SNew(STextBlock)
                          .Text(Label)]];
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildTargetMeshSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("TargetMeshLabel", "Target Mesh"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                  [Asset != nullptr && Asset->TargetMesh != nullptr
                       ? BuildAssetSummaryRow(Asset->TargetMesh.Get(), FText::FromString(Asset->TargetMesh->GetName()))
                       : BuildEmptyAssetRow(LOCTEXT("NoTargetMesh", "None"))];
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildRevealMaterialSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("RevealMaterialLabel", "Reveal Material"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))];

    if (Asset == nullptr || Asset->TransparencyData.BakedRevealLayers.Num() == 0)
    {
        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [BuildEmptyAssetRow(LOCTEXT("NoRevealMaterials", "Build Transparency to generate reveal materials."))];
        return Box;
    }

    for (const FWetClothingBakedTransparencyRevealLayer& Layer : Asset->TransparencyData.BakedRevealLayers)
    {
        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [BuildAssetSummaryRow(
                    Layer.RevealMaterial.Get(),
                    FText::FromString(GetNameSafe(Layer.RevealMaterial.Get())),
                    FText::Format(LOCTEXT("RevealMaterialDetail", "Slot {0} / {1}"), FText::AsNumber(Layer.MaterialSlotIndex), FText::FromName(Layer.LayerId)))];
    }

    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildRevealTextureSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 6.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("RevealTextureLabel", "Reveal Textures"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))];

    if (Asset == nullptr || Asset->TransparencyData.BakedRevealLayers.Num() == 0)
    {
        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                [BuildEmptyAssetRow(LOCTEXT("NoRevealTextures", "Build Transparency to generate reveal textures."))];
        return Box;
    }

    for (const FWetClothingBakedTransparencyRevealLayer& Layer : Asset->TransparencyData.BakedRevealLayers)
    {
        const FText SlotDetail = FText::Format(LOCTEXT("RevealTextureSlotDetail", "Slot {0}"), FText::AsNumber(Layer.MaterialSlotIndex));
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [BuildAssetSummaryRow(
                Layer.LookupMap.Get(),
                FText::Format(LOCTEXT("LookupMapRowLabel", "Lookup - {0}"), FText::FromString(GetNameSafe(Layer.LookupMap.Get()))),
                SlotDetail)];
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [BuildAssetSummaryRow(
                Layer.ColorMap.Get(),
                FText::Format(LOCTEXT("ColorMapRowLabel", "Color - {0}"), FText::FromString(GetNameSafe(Layer.ColorMap.Get()))),
                SlotDetail)];
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [BuildAssetSummaryRow(
                Layer.MaskMap.Get(),
                FText::Format(LOCTEXT("MaskMapRowLabel", "Mask - {0}"), FText::FromString(GetNameSafe(Layer.MaskMap.Get()))),
                SlotDetail)];
        Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [BuildAssetSummaryRow(
                Layer.ConfidenceMap.Get(),
                FText::Format(LOCTEXT("ConfidenceMapRowLabel", "Confidence - {0}"), FText::FromString(GetNameSafe(Layer.ConfidenceMap.Get()))),
                SlotDetail)];
    }

    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail)
{
    TSharedRef<SWidget> ThumbnailWidget = SNew(SBox)
        .WidthOverride(48.0f)
        .HeightOverride(48.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                     [SNullWidget::NullWidget]];

    if (Asset != nullptr && ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Asset, 48, 48, ThumbnailPool);
        ActiveThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(SBorder)
        .Padding(FMargin(4.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            [SNew(SHorizontalBox)
             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [ThumbnailWidget]
             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                   .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                       [SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                              .AutoHeight()
                                  [SNew(STextBlock)
                                       .Text(Label)
                                       .AutoWrapText(true)]
                        + SVerticalBox::Slot()
                              .AutoHeight()
                              .Padding(0.0f, 2.0f, 0.0f, 0.0f)
                                  [SNew(STextBlock)
                                       .Text(Detail.IsEmpty() && Asset != nullptr ? FText::FromString(Asset->GetName()) : Detail)
                                       .AutoWrapText(true)
                                       .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]];
}

TSharedRef<SWidget> SWetClothingTransparencyEditorPanel::BuildEmptyAssetRow(const FText& Label) const
{
    return SNew(SBorder)
        .Padding(FMargin(8.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [SNew(STextBlock)
                 .Text(Label)
                 .AutoWrapText(true)
                 .ColorAndOpacity(FSlateColor::UseSubduedForeground())];
}

#undef LOCTEXT_NAMESPACE
