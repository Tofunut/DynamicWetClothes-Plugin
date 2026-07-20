#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyBakePanel"

namespace
{
TSharedRef<SWidget> BuildLabeledControl(const FText& Label, const TSharedRef<SWidget>& Control)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
        + SVerticalBox::Slot().AutoHeight()[Control];
}
}

void SWetClothingTransparencyBakePanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        TransparencyPreviewStrength = Asset->TransparencyData.TransparencyPreviewStrength;
        WrinkleSuppressionStrength = FMath::Clamp(Asset->TransparencyData.WrinkleSuppressionStrength, 0.0f, 5.0f);
    }
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::Final));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::InnerColor));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::AutoAlpha));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::WrinkleSeparation));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::ValidHit));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::HitDistance));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RayConfidence));
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourcePriority));
    SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    RefreshFromAsset();
    RebuildEditorLayout();
}

void SWetClothingTransparencyBakePanel::RebuildEditorLayout()
{
    const float PreviousScrollOffset = ControlPanelScrollBox.IsValid() ? ControlPanelScrollBox->GetScrollOffset() : 0.0f;
    if (ControlPanelContainer.IsValid())
    {
        ControlPanelContainer->SetContent(BuildControlPanel());
    }

    if (ControlPanelScrollBox.IsValid())
    {
        ControlPanelScrollBox->SetScrollOffset(PreviousScrollOffset);
    }
    else
    {
        ChildSlot
            [SNew(SSplitter)
             + SSplitter::Slot().Value(0.34f)
                 [SAssignNew(ControlPanelContainer, SBox)
                     [BuildControlPanel()]]
             + SSplitter::Slot().Value(0.66f)
                 [BuildTransparencyPreviewSection()]];
    }

    if (LayerListView.IsValid())
    {
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item) { return Item.IsValid() && Item->LayerGuid == SelectedLayerGuid; });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
    RefreshViewportContext();
}

bool SWetClothingTransparencyBakePanel::RefreshOptionItems()
{
    if (SourceTypeItems.IsEmpty())
    {
        SourceTypeItems.Add(MakeShared<EDWCTransparencySourceType>(EDWCTransparencySourceType::SameMeshMaterialSlots));
        SourceTypeItems.Add(MakeShared<EDWCTransparencySourceType>(EDWCTransparencySourceType::OtherSkeletalMeshComponents));
        SourceTypeItems.Add(MakeShared<EDWCTransparencySourceType>(EDWCTransparencySourceType::ManualColorOrTexture));
    }
    if (AddressModeItems.IsEmpty())
    {
        AddressModeItems.Add(MakeShared<EDWCTransparencyUVAddressMode>(EDWCTransparencyUVAddressMode::Clamp));
        AddressModeItems.Add(MakeShared<EDWCTransparencyUVAddressMode>(EDWCTransparencyUVAddressMode::Wrap));
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* Mesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    int32 NumUVChannels = 0;
    if (Mesh != nullptr)
    {
        if (const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
            RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(0))
        {
            NumUVChannels = RenderData->LODRenderData[0].StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
        }
    }

    const int32 MaterialSlotCount = Mesh != nullptr ? Mesh->GetMaterials().Num() : 0;
    const bool bMeshOptionsChanged = OptionItemsTargetMesh.Get() != Mesh ||
        OptionItemsMaterialSlotCount != MaterialSlotCount || OptionItemsUVChannelCount != NumUVChannels;
    if (!bMeshOptionsChanged)
    {
        return false;
    }

    OptionItemsTargetMesh = const_cast<USkeletalMesh*>(Mesh);
    OptionItemsMaterialSlotCount = MaterialSlotCount;
    OptionItemsUVChannelCount = NumUVChannels;
    MaterialSlotItems.Reset();
    UVChannelItems.Reset();
    if (Mesh != nullptr)
    {
        const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
        for (int32 SlotIndex = 0; SlotIndex < Materials.Num(); ++SlotIndex)
        {
            FMaterialSlotItemPtr Item = MakeShared<FDWCTransparencyMaterialSlotItem>();
            Item->SlotIndex = SlotIndex;
            Item->SlotName = Materials[SlotIndex].MaterialSlotName;
            MaterialSlotItems.Add(Item);
        }
    }
    for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
    {
        UVChannelItems.Add(MakeShared<int32>(UVChannelIndex));
    }
    return true;
}

void SWetClothingTransparencyBakePanel::RefreshLayerItems()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    bool bMigratedLayerIdentity = false;
    if (Asset != nullptr)
    {
        for (FWetClothingTransparencyLayerData& Layer : Asset->TransparencyData.TransparencyLayers)
        {
            if (!Layer.LayerGuid.IsValid())
            {
                Layer.LayerGuid = FGuid::NewGuid();
                bMigratedLayerIdentity = true;
            }
        }
        if (bMigratedLayerIdentity)
        {
            Asset->TransparencyData.DataVersion = FWetClothingTransparencyData::CurrentDataVersion;
            Asset->MarkPackageDirty();
        }
    }

    bool bLayerItemsChanged = Asset == nullptr || LayerItems.Num() != Asset->TransparencyData.TransparencyLayers.Num();
    if (!bLayerItemsChanged && Asset != nullptr)
    {
        for (int32 LayerIndex = 0; LayerIndex < LayerItems.Num(); ++LayerIndex)
        {
            if (!LayerItems[LayerIndex].IsValid() ||
                LayerItems[LayerIndex]->LayerGuid != Asset->TransparencyData.TransparencyLayers[LayerIndex].LayerGuid)
            {
                bLayerItemsChanged = true;
                break;
            }
        }
    }
    if (bLayerItemsChanged)
    {
        LayerItems.Reset();
        if (Asset != nullptr)
        {
            for (const FWetClothingTransparencyLayerData& Layer : Asset->TransparencyData.TransparencyLayers)
            {
                FLayerItemPtr Item = MakeShared<FDWCTransparencyLayerListItem>();
                Item->LayerGuid = Layer.LayerGuid;
                LayerItems.Add(Item);
            }
        }
    }

    if (GetSelectedLayer() == nullptr)
    {
        SelectedLayerGuid = LayerItems.Num() > 0 ? LayerItems[0]->LayerGuid : FGuid();
    }
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item) { return Item.IsValid() && Item->LayerGuid == SelectedLayerGuid; });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
}

void SWetClothingTransparencyBakePanel::RefreshFromAsset()
{
    const bool bOptionItemsChanged = RefreshOptionItems();
    RefreshLayerItems();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr)
    {
        StatusMessage = TEXT("No Wet Clothing Asset.");
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Asset->GetDWCSkeletalMesh() == nullptr)
    {
        StatusMessage = TEXT("Assign a Target Skeletal Mesh before configuring Transparency.");
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Layer == nullptr)
    {
        StatusMessage = TEXT("Add a Transparency Target Part for a target material slot.");
        PanelStatus = EDWCTransparencyPanelStatus::Info;
    }
    else if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        TArray<FString> Errors;
        if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(Asset->GetDWCSkeletalMesh(), *Layer, Errors))
        {
            StatusMessage = FString::Join(Errors, TEXT("\n"));
            PanelStatus = EDWCTransparencyPanelStatus::Error;
        }
        else if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
                 Existing != nullptr && Existing->IsValid())
        {
            const FDWCTransparencyAutoBakeResult& Result = **Existing;
            StatusMessage = FString::Printf(TEXT("Auto map ready. Samples: %d, Valid Hits: %d, No Hits: %d, UV Overlaps: %d"),
                Result.OuterSampleCount, Result.ValidHitCount, Result.NoHitCount, Result.OverlappedUVPixelCount);
            PanelStatus = Result.OverlappedUVPixelCount > 0 ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
        }
        else
        {
            StatusMessage = TEXT("Ready for automatic transparency generation.");
            PanelStatus = EDWCTransparencyPanelStatus::Ready;
        }
    }
    else if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        StatusMessage = Asset->TransparencyData.SourceBlueprintClass.IsNull()
            ? TEXT("Assign a Source Blueprint containing a DWC Bake Component.")
            : TEXT("Ready to generate from other Skeletal Mesh Components.");
        PanelStatus = Asset->TransparencyData.SourceBlueprintClass.IsNull()
            ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
    }
    else
    {
        StatusMessage = TEXT("Manual Color or Texture source is not available yet.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    UpdateInnerSourceStatus();
    RefreshViewportContext();
    if (bOptionItemsChanged && ControlPanelContainer.IsValid())
    {
        RebuildEditorLayout();
    }
}

bool SWetClothingTransparencyBakePanel::HasPendingTransparencySetup(FString* OutSummary) const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        return false;
    }
    return FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(WetClothingAsset.Get(), OutSummary);
}

