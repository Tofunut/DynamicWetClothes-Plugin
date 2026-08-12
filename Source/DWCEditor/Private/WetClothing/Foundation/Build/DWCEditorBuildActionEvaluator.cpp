//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildActionEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "UObject/Package.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"

namespace
{
    EDWCEditorBuildActionState ConvertBakeStatus(const EDWCBakeStatus Status)
    {
        switch (Status)
        {
        case EDWCBakeStatus::Disabled:
            return EDWCEditorBuildActionState::Unavailable;
        case EDWCBakeStatus::Required:
        case EDWCBakeStatus::OutOfDate:
            return EDWCEditorBuildActionState::Required;
        case EDWCBakeStatus::Valid:
        case EDWCBakeStatus::ValidWithDiagnostics:
            return EDWCEditorBuildActionState::UpToDate;
        case EDWCBakeStatus::Failed:
            return EDWCEditorBuildActionState::Failed;
        default:
            return EDWCEditorBuildActionState::Unavailable;
        }
    }

    EDWCEditorBuildActionState CombineBuildStates(
        const EDWCEditorBuildActionState A,
        const EDWCEditorBuildActionState B)
    {
        if (A == EDWCEditorBuildActionState::Failed || B == EDWCEditorBuildActionState::Failed)
        {
            return EDWCEditorBuildActionState::Failed;
        }
        if (A == EDWCEditorBuildActionState::Required || B == EDWCEditorBuildActionState::Required)
        {
            return EDWCEditorBuildActionState::Required;
        }
        if (A == EDWCEditorBuildActionState::Blocked || B == EDWCEditorBuildActionState::Blocked)
        {
            return EDWCEditorBuildActionState::Blocked;
        }
        if (A == EDWCEditorBuildActionState::Running || B == EDWCEditorBuildActionState::Running)
        {
            return EDWCEditorBuildActionState::Running;
        }
        if (A == EDWCEditorBuildActionState::UpToDate && B == EDWCEditorBuildActionState::UpToDate)
        {
            return EDWCEditorBuildActionState::UpToDate;
        }
        return EDWCEditorBuildActionState::Unavailable;
    }

    FDWCEditorBuildActionStatus MakeStatus(
        const EDWCEditorBuildAction Action,
        const EDWCEditorBuildActionState State,
        FString Reason = {})
    {
        FDWCEditorBuildActionStatus Result;
        Result.Action = Action;
        Result.State = State;
        Result.Reason = MoveTemp(Reason);
        return Result;
    }

    void AddStatus(
        FDWCEditorBuildStatusSnapshot& Snapshot,
        const EDWCEditorBuildAction Action,
        const EDWCEditorBuildActionState State,
        FString Reason = {})
    {
        Snapshot.Actions.Add(Action, MakeStatus(Action, State, MoveTemp(Reason)));
    }

    void ApplyDependencyBlocking(FDWCEditorBuildStatusSnapshot& Snapshot)
    {
        for (const FDWCEditorBuildActionDescriptor& Descriptor : FDWCEditorBuildActionRegistry::GetDescriptors())
        {
            FDWCEditorBuildActionStatus* Status = Snapshot.Actions.Find(Descriptor.Action);
            if (Status == nullptr ||
                Status->State == EDWCEditorBuildActionState::Unavailable ||
                Status->State == EDWCEditorBuildActionState::UpToDate ||
                Status->State == EDWCEditorBuildActionState::Running)
            {
                continue;
            }

            bool bHasUnavailablePrerequisite = false;
            for (const FDWCEditorBuildActionDependency& Dependency : Descriptor.Dependencies)
            {
                if (Dependency.Kind != EDWCEditorBuildDependencyKind::HardPrerequisite)
                {
                    continue;
                }
                const FDWCEditorBuildActionStatus* Prerequisite = Snapshot.Actions.Find(Dependency.Action);
                if (Prerequisite == nullptr ||
                    Prerequisite->State != EDWCEditorBuildActionState::UpToDate)
                {
                    Status->BlockingActions.AddUnique(Dependency.Action);
                    bHasUnavailablePrerequisite |= Prerequisite == nullptr ||
                        Prerequisite->State == EDWCEditorBuildActionState::Unavailable ||
                        Prerequisite->State == EDWCEditorBuildActionState::Blocked;
                }
            }

            if (!Status->BlockingActions.IsEmpty())
            {
                if (bHasUnavailablePrerequisite)
                {
                    Status->State = EDWCEditorBuildActionState::Blocked;
                }
                if (Status->Reason.IsEmpty())
                {
                    Status->Reason = TEXT("Complete the required build prerequisites first.");
                }
            }
        }
    }
}

