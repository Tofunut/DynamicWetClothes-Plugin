#include "WetClothingAssetEditor.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DWCEditorStyle.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Common/Asset/WetClothingAssetFactory.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"
#include "WetClothing/Common/MeshPreparation/DWCDataUVBuildService.h"
#include "SWetClothingAssetEditorPanel.h"
#include "DetailsViewArgs.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/MaterialInterface.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Styling/ToolBarStyle.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditor"

namespace
{
    const FCheckBoxStyle& GetWetClothingModeToggleStyle()
    {
        static const FSlateRoundedBoxBrush UncheckedBrush(FStyleColors::Header, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedHoveredBrush(FStyleColors::Hover, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedPressedBrush(FStyleColors::Recessed, 4.0f);
        static const FSlateRoundedBoxBrush CheckedBrush(FStyleColors::AccentBlue, 4.0f);
        static const FSlateRoundedBoxBrush CheckedHoveredBrush(FStyleColors::PrimaryHover, 4.0f);

        static const FCheckBoxStyle Style =
            FCheckBoxStyle(FAppStyle::Get().GetWidgetStyle<FToolBarStyle>(TEXT("AssetEditorToolbar")).ToggleButton)
                .SetUncheckedImage(UncheckedBrush)
                .SetUncheckedHoveredImage(UncheckedHoveredBrush)
                .SetUncheckedPressedImage(UncheckedPressedBrush)
                .SetCheckedImage(CheckedBrush)
                .SetCheckedHoveredImage(CheckedHoveredBrush)
                .SetCheckedPressedImage(CheckedBrush)
                .SetPadding(FMargin(0.0f));

        return Style;
    }

    FText BuildAssetSetupSkeletalMeshUVChannelSummary(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 PreferredDWCDataUVChannelIndex,
        const bool bModifySourceMesh)
    {
        if (Mesh == nullptr)
        {
            return LOCTEXT("AssetSetupUVSummaryNoMesh", "Source UV Channels: unavailable.");
        }

        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
        {
            return LOCTEXT("AssetSetupUVSummaryNoRenderData", "Source UV Channels: render LOD data is unavailable.");
        }

        FString Summary = FString::Printf(
            TEXT("%s\n\nSource UV Channels\nOriginal UV: UV%d\nPreferred DWC Data UV: UV%d\n"),
            bModifySourceMesh
                ? TEXT("Mode: Modify Source Mesh. DWC will write the generated Data UV channel directly into the Source Skeletal Mesh asset.")
                : TEXT("Mode: Duplicate Mesh. DWC will create or update a prepared skeletal mesh copy and leave the Source Skeletal Mesh untouched."),
            OriginalUVChannelIndex,
            PreferredDWCDataUVChannelIndex);

        for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
        {
            const int32 UVChannelCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
            Summary += FString::Printf(TEXT("LOD%d: %d channel(s)"), LODIndex, UVChannelCount);
            if (UVChannelCount > 0)
            {
                Summary += TEXT(" - ");
                for (int32 UVIndex = 0; UVIndex < UVChannelCount; ++UVIndex)
                {
                    if (UVIndex > 0)
                    {
                        Summary += TEXT(", ");
                    }
                    Summary += FString::Printf(TEXT("UV%d"), UVIndex);
                }
            }
            Summary += TEXT("\n");
        }

        Summary += TEXT("Named skeletal-mesh UV channels are shown when the engine exposes them; otherwise DWC displays UV indices.");
        return FText::FromString(Summary);
    }

    int32 GetAssetSetupSkeletalMeshUVChannelCount(const USkeletalMesh* Mesh, const int32 LODIndex)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return 0;
        }

        return static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
    }

    int32 GetAssetSetupDefaultDWCDataUVChannelIndex(const USkeletalMesh* Mesh, const int32 OriginalUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        return UVChannelCount > 0 ? FMath::Clamp(UVChannelCount, 0, 7) : FMath::Clamp(OriginalUVChannelIndex + 1, 0, 7);
    }

    FText BuildAssetSetupDWCDataUVTargetText(const bool bModifySourceMesh)
    {
        return bModifySourceMesh
            ? LOCTEXT(
                  "AssetSetupModifySourceTargetText",
                  "Mode: Modify Source Mesh. DWC will write the generated Data UV channel directly into the Source Skeletal Mesh asset.")
            : LOCTEXT(
                  "AssetSetupDuplicateMeshTargetText",
                  "Mode: Duplicate Mesh. DWC will create or update a prepared skeletal mesh copy and leave the Source Skeletal Mesh untouched.");
    }

    bool HasWrinkleValidationData(const UWetClothingAsset& Asset)
    {
        if (!Asset.WrinkleData.BakedWrinkleMaps.IsEmpty())
        {
            return true;
        }
        for (const FWetWrinklePatchStroke& Stroke : Asset.WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.PatchPlacements.IsEmpty())
            {
                return true;
            }
        }
        return false;
    }

    FString BakeStatusToValidationString(const EDWCBakeStatus Status)
    {
        switch (Status)
        {
        case EDWCBakeStatus::Disabled: return TEXT("Disabled");
        case EDWCBakeStatus::Required: return TEXT("Required");
        case EDWCBakeStatus::Valid: return TEXT("Valid");
        case EDWCBakeStatus::ValidWithDiagnostics: return TEXT("Valid With Diagnostics");
        case EDWCBakeStatus::OutOfDate: return TEXT("Out of Date");
        case EDWCBakeStatus::Failed: return TEXT("Failed");
        default: return TEXT("Unknown");
        }
    }

    bool IsValidationActionRequiredStatus(const EDWCBakeStatus Status)
    {
        return Status == EDWCBakeStatus::Required ||
            Status == EDWCBakeStatus::OutOfDate ||
            Status == EDWCBakeStatus::Failed;
    }

    bool HasValidationFailedState(const FDWCAssetBakeState& State)
    {
        return State.GeneratedDataUV == EDWCBakeStatus::Failed ||
            State.OriginalUVTopology == EDWCBakeStatus::Failed ||
            State.CPURuntimeData == EDWCBakeStatus::Failed ||
            State.GPURuntimeData == EDWCBakeStatus::Failed ||
            State.GPUMaps == EDWCBakeStatus::Failed ||
            State.WrinkleMaps == EDWCBakeStatus::Failed ||
            State.TransparencyMaps == EDWCBakeStatus::Failed;
    }

    struct FDWCValidationActionItem
    {
        FText Name;
        FText Status;
        FText Action;
        bool bFailed = false;
    };

    void AddValidationActionIfRequired(
        TArray<FDWCValidationActionItem>& OutActions,
        const FText& Name,
        const EDWCBakeStatus Status,
        const FText& Action,
        const bool bSavePending = false)
    {
        if (IsValidationActionRequiredStatus(Status) || bSavePending)
        {
            FDWCValidationActionItem Item;
            Item.Name = Name;
            Item.Status = bSavePending && DWCBuildStatus::IsUsable(Status)
                              ? LOCTEXT("ValidationSaveRequiredStatus", "Save Required")
                              : FText::FromString(BakeStatusToValidationString(Status));
            Item.Action = bSavePending
                              ? LOCTEXT("ValidationPersistRuntimeDataAction", "Save the asset to persist the current data.")
                              : Action;
            Item.bFailed = Status == EDWCBakeStatus::Failed;
            OutActions.Add(MoveTemp(Item));
        }
    }

    TArray<FDWCValidationActionItem> GetValidationActionItems(const UWetClothingAsset& Asset)
    {
        TArray<FDWCValidationActionItem> Result;
#if WITH_EDITORONLY_DATA
        const FDWCAssetBakeState& State = Asset.GetBakeState();
        const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();

        AddValidationActionIfRequired(
            Result,
            LOCTEXT("ValidationGeneratedDataUV", "DWC Data UV"),
            State.GeneratedDataUV,
            LOCTEXT("ValidationGeneratedDataUVAction", "Use Rebuild DWC Data UV on the toolbar to rebuild it."));
        AddValidationActionIfRequired(
            Result,
            LOCTEXT("ValidationOriginalUVTopology", "Original UV Topology"),
            State.OriginalUVTopology,
            LOCTEXT("ValidationOriginalUVTopologyAction", "Rebuild DWC Data UV."));

        if ((Setup.bBuildCPUVertexSimulationData || Asset.HasCPURuntimeDataPayload()) &&
            State.CPURuntimeData != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationCPURuntimeData", "CPU Runtime Data"),
                State.CPURuntimeData,
                LOCTEXT("ValidationSaveAssetAction", "Save the asset to rebuild it."),
                Asset.IsBakeOutputSavePending(DWCBakeOutput::CPURuntimeData));
        }
        if ((Setup.bBuildGPUWetnessMapSimulationData || Asset.HasGPURuntimeDataPayload()) &&
            State.GPURuntimeData != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationGPURuntimeData", "GPU Runtime Data"),
                State.GPURuntimeData,
                LOCTEXT("ValidationSaveAssetAction", "Save the asset to rebuild it."),
                Asset.IsBakeOutputSavePending(DWCBakeOutput::GPURuntimeData));
        }
        if ((Setup.bBuildGPUWetnessMapSimulationData || Asset.HasGPUMapDataPayload()) &&
            State.GPUMaps != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationGPUMaps", "GPU Simulation Maps"),
                State.GPUMaps,
                LOCTEXT("ValidationBakeMapsAction", "Use Bake Maps to rebuild them."),
                Asset.IsBakeOutputSavePending(DWCBakeOutput::GPUMaps));
        }
        if (HasWrinkleValidationData(Asset) && State.WrinkleMaps != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationWrinkleMaps", "Wrinkle Maps"),
                State.WrinkleMaps,
                LOCTEXT("ValidationBakeMapsAction", "Use Bake Maps to rebuild them."));
        }
        if (!Asset.TransparencyData.SourceBlueprintClass.IsNull() && State.TransparencyMaps != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationTransparencyMaps", "Transparency Maps"),
                State.TransparencyMaps,
                LOCTEXT("ValidationBakeMapsAction", "Use Bake Maps to rebuild them."));
        }

        TArray<FString> GeneratedMaterialMessages;
        FWetClothingMaterialSetup::ValidateGeneratedMaterialOverrides(&Asset, GeneratedMaterialMessages);
        if (!GeneratedMaterialMessages.IsEmpty())
        {
            FDWCValidationActionItem Item;
            Item.Name = LOCTEXT("ValidationGeneratedMaterials", "Generated Materials");
            Item.Status = FText::Format(
                LOCTEXT("ValidationGeneratedMaterialsStatus", "{0} issue(s)"),
                FText::AsNumber(GeneratedMaterialMessages.Num()));
            Item.Action = LOCTEXT("ValidationGeneratedMaterialsAction", "Use Generate Materials.");
            Result.Add(MoveTemp(Item));
        }
