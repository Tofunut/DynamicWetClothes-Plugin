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
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
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
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyWorkflowPolicy.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
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
    AuthoringController = MakeShared<FDWCTransparencyAuthoringController>(
        WetClothingAsset.Get(), AuthoringDocument, SessionStore);
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    BakeCoordinator = InArgs._BakeCoordinator;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
    PreviewCommitCoordinator = InArgs._PreviewCommitCoordinator;
    RenderUploadQueue = InArgs._RenderUploadQueue;
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
    SessionStore->OnChanged().AddSP(this, &SWetClothingTransparencyBakePanel::HandleSessionStateChanged);
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
    VisualizationModeItems.Add(MakeShared<EDWCTransparencyVisualizationMode>(EDWCTransparencyVisualizationMode::SourcePriority));
    SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
    DispatchTransparencyPreviewState();
    RepairInvalidLayerIdentities();
    RefreshModelState();
    RebuildEditorLayout();
    RefreshViewportContext();
}

SWetClothingTransparencyBakePanel::~SWetClothingTransparencyBakePanel()
{
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
    Action.PreviewMode = PreviewViewport.IsValid()
        ? PreviewViewport->GetPreviewMode()
        : EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    Action.VisualizationMode = SelectedVisualizationMode;
    Action.WetnessPreviewPercent = WetnessPreviewPercent;
    Action.TransparencyPreviewStrength = TransparencyPreviewStrength;
    Action.WrinkleSuppressionStrength = WrinkleSuppressionStrength;
    Action.bShowSavedWrinkle = bShowSavedWrinkle;
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
    Settings.bEnabled = bRevealColorPaintEnabled;
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
    FDWCSetTransparencyPaintAction Action;
    Action.Paint = MoveTemp(Settings);
    Action.bRevealPaint = true;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

void SWetClothingTransparencyBakePanel::DisableRevealPaintInSession()
{
    FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
    Settings.bEnabled = false;
    DispatchRevealPaintState(MoveTemp(Settings));
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
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : INDEX_NONE;
    Action.Context.UVChannelIndex = GetTransparencyDataUVChannel();
    Action.Context.AddressMode = Layer != nullptr
        ? Layer->TargetSurface.UVAddressMode
        : EDWCTransparencyUVAddressMode::Clamp;
    Action.Context.PaintTarget = DWCTransparencyWorkflow::ResolvePaintTarget(
        Stage,
        Layer != nullptr ? Layer->SourceType : EDWCTransparencySourceType::SameMeshMaterialSlots);
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
    SelectedLayerGuid = TransparencyState.SelectedLayerGuid;
    StageByLayer = TransparencyState.StageByLayer;
    SelectedVisualizationMode = TransparencyState.VisualizationMode;
    WetnessPreviewPercent = TransparencyState.WetnessPreviewPercent;
    TransparencyPreviewStrength = TransparencyState.TransparencyPreviewStrength;
    WrinkleSuppressionStrength = TransparencyState.WrinkleSuppressionStrength;
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
    bRevealColorPaintEnabled = RevealPaint.bEnabled;

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
                return Candidate.IsValid() && Candidate->LayerGuid == SelectedLayerGuid;
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
            [this](const FLayerItemPtr& Item) { return Item.IsValid() && Item->LayerGuid == SelectedLayerGuid; });
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

void SWetClothingTransparencyBakePanel::RepairInvalidLayerIdentities()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !AuthoringDocument.IsValid() ||
        Asset->Authored.TransparencyData.DataVersion != FWetClothingTransparencyData::CurrentDataVersion)
    {
        return;
    }

    bool bHasInvalidLayerGuid = false;
    for (const FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
    {
        if (!Layer.LayerGuid.IsValid())
        {
            bHasInvalidLayerGuid = true;
            break;
        }
    }
    if (!bHasInvalidLayerGuid)
    {
        return;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::ElementList;
    AuthoringDocument->Edit(
        LOCTEXT("RepairTransparencyLayerIdentities", "Repair Transparency Target Part Identities"),
        Change,
        [](UWetClothingAsset& MutableAsset)
        {
            bool bChanged = false;
            for (FWetClothingTransparencyLayerData& Layer : MutableAsset.Authored.TransparencyData.TransparencyLayers)
            {
                if (!Layer.LayerGuid.IsValid())
                {
                    Layer.LayerGuid = FGuid::NewGuid();
                    bChanged = true;
                }
            }
            return bChanged;
        });
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

    if (GetSelectedLayer() == nullptr && SelectedLayerGuid.IsValid())
    {
        SelectedLayerGuid.Invalidate();
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSelectTransparencyLayerAction{FGuid()});
        }
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
    // Asset-wide refreshes arrive after saves, external edits and editor mode
    // changes. They update model/viewport state, but must not reconstruct the
    // active Stage 2/3 subtree. Stage content is refreshed explicitly only for
    // a stage or source-type change, or after an operation that changed its
    // generated-output data.
    bPreviewSlotStateRefreshRequested = true;
    RepairInvalidLayerIdentities();

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
    else if (Layer->SourceType != EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        TArray<FString> Errors;
        if (GetCurrentStage() == EDWCTransparencyEditorStage::FinalEditing &&
            EnsureFinalEditingWorkingMap())
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
    else if (Layer->SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        StatusMessage = Asset->Authored.TransparencyData.SourceBlueprintClass.IsNull()
            ? TEXT("Assign a Source Blueprint containing a DWC Bake Component.")
            : TEXT("Packed Transparency Map generation for Other Skeletal Mesh Components is not implemented yet.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
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
            // Selecting a structure type only edits Stage 1 data. Stage 2 is
            // unlocked by the explicit Continue action below.
            const EDWCTransparencyEditorStage Stage = EDWCTransparencyEditorStage::StructureSetup;
            if (SessionStore.IsValid())
            {
                SessionStore->Dispatch(FDWCSetTransparencyStageAction{FGuid(), Stage});
            }
            else
            {
                StageByLayer.Add(FGuid(), Stage);
            }
        }
        return;
    }
    if (StageByLayer.Contains(Layer->LayerGuid))
    {
        return;
    }

    const FWetClothingBakedTransparencyMap* BakedMap = FindExactBakedTransparencyMap(Asset, Layer);
    const bool bHasBakedMap = BakedMap != nullptr;
    const EDWCTransparencyEditorStage Stage = bHasBakedMap
        ? EDWCTransparencyEditorStage::FinalEditing
        : EDWCTransparencyEditorStage::StructureSetup;
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{Layer->LayerGuid, Stage});
    }
    else
    {
        StageByLayer.Add(Layer->LayerGuid, Stage);
    }
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
    const FGuid StageLayerGuid = Layer != nullptr ? Layer->LayerGuid : FGuid();
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetTransparencyStageAction{StageLayerGuid, Stage});
    }
    else
    {
        StageByLayer.FindOrAdd(StageLayerGuid) = Stage;
    }
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
        EnsureFinalEditingWorkingMap();
    }
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
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
        Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured &&
        Asset->Authored.TransparencyData.CharacterStructureType == SourceType
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

