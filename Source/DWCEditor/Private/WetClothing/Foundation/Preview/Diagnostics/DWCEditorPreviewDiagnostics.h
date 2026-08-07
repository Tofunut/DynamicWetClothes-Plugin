//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FDWCEditorPreviewSession;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorWorkerJobScheduler;
class UTexture2D;

DECLARE_LOG_CATEGORY_EXTERN(LogDWCEditorPreview, Log, All);

struct FDWCEditorPreviewMemoryBucket
{
    FString Name;
    uint64 UsedBytes = 0;
    uint64 BudgetBytes = 0;
    int32 EntryCount = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    int32 ActiveLeaseCount = 0;
    int32 RetiredEntryCount = 0;
};

struct FDWCEditorPreviewOperationCounter
{
    FString Name;
    uint64 Count = 0;
    uint64 Bytes = 0;
};

using FDWCEditorPreviewMemoryCollector =
    TFunction<void(TArray<FDWCEditorPreviewMemoryBucket>&)>;
using FDWCEditorPreviewOperationCollector =
    TFunction<void(TArray<FDWCEditorPreviewOperationCounter>&)>;
using FDWCEditorPreviewDiagnosticResetter = TFunction<void()>;

/**
 * Internal preview instrumentation used by editor preview systems.
 * This diagnostic layer is intentionally not exposed through shipping user UI or console commands.
 */
class FDWCEditorPreviewDiagnostics final
{
  public:
    static void RegisterSession(FDWCEditorPreviewSession* Session);
    static void UnregisterSession(FDWCEditorPreviewSession* Session);
    static void RegisterWorkerScheduler(FDWCEditorWorkerJobScheduler* Scheduler);
    static void UnregisterWorkerScheduler(FDWCEditorWorkerJobScheduler* Scheduler);
    static void RegisterCommitCoordinator(FDWCEditorPreviewCommitCoordinator* Coordinator);
    static void UnregisterCommitCoordinator(FDWCEditorPreviewCommitCoordinator* Coordinator);
    static void DumpAllSessions();
    static void ResetAllCounters();

    /** CPU source plus estimated resident resource bytes; intended for diagnostics only. */
    static uint64 EstimateTextureBytes(const UTexture2D* Texture);
    static FString FormatBytes(uint64 Bytes);
};