FDWCEditorBuildEvaluationInput FDWCEditorBuildActionEvaluator::CaptureAssetState(
    const UWetClothingAsset& Asset,
    const EDWCEditorBuildSurfaceMode SurfaceMode,
    FDWCEditorBuildEvaluationInput ServiceState)
{
    const FDWCAssetBakeState& BakeState = Asset.GetBakeState();
    const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();

    ServiceState.bHasAsset = true;
    ServiceState.bAssetDirty = Asset.GetOutermost() != nullptr && Asset.GetOutermost()->IsDirty();
    ServiceState.bHasRuntimeMesh = Asset.GetRuntimeSkeletalMesh() != nullptr;
    ServiceState.bHasWettableSlots = Asset.HasAnyWettableMaterialSlot();
    ServiceState.bHasValidDataUV = Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex());
    ServiceState.bCPUBackendEnabled = Setup.bBuildCPUVertexSimulationData;
    ServiceState.bGPUBackendEnabled = Setup.bBuildGPUWetnessMapSimulationData;
    if (!ServiceState.bWrinkleTargetStateProvided)
    {
        ServiceState.bHasWrinkleContent = Asset.HasWrinkleBakeContent();
    }
    if (!ServiceState.bTransparencyTargetStateProvided)
    {
        ServiceState.bHasTransparencyContent = Asset.HasTransparencyBakeContent();
    }
    ServiceState.DataUVState = ConvertBakeStatus(BakeState.GeneratedDataUV);
    ServiceState.CPURuntimeDataState = ConvertBakeStatus(BakeState.CPURuntimeData);
    ServiceState.GPURuntimeDataState = ConvertBakeStatus(BakeState.GPURuntimeData);
    ServiceState.GPUMapsState = ConvertBakeStatus(BakeState.GPUMaps);
    if (!ServiceState.bWrinkleTargetStateProvided)
    {
        ServiceState.WrinkleTexturesState = ConvertBakeStatus(BakeState.WrinkleMaps);
    }
    if (!ServiceState.bTransparencyTargetStateProvided)
    {
        ServiceState.TransparencyTexturesState = ConvertBakeStatus(BakeState.TransparencyMaps);
    }
    ServiceState.SurfaceMode = SurfaceMode;
    return ServiceState;
}

