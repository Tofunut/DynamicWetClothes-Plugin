//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"

#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyTempAssetStore.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowPolicy.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowStateResolver.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingTransparencyBakePanel"

namespace
{
struct FDWCTransparencyUVIslandColorSelection
{
    FLinearColor Color = FLinearColor::White;
    TWeakObjectPtr<UTexture2D> Texture;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
};

DECLARE_DELEGATE_OneParam(
    FOnDWCTransparencyUVIslandColorAccepted,
    const FDWCTransparencyUVIslandColorSelection&);

int32 ResolveTransparencyDataUVChannel(const UWetClothingAsset* Asset)
{
    return Asset != nullptr && Asset->HasValidDataUVForLOD(0)
        ? Asset->GetDWCDataUVChannelIndex()
        : INDEX_NONE;
}

const TCHAR* GetTransparencyStrokeModeLabel(const EDWCTransparencyBrushMode Mode)
{
    switch (Mode)
    {
    case EDWCTransparencyBrushMode::Erase:
        return TEXT("Erase");
    case EDWCTransparencyBrushMode::SetValue:
        return TEXT("Set");
    case EDWCTransparencyBrushMode::Smooth:
        return TEXT("Smooth");
    case EDWCTransparencyBrushMode::ResetToAuto:
        return TEXT("Reset");
    case EDWCTransparencyBrushMode::Apply:
    default:
        return TEXT("Apply");
    }
}

FText GetTransparencySourceTypeLabel(const EDWCTransparencySourceType SourceType)
{
    switch (SourceType)
    {
    case EDWCTransparencySourceType::OtherSkeletalMeshComponents:
        return LOCTEXT("TransparencySourceTypeMultipleMeshes", "Blueprint / Multiple Skeletal Meshes");
    case EDWCTransparencySourceType::ManualColorOrTexture:
        return LOCTEXT("TransparencySourceTypeManualColor", "No Inner Mesh / Base Color");
    case EDWCTransparencySourceType::ExternalSkeletalMesh:
        return LOCTEXT("TransparencySourceTypeExternalMesh", "External Skeletal Mesh");
    case EDWCTransparencySourceType::SameMeshMaterialSlots:
    default:
        return LOCTEXT("TransparencySourceTypeSameMesh", "Single Skeletal Mesh / Inner Material Slots");
    }
}

const FWetClothingBakedTransparencyMap* FindExactBakedTransparencyMap(
    const UWetClothingAsset* Asset,
    const FWetClothingTransparencyLayerData* Layer)
{
    if (Asset == nullptr || Layer == nullptr)
    {
        return nullptr;
    }
    return Layer->BakedMaps.FindByPredicate(
        [Layer](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex &&
                   Candidate.TransparencyMap != nullptr;
        });
}

TSharedRef<SWidget> BuildLabeledControl(const FText& Label, const TSharedRef<SWidget>& Control)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
        + SVerticalBox::Slot().AutoHeight()[Control];
}

constexpr float DWCTransparencyDefaultBrushSizeCm = 8.0f;
constexpr float DWCTransparencyDefaultBrushRadiusUV = 0.0677f;
constexpr float DWCTransparencyUVPerCm =
    DWCTransparencyDefaultBrushRadiusUV / DWCTransparencyDefaultBrushSizeCm;

float DWCTransparencyRadiusUVToSizeCm(const float RadiusUV)
{
    return FMath::Clamp(RadiusUV / DWCTransparencyUVPerCm, 0.1f, 100.0f);
}

float DWCTransparencySizeCmToRadiusUV(const float SizeCm)
{
    return FMath::Clamp(SizeCm * DWCTransparencyUVPerCm, 0.001f, 0.5f);
}

FText FormatDWCTransparencyBrushSizeCm(const float SizeCm)
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 0;
    Options.MaximumFractionalDigits = SizeCm < 10.0f ? 1 : 0;
    return FText::Format(LOCTEXT("TransparencyBrushSizeCmFormat", "{0} cm"), FText::AsNumber(SizeCm, &Options));
}

/**
 * Transient helper used while choosing a manual reveal color. The accepted
 * texture/island identity is persisted only as source metadata; raster data
 * remains in the generated stage artifacts.
 */
class SDWCTransparencyUVIslandColorPicker final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWCTransparencyUVIslandColorPicker) {}
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
        SLATE_ARGUMENT(USkeletalMesh*, InitialMesh)
        SLATE_ARGUMENT(int32, InitialOriginalUVChannel)
        SLATE_EVENT(FOnDWCTransparencyUVIslandColorAccepted, OnColorAccepted)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        ParentWindow = InArgs._ParentWindow;
        ReferenceMesh = InArgs._InitialMesh;
        PreferredOriginalUVChannel = FMath::Max(0, InArgs._InitialOriginalUVChannel);
        OnColorAccepted = InArgs._OnColorAccepted;
        RebuildSlotOptions();
        RebuildUVOptions();
        RebuildTextureOptions();

        ChildSlot
        [
            SNew(SBorder)
            .Padding(12.0f)
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("UVIslandColorPickerDescription", "Choose a color texture and one UV island. The selected island is sampled in linear color space and its average becomes the Base Reveal Color."))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                ]
                + SVerticalBox::Slot().FillHeight(1.0f)
                [
                    SNew(SSplitter)
                    .Orientation(Orient_Horizontal)
                    + SSplitter::Slot().Value(0.30f).MinSize(220.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 10.0f, 10.0f)
                        [
                            BuildLabeledControl(
                                LOCTEXT("ReferenceSkeletalMesh", "Reference Skeletal Mesh"),
                                SNew(SObjectPropertyEntryBox)
                                .AllowedClass(USkeletalMesh::StaticClass())
                                .AllowClear(true)
                                .ObjectPath_Lambda([this]()
                                {
                                    const USkeletalMesh* Mesh = ReferenceMesh.Get();
                                    return Mesh != nullptr ? Mesh->GetPathName() : FString();
                                })
                                .OnObjectChanged_Lambda([this](const FAssetData& AssetData)
                                {
                                    ReferenceMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
                                    SelectedSlotIndex = INDEX_NONE;
                                    SelectedIslandID = INDEX_NONE;
                                    CandidateColor = FLinearColor::White;
                                    bHasCandidateColor = false;
                                    RebuildSlotOptions();
                                    RebuildUVOptions();
                                    RebuildTextureOptions();
                                    RefreshSlotList();
                                    RefreshUVIslandView();
                                }))
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 10.0f, 6.0f)
                        [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("ReferenceMaterialSlots", "Material Slots"))]
                        + SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 0.0f, 10.0f, 0.0f)
                        [
                            SAssignNew(SlotListView, SListView<TSharedPtr<int32>>)
                            .ListItemsSource(&SlotOptions)
                            .SelectionMode(ESelectionMode::Single)
                            .OnGenerateRow(this, &SDWCTransparencyUVIslandColorPicker::GenerateSlotRow)
                            .OnSelectionChanged(this, &SDWCTransparencyUVIslandColorPicker::HandleSlotSelectionChanged)
                        ]
                    ]
                    + SSplitter::Slot().Value(0.70f).MinSize(440.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 0.0f, 0.0f, 10.0f)
                        [
                            BuildLabeledControl(
                                LOCTEXT("ReferenceColorTexture", "Color Texture"),
                                SNew(SComboBox<TSharedPtr<FWCATextureItem>>)
                                .OptionsSource(&TextureOptions)
                                .InitiallySelectedItem(SelectedTextureItem)
                                .OnGenerateWidget_Lambda([this](TSharedPtr<FWCATextureItem> Item)
                                {
                                    return SNew(STextBlock).Text(GetTextureLabel(Item));
                                })
                                .OnSelectionChanged_Lambda([this](TSharedPtr<FWCATextureItem> Item, ESelectInfo::Type)
                                {
                                    if (!Item.IsValid() || SelectedTextureItem == Item)
                                    {
                                        return;
                                    }
                                    SelectedTextureItem = Item;
                                    ReferenceTexture = Cast<UTexture2D>(Item->Texture.Get());
                                    SelectedIslandID = INDEX_NONE;
                                    bHasCandidateColor = false;
                                    RefreshUVIslandView();
                                })
                                [SNew(STextBlock).Text_Lambda([this]() { return GetTextureLabel(SelectedTextureItem); })]
                            )
                        ]
                        + SVerticalBox::Slot().FillHeight(1.0f).Padding(10.0f, 0.0f, 0.0f, 0.0f)
                        [
                            SNew(SBox)
                            .MinDesiredHeight(420.0f)
                            [
                                SAssignNew(UVIslandView, SWCAUVView)
                                .OnIslandSelectionChanged(this, &SDWCTransparencyUVIslandColorPicker::HandleUVIslandSelectionChanged)
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(10.0f, 10.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                            [SNew(STextBlock).Text(LOCTEXT("AverageColorLabel", "Average Island Color"))]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [SNew(SColorBlock).Color_Lambda([this]() { return CandidateColor; }).Size(FVector2D(64.0f, 24.0f)).ShowBackgroundForAlpha(false)]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f)[SNullWidget::NullWidget]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [SNew(SButton).Text(LOCTEXT("UVIslandColorPickerCancel", "Cancel")).OnClicked(this, &SDWCTransparencyUVIslandColorPicker::HandleCancelClicked)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SButton).Text(LOCTEXT("UVIslandColorPickerApply", "Use Average Color")).IsEnabled_Lambda([this]() { return bHasCandidateColor; }).OnClicked(this, &SDWCTransparencyUVIslandColorPicker::HandleApplyClicked)]
                ]
            ]
        ];

        RefreshSlotList();
        RefreshUVIslandView();
    }

private:
    void RebuildSlotOptions()
    {
        SlotOptions.Reset();
        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        if (Mesh == nullptr)
        {
            SelectedSlotIndex = INDEX_NONE;
            return;
        }
        for (int32 SlotIndex = 0; SlotIndex < Mesh->GetMaterials().Num(); ++SlotIndex)
        {
            SlotOptions.Add(MakeShared<int32>(SlotIndex));
        }
        if (!SlotOptions.ContainsByPredicate([this](const TSharedPtr<int32>& Item) { return Item.IsValid() && *Item == SelectedSlotIndex; }))
        {
            SelectedSlotIndex = SlotOptions.IsEmpty() ? INDEX_NONE : *SlotOptions[0];
        }
    }

    void RebuildUVOptions()
    {
        UVChannelOptions.Reset();
        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        const int32 UVCount = RenderData != nullptr && RenderData->LODRenderData.Num() > 0
            ? RenderData->LODRenderData[0].GetNumTexCoords()
            : 0;
        for (int32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
        {
            UVChannelOptions.Add(MakeShared<int32>(UVIndex));
        }
        SelectedUVChannel = UVChannelOptions.ContainsByPredicate([this](const TSharedPtr<int32>& Item)
        {
            return Item.IsValid() && *Item == PreferredOriginalUVChannel;
        })
            ? PreferredOriginalUVChannel
            : (UVChannelOptions.IsEmpty() ? INDEX_NONE : *UVChannelOptions[0]);
    }

    void RebuildTextureOptions()
    {
        TextureOptions.Reset();
        SelectedTextureItem.Reset();
        ReferenceTexture.Reset();

        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        if (Mesh == nullptr || !Mesh->GetMaterials().IsValidIndex(SelectedSlotIndex))
        {
            return;
        }

        FWetClothingMaterialTextureResolver::BuildTextureItems(
            Mesh->GetMaterials()[SelectedSlotIndex].MaterialInterface,
            TextureOptions);

        for (const TSharedPtr<FWCATextureItem>& Item : TextureOptions)
        {
            if (Item.IsValid() && Cast<UTexture2D>(Item->Texture.Get()) != nullptr)
            {
                SelectedTextureItem = Item;
                ReferenceTexture = Cast<UTexture2D>(Item->Texture.Get());
                break;
            }
        }
    }

    void RefreshSlotList()
    {
        if (!SlotListView.IsValid())
        {
            return;
        }

        SlotListView->RequestListRefresh();
        if (const TSharedPtr<int32>* Item = SlotOptions.FindByPredicate([this](const TSharedPtr<int32>& Candidate)
            {
                return Candidate.IsValid() && *Candidate == SelectedSlotIndex;
            }))
        {
            SlotListView->SetSelection(*Item, ESelectInfo::Direct);
        }
        else
        {
            SlotListView->ClearSelection();
        }
    }

    TSharedRef<ITableRow> GenerateSlotRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& OwnerTable)
    {
        return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
        .Padding(FMargin(4.0f, 3.0f))
        [
            SNew(STextBlock)
            .Text(GetSlotLabel(Item))
            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
        ];
    }

    void HandleSlotSelectionChanged(TSharedPtr<int32> Item, ESelectInfo::Type)
    {
        if (!Item.IsValid() || SelectedSlotIndex == *Item)
        {
            return;
        }

        SelectedSlotIndex = *Item;
        SelectedIslandID = INDEX_NONE;
        CandidateColor = FLinearColor::White;
        bHasCandidateColor = false;
        RebuildTextureOptions();
        RefreshUVIslandView();
    }

    FText GetTextureLabel(const TSharedPtr<FWCATextureItem>& Item) const
    {
        if (!Item.IsValid() || !Item->Texture.IsValid())
        {
            return LOCTEXT("NoReferenceColorTexture", "No usable color texture");
        }
        return FText::FromString(Item->Label);
    }

    FText GetSlotLabel(const TSharedPtr<int32>& Item) const
    {
        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        if (Mesh == nullptr || !Item.IsValid() || !Mesh->GetMaterials().IsValidIndex(*Item))
        {
            return LOCTEXT("NoReferenceMaterialSlot", "No material slot");
        }
        return FText::Format(LOCTEXT("ReferenceMaterialSlotItem", "[{0}] {1}"), FText::AsNumber(*Item), FText::FromName(Mesh->GetMaterials()[*Item].MaterialSlotName));
    }

    static float EdgeFunction(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
    {
        return (Point.X - A.X) * (B.Y - A.Y) - (Point.Y - A.Y) * (B.X - A.X);
    }

    static bool IsPointInTriangle(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const float AB = EdgeFunction(A, B, Point);
        const float BC = EdgeFunction(B, C, Point);
        const float CA = EdgeFunction(C, A, Point);
        return (AB >= 0.0f && BC >= 0.0f && CA >= 0.0f) || (AB <= 0.0f && BC <= 0.0f && CA <= 0.0f);
    }

    static float ApplyAddress(const float Value, const TextureAddress Address)
    {
        return Address == TA_Wrap ? FMath::Frac(Value) : FMath::Clamp(Value, 0.0f, 1.0f);
    }

    static FLinearColor SampleTextureBilinear(const FWetClothingTextureReadback& Readback, const FVector2D& UV)
    {
        const float U = ApplyAddress(UV.X, Readback.AddressX);
        const float V = ApplyAddress(UV.Y, Readback.AddressY);
        const float X = U * static_cast<float>(Readback.Width - 1);
        const float Y = V * static_cast<float>(Readback.Height - 1);
        const int32 X0 = FMath::FloorToInt(X);
        const int32 Y0 = FMath::FloorToInt(Y);
        const int32 X1 = FMath::Min(X0 + 1, Readback.Width - 1);
        const int32 Y1 = FMath::Min(Y0 + 1, Readback.Height - 1);
        return FMath::Lerp(
            FMath::Lerp(Readback.GetLinearColor(X0, Y0), Readback.GetLinearColor(X1, Y0), X - X0),
            FMath::Lerp(Readback.GetLinearColor(X0, Y1), Readback.GetLinearColor(X1, Y1), X - X0),
            Y - Y0);
    }

    void RefreshUVIslandView()
    {
        if (!UVIslandView.IsValid())
        {
            return;
        }

        UVIslandView->SetBackgroundTexture(ReferenceTexture.Get());
        UVIslandView->SetSelectedIslands({});
        IslandOptions.Reset();
        StatusMessage.Reset();

        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        if (Mesh == nullptr || SelectedSlotIndex == INDEX_NONE || SelectedUVChannel == INDEX_NONE)
        {
            UVIslandView->Clear();
            StatusMessage = TEXT("Select a reference mesh, material slot, and UV channel.");
            return;
        }

        FString ErrorMessage;
        if (!FWCAUVIslandViewCache::GetMaterialSlotUVIslands(Mesh, 0, SelectedUVChannel, SelectedSlotIndex, IslandOptions, &ErrorMessage))
        {
            UVIslandView->Clear();
            StatusMessage = ErrorMessage;
            return;
        }

        UVIslandView->SetBackgroundTexture(ReferenceTexture.Get());
        UVIslandView->SetIslands(IslandOptions);
        StatusMessage = ReferenceTexture.IsValid()
            ? TEXT("Select one UV island to calculate its average color.")
            : TEXT("Select a color texture, then select one UV island.");
    }

    void HandleUVIslandSelectionChanged(const TArray<int32>& UVIslandIDs, EWCAUVSelectionOp)
    {
        SelectedIslandID = UVIslandIDs.IsEmpty() ? INDEX_NONE : UVIslandIDs.Last();
        if (UVIslandView.IsValid())
        {
            TSet<int32> Selected;
            if (SelectedIslandID != INDEX_NONE)
            {
                Selected.Add(SelectedIslandID);
            }
            UVIslandView->SetSelectedIslands(Selected);
        }
        UpdateCandidateColor();
    }

    void UpdateCandidateColor()
    {
        bHasCandidateColor = false;
        UTexture2D* Texture = ReferenceTexture.Get();
        const TSharedPtr<FWetClothingAssetUVIsland>* Island = IslandOptions.FindByPredicate([this](const TSharedPtr<FWetClothingAssetUVIsland>& Item)
        {
            return Item.IsValid() && Item->UVIslandID == SelectedIslandID;
        });
        if (Texture == nullptr || Island == nullptr || !Island->IsValid())
        {
            StatusMessage = Texture == nullptr ? TEXT("Select a color texture.") : TEXT("Select one UV island.");
            return;
        }

        FWetClothingTextureReadback Readback;
        FString ErrorMessage;
        if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(Texture, Readback, ErrorMessage))
        {
            StatusMessage = ErrorMessage;
            return;
        }

        const int32 AnalysisWidth = FMath::Clamp(Readback.Width, 1, 1024);
        const int32 AnalysisHeight = FMath::Clamp(Readback.Height, 1, 1024);
        TBitArray<> CoveredPixels(false, AnalysisWidth * AnalysisHeight);
        const FWetClothingAssetUVIsland& SelectedIsland = **Island;
        for (const FWetClothingAssetUVTriangle& Triangle : SelectedIsland.UVTriangles)
        {
            const FVector2D A = Triangle.UVs[0] * FVector2D(AnalysisWidth, AnalysisHeight);
            const FVector2D B = Triangle.UVs[1] * FVector2D(AnalysisWidth, AnalysisHeight);
            const FVector2D C = Triangle.UVs[2] * FVector2D(AnalysisWidth, AnalysisHeight);
            const int32 MinX = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.X, B.X, C.X)), 0, AnalysisWidth - 1);
            const int32 MaxX = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.X, B.X, C.X)), 0, AnalysisWidth - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt(FMath::Min3(A.Y, B.Y, C.Y)), 0, AnalysisHeight - 1);
            const int32 MaxY = FMath::Clamp(FMath::CeilToInt(FMath::Max3(A.Y, B.Y, C.Y)), 0, AnalysisHeight - 1);
            for (int32 Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32 X = MinX; X <= MaxX; ++X)
                {
                    if (IsPointInTriangle(FVector2D(X + 0.5f, Y + 0.5f), A, B, C))
                    {
                        CoveredPixels[Y * AnalysisWidth + X] = true;
                    }
                }
            }
        }

        FLinearColor Sum = FLinearColor::Black;
        int32 SampleCount = 0;
        for (int32 Y = 0; Y < AnalysisHeight; ++Y)
        {
            for (int32 X = 0; X < AnalysisWidth; ++X)
            {
                if (!CoveredPixels[Y * AnalysisWidth + X])
                {
                    continue;
                }
                Sum += SampleTextureBilinear(Readback, FVector2D((X + 0.5f) / AnalysisWidth, (Y + 0.5f) / AnalysisHeight));
                ++SampleCount;
            }
        }
        if (SampleCount == 0)
        {
            StatusMessage = TEXT("The selected island did not cover any readable texture pixels.");
            return;
        }

        CandidateColor = Sum / static_cast<float>(SampleCount);
        CandidateColor.A = 1.0f;
        bHasCandidateColor = true;
        StatusMessage = FString::Printf(TEXT("Island %d: averaged %d covered samples."), SelectedIslandID, SampleCount);
    }

    FReply HandleCancelClicked()
    {
        if (const TSharedPtr<SWindow> Window = ParentWindow.Pin())
        {
            Window->RequestDestroyWindow();
        }
        return FReply::Handled();
    }

    FReply HandleApplyClicked()
    {
        if (bHasCandidateColor && OnColorAccepted.IsBound())
        {
            FDWCTransparencyUVIslandColorSelection Selection;
            Selection.Color = CandidateColor;
            Selection.Texture = ReferenceTexture;
            Selection.MaterialSlotIndex = SelectedSlotIndex;
            Selection.UVChannelIndex = SelectedUVChannel;
            Selection.UVIslandID = SelectedIslandID;
            OnColorAccepted.Execute(Selection);
        }
        return HandleCancelClicked();
    }

private:
    TWeakPtr<SWindow> ParentWindow;
    TWeakObjectPtr<USkeletalMesh> ReferenceMesh;
    TWeakObjectPtr<UTexture2D> ReferenceTexture;
    FOnDWCTransparencyUVIslandColorAccepted OnColorAccepted;
    TSharedPtr<SWCAUVView> UVIslandView;
    TSharedPtr<SListView<TSharedPtr<int32>>> SlotListView;
    TArray<TSharedPtr<int32>> SlotOptions;
    TArray<TSharedPtr<int32>> UVChannelOptions;
    TArray<TSharedPtr<FWCATextureItem>> TextureOptions;
    TSharedPtr<FWCATextureItem> SelectedTextureItem;
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> IslandOptions;
    int32 PreferredOriginalUVChannel = 0;
    int32 SelectedSlotIndex = INDEX_NONE;
    int32 SelectedUVChannel = INDEX_NONE;
    int32 SelectedIslandID = INDEX_NONE;
    FLinearColor CandidateColor = FLinearColor::White;
    FString StatusMessage;
    bool bHasCandidateColor = false;
};

FDWCTransparencyPreviewSettings MakeTransparencyPreviewSettings(
    const FWetClothingTransparencyData& Data)
{
    FDWCTransparencyPreviewSettings Settings;
    Settings.TransparencyStrength = Data.TransparencyPreviewStrength;
    Settings.WrinkleSuppressionStrength = Data.WrinkleSuppressionStrength;
    Settings.WrinkleMaskThreshold = Data.WrinkleSuppressionCoverageThreshold;
    Settings.WrinkleMaskSoftness = Data.WrinkleSuppressionMaskSoftness;
    return Settings;
}
}

void SWetClothingTransparencyBakePanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    AuthoringDocument = InArgs._AuthoringDocument;
    if (!AuthoringDocument.IsValid())
    {
        AuthoringDocument = MakeShared<FDWCEditorAuthoringDocument>(WetClothingAsset.Get());
    }
    SessionStore = InArgs._SessionStore;
    if (!SessionStore.IsValid())
    {
        SessionStore = MakeShared<FDWCEditorSessionStore>();
    }
    InitializeCharacterTypeSessionState();
    AuthoringController = MakeShared<FDWCTransparencyAuthoringController>(
        WetClothingAsset.Get(), AuthoringDocument, SessionStore);
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    BakeCoordinator = InArgs._BakeCoordinator;
    WrinkleSuppressionCoverageService = InArgs._WrinkleSuppressionCoverageService;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
    PreviewCommitCoordinator = InArgs._PreviewCommitCoordinator;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    ResourceGovernor = InArgs._ResourceGovernor;
    if (!RenderUploadQueue.IsValid())
    {
        RenderUploadQueue = MakeShared<FDWCEditorRenderUploadQueue>();
    }
    if (!TextureWorkspace.IsValid())
    {
        TextureWorkspace = MakeShared<FDWCEditorTextureWorkspace>(RenderUploadQueue.ToSharedRef());
    }
    if (!PreviewCommitCoordinator.IsValid())
    {
        PreviewCommitCoordinator = MakeShared<FDWCEditorPreviewCommitCoordinator>(
            TextureWorkspace.ToSharedRef(),
            WorkerJobScheduler.IsValid() ? WorkerJobScheduler->GetSessionEpoch() : FGuid());
    }
    DetailsView = InArgs._DetailsView;
    FDWCInitializeTransparencyPreviewSettingsAction InitializePreviewSettings;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        InitializePreviewSettings.Settings =
            MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
    }
    SessionStore->Dispatch(InitializePreviewSettings);
    SessionStore->OnChanged().AddSP(this, &SWetClothingTransparencyBakePanel::HandleSessionStateChanged);
    ThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::BaseRevealColor));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::InnerColor));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::CorrectionDifference));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RaycastGaps));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::ValidHit));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::HitDistance));
    RevealVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourcePriority));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::Final));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::AutoAlpha));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::WrinkleSeparation));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RevealNormalOnly));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::RevealNormalTexture));
    FinalVisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourceCoverage));
    BlueprintSourceRoleItems.Add(MakeShared<EDWCTransparencyBlueprintSourceRole>(EDWCTransparencyBlueprintSourceRole::RevealSource));
    BlueprintSourceRoleItems.Add(MakeShared<EDWCTransparencyBlueprintSourceRole>(EDWCTransparencyBlueprintSourceRole::BlockerOnly));
    SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    DispatchTransparencyPreviewState();
    RefreshModelState();
    RebuildEditorLayout();
    RefreshViewportContext();
}

SWetClothingTransparencyBakePanel::~SWetClothingTransparencyBakePanel()
{
    if (PendingRevealCommitTicket.IsValid() && WorkerJobScheduler.IsValid())
    {
        WorkerJobScheduler->Cancel(PendingRevealCommitTicket.Key);
        PendingRevealCommitTicket = {};
        ++RevealCommitEpoch;
    }
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
        AuthoringController->DetachViewport();
        AuthoringController.Reset();
    }
    if (SessionStore.IsValid())
    {
        SessionStore->OnChanged().RemoveAll(this);
    }
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPreviewState()
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSetTransparencyPreviewAction Action;
    Action.Stage = GetCurrentStage();
    Action.PreviewMode = PreviewViewport.IsValid()
        ? PreviewViewport->GetPreviewMode()
        : EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    Action.VisualizationMode = SelectedVisualizationMode;
    Action.WetnessPreviewPercent = WetnessPreviewPercent;
    Action.Settings = GetTransparencyPreviewSettings();
    Action.bShowSavedWrinkle = bShowSavedWrinkle;
    SessionStore->Dispatch(Action);
}

FDWCTransparencyPreviewSettings SWetClothingTransparencyBakePanel::GetTransparencyPreviewSettings() const
{
    if (SessionStore.IsValid())
    {
        return SessionStore->GetState().Transparency.PreviewSettings;
    }

    FDWCTransparencyPreviewSettings Settings;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Settings = MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
    }
    return Settings;
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPreviewSettings(
    FDWCTransparencyPreviewSettings Settings)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSetTransparencyPreviewAction Action;
    const FDWCEditorTransparencySessionState& Current = SessionStore->GetState().Transparency;
    Action.Stage = GetCurrentStage();
    Action.PreviewMode = Current.PreviewMode;
    Action.VisualizationMode = Current.VisualizationMode;
    Action.WetnessPreviewPercent = Current.WetnessPreviewPercent;
    Action.Settings = MoveTemp(Settings);
    Action.bShowSavedWrinkle = Current.bShowSavedWrinkle;
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyPaintState(
    const EDWCEditorSessionEffect Effects)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCTransparencyPaintSettings Paint;
    Paint.Mode = BrushMode;
    Paint.RadiusUV = BrushRadiusUV;
    Paint.Strength = BrushStrength;
    Paint.Falloff = BrushFalloff;
    Paint.Spacing = BrushSpacing;
    Paint.TargetAlpha = BrushTargetAlpha;
    Paint.bEnabled = true;
    Paint.bRevealColorPaint = false;

    FDWCSetTransparencyPaintAction Action;
    Action.Paint = Paint;
    Action.bRevealPaint = false;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

FDWCTransparencyPaintSettings SWetClothingTransparencyBakePanel::GetRevealPaintSettingsFromSession() const
{
    if (SessionStore.IsValid())
    {
        return SessionStore->GetState().Transparency.RevealPaint;
    }

    // Construction can briefly precede session attachment. This fallback is
    // display-only; all interactive changes still require a session action.
    FDWCTransparencyPaintSettings Settings;
    Settings.bRevealColorPaint = true;
    Settings.bEnabled = true;
    Settings.RevealColorMode = RevealPaintMode;
    Settings.RadiusUV = RevealPaintRadiusUV;
    Settings.Strength = RevealPaintStrength;
    Settings.Falloff = RevealPaintFalloff;
    Settings.Spacing = 0.25f;
    Settings.TargetAlpha = 1.0f;
    Settings.RevealColor = RevealPaintColor;
    return Settings;
}

