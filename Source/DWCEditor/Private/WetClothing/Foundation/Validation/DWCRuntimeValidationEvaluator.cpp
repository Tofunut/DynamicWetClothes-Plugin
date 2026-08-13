// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCRuntimeValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace
{
struct FRuntimeTargetSpec
{
    EDWCEditorValidationDomain Domain;
    EDWCEditorBuildAction Action;
    FName SubResource;
    FText Title;
    int32 OutputMask = 0;
    bool bEnabled = false;
    bool bPrerequisitesReady = false;
    bool bValid = false;
    bool bHasPayload = false;
    EDWCBakeStatus RecordedStatus = EDWCBakeStatus::Required;
};

void EvaluateTarget(
    const FDWCEditorValidationEvaluationContext& Context,
    const FRuntimeTargetSpec& Spec,
    FWCAEditorValidationSnapshot& Snapshot)
{
    const FDWCEditorValidationTargetKey Key{
        Spec.Domain,
        INDEX_NONE,
        FGuid(),
        Spec.SubResource};
    FDWCEditorValidationNode& Node = DWCEditorValidation::FindOrAddNode(Snapshot, Key);

    if (!Context.bHasWettableSlots)
    {
        Node.Intent = EDWCEditorValidationIntentState::NotApplicable;
        Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
        DWCEditorValidation::SetActionState(
            Snapshot, Spec.Action, EDWCEditorBuildActionState::Unavailable);
        return;
    }
    if (!Spec.bEnabled)
    {
        Node.Intent = EDWCEditorValidationIntentState::Disabled;
        Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
        DWCEditorValidation::SetActionState(
            Snapshot, Spec.Action, EDWCEditorBuildActionState::Unavailable);
        return;
    }

    Node.Intent = EDWCEditorValidationIntentState::Enabled;
    if (!Spec.bPrerequisitesReady)
    {
        Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
        Node.Artifact = Spec.bHasPayload
            ? EDWCEditorValidationArtifactState::Stale
            : EDWCEditorValidationArtifactState::Missing;
        const bool bRuntimeMeshMissing = Context.RuntimeMesh == nullptr;
        const EDWCEditorBuildAction BlockingAction = EDWCEditorBuildAction::InitializeDataUV;
        if (bRuntimeMeshMissing)
        {
            DWCEditorValidation::SetActionState(
                Snapshot,
                Spec.Action,
                EDWCEditorBuildActionState::Blocked,
                &Node.Key);
        }
        else
        {
            DWCEditorValidation::SetActionState(
                Snapshot,
                Spec.Action,
                EDWCEditorBuildActionState::Required,
                &Node.Key,
                MakeArrayView(&BlockingAction, 1));
        }
        DWCEditorValidation::AddDiagnostic(
            Snapshot,
            Node,
            FName(*FString::Printf(TEXT("%sPrerequisite"), *Spec.SubResource.ToString())),
            EDWCEditorValidationSeverity::Warning,
            Spec.Title,
            bRuntimeMeshMissing
                ? NSLOCTEXT("DWCRuntimeValidation", "Blocked", "Blocked")
                : NSLOCTEXT("DWCRuntimeValidation", "PrerequisiteRequired", "Prerequisite Required"),
            bRuntimeMeshMissing
                ? NSLOCTEXT("DWCRuntimeValidation", "RuntimeMeshMissingDetail", "Runtime data requires a DWC runtime mesh.")
                : NSLOCTEXT("DWCRuntimeValidation", "PrerequisiteDetail", "The prepared mesh and DWC UV prerequisites are not current."),
            bRuntimeMeshMissing
                ? NSLOCTEXT("DWCRuntimeValidation", "RuntimeMeshMissingAction", "Assign or generate the DWC runtime mesh first.")
                : NSLOCTEXT("DWCRuntimeValidation", "PrerequisiteAction", "Initialize the DWC data UV layout, then rebuild runtime data."),
            bRuntimeMeshMissing
                ? EDWCEditorValidationRemediation::Manual
                : EDWCEditorValidationRemediation::BuildAction,
            bRuntimeMeshMissing
                ? TOptional<EDWCEditorBuildAction>()
                : TOptional<EDWCEditorBuildAction>(Spec.Action));
        return;
    }

    Node.Input = EDWCEditorValidationInputState::Valid;
    const bool bSavePending = Context.IsBakeOutputSavePending(Spec.OutputMask);
    if (Spec.bValid)
    {
        Node.Artifact = EDWCEditorValidationArtifactState::Current;
        if (bSavePending)
        {
            Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            DWCEditorValidation::AddDiagnostic(
                Snapshot,
                Node,
                FName(*FString::Printf(TEXT("%sSavePending"), *Spec.SubResource.ToString())),
                EDWCEditorValidationSeverity::Warning,
                Spec.Title,
                NSLOCTEXT("DWCRuntimeValidation", "SaveRequired", "Save Required"),
                NSLOCTEXT("DWCRuntimeValidation", "SaveDetail", "The runtime output is current in memory but has not been saved."),
                NSLOCTEXT("DWCRuntimeValidation", "SaveAction", "Save the Wet Clothing Asset."),
                EDWCEditorValidationRemediation::BuildAction,
                EDWCEditorBuildAction::SaveAsset);
        }
        DWCEditorValidation::SetActionState(
            Snapshot, Spec.Action, EDWCEditorBuildActionState::UpToDate, &Node.Key);
        return;
    }

    Node.Artifact = Spec.bHasPayload
        ? EDWCEditorValidationArtifactState::Stale
        : EDWCEditorValidationArtifactState::Missing;
    const bool bFailed = Spec.RecordedStatus == EDWCBakeStatus::Failed;
    if (bFailed)
    {
        Node.Operation = EDWCEditorValidationOperationState::Failed;
    }
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        Spec.SubResource,
        bFailed ? EDWCEditorValidationSeverity::Error : EDWCEditorValidationSeverity::Warning,
        Spec.Title,
        bFailed
            ? NSLOCTEXT("DWCRuntimeValidation", "Failed", "Failed")
            : (Spec.bHasPayload
                ? NSLOCTEXT("DWCRuntimeValidation", "OutOfDate", "Out of Date")
                : NSLOCTEXT("DWCRuntimeValidation", "Missing", "Missing")),
        Spec.bHasPayload
            ? NSLOCTEXT("DWCRuntimeValidation", "StaleDetail", "Stored runtime data does not match the current mesh or authored Wet Part data.")
            : NSLOCTEXT("DWCRuntimeValidation", "MissingDetail", "Required runtime data has not been built."),
        Spec.Action == EDWCEditorBuildAction::BuildCPURuntimeData
            ? NSLOCTEXT("DWCRuntimeValidation", "CPUAction", "Use Build for Runtime > Build CPU Runtime Data.")
            : NSLOCTEXT("DWCRuntimeValidation", "GPUAction", "Use Build for Runtime > Build GPU Runtime Data."),
        EDWCEditorValidationRemediation::BuildAction,
        Spec.Action,
        bFailed);
}
}

