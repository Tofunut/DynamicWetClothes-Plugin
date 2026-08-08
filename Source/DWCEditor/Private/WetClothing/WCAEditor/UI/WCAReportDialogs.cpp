//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/UI/WCAReportDialogs.h"

#include "DataAssets/WetClothingAssetSetupData.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WCAReportDialogs"

namespace
{
    constexpr int32 ReportDialogFontSize = 10;

    FSlateFontInfo MakeReportFont(const int32 /*Size*/ = ReportDialogFontSize, const bool bBold = false)
    {
        return FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), ReportDialogFontSize);
    }

    FSlateColor WarningColor()
    {
        return FSlateColor(FLinearColor(1.0f, 0.78f, 0.18f, 1.0f));
    }

    FSlateColor ColoredStatusIconTint()
    {
        return FSlateColor(FLinearColor::White);
    }

    FSlateColor DataUVReadyColor()
    {
        return FSlateColor(FLinearColor(0.24f, 0.78f, 0.38f, 1.0f));
    }

    FSlateColor DataUVInfoColor()
    {
        return FSlateColor(FLinearColor(0.45f, 0.72f, 0.95f, 1.0f));
    }

    FLinearColor WarningBackground()
    {
        return FLinearColor(0.16f, 0.105f, 0.025f, 1.0f);
    }

    FSlateColor ErrorColor()
    {
        return FSlateColor(FStyleColors::Error);
    }

    FLinearColor ErrorBackground()
    {
        return FLinearColor(0.15f, 0.035f, 0.035f, 1.0f);
    }

    FLinearColor NeutralBackground()
    {
        return FLinearColor(0.045f, 0.048f, 0.052f, 1.0f);
    }

    FText BuildSlotLabel(const USkeletalMesh* Mesh, const int32 MaterialSlotIndex)
    {
        FString SlotName;
        if (Mesh != nullptr && Mesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            SlotName = Mesh->GetMaterials()[MaterialSlotIndex].MaterialSlotName.ToString();
        }

        return SlotName.IsEmpty()
            ? FText::FromString(FString::Printf(TEXT("Slot %d"), MaterialSlotIndex))
            : FText::FromString(FString::Printf(TEXT("Slot %d \u2014 %s"), MaterialSlotIndex, *SlotName));
    }

    const USkeletalMesh* ResolveSlotIdentityMesh(
        const UWetClothingAsset* Asset,
        const USkeletalMesh* FallbackMesh)
    {
        if (Asset != nullptr && Asset->GetSourceSkeletalMesh() != nullptr)
        {
            return Asset->GetSourceSkeletalMesh();
        }
        return FallbackMesh;
    }

    FText BuildAssetSlotLabel(
        const UWetClothingAsset* Asset,
        const USkeletalMesh* FallbackMesh,
        const int32 MaterialSlotIndex)
    {
        return BuildSlotLabel(ResolveSlotIdentityMesh(Asset, FallbackMesh), MaterialSlotIndex);
    }

    FText BuildLODListText(const TArray<int32>& LODIndices)
    {
        if (LODIndices.IsEmpty())
        {
            return LOCTEXT("NoLODs", "None");
        }

        TArray<int32> SortedLODIndices = LODIndices;
        SortedLODIndices.Sort();

        TArray<FString> Ranges;
        int32 RangeStart = SortedLODIndices[0];
        int32 RangeEnd = RangeStart;
        for (int32 Index = 1; Index < SortedLODIndices.Num(); ++Index)
        {
            const int32 LODIndex = SortedLODIndices[Index];
            if (LODIndex == RangeEnd + 1)
            {
                RangeEnd = LODIndex;
                continue;
            }

            Ranges.Add(RangeStart == RangeEnd
                ? FString::Printf(TEXT("LOD%d"), RangeStart)
                : FString::Printf(TEXT("LOD%d-LOD%d"), RangeStart, RangeEnd));
            RangeStart = LODIndex;
            RangeEnd = LODIndex;
        }

        Ranges.Add(RangeStart == RangeEnd
            ? FString::Printf(TEXT("LOD%d"), RangeStart)
            : FString::Printf(TEXT("LOD%d-LOD%d"), RangeStart, RangeEnd));
        return FText::FromString(FString::Join(Ranges, TEXT(", ")));
    }

    TArray<int32> BuildSkippedLODIndices(const FDWCDataUVBuildResult& Result)
    {
        TArray<int32> SkippedLODIndices;
        SkippedLODIndices.Reserve(Result.LODWarnings.Num());
        for (const FDWCDataUVLODWarning& Warning : Result.LODWarnings)
        {
            SkippedLODIndices.Add(Warning.LODIndex);
        }
        SkippedLODIndices.Sort();
        return SkippedLODIndices;
    }

    const FDWCDataUVLODWarning* FindLODWarning(
        const FDWCDataUVBuildResult& Result,
        const int32 LODIndex)
    {
        return Result.LODWarnings.FindByPredicate(
            [LODIndex](const FDWCDataUVLODWarning& Warning)
            {
                return Warning.LODIndex == LODIndex;
            });
    }

    FText BuildHeaderSummary(const FDWCDataUVBuildResult& Result)
    {
        if (!Result.FailedMaterialSlotIndices.IsEmpty())
        {
            return FText::Format(
                LOCTEXT(
                    "DWCDataUVPartialSlotGenerationSummary",
                    "DWC UV data was committed for {0} of {1} material slots in this build. Failed slots are listed below."),
                FText::AsNumber(Result.GeneratedMaterialSlotIndices.Num()),
                FText::AsNumber(Result.WettableMaterialSlotCount));
        }

        if (!Result.LODWarnings.IsEmpty())
        {
            const TArray<int32> SkippedLODIndices = BuildSkippedLODIndices(Result);
            return FText::Format(
                LOCTEXT(
                    "DWCDataUVPartialGenerationSummary",
                    "{0} of {1} LODs were generated successfully. {2} {3} skipped. The generated UV remains usable."),
                FText::AsNumber(Result.GeneratedLODIndices.Num()),
                FText::AsNumber(Result.TargetLODIndices.Num()),
                BuildLODListText(SkippedLODIndices),
                SkippedLODIndices.Num() == 1
                    ? LOCTEXT("DWCDataUVWasSkipped", "was")
                    : LOCTEXT("DWCDataUVWereSkipped", "were"));
        }

        if (DWCDataUVResultSeverity::Normalize(Result.ResultSeverity) == EDWCDataUVResultSeverity::Ready)
        {
            return FText::Format(
                LOCTEXT("DWCDataUVCleanGenerationSummary", "DWC UV data was generated successfully for all {0} target LODs."),
                FText::AsNumber(Result.TargetLODIndices.Num()));
        }

        return FText::Format(
            LOCTEXT("DWCDataUVDiagnosticGenerationSummary", "DWC UV data was generated for all {0} target LODs. See the diagnostics below."),
            FText::AsNumber(Result.TargetLODIndices.Num()));
    }

    TSharedRef<SWidget> BuildMetricPill(const FText& Label, const int32 Value)
    {
        return SNew(SBorder)
            .Padding(FMargin(8.0f, 4.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(FLinearColor(0.065f, 0.07f, 0.08f, 1.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .Font(MakeReportFont(10))
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::AsNumber(Value))
                    .Font(MakeReportFont(10, true))
                    .ColorAndOpacity(WarningColor())
                ]
            ];
    }

    TSharedRef<SWidget> BuildSummaryLine(const FText& Label, const FText& Value)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Label)
                .Font(MakeReportFont(10))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(Value)
                .Font(MakeReportFont(10, true))
            ];
    }

    TSharedRef<SWidget> BuildCompactStatusCount(
        const TCHAR* IconName,
        const FSlateColor& IconColor,
        const int32 Count,
        const FText& Tooltip)
    {
        return SNew(SHorizontalBox)
            .ToolTipText(Tooltip)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(14.0f)
                .HeightOverride(14.0f)
                [
                    SNew(SImage)
                    .Image(FAppStyle::GetBrush(IconName))
                    .ColorAndOpacity(IconColor)
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(3.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::AsNumber(Count))
                .Font(MakeReportFont(10, true))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
    }

    void AddMetricIfNonZero(
        const TSharedRef<SWrapBox>& Metrics,
        const FText& Label,
        const int32 Value)
    {
        if (Value <= 0)
        {
            return;
        }

        Metrics->AddSlot()
        .Padding(0.0f, 0.0f, 6.0f, 6.0f)
        [
            BuildMetricPill(Label, Value)
        ];
    }

    void AddBulletLine(
        const TSharedRef<SVerticalBox>& Lines,
        const FText& Text,
        const FSlateColor& Color = FSlateColor(FStyleColors::ForegroundHover))
    {
        Lines->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 3.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(FText::Format(LOCTEXT("ReportBulletFormat", "- {0}"), Text))
            .AutoWrapText(true)
            .Font(MakeReportFont())
            .ColorAndOpacity(Color)
        ];
    }

    TSharedRef<SWidget> BuildIssueSummarySection(const FDWCDataUVBuildResult& Result)
    {
        const bool bWarning = DWCDataUVResultSeverity::Normalize(Result.ResultSeverity) ==
            EDWCDataUVResultSeverity::ReadyWithWarnings;
        TSharedRef<SWrapBox> Metrics = SNew(SWrapBox).UseAllottedSize(true);
        AddMetricIfNonZero(Metrics, LOCTEXT("ExcludedTrianglesMetric", "Excluded triangles"), Result.ExcludedTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("Degenerate3DMetric", "3D Degenerate"), Result.Degenerate3DTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("DegenerateSourceUVMetric", "Degenerate Source UV"), Result.DegenerateSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("InvalidSourceUVMetric", "Invalid Source UV"), Result.InvalidSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("PackedDegenerateMetric", "Packed Degenerate"), Result.PackedDegenerateTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SplitOriginalUVIslandsMetric", "Split UV islands"), Result.SplitOriginalUVIslandCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("OverlapPairsMetric", "Overlap pairs"), Result.SelfOverlapPairCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("BudgetFallbackMetric", "Budget fallback islands"), Result.BudgetFallbackIslandCount);

        return SNew(SBorder)
            .Padding(FMargin(12.0f, 10.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(NeutralBackground())
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 7.0f, 0.0f)
                    [
                        SNew(SBox)
                        .WidthOverride(16.0f)
                        .HeightOverride(16.0f)
                        [
                            SNew(SImage)
                            .Image(FAppStyle::GetBrush(bWarning ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor")))
                            .ColorAndOpacity(ColoredStatusIconTint())
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(bWarning
                            ? LOCTEXT("DWCDataUVSourceUVWarningsHeading", "Warnings")
                            : LOCTEXT("DWCDataUVSourceUVDiagnosticsHeading", "Diagnostics"))
                        .Font(MakeReportFont(10, true))
                        .ColorAndOpacity(bWarning ? WarningColor() : DataUVReadyColor())
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                [
                    Metrics
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "DWCDataUVSourceIssuesCorrected",
                        "DWC handled the reported UV issues during generation."))
                    .AutoWrapText(true)
                    .Font(MakeReportFont())
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(bWarning
                        ? LOCTEXT("DWCDataUVSourceWarningsUsable", "The DWC UV was generated successfully, but some surface areas may not receive DWC data.")
                        : LOCTEXT("DWCDataUVSourceNotesUsable", "The generated DWC UV remains usable without expected coverage loss."))
                    .Font(MakeReportFont(10, true))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(
                        TEXT("Excluded surface: %.2f%%; largest excluded region: %.2f%%."),
                        Result.ExcludedVisible3DSurfaceRatio * 100.0,
                        Result.LargestConnectedExcluded3DSurfaceRatio * 100.0)))
                    .Font(MakeReportFont())
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                    .Visibility(Result.ExcludedVisibleTriangleCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
                ]
            ];
    }

    TSharedRef<SWidget> BuildSlotWarningCard(
        const FDWCDataUVSlotWarning& Diagnostic,
        const USkeletalMesh* PreparedMesh,
        const int32 LODIndex = INDEX_NONE,
        const bool bShowSlotLabel = true,
        const UWetClothingAsset* Asset = nullptr)
    {
        const bool bWarning = DWCDataUVResultSeverity::Normalize(Diagnostic.ResultSeverity) ==
            EDWCDataUVResultSeverity::ReadyWithWarnings;
        TSharedRef<SWrapBox> Metrics = SNew(SWrapBox).UseAllottedSize(true);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotDegenerate3DMetric", "3D Degenerate"), Diagnostic.Degenerate3DTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotDegenerateSourceUVMetric", "Degenerate Source UV"), Diagnostic.DegenerateSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotInvalidSourceUVMetric", "Invalid Source UV"), Diagnostic.InvalidSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotPackedDegenerateMetric", "Packed Degenerate"), Diagnostic.PackedDegenerateTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotSplitOriginalUVIslandsMetric", "Split islands"), Diagnostic.SplitOriginalUVIslandCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotOverlapPairsMetric", "Overlaps"), Diagnostic.SelfOverlapPairCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotBudgetFallbackMetric", "Budget fallback islands"), Diagnostic.BudgetFallbackIslandCount);

        TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
        if (Diagnostic.Degenerate3DTriangleCount > 0)
        {
            AddBulletLine(Lines, LOCTEXT("DWCDataUVExcluded3DDegenerate", "Zero-area 3D triangles were excluded without affecting visible coverage."));
        }
        if (Diagnostic.DegenerateSourceUVTriangleCount > 0)
        {
            AddBulletLine(Lines, LOCTEXT("DWCDataUVExcludedSourceDegenerate", "Degenerate source-UV triangles were excluded from DWC-derived data."));
        }
        if (Diagnostic.PackedDegenerateTriangleCount > 0)
        {
            AddBulletLine(Lines, LOCTEXT("DWCDataUVExcludedPackedDegenerate", "Near-degenerate packed triangles were excluded after final validation."));
        }
        if (Diagnostic.SplitOriginalUVIslandCount > 0 || Diagnostic.SelfOverlapPairCount > 0)
        {
            AddBulletLine(Lines, LOCTEXT("DWCDataUVSplitSelfOverlap", "Overlapping source UVs were automatically separated into valid packing charts."));
        }
        if (Diagnostic.BudgetFallbackIslandCount > 0)
        {
            AddBulletLine(Lines, LOCTEXT("DWCDataUVBudgetFallback", "Overlap analysis used a conservative safety fallback for some islands."));
        }
        if (Diagnostic.ExcludedVisibleTriangleCount > 0)
        {
            AddBulletLine(
                Lines,
                FText::FromString(Diagnostic.bVisibleExclusionSafetyLimitExceeded
                    ? FString::Printf(
                        TEXT("DWC UV was generated without %.2f%% of this material's surface (automatic limit %.2f%%). Largest excluded region: %.2f%%."),
                        Diagnostic.ExcludedVisible3DSurfaceRatio * 100.0,
                        DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0,
                        Diagnostic.LargestConnectedExcluded3DSurfaceRatio * 100.0)
                    : FString::Printf(
                        TEXT("Excluded surface: %.2f%%; largest excluded region: %.2f%%."),
                        Diagnostic.ExcludedVisible3DSurfaceRatio * 100.0,
                        Diagnostic.LargestConnectedExcluded3DSurfaceRatio * 100.0)));
        }

        FText HeadingText = bShowSlotLabel
            ? BuildAssetSlotLabel(Asset, PreparedMesh, Diagnostic.MaterialSlotIndex)
            : LODIndex != INDEX_NONE
                ? FText::FromString(FString::Printf(TEXT("LOD%d"), LODIndex))
                : FText::GetEmpty();

        return SNew(SBorder)
            .Padding(FMargin(12.0f, 9.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(bWarning ? FLinearColor(0.16f, 0.11f, 0.025f, 1.0f) : NeutralBackground())
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
                        .Text(HeadingText)
                        .Font(MakeReportFont(10, true))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(bWarning
                            ? LOCTEXT("DWCDataUVReadyWithWarningsLabel", "Ready with warnings")
                            : LOCTEXT("DWCDataUVReadyLabel", "Ready"))
                        .Font(MakeReportFont(10, true))
                        .ColorAndOpacity(bWarning ? WarningColor() : DataUVReadyColor())
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SBox)
                        .WidthOverride(14.0f)
                        .HeightOverride(14.0f)
                        [
                            SNew(SImage)
                            .Image(FAppStyle::GetBrush(bWarning ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor")))
                            .ColorAndOpacity(ColoredStatusIconTint())
                        ]
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 8.0f, 0.0f, 2.0f)
                [
                    Metrics
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    Lines
                ]
            ];
    }


    bool IsSlotIncludedInMetadata(const FDWCDataUVLODMetadata& Metadata, const int32 MaterialSlotIndex)
    {
        return Metadata.GeneratedMaterialSlotIndices.IsEmpty() ||
            Metadata.GeneratedMaterialSlotIndices.Contains(MaterialSlotIndex);
    }

    const FDWCDataUVSlotWarning* FindSlotDiagnostic(
        const FDWCDataUVLODMetadata& Metadata,
        const int32 MaterialSlotIndex)
    {
        return Metadata.SlotWarnings.FindByPredicate(
            [MaterialSlotIndex](const FDWCDataUVSlotWarning& Diagnostic)
            {
                return Diagnostic.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    TArray<int32> CollectMappedLODIndices(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh)
    {
        TArray<int32> LODIndices;
        const FSkeletalMeshRenderData* RenderData = PreparedMesh != nullptr
            ? PreparedMesh->GetResourceForRendering()
            : nullptr;
        const int32 LODCount = RenderData != nullptr ? RenderData->LODRenderData.Num() : 0;
        const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();
        if (LODCount > 0 && Setup.bBuildGPUWetnessMapSimulationData)
        {
            const int32 FirstLODIndex = FMath::Clamp(Setup.FirstGeneratedLODIndex, 0, LODCount - 1);
            const int32 LastLODIndex = FMath::Clamp(Setup.LastGeneratedLODIndex, FirstLODIndex, LODCount - 1);
            LODIndices.AddUnique(0);
            for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
            {
                LODIndices.AddUnique(LODIndex);
            }
            LODIndices.Sort();
        }
        else
        {
            for (const FDWCDataUVLODMetadata& Metadata : Asset.GetDataUVMetadata())
            {
                LODIndices.AddUnique(Metadata.LODIndex);
            }
            LODIndices.Sort();
        }
        return LODIndices;
    }

    TArray<int32> CollectRecordedSlotIndices(
        const UWetClothingAsset& Asset,
        const TSet<int32>& FailedMaterialSlotIndices)
    {
        TSet<int32> RecordedSlots = FailedMaterialSlotIndices;
#if WITH_EDITORONLY_DATA
        for (const FDWCDataUVSlotLODResult& Record : Asset.Derived.Inline.LastDataUVSlotLODResults)
        {
            if (Record.MaterialSlotIndex != INDEX_NONE)
            {
                RecordedSlots.Add(Record.MaterialSlotIndex);
            }
        }
#endif

        for (const FDWCDataUVLODMetadata& Metadata : Asset.GetDataUVMetadata())
        {
            if (Metadata.GeneratedMaterialSlotIndices.IsEmpty())
            {
                const USkeletalMesh* Mesh = Asset.GetSourceSkeletalMesh() != nullptr
                    ? Asset.GetSourceSkeletalMesh()
                    : Asset.GetRuntimeSkeletalMesh();
                if (Mesh != nullptr)
                {
                    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Mesh->GetMaterials().Num(); ++MaterialSlotIndex)
                    {
                        RecordedSlots.Add(MaterialSlotIndex);
                    }
                }
            }
            else
            {
                for (const int32 MaterialSlotIndex : Metadata.GeneratedMaterialSlotIndices)
                {
                    RecordedSlots.Add(MaterialSlotIndex);
                }
            }
        }

        TArray<int32> Result = RecordedSlots.Array();
        Result.Remove(INDEX_NONE);
        Result.Sort();
        return Result;
    }

    enum class EDataUVSlotLODStatus : uint8
    {
        Ready,
        ReadyWithWarnings,
        NotPresent,
        NotCommitted,
        OutOfDate,
        Failed,
        NotGenerated
    };

    struct FDataUVSlotLODDisplay
    {
        EDataUVSlotLODStatus Status = EDataUVSlotLODStatus::NotGenerated;
        FText StatusText;
        const TCHAR* IconName = TEXT("Icons.Minus");
        FSlateColor IconColor = FSlateColor(FStyleColors::ForegroundHover);
        FSlateColor TextColor = FSlateColor(FStyleColors::ForegroundHover);
        const FDWCDataUVSlotWarning* Warning = nullptr;
        bool bDiagnosticRecordAvailable = false;
    };

    const FDWCDataUVSlotLODResult* FindLastSlotLODResult(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex,
        const int32 LODIndex)
    {
#if WITH_EDITORONLY_DATA
        return Asset.Derived.Inline.LastDataUVSlotLODResults.FindByPredicate(
            [MaterialSlotIndex, LODIndex](const FDWCDataUVSlotLODResult& Record)
            {
                return Record.MaterialSlotIndex == MaterialSlotIndex && Record.LODIndex == LODIndex;
            });
#else
        return nullptr;
#endif
    }

    FDataUVSlotLODDisplay BuildSlotLODDisplay(
        const UWetClothingAsset& Asset,
        const int32 MaterialSlotIndex,
        const int32 LODIndex)
    {
        FDataUVSlotLODDisplay Display;
        const FDWCDataUVSlotLODResult* LastResult = FindLastSlotLODResult(
            Asset,
            MaterialSlotIndex,
            LODIndex);
        if (LastResult != nullptr && LastResult->State != EDWCDataUVSlotLODResultState::Ready)
        {
            switch (LastResult->State)
            {
            case EDWCDataUVSlotLODResultState::NotPresent:
                Display.Status = EDataUVSlotLODStatus::NotPresent;
                Display.StatusText = LOCTEXT("DWCDataUVDetailsNotPresent", "Not Used in This LOD");
                Display.IconName = TEXT("Icons.InfoWithColor");
                Display.IconColor = DataUVInfoColor();
                Display.TextColor = FSlateColor(FStyleColors::ForegroundHover);
                return Display;

            case EDWCDataUVSlotLODResultState::NotCommitted:
                Display.Status = EDataUVSlotLODStatus::NotCommitted;
                Display.StatusText = LOCTEXT("DWCDataUVDetailsNotCommitted", "Not Committed");
                Display.IconName = TEXT("Icons.Minus");
                Display.IconColor = FSlateColor(FStyleColors::ForegroundHover);
                Display.TextColor = FSlateColor(FStyleColors::ForegroundHover);
                return Display;

            case EDWCDataUVSlotLODResultState::Failed:
                Display.Status = EDataUVSlotLODStatus::Failed;
                Display.StatusText = LOCTEXT("DWCDataUVDetailsFailed", "Failed");
                Display.IconName = TEXT("Icons.ErrorWithColor");
                Display.IconColor = ColoredStatusIconTint();
                Display.TextColor = ErrorColor();
                return Display;

            case EDWCDataUVSlotLODResultState::NotGenerated:
            default:
                Display.Status = EDataUVSlotLODStatus::NotGenerated;
                Display.StatusText = LOCTEXT("DWCDataUVDetailsNotGenerated", "Not Generated");
                Display.IconName = TEXT("Icons.Minus");
                Display.IconColor = FSlateColor(FStyleColors::ForegroundHover);
                Display.TextColor = FSlateColor(FStyleColors::ForegroundHover);
                return Display;
            }
        }

        const FDWCDataUVLODMetadata* Metadata = Asset.FindDataUVMetadataForLOD(LODIndex);
        if (Metadata != nullptr && IsSlotIncludedInMetadata(*Metadata, MaterialSlotIndex))
        {
            Display.Warning = FindSlotDiagnostic(*Metadata, MaterialSlotIndex);
            Display.bDiagnosticRecordAvailable = Display.Warning != nullptr;
            if (Asset.HasValidDataUVForLOD(LODIndex))
            {
                if (!Display.bDiagnosticRecordAvailable)
                {
                    // Legacy or inconsistent metadata: the UV payload is usable, but a missing
                    // diagnostic record must never be presented as a measured clean 0.
                    Display.Status = EDataUVSlotLODStatus::Ready;
                    Display.StatusText = LOCTEXT(
                        "DWCDataUVDetailsReadyDiagnosticsUnavailable",
                        "Ready (diagnostics unavailable)");
                    Display.IconName = TEXT("Icons.InfoWithColor");
                    Display.IconColor = DataUVInfoColor();
                    Display.TextColor = DataUVReadyColor();
                }
                else
                {
                    const EDWCDataUVResultSeverity Severity =
                        DWCDataUVResultSeverity::Normalize(Display.Warning->ResultSeverity);
                    if (Severity == EDWCDataUVResultSeverity::ReadyWithWarnings)
                    {
                        Display.Status = EDataUVSlotLODStatus::ReadyWithWarnings;
                        Display.StatusText = LOCTEXT(
                            "DWCDataUVDetailsReadyWithWarnings",
                            "Ready with warnings");
                        Display.IconName = TEXT("Icons.WarningWithColor");
                        Display.IconColor = ColoredStatusIconTint();
                        Display.TextColor = WarningColor();
                    }
                    else
                    {
                        Display.Status = EDataUVSlotLODStatus::Ready;
                        Display.StatusText = LOCTEXT("DWCDataUVDetailsReady", "Ready");
                        Display.IconName = TEXT("Icons.SuccessWithColor");
                        Display.IconColor = ColoredStatusIconTint();
                        Display.TextColor = DataUVReadyColor();
                    }
                }
            }
            else
            {
                Display.Status = EDataUVSlotLODStatus::OutOfDate;
                Display.StatusText = LOCTEXT("DWCDataUVDetailsOutOfDate", "Out of Date");
                Display.IconName = TEXT("Icons.WarningWithColor");
                Display.IconColor = ColoredStatusIconTint();
                Display.TextColor = WarningColor();
            }
            return Display;
        }

        // Do not infer a failed LOD from the slot-level failure flag. A LOD is shown as
        // Failed only when the build service persisted that exact slot/LOD result.
        Display.Status = EDataUVSlotLODStatus::NotGenerated;
        Display.StatusText = LOCTEXT("DWCDataUVDetailsNotGeneratedLegacy", "Not Generated");
        Display.IconName = TEXT("Icons.Minus");
        Display.IconColor = FSlateColor(FStyleColors::ForegroundHover);
        Display.TextColor = FSlateColor(FStyleColors::ForegroundHover);
        return Display;
    }

    FDataUVSlotLODDisplay BuildSlotOverallDisplay(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const int32 MaterialSlotIndex,
        const TSet<int32>& FailedMaterialSlotIndices)
    {
        const TArray<int32> LODIndices = CollectMappedLODIndices(Asset, PreparedMesh);
        const bool bSlotFailed = FailedMaterialSlotIndices.Contains(MaterialSlotIndex);
        bool bHasReady = false;
        bool bHasWarnings = false;
        bool bHasNotPresent = false;
        bool bHasOutOfDate = false;
        bool bHasMissing = false;
        bool bHasDiagnosticsUnavailable = false;

        for (const int32 LODIndex : LODIndices)
        {
            const FDataUVSlotLODDisplay Display = BuildSlotLODDisplay(
                Asset,
                MaterialSlotIndex,
                LODIndex);
            bHasReady |= Display.Status == EDataUVSlotLODStatus::Ready ||
                Display.Status == EDataUVSlotLODStatus::ReadyWithWarnings;
            bHasWarnings |= Display.Status == EDataUVSlotLODStatus::ReadyWithWarnings;
            bHasDiagnosticsUnavailable |=
                Display.Status == EDataUVSlotLODStatus::Ready && !Display.bDiagnosticRecordAvailable;
            bHasNotPresent |= Display.Status == EDataUVSlotLODStatus::NotPresent;
            bHasOutOfDate |= Display.Status == EDataUVSlotLODStatus::OutOfDate;
            bHasMissing |= Display.Status == EDataUVSlotLODStatus::NotGenerated ||
                Display.Status == EDataUVSlotLODStatus::NotCommitted;
        }

        FDataUVSlotLODDisplay Overall;
        Overall.Status = EDataUVSlotLODStatus::NotGenerated;
        Overall.StatusText = LOCTEXT("DWCDataUVDetailsOverallNotGenerated", "Not Generated");
        Overall.IconName = TEXT("Icons.Minus");
        Overall.IconColor = FSlateColor(FStyleColors::ForegroundHover);
        Overall.TextColor = FSlateColor(FStyleColors::ForegroundHover);

        if (bSlotFailed)
        {
            Overall.Status = EDataUVSlotLODStatus::Failed;
            Overall.StatusText = LOCTEXT("DWCDataUVDetailsOverallFailed", "Failed");
            Overall.IconName = TEXT("Icons.ErrorWithColor");
            Overall.IconColor = ColoredStatusIconTint();
            Overall.TextColor = ErrorColor();
        }
        else if (bHasOutOfDate)
        {
            Overall.Status = EDataUVSlotLODStatus::OutOfDate;
            Overall.StatusText = LOCTEXT("DWCDataUVDetailsOverallOutOfDate", "Out of Date");
            Overall.IconName = TEXT("Icons.WarningWithColor");
            Overall.IconColor = ColoredStatusIconTint();
            Overall.TextColor = WarningColor();
        }
        else if (bHasMissing)
        {
            Overall.Status = EDataUVSlotLODStatus::NotGenerated;
            Overall.StatusText = LOCTEXT("DWCDataUVDetailsOverallIncomplete", "Incomplete");
            Overall.IconName = TEXT("Icons.WarningWithColor");
            Overall.IconColor = ColoredStatusIconTint();
            Overall.TextColor = WarningColor();
        }
        else if (bHasWarnings)
        {
            Overall.Status = EDataUVSlotLODStatus::ReadyWithWarnings;
            Overall.StatusText = LOCTEXT(
                "DWCDataUVDetailsOverallReadyWarnings",
                "Ready with warnings");
            Overall.IconName = TEXT("Icons.WarningWithColor");
            Overall.IconColor = ColoredStatusIconTint();
            Overall.TextColor = WarningColor();
        }
        else if (bHasDiagnosticsUnavailable)
        {
            Overall.Status = EDataUVSlotLODStatus::Ready;
            Overall.StatusText = LOCTEXT(
                "DWCDataUVDetailsOverallDiagnosticsUnavailable",
                "Ready (diagnostics unavailable)");
            Overall.IconName = TEXT("Icons.InfoWithColor");
            Overall.IconColor = DataUVInfoColor();
            Overall.TextColor = DataUVReadyColor();
        }
        else if (bHasReady || bHasNotPresent)
        {
            Overall.Status = EDataUVSlotLODStatus::Ready;
            Overall.StatusText = LOCTEXT("DWCDataUVDetailsOverallReady", "Ready");
            Overall.IconName = TEXT("Icons.SuccessWithColor");
            Overall.IconColor = ColoredStatusIconTint();
            Overall.TextColor = DataUVReadyColor();
        }
        return Overall;
    }

    TSharedRef<SWidget> BuildSlotLODStatusCard(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const int32 MaterialSlotIndex,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage,
        const bool bShowSlotHeading = true)
    {
        const TArray<int32> LODIndices = CollectMappedLODIndices(Asset, PreparedMesh);
        const bool bSlotFailed = FailedMaterialSlotIndices.Contains(MaterialSlotIndex);
        int32 RecordedLODCount = 0;
        int32 DiagnosticExpectedLODCount = 0;
        int32 DiagnosticRecordLODCount = 0;
        int32 LatestRenderVertexCount = 0;
        int32 ExcludedTriangleCount = 0;
        int32 PackedDegenerateTriangleCount = 0;
        int32 ResolvedOverlapPairCount = 0;
        int32 SplitIslandCount = 0;
        double MaxExcludedSurfaceRatio = 0.0;
        double MaxConnectedExcludedSurfaceRatio = 0.0;
        for (const int32 LODIndex : LODIndices)
        {
            const FDWCDataUVLODMetadata* Metadata = Asset.FindDataUVMetadataForLOD(LODIndex);
            if (Metadata == nullptr || !IsSlotIncludedInMetadata(*Metadata, MaterialSlotIndex))
            {
                continue;
            }
            ++RecordedLODCount;
            LatestRenderVertexCount = Metadata->RenderVertexCount;

            const FDWCDataUVSlotLODResult* LastResult = FindLastSlotLODResult(
                Asset,
                MaterialSlotIndex,
                LODIndex);
            const bool bSlotPresentInLOD =
                LastResult == nullptr || LastResult->State != EDWCDataUVSlotLODResultState::NotPresent;
            if (!bSlotPresentInLOD)
            {
                continue;
            }

            ++DiagnosticExpectedLODCount;
            if (const FDWCDataUVSlotWarning* Warning = FindSlotDiagnostic(*Metadata, MaterialSlotIndex))
            {
                ++DiagnosticRecordLODCount;
                ExcludedTriangleCount += Warning->Degenerate3DTriangleCount +
                    Warning->DegenerateSourceUVTriangleCount +
                    Warning->InvalidSourceUVTriangleCount +
                    Warning->PackedDegenerateTriangleCount;
                PackedDegenerateTriangleCount += Warning->PackedDegenerateTriangleCount;
                ResolvedOverlapPairCount += Warning->SelfOverlapPairCount;
                SplitIslandCount += Warning->SplitOriginalUVIslandCount;
                MaxExcludedSurfaceRatio = FMath::Max(MaxExcludedSurfaceRatio, Warning->ExcludedVisible3DSurfaceRatio);
                MaxConnectedExcludedSurfaceRatio = FMath::Max(
                    MaxConnectedExcludedSurfaceRatio,
                    Warning->LargestConnectedExcluded3DSurfaceRatio);
            }
        }
        const bool bDiagnosticDataComplete =
            DiagnosticExpectedLODCount > 0 &&
            DiagnosticRecordLODCount == DiagnosticExpectedLODCount;
        const FText UnknownDiagnosticValue = LOCTEXT(
            "DWCDataUVDiagnosticValueUnavailable",
            "-");

        TSharedRef<SVerticalBox> StatusRows = SNew(SVerticalBox);
        for (const int32 LODIndex : LODIndices)
        {
            const FDataUVSlotLODDisplay Display = BuildSlotLODDisplay(
                Asset,
                MaterialSlotIndex,
                LODIndex);
            StatusRows->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), LODIndex)))
                    .Font(MakeReportFont(10, true))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(Display.StatusText)
                    .Font(MakeReportFont(10, Display.Status == EDataUVSlotLODStatus::Failed))
                    .ColorAndOpacity(Display.TextColor)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(14.0f)
                    .HeightOverride(14.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(Display.IconName))
                        .ColorAndOpacity(Display.IconColor)
                    ]
                ]
            ];
        }

        const FDataUVSlotLODDisplay OverallDisplay = BuildSlotOverallDisplay(
            Asset, PreparedMesh, MaterialSlotIndex, FailedMaterialSlotIndices);

        TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
        if (bShowSlotHeading)
        {
            Card->AddSlot()
            .AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(BuildAssetSlotLabel(&Asset, PreparedMesh, MaterialSlotIndex))
                    .Font(MakeReportFont(10, true))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(OverallDisplay.StatusText)
                    .Font(MakeReportFont(10, true))
                    .ColorAndOpacity(OverallDisplay.TextColor)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(14.0f)
                    .HeightOverride(14.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(OverallDisplay.IconName))
                        .ColorAndOpacity(OverallDisplay.IconColor)
                    ]
                ]
            ];
        }

        Card->AddSlot()
        .AutoHeight()
        .Padding(0.0f, bShowSlotHeading ? 14.0f : 0.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("DWCDataUVDetailsLODStatusHeading", "LOD Generation Status"))
            .Font(MakeReportFont(10, true))
        ];
        Card->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 6.0f, 0.0f, 0.0f)
        [
            SNew(SBorder)
            .Padding(FMargin(12.0f, 8.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(NeutralBackground())
            [
                StatusRows
            ]
        ];

        if (RecordedLODCount > 0)
        {
            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 14.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVDetailsSummaryHeading", "Generation Summary"))
                .Font(MakeReportFont(10, true))
            ];
            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(NeutralBackground())
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsRecordedLODs", "Recorded LODs"),
                            FText::AsNumber(RecordedLODCount))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsDiagnosticRecords", "Diagnostic Records"),
                            FText::FromString(FString::Printf(
                                TEXT("%d / %d"),
                                DiagnosticRecordLODCount,
                                DiagnosticExpectedLODCount)))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsRenderVertices", "Render Vertices"),
                            LatestRenderVertexCount > 0 ? FText::AsNumber(LatestRenderVertexCount) : FText::FromString(TEXT("-")))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsResolvedOverlaps", "Resolved Overlap Pairs"),
                            bDiagnosticDataComplete
                                ? FText::AsNumber(ResolvedOverlapPairCount)
                                : UnknownDiagnosticValue)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsSplitIslands", "Split Source Islands"),
                            bDiagnosticDataComplete
                                ? FText::AsNumber(SplitIslandCount)
                                : UnknownDiagnosticValue)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsExcludedTriangles", "Excluded Triangles"),
                            bDiagnosticDataComplete
                                ? FText::AsNumber(ExcludedTriangleCount)
                                : UnknownDiagnosticValue)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsPackedDegenerate", "Packed Degenerate"),
                            bDiagnosticDataComplete
                                ? FText::AsNumber(PackedDegenerateTriangleCount)
                                : UnknownDiagnosticValue)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsExcludedSurfaceRatio", "Excluded Surface Ratio"),
                            bDiagnosticDataComplete
                                ? FText::FromString(FString::Printf(TEXT("%.6f%%"), MaxExcludedSurfaceRatio * 100.0))
                                : UnknownDiagnosticValue)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("DWCDataUVDetailsLargestExcludedRegion", "Largest Excluded Region"),
                            bDiagnosticDataComplete
                                ? FText::FromString(FString::Printf(TEXT("%.6f%%"), MaxConnectedExcludedSurfaceRatio * 100.0))
                                : UnknownDiagnosticValue)
                    ]
                ]
            ];

            if (DiagnosticExpectedLODCount > 0 && !bDiagnosticDataComplete)
            {
                Card->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "DWCDataUVDiagnosticsNotRecordedNotice",
                        "Diagnostic data was not recorded for one or more generated LODs. Values shown as '-' are unknown, not zero."))
                    .AutoWrapText(true)
                    .Font(MakeReportFont())
                    .ColorAndOpacity(DataUVInfoColor())
                ];
            }

        }

        bool bAddedDiagnostics = false;
        bool bAddedWarnings = false;
        for (const int32 LODIndex : LODIndices)
        {
            const FDWCDataUVLODMetadata* Metadata = Asset.FindDataUVMetadataForLOD(LODIndex);
            const FDWCDataUVSlotWarning* Diagnostic = Metadata != nullptr
                ? FindSlotDiagnostic(*Metadata, MaterialSlotIndex)
                : nullptr;
            if (Diagnostic == nullptr || !Diagnostic->HasDiagnostics())
            {
                continue;
            }

            const bool bWarning = DWCDataUVResultSeverity::Normalize(Diagnostic->ResultSeverity) ==
                EDWCDataUVResultSeverity::ReadyWithWarnings;
            bool& bHeadingAdded = bWarning ? bAddedWarnings : bAddedDiagnostics;
            if (!bHeadingAdded)
            {
                Card->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 14.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(bWarning
                        ? LOCTEXT("DWCDataUVDetailsWarningsHeading", "Warnings")
                        : LOCTEXT("DWCDataUVDetailsDiagnosticsHeading", "Diagnostics"))
                    .Font(MakeReportFont(10, true))
                ];
                bHeadingAdded = true;
            }

            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                BuildSlotWarningCard(*Diagnostic, PreparedMesh, LODIndex, false, &Asset)
            ];
        }

        bool bAddedErrors = false;