void SWetClothingTransparencyBakePanel::DispatchRevealPaintState(
    FDWCTransparencyPaintSettings Settings,
    const EDWCEditorSessionEffect Effects)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    Settings.bRevealColorPaint = true;
    Settings.bEnabled = true;
    FDWCSetTransparencyPaintAction Action;
    Action.Paint = MoveTemp(Settings);
    Action.bRevealPaint = true;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::DispatchTransparencyEditContext()
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const EDWCTransparencyEditorStage Stage = GetCurrentStage();
    FDWCSetTransparencyEditContextAction Action;
    Action.Context.LayerGuid = SelectedLayerGuid;
    Action.Context.MaterialSlotIndex =
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : SelectedMaterialSlotIndex;
    Action.Context.UVChannelIndex = GetTransparencyDataUVChannel();
    Action.Context.AddressMode = Layer != nullptr
        ? Layer->TargetSurface.UVAddressMode
        : EDWCTransparencyUVAddressMode::Clamp;
    Action.Context.PaintTarget = DWCTransparencyWorkflow::ResolvePaintTarget(
        Stage,
        Layer != nullptr ? Layer->SourceType : EDWCTransparencySourceType::SameMeshMaterialSlots);
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingMap = Layer != nullptr
        ? AutoBakeResults.Find(Layer->LayerGuid)
        : nullptr;
    Action.Context.bSurfacePaintingEnabled =
        Action.Context.PaintTarget != EDWCTransparencyPaintTarget::None &&
        WorkingMap != nullptr && WorkingMap->IsValid();
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::HandleSessionStateChanged(
    const FDWCEditorSessionState& State,
    const EDWCEditorSessionEffect Effects,
    uint64)
{
    TGuardValue<bool> Guard(bApplyingSessionState, true);
    const FDWCEditorTransparencySessionState& TransparencyState = State.Transparency;
    if (AuthoringController.IsValid())
    {
        AuthoringController->HandleSessionStateChanged(State);
    }
    SelectedMaterialSlotIndex = TransparencyState.SelectedMaterialSlotIndex;
    SelectedLayerGuid = TransparencyState.SelectedLayerGuid;
    StageByLayer = TransparencyState.StageByLayer;
    SelectedVisualizationMode = TransparencyState.VisualizationMode;
    WetnessPreviewPercent = TransparencyState.WetnessPreviewPercent;
    bShowSavedWrinkle = TransparencyState.bShowSavedWrinkle;

    const FDWCTransparencyPaintSettings& Paint = TransparencyState.Paint;
    BrushMode = Paint.Mode;
    BrushRadiusUV = Paint.RadiusUV;
    BrushStrength = Paint.Strength;
    BrushFalloff = Paint.Falloff;
    BrushSpacing = Paint.Spacing;
    BrushTargetAlpha = Paint.TargetAlpha;
    const FDWCTransparencyPaintSettings& RevealPaint = TransparencyState.RevealPaint;
    RevealPaintMode = RevealPaint.RevealColorMode;
    RevealPaintRadiusUV = RevealPaint.RadiusUV;
    RevealPaintStrength = RevealPaint.Strength;
    RevealPaintFalloff = RevealPaint.Falloff;
    RevealPaintColor = RevealPaint.RevealColor;

    if (State.ActiveMode != EWCAEditorMode::TransparencyBake)
    {
        return;
    }

    if (!bPreviewSuspended && PreviewViewport.IsValid() &&
        EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::UpdatePreviewParameters))
    {
        // Derive the complete viewport state from the session in one place.
        // This also prepares the Stage 2 transient reveal-color map before
        // input is rebound to that paint target.
        RefreshViewportContext();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshStageContent))
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshElementList))
    {
        RefreshTransparencyStrokeList();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RebuildPreviewContent))
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::SyncSelection) && LayerListView.IsValid())
    {
        const FLayerItemPtr* Item = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Candidate)
            {
                return Candidate.IsValid() &&
                       Candidate->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        TGuardValue<bool> SelectionGuard(bRefreshingLayerSelection, true);
        Item != nullptr
            ? LayerListView->SetSelection(*Item, ESelectInfo::Direct)
            : LayerListView->ClearSelection();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshDetails) && DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

void SWetClothingTransparencyBakePanel::RebuildEditorLayout()
{
    const float PreviousScrollOffset = ControlPanelScrollBox.IsValid() ? ControlPanelScrollBox->GetScrollOffset() : 0.0f;
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

    // The switcher owns all stage roots for this panel lifetime. Select the
    // current stage only after it exists; do not rebuild a stage tree here.
    RefreshStageContent();

    if (LayerListView.IsValid())
    {
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item)
            {
                return Item.IsValid() &&
                       Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
}

bool SWetClothingTransparencyBakePanel::RefreshOptionItems()
{
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
    const bool bBaseOptionsChanged = OptionItemsTargetMesh.Get() != Mesh ||
        OptionItemsMaterialSlotCount != MaterialSlotCount || OptionItemsUVChannelCount != NumUVChannels;
    if (!bBaseOptionsChanged && !bPreviewSlotStateRefreshRequested)
    {
        return false;
    }

    FDWCEditorPreviewSlotCollection ResolvedPreviewSlotStates =
        FDWCEditorPreviewSlotResolver::Resolve(Asset);
    const uint32 PreviewStateSignature = ResolvedPreviewSlotStates.StateSignature;
    const bool bMeshOptionsChanged = bBaseOptionsChanged ||
        OptionItemsPreviewStateSignature != PreviewStateSignature;
    bPreviewSlotStateRefreshRequested = false;
    if (!bMeshOptionsChanged)
    {
        return false;
    }

    OptionItemsTargetMesh = const_cast<USkeletalMesh*>(Mesh);
    OptionItemsMaterialSlotCount = MaterialSlotCount;
    OptionItemsUVChannelCount = NumUVChannels;
    OptionItemsPreviewStateSignature = PreviewStateSignature;
    PreviewSlotStates = MoveTemp(ResolvedPreviewSlotStates);
    MaterialSlotItems.Reset();
    TargetMaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();
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
            const FDWCEditorPreviewSlotState* PreviewState = PreviewSlotStates.Find(SlotIndex);
            if (PreviewState != nullptr && PreviewState->bWettable)
            {
                TargetMaterialSlotItems.Add(Item);
            }
        }
    }
    for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
    {
        UVChannelItems.Add(MakeShared<int32>(UVChannelIndex));
    }
    return true;
}

static const TCHAR* GetRevealColorStrokeModeLabel(const EDWCTransparencyRevealColorBrushMode Mode)
{
    switch (Mode)
    {
    case EDWCTransparencyRevealColorBrushMode::EraseToBase:
        return TEXT("Erase to Base");
    case EDWCTransparencyRevealColorBrushMode::Smooth:
        return TEXT("Smooth");
    case EDWCTransparencyRevealColorBrushMode::Paint:
    default:
        return TEXT("Paint");
    }
}

void SWetClothingTransparencyBakePanel::RefreshLayerItems()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    bool bLayerItemsChanged = LayerItems.Num() != TargetMaterialSlotItems.Num();
    if (!bLayerItemsChanged)
    {
        for (int32 ItemIndex = 0; ItemIndex < LayerItems.Num(); ++ItemIndex)
        {
            const FLayerItemPtr& ExistingItem = LayerItems[ItemIndex];
            const FMaterialSlotItemPtr& TargetSlotItem = TargetMaterialSlotItems[ItemIndex];
            const FWetClothingTransparencyLayerData* ExistingLayer =
                Asset != nullptr && TargetSlotItem.IsValid()
                    ? Asset->Authored.TransparencyData.FindTransparencyLayer(TargetSlotItem->SlotIndex)
                    : nullptr;
            const FGuid ExpectedGuid = ExistingLayer != nullptr
                ? ExistingLayer->LayerGuid
                : FGuid();
            if (!ExistingItem.IsValid() || !TargetSlotItem.IsValid() ||
                ExistingItem->MaterialSlotIndex != TargetSlotItem->SlotIndex ||
                ExistingItem->MaterialSlotName != TargetSlotItem->SlotName ||
                ExistingItem->LayerGuid != ExpectedGuid)
            {
                bLayerItemsChanged = true;
                break;
            }
        }
    }
    if (bLayerItemsChanged)
    {
        LayerItems.Reset();
        for (const FMaterialSlotItemPtr& TargetSlotItem : TargetMaterialSlotItems)
        {
            if (!TargetSlotItem.IsValid())
            {
                continue;
            }

            FLayerItemPtr Item = MakeShared<FDWCTransparencyLayerListItem>();
            Item->MaterialSlotIndex = TargetSlotItem->SlotIndex;
            Item->MaterialSlotName = TargetSlotItem->SlotName;
            if (Asset != nullptr)
            {
                if (const FWetClothingTransparencyLayerData* ExistingLayer =
                        Asset->Authored.TransparencyData.FindTransparencyLayer(Item->MaterialSlotIndex))
                {
                    Item->LayerGuid = ExistingLayer->LayerGuid;
                }
            }
            LayerItems.Add(Item);
        }
    }

    const bool bSelectedSlotStillExists = LayerItems.ContainsByPredicate(
        [this](const FLayerItemPtr& Item)
        {
            return Item.IsValid() && Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
        });
    if (!bSelectedSlotStillExists && SelectedMaterialSlotIndex != INDEX_NONE)
    {
        SelectedMaterialSlotIndex = INDEX_NONE;
        SelectedLayerGuid.Invalidate();
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSelectTransparencyLayerAction{FGuid(), INDEX_NONE});
        }
    }
    else if (bSelectedSlotStillExists)
    {
        const FWetClothingTransparencyLayerData* SelectedLayer =
            Asset != nullptr
                ? Asset->Authored.TransparencyData.FindTransparencyLayer(SelectedMaterialSlotIndex)
                : nullptr;
        SelectedLayerGuid = SelectedLayer != nullptr ? SelectedLayer->LayerGuid : FGuid();
    }
    EnsureStageForSelectedLayer();
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
        const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
            [this](const FLayerItemPtr& Item)
            {
                return Item.IsValid() &&
                       Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
            });
        bRefreshingLayerSelection = true;
        SelectedItem != nullptr ? LayerListView->SetSelection(*SelectedItem, ESelectInfo::Direct) : LayerListView->ClearSelection();
        bRefreshingLayerSelection = false;
    }
}

void SWetClothingTransparencyBakePanel::RefreshFromAsset()
{
    // Asset-wide refreshes arrive after saves, external edits and editor mode
    // changes. They update model/viewport state, but must not reconstruct the
    // active Stage 2/3 subtree. Stage content is refreshed explicitly only for
    // a stage or source-type change, or after an operation that changed its
    // generated-output data.
    bPreviewSlotStateRefreshRequested = true;
    if (SessionStore.IsValid())
    {
        if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
        {
            ReconcileCharacterTypeSessionState();
            FDWCInitializeTransparencyPreviewSettingsAction SyncPreviewSettings;
            SyncPreviewSettings.Settings =
                MakeTransparencyPreviewSettings(Asset->Authored.TransparencyData);
            SyncPreviewSettings.bForce = true;
            SessionStore->Dispatch(SyncPreviewSettings);
        }
    }
    EDWCTransparencyPanelRefreshFlags Flags = EDWCTransparencyPanelRefreshFlags::Model;
    if (!bPreviewSuspended)
    {
        Flags |= EDWCTransparencyPanelRefreshFlags::Viewport;
    }
    RequestRefresh(Flags);
}

void SWetClothingTransparencyBakePanel::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
{
    if (bPreviewSuspended)
    {
        return;
    }

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SuspendPreview(Reason);
    }
    bPreviewSuspended = true;
}

void SWetClothingTransparencyBakePanel::ResumePreviewIfNeeded()
{
    if (!bPreviewSuspended)
    {
        return;
    }

    bPreviewSuspended = false;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ResumePreviewIfNeeded();
    }
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
}

bool SWetClothingTransparencyBakePanel::RefreshModelState()
{
    const bool bOptionItemsChanged = RefreshOptionItems();
    RefreshLayerItems();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    RefreshBlueprintHierarchy(false);
    RefreshBlueprintSourcePriorityItems();
    RefreshExternalSourcePriorityItems();
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
    else if (Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        StatusMessage = FString::Printf(
            TEXT("Unsupported Transparency data version %d (current: %d). Recreate the Transparency setup for this WCA."),
            Asset->Authored.TransparencyData.DataVersion,
            FWetClothingTransparencyData::CurrentDataVersion);
        PanelStatus = EDWCTransparencyPanelStatus::Error;
    }
    else if (Layer == nullptr)
    {
        StatusMessage = TEXT("Select a ready Wettable material slot as a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Info;
    }
    else if (!HasUsableTransparencyDataUV())
    {
        StatusMessage = TEXT("Generate the DWC UV Channel before configuring Transparency Target Parts.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else if (!PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        const FDWCEditorPreviewSlotState* State =
            FindPreviewSlotState(Layer->TargetSurface.OuterMaterialSlotIndex);
        StatusMessage = State != nullptr
            ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
            : TEXT("The selected Transparency Target Part is unavailable for preview.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else
    {
        TArray<FString> Errors;
        const TSharedPtr<FDWCTransparencySourcePayload>* Existing =
            AutoBakeResults.Find(Layer->LayerGuid);
        if (GetCurrentStage() == EDWCTransparencyEditorStage::FinalEditing &&
            Existing != nullptr && Existing->IsValid())
        {
            const FDWCTransparencySourcePayload& Result = **Existing;
            if (Result.bIsFinalBakedBaseline)
            {
                StatusMessage = FString::Printf(
                    TEXT("Baked map loaded as an editable baseline. New brush edits: %d."),
                    FMath::Max(Layer->GetEditableStrokes().Num() - Result.BaselineStrokeCount, 0));
                PanelStatus = EDWCTransparencyPanelStatus::Ready;
            }
            else
            {
                StatusMessage = Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture
                    ? FString::Printf(
                        TEXT("Base-color preview map ready. Target texels: %d, UV Overlaps: %d"),
                        Result.OuterSampleCount,
                        Result.OverlappedUVPixelCount)
                    : FString::Printf(
                        TEXT("Preview map ready. Samples: %d, Valid Hits: %d, No Hits: %d, UV Overlaps: %d"),
                        Result.OuterSampleCount,
                        Result.ValidHitCount,
                        Result.NoHitCount,
                        Result.OverlappedUVPixelCount);
                PanelStatus = Result.OverlappedUVPixelCount > 0 ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
            }
        }
        else if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                     Asset->GetDWCSkeletalMesh(), *Layer, Errors, ResolveTransparencyDataUVChannel(Asset)))
        {
            StatusMessage = FString::Join(Errors, TEXT("\n"));
            PanelStatus = EDWCTransparencyPanelStatus::Error;
        }
        else if (const FWetClothingBakedTransparencyMap* BakedMap =
                     FindExactBakedTransparencyMap(Asset, Layer))
        {
            const bool bFinalBakeCurrent =
                BakedMap->BakeGuid.IsValid() && !BakedMap->BuildSignature.IsEmpty();
            StatusMessage = bFinalBakeCurrent
                ? FString::Printf(
                    TEXT("Baked map is available but its editable source data could not be loaded: %s."),
                    *GetNameSafe(BakedMap->TransparencyMap))
                : FString::Printf(
                    TEXT("A stale baked map is available for inspection: %s. Generate Transparency Map and rebake to update it."),
                    *GetNameSafe(BakedMap->TransparencyMap));
            PanelStatus = bFinalBakeCurrent
                ? EDWCTransparencyPanelStatus::Ready
                : EDWCTransparencyPanelStatus::Warning;
        }
        else
        {
            StatusMessage = TEXT("Ready for automatic transparency generation.");
            PanelStatus = EDWCTransparencyPanelStatus::Ready;
        }
    }
    UpdateInnerSourceStatus();
    // Edit context follows authoring state reconciliation. The viewport only
    // consumes it, preventing a viewport refresh from becoming a state write.
    DispatchTransparencyEditContext();
    return bOptionItemsChanged;
}

EDWCTransparencyEditorStage SWetClothingTransparencyBakePanel::GetCurrentStage() const
{
    if (const EDWCTransparencyEditorStage* Stage = StageByLayer.Find(SelectedLayerGuid))
    {
        return *Stage;
    }
    return ResolveSelectedLayerWorkflowState().DefaultStage;
}

void SWetClothingTransparencyBakePanel::EnsureStageForSelectedLayer()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FGuid LayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    const EDWCTransparencyEditorStage ResolvedStage = ResolveStageForLayer(Layer);
    const EDWCTransparencyEditorStage* ExistingStage = StageByLayer.Find(LayerGuid);
    if (ExistingStage != nullptr && *ExistingStage == ResolvedStage)
    {
        return;
    }

    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{LayerGuid, ResolvedStage});
    }
    else
    {
        StageByLayer.FindOrAdd(LayerGuid) = ResolvedStage;
    }
}

FText GetBlueprintSourceRoleLabel(const EDWCTransparencyBlueprintSourceRole Role)
{
    return Role == EDWCTransparencyBlueprintSourceRole::BlockerOnly
        ? LOCTEXT("BlueprintSourceRoleBlocker", "Blocker Only")
        : LOCTEXT("BlueprintSourceRoleReveal", "Reveal Source");
}

DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState
SWetClothingTransparencyBakePanel::ResolveSelectedLayerWorkflowState() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const bool bHasBakedBaseline = FindExactBakedTransparencyMap(Asset, Layer) != nullptr;
    return DWCTransparencyWorkflow::ResolveLayerWorkflowState(
        Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
        Layer,
        bHasBakedBaseline);
}

EDWCTransparencyEditorStage SWetClothingTransparencyBakePanel::ResolveStageForLayer(
    const FWetClothingTransparencyLayerData* Layer) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const bool bHasBakedBaseline = FindExactBakedTransparencyMap(Asset, Layer) != nullptr;
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState WorkflowState =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(
            Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
            Layer,
            bHasBakedBaseline);
    const FGuid LayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    if (const EDWCTransparencyEditorStage* SessionStage = StageByLayer.Find(LayerGuid))
    {
        return DWCTransparencyWorkflow::NormalizeRequestedStage(*SessionStage, WorkflowState);
    }
    return WorkflowState.DefaultStage;
}

void SWetClothingTransparencyBakePanel::SelectTransparencyLayerWithResolvedStage(
    const int32 MaterialSlotIndex,
    const FGuid& LayerGuid,
    const EDWCTransparencyEditorStage Stage)
{
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(
            FDWCSelectTransparencyLayerAndStageAction{LayerGuid, Stage, MaterialSlotIndex});
        return;
    }

    SelectedMaterialSlotIndex = MaterialSlotIndex;
    SelectedLayerGuid = LayerGuid;
    StageByLayer.FindOrAdd(LayerGuid) = Stage;
}

bool SWetClothingTransparencyBakePanel::CanEnterFinalEditingStage() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || Asset == nullptr)
    {
        return false;
    }

    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        const EDWCTransparencyEditorStage CurrentStage = GetCurrentStage();
        return (*Existing)->bIsFinalBakedBaseline ||
            CurrentStage == EDWCTransparencyEditorStage::RevealEditing ||
            CurrentStage == EDWCTransparencyEditorStage::FinalEditing;
    }

    return ResolveSelectedLayerWorkflowState().CanEnterFinalEditing();
}

bool SWetClothingTransparencyBakePanel::CanEnterRevealEditingStage() const
{
    if (bRevealCommitInFlight)
    {
        return false;
    }
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
    return (Existing != nullptr && Existing->IsValid() && !(*Existing)->bIsFinalBakedBaseline) ||
        ResolveSelectedLayerWorkflowState().CanEnterRevealEditing();
}

void SWetClothingTransparencyBakePanel::SetCurrentStage(const EDWCTransparencyEditorStage Stage)
{
    if (Stage != EDWCTransparencyEditorStage::StructureSetup && HasDirtySourceTypeDraft())
    {
        StatusMessage = TEXT("Apply or cancel the pending Character Structure change before leaving Stage 1.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return;
    }

    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Stage == EDWCTransparencyEditorStage::RevealEditing && !CanEnterRevealEditingStage())
    {
        StatusMessage = TEXT("Generate the Stage 2 source map before entering Reveal Color editing.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::RevealEditing && !EnsureRevealEditingWorkingMap())
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::FinalEditing && !CanEnterFinalEditingStage())
    {
        StatusMessage = TEXT("Complete source generation or load an existing Transparency Map before entering Stage 4.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    if (Stage == EDWCTransparencyEditorStage::FinalEditing && !EnsureFinalEditingWorkingMap())
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    const FGuid StageLayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{StageLayerGuid, Stage});
    }
    else
    {
        StageByLayer.FindOrAdd(StageLayerGuid) = Stage;
    }
    if (Stage == EDWCTransparencyEditorStage::RevealEditing)
    {
        SelectedVisualizationMode = GetVisualizationModeForStage(Stage);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
        }
        EnsureRevealEditingWorkingMap();
    }
    else if (Stage == EDWCTransparencyEditorStage::FinalEditing)
    {
        SelectedVisualizationMode = GetVisualizationModeForStage(Stage);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
        }
        EnsureFinalEditingWorkingMap();
    }
    DispatchTransparencyPreviewState();
    DispatchTransparencyEditContext();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

