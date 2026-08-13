// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCTransparencyLayerValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"

namespace
{
FDWCEditorValidationTargetKey MakeTarget(const FDWCTransparencyBuildTarget& Target)
{
    FDWCEditorValidationTargetKey Key;
    Key.Domain = EDWCEditorValidationDomain::Transparency;
    Key.MaterialSlotIndex = Target.MaterialSlotIndex;
    Key.LayerGuid = Target.LayerGuid;
    return Key;
}

EDWCEditorValidationIntentState ConvertIntent(const FDWCTransparencyBuildTarget& Target)
{
    if (!Target.LayerGuid.IsValid())
    {
        return EDWCEditorValidationIntentState::NotConfigured;
    }
    switch (Target.Intent)
    {
    case EDWCTransparencyLayerIntent::Draft:
        return EDWCEditorValidationIntentState::Draft;
    case EDWCTransparencyLayerIntent::Disabled:
        return EDWCEditorValidationIntentState::Disabled;
    case EDWCTransparencyLayerIntent::Enabled:
    default:
        return EDWCEditorValidationIntentState::Enabled;
    }
}

void RequireAction(
    FWCAEditorValidationSnapshot& Snapshot,
    const EDWCEditorBuildAction Action,
    const FDWCEditorValidationTargetKey& Target)
{
    FDWCEditorValidationActionState& State = Snapshot.Actions.FindOrAdd(Action);
    State.Action = Action;
    if (State.State != EDWCEditorBuildActionState::Failed)
    {
        State.State = EDWCEditorBuildActionState::Required;
    }
    State.Targets.AddUnique(Target);
}

void AddDiagnostic(
    FWCAEditorValidationSnapshot& Snapshot,
    FDWCEditorValidationNode& Node,
    const FName Code,
    const EDWCEditorValidationSeverity Severity,
    const FText& Status,
    const FString& Detail,
    const FText& RequiredAction,
    const EDWCEditorValidationRemediation Remediation,
    const TOptional<EDWCEditorBuildAction> SuggestedAction = {},
    const bool bFailed = false,
    const int32 OwnedBakeOutput = 0)
{
    FDWCEditorValidationDiagnostic& Diagnostic = Snapshot.Diagnostics.AddDefaulted_GetRef();
    Diagnostic.Code = Code;
    Diagnostic.Severity = Severity;
    Diagnostic.Target = Node.Key;
    Diagnostic.Remediation = Remediation;
    Diagnostic.SuggestedAction = SuggestedAction;
    Diagnostic.Presentation.Title = NSLOCTEXT(
        "DWCTransparencyLayerValidation", "TransparencyTexturesTitle", "Transparency Textures");
    Diagnostic.Presentation.Status = Status;
    Diagnostic.Presentation.Detail = FText::FromString(Detail);
    Diagnostic.Presentation.RequiredAction = RequiredAction;
    Diagnostic.Presentation.ContextLabel = Node.Key.MaterialSlotIndex != INDEX_NONE
        ? FText::Format(
            NSLOCTEXT("DWCTransparencyLayerValidation", "SlotContext", "Material Slot {0}"),
            FText::AsNumber(Node.Key.MaterialSlotIndex))
        : FText::GetEmpty();
    Diagnostic.bFailed = bFailed;
    Diagnostic.OwnedBakeOutput = OwnedBakeOutput;
    Node.DiagnosticIndices.Add(Snapshot.Diagnostics.Num() - 1);
    if (SuggestedAction.IsSet())
    {
        if (bFailed)
        {
            DWCEditorValidation::SetActionState(
                Snapshot,
                SuggestedAction.GetValue(),
                EDWCEditorBuildActionState::Failed,
                &Node.Key);
        }
        else
        {
            RequireAction(Snapshot, SuggestedAction.GetValue(), Node.Key);
        }
    }
}
} // namespace

void FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(
    const UWetClothingAsset& Asset,
    const bool bDeepValidation,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            Asset,
            bDeepValidation
                ? EDWCEditorValidationAccess::ExactPayload
                : EDWCEditorValidationAccess::MetadataOnly);
    const FDWCEditorValidationTargetKey RootKey{
        EDWCEditorValidationDomain::Transparency,
        INDEX_NONE,
        FGuid(),
        TEXT("TransparencyMaps")};
    FDWCEditorValidationNode& RootNode =
        DWCEditorValidation::FindOrAddNode(InOutSnapshot, RootKey);
    RootNode.Intent = Targets.HasEnabledLayers()
        ? EDWCEditorValidationIntentState::Enabled
        : EDWCEditorValidationIntentState::NotConfigured;
    RootNode.Artifact = Targets.HasEnabledLayers()
        ? (Targets.FullBakeState == EDWCEditorBuildActionState::UpToDate &&
           Targets.AffectedStage4State == EDWCEditorBuildActionState::UpToDate
            ? EDWCEditorValidationArtifactState::Current
            : EDWCEditorValidationArtifactState::Stale)
        : EDWCEditorValidationArtifactState::NotRequired;
    RootNode.Persistence = Targets.bSavePending
        ? EDWCEditorValidationPersistenceState::SavePending
        : EDWCEditorValidationPersistenceState::Saved;

    DWCEditorValidation::SetActionState(
        InOutSnapshot,
        EDWCEditorBuildAction::BakeTransparencyTextures,
        Targets.FullBakeState,
        &RootKey);
    DWCEditorValidation::SetActionState(
        InOutSnapshot,
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps,
        Targets.AffectedStage4State,
        &RootKey);

    if (Targets.RecordedStatus == EDWCBakeStatus::Failed)
    {
        RootNode.Operation = EDWCEditorValidationOperationState::Failed;
        AddDiagnostic(
            InOutSnapshot,
            RootNode,
            TEXT("TransparencyMaps.BuildFailed"),
            EDWCEditorValidationSeverity::Error,
            NSLOCTEXT("DWCTransparencyLayerValidation", "Failed", "Failed"),
            Targets.FailureMessage.IsEmpty()
                ? TEXT("The Transparency texture build failed.")
                : Targets.FailureMessage,
            NSLOCTEXT("DWCTransparencyLayerValidation", "RetryBake", "Fix any reported inputs, then retry Build for Runtime > Bake Transparency Textures."),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::BakeTransparencyTextures,
            true,
            DWCBakeOutput::TransparencyMaps);
    }
    for (const FDWCTransparencyBuildTarget& Target : Targets.Targets)
    {
        FDWCEditorValidationNode& Node = InOutSnapshot.Nodes.AddDefaulted_GetRef();
        Node.Key = MakeTarget(Target);
        Node.Intent = ConvertIntent(Target);
        if (Node.Intent == EDWCEditorValidationIntentState::NotConfigured ||
            Node.Intent == EDWCEditorValidationIntentState::Draft ||
            Node.Intent == EDWCEditorValidationIntentState::Disabled)
        {
            Node.Input = EDWCEditorValidationInputState::Unknown;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            continue;
        }

        if (Target.Requirement == EDWCTransparencyBuildRequirement::ManualRepair)
        {
            Node.Input = EDWCEditorValidationInputState::Invalid;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            AddDiagnostic(
                InOutSnapshot,
                Node,
                FName(*FString::Printf(TEXT("TransparencyInput_%s"),
                    *Target.LayerGuid.ToString(EGuidFormats::Digits))),
                EDWCEditorValidationSeverity::Error,
                NSLOCTEXT("DWCTransparencyLayerValidation", "ManualFix", "Manual Fix"),
                FString::Printf(TEXT("Transparency Textures: %s"), *Target.Detail),
                NSLOCTEXT("DWCTransparencyLayerValidation", "FixInputs",
                    "Fix this Transparency Target Part's inputs in Transparency Editor."),
                EDWCEditorValidationRemediation::Manual);
            continue;
        }

        Node.Input = EDWCEditorValidationInputState::Valid;
        if (Target.bOutputCurrent)
        {
            Node.Artifact = EDWCEditorValidationArtifactState::Current;
            continue;
        }
        Node.Artifact = Target.bHasBakedOutput
            ? EDWCEditorValidationArtifactState::Stale
            : EDWCEditorValidationArtifactState::Missing;
        const bool bAffected =
            Target.Requirement == EDWCTransparencyBuildRequirement::AffectedStage4;
        const EDWCEditorBuildAction Action = bAffected
            ? EDWCEditorBuildAction::RebakeAffectedTransparencyMaps
            : EDWCEditorBuildAction::BakeTransparencyTextures;
        AddDiagnostic(
            InOutSnapshot,
            Node,
            FName(*FString::Printf(TEXT("TransparencyOutput_%s"),
                *Target.LayerGuid.ToString(EGuidFormats::Digits))),
            EDWCEditorValidationSeverity::Warning,
            Target.bHasBakedOutput
                ? NSLOCTEXT("DWCTransparencyLayerValidation", "OutOfDate", "Out of Date")
                : NSLOCTEXT("DWCTransparencyLayerValidation", "NotBaked", "Not Baked"),
            Target.Detail,
            bAffected
                ? NSLOCTEXT("DWCTransparencyLayerValidation", "RebakeAffected",
                    "Use Build for Runtime > Rebake Affected Transparency Maps.")
                : NSLOCTEXT("DWCTransparencyLayerValidation", "BakeTransparency",
                    "Use Build for Runtime > Bake Transparency Textures."),
            EDWCEditorValidationRemediation::BuildAction,
            Action);
    }
}
