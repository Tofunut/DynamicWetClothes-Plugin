#include "WCAEditor.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DWCEditorStyle.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Asset/WetClothingAssetFactory.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBakeService.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"
#include "DetailsViewArgs.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
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
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WCAEditor"

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
        const int32 PreferredDWCDataUVChannelIndex)
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
            TEXT("Mode: Prepared Mesh. DWC creates or updates a dedicated skeletal mesh copy and leaves the Source Skeletal Mesh untouched.\n\nSource UV Channels\nOriginal UV: UV%d\nPreferred DWC Data UV: UV%d\n"),
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

    int32 GetAssetSetupSkeletalMeshLODCount(const USkeletalMesh* Mesh)
    {
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        return RenderData != nullptr ? RenderData->LODRenderData.Num() : 0;
    }

    void ClampAssetSetupLODRangeForMesh(
        const USkeletalMesh* Mesh,
        int32& FirstLODIndex,
        int32& LastLODIndex)
    {
        const int32 LODCount = GetAssetSetupSkeletalMeshLODCount(Mesh);
        if (LODCount <= 0)
        {
            FirstLODIndex = 0;
            LastLODIndex = 0;
            return;
        }

        const int32 LastAvailableLODIndex = LODCount - 1;
        FirstLODIndex = FMath::Clamp(FirstLODIndex, 0, LastAvailableLODIndex);
        LastLODIndex = FMath::Clamp(LastLODIndex, FirstLODIndex, LastAvailableLODIndex);
    }

    FText BuildAssetSetupLODRangeInfoText(
        const USkeletalMesh* Mesh,
        const int32 FirstLODIndex,
        const int32 LastLODIndex)
    {
        const int32 LODCount = GetAssetSetupSkeletalMeshLODCount(Mesh);
        if (LODCount <= 0)
        {
            return LOCTEXT("AssetSetupLODRangeInfoNoMesh", "Source mesh LOD data is unavailable.");
        }

        return FText::Format(
            LOCTEXT("AssetSetupLODRangeInfo", "Available LODs: LOD0 - LOD{0}. DWC Data UV and Original UV topology will be generated for LOD{1} - LOD{2}."),
            FText::AsNumber(LODCount - 1),
            FText::AsNumber(FirstLODIndex),
            FText::AsNumber(LastLODIndex));
    }

    int32 GetAssetSetupDefaultDWCDataUVChannelIndex(const USkeletalMesh* Mesh, const int32 OriginalUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        return UVChannelCount > 0 ? FMath::Clamp(UVChannelCount, 0, 7) : FMath::Clamp(OriginalUVChannelIndex + 1, 0, 7);
    }

    constexpr int32 RecommendedAssetSetupDataUVSelection = INDEX_NONE;

    FText BuildAssetSetupDataUVChannelLabel(
        const int32 Selection,
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex)
    {
        if (Selection == RecommendedAssetSetupDataUVSelection)
        {
            return FText::Format(
                LOCTEXT("AssetSetupRecommendedDataUVLabel", "Recommended (UV{0})"),
                FText::AsNumber(GetAssetSetupDefaultDWCDataUVChannelIndex(Mesh, OriginalUVChannelIndex)));
        }

        return FText::Format(
            LOCTEXT("AssetSetupExplicitDataUVLabel", "UV{0}"),
            FText::AsNumber(Selection));
    }

    FText BuildAssetSetupPreferredDataUVInfoText(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex)
    {
        if (DataUVChannelIndex == OriginalUVChannelIndex)
        {
            return LOCTEXT(
                "AssetSetupDataUVMatchesOriginal",
                "DWC Data UV cannot use the Original UV channel.");
        }

        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        if (DataUVChannelIndex >= 0 && DataUVChannelIndex < UVChannelCount)
        {
            return FText::Format(
                LOCTEXT(
                    "AssetSetupDataUVExistingChannelInfo",
                    "UV{0} already contains UV data. Update only stores this choice; Rebuild asks before replacing that channel."),
                FText::AsNumber(DataUVChannelIndex));
        }

        return FText::Format(
            LOCTEXT(
                "AssetSetupDataUVNewChannelInfo",
                "DWC Data UV will be generated in UV{0}."),
            FText::AsNumber(DataUVChannelIndex));
    }

    bool IsAssetSetupUVSelectionValid(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        return OriginalUVChannelIndex >= 0 &&
               OriginalUVChannelIndex < UVChannelCount &&
               DataUVChannelIndex >= 0 &&
               DataUVChannelIndex <= 7 &&
               DataUVChannelIndex != OriginalUVChannelIndex;
    }

    bool ConfirmAssetSetupDataUVOverwrite(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex,
        const int32 ExistingGeneratedDataUVChannelIndex,
        const bool bRebuildingSameTargetMode,
        bool& OutConfirmedOverwrite)
    {
        OutConfirmedOverwrite = false;
        if (!IsAssetSetupUVSelectionValid(Mesh, OriginalUVChannelIndex, DataUVChannelIndex))
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT(
                    "AssetSetupInvalidUVSelection",
                    "Select an existing Original UV channel and a different DWC Data UV channel."));
            return false;
        }

        // Rebuilding the channel already owned by this WCA is not a new destructive choice.
        if (bRebuildingSameTargetMode &&
            DataUVChannelIndex == ExistingGeneratedDataUVChannelIndex)
        {
            return true;
        }

        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        if (DataUVChannelIndex >= UVChannelCount)
        {
            return true;
        }

        const FText Warning = FText::Format(
            LOCTEXT(
                "AssetSetupConfirmDataUVOverwrite",
                "UV{0} already contains UV data.\n\nRebuilding DWC Data UV in this channel will replace the existing UV{0} data on the target mesh.\n\nContinue?"),
            FText::AsNumber(DataUVChannelIndex));
        OutConfirmedOverwrite =
            FMessageDialog::Open(EAppMsgType::YesNo, Warning) == EAppReturnType::Yes;
        return OutConfirmedOverwrite;
    }

    FText BuildAssetSetupDWCDataUVTargetText()
    {
        return LOCTEXT(
            "AssetSetupPreparedMeshTargetText",
            "Mode: Prepared Mesh. DWC creates or updates a dedicated skeletal mesh copy and stores generated UV/topology data only on that copy.");
    }

    bool HasWrinkleValidationData(const UWetClothingAsset& Asset)
    {
        if (!Asset.Authored.WrinkleData.BakedWrinkleMaps.IsEmpty())
        {
            return true;
        }
        return !Asset.Authored.WrinkleData.EditablePatches.IsEmpty() ||
               !Asset.Authored.WrinkleData.EditableProceduralRidgeStrokes.IsEmpty();
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
        if (Asset.HasWrinkleBakeContent() && State.WrinkleMaps != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationWrinkleMaps", "Wrinkle Maps"),
                State.WrinkleMaps,
                LOCTEXT("ValidationBakeMapsAction", "Use Bake Maps to rebuild them."));
        }
        if (Asset.HasTransparencyBakeContent() && State.TransparencyMaps != EDWCBakeStatus::Disabled)
        {
            AddValidationActionIfRequired(
                Result,
                LOCTEXT("ValidationTransparencyMaps", "Transparency Maps"),
                State.TransparencyMaps,
                LOCTEXT("ValidationBakeMapsAction", "Use Bake Maps to rebuild them."));
        }

        TArray<FString> GeneratedMaterialMessages;
        if (Asset.HasAnyWettableMaterialSlot())
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(&Asset, GeneratedMaterialMessages);
        }
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
                   PropertyName == FName(TEXT("OriginalUVTopologiesPerLOD")) ||
                   PropertyName == FName(TEXT("DataUVMetadataPerLOD")) ||
                   PropertyName == FName(TEXT("BakeState")) ||
                   PropertyName == FName(TEXT("ValidationSummary")) ||
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

    enum class EWCAPendingCloseChoice : uint8
    {
        ResolveAndSave,
        CloseAnyway,
        Cancel
    };

    enum class EWCASetupDialogResult : uint8
    {
        Closed,
        Update,
        Rebuild
    };

    EWCASetupDialogResult ShowWetClothingAssetSetupDialog(
        UWetClothingAsset& Asset,
        FDWCWetClothingAssetSetupSettings& OutSettings,
        bool& OutAllowOverwriteExistingDataUVChannel)
    {
        OutAllowOverwriteExistingDataUVChannel = false;
        TStrongObjectPtr<UWetClothingAssetSetupSettingsObject> SetupObject(
            NewObject<UWetClothingAssetSetupSettingsObject>(GetTransientPackage()));
        SetupObject->InitializeFromSettings(Asset.GetSetupSettings());
        ClampAssetSetupLODRangeForMesh(
            Asset.GetSourceSkeletalMesh(),
            SetupObject->FirstGeneratedLODIndex,
            SetupObject->LastGeneratedLODIndex);

        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        FDetailsViewArgs DetailsArgs;
        DetailsArgs.bAllowSearch = false;
        DetailsArgs.bHideSelectionTip = true;
        DetailsArgs.bLockable = false;
        DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
        TSharedRef<IDetailsView> SetupDetails = PropertyEditor.CreateDetailView(DetailsArgs);
        SetupDetails->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
        {
            const FName PropertyName = PropertyAndParent.Property.GetFName();
            return PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, PreferredDWCDataUVChannelIndex) &&
                   PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, FirstGeneratedLODIndex) &&
                   PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, LastGeneratedLODIndex);
        }));
        SetupDetails->SetObject(SetupObject.Get());

        TArray<TSharedPtr<int32>> DataUVChannelOptions;
        DataUVChannelOptions.Add(MakeShared<int32>(RecommendedAssetSetupDataUVSelection));
        for (int32 UVChannelIndex = 0; UVChannelIndex <= 7; ++UVChannelIndex)
        {
            DataUVChannelOptions.Add(MakeShared<int32>(UVChannelIndex));
        }

        const int32 InitialRecommendedDataUVChannel = GetAssetSetupDefaultDWCDataUVChannelIndex(
            Asset.GetSourceSkeletalMesh(),
            SetupObject->OriginalUVChannelIndex);
        bool bUseRecommendedDataUVChannel =
            SetupObject->PreferredDWCDataUVChannelIndex == InitialRecommendedDataUVChannel;
        TSharedPtr<int32> InitialDataUVChannelItem = DataUVChannelOptions[0];
        if (!bUseRecommendedDataUVChannel)
        {
            for (const TSharedPtr<int32>& Item : DataUVChannelOptions)
            {
                if (Item.IsValid() && *Item == SetupObject->PreferredDWCDataUVChannelIndex)
                {
                    InitialDataUVChannelItem = Item;
                    break;
                }
            }
        }

        auto GetEffectiveDataUVChannel = [&Asset, SetupObject, &bUseRecommendedDataUVChannel]()
        {
            return bUseRecommendedDataUVChannel
                ? GetAssetSetupDefaultDWCDataUVChannelIndex(
                    Asset.GetSourceSkeletalMesh(),
                    SetupObject->OriginalUVChannelIndex)
                : FMath::Clamp(SetupObject->PreferredDWCDataUVChannelIndex, 0, 7);
        };

        EWCASetupDialogResult Result = EWCASetupDialogResult::Closed;
        TSharedRef<SWindow> Dialog =
            SNew(SWindow)
            .Title(LOCTEXT("AssetSetupTitle", "Wet Clothing Asset Setup"))
            .SizingRule(ESizingRule::Autosized)
            .SupportsMaximize(false)
            .SupportsMinimize(false);

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
                    [
                        SNew(STextBlock)
                        .Text_Lambda([&Asset, SetupObject]()
                        {
                            return FText::Format(
                                LOCTEXT(
                                    "AssetSetupMeshInfo",
                                    "Mesh Information (Read Only)\n"
                                    "Source: {0}\n"
                                    "DWC Data UV LODs: {1}\n"
                                    "LOD Mapping Range: LOD{2} - LOD{3}\n"
                                    "Simulation Source LOD: LOD{4}"),
                                FText::FromString(GetNameSafe(Asset.GetSourceSkeletalMesh())),
                                FText::AsNumber(Asset.GetDataUVMetadataLODCount()),
                                FText::AsNumber(SetupObject->FirstGeneratedLODIndex),
                                FText::AsNumber(SetupObject->LastGeneratedLODIndex),
                                FText::AsNumber(Asset.GetSimulationLODIndex()));
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
                        SNew(STextBlock)
                        .AutoWrapText(true)
                        .Text_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                        {
                            return BuildAssetSetupSkeletalMeshUVChannelSummary(
                                Asset.GetSourceSkeletalMesh(),
                                SetupObject->OriginalUVChannelIndex,
                                GetEffectiveDataUVChannel());
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
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("AssetSetupDataUVSection", "DWC Data UV"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("AssetSetupPreferredDataUVChannel", "Preferred DWC Data UV Channel"))
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            [
                                SNew(SBox)
                                .WidthOverride(180.0f)
                                [
                                    SNew(SComboBox<TSharedPtr<int32>>)
                                    .OptionsSource(&DataUVChannelOptions)
                                    .InitiallySelectedItem(InitialDataUVChannelItem)
                                    .OnGenerateWidget_Lambda([&Asset, SetupObject](TSharedPtr<int32> Item)
                                    {
                                        const int32 Selection = Item.IsValid()
                                            ? *Item
                                            : RecommendedAssetSetupDataUVSelection;
                                        return SNew(STextBlock)
                                            .Text(BuildAssetSetupDataUVChannelLabel(
                                                Selection,
                                                Asset.GetSourceSkeletalMesh(),
                                                SetupObject->OriginalUVChannelIndex));
                                    })
                                    .OnSelectionChanged_Lambda([SetupObject, &bUseRecommendedDataUVChannel](TSharedPtr<int32> Item, ESelectInfo::Type)
                                    {
                                        if (!Item.IsValid())
                                        {
                                            return;
                                        }

                                        const int32 Selection = *Item;
                                        bUseRecommendedDataUVChannel =
                                            Selection == RecommendedAssetSetupDataUVSelection;
                                        if (!bUseRecommendedDataUVChannel)
                                        {
                                            SetupObject->PreferredDWCDataUVChannelIndex = FMath::Clamp(Selection, 0, 7);
                                        }
                                    })
                                    [
                                        SNew(STextBlock)
                                        .Text_Lambda([&Asset, SetupObject, &bUseRecommendedDataUVChannel]()
                                        {
                                            const int32 Selection = bUseRecommendedDataUVChannel
                                                ? RecommendedAssetSetupDataUVSelection
                                                : SetupObject->PreferredDWCDataUVChannelIndex;
                                            return BuildAssetSetupDataUVChannelLabel(
                                                Selection,
                                                Asset.GetSourceSkeletalMesh(),
                                                SetupObject->OriginalUVChannelIndex);
                                        })
                                    ]
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                            .ColorAndOpacity_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                            {
                                return IsAssetSetupUVSelectionValid(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->OriginalUVChannelIndex,
                                    GetEffectiveDataUVChannel())
                                    ? FStyleColors::AccentBlue
                                    : FStyleColors::Error;
                            })
                            .Text_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                            {
                                return BuildAssetSetupPreferredDataUVInfoText(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->OriginalUVChannelIndex,
                                    GetEffectiveDataUVChannel());
                            })
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(BuildAssetSetupDWCDataUVTargetText())
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .HAlign(HAlign_Left)
                        .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                        [
                            SNew(SButton)
                            .ToolTipText(LOCTEXT("RebuildGeneratedDataUVTooltip", "Apply these settings and rebuild DWC Data UV plus Original-UV topology."))
                            .ContentPadding(FMargin(8.0f, 5.0f))
                            .IsEnabled_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                            {
                                return IsAssetSetupUVSelectionValid(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->OriginalUVChannelIndex,
                                    GetEffectiveDataUVChannel());
                            })
                            .OnClicked_Lambda([&Asset, SetupObject, &Result, &OutAllowOverwriteExistingDataUVChannel, &GetEffectiveDataUVChannel, Dialog]()
                            {
                                ClampAssetSetupLODRangeForMesh(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->FirstGeneratedLODIndex,
                                    SetupObject->LastGeneratedLODIndex);
                                const int32 EffectiveDataUVChannel = GetEffectiveDataUVChannel();
                                if (!ConfirmAssetSetupDataUVOverwrite(
                                        Asset.GetSourceSkeletalMesh(),
                                        SetupObject->OriginalUVChannelIndex,
                                        EffectiveDataUVChannel,
                                        Asset.GetDWCDataUVChannelIndex(),
                                        true,
                                        OutAllowOverwriteExistingDataUVChannel))
                                {
                                    return FReply::Handled();
                                }
                                Result = EWCASetupDialogResult::Rebuild;
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
                                    .Text(LOCTEXT("RebuildGeneratedDataUV", "Rebuild"))
                                ]
                            ]
                        ]
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
                            SNew(STextBlock)
                            .Text(LOCTEXT("AssetSetupLODRangeSection", "LOD Mapping Range"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("AssetSetupFirstGeneratedLODLabel", "First Mapped LOD"))
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            [
                                SNew(SBox)
                                .WidthOverride(120.0f)
                                [
                                    SNew(SSpinBox<int32>)
                                    .MinValue(0)
                                    .MaxValue_Lambda([&Asset]()
                                    {
                                        return FMath::Max(0, GetAssetSetupSkeletalMeshLODCount(Asset.GetSourceSkeletalMesh()) - 1);
                                    })
                                    .Value_Lambda([SetupObject]()
                                    {
                                        return SetupObject->FirstGeneratedLODIndex;
                                    })
                                    .OnValueChanged_Lambda([&Asset, SetupObject](int32 NewValue)
                                    {
                                        SetupObject->FirstGeneratedLODIndex = NewValue;
                                        ClampAssetSetupLODRangeForMesh(
                                            Asset.GetSourceSkeletalMesh(),
                                            SetupObject->FirstGeneratedLODIndex,
                                            SetupObject->LastGeneratedLODIndex);
                                    })
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("AssetSetupLastGeneratedLODLabel", "Last Mapped LOD"))
                            ]
                            + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            [
                                SNew(SBox)
                                .WidthOverride(120.0f)
                                [
                                    SNew(SSpinBox<int32>)
                                    .MinValue_Lambda([SetupObject]()
                                    {
                                        return SetupObject->FirstGeneratedLODIndex;
                                    })
                                    .MaxValue_Lambda([&Asset]()
                                    {
                                        return FMath::Max(0, GetAssetSetupSkeletalMeshLODCount(Asset.GetSourceSkeletalMesh()) - 1);
                                    })
                                    .Value_Lambda([SetupObject]()
                                    {
                                        return SetupObject->LastGeneratedLODIndex;
                                    })
                                    .OnValueChanged_Lambda([&Asset, SetupObject](int32 NewValue)
                                    {
                                        SetupObject->LastGeneratedLODIndex = NewValue;
                                        ClampAssetSetupLODRangeForMesh(
                                            Asset.GetSourceSkeletalMesh(),
                                            SetupObject->FirstGeneratedLODIndex,
                                            SetupObject->LastGeneratedLODIndex);
                                    })
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .AutoWrapText(true)
                            .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                            .ColorAndOpacity(FStyleColors::AccentBlue)
                            .Text_Lambda([&Asset, SetupObject]()
                            {
                                ClampAssetSetupLODRangeForMesh(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->FirstGeneratedLODIndex,
                                    SetupObject->LastGeneratedLODIndex);
                                return BuildAssetSetupLODRangeInfoText(
                                    Asset.GetSourceSkeletalMesh(),
                                    SetupObject->FirstGeneratedLODIndex,
                                    SetupObject->LastGeneratedLODIndex);
                            })
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
                .HAlign(HAlign_Right)
                .Padding(0, 10, 0, 0)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ApplySetup", "Update"))
                    .IsEnabled_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                    {
                        return IsAssetSetupUVSelectionValid(
                            Asset.GetSourceSkeletalMesh(),
                            SetupObject->OriginalUVChannelIndex,
                            GetEffectiveDataUVChannel());
                    })
                    .OnClicked_Lambda([&Asset, SetupObject, &Result, Dialog]()
                    {
                        ClampAssetSetupLODRangeForMesh(
                            Asset.GetSourceSkeletalMesh(),
                            SetupObject->FirstGeneratedLODIndex,
                            SetupObject->LastGeneratedLODIndex);
                        Result = EWCASetupDialogResult::Update;
                        Dialog->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
        if (Result != EWCASetupDialogResult::Closed)
        {
            SetupObject->PreferredDWCDataUVChannelIndex = GetEffectiveDataUVChannel();
            OutSettings = SetupObject->BuildSettings();
        }
        return Result;
    }

    EWCAPendingCloseChoice ShowDWCResolveCloseDialog(const FString& IssueSummary)
    {
        EWCAPendingCloseChoice Choice = EWCAPendingCloseChoice::Cancel;

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
                                               Choice = EWCAPendingCloseChoice::ResolveAndSave;
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
                                               Choice = EWCAPendingCloseChoice::CloseAnyway;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                      [SNew(SButton)
                                           .Text(LOCTEXT("DWCCancelClose", "Cancel"))
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWCAPendingCloseChoice::Cancel;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })]]]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
        return Choice;
    }
} // namespace

const FName FWCAEditor::EditorAppDisplayName(TEXT("WCAEditorApp"));
const FName FWCAEditor::MainTabId(TEXT("WCAEditor_Main"));

FWCAEditor::~FWCAEditor()
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

void FWCAEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UWetClothingAsset* InWetClothingAsset)
{
    check(InWetClothingAsset != nullptr);

    const double InitializeStartTime = FPlatformTime::Seconds();
    WetClothingAsset = InWetClothingAsset;

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateStatic(&ShouldShowWetClothingAssetDetailProperty));
    const double DetailsSetObjectStartTime = FPlatformTime::Seconds();
    DetailsView->SetObject(InWetClothingAsset);
    UE_LOG(
        LogTemp,
        Display,
        TEXT("WCAEditor Details SetObject: '%s' completed in %.2f ms."),
        *GetNameSafe(InWetClothingAsset),
        (FPlatformTime::Seconds() - DetailsSetObjectStartTime) * 1000.0);
    ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FWCAEditor::HandleObjectPropertyChanged);
    AssetSavedHandle = DWCEditorUtils::OnAssetSaveAttemptFinished().AddSP(
        this,
        &FWCAEditor::HandleDWCEditorAssetSaveAttemptFinished);

    const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_WCAEditor_Layout_v3")
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
        FToolBarExtensionDelegate::CreateSP(this, &FWCAEditor::FillAssetToolbar));
    AddToolbarExtender(ToolbarExtender);

    RegenerateMenusAndToolbars();
    UE_LOG(
        LogTemp,
        Display,
        TEXT("WCAEditor Initialize: '%s' completed in %.2f ms."),
        *GetNameSafe(InWetClothingAsset),
        (FPlatformTime::Seconds() - InitializeStartTime) * 1000.0);
}

void FWCAEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "Wet Clothing Asset Editor"));

    FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

    InTabManager->RegisterTabSpawner(MainTabId, FOnSpawnTab::CreateSP(this, &FWCAEditor::SpawnMainTab))
        .SetDisplayName(LOCTEXT("MainTab", "Wet Clothing Asset"))
        .SetGroup(WorkspaceMenuCategory.ToSharedRef());
}

void FWCAEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
    FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
    InTabManager->UnregisterTabSpawner(MainTabId);
}

FName FWCAEditor::GetToolkitFName() const
{
    return EditorAppDisplayName;
}

FText FWCAEditor::GetBaseToolkitName() const
{
    return LOCTEXT("AppLabel", "Wet Clothing Asset Editor");
}

FString FWCAEditor::GetWorldCentricTabPrefix() const
{
    return TEXT("Wet Clothing Asset ");
}

FLinearColor FWCAEditor::GetWorldCentricTabColorScale() const
{
    return FLinearColor(0.1f, 0.1f, 0.1f, 0.5f);
}

bool FWCAEditor::OnRequestClose(EAssetEditorCloseReason InCloseReason)
{
    if (CloseConfirmationState == ECloseConfirmationState::Confirmed)
    {
        return true;
    }

    // A docked asset tab can re-enter OnRequestClose while its modal DWC confirmation is open.
    if (CloseConfirmationState == ECloseConfirmationState::PromptOpen)
    {
        return false;
    }

    if (EditorPanel.IsValid())
    {
        const FWCAEditorIssueStatus Status = EditorPanel->CollectIssueStatus();
        if (Status.HasIssues())
        {
            CloseConfirmationState = ECloseConfirmationState::PromptOpen;
            const EWCAPendingCloseChoice Choice = ShowDWCResolveCloseDialog(Status.BuildSummary());
            CloseConfirmationState = ECloseConfirmationState::Idle;

            if (Choice == EWCAPendingCloseChoice::Cancel)
            {
                return false;
            }

            if (Choice == EWCAPendingCloseChoice::ResolveAndSave)
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

            if (Choice == EWCAPendingCloseChoice::CloseAnyway)
            {
                // The DWC close dialog already received an explicit discard confirmation.
                CloseConfirmationState = ECloseConfirmationState::Confirmed;
                return true;
            }

            // ResolveIssuesAndSave completed successfully, so no additional close prompt is needed.
            CloseConfirmationState = ECloseConfirmationState::Confirmed;
            return true;
        }
    }

    return FAssetEditorToolkit::OnRequestClose(InCloseReason);
}


