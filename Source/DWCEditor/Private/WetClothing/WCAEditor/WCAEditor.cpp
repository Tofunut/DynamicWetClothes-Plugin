//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WCAEditor.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/DWCEditorStyle.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Asset/WetClothingAssetFactory.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Foundation/Preview/Slots/DWCEditorPreviewSlotState.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"
#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"
#include "WetClothing/WCAEditor/UI/WCAReportDialogs.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"
#include "DetailsViewArgs.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
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
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WCAEditor"

namespace
{
    bool IsSameMaterialFamily(UMaterialInterface* A, UMaterialInterface* B)
    {
        if (A == nullptr || B == nullptr)
        {
            return A == B;
        }

        UMaterial* ABase = A->GetMaterial();
        UMaterial* BBase = B->GetMaterial();
        return A == B || (ABase != nullptr && ABase == BBase);
    }

    constexpr int32 MaxDWCDataUVChannelIndex = 3;
    constexpr int32 WCAReportDialogFontSize = 10;
    const FLinearColor InfoIconTint(0.32f, 0.65f, 1.0f, 1.0f);
    const FLinearColor WarningIconTint(1.0f, 0.78f, 0.18f, 1.0f);

    TSet<int32> CollectWettableMaterialSlotIndices(const UWetClothingAsset& Asset)
    {
        TSet<int32> MaterialSlotIndices;
        for (const FWetClothingAuthoredMaterialSlot& SlotState :
             Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
        {
            if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
            {
                MaterialSlotIndices.Add(SlotState.MaterialSlotIndex);
            }
        }
        return MaterialSlotIndices;
    }

    bool IsMissingPreparedMeshForSealedLayout(const UWetClothingAsset* Asset)
    {
        return Asset != nullptr &&
               Asset->HasLockedDataUVLayout() &&
               Asset->GetRuntimeSkeletalMesh() == nullptr;
    }

    bool ConfirmMissingPreparedMeshRecovery(const UWetClothingAsset& Asset)
    {
        if (!IsMissingPreparedMeshForSealedLayout(&Asset))
        {
            return true;
        }

        const FText Message = FText::Format(
            LOCTEXT(
                "ConfirmMissingPreparedMeshRecovery",
                "The Prepared Mesh referenced by this Wet Clothing Asset is missing.\n\nDWC can recreate the Prepared Mesh from the Source Mesh and rebuild the DWC UV Channel.\n\nAsset: {0}\n\nContinue?"),
            FText::FromString(GetNameSafe(&Asset)));
        return FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes;
    }

    FDWCDataUVBuildResult GenerateDWCDataUVWithVisibleExclusionConfirmation(
        UWetClothingAsset& Asset,
        const bool bForceNewAsset,
        const bool bAllowOverwriteExistingDataUVChannel,
        const bool bUsePreferredDataUVChannel,
        const TSet<int32>& IncludedMaterialSlotIndices,
        const FDWCDataUVBuildOptions* Options = nullptr)
    {
        FDWCDataUVBuildOptions RetryOptions =
            Options != nullptr ? *Options : FDWCDataUVBuildOptions();
        const FDWCDataUVBuildOptions* OptionsToUse = Options;

        FDWCDataUVBuildResult Result;
        TSet<int32> AcceptedMaterialSlotIndices =
            RetryOptions.ConfirmedVisibleExclusionMaterialSlotIndices;
        const int32 MaxBuildAttempts = FMath::Max(IncludedMaterialSlotIndices.Num(), 1) + 2;

        for (int32 PassIndex = 0; PassIndex < MaxBuildAttempts; ++PassIndex)
        {
            Result = FDWCDataUVBuildService::Generate(
                Asset,
                bForceNewAsset,
                bAllowOverwriteExistingDataUVChannel,
                bUsePreferredDataUVChannel,
                OptionsToUse);

            if (!Result.bRequiresUserConfirmation ||
                Result.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
            {
                return Result;
            }

            const TSet<int32> NewlyAcceptedMaterialSlotIndices =
                WCAReportDialogs::ConfirmDWCDataUVVisibleExclusion(
                    Result,
                    Result.PreparedMesh != nullptr ? Result.PreparedMesh : Asset.GetRuntimeSkeletalMesh(),
                    IncludedMaterialSlotIndices);

            int32 AddedAcceptedSlotCount = 0;
            for (const int32 MaterialSlotIndex : NewlyAcceptedMaterialSlotIndices)
            {
                if (!AcceptedMaterialSlotIndices.Contains(MaterialSlotIndex))
                {
                    AcceptedMaterialSlotIndices.Add(MaterialSlotIndex);
                    ++AddedAcceptedSlotCount;
                }
            }

            if (AddedAcceptedSlotCount == 0)
            {
                return Result;
            }

            RetryOptions.ConfirmedVisibleExclusionMaterialSlotIndices = AcceptedMaterialSlotIndices;
            OptionsToUse = &RetryOptions;
        }

        return Result;
    }

    const FCheckBoxStyle& GetWetClothingModeToggleStyle()
    {
        static const FSlateRoundedBoxBrush UncheckedBrush(FStyleColors::Header, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedHoveredBrush(FStyleColors::Hover, 4.0f);
        static const FSlateRoundedBoxBrush UncheckedPressedBrush(FStyleColors::Recessed, 4.0f);
        static const FSlateRoundedBoxBrush CheckedBrush(FStyleColors::Primary, 4.0f);
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
            return LOCTEXT("AssetSetupUVSummaryNoMesh", "Source mesh UV information is unavailable.");
        }

        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
        {
            return LOCTEXT("AssetSetupUVSummaryNoRenderData", "Source mesh render LOD data is unavailable.");
        }

        FString Summary = FString::Printf(
            TEXT("Simulation Source LOD: LOD0\nOriginal UV: UV%d\nDWC UV Channel: UV%d\n\n"),
            OriginalUVChannelIndex,
            PreferredDWCDataUVChannelIndex);

        for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
        {
            const int32 UVChannelCount = static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
            Summary += FString::Printf(TEXT("LOD%d UV Channels: "), LODIndex);
            if (UVChannelCount <= 0)
            {
                Summary += TEXT("None");
            }
            else
            {
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
            LOCTEXT("AssetSetupLODRangeInfo", "Available LODs: LOD0 - LOD{0}. DWC UV Channel and Original UV topology will be generated for LOD{1} - LOD{2}."),
            FText::AsNumber(LODCount - 1),
            FText::AsNumber(FirstLODIndex),
            FText::AsNumber(LastLODIndex));
    }

    int32 GetAssetSetupDefaultDWCDataUVChannelIndex(const USkeletalMesh* Mesh, const int32 OriginalUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        const int32 PreferredChannel = UVChannelCount > 0
            ? FMath::Clamp(UVChannelCount, 0, MaxDWCDataUVChannelIndex)
            : FMath::Clamp(OriginalUVChannelIndex + 1, 0, MaxDWCDataUVChannelIndex);
        if (PreferredChannel != OriginalUVChannelIndex)
        {
            return PreferredChannel;
        }

        for (int32 UVChannelIndex = 0; UVChannelIndex <= MaxDWCDataUVChannelIndex; ++UVChannelIndex)
        {
            if (UVChannelIndex != OriginalUVChannelIndex)
            {
                return UVChannelIndex;
            }
        }

        return PreferredChannel;
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
                "DWC UV Channel cannot use the Original UV channel.");
        }

        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        if (DataUVChannelIndex >= 0 && DataUVChannelIndex < UVChannelCount)
        {
            return FText::Format(
                LOCTEXT(
                    "AssetSetupDataUVExistingChannelInfo",
                    "UV{0} already contains data and will be overwritten."),
                FText::AsNumber(DataUVChannelIndex));
        }

        return FText::GetEmpty();
    }

    FSlateColor GetAssetSetupDataUVMessageColor(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        if (Mesh == nullptr ||
            UVChannelCount <= 0 ||
            OriginalUVChannelIndex < 0 ||
            OriginalUVChannelIndex >= UVChannelCount ||
            DataUVChannelIndex == OriginalUVChannelIndex ||
            DataUVChannelIndex < 0 ||
            DataUVChannelIndex > MaxDWCDataUVChannelIndex)
        {
            return FStyleColors::Error;
        }

        if (DataUVChannelIndex < UVChannelCount)
        {
            return FSlateColor(WarningIconTint);
        }

        return FSlateColor(InfoIconTint);
    }

    const FSlateBrush* GetAssetSetupDataUVMessageIconBrush(
        const USkeletalMesh* Mesh,
        const int32 OriginalUVChannelIndex,
        const int32 DataUVChannelIndex)
    {
        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
        if (Mesh == nullptr ||
            UVChannelCount <= 0 ||
            OriginalUVChannelIndex < 0 ||
            OriginalUVChannelIndex >= UVChannelCount ||
            DataUVChannelIndex == OriginalUVChannelIndex ||
            DataUVChannelIndex < 0 ||
            DataUVChannelIndex > MaxDWCDataUVChannelIndex)
        {
            return FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Status.Error"));
        }

        return DataUVChannelIndex < UVChannelCount
            ? FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"))
            : FAppStyle::GetBrush(TEXT("Icons.InfoWithColor"));
    }

    TSharedRef<SWidget> BuildAssetSetupInfoTextRow(
        const TAttribute<FText>& Text,
        const TAttribute<EVisibility>& Visibility)
    {
        return SNew(SHorizontalBox)
            .Visibility(Visibility)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Top)
            .Padding(0.0f, 1.0f, 6.0f, 0.0f)
            [
                SNew(SBox)
                .WidthOverride(16.0f)
                .HeightOverride(16.0f)
                [
                    SNew(SImage)
                    .Image(FAppStyle::GetBrush(TEXT("Icons.InfoWithColor")))
                    .ColorAndOpacity(InfoIconTint)
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .AutoWrapText(true)
                .Text(Text)
                .Font(FAppStyle::GetFontStyle(TEXT("NormalFont")))
                .ColorAndOpacity(InfoIconTint)
            ];
    }

    TSharedRef<SWidget> BuildAssetSetupInfoTextRow(const TAttribute<FText>& Text)
    {
        return BuildAssetSetupInfoTextRow(Text, TAttribute<EVisibility>(EVisibility::Visible));
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
               DataUVChannelIndex <= MaxDWCDataUVChannelIndex &&
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
                    "Select an existing Original UV channel and a different DWC UV Channel."));
            return false;
        }

        // Reusing the channel already owned by this WCA is not a new destructive choice.
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
                "UV{0} already contains UV data.\n\nWriting DWC UV Channel to this channel will replace the existing UV{0} data on the target mesh.\n\nContinue?"),
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

    bool IsValidationActionRequiredStatus(const EDWCBakeStatus Status)
    {
        return Status == EDWCBakeStatus::Required ||
            Status == EDWCBakeStatus::OutOfDate ||
            Status == EDWCBakeStatus::Failed;
    }

    DECLARE_DELEGATE_OneParam(FDWCOnValidationFixRequested, EWCAValidationFixKind);

    bool IsValidationSectionBlockedByManualIssue(
        const FWCAValidationReport& Report,
        EWCAValidationSection Section);

    struct FDWCValidationSectionView
    {
        EWCAValidationSection Section = EWCAValidationSection::DataUV;
        TArray<const FWCAValidationIssue*> Issues;
        EWCAValidationSeverity MaxSeverity = EWCAValidationSeverity::Info;
        EWCAValidationFixKind CommonFixKind = EWCAValidationFixKind::None;
        FText NotApplicableReason;
        bool bApplicable = true;
        bool bHasManualIssue = false;
        bool bBlockedByManualIssue = false;
    };

    struct FDWCValidationIssueDisplay
    {
        const FWCAValidationIssue* Representative = nullptr;
        TArray<const FWCAValidationIssue*> GroupedIssues;
        FText Title;
        FText Status;
        FText Detail;
        FText RequiredAction;
        EWCAValidationSeverity Severity = EWCAValidationSeverity::Info;
        EWCAValidationFixKind FixKind = EWCAValidationFixKind::None;
    };

    FSlateFontInfo MakeValidationFont(const int32 /*Size*/ = WCAReportDialogFontSize, const bool bBold = false)
    {
        return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), WCAReportDialogFontSize);
    }

    FString NormalizeValidationAggregationKeyText(const FString& InText)
    {
        const FString LowerText = InText.ToLower();
        FString Result;
        Result.Reserve(InText.Len());

        int32 Index = 0;
        while (Index < InText.Len())
        {
            if (LowerText.Mid(Index).StartsWith(TEXT("slot ")))
            {
                Result += TEXT("slot #");
                Index += 5;
                while (Index < InText.Len() && FChar::IsWhitespace(InText[Index]))
                {
                    ++Index;
                }
                while (Index < InText.Len() && FChar::IsDigit(InText[Index]))
                {
                    ++Index;
                }
                continue;
            }

            Result.AppendChar(FChar::ToLower(InText[Index]));
            ++Index;
        }
        return Result;
    }

    FString StripLeadingContextFromDetail(const FString& Detail, const FString& ContextLabel)
    {
        if (!ContextLabel.IsEmpty())
        {
            const FString WithSpace = ContextLabel + TEXT(" ");
            if (Detail.StartsWith(WithSpace, ESearchCase::IgnoreCase))
            {
                return Detail.RightChop(WithSpace.Len());
            }

            const FString WithColon = ContextLabel + TEXT(": ");
            if (Detail.StartsWith(WithColon, ESearchCase::IgnoreCase))
            {
                return Detail.RightChop(WithColon.Len());
            }
        }
        return Detail;
    }

    FString RemoveSlotQualifierForGroupedDisplay(const FString& InDetail)
    {
        FString Result = InDetail;
        const FString LowerResult = Result.ToLower();
        const int32 InSlotIndex = LowerResult.Find(TEXT(" in slot "));
        if (InSlotIndex != INDEX_NONE)
        {
            int32 RemoveEnd = InSlotIndex + 9;
            while (RemoveEnd < Result.Len() && FChar::IsWhitespace(Result[RemoveEnd]))
            {
                ++RemoveEnd;
            }
            while (RemoveEnd < Result.Len() && FChar::IsDigit(Result[RemoveEnd]))
            {
                ++RemoveEnd;
            }
            Result.RemoveAt(InSlotIndex, RemoveEnd - InSlotIndex, EAllowShrinking::No);
        }
        return Result;
    }

    bool TryExtractSlotNumberFromContext(const FString& Context, int32& OutSlotIndex)
    {
        OutSlotIndex = INDEX_NONE;
        const FString LowerContext = Context.ToLower();
        const int32 SlotIndex = LowerContext.Find(TEXT("slot "));
        if (SlotIndex == INDEX_NONE)
        {
            return false;
        }

        int32 NumberStart = SlotIndex + 5;
        while (NumberStart < Context.Len() && FChar::IsWhitespace(Context[NumberStart]))
        {
            ++NumberStart;
        }
        int32 NumberEnd = NumberStart;
        while (NumberEnd < Context.Len() && FChar::IsDigit(Context[NumberEnd]))
        {
            ++NumberEnd;
        }
        if (NumberEnd == NumberStart)
        {
            return false;
        }

        OutSlotIndex = FCString::Atoi(*Context.Mid(NumberStart, NumberEnd - NumberStart));
        return OutSlotIndex != INDEX_NONE;
    }

    FString NormalizeValidationDisplayText(const FString& InText)
    {
        FString Result;
        Result.Reserve(InText.Len());
        for (const TCHAR Character : InText)
        {
            if (FChar::IsAlnum(Character))
            {
                Result.AppendChar(FChar::ToLower(Character));
            }
        }
        return Result;
    }

    bool IsRedundantValidationDetail(
        const FText& Title,
        const FText& Status,
        const FString& Detail)
    {
        const FString NormalizedDetail = NormalizeValidationDisplayText(Detail);
        if (NormalizedDetail.IsEmpty())
        {
            return true;
        }

        const FString NormalizedTitle = NormalizeValidationDisplayText(Title.ToString());
        const FString NormalizedStatus = NormalizeValidationDisplayText(Status.ToString());
        if (NormalizedDetail == NormalizedTitle || NormalizedDetail == NormalizedStatus)
        {
            return true;
        }

        const TArray<FString> RedundantCombinations = {
            NormalizedTitle + NormalizedStatus,
            NormalizedTitle + TEXT("is") + NormalizedStatus,
            NormalizedTitle + TEXT("are") + NormalizedStatus,
            NormalizedStatus + NormalizedTitle
        };
        if (RedundantCombinations.Contains(NormalizedDetail))
        {
            return true;
        }

        // Common generated-data messages only restate an Out-of-Date status.
        // Keep explanations that add a cause such as source changes, missing inputs, or mismatches.
        if (NormalizedStatus == TEXT("outofdate"))
        {
            const FString LowerDetail = Detail.ToLower();
            const bool bAddsCause =
                LowerDetail.Contains(TEXT("because")) ||
                LowerDetail.Contains(TEXT("due to")) ||
                LowerDetail.Contains(TEXT("after")) ||
                LowerDetail.Contains(TEXT("changed")) ||
                LowerDetail.Contains(TEXT("mismatch")) ||
                LowerDetail.Contains(TEXT("missing from")) ||
                LowerDetail.Contains(TEXT("does not")) ||
                LowerDetail.Contains(TEXT("cannot")) ||
                LowerDetail.Contains(TEXT("could not")) ||
                LowerDetail.Contains(TEXT("invalid"));
            const bool bOnlyRestatesStatus =
                LowerDetail.Contains(TEXT("out of date")) && !bAddsCause;
            if (bOnlyRestatesStatus)
            {
                return true;
            }
        }

        return false;
    }

    TArray<FDWCValidationIssueDisplay> BuildValidationIssueDisplays(const FDWCValidationSectionView& View)
    {
        TArray<FDWCValidationIssueDisplay> Result;
        TMap<FString, int32> KeyToIndex;

        for (const FWCAValidationIssue* Issue : View.Issues)
        {
            if (Issue == nullptr)
            {
                continue;
            }

            const bool bMergedGPURuntimeIssue =
                View.Section == EWCAValidationSection::RuntimeData &&
                Issue->Section == EWCAValidationSection::GPUSimulationMaps;
            const FText DisplayTitle = bMergedGPURuntimeIssue
                ? LOCTEXT("ValidationMergedGPURuntimeDataTitle", "GPU Runtime Data")
                : Issue->Title;
            const EWCAValidationFixKind DisplayFixKind = bMergedGPURuntimeIssue
                ? EWCAValidationFixKind::BakeGPUMaps
                : Issue->FixKind;
            const FString Key = View.Section == EWCAValidationSection::RuntimeData
                ? NormalizeValidationAggregationKeyText(DisplayTitle.ToString())
                : FString::Printf(
                    TEXT("%d|%d|%d|%s|%s|%s|%s"),
                    static_cast<int32>(Issue->Severity),
                    static_cast<int32>(Issue->Section),
                    static_cast<int32>(Issue->FixKind),
                    *NormalizeValidationAggregationKeyText(Issue->Title.ToString()),
                    *NormalizeValidationAggregationKeyText(Issue->Status.ToString()),
                    *NormalizeValidationAggregationKeyText(Issue->RequiredAction.ToString()),
                    *NormalizeValidationAggregationKeyText(Issue->Detail.ToString()));

            int32* ExistingIndex = KeyToIndex.Find(Key);
            if (ExistingIndex == nullptr)
            {
                FDWCValidationIssueDisplay Display;
                Display.Representative = Issue;
                Display.GroupedIssues.Add(Issue);
                Display.Title = DisplayTitle;
                Display.Status = Issue->Status;
                Display.RequiredAction = bMergedGPURuntimeIssue
                    ? LOCTEXT("ValidationMergedGPURuntimeDataAction", "Use Build for Runtime > Build GPU Runtime Data.")
                    : Issue->RequiredAction;
                Display.Severity = Issue->Severity;
                Display.FixKind = DisplayFixKind;
                Result.Add(MoveTemp(Display));
                KeyToIndex.Add(Key, Result.Num() - 1);
            }
            else
            {
                FDWCValidationIssueDisplay& ExistingDisplay = Result[*ExistingIndex];
                ExistingDisplay.GroupedIssues.Add(Issue);
                if (static_cast<uint8>(Issue->Severity) > static_cast<uint8>(ExistingDisplay.Severity))
                {
                    ExistingDisplay.Representative = Issue;
                    ExistingDisplay.Severity = Issue->Severity;
                    ExistingDisplay.Status = Issue->Status;
                }
                else if (!ExistingDisplay.Status.EqualTo(Issue->Status))
                {
                    ExistingDisplay.Status = LOCTEXT("ValidationRuntimeDataActionRequiredStatus", "Action Required");
                }
            }
        }

        for (FDWCValidationIssueDisplay& Display : Result)
        {
            if (Display.Representative == nullptr)
            {
                continue;
            }

            const FWCAValidationIssue* Representative = Display.Representative;
            const FString RepresentativeContext = Representative->ContextLabel.ToString();
            FString DetailText = StripLeadingContextFromDetail(
                Representative->Detail.ToString(),
                RepresentativeContext);
            if (IsRedundantValidationDetail(Display.Title, Display.Status, DetailText))
            {
                DetailText.Reset();
            }

            TArray<FString> ContextStrings;
            TArray<int32> SlotIndices;
            bool bAllContextsAreSlots = true;
            for (const FWCAValidationIssue* GroupedIssue : Display.GroupedIssues)
            {
                if (GroupedIssue == nullptr)
                {
                    continue;
                }

                const FString ContextText = GroupedIssue->ContextLabel.ToString();
                if (ContextText.IsEmpty())
                {
                    bAllContextsAreSlots = false;
                    continue;
                }

                ContextStrings.AddUnique(ContextText);
                int32 SlotIndex = INDEX_NONE;
                if (TryExtractSlotNumberFromContext(ContextText, SlotIndex))
                {
                    SlotIndices.AddUnique(SlotIndex);
                }
                else
                {
                    bAllContextsAreSlots = false;
                }
            }
            ContextStrings.Sort();
            SlotIndices.Sort();

            if (Display.GroupedIssues.Num() > 1)
            {
                FString CombinedDetail;
                if (bAllContextsAreSlots && !SlotIndices.IsEmpty())
                {
                    TArray<FString> SlotStrings;
                    SlotStrings.Reserve(SlotIndices.Num());
                    for (const int32 SlotIndex : SlotIndices)
                    {
                        SlotStrings.Add(FString::FromInt(SlotIndex));
                    }
                    CombinedDetail += FString::Printf(
                        TEXT("Affected slots: %s\n"),
                        *FString::Join(SlotStrings, TEXT(", ")));
                }
                else if (!ContextStrings.IsEmpty())
                {
                    CombinedDetail += FString::Printf(
                        TEXT("Affected items: %s\n"),
                        *FString::Join(ContextStrings, TEXT(", ")));
                }

                if (!DetailText.IsEmpty())
                {
                    CombinedDetail += RemoveSlotQualifierForGroupedDisplay(DetailText);
                }
                CombinedDetail.TrimEndInline();
                Display.Detail = CombinedDetail.IsEmpty()
                    ? FText::GetEmpty()
                    : FText::FromString(CombinedDetail);
            }
            else
            {
                Display.Title = Representative->ContextLabel.IsEmpty()
                    ? Display.Title
                    : FText::Format(
                        LOCTEXT("ValidationIssueContextTitle", "{0} - {1}"),
                        Representative->ContextLabel,
                        Display.Title);
                Display.Detail = DetailText.IsEmpty()
                    ? FText::GetEmpty()
                    : FText::FromString(DetailText);
            }
        }

        return Result;
    }

    bool IsValidationSectionApplicable(
        const UWetClothingAsset& Asset,
        const EWCAValidationSection Section,
        const bool bHasIssues)
    {
        if (bHasIssues)
        {
            return true;
        }

        const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();
        switch (Section)
        {
        case EWCAValidationSection::DataUV:
            return true;
        case EWCAValidationSection::RuntimeData:
            return Setup.bBuildCPUVertexSimulationData ||
                   Setup.bBuildGPUWetnessMapSimulationData ||
                   Asset.HasCPURuntimeDataPayload() ||
                   Asset.HasGPURuntimeDataPayload();
        case EWCAValidationSection::GeneratedMaterials:
        case EWCAValidationSection::RenderProfileData:
            return Asset.HasAnyWettableMaterialSlot();
        case EWCAValidationSection::GPUSimulationMaps:
            return Setup.bBuildGPUWetnessMapSimulationData;
        case EWCAValidationSection::WrinkleMaps:
            return HasWrinkleValidationData(Asset);
        case EWCAValidationSection::TransparencyMaps:
            return Asset.HasTransparencyBakeContent();
        case EWCAValidationSection::FailureDetails:
            return bHasIssues;
        default:
            return true;
        }
    }

    FText GetValidationSectionNotApplicableReason(const EWCAValidationSection Section)
    {
        switch (Section)
        {
        case EWCAValidationSection::RuntimeData:
            return LOCTEXT("ValidationRuntimeNotApplicable", "CPU and GPU runtime-data generation are disabled for this asset.");
        case EWCAValidationSection::GeneratedMaterials:
            return LOCTEXT("ValidationMaterialsNotApplicable", "No material slots are marked Wettable.");
        case EWCAValidationSection::GPUSimulationMaps:
            return LOCTEXT("ValidationGPUDataNotApplicable", "GPU Runtime Data generation is disabled for this asset.");
        case EWCAValidationSection::RenderProfileData:
            return LOCTEXT("ValidationRenderProfileNotApplicable", "No wettable material slots require a Render Profile Lookup Texture.");
        case EWCAValidationSection::WrinkleMaps:
            return LOCTEXT("ValidationWrinkleNotApplicable", "This asset has no authored or baked wrinkle textures.");
        case EWCAValidationSection::TransparencyMaps:
            return LOCTEXT("ValidationTransparencyNotApplicable", "This asset has no transparency layers or textures to validate.");
        case EWCAValidationSection::FailureDetails:
            return LOCTEXT("ValidationFailureNotApplicable", "No unclassified internal failures were recorded.");
        default:
            return LOCTEXT("ValidationSectionNotApplicableGeneric", "This validation section is not applicable to the current asset configuration.");
        }
    }

    TArray<FDWCValidationSectionView> BuildValidationSectionViews(
        const FWCAValidationReport& Report,
        const UWetClothingAsset& Asset)
    {
        static const EWCAValidationSection SectionOrder[] = {
            EWCAValidationSection::DataUV,
            EWCAValidationSection::RuntimeData,
            EWCAValidationSection::GeneratedMaterials,
            EWCAValidationSection::RenderProfileData,
            EWCAValidationSection::WrinkleMaps,
            EWCAValidationSection::TransparencyMaps,
            EWCAValidationSection::FailureDetails
        };

        TArray<FDWCValidationSectionView> Result;
        for (const EWCAValidationSection Section : SectionOrder)
        {
            FDWCValidationSectionView View;
            View.Section = Section;
            bool bCommonFixInitialized = false;
            bool bCommonFixValid = true;

            for (const FWCAValidationIssue& Issue : Report.Issues)
            {
                const bool bBelongsToRuntimeData =
                    Section == EWCAValidationSection::RuntimeData &&
                    (Issue.Section == EWCAValidationSection::RuntimeData ||
                     Issue.Section == EWCAValidationSection::GPUSimulationMaps);
                if (!bBelongsToRuntimeData && Issue.Section != Section)
                {
                    continue;
                }

                View.Issues.Add(&Issue);
                if (static_cast<uint8>(Issue.Severity) > static_cast<uint8>(View.MaxSeverity))
                {
                    View.MaxSeverity = Issue.Severity;
                }
                View.bHasManualIssue |= Issue.FixKind == EWCAValidationFixKind::Manual;

                if (Issue.FixKind == EWCAValidationFixKind::None ||
                    Issue.FixKind == EWCAValidationFixKind::Manual)
                {
                    bCommonFixValid = false;
                }
                else if (!bCommonFixInitialized)
                {
                    View.CommonFixKind = Issue.FixKind;
                    bCommonFixInitialized = true;
                }
                else if (View.CommonFixKind != Issue.FixKind)
                {
                    bCommonFixValid = false;
                }
            }

            View.bApplicable = IsValidationSectionApplicable(Asset, Section, !View.Issues.IsEmpty());
            if (!View.bApplicable)
            {
                View.NotApplicableReason = GetValidationSectionNotApplicableReason(Section);
            }

            View.bBlockedByManualIssue = !View.Issues.IsEmpty() &&
                IsValidationSectionBlockedByManualIssue(Report, Section);
            if (!bCommonFixInitialized || !bCommonFixValid || View.bBlockedByManualIssue)
            {
                View.CommonFixKind = EWCAValidationFixKind::None;
            }
            Result.Add(MoveTemp(View));
        }
        return Result;
    }

    bool HasManualValidationIssueInSection(
        const FWCAValidationReport& Report,
        const EWCAValidationSection Section)
    {
        return Report.Issues.ContainsByPredicate(
            [Section](const FWCAValidationIssue& Issue)
            {
                return Issue.Section == Section &&
                       Issue.FixKind == EWCAValidationFixKind::Manual;
            });
    }

    bool IsValidationSectionBlockedByManualIssue(
        const FWCAValidationReport& Report,
        const EWCAValidationSection Section)
    {
        if (HasManualValidationIssueInSection(Report, Section))
        {
            return true;
        }

        const bool bDataUVBlocked =
            HasManualValidationIssueInSection(Report, EWCAValidationSection::DataUV);
        if (bDataUVBlocked && Section != EWCAValidationSection::FailureDetails)
        {
            return true;
        }

        switch (Section)
        {
        case EWCAValidationSection::GPUSimulationMaps:
            return HasManualValidationIssueInSection(Report, EWCAValidationSection::RuntimeData);
        case EWCAValidationSection::RenderProfileData:
            return HasManualValidationIssueInSection(Report, EWCAValidationSection::GeneratedMaterials);
        default:
            return false;
        }
    }

    FWCAValidationReport MakeUnblockedValidationReport(const FWCAValidationReport& Report)
    {
        FWCAValidationReport Result;
        Result.Diagnostics = Report.Diagnostics;
        for (const FWCAValidationIssue& Issue : Report.Issues)
        {
            if (Issue.FixKind != EWCAValidationFixKind::Manual &&
                !IsValidationSectionBlockedByManualIssue(Report, Issue.Section))
            {
                Result.Issues.Add(Issue);
            }
        }
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

    FText GetValidationSectionTitle(const EWCAValidationSection Section)
    {
        switch (Section)
        {
        case EWCAValidationSection::DataUV: return LOCTEXT("ValidationSectionDataUV", "Prepared Mesh UV Layout");
        case EWCAValidationSection::RuntimeData: return LOCTEXT("ValidationSectionRuntimeData", "Runtime Data");
        case EWCAValidationSection::GeneratedMaterials: return LOCTEXT("ValidationSectionGeneratedMaterials", "Generated Materials");
        case EWCAValidationSection::GPUSimulationMaps: return LOCTEXT("ValidationSectionGPUData", "GPU Runtime Data");
        case EWCAValidationSection::RenderProfileData: return LOCTEXT("ValidationSectionRenderProfileData", "Render Profile Lookup Texture");
        case EWCAValidationSection::WrinkleMaps: return LOCTEXT("ValidationSectionWrinkleTextures", "Wrinkle Textures");
        case EWCAValidationSection::TransparencyMaps: return LOCTEXT("ValidationSectionTransparencyTextures", "Transparency Textures");
        case EWCAValidationSection::FailureDetails: return LOCTEXT("ValidationSectionInternalFailure", "Internal Failure");
        default: return LOCTEXT("ValidationSectionUnknown", "Validation");
        }
    }

    const FSlateBrush* GetValidationSectionIcon(const EWCAValidationSection Section)
    {
        switch (Section)
        {
        case EWCAValidationSection::DataUV:
            return FAppStyle::GetBrush(TEXT("ClassIcon.SkeletalMesh"));
        case EWCAValidationSection::RuntimeData:
        case EWCAValidationSection::GPUSimulationMaps:
            return FAppStyle::GetBrush(TEXT("ClassIcon.DataAsset"));
        case EWCAValidationSection::GeneratedMaterials:
            return FAppStyle::GetBrush(TEXT("ClassIcon.Material"));
        case EWCAValidationSection::RenderProfileData:
        case EWCAValidationSection::WrinkleMaps:
        case EWCAValidationSection::TransparencyMaps:
            return FAppStyle::GetBrush(TEXT("ClassIcon.Texture2D"));
        case EWCAValidationSection::FailureDetails:
            return FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Validation.Failure"));
        default:
            return FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Validation.Diagnostics"));
        }
    }

    const FSlateBrush* GetValidationSectionStateIcon(const FDWCValidationSectionView& View)
    {
        if (!View.bApplicable)
        {
            return FAppStyle::GetBrush(TEXT("Icons.Minus"));
        }
        if (View.Issues.IsEmpty())
        {
            return FAppStyle::GetBrush(TEXT("Icons.SuccessWithColor"));
        }
        return View.MaxSeverity == EWCAValidationSeverity::Error
            ? FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Status.Error"))
            : FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"));
    }

    FSlateColor GetValidationSeverityColor(const EWCAValidationSeverity Severity)
    {
        switch (Severity)
        {
        case EWCAValidationSeverity::Error:
            return FSlateColor(FLinearColor(1.0f, 0.22f, 0.16f, 1.0f));
        case EWCAValidationSeverity::Warning:
            return FSlateColor(WarningIconTint);
        case EWCAValidationSeverity::Info:
        default:
            return FSlateColor(FLinearColor(0.32f, 0.65f, 1.0f, 1.0f));
        }
    }

    FSlateColor GetValidationSectionColor(const FDWCValidationSectionView& View)
    {
        if (!View.bApplicable)
        {
            return FSlateColor(FLinearColor(0.42f, 0.44f, 0.48f, 1.0f));
        }
        if (View.Issues.IsEmpty())
        {
            return FSlateColor(FLinearColor(0.24f, 0.78f, 0.38f, 1.0f));
        }
        return GetValidationSeverityColor(View.MaxSeverity);
    }

    FText GetValidationSectionCountText(const FDWCValidationSectionView& View)
    {
        return View.bApplicable
            ? FText::AsNumber(BuildValidationIssueDisplays(View).Num())
            : LOCTEXT("ValidationSectionNotApplicableCount", "-");
    }

    FText GetValidationSectionCountTooltip(const FDWCValidationSectionView& View)
    {
        if (!View.bApplicable)
        {
            return View.NotApplicableReason;
        }
        if (View.Issues.IsEmpty())
        {
            return LOCTEXT("ValidationSectionNoActiveIssuesTooltip", "No active issues.");
        }
        return FText::Format(
            LOCTEXT("ValidationSectionActiveIssueCountTooltip", "{0} active validation issue(s)."),
            FText::AsNumber(BuildValidationIssueDisplays(View).Num()));
    }

    FLinearColor GetValidationIssueBackground(const EWCAValidationSeverity Severity)
    {
        switch (Severity)
        {
        case EWCAValidationSeverity::Error: return FLinearColor(0.20f, 0.035f, 0.025f, 1.0f);
        case EWCAValidationSeverity::Warning: return FLinearColor(0.16f, 0.105f, 0.025f, 1.0f);
        case EWCAValidationSeverity::Info:
        default: return FLinearColor(0.035f, 0.075f, 0.14f, 1.0f);
        }
    }

    FText GetValidationFixLabel(const EWCAValidationFixKind FixKind)
    {
        switch (FixKind)
        {
        case EWCAValidationFixKind::None:
        case EWCAValidationFixKind::Manual:
            return FText::GetEmpty();
        default:
            return LOCTEXT("ValidationFixResolve", "Resolve");
        }
    }

    TArray<FText> GetValidationSectionChecks(const EWCAValidationSection Section)
    {
        TArray<FText> Checks;
        switch (Section)
        {
        case EWCAValidationSection::DataUV:
            Checks = {
                LOCTEXT("ValidationCheckDataUVAvailability", "Prepared mesh UV layout availability and build version"),
                LOCTEXT("ValidationCheckOriginalUVTopology", "Original UV topology data"),
                LOCTEXT("ValidationCheckPreparedMeshCompatibility", "Prepared mesh and UV-channel compatibility")
            };
            break;
        case EWCAValidationSection::RuntimeData:
            Checks = {
                LOCTEXT("ValidationCheckCPURuntimeData", "CPU runtime data availability and freshness"),
                LOCTEXT("ValidationCheckGPURuntimeData", "GPU runtime data availability and freshness, including simulation lookup data"),
                LOCTEXT("ValidationCheckRuntimeSaveState", "Unsaved runtime payload changes")
            };
            break;
        case EWCAValidationSection::GeneratedMaterials:
            Checks = {
                LOCTEXT("ValidationCheckGeneratedMaterialAvailability", "Generated material and CPU/GPU instance availability"),
                LOCTEXT("ValidationCheckGeneratedMaterialSource", "Source material changes and parent consistency"),
                LOCTEXT("ValidationCheckGeneratedMaterialFunctions", "Required material functions and runtime parameters")
            };
            break;
        case EWCAValidationSection::GPUSimulationMaps:
            Checks = {
                LOCTEXT("ValidationCheckGPUDataAvailability", "GPU Runtime Data simulation lookup availability"),
                LOCTEXT("ValidationCheckGPUDataSignature", "Build signature and DWC UV Channel compatibility"),
                LOCTEXT("ValidationCheckGPUDataSaveState", "Unsaved GPU Runtime Data simulation lookup")
            };
            break;
        case EWCAValidationSection::RenderProfileData:
            Checks = {
                LOCTEXT("ValidationCheckWetPartDataTexture", "Wet Part Data Texture and local profile mapping"),
                LOCTEXT("ValidationCheckSurfaceWaterInputs", "Surface Water profile inputs"),
                LOCTEXT("ValidationCheckPreparedDropletTextures", "Prepared Droplet Normal and Mask references"),
                LOCTEXT("ValidationCheckRenderProfileSlots", "Material slot connections")
            };
            break;
        case EWCAValidationSection::WrinkleMaps:
            Checks = {
                LOCTEXT("ValidationCheckWrinkleTextures", "Baked wrinkle texture availability and freshness"),
                LOCTEXT("ValidationCheckCustomWrinkleTextures", "Custom wrinkle texture assignments"),
                LOCTEXT("ValidationCheckWrinkleSlots", "Per-slot wrinkle texture output state")
            };
            break;
        case EWCAValidationSection::TransparencyMaps:
            Checks = {
                LOCTEXT("ValidationCheckTransparencyInputs", "Transparency layer inputs"),
                LOCTEXT("ValidationCheckTransparencySlots", "Source and target material slot relationships"),
                LOCTEXT("ValidationCheckTransparencyOutputs", "Stored transparency texture availability and freshness")
            };
            break;
        case EWCAValidationSection::FailureDetails:
            Checks = {
                LOCTEXT("ValidationCheckRecentFailures", "Unclassified internal build and validation failures")
            };
            break;
        default:
            break;
        }
        return Checks;
    }

    TSharedRef<SWidget> BuildValidationEmptySectionBody(const FDWCValidationSectionView& View)
    {
        const FSlateColor StateColor = GetValidationSectionColor(View);
        TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
        Content->AddSlot()
        .AutoHeight()
        [
            SNew(STextBlock)
            .Text(View.bApplicable
                ? LOCTEXT("ValidationSectionReadyDetail", "No active issues. All checks in this section passed.")
                : View.NotApplicableReason)
            .AutoWrapText(true)
            .ColorAndOpacity(StateColor)
        ];

        const TArray<FText> Checks = GetValidationSectionChecks(View.Section);
        if (!Checks.IsEmpty())
        {
            Content->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 9.0f, 0.0f, 3.0f)
            [
                SNew(STextBlock)
                .Text(View.bApplicable
                    ? LOCTEXT("ValidationSectionChecksLabel", "Checks")
                    : LOCTEXT("ValidationSectionChecksWhenEnabledLabel", "Checks when applicable"))
                .Font(MakeValidationFont(10, true))
            ];

            for (const FText& Check : Checks)
            {
                Content->AddSlot()
                .AutoHeight()
                .Padding(4.0f, 2.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::Format(
                        LOCTEXT("ValidationSectionCheckItemFormat", "- {0}"),
                        Check))
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ];
            }
        }

        return SNew(SBorder)
            .Padding(FMargin(10.0f, 9.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(View.bApplicable
                ? FLinearColor(0.035f, 0.12f, 0.05f, 1.0f)
                : FLinearColor(0.055f, 0.06f, 0.07f, 1.0f))
            [
                Content
            ];
    }

    TSharedRef<SWidget> BuildValidationIssueRow(const FDWCValidationIssueDisplay& Issue)
    {
        TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
        Content->AddSlot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Issue.Title)
                .Font(FAppStyle::GetFontStyle(TEXT("NormalFont")))
                .AutoWrapText(true)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(10.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(8.0f, 2.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(GetValidationSeverityColor(Issue.Severity))
                [
                    SNew(STextBlock)
                    .Text(Issue.Status)
                    .Font(MakeValidationFont(10, true))
                    .ColorAndOpacity(FLinearColor::White)
                ]
            ]
        ];

        if (!Issue.Detail.IsEmpty())
        {
            Content->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 5.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Issue.Detail)
                .AutoWrapText(true)
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        const bool bShowRequiredAction =
            Issue.FixKind == EWCAValidationFixKind::Manual ||
            Issue.FixKind == EWCAValidationFixKind::None;
        if (bShowRequiredAction && !Issue.RequiredAction.IsEmpty())
        {
            Content->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 5.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Issue.RequiredAction)
                .AutoWrapText(true)
                .ColorAndOpacity(GetValidationSeverityColor(Issue.Severity))
            ];
        }

        return SNew(SBorder)
            .Padding(FMargin(10.0f, 7.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(GetValidationIssueBackground(Issue.Severity))
            [
                Content
            ];
    }

    TSharedRef<SWidget> BuildValidationSectionHeader(const FDWCValidationSectionView& View)
    {
        const FSlateColor SectionColor = GetValidationSectionColor(View);
        const FText CountTooltip = GetValidationSectionCountTooltip(View);

        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(SBox)
                .WidthOverride(20.0f)
                .HeightOverride(20.0f)
                .ToolTipText(GetValidationSectionTitle(View.Section))
                [
                    SNew(SImage)
                    .Image(GetValidationSectionIcon(View.Section))
                    .ColorAndOpacity(FLinearColor::White)
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(GetValidationSectionTitle(View.Section))
                .Font(MakeValidationFont(10, true))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(12.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SBox)
                .WidthOverride(36.0f)
                .ToolTipText(CountTooltip)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 3.0f, 0.0f)
                    [
                        SNew(SBox)
                        .WidthOverride(16.0f)
                        .HeightOverride(16.0f)
                        [
                            SNew(SImage)
                            .Image(GetValidationSectionStateIcon(View))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .HAlign(HAlign_Right)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(GetValidationSectionCountText(View))
                        .ColorAndOpacity(SectionColor)
                        .Font(MakeValidationFont(10, true))
                        .Justification(ETextJustify::Right)
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> BuildValidationIssueSection(const FDWCValidationSectionView& View)
    {
        TSharedRef<SVerticalBox> Section = SNew(SVerticalBox);
        Section->AddSlot()
        .AutoHeight()
        [
            BuildValidationSectionHeader(View)
        ];

        if (!View.Issues.IsEmpty())
        {
            TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
            const TArray<FDWCValidationIssueDisplay> Displays = BuildValidationIssueDisplays(View);
            for (int32 DisplayIndex = 0; DisplayIndex < Displays.Num(); ++DisplayIndex)
            {
                Rows->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, DisplayIndex + 1 < Displays.Num() ? 8.0f : 0.0f)
                [
                    BuildValidationIssueRow(Displays[DisplayIndex])
                ];
            }

            Section->AddSlot()
            .AutoHeight()
            .Padding(26.0f, 6.0f, 0.0f, 0.0f)
            [
                Rows
            ];
        }

        return Section;
    }

    const FDWCValidationSectionView* FindValidationSectionView(
        const TArray<FDWCValidationSectionView>& Views,
        const EWCAValidationSection Section)
    {
        return Views.FindByPredicate(
            [Section](const FDWCValidationSectionView& View)
            {
                return View.Section == Section;
            });
    }

    TSharedRef<SWidget> BuildValidationSubsectionLabel(
        const FText& Label,
        const bool bAddLeadingDivider = false)
    {
        TSharedRef<SVerticalBox> SectionLabel = SNew(SVerticalBox);
        if (bAddLeadingDivider)
        {
            SectionLabel->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 10.0f, 0.0f, 10.0f)
            [
                SNew(SSeparator)
                .Orientation(Orient_Horizontal)
            ];
        }

        SectionLabel->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text(Label)
            .Font(MakeValidationFont(10, true))
            .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
        ];

        SectionLabel->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 8.0f)
        [
            SNew(SSeparator)
            .Orientation(Orient_Horizontal)
        ];

        return SectionLabel;
    }

    TSharedRef<SWidget> BuildValidationDiagnosticsSection(const FDWCTriangleValidationSummary& Summary, const FString& Examples)
    {
        const FString TooltipString = FString::Printf(
            TEXT("Total wettable triangles: %d\nCPU usable: %d\nGPU usable: %d\nOriginal UV unavailable/degenerate: %d\nDWC UV Channel degenerate: %d\nInvalid/out-of-range UV: %d\n3D degenerate excluded: %d\nExample triangle indices: %s"),
            Summary.TotalWettableTriangles,
            Summary.CPUUsableTriangles,
            Summary.GPUUsableTriangles,
            Summary.DegenerateOriginalUVTriangles,
            Summary.DegenerateDWCDataUVTriangles,
            Summary.InvalidUVTriangles,
            Summary.Degenerate3DTriangles,
            *Examples);
        const FText Tooltip = FText::FromString(TooltipString);

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [
                    SNew(SImage)
                    .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Validation.Diagnostics")))
                    .ColorAndOpacity(FLinearColor::White)
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ValidationDiagnosticsSection", "Mesh / UV Diagnostics"))
                    .Font(MakeValidationFont(10, true))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(16.0f)
                    .HeightOverride(16.0f)
                    .ToolTipText(Tooltip)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.InfoWithColor")))
                        .ColorAndOpacity(FLinearColor(0.32f, 0.65f, 1.0f, 1.0f))
                    ]
                ]
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(30.0f, 4.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT(
                    "ValidationDiagnosticsDescription",
                    "Non-blocking mesh and UV observations. Invalid or unsuitable triangles may be excluded from simulation; no action is required unless the visual result is incorrect."))
                .AutoWrapText(true)
                .Font(MakeValidationFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                .ToolTipText(Tooltip)
            ];
    }

    TSharedRef<SWidget> BuildValidationDialogContent(
        const UWetClothingAsset& Asset,
        const FWCAValidationReport& Report,
        const FString& Examples,
        const FOnClicked& OnResolveClicked,
        const FOnClicked& OnRefreshClicked,
        const FDWCOnValidationFixRequested& OnFixRequested)
    {
        const TArray<FDWCValidationSectionView> SectionViews = BuildValidationSectionViews(Report, Asset);
        int32 ErrorCount = 0;
        int32 WarningCount = 0;
        int32 DisplayIssueCount = 0;
        int32 AttentionSectionCount = 0;
        for (const FDWCValidationSectionView& View : SectionViews)
        {
            const TArray<FDWCValidationIssueDisplay> Displays = BuildValidationIssueDisplays(View);
            if (!Displays.IsEmpty())
            {
                ++AttentionSectionCount;
            }
            DisplayIssueCount += Displays.Num();
            for (const FDWCValidationIssueDisplay& Display : Displays)
            {
                ErrorCount += Display.Severity == EWCAValidationSeverity::Error ? 1 : 0;
                WarningCount += Display.Severity == EWCAValidationSeverity::Warning ? 1 : 0;
            }
        }

        const bool bHasIssues = DisplayIssueCount > 0;
        const bool bHasErrors = ErrorCount > 0;
        const FWCAValidationReport UnblockedReport = MakeUnblockedValidationReport(Report);
        const bool bCanResolveAutomatically =
            UnblockedReport.HasAutoResolvableIssues() && OnResolveClicked.IsBound();
        const FSlateBrush* HeaderIconBrush = bHasErrors
            ? FDWCEditorStyle::GetBrush(TEXT("DWCEditor.Status.Error"))
            : (bHasIssues
                ? FAppStyle::GetBrush(TEXT("Icons.WarningWithColor"))
                : FAppStyle::GetBrush(TEXT("Icons.SuccessWithColor")));
        const FText HeaderSummary = bHasIssues
            ? FText::Format(
                LOCTEXT("ValidationHeaderIssueSummary", "{0} checks require attention\n{1} active issues - {2} errors - {3} warnings"),
                FText::AsNumber(AttentionSectionCount),
                FText::AsNumber(DisplayIssueCount),
                FText::AsNumber(ErrorCount),
                FText::AsNumber(WarningCount))
            : LOCTEXT("ValidationHeaderSuccessSummary", "All validation checks passed.");

        TSharedRef<SVerticalBox> Sections = SNew(SVerticalBox);
        auto AddValidationSection = [](
            const TSharedRef<SVerticalBox>& GroupBody,
            const FDWCValidationSectionView* View)
        {
            if (View == nullptr)
            {
                return;
            }

            GroupBody->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
            [
                BuildValidationIssueSection(*View)
            ];
        };

        const FDWCValidationSectionView* DataUVView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::DataUV);
        const FDWCValidationSectionView* RuntimeDataView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::RuntimeData);
        const FDWCValidationSectionView* MaterialsView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::GeneratedMaterials);
        const FDWCValidationSectionView* RenderProfileView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::RenderProfileData);
        const FDWCValidationSectionView* WrinkleTexturesView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::WrinkleMaps);
        const FDWCValidationSectionView* TransparencyTexturesView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::TransparencyMaps);
        const FDWCValidationSectionView* InternalFailureView =
            FindValidationSectionView(SectionViews, EWCAValidationSection::FailureDetails);

        TSharedRef<SVerticalBox> ValidationBody = SNew(SVerticalBox);
        ValidationBody->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            BuildValidationSubsectionLabel(LOCTEXT("ValidationRuntimeDataGroup", "RUNTIME DATA"))
        ];
        AddValidationSection(ValidationBody, RuntimeDataView);

        ValidationBody->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 6.0f)
        [
            BuildValidationSubsectionLabel(LOCTEXT("ValidationGeneratedAssetsGroup", "GENERATED ASSETS"))
        ];
        AddValidationSection(ValidationBody, MaterialsView);
        AddValidationSection(ValidationBody, RenderProfileView);
        AddValidationSection(ValidationBody, WrinkleTexturesView);
        AddValidationSection(ValidationBody, TransparencyTexturesView);

        ValidationBody->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 2.0f, 0.0f, 6.0f)
        [
            BuildValidationSubsectionLabel(
                LOCTEXT("ValidationGroupMeshUV", "MESH & UV"),
                true)
        ];
        AddValidationSection(ValidationBody, DataUVView);

        ValidationBody->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            BuildValidationDiagnosticsSection(Report.Diagnostics, Examples)
        ];

        Sections->AddSlot()
        .AutoHeight()
        [
            ValidationBody
        ];

        if (InternalFailureView != nullptr && !InternalFailureView->Issues.IsEmpty())
        {
            Sections->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                BuildValidationIssueSection(*InternalFailureView)
            ];
        }

        TSharedRef<SHorizontalBox> ButtonRow = SNew(SHorizontalBox);
        if (bCanResolveAutomatically)
        {
            ButtonRow->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .ContentPadding(FMargin(10.0f, 5.0f))
                .ToolTipText(Report.HasManualIssues()
                    ? LOCTEXT("ValidationResolveAutomaticWithManualTooltip", "Resolve automatic issues. Manual Fix items will remain in the report.")
                    : LOCTEXT("ValidationResolveAutomaticTooltip", "Resolve validation issues by rebuilding required runtime data, generating required assets, and saving the results."))
                .OnClicked(OnResolveClicked)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.MagicWandTool")))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ValidationDialogResolveAutomatic", "Resolve Automatic Issues"))
                        .Font(MakeValidationFont(10))
                    ]
                ]
            ];
        }
        ButtonRow->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .ContentPadding(FMargin(10.0f, 5.0f))
            .ToolTipText(LOCTEXT("ValidationDialogRefreshTooltip", "Run validation again and refresh this window."))
            .OnClicked(OnRefreshClicked)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(16.0f)
                    .HeightOverride(16.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ValidationDialogRefresh", "Refresh"))
                    .Font(MakeValidationFont(10))
                ]
            ]
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
                        .Image(HeaderIconBrush)
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
                            .Font(MakeValidationFont(10, true))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(HeaderSummary)
                            .AutoWrapText(true)
                            .Font(MakeValidationFont())
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
                    .Padding(0.0f, 0.0f, 14.0f, 0.0f)
                    [
                        Sections
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
            // Runtime bulk payloads are lazy-loaded for PIE/runtime use; the DetailsView must not walk them.
            return PropertyName == FName(TEXT("Bulk")) ||
                   PropertyName == FName(TEXT("NeighborRuntimeData")) ||
                   PropertyName == FName(TEXT("GPURuntimeData")) ||
                   PropertyName == FName(TEXT("LODVertexColorRuntimeData")) ||
                   PropertyName == FName(TEXT("BakedGPUWetMapLODs")) ||
                   PropertyName == FName(TEXT("OriginalUVTopologiesPerLOD")) ||
                   PropertyName == FName(TEXT("DataUVMetadataPerLOD")) ||
                   PropertyName == FName(TEXT("BakeState")) ||
                   PropertyName == FName(TEXT("ValidationSummary")) ||
                   PropertyName == FName(TEXT("PrecomputedSimulationData")) ||
                   PropertyName == FName(TEXT("Profiles")) ||
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

    bool AreAssetSetupSettingsEquivalent(
        const FDWCWetClothingAssetSetupSettings& Left,
        const FDWCWetClothingAssetSetupSettings& Right)
    {
        return Left.bBuildCPUVertexSimulationData == Right.bBuildCPUVertexSimulationData &&
               Left.bBuildGPUWetnessMapSimulationData == Right.bBuildGPUWetnessMapSimulationData &&
               Left.GetGPUSimulationMapResolution() == Right.GetGPUSimulationMapResolution() &&
               Left.GetSurfaceWaterRTResolution() == Right.GetSurfaceWaterRTResolution() &&
               Left.GetWrinkleMapResolution() == Right.GetWrinkleMapResolution() &&
               Left.GetTransparencyMapResolution() == Right.GetTransparencyMapResolution() &&
               Left.OriginalUVChannelIndex == Right.OriginalUVChannelIndex &&
               Left.PreferredDWCDataUVChannelIndex == Right.PreferredDWCDataUVChannelIndex &&
               Left.FirstGeneratedLODIndex == Right.FirstGeneratedLODIndex &&
               Left.LastGeneratedLODIndex == Right.LastGeneratedLODIndex;
    }

    bool DoesAssetSetupRequireDataUVRelocation(
        const UWetClothingAsset& Asset,
        const FDWCWetClothingAssetSetupSettings& Pending)
    {
        return Asset.HasLockedDataUVLayout() &&
               Asset.GetDWCDataUVChannelIndex() != Pending.PreferredDWCDataUVChannelIndex;
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
        ApplyChanges
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
        // Original UV is persistent WCA identity and is never editable after creation.
        SetupObject->OriginalUVChannelIndex = Asset.GetOriginalUVChannelIndex();
        SetupObject->PreferredDWCDataUVChannelIndex =
            FMath::Clamp(SetupObject->PreferredDWCDataUVChannelIndex, 0, MaxDWCDataUVChannelIndex);
        if (SetupObject->PreferredDWCDataUVChannelIndex == SetupObject->OriginalUVChannelIndex)
        {
            SetupObject->PreferredDWCDataUVChannelIndex = GetAssetSetupDefaultDWCDataUVChannelIndex(
                Asset.GetSourceSkeletalMesh(),
                SetupObject->OriginalUVChannelIndex);
        }
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
            return PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, OriginalUVChannelIndex) &&
                   PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, PreferredDWCDataUVChannelIndex) &&
                   PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, FirstGeneratedLODIndex) &&
                   PropertyName != GET_MEMBER_NAME_CHECKED(UWetClothingAssetSetupSettingsObject, LastGeneratedLODIndex);
        }));
        SetupDetails->SetObject(SetupObject.Get());

        TArray<TSharedPtr<int32>> DataUVChannelOptions;
        DataUVChannelOptions.Add(MakeShared<int32>(RecommendedAssetSetupDataUVSelection));
        for (int32 UVChannelIndex = 0; UVChannelIndex <= MaxDWCDataUVChannelIndex; ++UVChannelIndex)
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
                : FMath::Clamp(SetupObject->PreferredDWCDataUVChannelIndex, 0, MaxDWCDataUVChannelIndex);
        };

        auto BuildPendingSettings = [&Asset, SetupObject, &GetEffectiveDataUVChannel]()
        {
            ClampAssetSetupLODRangeForMesh(
                Asset.GetSourceSkeletalMesh(),
                SetupObject->FirstGeneratedLODIndex,
                SetupObject->LastGeneratedLODIndex);
            FDWCWetClothingAssetSetupSettings Pending = SetupObject->BuildSettings();
            Pending.OriginalUVChannelIndex = Asset.GetOriginalUVChannelIndex();
            Pending.PreferredDWCDataUVChannelIndex = GetEffectiveDataUVChannel();
            Pending.NormalizeMapResolutions();
            return Pending;
        };

        auto HasPendingChanges = [&Asset, &BuildPendingSettings]()
        {
            return !AreAssetSetupSettingsEquivalent(Asset.GetSetupSettings(), BuildPendingSettings());
        };

        auto HasValidPendingSettings = [&Asset, SetupObject, &GetEffectiveDataUVChannel]()
        {
            return IsAssetSetupUVSelectionValid(
                Asset.GetSourceSkeletalMesh(),
                SetupObject->OriginalUVChannelIndex,
                GetEffectiveDataUVChannel());
        };

        const FSlateColor ReadyColor(FLinearColor(0.24f, 0.78f, 0.38f, 1.0f));
        const FSlateColor WarningColor(WarningIconTint);
        const FSlateColor MissingColor(FLinearColor(1.0f, 0.24f, 0.18f, 1.0f));

        TSharedRef<SGridPanel> StatusGrid = SNew(SGridPanel);
        StatusGrid->AddSlot(0, 0).Padding(0.0f, 3.0f, 20.0f, 3.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupSourceMeshLabel", "Source Mesh")).ColorAndOpacity(FStyleColors::ForegroundHover)
        ];
        StatusGrid->AddSlot(1, 0).Padding(0.0f, 3.0f)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda([&Asset]() { return FText::FromString(GetPathNameSafe(Asset.GetSourceSkeletalMesh())); })
            .ToolTipText_Lambda([&Asset]() { return FText::FromString(GetPathNameSafe(Asset.GetSourceSkeletalMesh())); })
        ];

        StatusGrid->AddSlot(0, 1).Padding(0.0f, 3.0f, 20.0f, 3.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupPreparedMeshLabel", "Prepared Mesh")).ColorAndOpacity(FStyleColors::ForegroundHover)
        ];
        StatusGrid->AddSlot(1, 1).Padding(0.0f, 3.0f, 16.0f, 3.0f)
        [
            SNew(STextBlock)
            .AutoWrapText(true)
            .Text_Lambda([&Asset]() { return FText::FromString(GetPathNameSafe(Asset.GetRuntimeSkeletalMesh())); })
            .ToolTipText_Lambda([&Asset]() { return FText::FromString(GetPathNameSafe(Asset.GetRuntimeSkeletalMesh())); })
        ];
        StatusGrid->AddSlot(2, 1).Padding(0.0f, 3.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([&Asset, &BuildPendingSettings]()
            {
                if (Asset.GetRuntimeSkeletalMesh() == nullptr)
                {
                    return LOCTEXT("AssetSetupPreparedMeshMissing", "Missing");
                }
                return DoesAssetSetupRequireDataUVRelocation(Asset, BuildPendingSettings())
                    ? LOCTEXT("AssetSetupPreparedMeshRelocation", "UV Relocation Required")
                    : LOCTEXT("AssetSetupPreparedMeshReady", "Ready");
            })
            .ColorAndOpacity_Lambda([&Asset, &BuildPendingSettings, ReadyColor, WarningColor, MissingColor]()
            {
                if (Asset.GetRuntimeSkeletalMesh() == nullptr)
                {
                    return MissingColor;
                }
                return DoesAssetSetupRequireDataUVRelocation(Asset, BuildPendingSettings())
                    ? WarningColor
                    : ReadyColor;
            })
        ];

        StatusGrid->AddSlot(0, 2).Padding(0.0f, 3.0f, 20.0f, 3.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupDataUVStatusLabel", "DWC UV Channel")).ColorAndOpacity(FStyleColors::ForegroundHover)
        ];
        StatusGrid->AddSlot(1, 2).Padding(0.0f, 3.0f, 16.0f, 3.0f)
        [
            SNew(STextBlock).Text_Lambda([&GetEffectiveDataUVChannel]()
            {
                return FText::Format(LOCTEXT("AssetSetupDataUVValue", "UV{0}"), FText::AsNumber(GetEffectiveDataUVChannel()));
            })
        ];
        StatusGrid->AddSlot(2, 2).Padding(0.0f, 3.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([&Asset, &BuildPendingSettings]()
            {
                if (DoesAssetSetupRequireDataUVRelocation(Asset, BuildPendingSettings()))
                {
                    return LOCTEXT("AssetSetupDataUVRelocation", "Relocation Required");
                }
                return Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex())
                    ? LOCTEXT("AssetSetupDataUVReady", "Ready")
                    : LOCTEXT("AssetSetupDataUVMissing", "Missing");
            })
            .ColorAndOpacity_Lambda([&Asset, &BuildPendingSettings, ReadyColor, WarningColor, MissingColor]()
            {
                if (DoesAssetSetupRequireDataUVRelocation(Asset, BuildPendingSettings()))
                {
                    return WarningColor;
                }
                return Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex()) ? ReadyColor : MissingColor;
            })
        ];

        StatusGrid->AddSlot(0, 3).Padding(0.0f, 3.0f, 20.0f, 3.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupMappedLODsLabel", "Active Mapped LODs")).ColorAndOpacity(FStyleColors::ForegroundHover)
        ];
        StatusGrid->AddSlot(1, 3).ColumnSpan(2).Padding(0.0f, 3.0f)
        [
            SNew(STextBlock).Text_Lambda([SetupObject]()
            {
                return FText::Format(
                    LOCTEXT("AssetSetupMappedLODsValue", "LOD{0} - LOD{1}"),
                    FText::AsNumber(SetupObject->FirstGeneratedLODIndex),
                    FText::AsNumber(SetupObject->LastGeneratedLODIndex));
            })
        ];

        TSharedRef<SGridPanel> MeshGrid = SNew(SGridPanel);
        MeshGrid->AddSlot(0, 0).Padding(0.0f, 4.0f, 20.0f, 4.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupOriginalUVChannel", "Original UV Channel"))
        ];
        MeshGrid->AddSlot(1, 0).Padding(0.0f, 4.0f)
        [
            SNew(STextBlock)
            .Text_Lambda([&Asset]()
            {
                return FText::Format(
                    LOCTEXT("AssetSetupOriginalUVValue", "UV{0}"),
                    FText::AsNumber(Asset.GetOriginalUVChannelIndex()));
            })
            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
        ];

        MeshGrid->AddSlot(0, 1).Padding(0.0f, 4.0f, 20.0f, 4.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupPreferredDataUVChannel", "DWC UV Channel"))
        ];
        MeshGrid->AddSlot(1, 1).Padding(0.0f, 4.0f)
        [
            SNew(SBox)
            .WidthOverride(190.0f)
            [
                SNew(SComboBox<TSharedPtr<int32>>)
                .OptionsSource(&DataUVChannelOptions)
                .InitiallySelectedItem(InitialDataUVChannelItem)
                .OnGenerateWidget_Lambda([&Asset, SetupObject](TSharedPtr<int32> Item)
                {
                    const int32 Selection = Item.IsValid() ? *Item : RecommendedAssetSetupDataUVSelection;
                    return SNew(STextBlock).Text(BuildAssetSetupDataUVChannelLabel(
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
                    bUseRecommendedDataUVChannel = *Item == RecommendedAssetSetupDataUVSelection;
                    if (!bUseRecommendedDataUVChannel)
                    {
                        SetupObject->PreferredDWCDataUVChannelIndex = FMath::Clamp(*Item, 0, MaxDWCDataUVChannelIndex);
                    }
                })
                [
                    SNew(STextBlock).Text_Lambda([&Asset, SetupObject, &bUseRecommendedDataUVChannel]()
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
        ];

        MeshGrid->AddSlot(0, 2).Padding(0.0f, 4.0f, 20.0f, 4.0f)
        [
            SNew(STextBlock).Text(LOCTEXT("AssetSetupLODRangeLabel", "Active LOD Mapping Range"))
        ];
        MeshGrid->AddSlot(1, 2).Padding(0.0f, 4.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(78.0f)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue(0)
                    .MaxValue_Lambda([&Asset]() { return FMath::Max(0, GetAssetSetupSkeletalMeshLODCount(Asset.GetSourceSkeletalMesh()) - 1); })
                    .Value_Lambda([SetupObject]() { return SetupObject->FirstGeneratedLODIndex; })
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
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
            [
                SNew(STextBlock).Text(LOCTEXT("AssetSetupLODRangeTo", "to"))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(78.0f)
                [
                    SNew(SSpinBox<int32>)
                    .MinValue_Lambda([SetupObject]() { return SetupObject->FirstGeneratedLODIndex; })
                    .MaxValue_Lambda([&Asset]() { return FMath::Max(0, GetAssetSetupSkeletalMeshLODCount(Asset.GetSourceSkeletalMesh()) - 1); })
                    .Value_Lambda([SetupObject]() { return SetupObject->LastGeneratedLODIndex; })
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
        ];

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
                SNew(SBox)
                .WidthOverride(640.0f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SBox)
                        .MaxDesiredHeight(700.0f)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                [
                                    SNew(SBorder)
                                    .Padding(10.0f)
                                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                    [
                                        SNew(SVerticalBox)
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                        [
                                            SNew(STextBlock)
                                            .Text(LOCTEXT("AssetSetupStatusSection", "Setup Status"))
                                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                        ]
                                        + SVerticalBox::Slot().AutoHeight()
                                        [
                                            StatusGrid
                                        ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
                                        [
                                            BuildAssetSetupInfoTextRow(LOCTEXT("AssetSetupSourceMeshUnchanged", "The source mesh will not be modified."))
                                        ]
                                    ]
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                [
                                    SNew(SBorder)
                                    .Padding(10.0f)
                                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                    [
                                        SNew(SVerticalBox)
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                        [
                                            SNew(STextBlock)
                                            .Text(LOCTEXT("AssetSetupMeshUVSection", "Mesh & UV"))
                                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                        ]
                                        + SVerticalBox::Slot().AutoHeight()
                                        [
                                            MeshGrid
                                        ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
                                        [
                                            BuildAssetSetupInfoTextRow(LOCTEXT(
                                                "AssetSetupLockedUVLayoutInfo",
                                                "Original UV and island topology will not be modified."))
                                        ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
                                        [
                                            BuildAssetSetupInfoTextRow(LOCTEXT(
                                                "AssetSetupRetainedLODDataInfo",
                                                "Previously generated DWC UV data outside this range will be retained and reused if the range is expanded later."))
                                        ]
                                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 5.0f, 0.0f, 0.0f)
                                        [
                                            SNew(SHorizontalBox)
                                            .Visibility_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                            {
                                                return BuildAssetSetupPreferredDataUVInfoText(
                                                    Asset.GetSourceSkeletalMesh(),
                                                    SetupObject->OriginalUVChannelIndex,
                                                    GetEffectiveDataUVChannel()).IsEmpty()
                                                    ? EVisibility::Collapsed
                                                    : EVisibility::Visible;
                                            })
                                            + SHorizontalBox::Slot()
                                            .AutoWidth()
                                            .VAlign(VAlign_Top)
                                            .Padding(0.0f, 1.0f, 6.0f, 0.0f)
                                            [
                                                SNew(SBox)
                                                .WidthOverride(16.0f)
                                                .HeightOverride(16.0f)
                                                [
                                                    SNew(SImage)
                                                    .Image_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                                    {
                                                        return GetAssetSetupDataUVMessageIconBrush(
                                                            Asset.GetSourceSkeletalMesh(),
                                                            SetupObject->OriginalUVChannelIndex,
                                                            GetEffectiveDataUVChannel());
                                                    })
                                                    .ColorAndOpacity_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                                    {
                                                        const USkeletalMesh* Mesh = Asset.GetSourceSkeletalMesh();
                                                        const int32 DataUVChannelIndex = GetEffectiveDataUVChannel();
                                                        const int32 UVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(Mesh, 0);
                                                        return Mesh != nullptr &&
                                                               UVChannelCount > 0 &&
                                                               SetupObject->OriginalUVChannelIndex >= 0 &&
                                                               SetupObject->OriginalUVChannelIndex < UVChannelCount &&
                                                               DataUVChannelIndex >= UVChannelCount &&
                                                               DataUVChannelIndex <= MaxDWCDataUVChannelIndex
                                                                   ? InfoIconTint
                                                                   : FLinearColor::White;
                                                    })
                                                ]
                                            ]
                                            + SHorizontalBox::Slot()
                                            .FillWidth(1.0f)
                                            .VAlign(VAlign_Center)
                                            [
                                                SNew(STextBlock)
                                                .AutoWrapText(true)
                                                .Font(FAppStyle::GetFontStyle(TEXT("NormalFont")))
                                                .ColorAndOpacity_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                                {
                                                    return GetAssetSetupDataUVMessageColor(
                                                        Asset.GetSourceSkeletalMesh(),
                                                        SetupObject->OriginalUVChannelIndex,
                                                        GetEffectiveDataUVChannel());
                                                })
                                                .Text_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                                {
                                                    return BuildAssetSetupPreferredDataUVInfoText(
                                                        Asset.GetSourceSkeletalMesh(),
                                                        SetupObject->OriginalUVChannelIndex,
                                                        GetEffectiveDataUVChannel());
                                                })
                                            ]
                                        ]
                                    ]
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                [
                                    SNew(SBorder)
                                    .Padding(6.0f)
                                    .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                    [
                                        SNew(SBox)
                                        .WidthOverride(610.0f)
                                        .MaxDesiredHeight(300.0f)
                                        [
                                            SetupDetails
                                        ]
                                    ]
                                ]
                                + SVerticalBox::Slot().AutoHeight()
                                [
                                    SNew(SExpandableArea)
                                    .InitiallyCollapsed(true)
                                    .HeaderContent()
                                    [
                                        SNew(STextBlock)
                                        .Text(LOCTEXT("AssetSetupAdvancedMeshInformation", "Advanced Mesh Information"))
                                        .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                    ]
                                    .BodyContent()
                                    [
                                        SNew(SBorder)
                                        .Padding(10.0f)
                                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                        [
                                            SNew(STextBlock)
                                            .AutoWrapText(true)
                                            .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                                            .Text_Lambda([&Asset, SetupObject, &GetEffectiveDataUVChannel]()
                                            {
                                                return BuildAssetSetupSkeletalMeshUVChannelSummary(
                                                    Asset.GetSourceSkeletalMesh(),
                                                    SetupObject->OriginalUVChannelIndex,
                                                    GetEffectiveDataUVChannel());
                                            })
                                        ]
                                    ]
                                ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .HAlign(HAlign_Right)
                    .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("AssetSetupCancel", "Cancel"))
                            .OnClicked_Lambda([&Result, Dialog]()
                            {
                                Result = EWCASetupDialogResult::Closed;
                                Dialog->RequestDestroyWindow();
                                return FReply::Handled();
                            })
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("AssetSetupApplyChanges", "Apply Changes"))
                            .IsEnabled_Lambda([&HasValidPendingSettings, &HasPendingChanges]()
                            {
                                return HasValidPendingSettings() && HasPendingChanges();
                            })
                            .ToolTipText_Lambda([&HasValidPendingSettings, &HasPendingChanges]()
                            {
                                if (!HasValidPendingSettings())
                                {
                                    return LOCTEXT("AssetSetupInvalidSettingsTooltip", "Select a DWC UV Channel different from the locked Original UV channel.");
                                }
                                if (!HasPendingChanges())
                                {
                                    return LOCTEXT("AssetSetupNoChangesTooltip", "No setup changes to apply.");
                                }
                                return LOCTEXT("AssetSetupApplyChangesTooltip", "Apply setup changes. A DWC UV Channel change copies the sealed layout without rebuilding islands.");
                            })
                            .OnClicked_Lambda([&Asset, SetupObject, &BuildPendingSettings, &Result, &OutAllowOverwriteExistingDataUVChannel, Dialog]()
                            {
                                const FDWCWetClothingAssetSetupSettings PendingSettings = BuildPendingSettings();
                                const bool bRequiresDataUVRelocation = DoesAssetSetupRequireDataUVRelocation(
                                    Asset,
                                    PendingSettings);
                                const FDWCWetClothingAssetSetupSettings& ExistingSettings = Asset.GetSetupSettings();
                                const int32 SourceUVChannelCount = GetAssetSetupSkeletalMeshUVChannelCount(
                                    Asset.GetSourceSkeletalMesh(),
                                    0);
                                const bool bDeferredInitialBuildNeedsOverwriteConsent =
                                    !Asset.HasLockedDataUVLayout() &&
                                    PendingSettings.PreferredDWCDataUVChannelIndex < SourceUVChannelCount &&
                                    !(PendingSettings.PreferredDWCDataUVChannelIndex == ExistingSettings.PreferredDWCDataUVChannelIndex &&
                                      ExistingSettings.bAllowOverwritePreferredDWCDataUVChannel);
                                if ((bRequiresDataUVRelocation || bDeferredInitialBuildNeedsOverwriteConsent) &&
                                    !ConfirmAssetSetupDataUVOverwrite(
                                        Asset.GetSourceSkeletalMesh(),
                                        PendingSettings.OriginalUVChannelIndex,
                                        PendingSettings.PreferredDWCDataUVChannelIndex,
                                        Asset.GetDWCDataUVChannelIndex(),
                                        bRequiresDataUVRelocation,
                                        OutAllowOverwriteExistingDataUVChannel))
                                {
                                    return FReply::Handled();
                                }

                                SetupObject->PreferredDWCDataUVChannelIndex = PendingSettings.PreferredDWCDataUVChannelIndex;
                                Result = EWCASetupDialogResult::ApplyChanges;
                                Dialog->RequestDestroyWindow();
                                return FReply::Handled();
                            })
                        ]
                    ]
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow());
        if (Result == EWCASetupDialogResult::ApplyChanges)
        {
            OutSettings = BuildPendingSettings();
        }
        return Result;
    }

    bool IsRequiredTransparencyLayer(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer)
    {
        const int32 MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
        return Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots &&
               MaterialSlotIndex != INDEX_NONE &&
               Asset.IsMaterialSlotWettable(MaterialSlotIndex);
    }

    bool ResolveGeneratedWetMaterialsForAsset(
        UWetClothingAsset& Asset,
        FString& OutSummary,
        FString& OutFailure)
    {
        OutSummary.Reset();
        OutFailure.Reset();

        USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
        if (RuntimeMesh == nullptr)
        {
            OutFailure = TEXT("Generated Materials: assign a runtime skeletal mesh before generating wet materials.");
            return false;
        }

        if (!Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex()))
        {
            OutFailure = TEXT("Generated Materials: valid sealed DWC UV Channel is required. Create a new WCA if the stored layout is invalid.");
            return false;
        }

        TArray<int32> WettableSlots;
        for (const FWetClothingAuthoredMaterialSlot& SlotState :
             Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
        {
            if (SlotState.bIsWettableSlot && SlotState.MaterialSlotIndex != INDEX_NONE)
            {
                WettableSlots.AddUnique(SlotState.MaterialSlotIndex);
            }
        }
        WettableSlots.Sort();
        if (WettableSlots.IsEmpty())
        {
            OutSummary = TEXT("No wettable material slots require generated materials.");
            return true;
        }

        const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();

        TArray<FString> UpdatedMaterials;
        TArray<FString> Failures;
        Asset.Modify();
        for (const int32 MaterialSlotIndex : WettableSlots)
        {
            if (!Materials.IsValidIndex(MaterialSlotIndex))
            {
                Failures.Add(FString::Printf(TEXT("Slot %d is out of range."), MaterialSlotIndex));
                continue;
            }

            UMaterialInterface* SourceMaterial =
                FWCAMaterialGenerator::ResolveGeneratedMaterialSource(&Asset, MaterialSlotIndex, Materials[MaterialSlotIndex].MaterialInterface);
            if (SourceMaterial == nullptr)
            {
                Failures.Add(FString::Printf(TEXT("Slot %d has no source material."), MaterialSlotIndex));
                continue;
            }

            FWetClothingGeneratedWetMaterialOverride* ExistingOverride =
                Asset.Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                    [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
                    {
                        return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
                    });

            const FWCAMaterialGenerator::FOptions MaterialSetupOptions =
                FWCAMaterialGenerator::MakeOptionsForAsset(
                    &Asset,
                    EDWCSimulationMode::VertexCPU,
                    MaterialSlotIndex);
            const FWetClothingUnifiedMaterialSetupResult MaterialSet =
                FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);
            if (!MaterialSet.bSucceeded || MaterialSet.GeneratedMaterial == nullptr ||
                MaterialSet.GeneratedMaterialInstance == nullptr)
            {
                Failures.Add(FString::Printf(
                    TEXT("Slot %d: %s"),
                    MaterialSlotIndex,
                    *MaterialSet.Message));
                continue;
            }

            if (ExistingOverride == nullptr)
            {
                ExistingOverride = &Asset.Derived.Inline.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
                ExistingOverride->MaterialSlotIndex = MaterialSlotIndex;
            }

#if WITH_EDITORONLY_DATA
            Asset.Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction =
                MaterialSet.EvaluateSurfaceAppearanceFunction;
#endif
            ExistingOverride->SourceMaterial = SourceMaterial;
            ExistingOverride->GeneratedMaterial = MaterialSet.GeneratedMaterial;
            ExistingOverride->GeneratedMaterialInstance = MaterialSet.GeneratedMaterialInstance;
            ExistingOverride->GeneratorVersion = FWCAMaterialGenerator::GeneratedMaterialGeneratorVersion;
            ExistingOverride->GenerationSignature = FWCAMaterialGenerator::BuildGeneratedMaterialSignature(
                &Asset,
                MaterialSlotIndex,
                SourceMaterial);

            UMaterialInterface* CurrentMaterial = RuntimeMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface;
            const bool bCanApplyGeneratedMaterial = CurrentMaterial == nullptr ||
                CurrentMaterial == SourceMaterial ||
                CurrentMaterial == ExistingOverride->GeneratedMaterial ||
                CurrentMaterial == ExistingOverride->GeneratedMaterialInstance ||
                IsSameMaterialFamily(CurrentMaterial, SourceMaterial);
            if (bCanApplyGeneratedMaterial)
            {
                RuntimeMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface = MaterialSet.GeneratedMaterialInstance;
                RuntimeMesh->MarkPackageDirty();
            }

            UpdatedMaterials.Add(FString::Printf(
                TEXT("Slot %d -> shared %s, runtime %s%s"),
                MaterialSlotIndex,
                *GetNameSafe(MaterialSet.GeneratedMaterial),
                *GetNameSafe(MaterialSet.GeneratedMaterialInstance),
                bCanApplyGeneratedMaterial ? TEXT(" (applied to DWC mesh)") : TEXT(" (override only; mesh slot has custom material)")));
        }

        if (!UpdatedMaterials.IsEmpty())
        {
            Asset.MarkPackageDirty();
        }

        OutSummary = UpdatedMaterials.IsEmpty()
            ? TEXT("Generated materials were already up to date.")
            : FString::Printf(
                TEXT("Generated material overrides:\n- %s"),
                *FString::Join(UpdatedMaterials, TEXT("\n- ")));

        if (!Failures.IsEmpty())
        {
            OutFailure = FString::Printf(
                TEXT("Generated Materials:\n- %s"),
                *FString::Join(Failures, TEXT("\n- ")));
            return false;
        }
        return true;
    }

    bool ResolveTransparencyMapsForAsset(
        UWetClothingAsset& Asset,
        FString& OutSummary,
        FString& OutFailure,
        bool* OutHadWarnings = nullptr)
    {
        OutSummary.Reset();
        OutFailure.Reset();
        if (OutHadWarnings != nullptr)
        {
            *OutHadWarnings = false;
        }

        if (!Asset.HasTransparencyBakeContent())
        {
            OutSummary = TEXT("No transparency textures require baking.");
            return true;
        }

        if (!Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex()))
        {
            OutFailure = TEXT("Transparency Textures: valid sealed DWC UV Channel is required. Create a new WCA if the stored layout is invalid.");
            Asset.SetTransparencyBakeStatus(EDWCBakeStatus::Failed, OutFailure);
            return false;
        }

        TArray<FString> BakedLayerSummaries;
        TArray<FString> WarningMessages;
        int32 BakedLayerCount = 0;
        const FDWCEditorPreviewSlotCollection PreviewSlotStates =
            FDWCEditorPreviewSlotResolver::Resolve(&Asset);

        Asset.Modify();
        for (FWetClothingTransparencyLayerData& Layer :
             Asset.Authored.TransparencyData.TransparencyLayers)
        {
            if (!IsRequiredTransparencyLayer(Asset, Layer))
            {
                continue;
            }

            const int32 MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
            if (!PreviewSlotStates.IsReady(MaterialSlotIndex))
            {
                const FDWCEditorPreviewSlotState* State = PreviewSlotStates.Find(MaterialSlotIndex);
                OutFailure = FString::Printf(
                    TEXT("Transparency Maps: slot %d is not ready. %s"),
                    MaterialSlotIndex,
                    State != nullptr
                        ? *FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue).ToString()
                        : TEXT("The slot is unavailable for preview."));
                Asset.SetTransparencyBakeStatus(EDWCBakeStatus::Failed, OutFailure);
                return false;
            }

            FDWCTransparencyAutoBakeResult AutoResult;
            FString GenerateSummary;
            TArray<FString> GenerateWarnings;
            if (!FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
                    Asset,
                    Layer,
                    AutoResult,
                    GenerateSummary,
                    GenerateWarnings))
            {
                OutFailure = FString::Printf(
                    TEXT("Transparency Textures: slot %d auto-generation failed.\n%s"),
                    MaterialSlotIndex,
                    *GenerateSummary);
                Asset.SetTransparencyBakeStatus(EDWCBakeStatus::Failed, OutFailure);
                return false;
            }

            FDWCTransparencyEditedMapBakeResult BakeResult;
            FString BakeError;
            if (!FDWCTransparencyEditedMapBaker::Bake(Asset, Layer, AutoResult, BakeResult, BakeError))
            {
                OutFailure = FString::Printf(
                    TEXT("Transparency Textures: slot %d final bake failed.\n%s"),
                    MaterialSlotIndex,
                    *BakeError);
                Asset.SetTransparencyBakeStatus(EDWCBakeStatus::Failed, OutFailure);
                return false;
            }

            Layer.AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
            Layer.AutoBakeMetadata.BuildSignature = AutoResult.BuildSignature;
            Layer.AutoBakeMetadata.Resolution = AutoResult.Resolution.X;
            Layer.AutoBakeMetadata.PaddingPixels = Asset.Authored.TransparencyData.TransparencyPaddingPixels;
            Layer.AutoBakeMetadata.ValidHitCount = AutoResult.ValidHitCount;
            Layer.AutoBakeMetadata.NoHitCount = AutoResult.NoHitCount;

            ++BakedLayerCount;
            BakedLayerSummaries.Add(FString::Printf(
                TEXT("%s (Slot %d, UV%d, LOD%d) -> %s"),
                *Layer.TargetSurface.OuterMaterialSlotName.ToString(),
                MaterialSlotIndex,
                AutoResult.UVChannelIndex,
                AutoResult.LODIndex,
                *GetPathNameSafe(BakeResult.TransparencyMap)));

            for (const FString& Warning : GenerateWarnings)
            {
                WarningMessages.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *Warning));
            }
            if (BakeResult.IgnoredNoHitOverridePixelCount > 0)
            {
                WarningMessages.Add(FString::Printf(
                    TEXT("Slot %d: %d manually edited pixel(s) had no valid inner-surface color and were kept at Alpha 0."),
                    MaterialSlotIndex,
                    BakeResult.IgnoredNoHitOverridePixelCount));
            }
            if (!BakeResult.WarningMessage.IsEmpty())
            {
                WarningMessages.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *BakeResult.WarningMessage));
            }
        }

        if (BakedLayerCount == 0)
        {
            OutSummary = TEXT("No required transparency map layers were found.");
            return true;
        }

        Asset.SetTransparencyBakeStatus(EDWCBakeStatus::Valid);
        Asset.MarkPackageDirty();

        OutSummary = FString::Printf(
            TEXT("Baked transparency textures:\n- %s"),
            *FString::Join(BakedLayerSummaries, TEXT("\n- ")));
        if (!WarningMessages.IsEmpty())
        {
            if (OutHadWarnings != nullptr)
            {
                *OutHadWarnings = true;
            }
            OutSummary += FString::Printf(
                TEXT("\n\nWarnings:\n- %s"),
                *FString::Join(WarningMessages, TEXT("\n- ")));
        }
        return true;
    }

    EWCAPendingCloseChoice ShowDWCResolveCloseDialog(const FString& IssueSummary)
    {
        EWCAPendingCloseChoice Choice = EWCAPendingCloseChoice::Cancel;

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
                .Title(LOCTEXT("DWCResolveCloseTitle", "Wet Clothing Asset Is Not Up To Date"))
                .ClientSize(FVector2D(760.0f, 620.0f))
                .SizingRule(ESizingRule::UserSized)
                .SupportsMaximize(true)
                .SupportsMinimize(false);

        auto ResolveSectionFromHeading = [](const FString& Heading)
        {
            if (Heading.Contains(TEXT("DWC UV"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::DataUV;
            }
            if (Heading.Contains(TEXT("Runtime Data"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::RuntimeData;
            }
            if (Heading.Contains(TEXT("Generated Materials"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::GeneratedMaterials;
            }
            if (Heading.Contains(TEXT("Render Profile"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::RenderProfileData;
            }
            if (Heading.Contains(TEXT("Wrinkle"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::WrinkleMaps;
            }
            if (Heading.Contains(TEXT("Transparency"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::TransparencyMaps;
            }
            if (Heading.Contains(TEXT("Internal"), ESearchCase::IgnoreCase))
            {
                return EWCAValidationSection::FailureDetails;
            }
            return EWCAValidationSection::FailureDetails;
        };

        auto BuildIssueSummaryCard = [&ResolveSectionFromHeading](const FString& SectionText) -> TSharedRef<SWidget>
        {
            TArray<FString> Lines;
            SectionText.ParseIntoArrayLines(Lines, true);
            if (Lines.IsEmpty())
            {
                Lines.Add(SectionText);
            }

            FString Heading = Lines[0];
            Heading.TrimStartAndEndInline();
            const EWCAValidationSection Section = ResolveSectionFromHeading(Heading);
            const bool bFailed = SectionText.Contains(TEXT("Failed"), ESearchCase::IgnoreCase);
            const EWCAValidationSeverity Severity = bFailed
                ? EWCAValidationSeverity::Error
                : EWCAValidationSeverity::Warning;

            TSharedRef<SVerticalBox> DetailLines = SNew(SVerticalBox);
            for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
            {
                FString Line = Lines[LineIndex];
                Line.TrimStartAndEndInline();
                if (Line.IsEmpty())
                {
                    continue;
                }

                DetailLines->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Line))
                    .AutoWrapText(true)
                    .Font(MakeValidationFont())
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ];
            }

            return SNew(SBorder)
                .Padding(FMargin(10.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(GetValidationIssueBackground(Severity))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            SNew(SBox)
                            .WidthOverride(18.0f)
                            .HeightOverride(18.0f)
                            [
                                SNew(SImage)
                                .Image(GetValidationSectionIcon(Section))
                                .ColorAndOpacity(FLinearColor::White)
                            ]
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(Heading))
                            .Font(MakeValidationFont(10, true))
                            .AutoWrapText(true)
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                        [
                            SNew(SBorder)
                            .Padding(FMargin(8.0f, 2.0f))
                            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                            .BorderBackgroundColor(GetValidationSeverityColor(Severity))
                            [
                                SNew(STextBlock)
                                .Text(bFailed
                                    ? LOCTEXT("DWCCloseSummaryFailedStatus", "Failed")
                                    : LOCTEXT("DWCCloseSummaryWarningStatus", "Attention"))
                                .Font(MakeValidationFont(10, true))
                                .ColorAndOpacity(FLinearColor::White)
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(26.0f, 6.0f, 0.0f, 0.0f)
                    [
                        DetailLines
                    ]
                ];
        };

        TSharedRef<SVerticalBox> IssueCards = SNew(SVerticalBox);
        TArray<FString> Sections;
        IssueSummary.ParseIntoArray(Sections, TEXT("\n\n"), true);
        for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
        {
            FString SectionText = Sections[SectionIndex];
            SectionText.TrimStartAndEndInline();
            if (SectionText.IsEmpty())
            {
                continue;
            }

            IssueCards->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                BuildIssueSummaryCard(SectionText)
            ];
        }

        DialogWindow->SetContent(
            SNew(SBorder)
                .Padding(0.0f)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
                [SNew(SVerticalBox)

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(16.0f, 14.0f, 16.0f, 12.0f)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Top)
                                  .Padding(0.0f, 2.0f, 10.0f, 0.0f)
                                      [SNew(SImage)
                                           .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))]

                            + SHorizontalBox::Slot()
                                  .FillWidth(1.0f)
                                      [SNew(SVerticalBox)
                                       + SVerticalBox::Slot()
                                             .AutoHeight()
                                                 [SNew(STextBlock)
                                                      .Text(LOCTEXT("DWCResolveCloseHeader", "Wet Clothing Asset Is Not Up To Date"))
                                                      .Font(MakeValidationFont(10, true))]
                                       + SVerticalBox::Slot()
                                             .AutoHeight()
                                             .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                                                 [SNew(STextBlock)
                                                      .AutoWrapText(true)
                                                      .Text(LOCTEXT(
                                                          "DWCResolveCloseDescription",
                                                          "Resolve will perform only the required build, generation, bake, and save operations."))
                                                      .Font(MakeValidationFont())
                                                      .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))]]]

                 + SVerticalBox::Slot()
                       .FillHeight(1.0f)
                       .Padding(16.0f, 0.0f, 16.0f, 0.0f)
                           [SNew(SScrollBox)
                            + SScrollBox::Slot()
                                  .Padding(0.0f, 0.0f, 14.0f, 0.0f)
                                      [IssueCards]]

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .HAlign(HAlign_Right)
                       .Padding(16.0f, 12.0f, 16.0f, 16.0f)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                      [SNew(SButton)
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWCAPendingCloseChoice::ResolveAndSave;
                                               DialogWindow->RequestDestroyWindow();
                                               return FReply::Handled();
                                           })
                                           [SNew(SHorizontalBox)
                                            + SHorizontalBox::Slot()
                                                  .AutoWidth()
                                                  .VAlign(VAlign_Center)
                                                  .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                      [SNew(SImage)
                                                           .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.MagicWandTool")))]
                                            + SHorizontalBox::Slot()
                                                  .AutoWidth()
                                                  .VAlign(VAlign_Center)
                                                      [SNew(STextBlock)
                                                           .Text(LOCTEXT("DWCResolveIssuesAndSave", "Resolve"))]]]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                      [SNew(SButton)
                                           .Text(LOCTEXT("DWCCloseAnyway", "Close Anyway"))
                                           .OnClicked_Lambda([&Choice, DialogWindow]()
                                           {
                                               Choice = EWCAPendingCloseChoice::CloseAnyway;
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
    InWetClothingAsset->ReleaseLoadedRuntimeBulkPayloadForEditor();

    FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    DetailsViewArgs.bHideSelectionTip = true;
    DetailsViewArgs.bAllowSearch = false;
    DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    DetailsView->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateStatic(&ShouldShowWetClothingAssetDetailProperty));
    // The WCA panels own their editing UI. Binding the full asset to an unused DetailsView
    // makes Slate/property reflection walk runtime bulk arrays after PIE has lazy-loaded them.
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
                FString SuccessSummary;
                if (!ResolveIssuesAndSave(Failure, &SuccessSummary))
                {
                    FMessageDialog::Open(
                        EAppMsgCategory::Error,
                        EAppMsgType::Ok,
                        FText::FromString(FString::Printf(
                            TEXT("Failed to resolve all Wet Clothing Asset issues.\n\n%s"),
                            *Failure)));
                    return false;
                }
                const FString SuccessMessage = SuccessSummary.IsEmpty()
                    ? FString(TEXT("Wet Clothing Asset issues were resolved and saved."))
                    : FString::Printf(TEXT("Wet Clothing Asset issues were resolved and saved.\n\n%s"), *SuccessSummary);
                FMessageDialog::Open(
                    EAppMsgCategory::Success,
                    EAppMsgType::Ok,
                    FText::FromString(SuccessMessage));
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

    // The Validation toolbar button stores its label and icon when the toolbar
    // is built. Refresh it after an asset save without rebuilding the active
    // mode's preview resources.
    RegenerateMenusAndToolbars();
}

void FWCAEditor::HandleEditorPanelStatusChanged()
{
    RegenerateMenusAndToolbars();
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
                 .WetClothingAsset(WetClothingAsset.Get())
                 .OnStatusChanged(FSimpleDelegate::CreateSP(this, &FWCAEditor::HandleEditorPanelStatusChanged))];
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
        LOCTEXT("AssetSetupToolbarTooltip", "Review setup status and change mesh, simulation-data, and texture-resolution settings."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"));
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FWCAEditor::BuildRuntimeBuildMenu),
        LOCTEXT("BuildForRuntimeToolbarLabel", "Build for Runtime"),
        LOCTEXT("BuildForRuntimeToolbarTooltip", "Build runtime data and generate the assets required by this Wet Clothing Asset. Up-to-date actions are disabled."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.BuildForRuntime"), TEXT("DWCEditor.BuildForRuntime.Small")),
        false);
    ToolbarBuilder.AddComboButton(
        FUIAction(),
        FOnGetContent::CreateSP(this, &FWCAEditor::BuildPreviewDiagnosticsMenu),
        LOCTEXT("PreviewDiagnosticsToolbarLabel", "Preview Diagnostics"),
        LOCTEXT("PreviewDiagnosticsToolbarTooltip", "Write DWC editor preview CPU/GPU residency and upload-queue diagnostics to the Output Log."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Info")),
        false);
    FText ValidationLabel = LOCTEXT("ValidationToolbarLabel", "Validation");
    FText ValidationTooltip = LOCTEXT("ValidationToolbarTooltip", "Validation passed. Click to view the latest validation results.");
    FName ValidationIconStyleSet = FAppStyle::GetAppStyleSetName();
    FName ValidationIconName(TEXT("Icons.SuccessWithColor"));
#if WITH_EDITORONLY_DATA
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const FWCAValidationReport ValidationReport =
            BuildWCAValidationReport(*Asset, EWCAValidationMode::Fast, false);
        const TArray<FDWCValidationSectionView> ValidationViews = BuildValidationSectionViews(ValidationReport, *Asset);
        int32 ActionRequiredCount = 0;
        for (const FDWCValidationSectionView& View : ValidationViews)
        {
            ActionRequiredCount += BuildValidationIssueDisplays(View).Num();
        }
        if (ActionRequiredCount > 0)
        {
            const bool bHasFailedState = ValidationReport.Issues.ContainsByPredicate(
                [](const FWCAValidationIssue& Issue)
                {
                    return Issue.Severity == EWCAValidationSeverity::Error;
                });
            ValidationLabel = FText::Format(
                LOCTEXT("ValidationToolbarWarningLabel", "Validation ({0})"),
                FText::AsNumber(ActionRequiredCount));
            if (bHasFailedState)
            {
                ValidationTooltip = FText::Format(
                    LOCTEXT(
                        "ValidationToolbarErrorTooltip",
                        "{0} validation issue(s) require action, including failed data. Click to view details."),
                    FText::AsNumber(ActionRequiredCount));
                ValidationIconStyleSet = FDWCEditorStyle::GetStyleSetName();
                ValidationIconName = FName(TEXT("DWCEditor.Status.Error"));
            }
            else
            {
                ValidationTooltip = FText::Format(
                    LOCTEXT(
                        "ValidationToolbarWarningTooltip",
                        "{0} validation issue(s) require Save, Bake, Generate, or Rebuild. Click to view details."),
                    FText::AsNumber(ActionRequiredCount));
                ValidationIconStyleSet = FAppStyle::GetAppStyleSetName();
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
        FSlateIcon(ValidationIconStyleSet, ValidationIconName));
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

    const FDWCWetClothingAssetSetupSettings PreviousSettings = Asset->GetSetupSettings();
    if (!Asset->HasLockedDataUVLayout())
    {
        const bool bPreferredChannelUnchanged =
            NewSettings.PreferredDWCDataUVChannelIndex == PreviousSettings.PreferredDWCDataUVChannelIndex;
        NewSettings.bAllowOverwritePreferredDWCDataUVChannel =
            bAllowOverwriteExistingDataUVChannel ||
            (bPreferredChannelUnchanged && PreviousSettings.bAllowOverwritePreferredDWCDataUVChannel);
    }
    const bool bRequiresDataUVRelocation = DoesAssetSetupRequireDataUVRelocation(*Asset, NewSettings);
    const bool bLODRangeChanged =
        PreviousSettings.FirstGeneratedLODIndex != NewSettings.FirstGeneratedLODIndex ||
        PreviousSettings.LastGeneratedLODIndex != NewSettings.LastGeneratedLODIndex;
    const bool bGPUSimulationSettingChanged =
        PreviousSettings.bBuildGPUWetnessMapSimulationData !=
        NewSettings.bBuildGPUWetnessMapSimulationData;
    const bool bCPUSimulationDisabled =
        PreviousSettings.bBuildCPUVertexSimulationData && !NewSettings.bBuildCPUVertexSimulationData;
    const bool bGPUSimulationDisabled =
        PreviousSettings.bBuildGPUWetnessMapSimulationData && !NewSettings.bBuildGPUWetnessMapSimulationData;

    TOptional<FDWCLODRangeUpdateReport> LODRangeReport;
    TArray<int32> AddedLODIndices;
    TArray<int32> LODIndicesRequiringGeneration;
    auto HasUnresolvedDataUVResultForLOD = [Asset](const int32 LODIndex)
    {
#if WITH_EDITORONLY_DATA
        return Asset->Derived.Inline.LastDataUVSlotLODResults.ContainsByPredicate(
            [LODIndex](const FDWCDataUVSlotLODResult& Result)
            {
                return Result.LODIndex == LODIndex &&
                    (Result.State == EDWCDataUVSlotLODResultState::Failed ||
                     Result.State == EDWCDataUVSlotLODResultState::NotCommitted ||
                     Result.State == EDWCDataUVSlotLODResultState::NotGenerated);
            });
#else
        return false;
#endif
    };
    if (bLODRangeChanged)
    {
        FDWCLODRangeUpdateReport Report;
        Report.PreviousFirstLODIndex = PreviousSettings.FirstGeneratedLODIndex;
        Report.PreviousLastLODIndex = PreviousSettings.LastGeneratedLODIndex;
        Report.RequestedFirstLODIndex = NewSettings.FirstGeneratedLODIndex;
        Report.RequestedLastLODIndex = NewSettings.LastGeneratedLODIndex;
        Report.ActiveFirstLODIndex = NewSettings.FirstGeneratedLODIndex;
        Report.ActiveLastLODIndex = NewSettings.LastGeneratedLODIndex;

        for (int32 LODIndex = PreviousSettings.FirstGeneratedLODIndex;
             LODIndex <= PreviousSettings.LastGeneratedLODIndex;
             ++LODIndex)
        {
            if (LODIndex >= NewSettings.FirstGeneratedLODIndex &&
                LODIndex <= NewSettings.LastGeneratedLODIndex)
            {
                Report.RetainedLODIndices.Add(LODIndex);
            }
            else
            {
                Report.RemovedLODIndices.Add(LODIndex);
            }
        }
        for (int32 LODIndex = NewSettings.FirstGeneratedLODIndex;
             LODIndex <= NewSettings.LastGeneratedLODIndex;
             ++LODIndex)
        {
            if (LODIndex < PreviousSettings.FirstGeneratedLODIndex ||
                LODIndex > PreviousSettings.LastGeneratedLODIndex)
            {
                AddedLODIndices.Add(LODIndex);
                if (Asset->FindDataUVMetadataForLOD(LODIndex) != nullptr &&
                    Asset->HasValidDataUVForLOD(LODIndex) &&
                    !HasUnresolvedDataUVResultForLOD(LODIndex))
                {
                    Report.ReusedLODIndices.Add(LODIndex);
                }
            }
        }
        if (NewSettings.bBuildGPUWetnessMapSimulationData)
        {
            const bool bGPUWasJustEnabled =
                !PreviousSettings.bBuildGPUWetnessMapSimulationData &&
                NewSettings.bBuildGPUWetnessMapSimulationData;
            for (int32 LODIndex = NewSettings.FirstGeneratedLODIndex;
                 LODIndex <= NewSettings.LastGeneratedLODIndex;
                 ++LODIndex)
            {
                const bool bIsNewlyAdded = AddedLODIndices.Contains(LODIndex);
                if (!bIsNewlyAdded && !bGPUWasJustEnabled)
                {
                    continue;
                }

                const bool bHasReusablePayload =
                    Asset->FindDataUVMetadataForLOD(LODIndex) != nullptr &&
                    Asset->HasValidDataUVForLOD(LODIndex) &&
                    !HasUnresolvedDataUVResultForLOD(LODIndex);
                if (!bHasReusablePayload)
                {
                    LODIndicesRequiringGeneration.AddUnique(LODIndex);
                    Report.RetainedLODIndices.Remove(LODIndex);
                }
            }
            LODIndicesRequiringGeneration.Sort();
        }

        LODRangeReport = MoveTemp(Report);
    }

    // Apply non-channel settings while the WCA still references its current sealed DWC UV Channel.
    // RelocateChannel updates the preferred channel only after the prepared-mesh copy succeeds.
    FDWCWetClothingAssetSetupSettings SettingsToApply = NewSettings;
    if (bRequiresDataUVRelocation)
    {
        SettingsToApply.PreferredDWCDataUVChannelIndex = PreviousSettings.PreferredDWCDataUVChannelIndex;
    }

    Asset->Modify();
    FString ChangeSummary;
    if (!Asset->ApplySetupSettings(SettingsToApply, &ChangeSummary))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Error,
            EAppMsgType::Ok,
            FText::FromString(ChangeSummary.IsEmpty() ? TEXT("Failed to apply Wet Clothing Asset setup settings.") : ChangeSummary));
        return;
    }

    FString RangeSyncSummary;
    if (Asset->HasLockedDataUVLayout() && bLODRangeChanged && LODRangeReport.IsSet())
    {
        FDWCLODRangeUpdateReport& Report = LODRangeReport.GetValue();
        TArray<int32> GeneratedThisAttempt;
        TArray<int32> FailedThisAttempt;

        if (NewSettings.bBuildGPUWetnessMapSimulationData)
        {
            const TSet<int32> IncludedMaterialSlotIndices = CollectWettableMaterialSlotIndices(*Asset);
            for (const int32 LODIndex : LODIndicesRequiringGeneration)
            {
                FDWCDataUVBuildOptions BuildOptions;
                BuildOptions.TargetLODIndices.Add(LODIndex);
                BuildOptions.bMergeWithExistingLayout = true;
                // Commit successful material slots for this LOD even when another slot fails.
                // The range remains unchanged until every required slot succeeds, but completed
                // LOD payloads stay available for reuse on the next attempt.
                BuildOptions.bRequireAllMaterialSlots = false;
                const FDWCDataUVBuildResult LODResult =
                    GenerateDWCDataUVWithVisibleExclusionConfirmation(
                        *Asset,
                        false,
                        true,
                        false,
                        IncludedMaterialSlotIndices,
                        &BuildOptions);

                const bool bLODComplete =
                    LODResult.bSucceeded && LODResult.FailedMaterialSlotIndices.IsEmpty();
                FDWCLODRangeUpdateLODDetail& Detail = Report.LODDetails.AddDefaulted_GetRef();
                Detail.LODIndex = LODIndex;
                Detail.bSucceeded = bLODComplete;
                Detail.bHasNotes = bLODComplete &&
                    LODResult.ResultSeverity == EDWCDataUVResultSeverity::ReadyWithNotes;
                Detail.bHasWarnings = bLODComplete &&
                    LODResult.ResultSeverity == EDWCDataUVResultSeverity::ReadyWithWarnings;
                Detail.Message = LODResult.Message;

                if (bLODComplete)
                {
                    GeneratedThisAttempt.Add(LODIndex);
                }
                else
                {
                    FailedThisAttempt.Add(LODIndex);
                }
            }
        }

        if (!FailedThisAttempt.IsEmpty())
        {
            FString RevertSummary;
            Asset->ApplySetupSettings(PreviousSettings, &RevertSummary);
            Report.bApplied = false;
            Report.ActiveFirstLODIndex = PreviousSettings.FirstGeneratedLODIndex;
            Report.ActiveLastLODIndex = PreviousSettings.LastGeneratedLODIndex;
            Report.PreparedLODIndices = GeneratedThisAttempt;
            for (const int32 ReusedLODIndex : Report.ReusedLODIndices)
            {
                Report.PreparedLODIndices.AddUnique(ReusedLODIndex);
            }
            Report.PreparedLODIndices.Sort();
            Report.FailedLODIndices = MoveTemp(FailedThisAttempt);
            Report.FailedLODIndices.Sort();
            Report.GeneratedLODIndices.Reset();
            Report.ReusedLODIndices.Reset();
            Report.RemovedLODIndices.Reset();
            Asset->SetLastBakeFailure(TEXT("The requested LOD range was not activated because one or more LODs failed DWC UV generation."));
            RefreshAssetStateAndEditor();
            WCAReportDialogs::OpenLODRangeUpdateDialog(Report);
            return;
        }

        // A successful range update only needs detail cards for generated LODs that
        // produced diagnostics. Plain Ready results are already summarized above.
        Report.LODDetails.RemoveAll(
            [](const FDWCLODRangeUpdateLODDetail& Detail)
            {
                return Detail.bSucceeded && !Detail.bHasNotes && !Detail.bHasWarnings;
            });
        Report.GeneratedLODIndices = MoveTemp(GeneratedThisAttempt);
        Report.GeneratedLODIndices.Sort();
        Report.bApplied = true;
        Report.ActiveFirstLODIndex = NewSettings.FirstGeneratedLODIndex;
        Report.ActiveLastLODIndex = NewSettings.LastGeneratedLODIndex;

        TSet<int32> RetainedPayloadLODIndices;
        RetainedPayloadLODIndices.Add(UWetClothingAsset::RuntimeSimulationLODIndex);
        if (NewSettings.bBuildGPUWetnessMapSimulationData)
        {
            for (int32 LODIndex = NewSettings.FirstGeneratedLODIndex;
                 LODIndex <= NewSettings.LastGeneratedLODIndex;
                 ++LODIndex)
            {
                RetainedPayloadLODIndices.Add(LODIndex);
            }
        }
        Asset->PruneDataUVLODData(RetainedPayloadLODIndices);
    }
    else if (Asset->HasLockedDataUVLayout() && bGPUSimulationSettingChanged)
    {
        FDWCDataUVBuildOptions BuildOptions;
        BuildOptions.bRequireAllMaterialSlots = true;
        const TSet<int32> IncludedMaterialSlotIndices = CollectWettableMaterialSlotIndices(*Asset);
        const FDWCDataUVBuildResult RangeSyncResult =
            GenerateDWCDataUVWithVisibleExclusionConfirmation(
                *Asset,
                false,
                true,
                false,
                IncludedMaterialSlotIndices,
                &BuildOptions);
        if (!RangeSyncResult.bSucceeded)
        {
            FString RevertSummary;
            Asset->ApplySetupSettings(PreviousSettings, &RevertSummary);
            Asset->SetLastBakeFailure(RangeSyncResult.Message);
            RefreshAssetStateAndEditor();
            WCAReportDialogs::OpenDWCDataUVBuildFailureDialog(
                RangeSyncResult,
                Asset,
                RangeSyncResult.PreparedMesh != nullptr
                    ? RangeSyncResult.PreparedMesh
                    : Asset->GetRuntimeSkeletalMesh(),
                IncludedMaterialSlotIndices);
            return;
        }
        RangeSyncSummary = RangeSyncResult.Message;
    }
    else if (bLODRangeChanged && LODRangeReport.IsSet())
    {
        // No sealed layout exists yet. The range setting changed, but there is no DWC UV payload to regenerate.
        LODRangeReport.GetValue().bApplied = true;
    }

    if (bCPUSimulationDisabled)
    {
        Asset->ClearPrecomputedSimulationData();
    }
    if (bGPUSimulationDisabled)
    {
        Asset->ClearGPUWetMapData();
    }
    FWCAGeneratedDataInvalidator::InvalidateAsset(*Asset);
    Asset->MarkPackageDirty();

    if (bRequiresDataUVRelocation)
    {
        const FDWCDataUVBuildResult RelocationResult = FDWCDataUVBuildService::RelocateChannel(
            *Asset,
            NewSettings.PreferredDWCDataUVChannelIndex,
            bAllowOverwriteExistingDataUVChannel);
        if (!RelocationResult.bSucceeded)
        {
            Asset->SetLastBakeFailure(RelocationResult.Message);
            RefreshAssetStateAndEditor();
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(RelocationResult.Message));
            return;
        }

        Asset->MarkPackageDirty();
        RefreshAssetStateAndEditor();
        if (LODRangeReport.IsSet())
        {
            TArray<FString> AdditionalParts;
            if (!ChangeSummary.IsEmpty() && ChangeSummary != TEXT("Wet Clothing setup settings are unchanged."))
            {
                AdditionalParts.Add(ChangeSummary);
            }
            AdditionalParts.Add(RelocationResult.Message);
            LODRangeReport.GetValue().AdditionalSummary = FString::Join(AdditionalParts, TEXT("\n"));
            WCAReportDialogs::OpenLODRangeUpdateDialog(LODRangeReport.GetValue());
        }
        else
        {
            TArray<FString> SummaryParts;
            if (!ChangeSummary.IsEmpty() && ChangeSummary != TEXT("Wet Clothing setup settings are unchanged."))
            {
                SummaryParts.Add(ChangeSummary);
            }
            if (!RangeSyncSummary.IsEmpty())
            {
                SummaryParts.Add(RangeSyncSummary);
            }
            SummaryParts.Add(RelocationResult.Message);
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Join(SummaryParts, TEXT("\n"))));
        }
        return;
    }

    RefreshAssetStateAndEditor();
    if (LODRangeReport.IsSet())
    {
        if (!ChangeSummary.IsEmpty() && ChangeSummary != TEXT("Wet Clothing setup settings are unchanged."))
        {
            LODRangeReport.GetValue().AdditionalSummary = ChangeSummary;
        }
        WCAReportDialogs::OpenLODRangeUpdateDialog(LODRangeReport.GetValue());
        return;
    }

    TArray<FString> SummaryParts;
    if (!ChangeSummary.IsEmpty())
    {
        SummaryParts.Add(ChangeSummary);
    }
    if (!RangeSyncSummary.IsEmpty())
    {
        SummaryParts.Add(RangeSyncSummary);
    }
    if (!SummaryParts.IsEmpty())
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(FString::Join(SummaryParts, TEXT("\n"))));
    }
}

void FWCAEditor::HandleInitializeGeneratedDataUVClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    if (!ConfirmMissingPreparedMeshRecovery(*Asset))
    {
        return;
    }

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

    InitializeGeneratedDataUV(bAllowOverwriteExistingDataUVChannel);
}