bool SWetClothingTransparencyBakePanel::BakeTransparencyRevealAssets(FString& OutSummary, bool* OutHadWarnings)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        OutSummary = TEXT("Automatic generation for this Inner Source Type is not implemented yet.");
        return false;
    }
    const bool bSucceeded = FDWCTransparencyAssetBakeService::BakeTransparencyRevealAssets(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
    StatusMessage = OutSummary;
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RefreshFromAsset();
    RebuildEditorLayout();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
    return bSucceeded;
}

bool SWetClothingTransparencyBakePanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

const UClass* SWetClothingTransparencyBakePanel::GetSelectedSourceClass() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->TransparencyData.SourceBlueprintClass.LoadSynchronous() : nullptr;
}

void SWetClothingTransparencyBakePanel::HandleSourceClassChanged(const UClass* NewClass)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }
    const FScopedTransaction Transaction(LOCTEXT("SetTransparencySourceBlueprint", "Set Transparency Source Blueprint"));
    Asset->Modify();
    Asset->TransparencyData.SourceBlueprintClass = NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
        ? const_cast<UClass*>(NewClass) : nullptr;
    for (FWetClothingTransparencyLayerData& Layer : Asset->TransparencyData.TransparencyLayers)
    {
        if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
        {
            Layer.MarkAutoBakeStale();
            AutoBakeResults.Remove(Layer.LayerGuid);
        }
    }
    Asset->MarkPackageDirty();
    RefreshFromAsset();
    RebuildEditorLayout();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
}

FReply SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset != nullptr && Layer != nullptr && Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        TSharedPtr<FDWCTransparencyAutoBakeResult> Result = MakeShared<FDWCTransparencyAutoBakeResult>();
        FString Summary;
        TArray<FString> Warnings;
        if (!FDWCTransparencyAutoMapGenerator::GenerateSameMesh(*Asset, *Layer, *Result, Summary, Warnings))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
            return FReply::Handled();
        }

        const FScopedTransaction Transaction(LOCTEXT("GenerateTransparencyAutoMap", "Generate Transparency Map"));
        Asset->Modify();
        Layer = GetSelectedLayer();
        if (Layer == nullptr) return FReply::Handled();
        Layer->AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
        Layer->AutoBakeMetadata.BuildSignature = Result->BuildSignature;
        Layer->AutoBakeMetadata.LODIndex = Result->LODIndex;
        Layer->AutoBakeMetadata.Resolution = Result->Resolution.X;
        Layer->AutoBakeMetadata.PaddingPixels = Asset->TransparencyData.TransparencyPaddingPixels;
        Layer->AutoBakeMetadata.ValidHitCount = Result->ValidHitCount;
        Layer->AutoBakeMetadata.NoHitCount = Result->NoHitCount;
        Layer->MarkFinalBakeStale();
        Asset->MarkPackageDirty();
        // Keep only the active layer's large CPU intermediate buffers in memory.
        AutoBakeResults.Reset();
        AutoBakeResults.Add(Layer->LayerGuid, Result);

        if (Layer->TargetSurface.OuterUVChannel != 0)
        {
            Warnings.Add(TEXT("The current 3D transparency preview material samples UV channel 0. The generated result was kept, but it will not be applied to the mesh preview until the shared Transparency UV selector is implemented."));
        }
        const FWetClothingGeneratedWetMaterialOverride* WetOverride =
            Asset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
                [Layer](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                {
                    return Candidate.MaterialSlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex &&
                           Candidate.CPUMaterialInstance != nullptr;
                });
        if (WetOverride == nullptr)
        {
            Warnings.Add(TEXT("The selected Target Part has no generated DWC wet material override. Generate or refresh wet materials before expecting the Final mesh preview."));
        }

        for (const FDWCTransparencySourceHitStats& Stats : Result->SourceStats)
        {
            Summary += FString::Printf(TEXT("\n- Priority %d: %s (Slot %d) -> %d hit(s)"), Stats.PriorityIndex,
                *Stats.MaterialSlotName.ToString(), Stats.MaterialSlotIndex, Stats.HitCount);
        }
        if (!Warnings.IsEmpty()) Summary += TEXT("\n\nWarnings:\n- ") + FString::Join(Warnings, TEXT("\n- "));
        RefreshFromAsset();
        StatusMessage = Summary;
        PanelStatus = Warnings.IsEmpty() ? EDWCTransparencyPanelStatus::Ready : EDWCTransparencyPanelStatus::Warning;
        if (DetailsView.IsValid()) DetailsView->ForceRefresh();
        FMessageDialog::Open(Warnings.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    FString Summary;
    bool bHadWarnings = false;
    if (!BakeTransparencyRevealAssets(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }
    SaveTransparencySetupAssets();
    FMessageDialog::Open(bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleBakeEditedTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("BakeEditedNoTarget", "Select a Transparency Target Part before baking."));
        return FReply::Handled();
    }

    const TSharedPtr<FDWCTransparencyAutoBakeResult>* StoredResult = AutoBakeResults.Find(Layer->LayerGuid);
    if (StoredResult == nullptr || !StoredResult->IsValid())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("BakeEditedGenerateFirst", "Run Generate Transparency Map for the selected Target Part before baking the edited map."));
        return FReply::Handled();
    }

    FString CompatibilityReason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *StoredResult->Get(), CompatibilityReason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompatibilityReason));
        return FReply::Handled();
    }

    FDWCTransparencyEditedMapBakeResult BakeResult;
    FString ErrorMessage;
    {
        const FScopedTransaction Transaction(LOCTEXT("BakeEditedTransparencyMapTransaction", "Bake Edited Transparency Map"));
        Asset->Modify();
        if (!FDWCTransparencyEditedMapBaker::Bake(*Asset, *Layer, *StoredResult->Get(), BakeResult, ErrorMessage))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
            return FReply::Handled();
        }
    }

    FString Summary = FString::Printf(
        TEXT("Transparency Map bake completed.\n\nTarget Part: %s (Slot %d)\nUV Channel: %d\nLOD: %d\nResolution: %d\nApplied Strokes: %d\nApplied Samples: %d\nTexture: %s"),
        *Layer->TargetSurface.OuterMaterialSlotName.ToString(),
        Layer->TargetSurface.OuterMaterialSlotIndex,
        Layer->TargetSurface.OuterUVChannel,
        StoredResult->Get()->LODIndex,
        StoredResult->Get()->Resolution.X,
        BakeResult.AppliedStrokeCount,
        BakeResult.AppliedSampleCount,
        *GetNameSafe(BakeResult.TransparencyMap));
    bool bHadWarnings = BakeResult.IgnoredNoHitOverridePixelCount > 0;
    if (bHadWarnings)
    {
        Summary += FString::Printf(
            TEXT("\n\nWarning:\n%d manually edited pixel(s) had no valid inner-surface color and were kept at Alpha 0."),
            BakeResult.IgnoredNoHitOverridePixelCount);
    }

    if (!SaveTransparencySetupAssets())
    {
        bHadWarnings = true;
        Summary += TEXT("\n\nWarning:\nThe generated assets remain dirty because checkout/save was canceled or failed.");
    }
    RefreshFromAsset();
    RebuildEditorLayout();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    StatusMessage = Summary;
    PanelStatus = bHadWarnings ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
    FMessageDialog::Open(
        bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::FromString(Summary));
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

FText SWetClothingTransparencyBakePanel::GetStatusText() const { return FText::FromString(StatusMessage); }
FSlateColor SWetClothingTransparencyBakePanel::GetStatusColor() const
{
    switch (PanelStatus)
    {
    case EDWCTransparencyPanelStatus::Ready: return FSlateColor(FLinearColor(0.32f, 0.80f, 0.42f));
    case EDWCTransparencyPanelStatus::Warning: return FSlateColor(FLinearColor(0.95f, 0.68f, 0.20f));
    case EDWCTransparencyPanelStatus::Error: return FSlateColor(FLinearColor(0.95f, 0.30f, 0.25f));
    default: return FSlateColor::UseSubduedForeground();
    }
}
FText SWetClothingTransparencyBakePanel::GetGenerateTooltipText() const
{
    if (IsGenerateEnabled())
    {
        return LOCTEXT("GenerateTransparencyReadyTooltip", "Generate automatic inner color and transparency alpha for the selected Target Part.");
    }
    return FText::FromString(StatusMessage);
}
FText SWetClothingTransparencyBakePanel::GetBakeEditedTooltipText() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return LOCTEXT("BakeEditedSelectTargetTooltip", "Select a Transparency Target Part first.");
    }
    if (Layer->SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        return LOCTEXT("BakeEditedSameMeshOnlyTooltip", "Edited packed-map baking currently supports Same Skeletal Mesh / Material Slots.");
    }
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = AutoBakeResults.Find(Layer->LayerGuid);
    if (Result == nullptr || !Result->IsValid())
    {
        return LOCTEXT("BakeEditedGenerateTooltip", "Run Generate Transparency Map before baking the edited map.");
    }
    FString Reason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *Result->Get(), Reason))
    {
        return FText::FromString(Reason);
    }
    return LOCTEXT("BakeEditedReadyTooltip", "Bake the automatic inner color and saved brush edits into one packed RGBA Transparency Map.");
}
FText SWetClothingTransparencyBakePanel::GetInnerSourceStatusText() const { return FText::FromString(InnerSourceStatusMessage); }
float SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent() const { return WetnessPreviewPercent; }
void SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged(float InValue)
{
    WetnessPreviewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (PreviewViewport.IsValid()) PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
}