void FWCAEditor::SaveAsset_Execute()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        DWCEditorUtils::SaveAsset(Asset);
        return;
    }

    FAssetEditorToolkit::SaveAsset_Execute();
}

void FWCAEditor::HandleFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent)
{
    if (EditorPanel.IsValid())
    {
        EditorPanel->RequestRefreshFromAsset();
    }
}

void FWCAEditor::HandleObjectPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
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


void FWCAEditor::HandleDWCEditorAssetSaveAttemptFinished(UObject* SavedAsset, const bool /*bSaveSucceeded*/)
{
    if (SavedAsset != WetClothingAsset.Get())
    {
        return;
    }

    // Saving does not change the authoring inputs shown by the active mode. Update the
    // cached validation state only; rebuilding the mode here reloads the wrinkle texture
    // palette and preview resources after every save.
    if (EditorPanel.IsValid())
    {
        EditorPanel->RefreshStatusFromAsset();
    }
    else if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->RefreshBakeState(false);
    }
}

void FWCAEditor::RefreshAssetStateAndEditor(
    const bool bRunDeepValidation,
    const bool bRebuildActiveModePreview)
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->RefreshBakeState(bRunDeepValidation);
    }
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    if (EditorPanel.IsValid())
    {
        EditorPanel->RequestRefreshFromAsset(bRebuildActiveModePreview);
    }
    RegenerateMenusAndToolbars();
}

