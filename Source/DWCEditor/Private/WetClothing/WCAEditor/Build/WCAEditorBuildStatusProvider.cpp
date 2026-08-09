//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/Build/WCAEditorBuildStatusProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionEvaluator.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"
#include "WetClothing/WCAEditor/UI/SWCAEditorPanel.h"

namespace
{
    bool HasMaterialGenerationPrerequisites(const UWetClothingAsset& Asset, FString& OutReason)
    {
        OutReason.Reset();
        if (!Asset.HasAnyWettableMaterialSlot())
        {
            OutReason = TEXT("No wettable material slots are configured.");
            return false;
        }
        const FDWCWetClothingAssetSetupSettings& Setup = Asset.GetSetupSettings();
        if (!Setup.bBuildCPUVertexSimulationData && !Setup.bBuildGPUWetnessMapSimulationData)
        {
            OutReason = TEXT("Enable CPU or GPU simulation data in Asset Setup first.");
            return false;
        }
        USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
        if (RuntimeMesh == nullptr)
        {
            OutReason = TEXT("Prepared Mesh must be generated first.");
            return false;
        }
        if (!Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex()))
        {
            OutReason = TEXT("Initialize the DWC data UV layout first.");
            return false;
        }

        const TArray<FSkeletalMaterial>& Materials = RuntimeMesh->GetMaterials();
        for (const FWetClothingAuthoredMaterialSlot& SlotState :
             Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
        {
            if (!SlotState.bIsWettableSlot || SlotState.MaterialSlotIndex == INDEX_NONE)
            {
                continue;
            }
            if (!Materials.IsValidIndex(SlotState.MaterialSlotIndex) ||
                FWCAMaterialGenerator::ResolveGeneratedMaterialSource(
                    &Asset,
                    SlotState.MaterialSlotIndex,
                    Materials[SlotState.MaterialSlotIndex].MaterialInterface) == nullptr)
            {
                OutReason = FString::Printf(
                    TEXT("Wettable slot %d has no usable source material."),
                    SlotState.MaterialSlotIndex);
                return false;
            }
        }
        return true;
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
    const bool bDeepValidation)
{
    FDWCEditorBuildEvaluationInput ServiceState;
    ServiceState.SurfaceMode = SurfaceMode;

    if (Asset.HasAnyWettableMaterialSlot())
    {
        ServiceState.RenderProfileState =
            FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(&Asset, nullptr)
                ? EDWCEditorBuildActionState::Required
                : EDWCEditorBuildActionState::UpToDate;
    }

    FString MaterialPrerequisiteReason;
    if (Asset.GetRuntimeSkeletalMesh() != nullptr &&
        !Asset.HasValidDataUVForLOD(Asset.GetSimulationLODIndex()))
    {
        ServiceState.GeneratedMaterialsState = EDWCEditorBuildActionState::Required;
        ServiceState.GeneratedMaterialsReason =
            TEXT("Generate materials after the DWC data UV layout is initialized.");
    }
    else if (HasMaterialGenerationPrerequisites(Asset, MaterialPrerequisiteReason))
    {
        TArray<FString> MaterialMessages;
        if (bDeepValidation)
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrides(&Asset, MaterialMessages);
        }
        else
        {
            FWCAMaterialGenerator::ValidateGeneratedMaterialOverrideReferences(&Asset, MaterialMessages);
        }
        ServiceState.GeneratedMaterialsState = MaterialMessages.IsEmpty()
            ? EDWCEditorBuildActionState::UpToDate
            : EDWCEditorBuildActionState::Required;
        if (!MaterialMessages.IsEmpty())
        {
            ServiceState.GeneratedMaterialsReason = MaterialMessages[0];
        }
    }
    else if (Asset.HasAnyWettableMaterialSlot())
    {
        ServiceState.GeneratedMaterialsState = EDWCEditorBuildActionState::Blocked;
        ServiceState.GeneratedMaterialsReason = MoveTemp(MaterialPrerequisiteReason);
    }

    const EDWCBakeStatus TransparencyStatus = Asset.GetBakeState().TransparencyMaps;
    const bool bTransparencyRequiresBuild =
        TransparencyStatus == EDWCBakeStatus::Required ||
        TransparencyStatus == EDWCBakeStatus::OutOfDate;
    if (Asset.HasTransparencyBakeContent())
    {
        ServiceState.AffectedTransparencyState = EDWCEditorBuildActionState::UpToDate;
        ServiceState.AffectedTransparencyReason = TEXT("No wrinkle-dependent transparency outputs require a partial rebake.");
    }
    if (Asset.HasTransparencyBakeContent() && bTransparencyRequiresBuild)
    {
        TArray<FDWCTransparencyAffectedRebakeCandidate> Candidates;
        FDWCTransparencyAffectedStage4Rebake::CollectCandidates(
            Asset,
            TConstArrayView<int32>(),
            Candidates);
        bool bHasNonEligibleStaleCandidate = false;
        for (const FDWCTransparencyAffectedRebakeCandidate& Candidate : Candidates)
        {
            if (Candidate.IsEligible())
            {
                ServiceState.AffectedMaterialSlotIndices.AddUnique(Candidate.MaterialSlotIndex);
                ServiceState.AffectedLayerGuids.AddUnique(Candidate.LayerGuid);
            }
            else if (Candidate.Status != EDWCTransparencyAffectedRebakeStatus::AlreadyCurrent)
            {
                bHasNonEligibleStaleCandidate = true;
            }
        }
        if (!ServiceState.AffectedLayerGuids.IsEmpty() && !bHasNonEligibleStaleCandidate)
        {
            ServiceState.AffectedTransparencyState = EDWCEditorBuildActionState::Required;
            ServiceState.AffectedTransparencyReason = TEXT("Wrinkle coverage changed for one or more transparency outputs.");
        }
        else if (bHasNonEligibleStaleCandidate)
        {
            ServiceState.AffectedTransparencyState = EDWCEditorBuildActionState::Unavailable;
            ServiceState.AffectedTransparencyReason = TEXT("At least one stale transparency layer requires a full transparency bake.");
        }
    }

    if (EditorPanel != nullptr)
    {
        for (const EDWCEditorBuildAction Action : EditorPanel->GetRunningBuildActions())
        {
            ServiceState.RunningActions.Add(Action);
        }
    }

    FDWCEditorBuildEvaluationInput Input = FDWCEditorBuildActionEvaluator::CaptureAssetState(
        Asset, SurfaceMode, MoveTemp(ServiceState));
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
