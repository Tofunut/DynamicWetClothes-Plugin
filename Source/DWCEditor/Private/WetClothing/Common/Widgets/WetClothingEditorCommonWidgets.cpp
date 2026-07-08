#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Core/DWCEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Common/Widgets/SWetClothingMaterialSlotPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

namespace
{
    TArray<FWetClothingAssetUVTriangle> BuildMaterialSlotPreviewTriangles(const USkeletalMesh* SkeletalMesh, int32 MaterialSlotIndex)
    {
        TArray<FWetClothingAssetUVTriangle> PreviewTriangles;
        if (SkeletalMesh == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return PreviewTriangles;
        }

        TArray<FWetClothingAssetUVIsland> BuiltIslands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(SkeletalMesh, 0, 0, MaterialSlotIndex, BuiltIslands, nullptr))
        {
            return PreviewTriangles;
        }

        for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
        {
            PreviewTriangles.Append(Island.UVTriangles);
        }

        return PreviewTriangles;
    }

    FText GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry)
    {
        const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
        if (!TrimmedName.IsEmpty())
        {
            return FText::FromString(TrimmedName);
        }

        return Entry.WetPartID == 0
                   ? NSLOCTEXT("WetClothingEditorCommonWidgets", "DefaultWetPartName", "Part Default")
                   : FText::Format(NSLOCTEXT("WetClothingEditorCommonWidgets", "NumberedWetPartName", "Part {0}"), FText::AsNumber(Entry.WetPartID));
    }
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail)
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(STextBlock)
                       .Text(Title)
                       .Font(SectionHeadingFont)];

    if (Detail.IsSet())
    {
        Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
                [SNew(STextBlock)
                     .Text(Detail)];
    }

    return Header;
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildPreviewSection(
    const TSharedRef<SWidget>& PreviewContent,
    const FOnWetClothingPreviewFocusClicked& OnFocusClicked,
    TSharedPtr<SWidget> ExtraToolbarContent)
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(STextBlock)
                       .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "PreviewSectionTitle", "Preview"))
                       .Font(SectionHeadingFont)];

    if (ExtraToolbarContent.IsValid())
    {
        Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [ExtraToolbarContent.ToSharedRef()];
    }

    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
            [SNew(SButton)
                 .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "FocusMeshButton", "Focus Mesh"))
                 .OnClicked_Lambda([OnFocusClicked]()
                 {
                     return OnFocusClicked.IsBound() ? OnFocusClicked.Execute() : FReply::Handled();
                 })];

    return SNew(SBorder)
        .Padding(8.0f)
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                       [Header]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSeparator)
                            .Orientation(Orient_Horizontal)]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [PreviewContent]];
}

