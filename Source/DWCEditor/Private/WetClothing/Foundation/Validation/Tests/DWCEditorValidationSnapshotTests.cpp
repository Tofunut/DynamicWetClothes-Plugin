// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationReportAdapter.h"
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
        BuildWCAValidationSnapshot(*Asset, EWCAValidationMode::Fast, false);

    TestEqual(TEXT("Snapshot identifies its source asset"), Snapshot.AssetPath, Asset->GetPathName());
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

    FDWCEditorValidationEvaluationContext Context(*Asset, false);
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

    FDWCEditorValidationEvaluationContext Context(*Asset, false);
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

    FDWCEditorValidationEvaluationContext Context(*Asset, false);
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
    TestEqual(TEXT("An unconfigured Wettable slot owns one validation node"), Snapshot.Nodes.Num(), 1);
    TestEqual(TEXT("No layer is a healthy NotConfigured state"), Snapshot.Nodes[0].Intent,
        EDWCEditorValidationIntentState::NotConfigured);
    TestEqual(TEXT("No layer requires no runtime artifact"), Snapshot.Nodes[0].Artifact,
        EDWCEditorValidationArtifactState::NotRequired);
    TestEqual(TEXT("No layer emits no validation diagnostic"), Snapshot.Diagnostics.Num(), 0);

    FWetClothingTransparencyLayerData& Layer =
        Asset->Authored.TransparencyData.TransparencyLayers.AddDefaulted_GetRef();
    Layer.LayerGuid = FGuid::NewGuid();
    Layer.TargetSurface.OuterMaterialSlotIndex = 0;
    Layer.Intent = EDWCTransparencyLayerIntent::Draft;
    Snapshot = {};
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    TestEqual(TEXT("Explicit unfinished authoring is Draft"), Snapshot.Nodes[0].Intent,
        EDWCEditorValidationIntentState::Draft);
    TestFalse(TEXT("Draft content does not trigger a build action"),
        Snapshot.Actions.Contains(EDWCEditorBuildAction::BakeTransparencyTextures));
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
    TestEqual(TEXT("Disabled authored content remains disabled"), Snapshot.Nodes[0].Intent,
        EDWCEditorValidationIntentState::Disabled);
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
    FDWCTransparencyLayerValidationEvaluator::AppendToSnapshot(*Asset, false, Snapshot);
    TestEqual(TEXT("Invalid enabled input is reported"), Snapshot.Nodes[0].Input,
        EDWCEditorValidationInputState::Invalid);
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
        FDWCWrinkleBuildTargetResolver::Resolve(*Asset, false);
    TestEqual(TEXT("Unconfigured wrinkle bake is unavailable"),
        Targets.BakeState, EDWCEditorBuildActionState::Unavailable);
    TestEqual(TEXT("A Wettable slot still owns a canonical target"), Targets.Targets.Num(), 1);

    FWetWrinkleRuntimeNormalSource& Custom =
        Asset->Authored.WrinkleData.RuntimeNormalSources.AddDefaulted_GetRef();
    Custom.MaterialSlotIndex = 0;
    Custom.Source = EDWCWrinkleNormalSource::CustomTexture;
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(*Asset, false);
    TestEqual(TEXT("Missing custom normal requires manual repair"),
        Targets.Targets[0].Requirement, EDWCWrinkleBuildRequirement::ManualRepair);
    TestEqual(TEXT("A custom-only slot does not request wrinkle baking"),
        Targets.BakeState, EDWCEditorBuildActionState::Unavailable);

    Custom.CustomWrinkleNormalMap = NewObject<UTexture2D>(GetTransientPackage());
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(*Asset, false);
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

    Targets = FDWCWrinkleBuildTargetResolver::Resolve(*Asset, false);
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
    Targets = FDWCWrinkleBuildTargetResolver::Resolve(*Asset, false);
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
        FDWCTransparencyBuildTargetResolver::Resolve(*Asset, false);
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

#endif
