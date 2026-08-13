//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "Misc/ScopeExit.h"

namespace
{
    int32 GetStableOrder(const EDWCEditorBuildAction Action)
    {
        const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
        return Descriptor != nullptr ? Descriptor->StableOrder : MAX_int32;
    }

    void AddBlocked(FDWCEditorBuildPlan& Plan, const EDWCEditorBuildAction Action, const FString& Reason)
    {
        Plan.BlockedActions.AddUnique(Action);
        if (!Reason.IsEmpty())
        {
            Plan.Diagnostics.AddUnique(Reason);
        }
    }

    void ExpandHardPrerequisites(
        const FDWCEditorBuildStatusSnapshot& Snapshot,
        const EDWCEditorBuildAction Action,
        TSet<EDWCEditorBuildAction>& Selected,
        TSet<EDWCEditorBuildAction>& Visiting,
        FDWCEditorBuildPlan& Plan)
    {
        if (Selected.Contains(Action))
        {
            return;
        }
        if (Visiting.Contains(Action))
        {
            AddBlocked(Plan, Action, TEXT("The evaluated build prerequisites contain a cycle."));
            return;
        }
        Visiting.Add(Action);
        ON_SCOPE_EXIT
        {
            Visiting.Remove(Action);
        };

        const FDWCEditorBuildActionStatus* Status = Snapshot.Find(Action);
        if (Status == nullptr)
        {
            AddBlocked(Plan, Action, TEXT("A requested build action has no evaluated status."));
            return;
        }
        if (Status->State == EDWCEditorBuildActionState::Blocked)
        {
            AddBlocked(Plan, Action, Status->Reason);
            for (const EDWCEditorBuildAction Blocker : Status->BlockingActions)
            {
                ExpandHardPrerequisites(Snapshot, Blocker, Selected, Visiting, Plan);
            }
            return;
        }
        if (Status->State == EDWCEditorBuildActionState::Unavailable)
        {
            AddBlocked(Plan, Action, Status->Reason);
            return;
        }
        if (Status->State == EDWCEditorBuildActionState::Running)
        {
            AddBlocked(Plan, Action, Status->Reason.IsEmpty()
                ? TEXT("A required build action is already running.")
                : Status->Reason);
            return;
        }
        if (!Status->RequiresExecution())
        {
            return;
        }

        const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
        if (Descriptor == nullptr)
        {
            AddBlocked(Plan, Action, TEXT("A requested build action is not registered."));
            return;
        }

        for (const EDWCEditorBuildAction Blocker : Status->BlockingActions)
        {
            ExpandHardPrerequisites(Snapshot, Blocker, Selected, Visiting, Plan);
        }
        for (const FDWCEditorBuildActionDependency& Dependency : Descriptor->Dependencies)
        {
            if (Dependency.Kind == EDWCEditorBuildDependencyKind::HardPrerequisite)
            {
                ExpandHardPrerequisites(Snapshot, Dependency.Action, Selected, Visiting, Plan);
            }
        }
        if (!Plan.BlockedActions.Contains(Action))
        {
            Selected.Add(Action);
        }
    }

