// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSectionRegistry.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

namespace
{
    EWCAValidationSeverity MapSeverity(const EDWCEditorValidationSeverity Severity)
    {
        switch (Severity)
        {
        case EDWCEditorValidationSeverity::Error: return EWCAValidationSeverity::Error;
        case EDWCEditorValidationSeverity::Warning: return EWCAValidationSeverity::Warning;
        case EDWCEditorValidationSeverity::Info:
        default: return EWCAValidationSeverity::Info;
        }
    }

    EDWCEditorValidationOverallState MapActionState(
        const EDWCEditorBuildAction Action,
        const EDWCEditorBuildActionState State)
    {
        switch (State)
        {
        case EDWCEditorBuildActionState::Blocked:
            return EDWCEditorValidationOverallState::Blocked;
        case EDWCEditorBuildActionState::Required:
            return Action == EDWCEditorBuildAction::SaveAsset
                ? EDWCEditorValidationOverallState::SavePending
                : EDWCEditorValidationOverallState::Stale;
        case EDWCEditorBuildActionState::Running:
            return EDWCEditorValidationOverallState::Running;
        case EDWCEditorBuildActionState::Failed:
            return EDWCEditorValidationOverallState::Failed;
        case EDWCEditorBuildActionState::UpToDate:
            return EDWCEditorValidationOverallState::Current;
        case EDWCEditorBuildActionState::Unavailable:
        default:
            return EDWCEditorValidationOverallState::NotApplicable;
        }
    }

    void MergeState(
        FWCAValidationSectionResult& SectionResult,
        const EDWCEditorValidationOverallState Candidate)
    {
        if (FDWCEditorValidationSectionRegistry::GetStatePriority(Candidate) >
            FDWCEditorValidationSectionRegistry::GetStatePriority(SectionResult.OverallState))
        {
            SectionResult.OverallState = Candidate;
        }
    }

    void MergePresentationState(
        FWCAValidationSectionResult& SectionResult,
        const EDWCValidationPresentationState Candidate)
    {
        if (static_cast<uint8>(Candidate) > static_cast<uint8>(SectionResult.PresentationState))
        {
            SectionResult.PresentationState = Candidate;
        }
    }

    FWCAValidationSectionResult* FindMutableSection(
        FWCAValidationReport& Report,
        const EWCAValidationSection Section)
    {
        return Report.Sections.FindByPredicate(
            [Section](const FWCAValidationSectionResult& Result)
            {
                return Result.Section == Section;
            });
    }
}