#endif
        return Result;
    }

    TArray<FString> GetValidationFailureDetails(const UWetClothingAsset& Asset)
    {
        TArray<FString> Result;
#if WITH_EDITORONLY_DATA
        const FDWCAssetBakeState& State = Asset.GetBakeState();
        if (HasValidationFailedState(State) && !State.LastFailure.IsEmpty())
        {
            Result.Add(State.LastFailure);
        }
#endif
        return Result;
    }

    bool EnsureGPURuntimeDataReadyForMapBake(UWetClothingAsset* Asset, FString& OutFailure)
    {
        OutFailure.Reset();
        if (Asset == nullptr)
        {
            OutFailure = TEXT("The Wet Clothing Asset is no longer valid.");
            return false;
        }

        const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
        if (!Setup.bBuildGPUWetnessMapSimulationData)
        {
            return true;
        }

        Asset->RefreshBakeState(false);
        if (DWCBuildStatus::IsUsable(Asset->GetBakeState().GPURuntimeData))
        {
            return true;
        }

        FString RuntimeError;
        if (!Asset->RebuildGPURuntimeData(&RuntimeError))
        {
            OutFailure = RuntimeError.IsEmpty()
                             ? TEXT("GPU Runtime Data could not be rebuilt before map bake.")
                             : FString::Printf(TEXT("GPU Runtime Data: %s"), *RuntimeError);
            return false;
        }

        Asset->RefreshBakeState(false);
        if (!DWCBuildStatus::IsUsable(Asset->GetBakeState().GPURuntimeData))
        {
            const FString LastFailure = Asset->GetBakeState().LastFailure;
            OutFailure = LastFailure.IsEmpty()
                             ? TEXT("GPU Runtime Data is still missing or out of date after rebuilding.")
                             : FString::Printf(TEXT("GPU Runtime Data: %s"), *LastFailure);
            return false;
        }

        return true;
    }

    TSharedRef<SWidget> BuildValidationSectionHeader(const FText& Title, const FText& Detail = FText::GetEmpty())
    {
        TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
        Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(Title)
            .TextStyle(FAppStyle::Get(), TEXT("DetailsView.CategoryTextStyle"))
        ];

        if (!Detail.IsEmpty())
        {
            Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(8.0f, 1.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Detail)
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        return Header;
    }

    TSharedRef<SWidget> BuildValidationActionRow(const FDWCValidationActionItem& Item)
    {
        const FLinearColor BorderColor = Item.bFailed
            ? FLinearColor(0.35f, 0.04f, 0.03f, 1.0f)
            : FLinearColor(0.30f, 0.20f, 0.04f, 1.0f);
        const FLinearColor PillColor = Item.bFailed
            ? FLinearColor(0.55f, 0.06f, 0.04f, 1.0f)
            : FLinearColor(0.55f, 0.35f, 0.05f, 1.0f);

        return SNew(SBorder)
            .Padding(FMargin(10.0f, 8.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(BorderColor)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Top)
                .Padding(0.0f, 1.0f, 8.0f, 0.0f)
                [
                    SNew(SImage)
                    .Image(FAppStyle::GetBrush(Item.bFailed ? TEXT("Icons.ErrorWithColor") : TEXT("Icons.WarningWithColor")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(Item.Name)
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(SBorder)
                            .Padding(FMargin(8.0f, 2.0f))
                            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                            .BorderBackgroundColor(PillColor)
                            [
                                SNew(STextBlock)
                                .Text(Item.Status)
                                .ColorAndOpacity(FSlateColor(FLinearColor::White))
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(Item.Action)
                        .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> BuildValidationActionSection(const TArray<FDWCValidationActionItem>& ActionItems)
    {
        TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
        Content->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [
            BuildValidationSectionHeader(
                LOCTEXT("ValidationActionSection", "Action Required"),
                FText::Format(LOCTEXT("ValidationActionCount", "{0} item(s)"), FText::AsNumber(ActionItems.Num())))
        ];

        if (ActionItems.IsEmpty())
        {
            Content->AddSlot()
            .AutoHeight()
            [
                SNew(SBorder)
                .Padding(FMargin(10.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(FLinearColor(0.06f, 0.18f, 0.08f, 1.0f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ValidationNoActionRequired", "No action required."))
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ]
            ];
        }
        else
        {
            for (const FDWCValidationActionItem& Item : ActionItems)
            {
                Content->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                [
                    BuildValidationActionRow(Item)
                ];
            }
        }

        return Content;
    }

    TSharedRef<SWidget> BuildValidationFailureSection(const TArray<FString>& FailureDetails)
    {
        if (FailureDetails.IsEmpty())
        {
            return SNullWidget::NullWidget;
        }

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 4.0f, 0.0f, 8.0f)
            [
                BuildValidationSectionHeader(LOCTEXT("ValidationFailureDetailsSection", "Failure Details"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .Padding(FMargin(10.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(FLinearColor(0.30f, 0.04f, 0.03f, 1.0f))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Join(FailureDetails, TEXT("\n"))))
                    .AutoWrapText(true)
                ]
            ];
    }

    void AddValidationDiagnosticRow(TSharedRef<SGridPanel> Grid, int32& RowIndex, const FText& Label, const FText& Value)
    {
        Grid->AddSlot(0, RowIndex)
        .Padding(0.0f, 3.0f, 18.0f, 3.0f)
        [
            SNew(STextBlock)
            .Text(Label)
            .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
        ];
        Grid->AddSlot(1, RowIndex)
        .Padding(0.0f, 3.0f)
        .HAlign(HAlign_Right)
        [
            SNew(STextBlock)
            .Text(Value)
        ];
        ++RowIndex;
    }

    TSharedRef<SWidget> BuildValidationDiagnosticsSection(const FDWCTriangleValidationSummary& Summary, const FString& Examples)
    {
        TSharedRef<SGridPanel> Grid = SNew(SGridPanel);
        int32 RowIndex = 0;
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationTotalWettableTriangles", "Total wettable triangles"), FText::AsNumber(Summary.TotalWettableTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationCPUUsableTriangles", "CPU usable triangles"), FText::AsNumber(Summary.CPUUsableTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationGPUUsableTriangles", "GPU usable triangles"), FText::AsNumber(Summary.GPUUsableTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationOriginalUVDegenerate", "Original UV unavailable/degenerate"), FText::AsNumber(Summary.DegenerateOriginalUVTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationDWCDataUVDegenerate", "DWC Data UV degenerate"), FText::AsNumber(Summary.DegenerateDWCDataUVTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationInvalidUV", "Invalid/out-of-range UV"), FText::AsNumber(Summary.InvalidUVTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("Validation3DDegenerate", "3D degenerate excluded"), FText::AsNumber(Summary.Degenerate3DTriangles));
        AddValidationDiagnosticRow(Grid, RowIndex, LOCTEXT("ValidationExampleTriangles", "Example triangle indices"), FText::FromString(Examples));

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 8.0f)
            [
                BuildValidationSectionHeader(LOCTEXT("ValidationDiagnosticsSection", "Mesh/UV Diagnostics"))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SBorder)
                .Padding(FMargin(10.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(FLinearColor(0.07f, 0.07f, 0.07f, 1.0f))
                [
                    Grid
                ]
            ];
    }

    TSharedRef<SWidget> BuildValidationDialogContent(
        const TSharedRef<SWindow>& DialogWindow,
        const FDWCTriangleValidationSummary& Summary,
        const TArray<FDWCValidationActionItem>& ActionItems,
        const TArray<FString>& FailureDetails,
        const bool bHasFailedState,
        const FString& Examples,
        const FOnClicked& OnResolveClicked)
    {
        const bool bHasActions = !ActionItems.IsEmpty();
        const bool bCanResolve = (bHasActions || bHasFailedState) && OnResolveClicked.IsBound();
        const TCHAR* HeaderIconName = bHasFailedState
            ? TEXT("Icons.ErrorWithColor")
            : (bHasActions ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor"));
        const FText HeaderSummary = bHasFailedState
            ? FText::Format(LOCTEXT("ValidationHeaderErrorSummary", "{0} data item(s) require action, including failed data."), FText::AsNumber(ActionItems.Num()))
            : (bHasActions
                ? FText::Format(LOCTEXT("ValidationHeaderWarningSummary", "{0} data item(s) require Save, Bake, or Rebuild."), FText::AsNumber(ActionItems.Num()))
                : LOCTEXT("ValidationHeaderSuccessSummary", "No action required."));
        const FSlateBrush* ResolveIconBrush = FAppStyle::GetOptionalBrush(
            FName(TEXT("PlacementBrowser.Icons.Lights")),
            nullptr,
            nullptr);
        if (ResolveIconBrush == nullptr)
        {
            ResolveIconBrush = FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
        }
        TSharedRef<SHorizontalBox> ButtonRow = SNew(SHorizontalBox);

        if (bCanResolve)
        {
            ButtonRow->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .ToolTipText(LOCTEXT("ValidationResolveTooltip", "Resolve validation warnings/errors by rebuilding, baking, and saving required generated data."))
                .OnClicked(OnResolveClicked)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(ResolveIconBrush)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ValidationDialogResolve", "Resolve"))
                    ]
                ]
            ];
        }

        ButtonRow->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .Text(LOCTEXT("ValidationDialogOK", "OK"))
            .OnClicked_Lambda([DialogWindow]()
            {
                DialogWindow->RequestDestroyWindow();
                return FReply::Handled();
            })
        ];

        return SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(16.0f, 14.0f, 16.0f, 12.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Top)
                    .Padding(0.0f, 2.0f, 10.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(HeaderIconName))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("ValidationResultsHeader", "Validation Results"))
                            .TextStyle(FAppStyle::Get(), TEXT("DetailsView.CategoryTextStyle"))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(HeaderSummary)
                            .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(16.0f, 0.0f, 16.0f, 0.0f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                    [
                        BuildValidationActionSection(ActionItems)
                    ]
                    + SScrollBox::Slot()
                    .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                    [
                        BuildValidationFailureSection(FailureDetails)
                    ]
                    + SScrollBox::Slot()
                    .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                    [
                        BuildValidationDiagnosticsSection(Summary, Examples)
                    ]
                    + SScrollBox::Slot()
                    .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ValidationDiagnosticsNote", "Mesh/UV diagnostics do not affect the toolbar warning icon or count. The source Skeletal Mesh was not modified."))
                        .AutoWrapText(true)
                        .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Right)
                .Padding(16.0f, 12.0f, 16.0f, 16.0f)
                [
                    ButtonRow
                ]
            ];
    }

    bool ShouldShowWetClothingAssetDetailProperty(const FPropertyAndParent& PropertyAndParent)
    {
        auto IsHeavyGeneratedProperty = [](const FName PropertyName)
        {
            return PropertyName == FName(TEXT("BakedGPUWetMapLODs")) ||
                   PropertyName == FName(TEXT("OriginalUVTopology")) ||
                   PropertyName == FName(TEXT("PrecomputedSimulationData")) ||
                   PropertyName == FName(TEXT("Triangles")) ||
                   PropertyName == FName(TEXT("VertexIncidentTriangles")) ||
                   PropertyName == FName(TEXT("MaterialSlots")) ||
                   PropertyName == FName(TEXT("TexelTriangleIDs")) ||
                   PropertyName == FName(TEXT("PackedTexelBarycentricXY")) ||
                   PropertyName == FName(TEXT("RestTexelAreas")) ||
                   PropertyName == FName(TEXT("ValidMask")) ||
                   PropertyName == FName(TEXT("SeamDestinations")) ||
                   PropertyName == FName(TEXT("SeamIncoming")) ||
                   PropertyName == FName(TEXT("Vertices")) ||
                   PropertyName == FName(TEXT("NeighborGraph")) ||
                   PropertyName == FName(TEXT("BoneOptimizationCache")) ||
                   PropertyName == FName(TEXT("Islands")) ||
                   PropertyName == FName(TEXT("TriangleIndices"));
        };

        if (IsHeavyGeneratedProperty(PropertyAndParent.Property.GetFName()))
        {
            return false;
        }

        for (const FProperty* ParentProperty : PropertyAndParent.ParentProperties)
        {
            if (ParentProperty != nullptr && IsHeavyGeneratedProperty(ParentProperty->GetFName()))
            {
                return false;
            }
        }

        return true;
    }

    enum class EWetClothingPendingCloseChoice : uint8
    {
        ResolveAndSave,
        CloseAnyway,
        Cancel
    };

    enum class EWetClothingAssetSetupDialogResult : uint8
    {
        Cancel,
        ApplySettings,
        RebuildGeneratedDataUV
    };

    EWetClothingAssetSetupDialogResult ShowWetClothingAssetSetupDialog(UWetClothingAsset& Asset, FDWCWetClothingAssetSetupSettings& OutSettings)
    {
        TStrongObjectPtr<UWetClothingAssetSetupSettingsObject> SetupObject(
            NewObject<UWetClothingAssetSetupSettingsObject>(GetTransientPackage()));
        SetupObject->InitializeFromSettings(Asset.GetSetupSettings());
        SetupObject->PreferredDWCDataUVChannelIndex = GetAssetSetupDefaultDWCDataUVChannelIndex(
            Asset.GetSourceSkeletalMesh(),
            Asset.GetOriginalUVChannelIndex());

        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        FDetailsViewArgs DetailsArgs;
        DetailsArgs.bAllowSearch = false;
        DetailsArgs.bHideSelectionTip = true;
        DetailsArgs.bLockable = false;
        DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
        TSharedRef<IDetailsView> SetupDetails = PropertyEditor.CreateDetailView(DetailsArgs);
        SetupDetails->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
        {
            return PropertyAndParent.Property.GetFName() != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, bModifySourceMeshForDWCDataUV);
        }));
        SetupDetails->SetObject(SetupObject.Get());

        EWetClothingAssetSetupDialogResult Result = EWetClothingAssetSetupDialogResult::Cancel;
        TSharedRef<SWindow> Dialog =
            SNew(SWindow)
            .Title(LOCTEXT("AssetSetupTitle", "Wet Clothing Asset Setup"))
            .SizingRule(ESizingRule::Autosized)
            .SupportsMaximize(false)
            .SupportsMinimize(false);

        const FText MeshInformation = FText::Format(
            LOCTEXT(
                "AssetSetupMeshInfo",
                "Mesh Information (Read Only)\n"
                "Source: {0}\n"
                "Original UV: UV{2}\n"
                "DWC Data UV LODs: {1}\n"
                "Simulation LOD: LOD{3}"),
            FText::FromString(GetNameSafe(Asset.GetSourceSkeletalMesh())),
            FText::AsNumber(Asset.GeneratedDataUVsPerLOD.Num()),
            FText::AsNumber(Asset.GetOriginalUVChannelIndex()),
            FText::AsNumber(Asset.GetSimulationLODIndex()));

        Dialog->SetContent(
            SNew(SBorder)
            .Padding(12.0f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 10)
                [
                    SNew(SBorder)
                    .Padding(8.0f)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    [SNew(STextBlock).Text(MeshInformation)]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 10)
                [
                    SNew(SBorder)
                    .Padding(8.0f)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    [
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text_Lambda([&Asset, SetupObject]()
                        {
                            return BuildAssetSetupSkeletalMeshUVChannelSummary(
                                Asset.GetSourceSkeletalMesh(),
                                Asset.GetOriginalUVChannelIndex(),
                                SetupObject->PreferredDWCDataUVChannelIndex,
                                SetupObject->bModifySourceMeshForDWCDataUV);
                        })
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 0, 0, 10)
                [
                    SNew(SBorder)
                    .Padding(8.0f)
                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(SCheckBox)
                            .IsChecked_Lambda([SetupObject]()
                            {
                                return SetupObject->bModifySourceMeshForDWCDataUV
                                    ? ECheckBoxState::Checked
                                    : ECheckBoxState::Unchecked;
                            })
                            .OnCheckStateChanged_Lambda([SetupObject](ECheckBoxState NewState)
                            {
                                SetupObject->bModifySourceMeshForDWCDataUV = NewState == ECheckBoxState::Checked;
                            })
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("AssetSetupModifySourceMeshCheckbox", "Modify Source Mesh"))
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text_Lambda([SetupObject]()
                            {
                                return BuildAssetSetupDWCDataUVTargetText(SetupObject->bModifySourceMeshForDWCDataUV);
                            })
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.05f, 0.05f)))
                            .Visibility_Lambda([SetupObject]()
                            {
                                return SetupObject->bModifySourceMeshForDWCDataUV
                                    ? EVisibility::Visible
                                    : EVisibility::Collapsed;
                            })
                            .Text(LOCTEXT(
                                "AssetSetupModifySourceMeshWarning",
                                "Warning: this modifies the Source Skeletal Mesh asset. Use Duplicate Mesh mode when the original asset should remain untouched."))
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SBox)
                    .WidthOverride(560.0f)
                    .HeightOverride(390.0f)
                    [SetupDetails]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0, 10, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .HAlign(HAlign_Left)
                    [
                        SNew(SButton)
                        .ToolTipText(LOCTEXT("RebuildGeneratedDataUVTooltip", "Generate or rebuild DWC Data UV payloads and Original-UV topology."))
                        .ContentPadding(FMargin(8.0f, 5.0f))
                        .OnClicked_Lambda([&Result, Dialog]()
                        {
                            Result = EWetClothingAssetSetupDialogResult::RebuildGeneratedDataUV;
                            Dialog->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                            [
                                SNew(SImage)
                                .DesiredSizeOverride(FVector2D(16.0f, 16.0f))
                                .Image(FAppStyle::Get().GetBrush(TEXT("Icons.Refresh")))
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("RebuildGeneratedDataUV", "Rebuild DWC Data UV"))
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNullWidget::NullWidget
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(0, 0, 6, 0)
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("SetupCancel", "Cancel"))
                        .OnClicked_Lambda([Dialog]()
                        {
                            Dialog->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .Text(LOCTEXT("ApplySetup", "Apply Settings"))
                        .OnClicked_Lambda([&Result, Dialog]()
                        {
                            Result = EWetClothingAssetSetupDialogResult::ApplySettings;
                            Dialog->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                    ]
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
        if (Result != EWetClothingAssetSetupDialogResult::Cancel)
        {
            OutSettings = SetupObject->BuildSettings();
        }
        return Result;
    }

    EWetClothingPendingCloseChoice ShowDWCResolveCloseDialog(const FString& IssueSummary)
    {
        EWetClothingPendingCloseChoice Choice = EWetClothingPendingCloseChoice::Cancel;

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
                .Title(LOCTEXT("DWCResolveCloseTitle", "Wet Clothing Asset Is Not Up To Date"))
                .SizingRule(ESizingRule::Autosized)
                .SupportsMaximize(false)
                .SupportsMinimize(false);

        const FString FullMessage = FString::Printf(
            TEXT("The Wet Clothing Asset is not fully up to date.\n\n%s\n\nResolve Issues & Save will perform only the required rebuild, map-bake and save operations."),
            *IssueSummary);

        DialogWindow->SetContent(
            SNew(SBorder)
                .Padding(20.0f)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                [SNew(SVerticalBox)

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(0.0f, 0.0f, 0.0f, 18.0f)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Top)
                                  .Padding(0.0f, 2.0f, 16.0f, 0.0f)
                                      [SNew(SImage)
                                           .DesiredSizeOverride(FVector2D(48.0f, 48.0f))
                                           .Image(FAppStyle::Get().GetBrush(TEXT("Icons.WarningWithColor")))]

                            + SHorizontalBox::Slot()
                                  .FillWidth(1.0f)
                                      [SNew(SBox)
                                           .WidthOverride(560.0f)
                                               [SNew(STextBlock)
                                                    .AutoWrapText(true)
                                                    .Text(FText::FromString(FullMessage))]]]

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .HAlign(HAlign_Right)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                      [SNew(SButton)
                                           .Text(LOCTEXT("DWCResolveIssuesAndSave", "Resolve Issues & Save"))
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWetClothingPendingCloseChoice::ResolveAndSave;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                      [SNew(SButton)
                                           .Text(LOCTEXT("DWCCloseAnyway", "Close Anyway"))
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWetClothingPendingCloseChoice::CloseAnyway;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                      [SNew(SButton)
                                           .Text(LOCTEXT("DWCCancelClose", "Cancel"))
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWetClothingPendingCloseChoice::Cancel;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })]]]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
        return Choice;
    }
} // namespace

const FName FWetClothingAssetEditor::EditorAppDisplayName(TEXT("WetClothingAssetEditorApp"));
const FName FWetClothingAssetEditor::MainTabId(TEXT("WetClothingAssetEditor_Main"));

FWetClothingAssetEditor::~FWetClothingAssetEditor()
{
    if (ObjectPropertyChangedHandle.IsValid())
    {
        FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
    }
    if (AssetSavedHandle.IsValid())
    {
        DWCEditorUtils::OnAssetSaveAttemptFinished().Remove(AssetSavedHandle);
    }
}

void FWetClothingAssetEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingAsset* InWetClothingAsset)
{
    check(InWetClothingAsset != nullptr);

    WetClothingAsset = InWetClothingAsset;

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateStatic(&ShouldShowWetClothingAssetDetailProperty));
    DetailsView->SetObject(InWetClothingAsset);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWetClothingAssetEditor::HandleObjectPropertyChanged);
    AssetSavedHandle = DWCEditorUtils::OnAssetSaveAttemptFinished().AddSP(
        this,
        &FWetClothingAssetEditor::HandleDWCEditorAssetSaveAttemptFinished);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WetClothingAssetEditor_Layout_v3")
                                                        ->AddArea(
                                                            FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)->Split(FTabManager::NewStack()->SetHideTabWell(true)->AddTab(MainTabId, ETabState::OpenedTab)));

    const bool bCreateDefaultStandaloneMenu = true;
    const bool bCreateDefaultToolbar = true;
    FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, EditorAppDisplayName, Layout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InWetClothingAsset);

    ToolbarExtender = MakeShared<FExtender>();
    ToolbarExtender->AddToolBarExtension(
        TEXT("Asset"),
        EExtensionHook::After,
        GetToolkitCommands(),
        FToolBarExtensionDelegate::CreateSP(this, &FWetClothingAssetEditor::FillAssetToolbar));
    AddToolbarExtender(ToolbarExtender);

    RegenerateMenusAndToolbars();
}

void FWetClothingAssetEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wet Clothing Asset Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWetClothingAssetEditor::SpawnMainTab))
        .SetDisplayName(LOCTEXT("MainTab", "Wet Clothing Asset"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWetClothingAssetEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWetClothingAssetEditor::GetToolkitFName() const
{
    return EditorAppDisplayName;
}

FText FWetClothingAssetEditor::GetBaseToolkitName() const
{
    return LOCTEXT("AppLabel", "Wet Clothing Asset Editor");
}

FString FWetClothingAssetEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("Wet Clothing Asset ");
}

FLinearColor FWetClothingAssetEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
}

bool FWetClothingAssetEditor::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
    if (EditorPanel.IsValid())
    {
        const FDWCEditorIssueStatus Status = EditorPanel->CollectIssueStatus();
        if (Status.HasIssues())
        {
            const EWetClothingPendingCloseChoice Choice = ShowDWCResolveCloseDialog(Status.BuildSummary());
            if (Choice == EWetClothingPendingCloseChoice::Cancel)
            {
                return false;
            }

            if (Choice == EWetClothingPendingCloseChoice::ResolveAndSave)
            {
                FString Failure;
                if (!ResolveIssuesAndSave(Failure))
                {
                    FMessageDialog::Open(
                        EAppMsgCategory::Error,
                        EAppMsgType::Ok,
                        FText::FromString(FString::Printf(
                            TEXT("Failed to resolve all Wet Clothing Asset issues.\n\n%s"),
                            *Failure)));
                    return false;
                }
            }
        }
    }

    // Close Anyway deliberately falls through to the normal Unreal unsaved-asset prompt.
    return FAssetEditorToolkit::OnRequestClose(InCloseReason);
}


