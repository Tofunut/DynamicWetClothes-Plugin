// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCWrinkleValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Build/DWCWrinkleBuildTargetResolver.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace
{
FDWCEditorValidationTargetKey MakeTarget(
    const int32 MaterialSlotIndex,
    const FName SubResource)
{
    FDWCEditorValidationTargetKey Key;
    Key.Domain = EDWCEditorValidationDomain::Wrinkle;
    Key.MaterialSlotIndex = MaterialSlotIndex;
    Key.SubResource = SubResource;
    return Key;
}

FText SlotContext(const int32 MaterialSlotIndex)
{
    return FText::Format(
        NSLOCTEXT("DWCWrinkleValidation", "SlotContext", "Material Slot {0}"),
        FText::AsNumber(MaterialSlotIndex));
}
}

void FDWCWrinkleValidationEvaluator::AppendToSnapshot(
    const UWetClothingAsset& Asset,
    const bool bDeepValidation,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    using namespace DWCEditorValidation;
    const FDWCWrinkleBuildTargetSnapshot Targets =
        FDWCWrinkleBuildTargetResolver::Resolve(Asset, bDeepValidation);

    SetActionState(
        InOutSnapshot,
        EDWCEditorBuildAction::BakeWrinkleTextures,
        Targets.BakeState);

    for (const FDWCWrinkleBuildTarget& Target : Targets.Targets)
    {
        InOutSnapshot.Nodes.Reserve(InOutSnapshot.Nodes.Num() + 3);
        FDWCEditorValidationNode& SourceNode = FindOrAddNode(
            InOutSnapshot, MakeTarget(Target.MaterialSlotIndex, TEXT("Source")));
        FDWCEditorValidationNode& RuntimeNode = FindOrAddNode(
            InOutSnapshot, MakeTarget(Target.MaterialSlotIndex, TEXT("RuntimeNormal")));
        FDWCEditorValidationNode& CoverageNode = FindOrAddNode(
            InOutSnapshot, MakeTarget(Target.MaterialSlotIndex, TEXT("CoverageMask")));

        if (Target.Requirement == EDWCWrinkleBuildRequirement::ManualRepair)
        {
            SourceNode.Intent = EDWCEditorValidationIntentState::Enabled;
            RuntimeNode.Intent = EDWCEditorValidationIntentState::Enabled;
            CoverageNode.Intent = Target.SourceMode == EDWCWrinkleBuildSourceMode::BakedAuthoring
                ? EDWCEditorValidationIntentState::Enabled
                : EDWCEditorValidationIntentState::NotApplicable;
            SourceNode.Input = EDWCEditorValidationInputState::Invalid;
            SourceNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            RuntimeNode.Input = EDWCEditorValidationInputState::Invalid;
            RuntimeNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            CoverageNode.Input = Target.SourceMode == EDWCWrinkleBuildSourceMode::BakedAuthoring
                ? EDWCEditorValidationInputState::Invalid
                : EDWCEditorValidationInputState::Unknown;
            CoverageNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            AddDiagnostic(
                InOutSnapshot,
                SourceNode,
                Target.DiagnosticCode.IsNone() ? FName(TEXT("WrinkleSourceInvalid")) : Target.DiagnosticCode,
                EDWCEditorValidationSeverity::Error,
                NSLOCTEXT("DWCWrinkleValidation", "Title", "Wrinkle Textures"),
                NSLOCTEXT("DWCWrinkleValidation", "ManualFix", "Manual Fix"),
                FText::FromString(Target.Detail),
                NSLOCTEXT("DWCWrinkleValidation", "FixSource",
                    "Fix this material slot's wrinkle source in Wrinkle Editor."),
                EDWCEditorValidationRemediation::Manual,
                {},
                false,
                SlotContext(Target.MaterialSlotIndex));
            continue;
        }

        if (!Target.bConfigured)
        {
            SourceNode.Intent = EDWCEditorValidationIntentState::NotConfigured;
            RuntimeNode.Intent = EDWCEditorValidationIntentState::NotConfigured;
            CoverageNode.Intent = EDWCEditorValidationIntentState::NotConfigured;
            SourceNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            RuntimeNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            CoverageNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            continue;
        }

        SourceNode.Intent = EDWCEditorValidationIntentState::Enabled;
        RuntimeNode.Intent = EDWCEditorValidationIntentState::Enabled;
        CoverageNode.Intent = Target.SourceMode == EDWCWrinkleBuildSourceMode::BakedAuthoring
            ? EDWCEditorValidationIntentState::Enabled
            : EDWCEditorValidationIntentState::NotApplicable;

        SourceNode.Input = EDWCEditorValidationInputState::Valid;
        SourceNode.Artifact = EDWCEditorValidationArtifactState::Current;
        RuntimeNode.Input = EDWCEditorValidationInputState::Valid;

        if (Target.SourceMode == EDWCWrinkleBuildSourceMode::CustomTexture)
        {
            RuntimeNode.Artifact = EDWCEditorValidationArtifactState::Current;
            CoverageNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            continue;
        }

        CoverageNode.Input = EDWCEditorValidationInputState::Valid;
        RuntimeNode.Artifact = Target.bOutputCurrent
            ? EDWCEditorValidationArtifactState::Current
            : Target.bHasBakedNormal
                ? EDWCEditorValidationArtifactState::Stale
                : EDWCEditorValidationArtifactState::Missing;
        CoverageNode.Artifact = Target.bOutputCurrent
            ? EDWCEditorValidationArtifactState::Current
            : Target.bHasCoverageMask
                ? EDWCEditorValidationArtifactState::Stale
                : EDWCEditorValidationArtifactState::Missing;
        if (Target.bSavePending && Target.bOutputCurrent)
        {
            RuntimeNode.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            CoverageNode.Persistence = EDWCEditorValidationPersistenceState::SavePending;
        }

        if (!Target.bOutputCurrent)
        {
            FDWCEditorValidationNode& DiagnosticNode =
                !Target.bHasBakedNormal ? RuntimeNode : CoverageNode;
            AddDiagnostic(
                InOutSnapshot,
                DiagnosticNode,
                Target.DiagnosticCode.IsNone() ? FName(TEXT("WrinkleOutputStale")) : Target.DiagnosticCode,
                EDWCEditorValidationSeverity::Warning,
                NSLOCTEXT("DWCWrinkleValidation", "Title", "Wrinkle Textures"),
                Target.bHasBakedNormal || Target.bHasCoverageMask
                    ? NSLOCTEXT("DWCWrinkleValidation", "OutOfDate", "Out of Date")
                    : NSLOCTEXT("DWCWrinkleValidation", "NotBaked", "Not Baked"),
                FText::FromString(Target.Detail),
                NSLOCTEXT("DWCWrinkleValidation", "Bake",
                    "Use Build for Runtime > Bake Wrinkle Textures."),
                EDWCEditorValidationRemediation::BuildAction,
                EDWCEditorBuildAction::BakeWrinkleTextures,
                Targets.BakeState == EDWCEditorBuildActionState::Failed,
                SlotContext(Target.MaterialSlotIndex));
        }
    }
}
