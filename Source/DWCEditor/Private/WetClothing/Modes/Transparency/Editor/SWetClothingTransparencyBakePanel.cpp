#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/DWCEditorPreviewSlotUtils.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
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
#include "Widgets/Layout/SSplitter.h"
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
DECLARE_DELEGATE_OneParam(FOnDWCTransparencyUVIslandColorAccepted, FLinearColor);

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

const FWetClothingBakedTransparencyMap* FindExactBakedTransparencyMap(
    const UWetClothingAsset* Asset,
    const FWetClothingTransparencyLayerData* Layer)
{
    if (Asset == nullptr || Layer == nullptr)
    {
        return nullptr;
    }
    const int32 LODIndex = Asset->GetSimulationLODIndex();
    return Layer->BakedMaps.FindByPredicate(
        [Layer, LODIndex](const FWetClothingBakedTransparencyMap& Candidate)
        {
            return Candidate.MaterialSlotIndex == Layer->TargetSurface.OuterMaterialSlotIndex &&
                   Candidate.UVChannelIndex == Layer->TargetSurface.OuterUVChannel &&
                   Candidate.LODIndex == LODIndex &&
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
 * Transient helper used only while choosing a manual reveal color.  The mesh,
 * texture, UV channel and island are intentionally not persisted in WCA data.
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
            OnColorAccepted.Execute(CandidateColor);
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
}

void SWetClothingTransparencyBakePanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        TransparencyPreviewStrength = Asset->Authored.TransparencyData.TransparencyPreviewStrength;
        WrinkleSuppressionStrength = FMath::Clamp(Asset->Authored.TransparencyData.WrinkleSuppressionStrength, 0.0f, 5.0f);
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
    RefreshModelState();
    RebuildEditorLayout();
    RefreshViewportContext();
}

void SWetClothingTransparencyBakePanel::RebuildEditorLayout()
{
    const float PreviousScrollOffset = ControlPanelScrollBox.IsValid() ? ControlPanelScrollBox->GetScrollOffset() : 0.0f;
    if (ControlPanelContainer.IsValid())
    {
        RefreshStageContent();
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
    uint32 DWCReadySignature = 0;
    if (Asset != nullptr && Mesh != nullptr)
    {
        for (int32 SlotIndex = 0; SlotIndex < MaterialSlotCount; ++SlotIndex)
        {
            // Refresh target choices when Generate/Refresh DWC Materials replaces a CPU preview instance.
            DWCReadySignature = HashCombine(
                DWCReadySignature,
                GetTypeHash(DWCEditorPreviewSlotUtils::ResolveCpuPreviewMaterial(Asset, SlotIndex)));
        }
    }
    const bool bMeshOptionsChanged = OptionItemsTargetMesh.Get() != Mesh ||
        OptionItemsMaterialSlotCount != MaterialSlotCount || OptionItemsUVChannelCount != NumUVChannels ||
        OptionItemsDWCReadySignature != DWCReadySignature;
    if (!bMeshOptionsChanged)
    {
        return false;
    }

    OptionItemsTargetMesh = const_cast<USkeletalMesh*>(Mesh);
    OptionItemsMaterialSlotCount = MaterialSlotCount;
    OptionItemsUVChannelCount = NumUVChannels;
    OptionItemsDWCReadySignature = DWCReadySignature;
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
            if (DWCEditorPreviewSlotUtils::IsCpuPreviewReady(Asset, SlotIndex))
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

void SWetClothingTransparencyBakePanel::RefreshLayerItems()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr &&
        Asset->Authored.TransparencyData.DataVersion == FWetClothingTransparencyData::CurrentDataVersion)
    {
        const int32 DataUVChannel = ResolveTransparencyDataUVChannel(Asset);
        bool bRepairedLayerIdentity = false;
        bool bRepairedDataUV = false;
        for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
        {
            if (!Layer.LayerGuid.IsValid())
            {
                Layer.LayerGuid = FGuid::NewGuid();
                bRepairedLayerIdentity = true;
            }
            if (DataUVChannel != INDEX_NONE && Layer.TargetSurface.OuterUVChannel != DataUVChannel)
            {
                Layer.TargetSurface.OuterUVChannel = DataUVChannel;
                for (FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
                {
                    Stroke.UVChannelIndex = DataUVChannel;
                }
                Layer.MarkAutoBakeStale();
                bRepairedDataUV = true;
            }
        }
        if (bRepairedLayerIdentity || bRepairedDataUV)
        {
            Asset->MarkPackageDirty();
            if (bRepairedDataUV)
            {
                AutoBakeResults.Reset();
            }
        }
    }

    const int32 DataUVChannel = ResolveTransparencyDataUVChannel(Asset);
    bool bLayerItemsChanged = LayerItems.Num() != TargetMaterialSlotItems.Num();
    if (!bLayerItemsChanged)
    {
        for (int32 ItemIndex = 0; ItemIndex < LayerItems.Num(); ++ItemIndex)
        {
            const FLayerItemPtr& ExistingItem = LayerItems[ItemIndex];
            const FMaterialSlotItemPtr& TargetSlotItem = TargetMaterialSlotItems[ItemIndex];
            const FWetClothingTransparencyLayerData* ExistingLayer =
                Asset != nullptr && TargetSlotItem.IsValid()
                    ? Asset->Authored.TransparencyData.FindTransparencyLayer(
                        TargetSlotItem->SlotIndex,
                        DataUVChannel)
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
                        Asset->Authored.TransparencyData.FindTransparencyLayer(
                            Item->MaterialSlotIndex,
                            DataUVChannel))
                {
                    Item->LayerGuid = ExistingLayer->LayerGuid;
                }
            }
            LayerItems.Add(Item);
        }
    }

    if (GetSelectedLayer() == nullptr && SelectedLayerGuid.IsValid())
    {
        SelectedLayerGuid.Invalidate();
    }
    EnsureStageForSelectedLayer();
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
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

bool SWetClothingTransparencyBakePanel::RefreshModelState()
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
        StatusMessage = TEXT("Select a DWC-ready material slot as a Transparency Target Part.");
        PanelStatus = EDWCTransparencyPanelStatus::Info;
    }
    else if (!HasUsableTransparencyDataUV())
    {
        StatusMessage = TEXT("Generate the DWC UV Channel before configuring Transparency Target Parts.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else if (!DWCEditorPreviewSlotUtils::IsCpuPreviewReady(
        Asset,
        Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        StatusMessage = TEXT("The selected Transparency Target Part is not DWC-ready. Enable the slot and use Build for Runtime > Generate Materials before editing or baking transparency.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    else if (Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        TArray<FString> Errors;
        if (GetCurrentStage() == EDWCTransparencyEditorStage::FinalEditing &&
            EnsureActiveWorkingMap())
        {
            const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
            check(Existing != nullptr && Existing->IsValid());
            const FDWCTransparencyAutoBakeResult& Result = **Existing;
            if (Result.bIsFinalBakedBaseline)
            {
                StatusMessage = FString::Printf(
                    TEXT("Baked map loaded as an editable baseline. New brush edits: %d."),
                    FMath::Max(Layer->EditableStrokes.Num() - Result.BaselineStrokeCount, 0));
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
        else if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(Asset->GetDWCSkeletalMesh(), *Layer, Errors))
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
    else if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        StatusMessage = Asset->Authored.TransparencyData.SourceBlueprintClass.IsNull()
            ? TEXT("Assign a Source Blueprint containing a DWC Bake Component.")
            : TEXT("Packed Transparency Map generation for Other Skeletal Mesh Components is not implemented yet.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
    }
    UpdateInnerSourceStatus();
    return bOptionItemsChanged;
}

EDWCTransparencyEditorStage SWetClothingTransparencyBakePanel::GetCurrentStage() const
{
    if (const EDWCTransparencyEditorStage* Stage = StageByLayer.Find(SelectedLayerGuid))
    {
        return *Stage;
    }
    return EDWCTransparencyEditorStage::StructureSetup;
}

void SWetClothingTransparencyBakePanel::EnsureStageForSelectedLayer()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr)
    {
        if (!StageByLayer.Contains(FGuid()))
        {
            const bool bStructureConfigured = Asset != nullptr &&
                Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
            StageByLayer.Add(
                FGuid(),
                bStructureConfigured
                    ? EDWCTransparencyEditorStage::MapGeneration
                    : EDWCTransparencyEditorStage::StructureSetup);
        }
        return;
    }
    if (StageByLayer.Contains(Layer->LayerGuid))
    {
        return;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = FindExactBakedTransparencyMap(Asset, Layer);
    const bool bHasBakedMap = BakedMap != nullptr;
    const bool bStructureConfigured = Asset != nullptr &&
        Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
    StageByLayer.Add(
        Layer->LayerGuid,
        bHasBakedMap
            ? EDWCTransparencyEditorStage::FinalEditing
            : (bStructureConfigured
                ? EDWCTransparencyEditorStage::MapGeneration
                : EDWCTransparencyEditorStage::StructureSetup));
}

bool SWetClothingTransparencyBakePanel::CanEnterFinalEditingStage() const
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || Asset == nullptr)
    {
        return false;
    }

    if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        return true;
    }

    return FindExactBakedTransparencyMap(Asset, Layer) != nullptr;
}

