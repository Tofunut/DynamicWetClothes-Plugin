//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionEvaluator.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

namespace
{
    FDWCEditorBuildEvaluationInput MakeReadyInput()
    {
        FDWCEditorBuildEvaluationInput Input;
        Input.bHasAsset = true;
        Input.bHasRuntimeMesh = true;
        Input.bHasWettableSlots = true;
        Input.bHasValidDataUV = true;
        Input.bCPUBackendEnabled = true;
        Input.bGPUBackendEnabled = true;
        Input.bHasWrinkleContent = true;
        Input.bHasTransparencyContent = true;
        Input.DataUVState = EDWCEditorBuildActionState::UpToDate;
        Input.CPURuntimeDataState = EDWCEditorBuildActionState::UpToDate;
        Input.GPURuntimeDataState = EDWCEditorBuildActionState::UpToDate;
        Input.GPUMapsState = EDWCEditorBuildActionState::UpToDate;
        Input.RenderProfileState = EDWCEditorBuildActionState::UpToDate;
        Input.GeneratedMaterialsState = EDWCEditorBuildActionState::UpToDate;
        Input.WrinkleTexturesState = EDWCEditorBuildActionState::UpToDate;
        Input.TransparencyTexturesState = EDWCEditorBuildActionState::UpToDate;
        Input.AffectedTransparencyState = EDWCEditorBuildActionState::UpToDate;
        return Input;
    }