TOptional<float> SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength() const
{
    return TransparencyPreviewStrength;
}

void SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    TransparencyPreviewStrength = FMath::Max(0.0f, InValue);
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const FScopedTransaction Transaction(LOCTEXT("SetTransparencyPreviewStrength", "Set Transparency Preview Strength"));
        Asset->Modify();
        Asset->TransparencyData.TransparencyPreviewStrength = TransparencyPreviewStrength;
        Asset->MarkPackageDirty();
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetTransparencyPreviewStrength(TransparencyPreviewStrength);
    }
}


TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength() const
{
    return WrinkleSuppressionStrength;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    WrinkleSuppressionStrength = FMath::Clamp(InValue, 0.0f, 5.0f);
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const FScopedTransaction Transaction(LOCTEXT("SetWrinkleSuppressionStrength", "Set Wrinkle Suppression Strength"));
        Asset->Modify();
        Asset->TransparencyData.WrinkleSuppressionStrength = WrinkleSuppressionStrength;
        Asset->MarkPackageDirty();
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetWrinkleSuppressionStrength(WrinkleSuppressionStrength);
    }
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsBrushModeChecked(const EDWCTransparencyBrushMode Mode) const
{
    return BrushMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleBrushModeChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyBrushMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        BrushMode = Mode;
        PushPaintSettingsToViewport();
        RebuildEditorLayout();
    }
}

TOptional<float> SWetClothingTransparencyBakePanel::GetBrushRadius() const { return BrushRadiusUV; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushStrength() const { return BrushStrength; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushFalloff() const { return BrushFalloff; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushSpacing() const { return BrushSpacing; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushTargetAlpha() const { return BrushTargetAlpha; }

void SWetClothingTransparencyBakePanel::HandleBrushRadiusCommitted(float Value, ETextCommit::Type)
{
    BrushRadiusUV = FMath::Clamp(Value, 0.0001f, 0.5f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushStrengthCommitted(float Value, ETextCommit::Type)
{
    BrushStrength = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushFalloffCommitted(float Value, ETextCommit::Type)
{
    BrushFalloff = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushSpacingCommitted(float Value, ETextCommit::Type)
{
    BrushSpacing = FMath::Clamp(Value, 0.01f, 2.0f);
    PushPaintSettingsToViewport();
}
void SWetClothingTransparencyBakePanel::HandleBrushTargetAlphaCommitted(float Value, ETextCommit::Type)
{
    BrushTargetAlpha = FMath::Clamp(Value, 0.0f, 1.0f);
    PushPaintSettingsToViewport();
}

void SWetClothingTransparencyBakePanel::PushPaintSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }
    FDWCTransparencyPaintSettings Settings;
    Settings.Mode = BrushMode;
    Settings.RadiusUV = BrushRadiusUV;
    Settings.Strength = BrushStrength;
    Settings.Falloff = BrushFalloff;
    Settings.Spacing = BrushSpacing;
    Settings.TargetAlpha = BrushTargetAlpha;
    Settings.bEnabled = true;
    PreviewViewport->SetPaintSettings(Settings);
}

void SWetClothingTransparencyBakePanel::HandleViewportStrokesChanged()
{
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RebuildEditorLayout();
}

FReply SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.IsEmpty())
    {
        return FReply::Handled();
    }
    const FScopedTransaction Transaction(LOCTEXT("RemoveLastTransparencyStroke", "Remove Last Transparency Stroke"));
    Asset->Modify();
    Layer->EditableStrokes.Pop();
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();
    if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    RebuildEditorLayout();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleClearStrokesClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.IsEmpty())
    {
        return FReply::Handled();
    }
    const FScopedTransaction Transaction(LOCTEXT("ClearTransparencyStrokes", "Clear Transparency Strokes"));
    Asset->Modify();
    Layer->EditableStrokes.Reset();
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();
    if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    RebuildEditorLayout();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleDeleteStrokeClicked(const FGuid StrokeGuid)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return FReply::Handled();
    }
    const FScopedTransaction Transaction(LOCTEXT("DeleteTransparencyStroke", "Delete Transparency Stroke"));
    Asset->Modify();
    Layer->EditableStrokes.RemoveAll([StrokeGuid](const FDWCTransparencyBrushStroke& Stroke) { return Stroke.StrokeGuid == StrokeGuid; });
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();
    if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    RebuildEditorLayout();
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleStrokeEnabledChanged(
    const ECheckBoxState NewState,
    const FGuid StrokeGuid)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return;
    }
    if (FDWCTransparencyBrushStroke* Stroke = Layer->EditableStrokes.FindByPredicate(
            [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; }))
    {
        const FScopedTransaction Transaction(LOCTEXT("ToggleTransparencyStroke", "Toggle Transparency Stroke"));
        Asset->Modify();
        Stroke->bEnabled = NewState == ECheckBoxState::Checked;
        Layer->MarkFinalBakeStale();
        Asset->MarkPackageDirty();
        if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    }
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item) const
{
    return SNew(STextBlock).Text(GetVisualizationModeLabel(
        Item.IsValid() ? *Item : EDWCTransparencyVisualizationMode::Final));
}

void SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item,
    ESelectInfo::Type)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedVisualizationMode = *Item;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetVisualizationMode(SelectedVisualizationMode);
    }
}

FText SWetClothingTransparencyBakePanel::GetVisualizationModeLabel(
    const EDWCTransparencyVisualizationMode Mode) const
{
    switch (Mode)
    {
    case EDWCTransparencyVisualizationMode::InnerColor: return LOCTEXT("TransparencyViewInnerColor", "Inner Color");
    case EDWCTransparencyVisualizationMode::AutoAlpha: return LOCTEXT("TransparencyViewAutoAlpha", "Auto Alpha");
    case EDWCTransparencyVisualizationMode::WrinkleSeparation: return LOCTEXT("TransparencyViewWrinkleSeparation", "Wrinkle Separation");
    case EDWCTransparencyVisualizationMode::ValidHit: return LOCTEXT("TransparencyViewValidHit", "Valid Hit");
    case EDWCTransparencyVisualizationMode::HitDistance: return LOCTEXT("TransparencyViewHitDistance", "Hit Distance");
    case EDWCTransparencyVisualizationMode::RayConfidence: return LOCTEXT("TransparencyViewConfidence", "Ray Confidence");
    case EDWCTransparencyVisualizationMode::SourcePriority: return LOCTEXT("TransparencyViewSourcePriority", "Source Priority");
    default: return LOCTEXT("TransparencyViewFinal", "Final");
    }
}

TSharedPtr<EDWCTransparencyVisualizationMode> SWetClothingTransparencyBakePanel::FindVisualizationModeItem(
    const EDWCTransparencyVisualizationMode Mode) const
{
    const TSharedPtr<EDWCTransparencyVisualizationMode>* Match = VisualizationModeItems.FindByPredicate(
        [Mode](const TSharedPtr<EDWCTransparencyVisualizationMode>& Item)
        {
            return Item.IsValid() && *Item == Mode;
        });
    return Match != nullptr ? *Match : nullptr;
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewMode() == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}
void SWetClothingTransparencyBakePanel::HandlePreviewModeChanged(ECheckBoxState State, EWetClothingTransparencyPreviewMode Mode)
{
    if (State == ECheckBoxState::Checked && PreviewViewport.IsValid()) PreviewViewport->SetPreviewMode(Mode);
}
ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealMapTypeChecked(EDWCTransparencyRevealMapType Type) const
{
    return SelectedRevealMapType == Type ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}
void SWetClothingTransparencyBakePanel::HandleRevealMapTypeChanged(ECheckBoxState State, EDWCTransparencyRevealMapType Type)
{
    if (State == ECheckBoxState::Checked) { SelectedRevealMapType = Type; RebuildEditorLayout(); }
}

bool SWetClothingTransparencyBakePanel::IsGenerateEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr || Layer == nullptr) return false;
    if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        TArray<FString> Errors;
        return FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(Asset->GetDWCSkeletalMesh(), *Layer, Errors);
    }
    return Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents &&
        !Asset->TransparencyData.SourceBlueprintClass.IsNull();
}
bool SWetClothingTransparencyBakePanel::IsBakeEditedEnabled() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Layer->SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = AutoBakeResults.Find(Layer->LayerGuid);
    if (Result == nullptr || !Result->IsValid())
    {
        return false;
    }
    FString Reason;
    return FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *Result->Get(), Reason);
}
bool SWetClothingTransparencyBakePanel::CanUseFullBlueprintPreview() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr && Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents;
}

