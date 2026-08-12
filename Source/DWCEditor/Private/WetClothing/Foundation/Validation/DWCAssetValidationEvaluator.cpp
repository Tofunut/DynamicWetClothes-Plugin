// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCAssetValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"
#include "UObject/Package.h"

namespace
{
bool HasFailedBakeState(const FDWCAssetBakeState& State)
{
    return State.GeneratedDataUV == EDWCBakeStatus::Failed ||
           State.OriginalUVTopology == EDWCBakeStatus::Failed ||
           State.CPURuntimeData == EDWCBakeStatus::Failed ||
           State.GPURuntimeData == EDWCBakeStatus::Failed ||
           State.GPUMaps == EDWCBakeStatus::Failed ||
           State.WrinkleMaps == EDWCBakeStatus::Failed ||
           State.TransparencyMaps == EDWCBakeStatus::Failed;
}

EDWCEditorValidationArtifactState MapDataUVArtifact(const EDWCBakeStatus Status)
{
    switch (Status)
    {
    case EDWCBakeStatus::Valid:
        return EDWCEditorValidationArtifactState::Current;
    case EDWCBakeStatus::OutOfDate:
        return EDWCEditorValidationArtifactState::Stale;
    case EDWCBakeStatus::Failed:
        return EDWCEditorValidationArtifactState::Invalid;
    case EDWCBakeStatus::Required:
    case EDWCBakeStatus::Disabled:
    default:
        return EDWCEditorValidationArtifactState::Missing;
    }
}
}