void SWetClothingTransparencyBakePanel::SetCurrentStage(const EDWCTransparencyEditorStage Stage)
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Stage == EDWCTransparencyEditorStage::FinalEditing && !CanEnterFinalEditingStage())
    {
        StatusMessage = TEXT("Generate a Preview Transparency Map or bake/load an existing Transparency Map before entering Stage 3.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
        return;
    }
    StageByLayer.FindOrAdd(Layer != nullptr ? Layer->LayerGuid : FGuid()) = Stage;
    if (Stage == EDWCTransparencyEditorStage::FinalEditing)
    {
        if (SelectedVisualizationMode != EDWCTransparencyVisualizationMode::Final &&
            SelectedVisualizationMode != EDWCTransparencyVisualizationMode::AutoAlpha)
        {
            SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
        }
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
        }
        EnsureActiveWorkingMap();
    }
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
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
        Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured &&
        Asset->Authored.TransparencyData.CharacterStructureType == SourceType
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

FReply SWetClothingTransparencyBakePanel::HandleSourceTypeCardClicked(
    const EDWCTransparencySourceType SourceType)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ChooseTransparencyStructureType", "Choose Transparency Structure Type"));
    Asset->Modify();
    FWetClothingTransparencyData& TransparencyData = Asset->Authored.TransparencyData;
    const bool bTypeChanged =
        !TransparencyData.bCharacterStructureTypeConfigured ||
        TransparencyData.CharacterStructureType != SourceType;
    TransparencyData.CharacterStructureType = SourceType;
    TransparencyData.bCharacterStructureTypeConfigured = true;
    for (FWetClothingTransparencyLayerData& Layer : TransparencyData.TransparencyLayers)
    {
        Layer.SourceType = SourceType;
        Layer.bSourceTypeConfigured = true;
        if (bTypeChanged)
        {
            Layer.MarkAutoBakeStale();
        }
    }
    if (bTypeChanged)
    {
        AutoBakeResults.Reset();
    }
    Asset->MarkPackageDirty();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr &&
        Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured)
    {
        SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
    }
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::RefreshStageContent()
{
    ActiveThumbnails.Reset();
    if (StageContentContainer.IsValid())
    {
        StageContentContainer->SetContent(BuildCurrentStageContent());
    }
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
        if (StageContentContainer.IsValid())
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
        RefreshViewportContext();
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
    TSharedPtr<FDWCTransparencyAutoBakeResult>& OutResult,
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

    TSharedPtr<FDWCTransparencyAutoBakeResult> Result = MakeShared<FDWCTransparencyAutoBakeResult>();
    Result->LayerGuid = Layer.LayerGuid;
    Result->MaterialSlotIndex = BakedMap.MaterialSlotIndex;
    Result->UVChannelIndex = BakedMap.UVChannelIndex;
    Result->LODIndex = BakedMap.LODIndex;
    Result->Resolution = FIntPoint(Width, Height);
    Result->BuildSignature = BakedMap.BuildSignature;
    Result->InnerColorBuffer.SetNumUninitialized(PixelCount);
    Result->AutoAlphaBuffer.SetNumUninitialized(PixelCount);
    Result->ValidHitBuffer.Init(255, PixelCount);
    Result->HitDistanceBuffer.Init(0.0f, PixelCount);
    Result->RayConfidenceBuffer.Init(0, PixelCount);
    Result->SourcePriorityBuffer.Init(INDEX_NONE, PixelCount);
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        !FDWCTransparencyAutoMapGenerator::BuildTargetSurfaceBuffers(
            *Asset,
            Layer.TargetSurface,
            BakedMap.LODIndex,
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
        Layer.EditableStrokes.Num());
    Result->BaselineBakeGuid = BakedMap.BakeGuid;
    OutResult = MoveTemp(Result);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureActiveWorkingMap()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Layer == nullptr || GetCurrentStage() != EDWCTransparencyEditorStage::FinalEditing)
    {
        return false;
    }
    if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
        Existing != nullptr && Existing->IsValid())
    {
        return true;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = FindExactBakedTransparencyMap(Asset, Layer);
    if (BakedMap == nullptr)
    {
        return false;
    }
    TSharedPtr<FDWCTransparencyAutoBakeResult> LoadedResult;
    FString LoadError;
    if (!LoadBakedMapAsWorkingResult(*BakedMap, *Layer, LoadedResult, LoadError))
    {
        StatusMessage = LoadError;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(LoadedResult));
    if (!IsVisualizationModeAvailable(SelectedVisualizationMode))
    {
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    }
    return true;
}

int32 SWetClothingTransparencyBakePanel::GetCurrentBaselineStrokeCount() const
{
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = AutoBakeResults.Find(SelectedLayerGuid);
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
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr ? Asset->Authored.TransparencyData.SourceBlueprintClass.LoadSynchronous() : nullptr;
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
    Asset->Authored.TransparencyData.SourceBlueprintClass = NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
        ? const_cast<UClass*>(NewClass) : nullptr;
    for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
    {
        if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
        {
            Layer.MarkAutoBakeStale();
            AutoBakeResults.Remove(Layer.LayerGuid);
        }
    }
    Asset->MarkPackageDirty();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreview();
    }
}