void FWetClothingAssetEditor::SaveAsset_Execute()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        DWCEditorUtils::SaveAsset(Asset);
        return;
    }

    FAssetEditorToolkit::SaveAsset_Execute();
}

void FWetClothingAssetEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (EditorPanel.IsValid())
    {
        EditorPanel->RequestRefreshFromAsset();
    }
}

void FWetClothingAssetEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
    if (ObjectBeingModified == WetClothingAsset.Get())
    {
        if (EditorPanel.IsValid())
        {
            EditorPanel->RequestRefreshFromAsset();
        }
        RegenerateMenusAndToolbars();
    }
}


void FWetClothingAssetEditor::HandleDWCEditorAssetSaveAttemptFinished(UObject* SavedAsset, const bool /*bSaveSucceeded*/)
{
    if (SavedAsset != WetClothingAsset.Get())
    {
        return;
    }

    RefreshAssetStateAndEditor(false);
}

void FWetClothingAssetEditor::RefreshAssetStateAndEditor(const bool bIncludeMapValidation)
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->RefreshBakeState(bIncludeMapValidation);
    }
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    if (EditorPanel.IsValid())
    {
        EditorPanel->RequestRefreshFromAsset();
    }
    RegenerateMenusAndToolbars();
}

TSharedRef<SDockTab> FWetClothingAssetEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == MainTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("MainTabLabel", "Wet Clothing Asset Editor"))
            [SAssignNew(EditorPanel, SWetClothingAssetEditorPanel)
                 .DetailsView(DetailsView)
                 .WetClothingAsset(WetClothingAsset.Get())];
}