TSharedRef<SDockTab> FWCAEditor::SpawnMainTab(const FSpawnTabArgs& Args)
{
    check(Args.GetTabId().TabType == MainTabId);

    return SNew(SDockTab)
        .Label(LOCTEXT("MainTabLabel", "Wet Clothing Asset Editor"))
            [SAssignNew(EditorPanel, SWCAEditorPanel)
                 .DetailsView(DetailsView)
                 .WetClothingAsset(WetClothingAsset.Get())];
}

void FWCAEditor::PostRegenerateMenusAndToolbars()
{
    AddToolbarWidget(BuildModeToolbarWidget());
    GenerateToolbar();
}

void FWCAEditor::FillAssetToolbar(FToolBarBuilder& ToolbarBuilder)
{
    ToolbarBuilder.AddSeparator();
    ToolbarBuilder.AddToolBarButton(
        FUIAction(FExecuteAction::CreateSP(this, &FWCAEditor::HandleAssetSetupClicked)),
        NAME_None,
        LOCTEXT("AssetSetupToolbarLabel", "Asset Setup"),
        LOCTEXT("AssetSetupToolbarTooltip", "Review immutable mesh information and change simulation-data and map-resolution settings."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));
    ToolbarBuilder.AddComboButton(
        FUIAction(
            FExecuteAction(),
            FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeCurrentModeMaps)),
        FOnGetContent::CreateSP(this, &FWCAEditor::BuildBakeMapsMenu),
        LOCTEXT("BakeMapsToolbarLabel", "Bake Maps"),
        LOCTEXT("BakeMapsToolbarTooltip", "Bake pending texture and simulation maps. Disabled when no map output requires work."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.Bake")),
        false);
    if (CurrentMode == EWCAEditorMode::PartEdit)
    {
        ToolbarBuilder.AddComboButton(
            FUIAction(),
            FOnGetContent::CreateSP(this, &FWCAEditor::BuildGenerateMaterialsMenu),
            LOCTEXT("GenerateMaterialsToolbarLabel", "Generate Materials"),
            LOCTEXT("GenerateMaterialsToolbarTooltip", "Generate or refresh wet material assets for this Wet Clothing Asset."),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Material"),
            false);
    }
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
        FUIAction(FExecuteAction::CreateSP(this, &FWCAEditor::HandleValidationClicked)),
        NAME_None,
        ValidationLabel,
        ValidationTooltip,
        FSlateIcon(FAppStyle::GetAppStyleSetName(), ValidationIconName));
}