FReply SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset != nullptr && Layer != nullptr &&
        (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots ||
         Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture))
    {
        if (!HasUsableTransparencyDataUV())
        {
            const FString Message = TEXT("Generate the DWC UV Channel before generating a Transparency Map.");
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }
        if (!DWCEditorPreviewSlotUtils::IsCpuPreviewReady(Asset, Layer->TargetSurface.OuterMaterialSlotIndex))
        {
            const FString Message = TEXT("The selected Transparency Target Part is not DWC-ready. Enable the slot and use Build for Runtime > Generate Materials before generating a Transparency Map.");
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }

        TArray<FString> ValidationErrors;
        if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                Asset->GetDWCSkeletalMesh(),
                *Layer,
                ValidationErrors))
        {
            const FString Message = FString::Join(ValidationErrors, TEXT("\n"));
            StatusMessage = Message;
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return FReply::Handled();
        }

        if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture &&
            !bPreparingRevealColorPaintWorkingMap)
        {
            if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
                Existing != nullptr && Existing->IsValid() &&
                !(*Existing)->bIsFinalBakedBaseline)
            {
                // Type 3 has already authored its source color in the Stage 2
                // working map. Generate is a stage transition here, not a
                // second rasterization pass that would replace that work.
                bRevealColorPaintEnabled = false;
                SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
                StatusMessage = TEXT("Preview Transparency Map is ready for Stage 3 editing.");
                PanelStatus = EDWCTransparencyPanelStatus::Ready;
                SetCurrentStage(EDWCTransparencyEditorStage::FinalEditing);
                return FReply::Handled();
            }
        }

        TSharedPtr<FDWCTransparencyAutoBakeResult> Result = MakeShared<FDWCTransparencyAutoBakeResult>();
        FString Summary;
        TArray<FString> Warnings;
        const bool bGenerated = Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots
            ? FDWCTransparencyAutoMapGenerator::GenerateSameMesh(*Asset, *Layer, *Result, Summary, Warnings)
            : FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(*Asset, *Layer, *Result, Summary, Warnings);
        if (!bGenerated)
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
        Layer->AutoBakeMetadata.PaddingPixels = Asset->Authored.TransparencyData.TransparencyPaddingPixels;
        Layer->AutoBakeMetadata.ValidHitCount = Result->ValidHitCount;
        Layer->AutoBakeMetadata.NoHitCount = Result->NoHitCount;
        Layer->MarkFinalBakeStale();
        Asset->MarkPackageDirty();
        // Keep only the active layer's large CPU intermediate buffers in memory.
        AutoBakeResults.Reset();
        AutoBakeResults.Add(Layer->LayerGuid, Result);
        const bool bRevealColorPaintWorkflow =
            Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture;
        // Type 3 needs a transient Stage 2 canvas while reveal-color paint is
        // being authored. The explicit Generate button promotes that canvas to
        // Stage 3; it must not leave the user in paint mode after generation.
        const bool bRemainInRevealColorPaint = bRevealColorPaintWorkflow && bPreparingRevealColorPaintWorkingMap;
        StageByLayer.FindOrAdd(Layer->LayerGuid) = bRemainInRevealColorPaint
            ? EDWCTransparencyEditorStage::MapGeneration
            : EDWCTransparencyEditorStage::FinalEditing;
        if (bRevealColorPaintWorkflow && !bRemainInRevealColorPaint)
        {
            bRevealColorPaintEnabled = false;
            SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
        }

        // The working map is ready now. Push it directly so the viewport does
        // not depend on the deferred Stage 2 -> Stage 3 layout rebuild before
        // it can show an authored manual reveal color (or a ray-generated map).
        if (PreviewViewport.IsValid())
        {
            // Manual reveal-color painting is surface editing. It must always
            // use the single target mesh so the hit BVH and the displayed MID
            // describe the same selected material slot.
            if (bRevealColorPaintWorkflow)
            {
                PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
            }
            PreviewViewport->SetAutoBakePreviewResult(Result);
            PreviewViewport->SetTransparencyPaintingEnabled(!bRevealColorPaintWorkflow);
            if (bRevealColorPaintWorkflow)
            {
                PushRevealColorPaintSettingsToViewport();
            }
        }

        for (const FDWCTransparencySourceHitStats& Stats : Result->SourceStats)
        {
            Summary += FString::Printf(TEXT("\n- Priority %d: %s (Slot %d) -> %d hit(s)"), Stats.PriorityIndex,
                *Stats.MaterialSlotName.ToString(), Stats.MaterialSlotIndex, Stats.HitCount);
        }
        if (!Warnings.IsEmpty()) Summary += TEXT("\n\nWarnings:\n- ") + FString::Join(Warnings, TEXT("\n- "));
        StatusMessage = Summary;
        PanelStatus = Warnings.IsEmpty() ? EDWCTransparencyPanelStatus::Ready : EDWCTransparencyPanelStatus::Warning;
        EDWCTransparencyPanelRefreshFlags RefreshFlags = EDWCTransparencyPanelRefreshFlags::Viewport;
        if (!bPreparingRevealColorPaintWorkingMap)
        {
            RefreshFlags |=
                EDWCTransparencyPanelRefreshFlags::StageContent |
                EDWCTransparencyPanelRefreshFlags::Details;
        }
        RequestRefresh(RefreshFlags);
        if (!bSuppressGenerateResultDialog)
        {
            FMessageDialog::Open(Warnings.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
                EAppMsgType::Ok, FText::FromString(Summary));
        }
        return FReply::Handled();
    }

    const FString Message =
        TEXT("Packed Transparency Map generation for Other Skeletal Mesh Components is not implemented yet. "
             "The legacy reveal-map/material fallback has been removed.");
    StatusMessage = Message;
    PanelStatus = EDWCTransparencyPanelStatus::Warning;
    FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(Message));
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
            LOCTEXT("BakeEditedGenerateFirst", "Generate a Preview Transparency Map or load an existing baked map before baking."));
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

        // The baker owns the texture and per-layer metadata. WCA validation
        // additionally reads this aggregate bake state, so mark it current in
        // the same successful transaction before the asset is saved.
        Asset->SetTransparencyBakeStatus(EDWCBakeStatus::Valid);
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
        *GetPathNameSafe(BakeResult.TransparencyMap));
    bool bHadWarnings = BakeResult.IgnoredNoHitOverridePixelCount > 0;
    if (BakeResult.IgnoredNoHitOverridePixelCount > 0)
    {
        Summary += FString::Printf(
            TEXT("\n\nWarning:\n%d manually edited pixel(s) had no valid inner-surface color and were kept at Alpha 0."),
            BakeResult.IgnoredNoHitOverridePixelCount);
    }
    if (!BakeResult.WarningMessage.IsEmpty())
    {
        bHadWarnings = true;
        Summary += TEXT("\n\nWarning:\n") + BakeResult.WarningMessage;
    }
    else if (BakeResult.bAppliedWrinkleSuppression)
    {
        Summary += TEXT("\nWrinkle suppression was baked into the Transparency Map alpha.");
    }

    if (!SaveTransparencySetupAssets())
    {
        bHadWarnings = true;
        Summary += TEXT("\n\nWarning:\nThe generated assets remain dirty because checkout/save was canceled or failed.");
    }
    AutoBakeResults.Reset();
    StatusMessage = Summary;
    PanelStatus = bHadWarnings ? EDWCTransparencyPanelStatus::Warning : EDWCTransparencyPanelStatus::Ready;
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport |
        EDWCTransparencyPanelRefreshFlags::Details);
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
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = AutoBakeResults.Find(Layer->LayerGuid);
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
FText SWetClothingTransparencyBakePanel::GetInnerSourceStatusText() const { return FText::FromString(InnerSourceStatusMessage); }
float SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent() const { return WetnessPreviewPercent; }
void SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged(float InValue)
{
    const float NewPercent = FMath::Clamp(InValue, 0.0f, 100.0f);
    if (FMath::IsNearlyEqual(WetnessPreviewPercent, NewPercent))
    {
        return;
    }
    WetnessPreviewPercent = NewPercent;
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
    const float NewStrength = FMath::Max(0.0f, InValue);
    if (FMath::IsNearlyEqual(TransparencyPreviewStrength, NewStrength))
    {
        return;
    }
    TransparencyPreviewStrength = NewStrength;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const FScopedTransaction Transaction(LOCTEXT("SetTransparencyPreviewStrength", "Set Transparency Preview Strength"));
        Asset->Modify();
        Asset->Authored.TransparencyData.TransparencyPreviewStrength = TransparencyPreviewStrength;
        for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
        {
            Layer.MarkFinalBakeStale();
        }
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
    const float NewStrength = FMath::Clamp(InValue, 0.0f, 5.0f);
    if (FMath::IsNearlyEqual(WrinkleSuppressionStrength, NewStrength))
    {
        return;
    }
    WrinkleSuppressionStrength = NewStrength;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const FScopedTransaction Transaction(LOCTEXT("SetWrinkleSuppressionStrength", "Set Wrinkle Suppression Strength"));
        Asset->Modify();
        Asset->Authored.TransparencyData.WrinkleSuppressionStrength = WrinkleSuppressionStrength;
        for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
        {
            Layer.MarkFinalBakeStale();
        }
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
        const bool bTargetAlphaVisibilityChanged =
            (BrushMode == EDWCTransparencyBrushMode::SetValue) !=
            (Mode == EDWCTransparencyBrushMode::SetValue);
        BrushMode = Mode;
        PushPaintSettingsToViewport();
        if (bTargetAlphaVisibilityChanged)
        {
            RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
        }
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
        if (FMath::IsNearlyEqual(RevealPaintRadiusUV, NewRadiusUV))
        {
            return;
        }
        RevealPaintRadiusUV = NewRadiusUV;
        PushRevealColorPaintSettingsToViewport();
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
    // A stroke already updates the preview texture incrementally in the
    // viewport. Rebuilding Stage 3 here recreated the whole editor panel for
    // every stroke, interrupting interaction and invalidating unrelated UI.
    RefreshTransparencyStrokeList();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SWetClothingTransparencyBakePanel::RefreshTransparencyStrokeList()
{
    if (TransparencyStrokeListContainer.IsValid())
    {
        TransparencyStrokeListContainer->SetContent(BuildTransparencyStrokeList());
    }
}

FReply SWetClothingTransparencyBakePanel::HandleUndoLastStrokeClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    const FScopedTransaction Transaction(LOCTEXT("RemoveLastTransparencyStroke", "Remove Last Transparency Stroke"));
    Asset->Modify();
    Layer->EditableStrokes.Pop();
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();
    if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    RefreshTransparencyStrokeList();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleClearStrokesClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    const FScopedTransaction Transaction(LOCTEXT("ClearTransparencyStrokes", "Clear Transparency Strokes"));
    Asset->Modify();
    Layer->EditableStrokes.RemoveAt(
        BaselineStrokeCount,
        Layer->EditableStrokes.Num() - BaselineStrokeCount);
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();
    if (PreviewViewport.IsValid()) PreviewViewport->RefreshManualPreviewFromStrokes();
    RefreshTransparencyStrokeList();
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
    RefreshTransparencyStrokeList();
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
        RefreshTransparencyStrokeList();
    }
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateVisualizationModeComboItem(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item) const
{
    const EDWCTransparencyVisualizationMode Mode =
        Item.IsValid() ? *Item : EDWCTransparencyVisualizationMode::Final;
    return SNew(SBox)
        .IsEnabled(IsVisualizationModeAvailable(Mode))
        [SNew(STextBlock).Text(GetVisualizationModeLabel(Mode))];
}

