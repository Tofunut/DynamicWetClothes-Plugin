#include "WetClothing/WCAEditor/UI/WCAReportDialogs.h"

#include "DataAssets/WetClothingAssetSetupData.h"
#include "Engine/SkeletalMesh.h"
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
        return FSlateColor(FLinearColor(1.0f, 0.64f, 0.12f, 1.0f));
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

        return FText::Format(
            LOCTEXT(
                "DWCDataUVCorrectedGenerationSummary",
                "DWC UV data was generated for all {0} target LODs. Some source UV issues were corrected during generation."),
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
        TSharedRef<SWrapBox> Metrics = SNew(SWrapBox).UseAllottedSize(true);
        AddMetricIfNonZero(Metrics, LOCTEXT("ExcludedTrianglesMetric", "Excluded triangles"), Result.ExcludedTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("DegenerateSourceUVMetric", "Degenerate Source UV"), Result.DegenerateSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("InvalidSourceUVMetric", "Invalid Source UV"), Result.InvalidSourceUVTriangleCount);
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
                            .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("DWCDataUVSourceUVIssuesHeading", "Source UV Issues"))
                        .Font(MakeReportFont(10, true))
                        .ColorAndOpacity(WarningColor())
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
                        "Some problematic source triangles or overlapping UV islands were corrected during generation."))
                    .AutoWrapText(true)
                    .Font(MakeReportFont())
                    .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("DWCDataUVSourceIssuesUsable", "The generated DWC UV remains usable."))
                    .Font(MakeReportFont(10, true))
                ]
            ];
    }

    TSharedRef<SWidget> BuildSlotWarningCard(
        const FDWCDataUVSlotWarning& Warning,
        const USkeletalMesh* PreparedMesh)
    {
        TSharedRef<SWrapBox> Metrics = SNew(SWrapBox).UseAllottedSize(true);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotDegenerateSourceUVMetric", "Degenerate Source UV"), Warning.DegenerateSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotInvalidSourceUVMetric", "Invalid Source UV"), Warning.InvalidSourceUVTriangleCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotSplitOriginalUVIslandsMetric", "Split islands"), Warning.SplitOriginalUVIslandCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotOverlapPairsMetric", "Overlaps"), Warning.SelfOverlapPairCount);
        AddMetricIfNonZero(Metrics, LOCTEXT("SlotBudgetFallbackMetric", "Budget fallback islands"), Warning.BudgetFallbackIslandCount);

        TSharedRef<SVerticalBox> Lines = SNew(SVerticalBox);
        if (Warning.DegenerateSourceUVTriangleCount > 0 || Warning.InvalidSourceUVTriangleCount > 0)
        {
            AddBulletLine(
                Lines,
                LOCTEXT("DWCDataUVExcludedProblemTriangles", "Problematic triangles were excluded before generation."));
        }
        if (Warning.SplitOriginalUVIslandCount > 0 || Warning.SelfOverlapPairCount > 0)
        {
            AddBulletLine(
                Lines,
                LOCTEXT("DWCDataUVSplitSelfOverlap", "Overlapping islands were automatically separated."));
        }
        if (Warning.BudgetFallbackIslandCount > 0)
        {
            AddBulletLine(
                Lines,
                LOCTEXT("DWCDataUVBudgetFallback", "Overlap analysis exceeded its safety budget for some islands, so those islands were split conservatively."));
        }
        AddBulletLine(
            Lines,
            LOCTEXT("DWCDataUVReviewUnexpectedSourceUV", "Review the source UV only if this result was unexpected."));

        return SNew(SBorder)
            .Padding(FMargin(12.0f, 9.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Header")))
            .BorderBackgroundColor(NeutralBackground())
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
                        .Text(BuildSlotLabel(PreparedMesh, Warning.MaterialSlotIndex))
                        .Font(MakeReportFont(10, true))
                        .AutoWrapText(true)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        .Padding(0.0f, 0.0f, 5.0f, 0.0f)
                        [
                            SNew(SBox)
                            .WidthOverride(14.0f)
                            .HeightOverride(14.0f)
                            [
                                SNew(SImage)
                                .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                            ]
                        ]
                        + SHorizontalBox::Slot()
                        .AutoWidth()
                        .VAlign(VAlign_Center)
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("DWCDataUVUsableWithWarnings", "Usable with warnings"))
                            .Font(MakeReportFont(10, true))
                            .ColorAndOpacity(WarningColor())
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
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(16.0f)
                    .HeightOverride(16.0f)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(
                            bSkipped ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor")))
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
                    .Font(MakeReportFont(10, bSkipped))
                    .ColorAndOpacity(bSkipped ? WarningColor() : FSlateColor(FStyleColors::ForegroundHover))
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
                .Text(LOCTEXT("DWCDataUVFailureLODStatusHeading", "LOD Generation Status"))
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
                    BuildSlotLabel(PreparedMesh, Failure.MaterialSlotIndex))
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
            .Text(LOCTEXT("DWCDataUVFailureNoResult", "Result: No usable DWC UV channel was generated."))
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
        const USkeletalMesh* PreparedMesh)
    {
        const bool bHasSkippedLODs = !Result.LODWarnings.IsEmpty();
        const bool bHasSourceIssues =
            Result.ExcludedTriangleCount > 0 ||
            Result.DegenerateSourceUVTriangleCount > 0 ||
            Result.InvalidSourceUVTriangleCount > 0 ||
            Result.SplitOriginalUVIslandCount > 0 ||
            Result.SelfOverlapPairCount > 0 ||
            Result.BudgetFallbackIslandCount > 0;

        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

        if (!Result.TargetLODIndices.IsEmpty())
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildLODStatusSection(Result)
            ];
        }

        if (bHasSourceIssues)
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 10.0f)
            [
                BuildIssueSummarySection(Result)
            ];
        }

        TArray<FDWCDataUVSlotWarning> SlotWarnings = Result.SlotWarnings;
        SlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            });

        bool bHasSlotWarnings = false;
        for (const FDWCDataUVSlotWarning& Warning : SlotWarnings)
        {
            bHasSlotWarnings = bHasSlotWarnings || Warning.HasWarnings();
        }
        if (bHasSlotWarnings)
        {
            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 2.0f, 0.0f, 6.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("DWCDataUVSlotWarningsHeading", "Material Slot Warnings"))
                .Font(MakeReportFont(10, true))
            ];
        }

        for (const FDWCDataUVSlotWarning& Warning : SlotWarnings)
        {
            if (!Warning.HasWarnings())
            {
                continue;
            }

            Body->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                BuildSlotWarningCard(Warning, PreparedMesh)
            ];
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
                            bHasSkippedLODs ? TEXT("Icons.WarningWithColor") : TEXT("Icons.SuccessWithColor")))
                    ]
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(bHasSkippedLODs
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

    void OpenDWCDataUVBuildFailureDialog(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh* PreparedMesh)
    {
        const FString TechnicalDetails = BuildFailureTechnicalText(Result);
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

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
            BuildFailureDetailsSection(Result, PreparedMesh)
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
            .Title(LOCTEXT("DWCDataUVFailureTitle", "DWC UV Channel Generation Failed"))
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
                            .Text(LOCTEXT("DWCDataUVFailureHeader", "DWC UV Channel Generation Failed"))
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
                                        "DWC UV data could not be generated. No usable DWC UV channel was created."))
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
}

#undef LOCTEXT_NAMESPACE