void FWCAEditor::InitializeGeneratedDataUV(
    const bool bAllowOverwriteExistingDataUVChannel,
    const bool bUsePreferredDataUVChannel)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

    Asset->Modify();
    const TSet<int32> IncludedMaterialSlotIndices = CollectWettableMaterialSlotIndices(*Asset);
    FScopedSlowTask SlowTask(
        2.0f,
        FText::FromString(FString::Printf(TEXT("Initializing DWC UV Channel for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateDataUVBuildProgress", "Building DWC mesh UV data..."));
    const FDWCDataUVBuildResult Result = GenerateDWCDataUVWithVisibleExclusionConfirmation(
        *Asset,
        false,
        bAllowOverwriteExistingDataUVChannel,
        bUsePreferredDataUVChannel,
        IncludedMaterialSlotIndices);
    if (!Result.bSucceeded)
    {
        Asset->SetLastBakeFailure(Result.Message);
        // Generate() already invalidated transient derived data. Refresh the editor as well so
        // panels do not keep local copies of pre-initialization UV view data after a failed initial build.
        RefreshAssetStateAndEditor();
        WCAReportDialogs::OpenDWCDataUVBuildFailureDialog(
            Result,
            Asset,
            Result.PreparedMesh != nullptr ? Result.PreparedMesh : Asset->GetRuntimeSkeletalMesh(),
            IncludedMaterialSlotIndices);
        return;
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("GenerateDataUVRefreshProgress", "Refreshing Wet Clothing Asset editor state..."));
    Asset->MarkPackageDirty();
    RefreshAssetStateAndEditor();
    if (Result.bGeneratedWithWarnings)
    {
        WCAReportDialogs::OpenDWCDataUVBuildResultDialog(
            Result,
            Asset,
            Result.PreparedMesh != nullptr ? Result.PreparedMesh : Asset->GetRuntimeSkeletalMesh(),
            IncludedMaterialSlotIndices);
    }
    else
    {
        FMessageDialog::Open(
            EAppMsgCategory::Success,
            EAppMsgType::Ok,
            FText::FromString(Result.Message));
    }
}

void FWCAEditor::HandleValidationClicked()
{
    if (WetClothingAsset.Get() == nullptr)
    {
        return;
    }

    TSharedRef<SWindow> DialogWindow = SNew(SWindow)
        .Title(LOCTEXT("ValidationResultsWindowTitle", "Validation Results"))
        .ClientSize(FVector2D(700.0f, 620.0f))
        .SizingRule(ESizingRule::UserSized)
        .SupportsMaximize(true)
        .SupportsMinimize(false);

    RefreshValidationDialogContent(DialogWindow);
    FSlateApplication::Get().AddModalWindow(DialogWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
}

void FWCAEditor::RefreshValidationDialogContent(const TSharedRef<SWindow>& DialogWindow)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        DialogWindow->RequestDestroyWindow();
        return;
    }

    // Explicit validation is the only routine editor action that performs full
    // runtime/map signature and generated-material graph validation.
    Asset->RefreshBakeState(true);
    if (EditorPanel.IsValid())
    {
        EditorPanel->RefreshStatusFromAsset();
    }
    RegenerateMenusAndToolbars();

#if WITH_EDITORONLY_DATA
    const FWCAValidationReport ValidationReport =
        BuildWCAValidationReport(*Asset, EWCAValidationMode::Deep, false);
    const FDWCTriangleValidationSummary& Summary = ValidationReport.Diagnostics;

    FString Examples;
    for (const int32 TriangleIndex : Summary.ExampleTriangleIndices)
    {
        if (!Examples.IsEmpty())
        {
            Examples += TEXT(", ");
        }
        Examples += FString::FromInt(TriangleIndex);
    }
    if (Examples.IsEmpty())
    {
        Examples = TEXT("None");
    }

    const TWeakPtr<SWindow> WeakDialogWindow(DialogWindow);
    const FDWCOnValidationFixRequested OnFixRequested =
        FDWCOnValidationFixRequested::CreateLambda(
            [this, WeakDialogWindow](const EWCAValidationFixKind FixKind)
            {
                if (TSharedPtr<SWindow> PinnedWindow = WeakDialogWindow.Pin())
                {
                    PinnedWindow->RequestDestroyWindow();
                }

                switch (FixKind)
                {
                case EWCAValidationFixKind::Save:
                    SaveAsset_Execute();
                    break;
                case EWCAValidationFixKind::PrepareRuntimeData:
                    HandleBuildCPURuntimeDataClicked();
                    break;
                case EWCAValidationFixKind::InitializeDataUV:
                    HandleInitializeGeneratedDataUVClicked();
                    break;
                case EWCAValidationFixKind::BakeGPUMaps:
                    HandleBuildGPURuntimeDataClicked();
                    break;
                case EWCAValidationFixKind::BakeRenderProfileData:
                    HandleBakeRenderProfileDataClicked();
                    break;
                case EWCAValidationFixKind::BakeWrinkleMaps:
                    HandleBakeWrinkleNormalMapClicked();
                    break;
                case EWCAValidationFixKind::BakeTransparencyMaps:
                    HandleBakeTransparencyMapsClicked();
                    break;
                case EWCAValidationFixKind::GenerateMaterials:
                    HandleGenerateMaterialsClicked();
                    break;
                case EWCAValidationFixKind::None:
                case EWCAValidationFixKind::Manual:
                default:
                    break;
                }
            });

    DialogWindow->SetContent(BuildValidationDialogContent(
        *Asset,
        ValidationReport,
        Examples,
        FOnClicked::CreateSP(this, &FWCAEditor::HandleValidationResolveClicked, WeakDialogWindow),
        FOnClicked::CreateSP(this, &FWCAEditor::HandleValidationRefreshClicked, WeakDialogWindow),
        OnFixRequested));
#endif
}

