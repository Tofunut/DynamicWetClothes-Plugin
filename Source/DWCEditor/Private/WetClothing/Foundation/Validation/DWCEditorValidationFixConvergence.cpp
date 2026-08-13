// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationFixConvergence.h"

#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"

namespace
{
    FString TargetSignature(const FDWCEditorValidationTargetKey& Target)
    {
        return FString::Printf(
            TEXT("%u:%d:%s:%s"),
            static_cast<uint8>(Target.Domain),
            Target.MaterialSlotIndex,
            *Target.LayerGuid.ToString(EGuidFormats::Digits),
            *Target.SubResource.ToString());
    }

    template <typename ValueType>
    FString NumericArraySignature(const TArray<ValueType>& Values)
    {
        TArray<ValueType> Sorted = Values;
        Sorted.Sort();
        TArray<FString> Records;
        Records.Reserve(Sorted.Num());
        for (const ValueType Value : Sorted)
        {
            Records.Add(FString::Printf(TEXT("%lld"), static_cast<int64>(Value)));
        }
        return FString::Join(Records, TEXT(","));
    }

    FString GuidArraySignature(const TArray<FGuid>& Values)
    {
        TArray<FString> Records;
        Records.Reserve(Values.Num());
        for (const FGuid& Value : Values)
        {
            Records.Add(Value.ToString(EGuidFormats::Digits));
        }
        Records.Sort();
        return FString::Join(Records, TEXT(","));
    }

    FString NameArraySignature(const TArray<FName>& Values)
    {
        TArray<FString> Records;
        Records.Reserve(Values.Num());
        for (const FName Value : Values)
        {
            Records.Add(Value.ToString());
        }
        Records.Sort();
        return FString::Join(Records, TEXT(","));
    }

    FString TargetArraySignature(const TArray<FDWCEditorValidationTargetKey>& Values)
    {
        TArray<FString> Records;
        Records.Reserve(Values.Num());
        for (const FDWCEditorValidationTargetKey& Value : Values)
        {
            Records.Add(TargetSignature(Value));
        }
        Records.Sort();
        return FString::Join(Records, TEXT(","));
    }
}

FDWCEditorValidationFixConvergence::FDWCEditorValidationFixConvergence(
    const int32 InMaxObservations,
    const int32 InMaxIdenticalObservations)
    : MaxObservations(FMath::Max(1, InMaxObservations))
    , MaxIdenticalObservations(FMath::Max(1, InMaxIdenticalObservations))
{
}

FDWCEditorValidationFixDecisionResult FDWCEditorValidationFixConvergence::Observe(
    const FWCAEditorValidationSnapshot& Validation,
    const FDWCEditorBuildStatusSnapshot& BuildStatus,
    const FDWCEditorBuildPlan& Plan)
{
    FDWCEditorValidationFixDecisionResult Result;
    ++ObservationCount;
    if (ObservationCount > MaxObservations)
    {
        Result.Decision = EDWCEditorValidationFixDecision::IterationLimit;
        Result.Failure = TEXT("The automatic Build exceeded its bounded replanning limit.");
        return Result;
    }

    if (!Plan.IsExecutable())
    {
        Result.Decision = EDWCEditorValidationFixDecision::Blocked;
        Result.Failure = TEXT("One or more required automatic fixes are blocked.");
        return Result;
    }

    if (Plan.Steps.IsEmpty())
    {
        Result.Decision = EDWCEditorValidationFixDecision::Complete;
        return Result;
    }

    Result.Step = Plan.Steps[0];
    const FString Fingerprint = BuildStateFingerprint(Validation, BuildStatus, Plan);
    if (Fingerprint == LastFingerprint)
    {
        ++IdenticalObservationCount;
    }
    else
    {
        LastFingerprint = Fingerprint;
        IdenticalObservationCount = 1;
    }

    if (IdenticalObservationCount > MaxIdenticalObservations)
    {
        Result.Decision = EDWCEditorValidationFixDecision::NoProgress;
        Result.Failure = TEXT("The automatic Build made no canonical state progress after repeated execution.");
        return Result;
    }

    Result.Decision = EDWCEditorValidationFixDecision::ExecuteStep;
    return Result;
}