FReply SWetClothingTransparencyBakePanel::HandleStageClicked(const EDWCTransparencyEditorStage Stage)
{
    SetCurrentStage(Stage);
    return FReply::Handled();
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsStageChecked(const EDWCTransparencyEditorStage Stage) const
{
    return GetCurrentStage() == Stage ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsSourceTypeCardChecked(
    const EDWCTransparencySourceType SourceType) const
{
    const FDWCEditorTransparencySessionState* Transparency = SessionStore.IsValid()
        ? &SessionStore->GetState().Transparency
        : nullptr;
    return Transparency != nullptr &&
        Transparency->bDraftCharacterTypeConfigured &&
        Transparency->DraftCharacterType == SourceType
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

FText SWetClothingTransparencyBakePanel::GetSourceTypeCardStatusText(
    const EDWCTransparencySourceType SourceType,
    const FText Availability) const
{
    if (!IsSourceTypeAvailable(SourceType) || !SessionStore.IsValid())
    {
        return Availability;
    }

    const FDWCEditorTransparencySessionState& Transparency =
        SessionStore->GetState().Transparency;
    if (Transparency.bDraftCharacterTypeConfigured &&
        Transparency.DraftCharacterType == SourceType &&
        Transparency.bCharacterTypeDraftDirty)
    {
        return LOCTEXT("TransparencyTypeSelectedNotApplied", "Selected - Not Applied");
    }
    if (Transparency.bSavedCharacterTypeConfigured &&
        Transparency.SavedCharacterType == SourceType)
    {
        return LOCTEXT("TransparencyTypeSaved", "Saved");
    }
    return Availability;
}

bool SWetClothingTransparencyBakePanel::HasDirtySourceTypeDraft() const
{
    return SessionStore.IsValid() &&
        SessionStore->GetState().Transparency.bCharacterTypeDraftDirty;
}

EVisibility SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusVisibility() const
{
    return HasDirtySourceTypeDraft() ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusText() const
{
    if (!SessionStore.IsValid())
    {
        return FText::GetEmpty();
    }

    return FText::Format(
        LOCTEXT(
            "TransparencySourceTypeDraftStatus",
            "Selected: {0} (not applied). Continue to Stage 2 to apply this change."),
        GetTransparencySourceTypeLabel(
            SessionStore->GetState().Transparency.DraftCharacterType));
}

bool SWetClothingTransparencyBakePanel::IsSourceTypeAvailable(
    const EDWCTransparencySourceType SourceType) const
{
    return DWCTransparencyWorkflow::IsSourceTypeAvailable(SourceType);
}

bool SWetClothingTransparencyBakePanel::CanContinueToGeneration() const
{
    const FDWCEditorTransparencySessionState* Transparency = SessionStore.IsValid()
        ? &SessionStore->GetState().Transparency
        : nullptr;
    return DWCTransparencyWorkflow::CanContinueToGeneration(
        WetClothingAsset.IsValid(),
        Transparency != nullptr && Transparency->bDraftCharacterTypeConfigured,
        Transparency != nullptr
            ? Transparency->DraftCharacterType
            : EDWCTransparencySourceType::SameMeshMaterialSlots);
}

void SWetClothingTransparencyBakePanel::InitializeCharacterTypeSessionState()
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCInitializeTransparencyCharacterTypeAction Action;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Action.SavedType = Asset->Authored.TransparencyData.CharacterStructureType;
        Action.bSavedTypeConfigured =
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
    }
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::ReconcileCharacterTypeSessionState()
{
    if (!SessionStore.IsValid())
    {
        return;
    }

    FDWCReconcileTransparencyCharacterTypeAction Action;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Action.SavedType = Asset->Authored.TransparencyData.CharacterStructureType;
        Action.bSavedTypeConfigured =
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
    }
    SessionStore->Dispatch(Action);
}

DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact
SWetClothingTransparencyBakePanel::EvaluateCharacterTypeChangeImpact() const
{
    DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact Impact;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return Impact;
    }

    const TArray<FWetClothingTransparencyLayerData>& Layers =
        Asset->Authored.TransparencyData.TransparencyLayers;
    Impact.LayerCount = Layers.Num();
    for (const FWetClothingTransparencyLayerData& Layer : Layers)
    {
        Impact.RevealStrokeCount += Layer.GetRevealColorPaintStrokes().Num();
        Impact.AlphaStrokeCount += Layer.GetEditableStrokes().Num();
        if (Layer.AutoBakeMetadata.AutoBakeGuid.IsValid() ||
            !Layer.AutoBakeMetadata.BuildSignature.IsEmpty())
        {
            ++Impact.GeneratedResultCount;
        }
        Impact.GeneratedResultCount += Layer.BakedMaps.Num();
    }
    return Impact;
}

FReply SWetClothingTransparencyBakePanel::HandleSourceTypeCardClicked(
    const EDWCTransparencySourceType SourceType)
{
    if (!WetClothingAsset.IsValid() || !SessionStore.IsValid())
    {
        return FReply::Handled();
    }

    if (!IsSourceTypeAvailable(SourceType))
    {
        StatusMessage = TEXT("This Transparency character structure is not implemented yet.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }

    SessionStore->Dispatch(FDWCSelectTransparencyCharacterTypeDraftAction{SourceType});
    if (GetCurrentStage() != EDWCTransparencyEditorStage::StructureSetup)
    {
        SetCurrentStage(EDWCTransparencyEditorStage::StructureSetup);
    }
    else
    {
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleCancelSourceTypeDraftClicked()
{
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCCancelTransparencyCharacterTypeDraftAction{});
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked()
{
    if (!CanContinueToGeneration() || !SessionStore.IsValid())
    {
        StatusMessage = TEXT("Choose an available character structure type before continuing to Stage 2.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }


    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    const FDWCEditorTransparencySessionState& TransparencySession =
        SessionStore->GetState().Transparency;
    const EDWCTransparencySourceType DraftType = TransparencySession.DraftCharacterType;
    if (!TransparencySession.bCharacterTypeDraftDirty)
    {
        SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
        return FReply::Handled();
    }

    const DWCTransparencyWorkflow::FDWCTransparencyTypeChangeImpact Impact =
        EvaluateCharacterTypeChangeImpact();
    if (Impact.RequiresConfirmation())
    {
        const FText Message = FText::Format(
            LOCTEXT(
                "ConfirmTransparencyCharacterTypeChange",
                "Change Transparency Character Structure?\n\n"
                "Current: {0}\nNew: {1}\n\n"
                "{2} target part(s) will use the new source type. Previous source settings will be cleared, and generated Transparency results will become out of date.\n\n"
                "Reveal Color strokes ({3}) and Transparency Alpha strokes ({4}) will be preserved."),
            GetTransparencySourceTypeLabel(TransparencySession.SavedCharacterType),
            GetTransparencySourceTypeLabel(DraftType),
            FText::AsNumber(Impact.LayerCount),
            FText::AsNumber(Impact.RevealStrokeCount),
            FText::AsNumber(Impact.AlphaStrokeCount));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Message) != EAppReturnType::Yes)
        {
            return FReply::Handled();
        }
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake |
        EDWCEditorAuthoringImpact::Details;
    const FDWCEditorAuthoringResult Result = AuthoringDocument.IsValid()
        ? AuthoringDocument->Edit(
            LOCTEXT("ChangeTransparencyStructureType", "Change Transparency Character Structure"),
            Change,
            [DraftType](UWetClothingAsset& MutableAsset)
            {
                DWCTransparencyWorkflow::ApplyCharacterTypeCommit(
                    MutableAsset.Authored.TransparencyData,
                    DraftType);
                return true;
            })
        : FDWCEditorAuthoringResult{};
    if (!Result.bChanged)
    {
        StatusMessage = Result.Error.IsEmpty()
            ? TEXT("Could not apply the Transparency character structure change.")
            : Result.Error;
        PanelStatus = EDWCTransparencyPanelStatus::Error;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        return FReply::Handled();
    }

    AutoBakeResults.Reset();
    SessionStore->Dispatch(FDWCCommitTransparencyCharacterTypeSucceededAction{DraftType});
    StatusMessage = FString::Printf(
        TEXT("Transparency character structure changed to %s. Generated results are out of date."),
        *GetTransparencySourceTypeLabel(DraftType).ToString());
    PanelStatus = EDWCTransparencyPanelStatus::Warning;
    SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::RefreshStageContent()
{
    if (!StageContentSwitcher.IsValid())
    {
        return;
    }

    switch (GetCurrentStage())
    {
    case EDWCTransparencyEditorStage::MapGeneration:
        StageContentSwitcher->SetActiveWidgetIndex(1);
        RefreshMapGenerationSettings();
        break;
    case EDWCTransparencyEditorStage::FinalEditing:
        StageContentSwitcher->SetActiveWidgetIndex(3);
        RefreshFinalEditingContent();
        break;
    case EDWCTransparencyEditorStage::RevealEditing:
        StageContentSwitcher->SetActiveWidgetIndex(2);
        RefreshRevealEditingContent();
        break;
    default:
        StageContentSwitcher->SetActiveWidgetIndex(0);
        break;
    }
}

void SWetClothingTransparencyBakePanel::RefreshMapGenerationSettings()
{
    if (!MapGenerationSettingsSwitcher.IsValid())
    {
        return;
    }

    int32 SettingsIndex = 0;
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        switch (Layer->SourceType)
        {
        case EDWCTransparencySourceType::SameMeshMaterialSlots:
            SettingsIndex = 1;
            break;
        case EDWCTransparencySourceType::OtherSkeletalMeshComponents:
            SettingsIndex = 2;
            break;
        case EDWCTransparencySourceType::ManualColorOrTexture:
            SettingsIndex = 3;
            break;
        case EDWCTransparencySourceType::ExternalSkeletalMesh:
            SettingsIndex = 4;
            break;
        default:
            break;
        }
    }

    MapGenerationSettingsSwitcher->SetActiveWidgetIndex(SettingsIndex);
    RefreshInnerSourceSlotItems();
    RefreshBlueprintHierarchy(false);
    RefreshBlueprintSourcePriorityItems();
    RefreshExternalSourcePriorityItems();
    RefreshRevealColorStrokeList();
}

void SWetClothingTransparencyBakePanel::RefreshInnerSourceSlotItems()
{
    InnerSourceSlotItems.Reset();
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        for (int32 PriorityIndex = 0; PriorityIndex < Layer->SameMeshSource.InnerSlotPriority.Num(); ++PriorityIndex)
        {
            InnerSourceSlotItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }

    if (InnerSourceListView.IsValid())
    {
        InnerSourceListView->RequestListRefresh();
    }
}

bool SWetClothingTransparencyBakePanel::IsBlueprintHierarchyCurrent() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr &&
        Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents &&
        BlueprintHierarchyLayerGuid == Layer->LayerGuid &&
        BlueprintHierarchyClassPath ==
            Layer->BlueprintSource.BlueprintClass.ToSoftObjectPath().ToString();
}

bool SWetClothingTransparencyBakePanel::IsBlueprintTargetCandidate(
    const FDWCTransparencyBlueprintMeshComponent& Component) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && Component.SkeletalMesh != nullptr &&
        (Component.SkeletalMesh == Asset->GetRuntimeSkeletalMesh() ||
            Component.SkeletalMesh == Asset->GetSourceSkeletalMesh());
}

int32 SWetClothingTransparencyBakePanel::GetBlueprintHierarchyDepth(
    const FDWCTransparencyBlueprintMeshComponent& Component) const
{
    return Component.HierarchyDepth;
}

void SWetClothingTransparencyBakePanel::RefreshBlueprintHierarchy(const bool bAllowAutoTarget)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr ||
        Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
        Layer->BlueprintSource.BlueprintClass.IsNull())
    {
        BlueprintHierarchyItems.Reset();
        BlueprintHierarchyLayerGuid.Invalidate();
        BlueprintHierarchyClassPath.Reset();
        BlueprintHierarchyError.Reset();
        if (BlueprintHierarchyListView.IsValid())
        {
            BlueprintHierarchyListView->RequestListRefresh();
        }
        return;
    }

    if (IsBlueprintHierarchyCurrent())
    {
        return;
    }

    BlueprintHierarchyItems.Reset();
    BlueprintHierarchyError.Reset();
    if (!bAllowAutoTarget)
    {
        BlueprintHierarchyLayerGuid.Invalidate();
        BlueprintHierarchyClassPath.Reset();
        BlueprintHierarchyError =
            TEXT("Blueprint hierarchy is not active. Click Refresh Hierarchy to load it.");
        if (BlueprintHierarchyListView.IsValid())
        {
            BlueprintHierarchyListView->RequestListRefresh();
        }
        return;
    }

    BlueprintHierarchyLayerGuid = Layer->LayerGuid;
    BlueprintHierarchyClassPath = Layer->BlueprintSource.BlueprintClass.ToSoftObjectPath().ToString();

    FDWCTransparencyBlueprintHierarchy Hierarchy;
    FString HierarchyError;
    const TSubclassOf<AActor> BlueprintClass = Layer->BlueprintSource.BlueprintClass.LoadSynchronous();
    FDWCEditorAuthoringPayloadDiagnostics::RecordExplicitLoad(
        BlueprintClass.Get(),
        TEXT("Transparency.RefreshBlueprintHierarchy"));
    if (!FDWCTransparencyProjectionSourceProvider::BuildBlueprintHierarchy(
            BlueprintClass, Hierarchy, HierarchyError))
    {
        BlueprintHierarchyError = MoveTemp(HierarchyError);
    }
    else
    {
        BlueprintHierarchyItems.Reserve(Hierarchy.MeshComponents.Num());
        for (const FDWCTransparencyBlueprintMeshComponent& Component : Hierarchy.MeshComponents)
        {
            BlueprintHierarchyItems.Add(MakeShared<FDWCTransparencyBlueprintMeshComponent>(Component));
        }

        if (bAllowAutoTarget && !Layer->BlueprintSource.TargetComponent.IsBound())
        {
            TArray<TSharedPtr<FDWCTransparencyBlueprintMeshComponent>> Candidates;
            for (const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>& Component : BlueprintHierarchyItems)
            {
                if (Component.IsValid() && IsBlueprintTargetCandidate(*Component))
                {
                    Candidates.Add(Component);
                }
            }
            if (Candidates.Num() == 1)
            {
                const TSharedPtr<FDWCTransparencyBlueprintMeshComponent> Candidate = Candidates[0];
                EditSelectedLayer(
                    LOCTEXT("AutoBindTransparencyBlueprintTarget", "Bind Transparency Blueprint Target"),
                    [Candidate](FWetClothingTransparencyLayerData& MutableLayer)
                    {
                        MutableLayer.BlueprintSource.TargetComponent.ComponentName = Candidate->ComponentName;
                        MutableLayer.BlueprintSource.TargetComponent.ExpectedSkeletalMesh = Candidate->SkeletalMesh;
                        MutableLayer.MarkAutoBakeStale();
                        MutableLayer.MarkFinalBakeStale();
                    },
                    false);
            }
        }
    }

    if (BlueprintHierarchyListView.IsValid())
    {
        BlueprintHierarchyListView->RequestListRefresh();
    }
    RefreshBlueprintSourcePriorityItems();
}

void SWetClothingTransparencyBakePanel::RefreshBlueprintSourcePriorityItems()
{
    BlueprintSourcePriorityItems.Reset();
    if (const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer())
    {
        for (int32 PriorityIndex = 0;
             PriorityIndex < Layer->BlueprintSource.SourcePriority.Num();
             ++PriorityIndex)
        {
            BlueprintSourcePriorityItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }
    if (BlueprintSourcePriorityListView.IsValid())
    {
        BlueprintSourcePriorityListView->RequestListRefresh();
    }
}

void SWetClothingTransparencyBakePanel::RefreshExternalSourcePriorityItems()
{
    ExternalSourcePriorityItems.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer != nullptr)
    {
        for (int32 PriorityIndex = 0;
             PriorityIndex < Layer->ExternalMeshSource.SourcePriority.Num();
             ++PriorityIndex)
        {
            ExternalSourcePriorityItems.Add(MakeShared<int32>(PriorityIndex));
        }
    }
    if (ExternalSourcePriorityListView.IsValid())
    {
        ExternalSourcePriorityListView->RequestListRefresh();
    }
}

FReply SWetClothingTransparencyBakePanel::HandleRefreshBlueprintHierarchyClicked()
{
    BlueprintHierarchyLayerGuid.Invalidate();
    BlueprintHierarchyClassPath.Reset();
    RefreshBlueprintHierarchy(true);
    UpdateInnerSourceStatus();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintTargetComponentChanged(
    const ECheckBoxState NewState,
    const FName ComponentName)
{
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>* Component =
        BlueprintHierarchyItems.FindByPredicate(
            [ComponentName](const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>& Candidate)
            {
                return Candidate.IsValid() && Candidate->ComponentName == ComponentName;
            });
    if (Component == nullptr || !Component->IsValid() || !IsBlueprintTargetCandidate(**Component))
    {
        return;
    }

    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintTarget", "Set Transparency Blueprint Target"),
        [NewState, ComponentName, Component = *Component](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FWetClothingTransparencyBlueprintSource& BlueprintSource = MutableLayer.BlueprintSource;
            if (NewState == ECheckBoxState::Checked)
            {
                BlueprintSource.TargetComponent.ComponentName = ComponentName;
                BlueprintSource.TargetComponent.ExpectedSkeletalMesh = Component->SkeletalMesh;
                BlueprintSource.SourcePriority.RemoveAll(
                    [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                    {
                        return Source.ComponentName == ComponentName;
                    });
            }
            else if (BlueprintSource.TargetComponent.ComponentName == ComponentName)
            {
                BlueprintSource.TargetComponent = FWetClothingTransparencyBlueprintComponentBinding{};
            }
            MutableLayer.MarkAutoBakeStale();
            MutableLayer.MarkFinalBakeStale();
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshBlueprintSourcePriorityItems();
    UpdateInnerSourceStatus();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        const bool bWasFullSourcePreview =
            PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        if (bWasFullSourcePreview)
        {
            PreviewViewport->InvalidateFullSourceLayout();
        }
    }
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceComponentChanged(
    const ECheckBoxState NewState,
    const FName ComponentName)
{
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>* Component =
        BlueprintHierarchyItems.FindByPredicate(
            [ComponentName](const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>& Candidate)
            {
                return Candidate.IsValid() && Candidate->ComponentName == ComponentName;
            });
    if (Component == nullptr || !Component->IsValid())
    {
        return;
    }

    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSource", "Set Transparency Blueprint Source"),
        [NewState, ComponentName, Component = *Component](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FWetClothingTransparencyBlueprintSource& BlueprintSource = MutableLayer.BlueprintSource;
            if (BlueprintSource.TargetComponent.ComponentName == ComponentName)
            {
                return;
            }
            if (NewState == ECheckBoxState::Checked)
            {
                if (!BlueprintSource.SourcePriority.ContainsByPredicate(
                        [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                        {
                            return Source.ComponentName == ComponentName;
                        }))
                {
                    FWetClothingTransparencyBlueprintComponentBinding& Source =
                        BlueprintSource.SourcePriority.AddDefaulted_GetRef();
                    Source.ComponentName = ComponentName;
                    Source.ExpectedSkeletalMesh = Component->SkeletalMesh;
                }
            }
            else
            {
                BlueprintSource.SourcePriority.RemoveAll(
                    [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                    {
                        return Source.ComponentName == ComponentName;
                    });
            }
            MutableLayer.MarkAutoBakeStale();
            MutableLayer.MarkFinalBakeStale();
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshBlueprintSourcePriorityItems();
    UpdateInnerSourceStatus();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        const bool bWasFullSourcePreview =
            PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        if (bWasFullSourcePreview)
        {
            PreviewViewport->InvalidateFullSourceLayout();
        }
    }
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveBlueprintSourceClicked(const int32 PriorityIndex)
{
    EditSelectedLayer(
        LOCTEXT("RemoveTransparencyBlueprintSource", "Remove Transparency Blueprint Source"),
        [PriorityIndex](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                MutableLayer.BlueprintSource.SourcePriority.RemoveAt(PriorityIndex);
                MutableLayer.MarkAutoBakeStale();
                MutableLayer.MarkFinalBakeStale();
            }
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshBlueprintSourcePriorityItems();
    UpdateInnerSourceStatus();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        const bool bWasFullSourcePreview =
            PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        if (bWasFullSourcePreview)
        {
            PreviewViewport->InvalidateFullSourceLayout();
        }
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleMoveBlueprintSourceClicked(
    const int32 PriorityIndex,
    const int32 Direction)
{
    EditSelectedLayer(
        LOCTEXT("ReorderTransparencyBlueprintSource", "Reorder Transparency Blueprint Source"),
        [PriorityIndex, Direction](FWetClothingTransparencyLayerData& MutableLayer)
        {
            TArray<FWetClothingTransparencyBlueprintComponentBinding>& Sources =
                MutableLayer.BlueprintSource.SourcePriority;
            const int32 Destination = PriorityIndex + Direction;
            if (Sources.IsValidIndex(PriorityIndex) && Sources.IsValidIndex(Destination))
            {
                Sources.Swap(PriorityIndex, Destination);
                MutableLayer.MarkAutoBakeStale();
                MutableLayer.MarkFinalBakeStale();
            }
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshBlueprintSourcePriorityItems();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceUVChannelChanged(
    TSharedPtr<int32> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid())
    {
        return;
    }
    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSourceUV", "Set Transparency Blueprint Source UV"),
        [PriorityIndex, UVChannel = *Item](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].SourceUVChannel = UVChannel;
                MutableLayer.MarkAutoBakeStale();
                MutableLayer.MarkFinalBakeStale();
            }
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::HandleBlueprintSourceRoleChanged(
    TSharedPtr<EDWCTransparencyBlueprintSourceRole> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid())
    {
        return;
    }
    EditSelectedLayer(
        LOCTEXT("SetTransparencyBlueprintSourceRole", "Set Transparency Blueprint Source Role"),
        [PriorityIndex, Role = *Item](FWetClothingTransparencyLayerData& MutableLayer)
        {
            if (MutableLayer.BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                MutableLayer.BlueprintSource.SourcePriority[PriorityIndex].Role = Role;
                MutableLayer.MarkAutoBakeStale();
                MutableLayer.MarkFinalBakeStale();
            }
        },
        false);
    AutoBakeResults.Remove(SelectedLayerGuid);
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Model | EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::RefreshFinalEditingContent()
{
    if (FinalEditingNoticeContainer.IsValid())
    {
        FinalEditingNoticeContainer->SetContent(BuildFinalEditingNotice());
    }
    if (FinalEditingPreviewSettingsContainer.IsValid())
    {
        FinalEditingPreviewSettingsContainer->SetContent(BuildPreviewSettingsSection());
    }
    if (FinalEditingGeneratedOutputsContainer.IsValid())
    {
        GeneratedOutputThumbnails.Reset();
        FinalEditingGeneratedOutputsContainer->SetContent(BuildGeneratedOutputsSection());
    }
    RefreshTransparencyStrokeList();
}

void SWetClothingTransparencyBakePanel::RequestRefresh(
    const EDWCTransparencyPanelRefreshFlags Flags)
{
    PendingRefreshFlags |= Flags;
    if (bRefreshTimerRegistered)
    {
        return;
    }

    bRefreshTimerRegistered = true;
    RegisterActiveTimer(
        0.0f,
        FWidgetActiveTimerDelegate::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandleDeferredRefresh));
}

EActiveTimerReturnType SWetClothingTransparencyBakePanel::HandleDeferredRefresh(
    double,
    float)
{
    EDWCTransparencyPanelRefreshFlags RefreshFlags = PendingRefreshFlags;
    PendingRefreshFlags = EDWCTransparencyPanelRefreshFlags::None;

    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Model) &&
        RefreshModelState())
    {
        RefreshFlags |= EDWCTransparencyPanelRefreshFlags::StageContent;
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::StageContent))
    {
        // Stage changes only replace the active stage subtree. Rebuilding the
        // whole control panel needlessly recreates slot rows and thumbnails.
        if (StageContentSwitcher.IsValid())
        {
            RefreshStageContent();
        }
        else
        {
            RebuildEditorLayout();
        }
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Viewport))
    {
        if (!bPreviewSuspended)
        {
            RefreshViewportContext();
        }
    }
    if (EnumHasAnyFlags(RefreshFlags, EDWCTransparencyPanelRefreshFlags::Details) &&
        DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    if (PendingRefreshFlags != EDWCTransparencyPanelRefreshFlags::None)
    {
        return EActiveTimerReturnType::Continue;
    }

    bRefreshTimerRegistered = false;
    return EActiveTimerReturnType::Stop;
}

bool SWetClothingTransparencyBakePanel::LoadBakedMapAsWorkingResult(
    const FWetClothingBakedTransparencyMap& BakedMap,
    const FWetClothingTransparencyLayerData& Layer,
    TSharedPtr<FDWCTransparencySourcePayload>& OutResult,
    FString& OutError) const
{
    OutResult.Reset();
    OutError.Reset();
    UTexture2D* Texture = BakedMap.TransparencyMap;
    if (Texture == nullptr || !Texture->Source.IsValid())
    {
        OutError = TEXT("The baked Transparency Map has no readable source data.");
        return false;
    }
    if (Texture->Source.GetFormat() != TSF_BGRA8)
    {
        OutError = FString::Printf(
            TEXT("The baked Transparency Map source format is not BGRA8 (%s)."),
            *GetNameSafe(Texture));
        return false;
    }

    const int32 Width = Texture->Source.GetSizeX();
    const int32 Height = Texture->Source.GetSizeY();
    const int32 PixelCount = Width * Height;
    TArray64<uint8> RawMipData;
    if (Width <= 0 || Height <= 0 || !Texture->Source.GetMipData(RawMipData, 0) ||
        RawMipData.Num() < static_cast<int64>(PixelCount) * static_cast<int64>(sizeof(FColor)))
    {
        OutError = FString::Printf(TEXT("Could not read baked Transparency Map pixels from '%s'."), *GetNameSafe(Texture));
        return false;
    }

    TSharedPtr<FDWCTransparencySourcePayload> Result = MakeShared<FDWCTransparencySourcePayload>();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    Result->LayerGuid = Layer.LayerGuid;
    Result->MaterialSlotIndex = BakedMap.MaterialSlotIndex;
    Result->UVChannelIndex = Asset != nullptr ? Asset->GetDWCDataUVChannelIndex() : INDEX_NONE;
    Result->LODIndex = 0;
    Result->Resolution = FIntPoint(Width, Height);
    Result->BuildSignature = BakedMap.BuildSignature;
    Result->InnerColorBuffer.SetNumUninitialized(PixelCount);
    Result->AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    Result->ValidHitBuffer.Init(true, PixelCount);
    Result->HitDistanceBuffer.Init(0.0f, PixelCount);
    Result->SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    if (Asset == nullptr ||
        !FDWCTransparencyAutoMapGenerator::BuildTargetSurfaceBuffers(
            *Asset,
            Layer.TargetSurface,
            0,
            Result->Resolution,
            Result->OuterCoverageBuffer,
            Result->OuterIslandIDBuffer,
            &Result->OuterSampleCount,
            &Result->OverlappedUVPixelCount,
            OutError))
    {
        OutError = FString::Printf(
            TEXT("Could not rebuild target UV island clip data for baked Transparency Map editing. %s"),
            *OutError);
        return false;
    }
    const FColor* SourcePixels = reinterpret_cast<const FColor*>(RawMipData.GetData());
    for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
    {
        Result->InnerColorBuffer[PixelIndex] = SourcePixels[PixelIndex];
        Result->InnerColorBuffer[PixelIndex].A = 255;
        Result->AutoAlphaBuffer[PixelIndex] = SourcePixels[PixelIndex].A;
    }
    Result->bIsFinalBakedBaseline = true;
    Result->BaselineStrokeCount = FMath::Clamp(
        BakedMap.BakedStrokeCount,
        0,
        Layer.GetEditableStrokes().Num());
    Result->BaselineBakeGuid = BakedMap.BakeGuid;
    OutResult = MoveTemp(Result);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureSourcePayloadAccounted(
    const TSharedPtr<FDWCTransparencySourcePayload>& Payload,
    FString& OutError) const
{
    OutError.Reset();
    if (!Payload.IsValid())
    {
        OutError = TEXT("The Transparency working payload is invalid.");
        return false;
    }
    if (!ResourceGovernor.IsValid())
    {
        return true;
    }

    const uint64 ActualBytes = FMath::Max<uint64>(Payload->GetAllocatedBytes(), 1);
    if (Payload->PersistentMemoryAccount.IsValid())
    {
        if (Payload->PersistentMemoryAccount->GetActualBytes() == ActualBytes &&
            Payload->PersistentMemoryAccount->IsAccounted())
        {
            return true;
        }
        if (!Payload->PersistentMemoryAccount->TryAdoptActualBytes(ActualBytes, &OutError))
        {
            OutError = FString::Printf(
                TEXT("The Transparency working payload could not update its memory reservation. %s"),
                *OutError);
            return false;
        }
        return true;
    }

    FDWCEditorAsyncOperationIdentity Owner;
    Owner.Key.Namespace = TEXT("DWC.Transparency.SourcePayload");
    Owner.Key.MaterialSlotIndex = Payload->MaterialSlotIndex;
    Owner.Key.ResourceGuid = Payload->LayerGuid;
    Owner.SessionEpoch = WorkerJobScheduler.IsValid()
        ? WorkerJobScheduler->GetSessionEpoch()
        : FGuid::NewGuid();
    Owner.OperationId = 1;
    Owner.Generation = 1;
    Owner.Domain = EDWCEditorAuthoringDomain::Transparency;

    TSharedPtr<FDWCEditorAccountedMemory, ESPMode::ThreadSafe> Account =
        MakeShared<FDWCEditorAccountedMemory, ESPMode::ThreadSafe>();
    Account->Configure(
        ResourceGovernor,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        Owner,
        TEXT("Transparency canonical source payload"));
    if (!Account->TryAdoptActualBytes(ActualBytes, &OutError))
    {
        OutError = FString::Printf(
            TEXT("The Transparency working payload requires %.2f MiB, but the preview workspace cannot retain it. %s"),
            static_cast<double>(ActualBytes) / (1024.0 * 1024.0),
            *OutError);
        return false;
    }

    Payload->PersistentMemoryAccount = MoveTemp(Account);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureFinalEditingWorkingMap()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || Asset == nullptr)
    {
        return false;
    }
    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid() && !(*Existing)->bIsFinalBakedBaseline)
    {
        return true;
    }

    FString RestoreError;
    if (RestoreCanonicalWorkingMap(RestoreError))
    {
        return true;
    }

    // Old assets can have a baked output but no canonical Stage 2 Temp
    // artifacts. Keep that baseline path as a read-only compatibility fallback.
    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        return true;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = FindExactBakedTransparencyMap(Asset, Layer);
    if (BakedMap == nullptr)
    {
        return false;
    }
    TSharedPtr<FDWCTransparencySourcePayload> LoadedResult;
    FString LoadError;
    if (!LoadBakedMapAsWorkingResult(*BakedMap, *Layer, LoadedResult, LoadError))
    {
        StatusMessage = LoadError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    FString AccountingError;
    if (!EnsureSourcePayloadAccounted(LoadedResult, AccountingError))
    {
        StatusMessage = AccountingError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(LoadedResult));
    if (!IsVisualizationModeAvailable(SelectedVisualizationMode, GetCurrentStage()))
    {
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    }
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureRevealEditingWorkingMap()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return false;
    }

    if (const TSharedPtr<FDWCTransparencySourcePayload>* Existing =
            AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid() &&
        !(*Existing)->bIsFinalBakedBaseline)
    {
        return true;
    }
    FString RestoreError;
    if (RestoreCanonicalWorkingMap(RestoreError))
    {
        return true;
    }
    StatusMessage = RestoreError.IsEmpty()
        ? TEXT("The Stage 2 source working map is unavailable. Return to Stage 2 and generate it again.")
        : RestoreError;
    PanelStatus = EDWCTransparencyPanelStatus::Warning;
    return false;
}

bool SWetClothingTransparencyBakePanel::RestoreCanonicalWorkingMap(FString& OutError)
{
    OutError.Reset();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        OutError = TEXT("Select a Transparency Target Part before restoring its Stage 2 source.");
        return false;
    }

    FDWCTransparencySourcePayload RestoredPayload;
    if (!FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalSource(
            *Asset,
            Layer->LayerGuid,
            RestoredPayload,
            OutError))
    {
        return false;
    }

    RestoredPayload.bIsFinalBakedBaseline = false;
    RestoredPayload.BaselineStrokeCount = 0;
    RestoredPayload.BaselineBakeGuid.Invalidate();
    TSharedPtr<FDWCTransparencySourcePayload> RestoredResult =
        MakeShared<FDWCTransparencySourcePayload>(MoveTemp(RestoredPayload));
    if (!EnsureSourcePayloadAccounted(RestoredResult, OutError))
    {
        return false;
    }

    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(RestoredResult));
    return true;
}

bool SWetClothingTransparencyBakePanel::HasRestorableCanonicalSource() const
{
    return ResolveSelectedLayerWorkflowState().bHasCanonicalSource;
}

int32 SWetClothingTransparencyBakePanel::GetCurrentBaselineStrokeCount() const
{
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(SelectedLayerGuid);
    return Result != nullptr && Result->IsValid()
        ? FMath::Max((*Result)->BaselineStrokeCount, 0)
        : 0;
}

bool SWetClothingTransparencyBakePanel::SaveTransparencySetupAssets() const
{
    return FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(WetClothingAsset.Get());
}

const UClass* SWetClothingTransparencyBakePanel::GetSelectedSourceClass() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr ? Layer->BlueprintSource.BlueprintClass.Get() : nullptr;
}

void SWetClothingTransparencyBakePanel::HandleSourceClassChanged(const UClass* NewClass)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }
    const TSoftClassPtr<AActor> NewSourceClass =
        NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
            ? const_cast<UClass*>(NewClass)
            : nullptr;
    const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
    if (SelectedLayer == nullptr ||
        SelectedLayer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        return;
    }
    const FGuid LayerGuid = SelectedLayer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("SetTransparencySourceBlueprint", "Set Transparency Source Blueprint"),
            Change,
            [LayerGuid, NewSourceClass](UWetClothingAsset& MutableAsset)
            {
                FWetClothingTransparencyLayerData* MutableLayer =
                    MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                        [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                        {
                            return Candidate.LayerGuid == LayerGuid;
                        });
                if (MutableLayer == nullptr || MutableLayer->BlueprintSource.BlueprintClass == NewSourceClass)
                {
                    return false;
                }
                MutableLayer->BlueprintSource = FWetClothingTransparencyBlueprintSource{};
                MutableLayer->BlueprintSource.BlueprintClass = NewSourceClass;
                MutableLayer->MarkAutoBakeStale();
                MutableLayer->MarkFinalBakeStale();
                return true;
            }).bChanged)
    {
        return;
    }
    AutoBakeResults.Remove(LayerGuid);
    BlueprintHierarchyLayerGuid.Invalidate();
    BlueprintHierarchyClassPath.Reset();
    RefreshBlueprintHierarchy(true);
    UpdateInnerSourceStatus();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
}

void SWetClothingTransparencyBakePanel::RefreshRevealEditingContent()
{
    RefreshRevealColorStrokeList();
    if (RevealEditingContentContainer.IsValid())
    {
        RevealEditingContentContainer->SetContent(BuildRevealColorEditingSection());
    }
}