FReply FWCAEditor::HandleValidationRefreshClicked(TWeakPtr<SWindow> DialogWindow)
{
    if (TSharedPtr<SWindow> PinnedDialog = DialogWindow.Pin())
    {
        RefreshValidationDialogContent(PinnedDialog.ToSharedRef());
    }
    return FReply::Handled();
}

FReply FWCAEditor::HandleValidationResolveClicked(TWeakPtr<SWindow> DialogWindow)
{
    FString Failure;
    FString SuccessSummary;
    if (!ResolveIssuesAndSave(Failure, &SuccessSummary))
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

    const FString SuccessMessage = SuccessSummary.IsEmpty()
        ? FString(TEXT("Validation issues were resolved and saved."))
        : FString::Printf(TEXT("Validation issues were resolved and saved.\n\n%s"), *SuccessSummary);
    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        FText::FromString(SuccessMessage));
    RefreshAssetStateAndEditor();
    return FReply::Handled();
}

bool FWCAEditor::CanBuildGPURuntimeData() const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        !Asset->HasAnyWettableMaterialSlot() ||
        !Asset->GetSetupSettings().bBuildGPUWetnessMapSimulationData)
    {
        return false;
    }

#if WITH_EDITORONLY_DATA
    static const FName GPURuntimeIssueId(TEXT("GPURuntimeData"));
    static const FName GPUMapIssueId(TEXT("GPUMaps"));
    const FWCAValidationReport Report = BuildWCAValidationReport(*Asset, EWCAValidationMode::Fast, false);
    if (IsValidationSectionBlockedByManualIssue(Report, EWCAValidationSection::GPUSimulationMaps))
    {
        return false;
    }
    return Report.Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return (Issue.IssueId == GPURuntimeIssueId || Issue.IssueId == GPUMapIssueId) &&
                   Issue.FixKind != EWCAValidationFixKind::None &&
                   Issue.FixKind != EWCAValidationFixKind::Manual;
        });
