#include "SWetnessProfileEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditorPanel"

namespace
{
    const FName PreviewDropletDetailSizeMetadataKey(TEXT("DWC.Preview.DropletDetailSize"));

    float ReadFloatMetadata(
        const UWetnessProfile* Profile,
        const FName Key,
        const float DefaultValue)
    {
        UPackage* Package = Profile != nullptr ? Profile->GetOutermost() : nullptr;
        if (Package == nullptr)
        {
            return DefaultValue;
        }

        FMetaData& MetaData = Package->GetMetaData();
        const FString StoredValue = MetaData.GetValue(Profile, Key);
        if (StoredValue.IsEmpty())
        {
            return DefaultValue;
        }

        return FCString::Atof(*StoredValue);
    }

    void WriteFloatMetadata(
        UWetnessProfile* Profile,
        const FName Key,
        const float Value)
    {
        UPackage* Package = Profile != nullptr ? Profile->GetOutermost() : nullptr;
        if (Package == nullptr)
        {
            return;
        }

        Package->Modify();
        FMetaData& MetaData = Package->GetMetaData();
        MetaData.SetValue(Profile, Key, *FString::SanitizeFloat(Value));
    }

    FText FormatPreviewFloat(const float Value)
    {
        FNumberFormattingOptions Options;
        Options.MinimumFractionalDigits = 2;
        Options.MaximumFractionalDigits = 2;
        return FText::AsNumber(Value, &Options);
    }
}

void SWetnessProfileEditorPanel::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    AbsorbedDetailsView = InArgs._AbsorbedDetailsView;
    SurfaceDetailsView = InArgs._SurfaceDetailsView;
    LoadPersistedPreviewSettings();
    PreviewModeItems = {
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::Lit),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::Absorbed),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::SurfaceCoverage),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::FinalDropletCoverage),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::DropletNormal),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::DropletStampTest),
    };
    SelectedPreviewModeItem = PreviewModeItems[0];

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 10.0f, 10.0f, 6.0f)
                   [SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                          .FillWidth(1.0f)
                          .VAlign(VAlign_Center)
                              [SNew(STextBlock)
                                   .Text(LOCTEXT("PreviewHeading", "Preview"))
                                   .Font(PanelHeadingFont)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("SaveButton", "Save"))
                                   .OnClicked(this, &SWetnessProfileEditorPanel::HandleSaveClicked)]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 8.0f)
                   [BuildPreviewToolbar()]

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f, 0.0f, 10.0f, 0.0f)
                   [SNew(SBorder)
                        .Padding(0.0f)
                            [SAssignNew(PreviewViewport, SWetnessProfileViewport)
                                 .WetnessProfile(WetnessProfile.Get())]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 8.0f, 10.0f, 10.0f)
                   [BuildPreviewControlsSection()]];

    ApplyPreviewSettingsToViewport();
    RefreshFromProfile();
}

void SWetnessProfileEditorPanel::RefreshFromProfile()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshFromProfile();
    }
}

FReply SWetnessProfileEditorPanel::HandleSaveClicked()
{
    TArray<FString> ClampedValues;
    if (FWetnessProfileEditorPolicy::SanitizeProfile(WetnessProfile.Get(), &ClampedValues))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Wetness Profile values were clamped before save:\n- %s"),
            *FString::Join(ClampedValues, TEXT("\n- ")));
        RefreshDetailsViews();
        RefreshFromProfile();
    }

    DWCEditorUtils::SaveAsset(WetnessProfile.Get());
    return FReply::Handled();
}