    void SortSelectedActions(
        const TSet<EDWCEditorBuildAction>& Selected,
        const FDWCEditorBuildStatusSnapshot& Snapshot,
        FDWCEditorBuildPlan& Plan)
    {
        TMap<EDWCEditorBuildAction, int32> InDegree;
        TMap<EDWCEditorBuildAction, TArray<EDWCEditorBuildAction>> Dependents;
        for (const EDWCEditorBuildAction Action : Selected)
        {
            InDegree.Add(Action, 0);
        }

        const auto AddDependencyEdge = [&Selected, &InDegree, &Dependents](
            const EDWCEditorBuildAction Prerequisite,
            const EDWCEditorBuildAction Dependent)
        {
            if (!Selected.Contains(Prerequisite))
            {
                return;
            }
            TArray<EDWCEditorBuildAction>& Actions = Dependents.FindOrAdd(Prerequisite);
            if (!Actions.Contains(Dependent))
            {
                Actions.Add(Dependent);
                ++InDegree.FindChecked(Dependent);
            }
        };

        for (const EDWCEditorBuildAction Action : Selected)
        {
            const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
            if (Descriptor == nullptr)
            {
                continue;
            }
            for (const FDWCEditorBuildActionDependency& Dependency : Descriptor->Dependencies)
            {
                AddDependencyEdge(Dependency.Action, Action);
            }
            if (const FDWCEditorBuildActionStatus* Status = Snapshot.Find(Action))
            {
                for (const EDWCEditorBuildAction Blocker : Status->BlockingActions)
                {
                    AddDependencyEdge(Blocker, Action);
                }
            }
        }

        TArray<EDWCEditorBuildAction> Ready;
        for (const TPair<EDWCEditorBuildAction, int32>& Pair : InDegree)
        {
            if (Pair.Value == 0)
            {
                Ready.Add(Pair.Key);
            }
        }
        Ready.Sort([](const EDWCEditorBuildAction A, const EDWCEditorBuildAction B)
        {
            return GetStableOrder(A) < GetStableOrder(B);
        });

        while (!Ready.IsEmpty())
        {
            const EDWCEditorBuildAction Action = Ready[0];
            Ready.RemoveAt(0, EAllowShrinking::No);
            Plan.Steps.Add({Action});

            for (const EDWCEditorBuildAction Dependent : Dependents.FindRef(Action))
            {
                int32& Degree = InDegree.FindChecked(Dependent);
                --Degree;
                if (Degree == 0)
                {
                    Ready.Add(Dependent);
                }
            }
            Ready.Sort([](const EDWCEditorBuildAction A, const EDWCEditorBuildAction B)
            {
                return GetStableOrder(A) < GetStableOrder(B);
            });
        }

        if (Plan.Steps.Num() != Selected.Num())
        {
            Plan.Diagnostics.Add(TEXT("The selected build actions could not be topologically sorted."));
            for (const EDWCEditorBuildAction Action : Selected)
            {
                if (!Plan.Steps.ContainsByPredicate(
                        [Action](const FDWCEditorBuildPlanStep& Step) { return Step.Action == Action; }))
                {
                    Plan.BlockedActions.AddUnique(Action);
                }
            }
        }
    }

    void PopulateStepTargets(
        FDWCEditorBuildPlan& Plan,
        const FDWCEditorBuildStatusSnapshot& Snapshot,
        const TSet<EDWCEditorBuildAction>& ExplicitActions,
        const FWCAEditorValidationSnapshot* ValidationSnapshot)
    {
        for (FDWCEditorBuildPlanStep& Step : Plan.Steps)
        {
            Step.bExplicitlyRequested = ExplicitActions.Contains(Step.Action);
            if (const FDWCEditorBuildActionStatus* Status = Snapshot.Find(Step.Action))
            {
                Step.MaterialSlotIndices = Status->MaterialSlotIndices;
                Step.LayerGuids = Status->LayerGuids;
            }
            if (ValidationSnapshot == nullptr)
            {
                continue;
            }
            for (const FDWCEditorValidationDiagnostic& Diagnostic : ValidationSnapshot->Diagnostics)
            {
                if (!Diagnostic.SuggestedAction.IsSet() ||
                    Diagnostic.SuggestedAction.GetValue() != Step.Action)
                {
                    continue;
                }
                Step.SourceDiagnosticCodes.AddUnique(Diagnostic.Code);
                if (Diagnostic.Target.MaterialSlotIndex != INDEX_NONE)
                {
                    Step.MaterialSlotIndices.AddUnique(Diagnostic.Target.MaterialSlotIndex);
                }
                if (Diagnostic.Target.LayerGuid.IsValid())
                {
                    Step.LayerGuids.AddUnique(Diagnostic.Target.LayerGuid);
                }
            }
            Step.MaterialSlotIndices.Sort();
            Step.LayerGuids.Sort();
        }
    }

    FDWCEditorBuildStatusSnapshot ReconcileValidationActions(
        const FDWCEditorBuildStatusSnapshot& BuildSnapshot,
        const FWCAEditorValidationSnapshot& ValidationSnapshot)
    {
        FDWCEditorBuildStatusSnapshot Result = BuildSnapshot;
        for (const TPair<EDWCEditorBuildAction, FDWCEditorValidationActionState>& Pair :
             ValidationSnapshot.Actions)
        {
            FDWCEditorBuildActionStatus* Status = Result.Actions.Find(Pair.Key);
            if (Status == nullptr || Status->State == EDWCEditorBuildActionState::Running)
            {
                continue;
            }

            const FDWCEditorValidationActionState& ValidationAction = Pair.Value;
            const bool bValidationRequiresAction =
                ValidationAction.State == EDWCEditorBuildActionState::Required ||
                ValidationAction.State == EDWCEditorBuildActionState::Failed ||
                ValidationAction.State == EDWCEditorBuildActionState::Blocked;
            if (!bValidationRequiresAction)
            {
                continue;
            }

            if (Status->State == EDWCEditorBuildActionState::Unavailable)
            {
                Status->State = EDWCEditorBuildActionState::Blocked;
                if (Status->Reason.IsEmpty())
                {
                    Status->Reason = TEXT("Validation requires this action, but its build service is unavailable.");
                }
            }
            else
            {
                Status->State = ValidationAction.State;
            }
            for (const EDWCEditorBuildAction Blocker : ValidationAction.BlockingActions)
            {
                Status->BlockingActions.AddUnique(Blocker);
            }
            for (const FDWCEditorValidationTargetKey& Target : ValidationAction.Targets)
            {
                if (Target.MaterialSlotIndex != INDEX_NONE)
                {
                    Status->MaterialSlotIndices.AddUnique(Target.MaterialSlotIndex);
                }
                if (Target.LayerGuid.IsValid())
                {
                    Status->LayerGuids.AddUnique(Target.LayerGuid);
                }
            }
        }
        return Result;
    }
}

