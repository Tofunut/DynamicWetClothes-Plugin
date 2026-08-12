//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/Build/WCAEditorBuildStatusProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionEvaluator.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Foundation/Build/DWCWrinkleBuildTargetResolver.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/Foundation/Validation/DWCGeneratedMaterialValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRuntimeValidationEvaluator.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"

namespace
{
    FString FindActionReason(
        const FWCAEditorValidationSnapshot& Snapshot,
        const EDWCEditorBuildAction Action)
    {
        if (const FDWCEditorValidationDiagnostic* Diagnostic = Snapshot.Diagnostics.FindByPredicate(
                [Action](const FDWCEditorValidationDiagnostic& Candidate)
                {
                    return Candidate.SuggestedAction.IsSet() &&
                           Candidate.SuggestedAction.GetValue() == Action;
                }))
        {
            return Diagnostic->Presentation.Detail.ToString();
        }
        return FString();
    }

    bool HasAnyPendingBakeOutput(const UWetClothingAsset& Asset)
    {
        constexpr int32 AllOutputs =
            DWCBakeOutput::GeneratedDataUV |
            DWCBakeOutput::OriginalUVTopology |
            DWCBakeOutput::CPURuntimeData |
            DWCBakeOutput::GPURuntimeData |
            DWCBakeOutput::GPUMaps |
            DWCBakeOutput::WrinkleMaps |
            DWCBakeOutput::TransparencyMaps;
        return Asset.IsBakeOutputSavePending(AllOutputs);
    }
}

FDWCEditorBuildStatusSnapshot FWCAEditorBuildStatusProvider::BuildSnapshot(
    UWetClothingAsset& Asset,
    const SWCAEditorPanel* EditorPanel,
    const EDWCEditorBuildSurfaceMode SurfaceMode,
    const bool bDeepValidation,
    const FWCAEditorValidationSnapshot* ValidationSnapshot)
{
    FDWCEditorBuildEvaluationInput ServiceState;
    ServiceState.SurfaceMode = SurfaceMode;

    FWCAEditorValidationSnapshot LocalValidationSnapshot;
    if (ValidationSnapshot == nullptr)
    {
        const FDWCEditorValidationEvaluationContext ValidationContext(Asset, bDeepValidation);
        FDWCGeneratedMaterialValidationEvaluator::AppendToSnapshot(
            ValidationContext,
            LocalValidationSnapshot);
        FDWCRuntimeValidationEvaluator::AppendToSnapshot(
            ValidationContext,
            LocalValidationSnapshot);
        ValidationSnapshot = &LocalValidationSnapshot;
    }

    if (Asset.HasAnyWettableMaterialSlot())
    {
        ServiceState.RenderProfileState =
            FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(&Asset, nullptr)
                ? EDWCEditorBuildActionState::Required
                : EDWCEditorBuildActionState::UpToDate;
    }

    if (const FDWCEditorValidationActionState* MaterialAction =
            ValidationSnapshot->FindAction(EDWCEditorBuildAction::GenerateMaterials))
    {
        ServiceState.GeneratedMaterialsState = MaterialAction->State;
        ServiceState.GeneratedMaterialsReason = FindActionReason(
            *ValidationSnapshot,
            EDWCEditorBuildAction::GenerateMaterials);
    }

    const FDWCTransparencyBuildTargetSnapshot TransparencyTargets =
        FDWCTransparencyBuildTargetResolver::Resolve(Asset, bDeepValidation);
    ServiceState.bTransparencyTargetStateProvided = true;
    ServiceState.bHasTransparencyContent = TransparencyTargets.HasEnabledLayers();
    ServiceState.TransparencyTexturesState = TransparencyTargets.FullBakeState;
    ServiceState.TransparencyTexturesReason = TransparencyTargets.FullBakeReason;
    ServiceState.AffectedTransparencyState = TransparencyTargets.AffectedStage4State;
    ServiceState.AffectedTransparencyReason = TransparencyTargets.AffectedStage4Reason;
    for (const FDWCTransparencyBuildTarget& Target : TransparencyTargets.Targets)
    {
        if (Target.Requirement == EDWCTransparencyBuildRequirement::FullBake && Target.IsBuildable())
        {
            ServiceState.TransparencyMaterialSlotIndices.AddUnique(Target.MaterialSlotIndex);
            ServiceState.TransparencyLayerGuids.AddUnique(Target.LayerGuid);
        }
        else if (Target.Requirement == EDWCTransparencyBuildRequirement::AffectedStage4 &&
                 Target.IsBuildable())
        {
            ServiceState.AffectedMaterialSlotIndices.AddUnique(Target.MaterialSlotIndex);
            ServiceState.AffectedLayerGuids.AddUnique(Target.LayerGuid);
        }
    }

    const FDWCWrinkleBuildTargetSnapshot WrinkleTargets =
        FDWCWrinkleBuildTargetResolver::Resolve(Asset, bDeepValidation);
    ServiceState.bWrinkleTargetStateProvided = true;
    ServiceState.bHasWrinkleContent = WrinkleTargets.HasBakedAuthoringTargets();
    ServiceState.WrinkleTexturesState = WrinkleTargets.BakeState;
    ServiceState.WrinkleTexturesReason = WrinkleTargets.BakeReason;
    WrinkleTargets.CollectBuildMaterialSlots(ServiceState.WrinkleMaterialSlotIndices);

    if (EditorPanel != nullptr)
    {
        for (const EDWCEditorBuildAction Action : EditorPanel->GetRunningBuildActions())
        {
            ServiceState.RunningActions.Add(Action);
        }
    }

    FDWCEditorBuildEvaluationInput Input = FDWCEditorBuildActionEvaluator::CaptureAssetState(
        Asset, SurfaceMode, MoveTemp(ServiceState));
    if (const FDWCEditorValidationActionState* CPUAction =
            ValidationSnapshot->FindAction(EDWCEditorBuildAction::BuildCPURuntimeData))
    {
        Input.CPURuntimeDataState = CPUAction->State;
    }
    if (const FDWCEditorValidationActionState* GPUAction =
            ValidationSnapshot->FindAction(EDWCEditorBuildAction::BuildGPURuntimeData))
    {
        Input.GPURuntimeDataState = GPUAction->State;
        Input.GPUMapsState = GPUAction->State;
    }
    Input.bAssetDirty |= HasAnyPendingBakeOutput(Asset);
    if (!Input.bHasRuntimeMesh)
    {
        Input.DataUVState = EDWCEditorBuildActionState::Unavailable;
    }
    else if (!Input.bHasValidDataUV && Input.DataUVState == EDWCEditorBuildActionState::UpToDate)
    {
        Input.DataUVState = EDWCEditorBuildActionState::Required;
    }
    return FDWCEditorBuildActionEvaluator::Evaluate(Input);
}