#else
    return false;
#endif
}

bool FWCAEditor::CanBakeRenderProfileData() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && Asset->HasAnyWettableMaterialSlot() &&
           FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(Asset, nullptr);
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

bool FWCAEditor::CanBuildCPURuntimeData() const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        !Asset->HasAnyWettableMaterialSlot() ||
        !Asset->GetSetupSettings().bBuildCPUVertexSimulationData)
    {
        return false;
    }

#if WITH_EDITORONLY_DATA
    static const FName CPURuntimeIssueId(TEXT("CPURuntimeData"));
    const FWCAValidationReport Report = BuildWCAValidationReport(*Asset, EWCAValidationMode::Fast, false);
    return Report.Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.IssueId == CPURuntimeIssueId &&
                   Issue.FixKind != EWCAValidationFixKind::None &&
                   Issue.FixKind != EWCAValidationFixKind::Manual;
        });
#else
    return false;
#endif
}

bool FWCAEditor::CanBuildAllRequired() const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

#if WITH_EDITORONLY_DATA
    const FWCAValidationReport Report = BuildWCAValidationReport(*Asset, EWCAValidationMode::Fast, false);
    return Report.Issues.ContainsByPredicate(
        [&Report](const FWCAValidationIssue& Issue)
        {
            if (Issue.Section == EWCAValidationSection::DataUV ||
                Issue.Section == EWCAValidationSection::FailureDetails ||
                Issue.FixKind == EWCAValidationFixKind::None ||
                Issue.FixKind == EWCAValidationFixKind::Manual)
            {
                return false;
            }
            return !IsValidationSectionBlockedByManualIssue(Report, Issue.Section);
        });
