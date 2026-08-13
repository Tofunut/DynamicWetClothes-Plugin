// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/WCAValidationReport.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCAssetValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/Foundation/Validation/DWCGeneratedMaterialValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCOriginalUVTopologyValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRenderProfileValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRuntimeValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCTransparencyCookValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCTransparencyLayerValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCWetPartValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCWrinkleValidationEvaluator.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorAuthoringPayloadDiagnostics.h"
#include "UObject/Package.h"

namespace
{
void AppendIssueSection(
    TArray<FString>& Sections,
    const FWCAValidationReport& Report,
    const FWCAValidationSectionResult& SectionResult,
    const bool bManualOnly)
{
    TArray<FString> Lines;
    for (const int32 IssueIndex : SectionResult.IssueIndices)
    {
        if (!Report.Issues.IsValidIndex(IssueIndex))
        {
            continue;
        }
        const FWCAValidationIssue& Issue = Report.Issues[IssueIndex];
        if (bManualOnly && Issue.Remediation != EDWCEditorValidationRemediation::Manual)
        {
            continue;
        }

        FString Line = Issue.Detail.IsEmpty()
            ? Issue.Title.ToString()
            : Issue.Detail.ToString();
        if (!Issue.RequiredAction.IsEmpty())
        {
            Line += FString::Printf(TEXT(" %s"), *Issue.RequiredAction.ToString());
        }
        Lines.Add(Line);
    }
    if (!Lines.IsEmpty())
    {
        const FDWCValidationSectionDescriptor* Descriptor =
            FDWCEditorValidationSectionRegistry::Find(SectionResult.Section);
        const FString Heading = Descriptor != nullptr
            ? Descriptor->Title.ToString()
            : TEXT("Validation");
        Sections.Add(FString::Printf(
            TEXT("%s\n%s"),
            *Heading,
            *FString::Join(Lines, TEXT("\n"))));
    }
}

void AppendAllIssueSections(
    TArray<FString>& Sections,
    const FWCAValidationReport& Report,
    const bool bManualOnly)
{
    for (const FWCAValidationSectionResult& SectionResult : Report.Sections)
    {
        AppendIssueSection(Sections, Report, SectionResult, bManualOnly);
    }
}
}

bool FWCAValidationReport::HasIssues() const
{
    return !Issues.IsEmpty() || Sections.ContainsByPredicate(
        [](const FWCAValidationSectionResult& Section)
        {
            return Section.PresentationState == EDWCValidationPresentationState::Warning ||
                   Section.PresentationState == EDWCValidationPresentationState::Error;
        });
}

FString FWCAValidationIssue::BuildDisplayGroupKey() const
{
    return FString::Printf(
        TEXT("%d|%d|%d|%d|%d|%s|%s|%s|%s"),
        static_cast<int32>(Section),
        static_cast<int32>(Remediation),
        static_cast<int32>(Severity),
        bFailed ? 1 : 0,
        BuildAction.IsSet() ? static_cast<int32>(BuildAction.GetValue()) : INDEX_NONE,
        *IssueId.ToString(),
        *Status.ToString(),
        *Detail.ToString(),
        *RequiredAction.ToString());
}

bool FWCAValidationReport::HasManualIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Remediation == EDWCEditorValidationRemediation::Manual;
        });
}

bool FWCAValidationReport::HasAutoResolvableIssues() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Remediation == EDWCEditorValidationRemediation::BuildAction &&
                   Issue.BuildAction.IsSet();
        });
}

bool FWCAValidationReport::HasErrors() const
{
    return Issues.ContainsByPredicate(
        [](const FWCAValidationIssue& Issue)
        {
            return Issue.Severity == EWCAValidationSeverity::Error;
        }) || Sections.ContainsByPredicate(
        [](const FWCAValidationSectionResult& Section)
        {
            return Section.PresentationState == EDWCValidationPresentationState::Error;
        });
}