#if WITH_EDITORONLY_DATA
        for (const FDWCDataUVSlotLODResult& Record : Asset.Derived.Inline.LastDataUVSlotLODResults)
        {
            if (Record.MaterialSlotIndex != MaterialSlotIndex ||
                Record.State != EDWCDataUVSlotLODResultState::Failed)
            {
                continue;
            }

            if (!bAddedErrors)
            {
                Card->AddSlot()
                .AutoHeight()
                .Padding(0.0f, 14.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("DWCDataUVDetailsFailureHeading", "Errors"))
                    .Font(MakeReportFont(10, true))
                ];
                bAddedErrors = true;
            }

            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 9.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(ErrorBackground())
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), Record.LODIndex)))
                            .Font(MakeReportFont(10, true))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("DWCDataUVDetailsFailedDiagnostic", "Failed"))
                            .Font(MakeReportFont(10, true))
                            .ColorAndOpacity(ErrorColor())
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f, 0.0f, 0.0f)
                        [
                            SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)
                            [
                                SNew(SImage)
                                .Image(FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor")))
                                .ColorAndOpacity(ColoredStatusIconTint())
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Record.Message.IsEmpty()
                            ? TEXT("DWC UV generation failed for this LOD.")
                            : Record.Message))
                        .AutoWrapText(true)
                        .Font(MakeReportFont())
                        .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                    ]
                ]
            ];
        }