void FWCAEditor::HandleAssetSetupClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    FDWCWetClothingAssetSetupSettings NewSettings;
    bool bAllowOverwriteExistingDataUVChannel = false;
    const EWCASetupDialogResult DialogResult = ShowWetClothingAssetSetupDialog(
        *Asset,
        NewSettings,
        bAllowOverwriteExistingDataUVChannel);
    if (DialogResult == EWCASetupDialogResult::Closed)
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
    FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);
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

    if (DialogResult == EWCASetupDialogResult::Update && !ChangeSummary.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ChangeSummary));
    }

    if (DialogResult == EWCASetupDialogResult::Rebuild)
    {
        RebuildGeneratedDataUV(bAllowOverwriteExistingDataUVChannel);
    }
}

void FWCAEditor::HandleGenerateGeneratedDataUVClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    // Clicking Rebuild is an unconditional transient-derived-data invalidation boundary,
    // even when a later overwrite confirmation is cancelled.
    FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);

    bool bAllowOverwriteExistingDataUVChannel = false;
    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    if (!ConfirmAssetSetupDataUVOverwrite(
            Asset->GetSourceSkeletalMesh(),
            Setup.OriginalUVChannelIndex,
            Setup.PreferredDWCDataUVChannelIndex,
            Asset->GetDWCDataUVChannelIndex(),
            true,
            bAllowOverwriteExistingDataUVChannel))
    {
        return;
    }

    RebuildGeneratedDataUV(bAllowOverwriteExistingDataUVChannel);
}

void FWCAEditor::RebuildGeneratedDataUV(const bool bAllowOverwriteExistingDataUVChannel)
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
    const FDWCDataUVBuildResult Result = FDWCDataUVBuildService::Generate(*Asset, true, bAllowOverwriteExistingDataUVChannel);
    if (!Result.bSucceeded)
    {
        Asset->SetLastBakeFailure(Result.Message);
        // Generate() already invalidated transient derived data. Refresh the editor as well so
        // panels do not keep local copies of pre-rebuild UV view data after a failed rebuild.
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
        return;
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateDataUVRefreshProgress", "Refreshing Wet Clothing Asset editor state..."));
    Asset->MarkPackageDirty();
    RefreshAssetStateAndEditor();
    FMessageDialog::Open(
        Result.bGeneratedWithWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::FromString(Result.Message));
}