FDWCEditorBuildPlan FDWCEditorBuildPlanResolver::ResolveRequired(
    const FDWCEditorBuildStatusSnapshot& Snapshot)
{
    TArray<EDWCEditorBuildAction> Required;
    for (const FDWCEditorBuildActionDescriptor& Descriptor : FDWCEditorBuildActionRegistry::GetDescriptors())
    {
        const FDWCEditorBuildActionStatus* Status = Snapshot.Find(Descriptor.Action);
        if (Status != nullptr && Status->RequiresExecution())
        {
            Required.Add(Descriptor.Action);
        }
    }
    FDWCEditorBuildPlan Plan = ResolveActions(Snapshot, Required);
    Plan.Policy = EDWCEditorBuildPlanPolicy::AllRequired;
    return Plan;
}

FDWCEditorBuildPlan FDWCEditorBuildPlanResolver::ResolveActions(
    const FDWCEditorBuildStatusSnapshot& Snapshot,
    const TConstArrayView<EDWCEditorBuildAction> RequestedActions)
{
    FDWCEditorBuildPlan Plan;
    Plan.Policy = EDWCEditorBuildPlanPolicy::ExplicitActions;
    TSet<EDWCEditorBuildAction> Selected;
    TSet<EDWCEditorBuildAction> Visiting;
    TSet<EDWCEditorBuildAction> ExplicitActions;
    for (const EDWCEditorBuildAction Action : RequestedActions)
    {
        ExplicitActions.Add(Action);
        ExpandHardPrerequisites(Snapshot, Action, Selected, Visiting, Plan);
    }
    SortSelectedActions(Selected, Snapshot, Plan);
    PopulateStepTargets(Plan, Snapshot, ExplicitActions, nullptr);
    return Plan;
}

FDWCEditorBuildPlan FDWCEditorBuildPlanResolver::ResolveValidationSuggested(
    const FDWCEditorBuildStatusSnapshot& BuildSnapshot,
    const FWCAEditorValidationSnapshot& ValidationSnapshot)
{
    const FDWCEditorBuildStatusSnapshot EffectiveBuildSnapshot =
        ReconcileValidationActions(BuildSnapshot, ValidationSnapshot);
    TArray<EDWCEditorBuildAction> RequestedActions;
    TSet<EDWCEditorBuildAction> ExplicitActions;
    FDWCEditorBuildPlan Result;
    Result.Policy = EDWCEditorBuildPlanPolicy::ValidationSuggested;

    for (const FDWCEditorValidationDiagnostic& Diagnostic : ValidationSnapshot.Diagnostics)
    {
        if (Diagnostic.Remediation == EDWCEditorValidationRemediation::Manual)
        {
            Result.ManualDiagnosticCodes.AddUnique(Diagnostic.Code);
            continue;
        }
        if (Diagnostic.Remediation == EDWCEditorValidationRemediation::BuildAction &&
            Diagnostic.SuggestedAction.IsSet())
        {
            RequestedActions.AddUnique(Diagnostic.SuggestedAction.GetValue());
            ExplicitActions.Add(Diagnostic.SuggestedAction.GetValue());
        }
    }

    FDWCEditorBuildPlan Automatic = ResolveActions(EffectiveBuildSnapshot, RequestedActions);
    Result.Steps = MoveTemp(Automatic.Steps);
    Result.BlockedActions = MoveTemp(Automatic.BlockedActions);
    Result.Diagnostics = MoveTemp(Automatic.Diagnostics);
    PopulateStepTargets(Result, EffectiveBuildSnapshot, ExplicitActions, &ValidationSnapshot);
    return Result;
}
