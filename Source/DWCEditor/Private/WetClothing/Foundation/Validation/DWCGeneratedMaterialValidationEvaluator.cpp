// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCGeneratedMaterialValidationEvaluator.h"

#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace
{
bool IsManualInputIssue(const FName Code)
{
    return Code == TEXT("RuntimeMeshMissing") ||
           Code == TEXT("SlotOutOfRange") ||
           Code == TEXT("SourceMaterialMissing");
}

FText ContextLabel(const int32 MaterialSlotIndex)
{
    return MaterialSlotIndex == INDEX_NONE
        ? FText::GetEmpty()
        : FText::Format(
            NSLOCTEXT("DWCGeneratedMaterialValidation", "SlotContext", "Material Slot {0}"),
            FText::AsNumber(MaterialSlotIndex));
}
}

void FDWCGeneratedMaterialValidationEvaluator::AppendToSnapshot(
    const FDWCEditorValidationEvaluationContext& Context,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const FDWCEditorValidationTargetKey GlobalKey{
        EDWCEditorValidationDomain::GeneratedMaterial,
        INDEX_NONE,
        FGuid(),
        TEXT("GlobalContract")};

    if (!Context.bHasWettableSlots ||
        (!Context.bCPUBackendEnabled && !Context.bGPUBackendEnabled))
    {
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(InOutSnapshot, GlobalKey);
        Node.Intent = EDWCEditorValidationIntentState::NotApplicable;
        Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::GenerateMaterials,
            EDWCEditorBuildActionState::Unavailable);
        return;
    }

    if (Context.RuntimeMesh == nullptr || !Context.bDataUVReady)
    {
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(InOutSnapshot, GlobalKey);
        Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
        Node.Artifact = EDWCEditorValidationArtifactState::Missing;
        const EDWCEditorBuildAction BlockingAction = EDWCEditorBuildAction::InitializeDataUV;
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::GenerateMaterials,
            EDWCEditorBuildActionState::Blocked,
            &Node.Key,
            MakeArrayView(&BlockingAction, 1));
        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            Node,
            TEXT("GeneratedMaterialPrerequisite"),
            EDWCEditorValidationSeverity::Warning,
            NSLOCTEXT("DWCGeneratedMaterialValidation", "SetupTitle", "Generated Material Setup"),
            NSLOCTEXT("DWCGeneratedMaterialValidation", "Blocked", "Blocked"),
            NSLOCTEXT("DWCGeneratedMaterialValidation", "PrerequisiteDetail", "Generated materials require a runtime mesh and a current DWC data UV layout."),
            NSLOCTEXT("DWCGeneratedMaterialValidation", "PrerequisiteAction", "Initialize the DWC data UV layout, then generate materials."),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::GenerateMaterials);
        return;
    }

    for (const int32 SlotIndex : Context.WettableMaterialSlotIndices)
    {
        FDWCEditorValidationNode& Node = DWCEditorValidation::FindOrAddNode(
            InOutSnapshot,
            {EDWCEditorValidationDomain::GeneratedMaterial, SlotIndex});
        Node.Intent = EDWCEditorValidationIntentState::Enabled;
        Node.Input = EDWCEditorValidationInputState::Valid;
        Node.Artifact = EDWCEditorValidationArtifactState::Current;
    }

    TArray<FWCAGeneratedMaterialValidationIssue> Issues;
    FWCAMaterialGenerator::ValidateGeneratedMaterialOverridesStructured(
        &Context.Asset,
        Context.bDeepValidation,
        Issues);
    if (Issues.IsEmpty())
    {
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::GenerateMaterials,
            EDWCEditorBuildActionState::UpToDate);
        return;
    }

    bool bHasManualBlocker = false;
    for (const FWCAGeneratedMaterialValidationIssue& Issue : Issues)
    {
        const FDWCEditorValidationTargetKey Key{
            EDWCEditorValidationDomain::GeneratedMaterial,
            Issue.MaterialSlotIndex,
            FGuid(),
            Issue.MaterialSlotIndex == INDEX_NONE ? Issue.Code : NAME_None};
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(InOutSnapshot, Key);
        Node.Intent = EDWCEditorValidationIntentState::Enabled;

        const bool bManual = IsManualInputIssue(Issue.Code);
        bHasManualBlocker |= bManual;
        if (bManual)
        {
            Node.Input = EDWCEditorValidationInputState::Invalid;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
        }
        else
        {
            Node.Input = EDWCEditorValidationInputState::Valid;
            Node.Artifact = EDWCEditorValidationArtifactState::Stale;
        }

        DWCEditorValidation::AddDiagnostic(
            InOutSnapshot,
            Node,
            Issue.Code,
            Issue.bFailed ? EDWCEditorValidationSeverity::Error : EDWCEditorValidationSeverity::Warning,
            Issue.MaterialSlotIndex == INDEX_NONE
                ? NSLOCTEXT("DWCGeneratedMaterialValidation", "FunctionTitle", "Generated Material Functions")
                : NSLOCTEXT("DWCGeneratedMaterialValidation", "SlotTitle", "Generated Material Setup"),
            bManual
                ? NSLOCTEXT("DWCGeneratedMaterialValidation", "ManualFix", "Manual Fix")
                : NSLOCTEXT("DWCGeneratedMaterialValidation", "OutOfDate", "Out of Date"),
            FText::FromString(Issue.Message),
            bManual
                ? NSLOCTEXT("DWCGeneratedMaterialValidation", "ManualAction", "Fix the source mesh or source material first.")
                : NSLOCTEXT("DWCGeneratedMaterialValidation", "GenerateAction", "Use Build for Runtime > Generate Materials."),
            bManual
                ? EDWCEditorValidationRemediation::Manual
                : EDWCEditorValidationRemediation::BuildAction,
            bManual ? TOptional<EDWCEditorBuildAction>() : TOptional<EDWCEditorBuildAction>(EDWCEditorBuildAction::GenerateMaterials),
            Issue.bFailed,
            ContextLabel(Issue.MaterialSlotIndex));
    }

    if (bHasManualBlocker)
    {
        DWCEditorValidation::SetActionState(
            InOutSnapshot,
            EDWCEditorBuildAction::GenerateMaterials,
            EDWCEditorBuildActionState::Blocked);
    }
}
