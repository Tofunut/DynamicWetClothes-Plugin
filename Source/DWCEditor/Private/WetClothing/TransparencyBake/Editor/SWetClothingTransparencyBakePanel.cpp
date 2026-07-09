#include "WetClothing/TransparencyBake/Editor/SWetClothingTransparencyBakePanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetData.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/DWCBakeLayer.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/BakeSource/DWCBakeBlueprintSnapshotResolver.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCTransparencyAssetBakeService.h"
#include "WetClothing/TransparencyBake/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyBakePanel"

void SWetClothingTransparencyBakePanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

    RevealSourceTypeItems.Reset();
    RevealSourceTypeItems.Add(MakeShared<EDWCTransparencyRevealSourceType>(EDWCTransparencyRevealSourceType::MeshRaycast));
    RevealSourceTypeItems.Add(MakeShared<EDWCTransparencyRevealSourceType>(EDWCTransparencyRevealSourceType::ManualInnerTexture));
    RevealSourceTypeItems.Add(MakeShared<EDWCTransparencyRevealSourceType>(EDWCTransparencyRevealSourceType::FallbackColor));
    SelectedRevealSourceTypeItem = RevealSourceTypeItems[0];

    RefreshFromAsset();
    RebuildEditorLayout();
}

void SWetClothingTransparencyBakePanel::RebuildEditorLayout()
{
    ChildSlot
        [SNew(SSplitter)

         + SSplitter::Slot()
               .Value(0.30f)
                   [BuildControlPanel()]

         + SSplitter::Slot()
               .Value(0.70f)
                   [BuildTransparencyPreviewSection()]];
}

void SWetClothingTransparencyBakePanel::RefreshFromAsset()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        StatusMessage = TEXT("No Wet Clothing Asset.");
        InnerSourceStatusMessage = TEXT("No Wet Clothing Asset.");
        return;
    }

    if (Asset->TargetMesh == nullptr)
    {
        StatusMessage = TEXT("Assign a TargetMesh before building Transparency.");
        UpdateInnerSourceStatus();
        return;
    }

    if (RevealSourceType != EDWCTransparencyRevealSourceType::MeshRaycast)
    {
        StatusMessage = RevealSourceType == EDWCTransparencyRevealSourceType::ManualInnerTexture
                            ? TEXT("Manual Inner Texture is a UI placeholder. Mesh Raycast is still required for the Bake Maps build step.")
                            : TEXT("Fallback Color is a UI placeholder. Mesh Raycast is still required for the Bake Maps build step.");
        UpdateInnerSourceStatus();
        return;
    }

    if (Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        StatusMessage = TEXT("Assign a Source Blueprint that contains exactly one DWC Bake Component.");
        UpdateInnerSourceStatus();
        return;
    }

    StatusMessage = FString::Printf(
        TEXT("Ready. Existing baked reveal layers: %d"),
        Asset->TransparencyData.BakedRevealLayers.Num());
    UpdateInnerSourceStatus();
}

bool SWetClothingTransparencyBakePanel::HasPendingTransparencySetup(FString* OutSummary) const
{
    return FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(WetClothingAsset.Get(), OutSummary);
}

bool SWetClothingTransparencyBakePanel::BuildTransparencySetup(FString& OutSummary, bool* OutHadWarnings)
{
    const bool bSucceeded = FDWCTransparencyAssetBakeService::BuildTransparencySetup(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
    StatusMessage = OutSummary;
    UpdateInnerSourceStatus();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RebuildEditorLayout();
    return bSucceeded;
}

bool SWetClothingTransparencyBakePanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

const UClass* SWetClothingTransparencyBakePanel::GetSelectedSourceClass() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        return nullptr;
    }

    return Asset->TransparencyData.SourceBlueprintClass.LoadSynchronous();
}

void SWetClothingTransparencyBakePanel::HandleSourceClassChanged(const UClass* NewClass)
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
    RebuildEditorLayout();
}

FReply SWetClothingTransparencyBakePanel::HandleBuildAndSaveClicked()
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

FReply SWetClothingTransparencyBakePanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRevealMapPreviewClicked(UObject* Texture, const FText Label, const FText Detail)
{
    if (Texture == nullptr || !ThumbnailPool.IsValid())
    {
        return FReply::Handled();
    }

    TSharedPtr<FAssetThumbnail> LargeThumbnail = MakeShared<FAssetThumbnail>(Texture, 512, 512, ThumbnailPool);
    ActiveThumbnails.Add(LargeThumbnail);

    FAssetThumbnailConfig ThumbnailConfig;
    ThumbnailConfig.bAllowFadeIn = false;

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::Format(LOCTEXT("RevealMapPreviewWindowTitle", "{0} Preview"), Label))
        .ClientSize(FVector2D(620.0f, 680.0f))
        .SupportsMaximize(true)
        .SupportsMinimize(false)
        [SNew(SBorder)
            .Padding(16.0f)
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                [SNew(SVerticalBox)
                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                           [SNew(STextBlock)
                                .Text(FText::Format(LOCTEXT("RevealMapPreviewLargeTitle", "{0} - {1}"), Label, FText::FromString(Texture->GetName())))
                                .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
                                .AutoWrapText(true)]
                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                           [SNew(STextBlock)
                                .Text(Detail)
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                .AutoWrapText(true)]
                 + SVerticalBox::Slot()
                       .FillHeight(1.0f)
                       .HAlign(HAlign_Center)
                       .VAlign(VAlign_Center)
                           [SNew(SBox)
                                .WidthOverride(512.0f)
                                .HeightOverride(512.0f)
                                    [LargeThumbnail->MakeThumbnailWidget(ThumbnailConfig)]]
                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                           [SNew(STextBlock)
                                .Text(LOCTEXT("RevealMapPreviewHint", "This is a generated preview thumbnail. Open the texture asset for channel-level inspection."))
                                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                .AutoWrapText(true)]]];

    FSlateApplication::Get().AddWindow(Window);
    return FReply::Handled();
}

FText SWetClothingTransparencyBakePanel::GetStatusText() const
{
    return FText::FromString(StatusMessage);
}

FText SWetClothingTransparencyBakePanel::GetBuildButtonText() const
{
    return LOCTEXT("BuildAndSave", "Build & Save");
}

FText SWetClothingTransparencyBakePanel::GetTargetMeshText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return FText::FromString(Asset != nullptr && Asset->TargetMesh != nullptr ? Asset->TargetMesh->GetName() : TEXT("None"));
}

FText SWetClothingTransparencyBakePanel::GetInnerSourceStatusText() const
{
    return FText::FromString(InnerSourceStatusMessage);
}

FText SWetClothingTransparencyBakePanel::GetRevealSourceTypeLabel(const EDWCTransparencyRevealSourceType SourceType) const
{
    switch (SourceType)
    {
    case EDWCTransparencyRevealSourceType::ManualInnerTexture:
        return LOCTEXT("RevealSourceTypeManualTexture", "Manual Inner Texture");
    case EDWCTransparencyRevealSourceType::FallbackColor:
        return LOCTEXT("RevealSourceTypeFallbackColor", "Fallback Color");
    case EDWCTransparencyRevealSourceType::MeshRaycast:
    default:
        return LOCTEXT("RevealSourceTypeMeshRaycast", "Mesh Raycast");
    }
}