int32 FWCAValidationReport::GetDisplayIssueCount() const
{
    TSet<FString> DisplayKeys;
    for (const FWCAValidationIssue& Issue : Issues)
    {
        DisplayKeys.Add(Issue.BuildDisplayGroupKey());
    }
    for (const FWCAValidationSectionResult& Section : Sections)
    {
        if (Section.IssueIndices.IsEmpty() &&
            (Section.PresentationState == EDWCValidationPresentationState::Warning ||
             Section.PresentationState == EDWCValidationPresentationState::Error))
        {
            DisplayKeys.Add(FString::Printf(TEXT("SectionState|%d"), static_cast<int32>(Section.Section)));
        }
    }
    return DisplayKeys.Num();
}

const FWCAValidationSectionResult* FWCAValidationReport::FindSection(
    const EWCAValidationSection Section) const
{
    return Sections.FindByPredicate(
        [Section](const FWCAValidationSectionResult& Result)
        {
            return Result.Section == Section;
        });
}

FString FWCAValidationReport::BuildSummary() const
{
    TArray<FString> SummarySections;
    AppendAllIssueSections(SummarySections, *this, false);
    return FString::Join(SummarySections, TEXT("\n\n"));
}

FString FWCAValidationReport::BuildManualIssueSummary() const
{
    TArray<FString> SummarySections;
    AppendAllIssueSections(SummarySections, *this, true);
    return FString::Join(SummarySections, TEXT("\n\n"));
}

FWCAEditorValidationSnapshot BuildWCAValidationSnapshot(
    UWetClothingAsset& Asset,
    const EWCAValidationMode Mode)
{
#if WITH_EDITORONLY_DATA
    const EDWCEditorValidationAccess Access = Mode == EWCAValidationMode::ExactPayload
        ? EDWCEditorValidationAccess::ExactPayload
        : EDWCEditorValidationAccess::MetadataOnly;
    FDWCEditorAuthoringOperationScope DiagnosticScope(
        Access == EDWCEditorValidationAccess::ExactPayload
            ? TEXT("WCA.Validation.ExactPayload")
            : TEXT("WCA.Validation.MetadataOnly"),
        &Asset);
    FWCAEditorValidationSnapshot Snapshot;
    Snapshot.AssetPath = Asset.GetPathName();
    Snapshot.bAssetDirty = Asset.GetOutermost() != nullptr &&
        Asset.GetOutermost()->IsDirty();
    Snapshot.Access = Access;
    Snapshot.bDeepValidation = Access == EDWCEditorValidationAccess::ExactPayload;
    Snapshot.TriangleDiagnostics = Asset.GetValidationSummary();

    const FDWCEditorValidationEvaluationContext Context(
        Asset,
        Access);
    FDWCAssetValidationEvaluator::AppendAssetAndDataUV(Context, Snapshot);
    FDWCOriginalUVTopologyValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCWetPartValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCGeneratedMaterialValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCRuntimeValidationEvaluator::AppendToSnapshot(Context, Snapshot);
    FDWCRenderProfileValidationEvaluator::AppendToSnapshot(Asset, Snapshot);
    FDWCWrinkleValidationEvaluator::AppendToSnapshot(
        Asset,
        Snapshot.bDeepValidation,
        Snapshot);
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(
        Asset,
        Snapshot.bDeepValidation,
        Snapshot);
    if (Snapshot.bDeepValidation)
    {
        FDWCTransparencyCookValidationEvaluator::AppendToSnapshot(Asset, Snapshot);
    }
    FDWCAssetValidationEvaluator::AppendUnownedFailure(Asset, Snapshot);
    return Snapshot;
#else
    return {};
#endif
}

FWCAValidationReport BuildWCAValidationReport(
    UWetClothingAsset& Asset,
    const EWCAValidationMode Mode)
{
    return FDWCEditorValidationReportAdapter::BuildReport(
        BuildWCAValidationSnapshot(Asset, Mode));
}