#endif

        if (bSlotFailed && !bAddedErrors)
        {
            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 14.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVDetailsLegacyFailureHeading", "Errors"))
                .Font(MakeReportFont(10, true))
            ];
            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 9.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(ErrorBackground())
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT(
                        "DWCDataUVDetailsLegacyFailureMessage",
                        "DWC UV generation failed for this material slot. Check the error below and fix the reported source or validation issue before rebuilding."))
                    .AutoWrapText(true)
                    .Font(MakeReportFont())
                    .ColorAndOpacity(ErrorColor())
                ]
            ];
        }

        TSharedRef<SVerticalBox> TechnicalDetails = SNew(SVerticalBox);
        bool bHasTechnicalDetails = false;
        for (const int32 LODIndex : LODIndices)
        {
            const FDWCDataUVLODMetadata* Metadata = Asset.FindDataUVMetadataForLOD(LODIndex);
            if (Metadata == nullptr || !IsSlotIncludedInMetadata(*Metadata, MaterialSlotIndex))
            {
                continue;
            }
            bHasTechnicalDetails = true;
            TechnicalDetails->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(
                    TEXT("LOD%d | Generator %d | Vertices %d | Channel UV%d\nInput Signature: %s\nOutput Signature: %s"),
                    LODIndex,
                    Metadata->GeneratorVersion,
                    Metadata->RenderVertexCount,
                    Metadata->UVChannelIndex,
                    *Metadata->MeshInputSignature,
                    *Metadata->DataUVOutputSignature)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }
        if (bHasTechnicalDetails)
        {
            Card->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 14.0f, 0.0f, 0.0f)
            [
                SNew(SExpandableArea)
                .InitiallyCollapsed(true)
                .HeaderContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("DWCDataUVDetailsTechnicalHeading", "Technical Details"))
                    .Font(MakeReportFont(10, true))
                ]
                .BodyContent()
                [
                    TechnicalDetails
                ]
            ];
        }

        return SNew(SBorder)
            .Padding(FMargin(12.0f, 10.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(FLinearColor(0.035f, 0.038f, 0.043f, 1.0f))
            [
                Card
            ];
    }

    TSharedRef<SWidget> BuildSlotLODStatusExpandableArea(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const int32 MaterialSlotIndex,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage,
        const bool bInitiallyCollapsed)
    {
        const FDataUVSlotLODDisplay OverallDisplay = BuildSlotOverallDisplay(
            Asset, PreparedMesh, MaterialSlotIndex, FailedMaterialSlotIndices);

        return SNew(SExpandableArea)
            .InitiallyCollapsed(bInitiallyCollapsed)
            .HeaderContent()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(BuildAssetSlotLabel(&Asset, PreparedMesh, MaterialSlotIndex))
                    .Font(MakeReportFont(10, true))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(OverallDisplay.StatusText)
                    .Font(MakeReportFont(10, true))
                    .ColorAndOpacity(OverallDisplay.TextColor)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(14.0f)
                    .HeightOverride(14.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(OverallDisplay.IconName))
                        .ColorAndOpacity(OverallDisplay.IconColor)
                    ]
                ]
            ]
            .BodyContent()
            [
                SNew(SBox)
                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                [
                    BuildSlotLODStatusCard(
                        Asset,
                        PreparedMesh,
                        MaterialSlotIndex,
                        FailedMaterialSlotIndices,
                        LastFailureMessage,
                        false)
                ]
            ];
    }

    TSharedRef<SWidget> BuildOperationSlotSection(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage)
    {
        TArray<int32> SortedSlots = IncludedMaterialSlotIndices.Array();
        SortedSlots.Remove(INDEX_NONE);
        SortedSlots.Sort();

        TSharedRef<SVerticalBox> Content = SNew(SVerticalBox);
        Content->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("DWCDataUVOperationSlotsHeading", "Material Slot Results"))
            .Font(MakeReportFont(10, true))
        ];

        for (int32 SlotListIndex = 0; SlotListIndex < SortedSlots.Num(); ++SlotListIndex)
        {
            const int32 MaterialSlotIndex = SortedSlots[SlotListIndex];
            Content->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
            [
                BuildSlotLODStatusExpandableArea(
                    Asset,
                    PreparedMesh,
                    MaterialSlotIndex,
                    FailedMaterialSlotIndices,
                    FString(),
                    SlotListIndex > 0)
            ];
        }
        return Content;
    }

    TSharedRef<SWidget> BuildAllSlotsOverview(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const TArray<int32>& MaterialSlotIndices,
        const TSet<int32>& FailedMaterialSlotIndices)
    {
        const TArray<int32> LODIndices = CollectMappedLODIndices(Asset, PreparedMesh);
        TSharedRef<SVerticalBox> StatusRows = SNew(SVerticalBox);
        for (const int32 LODIndex : LODIndices)
        {
            int32 ReadyCount = 0;
            int32 WarningCount = 0;
            int32 DiagnosticsUnavailableCount = 0;
            int32 FailedCount = 0;
            int32 MissingCount = 0;
            int32 OutOfDateCount = 0;
            for (const int32 SlotIndex : MaterialSlotIndices)
            {
                const FDataUVSlotLODDisplay Display = BuildSlotLODDisplay(
                    Asset,
                    SlotIndex,
                    LODIndex);
                switch (Display.Status)
                {
                case EDataUVSlotLODStatus::Ready:
                    if (Display.bDiagnosticRecordAvailable)
                    {
                        ++ReadyCount;
                    }
                    else
                    {
                        ++DiagnosticsUnavailableCount;
                    }
                    break;
                case EDataUVSlotLODStatus::ReadyWithWarnings: ++WarningCount; break;
                case EDataUVSlotLODStatus::NotPresent: ++ReadyCount; break;
                case EDataUVSlotLODStatus::Failed: ++FailedCount; break;
                case EDataUVSlotLODStatus::OutOfDate: ++OutOfDateCount; break;
                case EDataUVSlotLODStatus::NotCommitted:
                case EDataUVSlotLODStatus::NotGenerated:
                default: ++MissingCount; break;
                }
            }

            FSlateColor TextColor = DataUVReadyColor();
            FText StatusText = LOCTEXT("DWCDataUVAllSlotsReady", "Ready");
            if (FailedCount > 0)
            {
                TextColor = ErrorColor();
                StatusText = LOCTEXT("DWCDataUVAllSlotsFailed", "Failed");
            }
            else if (OutOfDateCount > 0 || MissingCount > 0)
            {
                TextColor = WarningColor();
                StatusText = LOCTEXT("DWCDataUVAllSlotsIncomplete", "Incomplete");
            }
            else if (WarningCount > 0)
            {
                TextColor = WarningColor();
                StatusText = LOCTEXT(
                    "DWCDataUVAllSlotsReadyWithWarnings",
                    "Ready with warnings");
            }
            else if (DiagnosticsUnavailableCount > 0)
            {
                TextColor = DataUVInfoColor();
                StatusText = LOCTEXT(
                    "DWCDataUVAllSlotsDiagnosticsUnavailable",
                    "Ready (diagnostics unavailable)");
            }
            TSharedRef<SHorizontalBox> CompactCounts = SNew(SHorizontalBox);
            if (ReadyCount > 0)
            {
                CompactCounts->AddSlot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    BuildCompactStatusCount(TEXT("Icons.SuccessWithColor"), ColoredStatusIconTint(), ReadyCount,
                        LOCTEXT("DWCDataUVOverallReadyCountTooltip", "Ready slots"))
                ];
            }
            if (DiagnosticsUnavailableCount > 0)
            {
                CompactCounts->AddSlot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    BuildCompactStatusCount(TEXT("Icons.InfoWithColor"), ColoredStatusIconTint(), DiagnosticsUnavailableCount,
                        LOCTEXT("DWCDataUVOverallDiagnosticsUnavailableCountTooltip", "Ready slots with unavailable diagnostics"))
                ];
            }
            const int32 AttentionCount = WarningCount + OutOfDateCount + MissingCount;
            if (AttentionCount > 0)
            {
                CompactCounts->AddSlot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    BuildCompactStatusCount(TEXT("Icons.WarningWithColor"), ColoredStatusIconTint(), AttentionCount,
                        LOCTEXT("DWCDataUVOverallWarningCountTooltip", "Slots with warnings, missing data, or out-of-date data"))
                ];
            }
            if (FailedCount > 0)
            {
                CompactCounts->AddSlot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
                [
                    BuildCompactStatusCount(TEXT("Icons.ErrorWithColor"), ColoredStatusIconTint(), FailedCount,
                        LOCTEXT("DWCDataUVOverallFailedCountTooltip", "Failed slots"))
                ];
            }

            StatusRows->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 4.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), LODIndex)))
                    .Font(MakeReportFont(10, true))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(StatusText)
                    .Font(MakeReportFont(10, FailedCount > 0))
                    .ColorAndOpacity(TextColor)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    CompactCounts
                ]
            ];
        }

        const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock).Text(LOCTEXT("DWCDataUVOverallLODStatusHeading", "Overall LOD Status")).Font(MakeReportFont(10, true))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder).Padding(FMargin(12.0f, 8.0f)).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header"))).BorderBackgroundColor(NeutralBackground())
                [
                    StatusRows
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock).Text(LOCTEXT("DWCDataUVCommonSettingsHeading", "Generation Configuration")).Font(MakeReportFont(10, true))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder).Padding(FMargin(12.0f, 8.0f)).BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header"))).BorderBackgroundColor(NeutralBackground())
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [ BuildSummaryLine(LOCTEXT("DWCDataUVCommonSourceUV", "Original UV Channel"), FText::FromString(FString::Printf(TEXT("UV%d"), Asset.GetOriginalUVChannelIndex()))) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [ BuildSummaryLine(LOCTEXT("DWCDataUVCommonOutputUV", "DWC UV Channel"), Asset.GetDWCDataUVChannelIndex() != INDEX_NONE ? FText::FromString(FString::Printf(TEXT("UV%d"), Asset.GetDWCDataUVChannelIndex())) : FText::FromString(TEXT("-"))) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [ BuildSummaryLine(LOCTEXT("DWCDataUVCommonRange", "Active LOD Range"), FText::FromString(FString::Printf(TEXT("LOD%d-LOD%d"), Setup.FirstGeneratedLODIndex, Setup.LastGeneratedLODIndex))) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [ BuildSummaryLine(LOCTEXT("DWCDataUVCommonBackends", "Simulation Data"), FText::FromString(FString::Printf(TEXT("CPU %s · GPU %s"), Setup.bBuildCPUVertexSimulationData ? TEXT("ON") : TEXT("OFF"), Setup.bBuildGPUWetnessMapSimulationData ? TEXT("ON") : TEXT("OFF")))) ]
                ]
            ];
    }

    void OpenDetailsDialogInternal(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const TArray<int32>& MaterialSlotIndices,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage,
        const FText& WindowTitle,
        const FText& HeaderText,
        const bool bAllSlotsView)
    {
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        if (bAllSlotsView && !MaterialSlotIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 16.0f)
            [
                BuildAllSlotsOverview(Asset, PreparedMesh, MaterialSlotIndices, FailedMaterialSlotIndices)
            ];
        }
        if (bAllSlotsView && !MaterialSlotIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 8.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVMaterialSlotDetailsHeading", "Material Slot Details"))
                .Font(MakeReportFont(10, true))
            ];
        }

        for (int32 SlotListIndex = 0; SlotListIndex < MaterialSlotIndices.Num(); ++SlotListIndex)
        {
            const int32 MaterialSlotIndex = MaterialSlotIndices[SlotListIndex];
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 14.0f)
            [
                bAllSlotsView
                    ? BuildSlotLODStatusExpandableArea(
                        Asset,
                        PreparedMesh,
                        MaterialSlotIndex,
                        FailedMaterialSlotIndices,
                        FString(),
                        SlotListIndex > 0)
                    : BuildSlotLODStatusCard(
                        Asset,
                        PreparedMesh,
                        MaterialSlotIndex,
                        FailedMaterialSlotIndices,
                        FString())
            ];
        }

        if (MaterialSlotIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(18.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVDetailsNoRecordedSlots", "No generated or failed material-slot records were found."))
                .Justification(ETextJustify::Center)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
            .Title(WindowTitle)
            .ClientSize(FVector2D(650.0f, 620.0f))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(true)
            .SupportsMinimize(false);

        DialogWindow->SetContent(
            SNew(SBorder)
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
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.InfoWithColor")))
                        .ColorAndOpacity(DataUVInfoColor())
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(HeaderText)
                        .Font(MakeReportFont(10, true))
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
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
                        Body
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Right)
                .Padding(16.0f, 12.0f, 16.0f, 16.0f)
                [
                    SNew(SButton)
                    .ContentPadding(FMargin(14.0f, 5.0f))
                    .Text(LOCTEXT("DWCDataUVDetailsClose", "Close"))
                    .OnClicked_Lambda([DialogWindow]()
                    {
                        DialogWindow->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
    }

    TSharedRef<SWidget> BuildLODStatusSection(const FDWCDataUVBuildResult& Result)
    {
        TSharedRef<SVerticalBox> StatusRows = SNew(SVerticalBox);
        for (const int32 LODIndex : Result.TargetLODIndices)
        {
            const FDWCDataUVLODWarning* Warning = FindLODWarning(Result, LODIndex);
            const bool bSkipped = Warning != nullptr;
            const FText StatusText = bSkipped
                ? FText::Format(
                    LOCTEXT("DWCDataUVSkippedLODStatus", "Skipped - {0}"),
                    FText::FromString(Warning->Summary))
                : LOCTEXT("DWCDataUVReadyLODStatus", "Ready");

            StatusRows->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), LODIndex)))
                    .Font(MakeReportFont(10, true))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(StatusText)
                    .Font(MakeReportFont(10, bSkipped))
                    .ColorAndOpacity(bSkipped ? WarningColor() : DataUVReadyColor())
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(14.0f)
                    .HeightOverride(14.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(
                            bSkipped ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor")))
                    ]
                ]
            ];
        }

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVLODGenerationStatusHeading", "LOD Generation Status"))
                .Font(MakeReportFont(10, true))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(NeutralBackground())
                [
                    StatusRows
                ]
            ];
    }

    TSharedRef<SWidget> BuildDiagnosticsSection(const FDWCDataUVBuildResult& Result)
    {
        TSharedRef<SVerticalBox> Details = SNew(SVerticalBox);

        for (const FDWCDataUVLODWarning& Warning : Result.LODWarnings)
        {
            Details->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 9.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(WarningBackground())
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(FText::Format(
                            LOCTEXT("DWCDataUVLODTechnicalTitle", "LOD{0} - {1}"),
                            FText::AsNumber(Warning.LODIndex),
                            FText::FromString(Warning.Summary)))
                        .Font(MakeReportFont(10, true))
                        .ColorAndOpacity(WarningColor())
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Warning.TechnicalDetails))
                        .AutoWrapText(true)
                        .Font(MakeReportFont())
                        .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(FText::Format(
                            LOCTEXT("DWCDataUVLODNotGenerated", "Result: LOD{0} DWC UV data was not generated."),
                            FText::AsNumber(Warning.LODIndex)))
                        .Font(MakeReportFont(10, true))
                    ]
                ]
            ];
        }

        if (!Result.TimingSummary.IsEmpty())
        {
            Details->AddSlot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Result.TimingSummary))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        return SNew(SExpandableArea)
            .InitiallyCollapsed(true)
            .HeaderContent()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVDiagnosticsHeading", "Diagnostics"))
                .Font(MakeReportFont(10, true))
            ]
            .BodyContent()
            [
                Details
            ];
    }

    TSharedRef<SWidget> BuildFailureLODStatusSection(const FDWCDataUVBuildResult& Result)
    {
        TSharedRef<SVerticalBox> StatusRows = SNew(SVerticalBox);
        for (const int32 LODIndex : Result.TargetLODIndices)
        {
            const bool bReady = Result.GeneratedLODIndices.Contains(LODIndex);
            const bool bFailed = LODIndex == Result.FailureLODIndex;
            const TCHAR* IconName = bReady
                ? TEXT("Icons.SuccessWithColor")
                : bFailed
                    ? TEXT("Icons.ErrorWithColor")
                    : TEXT("Icons.Minus");
            const FText StatusText = bReady
                ? LOCTEXT("DWCDataUVFailureReadyStatus", "Ready")
                : bFailed
                    ? Result.ValidationFailure.bIsValid
                        ? LOCTEXT("DWCDataUVFailureFailedStatus", "Failed - Degenerate packed triangle")
                        : LOCTEXT("DWCDataUVFailureGenericFailedStatus", "Failed")
                    : LOCTEXT("DWCDataUVFailureNotGeneratedStatus", "Not Generated");

            StatusRows->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 3.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(16.0f)
                    .HeightOverride(16.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(IconName))
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), LODIndex)))
                    .Font(MakeReportFont(10, true))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .HAlign(HAlign_Right)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(StatusText)
                    .Font(MakeReportFont(10, bFailed))
                    .ColorAndOpacity(bFailed ? ErrorColor() : FSlateColor(FStyleColors::ForegroundHover))
                ]
            ];
        }

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVFailureLODStatusHeading", "LOD Build Status"))
                .Font(MakeReportFont(10, true))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(NeutralBackground())
                [
                    StatusRows
                ]
            ];
    }

    TSharedRef<SWidget> BuildFailureDetailsSection(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset* Asset,
        const USkeletalMesh* PreparedMesh)
    {
        const FDWCDataUVValidationFailure& Failure = Result.ValidationFailure;
        TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);

        if (Failure.bIsValid)
        {
            Lines->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildSummaryLine(
                    LOCTEXT("DWCDataUVFailureMaterialSlot", "Material Slot"),
                    BuildAssetSlotLabel(Asset, PreparedMesh, Failure.MaterialSlotIndex))
            ];
            Lines->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildSummaryLine(
                    LOCTEXT("DWCDataUVFailureTriangle", "Triangle"),
                    FText::AsNumber(Failure.MeshTriangleID))
            ];
            Lines->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildSummaryLine(
                    LOCTEXT("DWCDataUVFailureChart", "Chart"),
                    FText::AsNumber(Failure.ChartIndex))
            ];
            Lines->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildSummaryLine(
                    LOCTEXT("DWCDataUVFailureReason", "Reason"),
                    Failure.Reason.Contains(TEXT("area is below tolerance"))
                        ? LOCTEXT("DWCDataUVFailureZeroAreaReason", "Packed triangle has zero area")
                        : FText::FromString(Failure.Reason))
            ];
            Lines->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT(
                    "DWCDataUVFailureCollapsedTriangleExplanation",
                    "One triangle collapsed during UV packing, causing final validation to fail."))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }
        else
        {
            Lines->AddSlot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(Result.Message))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        Lines->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 8.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("DWCDataUVFailureNoResult", "No changes were made to the Prepared Mesh."))
            .Font(MakeReportFont(10, true))
            .ColorAndOpacity(ErrorColor())
        ];

        return SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVFailureDetailsHeading", "Failure Details"))
                .Font(MakeReportFont(10, true))
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 9.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(ErrorBackground())
                [
                    Lines
                ]
            ];
    }

    FString BuildFailureTechnicalText(const FDWCDataUVBuildResult& Result)
    {
        FString Details = Result.Message;
        const FDWCDataUVValidationFailure& Failure = Result.ValidationFailure;
        if (Failure.bIsValid)
        {
            Details += FString::Printf(
                TEXT("\n\nLOD: %d\nMaterial Slot: %d\nMesh Triangle ID: %d\nGenerator Triangle Index: %d\nChart: %d\nPacked Area: %.12g\nReason: %s\nPacked UVs: ((%.9g, %.9g), (%.9g, %.9g), (%.9g, %.9g))"),
                Result.FailureLODIndex,
                Failure.MaterialSlotIndex,
                Failure.MeshTriangleID,
                Failure.GeneratorTriangleIndex,
                Failure.ChartIndex,
                Failure.PackedArea,
                *Failure.Reason,
                Failure.PackedUVs[0].X,
                Failure.PackedUVs[0].Y,
                Failure.PackedUVs[1].X,
                Failure.PackedUVs[1].Y,
                Failure.PackedUVs[2].X,
                Failure.PackedUVs[2].Y);
        }
        return Details;
    }

}