FString FDWCEditorValidationFixConvergence::BuildStateFingerprint(
    const FWCAEditorValidationSnapshot& Validation,
    const FDWCEditorBuildStatusSnapshot& BuildStatus,
    const FDWCEditorBuildPlan& Plan)
{
    TArray<FString> NodeRecords;
    NodeRecords.Reserve(Validation.Nodes.Num());
    for (const FDWCEditorValidationNode& Node : Validation.Nodes)
    {
        NodeRecords.Add(FString::Printf(
            TEXT("%s:%u:%u:%u:%u:%u:%u:%u"),
            *TargetSignature(Node.Key),
            static_cast<uint8>(Node.Intent),
            static_cast<uint8>(Node.Input),
            static_cast<uint8>(Node.Dependency),
            static_cast<uint8>(Node.Artifact),
            static_cast<uint8>(Node.Persistence),
            static_cast<uint8>(Node.Operation),
            static_cast<uint8>(Node.GetOverallState())));
    }
    NodeRecords.Sort();

    TArray<FString> DiagnosticRecords;
    DiagnosticRecords.Reserve(Validation.Diagnostics.Num());
    for (const FDWCEditorValidationDiagnostic& Diagnostic : Validation.Diagnostics)
    {
        DiagnosticRecords.Add(FString::Printf(
            TEXT("%s:%s:%u:%u:%d:%d:%s"),
            *Diagnostic.Code.ToString(),
            *TargetSignature(Diagnostic.Target),
            static_cast<uint8>(Diagnostic.Severity),
            static_cast<uint8>(Diagnostic.Remediation),
            Diagnostic.SuggestedAction.IsSet()
                ? static_cast<int32>(Diagnostic.SuggestedAction.GetValue())
                : INDEX_NONE,
            Diagnostic.bFailed ? 1 : 0,
            *NumericArraySignature(Diagnostic.BlockingActions)));
    }
    DiagnosticRecords.Sort();

    TArray<FString> ValidationActionRecords;
    ValidationActionRecords.Reserve(Validation.Actions.Num());
    for (const TPair<EDWCEditorBuildAction, FDWCEditorValidationActionState>& Pair : Validation.Actions)
    {
        ValidationActionRecords.Add(FString::Printf(
            TEXT("%u:%u:%s:%s"),
            static_cast<uint8>(Pair.Key),
            static_cast<uint8>(Pair.Value.State),
            *TargetArraySignature(Pair.Value.Targets),
            *NumericArraySignature(Pair.Value.BlockingActions)));
    }
    ValidationActionRecords.Sort();

    TArray<FString> BuildActionRecords;
    BuildActionRecords.Reserve(BuildStatus.Actions.Num());
    for (const TPair<EDWCEditorBuildAction, FDWCEditorBuildActionStatus>& Pair : BuildStatus.Actions)
    {
        BuildActionRecords.Add(FString::Printf(
            TEXT("%u:%u:%s:%s:%s"),
            static_cast<uint8>(Pair.Key),
            static_cast<uint8>(Pair.Value.State),
            *NumericArraySignature(Pair.Value.BlockingActions),
            *NumericArraySignature(Pair.Value.MaterialSlotIndices),
            *GuidArraySignature(Pair.Value.LayerGuids)));
    }
    BuildActionRecords.Sort();

    TArray<FString> PlanRecords;
    PlanRecords.Reserve(Plan.Steps.Num());
    for (const FDWCEditorBuildPlanStep& Step : Plan.Steps)
    {
        PlanRecords.Add(FString::Printf(
            TEXT("%u:%d:%s:%s:%s"),
            static_cast<uint8>(Step.Action),
            Step.bExplicitlyRequested ? 1 : 0,
            *NumericArraySignature(Step.MaterialSlotIndices),
            *GuidArraySignature(Step.LayerGuids),
            *NameArraySignature(Step.SourceDiagnosticCodes)));
    }

    return FString::Printf(
        TEXT("N[%s]|D[%s]|V[%s]|B[%s]|P%u[%s]|X[%s]|M[%s]"),
        *FString::Join(NodeRecords, TEXT(";")),
        *FString::Join(DiagnosticRecords, TEXT(";")),
        *FString::Join(ValidationActionRecords, TEXT(";")),
        *FString::Join(BuildActionRecords, TEXT(";")),
        static_cast<uint8>(Plan.Policy),
        *FString::Join(PlanRecords, TEXT(";")),
        *NumericArraySignature(Plan.BlockedActions),
        *NameArraySignature(Plan.ManualDiagnosticCodes));
}
