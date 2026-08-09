//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"

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
        FDWCEditorBuildPlan& Plan)
    {
        if (Selected.Contains(Action))
        {
            return;
        }

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
                ExpandHardPrerequisites(Snapshot, Blocker, Selected, Plan);
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

        for (const FDWCEditorBuildActionDependency& Dependency : Descriptor->Dependencies)
        {
            if (Dependency.Kind == EDWCEditorBuildDependencyKind::HardPrerequisite)
            {
                ExpandHardPrerequisites(Snapshot, Dependency.Action, Selected, Plan);
            }
        }
        if (!Plan.BlockedActions.Contains(Action))
        {
            Selected.Add(Action);
        }
    }

    void SortSelectedActions(
        const TSet<EDWCEditorBuildAction>& Selected,
        FDWCEditorBuildPlan& Plan)
    {
        TMap<EDWCEditorBuildAction, int32> InDegree;
        TMap<EDWCEditorBuildAction, TArray<EDWCEditorBuildAction>> Dependents;
        for (const EDWCEditorBuildAction Action : Selected)
        {
            InDegree.Add(Action, 0);
        }

        for (const EDWCEditorBuildAction Action : Selected)
        {
            const FDWCEditorBuildActionDescriptor* Descriptor = FDWCEditorBuildActionRegistry::Find(Action);
            if (Descriptor == nullptr)
            {
                continue;
            }
            for (const FDWCEditorBuildActionDependency& Dependency : Descriptor->Dependencies)
            {
                if (!Selected.Contains(Dependency.Action))
                {
                    continue;
                }
                Dependents.FindOrAdd(Dependency.Action).Add(Action);
                ++InDegree.FindChecked(Action);
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
    return ResolveActions(Snapshot, Required);
}

FDWCEditorBuildPlan FDWCEditorBuildPlanResolver::ResolveActions(
    const FDWCEditorBuildStatusSnapshot& Snapshot,
    const TConstArrayView<EDWCEditorBuildAction> RequestedActions)
{
    FDWCEditorBuildPlan Plan;
    TSet<EDWCEditorBuildAction> Selected;
    for (const EDWCEditorBuildAction Action : RequestedActions)
    {
        ExpandHardPrerequisites(Snapshot, Action, Selected, Plan);
    }
    SortSelectedActions(Selected, Plan);
    return Plan;
}
