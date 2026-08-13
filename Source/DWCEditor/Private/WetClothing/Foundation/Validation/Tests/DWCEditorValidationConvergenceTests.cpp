// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationFixConvergence.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSectionRegistry.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

namespace
{
    FDWCEditorBuildStatusSnapshot MakeUpToDateBuildStatus()
    {
        FDWCEditorBuildStatusSnapshot Result;
        for (const FDWCEditorBuildActionDescriptor& Descriptor :
             FDWCEditorBuildActionRegistry::GetDescriptors())
        {
            FDWCEditorBuildActionStatus& Status = Result.Actions.Add(Descriptor.Action);
            Status.Action = Descriptor.Action;
            Status.State = EDWCEditorBuildActionState::UpToDate;
        }
        return Result;
    }

    void AddAutomaticIssue(
        FWCAEditorValidationSnapshot& Snapshot,
        const FName Code,
        const EDWCEditorValidationDomain Domain,
        const EDWCEditorBuildAction Action,
        const int32 MaterialSlotIndex = INDEX_NONE,
        const FGuid LayerGuid = FGuid())
    {
        FDWCEditorValidationTargetKey Key;
        Key.Domain = Domain;
        Key.MaterialSlotIndex = MaterialSlotIndex;
        Key.LayerGuid = LayerGuid;
        FDWCEditorValidationNode& Node = DWCEditorValidation::FindOrAddNode(Snapshot, Key);
        Node.Intent = EDWCEditorValidationIntentState::Enabled;
        Node.Artifact = EDWCEditorValidationArtifactState::Stale;
        DWCEditorValidation::AddDiagnostic(
            Snapshot,
            Node,
            Code,
            EDWCEditorValidationSeverity::Warning,
            FText::FromName(Code),
            FText::FromString(TEXT("Out of date")),
            FText::FromString(TEXT("Synthetic convergence state")),
            FText::FromString(TEXT("Run the typed build action")),
            EDWCEditorValidationRemediation::BuildAction,
            Action);
    }