#else
    return false;
#endif
}

TSharedRef<SWidget> FWCAEditor::BuildRuntimeBuildMenu()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->RefreshBakeState(false);
    }

    FWCARuntimeBuildMenuArgs Args;
    Args.OnBuildAllRequired = FSimpleDelegate::CreateLambda([this]() { HandleBuildAllRequiredClicked(); });
    Args.OnBuildCPURuntimeData = FSimpleDelegate::CreateLambda([this]() { HandleBuildCPURuntimeDataClicked(); });
    Args.OnBuildGPURuntimeData = FSimpleDelegate::CreateLambda([this]() { HandleBuildGPURuntimeDataClicked(); });
    Args.OnGenerateMaterials = FSimpleDelegate::CreateLambda([this]() { HandleGenerateMaterialsClicked(); });
    Args.OnBuildRenderProfileData = FSimpleDelegate::CreateLambda([this]() { HandleBakeRenderProfileDataClicked(); });
    Args.OnBakeWrinkleTextures = FSimpleDelegate::CreateLambda([this]() { HandleBakeWrinkleNormalMapClicked(); });
    Args.OnBakeTransparencyTextures = FSimpleDelegate::CreateLambda([this]() { HandleBakeTransparencyMapsClicked(); });
    Args.CanBuildAllRequired = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBuildAllRequired);
    Args.CanBuildCPURuntimeData = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBuildCPURuntimeData);
    Args.CanBuildGPURuntimeData = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBuildGPURuntimeData);
    Args.CanGenerateMaterials = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanGenerateMaterials);
    Args.CanBuildRenderProfileData = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeRenderProfileData);
    Args.CanBakeWrinkleTextures = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeWrinkleMaps);
    Args.CanBakeTransparencyTextures = FCanExecuteAction::CreateSP(this, &FWCAEditor::CanBakeTransparencyMaps);
    return FWCAEditorWidgets::BuildRuntimeBuildMenu(Args);
}