bool SWetClothingTransparencyBakePanel::IsSourceTypeAvailable(
    const EDWCTransparencySourceType SourceType) const
{
    return DWCTransparencyWorkflow::IsSourceTypeAvailable(SourceType);
}

bool SWetClothingTransparencyBakePanel::CanContinueToGeneration() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return DWCTransparencyWorkflow::CanContinueToGeneration(
        Asset != nullptr,
        Asset != nullptr && Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured,
        Asset != nullptr
            ? Asset->Authored.TransparencyData.CharacterStructureType
            : EDWCTransparencySourceType::SameMeshMaterialSlots);
}

FReply SWetClothingTransparencyBakePanel::HandleSourceTypeCardClicked(
    const EDWCTransparencySourceType SourceType)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
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

    const bool bTypeChanged = !Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured ||
        Asset->Authored.TransparencyData.CharacterStructureType != SourceType;
    if (!bTypeChanged)
    {
        return FReply::Handled();
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("ChooseTransparencyStructureType", "Choose Transparency Structure Type"),
            Change,
            [SourceType](UWetClothingAsset& MutableAsset)
            {
                FWetClothingTransparencyData& TransparencyData = MutableAsset.Authored.TransparencyData;
                TransparencyData.CharacterStructureType = SourceType;
                TransparencyData.bCharacterStructureTypeConfigured = true;
                return true;
            }).bChanged)
    {
        return FReply::Handled();
    }
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