void FWCAEditor::HandleValidationClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    // Explicit validation is the only routine editor action that performs full
    // runtime/map signature and generated-material graph validation.
    Asset->RefreshBakeState(true);

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
        FOnClicked::CreateSP(this, &FWCAEditor::HandleValidationResolveClicked, WeakDialogWindow)));
    FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
#endif
}

FReply FWCAEditor::HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow)
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

bool FWCAEditor::CanBakeGPUMaps() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Asset->HasAnyWettableMaterialSlot() || !Asset->GetSetupSettings().bBuildGPUWetnessMapSimulationData)
    {
        return false;
    }
    const EDWCBakeStatus Status = Asset->GetBakeState().GPUMaps;
    return Status == EDWCBakeStatus::Required || Status == EDWCBakeStatus::OutOfDate || Status == EDWCBakeStatus::Failed;
}

bool FWCAEditor::CanBakeWetnessProfileMaps() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && Asset->HasAnyWettableMaterialSlot() &&
           FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(Asset, nullptr);
}

bool FWCAEditor::CanBakeWrinkleMaps() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Asset->HasWrinkleBakeContent())
    {
        return false;
    }
    const EDWCBakeStatus Status = Asset->GetBakeState().WrinkleMaps;
    return Status == EDWCBakeStatus::Required || Status == EDWCBakeStatus::OutOfDate || Status == EDWCBakeStatus::Failed;
}

bool FWCAEditor::CanBakeTransparencyMaps() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Asset->HasTransparencyBakeContent())
    {
        return false;
    }
    const EDWCBakeStatus Status = Asset->GetBakeState().TransparencyMaps;
    return Status == EDWCBakeStatus::Required || Status == EDWCBakeStatus::OutOfDate || Status == EDWCBakeStatus::Failed;
}

bool FWCAEditor::CanBakeAnyMaps() const
{
    return CanBakeGPUMaps() || CanBakeWetnessProfileMaps() || CanBakeWrinkleMaps() || CanBakeTransparencyMaps();
}

bool FWCAEditor::CanBakeCurrentModeMaps() const
{
    switch (CurrentMode)
    {
    case EWCAEditorMode::PartEdit:
        return CanBakeWetnessProfileMaps() || CanBakeGPUMaps();

    case EWCAEditorMode::WrinkleEdit:
        return CanBakeWrinkleMaps();

    case EWCAEditorMode::TransparencyBake:
        return CanBakeTransparencyMaps();

    default:
        return false;
    }
}

TSharedRef<SWidget> FWCAEditor::BuildBakeMapsMenu()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->RefreshBakeState(false);
    }
    FWCABakeMapsMenuArgs Args;
    Args.EditorMode = CurrentMode;
    Args.OnBakeWetnessProfileMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeWetnessProfileMapsClicked(); });
    Args.OnBakeGPUWetnessMapData = FSimpleDelegate::CreateLambda([this]() { HandleBakeGPUWetnessMapDataClicked(); });
    Args.OnBakeTransparencyRevealMaps = FSimpleDelegate::CreateLambda([this]() { HandleBakeTransparencyRevealMapsClicked(); });
    Args.OnBakeWrinkleNormalMap = FSimpleDelegate::CreateLambda([this]() { HandleBakeWrinkleNormalMapClicked(); });
    Args.CanBakeWetnessProfileMaps = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeWetnessProfileMaps);
    Args.CanBakeGPUWetnessMapData = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeGPUMaps);
    Args.CanBakeTransparencyRevealMaps = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeTransparencyMaps);
    Args.CanBakeWrinkleNormalMap = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeWrinkleMaps);
    return FWCAEditorWidgets::BuildBakeMapsMenu(Args);
}

TSharedRef<SWidget> FWCAEditor::BuildGenerateMaterialsMenu()
{
    FWCAGenerateMaterialsMenuArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.OnGenerateMaterials = FSimpleDelegate::CreateLambda(
        [this]()
        {
            HandleGenerateMaterialsClicked();
        });
    return FWCAEditorWidgets::BuildGenerateMaterialsMenu(Args);
}

FReply FWCAEditor::HandleBakeAllMapsClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    Asset->RefreshBakeState(false);
    const bool bBakeGPU = CanBakeGPUMaps();
    const bool bBakeProfiles = CanBakeWetnessProfileMaps();
    const bool bBakeWrinkles = CanBakeWrinkleMaps();
    const bool bBakeTransparency = CanBakeTransparencyMaps();
    if (!bBakeGPU && !bBakeProfiles && !bBakeWrinkles && !bBakeTransparency)
    {
        return FReply::Handled();
    }