FText SWetClothingTransparencyBakePanel::GetRevealSourceTypeText() const
{
    return GetRevealSourceTypeLabel(RevealSourceType);
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateRevealSourceTypeComboItem(TSharedPtr<EDWCTransparencyRevealSourceType> Item) const
{
    return SNew(STextBlock)
        .Text(GetRevealSourceTypeLabel(Item.IsValid() ? *Item : EDWCTransparencyRevealSourceType::MeshRaycast));
}

void SWetClothingTransparencyBakePanel::HandleRevealSourceTypeChanged(TSharedPtr<EDWCTransparencyRevealSourceType> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedRevealSourceTypeItem = Item;
    RevealSourceType = *Item;
    RefreshFromAsset();
    RebuildEditorLayout();
}

FString SWetClothingTransparencyBakePanel::GetManualInnerColorTexturePath() const
{
    return GetPathNameSafe(ManualInnerColorTexture.Get());
}

FString SWetClothingTransparencyBakePanel::GetManualInnerMaskTexturePath() const
{
    return GetPathNameSafe(ManualInnerMaskTexture.Get());
}

void SWetClothingTransparencyBakePanel::HandleManualInnerColorTextureChanged(const FAssetData& AssetData)
{
    ManualInnerColorTexture = Cast<UTexture2D>(AssetData.GetAsset());
    RefreshFromAsset();
    RebuildEditorLayout();
}

void SWetClothingTransparencyBakePanel::HandleManualInnerMaskTextureChanged(const FAssetData& AssetData)
{
    ManualInnerMaskTexture = Cast<UTexture2D>(AssetData.GetAsset());
    RefreshFromAsset();
    RebuildEditorLayout();
}

float SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent() const
{
    return WetnessPreviewPercent;
}

void SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged(const float InValue)
{
    WetnessPreviewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
    }
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsPreviewModeChecked(const EWetClothingTransparencyPreviewMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewMode() == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandlePreviewModeChanged(const ECheckBoxState NewState, const EWetClothingTransparencyPreviewMode Mode)
{
    if (NewState == ECheckBoxState::Checked && PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(Mode);
    }
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealMapTypeChecked(const EDWCTransparencyRevealMapType MapType) const
{
    return SelectedRevealMapType == MapType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealMapTypeChanged(const ECheckBoxState NewState, const EDWCTransparencyRevealMapType MapType)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SelectedRevealMapType = MapType;
        RebuildEditorLayout();
    }
}

bool SWetClothingTransparencyBakePanel::IsBuildEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return RevealSourceType == EDWCTransparencyRevealSourceType::MeshRaycast
        && Asset != nullptr
        && Asset->TargetMesh != nullptr
        && !Asset->TransparencyData.SourceBlueprintClass.IsNull();
}

void SWetClothingTransparencyBakePanel::UpdateInnerSourceStatus()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        InnerSourceStatusMessage = TEXT("No Wet Clothing Asset.");
        return;
    }

    if (RevealSourceType == EDWCTransparencyRevealSourceType::ManualInnerTexture)
    {
        InnerSourceStatusMessage = TEXT("Manual texture source placeholder. Assign an inner color texture and optional mask here. Build integration will be added separately.");
        return;
    }

    if (RevealSourceType == EDWCTransparencyRevealSourceType::FallbackColor)
    {
        InnerSourceStatusMessage = TEXT("Fallback color source placeholder. This is for characters with no inner mesh or texture.");
        return;
    }

    if (Asset->TargetMesh == nullptr)
    {
        InnerSourceStatusMessage = TEXT("Assign a TargetMesh to inspect inner source layers.");
        return;
    }

    TSubclassOf<AActor> BlueprintClass = Asset->TransparencyData.SourceBlueprintClass.LoadSynchronous();
    if (BlueprintClass == nullptr)
    {
        InnerSourceStatusMessage = TEXT("Assign a Source Blueprint to inspect inner mesh layers.");
        return;
    }

    FDWCBakeSnapshot Snapshot;
    FString ErrorMessage;
    if (!FDWCBakeBlueprintSnapshotResolver::BuildSnapshot(BlueprintClass, Snapshot, ErrorMessage))
    {
        InnerSourceStatusMessage = FString::Printf(TEXT("Unable to inspect Source Blueprint: %s"), *ErrorMessage);
        return;
    }

    const FDWCBakeResolvedLayer* TargetLayer = nullptr;
    for (const FDWCBakeResolvedLayer& Layer : Snapshot.Layers)
    {
        if (Layer.bCanBeWetOuterLayer && Layer.SkeletalMesh == Asset->TargetMesh)
        {
            TargetLayer = &Layer;
            break;
        }
    }

    if (TargetLayer == nullptr)
    {
        InnerSourceStatusMessage = FString::Printf(
            TEXT("No wet outer layer was found for TargetMesh '%s'."),
            *GetNameSafe(Asset->TargetMesh.Get()));
        return;
    }

    TArray<FString> SourceLayerNames;
    for (const FDWCBakeResolvedLayer& Layer : Snapshot.Layers)
    {
        if (Layer.LayerOrder < TargetLayer->LayerOrder && (Layer.bCanBeRevealSource || Layer.bBlocksReveal))
        {
            SourceLayerNames.Add(Layer.LayerId.ToString());
        }
    }

    if (SourceLayerNames.Num() == 0)
    {
        InnerSourceStatusMessage = TEXT("No inner reveal source mesh was detected. Switch to Manual Inner Texture or Fallback Color when this case is supported.");
        return;
    }

    InnerSourceStatusMessage = FString::Printf(
        TEXT("Inner meshes found: %d\n%s"),
        SourceLayerNames.Num(),
        *FString::Join(SourceLayerNames, TEXT(", ")));
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildControlPanel()
{
    ActiveThumbnails.Reset();

    return SNew(SBorder)
        .Padding(16.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SScrollBox)

             + SScrollBox::Slot()
                 [SNew(SVerticalBox)

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 18.0f)
                            [BuildTargetSection()]

                  + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 18.0f)
                            [BuildInnerSourceSection()]

                  + SVerticalBox::Slot()
                        .AutoHeight()
                            [BuildGeneratedOutputsSection()]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTargetSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [FWetClothingEditorCommonWidgets::BuildSectionHeader(LOCTEXT("TargetSection", "Target"))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("RevealSourceTypeLabel", "Reveal Source Type"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                  [SNew(SComboBox<TSharedPtr<EDWCTransparencyRevealSourceType>>)
                       .OptionsSource(&RevealSourceTypeItems)
                       .InitiallySelectedItem(SelectedRevealSourceTypeItem)
                       .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateRevealSourceTypeComboItem)
                       .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealSourceTypeChanged)
                           [SNew(STextBlock)
                                .Text(this, &SWetClothingTransparencyBakePanel::GetRevealSourceTypeText)]]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("SourceBlueprintLabel", "Source Blueprint"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                  [SNew(SClassPropertyEntryBox)
                       .MetaClass(AActor::StaticClass())
                       .AllowAbstract(false)
                       .AllowNone(true)
                       .IsEnabled_Lambda([this]() { return RevealSourceType == EDWCTransparencyRevealSourceType::MeshRaycast; })
                       .SelectedClass(this, &SWetClothingTransparencyBakePanel::GetSelectedSourceClass)
                       .OnSetClass(this, &SWetClothingTransparencyBakePanel::HandleSourceClassChanged)]

        + SVerticalBox::Slot()
              .AutoHeight()
                  [BuildTargetMeshSection()];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildInnerSourceSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [FWetClothingEditorCommonWidgets::BuildSectionHeader(LOCTEXT("InnerSourceSection", "Inner Source"))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [SNew(SBorder)
                       .Padding(8.0f)
                       .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                           [SNew(STextBlock)
                                .AutoWrapText(true)
                                .Text(this, &SWetClothingTransparencyBakePanel::GetInnerSourceStatusText)]];

    if (RevealSourceType == EDWCTransparencyRevealSourceType::ManualInnerTexture)
    {
        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [SNew(STextBlock)
                     .Text(LOCTEXT("ManualInnerColorTextureLabel", "Inner Color Texture"))
                     .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))];

        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [SNew(SObjectPropertyEntryBox)
                     .AllowedClass(UTexture2D::StaticClass())
                     .AllowClear(true)
                     .AllowCreate(false)
                     .DisplayThumbnail(true)
                     .ObjectPath_Lambda([this]()
                     {
                         return GetManualInnerColorTexturePath();
                     })
                     .OnObjectChanged(this, &SWetClothingTransparencyBakePanel::HandleManualInnerColorTextureChanged)];

        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [SNew(STextBlock)
                     .Text(LOCTEXT("ManualInnerMaskTextureLabel", "Inner Mask Texture (Optional)"))
                     .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))];

        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [SNew(SObjectPropertyEntryBox)
                     .AllowedClass(UTexture2D::StaticClass())
                     .AllowClear(true)
                     .AllowCreate(false)
                     .DisplayThumbnail(true)
                     .ObjectPath_Lambda([this]()
                     {
                         return GetManualInnerMaskTexturePath();
                     })
                     .OnObjectChanged(this, &SWetClothingTransparencyBakePanel::HandleManualInnerMaskTextureChanged)];

        Box->AddSlot()
            .AutoHeight()
                [BuildEmptyAssetRow(LOCTEXT("ManualTexturePlaceholderNote", "Placeholder only: these textures are not used by the Bake Maps build step yet."))];
    }
    else if (RevealSourceType == EDWCTransparencyRevealSourceType::FallbackColor)
    {
        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [SNew(STextBlock)
                     .Text(LOCTEXT("FallbackColorLabel", "Fallback Color"))
                     .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))];

        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [SNew(SBorder)
                     .Padding(6.0f)
                     .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                     .BorderBackgroundColor(FallbackRevealColor)
                         [SNew(SBox)
                              .HeightOverride(32.0f)]];

        Box->AddSlot()
            .AutoHeight()
                [BuildEmptyAssetRow(LOCTEXT("FallbackColorPlaceholderNote", "Placeholder only: fallback color build support will be added separately."))];
    }

    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildGeneratedOutputsSection()
{
    return SNew(SBorder)
        .Padding(FMargin(12.0f, 10.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [FWetClothingEditorCommonWidgets::BuildSectionHeader(LOCTEXT("GeneratedOutputsSection", "Generated Outputs"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingTransparencyBakePanel::GetStatusText)
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [BuildRevealMaterialSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildRevealTextureSection()]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBuildSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [FWetClothingEditorCommonWidgets::BuildSectionHeader(LOCTEXT("BuildSection", "Build"))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [SNew(STextBlock)
                       .AutoWrapText(true)
                       .Text(this, &SWetClothingTransparencyBakePanel::GetStatusText)]

        + SVerticalBox::Slot()
              .AutoHeight()
                  [SNew(SButton)
                       .Text(this, &SWetClothingTransparencyBakePanel::GetBuildButtonText)
                       .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsBuildEnabled)
                       .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleBuildAndSaveClicked)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewSettingsSection()
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
    const FSlateFontInfo PreviewLabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
    const FSlateFontInfo PreviewValueFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18);

    return SNew(SBorder)
        .Padding(FMargin(14.0f, 12.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewSettingsSectionTitle", "Preview Settings"))
                            .Font(SectionHeadingFont)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("RevealAmountPreviewLabel", "Reveal Amount"))
                                       .Font(PreviewLabelFont)]
                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text_Lambda([this]()
                                       {
                                           return FText::Format(
                                               LOCTEXT("RevealAmountPreviewPercent", "{0}% revealed"),
                                               FText::AsNumber(FMath::RoundToInt(WetnessPreviewPercent)));
                                       })
                                       .Font(PreviewValueFont)]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(SSlider)
                            .MinValue(0.0f)
                            .MaxValue(100.0f)
                            .Value(this, &SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent)
                            .OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("RevealPreviewDryLabel", "Dry / hidden"))
                                       .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("RevealPreviewWetLabel", "Fully wet / revealed"))
                                       .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                  [BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::TargetMeshOnly, LOCTEXT("TargetOnlyMode", "Target Mesh"))]
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::FullBlueprint, LOCTEXT("FullBlueprintMode", "Full BP"))]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyPreviewSection()
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("PreviewSectionTitle", "Preview"))
                       .Font(SectionHeadingFont)]
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
                  [SNew(SButton)
                       .Text(LOCTEXT("FocusMeshButton", "Focus Mesh"))
                       .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleFocusPreviewClicked)];

    return SNew(SBorder)
        .Padding(14.0f)
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 8.0f, 0.0f, 6.0f)
                       [Header]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SNew(SSeparator)
                            .Orientation(Orient_Horizontal)]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [SNew(SSplitter)
                        .Orientation(Orient_Vertical)

                        + SSplitter::Slot()
                              .Value(0.76f)
                                  [SAssignNew(PreviewViewport, SWetClothingTransparencyPreviewViewport)
                                       .WetClothingAsset(WetClothingAsset.Get())]

                        + SSplitter::Slot()
                              .Value(0.24f)
                                  [BuildPreviewSettingsSection()]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewModeButton(
    const EWetClothingTransparencyPreviewMode Mode,
    const FText& Label)
{
    return SNew(SCheckBox)
        .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
        .Type(ESlateCheckBoxType::ToggleButton)
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsPreviewModeChecked, Mode)
        .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandlePreviewModeChanged, Mode)
            [SNew(SBox)
                 .MinDesiredWidth(86.0f)
                 .HAlign(HAlign_Center)
                 .Padding(FMargin(8.0f, 4.0f))
                     [SNew(STextBlock)
                          .Text(Label)]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealMapTypeButton(
    const EDWCTransparencyRevealMapType MapType,
    const FText& Label)
{
    return SNew(SCheckBox)
        .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
        .Type(ESlateCheckBoxType::ToggleButton)
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsRevealMapTypeChecked, MapType)
        .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealMapTypeChanged, MapType)
            [SNew(SBox)
                 .MinDesiredWidth(74.0f)
                 .HAlign(HAlign_Center)
                 .Padding(FMargin(8.0f, 4.0f))
                     [SNew(STextBlock)
                          .Text(Label)]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTargetMeshSection()
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealMaterialSection()
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealTextureSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 6.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("RevealTextureLabel", "Baked Reveal Maps"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                             [BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Color, LOCTEXT("RevealMapColor", "Color"))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                             [BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Mask, LOCTEXT("RevealMapMask", "Mask"))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                             [BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Confidence, LOCTEXT("RevealMapConfidence", "Confidence"))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                             [BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Lookup, LOCTEXT("RevealMapLookup", "Lookup"))]];

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
        UObject* SelectedTexture = nullptr;
        FText MapLabel;
        switch (SelectedRevealMapType)
        {
        case EDWCTransparencyRevealMapType::Mask:
            SelectedTexture = Layer.MaskMap.Get();
            MapLabel = LOCTEXT("SelectedRevealMapMaskLabel", "Mask Map");
            break;
        case EDWCTransparencyRevealMapType::Confidence:
            SelectedTexture = Layer.ConfidenceMap.Get();
            MapLabel = LOCTEXT("SelectedRevealMapConfidenceLabel", "Confidence Map");
            break;
        case EDWCTransparencyRevealMapType::Lookup:
            SelectedTexture = Layer.LookupMap.Get();
            MapLabel = LOCTEXT("SelectedRevealMapLookupLabel", "Lookup Map");
            break;
        case EDWCTransparencyRevealMapType::Color:
        default:
            SelectedTexture = Layer.ColorMap.Get();
            MapLabel = LOCTEXT("SelectedRevealMapColorLabel", "Color Map");
            break;
        }

        const FText SlotDetail = FText::Format(
            LOCTEXT("RevealTextureSlotDetail", "Slot {0} / {1}"),
            FText::AsNumber(Layer.MaterialSlotIndex),
            FText::FromName(Layer.LayerId));

        Box->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [BuildSelectedRevealMapPreview(SelectedTexture, MapLabel, SlotDetail)];
    }

    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildSelectedRevealMapPreview(
    UObject* Asset,
    const FText& Label,
    const FText& Detail)
{
    TSharedRef<SWidget> ThumbnailWidget = SNew(SBox)
        .WidthOverride(156.0f)
        .HeightOverride(156.0f)
            [SNew(SBorder)
                 .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                     [SNullWidget::NullWidget]];

    if (Asset != nullptr && ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Asset, 156, 156, ThumbnailPool);
        ActiveThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(SBorder)
        .Padding(FMargin(6.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            [SNew(SVerticalBox)
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(FText::Format(LOCTEXT("SelectedRevealMapTitle", "{0} - {1}"), Label, FText::FromString(GetNameSafe(Asset))))
                            .AutoWrapText(true)]
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(Detail)
                            .AutoWrapText(true)
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .HAlign(HAlign_Center)
                       [SNew(SButton)
                            .IsEnabled(Asset != nullptr)
                            .ToolTipText(LOCTEXT("RevealMapPreviewClickTooltip", "Click to open a larger preview."))
                            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRevealMapPreviewClicked, Asset, Label, Detail)
                                [ThumbnailWidget]]
             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                   .HAlign(HAlign_Center)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("RevealMapPreviewClickHint", "Click to enlarge"))
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail)
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildEmptyAssetRow(const FText& Label) const
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
