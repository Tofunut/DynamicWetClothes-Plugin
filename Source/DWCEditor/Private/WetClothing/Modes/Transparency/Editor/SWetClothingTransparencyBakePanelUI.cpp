//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Editor/SWetClothingTransparencyBakePanel.h"
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBakePanelUtilities.h"

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
#include "SAdvancedTransformInputBox.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/Foundation/UV/DWCEditorUVTopologyCache.h"
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
#include "WetClothing/Modes/Transparency/Editor/DWCTransparencyBlueprintHierarchySession.h"
#include "WetClothing/Modes/Transparency/Authoring/DWCTransparencyAuthoringController.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"
#include "WetClothing/Modes/Transparency/Viewport/SWetClothingTransparencyPreviewViewport.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "WetClothing/Modes/Transparency/Diagnostics/DWCTransparencyBaselineDiagnostics.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyBakedBaselineMemoryPolicy.h"
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

TSharedRef<SWidget> BuildLabeledControl(const FText& Label, const TSharedRef<SWidget>& Control)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [SNew(STextBlock).Text(Label).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
        + SVerticalBox::Slot().AutoHeight()[Control];
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
        SLATE_ARGUMENT(const UWetClothingAsset*, OwnerAsset)
        SLATE_ARGUMENT(TSharedPtr<FDWCEditorCacheStore>, CacheStore)
        SLATE_ARGUMENT(USkeletalMesh*, InitialMesh)
        SLATE_ARGUMENT(int32, InitialOriginalUVChannel)
        SLATE_EVENT(FOnDWCTransparencyUVIslandColorAccepted, OnColorAccepted)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        ParentWindow = InArgs._ParentWindow;
        OwnerAsset = InArgs._OwnerAsset;
        CacheStore = InArgs._CacheStore;
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
        TopologyLease.Reset();
        StatusMessage.Reset();

        const USkeletalMesh* Mesh = ReferenceMesh.Get();
        if (Mesh == nullptr || SelectedSlotIndex == INDEX_NONE || SelectedUVChannel == INDEX_NONE)
        {
            UVIslandView->Clear();
            StatusMessage = TEXT("Select a reference mesh, material slot, and UV channel.");
            return;
        }

        FString ErrorMessage;
        FDWCEditorCacheKey TopologyKey;
        if (!FDWCEditorUVTopologyCache::AcquireForMesh(
                CacheStore,
                OwnerAsset.Get(),
                Mesh,
                0,
                SelectedUVChannel,
                SelectedSlotIndex,
                TopologyKey,
                TopologyLease,
                &ErrorMessage))
        {
            UVIslandView->Clear();
            StatusMessage = ErrorMessage;
            return;
        }

        if (const FDWCEditorUVTopologyCacheValue* Topology =
                TopologyLease.GetAs<FDWCEditorUVTopologyCacheValue>())
        {
            IslandOptions = Topology->Islands;
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
    TWeakObjectPtr<const UWetClothingAsset> OwnerAsset;
    TSharedPtr<FDWCEditorCacheStore> CacheStore;
    FDWCEditorCacheLease TopologyLease;
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
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingResult = AutoBakeResults.Find(GetSelectedLayerGuid());
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
    TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FName ComponentName = Item.IsValid() ? Item->ComponentName : NAME_None;
    const bool bTargetCandidate = Item.IsValid() && IsBlueprintTargetCandidate(*Item);
    const int32 Indent = Item.IsValid() ? GetBlueprintHierarchyDepth(*Item) : 0;
    return SNew(STableRow<TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>>, OwnerTable)
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
                            .Text(Item.IsValid() && !Item->SkeletalMeshPath.IsNull()
                                ? FText::FromString(Item->SkeletalMeshPath.GetAssetName())
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
    const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>* Component = Source != nullptr
        ? BlueprintHierarchyItems.FindByPredicate(
            [Source](
                const TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>& Candidate)
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
                        .Text(Component != nullptr && Component->IsValid() &&
                                !(*Component)->SkeletalMeshPath.IsNull()
                            ? FText::FromString((*Component)->SkeletalMeshPath.GetAssetName())
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
                                ? UE::DWCEditor::TransparencyPanel::GetBlueprintSourceRoleLabel(*Role)
                                : LOCTEXT("MissingBlueprintSourceRole", "Missing"));
                        })
                        .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleBlueprintSourceRoleChanged, PriorityIndex)
                        [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
                        {
                            const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                            return SelectedLayer != nullptr &&
                                    SelectedLayer->BlueprintSource.SourcePriority.IsValidIndex(PriorityIndex)
                                ? UE::DWCEditor::TransparencyPanel::GetBlueprintSourceRoleLabel(
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
            .ToolTipText(LOCTEXT("RefreshBlueprintHierarchyTooltip", "Refresh the selected Blueprint's default Skeletal Mesh Component metadata."))
            .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleRefreshBlueprintHierarchyClicked)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
                [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(STextBlock).Text(LOCTEXT("RefreshBlueprintHierarchy", "Refresh Blueprint"))]]]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [SNew(STextBlock)
            .Text_Lambda([this]() { return FText::FromString(InnerSourceStatusMessage); })
            .ColorAndOpacity_Lambda([this]()
            {
                if (!GetBlueprintHierarchyError().IsEmpty())
                {
                    return FStyleColors::Error;
                }
                const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                const bool bHierarchyReady = Layer != nullptr &&
                    !Layer->BlueprintSource.BlueprintClass.IsNull() &&
                    IsBlueprintHierarchyCurrent();
                return bHierarchyReady && !HasBlueprintTargetCandidate()
                    ? FStyleColors::Error
                    : FStyleColors::Foreground;
            })
            .AutoWrapText(true)]
        + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
        [SNew(SBox)
            .Visibility_Lambda([this]()
            {
                return HasBlueprintTargetCandidate()
                    ? EVisibility::Visible
                    : EVisibility::Collapsed;
            })
            [BuildBlueprintHierarchySection()]]
        + SVerticalBox::Slot()
            .AutoHeight()
        [SNew(SBox)
            .Visibility_Lambda([this]()
            {
                return HasBlueprintTargetCandidate()
                    ? EVisibility::Visible
                    : EVisibility::Collapsed;
            })
            [BuildBlueprintSourcePrioritySection()]];
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildBlueprintHierarchySection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
        [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("BlueprintMeshHierarchy", "Blueprint Skeletal Mesh Hierarchy"))]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox)
            .HeightOverride(230.0f)
            [SAssignNew(BlueprintHierarchyListView,
                SListView<TSharedPtr<FDWCTransparencyBlueprintMeshComponentMetadata>>)
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
              "Select the target or a source in the viewport. Space switches Move/Rotate, F focuses the selection, Home frames all, and Alt-click cycles overlapping meshes."))]
        + SVerticalBox::Slot().AutoHeight()
          [BuildExternalMeshSourcePrioritySection()]
        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)
          [BuildExternalSourceTransformSection()];
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
                .OnGenerateRow(this, &SWetClothingTransparencyBakePanel::GenerateExternalSourcePriorityRow)
                .OnSelectionChanged(
                    this,
                    &SWetClothingTransparencyBakePanel::HandleExternalSourceListSelectionChanged)]];
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
               .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .ToolTipText(LOCTEXT("ToggleExternalSourceVisibility", "Show or hide this source in the editor preview."))
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleToggleExternalSourceVisibilityClicked,
                 Source != nullptr ? Source->SourceGuid : FGuid())
             [SNew(SImage).Image_Lambda([this, Source]()
             {
                 return FAppStyle::GetBrush(Source != nullptr &&
                         PlacementSession->IsSourceHidden(Source->SourceGuid)
                     ? TEXT("Icons.Hidden")
                     : TEXT("Icons.Visible"));
             })]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .ToolTipText(LOCTEXT("ToggleExternalSourceSolo", "Show only this source with the target mesh."))
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleToggleExternalSourceSoloClicked,
                 Source != nullptr ? Source->SourceGuid : FGuid())
             [SNew(STextBlock)
                .Text(LOCTEXT("ExternalSourceSolo", "S"))
                .ColorAndOpacity_Lambda([this, Source]()
                {
                    return Source != nullptr && PlacementSession->IsSourceSolo(Source->SourceGuid)
                        ? FStyleColors::AccentBlue
                        : FStyleColors::Foreground;
                })]]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
             .ToolTipText(LOCTEXT("ToggleExternalSourceLock", "Lock or unlock this source placement."))
             .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleToggleExternalSourceLockClicked,
                 Source != nullptr ? Source->SourceGuid : FGuid())
             [SNew(SImage).Image_Lambda([this, Source]()
             {
                 return FAppStyle::GetBrush(Source != nullptr &&
                         PlacementSession->IsSourceLocked(Source->SourceGuid)
                     ? TEXT("Icons.Lock")
                     : TEXT("Icons.Unlock"));
             })]]
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
                    ? UE::DWCEditor::TransparencyPanel::GetBlueprintSourceRoleLabel(*Role)
                    : LOCTEXT("MissingExternalSourceRole", "Missing"));
            })
            .OnSelectionChanged(this, &SWetClothingTransparencyBakePanel::HandleExternalSourceRoleChanged, PriorityIndex)
            [SNew(STextBlock).Text_Lambda([this, PriorityIndex]()
            {
                const FWetClothingTransparencyLayerData* SelectedLayer = GetSelectedLayer();
                return SelectedLayer != nullptr &&
                    SelectedLayer->ExternalMeshSource.SourcePriority.IsValidIndex(PriorityIndex)
                    ? UE::DWCEditor::TransparencyPanel::GetBlueprintSourceRoleLabel(
                        SelectedLayer->ExternalMeshSource.SourcePriority[PriorityIndex].Role)
                    : LOCTEXT("MissingExternalSourceRoleLabel", "Missing");
            })]]]
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