void FWetClothingAssetEditor::PostRegenerateMenusAndToolbars()
{
    AddToolbarWidget(BuildModeToolbarWidget());
    GenerateToolbar();
}

void FWetClothingAssetEditor::FillAssetToolbar(FToolBarBuilder& ToolbarBuilder)
{
    ToolbarBuilder.AddSeparator();
    ToolbarBuilder.AddToolBarButton(
        FUIAction(FExecuteAction::CreateSP(this, &FWetClothingAssetEditor::HandleAssetSetupClicked)),
        NAME_None,
        LOCTEXT("AssetSetupToolbarLabel", "Asset Setup"),
        LOCTEXT("AssetSetupToolbarTooltip", "Review immutable mesh information and change simulation-data and map-resolution settings."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FWetClothingAssetEditor::BuildBakeMapsMenu),
        LOCTEXT("BakeMapsToolbarLabel", "Bake Maps"),
        LOCTEXT("BakeMapsToolbarTooltip", "Bake generated texture and simulation maps for this Wet Clothing Asset."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.Bake")),
        false);
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FWetClothingAssetEditor::BuildGenerateMaterialsMenu),
        LOCTEXT("GenerateMaterialsToolbarLabel", "Generate Materials"),
        LOCTEXT("GenerateMaterialsToolbarTooltip", "Generate or refresh wet material assets for this Wet Clothing Asset."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Material"),
        false);
    FText ValidationLabel = LOCTEXT("ValidationToolbarLabel", "Validation");
    FText ValidationTooltip = LOCTEXT("ValidationToolbarTooltip", "Validation passed. Click to view the latest validation results.");
    FName ValidationIconName(TEXT("Icons.SuccessWithColor"));
#if WITH_EDITORONLY_DATA
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const TArray<FDWCValidationActionItem> ActionItems = GetValidationActionItems(*Asset);
        const int32 ActionRequiredCount = ActionItems.Num();
        if (ActionRequiredCount > 0)
        {
            const bool bHasFailedState =
                HasValidationFailedState(Asset->GetBakeState()) ||
                ActionItems.ContainsByPredicate(
                    [](const FDWCValidationActionItem& Item)
                    {
                        return Item.bFailed;
                    });
            ValidationLabel = FText::Format(
                LOCTEXT("ValidationToolbarWarningLabel", "Validation ({0})"),
                FText::AsNumber(ActionRequiredCount));
            if (bHasFailedState)
            {
                ValidationTooltip = FText::Format(
                    LOCTEXT(
                        "ValidationToolbarErrorTooltip",
                        "{0} data item(s) require action, including failed data. Click to view details."),
                    FText::AsNumber(ActionRequiredCount));
                ValidationIconName = FName(TEXT("Icons.ErrorWithColor"));
            }
            else
            {
                ValidationTooltip = FText::Format(
                    LOCTEXT(
                        "ValidationToolbarWarningTooltip",
                        "{0} data item(s) require Save, Bake, or Rebuild. Click to view details."),
                    FText::AsNumber(ActionRequiredCount));
                ValidationIconName = FName(TEXT("Icons.WarningWithColor"));
            }
        }
    }
#endif
    ToolbarBuilder.AddToolBarButton(
        FUIAction(FExecuteAction::CreateSP(this, &FWetClothingAssetEditor::HandleValidationClicked)),
        NAME_None,
        ValidationLabel,
        ValidationTooltip,
        FSlateIcon(FAppStyle::GetAppStyleSetName(), ValidationIconName));
}

void FWetClothingAssetEditor::HandleAssetSetupClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    FDWCWetClothingAssetSetupSettings NewSettings;
    const EWetClothingAssetSetupDialogResult DialogResult = ShowWetClothingAssetSetupDialog(*Asset, NewSettings);
    if (DialogResult == EWetClothingAssetSetupDialogResult::Cancel)
    {
        return;
    }

    Asset->Modify();
    FString ChangeSummary;
    if (!Asset->ApplySetupSettings(NewSettings, &ChangeSummary))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Error,
            EAppMsgType::Ok,
            FText::FromString(ChangeSummary.IsEmpty() ? TEXT("Failed to apply Wet Clothing Asset setup settings.") : ChangeSummary));
        return;
    }
    FWetClothingEditorCommonWidgets::ClearEditorSessionCaches();
    Asset->MarkPackageDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    if (EditorPanel.IsValid())
    {
        EditorPanel->RequestRefreshFromAsset();
    }
    RegenerateMenusAndToolbars();

    if (DialogResult == EWetClothingAssetSetupDialogResult::ApplySettings && !ChangeSummary.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ChangeSummary));
    }

    if (DialogResult == EWetClothingAssetSetupDialogResult::RebuildGeneratedDataUV)
    {
        HandleGenerateGeneratedDataUVClicked();
    }
}

void FWetClothingAssetEditor::HandleGenerateGeneratedDataUVClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    Asset->Modify();
    FScopedSlowTask SlowTask(
        2.0f,
        FText::FromString(FString::Printf(TEXT("Generating DWC Data UV for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateDataUVBuildProgress", "Building generated DWC mesh UV data..."));
    const FDWCDataUVBuildResult Result = FDWCDataUVBuildService::Generate(*Asset, true);
    if (!Result.bSucceeded)
    {
        Asset->SetLastBakeFailure(Result.Message);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
        return;
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateDataUVRefreshProgress", "Refreshing Wet Clothing Asset editor state..."));
    FWetClothingEditorCommonWidgets::ClearEditorSessionCaches();
    Asset->MarkPackageDirty();
    RefreshAssetStateAndEditor();
    FMessageDialog::Open(
        Result.bGeneratedWithWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::FromString(Result.Message));
}

void FWetClothingAssetEditor::HandleValidationClicked()
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

#if WITH_EDITORONLY_DATA
    const FDWCTriangleValidationSummary& Summary = Asset->GetValidationSummary();
    const TArray<FDWCValidationActionItem> ActionItems = GetValidationActionItems(*Asset);
    const TArray<FString> FailureDetails = GetValidationFailureDetails(*Asset);
    const bool bHasFailedState =
        HasValidationFailedState(Asset->GetBakeState()) ||
        ActionItems.ContainsByPredicate(
            [](const FDWCValidationActionItem& Item)
            {
                return Item.bFailed;
            });

    FString Examples;
    for (const int32 TriangleIndex : Summary.ExampleTriangleIndices)
    {
        if (!Examples.IsEmpty()) Examples += TEXT(", ");
        Examples += FString::FromInt(TriangleIndex);
    }
    if (Examples.IsEmpty()) Examples = TEXT("None");

    TSharedRef<SWindow> DialogWindow = SNew(SWindow)
        .Title(LOCTEXT("ValidationResultsWindowTitle", "Validation Results"))
        .ClientSize(FVector2D(640.0f, 520.0f))
        .SupportsMaximize(false)
        .SupportsMinimize(false);

    const TWeakPtr<SWindow> WeakDialogWindow(DialogWindow);
    DialogWindow->SetContent(BuildValidationDialogContent(
        DialogWindow,
        Summary,
        ActionItems,
        FailureDetails,
        bHasFailedState,
        Examples,
        FOnClicked::CreateSP(this, &FWetClothingAssetEditor::HandleValidationResolveClicked, WeakDialogWindow)));
    FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
#endif
}

FReply FWetClothingAssetEditor::HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow)
{
    FString Failure;
    if (!ResolveIssuesAndSave(Failure))
    {
        const FText FailureMessage = Failure.IsEmpty()
            ? LOCTEXT("ValidationResolveFailedFallback", "Validation issues could not be resolved.")
            : FText::FromString(Failure);
        FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, FailureMessage);
        RefreshAssetStateAndEditor();
        return FReply::Handled();
    }

    if (TSharedPtr<SWindow> PinnedDialog = DialogWindow.Pin())
    {
        PinnedDialog->RequestDestroyWindow();
    }

    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        LOCTEXT("ValidationResolveSucceeded", "Validation issues were resolved and saved."));
    RefreshAssetStateAndEditor();
    return FReply::Handled();
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildBakeMapsMenu()
{
    FWetClothingBakeMapsMenuArgs Args;
    Args.OnBakeAllMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeAllMapsClicked(); });
    Args.OnBakeWetnessProfileMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeWetnessProfileMapsClicked(); });
    Args.OnBakeGPUWetnessMapData = FSimpleDelegate::CreateLambda([this]() { HandleBakeGPUWetnessMapDataClicked(); });
    Args.OnBakeTransparencyRevealMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeTransparencyRevealMapsClicked(); });
    Args.OnBakeWrinkleNormalMap = FSimpleDelegate::CreateLambda([this]() { HandleBakeWrinkleNormalMapClicked(); });
    Args.OnBakeWrinkleMask = FSimpleDelegate::CreateLambda([this]() { HandleBakeWrinkleMaskClicked(); });
    return FWetClothingEditorCommonWidgets::BuildBakeMapsMenu(Args);
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildGenerateMaterialsMenu()
{
    FWetClothingGenerateMaterialsMenuArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.OnGenerateCPUMaterials = FSimpleDelegate::CreateLambda(
        [this]()
        {
            HandleGenerateMaterialsClicked(EWetClothingGenerateMaterialMode::CPU);
        });
    Args.OnGenerateGPUMaterials = FSimpleDelegate::CreateLambda(
        [this]()
        {
            HandleGenerateMaterialsClicked(EWetClothingGenerateMaterialMode::GPU);
        });
    Args.OnGenerateAllMaterials = FSimpleDelegate::CreateLambda(
        [this]()
        {
            HandleGenerateMaterialsClicked(EWetClothingGenerateMaterialMode::All);
        });
    return FWetClothingEditorCommonWidgets::BuildGenerateMaterialsMenu(Args);
}