void SWetClothingTransparencyBakePanel::UpdateInnerSourceStatus()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        InnerSourceStatusMessage = TEXT("Select a Transparency Target Part.");
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        int32 EnabledSlotCount = 0;
        for (const FWetClothingTransparencyInnerSlot& Slot : Layer->SameMeshSource.InnerSlotPriority)
        {
            EnabledSlotCount += Slot.bEnabled ? 1 : 0;
        }
        InnerSourceStatusMessage = FString::Printf(TEXT("Enabled inner material slots: %d"), EnabledSlotCount);
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        InnerSourceStatusMessage = TEXT("Manual source is not available yet.");
        return;
    }
    if (Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        InnerSourceStatusMessage = TEXT("Assign a Source Blueprint.");
        return;
    }
    InnerSourceStatusMessage = TEXT("Source Blueprint assigned. Its Bake Component layers will be validated when the map is generated.");
}

FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; }) : nullptr;
}
const FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; }) : nullptr;
}

SWetClothingTransparencyBakePanel::FMaterialSlotItemPtr SWetClothingTransparencyBakePanel::FindMaterialSlotItem(int32 SlotIndex) const
{
    const FMaterialSlotItemPtr* Match = MaterialSlotItems.FindByPredicate([SlotIndex](const FMaterialSlotItemPtr& Item) { return Item.IsValid() && Item->SlotIndex == SlotIndex; });
    return Match != nullptr ? *Match : nullptr;
}
TSharedPtr<int32> SWetClothingTransparencyBakePanel::FindUVChannelItem(int32 Index) const
{
    const TSharedPtr<int32>* Match = UVChannelItems.FindByPredicate([Index](const TSharedPtr<int32>& Item) { return Item.IsValid() && *Item == Index; });
    return Match != nullptr ? *Match : nullptr;
}
TSharedPtr<EDWCTransparencySourceType> SWetClothingTransparencyBakePanel::FindSourceTypeItem(EDWCTransparencySourceType Type) const
{
    const auto* Match = SourceTypeItems.FindByPredicate([Type](const auto& Item) { return Item.IsValid() && *Item == Type; });
    return Match != nullptr ? *Match : nullptr;
}
TSharedPtr<EDWCTransparencyUVAddressMode> SWetClothingTransparencyBakePanel::FindAddressModeItem(EDWCTransparencyUVAddressMode Mode) const
{
    const auto* Match = AddressModeItems.FindByPredicate([Mode](const auto& Item) { return Item.IsValid() && *Item == Mode; });
    return Match != nullptr ? *Match : nullptr;
}

void SWetClothingTransparencyBakePanel::EditSelectedLayer(const FText& Text, TFunctionRef<void(FWetClothingTransparencyLayerData&)> Edit, bool bRebuild)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr) return;
    const FScopedTransaction Transaction(Text);
    Asset->Modify();
    Edit(*Layer);
    Layer->MarkAutoBakeStale();
    AutoBakeResults.Remove(Layer->LayerGuid);
    Asset->MarkPackageDirty();
    RefreshFromAsset();
    if (bRebuild)
    {
        if (DetailsView.IsValid()) DetailsView->ForceRefresh();
        RebuildEditorLayout();
    }
    else if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
    }
}

void SWetClothingTransparencyBakePanel::EditGlobalSettings(const FText& Text, TFunctionRef<void(FWetClothingTransparencyData&)> Edit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr) return;
    const FScopedTransaction Transaction(Text);
    Asset->Modify();
    Edit(Asset->TransparencyData);
    for (FWetClothingTransparencyLayerData& Layer : Asset->TransparencyData.TransparencyLayers) Layer.MarkAutoBakeStale();
    AutoBakeResults.Reset();
    Asset->MarkPackageDirty();
    RefreshFromAsset();
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateLayerRow(FLayerItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    return SNew(STableRow<FLayerItemPtr>, Owner)
        [SNew(STextBlock).Text_Lambda([this, Item]()
        {
            const UWetClothingAsset* Asset = WetClothingAsset.Get();
            const FWetClothingTransparencyLayerData* Layer = Asset != nullptr ? Asset->TransparencyData.TransparencyLayers.FindByPredicate(
                [Item](const FWetClothingTransparencyLayerData& Candidate) { return Item.IsValid() && Candidate.LayerGuid == Item->LayerGuid; }) : nullptr;
            return Layer != nullptr ? FText::Format(LOCTEXT("TransparencyLayerRow", "{0} / Slot {1} / UV {2}"),
                FText::FromName(Layer->TargetSurface.OuterMaterialSlotName), FText::AsNumber(Layer->TargetSurface.OuterMaterialSlotIndex),
                FText::AsNumber(Layer->TargetSurface.OuterUVChannel)) : LOCTEXT("MissingTransparencyLayer", "Missing Layer");
        })];
}

void SWetClothingTransparencyBakePanel::HandleLayerSelectionChanged(FLayerItemPtr Item, ESelectInfo::Type)
{
    if (bRefreshingLayerSelection)
    {
        return;
    }
    const FGuid NewLayerGuid = Item.IsValid() ? Item->LayerGuid : FGuid();
    if (SelectedLayerGuid != NewLayerGuid)
    {
        // Full 2048 intermediate results are intentionally scoped to the active target part.
        AutoBakeResults.Reset();
    }
    SelectedLayerGuid = NewLayerGuid;
    RefreshFromAsset();
    RebuildEditorLayout();
}

FReply SWetClothingTransparencyBakePanel::HandleAddLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr) return FReply::Handled();
    int32 NewSlot = INDEX_NONE;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() && Asset->TransparencyData.FindTransparencyLayer(Item->SlotIndex, 0) == nullptr) { NewSlot = Item->SlotIndex; break; }
    }
    if (NewSlot == INDEX_NONE) { StatusMessage = TEXT("Every Material Slot already has a UV 0 Transparency Target Part."); return FReply::Handled(); }
    const FScopedTransaction Transaction(LOCTEXT("AddTransparencyLayer", "Add Transparency Target Part"));
    Asset->Modify();
    FWetClothingTransparencyLayerData& Layer = Asset->TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Asset->TransparencyData.DataVersion = FWetClothingTransparencyData::CurrentDataVersion;
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = NewSlot;
    Layer.TargetSurface.OuterMaterialSlotName = Asset->GetDWCSkeletalMesh()->GetMaterials()[NewSlot].MaterialSlotName;
    SelectedLayerGuid = Layer.LayerGuid;
    Asset->MarkPackageDirty();
    RefreshFromAsset();
    RebuildEditorLayout();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SelectedLayerGuid.IsValid()) return FReply::Handled();
    if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("RemoveTransparencyLayerConfirm", "Remove the selected Transparency Target Part and its editable strokes?")) != EAppReturnType::Yes)
        return FReply::Handled();
    const FScopedTransaction Transaction(LOCTEXT("RemoveTransparencyLayer", "Remove Transparency Target Part"));
    Asset->Modify();
    const FGuid RemovedLayerGuid = SelectedLayerGuid;
    Asset->TransparencyData.TransparencyLayers.RemoveAll([this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; });
    AutoBakeResults.Remove(RemovedLayerGuid);
    SelectedLayerGuid.Invalidate();
    Asset->MarkPackageDirty();
    RefreshFromAsset();
    RebuildEditorLayout();
    return FReply::Handled();
}
bool SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer() const { return GetSelectedLayer() != nullptr; }

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateMaterialSlotComboItem(FMaterialSlotItemPtr Item) const { return SNew(STextBlock).Text(GetMaterialSlotLabel(Item.IsValid() ? Item->SlotIndex : INDEX_NONE)); }
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem(TSharedPtr<int32> Item) const { return SNew(STextBlock).Text(FText::Format(LOCTEXT("UVChannelLabel", "UV {0}"), FText::AsNumber(Item.IsValid() ? *Item : 0))); }
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateSourceTypeComboItem(TSharedPtr<EDWCTransparencySourceType> Item) const { return SNew(STextBlock).Text(GetSourceTypeLabel(Item.IsValid() ? *Item : EDWCTransparencySourceType::SameMeshMaterialSlots)); }
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateAddressModeComboItem(TSharedPtr<EDWCTransparencyUVAddressMode> Item) const { return SNew(STextBlock).Text(GetAddressModeLabel(Item.IsValid() ? *Item : EDWCTransparencyUVAddressMode::Clamp)); }
FText SWetClothingTransparencyBakePanel::GetMaterialSlotLabel(int32 Index) const
{
    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(Index);
    return Item.IsValid() ? FText::Format(LOCTEXT("MaterialSlotLabel", "Slot {0} / {1}"), FText::AsNumber(Index), FText::FromName(Item->SlotName)) : LOCTEXT("NoMaterialSlot", "None");
}
FText SWetClothingTransparencyBakePanel::GetSourceTypeLabel(EDWCTransparencySourceType Type) const
{
    switch (Type) { case EDWCTransparencySourceType::OtherSkeletalMeshComponents: return LOCTEXT("OtherMeshSource", "Other Skeletal Mesh Components"); case EDWCTransparencySourceType::ManualColorOrTexture: return LOCTEXT("ManualSource", "Manual Color or Texture"); default: return LOCTEXT("SameMeshSource", "Same Skeletal Mesh / Material Slots"); }
}
FText SWetClothingTransparencyBakePanel::GetAddressModeLabel(EDWCTransparencyUVAddressMode Mode) const { return Mode == EDWCTransparencyUVAddressMode::Wrap ? LOCTEXT("UVWrap", "Wrap") : LOCTEXT("UVClamp", "Clamp"); }