TSharedRef<ITableRow> FWetClothingEditorCommonWidgets::GenerateMaterialSlotRow(
    TSharedPtr<FWetClothingMaterialSlotItem> Item,
    const TSharedRef<STableViewBase>& OwnerTable,
    const FWetClothingMaterialSlotRowArgs& Args)
{
    UMaterialInterface* MaterialObject = Item.IsValid() ? Item->Material.Get() : nullptr;
    const int32 MaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    const FText SlotTitle = Item.IsValid() && MaterialSlotIndex == INDEX_NONE
                                ? NSLOCTEXT("WetClothingEditorCommonWidgets", "AllMaterialSlotsTitle", "All Slots")
                                : Item.IsValid()
                                ? FText::Format(
                                      NSLOCTEXT("WetClothingEditorCommonWidgets", "MaterialSlotThumbnailTitle", "[{0}] {1}"),
                                      FText::AsNumber(MaterialSlotIndex),
                                      FText::FromName(Item->SlotName))
                                : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidMaterialSlotTitle", "Invalid Material Slot");

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (MaterialObject != nullptr && Args.ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(MaterialObject, 48, 48, Args.ThumbnailPool);
        if (Args.ThumbnailSink != nullptr)
        {
            Args.ThumbnailSink->Add(Thumbnail);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    UTexture* SlotPreviewTexture = Args.OverridePreviewTexture != nullptr && MaterialSlotIndex == Args.SelectedMaterialSlotIndex
                                       ? Args.OverridePreviewTexture
                                       : FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(MaterialObject);

    TArray<FWetClothingAssetUVTriangle> SlotPreviewTriangles = BuildMaterialSlotPreviewTriangles(Args.TargetMesh, MaterialSlotIndex);
    TSharedRef<SWidget> SlotPreviewWidget =
        SNew(SBorder)
        .Padding(0.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
            [SNew(SWetClothingMaterialSlotPreview)
                 .Triangles(MoveTemp(SlotPreviewTriangles))
                 .PreviewTexture(SlotPreviewTexture)];

    const bool bWettable = Item.IsValid() && Item->bIsWettableSlot;
    const FSlateColor WettableColor = bWettable
                                           ? FSlateColor(FLinearColor(0.35f, 0.85f, 1.0f, 1.0f))
                                           : FSlateColor(FLinearColor(1.0f, 0.36f, 0.36f, 1.0f));
    const FName WettableBrushName = bWettable
                                        ? TEXT("DWCEditor.Part.IsWettable.True")
                                        : TEXT("DWCEditor.Part.IsWettable.False");

    return SNew(STableRow<TSharedPtr<FWetClothingMaterialSlotItem>>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [ThumbnailWidget]]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [SlotPreviewWidget]]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(SlotTitle)
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                       [SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                            .ContentPadding(FMargin(4.0f, 2.0f))
                            .ToolTipText(NSLOCTEXT("WetClothingEditorCommonWidgets", "WettableSlotTooltip", "Toggle whether this material slot can be wetted."))
                            .OnClicked_Lambda([OnClicked = Args.OnWettableSlotClicked, MaterialSlotIndex]()
                            {
                                return OnClicked.IsBound() ? OnClicked.Execute(MaterialSlotIndex) : FReply::Handled();
                            })
                                [SNew(SBox)
                                     .WidthOverride(30.0f)
                                     .HeightOverride(30.0f)
                                     .HAlign(HAlign_Center)
                                     .VAlign(VAlign_Center)
                                         [SNew(SImage)
                                              .DesiredSizeOverride(FVector2D(30.0f, 30.0f))
                                              .Image(FDWCEditorStyle::GetBrush(WettableBrushName))
                                              .ColorAndOpacity(WettableColor)]]]];
}

TSharedRef<ITableRow> FWetClothingEditorCommonWidgets::GeneratePartMapRow(
    TSharedPtr<FWetClothingWetPartEntry> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor Color = Item.IsValid() ? (Item->WetPartID == 0 ? FLinearColor::White : Item->Color) : FLinearColor::White;
    const FText DisplayName = Item.IsValid() ? GetWetPartDisplayName(*Item) : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidWetPartName", "Invalid Part");
    const FText IDText = Item.IsValid()
                             ? FText::Format(NSLOCTEXT("WetClothingEditorCommonWidgets", "WetPartRowIDLabel", "ID {0}"), FText::AsNumber(Item->WetPartID))
                             : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidWetPartIDLabel", "Invalid");

    return SNew(STableRow<TSharedPtr<FWetClothingWetPartEntry>>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(20.0f)
                            .HeightOverride(20.0f)
                                [SNew(SBorder)
                                     .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                     .BorderBackgroundColor(Color)]]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(DisplayName)
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(IDText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]];
}

bool FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    const FWetClothingWettableMaterialSlotState* State =
        WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

    return State != nullptr && State->bIsWettableSlot;
}

void FWetClothingEditorCommonWidgets::SetMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex, bool bIsWettableSlot)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    WetClothingAsset->Modify();
    FWetClothingWettableMaterialSlotState* State =
        WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

    if (State == nullptr)
    {
        State = &WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.AddDefaulted_GetRef();
        State->MaterialSlotIndex = MaterialSlotIndex;
    }

    State->bIsWettableSlot = bIsWettableSlot;
    WetClothingAsset->MarkPackageDirty();
}

void FWetClothingEditorCommonWidgets::MarkMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    SetMaterialSlotWettable(WetClothingAsset, MaterialSlotIndex, true);
}
