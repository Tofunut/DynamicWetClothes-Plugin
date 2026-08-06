#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerAsyncCompatibility.h"

namespace
{
    FDWCEditorAsyncOperationIdentity MakeValidIdentity()
    {
        FDWCEditorAsyncOperationIdentity Identity;
        Identity.Key.Namespace = TEXT("ContractTest");
        Identity.Key.MaterialSlotIndex = 3;
        Identity.SessionEpoch = FGuid::NewGuid();
        Identity.OperationId = 11;
        Identity.Generation = 7;
        Identity.Domain = EDWCEditorAuthoringDomain::Wrinkle;
        Identity.DomainRevision = 5;
        return Identity;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOperationStateContractTest,
    "DWC.Editor.Foundation.Async.Operation.StateContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOperationStateContractTest::RunTest(const FString&)
{
    TestTrue(TEXT("Pending work can be admitted"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Pending,
            EDWCEditorAsyncOperationState::Admitted));
    TestTrue(TEXT("Running work can wait for commit"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Running,
            EDWCEditorAsyncOperationState::CommitPending));
    TestTrue(TEXT("Prepared work becomes ready before running"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Preparing,
            EDWCEditorAsyncOperationState::Ready));
    TestTrue(TEXT("Ready work can start running"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Ready,
            EDWCEditorAsyncOperationState::Running));
    TestTrue(TEXT("Committing work can transfer to retirement"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Committing,
            EDWCEditorAsyncOperationState::Retiring));
    TestFalse(TEXT("Pending work cannot skip directly to running"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Pending,
            EDWCEditorAsyncOperationState::Running));
    TestFalse(TEXT("Completed work cannot transition again"),
        FDWCEditorAsyncOperationContract::CanTransition(
            EDWCEditorAsyncOperationState::Completed,
            EDWCEditorAsyncOperationState::Pending));
    TestTrue(TEXT("Cancellation is requested before it is acknowledged"),
        FDWCEditorAsyncOperationContract::CanTransitionCancellation(
            EDWCEditorAsyncCancellationState::None,
            EDWCEditorAsyncCancellationState::CancelRequested));
    TestFalse(TEXT("Cancellation acknowledgment cannot skip the request"),
        FDWCEditorAsyncOperationContract::CanTransitionCancellation(
            EDWCEditorAsyncCancellationState::None,
            EDWCEditorAsyncCancellationState::CancelAcknowledged));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOperationCommitContractTest,
    "DWC.Editor.Foundation.Async.Operation.CommitContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOperationCommitContractTest::RunTest(const FString&)
{
    const FDWCEditorAsyncOperationIdentity Identity = MakeValidIdentity();
    TestTrue(TEXT("Current identity can commit"),
        FDWCEditorAsyncOperationContract::CanCommit(
            Identity,
            Identity.SessionEpoch,
            Identity.Generation,
            Identity.DomainRevision));
    TestFalse(TEXT("A previous editor session cannot commit"),
        FDWCEditorAsyncOperationContract::CanCommit(
            Identity,
            FGuid::NewGuid(),
            Identity.Generation,
            Identity.DomainRevision));
    TestFalse(TEXT("A superseded generation cannot commit"),
        FDWCEditorAsyncOperationContract::CanCommit(
            Identity,
            Identity.SessionEpoch,
            Identity.Generation + 1,
            Identity.DomainRevision));
    TestFalse(TEXT("A stale authoring revision cannot commit"),
        FDWCEditorAsyncOperationContract::CanCommit(
            Identity,
            Identity.SessionEpoch,
            Identity.Generation,
            Identity.DomainRevision + 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOperationMemoryContractTest,
    "DWC.Editor.Foundation.Async.Operation.MemoryContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOperationMemoryContractTest::RunTest(const FString&)
{
    FDWCEditorMemoryBreakdown Breakdown;
    Breakdown.SharedResidentBytes = 10;
    Breakdown.SnapshotBytes = 20;
    Breakdown.WorkingBytes = 30;
    Breakdown.OutputBytes = 40;
    Breakdown.ScratchBytes = 50;
    Breakdown.UploadStagingBytes = 60;

    uint64 Bytes = 0;
    TestTrue(TEXT("Private memory total is representable"),
        Breakdown.TryGetOperationPrivateBytes(Bytes));
    TestEqual(TEXT("Private memory excludes shared and staging resources"), Bytes, 140ull);
    TestTrue(TEXT("Described memory total is representable"),
        Breakdown.TryGetTotalDescribedBytes(Bytes));
    TestEqual(TEXT("Described memory includes every category"), Bytes, 210ull);

    Breakdown.SnapshotBytes = MAX_uint64;
    TestFalse(TEXT("Memory arithmetic rejects overflow"),
        Breakdown.TryGetOperationPrivateBytes(Bytes));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerAsyncCompatibilityTest,
    "DWC.Editor.Foundation.Async.Operation.WorkerCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerAsyncCompatibilityTest::RunTest(const FString&)
{
    FDWCEditorWorkerJobTicket Ticket;
    Ticket.Key.Kind = EDWCEditorWorkerJobKind::TransparencyVisualization;
    Ticket.Key.MaterialSlotIndex = 9;
    Ticket.Key.LayerGuid = FGuid::NewGuid();
    Ticket.JobId = 12;
    Ticket.Generation = 4;
    Ticket.Domain = EDWCEditorAuthoringDomain::Transparency;
    Ticket.DomainRevision = 8;
    const FGuid SessionEpoch = FGuid::NewGuid();

    const FDWCEditorAsyncOperationIdentity Identity =
        DWCEditorWorkerAsyncCompatibility::MakeOperationIdentity(Ticket, SessionEpoch);
    TestEqual(TEXT("Worker kind maps to a stable operation namespace"),
        Identity.Key.Namespace, FName(TEXT("TransparencyVisualization")));
    TestEqual(TEXT("Worker slot is preserved"), Identity.Key.MaterialSlotIndex, 9);
    TestEqual(TEXT("Worker layer is preserved"), Identity.Key.ResourceGuid, Ticket.Key.LayerGuid);
    TestEqual(TEXT("Worker job id becomes the operation id"), Identity.OperationId, 12ull);
    FDWCEditorWorkerJobDescriptor VisualizationDescriptor;
    VisualizationDescriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    FDWCEditorWorkerJobDescriptor BakeDescriptor;
    BakeDescriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    TestTrue(TEXT("Visualization policy is explicit on its descriptor"),
        VisualizationDescriptor.GetRequestPolicy() == EDWCEditorAsyncRequestPolicy::LatestWins);
    TestTrue(TEXT("Bake policy is explicit on its descriptor"),
        BakeDescriptor.GetRequestPolicy() == EDWCEditorAsyncRequestPolicy::FIFO);
    return true;
}

#endif
