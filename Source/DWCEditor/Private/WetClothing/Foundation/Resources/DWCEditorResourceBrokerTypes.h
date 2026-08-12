// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

enum class EDWCEditorResourcePressureLevel : uint8
{
    Admission,
    Soft,
    Critical,
    PIE
};

enum class EDWCEditorReclaimPriority : uint8
{
    Stale = 0,
    Background = 32,
    SharedCache = 64,
    InactivePreview = 96,
    ActivePreview = 128
};

enum class EDWCEditorExclusiveBuildState : uint8
{
    Idle,
    Draining,
    Active,
    Retiring
};

struct FDWCEditorExclusiveBuildRequest
{
    FGuid SessionId;
    FString AssetPath;
    FString DebugName;
};

struct FDWCEditorExclusiveBuildSnapshot
{
    FGuid ScopeId;
    FGuid SessionId;
    FString AssetPath;
    FString DebugName;
    EDWCEditorExclusiveBuildState State = EDWCEditorExclusiveBuildState::Idle;
    double StartedSeconds = 0.0;
    uint64 BlockedPreviewRequestCount = 0;
    uint64 BlockedBuildRequestCount = 0;

    bool IsActive() const
    {
        return ScopeId.IsValid() && State != EDWCEditorExclusiveBuildState::Idle;
    }
};

struct FDWCEditorResourceReclaimResult
{
    uint64 ImmediateBytes = 0;
    uint64 RetiringGPUBytes = 0;
    uint64 BlockedBytes = 0;
    int32 ReclaimedEntryCount = 0;
};

struct FDWCEditorResourceReclaimRequest
{
    EDWCEditorResourcePool RequestedPool = EDWCEditorResourcePool::WorkerPrivateCPU;
    EDWCEditorResourcePressureLevel PressureLevel = EDWCEditorResourcePressureLevel::Admission;
    uint64 TargetBytes = 0;
    FGuid RequestingSessionId;
    FName RequestOwnerNamespace;
};

struct FDWCEditorReclaimParticipantDescriptor
{
    FName Name;
    FName ReservationOwnerNamespace;
    FGuid ReservationSessionEpoch;
    FGuid SessionId;
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::SharedCacheCPU;
    EDWCEditorReclaimPriority Priority = EDWCEditorReclaimPriority::SharedCache;
    TFunction<uint64()> QueryReclaimableBytes;
    TFunction<FDWCEditorResourceReclaimResult(const FDWCEditorResourceReclaimRequest&)> Reclaim;

    bool IsValid() const
    {
        return !Name.IsNone() && QueryReclaimableBytes && Reclaim;
    }
};

struct FDWCEditorResourceBrokerDiagnostics
{
    int32 SessionCount = 0;
    int32 ParticipantCount = 0;
    uint64 PressureRequestCount = 0;
    uint64 SuccessfulReclaimCount = 0;
    uint64 ImmediateReclaimedBytes = 0;
    uint64 RetiringGPUBytes = 0;
    uint64 ReentrantPressureRejectCount = 0;
    EDWCEditorResourcePool LastRequestedPool = EDWCEditorResourcePool::WorkerPrivateCPU;
    uint64 LastRequestedBytes = 0;
    uint64 LastTargetBytes = 0;
    uint64 LastImmediateReclaimedBytes = 0;
    int32 LastCandidateCount = 0;
    int32 LastReclaimableParticipantCount = 0;
    int32 LastOwnerExcludedCount = 0;
    uint64 ExclusiveBuildAcquireCount = 0;
    uint64 ExclusiveBuildRejectCount = 0;
    uint64 ExclusiveBuildBlockedPreviewCount = 0;
    uint64 ExclusiveBuildBlockedActionCount = 0;
    FDWCEditorExclusiveBuildSnapshot ExclusiveBuild;
};