    FDWCEditorValidationNode MakeNodeForOverallState(
        const EDWCEditorValidationOverallState State)
    {
        FDWCEditorValidationNode Node;
        switch (State)
        {
        case EDWCEditorValidationOverallState::NotApplicable:
            Node.Intent = EDWCEditorValidationIntentState::NotApplicable;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            break;
        case EDWCEditorValidationOverallState::NotConfigured:
            Node.Intent = EDWCEditorValidationIntentState::NotConfigured;
            Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
            break;
        case EDWCEditorValidationOverallState::Draft:
            Node.Intent = EDWCEditorValidationIntentState::Draft;
            Node.Artifact = EDWCEditorValidationArtifactState::Missing;
            break;
        case EDWCEditorValidationOverallState::Disabled:
            Node.Intent = EDWCEditorValidationIntentState::Disabled;
            Node.Artifact = EDWCEditorValidationArtifactState::Stale;
            break;
        case EDWCEditorValidationOverallState::SavePending:
            Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            break;
        case EDWCEditorValidationOverallState::Partial:
            Node.Artifact = EDWCEditorValidationArtifactState::Partial;
            break;
        case EDWCEditorValidationOverallState::Stale:
            Node.Artifact = EDWCEditorValidationArtifactState::Stale;
            break;
        case EDWCEditorValidationOverallState::Missing:
            Node.Artifact = EDWCEditorValidationArtifactState::Missing;
            break;
        case EDWCEditorValidationOverallState::Invalid:
            Node.Artifact = EDWCEditorValidationArtifactState::Invalid;
            break;
        case EDWCEditorValidationOverallState::Blocked:
            Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            break;
        case EDWCEditorValidationOverallState::Running:
            Node.Operation = EDWCEditorValidationOperationState::Running;
            break;
        case EDWCEditorValidationOverallState::Failed:
            Node.Operation = EDWCEditorValidationOperationState::Failed;
            break;
        case EDWCEditorValidationOverallState::Cancelled:
            Node.Operation = EDWCEditorValidationOperationState::Cancelled;
            break;
        case EDWCEditorValidationOverallState::Current:
        default:
            break;
        }
        return Node;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationStatePresentationParityTest,
    "DWC.Editor.Foundation.Validation.StateParity.CanonicalToReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationStatePresentationParityTest::RunTest(const FString& Parameters)
{
    const EDWCEditorValidationOverallState States[] = {
        EDWCEditorValidationOverallState::NotApplicable,
        EDWCEditorValidationOverallState::NotConfigured,
        EDWCEditorValidationOverallState::Draft,
        EDWCEditorValidationOverallState::Disabled,
        EDWCEditorValidationOverallState::Current,
        EDWCEditorValidationOverallState::SavePending,
        EDWCEditorValidationOverallState::Partial,
        EDWCEditorValidationOverallState::Stale,
        EDWCEditorValidationOverallState::Missing,
        EDWCEditorValidationOverallState::Invalid,
        EDWCEditorValidationOverallState::Blocked,
        EDWCEditorValidationOverallState::Running,
        EDWCEditorValidationOverallState::Failed,
        EDWCEditorValidationOverallState::Cancelled
    };

    for (const EDWCEditorValidationOverallState Expected : States)
    {
        FWCAEditorValidationSnapshot Snapshot;
        FDWCEditorValidationNode Node = MakeNodeForOverallState(Expected);
        Node.Key.Domain = EDWCEditorValidationDomain::Wrinkle;
        Snapshot.Nodes.Add(Node);

        const FWCAValidationReport Report =
            FDWCEditorValidationReportAdapter::BuildReport(Snapshot);
        const FWCAValidationSectionResult* Section =
            Report.FindSection(EWCAValidationSection::WrinkleMaps);
        if (!TestNotNull(TEXT("Wrinkle report section exists"), Section))
        {
            continue;
        }

        const FString StateLabel = FDWCEditorValidationSectionRegistry::GetStateLabel(Expected).ToString();
        TestEqual(*FString::Printf(TEXT("Canonical state reaches report: %s"), *StateLabel),
            Section->OverallState, Expected);
        TestEqual(*FString::Printf(TEXT("Presentation follows canonical state: %s"), *StateLabel),
            Section->PresentationState,
            FDWCEditorValidationSectionRegistry::MapPresentationState(Expected));
        TestEqual(*FString::Printf(TEXT("Applicability follows canonical state: %s"), *StateLabel),
            Section->bApplicable,
            Expected != EDWCEditorValidationOverallState::NotApplicable);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationBuildStatusParityTest,
    "DWC.Editor.Foundation.Validation.StateParity.BuildStatusToReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationBuildStatusParityTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        EDWCEditorBuildAction Action;
        EDWCEditorBuildActionState BuildState;
        EWCAValidationSection Section;
        EDWCEditorValidationOverallState Expected;
    };
    const FCase Cases[] = {
        {EDWCEditorBuildAction::SaveAsset, EDWCEditorBuildActionState::Required,
            EWCAValidationSection::Asset, EDWCEditorValidationOverallState::SavePending},
        {EDWCEditorBuildAction::BakeWrinkleTextures, EDWCEditorBuildActionState::Required,
            EWCAValidationSection::WrinkleMaps, EDWCEditorValidationOverallState::Stale},
        {EDWCEditorBuildAction::BakeTransparencyTextures, EDWCEditorBuildActionState::Running,
            EWCAValidationSection::TransparencyMaps, EDWCEditorValidationOverallState::Running},
        {EDWCEditorBuildAction::GenerateMaterials, EDWCEditorBuildActionState::Blocked,
            EWCAValidationSection::GeneratedMaterials, EDWCEditorValidationOverallState::Blocked},
        {EDWCEditorBuildAction::BuildCPURuntimeData, EDWCEditorBuildActionState::Failed,
            EWCAValidationSection::RuntimeData, EDWCEditorValidationOverallState::Failed}
    };

    for (const FCase& Case : Cases)
    {
        FWCAEditorValidationSnapshot Snapshot;
        FDWCEditorBuildStatusSnapshot BuildStatus;
        FDWCEditorBuildActionStatus& Status = BuildStatus.Actions.Add(Case.Action);
        Status.Action = Case.Action;
        Status.State = Case.BuildState;

        const FWCAValidationReport Report =
            FDWCEditorValidationReportAdapter::BuildReport(Snapshot, &BuildStatus);
        const FWCAValidationSectionResult* Section = Report.FindSection(Case.Section);
        if (TestNotNull(TEXT("Mapped build section exists"), Section))
        {
            TestEqual(TEXT("Build state reaches canonical report section"),
                Section->OverallState, Case.Expected);
            const EDWCValidationPresentationState Presentation =
                FDWCEditorValidationSectionRegistry::MapPresentationState(Case.Expected);
            const bool bExpectedIssue =
                Presentation == EDWCValidationPresentationState::Warning ||
                Presentation == EDWCValidationPresentationState::Error;
            TestEqual(TEXT("Report issue state follows canonical presentation severity"),
                Report.HasIssues(), bExpectedIssue);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationAutomaticFixConvergenceTest,
    "DWC.Editor.Foundation.Validation.AutomaticFix.Converges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationAutomaticFixConvergenceTest::RunTest(const FString& Parameters)
{
    const FGuid LayerGuid = FGuid::NewGuid();
    const FDWCEditorBuildStatusSnapshot BuildStatus = MakeUpToDateBuildStatus();
    FDWCEditorValidationFixConvergence Convergence;

    FWCAEditorValidationSnapshot WrinkleStage;
    AddAutomaticIssue(WrinkleStage, TEXT("Wrinkle.Output.Stale"),
        EDWCEditorValidationDomain::Wrinkle,
        EDWCEditorBuildAction::BakeWrinkleTextures, 14);
    AddAutomaticIssue(WrinkleStage, TEXT("Transparency.Dependency.Stale"),
        EDWCEditorValidationDomain::Transparency,
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps, 14, LayerGuid);
    AddAutomaticIssue(WrinkleStage, TEXT("Asset.Save.Pending"),
        EDWCEditorValidationDomain::Asset,
        EDWCEditorBuildAction::SaveAsset);
    FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveValidationSuggested(
        BuildStatus, WrinkleStage);
    FDWCEditorValidationFixDecisionResult Decision =
        Convergence.Observe(WrinkleStage, BuildStatus, Plan);
    TestEqual(TEXT("First stage executes"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    TestTrue(TEXT("First stage has a step"), Decision.Step.IsSet());
    if (Decision.Step.IsSet())
    {
        TestEqual(TEXT("Wrinkle is rebuilt before its dependent transparency output"),
            Decision.Step.GetValue().Action, EDWCEditorBuildAction::BakeWrinkleTextures);
    }

    FWCAEditorValidationSnapshot TransparencyStage;
    AddAutomaticIssue(TransparencyStage, TEXT("Transparency.Dependency.Stale"),
        EDWCEditorValidationDomain::Transparency,
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps, 14, LayerGuid);
    AddAutomaticIssue(TransparencyStage, TEXT("Asset.Save.Pending"),
        EDWCEditorValidationDomain::Asset,
        EDWCEditorBuildAction::SaveAsset);
    Plan = FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, TransparencyStage);
    Decision = Convergence.Observe(TransparencyStage, BuildStatus, Plan);
    TestEqual(TEXT("Dependency transition is recognized as progress"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    if (Decision.Step.IsSet())
    {
        TestEqual(TEXT("Affected transparency is rebuilt second"),
            Decision.Step.GetValue().Action,
            EDWCEditorBuildAction::RebakeAffectedTransparencyMaps);
    }

    FWCAEditorValidationSnapshot SaveStage;
    AddAutomaticIssue(SaveStage, TEXT("Asset.Save.Pending"),
        EDWCEditorValidationDomain::Asset,
        EDWCEditorBuildAction::SaveAsset);
    Plan = FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, SaveStage);
    Decision = Convergence.Observe(SaveStage, BuildStatus, Plan);
    TestEqual(TEXT("Save stage is recognized as progress"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    if (Decision.Step.IsSet())
    {
        TestEqual(TEXT("Save is the final action"),
            Decision.Step.GetValue().Action, EDWCEditorBuildAction::SaveAsset);
    }

    const FWCAEditorValidationSnapshot CleanStage;
    Plan = FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, CleanStage);
    Decision = Convergence.Observe(CleanStage, BuildStatus, Plan);
    TestEqual(TEXT("A clean canonical state converges"), Decision.Decision,
        EDWCEditorValidationFixDecision::Complete);
    const FWCAValidationReport CleanReport =
        FDWCEditorValidationReportAdapter::BuildReport(CleanStage, &BuildStatus);
    TestFalse(TEXT("Converged state has no automatic issues"), CleanReport.HasAutoResolvableIssues());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationAutomaticFixNoProgressTest,
    "DWC.Editor.Foundation.Validation.AutomaticFix.NoProgressAndTargetIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationAutomaticFixNoProgressTest::RunTest(const FString& Parameters)
{
    const FDWCEditorBuildStatusSnapshot BuildStatus = MakeUpToDateBuildStatus();
    FDWCEditorValidationFixConvergence Convergence(16, 3);
    FWCAEditorValidationSnapshot Snapshot;
    AddAutomaticIssue(Snapshot, TEXT("Wrinkle.Output.Stale"),
        EDWCEditorValidationDomain::Wrinkle,
        EDWCEditorBuildAction::BakeWrinkleTextures, 3);
    const FDWCEditorBuildPlan Plan =
        FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, Snapshot);

    for (int32 Observation = 0; Observation < 3; ++Observation)
    {
        Snapshot.Diagnostics[0].Presentation.Detail =
            FText::FromString(FString::Printf(TEXT("Changing display text %d"), Observation));
        const FDWCEditorValidationFixDecisionResult Decision =
            Convergence.Observe(Snapshot, BuildStatus, Plan);
        TestEqual(TEXT("The bounded retry window permits execution"), Decision.Decision,
            EDWCEditorValidationFixDecision::ExecuteStep);
    }
    Snapshot.Diagnostics[0].Presentation.Detail = FText::FromString(TEXT("Another display-only change"));
    FDWCEditorValidationFixDecisionResult Decision =
        Convergence.Observe(Snapshot, BuildStatus, Plan);
    TestEqual(TEXT("Presentation changes do not hide canonical no-progress"), Decision.Decision,
        EDWCEditorValidationFixDecision::NoProgress);

    FDWCEditorValidationFixConvergence TargetConvergence(16, 1);
    Decision = TargetConvergence.Observe(Snapshot, BuildStatus, Plan);
    TestEqual(TEXT("Initial target executes"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    FWCAEditorValidationSnapshot OtherTarget;
    AddAutomaticIssue(OtherTarget, TEXT("Wrinkle.Output.Stale"),
        EDWCEditorValidationDomain::Wrinkle,
        EDWCEditorBuildAction::BakeWrinkleTextures, 8);
    const FDWCEditorBuildPlan OtherPlan =
        FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, OtherTarget);
    Decision = TargetConvergence.Observe(OtherTarget, BuildStatus, OtherPlan);
    TestEqual(TEXT("A new slot target is real progress despite using the same action"),
        Decision.Decision, EDWCEditorValidationFixDecision::ExecuteStep);

    FDWCEditorValidationFixConvergence IterationConvergence(2, 8);
    Decision = IterationConvergence.Observe(Snapshot, BuildStatus, Plan);
    TestEqual(TEXT("First bounded observation executes"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    Decision = IterationConvergence.Observe(OtherTarget, BuildStatus, OtherPlan);
    TestEqual(TEXT("Second bounded observation executes"), Decision.Decision,
        EDWCEditorValidationFixDecision::ExecuteStep);
    Decision = IterationConvergence.Observe(Snapshot, BuildStatus, Plan);
    TestEqual(TEXT("The global replanning bound terminates changing states"), Decision.Decision,
        EDWCEditorValidationFixDecision::IterationLimit);

    FWCAEditorValidationSnapshot OrderedSnapshot = Snapshot;
    FDWCEditorValidationNode AdditionalNode = OrderedSnapshot.Nodes[0];
    AdditionalNode.Key.MaterialSlotIndex = 9;
    OrderedSnapshot.Nodes.Add(AdditionalNode);
    FDWCEditorValidationDiagnostic AdditionalDiagnostic = OrderedSnapshot.Diagnostics[0];
    AdditionalDiagnostic.Code = TEXT("Wrinkle.Output.OtherTarget");
    AdditionalDiagnostic.Target.MaterialSlotIndex = 9;
    OrderedSnapshot.Diagnostics.Add(AdditionalDiagnostic);
    FWCAEditorValidationSnapshot ReorderedSnapshot = OrderedSnapshot;
    ReorderedSnapshot.Nodes.Swap(0, 1);
    ReorderedSnapshot.Diagnostics.Swap(0, 1);
    TestEqual(TEXT("Canonical fingerprint is independent of snapshot insertion order"),
        FDWCEditorValidationFixConvergence::BuildStateFingerprint(OrderedSnapshot, BuildStatus, Plan),
        FDWCEditorValidationFixConvergence::BuildStateFingerprint(ReorderedSnapshot, BuildStatus, Plan));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationAutomaticFixBlockedManualTest,
    "DWC.Editor.Foundation.Validation.AutomaticFix.BlockedAndManual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationAutomaticFixBlockedManualTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildStatusSnapshot BuildStatus = MakeUpToDateBuildStatus();
    FDWCEditorBuildActionStatus& WrinkleStatus =
        BuildStatus.Actions.FindChecked(EDWCEditorBuildAction::BakeWrinkleTextures);
    WrinkleStatus.State = EDWCEditorBuildActionState::Blocked;
    WrinkleStatus.BlockingActions.Add(EDWCEditorBuildAction::InitializeDataUV);

    FWCAEditorValidationSnapshot BlockedSnapshot;
    AddAutomaticIssue(BlockedSnapshot, TEXT("Wrinkle.Output.Blocked"),
        EDWCEditorValidationDomain::Wrinkle,
        EDWCEditorBuildAction::BakeWrinkleTextures, 4);
    FDWCEditorValidationActionState& ValidationWrinkleAction =
        BlockedSnapshot.Actions.FindChecked(EDWCEditorBuildAction::BakeWrinkleTextures);
    ValidationWrinkleAction.State = EDWCEditorBuildActionState::Blocked;
    ValidationWrinkleAction.BlockingActions.Add(EDWCEditorBuildAction::InitializeDataUV);
    const FDWCEditorBuildPlan BlockedPlan =
        FDWCEditorBuildPlanResolver::ResolveValidationSuggested(BuildStatus, BlockedSnapshot);
    FDWCEditorValidationFixConvergence BlockedConvergence;
    const FDWCEditorValidationFixDecisionResult BlockedDecision =
        BlockedConvergence.Observe(BlockedSnapshot, BuildStatus, BlockedPlan);
    TestEqual(TEXT("Blocked prerequisites cannot report convergence"), BlockedDecision.Decision,
        EDWCEditorValidationFixDecision::Blocked);

    FWCAEditorValidationSnapshot ManualSnapshot;
    FDWCEditorValidationNode& ManualNode = DWCEditorValidation::FindOrAddNode(
        ManualSnapshot, {EDWCEditorValidationDomain::Transparency, 5});
    DWCEditorValidation::AddDiagnostic(
        ManualSnapshot,
        ManualNode,
        TEXT("Transparency.Authoring.Manual"),
        EDWCEditorValidationSeverity::Error,
        FText::FromString(TEXT("Manual input required")),
        FText::GetEmpty(),
        FText::GetEmpty(),
        FText::GetEmpty(),
        EDWCEditorValidationRemediation::Manual);
    const FDWCEditorBuildStatusSnapshot CleanBuildStatus = MakeUpToDateBuildStatus();
    const FDWCEditorBuildPlan ManualPlan =
        FDWCEditorBuildPlanResolver::ResolveValidationSuggested(CleanBuildStatus, ManualSnapshot);
    FDWCEditorValidationFixConvergence ManualConvergence;
    const FDWCEditorValidationFixDecisionResult ManualDecision =
        ManualConvergence.Observe(ManualSnapshot, CleanBuildStatus, ManualPlan);
    TestEqual(TEXT("Manual-only state has no automatic step"), ManualDecision.Decision,
        EDWCEditorValidationFixDecision::Complete);
    TestTrue(TEXT("Manual issue remains visible after automatic planning"),
        FDWCEditorValidationReportAdapter::BuildReport(ManualSnapshot, &CleanBuildStatus)
            .HasManualIssues());
    return true;
}

#endif
