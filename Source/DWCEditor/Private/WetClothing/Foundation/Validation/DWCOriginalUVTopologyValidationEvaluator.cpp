// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCOriginalUVTopologyValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

void FDWCOriginalUVTopologyValidationEvaluator::AppendToSnapshot(
    const FDWCEditorValidationEvaluationContext& Context,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const UWetClothingAsset& Asset = Context.Asset;
    const FDWCEditorValidationTargetKey Key{
        EDWCEditorValidationDomain::DataUV,
        INDEX_NONE,
        FGuid(),
        TEXT("OriginalUVTopology")};
    FDWCEditorValidationNode& Node =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, Key);
    Node.Intent = EDWCEditorValidationIntentState::Enabled;
    const EDWCBakeStatus RecordedStatus =
        Asset.GetBakeOutputStatus(DWCBakeOutput::OriginalUVTopology);
    const bool bFailed = RecordedStatus == EDWCBakeStatus::Failed;
    if (bFailed)
    {
        Node.Operation = EDWCEditorValidationOperationState::Failed;
        const FString Failure = Asset.GetBakeOutputFailureMessage(
            DWCBakeOutput::OriginalUVTopology);
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            Node,
            TEXT("OriginalUVTopology.BuildFailed"),
            EDWCEditorValidationSeverity::Error,
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "Title", "Original UV Topology"),
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "Failed", "Failed"),
            FText::FromString(Failure.IsEmpty()
                ? TEXT("The Original UV topology build failed.")
                : Failure),
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "RetryBuild", "Fix the prepared mesh inputs, then initialize the DWC UV layout again."),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::InitializeDataUV,
            true,
            FText::GetEmpty(),
            DWCBakeOutput::OriginalUVTopology);
    }

    if (Context.RuntimeMesh == nullptr)
    {
        Node.Input = EDWCEditorValidationInputState::Missing;
        Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
        Node.Artifact = EDWCEditorValidationArtifactState::Missing;
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            Node,
            TEXT("OriginalUVTopology.RuntimeMeshMissing"),
            EDWCEditorValidationSeverity::Error,
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "Title", "Original UV Topology"),
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "Blocked", "Blocked"),
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "MeshMissing", "The prepared DWC Skeletal Mesh is unavailable."),
            NSLOCTEXT("DWCOriginalUVTopologyValidation", "RestoreMesh", "Restore the prepared mesh reference before initializing the DWC UV layout."),
            EDWCEditorValidationRemediation::Manual);
        return;
    }

    Node.Input = EDWCEditorValidationInputState::Valid;
    if (Context.bOriginalUVTopologyReady)
    {
        Node.Artifact = EDWCEditorValidationArtifactState::Current;
        if (Context.IsBakeOutputSavePending(DWCBakeOutput::OriginalUVTopology))
        {
            Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
        }
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::InitializeDataUV,
            EDWCEditorBuildActionState::UpToDate,
            &Node.Key);
        return;
    }

    Node.Artifact = Context.bHasOriginalUVTopologyPayload
        ? EDWCEditorValidationArtifactState::Stale
        : EDWCEditorValidationArtifactState::Missing;
    const FText Detail = Context.bHasOriginalUVTopologyPayload
        ? NSLOCTEXT("DWCOriginalUVTopologyValidation", "StaleDetail", "The saved Original UV topology does not match the prepared mesh, LOD 0, UV channel, or current topology generator contract.")
        : NSLOCTEXT("DWCOriginalUVTopologyValidation", "MissingDetail", "Original UV topology data has not been generated.");
    DWCEditorValidation::AddDiagnostic(
        InOutSnapshot,
        Node,
        TEXT("OriginalUVTopology.BuildRequired"),
        EDWCEditorValidationSeverity::Warning,
        NSLOCTEXT("DWCOriginalUVTopologyValidation", "Title", "Original UV Topology"),
        Context.bHasOriginalUVTopologyPayload
            ? NSLOCTEXT("DWCOriginalUVTopologyValidation", "OutOfDate", "Out of Date")
            : NSLOCTEXT("DWCOriginalUVTopologyValidation", "Missing", "Missing"),
        Detail,
        NSLOCTEXT("DWCOriginalUVTopologyValidation", "BuildAction", "Initialize the prepared mesh UV layout to rebuild Original UV topology data."),
        EDWCEditorValidationRemediation::BuildAction,
        EDWCEditorBuildAction::InitializeDataUV);
}