void FDWCRuntimeValidationEvaluator::AppendToSnapshot(
    const FDWCEditorValidationEvaluationContext& Context,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const UWetClothingAsset& Asset = Context.Asset;
    const FDWCAssetBakeState& BakeState = Asset.GetBakeState();
    const bool bCPUValid = Context.bCPUBackendEnabled && Context.RuntimeMesh != nullptr &&
        (Context.bDeepValidation
            ? Asset.IsPrecomputedSimulationDataValidForMesh(Context.RuntimeMesh)
            : Asset.IsPrecomputedSimulationDataMetadataValidForMesh(Context.RuntimeMesh));
    const bool bGPUValid = Context.bGPUBackendEnabled && Context.RuntimeMesh != nullptr &&
        (Context.bDeepValidation
            ? Asset.IsGPURuntimeDataValidForMesh(Context.RuntimeMesh, Context.RuntimeLODIndex)
            : Asset.IsGPURuntimeDataMetadataValidForMesh(Context.RuntimeMesh, Context.RuntimeLODIndex));
    const bool bGPUMapsValid = Context.bGPUBackendEnabled && Context.RuntimeMesh != nullptr &&
        (Context.bDeepValidation
            ? Asset.IsGPUWetMapDataValidForMesh(Context.RuntimeMesh, Context.RuntimeLODIndex)
            : Asset.IsGPUWetMapDataMetadataValidForMesh(Context.RuntimeMesh, Context.RuntimeLODIndex));

    EvaluateTarget(
        Context,
        {
            EDWCEditorValidationDomain::RuntimeCPU,
            EDWCEditorBuildAction::BuildCPURuntimeData,
            TEXT("CPURuntimeData"),
            NSLOCTEXT("DWCRuntimeValidation", "CPUTitle", "CPU Runtime Data"),
            DWCBakeOutput::CPURuntimeData,
            Context.bCPUBackendEnabled,
            Context.RuntimeMesh != nullptr && Context.bOriginalUVTopologyReady,
            bCPUValid,
            Asset.HasCPURuntimeDataPayload(),
            BakeState.CPURuntimeData
        },
        InOutSnapshot);

    EvaluateTarget(
        Context,
        {
            EDWCEditorValidationDomain::RuntimeGPU,
            EDWCEditorBuildAction::BuildGPURuntimeData,
            TEXT("GPURuntimeData"),
            NSLOCTEXT("DWCRuntimeValidation", "GPUTitle", "GPU Runtime Data"),
            DWCBakeOutput::GPURuntimeData,
            Context.bGPUBackendEnabled,
            Context.RuntimeMesh != nullptr && Context.bDataUVReady,
            bGPUValid,
            Asset.HasGPURuntimeDataPayload(),
            BakeState.GPURuntimeData
        },
        InOutSnapshot);

    EvaluateTarget(
        Context,
        {
            EDWCEditorValidationDomain::GPUSimulationMap,
            EDWCEditorBuildAction::BuildGPURuntimeData,
            TEXT("GPUMaps"),
            NSLOCTEXT("DWCRuntimeValidation", "GPUMapsTitle", "GPU Runtime Maps"),
            DWCBakeOutput::GPUMaps,
            Context.bGPUBackendEnabled,
            Context.RuntimeMesh != nullptr && Context.bDataUVReady,
            bGPUMapsValid,
            Asset.HasGPUMapDataPayload(),
            BakeState.GPUMaps
        },
        InOutSnapshot);
}
