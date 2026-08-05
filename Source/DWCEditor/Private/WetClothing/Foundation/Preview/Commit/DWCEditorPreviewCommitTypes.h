#pragma once

#include "CoreMinimal.h"

enum class EDWCEditorPreviewConsumerState : uint8
{
    Active,
    Suspended,
    Revoked
};

enum class EDWCEditorPreviewCommitResult : uint8
{
    Applied,
    CoordinatorShutdown,
    ConsumerExpired,
    ConsumerSuspended,
    StaleRequest,
    WorkspaceEntryMissing,
    DataRevisionMismatch,
    ResourceGenerationMismatch,
    DescriptorMismatch,
    InvalidPayload,
    WorkspaceRejected
};

struct FDWCEditorPreviewCommitDiagnostics
{
    uint64 AppliedCount = 0;
    uint64 StaleRequestCount = 0;
    uint64 ConsumerRejectedCount = 0;
    uint64 WorkspaceRejectedCount = 0;
    uint64 ShutdownRejectedCount = 0;
    uint64 RegionAppliedCount = 0;
    uint64 RegionRejectedCount = 0;
    uint64 WorkspaceEntryMissingCount = 0;
    uint64 DataRevisionMismatchCount = 0;
    uint64 ResourceGenerationMismatchCount = 0;
    uint64 DescriptorMismatchCount = 0;
    uint64 InvalidPayloadCount = 0;
};

struct FDWCEditorPreviewConsumerLifetimeState
{
    FGuid ConsumerEpoch = FGuid::NewGuid();
    uint64 Generation = 1;
    EDWCEditorPreviewConsumerState State = EDWCEditorPreviewConsumerState::Active;
};

struct FDWCEditorPreviewConsumerToken
{
    TWeakPtr<FDWCEditorPreviewConsumerLifetimeState> State;
    FGuid ConsumerEpoch;
    uint64 Generation = 0;

    bool IsValid() const
    {
        return ConsumerEpoch.IsValid() && Generation != 0 && State.IsValid();
    }
};

/** Game-thread lifetime gate owned by one preview result consumer. */
class FDWCEditorPreviewConsumerLifetime final
{
  public:
    FDWCEditorPreviewConsumerLifetime();

    FDWCEditorPreviewConsumerToken CaptureToken() const;
    void AdvanceGeneration();
    void Suspend();
    void Resume();
    void Revoke();
    bool IsActive() const;

  private:
    TSharedRef<FDWCEditorPreviewConsumerLifetimeState> State;
};

struct FDWCEditorPreviewCommitContext
{
    FDWCEditorPreviewConsumerToken ConsumerToken;
    FGuid ProducerSessionEpoch;
    TFunction<bool()> IsCurrent;
    FString DebugName;
};