void SWetClothingTransparencyBakePanel::HandleVisualizationModeChanged(
    TSharedPtr<EDWCTransparencyVisualizationMode> Item,
    ESelectInfo::Type)
{
    if (!Item.IsValid())
    {
        return;
    }
    if (!IsVisualizationModeAvailable(*Item))
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

bool SWetClothingTransparencyBakePanel::IsGenerateEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Asset->GetDWCSkeletalMesh() == nullptr || Layer == nullptr ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion ||
        !HasUsableTransparencyDataUV()) return false;
    if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots ||
        Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        TArray<FString> Errors;
        return Layer->TargetSurface.OuterUVChannel == GetTransparencyDataUVChannel() &&
            DWCEditorPreviewSlotUtils::IsCpuPreviewReady(Asset, Layer->TargetSurface.OuterMaterialSlotIndex) &&
            FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(Asset->GetDWCSkeletalMesh(), *Layer, Errors);
    }
    return false;
}
bool SWetClothingTransparencyBakePanel::IsBakeEditedEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion ||
        (Layer->SourceType != EDWCTransparencySourceType::SameMeshMaterialSlots &&
         Layer->SourceType != EDWCTransparencySourceType::ManualColorOrTexture) ||
        !HasUsableTransparencyDataUV() || Layer->TargetSurface.OuterUVChannel != GetTransparencyDataUVChannel() ||
        !DWCEditorPreviewSlotUtils::IsCpuPreviewReady(Asset, Layer->TargetSurface.OuterMaterialSlotIndex))
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
        InnerSourceStatusMessage.Reset();
        return;
    }
    if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        InnerSourceStatusMessage = TEXT("Base reveal color and Reveal Color Paint are written directly to the target DWC UV Channel. No ray projection is used.");
        return;
    }
    if (Asset->Authored.TransparencyData.SourceBlueprintClass.IsNull())
    {
        InnerSourceStatusMessage = TEXT("Assign a Source Blueprint.");
        return;
    }
    InnerSourceStatusMessage = TEXT("Source Blueprint assigned. Its Bake Component layers will be validated when the map is generated.");
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

void SWetClothingTransparencyBakePanel::EditGlobalSettings(const FText& Text, TFunctionRef<void(FWetClothingTransparencyData&)> Edit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr) return;
    const FScopedTransaction Transaction(Text);
    Asset->Modify();
    Edit(Asset->Authored.TransparencyData);
    for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers) Layer.MarkAutoBakeStale();
    AutoBakeResults.Reset();
    Asset->MarkPackageDirty();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::Viewport);
}

void SWetClothingTransparencyBakePanel::EditFinalBakeSettings(
    const FText& Text,
    TFunctionRef<bool(FWetClothingTransparencyData&)> Edit,
    const EDWCTransparencyFinalPreviewRefresh PreviewRefresh)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }
    const FScopedTransaction Transaction(Text);
    Asset->Modify();
    if (!Edit(Asset->Authored.TransparencyData))
    {
        return;
    }
    for (FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
    {
        Layer.MarkFinalBakeStale();
    }
    Asset->MarkPackageDirty();
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

    return SNew(STableRow<FLayerItemPtr>, Owner)
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
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]];
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

    const int32 DataUVChannel = GetTransparencyDataUVChannel();
    if (!Item->LayerGuid.IsValid())
    {
        if (!Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured)
        {
            StatusMessage = TEXT("Choose a Character Structure Type in Stage 1 before selecting a Transparency Target Part.");
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            return;
        }
        if (DataUVChannel == INDEX_NONE)
        {
            StatusMessage = TEXT("Generate the DWC UV Channel before selecting a Transparency Target Part.");
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            return;
        }
        if (!DWCEditorPreviewSlotUtils::IsCpuPreviewReady(Asset, Item->MaterialSlotIndex))
        {
            StatusMessage = TEXT("The selected material slot is not DWC-ready. Enable the slot and use Build for Runtime > Generate Materials first.");
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            return;
        }

        const FScopedTransaction Transaction(LOCTEXT("CreateTransparencyTargetPart", "Create Transparency Target Part"));
        Asset->Modify();
        FWetClothingTransparencyLayerData& NewLayer = Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
        NewLayer.LayerGuid = FGuid::NewGuid();
        NewLayer.TargetSurface.OuterMaterialSlotIndex = Item->MaterialSlotIndex;
        NewLayer.TargetSurface.OuterMaterialSlotName = Item->MaterialSlotName;
        NewLayer.TargetSurface.OuterUVChannel = DataUVChannel;
        NewLayer.SourceType = Asset->Authored.TransparencyData.CharacterStructureType;
        NewLayer.bSourceTypeConfigured = true;
        Item->LayerGuid = NewLayer.LayerGuid;
        Asset->MarkPackageDirty();
    }

    const FWetClothingTransparencyLayerData* PreviousLayer = GetSelectedLayer();
    const EDWCTransparencyEditorStage PreviousStage = GetCurrentStage();
    const EDWCTransparencySourceType PreviousSourceType = PreviousLayer != nullptr
        ? PreviousLayer->SourceType
        : EDWCTransparencySourceType::SameMeshMaterialSlots;
    if (SelectedLayerGuid != Item->LayerGuid)
    {
        // Full 2048 intermediate results are intentionally scoped to the active target part.
        AutoBakeResults.Reset();
    }
    SelectedLayerGuid = Item->LayerGuid;
    if (PreviousStage == EDWCTransparencyEditorStage::MapGeneration)
    {
        StageByLayer.FindOrAdd(SelectedLayerGuid) = EDWCTransparencyEditorStage::MapGeneration;
    }
    const FWetClothingTransparencyLayerData* NewLayer = GetSelectedLayer();
    const bool bNeedsStageContentRefresh = NewLayer == nullptr ||
        PreviousLayer == nullptr ||
        PreviousSourceType != NewLayer->SourceType ||
        PreviousStage != GetCurrentStage();
    if (LayerListView.IsValid())
    {
        LayerListView->RequestListRefresh();
    }

    EDWCTransparencyPanelRefreshFlags RefreshFlags = EDWCTransparencyPanelRefreshFlags::Viewport;
    if (bNeedsStageContentRefresh)
    {
        RefreshFlags |= EDWCTransparencyPanelRefreshFlags::StageContent;
    }
    RequestRefresh(RefreshFlags);
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
    Asset->Authored.TransparencyData.TransparencyLayers.RemoveAll([this](const FWetClothingTransparencyLayerData& Layer) { return Layer.LayerGuid == SelectedLayerGuid; });
    AutoBakeResults.Remove(RemovedLayerGuid);
    StageByLayer.Remove(RemovedLayerGuid);
    SelectedLayerGuid.Invalidate();
    Asset->MarkPackageDirty();
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
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
    switch (Type) { case EDWCTransparencySourceType::OtherSkeletalMeshComponents: return LOCTEXT("OtherMeshSource", "Other Skeletal Mesh Components"); case EDWCTransparencySourceType::ManualColorOrTexture: return LOCTEXT("ManualSource", "No Inner Mesh / Base Color"); default: return LOCTEXT("SameMeshSource", "Same Skeletal Mesh / Material Slots"); }
}
FText SWetClothingTransparencyBakePanel::GetAddressModeLabel(EDWCTransparencyUVAddressMode Mode) const { return Mode == EDWCTransparencyUVAddressMode::Wrap ? LOCTEXT("UVWrap", "Wrap") : LOCTEXT("UVClamp", "Clamp"); }