#if WITH_EDITORONLY_DATA
    if (bBakeGPU &&
        (Asset->GetBakeState().GeneratedDataUV != EDWCBakeStatus::Valid ||
         Asset->GetBakeState().OriginalUVTopology != EDWCBakeStatus::Valid))
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
    bool bBakedAnyOutput = false;

    const float TotalWork =
        (bBakeGPU ? 2.0f : 0.0f) +
        (bBakeProfiles ? 1.0f : 0.0f) +
        (bBakeWrinkles ? 1.0f : 0.0f) +
        (bBakeTransparency ? 1.0f : 0.0f) +
        1.0f;
    FScopedSlowTask SlowTask(
        TotalWork,
        FText::FromString(FString::Printf(TEXT("Baking pending maps for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);

    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    if (bBakeGPU)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllGPURuntimeProgress", "Checking GPU runtime data before baking simulation maps..."));
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
            bBakedAnyOutput = true;
        }
        else if (RuntimeFailure.IsEmpty())
        {
            Failures.Add(FString::Printf(TEXT("GPU maps: %s"), *ErrorMessage));
        }
    }

    if (bBakeProfiles)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllWetnessProfileMapsProgress", "Baking wetness profile maps..."));
        FString ProfileSummary;
        bool bProfileWarnings = false;
        if (EditorPanel->BakeWetVisualAssets(ProfileSummary, &bProfileWarnings))
        {
            Sections.Add(ProfileSummary);
            bHadWarnings |= bProfileWarnings;
            bBakedAnyOutput = true;
        }
        else if (!ProfileSummary.IsEmpty())
        {
            Failures.Add(FString::Printf(TEXT("Wetness profile maps: %s"), *ProfileSummary));
        }
    }

    if (bBakeWrinkles)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllWrinkleMapsProgress", "Baking wrinkle maps..."));
        FString WrinkleSummary;
        bool bWrinkleWarnings = false;
        if (EditorPanel->BakeAllWrinkleMaps(WrinkleSummary, &bWrinkleWarnings))
        {
            Sections.Add(WrinkleSummary);
            bHadWarnings |= bWrinkleWarnings;
            bBakedAnyOutput = true;
        }
        else if (!WrinkleSummary.IsEmpty())
        {
            Failures.Add(FString::Printf(TEXT("Wrinkle maps: %s"), *WrinkleSummary));
        }
    }

    if (bBakeTransparency)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BakeAllTransparencyMapsProgress", "Baking transparency maps..."));
        FString TransparencySummary;
        bool bTransparencyWarnings = false;
        if (EditorPanel->BakeTransparencyRevealAssets(TransparencySummary, &bTransparencyWarnings))
        {
            Sections.Add(TransparencySummary);
            bHadWarnings |= bTransparencyWarnings;
            bBakedAnyOutput = true;
        }
        else
        {
            Failures.Add(FString::Printf(TEXT("Transparency maps: %s"), *TransparencySummary));
        }
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeAllRefreshProgress", "Saving baked outputs and refreshing Wet Clothing Asset state..."));
    if (bBakedAnyOutput && !DWCEditorUtils::SaveAsset(Asset))
    {
        Failures.Add(TEXT("Baked outputs were generated, but the Wet Clothing Asset or one of its generated packages could not be saved."));
    }
    Asset->RefreshBakeState(false);
    RefreshAssetStateAndEditor(false);

    FString Summary = FString::Join(Sections, TEXT("\n\n"));
    if (!Failures.IsEmpty())
    {
        if (!Summary.IsEmpty())
        {
            Summary += TEXT("\n\n");
        }
        Summary += FString::Printf(TEXT("Failures:\n- %s"), *FString::Join(Failures, TEXT("\n- ")));
    }

    const EAppMsgCategory Category = !Failures.IsEmpty() || bHadWarnings
        ? EAppMsgCategory::Warning
        : EAppMsgCategory::Success;
    FMessageDialog::Open(Category, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeWetnessProfileMapsClicked()
{
    if (!CanBakeWetnessProfileMaps())
    {
        return FReply::Handled();
    }
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
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !DWCEditorUtils::SaveAsset(Asset))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeWetnessProfileMapsSaveFailed", "Wetness profile maps were generated, but the generated textures or Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }
    RefreshAssetStateAndEditor();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeGPUWetnessMapDataClicked()
{
    if (!CanBakeGPUMaps())
    {
        return FReply::Handled();
    }
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
        LOCTEXT("BakeGPUWetnessMapsRefreshProgress", "Saving GPU simulation maps and Wet Clothing Asset..."));
    if (!DWCEditorUtils::SaveAsset(Asset))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeGPUWetnessMapSaveFailed", "GPU simulation maps were generated, but the Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }
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
                "GPU simulation maps were baked and saved for LOD{3}.\n\nTriangles: {0}\nMaterial-slot maps: {1}\nResolution: {2} x {2}\n\nBefore runtime use, enable Support Compute Skin Cache and enable Skin Cache Usage for the target skeletal-mesh LOD. The wet material must expose the DWC_WetnessMap texture parameter."),
            FText::AsNumber(TriangleCount),
            FText::AsNumber(SlotCount),
            FText::AsNumber(Asset->GetSetupSettings().GetGPUSimulationMapResolution()),
            FText::AsNumber(Asset->GetSimulationLODIndex())));

    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeTransparencyRevealMapsClicked()
{
    if (!CanBakeTransparencyMaps())
    {
        return FReply::Handled();
    }
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
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !DWCEditorUtils::SaveAsset(Asset))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeTransparencyRevealMapsSaveFailed", "Transparency maps were generated, but the generated textures or Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }
    RefreshAssetStateAndEditor();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeWrinkleNormalMapClicked()
{
    if (!CanBakeWrinkleMaps())
    {
        return FReply::Handled();
    }
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    // The bake path saves the WCA and its generated texture. The save-completion
    // callback refreshes status/details without rebuilding the active wrinkle mode.
    return EditorPanel->BakeSelectedWrinkleNormalMap();
}

FReply FWCAEditor::HandleGenerateMaterialsClicked()
{
    return GenerateWetMaterials();
}

