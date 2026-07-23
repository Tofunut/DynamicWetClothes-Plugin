#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleCustomNormalPanel.h"

#include "AssetRegistry/AssetData.h"
#include "DataAssets/WetClothingAsset.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SWetWrinkleCustomNormalPanel"

namespace
{
    FWetWrinkleRuntimeNormalSource* FindExactRuntimeSource(
        UWetClothingAsset* Asset,
        const int32 MaterialSlotIndex)
    {
        if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return nullptr;
        }

        const int32 UVChannelIndex = Asset->Authored.WrinkleData.WrinkleUVChannelIndex;
        return Asset->Authored.WrinkleData.RuntimeNormalSources.FindByPredicate(
            [MaterialSlotIndex, UVChannelIndex](const FWetWrinkleRuntimeNormalSource& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       Candidate.UVChannelIndex == UVChannelIndex &&
                       Candidate.LODIndex == UWetClothingAsset::RuntimeSimulationLODIndex;
            });
    }

    FWetWrinkleRuntimeNormalSource& FindOrAddExactRuntimeSource(
        UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex)
    {
        if (FWetWrinkleRuntimeNormalSource* Existing = FindExactRuntimeSource(&Asset, MaterialSlotIndex))
        {
            return *Existing;
        }

        FWetWrinkleRuntimeNormalSource& Added = Asset.Authored.WrinkleData.RuntimeNormalSources.AddDefaulted_GetRef();
        Added.MaterialSlotIndex = MaterialSlotIndex;
        Added.UVChannelIndex = Asset.Authored.WrinkleData.WrinkleUVChannelIndex;
        Added.LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
        return Added;
    }

    bool HasRecommendedTextureSettings(const UTexture2D* Texture)
    {
        return Texture != nullptr &&
               !Texture->SRGB &&
               Texture->CompressionSettings == TC_Normalmap &&
               Texture->MipGenSettings == TMGS_NoMipmaps &&
               Texture->AddressX == TA_Clamp &&
               Texture->AddressY == TA_Clamp;
    }
}

void SWetWrinkleCustomNormalPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    MaterialSlotIndex = InArgs._MaterialSlotIndex;
    OnSettingsChanged = InArgs._OnSettingsChanged;

    PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
    PreviewBrush.ImageSize = FVector2D(256.0f, 256.0f);

    ChildSlot
    [
        SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("Heading", "Custom Wrinkle Normal Map"))
                .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(UTexture2D::StaticClass())
                .ObjectPath(this, &SWetWrinkleCustomNormalPanel::GetTextureObjectPath)
                .OnObjectChanged(this, &SWetWrinkleCustomNormalPanel::HandleTextureChanged)
                .DisplayUseSelected(true)
                .DisplayBrowse(true)
            ]

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(SBorder)
                .Padding(1.0f)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.DarkGroupBorder")))
                [
                    SNew(SScaleBox)
                    .Stretch(EStretch::ScaleToFit)
                    .StretchDirection(EStretchDirection::Both)
                    [
                        SNew(SImage)
                        .Image(this, &SWetWrinkleCustomNormalPanel::GetPreviewBrush)
                        .Visibility(this, &SWetWrinkleCustomNormalPanel::GetPreviewVisibility)
                    ]
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(STextBlock)
                .Text(this, &SWetWrinkleCustomNormalPanel::GetTextureInfoText)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(this, &SWetWrinkleCustomNormalPanel::GetStatusText)
                .ColorAndOpacity(this, &SWetWrinkleCustomNormalPanel::GetStatusColor)
                .AutoWrapText(true)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Browse", "Browse"))
                    .IsEnabled(this, &SWetWrinkleCustomNormalPanel::CanUseTextureCommands)
                    .OnClicked(this, &SWetWrinkleCustomNormalPanel::HandleBrowseClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("Open", "Open"))
                    .IsEnabled(this, &SWetWrinkleCustomNormalPanel::CanUseTextureCommands)
                    .OnClicked(this, &SWetWrinkleCustomNormalPanel::HandleOpenClicked)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(LOCTEXT("FixSettings", "Fix Texture Settings"))
                    .IsEnabled(this, &SWetWrinkleCustomNormalPanel::CanUseTextureCommands)
                    .OnClicked(this, &SWetWrinkleCustomNormalPanel::HandleFixSettingsClicked)
                ]
            ]
        ]
    ];

    Refresh();
}

void SWetWrinkleCustomNormalPanel::Refresh()
{
    UTexture2D* Texture = ResolveTexture();
    PreviewBrush.SetResourceObject(Texture);
    PreviewBrush.ImageSize = Texture != nullptr
                                 ? FVector2D(FMath::Max(Texture->GetSizeX(), 1), FMath::Max(Texture->GetSizeY(), 1))
                                 : FVector2D(256.0f, 256.0f);
    Invalidate(EInvalidateWidgetReason::Paint | EInvalidateWidgetReason::Layout);
}