void FDWCAssetValidationEvaluator::AppendAssetAndDataUV(
    const UWetClothingAsset& Asset,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const bool bSavePending = Asset.GetOutermost() != nullptr &&
        Asset.GetOutermost()->IsDirty();
    const FDWCEditorValidationTargetKey AssetKey{
        EDWCEditorValidationDomain::Asset};
    FDWCEditorValidationNode& AssetNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, AssetKey);
    AssetNode.Intent = EDWCEditorValidationIntentState::Enabled;
    AssetNode.Input = EDWCEditorValidationInputState::Valid;
    AssetNode.Artifact = EDWCEditorValidationArtifactState::Current;
    AssetNode.Persistence = bSavePending
        ? EDWCEditorValidationPersistenceState::SavePending
        : EDWCEditorValidationPersistenceState::Saved;
    if (bSavePending)
    {
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            AssetNode,
            TEXT("Asset.SavePending"),
            EDWCEditorValidationSeverity::Warning,
            NSLOCTEXT("DWCAssetValidation", "AssetTitle", "Wet Clothing Asset"),
            NSLOCTEXT("DWCAssetValidation", "SavePending", "Save Required"),
            NSLOCTEXT("DWCAssetValidation", "SavePendingDetail", "The Wet Clothing Asset contains changes that have not been saved."),
            NSLOCTEXT("DWCAssetValidation", "SaveAction", "Save the asset to persist the current data."),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::SaveAsset);
    }
    else
    {
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::SaveAsset,
            EDWCEditorBuildActionState::UpToDate,
            &AssetKey);
    }

    const FDWCEditorValidationTargetKey DataUVKey{
        EDWCEditorValidationDomain::DataUV};
    FDWCEditorValidationNode& DataUVNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, DataUVKey);
    DataUVNode.Intent = EDWCEditorValidationIntentState::Enabled;
    const FDWCAssetBakeState& BakeState = Asset.GetBakeState();
    DataUVNode.Artifact = MapDataUVArtifact(BakeState.GeneratedDataUV);

    const bool bHasRuntimeMesh = Asset.GetRuntimeSkeletalMesh() != nullptr;
    const bool bHasCurrentDataUV = bHasRuntimeMesh &&
        Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex());
    if (bHasCurrentDataUV)
    {
        DataUVNode.Input = EDWCEditorValidationInputState::Valid;
        DataUVNode.Artifact = EDWCEditorValidationArtifactState::Current;
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::InitializeDataUV,
            EDWCEditorBuildActionState::UpToDate,
            &DataUVKey);
        return;
    }

    const bool bLayoutLocked = Asset.HasLockedDataUVLayout();
    DataUVNode.Input = bHasRuntimeMesh
        ? EDWCEditorValidationInputState::Invalid
        : EDWCEditorValidationInputState::Missing;
    if (!bHasRuntimeMesh || bLayoutLocked)
    {
        DataUVNode.Dependency = EDWCEditorValidationDependencyState::Blocked;
        const FText Detail = !bHasRuntimeMesh
            ? NSLOCTEXT("DWCAssetValidation", "RuntimeMeshMissingDetail", "The prepared DWC Skeletal Mesh is missing, so its DWC UV layout cannot be validated or rebuilt.")
            : NSLOCTEXT("DWCAssetValidation", "LockedDataUVInvalidDetail", "The sealed DWC UV layout is invalid. Existing authored texture-space data prevents rebuilding it in place.");
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            DataUVNode,
            !bHasRuntimeMesh
                ? FName(TEXT("DataUV.RuntimeMeshMissing"))
                : FName(TEXT("DataUV.LockedLayoutInvalid")),
            EDWCEditorValidationSeverity::Error,
            NSLOCTEXT("DWCAssetValidation", "DataUVTitle", "Prepared Mesh UV Layout"),
            NSLOCTEXT("DWCAssetValidation", "ManualFix", "Manual Fix"),
            Detail,
            NSLOCTEXT("DWCAssetValidation", "DataUVManualAction", "Restore the prepared mesh reference or create a new WCA when the sealed UV layout no longer matches."),
            EDWCEditorValidationRemediation::Manual,
            {},
            BakeState.GeneratedDataUV == EDWCBakeStatus::Failed);
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::InitializeDataUV,
            EDWCEditorBuildActionState::Unavailable,
            &DataUVKey);
        return;
    }

    DWCEditorValidation::AddDiagnostic(
        InOutSnapshot,
        DataUVNode,
        TEXT("DataUV.BuildRequired"),
        BakeState.GeneratedDataUV == EDWCBakeStatus::Failed
            ? EDWCEditorValidationSeverity::Error
            : EDWCEditorValidationSeverity::Warning,
        NSLOCTEXT("DWCAssetValidation", "DataUVTitle", "Prepared Mesh UV Layout"),
        BakeState.GeneratedDataUV == EDWCBakeStatus::Failed
            ? NSLOCTEXT("DWCAssetValidation", "Failed", "Failed")
            : NSLOCTEXT("DWCAssetValidation", "Required", "Required"),
        NSLOCTEXT("DWCAssetValidation", "DataUVBuildDetail", "The prepared mesh does not contain a current DWC UV layout."),
        NSLOCTEXT("DWCAssetValidation", "DataUVBuildAction", "Initialize the prepared mesh UV layout for this asset."),
        EDWCEditorValidationRemediation::BuildAction,
        EDWCEditorBuildAction::InitializeDataUV,
        BakeState.GeneratedDataUV == EDWCBakeStatus::Failed);
}

void FDWCAssetValidationEvaluator::AppendUnownedFailure(
    const UWetClothingAsset& Asset,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const FDWCAssetBakeState& State = Asset.GetBakeState();
    if (!HasFailedBakeState(State) || State.LastFailure.IsEmpty() ||
        InOutSnapshot.Diagnostics.ContainsByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.bFailed;
            }))
    {
        return;
    }

    const FDWCEditorValidationTargetKey FailureKey{
        EDWCEditorValidationDomain::Failure};
    FDWCEditorValidationNode& FailureNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, FailureKey);
    FailureNode.Intent = EDWCEditorValidationIntentState::Enabled;
    FailureNode.Operation = EDWCEditorValidationOperationState::Failed;
    DWCEditorValidation::AddDiagnostic(
        InOutSnapshot,
        FailureNode,
        TEXT("Asset.UnownedBuildFailure"),
        EDWCEditorValidationSeverity::Error,
        NSLOCTEXT("DWCAssetValidation", "FailureTitle", "Internal Failure"),
        NSLOCTEXT("DWCAssetValidation", "Failed", "Failed"),
        FText::FromString(State.LastFailure),
        NSLOCTEXT("DWCAssetValidation", "FailureAction", "Review the failed build output and retry its corresponding Build for Runtime action."),
        EDWCEditorValidationRemediation::Manual,
        {},
        true);
}
