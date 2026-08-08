// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialCache.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "WetClothing/Foundation/Preview/Materials/DWCEditorPreviewMaterialFactory.h"

bool FDWCEditorPreviewMaterialCache::FGraphKey::operator==(const FGraphKey& Other) const
{
    return SourceBaseMaterial == Other.SourceBaseMaterial &&
           SourceStateId == Other.SourceStateId &&
           DWCDataUVChannelIndex == Other.DWCDataUVChannelIndex &&
           SurfaceWaterNormalUVChannelIndex == Other.SurfaceWaterNormalUVChannelIndex &&
           FeatureMask == Other.FeatureMask &&
           FeatureSchemaVersion == Other.FeatureSchemaVersion;
}

bool FDWCEditorPreviewMaterialCache::FParentKey::operator==(const FParentKey& Other) const
{
    return SourceMaterial == Other.SourceMaterial &&
           TransientBaseMaterial == Other.TransientBaseMaterial &&
           SourceParameterRevision == Other.SourceParameterRevision;
}

bool FDWCEditorPreviewMaterialCache::FSlotKey::operator==(const FSlotKey& Other) const
{
    return SlotOwner == Other.SlotOwner &&
           MaterialSlotIndex == Other.MaterialSlotIndex &&
           TransientParent == Other.TransientParent;
}