TSharedRef<SWidget> FWCAEditor::BuildPreviewDiagnosticsMenu()
{
    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.BeginSection(TEXT("PreviewDiagnostics"), LOCTEXT("PreviewDiagnosticsMenuSection", "PREVIEW DIAGNOSTICS"));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("DumpPreviewDiagnosticsMenuItem", "Dump GPU Residency"),
        LOCTEXT("DumpPreviewDiagnosticsMenuItemTooltip", "Write active preview sessions, GPU Resident/Retiring texture bytes, and render upload queue state to the Output Log."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Info")),
        FUIAction(FExecuteAction::CreateSP(this, &FWCAEditor::HandleDumpPreviewDiagnostics)));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("ResetPreviewDiagnosticsMenuItem", "Reset Preview Counters"),
        LOCTEXT("ResetPreviewDiagnosticsMenuItemTooltip", "Reset preview cache, texture workspace, and render upload queue counters. Current residency becomes the new high-water baseline."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
        FUIAction(FExecuteAction::CreateSP(this, &FWCAEditor::HandleResetPreviewDiagnostics)));
    MenuBuilder.EndSection();
    return MenuBuilder.MakeWidget();
}

void FWCAEditor::HandleDumpPreviewDiagnostics()
{
    FDWCEditorPreviewDiagnostics::DumpAllSessions();
}