FWCAValidationReport FDWCEditorValidationReportAdapter::BuildReport(
    const FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorBuildStatusSnapshot* BuildStatus)
{
    FWCAValidationReport Report;
    Report.Diagnostics = Snapshot.TriangleDiagnostics;
    for (const FDWCValidationSectionDescriptor& Descriptor :
         FDWCEditorValidationSectionRegistry::GetSections())
    {
        FWCAValidationSectionResult& SectionResult = Report.Sections.AddDefaulted_GetRef();
        SectionResult.Section = Descriptor.Section;
    }

    for (const FDWCEditorValidationNode& Node : Snapshot.Nodes)
    {
        FWCAValidationSectionResult* SectionResult = FindMutableSection(
            Report,
            FDWCEditorValidationSectionRegistry::MapDomain(Node.Key.Domain));
        if (SectionResult == nullptr)
        {
            continue;
        }

        const EDWCEditorValidationOverallState NodeState = Node.GetOverallState();
        SectionResult->bApplicable |= NodeState != EDWCEditorValidationOverallState::NotApplicable;
        MergeState(*SectionResult, NodeState);
    }

    Report.Issues.Reserve(Snapshot.Diagnostics.Num());
    for (const FDWCEditorValidationDiagnostic& Diagnostic : Snapshot.Diagnostics)
    {
        const EWCAValidationSection Section =
            FDWCEditorValidationSectionRegistry::MapDomain(Diagnostic.Target.Domain);
        FWCAValidationIssue& Issue = Report.Issues.AddDefaulted_GetRef();
        Issue.IssueId = Diagnostic.Code;
        Issue.Severity = MapSeverity(Diagnostic.Severity);
        Issue.Section = Section;
        Issue.Remediation = Diagnostic.Remediation;
        Issue.BuildAction = Diagnostic.SuggestedAction;
        Issue.Title = Diagnostic.Presentation.Title;
        Issue.Status = Diagnostic.Presentation.Status;
        Issue.Detail = Diagnostic.Presentation.Detail;
        Issue.RequiredAction = Diagnostic.Presentation.RequiredAction;
        Issue.ContextLabel = Diagnostic.Presentation.ContextLabel;
        Issue.Target = Diagnostic.Target;
        Issue.bFailed = Diagnostic.bFailed;

        if (FWCAValidationSectionResult* SectionResult = FindMutableSection(Report, Section))
        {
            SectionResult->bApplicable = true;
            SectionResult->IssueIndices.Add(Report.Issues.Num() - 1);
            SectionResult->ErrorCount += Issue.Severity == EWCAValidationSeverity::Error ? 1 : 0;
            SectionResult->WarningCount += Issue.Severity == EWCAValidationSeverity::Warning ? 1 : 0;
            MergePresentationState(
                *SectionResult,
                Issue.Severity == EWCAValidationSeverity::Error
                    ? EDWCValidationPresentationState::Error
                    : Issue.Severity == EWCAValidationSeverity::Warning
                        ? EDWCValidationPresentationState::Warning
                        : EDWCValidationPresentationState::Info);
            if (SectionResult->OverallState == EDWCEditorValidationOverallState::NotApplicable)
            {
                MergeState(
                    *SectionResult,
                    Issue.Severity == EWCAValidationSeverity::Error
                        ? EDWCEditorValidationOverallState::Failed
                        : Issue.Severity == EWCAValidationSeverity::Warning
                            ? EDWCEditorValidationOverallState::Stale
                            : EDWCEditorValidationOverallState::Draft);
            }
        }
    }

    for (const TPair<EDWCEditorBuildAction, FDWCEditorValidationActionState>& Pair : Snapshot.Actions)
    {
        const EDWCEditorBuildAction Action = Pair.Key;
        const FDWCEditorValidationActionState& ValidationAction = Pair.Value;
        EDWCEditorBuildActionState ActionState = ValidationAction.State;
        if (BuildStatus != nullptr)
        {
            if (const FDWCEditorBuildActionStatus* Status = BuildStatus->Find(Action))
            {
                ActionState = Status->State;
            }
        }

        FWCAValidationSectionResult* SectionResult = FindMutableSection(
            Report,
            FDWCEditorValidationSectionRegistry::MapAction(Action));
        if (SectionResult != nullptr &&
            ActionState != EDWCEditorBuildActionState::Unavailable &&
            ActionState != EDWCEditorBuildActionState::UpToDate)
        {
            SectionResult->bApplicable = true;
            SectionResult->SuggestedActions.AddUnique(Action);
            MergeState(*SectionResult, MapActionState(Action, ActionState));
        }
    }

    if (BuildStatus != nullptr)
    {
        for (const TPair<EDWCEditorBuildAction, FDWCEditorBuildActionStatus>& Pair : BuildStatus->Actions)
        {
            if (Snapshot.Actions.Contains(Pair.Key) ||
                Pair.Value.State == EDWCEditorBuildActionState::Unavailable ||
                Pair.Value.State == EDWCEditorBuildActionState::UpToDate)
            {
                continue;
            }
            if (FWCAValidationSectionResult* SectionResult = FindMutableSection(
                Report,
                FDWCEditorValidationSectionRegistry::MapAction(Pair.Key)))
            {
                SectionResult->bApplicable = true;
                SectionResult->SuggestedActions.AddUnique(Pair.Key);
                MergeState(*SectionResult, MapActionState(Pair.Key, Pair.Value.State));
            }
        }
    }

    for (FWCAValidationSectionResult& SectionResult : Report.Sections)
    {
        MergePresentationState(
            SectionResult,
            FDWCEditorValidationSectionRegistry::MapPresentationState(SectionResult.OverallState));
    }
    return Report;
}