FReply SWetClothingTransparencyBakePanel::HandleAlignSelectedPlacementToTargetClicked()
{
    if (PreviewViewport.IsValid() && PlacementSession->GetSelection().IsSource())
    {
        FTransform Transform = PreviewViewport->GetSelectedPlacementTransform();
        Transform.SetTranslation(FVector::ZeroVector);
        PreviewViewport->SetSelectedPlacementTransform(Transform, true);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleResetSelectedPlacementClicked()
{
    if (PreviewViewport.IsValid() &&
        PlacementSession->GetSelection().Type != EDWCTransparencyPlacementSelectionType::None)
    {
        PreviewViewport->SetSelectedPlacementTransform(FTransform::Identity, true);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleFocusSelectedPlacementClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusSelectedPlacement(false);
    }
    return FReply::Handled();
}

FReply SWetClothingTransparencyBakePanel::HandleFocusAllPlacementsClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusType3Assembly(false);
    }
    return FReply::Handled();
}

TSharedRef<SWidget> SWetClothingTransparencyBakePanel::BuildExternalSourceTransformSection()
{
    using FPlacementTransformInput = SAdvancedTransformInputBox<FTransform>;

    const auto GetTransform = [this]() -> TOptional<FTransform>
    {
        return PreviewViewport.IsValid() &&
                PlacementSession->GetSelection().Type !=
                    EDWCTransparencyPlacementSelectionType::None
            ? TOptional<FTransform>(PreviewViewport->GetSelectedPlacementTransform())
            : TOptional<FTransform>();
    };
    const auto IsTransformEditable = [this]()
    {
        return PreviewViewport.IsValid() &&
            PlacementSession->GetSelection().Type !=
                EDWCTransparencyPlacementSelectionType::None &&
            !PlacementSession->IsSelectionLocked();
    };
    const auto DiffersFromDefault = [this](const ESlateTransformComponent::Type Component)
    {
        if (!PreviewViewport.IsValid() ||
            PlacementSession->GetSelection().Type ==
                EDWCTransparencyPlacementSelectionType::None)
        {
            return false;
        }
        const FTransform Transform = PreviewViewport->GetSelectedPlacementTransform();
        switch (Component)
        {
        case ESlateTransformComponent::Location:
            return !Transform.GetTranslation().IsNearlyZero();
        case ESlateTransformComponent::Rotation:
            return !Transform.GetRotation().Equals(FQuat::Identity);
        case ESlateTransformComponent::Scale:
            return false;
        case ESlateTransformComponent::Max:
        default:
            return !Transform.GetTranslation().IsNearlyZero() ||
                !Transform.GetRotation().Equals(FQuat::Identity);
        }
    };
    const auto ResetComponent = [this](const ESlateTransformComponent::Type Component)
    {
        if (!PreviewViewport.IsValid())
        {
            return;
        }
        FTransform Transform = PreviewViewport->GetSelectedPlacementTransform();
        if (Component == ESlateTransformComponent::Location ||
            Component == ESlateTransformComponent::Max)
        {
            Transform.SetTranslation(FVector::ZeroVector);
        }
        if (Component == ESlateTransformComponent::Rotation ||
            Component == ESlateTransformComponent::Max)
        {
            Transform.SetRotation(FQuat::Identity);
        }
        Transform.SetScale3D(FVector::OneVector);
        PreviewViewport->SetSelectedPlacementTransform(Transform, true);
    };

    const TSharedRef<SWidget> EditableTransform =
        SNew(FPlacementTransformInput)
        .ConstructLocation(true)
        .ConstructRotation(true)
        .ConstructScale(false)
        .ShowInlineLabels(true)
        .AllowSpin(true)
        .AllowEditRotationRepresentation(false)
        .DisplayScaleLock(false)
        .DisplayRelativeWorld(false)
        .Transform_Lambda(GetTransform)
        .IsEnabled_Lambda(IsTransformEditable)
        .OnTransformChanged_Lambda([this](const FTransform Transform)
        {
            if (PreviewViewport.IsValid())
            {
                PreviewViewport->SetSelectedPlacementTransform(Transform, false);
            }
        })
        .OnNumericValueChanged_Lambda(
            [this](
                const ESlateTransformComponent::Type Component,
                const ESlateRotationRepresentation::Type Representation,
                const ESlateTransformSubComponent::Type SubComponent,
                const double Value)
            {
                if (!PreviewViewport.IsValid())
                {
                    return;
                }
                FTransform Transform = PreviewViewport->GetSelectedPlacementTransform();
                FPlacementTransformInput::ApplyNumericValueChange(
                    Transform,
                    Value,
                    Component,
                    Representation,
                    SubComponent);
                PreviewViewport->SetSelectedPlacementTransform(Transform, false);
            })
        .OnTransformCommitted_Lambda(
            [this](const FTransform Transform, ETextCommit::Type)
            {
                if (PreviewViewport.IsValid())
                {
                    PreviewViewport->SetSelectedPlacementTransform(Transform, true);
                }
            })
        .OnNumericValueCommitted_Lambda(
            [this](
                const ESlateTransformComponent::Type Component,
                const ESlateRotationRepresentation::Type Representation,
                const ESlateTransformSubComponent::Type SubComponent,
                const double Value,
                ETextCommit::Type)
            {
                if (!PreviewViewport.IsValid())
                {
                    return;
                }
                FTransform Transform = PreviewViewport->GetSelectedPlacementTransform();
                FPlacementTransformInput::ApplyNumericValueChange(
                    Transform,
                    Value,
                    Component,
                    Representation,
                    SubComponent);
                PreviewViewport->SetSelectedPlacementTransform(Transform, true);
            })
        .OnEndSliderMovement_Lambda(
            [this](ESlateTransformComponent::Type,
                ESlateRotationRepresentation::Type,
                ESlateTransformSubComponent::Type,
                double)
            {
                if (PreviewViewport.IsValid())
                {
                    PreviewViewport->SetSelectedPlacementTransform(
                        PreviewViewport->GetSelectedPlacementTransform(),
                        true);
                }
            })
        .OnResetToDefault_Lambda(ResetComponent)
        .DiffersFromDefault_Lambda(DiffersFromDefault);

    const TSharedRef<SWidget> ReadOnlyScale =
        SNew(FPlacementTransformInput)
        .ConstructLocation(false)
        .ConstructRotation(false)
        .ConstructScale(true)
        .ShowInlineLabels(true)
        .AllowSpin(false)
        .DisplayScaleLock(true)
        .Transform(FTransform::Identity)
        .IsEnabled(false);

    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
          [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("ExternalPlacementTransform", "Transform"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
          [SNew(SHorizontalBox)
           + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
             [SNew(STextBlock).Text_Lambda([this]()
             {
                 const FDWCTransparencyPlacementSelection& Selection = PlacementSession->GetSelection();
                 if (Selection.Type == EDWCTransparencyPlacementSelectionType::Target)
                 {
                     return LOCTEXT("SelectedPlacementTarget", "Selected: Target Mesh (preview assembly)");
                 }
                 const FWetClothingTransparencyLayerData* Layer = GetSelectedLayer();
                 const FWetClothingTransparencyExternalMeshEntry* Source = Layer != nullptr && Selection.IsSource()
                     ? Layer->ExternalMeshSource.SourcePriority.FindByPredicate(
                         [&Selection](const FWetClothingTransparencyExternalMeshEntry& Entry)
                         {
                             return Entry.SourceGuid == Selection.SourceGuid;
                         })
                     : nullptr;
                 return Source != nullptr && Source->SkeletalMesh != nullptr
                     ? FText::Format(LOCTEXT("SelectedPlacementSource", "Selected: {0}"),
                         FText::FromString(Source->SkeletalMesh->GetName()))
                     : LOCTEXT("SelectedPlacementNone", "Select the target or a source mesh in the viewport.");
             })]
           + SHorizontalBox::Slot().AutoWidth()
             [SNew(SButton)
                .Text(LOCTEXT("SelectPlacementTarget", "Select Target"))
                .OnClicked_Lambda([this]()
                {
                    if (PreviewViewport.IsValid())
                    {
                        PreviewViewport->SetPlacementSelection(
                            FDWCTransparencyPlacementSelection::Target());
                    }
                    return FReply::Handled();
                })]]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,2)[EditableTransform]
        + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[ReadOnlyScale]
        + SVerticalBox::Slot().AutoHeight()
          [SNew(SHorizontalBox)
           + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
             [SNew(SButton).Text(LOCTEXT("PlacementAlignTarget", "Align to Target Origin"))
                .IsEnabled_Lambda([this]() { return PlacementSession->GetSelection().IsSource(); })
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleAlignSelectedPlacementToTargetClicked)]
           + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
             [SNew(SButton).Text(LOCTEXT("PlacementReset", "Reset"))
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleResetSelectedPlacementClicked)]
           + SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)
             [SNew(SButton).Text(LOCTEXT("PlacementFocus", "Focus"))
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleFocusSelectedPlacementClicked)]
           + SHorizontalBox::Slot().AutoWidth()
             [SNew(SButton).Text(LOCTEXT("PlacementFocusAll", "Focus All"))
                .OnClicked(this, &SWetClothingTransparencyBakePanel::HandleFocusAllPlacementsClicked)]];
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
        .OwnerAsset(Asset)
        .CacheStore(CacheStore)
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
                        const FWetClothingTransparencyManualColorSource& Current =
                            TargetLayer.ManualColorSource;
                        if (Current.SourceMode ==
                                EDWCTransparencyManualRevealSourceMode::UVIslandAverage &&
                            Current.BaseRevealColor.Equals(Selection.Color) &&
                            Current.SampledColorTexture.Get() == Selection.Texture.Get() &&
                            Current.SampledMaterialSlotIndex == Selection.MaterialSlotIndex &&
                            Current.SampledUVChannelIndex == Selection.UVChannelIndex &&
                            Current.SampledUVIslandID == Selection.UVIslandID)
                        {
                            return false;
                        }
                        TargetLayer.ManualColorSource.SourceMode =
                            EDWCTransparencyManualRevealSourceMode::UVIslandAverage;
                        TargetLayer.ManualColorSource.BaseRevealColor = Selection.Color;
                        TargetLayer.ManualColorSource.SampledColorTexture = Selection.Texture.Get();
                        TargetLayer.ManualColorSource.SampledMaterialSlotIndex = Selection.MaterialSlotIndex;
                        TargetLayer.ManualColorSource.SampledUVChannelIndex = Selection.UVChannelIndex;
                        TargetLayer.ManualColorSource.SampledUVIslandID = Selection.UVIslandID;
                        return true;
                    },
                    EDWCTransparencyPanelRefreshFlags::Model |
                        EDWCTransparencyPanelRefreshFlags::Viewport);
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
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::Viewport);

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
    return UE::DWCEditor::TransparencyPanel::RadiusUVToSizeCm(GetRevealPaintSettingsFromSession().RadiusUV);
}

