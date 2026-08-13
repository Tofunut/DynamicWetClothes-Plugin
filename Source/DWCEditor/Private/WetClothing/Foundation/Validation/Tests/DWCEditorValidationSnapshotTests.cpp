// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSectionRegistry.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluationContext.h"
#include "WetClothing/Foundation/Validation/DWCGeneratedMaterialValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCRuntimeValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCTransparencyLayerValidationEvaluator.h"
#include "WetClothing/Foundation/Validation/DWCWetPartValidationEvaluator.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Foundation/Build/DWCWrinkleBuildTargetResolver.h"
#include "WetClothing/Foundation/Validation/DWCWrinkleValidationEvaluator.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationStateContractTest,
    "DWC.Editor.Foundation.Validation.StateContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationStateContractTest::RunTest(const FString& Parameters)
{
    FDWCEditorValidationNode Node;
    Node.Intent = EDWCEditorValidationIntentState::NotConfigured;
    Node.Artifact = EDWCEditorValidationArtifactState::NotRequired;
    TestFalse(TEXT("Not-configured content does not require a runtime output"), Node.RequiresRuntimeOutput());
    TestEqual(TEXT("Not-configured content remains healthy"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::NotConfigured);

    Node.Intent = EDWCEditorValidationIntentState::Draft;
    Node.Artifact = EDWCEditorValidationArtifactState::Missing;
    TestFalse(TEXT("Draft content does not require a runtime output"), Node.RequiresRuntimeOutput());
    TestEqual(TEXT("Draft intent gates an absent runtime artifact"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::Draft);

    Node.Intent = EDWCEditorValidationIntentState::Disabled;
    Node.Artifact = EDWCEditorValidationArtifactState::Stale;
    TestFalse(TEXT("Disabled content does not require a runtime output"), Node.RequiresRuntimeOutput());
    TestEqual(TEXT("Disabled intent gates a stale runtime artifact"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::Disabled);

    Node.Intent = EDWCEditorValidationIntentState::Enabled;
    Node.Artifact = EDWCEditorValidationArtifactState::Missing;
    TestTrue(TEXT("Enabled content requires a runtime output"), Node.RequiresRuntimeOutput());
    TestEqual(TEXT("A required absent output is missing"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::Missing);

    Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
    TestEqual(TEXT("A blocked prerequisite takes precedence over a missing artifact"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::Blocked);

    Node.Operation = EDWCEditorValidationOperationState::Failed;
    TestEqual(TEXT("An operation failure has the highest precedence"), Node.GetOverallState(),
        EDWCEditorValidationOverallState::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationDisplayGroupingTest,
    "DWC.Editor.Foundation.Validation.DisplayGrouping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationDisplayGroupingTest::RunTest(const FString& Parameters)
{
    FWCAValidationReport Report;
    FWCAValidationIssue& First = Report.Issues.AddDefaulted_GetRef();
    First.IssueId = TEXT("Transparency.Output.Stale");
    First.Section = EWCAValidationSection::TransparencyMaps;
    First.Remediation = EDWCEditorValidationRemediation::BuildAction;
    First.BuildAction = EDWCEditorBuildAction::BakeTransparencyTextures;
    First.Severity = EWCAValidationSeverity::Warning;
    First.Status = FText::FromString(TEXT("Out of Date"));
    First.Detail = FText::FromString(TEXT("The output signature is stale."));
    First.RequiredAction = FText::FromString(TEXT("Bake Transparency Textures."));
    First.Target.MaterialSlotIndex = 3;

    FWCAValidationIssue Second = First;
    Second.Target.MaterialSlotIndex = 7;
    Report.Issues.Add(Second);
    TestEqual(TEXT("Identical issue meaning is grouped across slot contexts"),
        Report.GetDisplayIssueCount(), 1);

    Report.Issues.Last().Detail =
        FText::FromString(TEXT("The canonical Stage 2 artifact is missing."));
    TestEqual(TEXT("Different details remain separate validation rows"),
        Report.GetDisplayIssueCount(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationSnapshotPurityTest,
    "DWC.Editor.Foundation.Validation.CanonicalCaptureIsReadOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationSnapshotPurityTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    TestNotNull(TEXT("Transient WCA was created"), Asset);
    if (Asset == nullptr)
    {
        return false;
    }

    const int32 LayerCountBefore = Asset->Authored.TransparencyData.TransparencyLayers.Num();
    const FDWCAssetBakeState BakeStateBefore = Asset->GetBakeState();
    const bool bPackageDirtyBefore = Asset->GetOutermost()->IsDirty();

    const FWCAEditorValidationSnapshot Snapshot =
        BuildWCAValidationSnapshot(*Asset, EWCAValidationMode::MetadataOnly);

    TestEqual(TEXT("Snapshot identifies its source asset"), Snapshot.AssetPath, Asset->GetPathName());
    TestEqual(TEXT("Routine validation records metadata-only access"),
        Snapshot.Access, EDWCEditorValidationAccess::MetadataOnly);
    TestFalse(TEXT("Routine validation does not claim exact payload access"),
        Snapshot.bDeepValidation);
    TestEqual(TEXT("Transparency layer count is unchanged"),
        Asset->Authored.TransparencyData.TransparencyLayers.Num(), LayerCountBefore);
    TestEqual(TEXT("Bake status is unchanged"),
        static_cast<uint8>(Asset->GetBakeState().TransparencyMaps),
        static_cast<uint8>(BakeStateBefore.TransparencyMaps));
    TestEqual(TEXT("Package dirty state is unchanged"),
        Asset->GetOutermost()->IsDirty(), bPackageDirtyBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBakeOutputFailureOwnershipTest,
    "DWC.Editor.Foundation.Validation.BakeOutputFailureOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBakeOutputFailureOwnershipTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    TestNotNull(TEXT("Transient WCA was created"), Asset);
    if (Asset == nullptr)
    {
        return false;
    }

    Asset->SetWrinkleBakeStatus(EDWCBakeStatus::Failed, TEXT("Wrinkle bake failed."));
    Asset->SetTransparencyBakeStatus(EDWCBakeStatus::Failed, TEXT("Transparency bake failed."));

    TestEqual(
        TEXT("Wrinkle owns its failure message"),
        Asset->GetBakeOutputFailureMessage(DWCBakeOutput::WrinkleMaps),
        FString(TEXT("Wrinkle bake failed.")));
    TestEqual(
        TEXT("Transparency owns its failure message"),
        Asset->GetBakeOutputFailureMessage(DWCBakeOutput::TransparencyMaps),
        FString(TEXT("Transparency bake failed.")));

    const FWCAEditorValidationSnapshot FailureSnapshot =
        BuildWCAValidationSnapshot(*Asset, EWCAValidationMode::MetadataOnly);
    TestTrue(
        TEXT("Validation independently exposes the wrinkle failure"),
        FailureSnapshot.Diagnostics.ContainsByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.bFailed &&
                       Diagnostic.Target.SubResource == TEXT("WrinkleMaps") &&
                       Diagnostic.Presentation.Detail.ToString() == TEXT("Wrinkle bake failed.");
            }));
    TestTrue(
        TEXT("Validation independently exposes the transparency failure"),
        FailureSnapshot.Diagnostics.ContainsByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.bFailed &&
                       Diagnostic.Target.SubResource == TEXT("TransparencyMaps") &&
                       Diagnostic.Presentation.Detail.ToString() == TEXT("Transparency bake failed.");
            }));

    Asset->SetTransparencyBakeStatus(EDWCBakeStatus::Valid);
    TestFalse(
        TEXT("A successful transparency output clears only its own failure"),
        Asset->HasBakeOutputFailure(DWCBakeOutput::TransparencyMaps));
    TestTrue(
        TEXT("A successful transparency output preserves the wrinkle failure"),
        Asset->HasBakeOutputFailure(DWCBakeOutput::WrinkleMaps));
    TestEqual(
        TEXT("The preserved wrinkle status remains failed"),
        Asset->GetBakeOutputStatus(DWCBakeOutput::WrinkleMaps),
        EDWCBakeStatus::Failed);

    Asset->ClearBakeOutputFailure(DWCBakeOutput::WrinkleMaps);
    TestFalse(
        TEXT("Targeted failure cleanup removes the requested output"),
        Asset->HasBakeOutputFailure(DWCBakeOutput::WrinkleMaps));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorEvaluatorOutputOwnershipTest,
    "DWC.Editor.Foundation.Validation.EvaluatorOutputOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorEvaluatorOutputOwnershipTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset))
    {
        return false;
    }

    const FString SharedFailure = TEXT("A deliberately shared failure message.");
    Asset->SetBakeOutputStatus(
        DWCBakeOutput::OriginalUVTopology,
        EDWCBakeStatus::Failed,
        SharedFailure);
    Asset->SetTransparencyBakeStatus(EDWCBakeStatus::Failed, SharedFailure);
    Asset->SetRenderProfileBakeStatus(EDWCBakeStatus::Failed, SharedFailure);

    const FWCAEditorValidationSnapshot Snapshot =
        BuildWCAValidationSnapshot(*Asset, EWCAValidationMode::MetadataOnly);
    for (const int32 Output : {
             DWCBakeOutput::OriginalUVTopology,
             DWCBakeOutput::TransparencyMaps,
             DWCBakeOutput::RenderProfileData})
    {
        int32 OwnerCount = 0;
        for (const FDWCEditorValidationDiagnostic& Diagnostic : Snapshot.Diagnostics)
        {
            if (Diagnostic.bFailed && Diagnostic.OwnedBakeOutput == Output)
            {
                ++OwnerCount;
            }
        }
        TestEqual(
            *FString::Printf(TEXT("Output %d has exactly one canonical failure owner"), Output),
            OwnerCount,
            1);
    }

    TestFalse(
        TEXT("Owned evaluator failures do not fall through to Internal Failure"),
        Snapshot.Diagnostics.ContainsByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.Target.Domain == EDWCEditorValidationDomain::Failure &&
                       (Diagnostic.OwnedBakeOutput == DWCBakeOutput::OriginalUVTopology ||
                        Diagnostic.OwnedBakeOutput == DWCBakeOutput::TransparencyMaps ||
                        Diagnostic.OwnedBakeOutput == DWCBakeOutput::RenderProfileData);
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBakeOutputCatalogTest,
    "DWC.Editor.Foundation.Validation.BakeOutputCatalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBakeOutputCatalogTest::RunTest(const FString& Parameters)
{
    int32 CombinedMask = 0;
    for (const int32 Output : DWCBakeOutput::GetOutputs())
    {
        TestTrue(TEXT("Every catalog entry is a single bit"), Output > 0 && (Output & (Output - 1)) == 0);
        TestFalse(TEXT("Bake output catalog contains no duplicates"), DWCBakeOutput::Has(CombinedMask, Output));
        CombinedMask |= Output;
    }
    TestEqual(TEXT("Bake output catalog matches DWCBakeOutput::All"), CombinedMask, DWCBakeOutput::All);
    TestTrue(TEXT("Render Profile is a first-class bake output"),
        DWCBakeOutput::Has(CombinedMask, DWCBakeOutput::RenderProfileData));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRenderProfileBakeContentContractTest,
    "DWC.Editor.Foundation.Validation.RenderProfileBakeContentContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRenderProfileBakeContentContractTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset))
    {
        return false;
    }

    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 4;
    Slot.bIsWettableSlot = true;
    Asset->MarkRenderProfileBakeOutOfDate();
    TestFalse(TEXT("A wettable slot without an assigned Wet Part is not bakeable"),
        Asset->HasRenderProfileBakeContent());
    TestEqual(TEXT("A non-bakeable Render Profile output is disabled"),
        Asset->GetBakeOutputStatus(DWCBakeOutput::RenderProfileData),
        EDWCBakeStatus::Disabled);

    FWetClothingWetPartEntry& Entry = Slot.WetPartEntries.AddDefaulted_GetRef();
    Entry.WetPartID = 1;
    Asset->MarkRenderProfileBakeOutOfDate();
    TestFalse(TEXT("A Wet Part without assigned UV islands is not bakeable"),
        Asset->HasRenderProfileBakeContent());

    Entry.AssignedUVIslandIDs.Add(7);
    Asset->MarkRenderProfileBakeOutOfDate();
    TestTrue(TEXT("An assigned Wet Part requires Render Profile data"),
        Asset->HasRenderProfileBakeContent());
    TestEqual(TEXT("The first Render Profile build is required"),
        Asset->GetBakeOutputStatus(DWCBakeOutput::RenderProfileData),
        EDWCBakeStatus::Required);

    Asset->MarkBakeOutputGenerated(DWCBakeOutput::RenderProfileData);
    TestTrue(TEXT("A generated Render Profile output is save-pending"),
        Asset->IsBakeOutputSavePending(DWCBakeOutput::RenderProfileData));
    Asset->MarkBakeOutputsSaved(DWCBakeOutput::RenderProfileData);
    TestFalse(TEXT("A saved Render Profile output is no longer save-pending"),
        Asset->IsBakeOutputSavePending(DWCBakeOutput::RenderProfileData));

    Asset->MarkRenderProfileBakeOutOfDate();
    TestEqual(TEXT("An authored change makes a prior Render Profile output stale"),
        Asset->GetBakeOutputStatus(DWCBakeOutput::RenderProfileData),
        EDWCBakeStatus::OutOfDate);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationPresentationIndependenceTest,
    "DWC.Editor.Foundation.Validation.PresentationIndependence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationPresentationIndependenceTest::RunTest(const FString& Parameters)
{
    FWCAEditorValidationSnapshot Snapshot;
    const FGuid LayerGuid = FGuid::NewGuid();
    const FDWCEditorValidationTargetKey Key{
        EDWCEditorValidationDomain::Transparency,
        6,
        LayerGuid};
    FDWCEditorValidationNode& Node =
        DWCEditorValidation::FindOrAddNode(Snapshot, Key);
    Node.Intent = EDWCEditorValidationIntentState::Enabled;
    Node.Artifact = EDWCEditorValidationArtifactState::Stale;
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        TEXT("Transparency.Output.Stale"),
        EDWCEditorValidationSeverity::Warning,
        FText::FromString(TEXT("Localized title A")),
        FText::FromString(TEXT("Localized status A")),
        FText::FromString(TEXT("Localized detail A")),
        FText::FromString(TEXT("Localized action A")),
        EDWCEditorValidationRemediation::BuildAction,
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps);

    const FDWCEditorValidationActionState* Action =
        Snapshot.FindAction(EDWCEditorBuildAction::RebakeAffectedTransparencyMaps);
    TestNotNull(TEXT("Suggested build action is indexed by the snapshot"), Action);
    if (Action != nullptr)
    {
        TestEqual(TEXT("The action retains its target"), Action->Targets.Num(), 1);
        TestEqual(TEXT("The action is required"), Action->State, EDWCEditorBuildActionState::Required);
    }

    const FWCAValidationReport FirstReport =
        FDWCEditorValidationReportAdapter::BuildReport(Snapshot);
    Snapshot.Diagnostics[0].Presentation.Title = FText::FromString(TEXT("Completely different title"));
    Snapshot.Diagnostics[0].Presentation.Status = FText::FromString(TEXT("Different status"));
    Snapshot.Diagnostics[0].Presentation.Detail = FText::FromString(TEXT("No state keywords are present"));
    const FWCAValidationReport SecondReport =
        FDWCEditorValidationReportAdapter::BuildReport(Snapshot);
    if (!TestEqual(TEXT("Both presentations contain one issue"), FirstReport.Issues.Num(), 1) ||
        !TestEqual(TEXT("Changed presentation contains one issue"), SecondReport.Issues.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("Issue code is independent of presentation"),
        FirstReport.Issues[0].IssueId, SecondReport.Issues[0].IssueId);
    TestEqual(TEXT("Section is independent of presentation"),
        FirstReport.Issues[0].Section, SecondReport.Issues[0].Section);
    TestEqual(TEXT("Build action is independent of presentation"),
        FirstReport.Issues[0].BuildAction, SecondReport.Issues[0].BuildAction);
    TestEqual(TEXT("Target slot is preserved"), SecondReport.Issues[0].Target.MaterialSlotIndex, 6);
    TestEqual(TEXT("Target layer is preserved"), SecondReport.Issues[0].Target.LayerGuid, LayerGuid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationTargetIsolationTest,
    "DWC.Editor.Foundation.Validation.TargetIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationTargetIsolationTest::RunTest(const FString& Parameters)
{
    FWCAEditorValidationSnapshot First;
    for (const int32 SlotIndex : {3, 8})
    {
        const FDWCEditorValidationTargetKey Key{
            EDWCEditorValidationDomain::Wrinkle,
            SlotIndex};
        FDWCEditorValidationNode& Node =
            DWCEditorValidation::FindOrAddNode(First, Key);
        Node.Intent = EDWCEditorValidationIntentState::Enabled;
        Node.Artifact = EDWCEditorValidationArtifactState::Stale;
        DWCEditorValidation::AddDiagnostic(
            First,
            Node,
            FName(*FString::Printf(TEXT("WrinkleStale_Slot%d"), SlotIndex)),
            EDWCEditorValidationSeverity::Warning,
            FText::GetEmpty(),
            FText::GetEmpty(),
            FText::GetEmpty(),
            FText::GetEmpty(),
            EDWCEditorValidationRemediation::BuildAction,
            EDWCEditorBuildAction::BakeWrinkleTextures);
    }
    const FWCAEditorValidationSnapshot Second = First;

    TestEqual(TEXT("Each material slot owns a separate validation node"), First.Nodes.Num(), 2);
    TestEqual(TEXT("Slot 3 resolves exactly one node"), First.FindMaterialSlotNodes(3).Num(), 1);
    TestEqual(TEXT("Slot 8 resolves exactly one node"), First.FindMaterialSlotNodes(8).Num(), 1);
    TestEqual(TEXT("Repeated capture keeps node count deterministic"), Second.Nodes.Num(), First.Nodes.Num());
    TestEqual(TEXT("Repeated capture keeps diagnostic count deterministic"),
        Second.Diagnostics.Num(), First.Diagnostics.Num());

    const FDWCEditorValidationActionState* Action =
        First.FindAction(EDWCEditorBuildAction::BakeWrinkleTextures);
    TestNotNull(TEXT("Shared action is indexed"), Action);
    if (Action != nullptr)
    {
        TestEqual(TEXT("Shared action retains both material-slot targets"), Action->Targets.Num(), 2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWetPartCanonicalValidationTest,
    "DWC.Editor.Foundation.Validation.WetPartCanonicalState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWetPartCanonicalValidationTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset) ||
        !TestNotNull(TEXT("Transient mesh was created"), Mesh))
    {
        return false;
    }
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;

    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;
    FWetClothingWetPartEntry& First = Slot.WetPartEntries.AddDefaulted_GetRef();
    First.WetPartID = 1;
    First.ProfileIndex = 7;
    First.AssignedUVIslandIDs.Add(3);
    FWetClothingWetPartEntry& Duplicate = Slot.WetPartEntries.AddDefaulted_GetRef();
    Duplicate.WetPartID = 1;
    Duplicate.ProfileIndex = 0;
    Duplicate.AssignedUVIslandIDs.Add(3);

    FDWCEditorValidationEvaluationContext Context(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    FWCAEditorValidationSnapshot Snapshot;
    FDWCWetPartValidationEvaluator::AppendToSnapshot(Context, Snapshot);

    TestEqual(TEXT("Wet Part evaluator owns one slot node"), Snapshot.Nodes.Num(), 1);
    TestEqual(TEXT("Wet Part diagnostics preserve their slot"),
        Snapshot.Diagnostics.FilterByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.Target.MaterialSlotIndex == 0;
            }).Num(),
        Snapshot.Diagnostics.Num());
    TestTrue(TEXT("Duplicate IDs, profiles, and island ownership are blocking"), Snapshot.HasBlockingErrors());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCGeneratedMaterialStructuredOwnershipTest,
    "DWC.Editor.Foundation.Validation.GeneratedMaterialStructuredOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCGeneratedMaterialStructuredOwnershipTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset) ||
        !TestNotNull(TEXT("Transient mesh was created"), Mesh))
    {
        return false;
    }
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    Asset->Metadata.SetupSettings.bBuildCPUVertexSimulationData = true;
    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;

    TArray<FWCAGeneratedMaterialValidationIssue> Issues;
    FWCAMaterialGenerator::ValidateGeneratedMaterialOverridesStructured(Asset, false, Issues);
    const FWCAGeneratedMaterialValidationIssue* SlotIssue = Issues.FindByPredicate(
        [](const FWCAGeneratedMaterialValidationIssue& Issue)
        {
            return Issue.MaterialSlotIndex == 0;
        });
    TestNotNull(TEXT("Structured validation reports the owning slot without parsing text"), SlotIssue);
    if (SlotIssue != nullptr)
    {
        TestEqual(TEXT("Missing source material has a stable code"),
            SlotIssue->Code,
            FName(TEXT("SourceMaterialMissing")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCGeneratedMaterialRetryablePrerequisiteTest,
    "DWC.Editor.Foundation.Validation.GeneratedMaterialRetryablePrerequisite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCGeneratedMaterialRetryablePrerequisiteTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    Asset->Metadata.SetupSettings.bBuildCPUVertexSimulationData = true;
    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;

    const FDWCEditorValidationEvaluationContext Context(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    FWCAEditorValidationSnapshot Snapshot;
    FDWCGeneratedMaterialValidationEvaluator::AppendToSnapshot(Context, Snapshot);

    const FDWCEditorValidationActionState* Action =
        Snapshot.FindAction(EDWCEditorBuildAction::GenerateMaterials);
    if (TestNotNull(TEXT("Generated Material action state exists"), Action))
    {
        TestEqual(TEXT("Missing Data UV is retryable"), Action->State,
            EDWCEditorBuildActionState::Required);
        TestTrue(TEXT("Data UV initialization is retained as the prerequisite"),
            Action->BlockingActions.Contains(EDWCEditorBuildAction::InitializeDataUV));
    }
    TestTrue(TEXT("The retryable issue remains automatically resolvable"),
        Snapshot.Diagnostics.ContainsByPredicate(
            [](const FDWCEditorValidationDiagnostic& Diagnostic)
            {
                return Diagnostic.SuggestedAction == EDWCEditorBuildAction::GenerateMaterials &&
                       Diagnostic.Remediation == EDWCEditorValidationRemediation::BuildAction;
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRuntimeDisabledBackendValidationTest,
    "DWC.Editor.Foundation.Validation.RuntimeDisabledBackends",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRuntimeDisabledBackendValidationTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    Asset->Metadata.SetupSettings.bBuildCPUVertexSimulationData = false;
    Asset->Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData = false;
    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;

    FDWCEditorValidationEvaluationContext Context(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    FWCAEditorValidationSnapshot Snapshot;
    FDWCRuntimeValidationEvaluator::AppendToSnapshot(Context, Snapshot);

    const FDWCEditorValidationActionState* CPU =
        Snapshot.FindAction(EDWCEditorBuildAction::BuildCPURuntimeData);
    const FDWCEditorValidationActionState* GPU =
        Snapshot.FindAction(EDWCEditorBuildAction::BuildGPURuntimeData);
    TestNotNull(TEXT("CPU action state exists"), CPU);
    TestNotNull(TEXT("GPU action state exists"), GPU);
    if (CPU != nullptr)
    {
        TestEqual(TEXT("Disabled CPU backend is unavailable"),
            CPU->State,
            EDWCEditorBuildActionState::Unavailable);
    }
    if (GPU != nullptr)
    {
        TestEqual(TEXT("Disabled GPU backend is unavailable"),
            GPU->State,
            EDWCEditorBuildActionState::Unavailable);
    }
    TestEqual(TEXT("Disabled backends emit no diagnostics"), Snapshot.Diagnostics.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRuntimeRetryablePrerequisiteTest,
    "DWC.Editor.Foundation.Validation.RuntimeRetryablePrerequisite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRuntimeRetryablePrerequisiteTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    Asset->Metadata.SetupSettings.bBuildCPUVertexSimulationData = false;
    Asset->Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData = true;
    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;

    const FDWCEditorValidationEvaluationContext Context(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    FWCAEditorValidationSnapshot Snapshot;
    FDWCRuntimeValidationEvaluator::AppendToSnapshot(Context, Snapshot);

    const FDWCEditorValidationActionState* Action =
        Snapshot.FindAction(EDWCEditorBuildAction::BuildGPURuntimeData);
    if (TestNotNull(TEXT("GPU Runtime action state exists"), Action))
    {
        TestEqual(TEXT("Missing Data UV is retryable"), Action->State,
            EDWCEditorBuildActionState::Required);
        TestTrue(TEXT("Data UV initialization is retained as the prerequisite"),
            Action->BlockingActions.Contains(EDWCEditorBuildAction::InitializeDataUV));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCRuntimeGPURebuildAdmissionTest,
    "DWC.Editor.Foundation.Validation.RuntimeGPURebuildAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCRuntimeGPURebuildAdmissionTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    Asset->Metadata.DWCDataUVChannelIndex = 1;
    Asset->Metadata.SetupSettings.bBuildCPUVertexSimulationData = false;
    Asset->Metadata.SetupSettings.bBuildGPUWetnessMapSimulationData = true;

    FDWCDataUVLODMetadata& DataUV = Asset->Derived.Inline.DataUVMetadata.AddDefaulted_GetRef();
    DataUV.bIsValid = true;
    DataUV.LODIndex = 0;
    DataUV.RenderVertexCount = 1;
    DataUV.UVChannelIndex = 1;
    DataUV.GeneratorVersion = DWCGeneratedDataVersion::DataUV;

    FWetClothingAuthoredMaterialSlot& Slot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Slot.MaterialSlotIndex = 0;
    Slot.bIsWettableSlot = true;

    FDWCEditorValidationEvaluationContext Context(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    FWCAEditorValidationSnapshot Snapshot;
    FDWCRuntimeValidationEvaluator::AppendToSnapshot(Context, Snapshot);

    const FDWCEditorValidationActionState* GPU =
        Snapshot.FindAction(EDWCEditorBuildAction::BuildGPURuntimeData);
    TestNotNull(TEXT("GPU action state exists"), GPU);
    if (GPU != nullptr)
    {
        TestEqual(TEXT("Missing GPU runtime payload remains rebuildable"),
            GPU->State,
            EDWCEditorBuildActionState::Required);
        TestEqual(TEXT("GPU rebuild does not block itself"), GPU->BlockingActions.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyIntentValidationTest,
    "DWC.Editor.Foundation.Validation.TransparencyIntent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyIntentValidationTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset) ||
        !TestNotNull(TEXT("Transient skeletal mesh was created"), Mesh))
    {
        return false;
    }
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    FWetClothingAuthoredMaterialSlot& WettableSlot =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    WettableSlot.MaterialSlotIndex = 0;
    WettableSlot.bIsWettableSlot = true;

    FWCAEditorValidationSnapshot Snapshot;
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    TestEqual(TEXT("Transparency owns one root node and one Wettable slot node"), Snapshot.Nodes.Num(), 2);
    TArray<const FDWCEditorValidationNode*> SlotNodes = Snapshot.FindMaterialSlotNodes(0);
    TestEqual(TEXT("An unconfigured Wettable slot owns one target node"), SlotNodes.Num(), 1);
    if (SlotNodes.Num() == 1)
    {
        TestEqual(TEXT("No layer is a healthy NotConfigured state"), SlotNodes[0]->Intent,
            EDWCEditorValidationIntentState::NotConfigured);
        TestEqual(TEXT("No layer requires no runtime artifact"), SlotNodes[0]->Artifact,
            EDWCEditorValidationArtifactState::NotRequired);
    }
    TestEqual(TEXT("No layer emits no validation diagnostic"), Snapshot.Diagnostics.Num(), 0);

    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 0;
    Layer.Intent = EDWCTransparencyLayerIntent::Draft;
    Snapshot = {};
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    SlotNodes = Snapshot.FindMaterialSlotNodes(0);
    TestEqual(TEXT("Draft authoring owns one target node"), SlotNodes.Num(), 1);
    if (SlotNodes.Num() == 1)
    {
        TestEqual(TEXT("Explicit unfinished authoring is Draft"), SlotNodes[0]->Intent,
            EDWCEditorValidationIntentState::Draft);
    }
    const FDWCEditorValidationActionState* DraftAction =
        Snapshot.FindAction(EDWCEditorBuildAction::BakeTransparencyTextures);
    TestTrue(TEXT("Draft content does not require a build action"),
        DraftAction == nullptr ||
        (DraftAction->State != EDWCEditorBuildActionState::Required &&
         DraftAction->State != EDWCEditorBuildActionState::Failed));
    TestFalse(TEXT("Draft content does not count as runtime bake content"),
        Asset->HasTransparencyBakeContent());

    Layer.Intent = EDWCTransparencyLayerIntent::Disabled;
    Layer.SourceType = EDWCTransparencySourceType::ManualColorOrTexture;
    FWetClothingBakedTransparencyMap& BakedMap = Layer.BakedMaps.AddDefaulted_GetRef();
    BakedMap.MaterialSlotIndex = 0;
    BakedMap.TransparencyMap = NewObject<UTexture2D>(GetTransientPackage());
    BakedMap.BakeGuid = FGuid::NewGuid();
    BakedMap.BuildSignature = TEXT("IntentTest");
    BakedMap.bContainsColorRGB = true;
    BakedMap.bContainsTransparencyAlpha = true;
    Snapshot = {};
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    SlotNodes = Snapshot.FindMaterialSlotNodes(0);
    TestEqual(TEXT("Disabled authoring owns one target node"), SlotNodes.Num(), 1);
    if (SlotNodes.Num() == 1)
    {
        TestEqual(TEXT("Disabled authored content remains disabled"), SlotNodes[0]->Intent,
            EDWCEditorValidationIntentState::Disabled);
    }
    TestFalse(TEXT("Disabled content does not count as runtime bake content"),
        Asset->HasTransparencyBakeContent());
    TestNull(TEXT("Disabled content is excluded from runtime lookup"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(0));

    Layer.Intent = EDWCTransparencyLayerIntent::Enabled;
    TestTrue(TEXT("Only enabled content participates in runtime baking"),
        Asset->HasTransparencyBakeContent());
    TestNotNull(TEXT("Enabled valid content participates in runtime lookup"),
        Asset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(0));
    Snapshot = {};
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, true, Snapshot);
    SlotNodes = Snapshot.FindMaterialSlotNodes(0);
    TestEqual(TEXT("Enabled authoring owns one target node"), SlotNodes.Num(), 1);
    if (SlotNodes.Num() == 1)
    {
        TestEqual(TEXT("Invalid enabled input is reported"), SlotNodes[0]->Input,
            EDWCEditorValidationInputState::Invalid);
    }
    TestTrue(TEXT("Invalid enabled input emits a blocking diagnostic"),
        Snapshot.HasBlockingErrors());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyLegacyIntentNormalizationTest,
    "DWC.Editor.Foundation.Validation.TransparencyLegacyIntentNormalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyLegacyIntentNormalizationTest::RunTest(const FString& Parameters)
{
    FWetClothingTransparencyData Data;
    Data.DataVersion = FWetClothingTransparencyData::PerLayerResolutionDataVersion;

    Data.TransparencyLayers.AddDefaulted();
    Data.TransparencyLayers[0].TargetSurface.OuterMaterialSlotIndex = 2;
    Data.TransparencyLayers[0].Intent = EDWCTransparencyLayerIntent::Enabled;

    FWetClothingTransparencyLayerData& AuthoredLayer =
        Data.TransparencyLayers.AddDefaulted_GetRef();
    AuthoredLayer.TargetSurface.OuterMaterialSlotIndex = 4;
    AuthoredLayer.Intent = EDWCTransparencyLayerIntent::Enabled;
    AuthoredLayer.bSourceTypeConfigured = true;

    int32 DraftCount = 0;
    int32 RepairedIdentityCount = 0;
    TestTrue(TEXT("Pre-v14 data is normalized"),
        Data.NormalizeLegacyLayerIntents(
            TEXT("/DWC/Test/LegacyTransparency"),
            DraftCount,
            RepairedIdentityCount));
    TestEqual(TEXT("Only the empty selection-era placeholder becomes Draft"), DraftCount, 1);
    TestEqual(TEXT("Missing identities are repaired"), RepairedIdentityCount, 2);
    TestEqual(TEXT("Empty placeholder is Draft"), Data.TransparencyLayers[0].Intent,
        EDWCTransparencyLayerIntent::Draft);
    TestEqual(TEXT("Authored content remains Enabled"), Data.TransparencyLayers[1].Intent,
        EDWCTransparencyLayerIntent::Enabled);
    TestTrue(TEXT("First repaired identity is valid"), Data.TransparencyLayers[0].LayerGuid.IsValid());
    TestTrue(TEXT("Second repaired identity is valid"), Data.TransparencyLayers[1].LayerGuid.IsValid());
    TestNotEqual(TEXT("Repaired identities are unique"),
        Data.TransparencyLayers[0].LayerGuid, Data.TransparencyLayers[1].LayerGuid);
    TestEqual(TEXT("Data version advances to the current contract"),
        Data.DataVersion, FWetClothingTransparencyData::CurrentDataVersion);

    DraftCount = -1;
    RepairedIdentityCount = -1;
    TestFalse(TEXT("Normalization is idempotent"),
        Data.NormalizeLegacyLayerIntents(
            TEXT("/DWC/Test/LegacyTransparency"),
            DraftCount,
            RepairedIdentityCount));
    TestEqual(TEXT("Second normalization performs no Draft conversion"), DraftCount, 0);
    TestEqual(TEXT("Second normalization performs no identity repair"),
        RepairedIdentityCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWrinkleCanonicalSourceAndOutputTest,
    "DWC.Editor.Foundation.Validation.WrinkleSourceAndOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWrinkleCanonicalSourceAndOutputTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset) ||
        !TestNotNull(TEXT("Transient skeletal mesh was created"), Mesh))
    {
        return false;
    }
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    FWetClothingAuthoredMaterialSlot& Wettable =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Wettable.MaterialSlotIndex = 0;
    Wettable.bIsWettableSlot = true;

    FDWCWrinkleBuildTargetSnapshot Targets =
        FDWCWrinkleBuildTargetResolver::Resolve(
            *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(TEXT("Unconfigured wrinkle bake is unavailable"),
        Targets.BakeState, EDWCEditorBuildActionState::Unavailable);
    TestEqual(TEXT("A Wettable slot still owns a canonical target"), Targets.Targets.Num(), 1);

    FWetWrinkleRuntimeNormalSource& Custom =
        Asset->Authored.WrinkleData.RuntimeNormalSources.AddDefaulted_GetRef();
    Custom.MaterialSlotIndex = 0;
    Custom.Source = EDWCWrinkleNormalSource::CustomTexture;
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(TEXT("Missing custom normal requires manual repair"),
        Targets.Targets[0].Requirement, EDWCWrinkleBuildRequirement::ManualRepair);
    TestEqual(TEXT("A custom-only slot does not request wrinkle baking"),
        Targets.BakeState, EDWCEditorBuildActionState::Unavailable);

    Custom.CustomWrinkleNormalMap = NewObject<UTexture2D>(GetTransientPackage());
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestTrue(TEXT("A valid custom normal is runtime-ready"), Targets.Targets[0].bHasRuntimeNormal);
    TestEqual(TEXT("A valid custom normal needs no baked output"),
        Targets.BakeState, EDWCEditorBuildActionState::Unavailable);

    Asset->Authored.WrinkleData.RuntimeNormalSources.Reset();
    FWetWrinklePatchPlacement& Patch =
        Asset->Authored.WrinkleData.EditablePatches.AddDefaulted_GetRef();
    Patch.MaterialSlotIndex = 0;
    Patch.WrinkleNormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    Patch.bHasSurfaceAnchor = true;
    Patch.AnchorTriangleID = 0;
    Patch.AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
    Patch.bHasSurfaceFrame = true;
    Patch.SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
    Patch.SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
    Patch.bHasSurfaceFootprint = true;
    Patch.SurfaceHalfExtentLocal = FVector2f(1.0f, 1.0f);

    Targets = FDWCWrinkleBuildTargetResolver::Resolve(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(TEXT("Valid authored patches require a missing bake"),
        Targets.BakeState, EDWCEditorBuildActionState::Required);
    TestEqual(TEXT("Missing outputs are buildable"),
        Targets.Targets[0].Requirement, EDWCWrinkleBuildRequirement::Bake);

    FWetWrinkleBakedMapSet& Baked =
        Asset->Authored.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    Baked.MaterialSlotIndex = 0;
    Baked.BakedWrinkleNormalMap = NewObject<UTexture2D>(GetTransientPackage());
    Baked.BakedWrinkleMask = NewObject<UTexture2D>(GetTransientPackage());
    Baked.Resolution = Asset->GetWrinkleMapResolution();
    Baked.PaddingPixels = Asset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Baked.BuildSignature = TEXT("FastValidationIdentity");
    Baked.BakeGuid = FGuid::NewGuid();
    Asset->SetWrinkleBakeStatus(EDWCBakeStatus::Valid);
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(
        *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(TEXT("Fast validation accepts complete identified outputs"),
        Targets.BakeState, EDWCEditorBuildActionState::UpToDate);

    FWCAEditorValidationSnapshot Snapshot;
    FDWCWrinkleValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    TestEqual(TEXT("Wrinkle evaluator emits source, runtime, and coverage nodes"),
        Snapshot.Nodes.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCWrinkleCoverageRejectsStaleOutputTest,
    "DWC.Editor.Foundation.Validation.WrinkleCoverageRejectsStaleOutput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCWrinkleCoverageRejectsStaleOutputTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    FWetClothingAuthoredMaterialSlot& Wettable =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Wettable.MaterialSlotIndex = 0;
    Wettable.bIsWettableSlot = true;

    FWetWrinklePatchPlacement& Patch =
        Asset->Authored.WrinkleData.EditablePatches.AddDefaulted_GetRef();
    Patch.MaterialSlotIndex = 0;
    Patch.WrinkleNormalTexture = NewObject<UTexture2D>(GetTransientPackage());
    Patch.bHasSurfaceAnchor = true;
    Patch.AnchorTriangleID = 0;
    Patch.bHasSurfaceFrame = true;
    Patch.bHasSurfaceFootprint = true;
    Patch.SurfaceHalfExtentLocal = FVector2f(1.0f, 1.0f);

    FWetWrinkleBakedMapSet& Baked =
        Asset->Authored.WrinkleData.BakedWrinkleMaps.AddDefaulted_GetRef();
    Baked.MaterialSlotIndex = 0;
    Baked.BakedWrinkleNormalMap = NewObject<UTexture2D>(GetTransientPackage());
    Baked.BakedWrinkleMask = NewObject<UTexture2D>(GetTransientPackage());
    Baked.Resolution = Asset->GetWrinkleMapResolution();
    Baked.PaddingPixels = Asset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Baked.BuildSignature = TEXT("OldSignature");
    Baked.BakeGuid = FGuid::NewGuid();

    const FDWCWrinkleSuppressionDependencySnapshot Dependency =
        FDWCWrinkleSuppressionCoverageService::ResolveDependency(Asset, 0, true);
    TestEqual(TEXT("An old wrinkle mask is not exposed as a current dependency"),
        Dependency.Status, EDWCWrinkleSuppressionDependencyStatus::Stale);
    TestFalse(TEXT("A stale wrinkle mask is unavailable to transparency composition"),
        Dependency.IsAvailable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyDuplicateTargetContractTest,
    "DWC.Editor.Foundation.Validation.TransparencyDuplicateTargetContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyDuplicateTargetContractTest::RunTest(const FString& Parameters)
{
    UWetClothingAsset* Asset = NewObject<UWetClothingAsset>(GetTransientPackage());
    USkeletalMesh* Mesh = NewObject<USkeletalMesh>(GetTransientPackage());
    if (!TestNotNull(TEXT("Transient WCA was created"), Asset) ||
        !TestNotNull(TEXT("Transient skeletal mesh was created"), Mesh))
    {
        return false;
    }
    Mesh->GetMaterials().AddDefaulted();
    Asset->Metadata.DWCSkeletalMesh = Mesh;
    FWetClothingAuthoredMaterialSlot& Wettable =
        Asset->Authored.PartData.EditableWetPartData.MaterialSlots.AddDefaulted_GetRef();
    Wettable.MaterialSlotIndex = 0;
    Wettable.bIsWettableSlot = true;

    for (int32 Index = 0; Index < 2; ++Index)
    {
        FWetClothingTransparencyLayerData& Layer =
            Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
        Layer.LayerGuid = FGuid::NewGuid();
        Layer.TargetSurface.OuterMaterialSlotIndex = 0;
        Layer.Intent = EDWCTransparencyLayerIntent::Enabled;
    }

    const FDWCTransparencyBuildTargetSnapshot Targets =
        FDWCTransparencyBuildTargetResolver::Resolve(
            *Asset, EDWCEditorValidationAccess::MetadataOnly);
    TestEqual(TEXT("Both duplicate layers are retained as separate targets"),
        Targets.Targets.Num(), 2);
    TestEqual(TEXT("Duplicate enabled layers block automatic full bake"),
        Targets.FullBakeState, EDWCEditorBuildActionState::Blocked);
    TestTrue(TEXT("Every duplicate requires manual repair"),
        Targets.Targets.ContainsByPredicate(
            [](const FDWCTransparencyBuildTarget& Target)
            {
                return Target.Requirement == EDWCTransparencyBuildRequirement::ManualRepair;
            }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationFullStateMatrixTest,
    "DWC.Editor.Foundation.Validation.FullStateMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationFullStateMatrixTest::RunTest(const FString& Parameters)
{
    struct FStateCase
    {
        const TCHAR* Label;
        TFunction<void(FDWCEditorValidationNode&)> Configure;
        EDWCEditorValidationOverallState Expected;
    };

    const TArray<FStateCase> Cases{
        {TEXT("NotApplicable gates invalid inputs"), [](FDWCEditorValidationNode& Node)
            {
                Node.Intent = EDWCEditorValidationIntentState::NotApplicable;
                Node.Input = EDWCEditorValidationInputState::Invalid;
            }, EDWCEditorValidationOverallState::NotApplicable},
        {TEXT("NotConfigured gates missing artifacts"), [](FDWCEditorValidationNode& Node)
            {
                Node.Intent = EDWCEditorValidationIntentState::NotConfigured;
                Node.Artifact = EDWCEditorValidationArtifactState::Missing;
            }, EDWCEditorValidationOverallState::NotConfigured},
        {TEXT("Draft gates stale artifacts"), [](FDWCEditorValidationNode& Node)
            {
                Node.Intent = EDWCEditorValidationIntentState::Draft;
                Node.Artifact = EDWCEditorValidationArtifactState::Stale;
            }, EDWCEditorValidationOverallState::Draft},
        {TEXT("Disabled gates invalid artifacts"), [](FDWCEditorValidationNode& Node)
            {
                Node.Intent = EDWCEditorValidationIntentState::Disabled;
                Node.Artifact = EDWCEditorValidationArtifactState::Invalid;
            }, EDWCEditorValidationOverallState::Disabled},
        {TEXT("Current"), [](FDWCEditorValidationNode&) {}, EDWCEditorValidationOverallState::Current},
        {TEXT("SavePending"), [](FDWCEditorValidationNode& Node)
            {
                Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            }, EDWCEditorValidationOverallState::SavePending},
        {TEXT("Partial precedes SavePending"), [](FDWCEditorValidationNode& Node)
            {
                Node.Artifact = EDWCEditorValidationArtifactState::Partial;
                Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            }, EDWCEditorValidationOverallState::Partial},
        {TEXT("Stale precedes Partial"), [](FDWCEditorValidationNode& Node)
            {
                Node.Artifact = EDWCEditorValidationArtifactState::Stale;
                Node.Persistence = EDWCEditorValidationPersistenceState::SavePending;
            }, EDWCEditorValidationOverallState::Stale},
        {TEXT("Missing input"), [](FDWCEditorValidationNode& Node)
            {
                Node.Input = EDWCEditorValidationInputState::Missing;
                Node.Artifact = EDWCEditorValidationArtifactState::Stale;
            }, EDWCEditorValidationOverallState::Missing},
        {TEXT("Invalid input"), [](FDWCEditorValidationNode& Node)
            {
                Node.Input = EDWCEditorValidationInputState::Invalid;
                Node.Artifact = EDWCEditorValidationArtifactState::Missing;
            }, EDWCEditorValidationOverallState::Invalid},
        {TEXT("Blocked dependency"), [](FDWCEditorValidationNode& Node)
            {
                Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
                Node.Input = EDWCEditorValidationInputState::Invalid;
            }, EDWCEditorValidationOverallState::Blocked},
        {TEXT("Running operation"), [](FDWCEditorValidationNode& Node)
            {
                Node.Operation = EDWCEditorValidationOperationState::Running;
                Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            }, EDWCEditorValidationOverallState::Running},
        {TEXT("Cancelled operation"), [](FDWCEditorValidationNode& Node)
            {
                Node.Operation = EDWCEditorValidationOperationState::Cancelled;
                Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            }, EDWCEditorValidationOverallState::Cancelled},
        {TEXT("Failed operation"), [](FDWCEditorValidationNode& Node)
            {
                Node.Operation = EDWCEditorValidationOperationState::Failed;
                Node.Dependency = EDWCEditorValidationDependencyState::Blocked;
            }, EDWCEditorValidationOverallState::Failed}
    };

    for (const FStateCase& Case : Cases)
    {
        FDWCEditorValidationNode Node;
        Case.Configure(Node);
        TestEqual(Case.Label, Node.GetOverallState(), Case.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationRemediationMatrixTest,
    "DWC.Editor.Foundation.Validation.RemediationMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationRemediationMatrixTest::RunTest(const FString& Parameters)
{
    FWCAEditorValidationSnapshot Snapshot;
    FDWCEditorValidationNode& Node = DWCEditorValidation::FindOrAddNode(
        Snapshot,
        {EDWCEditorValidationDomain::Transparency, 4, FGuid::NewGuid()});

    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        TEXT("Matrix.None"),
        EDWCEditorValidationSeverity::Info,
        FText::FromString(TEXT("Required Failed Missing")),
        FText::GetEmpty(),
        FText::GetEmpty(),
        FText::GetEmpty(),
        EDWCEditorValidationRemediation::None);
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        TEXT("Matrix.Manual"),
        EDWCEditorValidationSeverity::Error,
        FText::FromString(TEXT("Bake Transparency Textures")),
        FText::GetEmpty(),
        FText::GetEmpty(),
        FText::GetEmpty(),
        EDWCEditorValidationRemediation::Manual);
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        TEXT("Matrix.Automatic"),
        EDWCEditorValidationSeverity::Warning,
        FText::FromString(TEXT("Presentation contains no build keywords")),
        FText::GetEmpty(),
        FText::GetEmpty(),
        FText::GetEmpty(),
        EDWCEditorValidationRemediation::BuildAction,
        EDWCEditorBuildAction::BakeTransparencyTextures);

    TestEqual(TEXT("Only the typed automatic remediation creates an action"),
        Snapshot.Actions.Num(), 1);
    const FDWCEditorValidationActionState* Action =
        Snapshot.FindAction(EDWCEditorBuildAction::BakeTransparencyTextures);
    TestNotNull(TEXT("The typed build action is indexed"), Action);
    if (Action != nullptr)
    {
        TestEqual(TEXT("The action keeps its canonical target"), Action->Targets.Num(), 1);
        TestEqual(TEXT("The action is required"), Action->State,
            EDWCEditorBuildActionState::Required);
    }

    const FWCAValidationReport Report =
        FDWCEditorValidationReportAdapter::BuildReport(Snapshot);
    TestEqual(TEXT("The presentation adapter preserves all diagnostics"), Report.Issues.Num(), 3);
    TestFalse(TEXT("Keyword-heavy presentation does not become auto-resolvable"),
        Report.Issues[0].BuildAction.IsSet());
    TestFalse(TEXT("Manual remediation does not become auto-resolvable"),
        Report.Issues[1].BuildAction.IsSet());
    TestTrue(TEXT("Typed automatic remediation remains auto-resolvable"),
        Report.Issues[2].BuildAction.IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationSectionRegistryTest,
    "DWC.Editor.Foundation.Validation.SectionRegistry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationSectionRegistryTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Asset owns its own presentation section"),
        FDWCEditorValidationSectionRegistry::MapDomain(EDWCEditorValidationDomain::Asset),
        EWCAValidationSection::Asset);
    TestEqual(TEXT("Wet Part no longer aliases Render Profile"),
        FDWCEditorValidationSectionRegistry::MapDomain(EDWCEditorValidationDomain::WetPart),
        EWCAValidationSection::WetPart);
    TestEqual(TEXT("GPU simulation lookup is presented with runtime data"),
        FDWCEditorValidationSectionRegistry::MapDomain(EDWCEditorValidationDomain::GPUSimulationMap),
        EWCAValidationSection::RuntimeData);
    TestEqual(TEXT("Save action belongs to asset state"),
        FDWCEditorValidationSectionRegistry::MapAction(EDWCEditorBuildAction::SaveAsset),
        EWCAValidationSection::Asset);
    TestEqual(TEXT("Affected transparency rebuild belongs to transparency"),
        FDWCEditorValidationSectionRegistry::MapAction(
            EDWCEditorBuildAction::RebakeAffectedTransparencyMaps),
        EWCAValidationSection::TransparencyMaps);
    TestEqual(TEXT("Every canonical section has one descriptor"),
        FDWCEditorValidationSectionRegistry::GetSections().Num(), 9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorValidationSectionAggregationTest,
    "DWC.Editor.Foundation.Validation.SectionAggregation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorValidationSectionAggregationTest::RunTest(const FString& Parameters)
{
    FWCAEditorValidationSnapshot Snapshot;

    FDWCEditorValidationNode& AssetNode = DWCEditorValidation::FindOrAddNode(
        Snapshot,
        {EDWCEditorValidationDomain::Asset});
    AssetNode.Intent = EDWCEditorValidationIntentState::Enabled;

    FDWCEditorValidationNode& WetPartNode = DWCEditorValidation::FindOrAddNode(
        Snapshot,
        {EDWCEditorValidationDomain::WetPart, 2});
    WetPartNode.Intent = EDWCEditorValidationIntentState::NotConfigured;
    WetPartNode.Artifact = EDWCEditorValidationArtifactState::NotRequired;

    FDWCEditorValidationNode& GPURuntimeNode = DWCEditorValidation::FindOrAddNode(
        Snapshot,
        {EDWCEditorValidationDomain::GPUSimulationMap});
    GPURuntimeNode.Intent = EDWCEditorValidationIntentState::Enabled;
    GPURuntimeNode.Artifact = EDWCEditorValidationArtifactState::Stale;
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        GPURuntimeNode,
        TEXT("Runtime.GPU.Stale"),
        EDWCEditorValidationSeverity::Warning,
        FText::FromString(TEXT("Presentation text does not define ownership")),
        FText::GetEmpty(),
        FText::GetEmpty(),
        FText::GetEmpty(),
        EDWCEditorValidationRemediation::BuildAction,
        EDWCEditorBuildAction::BuildGPURuntimeData);

    FDWCEditorBuildStatusSnapshot BuildStatus;
    FDWCEditorBuildActionStatus& WrinkleStatus =
        BuildStatus.Actions.Add(EDWCEditorBuildAction::BakeWrinkleTextures);
    WrinkleStatus.Action = EDWCEditorBuildAction::BakeWrinkleTextures;
    WrinkleStatus.State = EDWCEditorBuildActionState::Running;

    const FWCAValidationReport Report =
        FDWCEditorValidationReportAdapter::BuildReport(Snapshot, &BuildStatus);
    const FWCAValidationSectionResult* AssetSection =
        Report.FindSection(EWCAValidationSection::Asset);
    const FWCAValidationSectionResult* WetPartSection =
        Report.FindSection(EWCAValidationSection::WetPart);
    const FWCAValidationSectionResult* RuntimeSection =
        Report.FindSection(EWCAValidationSection::RuntimeData);
    const FWCAValidationSectionResult* WrinkleSection =
        Report.FindSection(EWCAValidationSection::WrinkleMaps);

    TestNotNull(TEXT("Asset section exists"), AssetSection);
    TestNotNull(TEXT("Wet Part section exists"), WetPartSection);
    TestNotNull(TEXT("Runtime section exists"), RuntimeSection);
    TestNotNull(TEXT("Wrinkle section exists"), WrinkleSection);
    if (AssetSection != nullptr)
    {
        TestEqual(TEXT("Current asset state remains current"), AssetSection->OverallState,
            EDWCEditorValidationOverallState::Current);
        TestEqual(TEXT("Current asset uses success presentation"), AssetSection->PresentationState,
            EDWCValidationPresentationState::Success);
    }
    if (WetPartSection != nullptr)
    {
        TestTrue(TEXT("Not-configured Wet Part is still an explicit applicable state"),
            WetPartSection->bApplicable);
        TestEqual(TEXT("Wet Part state is not configured"), WetPartSection->OverallState,
            EDWCEditorValidationOverallState::NotConfigured);
    }
    if (RuntimeSection != nullptr)
    {
        TestEqual(TEXT("GPU lookup stale state is aggregated into Runtime Data"),
            RuntimeSection->OverallState, EDWCEditorValidationOverallState::Stale);
        TestEqual(TEXT("Runtime owns the GPU diagnostic"), RuntimeSection->IssueIndices.Num(), 1);
    }
    if (WrinkleSection != nullptr)
    {
        TestEqual(TEXT("Build-status-only running state is presented"),
            WrinkleSection->OverallState, EDWCEditorValidationOverallState::Running);
        TestTrue(TEXT("Running build action marks the section applicable"),
            WrinkleSection->bApplicable);
    }
    return true;
}

#endif