FReply FWCAEditor::GenerateWetMaterials()
{

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        FMessageDialog::Open(
            EAppMsgCategory::Error,
            EAppMsgType::Ok,
            LOCTEXT("GenerateMaterialsNoMesh", "Assign a runtime skeletal mesh before generating wet materials."));
        return FReply::Handled();
    }

    if (!Asset->HasValidDataUVForLOD(Asset->GetSimulationLODIndex()))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("GenerateMaterialsNoDataUV", "Generate DWC Data UV before generating wet materials."));
        return FReply::Handled();
    }

    TArray<int32> WettableSlots;
    for (const FWetClothingWettableMaterialSlotState& SlotState : Asset->Authored.PartData.EditableWetPartData.WettableMaterialSlots)
    {
        if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
        {
            WettableSlots.AddUnique(SlotState.MaterialSlotIndex);
        }
    }
    WettableSlots.Sort();
    if (WettableSlots.IsEmpty())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("GenerateMaterialsNoWettableSlots", "Mark at least one material slot as wettable before generating wet materials."));
        return FReply::Handled();
    }

    const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();
    TArray<FString> UpdatedMaterials;
    TArray<FString> Failures;

    FScopedSlowTask SlowTask(
        static_cast<float>(FMath::Max(1, WettableSlots.Num() + 2)),
        FText::FromString(FString::Printf(
            TEXT("Generating unified DWC materials for %s (%d slot%s)..."),
            *GetNameSafe(Asset),
            WettableSlots.Num(),
            WettableSlots.Num() == 1 ? TEXT("") : TEXT("s"))));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        FText::FromString(FString::Printf(
            TEXT("Preparing %d wettable material slot%s..."),
            WettableSlots.Num(),
            WettableSlots.Num() == 1 ? TEXT("") : TEXT("s"))));

    const FWCAMaterialGenerator::FOptions MaterialSetupOptions =
        FWCAMaterialGenerator::MakeOptionsForAsset(Asset, EDWCSimulationMode::VertexCPU);

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
            Asset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
                {
                    return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
                });
        const bool bHadCompleteOverride =
            ExistingOverride != nullptr &&
            ExistingOverride->GeneratedMaterial != nullptr &&
            ExistingOverride->CPUMaterialInstance != nullptr &&
            ExistingOverride->GPUMaterialInstance != nullptr;

        SlowTask.EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(
                TEXT("Generating shared material and CPU/GPU permutations for slot %d from '%s'..."),
                MaterialSlotIndex,
                *GetNameSafe(SourceMaterial))));

        const FWetClothingUnifiedMaterialSetupResult MaterialSet =
            FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);
        if (!MaterialSet.bSucceeded || MaterialSet.GeneratedMaterial == nullptr ||
            MaterialSet.CPUMaterialInstance == nullptr || MaterialSet.GPUMaterialInstance == nullptr)
        {
            Failures.Add(FString::Printf(
                TEXT("Slot %d: %s"),
                MaterialSlotIndex,
                *MaterialSet.Message));
            continue;
        }

        if (ExistingOverride == nullptr)
        {
            ExistingOverride = &Asset->Derived.Inline.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
            ExistingOverride->MaterialSlotIndex = MaterialSlotIndex;
        }

        ExistingOverride->SourceMaterial = SourceMaterial;
        ExistingOverride->GeneratedMaterial = MaterialSet.GeneratedMaterial;
        ExistingOverride->CPUMaterialInstance = MaterialSet.CPUMaterialInstance;
        ExistingOverride->GPUMaterialInstance = MaterialSet.GPUMaterialInstance;
        bUpdatedAnyMaterial = true;

        UpdatedMaterials.Add(FString::Printf(
            TEXT("Slot %d -> shared %s, CPU %s, GPU %s (%s)"),
            MaterialSlotIndex,
            *GetNameSafe(MaterialSet.GeneratedMaterial),
            *GetNameSafe(MaterialSet.CPUMaterialInstance),
            *GetNameSafe(MaterialSet.GPUMaterialInstance),
            bHadCompleteOverride || MaterialSet.bAlreadyConfigured
                ? TEXT("overwritten/refreshed")
                : TEXT("created")));
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateMaterialsRefreshState", "Refreshing generated material state..."));

    if (bUpdatedAnyMaterial)
    {
        Asset->MarkPackageDirty();
    }
    RefreshAssetStateAndEditor();

    const FString MaterialSummary = UpdatedMaterials.IsEmpty()
        ? TEXT("No material overrides were updated.")
        : FString::Printf(
            TEXT("Updated unified material sets:\n- %s"),
            *FString::Join(UpdatedMaterials, TEXT("\n- ")));
    FString Summary = FString::Printf(TEXT("Generated unified DWC material overrides.\n\n%s"), *MaterialSummary);
    if (!Failures.IsEmpty())
    {
        Summary += FString::Printf(TEXT("\n\nFailures:\n- %s"), *FString::Join(Failures, TEXT("\n- ")));
    }

    const EAppMsgCategory Category = Failures.IsEmpty() ? EAppMsgCategory::Success : EAppMsgCategory::Warning;
    FMessageDialog::Open(Category, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

bool FWCAEditor::ResolveIssuesAndSave(FString& OutFailure)
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

    FWCAEditorIssueStatus Status = EditorPanel->CollectIssueStatus(true, true);
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

    const bool bHasWrinkleContent = !Asset->Authored.WrinkleData.BakedWrinkleMaps.IsEmpty() ||
                                    !Asset->Authored.WrinkleData.EditablePatches.IsEmpty() ||
                                    !Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.IsEmpty();
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

    if (!Asset->Authored.TransparencyData.SourceBlueprintClass.IsNull() &&
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

TSharedRef<SWidget> FWCAEditor::BuildModeToolbarWidget()
{
    return SNew(SBox)
        .Padding(FMargin(12.0f, 0.0f))
            [SNew(SHorizontalBox)
             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::PartEdit,
                           TEXT("DWCEditor.Mode.Part"),
                           LOCTEXT("PartEditModeTooltip", "Part Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 16.0f, 0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::WrinkleEdit,
                           TEXT("DWCEditor.Mode.Wrinkle"),
                           LOCTEXT("WrinkleEditModeTooltip", "Wrinkle Edit Mode"))]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f)
                       [BuildModeToggleButton(
                           EWCAEditorMode::TransparencyBake,
                           TEXT("DWCEditor.Mode.Transparency"),
                           LOCTEXT("TransparencyBakeModeTooltip", "Transparency Bake Mode"))]];
}

TSharedRef<SWidget> FWCAEditor::BuildModeToggleButton(EWCAEditorMode Mode, FName IconName, const FText& ToolTipText)
{
    return SNew(SCheckBox)
        .Style(&GetWetClothingModeToggleStyle())
        .Type(ESlateCheckBoxType::ToggleButton)
        .ToolTipText(ToolTipText)
        .IsChecked(this, &FWCAEditor::IsModeChecked, Mode)
        .OnCheckStateChanged(this, &FWCAEditor::HandleModeCheckStateChanged, Mode)
            [SNew(SBox)
                 .WidthOverride(76.0f)
                 .HeightOverride(32.0f)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(SImage)
                          .DesiredSizeOverride(FVector2D(24.0f, 24.0f))
                          .Image(FDWCEditorStyle::GetBrush(IconName))
                          .ColorAndOpacity(this, &FWCAEditor::GetModeIconColor, Mode)]];
}

void FWCAEditor::SetEditorMode(EWCAEditorMode NewMode)
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

    RegenerateMenusAndToolbars();
}

ECheckBoxState FWCAEditor::IsModeChecked(EWCAEditorMode Mode) const
{
    return CurrentMode == Mode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FWCAEditor::HandleModeCheckStateChanged(ECheckBoxState NewState, EWCAEditorMode Mode)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SetEditorMode(Mode);
    }
}

FSlateColor FWCAEditor::GetModeIconColor(EWCAEditorMode Mode) const
{
    if (CurrentMode == Mode)
    {
        return FSlateColor(FLinearColor::White);
    }

    switch (Mode)
    {
    case EWCAEditorMode::PartEdit:
        return FSlateColor(FLinearColor(1.0f, 0.66f, 0.78f, 1.0f));
    case EWCAEditorMode::WrinkleEdit:
        return FSlateColor(FLinearColor(0.62f, 0.95f, 0.62f, 1.0f));
    case EWCAEditorMode::TransparencyBake:
        return FSlateColor(FLinearColor(0.45f, 0.78f, 1.0f, 1.0f));
    default:
        return FSlateColor::UseForeground();
    }
}

#undef LOCTEXT_NAMESPACE