FString SWetWrinkleCustomNormalPanel::GetTextureObjectPath() const
{
    return GetPathNameSafe(ResolveTexture());
}

void SWetWrinkleCustomNormalPanel::HandleTextureChanged(const FAssetData& AssetData)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 SlotIndex = MaterialSlotIndex.Get(INDEX_NONE);
    if (Asset == nullptr || SlotIndex == INDEX_NONE)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("SetCustomNormalTransaction", "Set Custom Wrinkle Normal Map"));
    Asset->Modify();
    FWetWrinkleRuntimeNormalSource& Source = FindOrAddExactRuntimeSource(*Asset, SlotIndex);
    Source.Source = EDWCWrinkleNormalSource::CustomTexture;
    Source.CustomWrinkleNormalMap = Cast<UTexture2D>(AssetData.GetAsset());
    Asset->MarkPackageDirty();
    Refresh();
    OnSettingsChanged.ExecuteIfBound();
}

const FSlateBrush* SWetWrinkleCustomNormalPanel::GetPreviewBrush() const
{
    return &PreviewBrush;
}

EVisibility SWetWrinkleCustomNormalPanel::GetPreviewVisibility() const
{
    return ResolveTexture() != nullptr ? EVisibility::Visible : EVisibility::Hidden;
}

FText SWetWrinkleCustomNormalPanel::GetStatusText() const
{
    UTexture2D* Texture = ResolveTexture();
    if (MaterialSlotIndex.Get(INDEX_NONE) == INDEX_NONE)
    {
        return LOCTEXT("NoSlot", "Select a single material slot.");
    }
    if (Texture == nullptr)
    {
        return LOCTEXT("MissingTexture", "Missing Custom Wrinkle Normal Map.");
    }
    return HasRecommendedTextureSettings(Texture)
               ? LOCTEXT("Ready", "Texture Status: Ready")
               : LOCTEXT("SettingsWarning", "Texture Status: Settings do not match the DWC packed wrinkle normal contract.");
}

FSlateColor SWetWrinkleCustomNormalPanel::GetStatusColor() const
{
    UTexture2D* Texture = ResolveTexture();
    if (Texture == nullptr)
    {
        return FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f));
    }
    return HasRecommendedTextureSettings(Texture)
               ? FSlateColor(FLinearColor(0.25f, 0.9f, 0.35f))
               : FSlateColor(FLinearColor(1.0f, 0.75f, 0.15f));
}

FText SWetWrinkleCustomNormalPanel::GetTextureInfoText() const
{
    const UTexture2D* Texture = ResolveTexture();
    return Texture != nullptr
               ? FText::Format(LOCTEXT("TextureInfo", "{0} x {1} | UV {2}"),
                     FText::AsNumber(Texture->GetSizeX()),
                     FText::AsNumber(Texture->GetSizeY()),
                     FText::AsNumber(WetClothingAsset.IsValid() ? WetClothingAsset->Authored.WrinkleData.WrinkleUVChannelIndex : 0))
               : FText::GetEmpty();
}

FReply SWetWrinkleCustomNormalPanel::HandleBrowseClicked()
{
    if (UTexture2D* Texture = ResolveTexture(); Texture != nullptr && GEditor != nullptr)
    {
        TArray<UObject*> ObjectsToSync;
        ObjectsToSync.Add(Texture);
        GEditor->SyncBrowserToObjects(ObjectsToSync);
    }
    return FReply::Handled();
}

FReply SWetWrinkleCustomNormalPanel::HandleOpenClicked()
{
    if (UTexture2D* Texture = ResolveTexture(); Texture != nullptr && GEditor != nullptr)
    {
        GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Texture);
    }
    return FReply::Handled();
}

FReply SWetWrinkleCustomNormalPanel::HandleFixSettingsClicked()
{
    UTexture2D* Texture = ResolveTexture();
    if (Texture == nullptr)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("FixCustomTextureTransaction", "Fix Custom Wrinkle Normal Texture Settings"));
    Texture->Modify();
    Texture->SRGB = false;
    Texture->CompressionSettings = TC_Normalmap;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->PostEditChange();
    Texture->MarkPackageDirty();
    Refresh();
    OnSettingsChanged.ExecuteIfBound();
    return FReply::Handled();
}

bool SWetWrinkleCustomNormalPanel::CanUseTextureCommands() const
{
    return ResolveTexture() != nullptr;
}

UTexture2D* SWetWrinkleCustomNormalPanel::ResolveTexture() const
{
    const FWetWrinkleRuntimeNormalSource* Source =
        FindExactRuntimeSource(WetClothingAsset.Get(), MaterialSlotIndex.Get(INDEX_NONE));
    return Source != nullptr ? Source->CustomWrinkleNormalMap.Get() : nullptr;
}

#undef LOCTEXT_NAMESPACE