FString SWetClothingTransparencyBakePanel::GetPendingExternalSourceMeshPath() const
{
    return PendingExternalSourceMesh.ToSoftObjectPath().ToString();
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceMeshChanged(
    const FAssetData& AssetData)
{
    PendingExternalSourceMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
}

FReply SWetClothingTransparencyBakePanel::HandleAddExternalSourceClicked()
{
    USkeletalMesh* NewMesh = PendingExternalSourceMesh.LoadSynchronous();
    if (NewMesh == nullptr)
    {
        StatusMessage = TEXT("Choose a Skeletal Mesh before adding an External Raycast Source.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    FGuid NewSourceGuid;
    EditSelectedLayer(
        LOCTEXT("AddTransparencyExternalMesh", "Add Transparency External Mesh Source"),
        [NewMesh, &NewSourceGuid](FWetClothingTransparencyLayerData& Layer)
        {
            FWetClothingTransparencyExternalMeshEntry& Entry =
                Layer.ExternalMeshSource.SourcePriority.AddDefaulted_GetRef();
            Entry.SourceGuid = FGuid::NewGuid();
            Entry.SkeletalMesh = NewMesh;
            Entry.BakeTransform = FTransform::Identity;
            Entry.SourceUVChannel = 0;
            Entry.Role = EDWCTransparencyBlueprintSourceRole::RevealSource;
            NewSourceGuid = Entry.SourceGuid;
            Layer.MarkAutoBakeStale();
            Layer.MarkFinalBakeStale();
        },
        true);
    if (!NewSourceGuid.IsValid())
    {
        return FReply::Handled();
    }
    SelectedExternalSourceGuid = NewSourceGuid;
    PendingExternalSourceMesh.Reset();
    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshExternalSourcePriorityItems();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        const bool bWasFullSourcePreview =
            PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        if (bWasFullSourcePreview)
        {
            PreviewViewport->InvalidateFullSourceLayout();
        }
        PreviewViewport->SetExternalSourcePlacementSelection(SelectedExternalSourceGuid);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveExternalSourceClicked(const int32 PriorityIndex)
{
    EditSelectedLayer(
        LOCTEXT("RemoveTransparencyExternalMesh", "Remove Transparency External Mesh Source"),
        [PriorityIndex](FWetClothingTransparencyLayerData& Layer)
        {
            if (Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                Layer.ExternalMeshSource.SourcePriority.RemoveAt(PriorityIndex);
                Layer.MarkAutoBakeStale();
                Layer.MarkFinalBakeStale();
            }
        },
        true);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr ||
        !Layer->ExternalMeshSource.SourcePriority.ContainsByPredicate(
            [this](const FWetClothingTransparencyExternalMeshEntry& Entry)
            {
                return Entry.SourceGuid == SelectedExternalSourceGuid;
            }))
    {
        SelectedExternalSourceGuid.Invalidate();
    }
    RefreshExternalSourcePriorityItems();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->InvalidateFullSourceLayout();
        PreviewViewport->SetExternalSourcePlacementSelection(SelectedExternalSourceGuid);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleMoveExternalSourceClicked(
    const int32 PriorityIndex,
    const int32 Direction)
{
    EditSelectedLayer(
        LOCTEXT("ReorderTransparencyExternalMesh", "Reorder Transparency External Mesh Source"),
        [PriorityIndex, Direction](FWetClothingTransparencyLayerData& Layer)
        {
            TArray<FWetClothingTransparencyExternalMeshEntry>& Sources =
                Layer.ExternalMeshSource.SourcePriority;
            const int32 Destination = PriorityIndex + Direction;
            if (Sources.IsValidIndex(PriorityIndex) && Sources.IsValidIndex(Destination))
            {
                Sources.Swap(PriorityIndex, Destination);
                Layer.MarkAutoBakeStale();
                Layer.MarkFinalBakeStale();
            }
        },
        true);
    RefreshExternalSourcePriorityItems();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleResetExternalSourceTransformClicked(
    const int32 PriorityIndex)
{
    EditSelectedLayer(
        LOCTEXT("ResetTransparencyExternalMeshTransform", "Reset Transparency External Mesh Transform"),
        [PriorityIndex](FWetClothingTransparencyLayerData& Layer)
        {
            if (Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                Layer.ExternalMeshSource.SourcePriority[PriorityIndex].BakeTransform = FTransform::Identity;
                Layer.MarkAutoBakeStale();
                Layer.MarkFinalBakeStale();
            }
        },
        false);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
        PreviewViewport->SetExternalSourcePlacementSelection(SelectedExternalSourceGuid);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleSelectExternalSourceClicked(const int32 PriorityIndex)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || !Layer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
    {
        return FReply::Handled();
    }
    SelectedExternalSourceGuid = Layer->ExternalMeshSource.SourcePriority[PriorityIndex].SourceGuid;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::FullBlueprint);
        PreviewViewport->SetExternalSourcePlacementSelection(SelectedExternalSourceGuid);
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceUVChannelChanged(
    TSharedPtr<int32> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(
        LOCTEXT("SetTransparencyExternalMeshUV", "Set Transparency External Mesh Source UV"),
        [PriorityIndex, UVChannel = *Item](FWetClothingTransparencyLayerData& Layer)
        {
            if (Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                Layer.ExternalMeshSource.SourcePriority[PriorityIndex].SourceUVChannel = UVChannel;
                Layer.MarkAutoBakeStale();
                Layer.MarkFinalBakeStale();
            }
        },
        false);
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceRoleChanged(
    TSharedPtr<EDWCTransparencyBlueprintSourceRole> Item,
    ESelectInfo::Type,
    const int32 PriorityIndex)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(
        LOCTEXT("SetTransparencyExternalMeshRole", "Set Transparency External Mesh Source Role"),
        [PriorityIndex, Role = *Item](FWetClothingTransparencyLayerData& Layer)
        {
            if (Layer.ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex))
            {
                Layer.ExternalMeshSource.SourcePriority[PriorityIndex].Role = Role;
                Layer.MarkAutoBakeStale();
                Layer.MarkFinalBakeStale();
            }
        },
        false);
}

void SWetClothingTransparencyBakePanel::HandleExternalSourceTransformCommitted(
    const FGuid& SourceGuid,
    const FTransform& Transform)
{
    if (!SourceGuid.IsValid()) return;
    const FWetClothingTransparencyLayerData* ExistingLayer = GetSelectedLayer();
    const FWetClothingTransparencyExternalMeshEntry* ExistingEntry = ExistingLayer != nullptr
        ? ExistingLayer->ExternalMeshSource.SourcePriority.FindByPredicate(
            [SourceGuid](const FWetClothingTransparencyExternalMeshEntry& Candidate)
            {
                return Candidate.SourceGuid == SourceGuid;
            })
        : nullptr;
    if (ExistingEntry == nullptr || ExistingEntry->BakeTransform.Equals(Transform))
    {
        return;
    }

    EditSelectedLayer(
        LOCTEXT("PlaceTransparencyExternalMesh", "Place Transparency External Mesh Source"),
        [SourceGuid, Transform](FWetClothingTransparencyLayerData& Layer)
        {
            FWetClothingTransparencyExternalMeshEntry* Entry =
                Layer.ExternalMeshSource.SourcePriority.FindByPredicate(
                    [SourceGuid](const FWetClothingTransparencyExternalMeshEntry& Candidate)
                    {
                        return Candidate.SourceGuid == SourceGuid;
                    });
            if (Entry == nullptr || Entry->BakeTransform.Equals(Transform))
            {
                return;
            }
            Entry->BakeTransform = Transform;
            Layer.MarkAutoBakeStale();
            Layer.MarkFinalBakeStale();
        },
        false);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
        PreviewViewport->SetExternalSourcePlacementSelection(SourceGuid);
    }
    RefreshExternalSourcePriorityItems();
}

FReply SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset != nullptr && Layer != nullptr &&
        IsSourceTypeAvailable(Layer->SourceType))
    {
        if (!HasUsableTransparencyDataUV())
        {
            const FString Message = TEXT("Generate the DWC UV Channel before generating a Transparency Map.");
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }
        if (!PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
        {
            const FDWCEditorPreviewSlotState* State =
                FindPreviewSlotState(Layer->TargetSurface.OuterMaterialSlotIndex);
            const FString Message = State != nullptr
                ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
                : TEXT("The selected Transparency Target Part is unavailable for preview.");
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }

        TArray<FString> ValidationErrors;
        if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                Asset->GetDWCSkeletalMesh(),
                *Layer,
                ValidationErrors,
                ResolveTransparencyDataUVChannel(Asset)))
        {
            const FString Message = FString::Join(ValidationErrors, TEXT("\n"));
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }

        TSharedPtr<FDWCTransparencySourcePayload> Result = MakeShared<FDWCTransparencySourcePayload>();
        FString Summary;
        TArray<FString> Warnings;
        bool bGenerated = false;
        if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
        {
            bGenerated = FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
                *Asset,
                *Layer,
                *Result,
                Summary,
                Warnings);
        }
        else
        {
            FScopedSlowTask RaycastTask(
                1.0f,
                LOCTEXT(
                    "GenerateTransparencyRaycastProgress",
                    "Generating preview transparency data from the selected source surfaces..."));
            RaycastTask.MakeDialog(false);
            RaycastTask.EnterProgressFrame(
                1.0f,
                LOCTEXT(
                    "GenerateTransparencyRaycastProgressDetail",
                    "Baking source materials and raycasting the target surface."));
            bGenerated = FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
                *Asset,
                *Layer,
                *Result,
                Summary,
                Warnings);
        }
        if (!bGenerated)
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
            return FReply::Handled();
        }

        FString AccountingError;
        if (!EnsureSourcePayloadAccounted(Result, AccountingError))
        {
            StatusMessage = AccountingError;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(AccountingError));
            return FReply::Handled();
        }

        const FScopedTransaction Transaction(LOCTEXT("GenerateTransparencyAutoMap", "Generate Transparency Map"));
        Asset->Modify();
        Layer = GetSelectedLayer();
        if (Layer == nullptr) return FReply::Handled();
        // Generation is the explicit authoring checkpoint. From this point the
        // layer requires a current runtime output until the user disables or removes it.
        Layer->Intent = EDWCTransparencyLayerIntent::Enabled;
        Layer->AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
        Layer->AutoBakeMetadata.BuildSignature = Result->BuildSignature;
        Layer->AutoBakeMetadata.Resolution = Result->Resolution.X;
        Layer->AutoBakeMetadata.PaddingPixels = Asset->Authored.TransparencyData.TransparencyPaddingPixels;
        Layer->AutoBakeMetadata.ValidHitCount = Result->ValidHitCount;
        Layer->AutoBakeMetadata.NoHitCount = Result->NoHitCount;

        FString SourceSignature;
        FString MaterialBakeSignature;
        FString SignatureError;
        if (!FDWCTransparencySignatureService::BuildSourceSignature(
                *Asset,
                *Layer,
                SourceSignature,
                MaterialBakeSignature,
                SignatureError))
        {
            Warnings.Add(SignatureError);
        }
        else
        {
            Result->BuildSignature = SourceSignature;
            Layer->AutoBakeMetadata.BuildSignature = SourceSignature;
            FString TempStoreError;
            if (!FDWCTransparencyTempAssetStore::CommitSourceArtifacts(
                    *Asset,
                    *Layer,
                    *Result,
                    MaterialBakeSignature,
                    TempStoreError))
            {
                Warnings.Add(FString::Printf(
                    TEXT("The generated map is usable, but its editor Temp artifacts could not be updated: %s"),
                    *TempStoreError));
            }
        }
        Layer->MarkFinalBakeStale();
        Asset->MarkPackageDirty();
        // Keep only the active layer's large CPU intermediate buffers in memory.
        AutoBakeResults.Reset();
        AutoBakeResults.Add(Layer->LayerGuid, Result);
        const EDWCTransparencyEditorStage ResultStage =
            EDWCTransparencyEditorStage::RevealEditing;
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSetTransparencyStageAction{Layer->LayerGuid, ResultStage});
        }
        else
        {
            StageByLayer.FindOrAdd(Layer->LayerGuid) = ResultStage;
        }
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        // Generation can advance Stage 2 directly to Stage 3 without passing
        // through SetCurrentStage. Keep the controller's paint target in the
        // session authoritative for that direct transition as well.
        DispatchTransparencyEditContext();

        // The working map is ready now. Push it directly so the viewport does
        // not depend on the deferred Stage 2 -> Stage 3 layout rebuild before
        // it can show an authored manual reveal color (or a ray-generated map).
        if (PreviewViewport.IsValid())
        {
            // Reveal-color painting is surface editing. It must always
            // use the single target mesh so the hit BVH and the displayed MID
            // describe the same selected material slot.
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
            PreviewViewport->SetAutoBakePreviewResult(Result);
        }
        DispatchTransparencyPreviewState();

        for (const FDWCTransparencySourceHitStats& Stats : Result->SourceStats)
        {
            Summary += FString::Printf(
                TEXT("\n- Priority %d: %s (Slot %d, source bake %d) -> %d hit(s)"),
                Stats.PriorityIndex,
                *Stats.MaterialSlotName.ToString(),
                Stats.MaterialSlotIndex,
                Stats.SourceBakeResolution,
                Stats.HitCount);
        }
        if (!Warnings.IsEmpty()) Summary += TEXT("\n\nWarnings:\n- ") + FString::Join(Warnings, TEXT("\n- "));
        StatusMessage = Summary;
        PanelStatus = Warnings.IsEmpty() ? EDWCTransparencyPanelStatus::Ready : EDWCTransparencyPanelStatus::Warning;
        EDWCTransparencyPanelRefreshFlags RefreshFlags = EDWCTransparencyPanelRefreshFlags::Viewport;
        RefreshFlags |=
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Details;
        RequestRefresh(RefreshFlags);
        FMessageDialog::Open(Warnings.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleContinueToFinalEditingClicked()
{
    if (!CanEnterRevealEditingStage())
    {
        StatusMessage = TEXT("The Reveal Color working map is unavailable. Return to Stage 2 and generate it again.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return FReply::Handled();
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const TSharedPtr<FDWCTransparencySourcePayload>* StoredResult =
        Layer != nullptr ? AutoBakeResults.Find(Layer->LayerGuid) : nullptr;
    if (Asset == nullptr || Layer == nullptr || StoredResult == nullptr || !StoredResult->IsValid())
    {
        StatusMessage = TEXT("The Reveal Color stage could not commit because its source result is unavailable.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    if (!WorkerJobScheduler.IsValid() || !PreviewViewport.IsValid())
    {
        StatusMessage = TEXT("The asynchronous reveal-color commit service is unavailable.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }
    if (bRevealCommitInFlight)
    {
        return FReply::Handled();
    }

    const FGuid ExpectedLayerGuid = Layer->LayerGuid;
    const int32 ExpectedSlot = Layer->TargetSurface.OuterMaterialSlotIndex;
    const FString ExpectedSourceSignature = (*StoredResult)->BuildSignature;
    const float ExpectedMetallicDarkeningStrength =
        Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength;
    const FString ExpectedRevealSignature = FDWCTransparencySignatureService::BuildRevealSignature(
        (*StoredResult)->BuildSignature,
        *Layer,
        ExpectedMetallicDarkeningStrength);
    const uint64 ExpectedEpoch = ++RevealCommitEpoch;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyRevealColorCommit;
    Descriptor.Key.MaterialSlotIndex = ExpectedSlot;
    Descriptor.Key.LayerGuid = ExpectedLayerGuid;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.WorkClass = EDWCEditorWorkClass::UserBuild;
    const uint64 PixelBytes = static_cast<uint64>((*StoredResult)->Resolution.X) *
        (*StoredResult)->Resolution.Y * sizeof(FColor);
    Descriptor.MemoryEstimate.ResidentSharedBytes = (*StoredResult)->InnerColorBuffer.GetAllocatedSize();
    Descriptor.MemoryEstimate.OutputBytes = PixelBytes;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency reveal commit slot %d"), ExpectedSlot);

    bRevealCommitInFlight = true;
    StatusMessage = TEXT("Committing the Stage 3 reveal-color corrections...");
    PanelStatus = EDWCTransparencyPanelStatus::Info;

    TWeakPtr<SWetClothingTransparencyBakePanel> WeakPanel = SharedThis(this);
    FString SubmitError;
    PendingRevealCommitTicket = WorkerJobScheduler->SubmitPrepared(
        Descriptor,
        [WeakPanel, ExpectedLayerGuid, ExpectedSlot, ExpectedEpoch](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& OutPrepareError)
        {
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            if (!Panel.IsValid() || CancellationToken->IsCanceled() ||
                Panel->RevealCommitEpoch != ExpectedEpoch ||
                !Panel->PreviewViewport.IsValid())
            {
                OutPrepareError = TEXT("The reveal-color commit context changed before admission.");
                return false;
            }
            const FWetClothingTransparencyLayerData* CurrentLayer = Panel->GetSelectedLayer();
            if (CurrentLayer == nullptr || CurrentLayer->LayerGuid != ExpectedLayerGuid ||
                CurrentLayer->TargetSurface.OuterMaterialSlotIndex != ExpectedSlot)
            {
                OutPrepareError = TEXT("The selected transparency layer changed before reveal commit.");
                return false;
            }

            FDWCTransparencyRevealCommitJobInput Input;
            if (!Panel->PreviewViewport->BuildRevealColorCommitInput(Input, OutPrepareError))
            {
                return false;
            }
            OutPrepared.ActualMemoryEstimate =
                FDWCTransparencyRevealCommitWorker::EstimateMemory(Input);
            OutPrepared.Work = [Input = MoveTemp(Input)](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
            {
                return FDWCTransparencyRevealCommitWorker::Build(MoveTemp(Input), WorkerToken);
            };
            return true;
        },
        [WeakPanel, ExpectedLayerGuid, ExpectedSlot, ExpectedSourceSignature,
         ExpectedRevealSignature, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& CompletedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            const TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FDWCTransparencyRevealCommitJobResult>(BaseResult);
            if (!Panel.IsValid() || Panel->RevealCommitEpoch != ExpectedEpoch ||
                Panel->PendingRevealCommitTicket.JobId != CompletedTicket.JobId ||
                Panel->PendingRevealCommitTicket.Generation != CompletedTicket.Generation)
            {
                return;
            }
            Panel->PendingRevealCommitTicket = {};
            Panel->bRevealCommitInFlight = false;

            UWetClothingAsset* CurrentAsset = Panel->WetClothingAsset.Get();
            FWetClothingTransparencyLayerData* CurrentLayer = Panel->GetSelectedLayer();
            const FString CurrentRevealSignature = CurrentLayer != nullptr
                ? FDWCTransparencySignatureService::BuildRevealSignature(
                    ExpectedSourceSignature,
                    *CurrentLayer,
                    CurrentAsset != nullptr
                        ? CurrentAsset->Authored.TransparencyData.RevealMetallicDarkeningStrength
                        : 0.0f)
                : FString();
            if (!Result.IsValid() || !Result->bSucceeded || CurrentAsset == nullptr ||
                CurrentLayer == nullptr || CurrentLayer->LayerGuid != ExpectedLayerGuid ||
                CurrentLayer->TargetSurface.OuterMaterialSlotIndex != ExpectedSlot ||
                Result->SourceSignature != ExpectedSourceSignature ||
                CurrentRevealSignature != ExpectedRevealSignature)
            {
                Panel->StatusMessage = Result.IsValid() && !Result->Error.IsEmpty()
                    ? Result->Error
                    : TEXT("The reveal-color commit became stale before it could be applied.");
                Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
                return;
            }

            FString CommitError;
            if (!FDWCTransparencyTempAssetStore::CommitRevealArtifact(
                    *CurrentAsset,
                    *CurrentLayer,
                    Result->CorrectedRevealPixels,
                    Result->Resolution,
                    ExpectedSourceSignature,
                    ExpectedRevealSignature,
                    CommitError))
            {
                Panel->StatusMessage = CommitError;
                Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
                return;
            }

            CurrentAsset->MarkPackageDirty();
            Panel->SetCurrentStage(EDWCTransparencyEditorStage::FinalEditing);
        },
        &SubmitError,
        [WeakPanel, ExpectedEpoch](
            const FDWCEditorWorkerJobTicket& FinishedTicket,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString& Detail)
        {
            if (Completion == EDWCEditorWorkerJobCompletion::Applied)
            {
                return;
            }
            const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
            if (!Panel.IsValid() || Panel->RevealCommitEpoch != ExpectedEpoch ||
                Panel->PendingRevealCommitTicket.JobId != FinishedTicket.JobId ||
                Panel->PendingRevealCommitTicket.Generation != FinishedTicket.Generation)
            {
                return;
            }
            Panel->PendingRevealCommitTicket = {};
            Panel->bRevealCommitInFlight = false;
            Panel->StatusMessage = Detail.IsEmpty()
                ? TEXT("The reveal-color commit did not complete.")
                : Detail;
            Panel->PanelStatus = EDWCTransparencyPanelStatus::Warning;
        });

    if (!PendingRevealCommitTicket.IsValid())
    {
        bRevealCommitInFlight = false;
        StatusMessage = SubmitError.IsEmpty()
            ? TEXT("The reveal-color commit could not be scheduled.")
            : SubmitError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
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

    const TSharedPtr<FDWCTransparencySourcePayload>* StoredResult = AutoBakeResults.Find(Layer->LayerGuid);
    if (StoredResult == nullptr || !StoredResult->IsValid())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("BakeEditedGenerateFirst", "Generate a Preview Transparency Map or load an existing baked map before baking."));
        return FReply::Handled();
    }

    FString CompatibilityReason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *StoredResult->Get(), CompatibilityReason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(CompatibilityReason));
        return FReply::Handled();
    }
    if (!BakeCoordinator.IsValid())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeCoordinatorUnavailable", "The asynchronous bake service is unavailable."));
        return FReply::Handled();
    }

    // The preview controls are session-owned while the bake/signature contract
    // is asset-owned. Persist the current session values once, then freeze the
    // exact resulting settings for the job so a pending numeric commit cannot
    // bake a different strength than the viewport just displayed.
    const FDWCTransparencyPreviewSettings BakePreviewSettings = GetTransparencyPreviewSettings();
    CommitTransparencyPreviewSettings(
        LOCTEXT("CommitTransparencyBakeSettings", "Commit Transparency Bake Settings"),
        BakePreviewSettings);
    const FDWCTransparencyFinalSettingsSnapshot FinalSettings =
        FDWCTransparencyFinalSettingsSnapshot::FromAuthoredData(Asset->Authored.TransparencyData);
    FString FinalSettingsError;
    if (!FinalSettings.IsValid(&FinalSettingsError))
    {
        StatusMessage = FinalSettingsError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(FinalSettingsError));
        return FReply::Handled();
    }
    const TSharedRef<const FDWCTransparencyFinalSettingsSnapshot> FinalSettingsSnapshot =
        MakeShared<FDWCTransparencyFinalSettingsSnapshot>(FinalSettings);

    StatusMessage = TEXT("Baking the edited transparency map...");
    PanelStatus = EDWCTransparencyPanelStatus::Info;
    const FGuid LayerGuid = Layer->LayerGuid;
    TWeakPtr<SWetClothingTransparencyBakePanel> WeakPanel = SharedThis(this);
    FString RequestError;
    const bool bRequiresCanonicalRebuild = (*StoredResult)->bIsFinalBakedBaseline;
    const auto Completion = [WeakPanel, LayerGuid](const FDWCEditorBakeBatchResult& Result)
    {
        const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
        if (!Panel.IsValid())
        {
            return;
        }
        if (Result.bSucceeded)
        {
            Panel->AutoBakeResults.Remove(LayerGuid);
        }
        Panel->StatusMessage = Result.Summary;
        Panel->PanelStatus = Result.bSucceeded
            ? EDWCTransparencyPanelStatus::Ready
            : EDWCTransparencyPanelStatus::Warning;
        Panel->RequestRefresh(
            EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Viewport |
            EDWCTransparencyPanelRefreshFlags::Details);
        FMessageDialog::Open(
            Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(Result.Summary));
    };
    TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot;
    if (!bRequiresCanonicalRebuild && PreviewViewport.IsValid())
    {
        FDWCTransparencyAlphaWorkingSnapshot CapturedAlpha;
        FString SnapshotError;
        if (!PreviewViewport->BuildAlphaWorkingSnapshot(CapturedAlpha, SnapshotError))
        {
            StatusMessage = SnapshotError;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(SnapshotError));
            return FReply::Handled();
        }
        AlphaSnapshot = MakeShared<const FDWCTransparencyAlphaWorkingSnapshot>(MoveTemp(CapturedAlpha));
    }
    const bool bRequested = bRequiresCanonicalRebuild
        ? BakeCoordinator->RequestTransparencyBake(
            TArray<FGuid>{LayerGuid},
            true,
            Completion,
            &RequestError)
        : BakeCoordinator->RequestTransparencyFinalBake(
            LayerGuid,
            StoredResult->ToSharedRef(),
            MoveTemp(AlphaSnapshot),
            FinalSettingsSnapshot,
            true,
            Completion,
            &RequestError);
    if (!bRequested)
    {
        StatusMessage = RequestError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(RequestError));
    }
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
        return LOCTEXT("GenerateTransparencyReadyTooltip", "Generate an editable preview Transparency Map for the selected Target Part.");
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
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(Layer->LayerGuid);
    if (Result == nullptr || !Result->IsValid())
    {
        return LOCTEXT("BakeEditedGenerateTooltip", "Generate a preview map or load an existing baked map before baking.");
    }
    FString Reason;
    if (!FDWCTransparencyEditedMapBaker::IsAutoResultCompatible(*Layer, *Result->Get(), Reason))
    {
        return FText::FromString(Reason);
    }
    return LOCTEXT("BakeEditedReadyTooltip", "Bake the current working map and new brush edits into one packed RGBA Transparency Map.");
}
float SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent() const { return WetnessPreviewPercent; }
void SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged(float InValue)
{
    const float NewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (FMath::IsNearlyEqual(WetnessPreviewPercent, NewPercent))
    {
        return;
    }
    WetnessPreviewPercent = NewPercent;
    DispatchTransparencyPreviewState();
}

TOptional<float> SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength() const
{
    return GetTransparencyPreviewSettings().TransparencyStrength;
}

void SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.TransparencyStrength = FMath::Max(0.0f, InValue);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleTransparencyPreviewStrengthChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetTransparencyPreviewStrength", "Set Transparency Preview Strength"),
        GetTransparencyPreviewSettings());
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetRevealNormalEnabledState() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr && Layer->bEnableRevealNormal
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalEnabledChanged(
    const ECheckBoxState NewState)
{
    const bool bEnable = NewState == ECheckBoxState::Checked;
    CommitRevealNormalRuntimeSettings(
        LOCTEXT("SetRevealNormalEnabled", "Set Reveal Normal Enabled"),
        bEnable,
        GetTransparencyPreviewSettings().RevealNormalStrength);

    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.bShowRevealNormal = bEnable;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

TOptional<float> SWetClothingTransparencyBakePanel::GetRevealNormalStrength() const
{
    return GetTransparencyPreviewSettings().RevealNormalStrength;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.RevealNormalStrength = FMath::Clamp(InValue, 0.0f, 4.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleRevealNormalStrengthChanged(InValue);
    CommitRevealNormalRuntimeSettings(
        LOCTEXT("SetRevealNormalStrength", "Set Reveal Normal Strength"),
        GetRevealNormalEnabledState() == ECheckBoxState::Checked,
        GetTransparencyPreviewSettings().RevealNormalStrength);
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetShowRevealNormalState() const
{
    return GetTransparencyPreviewSettings().bShowRevealNormal
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleShowRevealNormalChanged(
    const ECheckBoxState NewState)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.bShowRevealNormal = NewState == ECheckBoxState::Checked;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetRevealNormalSourceState(
    const EDWCTransparencyRevealNormalPreviewSource Source) const
{
    return GetTransparencyPreviewSettings().RevealNormalSource == Source
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealNormalSourceChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyRevealNormalPreviewSource Source)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.RevealNormalSource = Source;
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));

    if (Source == EDWCTransparencyRevealNormalPreviewSource::Baked &&
        (SelectedVisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
         SelectedVisualizationMode == EDWCTransparencyVisualizationMode::RevealNormalTexture ||
         SelectedVisualizationMode == EDWCTransparencyVisualizationMode::SourceCoverage))
    {
        SetVisualizationModeForStage(
            EDWCTransparencyVisualizationMode::Final,
            EDWCTransparencyEditorStage::FinalEditing);
    }
}

FText SWetClothingTransparencyBakePanel::GetRevealNormalPreviewSourceStatusText() const
{
    return PreviewViewport.IsValid()
        ? PreviewViewport->GetRevealNormalPreviewSourceStatusText()
        : LOCTEXT("RevealNormalPreviewSourceUnavailable", "Preview Source: unavailable");
}

ECheckBoxState SWetClothingTransparencyBakePanel::GetShowSavedWrinkleState() const
{
    return bShowSavedWrinkle ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleShowSavedWrinkleChanged(
    const ECheckBoxState NewState)
{
    bShowSavedWrinkle = NewState == ECheckBoxState::Checked;
    DispatchTransparencyPreviewState();
}


TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength() const
{
    return GetTransparencyPreviewSettings().WrinkleSuppressionStrength;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleSuppressionStrength = FMath::Clamp(InValue, 0.0f, 5.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleSuppressionStrengthChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionStrength", "Set Wrinkle Suppression Strength"),
        GetTransparencyPreviewSettings());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleMaskThreshold() const
{
    return GetTransparencyPreviewSettings().WrinkleMaskThreshold;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleMaskThreshold = FMath::Clamp(InValue, 0.0f, 1.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleMaskThresholdChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionThreshold", "Set Wrinkle Coverage Threshold"),
        GetTransparencyPreviewSettings());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetWrinkleMaskSoftness() const
{
    return GetTransparencyPreviewSettings().WrinkleMaskSoftness;
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessChanged(const float InValue)
{
    FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
    Settings.WrinkleMaskSoftness = FMath::Clamp(InValue, 0.0f, 1.0f);
    DispatchTransparencyPreviewSettings(MoveTemp(Settings));
}

void SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessCommitted(
    const float InValue,
    ETextCommit::Type)
{
    HandleWrinkleMaskSoftnessChanged(InValue);
    CommitTransparencyPreviewSettings(
        LOCTEXT("SetWrinkleSuppressionSoftness", "Set Wrinkle Mask Softness"),
        GetTransparencyPreviewSettings());
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
        // Target Alpha is a persistent child with a live visibility binding.
        // Changing modes must not reconstruct the stage or preview viewport.
    }
}

float SWetClothingTransparencyBakePanel::GetBrushSizeCm() const
{
    return DWCTransparencyRadiusUVToSizeCm(BrushRadiusUV);
}

FText SWetClothingTransparencyBakePanel::GetBrushSizeDisplayText() const
{
    return FormatDWCTransparencyBrushSizeCm(GetBrushSizeCm());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetBrushStrength() const { return BrushStrength; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushFalloff() const { return BrushFalloff; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushSpacing() const { return BrushSpacing; }
TOptional<float> SWetClothingTransparencyBakePanel::GetBrushTargetAlpha() const { return BrushTargetAlpha; }

void SWetClothingTransparencyBakePanel::HandleBrushSizeChanged(
    const float Value,
    const EDWCTransparencyBrushSizeTarget Target)
{
    const float NewRadiusUV = DWCTransparencySizeCmToRadiusUV(Value);
    if (Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint)
    {
        FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
        if (FMath::IsNearlyEqual(Settings.RadiusUV, NewRadiusUV))
        {
            return;
        }
        Settings.RadiusUV = NewRadiusUV;
        DispatchRevealPaintState(MoveTemp(Settings));
        return;
    }

    if (FMath::IsNearlyEqual(BrushRadiusUV, NewRadiusUV))
    {
        return;
    }
    BrushRadiusUV = NewRadiusUV;
    PushPaintSettingsToViewport();
}

void SWetClothingTransparencyBakePanel::HandleBrushSizeCommitted(
    const float Value,
    ETextCommit::Type,
    const EDWCTransparencyBrushSizeTarget Target)
{
    HandleBrushSizeChanged(Value, Target);
}

FReply SWetClothingTransparencyBakePanel::HandleBrushSizePresetClicked(
    const float Value,
    const EDWCTransparencyBrushSizeTarget Target)
{
    HandleBrushSizeChanged(Value, Target);
    TSharedPtr<SComboButton> ComboButton = Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint
        ? RevealPaintSizeComboButton
        : TransparencyBrushSizeComboButton;
    if (ComboButton.IsValid())
    {
        ComboButton->SetIsOpen(false);
    }
    return FReply::Handled();
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
    DispatchTransparencyPaintState(EDWCEditorSessionEffect::None);
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

void SWetClothingTransparencyBakePanel::RefreshTransparencyStrokeList()
{
    if (TransparencyStrokeListContainer.IsValid())
    {
        TransparencyStrokeListContainer->SetContent(BuildTransparencyStrokeList());
    }
    RefreshRevealColorStrokeList();
}

void SWetClothingTransparencyBakePanel::RefreshRevealColorStrokeList()
{
    RevealColorStrokeItems.Reset();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer != nullptr)
    {
        const int32 SlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer->GetRevealColorPaintStrokes())
        {
            if (Stroke.MaterialSlotIndex == SlotIndex)
            {
                RevealColorStrokeItems.Add(MakeShared<FGuid>(Stroke.StrokeGuid));
            }
        }
    }
    if (RevealColorStrokeListView.IsValid())
    {
        RevealColorStrokeListView->RequestListRefresh();
        if (!RevealColorStrokeItems.IsEmpty())
        {
            RevealColorStrokeListView->RequestScrollIntoView(RevealColorStrokeItems.Last());
        }
    }
}

FReply SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->GetEditableStrokes().Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    const FGuid StrokeGuid = Layer->GetEditableStrokes().Last().StrokeGuid;
    const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {Layer->GetEditableStrokes().Last()};
    if (!EditSelectedLayerFinal(
            LOCTEXT("RemoveLastTransparencyStroke", "Remove Last Transparency Stroke"),
            StrokeGuid,
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                TArray<FDWCTransparencyBrushStroke>& Strokes = MutableLayer.GetMutableEditableStrokes();
                if (Strokes.Num() <= BaselineStrokeCount) return false;
                Strokes.Pop();
                return true;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleClearStrokesClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->GetEditableStrokes().Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes;
    const TArray<FDWCTransparencyBrushStroke>& ExistingStrokes = Layer->GetEditableStrokes();
    InvalidatedStrokes.Append(
        ExistingStrokes.GetData() + BaselineStrokeCount,
        ExistingStrokes.Num() - BaselineStrokeCount);
    if (!EditSelectedLayerFinal(
            LOCTEXT("ClearTransparencyStrokes", "Clear Transparency Strokes"),
            FGuid(),
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                TArray<FDWCTransparencyBrushStroke>& Strokes = MutableLayer.GetMutableEditableStrokes();
                if (Strokes.Num() <= BaselineStrokeCount) return false;
                Strokes.RemoveAt(
                    BaselineStrokeCount,
                    Strokes.Num() - BaselineStrokeCount);
                return true;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleDeleteStrokeClicked(const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return FReply::Handled();
    }
    const FDWCTransparencyBrushStroke* Stroke = Layer->GetEditableStrokes().FindByPredicate(
        [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate)
        {
            return Candidate.StrokeGuid == StrokeGuid;
        });
    if (Stroke == nullptr)
    {
        return FReply::Handled();
    }
    const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {*Stroke};
    if (!EditSelectedLayerFinal(
            LOCTEXT("DeleteTransparencyStroke", "Delete Transparency Stroke"),
            StrokeGuid,
            [StrokeGuid](FWetClothingTransparencyLayerData& MutableLayer)
            {
                return MutableLayer.GetMutableEditableStrokes().RemoveAll(
                    [StrokeGuid](const FDWCTransparencyBrushStroke& Stroke)
                    {
                        return Stroke.StrokeGuid == StrokeGuid;
                    }) > 0;
            })) return FReply::Handled();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleStrokeEnabledChanged(
    const ECheckBoxState NewState,
    const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr)
    {
        return;
    }
    if (const FDWCTransparencyBrushStroke* Stroke = Layer->GetEditableStrokes().FindByPredicate(
            [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; }))
    {
        const TArray<FDWCTransparencyBrushStroke> InvalidatedStrokes = {*Stroke};
        const bool bEnabled = NewState == ECheckBoxState::Checked;
        if (!EditSelectedLayerFinal(
                LOCTEXT("ToggleTransparencyStroke", "Toggle Transparency Stroke"),
                StrokeGuid,
                [StrokeGuid, bEnabled](FWetClothingTransparencyLayerData& MutableLayer)
                {
                    FDWCTransparencyBrushStroke* MutableStroke = MutableLayer.GetMutableEditableStrokes().FindByPredicate(
                        [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate)
                        {
                            return Candidate.StrokeGuid == StrokeGuid;
                        });
                    if (MutableStroke == nullptr || MutableStroke->bEnabled == bEnabled) return false;
                    MutableStroke->bEnabled = bEnabled;
                    return true;
                })) return;
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ReplayAlphaStrokeHistory(InvalidatedStrokes);
        }
    }
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item) const
{
    const EDWCTransparencyVisualizationMode Mode =
        Item.IsValid() ? *Item : EDWCTransparencyVisualizationMode::Final;
    return SNew(SBox)
        .IsEnabled_Lambda([this, Mode]()
        {
            return IsVisualizationModeAvailable(Mode, GetCurrentStage());
        })
        [SNew(STextBlock).Text(GetVisualizationModeLabel(Mode))];
}

void SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item,
    ESelectInfo::Type,
    const EDWCTransparencyEditorStage Stage)
{
    if (!Item.IsValid())
    {
        return;
    }
    if (!IsVisualizationModeAvailable(*Item, Stage))
    {
        return;
    }

    SetVisualizationModeForStage(*Item, Stage);
}

FText SWetClothingTransparencyBakePanel::GetVisualizationModeLabel(
    const EDWCTransparencyVisualizationMode Mode) const
{
    switch (Mode)
    {
    case EDWCTransparencyVisualizationMode::BaseRevealColor: return LOCTEXT("TransparencyViewBaseRevealColor", "Base Reveal Color");
    case EDWCTransparencyVisualizationMode::InnerColor: return LOCTEXT("TransparencyViewCorrectedRevealColor", "Corrected Reveal Color");
    case EDWCTransparencyVisualizationMode::CorrectionDifference: return LOCTEXT("TransparencyViewCorrectionDifference", "Correction Difference");
    case EDWCTransparencyVisualizationMode::RaycastGaps: return LOCTEXT("TransparencyViewRaycastGaps", "Raycast Gaps");
    case EDWCTransparencyVisualizationMode::AutoAlpha: return LOCTEXT("TransparencyViewAutoAlpha", "Auto Alpha");
    case EDWCTransparencyVisualizationMode::WrinkleSeparation: return LOCTEXT("TransparencyViewWrinkleSeparation", "Wrinkle Separation");
    case EDWCTransparencyVisualizationMode::ValidHit: return LOCTEXT("TransparencyViewValidHit", "Valid Hit");
    case EDWCTransparencyVisualizationMode::HitDistance: return LOCTEXT("TransparencyViewHitDistance", "Hit Distance");
    case EDWCTransparencyVisualizationMode::SourcePriority: return LOCTEXT("TransparencyViewSourcePriority", "Source Priority");
    case EDWCTransparencyVisualizationMode::RevealNormalOnly: return LOCTEXT("TransparencyViewRevealNormalOnly", "Reveal Normal Only");
    case EDWCTransparencyVisualizationMode::RevealNormalTexture: return LOCTEXT("TransparencyViewRevealNormalTexture", "Reveal Normal Texture");
    case EDWCTransparencyVisualizationMode::SourceCoverage: return LOCTEXT("TransparencyViewSourceCoverage", "Source Coverage");
    default: return LOCTEXT("TransparencyViewFinal", "Final");
    }
}

TSharedPtr<EDWCTransparencyVisualizationMode> SWetClothingTransparencyBakePanel::FindVisualizationModeItem(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage) const
{
    const TArray<TSharedPtr<EDWCTransparencyVisualizationMode>>& Items =
        Stage == EDWCTransparencyEditorStage::RevealEditing
        ? RevealVisualizationModeItems
        : FinalVisualizationModeItems;
    const TSharedPtr<EDWCTransparencyVisualizationMode>* Match = Items.FindByPredicate(
        [Mode](const TSharedPtr<EDWCTransparencyVisualizationMode>& Item)
        {
            return Item.IsValid() && *Item == Mode;
        });
    return Match != nullptr ? *Match : nullptr;
}

EDWCTransparencyVisualizationMode SWetClothingTransparencyBakePanel::GetVisualizationModeForStage(
    const EDWCTransparencyEditorStage Stage) const
{
    if (SessionStore.IsValid())
    {
        const FDWCEditorTransparencySessionState& Transparency =
            SessionStore->GetState().Transparency;
        return Stage == EDWCTransparencyEditorStage::RevealEditing
            ? Transparency.RevealVisualizationMode
            : Stage == EDWCTransparencyEditorStage::FinalEditing
            ? Transparency.FinalVisualizationMode
            : SelectedVisualizationMode;
    }
    return Stage == EDWCTransparencyEditorStage::RevealEditing
        ? EDWCTransparencyVisualizationMode::InnerColor
        : EDWCTransparencyVisualizationMode::Final;
}

void SWetClothingTransparencyBakePanel::SetVisualizationModeForStage(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const EDWCTransparencySourceType SourceType = Layer != nullptr
        ? Layer->SourceType
        : EDWCTransparencySourceType::SameMeshMaterialSlots;
    if (!DWCTransparencyWorkflow::IsVisualizationModeAllowed(Stage, SourceType, Mode))
    {
        return;
    }
    SelectedVisualizationMode = Mode;
    DispatchTransparencyPreviewState();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetVisualizationMode(Mode);
    }
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsPreviewModeChecked(EWetClothingTransparencyPreviewMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewMode() == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}
void SWetClothingTransparencyBakePanel::HandlePreviewModeChanged(ECheckBoxState State, EWetClothingTransparencyPreviewMode Mode)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    if (State == ECheckBoxState::Checked && PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(Mode);
        DispatchTransparencyPreviewState();
    }
}

bool SWetClothingTransparencyBakePanel::IsGenerateEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr || Layer == nullptr ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion ||
        !HasUsableTransparencyDataUV()) return false;
    if (!IsSourceTypeAvailable(Layer->SourceType)) return false;
    if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents &&
        (Layer->BlueprintSource.BlueprintClass.IsNull() ||
            !Layer->BlueprintSource.TargetComponent.IsBound() ||
            Layer->BlueprintSource.SourcePriority.IsEmpty() ||
            !IsBlueprintHierarchyCurrent() || !BlueprintHierarchyError.IsEmpty())) return false;
    TArray<FString> Errors;
    return PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex) &&
        FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
            Asset->GetDWCSkeletalMesh(), *Layer, Errors, GetTransparencyDataUVChannel());
}
bool SWetClothingTransparencyBakePanel::IsBakeEditedEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion ||
        !IsSourceTypeAvailable(Layer->SourceType) ||
        !HasUsableTransparencyDataUV() ||
        !PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        return false;
    }
    const TSharedPtr<FDWCTransparencySourcePayload>* Result = AutoBakeResults.Find(Layer->LayerGuid);
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
    return Layer != nullptr &&
        (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents ||
         Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh);
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
        InnerSourceStatusMessage.Reset();
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        InnerSourceStatusMessage = TEXT("Base reveal color and Reveal Color Paint are written directly to the target DWC UV Channel. No ray projection is used.");
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        const int32 SourceCount = Layer->ExternalMeshSource.SourcePriority.Num();
        InnerSourceStatusMessage = SourceCount > 0
            ? FString::Printf(TEXT("%d external raycast source(s) configured. Use Full Preview and Place to align them with the target mesh."), SourceCount)
            : TEXT("Add at least one External Skeletal Mesh raycast source.");
        return;
    }
    if (Layer->BlueprintSource.BlueprintClass.IsNull())
    {
        InnerSourceStatusMessage = TEXT("Assign a Source Blueprint.");
        return;
    }
    if (!BlueprintHierarchyError.IsEmpty())
    {
        InnerSourceStatusMessage = BlueprintHierarchyError;
        return;
    }
    int32 TargetCandidateCount = 0;
    for (const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>& Component : BlueprintHierarchyItems)
    {
        TargetCandidateCount += Component.IsValid() && IsBlueprintTargetCandidate(*Component) ? 1 : 0;
    }
    if (TargetCandidateCount == 0)
    {
        InnerSourceStatusMessage = TEXT("The selected Blueprint does not contain the WCA target Skeletal Mesh.");
        return;
    }
    if (!Layer->BlueprintSource.TargetComponent.IsBound())
    {
        InnerSourceStatusMessage = TargetCandidateCount == 1
            ? TEXT("Refresh the Blueprint hierarchy to bind the target mesh.")
            : TEXT("Select the Blueprint Target Component before choosing raycast sources.");
        return;
    }
    if (Layer->BlueprintSource.SourcePriority.IsEmpty())
    {
        InnerSourceStatusMessage = TEXT("Select one or more Blueprint Skeletal Mesh Components for raycast.");
        return;
    }
    InnerSourceStatusMessage = TEXT("Blueprint target and raycast source priority are ready.");
}

FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; }) : nullptr;
}
const FWetClothingTransparencyLayerData* SWetClothingTransparencyBakePanel::GetSelectedLayer() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
        [this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; }) : nullptr;
}

SWetClothingTransparencyBakePanel::FMaterialSlotItemPtr SWetClothingTransparencyBakePanel::FindMaterialSlotItem(int32 SlotIndex) const
{
    const FMaterialSlotItemPtr* Match = MaterialSlotItems.FindByPredicate([SlotIndex](const FMaterialSlotItemPtr& Item) { return Item.IsValid() && Item->SlotIndex == SlotIndex; });
    return Match != nullptr ? *Match : nullptr;
}

const FDWCEditorPreviewSlotState* SWetClothingTransparencyBakePanel::FindPreviewSlotState(
    const int32 SlotIndex) const
{
    return PreviewSlotStates.Find(SlotIndex);
}

int32 SWetClothingTransparencyBakePanel::GetTransparencyDataUVChannel() const
{
    return ResolveTransparencyDataUVChannel(WetClothingAsset.Get());
}

bool SWetClothingTransparencyBakePanel::HasUsableTransparencyDataUV() const
{
    return GetTransparencyDataUVChannel() != INDEX_NONE;
}
TSharedPtr<int32> SWetClothingTransparencyBakePanel::FindUVChannelItem(int32 Index) const
{
    const TSharedPtr<int32>* Match = UVChannelItems.FindByPredicate([Index](const TSharedPtr<int32>& Item) { return Item.IsValid() && *Item == Index; });
    return Match != nullptr ? *Match : nullptr;
}
void SWetClothingTransparencyBakePanel::EditSelectedLayer(const FText& Text, TFunctionRef<void(FWetClothingTransparencyLayerData&)> Edit, bool bRebuild)
{
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AuthoringDocument.IsValid() || Layer == nullptr) return;
    const FGuid LayerGuid = Layer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        Text,
        Change,
        [LayerGuid, &Edit](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (MutableLayer == nullptr) return false;
            Edit(*MutableLayer);
            return true;
        });
    if (!Result.bChanged) return;
    AutoBakeResults.Remove(LayerGuid);
    EDWCTransparencyPanelRefreshFlags RefreshFlags =
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::Viewport;
    if (bRebuild)
    {
        RefreshFlags |=
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Details;
    }
    else if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
    }
    RequestRefresh(RefreshFlags);
}

bool SWetClothingTransparencyBakePanel::EditSelectedLayerFinal(
    const FText& Text,
    const FGuid& ElementGuid,
    TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AuthoringDocument.IsValid() || Layer == nullptr)
    {
        return false;
    }

    const FGuid LayerGuid = Layer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::PreviewIncremental |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = ElementGuid;
    UWetClothingAsset* MutableAsset = WetClothingAsset.Get();
    UDWCTransparencyLayerStrokeHistory* StrokeHistory = MutableAsset != nullptr
        ? MutableAsset->EnsureTransparencyLayerStrokeHistory(LayerGuid)
        : nullptr;
    if (StrokeHistory == nullptr)
    {
        return false;
    }
    return AuthoringDocument->Edit(
        Text,
        Change,
        StrokeHistory,
        [LayerGuid, &Edit](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            return MutableLayer != nullptr && Edit(*MutableLayer);
        }).bChanged;
}

void SWetClothingTransparencyBakePanel::EditGlobalSettings(const FText& Text, TFunctionRef<void(FWetClothingTransparencyData&)> Edit)
{
    if (!AuthoringDocument.IsValid()) return;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        Text,
        Change,
        [&Edit](UWetClothingAsset& Asset)
        {
            Edit(Asset.Authored.TransparencyData);
            return true;
        });
    if (!Result.bChanged) return;
    AutoBakeResults.Reset();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::EditFinalBakeSettings(
    const FText& Text,
    TFunctionRef<bool(FWetClothingTransparencyData&)> Edit,
    const EDWCTransparencyFinalPreviewRefresh PreviewRefresh)
{
    if (!AuthoringDocument.IsValid())
    {
        return;
    }
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        Text,
        Change,
        [&Edit](UWetClothingAsset& Asset)
        {
            return Edit(Asset.Authored.TransparencyData);
        });
    if (!Result.bChanged) return;
    if (PreviewViewport.IsValid())
    {
        if (PreviewRefresh == EDWCTransparencyFinalPreviewRefresh::WrinkleSuppression)
        {
            PreviewViewport->RefreshWrinkleSuppressionPreview();
        }
        else if (PreviewRefresh == EDWCTransparencyFinalPreviewRefresh::OuterEdgeFeather)
        {
            PreviewViewport->RefreshOuterEdgeFeatherPreview();
        }
    }
}

void SWetClothingTransparencyBakePanel::CommitTransparencyPreviewSettings(
    const FText& Text,
    const FDWCTransparencyPreviewSettings& Settings)
{
    if (!AuthoringDocument.IsValid())
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    AuthoringDocument->Edit(
        Text,
        Change,
        [&Settings](UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyData& Data = Asset.Authored.TransparencyData;
            const bool bChanged =
                !FMath::IsNearlyEqual(Data.TransparencyPreviewStrength, Settings.TransparencyStrength) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionStrength, Settings.WrinkleSuppressionStrength) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionCoverageThreshold, Settings.WrinkleMaskThreshold) ||
                !FMath::IsNearlyEqual(Data.WrinkleSuppressionMaskSoftness, Settings.WrinkleMaskSoftness);
            if (!bChanged)
            {
                return false;
            }

            Data.TransparencyPreviewStrength = Settings.TransparencyStrength;
            Data.WrinkleSuppressionStrength = Settings.WrinkleSuppressionStrength;
            Data.WrinkleSuppressionCoverageThreshold = Settings.WrinkleMaskThreshold;
            Data.WrinkleSuppressionMaskSoftness = Settings.WrinkleMaskSoftness;
            return true;
        });
}

void SWetClothingTransparencyBakePanel::CommitRevealNormalRuntimeSettings(
    const FText& TransactionText,
    const bool bEnable,
    const float Strength)
{
    if (!AuthoringDocument.IsValid() || !SelectedLayerGuid.IsValid())
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::RuntimeBinding;
    Change.LayerGuid = SelectedLayerGuid;
    AuthoringDocument->Edit(
        TransactionText,
        Change,
        [LayerGuid = SelectedLayerGuid, bEnable, SafeStrength = FMath::Clamp(Strength, 0.0f, 4.0f)](
            UWetClothingAsset& Asset)
        {
            FWetClothingTransparencyLayerData* Layer =
                Asset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (Layer == nullptr ||
                (Layer->bEnableRevealNormal == bEnable &&
                 FMath::IsNearlyEqual(Layer->RevealNormalStrength, SafeStrength)))
            {
                return false;
            }
            Layer->bEnableRevealNormal = bEnable;
            Layer->RevealNormalStrength = SafeStrength;
            return true;
        });
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateLayerRow(FLayerItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
    UMaterialInterface* Material = nullptr;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get();
        Asset != nullptr && Item.IsValid())
    {
        if (const USkeletalMesh* Mesh = Asset->GetDWCSkeletalMesh();
            Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(Item->MaterialSlotIndex))
        {
            Material = Mesh->GetMaterials()[Item->MaterialSlotIndex].MaterialInterface;
        }
    }

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));
    if (Material != nullptr && ThumbnailPool.IsValid() && Item.IsValid())
    {
        TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialSlotThumbnails.FindOrAdd(Item->MaterialSlotIndex);
        if (!Thumbnail.IsValid())
        {
            Thumbnail = MakeShared<FAssetThumbnail>(Material, 48, 48, ThumbnailPool);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    const FDWCEditorPreviewSlotState* PreviewState = Item.IsValid()
        ? FindPreviewSlotState(Item->MaterialSlotIndex)
        : nullptr;
    const bool bPreviewReady = PreviewState != nullptr && PreviewState->bPreviewReady;
    const FText PreviewTooltip = PreviewState != nullptr
        ? FDWCEditorPreviewSlotResolver::GetIssueText(PreviewState->Issue)
        : FText::GetEmpty();
    FText ResolutionText = LOCTEXT("TransparencyNotConfigured", "Not Configured");
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get();
        Asset != nullptr && Item.IsValid())
    {
        if (const FWetClothingTransparencyLayerData* Layer =
                Asset->Authored.TransparencyData.FindTransparencyLayer(Item->MaterialSlotIndex))
        {
            if (Layer->Intent == EDWCTransparencyLayerIntent::Draft)
            {
                ResolutionText = LOCTEXT("TransparencyDraft", "Draft");
            }
            else if (Layer->Intent == EDWCTransparencyLayerIntent::Disabled)
            {
                ResolutionText = LOCTEXT("TransparencyDisabled", "Disabled");
            }
            else
            {
                const FDWCTransparencyResolvedOutputResolution Resolved =
                    FDWCTransparencyResolutionResolver::Resolve(*Asset, *Layer);
                ResolutionText = Layer->OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Auto
                    ? FText::Format(
                        LOCTEXT("TransparencyAutoResolutionRow", "Auto  {0}"),
                        FText::AsNumber(Resolved.Size))
                    : FText::AsNumber(Resolved.Size);
            }
        }
    }

    return SNew(STableRow<FLayerItemPtr>, Owner)
        .Padding(4.0f)
        .IsEnabled(bPreviewReady)
        .ToolTipText(PreviewTooltip)
        [SNew(SHorizontalBox)
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
               [SNew(SBox)
                    .WidthOverride(52.0f)
                    .HeightOverride(52.0f)
                    [ThumbnailWidget]]
         // Matches the Material Slots row layout without paying the cost of per-row UV previews.
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
               [SNew(SBox)
                    .WidthOverride(52.0f)
                    .HeightOverride(52.0f)]
         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .VAlign(VAlign_Center)
               .Padding(2.0f, 0.0f, 10.0f, 0.0f)
               [SNew(STextBlock)
                    .Text(Item.IsValid()
                              ? FText::Format(
                                    LOCTEXT("TransparencyTargetPartRow", "[{0}] {1}"),
                                    FText::AsNumber(Item->MaterialSlotIndex),
                                    FText::FromName(Item->MaterialSlotName))
                              : LOCTEXT("MissingTransparencyTargetPart", "Missing Target Part"))
                    .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               [SNew(STextBlock)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .Text(ResolutionText)]];
}

void SWetClothingTransparencyBakePanel::HandleLayerSelectionChanged(FLayerItemPtr Item, ESelectInfo::Type)
{
    if (bRefreshingLayerSelection)
    {
        return;
    }
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr)
    {
        return;
    }
    if (Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        StatusMessage = FString::Printf(
            TEXT("Unsupported Transparency data version %d (current: %d). Recreate the Transparency setup for this WCA."),
            Asset->Authored.TransparencyData.DataVersion,
            FWetClothingTransparencyData::CurrentDataVersion);
        PanelStatus = EDWCTransparencyPanelStatus::Error;
        return;
    }

    if (!Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured)
    {
        StatusMessage = TEXT("Choose a Character Structure Type in Stage 1 before selecting a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return;
    }

    FDWCEditorAuthoringOperationScope DiagnosticScope(
        TEXT("Transparency.SelectTargetPart"),
        Asset);

    if (SelectedMaterialSlotIndex != Item->MaterialSlotIndex)
    {
        // Full 2048 intermediate results are intentionally scoped to the active target part.
        AutoBakeResults.Reset();
    }
    const FWetClothingTransparencyLayerData* NewLayer =
        Asset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
            [LayerGuid = Item->LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
            {
                return Candidate.LayerGuid == LayerGuid;
            });
    const bool bHasBakedBaseline = FindExactBakedTransparencyMap(Asset, NewLayer) != nullptr;
    const DWCTransparencyWorkflow::FDWCTransparencyLayerWorkflowState WorkflowState =
        DWCTransparencyWorkflow::ResolveLayerWorkflowState(
            Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
            NewLayer,
            bHasBakedBaseline);
    const EDWCTransparencyEditorStage Stage =
        [&]()
        {
            if (const EDWCTransparencyEditorStage* ExistingStage = StageByLayer.Find(Item->LayerGuid))
            {
                return DWCTransparencyWorkflow::NormalizeRequestedStage(*ExistingStage, WorkflowState);
            }
            return WorkflowState.DefaultStage;
        }();
    SelectTransparencyLayerWithResolvedStage(Item->MaterialSlotIndex, Item->LayerGuid, Stage);
    if (NewLayer != nullptr)
    {
        FDWCTransparencyPreviewSettings Settings = GetTransparencyPreviewSettings();
        Settings.RevealNormalStrength = NewLayer->RevealNormalStrength;
        DispatchTransparencyPreviewSettings(MoveTemp(Settings));
    }
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
    }
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

bool SWetClothingTransparencyBakePanel::CanCreateLayerForSelectedSlot() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
           SelectedMaterialSlotIndex != INDEX_NONE &&
           GetSelectedLayer() == nullptr &&
           Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured &&
           GetTransparencyDataUVChannel() != INDEX_NONE &&
           PreviewSlotStates.IsReady(SelectedMaterialSlotIndex);
}