void SWetClothingTransparencyBakePanel::HandleOuterMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type)
{
    if (!Item.IsValid()) return;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Duplicate = Asset != nullptr ? Asset->TransparencyData.FindTransparencyLayer(Item->SlotIndex, GetSelectedLayer() ? GetSelectedLayer()->TargetSurface.OuterUVChannel : 0) : nullptr;
    if (Duplicate != nullptr && Duplicate->LayerGuid != SelectedLayerGuid) { StatusMessage = TEXT("A Transparency Target Part already uses this Material Slot and UV Channel."); return; }
    EditSelectedLayer(LOCTEXT("SetTransparencyOuterSlot", "Set Transparency Outer Material Slot"), [Item](auto& Layer) { Layer.TargetSurface.OuterMaterialSlotIndex = Item->SlotIndex; Layer.TargetSurface.OuterMaterialSlotName = Item->SlotName; }, false);
}
void SWetClothingTransparencyBakePanel::HandleOuterUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
{
    if (!Item.IsValid()) return;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
    const FWetClothingTransparencyLayerData* Duplicate = Asset != nullptr && SelectedLayer != nullptr
        ? Asset->TransparencyData.FindTransparencyLayer(SelectedLayer->TargetSurface.OuterMaterialSlotIndex, *Item) : nullptr;
    if (Duplicate != nullptr && Duplicate->LayerGuid != SelectedLayerGuid)
    {
        StatusMessage = TEXT("A Transparency Target Part already uses this Material Slot and UV Channel.");
        return;
    }
    EditSelectedLayer(LOCTEXT("SetTransparencyUV", "Set Transparency UV Channel"), [Item](auto& Layer) { Layer.TargetSurface.OuterUVChannel = *Item; for (auto& Stroke : Layer.EditableStrokes) Stroke.UVChannelIndex = *Item; }, false);
}
void SWetClothingTransparencyBakePanel::HandleSourceTypeChanged(TSharedPtr<EDWCTransparencySourceType> Item, ESelectInfo::Type)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(LOCTEXT("SetTransparencySourceType", "Set Transparency Inner Source Type"), [Item](auto& Layer) { Layer.SourceType = *Item; }, true);
}
void SWetClothingTransparencyBakePanel::HandleAddressModeChanged(TSharedPtr<EDWCTransparencyUVAddressMode> Item, ESelectInfo::Type)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(LOCTEXT("SetTransparencyAddressMode", "Set Transparency UV Address Mode"), [Item](auto& Layer) { Layer.TargetSurface.UVAddressMode = *Item; for (auto& Stroke : Layer.EditableStrokes) Stroke.UVAddressMode = *Item; }, false);
}