FText SWetClothingTransparencyBakePanel::GetRevealPaintSizeDisplayText() const
{
    return UE::DWCEditor::TransparencyPanel::FormatBrushSizeCm(GetRevealPaintSizeCm());
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
        FText::FromString(
            UE::DWCEditor::TransparencyPanel::GetRevealColorStrokeModeLabel(Stroke->BrushMode)),
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
            if (FMath::IsNearlyEqual(
                    TargetLayer.ManualColorSource.InitialTransparencyAlpha,
                    ClampedValue))
            {
                return false;
            }
            TargetLayer.ManualColorSource.InitialTransparencyAlpha = ClampedValue;
            return true;
        },
        EDWCTransparencyPanelRefreshFlags::Model |
            EDWCTransparencyPanelRefreshFlags::Viewport);

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
                            const float ClampedValue = FMath::Max(MinimumValue, NewValue);
                            if (FMath::IsNearlyEqual(TargetLayer.RaySettings.*Member, ClampedValue))
                            {
                                return false;
                            }
                            TargetLayer.RaySettings.*Member = ClampedValue;
                            return true;
                        },
                        EDWCTransparencyPanelRefreshFlags::Model |
                            EDWCTransparencyPanelRefreshFlags::Viewport);
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
                            if (Layer.OutputResolutionMode == Mode)
                            {
                                return false;
                            }
                            Layer.OutputResolutionMode = Mode;
                            return true;
                        },
                        EDWCTransparencyPanelRefreshFlags::Model |
                            EDWCTransparencyPanelRefreshFlags::Viewport);
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
                            if (Layer.OutputResolutionMode == EDWCTransparencyOutputResolutionMode::Override &&
                                FDWCTransparencyResolutionResolver::NormalizeResolution(
                                    Layer.OutputResolutionOverride) == ResolutionOption)
                            {
                                return false;
                            }
                            Layer.OutputResolutionMode = EDWCTransparencyOutputResolutionMode::Override;
                            Layer.OutputResolutionOverride = ResolutionOption;
                            return true;
                        },
                        EDWCTransparencyPanelRefreshFlags::Model |
                            EDWCTransparencyPanelRefreshFlags::Viewport);
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
                    .MinValue(UE::DWCEditor::TransparencyPanel::MinBrushSizeCm)
                    .MaxValue(UE::DWCEditor::TransparencyPanel::MaxBrushSizeCm)
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
        0.5f, 0.7f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f,
        6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 15.0f, 17.0f, 20.0f,
        25.0f, 30.0f, 40.0f
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
                            .Text(UE::DWCEditor::TransparencyPanel::FormatBrushSizeCm(PresetSizeCm))]]];
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
            (AutoBakeResults.Contains(GetSelectedLayerGuid()) &&
             AutoBakeResults[GetSelectedLayerGuid()].IsValid() &&
             AutoBakeResults[GetSelectedLayerGuid()]->bIsFinalBakedBaseline);
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
                UE::DWCEditor::TransparencyPanel::GetStrokeModeLabel(Stroke.BrushMode),
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
        const FWetClothingBakedTransparencyMap* BakedMap = UE::DWCEditor::TransparencyPanel::FindExactBakedMap(Asset, Layer);
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
    const TSharedPtr<FDWCTransparencySourcePayload>* WorkingResult = AutoBakeResults.Find(GetSelectedLayerGuid());
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
            .PreviewModeLifetime(PreviewModeLifetime)
            .RenderUploadQueue(RenderUploadQueue)
            .PlacementSession(PlacementSession)]];
    if (AuthoringController.IsValid())
    {
        AuthoringController->AttachViewport(PreviewViewport);
        PreviewViewport->SetAuthoringController(AuthoringController);
    }
    PreviewViewport->SetExternalSourceTransformCommittedDelegate(
        FDWCTransparencyExternalSourceTransformCommitted::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandleExternalSourceTransformCommitted));
    PreviewViewport->SetPlacementSelectionChangedDelegate(
        FDWCTransparencyPlacementSelectionChanged::CreateSP(
            this,
            &SWetClothingTransparencyBakePanel::HandlePlacementSelectionChanged));
    PreviewViewport->SetPlacementSelection(PlacementSession->GetSelection());
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
    PushBlueprintHierarchySnapshotToPreview();
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
    PreviewViewport->SetPlacementHelpVisible(
        CurrentStage == EDWCTransparencyEditorStage::MapGeneration &&
        SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh);
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
        AutoBakeResults[Layer->LayerGuid].IsValid();
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

    PreviewViewport->SetTransparencyEditContext(GetSelectedLayerGuid(),
        Layer != nullptr ? Layer->TargetSurface.OuterMaterialSlotIndex : SelectedMaterialSlotIndex,
        GetTransparencyDataUVChannel(),
        Layer != nullptr ? Layer->TargetSurface.UVAddressMode : EDWCTransparencyUVAddressMode::Clamp,
        PreviewContext.PaintTarget,
        PreviewContext.bEnableRevealColorPainting || PreviewContext.bEnableFinalAlphaPainting);
    PreviewViewport->SetPreviewMode(PreviewContext.PreviewMode);

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
