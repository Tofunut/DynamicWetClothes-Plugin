// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCRenderProfileValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Build/DWCRenderProfileBuildTargetResolver.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace
{
FText BuildContextLabel(const FDWCRenderProfileValidationIssue& Issue)
{
    if (!Issue.ProfileStableKey.IsEmpty())
    {
        return FText::FromString(Issue.ProfileStableKey);
    }
    return Issue.MaterialSlotIndex != INDEX_NONE
        ? FText::Format(
            NSLOCTEXT("DWCRenderProfileValidation", "SlotContext", "Material Slot {0}"),
            FText::AsNumber(Issue.MaterialSlotIndex))
        : FText::GetEmpty();
}
}

void FDWCRenderProfileValidationEvaluator::AppendToSnapshot(
    const UWetClothingAsset& Asset,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const FDWCRenderProfileValidationSnapshot ServiceState =
        FDWCRenderProfileBuildTargetResolver::Resolve(&Asset);
    const FDWCEditorValidationTargetKey RootKey{
        EDWCEditorValidationDomain::RenderProfile};
    FDWCEditorValidationNode& RootNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, RootKey);
    RootNode.Intent = ServiceState.bRequired
        ? EDWCEditorValidationIntentState::Enabled
        : EDWCEditorValidationIntentState::NotConfigured;
    RootNode.Input = EDWCEditorValidationInputState::Valid;
    RootNode.Artifact = ServiceState.bRequired
        ? EDWCEditorValidationArtifactState::Current
        : EDWCEditorValidationArtifactState::NotRequired;
    RootNode.Persistence = ServiceState.bSavePending
        ? EDWCEditorValidationPersistenceState::SavePending
        : EDWCEditorValidationPersistenceState::Saved;

    DWCEditorValidation::SetActionState(
        InOutSnapshot,
        EDWCEditorBuildAction::BakeRenderProfileData,
        ServiceState.BakeState,
        &RootKey);

    if (ServiceState.RecordedStatus == EDWCBakeStatus::Failed)
    {
        RootNode.Operation = EDWCEditorValidationOperationState::Failed;
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            RootNode,
            TEXT("RenderProfile.BuildFailed"),
            EDWCEditorValidationSeverity::Error,
            NSLOCTEXT("DWCRenderProfileValidation", "Title", "Render Profile Lookup Texture"),
            NSLOCTEXT("DWCRenderProfileValidation", "Failed", "Failed"),
            FText::FromString(ServiceState.BakeReason),
            NSLOCTEXT("DWCRenderProfileValidation", "Retry", "Fix any reported inputs, then retry Build for Runtime > Bake Render Profile Data."),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::BakeRenderProfileData,
            true,
            FText::GetEmpty(),
            DWCBakeOutput::RenderProfileData);
    }

    if (!ServiceState.bRequired && ServiceState.Issues.IsEmpty())
    {
        return;
    }

    for (const FDWCRenderProfileValidationIssue& Issue : ServiceState.Issues)
    {
        FDWCEditorValidationTargetKey Key{
            EDWCEditorValidationDomain::RenderProfile,
            Issue.MaterialSlotIndex};
        if (!Issue.ProfileStableKey.IsEmpty())
        {
            Key.SubResource = FName(*Issue.ProfileStableKey);
        }
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(InOutSnapshot, Key);
        Node.Intent = EDWCEditorValidationIntentState::Enabled;
        Node.Artifact = EDWCEditorValidationArtifactState::Stale;

        EDWCEditorValidationRemediation Remediation =
            EDWCEditorValidationRemediation::BuildAction;
        TOptional<EDWCEditorBuildAction> SuggestedAction =
            EDWCEditorBuildAction::BakeRenderProfileData;
        FText RequiredAction = NSLOCTEXT(
            "DWCRenderProfileValidation",
            "BakeAction",
            "Use Build for Runtime > Bake Render Profile Lookup Texture.");

        switch (Issue.Resolution)
        {
        case EDWCRenderProfileIssueResolution::GenerateMaterials:
            SuggestedAction = EDWCEditorBuildAction::GenerateMaterials;
            RequiredAction = NSLOCTEXT(
                "DWCRenderProfileValidation",
                "GenerateAction",
                "Use Build for Runtime > Generate Materials.");
            Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            break;
        case EDWCRenderProfileIssueResolution::Manual:
            Remediation = EDWCEditorValidationRemediation::Manual;
            SuggestedAction.Reset();
            RequiredAction = NSLOCTEXT(
                "DWCRenderProfileValidation",
                "ManualAction",
                "Fix the referenced Wet Part, material, mesh, or texture input before rebuilding Render Profile data.");
            Node.Input = EDWCEditorValidationInputState::Invalid;
            break;
        case EDWCRenderProfileIssueResolution::BakeRenderProfile:
        default:
            break;
        }

        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            Node,
            Issue.Code,
            Issue.bFailed
                ? EDWCEditorValidationSeverity::Error
                : EDWCEditorValidationSeverity::Warning,
            NSLOCTEXT("DWCRenderProfileValidation", "Title", "Render Profile Lookup Texture"),
            Issue.Resolution == EDWCRenderProfileIssueResolution::Manual
                ? NSLOCTEXT("DWCRenderProfileValidation", "ManualFix", "Manual Fix")
                : NSLOCTEXT("DWCRenderProfileValidation", "OutOfDate", "Out of Date"),
            FText::FromString(Issue.Detail),
            RequiredAction,
            Remediation,
            SuggestedAction,
            Issue.bFailed,
            BuildContextLabel(Issue));
    }

    FDWCEditorValidationNode& FinalRootNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, RootKey);
    FinalRootNode.Artifact = ServiceState.Issues.IsEmpty()
        ? EDWCEditorValidationArtifactState::Current
        : EDWCEditorValidationArtifactState::Stale;
}