void SWetClothingTransparencyBakePanel::HandleSourceTypeChanged(TSharedPtr<EDWCTransparencySourceType> Item, ESelectInfo::Type)
{
    if (!Item.IsValid()) return;
    HandleSourceTypeCardClicked(*Item);
}

bool SWetClothingTransparencyBakePanel::IsVisualizationModeAvailable(
    const EDWCTransparencyVisualizationMode Mode) const
{
    if (Mode == EDWCTransparencyVisualizationMode::Final ||
        Mode == EDWCTransparencyVisualizationMode::InnerColor ||
        Mode == EDWCTransparencyVisualizationMode::AutoAlpha ||
        Mode == EDWCTransparencyVisualizationMode::WrinkleSeparation)
    {
        return true;
    }
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* Result = AutoBakeResults.Find(SelectedLayerGuid);
    return Result != nullptr && Result->IsValid() && !(*Result)->bIsFinalBakedBaseline;
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
    ActiveThumbnails.Reset();
    return SNew(SBorder).Padding(12.0f).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [SAssignNew(ControlPanelScrollBox, SScrollBox) + SScrollBox::Slot()[SNew(SVerticalBox)
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildTargetMeshSection()]
         + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[BuildStageNavigation()]
         + SVerticalBox::Slot().AutoHeight()
           [SAssignNew(StageContentContainer, SBox)
             [BuildCurrentStageContent()]]]];
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
                    return Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured;
                }
                return CanEnterFinalEditingStage();
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
            + SHorizontalBox::Slot().FillWidth(1)
              [StageButton(
                  EDWCTransparencyEditorStage::FinalEditing,
                  LOCTEXT("TransparencyStage3", "3. Edit & Bake"),
                  LOCTEXT("TransparencyStage3Tooltip", "Edit alpha and bake the runtime Transparency Map."))]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildCurrentStageContent()
{
    switch (GetCurrentStage())
    {
    case EDWCTransparencyEditorStage::MapGeneration:
        return BuildMapGenerationStage();
    case EDWCTransparencyEditorStage::FinalEditing:
        return GetSelectedLayer() != nullptr
            ? BuildFinalEditingStage()
            : BuildEmptyAssetRow(LOCTEXT("SelectTargetPartForEditing", "Select a Transparency Target Part in Stage 2 before editing."));
    default:
        return BuildStructureSetupStage();
    }
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
                  [SNew(STextBlock).Text(Availability).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
              [SNew(STextBlock).Text(Description).AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildStructureSetupStage()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
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
              LOCTEXT("StructurePlanned", "Planned"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [BuildSourceTypeCard(
              EDWCTransparencySourceType::ManualColorOrTexture,
              LOCTEXT("NoInnerMeshStructure", "No Inner Mesh / Base Color"),
              LOCTEXT("NoInnerMeshStructureDescription", "Fill the target surface with an authored base reveal color, then edit its transparency alpha with the brush."),
              LOCTEXT("StructureAvailable2", "Available"))]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("ContinueToTransparencyGeneration", "Continue to Stage 2"))
            .IsEnabled(Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured)
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked)];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildMapGenerationStage()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    TSharedRef<SVerticalBox> SettingsBox = SNew(SVerticalBox);
    if (Layer == nullptr)
    {
        SettingsBox->AddSlot().AutoHeight()
          [BuildEmptyAssetRow(LOCTEXT("SelectTargetPartForGeneration", "Select a DWC-ready Transparency Target Part above to configure its source and generate a preview map."))];
    }
    else if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        SettingsBox->AddSlot().AutoHeight().Padding(0,8,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("Stage2InnerSources", "Inner Source Parts"))];
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildSameMeshSourceSection()];
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildRaySettingsSection()];
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildBakeSettingsSection(false)];
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,8)
          [SNew(SButton)
            .HAlign(HAlign_Center)
            .Text(LOCTEXT("GeneratePreviewTransparencyMap", "Generate Preview Transparency Map"))
            .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
            .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    }
    else if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,10)[BuildOtherMeshSourceSection()];
        SettingsBox->AddSlot().AutoHeight()
          [BuildEmptyAssetRow(LOCTEXT("Stage2MultiMeshPlanned", "Multiple Skeletal Mesh generation will be implemented in a later step."))];
    }
    else
    {
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildManualSourceSection()];
        SettingsBox->AddSlot().AutoHeight().Padding(0,0,0,8)
          [SNew(SButton)
            .HAlign(HAlign_Fill)
            .ContentPadding(FMargin(8.0f, 7.0f))
            .Text(LOCTEXT("GenerateManualPreviewTransparencyMap", "Generate Preview Transparency Map"))
            .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetGenerateTooltipText)
            .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsGenerateEnabled)
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleGenerateTransparencyMapClicked)];
    }

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("MapGenerationStage", "Stage 2 - Preview Map Generation"))]
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
                // The surrounding control panel owns scrolling for the full Stage 2 workflow.
                .OnSlotResized_Lambda([](float) {})
                [SettingsBox]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildFinalEditingStage()
{
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* WorkingResult = AutoBakeResults.Find(SelectedLayerGuid);
    const bool bHasWorkingResult = WorkingResult != nullptr && WorkingResult->IsValid();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("FinalEditingStage", "Stage 3 - Transparency Editing & Bake"))];
    if (!bHasWorkingResult)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,10)
          [BuildEmptyAssetRow(LOCTEXT("NoWorkingTransparencyMap", "No editable working map is available. Return to Stage 2 and generate a Preview Transparency Map."))];
    }
    else if ((*WorkingResult)->bIsFinalBakedBaseline)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,10)
          [SNew(SBorder).Padding(7).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [SNew(STextBlock)
              .Text(LOCTEXT("BakedBaselineNotice", "The existing baked map is loaded as the baseline. New brush strokes are editable. Transparency and wrinkle-suppression settings are already flattened into this map; regenerate in Stage 2 to change those settings or rebuild earlier ray data."))
              .AutoWrapText(true)
              .ColorAndOpacity(FSlateColor::UseSubduedForeground())]];
    }
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildTransparencyBrushSection()];
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildPreviewSettingsSection()];
    Box->AddSlot().AutoHeight().Padding(0,0,0,14)[BuildGeneratedOutputsSection()];
    Box->AddSlot().AutoHeight()
      [SNew(SButton)
        .HAlign(HAlign_Center)
        .Text(LOCTEXT("BakeTransparencyMap", "Bake Transparency Map"))
        .ToolTipText(this, &SWetClothingTransparencyBakePanel::GetBakeEditedTooltipText)
        .IsEnabled(this, &SWetClothingTransparencyBakePanel::IsBakeEditedEnabled)
        .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleBakeEditedTransparencyMapClicked)];
    return Box;
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
    if (StageContentContainer.IsValid())
    {
        StageContentContainer->Invalidate(EInvalidateWidget::Layout);
    }
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

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* Mesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    const auto MakeMaterialThumbnail = [this, Mesh](const int32 MaterialSlotIndex) -> TSharedRef<SWidget>
    {
        TSharedRef<SWidget> ThumbnailWidget =
            SNew(SBorder)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

        UMaterialInterface* Material = nullptr;
        if (Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            Material = Mesh->GetMaterials()[MaterialSlotIndex].MaterialInterface;
        }
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
        return ThumbnailWidget;
    };

    for (int32 Index = 0; Index < Layer->SameMeshSource.InnerSlotPriority.Num(); ++Index)
    {
        const auto& Slot = Layer->SameMeshSource.InnerSlotPriority[Index];
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)
        [
            SNew(SBorder)
            .Padding(FMargin(4, 3))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                [
                    SNew(SBox)
                    .WidthOverride(44.0f)
                    .HeightOverride(44.0f)
                    [MakeMaterialThumbnail(Slot.MaterialSlotIndex)]
                ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,4,0)
                [
                    SNew(SComboBox<FMaterialSlotItemPtr>)
                    .OptionsSource(&MaterialSlotItems)
                    .InitiallySelectedItem(FindMaterialSlotItem(Slot.MaterialSlotIndex))
                    .OnGenerateWidget_Lambda([](const FMaterialSlotItemPtr& Item)
                    {
                        return SNew(STextBlock).Text(Item.IsValid()
                            ? FText::Format(LOCTEXT("InnerSourceMaterialOption", "[{0}] {1}"), FText::AsNumber(Item->SlotIndex), FText::FromName(Item->SlotName))
                            : LOCTEXT("MissingInnerSourceMaterialOption", "Missing"));
                    })
                    .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerMaterialSlotChanged, Index)
                    [
                        SNew(STextBlock).Text_Lambda([this, Index]()
                        {
                            const FWetClothingTransparencyLayerData* Selected = GetSelectedLayer();
                            if (Selected == nullptr || !Selected->SameMeshSource.InnerSlotPriority.IsValidIndex(Index))
                            {
                                return LOCTEXT("MissingInnerPart", "Missing");
                            }
                            const FWetClothingTransparencyInnerSlot& InnerSlot = Selected->SameMeshSource.InnerSlotPriority[Index];
                            return FText::Format(LOCTEXT("InnerSourceMaterialLabel", "[{0}] {1}"), FText::AsNumber(InnerSlot.MaterialSlotIndex), FText::FromName(InnerSlot.MaterialSlotName));
                        })
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                [SNew(SBox).WidthOverride(62)[SNew(SComboBox<TSharedPtr<int32>>).OptionsSource(&UVChannelItems).InitiallySelectedItem(FindUVChannelItem(Slot.SourceUVChannel)).OnGenerateWidget(this, &SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem).OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleInnerUVChannelChanged, Index)[SNew(STextBlock).Text_Lambda([this, Index](){ const auto* Selected = GetSelectedLayer(); const int32 UV = Selected && Selected->SameMeshSource.InnerSlotPriority.IsValidIndex(Index) ? Selected->SameMeshSource.InnerSlotPriority[Index].SourceUVChannel : 0; return FText::Format(LOCTEXT("InnerUV", "UV {0}"), FText::AsNumber(UV)); })]]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerUpTooltip", "Move this source earlier in the priority order.")).IsEnabled(Index > 0).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, Index, -1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowUp")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("MoveInnerDownTooltip", "Move this source later in the priority order.")).IsEnabled(Index + 1 < Layer->SameMeshSource.InnerSlotPriority.Num()).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleMoveInnerSlotClicked, Index, 1)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.ArrowDown")))]]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ToolTipText(LOCTEXT("DeleteInnerTooltip", "Remove this Inner Source Part.")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRemoveInnerSlotClicked, Index)[SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildManualSourceSection()
{
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Layer == nullptr)
    {
        return BuildEmptyAssetRow(LOCTEXT("NoManualTargetPart", "Select a Transparency Target Part before configuring its base reveal color."));
    }

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
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("ManualColorSource", "Base Reveal Color"))]
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
                  .ToolTipText(LOCTEXT("ManualBaseRevealColorTooltip", "Choose the color visible through this wet target surface."))
                  .Text(LOCTEXT("ManualBaseRevealColorButton", "Select Color"))
                  .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleManualBaseColorClicked)]
               + SVerticalBox::Slot().AutoHeight()
                 [SNew(SButton)
                  .ToolTipText(LOCTEXT("ManualPickUVIslandTooltip", "Choose a reference color texture and sample one of its UV islands."))
                  .Text(LOCTEXT("ManualPickUVIslandButton", "Pick From UV Island"))
                  .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleManualPickBaseColorFromUVIslandClicked)]
               + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 4.0f)
                 [SNew(STextBlock)
                  .Text(LOCTEXT("ManualInitialAlpha", "Initial Transparency Alpha"))
                  .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
               + SVerticalBox::Slot().AutoHeight()
                 [SNew(SNumericEntryBox<float>)
                  .MinValue(0.0f)
                  .MaxValue(1.0f)
                  .Value(this, &SWetClothingTransparencyBakePanel::GetManualInitialTransparencyAlpha)
                  .OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleManualInitialTransparencyAlphaCommitted)]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 6)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RevealColorPaint", "Reveal Color Paint"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SCheckBox)
             .IsChecked(this, &SWetClothingTransparencyBakePanel::IsRevealColorPaintEnabledChecked)
             .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleRevealColorPaintEnabledChanged)
             [SNew(STextBlock).Text(LOCTEXT("EnableRevealColorPaint", "Enable Reveal Color Paint"))]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [SNew(SBox)
             .Visibility_Lambda([this]()
             {
                 return bRevealColorPaintEnabled ? EVisibility::Visible : EVisibility::Collapsed;
             })
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
                        switch (RevealPaintMode)
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
                        return SelectedLayer != nullptr && !SelectedLayer->RevealColorPaintStrokes.IsEmpty();
                    })
                    .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleClearRevealColorPaintClicked)
                    [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [SNew(SBox)
                 .Visibility_Lambda([this]()
                 {
                     return RevealPaintMode == EDWCTransparencyRevealColorBrushMode::Paint
                         ? EVisibility::Visible
                         : EVisibility::Collapsed;
                 })
                 [SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                    [SNew(SColorBlock).Color_Lambda([this] { return RevealPaintColor; }).Size(FVector2D(38, 38)).ShowBackgroundForAlpha(false)]
                  + SHorizontalBox::Slot().FillWidth(1)
                    [SNew(SButton).Text(LOCTEXT("SelectRevealPaintColor", "Select Paint Color")).OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintColorClicked)]]]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [BuildBrushSizeControl(LOCTEXT("RevealPaintBrushSize", "Brush Size"), EDWCTransparencyBrushSizeTarget::RevealColorPaint)]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [BuildLabeledControl(LOCTEXT("RevealPaintStrength", "Brush Strength"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetRevealPaintStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintStrengthCommitted))]
              + SVerticalBox::Slot().AutoHeight()
                [BuildLabeledControl(LOCTEXT("RevealPaintFalloff", "Brush Falloff"), SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetRevealPaintFalloff).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleRevealPaintFalloffCommitted))]]];

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
        .InitialMesh(Asset->GetDWCSkeletalMesh())
        .InitialOriginalUVChannel(Asset->GetOriginalUVChannelIndex())
        .OnColorAccepted(FOnDWCTransparencyUVIslandColorAccepted::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandleManualBaseColorCommitted)));

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
            TargetLayer.ManualColorSource.BaseRevealColor = NewColor;
        },
        false);

    if (bRevealColorPaintEnabled)
    {
        TGuardValue<bool> SuppressDialog(bSuppressGenerateResultDialog, true);
        TGuardValue<bool> PreparingWorkingMap(bPreparingRevealColorPaintWorkingMap, true);
        HandleGenerateTransparencyMapClicked();
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        PushRevealColorPaintSettingsToViewport();
    }
}