FReply SWetClothingTransparencyBakePanel::HandleCreateLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !CanCreateLayerForSelectedSlot())
    {
        StatusMessage = TEXT("The selected Wettable slot is not ready to create a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return FReply::Handled();
    }

    const FLayerItemPtr* SelectedItem = LayerItems.FindByPredicate(
        [this](const FLayerItemPtr& Item)
        {
            return Item.IsValid() && Item->MaterialSlotIndex == SelectedMaterialSlotIndex;
        });
    if (SelectedItem == nullptr)
    {
        return FReply::Handled();
    }

    const FGuid NewLayerGuid = FGuid::NewGuid();
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = SelectedMaterialSlotIndex;
    Change.LayerGuid = NewLayerGuid;
    const int32 SlotIndex = (*SelectedItem)->MaterialSlotIndex;
    const FName SlotName = (*SelectedItem)->MaterialSlotName;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("CreateTransparencyTargetPart", "Create Transparency Target Part"),
            Change,
            [NewLayerGuid, SlotIndex, SlotName](UWetClothingAsset& MutableAsset)
            {
                if (MutableAsset.Authored.TransparencyData.FindTransparencyLayer(SlotIndex) != nullptr)
                {
                    return false;
                }
                FWetClothingTransparencyLayerData& NewLayer =
                    MutableAsset.Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
                NewLayer.LayerGuid = NewLayerGuid;
                NewLayer.Intent = EDWCTransparencyLayerIntent::Draft;
                NewLayer.TargetSurface.OuterMaterialSlotIndex = SlotIndex;
                NewLayer.TargetSurface.OuterMaterialSlotName = SlotName;
                NewLayer.SourceType = MutableAsset.Authored.TransparencyData.CharacterStructureType;
                NewLayer.bSourceTypeConfigured = true;
                return true;
            }).bChanged)
    {
        return FReply::Handled();
    }

    (*SelectedItem)->LayerGuid = NewLayerGuid;
    SelectTransparencyLayerWithResolvedStage(
        SlotIndex,
        NewLayerGuid,
        EDWCTransparencyEditorStage::MapGeneration);
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleRemoveLayerClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SelectedLayerGuid.IsValid()) return FReply::Handled();
    if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("RemoveTransparencyLayerConfirm", "Remove the selected Transparency Target Part and its editable strokes?")) != EAppReturnType::Yes)
        return FReply::Handled();
    const FGuid RemovedLayerGuid = SelectedLayerGuid;
    const int32 RemovedSlot = GetSelectedLayer()->TargetSurface.OuterMaterialSlotIndex;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = RemovedSlot;
    Change.LayerGuid = RemovedLayerGuid;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("RemoveTransparencyLayer", "Remove Transparency Target Part"),
            Change,
            [RemovedLayerGuid](UWetClothingAsset& MutableAsset)
            {
                return MutableAsset.Authored.TransparencyData.TransparencyLayers.RemoveAll(
                    [RemovedLayerGuid](const FWetClothingTransparencyLayerData& Layer)
                    {
                        return Layer.LayerGuid == RemovedLayerGuid;
                    }) > 0;
            }).bChanged) return FReply::Handled();
    AutoBakeResults.Remove(RemovedLayerGuid);
    StageByLayer.Remove(RemovedLayerGuid);
    SelectedLayerGuid.Invalidate();
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(
            FDWCSelectTransparencyLayerAction{FGuid(), RemovedSlot});
    }
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
bool SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer() const { return GetSelectedLayer() != nullptr; }

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem(TSharedPtr<int32> Item) const { return SNew(STextBlock).Text(FText::Format(LOCTEXT("UVChannelLabel", "UV {0}"), FText::AsNumber(Item.IsValid() ? *Item : 0))); }
bool SWetClothingTransparencyBakePanel::IsVisualizationModeAvailable(
    const EDWCTransparencyVisualizationMode Mode,
    const EDWCTransparencyEditorStage Stage) const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || !DWCTransparencyWorkflow::IsVisualizationModeAllowed(
            Stage,
            Layer->SourceType,
            Mode))
    {
        return false;
    }
    if ((Mode == EDWCTransparencyVisualizationMode::RevealNormalOnly ||
         Mode == EDWCTransparencyVisualizationMode::SourceCoverage ||
         Mode == EDWCTransparencyVisualizationMode::RevealNormalTexture) &&
        GetTransparencyPreviewSettings().RevealNormalSource ==
            EDWCTransparencyRevealNormalPreviewSource::Baked)
    {
        return false;
    }

    const TSharedPtr<FDWCTransparencySourcePayload>* Result =
        AutoBakeResults.Find(SelectedLayerGuid);
    return Result != nullptr && Result->IsValid() &&
        (Stage != EDWCTransparencyEditorStage::RevealEditing ||
         !(*Result)->bIsFinalBakedBaseline);
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
        Slot.bEnabled = true;
        Slot.MaterialSlotIndex = Item->SlotIndex;
        Slot.MaterialSlotName = Item->SlotName;
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
    // The row thumbnail is material-specific, so rebuild only this Stage 2
    // content after a valid source material change.
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerSlot", "Set Transparency Inner Material Slot"), [Item, Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) { auto& Slot = Layer.SameMeshSource.InnerSlotPriority[Index]; Slot.MaterialSlotIndex = Item->SlotIndex; Slot.MaterialSlotName = Item->SlotName; } }, true);
}
void SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged(TSharedPtr<int32> Item, ESelectInfo::Type, int32 Index)
{
    if (!Item.IsValid()) return;
    EditSelectedLayer(LOCTEXT("SetTransparencyInnerUV", "Set Transparency Inner UV Channel"), [Item, Index](auto& Layer) { if (Layer.SameMeshSource.InnerSlotPriority.IsValidIndex(Index)) Layer.SameMeshSource.InnerSlotPriority[Index].SourceUVChannel = *Item; }, false);
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildControlPanel()
{
    return SNew(SBorder).Padding(12.0f).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [SAssignNew(ControlPanelScrollBox, SScrollBox) + SScrollBox::Slot()[SNew(SVerticalBox)
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTargetMeshSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildStageNavigation()]
         + SVerticalBox::Slot().AutoHeight()
           [SAssignNew(StageContentSwitcher, SWidgetSwitcher)
             + SWidgetSwitcher::Slot()[BuildStructureSetupStage()]
             + SWidgetSwitcher::Slot()[BuildMapGenerationStage()]
             + SWidgetSwitcher::Slot()[BuildRevealEditingStage()]
             + SWidgetSwitcher::Slot()[BuildFinalEditingStage()]]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildStageNavigation()
{
    auto StageButton = [this](const EDWCTransparencyEditorStage Stage, const FText& Label, const FText& Tooltip)
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
            .Type(ESlateCheckBoxType::ToggleButton)
            .ToolTipText(Tooltip)
            .IsEnabled_Lambda([this, Stage]()
            {
                const UWetClothingAsset* Asset = WetClothingAsset.Get();
                if (Asset == nullptr)
                {
                    return false;
                }
                if (Stage == EDWCTransparencyEditorStage::StructureSetup)
                {
                    return true;
                }
                if (Stage == EDWCTransparencyEditorStage::MapGeneration)
                {
                    return !HasDirtySourceTypeDraft() &&
                        ResolveSelectedLayerWorkflowState().CanEnterMapGeneration();
                }
                if (Stage == EDWCTransparencyEditorStage::RevealEditing)
                {
                    return !HasDirtySourceTypeDraft() && CanEnterRevealEditingStage();
                }
                return !HasDirtySourceTypeDraft() && CanEnterFinalEditingStage();
            })
            .IsChecked(this, &SWetClothingTransparencyBakePanel::IsStageChecked, Stage)
            .OnCheckStateChanged_Lambda([this, Stage](const ECheckBoxState State)
            {
                if (State == ECheckBoxState::Checked)
                {
                    HandleStageClicked(Stage);
                }
            })
            [SNew(STextBlock).Text(Label).Justification(ETextJustify::Center)];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TransparencyWorkflow", "Transparency Workflow"))]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)
              [StageButton(
                  EDWCTransparencyEditorStage::StructureSetup,
                  LOCTEXT("TransparencyStage1", "1. Structure"),
                  LOCTEXT("TransparencyStage1Tooltip", "Choose how the inner color source is produced."))]
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)
              [StageButton(
                  EDWCTransparencyEditorStage::MapGeneration,
                  LOCTEXT("TransparencyStage2", "2. Generate"),
                  LOCTEXT("TransparencyStage2Tooltip", "Configure the source and generate an editable preview map."))]
            + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,3,0)
              [StageButton(
                  EDWCTransparencyEditorStage::RevealEditing,
                  LOCTEXT("TransparencyStage3", "3. Reveal Color"),
                  LOCTEXT("TransparencyStage3Tooltip", "Review and paint the reveal color produced by Stage 2."))]
            + SHorizontalBox::Slot().FillWidth(1)
              [StageButton(
                  EDWCTransparencyEditorStage::FinalEditing,
                  LOCTEXT("TransparencyStage4", "4. Alpha & Bake"),
                  LOCTEXT("TransparencyStage4Tooltip", "Edit alpha and bake the runtime Transparency Map."))]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildSourceTypeCard(
    const EDWCTransparencySourceType SourceType,
    const FText& Title,
    const FText& Description,
    const FText& Availability)
{
    return SNew(SCheckBox)
        .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
        .Type(ESlateCheckBoxType::ToggleButton)
        .IsEnabled_Lambda([this, SourceType]()
        {
            return IsSourceTypeAvailable(SourceType);
        })
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsSourceTypeCardChecked, SourceType)
        .OnCheckStateChanged_Lambda([this, SourceType](const ECheckBoxState State)
        {
            if (State == ECheckBoxState::Checked)
            {
                HandleSourceTypeCardClicked(SourceType);
            }
        })
        [SNew(SBox).Padding(FMargin(8,6))
          [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
              [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                  [SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                + SHorizontalBox::Slot().AutoWidth()
                  [SNew(STextBlock)
                    .Text_Lambda([this, SourceType, Availability]()
                    {
                        return GetSourceTypeCardStatusText(SourceType, Availability);
                    })
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
              [SNew(STextBlock).Text(Description).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildStructureSetupStage()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("StructureSetupStage", "Stage 1 - Character Structure"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
          [SNew(STextBlock)
            .Text(LOCTEXT("StructureSetupDescription", "Choose how this character provides the color visible through wet target surfaces. Target Parts created in Stage 2 use this structure type."))
            .AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [BuildSourceTypeCard(
              EDWCTransparencySourceType::SameMeshMaterialSlots,
              LOCTEXT("SingleMeshStructure", "Single Skeletal Mesh / Inner Material Slots"),
              LOCTEXT("SingleMeshStructureDescription", "The target clothing and its inner body or garment surfaces are material slots of the same Skeletal Mesh."),
              LOCTEXT("StructureAvailable", "Available"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [BuildSourceTypeCard(
              EDWCTransparencySourceType::OtherSkeletalMeshComponents,
              LOCTEXT("MultiMeshStructure", "Blueprint / Multiple Skeletal Meshes"),
              LOCTEXT("MultiMeshStructureDescription", "The target and inner surfaces are separate Skeletal Mesh Components in one character Blueprint."),
              LOCTEXT("StructureAvailableMulti", "Available"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [BuildSourceTypeCard(
              EDWCTransparencySourceType::ExternalSkeletalMesh,
              LOCTEXT("ExternalMeshStructure", "External Skeletal Mesh"),
              LOCTEXT("ExternalMeshStructureDescription", "Use a separately authored Skeletal Mesh as the inner reveal source and place it in the target bake space."),
              LOCTEXT("StructureAvailableExternal", "Available"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [BuildSourceTypeCard(
              EDWCTransparencySourceType::ManualColorOrTexture,
              LOCTEXT("NoInnerMeshStructure", "No Inner Mesh / Base Color"),
              LOCTEXT("NoInnerMeshStructureDescription", "Create a base reveal color from an authored color or UV-island average, correct it with the Reveal Color brush, then edit transparency alpha."),
              LOCTEXT("StructureAvailable2", "Available"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
          [SNew(SHorizontalBox)
            .Visibility(this, &SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusVisibility)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
              [SNew(STextBlock)
                .Text(this, &SWetClothingTransparencyBakePanel::GetSourceTypeDraftStatusText)
                .AutoWrapText(true)
                .ColorAndOpacity(FStyleColors::Warning)]
            + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
              [SNew(SButton)
                .Text(LOCTEXT("CancelTransparencySourceTypeDraft", "Cancel Change"))
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleCancelSourceTypeDraftClicked)]]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("ContinueToTransparencyGeneration", "Continue to Stage 2"))
            .IsEnabled_Lambda([this]()
            {
                return CanContinueToGeneration();
            })
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildMapGenerationStage()
{
    const auto BuildSameMeshSettings = [this]()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,6)
              [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("Stage2InnerSources", "Inner Source Parts"))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildSameMeshSourceSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildRaySettingsSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildBakeSettingsSection(false)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
              [SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GeneratePreviewTransparencyMap", "Generate Preview Transparency Map"))
                .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
                .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    };

    const auto BuildOtherMeshSettings = [this]()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[BuildOtherMeshSourceSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildRaySettingsSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
              [SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenerateBlueprintPreviewTransparencyMap", "Generate Preview Transparency Map"))
                .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
                .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    };

    const auto BuildExternalMeshSettings = [this]()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[BuildExternalMeshSourceSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildRaySettingsSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
              [SNew(SButton)
                .HAlign(HAlign_Center)
                .Text(LOCTEXT("GenerateExternalPreviewTransparencyMap", "Generate Preview Transparency Map"))
                .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
                .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    };

    const auto BuildManualSettings = [this]()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildManualSourceSection()]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
              [SNew(SButton)
                .HAlign(HAlign_Fill)
                .ContentPadding(FMargin(8.0f, 7.0f))
                .Text(LOCTEXT("GenerateManualPreviewTransparencyMap", "Generate Preview Transparency Map"))
                .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
                .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("MapGenerationStage", "Stage 2 - Reveal Source Generation"))]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SSplitter)
            .Orientation(Orient_Vertical)
            + SSplitter::Slot()
                .SizeRule(SSplitter::SizeToContent)
                .MinSize(148.0f)
                .Resizable(true)
                .OnSlotResized(this, &SWetClothingTransparencyBakePanel::HandleTransparencyTargetPartsResized)
                [BuildTransparencyLayersSection()]
            + SSplitter::Slot()
                .SizeRule(SSplitter::SizeToContent)
                .MinSize(180.0f)
                .Resizable(true)
                .OnSlotResized_Lambda([](float) {})
                [SAssignNew(MapGenerationSettingsSwitcher, SWidgetSwitcher)
                    + SWidgetSwitcher::Slot()
                      [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                          [BuildEmptyAssetRow(LOCTEXT(
                              "SelectTargetPartForGeneration",
                              "Select a Wettable slot above. Selecting a slot does not create Transparency data."))]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                          [SNew(SButton)
                            .HAlign(HAlign_Center)
                            .Text(LOCTEXT("CreateSelectedTransparencyTargetPart", "Create Transparency Target Part"))
                            .ToolTipText(LOCTEXT(
                                "CreateSelectedTransparencyTargetPartTooltip",
                                "Explicitly create editable Transparency data for the selected Wettable slot."))
                            .IsEnabled(this, &SWetClothingTransparencyBakePanel::CanCreateLayerForSelectedSlot)
                            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleCreateLayerClicked)]]
                    + SWidgetSwitcher::Slot()[BuildSameMeshSettings()]
                    + SWidgetSwitcher::Slot()[BuildOtherMeshSettings()]
                    + SWidgetSwitcher::Slot()[BuildManualSettings()]
                    + SWidgetSwitcher::Slot()[BuildExternalMeshSettings()]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealEditingStage()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(
              LOCTEXT("RevealEditingStage", "Stage 3 - Reveal Color Editing"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
          [SNew(STextBlock)
           .Text(LOCTEXT("RevealEditingDescription", "Review the Stage 2 source result and correct missed or unsuitable colors before editing transparency alpha."))
           .AutoWrapText(true)
           .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
          [SAssignNew(RevealEditingContentContainer, SBox)
           [BuildRevealColorEditingSection()]]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SButton)
           .HAlign(HAlign_Center)
           .Text(LOCTEXT("ContinueToFinalTransparencyEditing", "Continue to Stage 4"))
           .IsEnabled(this, &SWetClothingTransparencyBakePanel::CanEnterRevealEditingStage)
           .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleContinueToFinalEditingClicked)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildFinalEditingStage()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("FinalEditingStage", "Stage 4 - Transparency Alpha & Bake"))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,10)
        [SAssignNew(FinalEditingNoticeContainer, SBox)[BuildFinalEditingNotice()]];
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildTransparencyBrushSection()];
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)
        [SAssignNew(FinalEditingPreviewSettingsContainer, SBox)[BuildPreviewSettingsSection()]];
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)
        [SAssignNew(FinalEditingGeneratedOutputsContainer, SBox)[BuildGeneratedOutputsSection()]];
    Box->AddSlot().AutoHeight()
      [SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(LOCTEXT("BakeTransparencyMap", "Bake Transparency Map"))
        .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetBakeEditedTooltipText)
        .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsBakeEditedEnabled)
        .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleBakeEditedTransparencyMapClicked)];
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildFinalEditingNotice()
{
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingResult = AutoBakeResults.Find(SelectedLayerGuid);
    const bool bHasWorkingResult = WorkingResult != nullptr && WorkingResult->IsValid();
    if (!bHasWorkingResult)
    {
        return BuildEmptyAssetRow(
            LOCTEXT("NoWorkingTransparencyMap", "No editable working map is available. Return to Stage 2 and generate a Preview Transparency Map."));
    }
    if ((*WorkingResult)->bIsFinalBakedBaseline)
    {
        return SNew(SBorder).Padding(7).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [SNew(STextBlock)
              .Text(LOCTEXT("BakedBaselineNotice", "The existing baked map is loaded as the baseline. New brush strokes are editable. Transparency and wrinkle-suppression settings are already flattened into this map; regenerate in Stage 2 to change those settings or rebuild earlier ray data."))
              .AutoWrapText(true)
              .ColorAndOpacity(FSlateColor::UseSubduedForeground())];
    }
    return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTargetMeshSection()
{
    TargetMeshThumbnails.Reset();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 SlotCount = Asset != nullptr && Asset->GetDWCSkeletalMesh() != nullptr ? Asset->GetDWCSkeletalMesh()->GetMaterials().Num() : 0;
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TargetMeshSection", "Target Mesh"))]
        + SVerticalBox::Slot().AutoHeight()[Asset != nullptr && Asset->GetDWCSkeletalMesh() != nullptr
            ? BuildAssetSummaryRow(Asset->GetDWCSkeletalMesh(), FText::FromString(Asset->GetDWCSkeletalMesh()->GetName()), FText::Format(LOCTEXT("TargetMeshDetails", "{0} material slots / {1} UV channels"), FText::AsNumber(SlotCount), FText::AsNumber(UVChannelItems.Num())), TargetMeshThumbnails)
            : BuildEmptyAssetRow(LOCTEXT("NoTargetMesh", "None"))];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyLayersSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
              [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("TransparencyLayers", "Transparency Target Parts"))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                .ToolTipText(LOCTEXT("RemoveTargetPartTooltip", "Remove the selected Transparency Target Part and its editable data."))
                .IsEnabled(this, &SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer)
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveLayerClicked)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]
        + SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride_Lambda([this]() { return TransparencyTargetPartsListHeight; })[SAssignNew(LayerListView, SListView<FLayerItemPtr>).ListItemsSource(&LayerItems).OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateLayerRow).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleLayerSelectionChanged)]]
        ;
}

void SWetClothingTransparencyBakePanel::HandleTransparencyTargetPartsResized(const float NewHeight)
{
    // The splitter resizes the complete section; keep the compact title row outside the list height.
    const float NewListHeight = FMath::Clamp(NewHeight - 30.0f, 88.0f, 640.0f);
    if (FMath::IsNearlyEqual(TransparencyTargetPartsListHeight, NewListHeight))
    {
        return;
    }

    TransparencyTargetPartsListHeight = NewListHeight;
    if (StageContentSwitcher.IsValid())
    {
        StageContentSwitcher->Invalidate(EInvalidateWidgetReason::Layout);
    }
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildSameMeshSourceSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)
    [
        SNew(SBox)
        .HeightOverride_Lambda([this]()
        {
            return FMath::Clamp(static_cast<float>(InnerSourceSlotItems.Num()) * 56.0f, 0.0f, 280.0f);
        })
        [
            SAssignNew(InnerSourceListView, SListView<TSharedPtr<int32>>)
            .ListItemsSource(&InnerSourceSlotItems)
            .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateInnerSourceRow)
        ]
    ];
    Box->AddSlot().AutoHeight()
    [SNew(SButton).HAlign(HAlign_Center).ToolTipText(LOCTEXT("AddInnerSlotTooltip", "Add a material slot to the Inner Source priority list.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleAddInnerSlotClicked)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(LOCTEXT("AddInnerSlot", "Add Inner Source Part"))]]];
    return Box;
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateInnerSourceRow(
    TSharedPtr<int32> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const int32 PriorityIndex = Item.IsValid() ? *Item : INDEX_NONE;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 MaterialSlotIndex = Layer != nullptr &&
        Layer->SameMeshSource.InnerSlotPriority.IsValidIndex(PriorityIndex)
        ? Layer->SameMeshSource.InnerSlotPriority[PriorityIndex].MaterialSlotIndex
        : INDEX_NONE;

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* Mesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    UMaterialInterface* Material = Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(MaterialSlotIndex)
        ? Mesh->GetMaterials()[MaterialSlotIndex].MaterialInterface
        : nullptr;
    if (Material != nullptr && ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail>& Thumbnail = MaterialSlotThumbnails.FindOrAdd(MaterialSlotIndex);
        if (!Thumbnail.IsValid())
        {
            Thumbnail = MakeShared<FAssetThumbnail>(Material, 44, 44, ThumbnailPool);
        }
        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
        [
            SNew(SBorder)
            .Padding(FMargin(4.0f, 3.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                [SNew(SBox).WidthOverride(44.0f).HeightOverride(44.0f)[ThumbnailWidget]]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,4,0)
                [
                    SNew(SComboBox<FMaterialSlotItemPtr>)
                    .OptionsSource(&MaterialSlotItems)
                    .InitiallySelectedItem(FindMaterialSlotItem(MaterialSlotIndex))
                    .OnGenerateWidget_Lambda([](const FMaterialSlotItemPtr& SlotItem)
                    {
                        return SNew(STextBlock).Text(SlotItem.IsValid()
                            ? FText::Format(LOCTEXT("InnerSourceMaterialOption", "[{0}] {1}"), FText::AsNumber(SlotItem->SlotIndex), FText::FromName(SlotItem->SlotName))
                            : LOCTEXT("MissingInnerSourceMaterialOption", "Missing"));
                    })
                    .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerMaterialSlotChanged, PriorityIndex)
                    [
                        SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
                        {
                            const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                            if (SelectedLayer == nullptr || !SelectedLayer->SameMeshSource.InnerSlotPriority.IsValidIndex(PriorityIndex))
                            {
                                return LOCTEXT("MissingInnerPart", "Missing");
                            }
                            const FWetClothingTransparencyInnerSlot& InnerSlot = SelectedLayer->SameMeshSource.InnerSlotPriority[PriorityIndex];
                            return FText::Format(LOCTEXT("InnerSourceMaterialLabel", "[{0}] {1}"), FText::AsNumber(InnerSlot.MaterialSlotIndex), FText::FromName(InnerSlot.MaterialSlotName));
                        })
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                [
                    SNew(SBox).WidthOverride(62.0f)
                    [
                        SNew(SComboBox<TSharedPtr<int32>>)
                        .OptionsSource(&UVChannelItems)
                        .InitiallySelectedItem(Layer != nullptr && Layer->SameMeshSource.InnerSlotPriority.IsValidIndex(PriorityIndex)
                            ? FindUVChannelItem(Layer->SameMeshSource.InnerSlotPriority[PriorityIndex].SourceUVChannel)
                            : nullptr)
                        .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem)
                        .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged, PriorityIndex)
                        [
                            SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
                            {
                                const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                                const int32 UVChannel = SelectedLayer != nullptr && SelectedLayer->SameMeshSource.InnerSlotPriority.IsValidIndex(PriorityIndex)
                                    ? SelectedLayer->SameMeshSource.InnerSlotPriority[PriorityIndex].SourceUVChannel
                                    : 0;
                                return FText::Format(LOCTEXT("InnerUV", "UV {0}"), FText::AsNumber(UVChannel));
                            })
                        ]
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerUpTooltip", "Move this source earlier in the priority order.")).IsEnabled_Lambda([PriorityIndex]() { return PriorityIndex > 0; }).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, PriorityIndex, -1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowUp")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerDownTooltip", "Move this source later in the priority order.")).IsEnabled_Lambda([this, PriorityIndex]() { const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer(); return SelectedLayer != nullptr && PriorityIndex + 1 < SelectedLayer->SameMeshSource.InnerSlotPriority.Num(); }).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, PriorityIndex, 1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowDown")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("DeleteInnerTooltip", "Remove this Inner Source Part.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveInnerSlotClicked, PriorityIndex)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]
            ]
        ];
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateBlueprintHierarchyRow(
    TSharedPtr<FDWCTransparencyBlueprintMeshComponent> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FName ComponentName = Item.IsValid() ? Item->ComponentName : NAME_None;
    const bool bTargetCandidate = Item.IsValid() && IsBlueprintTargetCandidate(*Item);
    const int32 Indent = Item.IsValid() ? GetBlueprintHierarchyDepth(*Item) : 0;
    return SNew(STableRow<TSharedPtr<FDWCTransparencyBlueprintMeshComponent>>, OwnerTable)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 3.0f))
        [
            SNew(SBorder)
            .Padding(FMargin(5.0f, 4.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SCheckBox)
                    .Visibility(bTargetCandidate ? EVisibility::Visible : EVisibility::Hidden)
                    .ToolTipText(LOCTEXT("BlueprintTargetComponentTooltip", "Use this Blueprint component as the transparency target mesh."))
                    .IsChecked_Lambda([this, ComponentName]()
                    {
                        const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                        return Layer != nullptr &&
                            Layer->BlueprintSource.TargetComponent.ComponentName == ComponentName
                            ? ECheckBoxState::Checked
                            : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleBlueprintTargetComponentChanged, ComponentName)
                    [SNew(STextBlock).Text(LOCTEXT("BlueprintTargetComponent", "Target"))]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SCheckBox)
                    .ToolTipText(LOCTEXT("BlueprintSourceComponentTooltip", "Include this component in the raycast source priority list."))
                    .IsEnabled_Lambda([this, ComponentName]()
                    {
                        const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                        return Layer != nullptr &&
                            Layer->BlueprintSource.TargetComponent.ComponentName != ComponentName;
                    })
                    .IsChecked_Lambda([this, ComponentName]()
                    {
                        const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                        return Layer != nullptr && Layer->BlueprintSource.SourcePriority.ContainsByPredicate(
                            [ComponentName](const FWetClothingTransparencyBlueprintComponentBinding& Source)
                            {
                                return Source.ComponentName == ComponentName;
                            })
                            ? ECheckBoxState::Checked
                            : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleBlueprintSourceComponentChanged, ComponentName)
                    [SNew(STextBlock).Text(LOCTEXT("BlueprintSourceComponent", "Raycast"))]
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [SNew(SSpacer).Size(FVector2D(static_cast<float>(Indent) * 14.0f, 1.0f))]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [SNew(STextBlock).Text(FText::FromName(ComponentName)).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [SNew(STextBlock)
                            .Text(Item.IsValid() && Item->SkeletalMesh != nullptr
                                ? FText::FromString(Item->SkeletalMesh->GetName())
                                : LOCTEXT("MissingBlueprintHierarchyMesh", "Missing Skeletal Mesh"))
                            .ColorAndOpacity(FStyleColors::Foreground)
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                        + SVerticalBox::Slot().AutoHeight()
                        [SNew(STextBlock)
                            .Text(Item.IsValid() ? FText::FromString(Item->DisplayPath) : FText::GetEmpty())
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]
                ]
            ]
        ];
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateBlueprintSourcePriorityRow(
    TSharedPtr<int32> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const int32 PriorityIndex = Item.IsValid() ? *Item : INDEX_NONE;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FWetClothingTransparencyBlueprintComponentBinding* Source =
        Layer != nullptr && Layer->BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex)
            ? &Layer->BlueprintSource.SourcePriority[PriorityIndex]
            : nullptr;
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>* Component = Source != nullptr
        ? BlueprintHierarchyItems.FindByPredicate(
            [Source](const TSharedPtr<FDWCTransparencyBlueprintMeshComponent>& Candidate)
            {
                return Candidate.IsValid() && Candidate->ComponentName == Source->ComponentName;
            })
        : nullptr;
    const TSharedPtr<EDWCTransparencyBlueprintSourceRole>* RoleItem = Source != nullptr
        ? BlueprintSourceRoleItems.FindByPredicate(
            [Source](const TSharedPtr<EDWCTransparencyBlueprintSourceRole>& Candidate)
            {
                return Candidate.IsValid() && *Candidate == Source->Role;
            })
        : nullptr;

    return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
        [
            SNew(SBorder)
            .Padding(FMargin(5.0f, 4.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [SNew(STextBlock).Text(FText::Format(LOCTEXT("BlueprintSourcePriorityNumber", "#{0}"), FText::AsNumber(PriorityIndex + 1)))]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock)
                        .Text(Source != nullptr ? FText::FromName(Source->ComponentName) : LOCTEXT("MissingBlueprintSource", "Missing"))
                        .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock)
                        .Text(Component != nullptr && Component->IsValid() && (*Component)->SkeletalMesh != nullptr
                            ? FText::FromString((*Component)->SkeletalMesh->GetName())
                            : LOCTEXT("MissingBlueprintSourceMesh", "Missing Skeletal Mesh"))
                        .ColorAndOpacity(FStyleColors::Foreground)]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SBox).WidthOverride(64.0f)
                    [
                        SNew(SComboBox<TSharedPtr<int32>>)
                        .OptionsSource(&UVChannelItems)
                        .InitiallySelectedItem(Source != nullptr ? FindUVChannelItem(Source->SourceUVChannel) : nullptr)
                        .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem)
                        .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleBlueprintSourceUVChannelChanged, PriorityIndex)
                        [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
                        {
                            const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                            const int32 UVChannel = SelectedLayer != nullptr &&
                                    SelectedLayer->BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex)
                                ? SelectedLayer->BlueprintSource.SourcePriority[PriorityIndex].SourceUVChannel
                                : 0;
                            return FText::Format(LOCTEXT("BlueprintSourceUV", "UV {0}"), FText::AsNumber(UVChannel));
                        })]
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [
                    SNew(SBox).WidthOverride(106.0f)
                    [
                        SNew(SComboBox<TSharedPtr<EDWCTransparencyBlueprintSourceRole>>)
                        .OptionsSource(&BlueprintSourceRoleItems)
                        .InitiallySelectedItem(RoleItem != nullptr ? *RoleItem : nullptr)
                        .OnGenerateWidget_Lambda([](const TSharedPtr<EDWCTransparencyBlueprintSourceRole>& Role)
                        {
                            return SNew(STextBlock).Text(Role.IsValid()
                                ? GetBlueprintSourceRoleLabel(*Role)
                                : LOCTEXT("MissingBlueprintSourceRole", "Missing"));
                        })
                        .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleBlueprintSourceRoleChanged, PriorityIndex)
                        [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
                        {
                            const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                            return SelectedLayer != nullptr &&
                                    SelectedLayer->BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex)
                                ? GetBlueprintSourceRoleLabel(
                                    SelectedLayer->BlueprintSource.SourcePriority[PriorityIndex].Role)
                                : LOCTEXT("MissingBlueprintSourceRoleLabel", "Missing");
                        })]
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("MoveBlueprintSourceUpTooltip", "Move this source earlier in the priority order."))
                    .IsEnabled_Lambda([PriorityIndex]() { return PriorityIndex > 0; })
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveBlueprintSourceClicked, PriorityIndex, -1)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowUp")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("MoveBlueprintSourceDownTooltip", "Move this source later in the priority order."))
                    .IsEnabled_Lambda([this, PriorityIndex]()
                    {
                        const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                        return SelectedLayer != nullptr &&
                            PriorityIndex + 1 < SelectedLayer->BlueprintSource.SourcePriority.Num();
                    })
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveBlueprintSourceClicked, PriorityIndex, 1)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowDown")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("RemoveBlueprintSourceTooltip", "Remove this component from raycast sources."))
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveBlueprintSourceClicked, PriorityIndex)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]
            ]
        ];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildOtherMeshSourceSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [BuildLabeledControl(
            LOCTEXT("SourceBlueprint", "Source Blueprint"),
            SNew(SClassPropertyEntryBox)
                .MetaClass(AActor::StaticClass())
                .AllowAbstract(false)
                .AllowNone(true)
                .SelectedClass(this, &SWetClothingTransparencyBakePanel::GetSelectedSourceClass)
                .OnSetClass(this, &SWetClothingTransparencyBakePanel::HandleSourceClassChanged))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [SNew(SButton)
            .ToolTipText(LOCTEXT("RefreshBlueprintHierarchyTooltip", "Instantiate the selected Blueprint and refresh its Skeletal Mesh Component hierarchy."))
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRefreshBlueprintHierarchyClicked)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(STextBlock).Text(LOCTEXT("RefreshBlueprintHierarchy", "Refresh Hierarchy"))]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [SNew(STextBlock)
            .Text_Lambda([this]() { return FText::FromString(InnerSourceStatusMessage); })
            .ColorAndOpacity_Lambda([this]()
            {
                return BlueprintHierarchyError.IsEmpty() ? FStyleColors::Foreground : FStyleColors::Warning;
            })
            .AutoWrapText(true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 12.0f)
        [BuildBlueprintHierarchySection()]
        + SVerticalBox::Slot().AutoHeight()
        [BuildBlueprintSourcePrioritySection()];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBlueprintHierarchySection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
        [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BlueprintMeshHierarchy", "Blueprint Skeletal Mesh Hierarchy"))]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox)
            .HeightOverride(230.0f)
            [SAssignNew(BlueprintHierarchyListView, SListView<TSharedPtr<FDWCTransparencyBlueprintMeshComponent>>)
                .ListItemsSource(&BlueprintHierarchyItems)
                .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateBlueprintHierarchyRow)]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBlueprintSourcePrioritySection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
        [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BlueprintSourcePriority", "Raycast Source Priority"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("BlueprintSourcePriorityHint", "Selected hierarchy components appear here. Higher rows win before distance is considered."))
            .AutoWrapText(true)
            .ColorAndOpacity(FStyleColors::Foreground)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox)
            .HeightOverride_Lambda([this]()
            {
                return FMath::Clamp(
                    static_cast<float>(BlueprintSourcePriorityItems.Num()) * 58.0f,
                    58.0f,
                    280.0f);
            })
            [SAssignNew(BlueprintSourcePriorityListView, SListView<TSharedPtr<int32>>)
                .ListItemsSource(&BlueprintSourcePriorityItems)
                .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateBlueprintSourcePriorityRow)]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildExternalMeshSourceSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
          [BuildLabeledControl(
              LOCTEXT("ExternalSourceMesh", "Add Skeletal Mesh Source"),
              SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1.0f)
              [SNew(SObjectPropertyEntryBox)
                  .AllowedClass(USkeletalMesh::StaticClass())
                  .AllowClear(true)
                  .ObjectPath(this, &SWetClothingTransparencyBakePanel::GetPendingExternalSourceMeshPath)
                  .OnObjectChanged(this, &SWetClothingTransparencyBakePanel::HandleExternalSourceMeshChanged)]
              + SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
              [SNew(SButton)
                  .ToolTipText(LOCTEXT("AddExternalSourceTooltip", "Add this Skeletal Mesh as a separately placeable raycast source."))
                  .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleAddExternalSourceClicked)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
                   [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))]
                   + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                   [SNew(STextBlock).Text(LOCTEXT("AddExternalSource", "Add"))]]])]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
          [BuildEmptyAssetRow(LOCTEXT(
              "ExternalSourceMeshTransformHint",
              "Each listed mesh is a separate reference-pose source. Select Place, use the viewport translate/rotate gizmo or arrow-key nudges, then order the rows by raycast priority."))]
        + SVerticalBox::Slot().AutoHeight()
          [BuildExternalMeshSourcePrioritySection()];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildExternalMeshSourcePrioritySection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
        [FWCAEditorWidgets::BuildSectionHeader(
            LOCTEXT("ExternalSourcePriority", "Raycast Source Priority & Placement"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT(
                "ExternalSourcePriorityHint",
                "Higher rows win before hit distance. Select Place to move one source in the Full Preview; every material slot of that mesh shares this priority."))
            .AutoWrapText(true)
            .ColorAndOpacity(FStyleColors::Foreground)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox)
            .HeightOverride_Lambda([this]()
            {
                return FMath::Clamp(
                    static_cast<float>(ExternalSourcePriorityItems.Num()) * 72.0f,
                    72.0f,
                    360.0f);
            })
            [SAssignNew(ExternalSourcePriorityListView, SListView<TSharedPtr<int32>>)
                .ListItemsSource(&ExternalSourcePriorityItems)
                .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateExternalSourcePriorityRow)]];
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateExternalSourcePriorityRow(
    TSharedPtr<int32> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const int32 PriorityIndex = Item.IsValid() ? *Item : INDEX_NONE;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FWetClothingTransparencyExternalMeshEntry* Source = Layer != nullptr &&
            Layer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex)
        ? &Layer->ExternalMeshSource.SourcePriority[PriorityIndex]
        : nullptr;
    const TSharedPtr<EDWCTransparencyBlueprintSourceRole>* RoleItem = Source != nullptr
        ? BlueprintSourceRoleItems.FindByPredicate(
            [Source](const TSharedPtr<EDWCTransparencyBlueprintSourceRole>& Candidate)
            {
                return Candidate.IsValid() && *Candidate == Source->Role;
            })
        : nullptr;

    return SNew(STableRow<TSharedPtr<int32>>, OwnerTable)
        .Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
        [SNew(SBorder)
         .Padding(FMargin(5.0f, 4.0f))
         .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
         [SNew(SHorizontalBox)
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
          [SNew(STextBlock).Text(FText::Format(LOCTEXT("ExternalSourcePriorityNumber", "#{0}"), FText::AsNumber(PriorityIndex + 1)))]
          + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)
          [SNew(SVerticalBox)
           + SVerticalBox::Slot().AutoHeight()
           [SNew(STextBlock)
               .Text(Source != nullptr && Source->SkeletalMesh != nullptr
                   ? FText::FromString(Source->SkeletalMesh->GetName())
                   : LOCTEXT("MissingExternalSourceMesh", "Missing Skeletal Mesh"))
               .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
           + SVerticalBox::Slot().AutoHeight()
           [SNew(STextBlock)
               .Text(Source != nullptr
                   ? FText::FromString(Source->BakeTransform.ToHumanReadableString())
                   : FText::GetEmpty())
               .ColorAndOpacity(FStyleColors::Foreground)
               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
          [SNew(SButton)
             .Text_Lambda([this, Source]()
             {
                 return Source != nullptr && Source->SourceGuid == SelectedExternalSourceGuid
                     ? LOCTEXT("ExternalSourcePlaced", "Placing")
                     : LOCTEXT("ExternalSourcePlace", "Place");
             })
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleSelectExternalSourceClicked, PriorityIndex)]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
          [SNew(SBox).WidthOverride(58.0f)
           [SNew(SComboBox<TSharedPtr<int32>>)
            .OptionsSource(&UVChannelItems)
            .InitiallySelectedItem(Source != nullptr ? FindUVChannelItem(Source->SourceUVChannel) : nullptr)
            .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem)
            .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleExternalSourceUVChannelChanged, PriorityIndex)
            [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
            {
                const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                const int32 UV = SelectedLayer != nullptr &&
                    SelectedLayer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex)
                    ? SelectedLayer->ExternalMeshSource.SourcePriority[PriorityIndex].SourceUVChannel : 0;
                return FText::Format(LOCTEXT("ExternalSourceUV", "UV {0}"), FText::AsNumber(UV));
            })]]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
          [SNew(SBox).WidthOverride(104.0f)
           [SNew(SComboBox<TSharedPtr<EDWCTransparencyBlueprintSourceRole>>)
            .OptionsSource(&BlueprintSourceRoleItems)
            .InitiallySelectedItem(RoleItem != nullptr ? *RoleItem : nullptr)
            .OnGenerateWidget_Lambda([](const TSharedPtr<EDWCTransparencyBlueprintSourceRole>& Role)
            {
                return SNew(STextBlock).Text(Role.IsValid()
                    ? GetBlueprintSourceRoleLabel(*Role)
                    : LOCTEXT("MissingExternalSourceRole", "Missing"));
            })
            .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleExternalSourceRoleChanged, PriorityIndex)
            [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
            {
                const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                return SelectedLayer != nullptr &&
                    SelectedLayer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex)
                    ? GetBlueprintSourceRoleLabel(
                        SelectedLayer->ExternalMeshSource.SourcePriority[PriorityIndex].Role)
                    : LOCTEXT("MissingExternalSourceRoleLabel", "Missing");
            })]]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .ToolTipText(LOCTEXT("ResetExternalSourceTransformTooltip", "Reset this source placement to the target mesh origin."))
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleResetExternalSourceTransformClicked, PriorityIndex)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Undo")))]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .IsEnabled_Lambda([PriorityIndex]() { return PriorityIndex > 0; })
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveExternalSourceClicked, PriorityIndex, -1)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowUp")))]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .IsEnabled_Lambda([this, PriorityIndex]() { return PriorityIndex + 1 < ExternalSourcePriorityItems.Num(); })
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveExternalSourceClicked, PriorityIndex, 1)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowDown")))]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveExternalSourceClicked, PriorityIndex)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildManualSourceSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("ManualColorSource", "Base Reveal Color Source"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
          [SNew(STextBlock)
           .Text(LOCTEXT("ManualColorSourceDescription", "Choose one representative reveal color. Select Color uses Unreal's color picker and screen eyedropper; Pick From UV Island averages only texels covered by the selected island."))
           .AutoWrapText(true)
           .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 0.0f, 10.0f, 0.0f)
              [SNew(SColorBlock)
               .Color_Lambda([this]()
               {
                   const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                   return SelectedLayer != nullptr
                       ? SelectedLayer->ManualColorSource.BaseRevealColor
                       : FLinearColor::White;
               })
               .Size(FVector2D(112.0f, 112.0f))
               .ShowBackgroundForAlpha(false)]
              + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Top)
              [SNew(SVerticalBox)
               + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
                 [SNew(SButton)
                  .ToolTipText(LOCTEXT("ManualBaseRevealColorTooltip", "Choose the color directly or use Unreal's screen eyedropper."))
                  .Text(LOCTEXT("ManualBaseRevealColorButton", "Select Color / Eyedropper"))
                  .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleManualBaseColorClicked)]
               + SVerticalBox::Slot().AutoHeight()
                 [SNew(SButton)
                  .ToolTipText(LOCTEXT("ManualPickUVIslandTooltip", "Choose a reference color texture and average only valid texels inside one UV island."))
                  .Text(LOCTEXT("ManualPickUVIslandButton", "Pick From UV Island"))
                  .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleManualPickBaseColorFromUVIslandClicked)]
               + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
                 [SNew(STextBlock)
                  .Text_Lambda([this]()
                  {
                      const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                      if (Layer == nullptr)
                      {
                          return FText::GetEmpty();
                      }
                      return Layer->ManualColorSource.SourceMode ==
                              EDWCTransparencyManualRevealSourceMode::UVIslandAverage
                          ? FText::Format(
                              LOCTEXT("ManualUVIslandSourceStatus", "Source: UV Island Average (Slot {0}, UV {1}, Island {2})"),
                              FText::AsNumber(Layer->ManualColorSource.SampledMaterialSlotIndex),
                              FText::AsNumber(Layer->ManualColorSource.SampledUVChannelIndex),
                              FText::AsNumber(Layer->ManualColorSource.SampledUVIslandID))
                          : LOCTEXT("ManualAuthoredColorSourceStatus", "Source: Authored Color / Eyedropper");
                  })
                  .AutoWrapText(true)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
               + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 4.0f)
                 [SNew(STextBlock)
                  .Text(LOCTEXT("ManualInitialAlpha", "Initial Transparency Alpha"))
                  .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
               + SVerticalBox::Slot().AutoHeight()
                 [SNew(SNumericEntryBox<float>)
                  .MinValue(0.0f)
                  .MaxValue(1.0f)
                  .Value(this, &SWetClothingTransparencyBakePanel::GetManualInitialTransparencyAlpha)
                  .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleManualInitialTransparencyAlphaCommitted)]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealColorEditingSection()
{
    RefreshRevealColorStrokeList();
    auto RevealModeButton = [this](
        EDWCTransparencyRevealColorBrushMode Mode,
        const FText& Label,
        const FText& Tooltip)
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
            .Type(ESlateCheckBoxType::ToggleButton)
            .ToolTipText(Tooltip)
            .IsChecked(this, &SWetClothingTransparencyBakePanel::IsRevealColorPaintModeChecked, Mode)
            .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealColorPaintModeChanged, Mode)
            [SNew(STextBlock).Text(Label)];
    };

    TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RevealColorPaint", "Reveal Color Paint"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
          [BuildRevealVisualizationSection()]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
          [BuildLabeledControl(
              LOCTEXT("RevealMetallicDarkening", "Metallic Darkening"),
              SNew(SNumericEntryBox<float>)
              .MinValue(0.0f)
              .MaxValue(1.0f)
              .Value(this, &SWetClothingTransparencyBakePanel::GetRevealMetallicDarkeningStrength)
              .OnValueCommitted(
                  this,
                  &SWetClothingTransparencyBakePanel::HandleRevealMetallicDarkeningStrengthCommitted))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(STextBlock)
             .AutoWrapText(true)
             .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.75f, 0.0f)))
             .Visibility_Lambda([this]()
             {
                 return CanEnterRevealEditingStage() ? EVisibility::Collapsed : EVisibility::Visible;
             })
             .Text(LOCTEXT(
                 "RevealWorkingMapRequired",
                 "Generate the Stage 2 source map before painting the reveal color."))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [SNew(SWrapBox).UseAllottedSize(true)
                 + SWrapBox::Slot()[RevealModeButton(
                     EDWCTransparencyRevealColorBrushMode::Paint,
                     LOCTEXT("RevealPaintModePaint", "Paint"),
                     LOCTEXT("RevealPaintModePaintTooltip", "Paint the selected reveal color onto the target surface."))]
                 + SWrapBox::Slot()[RevealModeButton(
                     EDWCTransparencyRevealColorBrushMode::EraseToBase,
                     LOCTEXT("RevealPaintModeEraseToBase", "Erase to Base"),
                     LOCTEXT("RevealPaintModeEraseToBaseTooltip", "Restore the target surface back toward its Base Reveal Color."))]
                 + SWrapBox::Slot()[RevealModeButton(
                     EDWCTransparencyRevealColorBrushMode::Smooth,
                     LOCTEXT("RevealPaintModeSmooth", "Smooth"),
                     LOCTEXT("RevealPaintModeSmoothTooltip", "Blend neighboring reveal colors to soften paint transitions."))]]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [SNew(SHorizontalBox)
                 + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                   [SNew(STextBlock)
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .Text_Lambda([this]()
                    {
                        switch (GetRevealPaintSettingsFromSession().RevealColorMode)
                        {
                        case EDWCTransparencyRevealColorBrushMode::EraseToBase:
                            return LOCTEXT("RevealPaintEraseHelp", "Brush strokes restore painted areas toward the Base Reveal Color.");
                        case EDWCTransparencyRevealColorBrushMode::Smooth:
                            return LOCTEXT("RevealPaintSmoothHelp", "Brush strokes average nearby reveal colors to soften hard color edges.");
                        default:
                            return LOCTEXT("RevealPaintPaintHelp", "Brush strokes apply the selected Paint Color.");
                        }
                    })]
                 + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
                   [SNew(SButton)
                    .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("ClearRevealColorPaintTooltip", "Remove all reveal-color paint strokes and restore the target to the Base Reveal Color."))
                    .IsEnabled_Lambda([this]()
                    {
                        const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                        return SelectedLayer != nullptr && !SelectedLayer->GetRevealColorPaintStrokes().IsEmpty();
                    })
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleClearRevealColorPaintClicked)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [SNew(SBox)
                 .Visibility_Lambda([this]()
                 {
                     return GetRevealPaintSettingsFromSession().RevealColorMode == EDWCTransparencyRevealColorBrushMode::Paint
                         ? EVisibility::Visible
                         : EVisibility::Collapsed;
                 })
                 [SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                    [SNew(SColorBlock).Color_Lambda([this] { return GetRevealPaintSettingsFromSession().RevealColor; }).Size(FVector2D(38, 38)).ShowBackgroundForAlpha(false)]
                  + SHorizontalBox::Slot().FillWidth(1)
                    [SNew(SButton).Text(LOCTEXT("SelectRevealPaintColor", "Select Paint Color")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintColorClicked)]]]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [BuildBrushSizeControl(LOCTEXT("RevealPaintBrushSize", "Brush Size"), EDWCTransparencyBrushSizeTarget::RevealColorPaint)]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [BuildLabeledControl(LOCTEXT("RevealPaintStrength", "Brush Strength"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetRevealPaintStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintStrengthCommitted))]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [BuildLabeledControl(LOCTEXT("RevealPaintFalloff", "Brush Falloff"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetRevealPaintFalloff).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintFalloffCommitted))]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 4)
                [SNew(SHorizontalBox)
                 + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                   [SNew(STextBlock)
                    .Text_Lambda([this]()
                    {
                        return FText::Format(
                            LOCTEXT("RevealColorStrokeHistoryCount", "Reveal Color Strokes ({0})"),
                            FText::AsNumber(RevealColorStrokeItems.Num()));
                    })
                    .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                 + SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
                   [SNew(SButton)
                    .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("UndoLastRevealColorStrokeTooltip", "Remove the most recent reveal-color stroke for this material slot."))
                    .IsEnabled_Lambda([this]() { return !RevealColorStrokeItems.IsEmpty(); })
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleUndoLastRevealColorStrokeClicked)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Undo")))]]]
              + SVerticalBox::Slot().AutoHeight()
                [SNew(SBox)
                 .HeightOverride(150.0f)
                 [SAssignNew(RevealColorStrokeListView, SListView<TSharedPtr<FGuid>>)
                  .ListItemsSource(&RevealColorStrokeItems)
                  .SelectionMode(ESelectionMode::Single)
                   .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateRevealColorStrokeRow)]]];

    return Content;
}