FReply SWetClothingTransparencyBakePanel::HandleContinueToGenerationClicked()
{
    if (CanContinueToGeneration())
    {
        SetCurrentStage(EDWCTransparencyEditorStage::MapGeneration);
    }
    else
    {
        StatusMessage = TEXT("Choose an available character structure type before continuing to Stage 2.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        RequestRefresh(EDWCTransparencyPanelRefreshFlags::StageContent);
    }
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
        StageContentSwitcher->SetActiveWidgetIndex(2);
        RefreshFinalEditingContent();
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
        default:
            break;
        }
    }

    MapGenerationSettingsSwitcher->SetActiveWidgetIndex(SettingsIndex);
    RefreshInnerSourceSlotItems();
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
        Layer.EditableStrokes.Num());
    Result->BaselineBakeGuid = BakedMap.BakeGuid;
    OutResult = MoveTemp(Result);
    return true;
}

bool SWetClothingTransparencyBakePanel::EnsureFinalEditingWorkingMap()
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

bool SWetClothingTransparencyBakePanel::EnsureManualRevealWorkingMap(const bool bForceRebuild)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    if (Asset == nullptr || Layer == nullptr ||
        GetCurrentStage() != EDWCTransparencyEditorStage::MapGeneration ||
        Layer->SourceType != EDWCTransparencySourceType::ManualColorOrTexture)
    {
        return false;
    }

    if (!bForceRebuild)
    {
        if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
            Existing != nullptr && Existing->IsValid() && !(*Existing)->bIsFinalBakedBaseline)
        {
            return true;
        }
    }

    if (!HasUsableTransparencyDataUV())
    {
        StatusMessage = TEXT("Generate the DWC Data UV before editing Reveal Color.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }
    if (!PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
    {
        const FDWCEditorPreviewSlotState* State =
            FindPreviewSlotState(Layer->TargetSurface.OuterMaterialSlotIndex);
        StatusMessage = State != nullptr
            ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
            : TEXT("The selected Transparency Target Part is unavailable for preview.");
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    TArray<FString> ValidationErrors;
    if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
            Asset->GetDWCSkeletalMesh(),
            *Layer,
            ValidationErrors,
            ResolveTransparencyDataUVChannel(Asset)))
    {
        StatusMessage = FString::Join(ValidationErrors, TEXT("\n"));
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    TSharedPtr<FDWCTransparencyAutoBakeResult> Result = MakeShared<FDWCTransparencyAutoBakeResult>();
    FString Summary;
    TArray<FString> Warnings;
    if (!FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
            *Asset, *Layer, *Result, Summary, Warnings))
    {
        StatusMessage = Summary;
        PanelStatus = EDWCTransparencyPanelStatus::Warning;
        return false;
    }

    // Stage 2 owns only an editor-session buffer.  Do not create a
    // transaction, mutate bake metadata, dirty the asset, or change stages.
    AutoBakeResults.Reset();
    AutoBakeResults.Add(Layer->LayerGuid, MoveTemp(Result));
    StatusMessage = Warnings.IsEmpty()
        ? TEXT("Reveal Color working map is ready.")
        : Summary;
    PanelStatus = Warnings.IsEmpty()
        ? EDWCTransparencyPanelStatus::Ready
        : EDWCTransparencyPanelStatus::Warning;
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
    const TSoftClassPtr<AActor> NewSourceClass =
        NewClass != nullptr && NewClass->IsChildOf(AActor::StaticClass())
            ? const_cast<UClass*>(NewClass)
            : nullptr;
    TArray<FGuid> AffectedLayerGuids;
    for (const FWetClothingTransparencyLayerData& Layer : Asset->Authored.TransparencyData.TransparencyLayers)
    {
        if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
        {
            AffectedLayerGuids.Add(Layer.LayerGuid);
        }
    }
    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Transparency;
    Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    if (!AuthoringDocument.IsValid() ||
        !AuthoringDocument->Edit(
            LOCTEXT("SetTransparencySourceBlueprint", "Set Transparency Source Blueprint"),
            Change,
            [NewSourceClass](UWetClothingAsset& MutableAsset)
            {
                if (MutableAsset.Authored.TransparencyData.SourceBlueprintClass == NewSourceClass) return false;
                MutableAsset.Authored.TransparencyData.SourceBlueprintClass = NewSourceClass;
                return true;
            }).bChanged)
    {
        return;
    }
    for (const FGuid& LayerGuid : AffectedLayerGuids)
    {
        AutoBakeResults.Remove(LayerGuid);
    }
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

        if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
        {
            if (const TSharedPtr<FDWCTransparencyAutoBakeResult>* Existing = AutoBakeResults.Find(Layer->LayerGuid);
                Existing != nullptr && Existing->IsValid() &&
                !(*Existing)->bIsFinalBakedBaseline)
            {
                // Type 3 has already authored its source color in the Stage 2
                // working map. Generate is a stage transition here, not a
                // second rasterization pass that would replace that work.
                SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
                DisableRevealPaintInSession();
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
        const EDWCTransparencyEditorStage ResultStage =
            EDWCTransparencyEditorStage::FinalEditing;
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSetTransparencyStageAction{Layer->LayerGuid, ResultStage});
        }
        else
        {
            StageByLayer.FindOrAdd(Layer->LayerGuid) = ResultStage;
        }
        if (bRevealColorPaintWorkflow)
        {
            SelectedVisualizationMode = EDWCTransparencyVisualizationMode::Final;
            DisableRevealPaintInSession();
        }
        // Generation can advance Stage 2 directly to Stage 3 without passing
        // through SetCurrentStage. Keep the controller's paint target in the
        // session authoritative for that direct transition as well.
        DispatchTransparencyEditContext();

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
        RefreshFlags |=
            EDWCTransparencyPanelRefreshFlags::StageContent |
            EDWCTransparencyPanelRefreshFlags::Details;
        RequestRefresh(RefreshFlags);
        FMessageDialog::Open(Warnings.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
            EAppMsgType::Ok, FText::FromString(Summary));
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
    if (!BakeCoordinator.IsValid())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeCoordinatorUnavailable", "The asynchronous bake service is unavailable."));
        return FReply::Handled();
    }

    StatusMessage = TEXT("Baking the edited transparency map...");
    PanelStatus = EDWCTransparencyPanelStatus::Info;
    const FGuid LayerGuid = Layer->LayerGuid;
    TWeakPtr<SWetClothingTransparencyBakePanel> WeakPanel = SharedThis(this);
    FString RequestError;
    if (!BakeCoordinator->RequestTransparencyFinalBake(
            LayerGuid,
            StoredResult->ToSharedRef(),
            true,
            [WeakPanel, LayerGuid](const FDWCEditorBakeBatchResult& Result)
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
            },
            &RequestError))
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
    DispatchTransparencyPreviewState();
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
    EditFinalBakeSettings(
        LOCTEXT("SetTransparencyPreviewStrength", "Set Transparency Preview Strength"),
        [NewStrength](FWetClothingTransparencyData& Data)
        {
            if (FMath::IsNearlyEqual(Data.TransparencyPreviewStrength, NewStrength)) return false;
            Data.TransparencyPreviewStrength = NewStrength;
            return true;
        },
        EDWCTransparencyFinalPreviewRefresh::None);
    DispatchTransparencyPreviewState();
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
    EditFinalBakeSettings(
        LOCTEXT("SetWrinkleSuppressionStrength", "Set Wrinkle Suppression Strength"),
        [NewStrength](FWetClothingTransparencyData& Data)
        {
            if (FMath::IsNearlyEqual(Data.WrinkleSuppressionStrength, NewStrength)) return false;
            Data.WrinkleSuppressionStrength = NewStrength;
            return true;
        },
        EDWCTransparencyFinalPreviewRefresh::WrinkleSuppression);
    DispatchTransparencyPreviewState();
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
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer->RevealColorPaintStrokes)
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
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    const FGuid StrokeGuid = Layer->EditableStrokes.Last().StrokeGuid;
    if (!EditSelectedLayerFinal(
            LOCTEXT("RemoveLastTransparencyStroke", "Remove Last Transparency Stroke"),
            StrokeGuid,
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                if (MutableLayer.EditableStrokes.Num() <= BaselineStrokeCount) return false;
                MutableLayer.EditableStrokes.Pop();
                return true;
            })) return FReply::Handled();
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleClearStrokesClicked()
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const int32 BaselineStrokeCount = GetCurrentBaselineStrokeCount();
    if (Asset == nullptr || Layer == nullptr || Layer->EditableStrokes.Num() <= BaselineStrokeCount)
    {
        return FReply::Handled();
    }
    if (!EditSelectedLayerFinal(
            LOCTEXT("ClearTransparencyStrokes", "Clear Transparency Strokes"),
            FGuid(),
            [BaselineStrokeCount](FWetClothingTransparencyLayerData& MutableLayer)
            {
                if (MutableLayer.EditableStrokes.Num() <= BaselineStrokeCount) return false;
                MutableLayer.EditableStrokes.RemoveAt(
                    BaselineStrokeCount,
                    MutableLayer.EditableStrokes.Num() - BaselineStrokeCount);
                return true;
            })) return FReply::Handled();
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
    if (!EditSelectedLayerFinal(
            LOCTEXT("DeleteTransparencyStroke", "Delete Transparency Stroke"),
            StrokeGuid,
            [StrokeGuid](FWetClothingTransparencyLayerData& MutableLayer)
            {
                return MutableLayer.EditableStrokes.RemoveAll(
                    [StrokeGuid](const FDWCTransparencyBrushStroke& Stroke)
                    {
                        return Stroke.StrokeGuid == StrokeGuid;
                    }) > 0;
            })) return FReply::Handled();
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
    if (FDWCTransparencyBrushStroke* Stroke = Layer->EditableStrokes.FindByPredicate(
            [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; }))
    {
        const bool bEnabled = NewState == ECheckBoxState::Checked;
        if (!EditSelectedLayerFinal(
                LOCTEXT("ToggleTransparencyStroke", "Toggle Transparency Stroke"),
                StrokeGuid,
                [StrokeGuid, bEnabled](FWetClothingTransparencyLayerData& MutableLayer)
                {
                    FDWCTransparencyBrushStroke* MutableStroke = MutableLayer.EditableStrokes.FindByPredicate(
                        [StrokeGuid](const FDWCTransparencyBrushStroke& Candidate)
                        {
                            return Candidate.StrokeGuid == StrokeGuid;
                        });
                    if (MutableStroke == nullptr || MutableStroke->bEnabled == bEnabled) return false;
                    MutableStroke->bEnabled = bEnabled;
                    return true;
                })) return;
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
    DispatchTransparencyPreviewState();
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
    if (Layer->SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots ||
        Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        TArray<FString> Errors;
        return PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex) &&
            FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                Asset->GetDWCSkeletalMesh(), *Layer, Errors, GetTransparencyDataUVChannel());
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
        !HasUsableTransparencyDataUV() ||
        !PreviewSlotStates.IsReady(Layer->TargetSurface.OuterMaterialSlotIndex))
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
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyFinalBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = ElementGuid;
    return AuthoringDocument->Edit(
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
        if (!PreviewSlotStates.IsReady(Item->MaterialSlotIndex))
        {
            const FDWCEditorPreviewSlotState* State = FindPreviewSlotState(Item->MaterialSlotIndex);
            StatusMessage = State != nullptr
                ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
                : TEXT("The selected material slot is unavailable for preview.");
            PanelStatus = EDWCTransparencyPanelStatus::Warning;
            return;
        }

        const FGuid NewLayerGuid = FGuid::NewGuid();
        FDWCEditorAuthoringChange Change;
        Change.Domain = EDWCEditorAuthoringDomain::Transparency;
        Change.Impact = EDWCEditorAuthoringImpact::AssetDirty |
            EDWCEditorAuthoringImpact::ElementList |
            EDWCEditorAuthoringImpact::Preview |
            EDWCEditorAuthoringImpact::TransparencyAutoBake;
        Change.MaterialSlotIndex = Item->MaterialSlotIndex;
        Change.LayerGuid = NewLayerGuid;
        if (!AuthoringDocument.IsValid() ||
            !AuthoringDocument->Edit(
                LOCTEXT("CreateTransparencyTargetPart", "Create Transparency Target Part"),
                Change,
                [NewLayerGuid, SlotIndex = Item->MaterialSlotIndex, SlotName = Item->MaterialSlotName](UWetClothingAsset& MutableAsset)
                {
                    FWetClothingTransparencyLayerData& NewLayer =
                        MutableAsset.Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
                    NewLayer.LayerGuid = NewLayerGuid;
                    NewLayer.TargetSurface.OuterMaterialSlotIndex = SlotIndex;
                    NewLayer.TargetSurface.OuterMaterialSlotName = SlotName;
                    NewLayer.SourceType = MutableAsset.Authored.TransparencyData.CharacterStructureType;
                    NewLayer.bSourceTypeConfigured = true;
                    return true;
                }).bChanged)
        {
            return;
        }
        Item->LayerGuid = NewLayerGuid;
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
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSelectTransparencyLayerAction{SelectedLayerGuid});
    }
    if (PreviousStage == EDWCTransparencyEditorStage::MapGeneration)
    {
        if (SessionStore.IsValid())
        {
            SessionStore->Dispatch(FDWCSetTransparencyStageAction{
                SelectedLayerGuid,
                EDWCTransparencyEditorStage::MapGeneration});
        }
        else
        {
            StageByLayer.FindOrAdd(SelectedLayerGuid) = EDWCTransparencyEditorStage::MapGeneration;
        }
    }
    DispatchTransparencyEditContext();
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
    if (bNeedsStageContentRefresh || GetCurrentStage() == EDWCTransparencyEditorStage::MapGeneration)
    {
        // Stage 2 owns persistent settings widgets. The selected target part
        // can still change its Inner Source list, so refresh that list even
        // when the source type and stage themselves did not change.
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
        SessionStore->Dispatch(FDWCSelectTransparencyLayerAction{FGuid()});
    }
    RequestRefresh(
        EDWCTransparencyPanelRefreshFlags::Model |
        EDWCTransparencyPanelRefreshFlags::StageContent |
        EDWCTransparencyPanelRefreshFlags::Viewport);
    return FReply::Handled();
}
bool SWetClothingTransparencyBakePanel::CanRemoveSelectedLayer() const { return GetSelectedLayer() != nullptr; }

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::GenerateUVChannelComboItem(TSharedPtr<int32> Item) const { return SNew(STextBlock).Text(FText::Format(LOCTEXT("UVChannelLabel", "UV {0}"), FText::AsNumber(Item.IsValid() ? *Item : 0))); }
FText SWetClothingTransparencyBakePanel::GetMaterialSlotLabel(int32 Index) const
{
    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(Index);
    return Item.IsValid() ? FText::Format(LOCTEXT("MaterialSlotLabel", "Slot {0} / {1}"), FText::AsNumber(Index), FText::FromName(Item->SlotName)) : LOCTEXT("NoMaterialSlot", "None");
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
                    // Stage 2 is a user-driven transition, not a side effect
                    // of choosing a character structure type in Stage 1.
                    return Asset->Authored.TransparencyData.bCharacterStructureTypeConfigured &&
                        GetCurrentStage() != EDWCTransparencyEditorStage::StructureSetup;
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
                  [SNew(STextBlock).Text(Availability).ColorAndOpacity(FSlateColor::UseSubduedForeground())]]
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
            + SVerticalBox::Slot().AutoHeight()
              [BuildEmptyAssetRow(LOCTEXT("Stage2MultiMeshPlanned", "Multiple Skeletal Mesh generation will be implemented in a later step."))];
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
                .OnSlotResized_Lambda([](float) {})
                [SAssignNew(MapGenerationSettingsSwitcher, SWidgetSwitcher)
                    + SWidgetSwitcher::Slot()
                      [BuildEmptyAssetRow(LOCTEXT("SelectTargetPartForGeneration", "Select a ready Wettable Transparency Target Part above to configure its source and generate a preview map."))]
                    + SWidgetSwitcher::Slot()[BuildSameMeshSettings()]
                    + SWidgetSwitcher::Slot()[BuildOtherMeshSettings()]
                    + SWidgetSwitcher::Slot()[BuildManualSettings()]]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildFinalEditingStage()
{
    TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("FinalEditingStage", "Stage 3 - Transparency Editing & Bake"))];
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
    const TSharedPtr<FDWCTransparencyAutoBakeResult>* WorkingResult = AutoBakeResults.Find(SelectedLayerGuid);
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

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildOtherMeshSourceSection()
{
    return BuildLabeledControl(LOCTEXT("SourceBlueprint", "Source Blueprint"),
        SNew(SClassPropertyEntryBox).MetaClass(AActor::StaticClass()).AllowAbstract(false).AllowNone(true).SelectedClass(this, &SWetClothingTransparencyBakePanel::GetSelectedSourceClass).OnSetClass(this, &SWetClothingTransparencyBakePanel::HandleSourceClassChanged));
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildManualSourceSection()
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
                 return GetRevealPaintSettingsFromSession().bEnabled
                     ? EVisibility::Visible
                     : EVisibility::Collapsed;
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
                        return SelectedLayer != nullptr && !SelectedLayer->RevealColorPaintStrokes.IsEmpty();
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
                  .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateRevealColorStrokeRow)]]]];

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

    EnsureManualRevealWorkingMap(true);
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