FReply SWetClothingTransparencyBakePanel::HandleRevealPaintColorClicked()
{
    FColorPickerArgs Args;
    Args.InitialColor = RevealPaintColor;
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
    if (RevealPaintColor.Equals(NewColor)) return;
    RevealPaintColor = NewColor;
    PushRevealColorPaintSettingsToViewport();
    Invalidate(EInvalidateWidgetReason::Paint);
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealColorPaintEnabledChecked() const
{
    return bRevealColorPaintEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealColorPaintModeChecked(
    const EDWCTransparencyRevealColorBrushMode Mode) const
{
    return RevealPaintMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingTransparencyBakePanel::HandleRevealColorPaintModeChanged(
    const ECheckBoxState NewState,
    const EDWCTransparencyRevealColorBrushMode Mode)
{
    if (NewState != ECheckBoxState::Checked || RevealPaintMode == Mode)
    {
        return;
    }

    RevealPaintMode = Mode;
    PushRevealColorPaintSettingsToViewport();
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SWetClothingTransparencyBakePanel::HandleRevealColorPaintEnabledChanged(ECheckBoxState NewState)
{
    bool bEnabled = NewState == ECheckBoxState::Checked;
    if (bRevealColorPaintEnabled == bEnabled)
    {
        return;
    }

    if (bEnabled)
    {
        const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
        const bool bNeedsManualWorkingMap =
            Layer != nullptr &&
            Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture &&
            (!AutoBakeResults.Contains(Layer->LayerGuid) || !AutoBakeResults.FindChecked(Layer->LayerGuid).IsValid());
        if (bNeedsManualWorkingMap)
        {
            // Type 3 has no ray-dependent source.  Enabling paint therefore
            // creates the base-color/initial-alpha working map immediately,
            // rather than making the user press Generate first.
            TGuardValue<bool> SuppressDialog(bSuppressGenerateResultDialog, true);
            TGuardValue<bool> PreparingWorkingMap(bPreparingRevealColorPaintWorkingMap, true);
            HandleGenerateTransparencyMapClicked();
            const FWetClothingTransparencyLayerData* UpdatedLayer = GetSelectedLayer();
            bEnabled = UpdatedLayer != nullptr &&
                AutoBakeResults.Contains(UpdatedLayer->LayerGuid) &&
                AutoBakeResults.FindChecked(UpdatedLayer->LayerGuid).IsValid();
        }
    }

    bRevealColorPaintEnabled = bEnabled;
    if (bRevealColorPaintEnabled && PreviewViewport.IsValid())
    {
        // Stage 2 edits the reveal-color source. Display RGB directly so a
        // zero initial transparency alpha does not hide the selected target.
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    }
    else if (!bRevealColorPaintEnabled && PreviewViewport.IsValid())
    {
        if (SelectedVisualizationMode == EDWCTransparencyVisualizationMode::InnerColor)
        {
            SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
            PreviewViewport->SetVisualizationMode(SelectedVisualizationMode);
        }
        PreviewViewport->SetRevealColorPaintingEnabled(false);
    }
    PushRevealColorPaintSettingsToViewport();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetTransparencyPaintingEnabled(false);
    }
    // Re-run only the viewport context push. The checkbox and reveal-paint
    // controls are already live Slate widgets, so rebuilding the full Stage 2
    // subtree here just recreates rows and thumbnails unnecessarily.
    Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Volatility);
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
}

float SWetClothingTransparencyBakePanel::GetRevealPaintSizeCm() const
{
    return DWCTransparencyRadiusUVToSizeCm(RevealPaintRadiusUV);
}

FText SWetClothingTransparencyBakePanel::GetRevealPaintSizeDisplayText() const
{
    return FormatDWCTransparencyBrushSizeCm(GetRevealPaintSizeCm());
}
TOptional<float> SWetClothingTransparencyBakePanel::GetRevealPaintStrength() const { return RevealPaintStrength; }
TOptional<float> SWetClothingTransparencyBakePanel::GetRevealPaintFalloff() const { return RevealPaintFalloff; }
void SWetClothingTransparencyBakePanel::HandleRevealPaintStrengthCommitted(float Value, ETextCommit::Type) { RevealPaintStrength = FMath::Clamp(Value, 0.0f, 1.0f); PushRevealColorPaintSettingsToViewport(); }
void SWetClothingTransparencyBakePanel::HandleRevealPaintFalloffCommitted(float Value, ETextCommit::Type) { RevealPaintFalloff = FMath::Clamp(Value, 0.0f, 1.0f); PushRevealColorPaintSettingsToViewport(); }

FReply SWetClothingTransparencyBakePanel::HandleClearRevealColorPaintClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr || Layer->RevealColorPaintStrokes.IsEmpty())
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ClearRevealColorPaint", "Clear Reveal Color Paint"));
    Asset->Modify();
    Layer->RevealColorPaintStrokes.Reset();
    Layer->MarkAutoBakeStale();
    Layer->MarkFinalBakeStale();
    Asset->MarkPackageDirty();

    TGuardValue<bool> SuppressDialog(bSuppressGenerateResultDialog, true);
    TGuardValue<bool> PreparingWorkingMap(bPreparingRevealColorPaintWorkingMap, true);
    HandleGenerateTransparencyMapClicked();
    SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
    PushRevealColorPaintSettingsToViewport();
    RequestRefresh(EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::PushRevealColorPaintSettingsToViewport()
{
    if (!PreviewViewport.IsValid()) return;
    FDWCTransparencyPaintSettings Settings;
    Settings.RadiusUV = RevealPaintRadiusUV;
    Settings.Strength = RevealPaintStrength;
    Settings.Falloff = RevealPaintFalloff;
    Settings.Spacing = 0.25f;
    Settings.bEnabled = bRevealColorPaintEnabled;
    Settings.bRevealColorPaint = bRevealColorPaintEnabled;
    Settings.RevealColorMode = RevealPaintMode;
    Settings.RevealColor = RevealPaintColor;
    PreviewViewport->SetPaintSettings(Settings);
    PreviewViewport->SetRevealColorPaintingEnabled(bRevealColorPaintEnabled);
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

    if (bRevealColorPaintEnabled)
    {
        TGuardValue<bool> SuppressDialog(bSuppressGenerateResultDialog, true);
        TGuardValue<bool> PreparingWorkingMap(bPreparingRevealColorPaintWorkingMap, true);
        HandleGenerateTransparencyMapClicked();
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        PushRevealColorPaintSettingsToViewport();
    }
}

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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBakeSettingsSection(const bool bShowResolution)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BakeSettings", "Bake Settings"))];
    if (Asset == nullptr) return Box;
    if (bShowResolution)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BakeResolution", "Resolution"), SNew(SNumericEntryBox<int32>).MinValue(16).MaxValue(4096).Value(Asset->Authored.TransparencyData.TransparencyBakeResolution).OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type){ EditGlobalSettings(LOCTEXT("SetTransparencyResolution", "Set Transparency Resolution"), [V](auto& D){ D.TransparencyBakeResolution = FMath::Clamp(V,16,4096); }); }))];
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
    if (BrushMode == EDWCTransparencyBrushMode::SetValue)
    {
        Box->AddSlot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("BrushTargetAlpha", "Target Alpha"),
            SNew(SNumericEntryBox<float>).MinValue(0.0f).MaxValue(1.0f).Value(this, &SWetClothingTransparencyBakePanel::GetBrushTargetAlpha).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleBrushTargetAlphaCommitted))];
    }

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
        ? FMath::Max(Layer->EditableStrokes.Num() - BaselineStrokeCount, 0)
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
        for (int32 StrokeIndex = BaselineStrokeCount; StrokeIndex < Layer->EditableStrokes.Num(); ++StrokeIndex)
        {
            const FDWCTransparencyBrushStroke& Stroke = Layer->EditableStrokes[StrokeIndex];
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
        + SVerticalBox::Slot().AutoHeight()[BuildPackedTransparencyMapSection()];
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
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* WorkingResult = AutoBakeResults.Find(SelectedLayerGuid);
    const bool bCanRecomputeFinalSettings = WorkingResult == nullptr ||
        !WorkingResult->IsValid() ||
        !(*WorkingResult)->bIsFinalBakedBaseline;
    return SNew(SBorder).Padding(10).BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))[SNew(SVerticalBox)
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("PreviewSettings", "Preview Settings"))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,3)[SNew(STextBlock).Text(LOCTEXT("PreviewWetnessLabel", "Preview Wetness"))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(SSlider).MinValue(0).MaxValue(100).Value(this, &SWetClothingTransparencyBakePanel::GetWetnessPreviewPercent).OnValueChanged(this, &SWetClothingTransparencyBakePanel::HandleWetnessPreviewChanged)]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("TransparencyPreviewStrengthLabel", "Transparency Strength"),
          SNew(SNumericEntryBox<float>).IsEnabled(bCanRecomputeFinalSettings).MinValue(0.0f).MaxValue(8.0f).Value(this, &SWetClothingTransparencyBakePanel::GetTransparencyPreviewStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleTransparencyPreviewStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionStrengthLabel", "Wrinkle Suppression Strength"),
          SNew(SNumericEntryBox<float>).IsEnabled(bCanRecomputeFinalSettings).MinValue(0.0f).MaxValue(5.0f).Value(this, &SWetClothingTransparencyBakePanel::GetWrinkleSuppressionStrength).OnValueCommitted(this, &SWetClothingTransparencyBakePanel::HandleWrinkleSuppressionStrengthCommitted))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionThresholdLabel", "Wrinkle Mask Threshold"),
          SNew(SNumericEntryBox<float>)
              .IsEnabled(bCanRecomputeFinalSettings)
              .MinValue(0.0f).MaxValue(1.0f)
              .Value_Lambda([this]() -> TOptional<float> { const UWetClothingAsset* A = WetClothingAsset.Get(); return A != nullptr ? TOptional<float>(A->Authored.TransparencyData.WrinkleSuppressionCoverageThreshold) : TOptional<float>(); })
              .OnValueCommitted_Lambda([this](float V, ETextCommit::Type){ EditFinalBakeSettings(LOCTEXT("SetWrinkleSuppressionThreshold", "Set Wrinkle Coverage Threshold"), [V](auto& D){ const float NewValue = FMath::Clamp(V, 0.0f, 1.0f); if (FMath::IsNearlyEqual(D.WrinkleSuppressionCoverageThreshold, NewValue)) return false; D.WrinkleSuppressionCoverageThreshold = NewValue; return true; }, EDWCTransparencyFinalPreviewRefresh::WrinkleSuppression); }))]
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[BuildLabeledControl(LOCTEXT("WrinkleSuppressionSoftnessLabel", "Wrinkle Mask Softness"),
          SNew(SNumericEntryBox<float>)
              .IsEnabled(bCanRecomputeFinalSettings)
              .MinValue(0.0f).MaxValue(1.0f)
              .Value_Lambda([this]() -> TOptional<float> { const UWetClothingAsset* A = WetClothingAsset.Get(); return A != nullptr ? TOptional<float>(A->Authored.TransparencyData.WrinkleSuppressionMaskSoftness) : TOptional<float>(); })
              .OnValueCommitted_Lambda([this](float V, ETextCommit::Type){ EditFinalBakeSettings(LOCTEXT("SetWrinkleSuppressionSoftness", "Set Wrinkle Mask Softness"), [V](auto& D){ const float NewValue = FMath::Clamp(V, 0.0f, 1.0f); if (FMath::IsNearlyEqual(D.WrinkleSuppressionMaskSoftness, NewValue)) return false; D.WrinkleSuppressionMaskSoftness = NewValue; return true; }, EDWCTransparencyFinalPreviewRefresh::WrinkleSuppression); }))]
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
      + SVerticalBox::Slot().FillHeight(1)
        [SAssignNew(PreviewViewport, SWetClothingTransparencyPreviewViewport)
            .WetClothingAsset(WetClothingAsset.Get())
            .OnStrokesChanged(this, &SWetClothingTransparencyBakePanel::HandleViewportStrokesChanged)]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildPreviewModeButton(EWetClothingTransparencyPreviewMode Mode, const FText& Label)
{
    return SNew(SCheckBox).Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton")).Type(ESlateCheckBoxType::ToggleButton)
        .IsEnabled_Lambda([this, Mode](){ return Mode != EWetClothingTransparencyPreviewMode::FullBlueprint || CanUseFullBlueprintPreview(); })
        .IsChecked(this, &SWetClothingTransparencyBakePanel::IsPreviewModeChecked, Mode).OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandlePreviewModeChanged, Mode)[SNew(STextBlock).Text(Label)];
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
    EnsureActiveWorkingMap();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const bool bFinalEditing =
        GetCurrentStage() == EDWCTransparencyEditorStage::FinalEditing;
    const bool bRevealColorPaintActive =
        GetCurrentStage() == EDWCTransparencyEditorStage::MapGeneration &&
        Layer != nullptr &&
        Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture &&
        bRevealColorPaintEnabled;
    if (bRevealColorPaintActive)
    {
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::InnerColor;
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    }
    else if (bFinalEditing &&
             SelectedVisualizationMode != EDWCTransparencyVisualizationMode::Final &&
             SelectedVisualizationMode != EDWCTransparencyVisualizationMode::AutoAlpha)
    {
        SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    }
    if (!CanUseFullBlueprintPreview() && PreviewViewport->GetPreviewMode() == EWetClothingTransparencyPreviewMode::FullBlueprint)
        PreviewViewport->SetPreviewMode(EWetClothingTransparencyPreviewMode::TargetMeshOnly);
    PreviewViewport->SetTransparencyEditContext(SelectedLayerGuid,
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : INDEX_NONE,
        Layer != nullptr ? Layer->TargetSurface.OuterUVChannel : 0,
        Layer != nullptr ? Layer->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp);

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

    PreviewViewport->SetWetnessPreviewPercent(WetnessPreviewPercent);
    PreviewViewport->SetTransparencyPreviewStrength(TransparencyPreviewStrength);
    PreviewViewport->SetWrinkleSuppressionStrength(WrinkleSuppressionStrength);
    PreviewViewport->SetVisualizationMode(SelectedVisualizationMode);
    if (bRevealColorPaintActive)
    {
        PushRevealColorPaintSettingsToViewport();
        PreviewViewport->SetTransparencyPaintingEnabled(false);
    }
    else
    {
        PreviewViewport->SetRevealColorPaintingEnabled(false);
        PushPaintSettingsToViewport();
        PreviewViewport->SetTransparencyPaintingEnabled(bFinalEditing);
    }
}

#undef LOCTEXT_NAMESPACE