FDWCEditorPreviewMaterialResult FDWCEditorPreviewMaterialCache::GetOrCreate(
    const FDWCEditorPreviewMaterialRequest& Request)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorPreviewMaterialCache_GetOrCreate);

    FDWCEditorPreviewMaterialResult Result;
    if (Request.SourceMaterial == nullptr)
    {
        Result.Message = TEXT("No source material was supplied to the editor preview material cache.");
        return Result;
    }
    if (Request.MaterialSlotIndex == INDEX_NONE)
    {
        Result.Message = TEXT("A material slot index is required to create an editor preview MID.");
        return Result;
    }

    UMaterial* SourceBaseMaterial = const_cast<UMaterial*>(Request.SourceMaterial->GetMaterial());
    if (SourceBaseMaterial == nullptr)
    {
        Result.Message = FString::Printf(
            TEXT("Preview source '%s' has no base material."), *GetNameSafe(Request.SourceMaterial));
        return Result;
    }

    FGraphKey GraphKey;
    GraphKey.SourceBaseMaterial = FObjectKey(SourceBaseMaterial);
    GraphKey.SourceStateId = SourceBaseMaterial->StateId;
    GraphKey.DWCDataUVChannelIndex = Request.DWCDataUVChannelIndex;
    GraphKey.SurfaceWaterNormalUVChannelIndex = Request.SurfaceWaterNormalUVChannelIndex;
    GraphKey.FeatureMask = static_cast<uint8>(Request.FeatureMask);
    GraphKey.FeatureSchemaVersion = HashCombine(
        FDWCEditorPreviewMaterialFactory::CommonGraphSchemaVersion,
        Request.FeatureSchemaVersion);

    FGraphEntry* GraphEntry = GraphEntries.Find(GraphKey);
    Result.bGraphCacheHit = GraphEntry != nullptr;
    Result.bGraphCacheHit ? ++LifetimeStats.GraphHitCount : ++LifetimeStats.GraphMissCount;
    if (GraphEntry == nullptr)
    {
        const double BuildStartSeconds = FPlatformTime::Seconds();
        FGraphEntry  NewEntry;
        NewEntry.SourceMaterial = Request.SourceMaterial;
        NewEntry.Material = FDWCEditorPreviewMaterialFactory::BuildTransientBaseMaterialGraph(
            Request, NewEntry.FailureMessage);
        if (NewEntry.Material == nullptr)
        {
            NewEntry.State = EDWCEditorPreviewMaterialState::Failed;
        }
        else if (FDWCEditorPreviewMaterialFactory::BeginTransientBaseMaterialCompilation(
                     NewEntry.Material,
                     NewEntry.FailureMessage))
        {
            NewEntry.State = EDWCEditorPreviewMaterialState::Compiling;
            ++LifetimeStats.GraphCompileRequestCount;
        }
        else
        {
            NewEntry.State = EDWCEditorPreviewMaterialState::Failed;
        }
        GraphEntry = &GraphEntries.Add(GraphKey, MoveTemp(NewEntry));
        const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
        ++LifetimeStats.GraphBuildCount;
        LifetimeStats.TotalGraphBuildMilliseconds += BuildMilliseconds;
        LifetimeStats.MaxGraphBuildMilliseconds =
            FMath::Max(LifetimeStats.MaxGraphBuildMilliseconds, BuildMilliseconds);
    }
    if (GraphEntry->Material == nullptr || GraphEntry->State == EDWCEditorPreviewMaterialState::Failed)
    {
        Result.State = EDWCEditorPreviewMaterialState::Failed;
        Result.Message = GraphEntry->FailureMessage;
        return Result;
    }
    Result.TransientBaseMaterial = GraphEntry->Material;

    if (GraphEntry->State == EDWCEditorPreviewMaterialState::Compiling)
    {
        FString                              CompileMessage;
        const EDWCEditorPreviewMaterialState CompileState =
            FDWCEditorPreviewMaterialFactory::PollTransientBaseMaterialCompilation(
                GraphEntry->Material,
                CompileMessage);
        if (CompileState == EDWCEditorPreviewMaterialState::Ready)
        {
            GraphEntry->State = CompileState;
            ++LifetimeStats.GraphCompileCompleteCount;
        }
        else if (CompileState == EDWCEditorPreviewMaterialState::Failed)
        {
            GraphEntry->State = CompileState;
            GraphEntry->FailureMessage = MoveTemp(CompileMessage);
            ++LifetimeStats.GraphCompileFailureCount;
        }
    }

    if (GraphEntry->State == EDWCEditorPreviewMaterialState::Compiling)
    {
        Result.State = EDWCEditorPreviewMaterialState::Compiling;
        Result.bPending = true;
        Result.Message = TEXT("The editor preview material is compiling shaders.");
        return Result;
    }
    if (GraphEntry->State == EDWCEditorPreviewMaterialState::Failed)
    {
        Result.State = EDWCEditorPreviewMaterialState::Failed;
        Result.Message = GraphEntry->FailureMessage;
        return Result;
    }

    FParentKey ParentKey;
    ParentKey.SourceMaterial = FObjectKey(Request.SourceMaterial);
    ParentKey.TransientBaseMaterial = FObjectKey(GraphEntry->Material);
    ParentKey.SourceParameterRevision = Request.SourceParameterRevision;

    FParentEntry* ParentEntry = ParentEntries.Find(ParentKey);
    Result.bParentCacheHit = ParentEntry != nullptr;
    Result.bParentCacheHit ? ++LifetimeStats.ParentHitCount : ++LifetimeStats.ParentMissCount;
    if (ParentEntry == nullptr)
    {
        const double BuildStartSeconds = FPlatformTime::Seconds();
        FParentEntry NewEntry;
        NewEntry.SourceMaterial = Request.SourceMaterial;
        NewEntry.Parent = FDWCEditorPreviewMaterialFactory::BuildTransientParent(
            Request.SourceMaterial,
            GraphEntry->Material,
            NewEntry.FailureMessage);
        ParentEntry = &ParentEntries.Add(ParentKey, MoveTemp(NewEntry));
        const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
        ++LifetimeStats.ParentBuildCount;
        LifetimeStats.TotalParentBuildMilliseconds += BuildMilliseconds;
        LifetimeStats.MaxParentBuildMilliseconds =
            FMath::Max(LifetimeStats.MaxParentBuildMilliseconds, BuildMilliseconds);
    }
    if (ParentEntry->Parent == nullptr)
    {
        Result.Message = ParentEntry->FailureMessage;
        return Result;
    }
    Result.TransientParent = ParentEntry->Parent;

    UObject* SlotOwner = Request.SlotOwner != nullptr
                             ? Request.SlotOwner
                             : static_cast<UObject*>(Request.SourceMaterial);
    FSlotKey SlotKey;
    SlotKey.SlotOwner = FObjectKey(SlotOwner);
    SlotKey.MaterialSlotIndex = Request.MaterialSlotIndex;
    SlotKey.TransientParent = FObjectKey(ParentEntry->Parent);

    FSlotEntry* SlotEntry = SlotEntries.Find(SlotKey);
    Result.bMIDCacheHit = SlotEntry != nullptr;
    Result.bMIDCacheHit ? ++LifetimeStats.MIDHitCount : ++LifetimeStats.MIDMissCount;
    if (SlotEntry == nullptr)
    {
        const double BuildStartSeconds = FPlatformTime::Seconds();
        FString      MIDError;
        FSlotEntry   NewEntry;
        NewEntry.SlotOwner = SlotOwner;
        NewEntry.SourceMaterial = Request.SourceMaterial;
        NewEntry.MID = FDWCEditorPreviewMaterialFactory::BuildSlotMID(
            ParentEntry->Parent,
            Request.MIDOuter,
            MIDError);
        if (NewEntry.MID == nullptr)
        {
            const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
            ++LifetimeStats.MIDBuildCount;
            LifetimeStats.TotalMIDBuildMilliseconds += BuildMilliseconds;
            LifetimeStats.MaxMIDBuildMilliseconds =
                FMath::Max(LifetimeStats.MaxMIDBuildMilliseconds, BuildMilliseconds);
            Result.Message = MoveTemp(MIDError);
            return Result;
        }
        SlotEntry = &SlotEntries.Add(SlotKey, MoveTemp(NewEntry));
        const double BuildMilliseconds = (FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0;
        ++LifetimeStats.MIDBuildCount;
        LifetimeStats.TotalMIDBuildMilliseconds += BuildMilliseconds;
        LifetimeStats.MaxMIDBuildMilliseconds =
            FMath::Max(LifetimeStats.MaxMIDBuildMilliseconds, BuildMilliseconds);
    }

    Result.PreviewMID = SlotEntry->MID;
    Result.State = EDWCEditorPreviewMaterialState::Ready;
    Result.bSucceeded = Result.PreviewMID != nullptr;
    Result.Message = Result.bSucceeded
                         ? TEXT("Editor preview material hierarchy is ready.")
                         : TEXT("The cached editor preview MID is no longer valid.");
    return Result;
}

