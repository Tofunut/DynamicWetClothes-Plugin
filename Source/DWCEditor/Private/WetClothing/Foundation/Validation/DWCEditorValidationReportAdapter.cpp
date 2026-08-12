// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"

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

    EWCAValidationSection MapSection(const EDWCEditorValidationDomain Domain)
    {
        switch (Domain)
        {
        case EDWCEditorValidationDomain::DataUV:
        case EDWCEditorValidationDomain::Asset:
            return EWCAValidationSection::DataUV;
        case EDWCEditorValidationDomain::RuntimeCPU:
        case EDWCEditorValidationDomain::RuntimeGPU:
            return EWCAValidationSection::RuntimeData;
        case EDWCEditorValidationDomain::GeneratedMaterial:
            return EWCAValidationSection::GeneratedMaterials;
        case EDWCEditorValidationDomain::WetPart:
            return EWCAValidationSection::RenderProfileData;
        case EDWCEditorValidationDomain::GPUSimulationMap:
            return EWCAValidationSection::GPUSimulationMaps;
        case EDWCEditorValidationDomain::RenderProfile:
            return EWCAValidationSection::RenderProfileData;
        case EDWCEditorValidationDomain::Wrinkle:
            return EWCAValidationSection::WrinkleMaps;
        case EDWCEditorValidationDomain::Transparency:
            return EWCAValidationSection::TransparencyMaps;
        case EDWCEditorValidationDomain::Failure:
        default:
            return EWCAValidationSection::FailureDetails;
        }
    }

}

FWCAValidationReport FDWCEditorValidationReportAdapter::BuildReport(
    const FWCAEditorValidationSnapshot& Snapshot)
{
    FWCAValidationReport Report;
    Report.Diagnostics = Snapshot.TriangleDiagnostics;
    Report.Issues.Reserve(Snapshot.Diagnostics.Num());
    for (const FDWCEditorValidationDiagnostic& Diagnostic : Snapshot.Diagnostics)
    {
        FWCAValidationIssue& Issue = Report.Issues.AddDefaulted_GetRef();
        Issue.IssueId = Diagnostic.Code;
        Issue.Severity = MapSeverity(Diagnostic.Severity);
        Issue.Section = MapSection(Diagnostic.Target.Domain);
        Issue.Remediation = Diagnostic.Remediation;
        Issue.BuildAction = Diagnostic.SuggestedAction;
        Issue.Title = Diagnostic.Presentation.Title;
        Issue.Status = Diagnostic.Presentation.Status;
        Issue.Detail = Diagnostic.Presentation.Detail;
        Issue.RequiredAction = Diagnostic.Presentation.RequiredAction;
        Issue.ContextLabel = Diagnostic.Presentation.ContextLabel;
        Issue.Target = Diagnostic.Target;
        Issue.bFailed = Diagnostic.bFailed;
    }
    return Report;
}