void FWCAEditor::HandleResetPreviewDiagnostics()
{
    FDWCEditorPreviewDiagnostics::ResetAllCounters();
}

FReply FWCAEditor::HandleBuildCPURuntimeDataClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !CanBuildCPURuntimeData())
    {
        return FReply::Handled();
    }

    Asset->Modify();
    Asset->RefreshBakeState(false);
    const bool bNeedsBuild = IsValidationActionRequiredStatus(Asset->GetBakeState().CPURuntimeData);

    FScopedSlowTask SlowTask(
        bNeedsBuild ? 2.0f : 1.0f,
        FText::FromString(FString::Printf(TEXT("Building CPU Runtime Data for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);

    if (bNeedsBuild)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("BuildCPURuntimeDataProgress", "Building CPU Runtime Data..."));
        FString Failure;
        if (!Asset->RebuildPrecomputedSimulationData(&Failure))
        {
            Asset->SetCPURuntimeDataStatus(EDWCBakeStatus::Failed, Failure);
            RefreshAssetStateAndEditor();
            FMessageDialog::Open(
                EAppMsgCategory::Error,
                EAppMsgType::Ok,
                FText::FromString(Failure.IsEmpty() ? TEXT("CPU Runtime Data build failed.") : Failure));
            return FReply::Handled();
        }
        Asset->SetCPURuntimeDataStatus(EDWCBakeStatus::Valid);
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BuildCPURuntimeDataSaveProgress", "Saving CPU Runtime Data..."));
    if (!DWCEditorUtils::SaveAsset(Asset, false))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BuildCPURuntimeDataSaveFailed", "CPU Runtime Data was built, but the Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }

    RefreshAssetStateAndEditor();
    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        LOCTEXT("BuildCPURuntimeDataSucceeded", "CPU Runtime Data was built and saved."));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBuildAllRequiredClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

#if WITH_EDITORONLY_DATA
    const FWCAValidationReport InitialReport = BuildWCAValidationReport(*Asset, EWCAValidationMode::Deep, false);
    const bool bMeshOrUVRequiresAction = InitialReport.Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Section == EWCAValidationSection::DataUV;
        });
    if (bMeshOrUVRequiresAction)
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT(
                "BuildAllRequiredMeshUVRequired",
                "Mesh & UV requires attention before Build for Runtime can continue. Open Validation; a sealed invalid UV layout requires a new WCA."));
        return FReply::Handled();
    }
#endif

    FString Failure;
    FString SuccessSummary;
    if (!ResolveIssuesAndSave(Failure, &SuccessSummary))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Error,
            EAppMsgType::Ok,
            FText::FromString(Failure.IsEmpty() ? TEXT("Required runtime outputs could not be completed.") : Failure));
        return FReply::Handled();
    }

    RefreshAssetStateAndEditor();
    const FString SuccessMessage = SuccessSummary.IsEmpty()
        ? FString(TEXT("All required runtime outputs are up to date."))
        : FString::Printf(TEXT("All required runtime outputs were completed.\n\n%s"), *SuccessSummary);
    FMessageDialog::Open(EAppMsgCategory::Success, EAppMsgType::Ok, FText::FromString(SuccessMessage));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeRenderProfileDataClicked()
{
    if (!CanBakeRenderProfileData())
    {
        return FReply::Handled();
    }
    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }

    FScopedSlowTask SlowTask(
        2.0f,
        LOCTEXT("BakeRenderProfileDataProgress", "Baking the Render Profile Lookup Texture..."));
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BakeRenderProfileDataBuildProgress", "Baking Wet Part Data textures..."));

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
        LOCTEXT("BakeRenderProfileDataSaveProgress", "Saving render profile assets..."));
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !DWCEditorUtils::SaveAsset(Asset))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeRenderProfileDataSaveFailed", "The Render Profile Lookup Texture was baked, but the generated textures or Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }
    RefreshAssetStateAndEditor();

    const EAppMsgCategory MessageCategory = bHadWarnings ? EAppMsgCategory::Warning : EAppMsgCategory::Success;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Summary));
    return FReply::Handled();
}

FReply FWCAEditor::HandleBuildGPURuntimeDataClicked()
{
    if (!CanBuildGPURuntimeData())
    {
        return FReply::Handled();
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    Asset->Modify();
    Asset->RefreshBakeState(false);
    const bool bNeedsRuntimeBuild = IsValidationActionRequiredStatus(Asset->GetBakeState().GPURuntimeData);
    const bool bNeedsLookupBuildBeforeRuntime = IsValidationActionRequiredStatus(Asset->GetBakeState().GPUMaps);

    FScopedSlowTask SlowTask(
        3.0f,
        FText::FromString(FString::Printf(TEXT("Building GPU Runtime Data for %s..."), *GetNameSafe(Asset))));
    SlowTask.MakeDialog(false);

    SlowTask.EnterProgressFrame(
        1.0f,
        bNeedsRuntimeBuild
            ? LOCTEXT("BuildGPURuntimePayloadProgress", "Building the GPU Runtime Data payload...")
            : LOCTEXT("BuildGPURuntimePayloadCurrent", "GPU Runtime Data payload is up to date..."));
    if (bNeedsRuntimeBuild)
    {
        FString RuntimeFailure;
        if (!EnsureGPURuntimeDataReadyForMapBake(Asset, RuntimeFailure))
        {
            Asset->SetGPURuntimeDataStatus(EDWCBakeStatus::Failed, RuntimeFailure);
            RefreshAssetStateAndEditor();
            FMessageDialog::Open(
                EAppMsgCategory::Error,
                EAppMsgType::Ok,
                FText::FromString(RuntimeFailure.IsEmpty() ? TEXT("GPU Runtime Data build failed.") : RuntimeFailure));
            return FReply::Handled();
        }
    }

    Asset->RefreshBakeState(false);
    const bool bNeedsLookupBuild =
        bNeedsLookupBuildBeforeRuntime || IsValidationActionRequiredStatus(Asset->GetBakeState().GPUMaps);
    SlowTask.EnterProgressFrame(
        1.0f,
        bNeedsLookupBuild
            ? LOCTEXT("BuildGPURuntimeLookupProgress", "Building the GPU simulation lookup data...")
            : LOCTEXT("BuildGPURuntimeLookupCurrent", "GPU simulation lookup data is up to date..."));
    if (bNeedsLookupBuild)
    {
        FString LookupFailure;
        if (!Asset->BakeGPUWetnessMaps(&LookupFailure))
        {
            RefreshAssetStateAndEditor();
            FMessageDialog::Open(
                EAppMsgCategory::Error,
                EAppMsgType::Ok,
                FText::FromString(LookupFailure.IsEmpty()
                    ? TEXT("GPU Runtime Data simulation lookup build failed.")
                    : FString::Printf(TEXT("GPU Runtime Data: %s"), *LookupFailure)));
            return FReply::Handled();
        }
    }

    SlowTask.EnterProgressFrame(
        1.0f,
        LOCTEXT("BuildGPURuntimeDataSaveProgress", "Saving GPU Runtime Data..."));
    if (!DWCEditorUtils::SaveAsset(Asset, false))
    {
        RefreshAssetStateAndEditor();
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BuildGPURuntimeDataSaveFailed", "GPU Runtime Data was built, but the Wet Clothing Asset could not be saved."));
        return FReply::Handled();
    }

    RefreshAssetStateAndEditor();
    FMessageDialog::Open(
        EAppMsgCategory::Success,
        EAppMsgType::Ok,
        LOCTEXT("BuildGPURuntimeDataSucceeded", "GPU Runtime Data was built and saved."));
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

    TWeakPtr<FWCAEditor> WeakEditor = SharedThis(this);
    FString RequestError;
    if (!EditorPanel->RequestBakeAllWrinkleMaps(
            [WeakEditor](const FDWCEditorBakeBatchResult& Result)
            {
                const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin();
                if (!Editor.IsValid())
                {
                    return;
                }
                Editor->RefreshAssetStateAndEditor();
                FMessageDialog::Open(
                    Result.bSucceeded
                        ? EAppMsgCategory::Success
                        : EAppMsgCategory::Warning,
                    EAppMsgType::Ok,
                    FText::FromString(Result.Summary));
            },
            &RequestError))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(RequestError));
    }
    return FReply::Handled();
}