void FDWCEditorPreviewMaterialCache::InvalidateSource(
    UMaterialInterface* SourceMaterial,
    const bool          bIncludeSharedBaseMaterial)
{
    if (SourceMaterial == nullptr)
    {
        return;
    }

    ++LifetimeStats.SourceInvalidationCount;

    UMaterial* SourceBaseMaterial = SourceMaterial->GetMaterial();
    const auto MatchesInvalidatedSource =
        [SourceMaterial, SourceBaseMaterial, bIncludeSharedBaseMaterial](UMaterialInterface* Candidate)
    {
        return Candidate == SourceMaterial ||
               (bIncludeSharedBaseMaterial && Candidate != nullptr &&
                Candidate->GetMaterial() == SourceBaseMaterial);
    };

    for (auto It = SlotEntries.CreateIterator(); It; ++It)
    {
        if (MatchesInvalidatedSource(It.Value().SourceMaterial))
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = ParentEntries.CreateIterator(); It; ++It)
    {
        if (MatchesInvalidatedSource(It.Value().SourceMaterial))
        {
            It.RemoveCurrent();
        }
    }

    if (bIncludeSharedBaseMaterial)
    {
        for (auto It = GraphEntries.CreateIterator(); It; ++It)
        {
            if (MatchesInvalidatedSource(It.Value().SourceMaterial))
            {
                FDWCEditorPreviewMaterialFactory::CancelTransientBaseMaterialCompilation(
                    It.Value().Material);
                It.RemoveCurrent();
            }
        }
    }
}

void FDWCEditorPreviewMaterialCache::InvalidateSlot(UObject* SlotOwner, const int32 MaterialSlotIndex)
{
    if (SlotOwner == nullptr)
    {
        return;
    }
    ++LifetimeStats.SlotInvalidationCount;
    for (auto It = SlotEntries.CreateIterator(); It; ++It)
    {
        if (It.Value().SlotOwner == SlotOwner && It.Key().MaterialSlotIndex == MaterialSlotIndex)
        {
            It.RemoveCurrent();
            ++LifetimeStats.SlotMIDPruneCount;
        }
    }
}