FReply FWetClothingAssetEditor::HandleBakeAllMapsClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    Asset->RefreshBakeState();
#if WITH_EDITORONLY_DATA
    if (Asset->GetBakeState().GeneratedDataUV != EDWCBakeStatus::Valid ||
        Asset->GetBakeState().OriginalUVTopology != EDWCBakeStatus::Valid)
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeAllMapsGeneratedDataUVRequired", "DWC Data UV or Original UV topology is missing or out of date. Rebuild DWC Data UV before baking maps."));
        return FReply::Handled();
    }
#endif

    TArray<FString> Sections;
    TArray<FString> Failures;
    bool bHadWarnings = false;

    FScopedSlowTask SlowTask(
        6.0f,
        FText::FromString(FString::Printf(TEXT("Baking maps for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);

    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    if (Setup.bBuildGPUWetnessMapSimulationData)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllGPURuntimeProgress", "Preparing GPU runtime data before baking simulation maps..."));
        FString RuntimeFailure;
        if (!EnsureGPURuntimeDataReadyForMapBake(Asset, RuntimeFailure))
        {
            Failures.Add(RuntimeFailure);
        }

        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllGPUMapsProgress", "Baking GPU simulation maps..."));
        FString ErrorMessage;
        if (RuntimeFailure.IsEmpty() && Asset->BakeGPUWetnessMaps(&ErrorMessage))
        {
            Sections.Add(FString::Printf(
                TEXT("GPU maps: %d material-slot map(s) at %d x %d."),
                Asset->GetGPUWetMapRuntimeData(Asset->GetSimulationLODIndex()).MaterialSlotMapCount,
                Setup.GetGPUSimulationMapResolution(),
                Setup.GetGPUSimulationMapResolution()));
        }
        else if (RuntimeFailure.IsEmpty())
        {
            Failures.Add(FString::Printf(TEXT("GPU maps: %s"), *ErrorMessage));
        }
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeAllWetnessProfileMapsProgress", "Baking wetness profile maps..."));
    FString VisualSummary;
    bool bVisualWarnings = false;
    if (EditorPanel->BakeWetVisualAssets(VisualSummary, &bVisualWarnings))
    {
        EditorPanel->SaveBakedVisualAssets();
        Sections.Add(VisualSummary);
        bHadWarnings |= bVisualWarnings;
    }
    else if (!VisualSummary.IsEmpty())
    {
        Failures.Add(FString::Printf(TEXT("Wetness profile maps: %s"), *VisualSummary));
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeAllWrinkleMapsProgress", "Baking wrinkle maps..."));
    FString WrinkleSummary;
    bool bWrinkleWarnings = false;
    if (EditorPanel->BakeAllWrinkleMaps(WrinkleSummary, &bWrinkleWarnings))
    {
        Sections.Add(WrinkleSummary);
        bHadWarnings |= bWrinkleWarnings;
    }
    else if (!WrinkleSummary.IsEmpty())
    {
        Failures.Add(FString::Printf(TEXT("Wrinkle maps: %s"), *WrinkleSummary));
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeAllTransparencyMapsProgress", "Baking transparency reveal maps..."));
    if (!Asset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        FString TransparencySummary;
        bool bTransparencyWarnings = false;
        if (EditorPanel->BakeTransparencyRevealAssets(TransparencySummary, &bTransparencyWarnings))
        {
            EditorPanel->SaveTransparencySetupAssets();
            Sections.Add(TransparencySummary);
            bHadWarnings |= bTransparencyWarnings;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Transparency maps: %s"), *TransparencySummary));
        }
    }
    else
    {
        Sections.Add(TEXT("Transparency maps: skipped because no Source Blueprint is configured."));
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeAllRefreshProgress", "Refreshing Wet Clothing Asset state..."));
    Asset->RefreshBakeState();
    Asset->MarkPackageDirty();
    RefreshAssetStateAndEditor();

    FString Summary = FString::Join(Sections, TEXT("\n\n"));
    if (!Failures.IsEmpty())
    {
        if (!Summary.IsEmpty()) Summary += TEXT("\n\n");
        Summary += FString::Printf(TEXT("Failures:\n- %s"), *FString::Join(Failures, TEXT("\n- ")));
    }

    const EAppMsgCategory Category = !Failures.IsEmpty() || bHadWarnings
        ? EAppMsgCategory::Warning
        : EAppMsgCategory::Success;
    FMessageDialog::Open(Category, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeWetnessProfileMapsClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FScopedSlowTask SlowTask(
        2.0f,
        LOCTEXT("BakeWetnessProfileMapsProgress", "Baking wetness profile maps..."));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeWetnessProfileMapsBuildProgress", "Generating wetness profile textures..."));

    FString Summary;
    bool    bHadWarnings = false;
    if (!EditorPanel->BakeWetVisualAssets(Summary, &bHadWarnings))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeWetnessProfileMapsSaveProgress", "Saving baked wetness profile assets..."));
    EditorPanel->SaveBakedVisualAssets();
    RefreshAssetStateAndEditor();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeGPUWetnessMapDataClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    if (!Asset->GetSetupSettings().bBuildGPUWetnessMapSimulationData)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GPUDataDisabled", "GPU Wetness Map data is disabled in Asset Setup."));
        return FReply::Handled();
    }

    Asset->Modify();
    FScopedSlowTask SlowTask(
        3.0f,
        FText::FromString(FString::Printf(TEXT("Baking GPU simulation maps for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeGPUWetnessMapsRuntimeProgress", "Preparing GPU runtime data before map bake..."));
    FString RuntimeFailure;
    if (!EnsureGPURuntimeDataReadyForMapBake(Asset, RuntimeFailure))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(RuntimeFailure));
        return FReply::Handled();
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeGPUWetnessMapsBuildProgress", "Generating GPU simulation map data..."));
    FString ErrorMessage;
    if (!Asset->BakeGPUWetnessMaps(&ErrorMessage))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::Format(
                LOCTEXT("BakeGPUWetnessMapFailed", "Failed to bake GPU simulation maps.\n\n{0}"),
                FText::FromString(ErrorMessage)));
        return FReply::Handled();
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeGPUWetnessMapsRefreshProgress", "Refreshing Wet Clothing Asset state..."));
    Asset->MarkPackageDirty();
    RefreshAssetStateAndEditor();

    const FDWCGPULODBakeData& BakedGPUData = Asset->GetGPUWetMapRuntimeData(Asset->GetSimulationLODIndex());
    const int32 TriangleCount = BakedGPUData.TriangleCount;
    const int32 SlotCount = BakedGPUData.MaterialSlotMapCount;
    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::Format(
            LOCTEXT(
                "BakeGPUWetnessMapSucceeded",
                "GPU simulation maps were baked for LOD{3}.\n\nTriangles: {0}\nMaterial-slot maps: {1}\nResolution: {2} x {2}\n\nBefore runtime use, enable Support Compute Skin Cache and enable Skin Cache Usage for the target skeletal-mesh LOD. The wet material must expose the DWC_WetnessMap texture parameter."),
            FText::AsNumber(TriangleCount),
            FText::AsNumber(SlotCount),
            FText::AsNumber(Asset->GetSetupSettings().GetGPUSimulationMapResolution()),
            FText::AsNumber(Asset->GetSimulationLODIndex())));

    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeTransparencyRevealMapsClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FScopedSlowTask SlowTask(
        2.0f,
        LOCTEXT("BakeTransparencyRevealMapsProgress", "Baking transparency reveal maps..."));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeTransparencyRevealMapsBuildProgress", "Generating transparency reveal textures..."));

    FString Summary;
    bool bHadWarnings = false;
    if (!EditorPanel->BakeTransparencyRevealAssets(Summary, &bHadWarnings))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeTransparencyRevealMapsSaveProgress", "Saving transparency reveal assets..."));
    EditorPanel->SaveTransparencySetupAssets();
    RefreshAssetStateAndEditor();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWetClothingAssetEditor::HandleBakeWrinkleNormalMapClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    const FReply Result = EditorPanel->BakeSelectedWrinkleNormalMap();
    RefreshAssetStateAndEditor();
    return Result;
}

FReply FWetClothingAssetEditor::HandleBakeWrinkleMaskClicked()
{
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    const FReply Result = EditorPanel->BakeSelectedWrinkleMask();
    RefreshAssetStateAndEditor();
    return Result;
}

FReply FWetClothingAssetEditor::HandleGenerateMaterialsClicked(const EWetClothingGenerateMaterialMode GenerateMode)
{
    return GenerateWetMaterials(GenerateMode);
}

FReply FWetClothingAssetEditor::GenerateWetMaterials(const EWetClothingGenerateMaterialMode GenerateMode)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        FMessageDialog::Open(EAppMsgCategory::Error, EAppMsgType::Ok, LOCTEXT("GenerateMaterialsNoMesh", "Assign a runtime skeletal mesh before generating wet materials."));
        return FReply::Handled();
    }

    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    TArray<EDWCSimulationMode> SimulationModes;
    if (GenerateMode == EWetClothingGenerateMaterialMode::CPU || GenerateMode == EWetClothingGenerateMaterialMode::All)
    {
        SimulationModes.Add(EDWCSimulationMode::VertexCPU);
    }
    if (GenerateMode == EWetClothingGenerateMaterialMode::GPU || GenerateMode == EWetClothingGenerateMaterialMode::All)
    {
        SimulationModes.Add(EDWCSimulationMode::WetnessMapGPU);
    }

    const bool bNeedsCPU = SimulationModes.Contains(EDWCSimulationMode::VertexCPU);
    const bool bNeedsGPU = SimulationModes.Contains(EDWCSimulationMode::WetnessMapGPU);
    TArray<FString> DisabledBackends;
    if (bNeedsCPU && !Setup.bBuildCPUVertexSimulationData)
    {
        DisabledBackends.Add(TEXT("CPU Vertex Simulation Data"));
    }
    if (bNeedsGPU && !Setup.bBuildGPUWetnessMapSimulationData)
    {
        DisabledBackends.Add(TEXT("GPU Wetness Map Simulation Data"));
    }
    if (!DisabledBackends.IsEmpty())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(FString::Printf(
                TEXT("Enable %s in Asset Setup before generating the requested wet materials."),
                *FString::Join(DisabledBackends, TEXT(" and ")))));
        return FReply::Handled();
    }

    const FDWCDataUVPerLOD* DataUV = Asset->FindGeneratedDataUVForLOD(Asset->GetSimulationLODIndex());
    if (Asset->GetDWCDataUVChannelIndex() == INDEX_NONE || DataUV == nullptr || !DataUV->bIsValid)
    {
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, LOCTEXT("GenerateMaterialsNoDataUV", "Generate DWC Data UV before generating wet materials."));
        return FReply::Handled();
    }

    TArray<int32> WettableSlots;
    for (const FWetClothingWettableMaterialSlotState& SlotState : Asset->PartData.EditableWetPartData.WettableMaterialSlots)
    {
        if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
        {
            WettableSlots.AddUnique(SlotState.MaterialSlotIndex);
        }
    }
    WettableSlots.Sort();
    if (WettableSlots.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, LOCTEXT("GenerateMaterialsNoWettableSlots", "Mark at least one material slot as wettable before generating wet materials."));
        return FReply::Handled();
    }

    const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();
    TArray<FString> UpdatedMaterials;
    TArray<FString> Failures;

    const FString ModeLabel = GenerateMode == EWetClothingGenerateMaterialMode::All
                                  ? TEXT("CPU + GPU")
                                  : (GenerateMode == EWetClothingGenerateMaterialMode::GPU ? TEXT("GPU") : TEXT("CPU"));
    FScopedSlowTask SlowTask(
        static_cast<float>(FMath::Max(1, WettableSlots.Num() * SimulationModes.Num() + 2)),
        FText::FromString(FString::Printf(
            TEXT("Generating %s wet materials for %s (%d slot%s)..."),
            *ModeLabel,
            *GetNameSafe(Asset),
            WettableSlots.Num(),
            WettableSlots.Num() == 1 ? TEXT("") : TEXT("s"))));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        FText::FromString(FString::Printf(
            TEXT("Preparing %d wettable material slot%s for %s generation..."),
            WettableSlots.Num(),
            WettableSlots.Num() == 1 ? TEXT("") : TEXT("s"),
            *ModeLabel)));

    TMap<EDWCSimulationMode, FWetClothingMaterialSetup::FOptions> MaterialSetupOptionsByMode;
    for (const EDWCSimulationMode Mode : SimulationModes)
    {
        MaterialSetupOptionsByMode.Add(Mode, FWetClothingMaterialSetup::MakeOptionsForAsset(Asset, Mode));
    }

    auto GetModeLabel = [](const EDWCSimulationMode Mode) -> const TCHAR*
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU ? TEXT("GPU") : TEXT("CPU");
    };

    Asset->Modify();
    bool bUpdatedAnyMaterial = false;
    for (const int32 MaterialSlotIndex : WettableSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            Failures.Add(FString::Printf(TEXT("Slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = Materials[MaterialSlotIndex].MaterialInterface;
        if (SourceMaterial == nullptr)
        {
            Failures.Add(FString::Printf(TEXT("Slot %d has no source material."), MaterialSlotIndex));
            continue;
        }

        FWetClothingGeneratedWetMaterialOverride* ExistingOverride =
            Asset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
                {
                    return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
                });
        const bool bHadOverride = ExistingOverride != nullptr &&
                                  (ExistingOverride->CPUWetMaterial != nullptr ||
                                   ExistingOverride->GPUWetMaterial != nullptr);

        for (const EDWCSimulationMode Mode : SimulationModes)
        {
            const TCHAR* PerModeLabel = GetModeLabel(Mode);
            const bool bGenerateGPU = Mode == EDWCSimulationMode::WetnessMapGPU;
            UMaterialInterface* ExistingTargetMaterial =
                ExistingOverride != nullptr
                    ? (bGenerateGPU ? ExistingOverride->GPUWetMaterial.Get() : ExistingOverride->CPUWetMaterial.Get())
                    : nullptr;

            SlowTask.EnterProgressFrame(
                1.0f,
                FText::FromString(FString::Printf(
                    TEXT("Generating %s material for slot %d from '%s'..."),
                    PerModeLabel,
                    MaterialSlotIndex,
                    *GetNameSafe(SourceMaterial))));

            const FWetClothingMaterialSetup::FOptions* MaterialSetupOptions = MaterialSetupOptionsByMode.Find(Mode);
            FWetClothingMaterialSetupResult MaterialResult =
                FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
                    SourceMaterial,
                    MaterialSetupOptions != nullptr ? *MaterialSetupOptions : FWetClothingMaterialSetup::FOptions());
            if (!MaterialResult.bSucceeded || MaterialResult.ConfiguredMaterial == nullptr)
            {
                Failures.Add(FString::Printf(TEXT("Slot %d %s: %s"), MaterialSlotIndex, PerModeLabel, *MaterialResult.Message));
                continue;
            }

            if (ExistingOverride == nullptr)
            {
                ExistingOverride = &Asset->PartData.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
                ExistingOverride->MaterialSlotIndex = MaterialSlotIndex;
            }

            ExistingOverride->SourceMaterial = SourceMaterial;
            if (bGenerateGPU)
            {
                ExistingOverride->GPUWetMaterial = MaterialResult.ConfiguredMaterial;
            }
            else
            {
                ExistingOverride->CPUWetMaterial = MaterialResult.ConfiguredMaterial;
            }
            bUpdatedAnyMaterial = true;

            UpdatedMaterials.Add(FString::Printf(
                TEXT("Slot %d -> %s %s (%s)"),
                MaterialSlotIndex,
                PerModeLabel,
                *GetNameSafe(MaterialResult.ConfiguredMaterial),
                ExistingTargetMaterial != nullptr || MaterialResult.bAlreadyConfigured || bHadOverride
                    ? TEXT("overwritten/refreshed")
                    : TEXT("created")));
        }
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        FText::FromString(FString::Printf(
            TEXT("Refreshing generated %s material state..."),
            *ModeLabel)));

    if (bUpdatedAnyMaterial)
    {
        Asset->MarkPackageDirty();
    }
    RefreshAssetStateAndEditor();

    TArray<FString> SummarySections;
    if (!UpdatedMaterials.IsEmpty())
    {
        SummarySections.Add(FString::Printf(
            TEXT("Updated %s materials:\n- %s"),
            *ModeLabel,
            *FString::Join(UpdatedMaterials, TEXT("\n- "))));
    }

    const FString MaterialSummary = SummarySections.IsEmpty()
                                        ? TEXT("No material overrides were updated.")
                                        : FString::Join(SummarySections, TEXT("\n\n"));
    FString Summary = FString::Printf(TEXT("Generated %s material overrides.\n\n%s"), *ModeLabel, *MaterialSummary);
    if (!Failures.IsEmpty())
    {
        Summary += FString::Printf(TEXT("\n\nFailures:\n- %s"), *FString::Join(Failures, TEXT("\n- ")));
    }

    const EAppMsgCategory Category = Failures.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning;
    FMessageDialog::Open(Category, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

bool FWetClothingAssetEditor::ResolveIssuesAndSave(FString& OutFailure)
{
    OutFailure.Reset();
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !EditorPanel.IsValid())
    {
        OutFailure = TEXT("The Wet Clothing Asset editor is no longer valid.");
        return false;
    }

    FScopedSlowTask SlowTask(
        9.0f,
        FText::FromString(FString::Printf(TEXT("Resolving and saving %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);

    FDWCEditorIssueStatus Status = EditorPanel->CollectIssueStatus(true, true);
    bool bPreparedRuntimePrerequisites = false;
    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();

    if (Status.bGeneratedDataUVIssue)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesGenerateDataUVProgress", "Rebuilding DWC Data UV because mesh UV data is missing or stale..."));
        const FDWCDataUVBuildResult DataUVResult = FDWCDataUVBuildService::Generate(*Asset, true);
        if (!DataUVResult.bSucceeded)
        {
            Asset->SetLastBakeFailure(DataUVResult.Message);
            OutFailure = DataUVResult.Message;
            return false;
        }
        FWetClothingEditorCommonWidgets::ClearEditorSessionCaches();
    }

    const bool bRuntimeBackendEnabled =
        Setup.bBuildCPUVertexSimulationData ||
        Setup.bBuildGPUWetnessMapSimulationData;
    if (bRuntimeBackendEnabled)
    {
        // Runtime structures are explicit generated data now. Build them before dependent map bakes,
        // then let the final save persist all pending runtime/map payloads once.
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesRuntimeDataProgress", "Preparing prerequisite runtime data before dependent map bakes..."));
        FString RuntimeDataError;
        if (!Asset->PrepareRuntimeDataForEditorSave(&RuntimeDataError))
        {
            OutFailure = RuntimeDataError.IsEmpty()
                             ? TEXT("Runtime Data: prerequisite runtime data rebuild failed.")
                             : RuntimeDataError;
            return false;
        }
        bPreparedRuntimePrerequisites = true;
    }

    Asset->RefreshBakeState();

#if WITH_EDITORONLY_DATA
    if (Setup.bBuildGPUWetnessMapSimulationData &&
        !DWCBuildStatus::IsUsable(Asset->GetBakeState().GPURuntimeData))
    {
        OutFailure = Asset->GetBakeState().LastFailure.IsEmpty()
                         ? TEXT("GPU Runtime Data is missing or out of date after preparing.")
                         : FString::Printf(TEXT("GPU Runtime Data: %s"), *Asset->GetBakeState().LastFailure);
        return false;
    }

    if (Setup.bBuildGPUWetnessMapSimulationData &&
        !DWCBuildStatus::IsUsable(Asset->GetBakeState().GPUMaps))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesGPUMapsProgress", "Baking GPU simulation maps from the prepared runtime signature..."));
        FString GPUMapError;
        if (!Asset->BakeGPUWetnessMaps(&GPUMapError))
        {
            OutFailure = FString::Printf(TEXT("GPU Simulation Maps: %s"), *GPUMapError);
            return false;
        }
    }

    FString VisualSummary;
    if (EditorPanel->HasPendingVisualBakeTasks(&VisualSummary))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesVisualMapsProgress", "Baking wetness/transparency visual maps that are missing or stale..."));
        bool bVisualWarnings = false;
        if (!EditorPanel->BakePendingVisualAssets(VisualSummary, &bVisualWarnings))
        {
            OutFailure = FString::Printf(TEXT("Wetness Profile Maps: %s"), *VisualSummary);
            return false;
        }
        EditorPanel->SaveBakedVisualAssets();
    }

    bool bHasWrinkleContent = !Asset->WrinkleData.BakedWrinkleMaps.IsEmpty();
    for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
    {
        bHasWrinkleContent |= !Stroke.PatchPlacements.IsEmpty();
    }
    if (bHasWrinkleContent && !DWCBuildStatus::IsUsable(Asset->GetBakeState().WrinkleMaps))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesWrinkleMapsProgress", "Baking wrinkle maps because stored wrinkle outputs are missing or stale..."));
        FString WrinkleSummary;
        bool bWrinkleWarnings = false;
        if (!EditorPanel->BakeAllWrinkleMaps(WrinkleSummary, &bWrinkleWarnings))
        {
            OutFailure = FString::Printf(TEXT("Wrinkle Maps: %s"), *WrinkleSummary);
            return false;
        }
    }

    if (!Asset->TransparencyData.SourceBlueprintClass.IsNull() &&
        !DWCBuildStatus::IsUsable(Asset->GetBakeState().TransparencyMaps))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesTransparencyMapsProgress", "Baking transparency reveal maps from the configured source blueprint..."));
        FString TransparencySummary;
        bool bTransparencyWarnings = false;
        if (!EditorPanel->BakeTransparencyRevealAssets(TransparencySummary, &bTransparencyWarnings))
        {
            OutFailure = FString::Printf(TEXT("Transparency Maps: %s"), *TransparencySummary);
            return false;
        }
        EditorPanel->SaveTransparencySetupAssets();
    }
#endif

    // Persist the rebuilt runtime structures and every explicit map-bake result in one final save.
    SlowTask.EnterProgressFrame(
        1.0f,
        bPreparedRuntimePrerequisites
            ? LOCTEXT("ResolveIssuesFinalSaveProgress", "Saving prepared runtime data and baked map results...")
            : LOCTEXT("ResolveIssuesSaveProgress", "Saving resolved Wet Clothing Asset data..."));
    if (!DWCEditorUtils::SaveAsset(Asset))
    {
        OutFailure = TEXT("The asset or its generated data could not be saved.");
        return false;
    }

    Asset->RefreshBakeState();
    EditorPanel->RequestRefreshFromAsset();
    Status = EditorPanel->CollectIssueStatus(false, false);
    if (Status.HasIssues())
    {
        OutFailure = Status.BuildSummary();
        return false;
    }
    return true;
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildModeToolbarWidget()
{
    return SNew(SBox)
        .Padding(FMargin(12.0f, 0.0f))
            [SNew(SHorizontalBox)
             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::PartEdit,
                           TEXT("DWCEditor.Mode.Part"),
                           LOCTEXT("PartEditModeTooltip", "Part Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::WrinkleEdit,
                           TEXT("DWCEditor.Mode.Wrinkle"),
                           LOCTEXT("WrinkleEditModeTooltip", "Wrinkle Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f)
                       [BuildModeToggleButton(
                           EWetClothingEditorMode::TransparencyBake,
                           TEXT("DWCEditor.Mode.Transparency"),
                           LOCTEXT("TransparencyBakeModeTooltip", "Transparency Bake Mode"))]];
}

TSharedRef<SWidget> FWetClothingAssetEditor::BuildModeToggleButton(EWetClothingEditorMode Mode, FName IconName, const FText& ToolTipText)
{
    return SNew(SCheckBox)
        .Style(&GetWetClothingModeToggleStyle())
        .Type(ESlateCheckBoxType::ToggleButton)
        .ToolTipText(ToolTipText)
        .IsChecked(this, &FWetClothingAssetEditor::IsModeChecked, Mode)
        .OnCheckStateChanged(this, &FWetClothingAssetEditor::HandleModeCheckStateChanged, Mode)
            [SNew(SBox)
                 .WidthOverride(76.0f)
                 .HeightOverride(32.0f)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(SImage)
                          .DesiredSizeOverride(FVector2D(24.0f, 24.0f))
                          .Image(FDWCEditorStyle::GetBrush(IconName))
                          .ColorAndOpacity(this, &FWetClothingAssetEditor::GetModeIconColor, Mode)]];
}

void FWetClothingAssetEditor::SetEditorMode(EWetClothingEditorMode NewMode)
{
    if (CurrentMode == NewMode)
    {
        return;
    }

    CurrentMode = NewMode;

    if (EditorPanel.IsValid())
    {
        EditorPanel->SetEditorMode(NewMode);
    }
}

ECheckBoxState FWetClothingAssetEditor::IsModeChecked(EWetClothingEditorMode Mode) const
{
    return CurrentMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FWetClothingAssetEditor::HandleModeCheckStateChanged(ECheckBoxState NewState, EWetClothingEditorMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SetEditorMode(Mode);
    }
}

FSlateColor FWetClothingAssetEditor::GetModeIconColor(EWetClothingEditorMode Mode) const
{
    if (CurrentMode == Mode)
    {
        return FSlateColor(FLinearColor::White);
    }

    switch (Mode)
    {
    case EWetClothingEditorMode::PartEdit:
        return FSlateColor(FLinearColor(1.0f, 0.66f, 0.78f, 1.0f));
    case EWetClothingEditorMode::WrinkleEdit:
        return FSlateColor(FLinearColor(0.62f, 0.95f, 0.62f, 1.0f));
    case EWetClothingEditorMode::TransparencyBake:
        return FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 1.0f));
    default:
        return FSlateColor::UseForeground();
    }
}

#undef LOCTEXT_NAMESPACE