ECheckBoxState SWetClothingTransparencyBakePanel::IsRevealColorPaintEnabledChecked() const
{
    return GetRevealPaintSettingsFromSession().bEnabled
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
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

void SWetClothingTransparencyBakePanel::HandleRevealColorPaintEnabledChanged(ECheckBoxState NewState)
{
    const bool bEnabled = NewState == ECheckBoxState::Checked;
    FDWCTransparencyPaintSettings Settings = GetRevealPaintSettingsFromSession();
    if (Settings.bEnabled == bEnabled)
    {
        return;
    }

    // The toggle only gates stroke writes.  The Stage 2 reveal target and its
    // working map are derived from the layer/stage context, not from this UI.
    Settings.bEnabled = bEnabled;
    DispatchRevealPaintState(MoveTemp(Settings));
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
    EditRevealColorStrokeHistory(
        LOCTEXT("ClearRevealColorPaint", "Clear Reveal Color Paint"),
        FGuid(),
        [SlotIndex](FWetClothingTransparencyLayerData& MutableLayer)
        {
            return MutableLayer.RevealColorPaintStrokes.RemoveAll(
                [SlotIndex](const FDWCTransparencyRevealColorStroke& Stroke)
                {
                    return Stroke.MaterialSlotIndex == SlotIndex;
                }) > 0;
        });
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
        EDWCEditorAuthoringImpact::Preview |
        EDWCEditorAuthoringImpact::TransparencyAutoBake;
    Change.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Change.LayerGuid = LayerGuid;
    Change.ElementGuid = StrokeGuid;
    const FDWCEditorAuthoringResult Result = AuthoringDocument->Edit(
        TransactionText,
        Change,
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
    EnsureManualRevealWorkingMap(true);
    RefreshViewportContext();
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
    for (int32 Index = Layer->RevealColorPaintStrokes.Num() - 1; Index >= 0; --Index)
    {
        if (Layer->RevealColorPaintStrokes[Index].MaterialSlotIndex == SlotIndex)
        {
            LastStrokeGuid = Layer->RevealColorPaintStrokes[Index].StrokeGuid;
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
    EditRevealColorStrokeHistory(
        LOCTEXT("DeleteRevealColorStroke", "Delete Reveal Color Stroke"),
        StrokeGuid,
        [StrokeGuid](FWetClothingTransparencyLayerData& MutableLayer)
        {
            return MutableLayer.RevealColorPaintStrokes.RemoveAll(
                [StrokeGuid](const FDWCTransparencyRevealColorStroke& Stroke)
                {
                    return Stroke.StrokeGuid == StrokeGuid;
                }) > 0;
        });
    return FReply::Handled();
}

void SWetClothingTransparencyBakePanel::HandleRevealColorStrokeEnabledChanged(
    const ECheckBoxState NewState,
    const FGuid StrokeGuid)
{
    if (AuthoringController.IsValid()) AuthoringController->CancelActiveInteraction(true);
    const bool bEnabled = NewState == ECheckBoxState::Checked;
    EditRevealColorStrokeHistory(
        LOCTEXT("ToggleRevealColorStroke", "Toggle Reveal Color Stroke"),
        StrokeGuid,
        [StrokeGuid, bEnabled](FWetClothingTransparencyLayerData& MutableLayer)
        {
            FDWCTransparencyRevealColorStroke* Stroke =
                MutableLayer.RevealColorPaintStrokes.FindByPredicate(
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
        });
}

TSharedRef<ITableRow> SWetClothingTransparencyBakePanel::GenerateRevealColorStrokeRow(
    TSharedPtr<FGuid> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FGuid StrokeGuid = Item.IsValid() ? *Item : FGuid();
    const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
    const FDWCTransparencyRevealColorStroke* Stroke = Layer != nullptr
        ? Layer->RevealColorPaintStrokes.FindByPredicate(
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

    EnsureManualRevealWorkingMap(true);
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
                    FText::AsNumber(GetTransparencyDataUVChannel()),
                    FText::AsNumber(0),
                    FText::AsNumber(Map.Resolution),
                    BakeState),
                GeneratedOutputThumbnails)];
    }
    return Box;
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
      + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
        [SNew(SCheckBox)
            .IsChecked(this, &SWetClothingTransparencyBakePanel::GetShowSavedWrinkleState)
            .OnCheckStateChanged(this, &SWetClothingTransparencyBakePanel::HandleShowSavedWrinkleChanged)
            .ToolTipText(LOCTEXT("ShowSavedWrinkleTooltip", "Show the wrinkle normal texture currently selected for runtime. Live Wrinkle Editor hover and stroke data are not included."))
            [SNew(STextBlock).Text(LOCTEXT("ShowSavedWrinkle", "Show Saved Wrinkle"))]]
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
        + SHorizontalBox::Slot().AutoWidth()[BuildPreviewModeButton(EWetClothingTransparencyPreviewMode::FullBlueprint, LOCTEXT("AllWettableSlotsPreview", "All Wettable Slots"))]]];
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
            .SpatialQueryService(SpatialQueryService)
            .TextureWorkspace(TextureWorkspace)
            .PreviewCommitCoordinator(PreviewCommitCoordinator)
            .RenderUploadQueue(RenderUploadQueue)]];
    if (AuthoringController.IsValid())
    {
        AuthoringController->AttachViewport(PreviewViewport);
        PreviewViewport->SetAuthoringController(AuthoringController);
    }
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
    const EDWCTransparencySourceType SourceType = Layer != nullptr
        ? Layer->SourceType
        : EDWCTransparencySourceType::SameMeshMaterialSlots;
    const EWetClothingTransparencyPreviewMode RequestedPreviewMode =
        PreviewViewport->GetPreviewMode();
    const bool bRevealPaintEnabled = GetRevealPaintSettingsFromSession().bEnabled;
    DWCTransparencyWorkflow::FDWCTransparencyPreviewContext PreviewContext =
        DWCTransparencyWorkflow::ResolvePreviewContext(
            CurrentStage,
            SourceType,
            SelectedVisualizationMode,
            RequestedPreviewMode,
            bRevealPaintEnabled,
            CanUseFullBlueprintPreview(),
            false,
            false);

    // Working maps are session resources. The policy decides which one is
    // relevant; only that resource is prepared here.
    if (PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor)
    {
        EnsureManualRevealWorkingMap();
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
        bRevealPaintEnabled,
        CanUseFullBlueprintPreview(),
        PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::RevealColor && bHasWorkingMap,
        PreviewContext.PaintTarget == EDWCTransparencyPaintTarget::FinalAlpha && bHasWorkingMap);

    PreviewViewport->SetPreviewMode(PreviewContext.PreviewMode);
    PreviewViewport->SetTransparencyEditContext(SelectedLayerGuid,
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : INDEX_NONE,
        GetTransparencyDataUVChannel(),
        Layer != nullptr ? Layer->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp,
        PreviewContext.PaintTarget);

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
    PreviewViewport->SetShowSavedWrinkle(bShowSavedWrinkle);
    PreviewViewport->SetWrinkleSuppressionStrength(WrinkleSuppressionStrength);
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