FReply SWetClothingTransparencyBakePanel::HandleAddInnerSlotClicked()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr) return FReply::Handled();
    int32 SlotIndex = INDEX_NONE;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (!Item.IsValid() || Item->SlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex) continue;
        if (!Layer->SameMeshSource.InnerSlotPriority.ContainsByPredicate([Item](const auto& Slot) { return Slot.MaterialSlotIndex == Item->SlotIndex; })) { SlotIndex = Item->SlotIndex; break; }
    }
    if (SlotIndex == INDEX_NONE) return FReply::Handled();
    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(SlotIndex);
    EditSelectedLayer(LOCTEXT("AddTransparencyInnerSlot", "Add Transparency Inner Material Slot"), [Item](auto& TargetLayer)
    {
        auto& Slot = TargetLayer.SameMeshSource.InnerSlotPriority.AddDefaulted_GetRef();
        Slot.MaterialSlotIndex = Item->SlotIndex; Slot.MaterialSlotName = Item->SlotName;
    }, true);
    return FReply::Handled();
}
FReply SWetClothingTransparencyBakePanel::HandleRemoveInnerSlotClicked(int32 Index)
{
    EditSelectedLayer(LOCTEXT("RemoveTransparencyInnerSlot", "Remove Transparency Inner Material Slot"), [Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) Layer.SameMeshSource.InnerSlotPriority.RemoveAt(Index); }, true); return FReply::Handled();
}
FReply SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked(int32 Index, int32 Direction)
{
    EditSelectedLayer(LOCTEXT("MoveTransparencyInnerSlot", "Reorder Transparency Inner Material Slot"), [Index, Direction](auto& Layer) { const int32 To = Index + Direction; if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index) && Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(To)) Layer.SameMeshSource.InnerSlotPriority.Swap(Index, To); }, true); return FReply::Handled();
}
void SWetClothingTransparencyBakePanel::HandleInnerSlotEnabledChanged(ECheckBoxState State, int32 Index)
{
    EditSelectedLayer(LOCTEXT("ToggleTransparencyInnerSlot", "Toggle Transparency Inner Material Slot"), [State, Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) Layer.SameMeshSource.InnerSlotPriority[Index].bEnabled = State == ECheckBoxState::Checked; }, false);
}
void SWetClothingTransparencyBakePanel::HandleInnerMaterialSlotChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type, int32 Index)
{
    if (!Item.IsValid()) return;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Item->SlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex ||
        Layer->SameMeshSource.InnerSlotPriority.ContainsByPredicate(
            [Item, Index, Layer](const FWetClothingTransparencyInnerSlot& Slot)
            {
                const int32 CandidateIndex = static_cast<int32>(&Slot - Layer->SameMeshSource.InnerSlotPriority.GetData());
                return CandidateIndex != Index && Slot.MaterialSlotIndex == Item->SlotIndex;
            }))
    {
        StatusMessage = TEXT("The Inner Source Part must be unique and different from the Transparency Target Part.");
        return;
    }
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerSlot", "Set Transparency Inner Material Slot"), [Item, Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) { auto& Slot = Layer.SameMeshSource.InnerSlotPriority[Index]; Slot.MaterialSlotIndex = Item->SlotIndex; Slot.MaterialSlotName = Item->SlotName; } }, false);
}
void SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type, int32 Index)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerUV", "Set Transparency Inner UV Channel"), [Item, Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) Layer.SameMeshSource.InnerSlotPriority[Index].SourceUVChannel = *Item; }, false);
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildControlPanel()
{
    ActiveThumbnails.Reset();
    return SNew(SBorder).Padding(12.0f).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [SAssignNew(ControlPanelScrollBox, SScrollBox) + SScrollBox::Slot()[SNew(SVerticalBox)
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTargetMeshSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTransparencyLayersSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTargetSurfaceSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildInnerSourceSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildRaySettingsSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildBakeSettingsSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTransparencyBrushSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildGeneratedOutputsSection()]
         + SVerticalBox::Slot().AutoHeight()[BuildBakeSection()]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTargetMeshSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 SlotCount = Asset != nullptr && Asset->GetDWCSkeletalMesh() != nullptr ? Asset->GetDWCSkeletalMesh()->GetMaterials().Num() : 0;
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TargetMeshSection", "Target Mesh"))]
        + SVerticalBox::Slot().AutoHeight()[Asset != nullptr && Asset->GetDWCSkeletalMesh() != nullptr
            ? BuildAssetSummaryRow(Asset->GetDWCSkeletalMesh(), FText::FromString(Asset->GetDWCSkeletalMesh()->GetName()), FText::Format(LOCTEXT("TargetMeshDetails", "{0} material slots / {1} UV channels"), FText::AsNumber(SlotCount), FText::AsNumber(UVChannelItems.Num())))
            : BuildEmptyAssetRow(LOCTEXT("NoTargetMesh", "None"))];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyLayersSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TransparencyLayers", "Transparency Target Parts"))]
        + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(110)[SAssignNew(LayerListView, SListView<FLayerItemPtr>).ListItemsSource(&LayerItems).OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateLayerRow).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleLayerSelectionChanged)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[SNew(SButton).HAlign(HAlign_Center).ToolTipText(LOCTEXT("AddTargetPartTooltip", "Add a Transparency Target Part."))
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleAddLayerClicked)[SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("AddTargetPart", "Add Target Part"))]]]
          + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("RemoveTargetPartTooltip", "Remove the selected Transparency Target Part.")).IsEnabled(this, &SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveLayerClicked)
            [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTargetSurfaceSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TargetSurface", "Target Surface"))];
    if (Layer == nullptr) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoSelectedLayer", "Select a Transparency Target Part."))]; return Box; }
    Box->AddSlot().AutoHeight().Padding(0,0,0,8)[BuildLabeledControl(LOCTEXT("OuterMaterialSlot", "Outer Material Slot"),
        SNew(SComboBox<FMaterialSlotItemPtr>).OptionsSource(&MaterialSlotItems).InitiallySelectedItem(FindMaterialSlotItem(Layer->TargetSurface.OuterMaterialSlotIndex)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateMaterialSlotComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleOuterMaterialSlotChanged)[SNew(STextBlock).Text_Lambda([this](){ const auto* Selected = GetSelectedLayer(); return GetMaterialSlotLabel(Selected ? Selected->TargetSurface.OuterMaterialSlotIndex : INDEX_NONE); })])];
    Box->AddSlot().AutoHeight().Padding(0,0,0,8)[BuildLabeledControl(LOCTEXT("TransparencyUV", "Transparency UV Channel"),
        SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&UVChannelItems).InitiallySelectedItem(FindUVChannelItem(Layer->TargetSurface.OuterUVChannel)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleOuterUVChannelChanged)[SNew(STextBlock).Text_Lambda([this](){ const auto* Selected = GetSelectedLayer(); return FText::Format(LOCTEXT("SelectedUV", "UV {0}"), FText::AsNumber(Selected ? Selected->TargetSurface.OuterUVChannel : 0)); })])];
    Box->AddSlot().AutoHeight()[BuildLabeledControl(LOCTEXT("UVAddressMode", "UV Address Mode"),
        SNew(SComboBox<TSharedPtr<EDWCTransparencyUVAddressMode>>).OptionsSource(&AddressModeItems).InitiallySelectedItem(FindAddressModeItem(Layer->TargetSurface.UVAddressMode)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateAddressModeComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleAddressModeChanged)[SNew(STextBlock).Text_Lambda([this](){ const auto* Selected = GetSelectedLayer(); return GetAddressModeLabel(Selected ? Selected->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp); })])];
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildInnerSourceSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("InnerSource", "Inner Source Parts"))];
    if (Layer == nullptr) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoLayerForSource", "Select a Transparency Target Part."))]; return Box; }
    Box->AddSlot().AutoHeight().Padding(0,0,0,8)[BuildLabeledControl(LOCTEXT("InnerSourceType", "Inner Source Type"),
        SNew(SComboBox<TSharedPtr<EDWCTransparencySourceType>>).OptionsSource(&SourceTypeItems).InitiallySelectedItem(FindSourceTypeItem(Layer->SourceType)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateSourceTypeComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleSourceTypeChanged)[SNew(STextBlock).Text_Lambda([this](){ const auto* Selected = GetSelectedLayer(); return GetSourceTypeLabel(Selected ? Selected->SourceType : EDWCTransparencySourceType::SameMeshMaterialSlots); })])];
    Box->AddSlot().AutoHeight().Padding(0,0,0,8)[SNew(STextBlock).AutoWrapText(true).Text(this, &SWetClothingTransparencyBakePanel::GetInnerSourceStatusText).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
    Box->AddSlot().AutoHeight()[Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots ? BuildSameMeshSourceSection() : Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ? BuildOtherMeshSourceSection() : BuildManualSourceSection()];
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildSameMeshSourceSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    if (Layer == nullptr) return Box;
    for (int32 Index = 0; Index < Layer->SameMeshSource.InnerSlotPriority.Num(); ++Index)
    {
        const auto& Slot = Layer->SameMeshSource.InnerSlotPriority[Index];
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [
            SNew(SBorder)
            .Padding(FMargin(6, 5))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                    [SNew(STextBlock).Text(FText::Format(LOCTEXT("InnerPriorityLabel", "P{0}"), FText::AsNumber(Index))).ColorAndOpacity(FSlateColor::UseSubduedForeground())]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                    [SNew(SCheckBox).ToolTipText(LOCTEXT("EnableInnerPartTooltip", "Include this Inner Source Part when generating the map.")).IsChecked(Slot.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerSlotEnabledChanged, Index)]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,4,0)
                    [SNew(SComboBox<FMaterialSlotItemPtr>).OptionsSource(&MaterialSlotItems).InitiallySelectedItem(FindMaterialSlotItem(Slot.MaterialSlotIndex)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateMaterialSlotComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerMaterialSlotChanged, Index)[SNew(STextBlock).Text_Lambda([this, Index](){ const auto* Selected = GetSelectedLayer(); return Selected && Selected->SameMeshSource.InnerSlotPriority.IsValidIndex(Index) ? GetMaterialSlotLabel(Selected->SameMeshSource.InnerSlotPriority[Index].MaterialSlotIndex) : LOCTEXT("MissingInnerPart", "Missing"); })]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                    [SNew(SBox).WidthOverride(62)[SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&UVChannelItems).InitiallySelectedItem(FindUVChannelItem(Slot.SourceUVChannel)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged, Index)[SNew(STextBlock).Text_Lambda([this, Index](){ const auto* Selected = GetSelectedLayer(); const int32 UV = Selected && Selected->SameMeshSource.InnerSlotPriority.IsValidIndex(Index) ? Selected->SameMeshSource.InnerSlotPriority[Index].SourceUVChannel : 0; return FText::Format(LOCTEXT("InnerUV", "UV {0}"), FText::AsNumber(UV)); })]]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerUpTooltip", "Move this source earlier in the priority order.")).IsEnabled(Index > 0).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, Index, -1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowUp")))]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerDownTooltip", "Move this source later in the priority order.")).IsEnabled(Index + 1 < Layer->SameMeshSource.InnerSlotPriority.Num()).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, Index, 1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowDown")))]]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("DeleteInnerTooltip", "Remove this Inner Source Part.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveInnerSlotClicked, Index)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(42,4,0,0)
                [
                    BuildLabeledControl(LOCTEXT("InnerMaxDistance", "Maximum Hit Distance"),
                        SNew(SNumericEntryBox<float>)
                        .MinValue(0.001f)
                        .Value_Lambda([this, Index]() -> TOptional<float>
                        {
                            const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                            return SelectedLayer != nullptr && SelectedLayer->SameMeshSource.InnerSlotPriority.IsValidIndex(Index)
                                ? TOptional<float>(SelectedLayer->SameMeshSource.InnerSlotPriority[Index].MaxHitDistance)
                                : TOptional<float>();
                        })
                        .OnValueCommitted_Lambda([this, Index](float Value, ETextCommit::Type)
                        {
                            EditSelectedLayer(
                                LOCTEXT("SetInnerMaxDistance", "Set Inner Maximum Hit Distance"),
                                [Index, Value](auto& TargetLayer)
                                {
                                    if (TargetLayer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index))
                                    {
                                        TargetLayer.SameMeshSource.InnerSlotPriority[Index].MaxHitDistance = FMath::Max(Value, 0.001f);
                                    }
                                },
                                false);
                        }))
                ]
            ]
        ];
    }
    Box->AddSlot().AutoHeight()
    [SNew(SButton).HAlign(HAlign_Center).ToolTipText(LOCTEXT("AddInnerSlotTooltip", "Add a material slot to the Inner Source priority list.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleAddInnerSlotClicked)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("AddInnerSlot", "Add Inner Source Part"))]]];
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildOtherMeshSourceSection()
{
    return BuildLabeledControl(LOCTEXT("SourceBlueprint", "Source Blueprint"),
        SNew(SClassPropertyEntryBox).MetaClass(AActor::StaticClass()).AllowAbstract(false).AllowNone(true).SelectedClass(this, &SWetClothingTransparencyBakePanel::GetSelectedSourceClass).OnSetClass(this, &SWetClothingTransparencyBakePanel::HandleSourceClassChanged));
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildManualSourceSection() { return BuildEmptyAssetRow(LOCTEXT("ManualNotAvailable", "Manual Color or Texture source is not available yet.")); }

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRaySettingsSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RaySettings", "Ray Settings"))];
    if (Layer == nullptr) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoLayerRay", "Select a Transparency Target Part."))]; return Box; }
    using FSettingMember = float FWetClothingTransparencyRaySettings::*;
    auto AddFloat = [this, &Box](
        const FText& Label,
        const FText& TransactionText,
        FSettingMember Member,
        const float MinimumValue = 0.0f)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [
            BuildLabeledControl(
                Label,
                SNew(SNumericEntryBox<float>)
                .MinValue(MinimumValue)
                .Value_Lambda([this, Member]() -> TOptional<float>
                {
                    const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                    return SelectedLayer != nullptr
                        ? TOptional<float>(SelectedLayer->RaySettings.*Member)
                        : TOptional<float>();
                })
                .OnValueCommitted_Lambda([this, TransactionText, Member, MinimumValue](float NewValue, ETextCommit::Type)
                {
                    EditSelectedLayer(
                        TransactionText,
                        [Member, NewValue, MinimumValue](auto& TargetLayer)
                        {
                            TargetLayer.RaySettings.*Member = FMath::Max(MinimumValue, NewValue);
                        },
                        false);
                }))
        ];
    };
    AddFloat(
        LOCTEXT("RayStartOffset", "Ray Start Offset"),
        LOCTEXT("SetRayStartOffset", "Set Transparency Ray Start Offset"),
        &FWetClothingTransparencyRaySettings::RayStartOffset,
        -100.0f);
    AddFloat(LOCTEXT("MinHitDistance", "Minimum Hit Distance"), LOCTEXT("SetMinHitDistance", "Set Transparency Minimum Hit Distance"), &FWetClothingTransparencyRaySettings::MinHitDistance);
    AddFloat(LOCTEXT("FullDistance", "Full Transparency Distance"), LOCTEXT("SetFullDistance", "Set Full Transparency Distance"), &FWetClothingTransparencyRaySettings::FullTransparencyDistance);
    AddFloat(LOCTEXT("NoDistance", "No Transparency Distance"), LOCTEXT("SetNoDistance", "Set No Transparency Distance"), &FWetClothingTransparencyRaySettings::NoTransparencyDistance);
    AddFloat(LOCTEXT("MaxRayDistance", "Maximum Ray Distance"), LOCTEXT("SetMaxRayDistance", "Set Transparency Maximum Ray Distance"), &FWetClothingTransparencyRaySettings::MaxRayDistance);
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBakeSettingsSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BakeSettings", "Bake Settings"))];
    if (Asset == nullptr) return Box;
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BakeResolution", "Resolution"), SNew(SNumericEntryBox<int32>).MinValue(16).MaxValue(4096).Value(Asset->TransparencyData.TransparencyBakeResolution).OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type){ EditGlobalSettings(LOCTEXT("SetTransparencyResolution", "Set Transparency Resolution"), [V](auto& D){ D.TransparencyBakeResolution = FMath::Clamp(V,16,4096); }); }))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("PaddingPixels", "Padding Pixels"), SNew(SNumericEntryBox<int32>).MinValue(0).MaxValue(64).Value(Asset->TransparencyData.TransparencyPaddingPixels).OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type){ EditGlobalSettings(LOCTEXT("SetTransparencyPadding", "Set Transparency Padding"), [V](auto& D){ D.TransparencyPaddingPixels = FMath::Clamp(V,0,64); }); }))];
    Box->AddSlot().AutoHeight()[BuildLabeledControl(LOCTEXT("EdgeFeather", "Edge Feather Pixels"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(32.0f).Value(Asset->TransparencyData.TransparencyEdgeFeatherPixels).OnValueCommitted_Lambda([this](float V, ETextCommit::Type){ EditGlobalSettings(LOCTEXT("SetTransparencyFeather", "Set Transparency Edge Feather"), [V](auto& D){ D.TransparencyEdgeFeatherPixels = FMath::Clamp(V,0.0f,32.0f); }); }))];
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyBrushSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
        [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TransparencyBrush", "Transparency Brush"))];

    auto ModeButton = [this](EDWCTransparencyBrushMode Mode, const FText& Label, const FText& Tooltip)
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
            .Type(ESlateCheckBoxType::ToggleButton)
            .ToolTipText(Tooltip)
            .IsChecked(this, &SWetClothingTransparencyBakePanel::IsBrushModeChecked, Mode)
            .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleBrushModeChanged, Mode)
            [SNew(STextBlock).Text(Label)];
    };

    Box->AddSlot().AutoHeight().Padding(0,0,0,8)
    [SNew(SWrapBox).UseAllottedSize(true)
        + SWrapBox::Slot()[ModeButton(EDWCTransparencyBrushMode::Apply, LOCTEXT("BrushApply", "Apply"), LOCTEXT("BrushApplyTooltip", "Increase transparency alpha."))]
        + SWrapBox::Slot()[ModeButton(EDWCTransparencyBrushMode::Erase, LOCTEXT("BrushErase", "Erase"), LOCTEXT("BrushEraseTooltip", "Reduce transparency alpha."))]
        + SWrapBox::Slot()[ModeButton(EDWCTransparencyBrushMode::SetValue, LOCTEXT("BrushSet", "Set"), LOCTEXT("BrushSetTooltip", "Paint toward the target alpha."))]
        + SWrapBox::Slot()[ModeButton(EDWCTransparencyBrushMode::Smooth, LOCTEXT("BrushSmooth", "Smooth"), LOCTEXT("BrushSmoothTooltip", "Smooth neighboring edited alpha."))]
        + SWrapBox::Slot()[ModeButton(EDWCTransparencyBrushMode::ResetToAuto, LOCTEXT("BrushReset", "Reset"), LOCTEXT("BrushResetTooltip", "Restore the automatic alpha."))]];

    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushRadius", "Radius (UV)"),
        SNew(SNumericEntryBox<float>).MinValue(0.0001f).MaxValue(0.5f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushRadius).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushRadiusCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushStrength", "Strength"),
        SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushStrengthCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushFalloff", "Falloff"),
        SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushFalloff).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushFalloffCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushSpacing", "Spacing"),
        SNew(SNumericEntryBox<float>).MinValue(0.01f).MaxValue(2.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushSpacing).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushSpacingCommitted))];
    if (BrushMode == EDWCTransparencyBrushMode::SetValue)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushTargetAlpha", "Target Alpha"),
            SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushTargetAlpha).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushTargetAlphaCommitted))];
    }

    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    Box->AddSlot().AutoHeight().Padding(0,2,0,6)
    [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
        [SNew(SButton).Text(LOCTEXT("UndoLastStroke", "Undo Last Stroke")).IsEnabled(Layer != nullptr && !Layer->EditableStrokes.IsEmpty()).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked)]
        + SHorizontalBox::Slot().FillWidth(1)
        [SNew(SButton).Text(LOCTEXT("ClearStrokes", "Clear")).IsEnabled(Layer != nullptr && !Layer->EditableStrokes.IsEmpty()).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleClearStrokesClicked)]];

    if (Layer == nullptr || Layer->EditableStrokes.IsEmpty())
    {
        Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoTransparencyStrokes", "No manual transparency strokes."))];
    }
    else
    {
        for (const FDWCTransparencyBrushStroke& Stroke : Layer->EditableStrokes)
        {
            Box->AddSlot().AutoHeight().Padding(0,0,0,3)
            [SNew(SBorder).Padding(4).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                    [SNew(SCheckBox).IsChecked(Stroke.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleStrokeEnabledChanged, Stroke.StrokeGuid)]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(FText::FromString(Stroke.DisplayName))]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("DeleteStrokeTooltip", "Delete this stroke.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleDeleteStrokeClicked, Stroke.StrokeGuid)
                        [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]];
        }
    }
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildGeneratedOutputsSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("GeneratedOutputs", "Generated Output"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SBorder).Padding(FMargin(8,6)).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [SNew(STextBlock).AutoWrapText(true).Text(this, &SWetClothingTransparencyBakePanel::GetStatusText).ColorAndOpacity(this, &SWetClothingTransparencyBakePanel::GetStatusColor)]]
        + SVerticalBox::Slot().AutoHeight()[Layer != nullptr && Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents
            ? SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[BuildRevealMaterialSection()] + SVerticalBox::Slot().AutoHeight()[BuildRevealTextureSection()]
            : BuildPackedTransparencyMapSection()];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPackedTransparencyMapSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    if (Layer == nullptr || Layer->BakedMaps.Num() == 0) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoPackedMap", "No packed Transparency Map."))]; return Box; }
    for (const auto& Map : Layer->BakedMaps)
    {
        const FText BakeState = Map.BakeGuid.IsValid() && !Map.BuildSignature.IsEmpty()
            ? LOCTEXT("PackedMapReady", "Ready")
            : LOCTEXT("PackedMapStale", "Stale");
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
            [BuildAssetSummaryRow(
                Map.TransparencyMap,
                FText::FromString(GetNameSafe(Map.TransparencyMap)),
                FText::Format(
                    LOCTEXT("PackedMapDetail", "Slot {0} / UV {1} / LOD {2} / {3} / {4}"),
                    FText::AsNumber(Map.MaterialSlotIndex),
                    FText::AsNumber(Map.UVChannelIndex),
                    FText::AsNumber(Map.LODIndex),
                    FText::AsNumber(Map.Resolution),
                    BakeState))];
    }
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBakeSection()
{
    return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("Actions", "Actions"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("GenerateTransparencyMap", "Generate Transparency Map")).ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText).IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)]
        + SVerticalBox::Slot().AutoHeight()[SNew(SButton).HAlign(HAlign_Center).Text(LOCTEXT("BakeEditedTransparencyMap", "Bake Edited Transparency Map")).ToolTipText(this, &SWetClothingTransparencyBakePanel::GetBakeEditedTooltipText).IsEnabled(this, &SWetClothingTransparencyBakePanel::IsBakeEditedEnabled).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleBakeEditedTransparencyMapClicked)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewSettingsSection()
{
    return SNew(SBorder).Padding(10).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))[SNew(SVerticalBox)
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Text(LOCTEXT("PreviewSettings", "Preview Settings")).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,3)[SNew(STextBlock).Text(LOCTEXT("PreviewWetnessLabel", "Preview Wetness"))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SSlider).MinValue(0).MaxValue(100).Value(this, &SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent).OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged)]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("TransparencyPreviewStrengthLabel", "Transparency Preview Strength"),
          SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(8.0f).Value(this, &SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionStrengthLabel", "Wrinkle Suppression Strength"),
          SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(5.0f).Value(this, &SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("TransparencyVisualizationLabel", "Visualization"),
          SNew(SComboBox<TSharedPtr<EDWCTransparencyVisualizationMode>>)
              .OptionsSource(&VisualizationModeItems)
              .InitiallySelectedItem(FindVisualizationModeItem(SelectedVisualizationMode))
              .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem)
              .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged)
              [SNew(STextBlock).Text_Lambda([this](){ return GetVisualizationModeLabel(SelectedVisualizationMode); })])]
      + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)[BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::TargetMeshOnly, LOCTEXT("TargetMeshPreview", "Target Mesh"))]
        + SHorizontalBox::Slot().AutoWidth()[BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::FullBlueprint, LOCTEXT("FullBPPreview", "Full BP"))]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyPreviewSection()
{
    return SNew(SBorder).Padding(12)[SNew(SVerticalBox)
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(LOCTEXT("Preview", "Preview")).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("FocusMesh", "Focus Mesh")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleFocusPreviewClicked)]]
      + SVerticalBox::Slot().FillHeight(1)[SNew(SSplitter).Orientation(Orient_Vertical)
        + SSplitter::Slot().Value(0.78f)[SAssignNew(PreviewViewport, SWetClothingTransparencyPreviewViewport)
            .WetClothingAsset(WetClothingAsset.Get())
            .OnStrokesChanged(this, &SWetClothingTransparencyBakePanel::HandleViewportStrokesChanged)]
        + SSplitter::Slot().Value(0.22f)
          [SNew(SScrollBox)
            .Orientation(Orient_Vertical)
            + SScrollBox::Slot()
            [BuildPreviewSettingsSection()]]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label)
{
    return SNew(SCheckBox).Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton")).Type(ESlateCheckBoxType::ToggleButton)
        .IsEnabled_Lambda([this, Mode](){ return Mode != EWetClothingTransparencyPreviewMode::FullBlueprint || CanUseFullBlueprintPreview(); })
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsPreviewModeChecked, Mode).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandlePreviewModeChanged, Mode)[SNew(STextBlock).Text(Label)];
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealMapTypeButton(EDWCTransparencyRevealMapType Type, const FText& Label)
{
    return SNew(SCheckBox).Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton")).Type(ESlateCheckBoxType::ToggleButton).IsChecked(this, &SWetClothingTransparencyBakePanel::IsRevealMapTypeChecked, Type).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealMapTypeChanged, Type)[SNew(STextBlock).Text(Label)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealMaterialSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get(); TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    if (Asset == nullptr || Asset->TransparencyData.BakedRevealLayers.Num() == 0) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoLegacyMaterials", "No generated reveal materials."))]; return Box; }
    for (const auto& Layer : Asset->TransparencyData.BakedRevealLayers) Box->AddSlot().AutoHeight().Padding(0,0,0,4)[BuildAssetSummaryRow(Layer.RevealMaterial, FText::FromString(GetNameSafe(Layer.RevealMaterial)), FText::Format(LOCTEXT("LegacyMaterialDetail", "Slot {0}"), FText::AsNumber(Layer.MaterialSlotIndex)))];
    return Box;
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealTextureSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get(); TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    if (Asset == nullptr || Asset->TransparencyData.BakedRevealLayers.Num() == 0) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoLegacyMaps", "No generated reveal maps."))]; return Box; }
    Box->AddSlot().AutoHeight().Padding(0,4,0,6)[SNew(SHorizontalBox)
      + SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Color, LOCTEXT("ColorMap", "Color"))]
      + SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Mask, LOCTEXT("MaskMap", "Mask"))]
      + SHorizontalBox::Slot().AutoWidth().Padding(0,0,3,0)[BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Confidence, LOCTEXT("ConfidenceMap", "Confidence"))]
      + SHorizontalBox::Slot().AutoWidth()[BuildRevealMapTypeButton(EDWCTransparencyRevealMapType::Lookup, LOCTEXT("LookupMap", "Lookup"))]];
    for (const auto& Layer : Asset->TransparencyData.BakedRevealLayers)
    {
        UObject* Texture = SelectedRevealMapType == EDWCTransparencyRevealMapType::Mask ? Layer.MaskMap.Get() : SelectedRevealMapType == EDWCTransparencyRevealMapType::Confidence ? Layer.ConfidenceMap.Get() : SelectedRevealMapType == EDWCTransparencyRevealMapType::Lookup ? Layer.LookupMap.Get() : Layer.ColorMap.Get();
        Box->AddSlot().AutoHeight().Padding(0,0,0,4)[BuildSelectedRevealMapPreview(Texture, LOCTEXT("LegacyMap", "Legacy Reveal Map"), FText::Format(LOCTEXT("LegacyMapSlot", "Slot {0}"), FText::AsNumber(Layer.MaterialSlotIndex)))];
    }
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildSelectedRevealMapPreview(UObject* Asset, const FText& Label, const FText& Detail)
{
    return BuildAssetSummaryRow(Asset, FText::Format(LOCTEXT("RevealAssetLabel", "{0} / {1}"), Label, FText::FromString(GetNameSafe(Asset))), Detail);
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildAssetSummaryRow(UObject* Asset, const FText& Label, const FText& Detail)
{
    TSharedRef<SWidget> Thumbnail = SNew(SBox).WidthOverride(44).HeightOverride(44)[SNullWidget::NullWidget];
    if (Asset != nullptr && ThumbnailPool.IsValid()) { TSharedPtr<FAssetThumbnail> T = MakeShared<FAssetThumbnail>(Asset,44,44,ThumbnailPool); ActiveThumbnails.Add(T); Thumbnail = T->MakeThumbnailWidget(FAssetThumbnailConfig()); }
    return SNew(SBorder).Padding(4).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))[SNew(SHorizontalBox)
      + SHorizontalBox::Slot().AutoWidth()[Thumbnail]
      + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6,0,0,0)[SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Label).AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(Detail).ColorAndOpacity(FSlateColor::UseSubduedForeground()).AutoWrapText(true)]]];
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildEmptyAssetRow(const FText& Label) const
{
    return SNew(SBorder).Padding(7).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))[SNew(STextBlock).Text(Label).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())];
}

void SWetClothingTransparencyBakePanel::RefreshViewportContext()
{
    if (!PreviewViewport.IsValid()) return;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!CanUseFullBlueprintPreview() && PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint)
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    PreviewViewport->SetTransparencyEditContext(SelectedLayerGuid,
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : INDEX_NONE,
        Layer != nullptr ? Layer->TargetSurface.OuterUVChannel : 0,
        Layer != nullptr ? Layer->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp);
    PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
    PreviewViewport->SetTransparencyPreviewStrength(TransparencyPreviewStrength);
    PreviewViewport->SetWrinkleSuppressionStrength(WrinkleSuppressionStrength);
    PreviewViewport->SetVisualizationMode(SelectedVisualizationMode);
    PushPaintSettingsToViewport();

    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = Layer != nullptr
        ? AutoBakeResults.Find(Layer->LayerGuid)
        : nullptr;
    if (Result != nullptr && Result->IsValid())
    {
        PreviewViewport->SetAutoBakePreviewResult(*Result);
    }
    else
    {
        PreviewViewport->ClearAutoBakePreviewResult();
    }
}

#undef LOCTEXT_NAMESPACE