void SWetnessProfileEditorPanel::RefreshDetailsViews()
{
    if (AbsorbedDetailsView.IsValid())
    {
        AbsorbedDetailsView->ForceRefresh();
    }
    if (SurfaceDetailsView.IsValid())
    {
        SurfaceDetailsView->ForceRefresh();
    }
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewToolbar()
{
    return SNew(SBorder)
        .Padding(FMargin(8.0f, 5.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewMeshLabel", "Mesh:"))]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(SObjectPropertyEntryBox)
                            .AllowedClass(USkeletalMesh::StaticClass())
                            .AllowClear(true)
                            .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                            .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                       [SNew(SButton)
                            .Text(LOCTEXT("UseDefaultMeshButton", "Use Default"))
                            .ToolTipText(LOCTEXT("UseDefaultMeshTooltip", "Use the preview mesh saved on this Wetness Profile."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleUseReferenceMeshClicked)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                       [SNew(SButton)
                            .Text(LOCTEXT("SaveCurrentMeshAsDefaultButton", "Set Default"))
                            .ToolTipText(LOCTEXT("SaveCurrentMeshAsDefaultTooltip", "Save the currently displayed skeletal mesh as this profile's preview mesh."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleSaveCurrentMeshAsReferenceClicked)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                       [SNew(SButton)
                            .Text(LOCTEXT("UseSphereMeshButton", "Sphere"))
                            .ToolTipText(LOCTEXT("UseSphereMeshTooltip", "Temporarily preview on the standard sphere."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleUseSphereMeshClicked)]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewControlsSection()
{
    return SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewControlsHeading", "Preview Controls"))
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildPreviewModeSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 9.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildPreviewWaterSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 9.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildPreviewDetailSizeSection()]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewWaterSection()
{
    const auto BuildAmountSlider = [](const FText& Label, const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, const TAttribute<FText>& ValueText)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                      [SNew(SBox)
                           .WidthOverride(112.0f)
                               [SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(SSlider)
                           .MinValue(0.0f)
                           .MaxValue(100.0f)
                           .Value(Value)
                           .OnValueChanged(OnChanged)]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(42.0f)
                               [SNew(STextBlock)
                                    .Justification(ETextJustify::Right)
                                    .Text(ValueText)]];
    };

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 5.0f)
                  [BuildAmountSlider(
                      LOCTEXT("PreviewAbsorbedWaterLabel", "Absorbed Water"),
                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent)),
                      FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged),
                      TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText)))];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewModeSection()
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
                  [SNew(SBox)
                       .WidthOverride(112.0f)
                           [SNew(STextBlock).Text(LOCTEXT("PreviewModeLabel", "Preview Mode"))]]
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(SComboBox<TSharedPtr<SWetnessProfileViewport::EPreviewMode>>)
                       .OptionsSource(&PreviewModeItems)
                       .InitiallySelectedItem(SelectedPreviewModeItem)
                       .OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewModeWidget)
                       .OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewModeChanged)
                       [SNew(STextBlock)
                            .Text(this, &SWetnessProfileEditorPanel::GetPreviewModeText)]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewDetailSizeSection()
{
    const auto BuildDetailSlider = [](const FText& Label, const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, const TAttribute<FText>& ValueText)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                      [SNew(SBox)
                           .WidthOverride(112.0f)
                               [SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(SSlider)
                           .MinValue(0.0f)
                           .MaxValue(4.0f)
                           .Value(Value)
                           .OnValueChanged(OnChanged)]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(42.0f)
                               [SNew(STextBlock)
                                    .Justification(ETextJustify::Right)
                                    .Text(ValueText)]];
    };

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 2.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("PreviewDetailSizeHeading", "Droplet Detail Size"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("PreviewDetailSizeHint", "Preview metadata only. This does not change the Wetness Profile render settings; it is stored on the asset package to restore this editor preview's droplet normal/mask scale."))
                       .AutoWrapText(true)
                       .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

        + SVerticalBox::Slot()
              .AutoHeight()
                  [BuildDetailSlider(
                      LOCTEXT("PreviewDropletDetailSizeLabel", "Detail Size"),
                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDropletDetailSize)),
                      FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewDropletDetailSizeChanged),
                      TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDropletDetailSizeText)))];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::GeneratePreviewModeWidget(
    TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode) const
{
    return SNew(STextBlock)
        .Text(InMode.IsValid() ? GetPreviewModeText(*InMode) : FText::GetEmpty());
}