void FDWCEditorPreviewMaterialCache::PruneUnusedHierarchies()
{
    TSet<FObjectKey> ReferencedParents;
    for (const TPair<FSlotKey, FSlotEntry>& Pair : SlotEntries)
    {
        ReferencedParents.Add(Pair.Key.TransientParent);
    }

    TSet<FObjectKey> ReferencedGraphs;
    for (auto It = ParentEntries.CreateIterator(); It; ++It)
    {
        if (!ReferencedParents.Contains(FObjectKey(It.Value().Parent)))
        {
            // Parent entry identity is the transient parent object. Keep only
            // entries that are still referenced by at least one slot MID.
            It.RemoveCurrent();
            ++LifetimeStats.ParentPruneCount;
            continue;
        }
        ReferencedGraphs.Add(It.Key().TransientBaseMaterial);
    }

    for (auto It = GraphEntries.CreateIterator(); It; ++It)
    {
        if (It.Value().State == EDWCEditorPreviewMaterialState::Compiling)
        {
            // A pending graph is still owned by the shader compiler. Keep it
            // alive until the next poll can promote or fail it.
            continue;
        }
        if (!ReferencedGraphs.Contains(FObjectKey(It.Value().Material)))
        {
            FDWCEditorPreviewMaterialFactory::CancelTransientBaseMaterialCompilation(
                It.Value().Material);
            It.RemoveCurrent();
            ++LifetimeStats.GraphPruneCount;
        }
    }
}

void FDWCEditorPreviewMaterialCache::Reset()
{
    ++LifetimeStats.ResetCount;
    SlotEntries.Reset();
    ParentEntries.Reset();
    for (TPair<FGraphKey, FGraphEntry>& Pair : GraphEntries)
    {
        FDWCEditorPreviewMaterialFactory::CancelTransientBaseMaterialCompilation(
            Pair.Value.Material);
    }
    GraphEntries.Reset();
}

void FDWCEditorPreviewMaterialCache::ResetDiagnosticCounters()
{
    LifetimeStats = FDWCEditorPreviewMaterialCacheStats();
}

FDWCEditorPreviewMaterialCacheStats FDWCEditorPreviewMaterialCache::GetStats() const
{
    FDWCEditorPreviewMaterialCacheStats Stats = LifetimeStats;
    Stats.GraphEntryCount = GraphEntries.Num();
    Stats.ParentEntryCount = ParentEntries.Num();
    Stats.SlotMIDEntryCount = SlotEntries.Num();
    for (const TPair<FGraphKey, FGraphEntry>& Pair : GraphEntries)
    {
        Stats.FailedGraphEntryCount +=
            Pair.Value.State == EDWCEditorPreviewMaterialState::Failed ? 1 : 0;
        Stats.PendingGraphEntryCount +=
            Pair.Value.State == EDWCEditorPreviewMaterialState::Compiling ? 1 : 0;
    }
    for (const TPair<FParentKey, FParentEntry>& Pair : ParentEntries)
    {
        Stats.FailedParentEntryCount += Pair.Value.Parent == nullptr ? 1 : 0;
    }
    Stats.EstimatedContainerBytes =
        static_cast<uint64>(GraphEntries.GetAllocatedSize()) +
        static_cast<uint64>(ParentEntries.GetAllocatedSize()) +
        static_cast<uint64>(SlotEntries.GetAllocatedSize());
    for (const TPair<FGraphKey, FGraphEntry>& Pair : GraphEntries)
    {
        Stats.EstimatedContainerBytes += static_cast<uint64>(Pair.Value.FailureMessage.GetAllocatedSize());
    }
    for (const TPair<FParentKey, FParentEntry>& Pair : ParentEntries)
    {
        Stats.EstimatedContainerBytes += static_cast<uint64>(Pair.Value.FailureMessage.GetAllocatedSize());
    }
    return Stats;
}

void FDWCEditorPreviewMaterialCache::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (TPair<FGraphKey, FGraphEntry>& Pair : GraphEntries)
    {
        Collector.AddReferencedObject(Pair.Value.Material);
        Collector.AddReferencedObject(Pair.Value.SourceMaterial);
    }
    for (TPair<FParentKey, FParentEntry>& Pair : ParentEntries)
    {
        Collector.AddReferencedObject(Pair.Value.Parent);
        Collector.AddReferencedObject(Pair.Value.SourceMaterial);
    }
    for (TPair<FSlotKey, FSlotEntry>& Pair : SlotEntries)
    {
        Collector.AddReferencedObject(Pair.Value.MID);
        Collector.AddReferencedObject(Pair.Value.SlotOwner);
        Collector.AddReferencedObject(Pair.Value.SourceMaterial);
    }
}

FString FDWCEditorPreviewMaterialCache::GetReferencerName() const
{
    return TEXT("FDWCEditorPreviewMaterialCache");
}