FReply FWCAEditor::HandleBakeTransparencyMapsClicked()
{
    if (!CanBakeTransparencyMaps())
    {
        return FReply::Handled();
    }

    if (!EditorPanel.IsValid())
    {
        return FReply::Handled();
    }
    TWeakPtr<FWCAEditor> WeakEditor = SharedThis(this);
    FString RequestError;
    if (!EditorPanel->RequestBakeAllTransparencyMaps(
            [WeakEditor](const FDWCEditorBakeBatchResult& Result)
            {
                const TSharedPtr<FWCAEditor> Editor = WeakEditor.Pin();
                if (!Editor.IsValid())
                {
                    return;
                }
                Editor->RefreshAssetStateAndEditor();
                FMessageDialog::Open(
                    Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Warning,
                    EAppMsgType::Ok,
                    FText::FromString(Result.Summary));
            },
            &RequestError))
    {
        FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::Ok, FText::FromString(RequestError));
    }
    return FReply::Handled();
}

bool FWCAEditor::HasMaterialGenerationPrerequisites(FText* OutFailureReason) const
{
    auto SetFailure = [OutFailureReason](const FText& Failure)
    {
        if (OutFailureReason != nullptr)
        {
            *OutFailureReason = Failure;
        }
    };

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        SetFailure(LOCTEXT("GenerateMaterialsAssetUnavailable", "The Wet Clothing Asset is unavailable."));
        return false;
    }

    if (!Asset->HasAnyWettableMaterialSlot())
    {
        SetFailure(LOCTEXT("GenerateMaterialsNoWettableSlotsTooltip", "No wettable material slots."));
        return false;
    }

    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    if (!Setup.bBuildCPUVertexSimulationData && !Setup.bBuildGPUWetnessMapSimulationData)
    {
        SetFailure(LOCTEXT("GenerateMaterialsNoBackendTooltip", "Enable CPU or GPU simulation data in Asset Setup first."));
        return false;
    }

    USkeletalMesh* RuntimeMesh = Asset->GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        SetFailure(LOCTEXT("GenerateMaterialsNoMeshTooltip", "Prepared Mesh must be generated first."));
        return false;
    }

    if (!Asset->HasValidDataUVForLOD(Asset->GetSimulationLODIndex()))
    {
        SetFailure(LOCTEXT("GenerateMaterialsNoDataUVTooltip", "DWC UV Channel must be generated first."));
        return false;
    }

    const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();
    for (const FWetClothingAuthoredMaterialSlot& SlotState : Asset->Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (!SlotState.bIsWettableSlot || SlotState.MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        if (!Materials.IsValidIndex(SlotState.MaterialSlotIndex))
        {
            SetFailure(FText::Format(
                LOCTEXT("GenerateMaterialsInvalidSlotTooltip", "Wettable slot {0} is not available on the Prepared Mesh."),
                FText::AsNumber(SlotState.MaterialSlotIndex)));
            return false;
        }

        UMaterialInterface* SourceMaterial = FWCAMaterialGenerator::ResolveGeneratedMaterialSource(
            Asset,
            SlotState.MaterialSlotIndex,
            Materials[SlotState.MaterialSlotIndex].MaterialInterface);
        if (SourceMaterial == nullptr)
        {
            SetFailure(FText::Format(
                LOCTEXT("GenerateMaterialsMissingSourceTooltip", "Wettable slot {0} has no source material."),
                FText::AsNumber(SlotState.MaterialSlotIndex)));
            return false;
        }
    }

    SetFailure(FText::GetEmpty());
    return true;
}

bool FWCAEditor::IsMaterialGenerationRequired() const
{
    if (!HasMaterialGenerationPrerequisites(nullptr))
    {
        return false;
    }

    TArray<FString> Messages;
    FWCAMaterialGenerator::ValidateGeneratedMaterialOverrideReferences(WetClothingAsset.Get(), Messages);
    return !Messages.IsEmpty();
}

bool FWCAEditor::CanGenerateMaterials() const
{
    return HasMaterialGenerationPrerequisites(nullptr) && IsMaterialGenerationRequired();
}

FText FWCAEditor::GetGenerateMaterialsTooltip() const
{
    FText FailureReason;
    if (!HasMaterialGenerationPrerequisites(&FailureReason))
    {
        return FailureReason;
    }

    if (!IsMaterialGenerationRequired())
    {
        return LOCTEXT("GenerateMaterialsUpToDateTooltip", "Generated materials are up to date.");
    }

    return LOCTEXT(
        "GenerateMaterialsRequiredTooltip",
        "Generate or update the shared DWC material and runtime-selectable instance for wettable slots.");
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
            LOCTEXT("GenerateMaterialsNoDataUV", "Generate DWC UV Channel before generating wet materials."));
        return FReply::Handled();
    }

    TArray<int32> WettableSlots;
    for (const FWetClothingAuthoredMaterialSlot& SlotState : Asset->Authored.PartData.EditableWetPartData.MaterialSlots)
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

    Asset->Modify();
    bool bUpdatedAnyMaterial = false;
    for (const int32 MaterialSlotIndex : WettableSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            Failures.Add(FString::Printf(TEXT("Slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial =
            FWCAMaterialGenerator::ResolveGeneratedMaterialSource(Asset, MaterialSlotIndex, Materials[MaterialSlotIndex].MaterialInterface);
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
            ExistingOverride->GeneratedMaterialInstance != nullptr;

        SlowTask.EnterProgressFrame(
            1.0f,
            FText::FromString(FString::Printf(
                TEXT("Generating shared material and runtime instance for slot %d from '%s'..."),
                MaterialSlotIndex,
                *GetNameSafe(SourceMaterial))));

        const FWCAMaterialGenerator::FOptions MaterialSetupOptions =
            FWCAMaterialGenerator::MakeOptionsForAsset(
                Asset,
                EDWCSimulationMode::VertexCPU,
                MaterialSlotIndex);
        const FWetClothingUnifiedMaterialSetupResult MaterialSet =
            FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);
        if (!MaterialSet.bSucceeded || MaterialSet.GeneratedMaterial == nullptr ||
            MaterialSet.GeneratedMaterialInstance == nullptr)
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

#if WITH_EDITORONLY_DATA
        Asset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction =
            MaterialSet.EvaluateSurfaceAppearanceFunction;
#endif
        ExistingOverride->SourceMaterial = SourceMaterial;
        ExistingOverride->GeneratedMaterial = MaterialSet.GeneratedMaterial;
        ExistingOverride->GeneratedMaterialInstance = MaterialSet.GeneratedMaterialInstance;
        ExistingOverride->GeneratorVersion = FWCAMaterialGenerator::GeneratedMaterialGeneratorVersion;
        ExistingOverride->GenerationSignature = FWCAMaterialGenerator::BuildGeneratedMaterialSignature(
            Asset,
            MaterialSlotIndex,
            SourceMaterial);

        UMaterialInterface* CurrentMaterial = RuntimeMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface;
        const bool bCanApplyGeneratedMaterial = CurrentMaterial == nullptr ||
            CurrentMaterial == SourceMaterial ||
            (ExistingOverride != nullptr &&
                (CurrentMaterial == ExistingOverride->GeneratedMaterial ||
                 CurrentMaterial == ExistingOverride->GeneratedMaterialInstance)) ||
            IsSameMaterialFamily(CurrentMaterial, SourceMaterial);
        if (bCanApplyGeneratedMaterial)
        {
            RuntimeMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface = MaterialSet.GeneratedMaterialInstance;
            RuntimeMesh->MarkPackageDirty();
        }
        bUpdatedAnyMaterial = true;

        UpdatedMaterials.Add(FString::Printf(
            TEXT("Slot %d -> shared %s, runtime %s (%s)"),
            MaterialSlotIndex,
            *GetNameSafe(MaterialSet.GeneratedMaterial),
            *GetNameSafe(MaterialSet.GeneratedMaterialInstance),
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

bool FWCAEditor::ResolveIssuesAndSave(FString& OutFailure, FString* OutSuccessSummary)
{
    OutFailure.Reset();
    if (OutSuccessSummary != nullptr)
    {
        OutSuccessSummary->Reset();
    }
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

    const FDWCWetClothingAssetSetupSettings& Setup = Asset->GetSetupSettings();
    FWCAEditorIssueStatus Status = EditorPanel->CollectIssueStatus(true, true);
    const FWCAValidationReport InitialValidationReport =
        BuildWCAValidationReport(*Asset, EWCAValidationMode::Deep, false);
    const bool bHadManualIssues = InitialValidationReport.HasManualIssues();
    const bool bDataUVBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::DataUV);
    const bool bRuntimeBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::RuntimeData);
    const bool bGeneratedMaterialsBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::GeneratedMaterials);
    const bool bGPUMapsBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::GPUSimulationMaps);
    const bool bRenderProfileBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::RenderProfileData);
    const bool bWrinkleMapsBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::WrinkleMaps);
    const bool bTransparencyMapsBlockedByManual = IsValidationSectionBlockedByManualIssue(
        InitialValidationReport,
        EWCAValidationSection::TransparencyMaps);
#if WITH_EDITORONLY_DATA
    const FDWCAssetBakeState InitialBakeState = Asset->GetBakeState();
    const bool bInitialGPUMapsRequireBake =
        Setup.bBuildGPUWetnessMapSimulationData &&
        IsValidationActionRequiredStatus(InitialBakeState.GPUMaps);
    const bool bInitialWrinkleMapsRequireBake =
        InitialValidationReport.Issues.ContainsByPredicate(
            [](const FWCAValidationIssue& Issue)
            {
                return Issue.FixKind == EWCAValidationFixKind::BakeWrinkleMaps;
            });
    const bool bInitialTransparencyMapsRequireBake =
        InitialValidationReport.Issues.ContainsByPredicate(
            [](const FWCAValidationIssue& Issue)
            {
                return Issue.FixKind == EWCAValidationFixKind::BakeTransparencyMaps;
            });
#endif
    bool bPreparedRuntimePrerequisites = false;
    TArray<FString> ResolveSummaries;

    if (Status.bGeneratedDataUVIssue && !bDataUVBlockedByManual)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesGenerateDataUVProgress", "Initializing DWC UV Channel for an asset that has no sealed layout..."));
        const FDWCDataUVBuildResult DataUVResult =
            GenerateDWCDataUVWithVisibleExclusionConfirmation(
                *Asset,
                false,
                false,
                true,
                CollectWettableMaterialSlotIndices(*Asset));
        if (!DataUVResult.bSucceeded)
        {
            Asset->SetLastBakeFailure(DataUVResult.Message);
            OutFailure = DataUVResult.Message;
            return false;
        }
        ResolveSummaries.Add(DataUVResult.Message.IsEmpty()
            ? TEXT("Initialized and sealed DWC UV Channel.")
            : FString::Printf(TEXT("DWC UV Channel: %s"), *DataUVResult.Message));
    }

    const bool bRuntimeBackendEnabled =
        Setup.bBuildCPUVertexSimulationData ||
        Setup.bBuildGPUWetnessMapSimulationData;
    const bool bRuntimePreparationRequired =
        bRuntimeBackendEnabled &&
        !bRuntimeBlockedByManual &&
        (Status.bGeneratedDataUVIssue || Status.bRuntimeIssue);
    if (bRuntimePreparationRequired)
    {
        // Runtime structures are explicit generated data now. Build them before dependent map bakes,
        // then let the final save persist all pending runtime/map payloads once.
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesRuntimeDataProgress", "Preparing prerequisite runtime data before dependent generated assets..."));
        FString RuntimeDataError;
        if (!Asset->PrepareRuntimeDataForEditorSave(&RuntimeDataError))
        {
            OutFailure = RuntimeDataError.IsEmpty()
                             ? TEXT("Runtime Data: prerequisite runtime data rebuild failed.")
                             : RuntimeDataError;
            return false;
        }
        bPreparedRuntimePrerequisites = true;
        ResolveSummaries.Add(TEXT("Prepared runtime simulation data."));
    }

    Asset->RefreshBakeState(true);

    TArray<FString> GeneratedMaterialMessages;
    if (Asset->HasAnyWettableMaterialSlot())
    {
        FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(Asset, GeneratedMaterialMessages);
    }
    if (!GeneratedMaterialMessages.IsEmpty() && !bGeneratedMaterialsBlockedByManual)
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesGeneratedMaterialsProgress", "Generating DWC wet material overrides needed by preview and rendering outputs..."));
        FString MaterialSummary;
        FString MaterialFailure;
        if (!ResolveGeneratedWetMaterialsForAsset(*Asset, MaterialSummary, MaterialFailure))
        {
            OutFailure = MaterialFailure.IsEmpty()
                             ? TEXT("Generated Materials: material generation failed.")
                             : MaterialFailure;
            return false;
        }
        ResolveSummaries.Add(MaterialSummary);
        Asset->RefreshBakeState(true);
    }

#if WITH_EDITORONLY_DATA
    if (!bGPUMapsBlockedByManual)
    {
        if (Setup.bBuildGPUWetnessMapSimulationData &&
            !DWCBuildStatus::IsUsable(Asset->GetBakeState().GPURuntimeData))
        {
            OutFailure = Asset->GetBakeState().LastFailure.IsEmpty()
                             ? TEXT("GPU Runtime Data is missing or out of date after preparing.")
                             : FString::Printf(TEXT("GPU Runtime Data: %s"), *Asset->GetBakeState().LastFailure);
            return false;
        }

        if (Setup.bBuildGPUWetnessMapSimulationData &&
            (bInitialGPUMapsRequireBake ||
             !DWCBuildStatus::IsUsable(Asset->GetBakeState().GPUMaps)))
        {
            SlowTask.EnterProgressFrame(
                1.0f,
                LOCTEXT("ResolveIssuesGPUMapsProgress", "Building the GPU Runtime Data simulation lookup from the prepared runtime signature..."));
            FString GPUMapError;
            if (!Asset->BakeGPUWetnessMaps(&GPUMapError))
            {
                OutFailure = FString::Printf(TEXT("GPU Runtime Data: %s"), *GPUMapError);
                return false;
            }
            ResolveSummaries.Add(TEXT("Built GPU Runtime Data simulation lookup."));
            Asset->RefreshBakeState(true);
        }
    }

    FString VisualSummary;
    if (!bRenderProfileBlockedByManual &&
        EditorPanel->HasPendingVisualBakeTasks(&VisualSummary))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesVisualMapsProgress", "Baking wetness/transparency visual maps that are missing or stale..."));
        bool bVisualWarnings = false;
        if (!EditorPanel->BakePendingVisualAssets(VisualSummary, &bVisualWarnings))
        {
            OutFailure = FString::Printf(TEXT("Render Profile Lookup Texture: %s"), *VisualSummary);
            return false;
        }
        ResolveSummaries.Add(VisualSummary);
    }

    const bool bHasWrinkleContent = !Asset->Authored.WrinkleData.BakedWrinkleMaps.IsEmpty() ||
                                    !Asset->Authored.WrinkleData.EditablePatches.IsEmpty() ||
                                    !Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.IsEmpty();
    if (bHasWrinkleContent &&
        !bWrinkleMapsBlockedByManual &&
        (bInitialWrinkleMapsRequireBake ||
         !DWCBuildStatus::IsUsable(Asset->GetBakeState().WrinkleMaps)))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesWrinkleMapsProgress", "Baking wrinkle textures because stored outputs are missing or stale..."));
        FString WrinkleSummary;
        bool bWrinkleWarnings = false;
        if (!EditorPanel->BakeAllWrinkleMaps(WrinkleSummary, &bWrinkleWarnings))
        {
            OutFailure = FString::Printf(TEXT("Wrinkle Textures: %s"), *WrinkleSummary);
            return false;
        }
        ResolveSummaries.Add(WrinkleSummary);
    }

    if (Asset->HasTransparencyBakeContent() &&
        !bTransparencyMapsBlockedByManual &&
        (bInitialTransparencyMapsRequireBake ||
         !DWCBuildStatus::IsUsable(Asset->GetBakeState().TransparencyMaps)))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesTransparencyMapsProgress", "Generating and baking packed transparency textures for required layers..."));
        FString TransparencySummary;
        bool bTransparencyWarnings = false;
        if (!ResolveTransparencyMapsForAsset(*Asset, TransparencySummary, OutFailure, &bTransparencyWarnings))
        {
            if (OutFailure.IsEmpty())
            {
                OutFailure = TEXT("Transparency Textures: transparency texture bake failed.");
            }
            return false;
        }
        ResolveSummaries.Add(TransparencySummary);
        Asset->RefreshBakeState(true);
    }
#endif

    // Persist the rebuilt runtime structures and every explicit map-bake result in one final save.
    SlowTask.EnterProgressFrame(
        1.0f,
        bPreparedRuntimePrerequisites
            ? LOCTEXT("ResolveIssuesFinalSaveProgress", "Saving prepared runtime data and built rendering outputs...")
            : LOCTEXT("ResolveIssuesSaveProgress", "Saving resolved Wet Clothing Asset data..."));
    if (!DWCEditorUtils::SaveAsset(Asset))
    {
        OutFailure = TEXT("The asset or its generated data could not be saved.");
        return false;
    }
    ResolveSummaries.Add(TEXT("Saved the Wet Clothing Asset and generated assets."));

    Asset->RefreshBakeState(true);
    EditorPanel->RequestRefreshFromAsset();

#if WITH_EDITORONLY_DATA
    FString PostSaveVisualSummary;
    if (!bRenderProfileBlockedByManual &&
        EditorPanel->HasPendingVisualBakeTasks(&PostSaveVisualSummary))
    {
        SlowTask.EnterProgressFrame(
            1.0f,
            LOCTEXT("ResolveIssuesPostSaveRenderProfileProgress", "Building the Render Profile Lookup Texture after final Runtime Data save refresh..."));
        bool bPostSaveVisualWarnings = false;
        if (!EditorPanel->BakePendingVisualAssets(PostSaveVisualSummary, &bPostSaveVisualWarnings))
        {
            OutFailure = FString::Printf(TEXT("Render Profile Lookup Texture: %s"), *PostSaveVisualSummary);
            return false;
        }
        ResolveSummaries.Add(PostSaveVisualSummary);

        if (!DWCEditorUtils::SaveAsset(Asset))
        {
            OutFailure = TEXT("The Render Profile Lookup Texture was built, but the asset or generated textures could not be saved.");
            return false;
        }
        ResolveSummaries.Add(TEXT("Saved the Render Profile Lookup Texture after the Runtime Data refresh."));
        Asset->RefreshBakeState(true);
        EditorPanel->RequestRefreshFromAsset();
    }
#endif

    const FWCAValidationReport FinalValidationReport =
        BuildWCAValidationReport(*Asset, EWCAValidationMode::Deep, false);
    const FWCAValidationReport UnblockedFinalValidationReport =
        MakeUnblockedValidationReport(FinalValidationReport);
    if (UnblockedFinalValidationReport.HasIssues())
    {
        OutFailure = UnblockedFinalValidationReport.BuildSummary();
        return false;
    }
    if (bHadManualIssues || FinalValidationReport.HasManualIssues())
    {
        ResolveSummaries.Add(TEXT("Automatic issues were resolved. Manual Fix items still require user input."));
    }
    if (OutSuccessSummary != nullptr)
    {
        *OutSuccessSummary = ResolveSummaries.IsEmpty()
            ? TEXT("No rebuilds were required; the asset was saved.")
            : FString::Join(ResolveSummaries, TEXT("\n\n"));
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