void SWetnessProfileEditorPanel::HandlePreviewModeChanged(
    TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode,
    ESelectInfo::Type /*SelectInfo*/)
{
    if (!InMode.IsValid())
    {
        return;
    }

    SelectedPreviewModeItem = InMode;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(*InMode);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewModeText() const
{
    return SelectedPreviewModeItem.IsValid()
               ? GetPreviewModeText(*SelectedPreviewModeItem)
               : FText::GetEmpty();
}

FText SWetnessProfileEditorPanel::GetPreviewModeText(const SWetnessProfileViewport::EPreviewMode InMode) const
{
    switch (InMode)
    {
    case SWetnessProfileViewport::EPreviewMode::Lit:
        return LOCTEXT("PreviewModeLit", "Lit");
    case SWetnessProfileViewport::EPreviewMode::Absorbed:
        return LOCTEXT("PreviewModeAbsorbed", "Absorbed");
    case SWetnessProfileViewport::EPreviewMode::SurfaceCoverage:
        return LOCTEXT("PreviewModeSurfaceCoverage", "Surface Coverage");
    case SWetnessProfileViewport::EPreviewMode::FinalDropletCoverage:
        return LOCTEXT("PreviewModeFinalDropletCoverage", "Final Droplet Coverage");
    case SWetnessProfileViewport::EPreviewMode::DropletNormal:
        return LOCTEXT("PreviewModeDropletNormal", "Droplet Normal");
    case SWetnessProfileViewport::EPreviewMode::DropletStampTest:
        return LOCTEXT("PreviewModeDropletStampTest", "Droplet Stamp Test");
    default:
        return FText::GetEmpty();
    }
}

FString SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath() const
{
    USkeletalMesh* Mesh = PreviewViewport.IsValid()
                              ? PreviewViewport->GetDisplayedPreviewSkeletalMesh()
                              : nullptr;
    return Mesh != nullptr ? FSoftObjectPath(Mesh).ToString() : FString();
}

void SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged(const FAssetData& AssetData)
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetPreviewSkeletalMeshOverride(Cast<USkeletalMesh>(AssetData.GetAsset()));
}

FReply SWetnessProfileEditorPanel::HandleUseReferenceMeshClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearPreviewSkeletalMeshOverride();
    }
    return FReply::Handled();
}

FReply SWetnessProfileEditorPanel::HandleSaveCurrentMeshAsReferenceClicked()
{
#if WITH_EDITORONLY_DATA
    UWetnessProfile* Profile = WetnessProfile.Get();
    USkeletalMesh* CurrentMesh = PreviewViewport.IsValid()
                                     ? PreviewViewport->GetDisplayedPreviewSkeletalMesh()
                                     : nullptr;
    if (Profile != nullptr)
    {
        const FScopedTransaction Transaction(LOCTEXT("SetCurrentPreviewMeshAsDefault", "Set Temporary Preview Mesh as Default"));
        Profile->Modify();
        Profile->PreviewSkeletalMesh = CurrentMesh;
        PersistPreviewDetailSizes();
        Profile->MarkPackageDirty();
        RefreshDetailsViews();
    }
#endif
    return FReply::Handled();
}

FReply SWetnessProfileEditorPanel::HandleUseSphereMeshClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->UseSpherePreview();
    }
    return FReply::Handled();
}

float SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent() const
{
    return PreviewViewport.IsValid()
               ? PreviewViewport->GetPreviewAbsorbedWater() * 100.0f
               : 50.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged(const float InPercent)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewAbsorbedWater(InPercent * 0.01f);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText() const
{
    return FText::Format(
        LOCTEXT("PreviewPercentFormat", "{0}%"),
        FText::AsNumber(FMath::RoundToInt(GetPreviewAbsorbedWaterPercent())));
}

float SWetnessProfileEditorPanel::GetPreviewDropletDetailSize() const
{
    return PreviewDropletDetailSize;
}

void SWetnessProfileEditorPanel::HandlePreviewDropletDetailSizeChanged(const float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(NewValue, PreviewDropletDetailSize))
    {
        return;
    }

    PreviewDropletDetailSize = NewValue;
    PersistPreviewDetailSizes();
    ApplyPreviewSettingsToViewport();
}

FText SWetnessProfileEditorPanel::GetPreviewDropletDetailSizeText() const
{
    return FormatPreviewFloat(PreviewDropletDetailSize);
}

void SWetnessProfileEditorPanel::LoadPersistedPreviewSettings()
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    PreviewDropletDetailSize = FMath::Clamp(
        ReadFloatMetadata(Profile, PreviewDropletDetailSizeMetadataKey, 1.0f),
        0.0f,
        4.0f);
}

void SWetnessProfileEditorPanel::PersistPreviewDetailSizes()
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return;
    }

    Profile->Modify();
    WriteFloatMetadata(Profile, PreviewDropletDetailSizeMetadataKey, PreviewDropletDetailSize);
    Profile->MarkPackageDirty();
}

void SWetnessProfileEditorPanel::ApplyPreviewSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetPreviewDropletDetailSize(PreviewDropletDetailSize);
    PreviewViewport->SetPreviewSurfaceWater(1.0f);
    if (SelectedPreviewModeItem.IsValid())
    {
        PreviewViewport->SetPreviewMode(*SelectedPreviewModeItem);
    }
    PreviewViewport->SetPreviewAnimationEnabled(false);
    PreviewViewport->SetPreviewAnimationSpeed(0.0f);
}

#undef LOCTEXT_NAMESPACE