FDWCEditorBuildStatusSnapshot FDWCEditorBuildActionEvaluator::Evaluate(
    const FDWCEditorBuildEvaluationInput& Input)
{
    FDWCEditorBuildStatusSnapshot Snapshot;
    if (!Input.bHasAsset)
    {
        for (const FDWCEditorBuildActionDescriptor& Descriptor : FDWCEditorBuildActionRegistry::GetDescriptors())
        {
            AddStatus(Snapshot, Descriptor.Action, EDWCEditorBuildActionState::Unavailable,
                TEXT("The Wet Clothing Asset is unavailable."));
        }
        return Snapshot;
    }

    AddStatus(Snapshot, EDWCEditorBuildAction::SaveAsset,
        Input.bAssetDirty ? EDWCEditorBuildActionState::Required : EDWCEditorBuildActionState::UpToDate,
        Input.bAssetDirty ? TEXT("The asset contains unsaved changes.") : TEXT("The asset is saved."));

    AddStatus(Snapshot, EDWCEditorBuildAction::InitializeDataUV, Input.DataUVState,
        Input.bHasValidDataUV ? TEXT("The DWC data UV layout is ready.") : TEXT("The DWC data UV layout is missing or out of date."));

    AddStatus(Snapshot, EDWCEditorBuildAction::BuildCPURuntimeData,
        Input.bHasWettableSlots && Input.bCPUBackendEnabled
            ? Input.CPURuntimeDataState
            : EDWCEditorBuildActionState::Unavailable,
        !Input.bHasWettableSlots
            ? TEXT("No wettable material slots are configured.")
            : (!Input.bCPUBackendEnabled ? TEXT("CPU simulation data is disabled in Asset Setup.") : FString()));

    AddStatus(Snapshot, EDWCEditorBuildAction::BuildGPURuntimeData,
        Input.bHasWettableSlots && Input.bGPUBackendEnabled
            ? CombineBuildStates(Input.GPURuntimeDataState, Input.GPUMapsState)
            : EDWCEditorBuildActionState::Unavailable,
        !Input.bHasWettableSlots
            ? TEXT("No wettable material slots are configured.")
            : (!Input.bGPUBackendEnabled ? TEXT("GPU simulation data is disabled in Asset Setup.") : FString()));

    AddStatus(Snapshot, EDWCEditorBuildAction::BakeRenderProfileData,
        Input.bHasWettableSlots ? Input.RenderProfileState : EDWCEditorBuildActionState::Unavailable,
        Input.bHasWettableSlots ? Input.RenderProfileReason : TEXT("No wettable material slots are configured."));
    AddStatus(Snapshot, EDWCEditorBuildAction::GenerateMaterials,
        Input.bHasWettableSlots && (Input.bCPUBackendEnabled || Input.bGPUBackendEnabled)
            ? Input.GeneratedMaterialsState
            : EDWCEditorBuildActionState::Unavailable,
        Input.bHasWettableSlots ? Input.GeneratedMaterialsReason : TEXT("No wettable material slots are configured."));

    AddStatus(Snapshot, EDWCEditorBuildAction::BakeWrinkleTextures,
        Input.bHasWrinkleContent ? Input.WrinkleTexturesState : EDWCEditorBuildActionState::Unavailable,
        Input.bHasWrinkleContent ? Input.WrinkleTexturesReason : TEXT("No authored wrinkle content is available."));
    AddStatus(Snapshot, EDWCEditorBuildAction::BakeTransparencyTextures,
        Input.bHasTransparencyContent ? Input.TransparencyTexturesState : EDWCEditorBuildActionState::Unavailable,
        Input.bHasTransparencyContent ? Input.TransparencyTexturesReason : TEXT("No authored transparency content is available."));
    AddStatus(Snapshot, EDWCEditorBuildAction::RebakeAffectedTransparencyMaps,
        Input.bHasTransparencyContent ? Input.AffectedTransparencyState : EDWCEditorBuildActionState::Unavailable,
        Input.bHasTransparencyContent ? Input.AffectedTransparencyReason : TEXT("No authored transparency content is available."));

    if (FDWCEditorBuildActionStatus* Full = Snapshot.Actions.Find(
            EDWCEditorBuildAction::BakeTransparencyTextures))
    {
        Full->MaterialSlotIndices = Input.TransparencyMaterialSlotIndices;
        Full->LayerGuids = Input.TransparencyLayerGuids;
    }

    if (FDWCEditorBuildActionStatus* Wrinkle = Snapshot.Actions.Find(
            EDWCEditorBuildAction::BakeWrinkleTextures))
    {
        Wrinkle->MaterialSlotIndices = Input.WrinkleMaterialSlotIndices;
    }

    if (FDWCEditorBuildActionStatus* Affected = Snapshot.Actions.Find(
            EDWCEditorBuildAction::RebakeAffectedTransparencyMaps))
    {
        Affected->MaterialSlotIndices = Input.AffectedMaterialSlotIndices;
        Affected->LayerGuids = Input.AffectedLayerGuids;
        if (Affected->State == EDWCEditorBuildActionState::Required)
        {
            FDWCEditorBuildActionStatus& Full = Snapshot.Actions.FindChecked(
                EDWCEditorBuildAction::BakeTransparencyTextures);
            if (!Input.bTransparencyTargetStateProvided ||
                Full.State == EDWCEditorBuildActionState::UpToDate)
            {
                Full.State = EDWCEditorBuildActionState::UpToDate;
                Full.Reason = TEXT("All stale transparency outputs are covered by the affected rebake action.");
            }
        }
    }

    for (const EDWCEditorBuildAction RunningAction : Input.RunningActions)
    {
        if (FDWCEditorBuildActionStatus* Status = Snapshot.Actions.Find(RunningAction))
        {
            Status->State = EDWCEditorBuildActionState::Running;
            Status->Reason = TEXT("The build action is currently running.");
            Status->BlockingActions.Reset();
        }
    }
    ApplyDependencyBlocking(Snapshot);
    return Snapshot;
}