FReply SWetClothingTransparencyBakePanel::HandleManualBaseColorClicked()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return FReply::Handled();
    }

    FColorPickerArgs PickerArgs;
    PickerArgs.InitialColor = Layer->ManualColorSource.BaseRevealColor;
    PickerArgs.bUseAlpha = false;
    PickerArgs.bOnlyRefreshOnMouseUp = true;
    PickerArgs.ParentWidget = AsShared();
    PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(
        this,
        &SWetClothingTransparencyBakePanel::HandleManualBaseColorCommitted);
    OpenColorPicker(PickerArgs);
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleManualPickBaseColorFromUVIslandClicked()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || GetSelectedLayer() == nullptr)
    {
        return FReply::Handled();
    }

    const TSharedRef<SWindow> DialogWindow = SNew(SWindow)
        .Title(LOCTEXT("UVIslandColorPickerTitle", "Pick Base Reveal Color From UV Island"))
        .ClientSize(FVector2D(900.0f, 760.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false);

    DialogWindow->SetContent(
        SNew(SDWCTransparencyUVIslandColorPicker)
        .ParentWindow(DialogWindow)
        .InitialMesh(Asset->GetSourceSkeletalMesh())
        .InitialOriginalUVChannel(Asset->GetOriginalUVChannelIndex())
        .OnColorAccepted(FOnDWCTransparencyUVIslandColorAccepted::CreateLambda(
            [WeakPanel = TWeakPtr<SWetClothingTransparencyBakePanel>(SharedThis(this))]
            (const FDWCTransparencyUVIslandColorSelection& Selection)
            {
                const TSharedPtr<SWetClothingTransparencyBakePanel> Panel = WeakPanel.Pin();
                if (!Panel.IsValid())
                {
                    return;
                }
                Panel->EditSelectedLayer(
                    LOCTEXT("SetManualUVIslandRevealColor", "Set UV Island Reveal Color"),
                    [&Selection](FWetClothingTransparencyLayerData& TargetLayer)
                    {
                        TargetLayer.ManualColorSource.SourceMode =
                            EDWCTransparencyManualRevealSourceMode::UVIslandAverage;
                        TargetLayer.ManualColorSource.BaseRevealColor = Selection.Color;
                        TargetLayer.ManualColorSource.SampledColorTexture = Selection.Texture.Get();
                        TargetLayer.ManualColorSource.SampledMaterialSlotIndex = Selection.MaterialSlotIndex;
                        TargetLayer.ManualColorSource.SampledUVChannelIndex = Selection.UVChannelIndex;
                        TargetLayer.ManualColorSource.SampledUVIslandID = Selection.UVIslandID;
                    },
                    false);
                Panel->AutoBakeResults.Remove(Panel->SelectedLayerGuid);
                Panel->RefreshViewportContext();
            })));

    FSlateApplication::Get().AddModalWindow(
        DialogWindow,
        FSlateApplication::Get().GetActiveTopLevelWindow());
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleManualBaseColorCommitted(FLinearColor NewColor)
{
    NewColor.A = 1.0f;
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || Layer->ManualColorSource.BaseRevealColor.Equals(NewColor))
    {
        return;
    }

    EditSelectedLayer(
        LOCTEXT("SetManualBaseRevealColor", "Set Transparency Base Reveal Color"),
        [NewColor](FWetClothingTransparencyLayerData& TargetLayer)
        {
            TargetLayer.ManualColorSource.SourceMode =
                EDWCTransparencyManualRevealSourceMode::AuthoredColor;
            TargetLayer.ManualColorSource.BaseRevealColor = NewColor;
            TargetLayer.ManualColorSource.SampledColorTexture.Reset();
            TargetLayer.ManualColorSource.SampledMaterialSlotIndex = INDEX_NONE;
            TargetLayer.ManualColorSource.SampledUVChannelIndex = INDEX_NONE;
            TargetLayer.ManualColorSource.SampledUVIslandID = INDEX_NONE;
        },
        false);

    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshViewportContext();
}

FReply SWetClothingTransparencyBakePanel::HandleRevealPaintColorClicked()
{
    FColorPickerArgs Args;
    Args.InitialColor = GetRevealPaintSettingsFromSession().RevealColor;
    Args.bUseAlpha = false;
    Args.bOnlyRefreshOnMouseUp = true;
    Args.ParentWidget = AsShared();
    Args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintColorCommitted);
    OpenColorPicker(Args);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleRevealPaintColorCommitted(FLinearColor NewColor)
{
    NewColor.A = 1.0f;
    FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
    if (Settings.RevealColor.Equals(NewColor)) return;
    Settings.RevealColor = NewColor;
    DispatchRevealPaintState(MoveTemp(Settings));
    Invalidate(EInvalidateWidgetReason::Paint);
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealColorPaintModeChecked(
    const EDWCTransparencyRevealColorBrushMode Mode) const
{
    return GetRevealPaintSettingsFromSession().RevealColorMode == Mode
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealColorPaintModeChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyRevealColorBrushMode Mode)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }

    FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
    if (Settings.RevealColorMode == Mode)
    {
        return;
    }
    Settings.RevealColorMode = Mode;
    DispatchRevealPaintState(MoveTemp(Settings));
    Invalidate(EInvalidateWidgetReason::Paint);
}

float SWetClothingTransparencyBakePanel::GetRevealPaintSizeCm() const
{
    return DWCTransparencyRadiusUVToSizeCm(GetRevealPaintSettingsFromSession().RadiusUV);
}

FText SWetClothingTransparencyBakePanel::GetRevealPaintSizeDisplayText() const
{
    return FormatDWCTransparencyBrushSizeCm(GetRevealPaintSizeCm());
}
TOptional<float> SWetClothingTransparencyBakePanel::GetRevealPaintStrength() const { return GetRevealPaintSettingsFromSession().Strength; }
TOptional<float> SWetClothingTransparencyBakePanel::GetRevealPaintFalloff() const { return GetRevealPaintSettingsFromSession().Falloff; }
void SWetClothingTransparencyBakePanel::HandleRevealPaintStrengthCommitted(float Value, ETextCommit::Type) { FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession(); Settings.Strength = Value; DispatchRevealPaintState(MoveTemp(Settings)); }
void SWetClothingTransparencyBakePanel::HandleRevealPaintFalloffCommitted(float Value, ETextCommit::Type) { FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession(); Settings.Falloff = Value; DispatchRevealPaintState(MoveTemp(Settings)); }

FReply SWetClothingTransparencyBakePanel::HandleClearRevealColorPaintClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return FReply::Handled();
    }
    const int32 SlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    TArray<FDWCTransparencyRevealColorStroke> InvalidatedStrokes;
    for (const FDWCTransparencyRevealColorStroke& Stroke : Layer->GetRevealColorPaintStrokes())
    {
        if (Stroke.MaterialSlotIndex == SlotIndex)
        {
            InvalidatedStrokes.Add(Stroke);
        }
    }
    if (EditRevealColorStrokeHistory(
        LOCTEXT("ClearRevealColorPaint", "Clear Reveal Color Paint"),
        FGuid(),
        [SlotIndex](FWetClothingTransparencyLayerData& MutableLayer)
        {
            return MutableLayer.GetMutableRevealColorPaintStrokes().RemoveAll(
                [SlotIndex](const FDWCTransparencyRevealColorStroke& Stroke)
                {
                    return Stroke.MaterialSlotIndex == SlotIndex;
                }) > 0;
        }) && PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayRevealColorStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

bool SWetClothingTransparencyBakePanel::EditRevealColorStrokeHistory(
    const FText& TransactionText,
    const FGuid StrokeGuid,
    TFunctionRef<bool(FWetClothingTransparencyLayerData&)> Edit)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (!AuthoringDocument.IsValid() || Layer == nullptr)
    {
        return false;
    }
    const FGuid LayerGuid = Layer->LayerGuid;
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList |
        EDWCEditorAuthoringImpact::PreviewIncremental |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = StrokeGuid;
    UWetClothingAsset* MutableAsset = WetClothingAsset.Get();
    UDWCTransparencyLayerStrokeHistory* StrokeHistory = MutableAsset != nullptr
        ? MutableAsset->EnsureTransparencyLayerStrokeHistory(LayerGuid)
        : nullptr;
    if (StrokeHistory == nullptr)
    {
        return false;
    }
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        TransactionText,
        Change,
        StrokeHistory,
        [LayerGuid, &Edit](UWetClothingAsset& MutableAsset)
        {
            FWetClothingTransparencyLayerData* MutableLayer =
                MutableAsset.Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            return MutableLayer != nullptr && Edit(*MutableLayer);
        });
    if (!Result.bChanged)
    {
        return false;
    }
    RefreshRevealColorStrokeList();
    return true;
}

FReply SWetClothingTransparencyBakePanel::HandleUndoLastRevealColorStrokeClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return FReply::Handled();
    }
    const int32 SlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    FGuid LastStrokeGuid;
    const TArray<FDWCTransparencyRevealColorStroke>& RevealStrokes = Layer->GetRevealColorPaintStrokes();
    for (int32 Index = RevealStrokes.Num() - 1; Index >= 0; --Index)
    {
        if (RevealStrokes[Index].MaterialSlotIndex == SlotIndex)
        {
            LastStrokeGuid = RevealStrokes[Index].StrokeGuid;
            break;
        }
    }
    if (LastStrokeGuid.IsValid())
    {
        HandleDeleteRevealColorStrokeClicked(LastStrokeGuid);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleDeleteRevealColorStrokeClicked(const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FDWCTransparencyRevealColorStroke* Stroke = Layer != nullptr
        ? Layer->GetRevealColorPaintStrokes().FindByPredicate(
            [StrokeGuid](const FDWCTransparencyRevealColorStroke& Candidate)
            {
                return Candidate.StrokeGuid == StrokeGuid;
            })
        : nullptr;
    if (Stroke == nullptr)
    {
        return FReply::Handled();
    }
    const TArray<FDWCTransparencyRevealColorStroke> InvalidatedStrokes = {*Stroke};
    if (EditRevealColorStrokeHistory(
        LOCTEXT("DeleteRevealColorStroke", "Delete Reveal Color Stroke"),
        StrokeGuid,
        [StrokeGuid](FWetClothingTransparencyLayerData& MutableLayer)
        {
            return MutableLayer.GetMutableRevealColorPaintStrokes().RemoveAll(
                [StrokeGuid](const FDWCTransparencyRevealColorStroke& Stroke)
                {
                    return Stroke.StrokeGuid == StrokeGuid;
                }) > 0;
        }) && PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayRevealColorStrokeHistory(InvalidatedStrokes);
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleRevealColorStrokeEnabledChanged(
    const ECheckBoxState NewState,
    const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FDWCTransparencyRevealColorStroke* ExistingStroke = Layer != nullptr
        ? Layer->GetRevealColorPaintStrokes().FindByPredicate(
            [StrokeGuid](const FDWCTransparencyRevealColorStroke& Candidate)
            {
                return Candidate.StrokeGuid == StrokeGuid;
            })
        : nullptr;
    if (ExistingStroke == nullptr)
    {
        return;
    }
    const TArray<FDWCTransparencyRevealColorStroke> InvalidatedStrokes = {*ExistingStroke};
    const bool bEnabled = NewState == ECheckBoxState::Checked;
    if (EditRevealColorStrokeHistory(
        LOCTEXT("ToggleRevealColorStroke", "Toggle Reveal Color Stroke"),
        StrokeGuid,
        [StrokeGuid, bEnabled](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FDWCTransparencyRevealColorStroke* Stroke =
                MutableLayer.GetMutableRevealColorPaintStrokes().FindByPredicate(
                    [StrokeGuid](const FDWCTransparencyRevealColorStroke& Candidate)
                    {
                        return Candidate.StrokeGuid == StrokeGuid;
                    });
            if (Stroke == nullptr || Stroke->bEnabled == bEnabled)
            {
                return false;
            }
            Stroke->bEnabled = bEnabled;
            return true;
        }) && PreviewViewport.IsValid())
    {
        PreviewViewport->ReplayRevealColorStrokeHistory(InvalidatedStrokes);
    }
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateRevealColorStrokeRow(
    TSharedPtr<FGuid> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FGuid StrokeGuid = Item.IsValid() ? *Item : FGuid();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FDWCTransparencyRevealColorStroke* Stroke = Layer != nullptr
        ? Layer->GetRevealColorPaintStrokes().FindByPredicate(
            [StrokeGuid](const FDWCTransparencyRevealColorStroke& Candidate)
            {
                return Candidate.StrokeGuid == StrokeGuid;
            })
        : nullptr;
    if (Stroke == nullptr)
    {
        return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
            [SNew(STextBlock).Text(LOCTEXT("MissingRevealColorStroke", "Missing stroke"))];
    }

    const FText Label = FText::Format(
        LOCTEXT("RevealColorStrokeRowLabel", "{0}  |  {1} samples"),
        FText::FromString(GetRevealColorStrokeModeLabel(Stroke->BrushMode)),
        FText::AsNumber(Stroke->Samples.Num()));
    return SNew(STableRow<TSharedPtr<FGuid>>, OwnerTable)
        [SNew(SBorder)
         .Padding(FMargin(4, 3))
         .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
         [SNew(SHorizontalBox)
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
            [SNew(SCheckBox)
             .IsChecked(Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
             .OnCheckStateChanged(
                 this,
                 &SWetClothingTransparencyBakePanel::HandleRevealColorStrokeEnabledChanged,
                 StrokeGuid)]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
            [SNew(SBox)
             .WidthOverride(18)
             .HeightOverride(18)
             .Visibility(Stroke->BrushMode == EDWCTransparencyRevealColorBrushMode::Paint
                 ? EVisibility::Visible
                 : EVisibility::Collapsed)
             [SNew(SColorBlock)
              .Color(Stroke->PaintColor)
              .ShowBackgroundForAlpha(false)]]
          + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(STextBlock).Text(Label)]
          + SHorizontalBox::Slot().AutoWidth()
            [SNew(SButton)
             .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .ToolTipText(LOCTEXT("DeleteRevealColorStrokeTooltip", "Delete this reveal-color stroke."))
             .OnClicked(
                 this,
                 &SWetClothingTransparencyBakePanel::HandleDeleteRevealColorStrokeClicked,
                 StrokeGuid)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]];
}

void SWetClothingTransparencyBakePanel::PushRevealColorPaintSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    // Reveal Paint is owned by the shared session. The panel mirrors it for
    // display only; the viewport always receives the authoritative snapshot.
    PreviewViewport->SetPaintSettings(GetRevealPaintSettingsFromSession());
}

TOptional<float> SWetClothingTransparencyBakePanel::GetRevealMetallicDarkeningStrength() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr
        ? TOptional<float>(Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength)
        : TOptional<float>();
}

void SWetClothingTransparencyBakePanel::HandleRevealMetallicDarkeningStrengthCommitted(
    const float Value,
    ETextCommit::Type)
{
    const float ClampedValue = FMath::Clamp(Value, 0.0f, 1.0f);
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (!AuthoringDocument.IsValid() || Asset == nullptr || FMath::IsNearlyEqual(
            Asset->Authored.TransparencyData.RevealMetallicDarkeningStrength,
            ClampedValue))
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        LOCTEXT("SetRevealMetallicDarkening", "Set Reveal Metallic Darkening"),
        Change,
        [ClampedValue](UWetClothingAsset& MutableAsset)
        {
            FWetClothingTransparencyData& Data = MutableAsset.Authored.TransparencyData;
            if (FMath::IsNearlyEqual(Data.RevealMetallicDarkeningStrength, ClampedValue))
            {
                return false;
            }
            Data.RevealMetallicDarkeningStrength = ClampedValue;
            for (FWetClothingTransparencyLayerData& Layer : Data.TransparencyLayers)
            {
#if WITH_EDITORONLY_DATA
                Layer.EditorStageCache.MarkRevealStale();
#endif
                Layer.MarkFinalBakeStale();
            }
            return true;
        });
    if (!Result.bChanged)
    {
        return;
    }

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshRevealColorCorrectionPreview();
    }
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Details);
}

TOptional<float> SWetClothingTransparencyBakePanel::GetManualInitialTransparencyAlpha() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    return Layer != nullptr
        ? TOptional<float>(Layer->ManualColorSource.InitialTransparencyAlpha)
        : TOptional<float>();
}