namespace WCAReportDialogs
{
    void OpenDWCDataUVBuildResultDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset* Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices)
    {
        const bool bHasSkippedLODs = !Result.LODWarnings.IsEmpty();
        const bool bHasSourceIssues =
            Result.ExcludedTriangleCount > 0 ||
            Result.Degenerate3DTriangleCount > 0 ||
            Result.DegenerateSourceUVTriangleCount > 0 ||
            Result.InvalidSourceUVTriangleCount > 0 ||
            Result.PackedDegenerateTriangleCount > 0 ||
            Result.SplitOriginalUVIslandCount > 0 ||
            Result.SelfOverlapPairCount > 0 ||
            Result.BudgetFallbackIslandCount > 0;

        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        const bool bUsesMaterialSlotCards = Asset != nullptr && !IncludedMaterialSlotIndices.IsEmpty();

        if (bUsesMaterialSlotCards)
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildOperationSlotSection(
                    *Asset,
                    PreparedMesh,
                    IncludedMaterialSlotIndices,
                    Result.FailedMaterialSlotIndices,
                    Result.Message)
            ];
        }
        else if (!Result.TargetLODIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildLODStatusSection(Result)
            ];
        }

        if (!bUsesMaterialSlotCards && bHasSourceIssues)
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildIssueSummarySection(Result)
            ];
        }

        TArray<FDWCDataUVSlotWarning> SlotWarnings = Result.SlotWarnings;
        if (!IncludedMaterialSlotIndices.IsEmpty())
        {
            SlotWarnings.RemoveAll(
                [&IncludedMaterialSlotIndices](const FDWCDataUVSlotWarning& Warning)
                {
                    return !IncludedMaterialSlotIndices.Contains(Warning.MaterialSlotIndex);
                });
        }
        SlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            });

        if (!bUsesMaterialSlotCards)
        {
            for (const EDWCDataUVResultSeverity SectionSeverity :
                { EDWCDataUVResultSeverity::Ready, EDWCDataUVResultSeverity::ReadyWithWarnings })
            {
                const bool bHasSection = SlotWarnings.ContainsByPredicate(
                    [SectionSeverity](const FDWCDataUVSlotWarning& Diagnostic)
                    {
                        return DWCDataUVResultSeverity::Normalize(Diagnostic.ResultSeverity) == SectionSeverity &&
                            Diagnostic.HasDiagnostics();
                    });
                if (!bHasSection)
                {
                    continue;
                }

                Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 6.0f)
                [
                    SNew(STextBlock)
                    .Text(SectionSeverity == EDWCDataUVResultSeverity::ReadyWithWarnings
                        ? LOCTEXT("DWCDataUVSlotWarningsHeading", "Warnings")
                        : LOCTEXT("DWCDataUVSlotDiagnosticsHeading", "Diagnostics"))
                    .Font(MakeReportFont(10, true))
                ];

                for (const FDWCDataUVSlotWarning& Diagnostic : SlotWarnings)
                {
                    if (DWCDataUVResultSeverity::Normalize(Diagnostic.ResultSeverity) != SectionSeverity ||
                        !Diagnostic.HasDiagnostics())
                    {
                        continue;
                    }
                    Body->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                    [ BuildSlotWarningCard(Diagnostic, PreparedMesh, INDEX_NONE, true, Asset) ];
                }
            }
        }

        if (!Result.LODWarnings.IsEmpty() || !Result.TimingSummary.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 0.0f)
            [
                BuildDiagnosticsSection(Result)
            ];
        }

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
            .Title(LOCTEXT("DWCDataUVGeneratedTitle", "DWC UV Channel Generated"))
            .ClientSize(FVector2D(760.0f, 620.0f))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(true)
            .SupportsMinimize(false);

        DialogWindow->SetContent(
            SNew(SBorder)
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
                        .Image(FAppStyle::GetBrush(
                            DWCDataUVResultSeverity::Normalize(Result.ResultSeverity) ==
                                    EDWCDataUVResultSeverity::ReadyWithWarnings ||
                                bHasSkippedLODs
                                ? TEXT("Icons.WarningWithColor")
                                : TEXT("Icons.SuccessWithColor")))
                        .ColorAndOpacity(ColoredStatusIconTint())
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(DWCDataUVResultSeverity::Normalize(Result.ResultSeverity) ==
                                        EDWCDataUVResultSeverity::ReadyWithWarnings ||
                                    bHasSkippedLODs
                                ? LOCTEXT("DWCDataUVWarningsHeader", "DWC UV Channel Generated With Warnings")
                                : LOCTEXT("DWCDataUVSuccessHeader", "DWC UV Channel Generated Successfully"))
                            .Font(MakeReportFont(10, true))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(BuildHeaderSummary(Result))
                            .AutoWrapText(true)
                            .Font(MakeReportFont())
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
                        Body
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Right)
                .Padding(16.0f, 12.0f, 16.0f, 16.0f)
                [
                    SNew(SButton)
                    .ContentPadding(FMargin(14.0f, 5.0f))
                    .Text(LOCTEXT("DWCDataUVResultOK", "OK"))
                    .OnClicked_Lambda([DialogWindow]()
                    {
                        DialogWindow->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
    }

    FDWCDataUVVisibleExclusionDecision ConfirmDWCDataUVVisibleExclusion(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh* SlotIdentityMesh)
    {
        FDWCDataUVVisibleExclusionDecision Decision;

        TArray<const FDWCDataUVSlotWarning*> AffectedWarnings;
        for (const FDWCDataUVSlotWarning& Warning : Result.SlotWarnings)
        {
            // The build service is the source of truth for which slots are waiting for
            // confirmation. Do not re-filter by the caller's original target set: merge
            // builds may also need to refresh an existing DWC UV slot.
            if (Warning.bVisibleExclusionSafetyLimitExceeded &&
                Result.ConfirmationRequiredMaterialSlotIndices.Contains(Warning.MaterialSlotIndex))
            {
                AffectedWarnings.Add(&Warning);
            }
        }
        AffectedWarnings.Sort([](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
        {
            return A.MaterialSlotIndex < B.MaterialSlotIndex;
        });

        // A pending-confirmation result without a matching diagnostic is an internal
        // state mismatch, not a user cancellation. Surface it explicitly so callers do
        // not silently hide missing diagnostics or convert an unknown state into Ready.
        if (AffectedWarnings.IsEmpty())
        {
            TArray<int32> PendingSlots = Result.ConfirmationRequiredMaterialSlotIndices.Array();
            PendingSlots.Sort();
            TArray<FString> PendingSlotLabels;
            for (const int32 MaterialSlotIndex : PendingSlots)
            {
                PendingSlotLabels.Add(FString::FromInt(MaterialSlotIndex));
            }

            Decision.bInternalError = true;
            Decision.ErrorMessage = FString::Printf(
                TEXT("Internal DWC UV diagnostic mismatch: confirmation was requested for material slot(s) [%s], but no matching visible-exclusion diagnostic was found."),
                *FString::Join(PendingSlotLabels, TEXT(", ")));
            return Decision;
        }

        enum class ESlotDecision : uint8
        {
            None,
            GenerateWithoutAreas,
            Skip,
            Cancel
        };

        for (const FDWCDataUVSlotWarning* Warning : AffectedWarnings)
        {
            if (Warning == nullptr)
            {
                continue;
            }

            ESlotDecision SlotDecision = ESlotDecision::None;
            TSharedRef<SWindow> DialogWindow =
                SNew(SWindow)
                .Title(FText::Format(
                    LOCTEXT("DWCDataUVSlotExclusionConfirmationTitle", "DWC UV Generation Warning - {0}"),
                    BuildSlotLabel(SlotIdentityMesh, Warning->MaterialSlotIndex)))
                .ClientSize(FVector2D(680.0f, 470.0f))
                .SizingRule(ESizingRule::UserSized)
                .SupportsMaximize(false)
                .SupportsMinimize(false);

            DialogWindow->SetContent(
                SNew(SBorder)
                .Padding(FMargin(16.0f, 14.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Top)
                        .Padding(0.0f, 2.0f, 10.0f, 0.0f)
                        [
                            SNew(SImage)
                            .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                        ]
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(BuildSlotLabel(SlotIdentityMesh, Warning->MaterialSlotIndex))
                                .Font(MakeReportFont(10, true))
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT(
                                    "DWCDataUVSlotExclusionConfirmationHeader",
                                    "Some triangles cannot be included in the DWC UV."))
                                .Font(MakeReportFont())
                                .ColorAndOpacity(WarningColor())
                                .AutoWrapText(true)
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(
                                    TEXT("They cover %.2f%% of this material's surface, which exceeds the automatic limit of %.2f%%."),
                                    Warning->ExcludedVisible3DSurfaceRatio * 100.0,
                                    DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0)))
                                .AutoWrapText(true)
                                .Font(MakeReportFont())
                                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 14.0f, 0.0f, 0.0f)
                    [
                        SNew(SBorder)
                        .Padding(FMargin(10.0f, 8.0f))
                        .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
                        .BorderBackgroundColor(WarningBackground())
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(
                                    TEXT("Excluded surface: %.2f%%"),
                                    Warning->ExcludedVisible3DSurfaceRatio * 100.0)))
                                .Font(MakeReportFont())
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(
                                    TEXT("Automatic limit: %.2f%%"),
                                    DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0)))
                                .Font(MakeReportFont())
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(
                                    TEXT("Largest excluded region: %.2f%%"),
                                    Warning->LargestConnectedExcluded3DSurfaceRatio * 100.0)))
                                .Font(MakeReportFont())
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(
                                    TEXT("Excluded triangles: %d"),
                                    Warning->ExcludedVisibleTriangleCount)))
                                .Font(MakeReportFont())
                            ]
                            + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 9.0f, 0.0f, 0.0f)
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT(
                                    "DWCDataUVSlotExclusionConfirmationResult",
                                    "DWC can still generate the UV by excluding these triangles. The excluded areas will not participate in DWC simulation or texture-based data."))
                                .AutoWrapText(true)
                                .Font(MakeReportFont())
                                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot()
                    .FillHeight(1.0f)
                    [
                        SNew(SSpacer)
                    ]
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .FillWidth(1.0f)
                        [ SNew(SSpacer) ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("DWCDataUVExclusionCancelBuild", "Cancel Build"))
                            .OnClicked_Lambda([DialogWindow, &SlotDecision]()
                            {
                                SlotDecision = ESlotDecision::Cancel;
                                DialogWindow->RequestDestroyWindow();
                                return FReply::Handled();
                            })
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                        [
                            SNew(SButton)
                            .Text(LOCTEXT("DWCDataUVExclusionSkipThisSlot", "Skip This Slot"))
                            .OnClicked_Lambda([DialogWindow, &SlotDecision]()
                            {
                                SlotDecision = ESlotDecision::Skip;
                                DialogWindow->RequestDestroyWindow();
                                return FReply::Handled();
                            })
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        [
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
                            .Text(LOCTEXT("DWCDataUVExclusionGenerateWithoutAreas", "Generate Without These Areas"))
                            .OnClicked_Lambda([DialogWindow, &SlotDecision]()
                            {
                                SlotDecision = ESlotDecision::GenerateWithoutAreas;
                                DialogWindow->RequestDestroyWindow();
                                return FReply::Handled();
                            })
                        ]
                    ]
                ]);

            FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);

            switch (SlotDecision)
            {
            case ESlotDecision::GenerateWithoutAreas:
                Decision.AcceptedMaterialSlotIndices.Add(Warning->MaterialSlotIndex);
                break;
            case ESlotDecision::Skip:
                Decision.SkippedMaterialSlotIndices.Add(Warning->MaterialSlotIndex);
                break;
            case ESlotDecision::Cancel:
            case ESlotDecision::None: // Closing the window is equivalent to cancelling the build.
                Decision.bCancelBuild = true;
                break;
            }

            // Resolve exactly one material slot per modal interaction. The caller rebuilds
            // with that single decision, then asks again for the next still-pending slot.
            // This makes multi-slot builds deterministic and guarantees one warning window
            // per affected slot instead of consuming a batch behind a single build pass.
            return Decision;
        }

        return Decision;
    }

    void OpenDWCDataUVBuildFailureDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset* Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices)
    {
        const FString TechnicalDetails = BuildFailureTechnicalText(Result);
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

        if (Asset != nullptr && !IncludedMaterialSlotIndices.IsEmpty())
        {
            TSet<int32> FailedSlots;
            for (const int32 MaterialSlotIndex : Result.FailedMaterialSlotIndices)
            {
                if (IncludedMaterialSlotIndices.Contains(MaterialSlotIndex))
                {
                    FailedSlots.Add(MaterialSlotIndex);
                }
            }
            if (FailedSlots.IsEmpty())
            {
                FailedSlots = IncludedMaterialSlotIndices;
            }
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildOperationSlotSection(
                    *Asset,
                    PreparedMesh,
                    IncludedMaterialSlotIndices,
                    FailedSlots,
                    Result.Message)
            ];
        }

        if (!Result.TargetLODIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildFailureLODStatusSection(Result)
            ];
        }

        Body->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 10.0f)
        [
            BuildFailureDetailsSection(Result, Asset, PreparedMesh)
        ];

        Body->AddSlot()
        .AutoHeight()
        [
            SNew(SExpandableArea)
            .InitiallyCollapsed(true)
            .HeaderContent()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVFailureDiagnosticsHeading", "Technical Details"))
                .Font(MakeReportFont(10, true))
            ]
            .BodyContent()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TechnicalDetails))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ]
        ];

        TSharedRef<SWindow> DialogWindow =
            SNew(SWindow)
            .Title(LOCTEXT("DWCDataUVFailureTitle", "DWC UV Generation Failed"))
            .ClientSize(FVector2D(720.0f, 520.0f))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(true)
            .SupportsMinimize(false);

        DialogWindow->SetContent(
            SNew(SBorder)
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
                        .Image(FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor")))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("DWCDataUVFailureHeader", "DWC UV Generation Failed"))
                            .Font(MakeReportFont(10, true))
                            .ColorAndOpacity(ErrorColor())
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(Result.FailureLODIndex == 0 && Result.ValidationFailure.bIsValid
                                ? LOCTEXT(
                                    "DWCDataUVFailureLOD0Summary",
                                    "LOD0 failed final validation, so no usable DWC UV channel was generated.")
                                : Result.FailureLODIndex == 0
                                    ? LOCTEXT(
                                        "DWCDataUVFailureLOD0GenericSummary",
                                        "LOD0 generation failed, so no usable DWC UV channel was generated.")
                                    : LOCTEXT(
                                        "DWCDataUVFailureGenericSummary",
                                        "DWC UV generation could not be completed. No changes were made to the Prepared Mesh. See Failure Details for the specific cause."))
                            .AutoWrapText(true)
                            .Font(MakeReportFont())
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
                        Body
                    ]
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Right)
                .Padding(16.0f, 12.0f, 16.0f, 16.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                    [
                        SNew(SButton)
                        .ContentPadding(FMargin(12.0f, 5.0f))
                        .Text(LOCTEXT("DWCDataUVFailureCopyDetails", "Copy Details"))
                        .OnClicked_Lambda([TechnicalDetails]()
                        {
                            FPlatformApplicationMisc::ClipboardCopy(*TechnicalDetails);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SButton)
                        .ContentPadding(FMargin(14.0f, 5.0f))
                        .Text(LOCTEXT("DWCDataUVFailureOK", "OK"))
                        .OnClicked_Lambda([DialogWindow]()
                        {
                            DialogWindow->RequestDestroyWindow();
                            return FReply::Handled();
                        })
                    ]
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
    }
    void OpenDWCDataUVSlotDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const int32 MaterialSlotIndex,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage)
    {
        TArray<int32> MaterialSlotIndices;
        if (MaterialSlotIndex != INDEX_NONE)
        {
            MaterialSlotIndices.Add(MaterialSlotIndex);
        }
        const FText SlotLabel = BuildAssetSlotLabel(&Asset, PreparedMesh, MaterialSlotIndex);
        OpenDetailsDialogInternal(
            Asset,
            PreparedMesh,
            MaterialSlotIndices,
            FailedMaterialSlotIndices,
            LastFailureMessage,
            LOCTEXT("DWCDataUVSlotDetailsWindowTitle", "DWC UV Details"),
            FText::Format(LOCTEXT("DWCDataUVSlotDetailsHeader", "DWC UV Details - {0}"), SlotLabel),
            false);
    }

    void OpenDWCDataUVAllSlotsDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage)
    {
        const TArray<int32> MaterialSlotIndices = CollectRecordedSlotIndices(Asset, FailedMaterialSlotIndices);
        OpenDetailsDialogInternal(
            Asset,
            PreparedMesh,
            MaterialSlotIndices,
            FailedMaterialSlotIndices,
            LastFailureMessage,
            LOCTEXT("DWCDataUVAllSlotsDetailsWindowTitle", "DWC UV Details - All Slots"),
            LOCTEXT("DWCDataUVAllSlotsDetailsHeader", "DWC UV Details - All Slots"),
            true);
    }

    void OpenLODRangeUpdateDialog(const FDWCLODRangeUpdateReport& Report)
    {
        const bool bHasGeneration = !Report.GeneratedLODIndices.IsEmpty();
        const bool bHasFailure = !Report.FailedLODIndices.IsEmpty() || !Report.bApplied;
        const bool bHasWarnings = Report.LODDetails.ContainsByPredicate(
            [](const FDWCLODRangeUpdateLODDetail& Detail)
            {
                return Detail.bHasWarnings;
            });

        auto BuildLODRangeText = [](const int32 FirstLODIndex, const int32 LastLODIndex)
        {
            return FirstLODIndex == LastLODIndex
                ? FString::Printf(TEXT("LOD%d"), FirstLODIndex)
                : FString::Printf(TEXT("LOD%d-LOD%d"), FirstLODIndex, LastLODIndex);
        };

        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        Body->AddSlot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(FText::FromString(FString::Printf(
                TEXT("%s  \u2192  %s"),
                *BuildLODRangeText(Report.PreviousFirstLODIndex, Report.PreviousLastLODIndex),
                *BuildLODRangeText(Report.RequestedFirstLODIndex, Report.RequestedLastLODIndex))))
            .Font(MakeReportFont(11, true))
        ];

        TSharedRef<SVerticalBox> ChangeRows = SNew(SVerticalBox);
        auto AddLODChangeRow = [&ChangeRows](const FText& Label, const TArray<int32>& LODIndices)
        {
            if (LODIndices.IsEmpty())
            {
                return;
            }
            ChangeRows->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildSummaryLine(Label, BuildLODListText(LODIndices))
            ];
        };
        AddLODChangeRow(LOCTEXT("LODRangeRetained", "Retained"), Report.RetainedLODIndices);
        AddLODChangeRow(LOCTEXT("LODRangeReused", "Reused"), Report.ReusedLODIndices);
        AddLODChangeRow(LOCTEXT("LODRangeGenerated", "Generated"), Report.GeneratedLODIndices);
        AddLODChangeRow(LOCTEXT("LODRangeRemoved", "Removed"), Report.RemovedLODIndices);
        AddLODChangeRow(LOCTEXT("LODRangePrepared", "Prepared"), Report.PreparedLODIndices);
        AddLODChangeRow(LOCTEXT("LODRangeFailed", "Failed"), Report.FailedLODIndices);

        Body->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
        [
            SNew(SBorder)
            .Padding(FMargin(12.0f, 8.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(NeutralBackground())
            [
                ChangeRows
            ]
        ];

        TSharedRef<SVerticalBox> Explanation = SNew(SVerticalBox);
        if (!Report.RetainedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeRetainedExplanation", "Existing DWC UV data for {0} was retained."),
                    BuildLODListText(Report.RetainedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
            ];
        }
        if (!Report.ReusedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeReusedExplanation", "Existing valid DWC UV data was reused for {0}."),
                    BuildLODListText(Report.ReusedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
            ];
        }
        if (!Report.GeneratedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeGeneratedExplanation", "DWC UV data was generated for {0}."),
                    BuildLODListText(Report.GeneratedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
            ];
        }
        if (!Report.RemovedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeRemovedExplanation", "Data and runtime mappings for {0} were removed from the active range."),
                    BuildLODListText(Report.RemovedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
            ];
        }

        if (!Report.PreparedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangePreparedExplanation", "Valid DWC UV data for {0} was retained and will be reused when the update is retried."),
                    BuildLODListText(Report.PreparedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
            ];
        }
        if (!Report.FailedLODIndices.IsEmpty())
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeFailedExplanation", "DWC UV generation failed for {0}."),
                    BuildLODListText(Report.FailedLODIndices)))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(ErrorColor())
            ];
        }

        if (Report.bApplied)
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(bHasGeneration
                    ? LOCTEXT("LODRangeGenerationPerformed", "Only LODs without reusable DWC UV data were generated.")
                    : LOCTEXT("LODRangeNoGenerationPerformed", "No DWC UV generation was performed."))
                .Font(MakeReportFont(10, true))
                .ColorAndOpacity(bHasGeneration ? FSlateColor(FStyleColors::ForegroundHover) : DataUVInfoColor())
            ];
        }
        else
        {
            Explanation->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::Format(
                    LOCTEXT("LODRangeNotAppliedExplanation", "The requested LOD range could not be activated. The active range remains {0}."),
                    FText::FromString(BuildLODRangeText(
                        Report.ActiveFirstLODIndex,
                        Report.ActiveLastLODIndex))))
                .AutoWrapText(true)
                .Font(MakeReportFont(10, true))
                .ColorAndOpacity(ErrorColor())
            ];
        }

        Body->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
        [
            SNew(SSeparator)
        ];
        Body->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
        [
            Explanation
        ];

        if (!Report.AdditionalSummary.IsEmpty())
        {
            Body->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Report.AdditionalSummary))
                .AutoWrapText(true)
                .Font(MakeReportFont())
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
            ];
        }

        if (Report.bApplied && bHasGeneration)
        {
            Body->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(FMargin(12.0f, 8.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                .BorderBackgroundColor(NeutralBackground())
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("LODRangeGeneratedCount", "Generated"),
                            FText::Format(
                                LOCTEXT("LODRangeGeneratedFraction", "{0} of {1} required LODs"),
                                FText::AsNumber(Report.GeneratedLODIndices.Num()),
                                FText::AsNumber(Report.GeneratedLODIndices.Num())))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("LODRangeReusedCount", "Reused"),
                            FText::AsNumber(Report.ReusedLODIndices.Num()))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
                    [
                        BuildSummaryLine(
                            LOCTEXT("LODRangeStatus", "Status"),
                            bHasWarnings
                                ? LOCTEXT("LODRangeStatusWarnings", "Ready with warnings")
                                : LOCTEXT("LODRangeStatusReady", "Ready"))
                    ]
                ]
            ];
        }

        if (!Report.LODDetails.IsEmpty())
        {
            TSharedRef<SVerticalBox> DetailsBody = SNew(SVerticalBox);
            for (const FDWCLODRangeUpdateLODDetail& Detail : Report.LODDetails)
            {
                const TCHAR* IconName = Detail.bSucceeded
                    ? Detail.bHasWarnings
                        ? TEXT("Icons.WarningWithColor")
                        : TEXT("Icons.SuccessWithColor")
                    : TEXT("Icons.ErrorWithColor");
                const FSlateColor IconColor = ColoredStatusIconTint();
                const FSlateColor TextColor = Detail.bSucceeded
                    ? DataUVReadyColor()
                    : ErrorColor();

                DetailsBody->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
                [
                    SNew(SBorder)
                    .Padding(FMargin(12.0f, 9.0f))
                    .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
                    .BorderBackgroundColor(Detail.bSucceeded ? NeutralBackground() : ErrorBackground())
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [
                                SNew(STextBlock)
                                .Text(FText::FromString(FString::Printf(TEXT("LOD%d"), Detail.LODIndex)))
                                .Font(MakeReportFont(10, true))
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(Detail.bSucceeded
                                    ? LOCTEXT("LODRangeDetailReady", "Ready")
                                    : LOCTEXT("LODRangeDetailFailed", "Failed"))
                                .Font(MakeReportFont(10, true))
                                .ColorAndOpacity(TextColor)
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f, 0.0f, 0.0f)
                            [
                                SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)
                                [
                                    SNew(SImage)
                                    .Image(FAppStyle::GetBrush(IconName))
                                    .ColorAndOpacity(IconColor)
                                ]
                            ]
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(Detail.Message))
                            .AutoWrapText(true)
                            .Font(MakeReportFont())
                            .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                        ]
                    ]
                ];
            }

            Body->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 0.0f)
            [
                SNew(SExpandableArea)
                .InitiallyCollapsed(!bHasFailure)
                .HeaderContent()
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("LODRangeDetailsHeading", "LOD Details"))
                    .Font(MakeReportFont(10, true))
                ]
                .BodyContent()
                [
                    DetailsBody
                ]
            ];
        }

        TSharedRef<SWindow> DialogWindow = SNew(SWindow)
            .Title(Report.bApplied
                ? LOCTEXT("LODRangeUpdatedTitle", "LOD Range Updated")
                : LOCTEXT("LODRangeNotAppliedTitle", "LOD Range Update Not Applied"))
            .ClientSize(FVector2D(570.0f, 520.0f))
            .SizingRule(ESizingRule::UserSized)
            .SupportsMaximize(false)
            .SupportsMinimize(false);

        DialogWindow->SetContent(
            SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(16.0f, 14.0f, 16.0f, 12.0f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 2.0f, 10.0f, 0.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(bHasFailure
                            ? TEXT("Icons.ErrorWithColor")
                            : bHasWarnings
                                ? TEXT("Icons.WarningWithColor")
                                : TEXT("Icons.InfoWithColor")))
                        .ColorAndOpacity(!bHasFailure && !bHasWarnings
                            ? DataUVInfoColor()
                            : ColoredStatusIconTint())
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    [
                        SNew(STextBlock)
                        .Text(Report.bApplied
                            ? LOCTEXT("LODRangeUpdatedHeader", "LOD Range Updated")
                            : LOCTEXT("LODRangeNotAppliedHeader", "LOD Range Update Not Applied"))
                        .Font(MakeReportFont(10, true))
                    ]
                ]
                + SVerticalBox::Slot().FillHeight(1.0f).Padding(16.0f, 0.0f, 16.0f, 0.0f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot().Padding(0.0f, 0.0f, 12.0f, 0.0f)
                    [
                        Body
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(16.0f, 12.0f, 16.0f, 16.0f)
                [
                    SNew(SButton)
                    .ContentPadding(FMargin(14.0f, 5.0f))
                    .Text(LOCTEXT("LODRangeClose", "Close"))
                    .OnClicked_Lambda([DialogWindow]()
                    {
                        DialogWindow->RequestDestroyWindow();
                        return FReply::Handled();
                    })
                ]
            ]);

        FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
    }

}

#undef LOCTEXT_NAMESPACE