    int32 FindStep(const FDWCEditorBuildPlan& Plan, const EDWCEditorBuildAction Action)
    {
        return Plan.Steps.IndexOfByPredicate(
            [Action](const FDWCEditorBuildPlanStep& Step)
            {
                return Step.Action == Action;
            });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildRegistryCompletenessTest,
    "DWC.Editor.Foundation.Build.RegistryCompleteness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildRegistryCompletenessTest::RunTest(const FString& Parameters)
{
    FString Error;
    TestTrue(TEXT("The common build registry is complete and acyclic"),
        FDWCEditorBuildActionRegistry::Validate(Error));
    TestTrue(TEXT("Registry validation has no error"), Error.IsEmpty());
    TestEqual(TEXT("Every enum action has one descriptor"),
        FDWCEditorBuildActionRegistry::GetDescriptors().Num(),
        static_cast<int32>(EDWCEditorBuildAction::Count));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildStatusEvaluationTest,
    "DWC.Editor.Foundation.Build.StatusEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildStatusEvaluationTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildEvaluationInput Input = MakeReadyInput();
    Input.bAssetDirty = true;
    Input.CPURuntimeDataState = EDWCEditorBuildActionState::Required;
    Input.WrinkleTexturesState = EDWCEditorBuildActionState::Failed;
    Input.RunningActions.Add(EDWCEditorBuildAction::BuildGPURuntimeData);

    const FDWCEditorBuildStatusSnapshot Snapshot = FDWCEditorBuildActionEvaluator::Evaluate(Input);
    TestEqual(TEXT("Dirty asset requires save"),
        Snapshot.Find(EDWCEditorBuildAction::SaveAsset)->State,
        EDWCEditorBuildActionState::Required);
    TestEqual(TEXT("CPU runtime state is preserved"),
        Snapshot.Find(EDWCEditorBuildAction::BuildCPURuntimeData)->State,
        EDWCEditorBuildActionState::Required);
    TestEqual(TEXT("Failed wrinkle output remains failed"),
        Snapshot.Find(EDWCEditorBuildAction::BakeWrinkleTextures)->State,
        EDWCEditorBuildActionState::Failed);
    TestEqual(TEXT("Running state overrides captured output state"),
        Snapshot.Find(EDWCEditorBuildAction::BuildGPURuntimeData)->State,
        EDWCEditorBuildActionState::Running);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildDependencyBlockingTest,
    "DWC.Editor.Foundation.Build.DependencyBlocking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildDependencyBlockingTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildEvaluationInput Input = MakeReadyInput();
    Input.bHasValidDataUV = false;
    Input.DataUVState = EDWCEditorBuildActionState::Unavailable;
    Input.WrinkleTexturesState = EDWCEditorBuildActionState::Required;

    const FDWCEditorBuildStatusSnapshot Snapshot = FDWCEditorBuildActionEvaluator::Evaluate(Input);
    const FDWCEditorBuildActionStatus* Wrinkle = Snapshot.Find(EDWCEditorBuildAction::BakeWrinkleTextures);
    TestEqual(TEXT("Wrinkle bake is blocked without Data UV"),
        Wrinkle->State, EDWCEditorBuildActionState::Blocked);
    TestTrue(TEXT("Data UV is reported as the blocker"),
        Wrinkle->BlockingActions.Contains(EDWCEditorBuildAction::InitializeDataUV));

    const EDWCEditorBuildAction Requested = EDWCEditorBuildAction::BakeWrinkleTextures;
    const FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveActions(Snapshot, MakeArrayView(&Requested, 1));
    TestFalse(TEXT("Blocked plan is not executable"), Plan.IsExecutable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildPlanOrderingTest,
    "DWC.Editor.Foundation.Build.PlanOrdering",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildPlanOrderingTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildEvaluationInput Input = MakeReadyInput();
    Input.bAssetDirty = true;
    Input.DataUVState = EDWCEditorBuildActionState::Required;
    Input.CPURuntimeDataState = EDWCEditorBuildActionState::Required;
    Input.RenderProfileState = EDWCEditorBuildActionState::Required;
    Input.GeneratedMaterialsState = EDWCEditorBuildActionState::Required;
    Input.WrinkleTexturesState = EDWCEditorBuildActionState::Required;

    const FDWCEditorBuildStatusSnapshot Snapshot = FDWCEditorBuildActionEvaluator::Evaluate(Input);
    const FDWCEditorBuildActionStatus* WrinkleStatus =
        Snapshot.Find(EDWCEditorBuildAction::BakeWrinkleTextures);
    TestEqual(TEXT("Wrinkle remains a required action while its retryable prerequisite is pending"),
        WrinkleStatus->State, EDWCEditorBuildActionState::Required);
    TestFalse(TEXT("Wrinkle cannot be executed alone before Data UV"),
        WrinkleStatus->IsExecutable());
    TestFalse(TEXT("Materials cannot be generated alone before Data UV"),
        Snapshot.Find(EDWCEditorBuildAction::GenerateMaterials)->IsExecutable());
    const FDWCEditorBuildPlan First = FDWCEditorBuildPlanResolver::ResolveRequired(Snapshot);
    const FDWCEditorBuildPlan Second = FDWCEditorBuildPlanResolver::ResolveRequired(Snapshot);
    TestTrue(TEXT("Required plan is executable"), First.IsExecutable());
    TestEqual(TEXT("Repeated planning is deterministic"), First.Steps.Num(), Second.Steps.Num());
    for (int32 Index = 0; Index < First.Steps.Num() && Index < Second.Steps.Num(); ++Index)
    {
        TestEqual(FString::Printf(TEXT("Step %d is deterministic"), Index),
            First.Steps[Index].Action, Second.Steps[Index].Action);
    }

    const int32 DataUVIndex = FindStep(First, EDWCEditorBuildAction::InitializeDataUV);
    TestTrue(TEXT("Data UV precedes CPU runtime"),
        DataUVIndex < FindStep(First, EDWCEditorBuildAction::BuildCPURuntimeData));
    TestTrue(TEXT("Data UV precedes generated materials"),
        DataUVIndex < FindStep(First, EDWCEditorBuildAction::GenerateMaterials));
    TestTrue(TEXT("Render profile precedes generated materials"),
        FindStep(First, EDWCEditorBuildAction::BakeRenderProfileData) <
        FindStep(First, EDWCEditorBuildAction::GenerateMaterials));
    TestTrue(TEXT("Generated materials precede wrinkle when both are selected"),
        FindStep(First, EDWCEditorBuildAction::GenerateMaterials) <
        FindStep(First, EDWCEditorBuildAction::BakeWrinkleTextures));
    TestEqual(TEXT("Save is the final selected action"),
        First.Steps.Last().Action, EDWCEditorBuildAction::SaveAsset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationBuildActionMappingTest,
    "DWC.Editor.Foundation.Build.ValidationFixMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationBuildActionMappingTest::RunTest(const FString& Parameters)
{
    struct FExpectedMapping
    {
        EWCAValidationFixKind FixKind;
        EDWCEditorBuildAction Action;
    };
    const FExpectedMapping Expected[] = {
        {EWCAValidationFixKind::Save, EDWCEditorBuildAction::SaveAsset},
        {EWCAValidationFixKind::InitializeDataUV, EDWCEditorBuildAction::InitializeDataUV},
        {EWCAValidationFixKind::PrepareRuntimeData, EDWCEditorBuildAction::BuildCPURuntimeData},
        {EWCAValidationFixKind::BakeGPUMaps, EDWCEditorBuildAction::BuildGPURuntimeData},
        {EWCAValidationFixKind::BakeRenderProfileData, EDWCEditorBuildAction::BakeRenderProfileData},
        {EWCAValidationFixKind::GenerateMaterials, EDWCEditorBuildAction::GenerateMaterials},
        {EWCAValidationFixKind::BakeWrinkleMaps, EDWCEditorBuildAction::BakeWrinkleTextures},
        {EWCAValidationFixKind::BakeTransparencyMaps, EDWCEditorBuildAction::BakeTransparencyTextures},
        {EWCAValidationFixKind::RebakeAffectedTransparencyMaps,
            EDWCEditorBuildAction::RebakeAffectedTransparencyMaps}
    };

    for (const FExpectedMapping& Mapping : Expected)
    {
        const TOptional<EDWCEditorBuildAction> Action =
            GetBuildActionForValidationFix(Mapping.FixKind);
        TestTrue(TEXT("Automatic validation fix has a common build action"), Action.IsSet());
        if (Action.IsSet())
        {
            TestEqual(TEXT("Validation fix maps to the expected common action"),
                Action.GetValue(), Mapping.Action);
        }
    }
    TestFalse(TEXT("Manual validation issues are not dispatched automatically"),
        GetBuildActionForValidationFix(EWCAValidationFixKind::Manual).IsSet());
    TestFalse(TEXT("Informational validation issues have no build action"),
        GetBuildActionForValidationFix(EWCAValidationFixKind::None).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildFailedPrerequisiteRetryTest,
    "DWC.Editor.Foundation.Build.FailedPrerequisiteRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildFailedPrerequisiteRetryTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildEvaluationInput Input = MakeReadyInput();
    Input.DataUVState = EDWCEditorBuildActionState::Failed;
    Input.WrinkleTexturesState = EDWCEditorBuildActionState::Required;

    const FDWCEditorBuildStatusSnapshot Snapshot = FDWCEditorBuildActionEvaluator::Evaluate(Input);
    TestEqual(TEXT("A retryable failed prerequisite does not permanently block its dependent"),
        Snapshot.Find(EDWCEditorBuildAction::BakeWrinkleTextures)->State,
        EDWCEditorBuildActionState::Required);

    const EDWCEditorBuildAction Requested = EDWCEditorBuildAction::BakeWrinkleTextures;
    const FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveActions(
        Snapshot, MakeArrayView(&Requested, 1));
    TestTrue(TEXT("The retry plan remains executable"), Plan.IsExecutable());
    TestTrue(TEXT("Failed Data UV is retried"),
        FindStep(Plan, EDWCEditorBuildAction::InitializeDataUV) != INDEX_NONE);
    TestTrue(TEXT("Data UV retry precedes wrinkle bake"),
        FindStep(Plan, EDWCEditorBuildAction::InitializeDataUV) <
        FindStep(Plan, EDWCEditorBuildAction::BakeWrinkleTextures));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildAffectedTransparencySelectionTest,
    "DWC.Editor.Foundation.Build.AffectedTransparencySelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildAffectedTransparencySelectionTest::RunTest(const FString& Parameters)
{
    FDWCEditorBuildEvaluationInput Input = MakeReadyInput();
    Input.TransparencyTexturesState = EDWCEditorBuildActionState::Required;
    Input.AffectedTransparencyState = EDWCEditorBuildActionState::Required;
    Input.AffectedMaterialSlotIndices = {7};
    Input.AffectedLayerGuids = {FGuid::NewGuid()};

    const FDWCEditorBuildStatusSnapshot Snapshot = FDWCEditorBuildActionEvaluator::Evaluate(Input);
    TestEqual(TEXT("Full bake is suppressed when every stale layer is affected-only"),
        Snapshot.Find(EDWCEditorBuildAction::BakeTransparencyTextures)->State,
        EDWCEditorBuildActionState::UpToDate);
    const FDWCEditorBuildActionStatus* Affected = Snapshot.Find(
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps);
    TestEqual(TEXT("Affected rebake remains required"),
        Affected->State, EDWCEditorBuildActionState::Required);
    TestEqual(TEXT("Affected slot metadata is retained"), Affected->MaterialSlotIndices.Num(), 1);
    TestEqual(TEXT("Affected layer metadata is retained"), Affected->LayerGuids.Num(), 1);
    return true;
}

#endif