void SWetClothingTransparencyBakePanel::HandleManualInitialTransparencyAlphaCommitted(
    const float NewValue,
    ETextCommit::Type)
{
    const float ClampedValue = FMath::Clamp(NewValue, 0.0f, 1.0f);
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr || FMath::IsNearlyEqual(
            Layer->ManualColorSource.InitialTransparencyAlpha,
            ClampedValue))
    {
        return;
    }

    EditSelectedLayer(
        LOCTEXT("SetManualInitialTransparencyAlpha", "Set Initial Transparency Alpha"),
        [ClampedValue](FWetClothingTransparencyLayerData& TargetLayer)
        {
            TargetLayer.ManualColorSource.InitialTransparencyAlpha = ClampedValue;
        },
        false);

    AutoBakeResults.Remove(SelectedLayerGuid);
    RefreshViewportContext();
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRaySettingsSection()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RaySettings", "Ray Settings"))];
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBakeSettingsSection(const bool bShowResolution)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BakeSettings", "Bake Settings"))];
    if (Asset == nullptr) return Box;
    if (bShowResolution)
    {
        const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
        const FDWCTransparencyResolvedOutputResolution AutomaticResolution =
            SelectedLayer != nullptr
            ? FDWCTransparencyResolutionResolver::ResolveAutomatic(*Asset, *SelectedLayer)
            : FDWCTransparencyResolvedOutputResolution();
        const auto ModeButton = [this](
            const EDWCTransparencyOutputResolutionMode Mode,
            const FText& Label)
        {
            return SNew(SCheckBox)
                .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
                .Type(ESlateCheckBoxType::ToggleButton)
                .IsChecked_Lambda([this, Mode]()
                {
                    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                    return Layer != nullptr && Layer->OutputResolutionMode == Mode
                        ? ECheckBoxState::Checked
                        : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, Mode](const ECheckBoxState State)
                {
                    if (State != ECheckBoxState::Checked)
                    {
                        return;
                    }
                    const FWetClothingTransparencyLayerData* CurrentLayer = GetSelectedLayer();
                    if (CurrentLayer == nullptr || CurrentLayer->OutputResolutionMode == Mode)
                    {
                        return;
                    }
                    EditSelectedLayer(
                        LOCTEXT("SetTransparencyResolutionMode", "Set Transparency Resolution Mode"),
                        [Mode](FWetClothingTransparencyLayerData& Layer)
                        {
                            Layer.OutputResolutionMode = Mode;
                        },
                        false);
                })
                [SNew(STextBlock).Text(Label)];
        };

        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [BuildLabeledControl(
            LOCTEXT("BakeResolutionMode", "Resolution Mode"),
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [ModeButton(
                EDWCTransparencyOutputResolutionMode::Auto,
                LOCTEXT("TransparencyResolutionAuto", "Auto"))]
            + SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f, 0.0f, 0.0f)
            [ModeButton(
                EDWCTransparencyOutputResolutionMode::Override,
                LOCTEXT("TransparencyResolutionOverride", "Override"))])];

        TSharedRef<SVerticalBox> ResolutionMenu = SNew(SVerticalBox);
        constexpr int32 ResolutionOptions[] = {256, 512, 1024, 2048, 4096};
        for (const int32 ResolutionOption : ResolutionOptions)
        {
            ResolutionMenu->AddSlot().AutoHeight()
            [SNew(SButton)
                .ButtonStyle(FAppStyle::Get(), TEXT("Menu.Button"))
                .ContentPadding(FMargin(12.0f, 4.0f))
                .OnClicked_Lambda([this, ResolutionOption]()
                {
                    const FWetClothingTransparencyLayerData* CurrentLayer = GetSelectedLayer();
                    if (CurrentLayer != nullptr &&
                        CurrentLayer->OutputResolutionMode ==
                            EDWCTransparencyOutputResolutionMode::Override &&
                        FDWCTransparencyResolutionResolver::NormalizeResolution(
                            CurrentLayer->OutputResolutionOverride) == ResolutionOption)
                    {
                        return FReply::Handled();
                    }
                    EditSelectedLayer(
                        LOCTEXT("SetTransparencyResolutionOverride", "Set Transparency Resolution Override"),
                        [ResolutionOption](FWetClothingTransparencyLayerData& Layer)
                        {
                            Layer.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Override;
                            Layer.OutputResolutionOverride = ResolutionOption;
                        },
                        false);
                    return FReply::Handled();
                })
                [SNew(STextBlock).Text(FText::AsNumber(ResolutionOption))]];
        }

        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [BuildLabeledControl(
            LOCTEXT("BakeResolution", "Output Resolution"),
            SNew(SComboButton)
            .IsEnabled_Lambda([this]()
            {
                const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                return Layer != nullptr &&
                    Layer->OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Override;
            })
            .ButtonContent()
            [SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                    return FText::AsNumber(Layer != nullptr
                        ? FDWCTransparencyResolutionResolver::NormalizeResolution(
                            Layer->OutputResolutionOverride)
                        : FDWCTransparencyResolutionResolver::DefaultResolution);
                })]
            .MenuContent()
            [ResolutionMenu])];

        Box->AddSlot().AutoHeight().Padding(0,0,0,8)
        [BuildLabeledControl(
            LOCTEXT("ResolvedBakeResolution", "Resolved Resolution"),
            SNew(STextBlock)
            .Text_Lambda([this, AutomaticResolution]()
            {
                const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                const int32 Resolution = Layer != nullptr &&
                    Layer->OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Override
                    ? FDWCTransparencyResolutionResolver::NormalizeResolution(
                        Layer->OutputResolutionOverride)
                    : AutomaticResolution.Size;
                return FText::FromString(FString::Printf(
                    TEXT("%d x %d"),
                    Resolution,
                    Resolution));
            })
            .ToolTipText_Lambda([this, AutomaticResolution]()
            {
                const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                if (Layer != nullptr &&
                    Layer->OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Override)
                {
                    return FText::FromString(FString::Printf(
                        TEXT("Override %d"),
                        FDWCTransparencyResolutionResolver::NormalizeResolution(
                            Layer->OutputResolutionOverride)));
                }
                return FText::FromString(AutomaticResolution.SourceDescription);
            }))];
    }
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("PaddingPixels", "Padding Pixels"), SNew(SNumericEntryBox<int32>).MinValue(0).MaxValue(64).Value(Asset->Authored.TransparencyData.TransparencyPaddingPixels).OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type){ EditFinalBakeSettings(LOCTEXT("SetTransparencyPadding", "Set Transparency Padding"), [V](auto& D){ const int32 NewValue = FMath::Clamp(V,0,64); if (D.TransparencyPaddingPixels == NewValue) return false; D.TransparencyPaddingPixels = NewValue; return true; }, EDWCTransparencyFinalPreviewRefresh::None); }))];
    Box->AddSlot().AutoHeight()[BuildLabeledControl(LOCTEXT("EdgeFeather", "Edge Feather Pixels"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(32.0f).Value(Asset->Authored.TransparencyData.TransparencyEdgeFeatherPixels).OnValueCommitted_Lambda([this](float V, ETextCommit::Type){ EditFinalBakeSettings(LOCTEXT("SetTransparencyFeather", "Set Transparency Edge Feather"), [V](auto& D){ const float NewValue = FMath::Clamp(V,0.0f,32.0f); if (FMath::IsNearlyEqual(D.TransparencyEdgeFeatherPixels, NewValue)) return false; D.TransparencyEdgeFeatherPixels = NewValue; return true; }, EDWCTransparencyFinalPreviewRefresh::OuterEdgeFeather); }))];
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

    Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [BuildBrushSizeControl(LOCTEXT("BrushSize", "Brush Size"), EDWCTransparencyBrushSizeTarget::TransparencyBrush)];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushStrength", "Strength"),
        SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushStrengthCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushFalloff", "Falloff"),
        SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushFalloff).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushFalloffCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushSpacing", "Spacing"),
        SNew(SNumericEntryBox<float>).MinValue(0.01f).MaxValue(2.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushSpacing).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushSpacingCommitted))];
    Box->AddSlot().AutoHeight().Padding(0,0,0,6)
    [
        SNew(SBox)
        .Visibility_Lambda([this]()
        {
            return BrushMode == EDWCTransparencyBrushMode::SetValue
                ? EVisibility::Visible
                : EVisibility::Collapsed;
        })
        [
            BuildLabeledControl(
                LOCTEXT("BrushTargetAlpha", "Target Alpha"),
                SNew(SNumericEntryBox<float>)
                .MinValue(0.0f)
                .MaxValue(1.0f)
                .Value(this, &SWetClothingTransparencyBakePanel::GetBrushTargetAlpha)
                .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushTargetAlphaCommitted))
        ]
    ];

    Box->AddSlot().AutoHeight().Padding(0,2,0,6)
    [SAssignNew(TransparencyStrokeListContainer, SBox)
        [BuildTransparencyStrokeList()]];

    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBrushSizeControl(
    const FText& Label,
    const EDWCTransparencyBrushSizeTarget Target)
{
    TSharedPtr<SComboButton> NewComboButton;
    TSharedRef<SWidget> Control = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
            .AutoWidth()
            [SNew(SBox)
                .WidthOverride(72.0f)
                [SNew(SSpinBox<float>)
                    .MinValue(0.1f)
                    .MaxValue(100.0f)
                    .Value_Lambda([this, Target]()
                    {
                        return Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint
                            ? GetRevealPaintSizeCm()
                            : GetBrushSizeCm();
                    })
                    .OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleBrushSizeChanged, Target)
                    .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushSizeCommitted, Target)]]
        + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(4.0f, 0.0f, 0.0f, 0.0f)
            [SAssignNew(NewComboButton, SComboButton)
                .HasDownArrow(true)
                .ContentPadding(FMargin(8.0f, 2.0f))
                .ButtonContent()
                [SNew(STextBlock)
                    .Text_Lambda([this, Target]()
                    {
                        return Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint
                            ? GetRevealPaintSizeDisplayText()
                            : GetBrushSizeDisplayText();
                    })]
                .MenuContent()
                [BuildBrushSizeMenu(Target)]];

    if (Target == EDWCTransparencyBrushSizeTarget::RevealColorPaint)
    {
        RevealPaintSizeComboButton = NewComboButton;
    }
    else
    {
        TransparencyBrushSizeComboButton = NewComboButton;
    }

    return BuildLabeledControl(Label, Control);
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBrushSizeMenu(
    const EDWCTransparencyBrushSizeTarget Target)
{
    static constexpr float BrushSizePresetsCm[] = {
        0.7f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f,
        6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 15.0f, 17.0f, 20.0f,
        25.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 100.0f
    };

    constexpr int32 ColumnCount = 8;
    TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.0f, 2.0f));

    for (int32 PresetIndex = 0; PresetIndex < UE_ARRAY_COUNT(BrushSizePresetsCm); ++PresetIndex)
    {
        const float PresetSizeCm = BrushSizePresetsCm[PresetIndex];
        const int32 Column = PresetIndex % ColumnCount;
        const int32 Row = PresetIndex / ColumnCount;
        const int32 DotFontSize = FMath::RoundToInt(8.0f + FMath::Sqrt(PresetSizeCm / 100.0f) * 18.0f);

        Grid->AddSlot(Column, Row)
            [SNew(SButton)
                .ContentPadding(FMargin(3.0f, 2.0f))
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleBrushSizePresetClicked, PresetSizeCm, Target)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                        .AutoHeight()
                        .HAlign(HAlign_Center)
                        [SNew(SBox)
                            .HeightOverride(24.0f)
                            .VAlign(VAlign_Center)
                            [SNew(STextBlock)
                                .Text(FText::FromString(TEXT("\u25CF")))
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), DotFontSize))
                                .ColorAndOpacity(FSlateColor(FLinearColor::White))]]
                    + SVerticalBox::Slot()
                        .AutoHeight()
                        .HAlign(HAlign_Center)
                        [SNew(STextBlock)
                            .Text(FormatDWCTransparencyBrushSizeCm(PresetSizeCm))]]];
    }

    return SNew(SBorder)
        .Padding(4.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
        [Grid];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyStrokeList()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    const int32 NewStrokeCount = Layer != nullptr
        ? FMath::Max(Layer->GetEditableStrokes().Num() - BaselineStrokeCount, 0)
        : 0;
    Box->AddSlot().AutoHeight().Padding(0,2,0,6)
    [SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)
        [SNew(SButton).Text(LOCTEXT("UndoLastStroke", "Undo Last Stroke")).IsEnabled(NewStrokeCount > 0).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked)]
        + SHorizontalBox::Slot().FillWidth(1)
        [SNew(SButton).Text(LOCTEXT("ClearStrokes", "Clear")).IsEnabled(NewStrokeCount > 0).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleClearStrokesClicked)]];

    if (Layer == nullptr || NewStrokeCount == 0)
    {
        const bool bUsingBakedBaseline = BaselineStrokeCount > 0 ||
            (AutoBakeResults.Contains(SelectedLayerGuid) &&
             AutoBakeResults[SelectedLayerGuid].IsValid() &&
             AutoBakeResults[SelectedLayerGuid]->bIsFinalBakedBaseline);
        Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(
            bUsingBakedBaseline
                ? LOCTEXT("NoNewTransparencyStrokes", "No new edits. Reset restores the loaded baked baseline.")
                : LOCTEXT("NoTransparencyStrokes", "No manual transparency strokes."))];
    }
    else
    {
        TMap<EDWCTransparencyBrushMode, int32> StrokeNumberByMode;
        const TArray<FDWCTransparencyBrushStroke>& EditableStrokes = Layer->GetEditableStrokes();
        for (int32 StrokeIndex = BaselineStrokeCount; StrokeIndex < EditableStrokes.Num(); ++StrokeIndex)
        {
            const FDWCTransparencyBrushStroke& Stroke = EditableStrokes[StrokeIndex];
            const int32 StrokeNumber = ++StrokeNumberByMode.FindOrAdd(Stroke.BrushMode);
            const FText StrokeLabel = FText::FromString(FString::Printf(
                TEXT("%s %d"),
                GetTransparencyStrokeModeLabel(Stroke.BrushMode),
                StrokeNumber));
            Box->AddSlot().AutoHeight().Padding(0,0,0,3)
            [SNew(SBorder).Padding(4).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
                [SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                    [SNew(SCheckBox).IsChecked(Stroke.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleStrokeEnabledChanged, Stroke.StrokeGuid)]
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(STextBlock).Text(StrokeLabel)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("DeleteStrokeTooltip", "Delete this stroke.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleDeleteStrokeClicked, Stroke.StrokeGuid)
                        [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]];
        }
    }
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildGeneratedOutputsSection()
{
    return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("GeneratedOutputs", "Generated Output"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SBorder).Padding(FMargin(8,6)).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [SNew(STextBlock).AutoWrapText(true).Text(this, &SWetClothingTransparencyBakePanel::GetStatusText).ColorAndOpacity(this, &SWetClothingTransparencyBakePanel::GetStatusColor)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[BuildRevealNormalStatusSection()]
        + SVerticalBox::Slot().AutoHeight()[BuildPackedTransparencyMapSection()];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealNormalStatusSection()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    FText StatusText = LOCTEXT("RevealNormalNoTarget", "Reveal Normal: Select a Transparency Target Part.");
    FLinearColor StatusColor(0.70f, 0.70f, 0.70f);

    if (Layer != nullptr)
    {
        const FWetClothingBakedTransparencyMap* BakedMap = FindExactBakedTransparencyMap(Asset, Layer);
        if (!Layer->RequiresRevealSurface())
        {
            StatusText = BakedMap != nullptr && BakedMap->HasRuntimeRevealNormalPayload()
                ? LOCTEXT("RevealNormalOptionalAvailable", "Reveal Normal: Available (optional for Manual Color).")
                : LOCTEXT("RevealNormalNotRequired", "Reveal Normal: Not required for Manual Color.");
            StatusColor = FLinearColor(0.34f, 0.82f, 0.42f);
        }
        else if (!Layer->bEnableRevealNormal)
        {
            StatusText = LOCTEXT(
                "RevealNormalRuntimeDisabled",
                "Reveal Normal: Runtime Disabled (the baked artifact is preserved)." );
            StatusColor = FLinearColor(0.70f, 0.70f, 0.70f);
        }
        else if (BakedMap == nullptr)
        {
            StatusText = LOCTEXT(
                "RevealNormalNotBaked",
                "Reveal Normal: Not Baked. Bake the Transparency Map to create it.");
            StatusColor = FLinearColor(1.00f, 0.72f, 0.24f);
        }
        else if (!BakedMap->HasRuntimeRevealNormalPayload())
        {
            StatusText = LOCTEXT(
                "RevealSurfaceRequiredMissing",
                "Reveal Normal: Incomplete. The baked output is missing its coverage-weighted runtime normal.");
            StatusColor = FLinearColor(0.94f, 0.30f, 0.30f);
        }
        else
        {
            FString CurrentnessReason;
            const bool bCurrent = Asset != nullptr &&
                FDWCTransparencyEditedMapBaker::IsLayerBakeCurrent(*Asset, *Layer, &CurrentnessReason);
            StatusText = bCurrent
                ? LOCTEXT("RevealNormalReady", "Reveal Normal: Ready")
                : FText::FromString(FString::Printf(
                    TEXT("Reveal Normal: Out of Date. %s"),
                    *CurrentnessReason));
            StatusColor = bCurrent
                ? FLinearColor(0.34f, 0.82f, 0.42f)
                : FLinearColor(1.00f, 0.72f, 0.24f);
        }
    }

    return SNew(SBorder)
        .Padding(FMargin(8.0f, 6.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
        [SNew(STextBlock)
            .AutoWrapText(true)
            .Text(StatusText)
            .ColorAndOpacity(StatusColor)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPackedTransparencyMapSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
    if (Layer == nullptr || Layer->BakedMaps.Num() == 0) { Box->AddSlot().AutoHeight()[BuildEmptyAssetRow(LOCTEXT("NoPackedMap", "No packed Transparency Map."))]; return Box; }
    for (const auto& Map : Layer->BakedMaps)
    {
        const FText BakeState = Map.IsRuntimeUsableForLayer(Layer->RequiresRuntimeRevealNormal())
            ? LOCTEXT("PackedMapReady", "Ready")
            : LOCTEXT("PackedMapStale", "Missing or Stale");
        const FText RevealState = !Layer->RequiresRevealSurface()
            ? LOCTEXT("PackedRevealNotRequired", "Reveal Normal: Not Required")
            : !Layer->bEnableRevealNormal
                ? LOCTEXT("PackedRevealDisabled", "Reveal Normal: Runtime Disabled")
            : Map.HasRuntimeRevealNormalPayload()
                ? LOCTEXT("PackedRevealAvailable", "Reveal Normal: Available")
                : LOCTEXT("PackedRevealMissing", "Reveal Normal: Missing");
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
            [BuildAssetSummaryRow(
                Map.TransparencyMap,
                FText::FromString(GetNameSafe(Map.TransparencyMap)),
                FText::Format(
                    LOCTEXT("PackedMapDetail", "Slot {0} / UV {1} / LOD {2} / {3} / {4}\n{5}"),
                    FText::AsNumber(Map.MaterialSlotIndex),
                    FText::AsNumber(GetTransparencyDataUVChannel()),
                    FText::AsNumber(0),
                    FText::AsNumber(Map.Resolution),
                    BakeState,
                    RevealState),
                GeneratedOutputThumbnails)];
    }
    return Box;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildVisualizationModeControl(
    const EDWCTransparencyEditorStage Stage)
{
    const TArray<TSharedPtr<EDWCTransparencyVisualizationMode>>* ModeItems =
        Stage == EDWCTransparencyEditorStage::RevealEditing
        ? &RevealVisualizationModeItems
        : &FinalVisualizationModeItems;
    const EDWCTransparencyVisualizationMode SelectedMode = GetVisualizationModeForStage(Stage);
    return BuildLabeledControl(
        LOCTEXT("TransparencyVisualizationLabel", "View Mode"),
        SNew(SComboBox<TSharedPtr<EDWCTransparencyVisualizationMode>>)
            .OptionsSource(ModeItems)
            .InitiallySelectedItem(FindVisualizationModeItem(SelectedMode, Stage))
            .OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem)
            .OnSelectionChanged(
                this,
                &SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged,
                Stage)
            [SNew(STextBlock).Text_Lambda([this, Stage]()
            {
                return GetVisualizationModeLabel(GetVisualizationModeForStage(Stage));
            })]);
}

const FSlateBrush* SWetClothingTransparencyBakePanel::GetRevealVisualizationBrush() const
{
    UTexture2D* Texture = PreviewViewport.IsValid()
        ? PreviewViewport->GetVisualizationPreviewTexture()
        : nullptr;
    RevealVisualizationBrush.SetResourceObject(Texture);
    RevealVisualizationBrush.SetImageSize(Texture != nullptr
        ? FVector2D(FMath::Max(Texture->GetSizeX(), 1), FMath::Max(Texture->GetSizeY(), 1))
        : FVector2D(256.0f, 256.0f));
    return &RevealVisualizationBrush;
}

EVisibility SWetClothingTransparencyBakePanel::GetRevealVisualizationVisibility() const
{
    return GetCurrentStage() == EDWCTransparencyEditorStage::RevealEditing &&
        PreviewViewport.IsValid() && PreviewViewport->GetVisualizationPreviewTexture() != nullptr
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealVisualizationSection()
{
    return SNew(SBorder)
        .Padding(8.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [SNew(SVerticalBox)
         + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
           [FWCAEditorWidgets::BuildSectionHeader(
               LOCTEXT("RevealVisualization", "Reveal Color Preview"))]
         + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
           [BuildVisualizationModeControl(EDWCTransparencyEditorStage::RevealEditing)]
         + SVerticalBox::Slot().AutoHeight()
           [SNew(SBox)
            .HeightOverride(170.0f)
            .Visibility(this, &SWetClothingTransparencyBakePanel::GetRevealVisualizationVisibility)
            [SNew(SBorder)
             .Padding(1.0f)
             .BorderImage(FAppStyle::GetBrush(TEXT("WhiteBrush")))
             .BorderBackgroundColor(FLinearColor::Black)
             [SNew(SScaleBox)
              .Stretch(EStretch::ScaleToFit)
              .StretchDirection(EStretchDirection::Both)
              [SNew(SImage).Image(this, &SWetClothingTransparencyBakePanel::GetRevealVisualizationBrush)]]]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildRevealNormalPreviewSettingsSection()
{
    return SNew(SBorder)
        .Visibility_Lambda([this]()
        {
            const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
            return Layer != nullptr && Layer->RequiresRevealSurface()
                ? EVisibility::Visible
                : EVisibility::Collapsed;
        })
        .Padding(8.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RevealNormalPreviewSettings", "Reveal Normal"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [SNew(SCheckBox)
              .IsChecked(this, &SWetClothingTransparencyBakePanel::GetRevealNormalEnabledState)
              .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealNormalEnabledChanged)
              [SNew(STextBlock).Text(LOCTEXT("EnableRevealNormal", "Enable Reveal Normal"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [BuildLabeledControl(
              LOCTEXT("RevealNormalStrengthLabel", "Strength"),
              SNew(SNumericEntryBox<float>)
                  .IsEnabled_Lambda([this]() { return GetRevealNormalEnabledState() == ECheckBoxState::Checked; })
                  .MinValue(0.0f).MaxValue(4.0f)
                  .Value(this, &SWetClothingTransparencyBakePanel::GetRevealNormalStrength)
                  .OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthChanged)
                  .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleRevealNormalStrengthCommitted))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [SNew(SCheckBox)
              .IsEnabled_Lambda([this]() { return GetRevealNormalEnabledState() == ECheckBoxState::Checked; })
              .IsChecked(this, &SWetClothingTransparencyBakePanel::GetShowRevealNormalState)
              .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleShowRevealNormalChanged)
              [SNew(STextBlock).Text(LOCTEXT("ShowRevealNormal", "Show Reveal Normal"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
          [BuildLabeledControl(
              LOCTEXT("RevealNormalPreviewSourceLabel", "Preview Source"),
              SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
                [SNew(SCheckBox)
                    .Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
                    .IsChecked(this, &SWetClothingTransparencyBakePanel::GetRevealNormalSourceState,
                        EDWCTransparencyRevealNormalPreviewSource::Working)
                    .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealNormalSourceChanged,
                        EDWCTransparencyRevealNormalPreviewSource::Working)
                    [SNew(STextBlock).Text(LOCTEXT("RevealNormalSourceWorking", "Working"))]]
              + SHorizontalBox::Slot().AutoWidth()
                [SNew(SCheckBox)
                    .Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
                    .IsChecked(this, &SWetClothingTransparencyBakePanel::GetRevealNormalSourceState,
                        EDWCTransparencyRevealNormalPreviewSource::Baked)
                    .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealNormalSourceChanged,
                        EDWCTransparencyRevealNormalPreviewSource::Baked)
                    [SNew(STextBlock).Text(LOCTEXT("RevealNormalSourceBaked", "Baked"))]])]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(STextBlock)
              .Text(this, &SWetClothingTransparencyBakePanel::GetRevealNormalPreviewSourceStatusText)
              .ColorAndOpacity(FSlateColor::UseSubduedForeground())]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewSettingsSection()
{
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingResult = AutoBakeResults.Find(SelectedLayerGuid);
    const bool bCanRecomputeFinalSettings = WorkingResult == nullptr ||
        !WorkingResult->IsValid() ||
        !(*WorkingResult)->bIsFinalBakedBaseline;
    return SNew(SBorder).Padding(10).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))[SNew(SVerticalBox)
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BakeSettings", "Bake Settings"))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,3)[SNew(STextBlock).Text(LOCTEXT("PreviewWetnessLabel", "Preview Wetness"))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SSlider).MinValue(0).MaxValue(100).Value(this, &SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent).OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged)]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("TransparencyPreviewStrengthLabel", "Transparency Strength"),
          SNew(SNumericEntryBox<float>).IsEnabled(bCanRecomputeFinalSettings).MinValue(0.0f).MaxValue(8.0f).Value(this, &SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength).OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthChanged).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
        [BuildRevealNormalPreviewSettingsSection()]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
        [SNew(SCheckBox)
            .IsChecked(this, &SWetClothingTransparencyBakePanel::GetShowSavedWrinkleState)
            .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleShowSavedWrinkleChanged)
            .ToolTipText(LOCTEXT("ShowSavedWrinkleTooltip", "Show the saved runtime wrinkle normal and use its coverage to suppress transparency in this preview. Live Wrinkle Editor hover and stroke data are not included."))
            [SNew(STextBlock).Text(LOCTEXT("ShowSavedWrinkle", "Show Saved Wrinkle"))]]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionStrengthLabel", "Wrinkle Suppression Strength"),
          SNew(SNumericEntryBox<float>).IsEnabled(bCanRecomputeFinalSettings).MinValue(0.0f).MaxValue(5.0f).Value(this, &SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength).OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthChanged).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionThresholdLabel", "Wrinkle Mask Threshold"),
          SNew(SNumericEntryBox<float>)
              .IsEnabled(bCanRecomputeFinalSettings)
              .MinValue(0.0f).MaxValue(1.0f)
              .Value(this, &SWetClothingTransparencyBakePanel::GetWrinkleMaskThreshold)
              .OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdChanged)
              .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleWrinkleMaskThresholdCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionSoftnessLabel", "Wrinkle Mask Softness"),
          SNew(SNumericEntryBox<float>)
              .IsEnabled(bCanRecomputeFinalSettings)
              .MinValue(0.0f).MaxValue(1.0f)
              .Value(this, &SWetClothingTransparencyBakePanel::GetWrinkleMaskSoftness)
              .OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessChanged)
              .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleWrinkleMaskSoftnessCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
        [BuildVisualizationModeControl(EDWCTransparencyEditorStage::FinalEditing)]
      + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)[BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::TargetMeshOnly, LOCTEXT("TargetMeshPreview", "Target Mesh"))]
        + SHorizontalBox::Slot().AutoWidth()[BuildPreviewModeButton(
            EWetClothingTransparencyPreviewMode::FullBlueprint,
            GetSelectedLayer() != nullptr &&
                    GetSelectedLayer()->SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh
                ? LOCTEXT("ExternalSourceLayoutPreview", "Source Layout")
                : LOCTEXT("AllWettableSlotsPreview", "All Wettable Slots"))]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildTransparencyPreviewSection()
{
    TSharedRef<SWidget> Content = SNew(SBorder).Padding(12)[SNew(SVerticalBox)
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(LOCTEXT("Preview", "Preview")).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))] + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("FocusMesh", "Focus Mesh")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleFocusPreviewClicked)]]
      + SVerticalBox::Slot().FillHeight(1)
        [SAssignNew(PreviewViewport, SWetClothingTransparencyPreviewViewport)
            .WetClothingAsset(WetClothingAsset.Get())
            .WorkerJobScheduler(WorkerJobScheduler)
            .SessionStore(SessionStore)
            .WrinkleSuppressionCoverageService(WrinkleSuppressionCoverageService)
            .SpatialQueryService(SpatialQueryService)
            .TextureWorkspace(TextureWorkspace)
            .PreviewCommitCoordinator(PreviewCommitCoordinator)
            .RenderUploadQueue(RenderUploadQueue)]];
    if (AuthoringController.IsValid())
    {
        AuthoringController->AttachViewport(PreviewViewport);
        PreviewViewport->SetAuthoringController(AuthoringController);
    }
    PreviewViewport->SetExternalSourceTransformCommittedDelegate(
        FDWCTransparencyExternalSourceTransformCommitted::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandleExternalSourceTransformCommitted));
    PreviewViewport->SetExternalSourcePlacementSelection(SelectedExternalSourceGuid);
    return Content;
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label)
{
    return SNew(SCheckBox).Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton")).Type(ESlateCheckBoxType::ToggleButton)
        .IsEnabled_Lambda([this, Mode](){ return Mode != EWetClothingTransparencyPreviewMode::FullBlueprint || CanUseFullBlueprintPreview(); })
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsPreviewModeChecked, Mode).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandlePreviewModeChanged, Mode)[SNew(STextBlock).Text(Label)];
}
TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildAssetSummaryRow(
    UObject* Asset,
    const FText& Label,
    const FText& Detail,
    TArray<TSharedPtr<FAssetThumbnail>>& ThumbnailStorage)
{
    TSharedRef<SWidget> Thumbnail = SNew(SBox).WidthOverride(44).HeightOverride(44)[SNullWidget::NullWidget];
    if (Asset != nullptr && ThumbnailPool.IsValid()) { TSharedPtr<FAssetThumbnail> T = MakeShared<FAssetThumbnail>(Asset,44,44,ThumbnailPool); ThumbnailStorage.Add(T); Thumbnail = T->MakeThumbnailWidget(FAssetThumbnailConfig()); }
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
    if (bPreviewSuspended || !PreviewViewport.IsValid()) return;
    // The panel owns the controller while the viewport can be rebuilt by the
    // preview session. Rebind on every context push so slot/stage transitions
    // cannot leave the interactive tool talking to an expired viewport.
    if (AuthoringController.IsValid())
    {
        AuthoringController->AttachViewport(PreviewViewport);
        PreviewViewport->SetAuthoringController(AuthoringController);
    }
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const EDWCTransparencyEditorStage CurrentStage = GetCurrentStage();
    if (CurrentStage == EDWCTransparencyEditorStage::RevealEditing ||
        CurrentStage == EDWCTransparencyEditorStage::FinalEditing)
    {
        // The session owns independent selections for Stage 3 and Stage 4.
        // Synchronize the compatibility value before resolving the viewport
        // context so slot and stage refreshes cannot reuse the other stage's
        // visualization mode.
        SelectedVisualizationMode = GetVisualizationModeForStage(CurrentStage);
    }
    const EDWCTransparencySourceType SourceType = Layer != nullptr
        ? Layer->SourceType
        : EDWCTransparencySourceType::SameMeshMaterialSlots;
    const EWetClothingTransparencyPreviewMode RequestedPreviewMode =
        PreviewViewport->GetPreviewMode();
    DWCTransparencyWorkflow::FDWCTransparencyPreviewContext PreviewContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            CurrentStage,
            SourceType,
            SelectedVisualizationMode,
            RequestedPreviewMode,
            CanUseFullBlueprintPreview(),
            false,
            false);

    // Working maps are session resources. The policy decides which one is
    // relevant; only that resource is prepared here.
    if (PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        EnsureRevealEditingWorkingMap();
    }
    else if (PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha)
    {
        EnsureFinalEditingWorkingMap();
    }

    const bool bHasWorkingMap = Layer != nullptr &&
        AutoBakeResults.Contains(Layer->LayerGuid) &&
        AutoBakeResults.FindChecked(Layer->LayerGuid).IsValid();
    PreviewContext = DWCTransparencyWorkflow::ResolvePreviewContext(
        CurrentStage,
        SourceType,
        SelectedVisualizationMode,
        RequestedPreviewMode,
        CanUseFullBlueprintPreview(),
        PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor && bHasWorkingMap,
        PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha && bHasWorkingMap);

    // Stage 3 and Stage 4 input are both derived from the same working-map
    // readiness rule. Keep the authoring controller's session context in
    // sync with the viewport before the brush can receive a surface hit.
    DispatchTransparencyEditContext();

    PreviewViewport->SetPreviewMode(PreviewContext.PreviewMode);
    PreviewViewport->SetTransparencyEditContext(SelectedLayerGuid,
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : SelectedMaterialSlotIndex,
        GetTransparencyDataUVChannel(),
        Layer != nullptr ? Layer->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp,
        PreviewContext.PaintTarget,
        PreviewContext.bEnableRevealColorPainting || PreviewContext.bEnableFinalAlphaPainting);

    const TSharedPtr<FDWCTransparencySourcePayload>* Result = Layer != nullptr
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

    PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
    PreviewViewport->ApplyTransparencyPreviewSettings(GetTransparencyPreviewSettings());
    PreviewViewport->SetShowSavedWrinkle(bShowSavedWrinkle);
    PreviewViewport->SetVisualizationMode(PreviewContext.VisualizationMode);
    if (PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        PushRevealColorPaintSettingsToViewport();
    }
    else
    {
        PushPaintSettingsToViewport();
    }
}

#undef LOCTEXT_NAMESPACE
