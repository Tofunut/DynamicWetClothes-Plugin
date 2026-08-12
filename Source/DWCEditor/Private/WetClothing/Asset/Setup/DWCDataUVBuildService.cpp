// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCDataUVBuildService.h"

#include "Async/ParallelFor.h"

#include "DWCDataUVGenerator.h"
#include "DWCDataUVMetadataBuilder.h"
#include "DWCOriginalUVTopologyBuilder.h"
#include "DWCPreparedMeshResolver.h"
#include "DWCPreparedMeshEditTransaction.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "Utility/DWCLog.h"

namespace DWCDataUVBuildServicePrivate
{
    static constexpr int32 CanonicalDataUVLODIndex = 0;

    int32 ResolveMaxParallelMaterialSlots(const FDWCDataUVBuildOptions* Options)
    {
        return FMath::Clamp(Options != nullptr ? Options->MaxParallelMaterialSlots : 2, 1, 4);
    }

    template <typename BodyType>
    void RunBoundedMaterialSlotTasks(
        const int32 NumTasks,
        const int32 MaxParallelTasks,
        BodyType&& Body)
    {
        const int32 LaneCount = FMath::Clamp(MaxParallelTasks, 1, FMath::Max(NumTasks, 1));
        if (LaneCount == 1)
        {
            for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
            {
                Body(TaskIndex);
            }
            return;
        }

        ParallelFor(
            LaneCount,
            [&Body, NumTasks, LaneCount](const int32 LaneIndex)
            {
                for (int32 TaskIndex = LaneIndex; TaskIndex < NumTasks; TaskIndex += LaneCount)
                {
                    Body(TaskIndex);
                }
            });
    }

    uint64 EstimateDataUVPeakBytes(
        const USkeletalMesh* Mesh,
        const TArray<int32>& RequestedLODIndices,
        const int32 MaxParallelMaterialSlots)
    {
        constexpr uint64 MiB = 1024ull * 1024ull;
        const FSkeletalMeshRenderData* RenderData = Mesh != nullptr ? Mesh->GetResourceForRendering() : nullptr;
        if (RenderData == nullptr)
        {
            return 32ull * MiB;
        }

        TArray<int32, TInlineAllocator<4>> LODIndices;
        if (!RequestedLODIndices.IsEmpty())
        {
            for (const int32 LODIndex : RequestedLODIndices)
            {
                if (RenderData->LODRenderData.IsValidIndex(LODIndex))
                {
                    LODIndices.AddUnique(LODIndex);
                }
            }
        }
        else if (!RenderData->LODRenderData.IsEmpty())
        {
            LODIndices.Add(CanonicalDataUVLODIndex);
        }

        uint64 RetainedSnapshots = 0;
        uint64 LargestWorkerCopy = 0;
        for (const int32 LODIndex : LODIndices)
        {
            const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
            uint64 TriangleCount = 0;
            for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
            {
                TriangleCount += static_cast<uint64>(Section.NumTriangles);
            }
            const uint64 VertexCount = static_cast<uint64>(LODData.GetNumVertices());
            const uint64 SnapshotBytes = VertexCount * 128ull + TriangleCount * 160ull;
            const uint64 WorkerBytes = VertexCount * 192ull + TriangleCount * 256ull;
            RetainedSnapshots += SnapshotBytes;
            LargestWorkerCopy = FMath::Max(LargestWorkerCopy, WorkerBytes);
        }

        return FMath::Max<uint64>(
            32ull * MiB,
            16ull * MiB + RetainedSnapshots +
                LargestWorkerCopy * static_cast<uint64>(FMath::Max(MaxParallelMaterialSlots, 1)));
    }

    void SetFailure(FDWCDataUVBuildResult& Result, const FString& Message)
    {
        Result.BuildState = EDWCDataUVBuildState::Failed;
        Result.bSucceeded = false;
        Result.bRequiresUserConfirmation = false;
        Result.ConfirmationRequiredMaterialSlotIndices.Reset();
        Result.ResultSeverity = EDWCDataUVResultSeverity::Failed;
        Result.Message = Message;
    }

    void PersistLastSlotLODResults(
        UWetClothingAsset&           Asset,
        const FDWCDataUVBuildResult& Result,
        const bool                   bMergeWithExisting)
    {
#if WITH_EDITORONLY_DATA
        if (!bMergeWithExisting)
        {
            Asset.Derived.Inline.LastDataUVSlotLODResults = Result.SlotLODResults;
        }
        else
        {
            for (const FDWCDataUVSlotLODResult& NewRecord : Result.SlotLODResults)
            {
                Asset.Derived.Inline.LastDataUVSlotLODResults.RemoveAll(
                    [&NewRecord](const FDWCDataUVSlotLODResult& ExistingRecord)
                    {
                        return ExistingRecord.MaterialSlotIndex == NewRecord.MaterialSlotIndex &&
                               ExistingRecord.LODIndex == NewRecord.LODIndex;
                    });
                Asset.Derived.Inline.LastDataUVSlotLODResults.Add(NewRecord);
            }
        }
        Asset.Derived.Inline.LastDataUVSlotLODResults.Sort(
            [](const FDWCDataUVSlotLODResult& A, const FDWCDataUVSlotLODResult& B)
            {
                return A.MaterialSlotIndex == B.MaterialSlotIndex
                           ? A.LODIndex < B.LODIndex
                           : A.MaterialSlotIndex < B.MaterialSlotIndex;
            });
#endif
    }

    void RefreshPersistedFailedSlots(UWetClothingAsset& Asset)
    {
#if WITH_EDITORONLY_DATA
        TSet<int32> FailedSlotSet;
        for (const FDWCDataUVSlotLODResult& Record : Asset.Derived.Inline.LastDataUVSlotLODResults)
        {
            if (Record.State == EDWCDataUVSlotLODResultState::Failed &&
                Record.MaterialSlotIndex != INDEX_NONE)
            {
                FailedSlotSet.Add(Record.MaterialSlotIndex);
            }
        }

        Asset.Derived.Inline.FailedDataUVMaterialSlotIndices = FailedSlotSet.Array();
        Asset.Derived.Inline.FailedDataUVMaterialSlotIndices.Sort();
#endif
    }

    FDWCDataUVSlotWarning& FindOrAddSlotWarning(
        TArray<FDWCDataUVSlotWarning>& SlotWarnings,
        const int32                    MaterialSlotIndex)
    {
        for (FDWCDataUVSlotWarning& SlotWarning : SlotWarnings)
        {
            if (SlotWarning.MaterialSlotIndex == MaterialSlotIndex)
            {
                return SlotWarning;
            }
        }

        FDWCDataUVSlotWarning& SlotWarning = SlotWarnings.AddDefaulted_GetRef();
        SlotWarning.MaterialSlotIndex = MaterialSlotIndex;
        return SlotWarning;
    }

    void MergeSlotDiagnostics(
        TArray<FDWCDataUVSlotWarning>&       OutWarnings,
        const TArray<FDWCDataUVSlotWarning>& InWarnings)
    {
        for (const FDWCDataUVSlotWarning& InWarning : InWarnings)
        {
            // Preserve every analyzed slot record, including a completely clean result.
            // The Details UI must be able to distinguish a measured 0 from legacy/missing
            // diagnostic data. Only malformed records without a slot identity are ignored.
            if (InWarning.MaterialSlotIndex == INDEX_NONE)
            {
                continue;
            }

            FDWCDataUVSlotWarning& OutWarning = FindOrAddSlotWarning(
                OutWarnings,
                InWarning.MaterialSlotIndex);
            OutWarning.Degenerate3DTriangleCount += InWarning.Degenerate3DTriangleCount;
            OutWarning.DegenerateSourceUVTriangleCount += InWarning.DegenerateSourceUVTriangleCount;
            OutWarning.InvalidSourceUVTriangleCount += InWarning.InvalidSourceUVTriangleCount;
            OutWarning.PackedDegenerateTriangleCount += InWarning.PackedDegenerateTriangleCount;
            OutWarning.ExcludedVisibleTriangleCount += InWarning.ExcludedVisibleTriangleCount;
            OutWarning.TotalValid3DSurfaceArea += InWarning.TotalValid3DSurfaceArea;
            OutWarning.ExcludedVisible3DSurfaceArea += InWarning.ExcludedVisible3DSurfaceArea;
            OutWarning.ExcludedVisible3DSurfaceRatio = FMath::Max(
                OutWarning.ExcludedVisible3DSurfaceRatio,
                InWarning.ExcludedVisible3DSurfaceRatio);
            OutWarning.LargestConnectedExcluded3DSurfaceArea = FMath::Max(
                OutWarning.LargestConnectedExcluded3DSurfaceArea,
                InWarning.LargestConnectedExcluded3DSurfaceArea);
            OutWarning.LargestConnectedExcluded3DSurfaceRatio = FMath::Max(
                OutWarning.LargestConnectedExcluded3DSurfaceRatio,
                InWarning.LargestConnectedExcluded3DSurfaceRatio);
            OutWarning.SplitOriginalUVIslandCount += InWarning.SplitOriginalUVIslandCount;
            OutWarning.SelfOverlapPairCount += InWarning.SelfOverlapPairCount;
            OutWarning.BudgetFallbackIslandCount += InWarning.BudgetFallbackIslandCount;
            OutWarning.bVisibleExclusionSafetyLimitExceeded =
                OutWarning.bVisibleExclusionSafetyLimitExceeded ||
                InWarning.bVisibleExclusionSafetyLimitExceeded;
            OutWarning.ResultSeverity = DWCDataUVResultSeverity::Max(
                OutWarning.ResultSeverity,
                InWarning.ResultSeverity);
        }
    }

    int32 GetBudgetFallbackIslandCount(const TArray<FDWCDataUVSlotWarning>& SlotWarnings)
    {
        int32 Count = 0;
        for (const FDWCDataUVSlotWarning& SlotWarning : SlotWarnings)
        {
            Count += SlotWarning.BudgetFallbackIslandCount;
        }
        return Count;
    }

    FString ResolveMaterialSlotName(const USkeletalMesh* SkeletalMesh, const int32 MaterialSlotIndex)
    {
        if (SkeletalMesh != nullptr && SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            const FSkeletalMaterial& Material = SkeletalMesh->GetMaterials()[MaterialSlotIndex];
            if (!Material.MaterialSlotName.IsNone())
            {
                return Material.MaterialSlotName.ToString();
            }
            if (!Material.ImportedMaterialSlotName.IsNone())
            {
                return Material.ImportedMaterialSlotName.ToString();
            }
        }
        return TEXT("Unknown");
    }

    FString BuildSlotWarningLogText(
        const FDWCDataUVSlotWarning& Warning,
        const USkeletalMesh*         SkeletalMesh)
    {
        TArray<FString> Lines;
        Lines.Add(FString::Printf(
            TEXT("[DWC UV Channel Diagnostic] Slot %d (%s)"),
            Warning.MaterialSlotIndex,
            *ResolveMaterialSlotName(SkeletalMesh, Warning.MaterialSlotIndex)));
        if (Warning.DegenerateSourceUVTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Degenerate Source UV triangles excluded: %d"),
                Warning.DegenerateSourceUVTriangleCount));
        }
        if (Warning.Degenerate3DTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Zero-area 3D triangles excluded: %d"),
                Warning.Degenerate3DTriangleCount));
        }
        if (Warning.PackedDegenerateTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Near-degenerate packed triangles excluded: %d"),
                Warning.PackedDegenerateTriangleCount));
        }
        if (Warning.InvalidSourceUVTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Invalid Source UV triangles excluded: %d"),
                Warning.InvalidSourceUVTriangleCount));
        }
        if (Warning.SelfOverlapPairCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Overlapping Source UV triangle pairs separated: %d"),
                Warning.SelfOverlapPairCount));
        }
        if (Warning.SplitOriginalUVIslandCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Physical Source UV shells split across packing charts: %d"),
                Warning.SplitOriginalUVIslandCount));
        }
        if (Warning.BudgetFallbackIslandCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Overlap analysis safety-budget fallbacks: %d"),
                Warning.BudgetFallbackIslandCount));
        }
        if (Warning.ExcludedVisibleTriangleCount > 0)
        {
            Lines.Add(Warning.bVisibleExclusionSafetyLimitExceeded
                          ? FString::Printf(
                                TEXT("- Generated without %.2f%% of this material's surface (automatic limit %.2f%%; largest excluded region %.2f%%; excluded triangles %d)"),
                                Warning.ExcludedVisible3DSurfaceRatio * 100.0,
                                DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0,
                                Warning.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                                Warning.ExcludedVisibleTriangleCount)
                          : FString::Printf(
                                TEXT("- Excluded surface: %.2f%% (largest excluded region %.2f%%; excluded triangles %d)"),
                                Warning.ExcludedVisible3DSurfaceRatio * 100.0,
                                Warning.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                                Warning.ExcludedVisibleTriangleCount));
        }

        TArray<FString> Results;
        if (Warning.Degenerate3DTriangleCount > 0)
        {
            Results.Add(TEXT("zero-area 3D triangles were excluded without visible coverage loss"));
        }
        if (Warning.DegenerateSourceUVTriangleCount > 0 || Warning.InvalidSourceUVTriangleCount > 0)
        {
            Results.Add(TEXT("problem triangles were excluded before chart generation"));
        }
        if (Warning.PackedDegenerateTriangleCount > 0)
        {
            Results.Add(TEXT("near-degenerate packed triangles were excluded from DWC-derived data"));
        }
        if (Warning.SelfOverlapPairCount > 0 || Warning.SplitOriginalUVIslandCount > 0)
        {
            Results.Add(TEXT("self-overlapping physical Source UV shells were separated into topology-connected conflict-free charts, then safe adjacent fragments were merged"));
        }
        if (Warning.BudgetFallbackIslandCount > 0)
        {
            Results.Add(TEXT("budget fallback islands were conservatively split into individual triangle charts"));
        }
        if (!Results.IsEmpty())
        {
            Lines.Add(FString::Printf(TEXT("- Result: %s"), *FString::Join(Results, TEXT("; "))));
        }
        Lines.Add(FString::Printf(
            TEXT("- Status: %s"),
            DWCDataUVResultSeverity::Normalize(Warning.ResultSeverity) == EDWCDataUVResultSeverity::ReadyWithWarnings
                ? TEXT("Ready with warnings")
            : DWCDataUVResultSeverity::Normalize(Warning.ResultSeverity) == EDWCDataUVResultSeverity::Failed
                ? TEXT("Failed")
                : TEXT("Ready")));
        return FString::Join(Lines, TEXT("\n"));
    }

    FString BuildLODList(const TArray<int32>& LODIndices)
    {
        if (LODIndices.IsEmpty())
        {
            return TEXT("None");
        }

        TArray<int32> SortedLODIndices = LODIndices;
        SortedLODIndices.Sort();

        TArray<FString> Ranges;
        int32           RangeStart = SortedLODIndices[0];
        int32           RangeEnd = RangeStart;
        for (int32 Index = 1; Index < SortedLODIndices.Num(); ++Index)
        {
            const int32 LODIndex = SortedLODIndices[Index];
            if (LODIndex == RangeEnd + 1)
            {
                RangeEnd = LODIndex;
                continue;
            }

            Ranges.Add(RangeStart == RangeEnd
                           ? FString::Printf(TEXT("LOD%d"), RangeStart)
                           : FString::Printf(TEXT("LOD%d-LOD%d"), RangeStart, RangeEnd));
            RangeStart = LODIndex;
            RangeEnd = LODIndex;
        }

        Ranges.Add(RangeStart == RangeEnd
                       ? FString::Printf(TEXT("LOD%d"), RangeStart)
                       : FString::Printf(TEXT("LOD%d-LOD%d"), RangeStart, RangeEnd));
        return FString::Join(Ranges, TEXT(", "));
    }

    bool ResolveGeneratedLODRange(
        const UWetClothingAsset&       Asset,
        const FSkeletalMeshRenderData& RenderData,
        int32&                         OutFirstLODIndex,
        int32&                         OutLastLODIndex,
        FString*                       OutErrorMessage)
    {
        if (RenderData.LODRenderData.IsEmpty())
        {
            if (OutErrorMessage)
                *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh has no render LOD data.");
            return false;
        }

        const int32 LastAvailableLODIndex = RenderData.LODRenderData.Num() - 1;
        OutFirstLODIndex = FMath::Clamp(Asset.GetSetupSettings().FirstGeneratedLODIndex, 0, LastAvailableLODIndex);
        OutLastLODIndex = FMath::Clamp(Asset.GetSetupSettings().LastGeneratedLODIndex, OutFirstLODIndex, LastAvailableLODIndex);
        if (OutErrorMessage)
            OutErrorMessage->Reset();
        return true;
    }

    bool BuildMeshDescriptionSnapshot(
        USkeletalMesh*    SkeletalMesh,
        const int32       LODIndex,
        FMeshDescription& OutMeshDescription,
        FString*          OutErrorMessage)
    {
        if (SkeletalMesh == nullptr)
        {
            if (OutErrorMessage)
                *OutErrorMessage = TEXT("The Skeletal Mesh is unavailable.");
            return false;
        }

        if (const FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex))
        {
            OutMeshDescription = *MeshDescription;
            if (OutErrorMessage)
                OutErrorMessage->Reset();
            return true;
        }

        const FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
        if (ImportedModel != nullptr && ImportedModel->LODModels.IsValidIndex(LODIndex))
        {
            ImportedModel->LODModels[LODIndex].GetMeshDescription(
                SkeletalMesh,
                LODIndex,
                OutMeshDescription);
            if (!OutMeshDescription.IsEmpty())
            {
                if (OutErrorMessage)
                    OutErrorMessage->Reset();
                return true;
            }
        }

        if (OutErrorMessage)
        {
            *OutErrorMessage = FString::Printf(
                TEXT("LOD%d does not expose Source MeshDescription data for DWC UV analysis."),
                LODIndex);
        }
        return false;
    }

    bool RestorePreparedLODFromSource(
        USkeletalMesh* SourceMesh,
        USkeletalMesh* PreparedMesh,
        const int32    LODIndex,
        FString*       OutErrorMessage)
    {
        if (SourceMesh == nullptr || PreparedMesh == nullptr)
        {
            if (OutErrorMessage)
                *OutErrorMessage = TEXT("Source or Prepared Skeletal Mesh is unavailable.");
            return false;
        }

        FMeshDescription SourceSnapshot;
        if (!BuildMeshDescriptionSnapshot(SourceMesh, LODIndex, SourceSnapshot, OutErrorMessage))
        {
            return false;
        }

        PreparedMesh->Modify();
        FMeshDescription* PreparedMeshDescription = PreparedMesh->GetMeshDescription(LODIndex);
        if (PreparedMeshDescription == nullptr)
        {
            PreparedMeshDescription = PreparedMesh->CreateMeshDescription(
                LODIndex,
                MoveTemp(SourceSnapshot));
        }
        else
        {
            *PreparedMeshDescription = MoveTemp(SourceSnapshot);
        }

        if (PreparedMeshDescription == nullptr)
        {
            if (OutErrorMessage)
            {
                *OutErrorMessage = FString::Printf(
                    TEXT("Failed to restore Prepared Mesh LOD%d from the Source Mesh."),
                    LODIndex);
            }
            return false;
        }

        if (OutErrorMessage)
            OutErrorMessage->Reset();
        return true;
    }

    bool ResolveSourceSafetyPayloadLODIndices(
        const UWetClothingAsset&       Asset,
        const FSkeletalMeshRenderData& RenderData,
        const FDWCDataUVBuildOptions*  Options,
        TArray<int32>&                 OutPayloadLODIndices,
        FString*                       OutErrorMessage)
    {
        OutPayloadLODIndices.Reset();

        int32 FirstLODIndex = 0;
        int32 LastLODIndex = 0;
        if (!ResolveGeneratedLODRange(
                Asset,
                RenderData,
                FirstLODIndex,
                LastLODIndex,
                OutErrorMessage))
        {
            return false;
        }

        if (Options != nullptr && !Options->TargetLODIndices.IsEmpty())
        {
            const int32 LastAvailableLODIndex = RenderData.LODRenderData.Num() - 1;
            for (const int32 RequestedLODIndex : Options->TargetLODIndices)
            {
                if (RequestedLODIndex >= 0 && RequestedLODIndex <= LastAvailableLODIndex)
                {
                    OutPayloadLODIndices.AddUnique(RequestedLODIndex);
                }
            }
            OutPayloadLODIndices.Sort();
            if (OutPayloadLODIndices.IsEmpty())
            {
                if (OutErrorMessage)
                    *OutErrorMessage = TEXT("No valid target LOD was supplied for DWC UV generation.");
                return false;
            }
            if (OutErrorMessage)
                OutErrorMessage->Reset();
            return true;
        }

        OutPayloadLODIndices.Reserve(LastLODIndex - FirstLODIndex + 2);
        OutPayloadLODIndices.Add(CanonicalDataUVLODIndex);
        if (Asset.GetSetupSettings().bBuildGPUWetnessMapSimulationData)
        {
            for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
            {
                OutPayloadLODIndices.AddUnique(LODIndex);
            }
        }
        OutPayloadLODIndices.Sort();
        if (OutErrorMessage)
            OutErrorMessage->Reset();
        return true;
    }

    struct FSourceMeshSafetyPreflightResult
    {
        bool                          bSucceeded = true;
        bool                          bRequiresUserConfirmation = false;
        int32                         DataUVChannelIndex = INDEX_NONE;
        int32                         FailureLODIndex = INDEX_NONE;
        EDWCDataUVResultSeverity      ResultSeverity = EDWCDataUVResultSeverity::Ready;
        FDWCDataUVValidationFailure   ValidationFailure;
        TArray<int32>                 TargetLODIndices;
        TArray<FDWCDataUVSlotWarning> SlotWarnings;
        TSet<int32>                   FailedMaterialSlotIndices;
        TSet<int32>                   ConfirmationRequiredMaterialSlotIndices;

        // Authoritative per-slot/per-LOD analysis produced from the immutable Source Mesh.
        // When a pristine rebuild is requested, these exact plans are applied to the
        // Prepared Mesh after it is restored from Source. Do not re-analyze Prepared Mesh:
        // doing so can make a previously approved exclusion disappear from diagnostics.
        TMap<int32, TMap<int32, FDWCDataUVGenerationResult>> AnalysisResultsBySlotLOD;
        FString                                              Message;
    };

    FSourceMeshSafetyPreflightResult RunSourceMeshSafetyPreflight(
        UWetClothingAsset&            Asset,
        USkeletalMesh*                SourceMesh,
        const TArray<int32>&          SortedMaterialSlotIndices,
        const bool                    bForceNewAsset,
        const bool                    bAllowOverwriteExistingDataUVChannel,
        const bool                    bUsePreferredDataUVChannel,
        const FDWCDataUVBuildOptions* Options)
    {
        FSourceMeshSafetyPreflightResult Preflight;
        if (SourceMesh == nullptr)
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = TEXT("The Wet Clothing Asset has no Source Skeletal Mesh.");
            return Preflight;
        }

        const FSkeletalMeshRenderData* SourceRenderData = SourceMesh->GetResourceForRendering();
        if (SourceRenderData == nullptr || SourceRenderData->LODRenderData.IsEmpty())
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = TEXT("The Source Skeletal Mesh has no render LOD data for DWC UV analysis.");
            return Preflight;
        }

        FString LODRangeError;
        if (!ResolveSourceSafetyPayloadLODIndices(
                Asset,
                *SourceRenderData,
                Options,
                Preflight.TargetLODIndices,
                &LODRangeError))
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = LODRangeError;
            return Preflight;
        }

        TArray<FMeshDescription> SourceLODSnapshots;
        TArray<bool>             LODRenderDataAvailable;
        TArray<bool>             LODHasVertices;
        SourceLODSnapshots.Reserve(Preflight.TargetLODIndices.Num());
        LODRenderDataAvailable.Reserve(Preflight.TargetLODIndices.Num());
        LODHasVertices.Reserve(Preflight.TargetLODIndices.Num());
        for (const int32 LODIndex : Preflight.TargetLODIndices)
        {
            const bool bHasRenderLOD = SourceRenderData->LODRenderData.IsValidIndex(LODIndex);
            LODRenderDataAvailable.Add(bHasRenderLOD);
            LODHasVertices.Add(
                bHasRenderLOD && SourceRenderData->LODRenderData[LODIndex].GetNumVertices() > 0);

            FMeshDescription SourceSnapshot;
            FString          SnapshotError;
            if (!BuildMeshDescriptionSnapshot(SourceMesh, LODIndex, SourceSnapshot, &SnapshotError))
            {
                Preflight.bSucceeded = false;
                Preflight.FailureLODIndex = LODIndex;
                Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
                Preflight.Message = SnapshotError;
                return Preflight;
            }
            SourceLODSnapshots.Add(MoveTemp(SourceSnapshot));
        }

        int32 CandidateDataUVChannelIndex = Asset.GetDWCDataUVChannelIndex();
        if (CandidateDataUVChannelIndex == INDEX_NONE || bForceNewAsset || bUsePreferredDataUVChannel)
        {
            CandidateDataUVChannelIndex = FMath::Clamp(
                Asset.GetSetupSettings().PreferredDWCDataUVChannelIndex,
                0,
                7);
        }
        if (CandidateDataUVChannelIndex == Asset.GetOriginalUVChannelIndex())
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = TEXT("DWC UV Channel cannot overwrite the configured Original UV Channel.");
            return Preflight;
        }

        const bool bAllowOverwriteExistingChannel =
            bAllowOverwriteExistingDataUVChannel ||
            Asset.GetDWCDataUVChannelIndex() == CandidateDataUVChannelIndex;
        const int32        SourceUVChannelIndex = Asset.GetOriginalUVChannelIndex();
        const TSet<int32>* ConfirmedSlots = Options != nullptr
                                                ? &Options->ConfirmedVisibleExclusionMaterialSlotIndices
                                                : nullptr;
        const bool         bRequireAllMaterialSlots = Options != nullptr && Options->bRequireAllMaterialSlots;

        TMap<FName, int32>               MaterialSlotIndexByName;
        const TArray<FSkeletalMaterial>& SourceMaterials = SourceMesh->GetMaterials();
        for (int32 MaterialIndex = 0; MaterialIndex < SourceMaterials.Num(); ++MaterialIndex)
        {
            const FSkeletalMaterial& Material = SourceMaterials[MaterialIndex];
            if (!Material.MaterialSlotName.IsNone())
            {
                MaterialSlotIndexByName.Add(Material.MaterialSlotName, MaterialIndex);
            }
            if (!Material.ImportedMaterialSlotName.IsNone())
            {
                MaterialSlotIndexByName.Add(Material.ImportedMaterialSlotName, MaterialIndex);
            }
        }

        struct FSlotSourcePreflightResult
        {
            int32                              MaterialSlotIndex = INDEX_NONE;
            bool                               bSucceeded = true;
            bool                               bGeneratedPayload = false;
            int32                              CandidateDataUVChannelIndex = INDEX_NONE;
            int32                              FailureLODIndex = INDEX_NONE;
            FDWCDataUVValidationFailure        ValidationFailure;
            TArray<int32>                      ResultLODIndices;
            TArray<FDWCDataUVGenerationResult> Results;
            FString                            FailureMessage;
        };

        TArray<FSlotSourcePreflightResult> SlotResults;
        SlotResults.SetNum(SortedMaterialSlotIndices.Num());
        RunBoundedMaterialSlotTasks(
            SortedMaterialSlotIndices.Num(),
            ResolveMaxParallelMaterialSlots(Options),
            [&](const int32 SlotArrayIndex)
            {
                FSlotSourcePreflightResult& SlotResult = SlotResults[SlotArrayIndex];
                SlotResult.MaterialSlotIndex = SortedMaterialSlotIndices[SlotArrayIndex];
                SlotResult.CandidateDataUVChannelIndex = CandidateDataUVChannelIndex;
                SlotResult.ResultLODIndices.Reserve(Preflight.TargetLODIndices.Num());
                SlotResult.Results.Reserve(Preflight.TargetLODIndices.Num());

                for (int32 LODArrayIndex = 0; LODArrayIndex < Preflight.TargetLODIndices.Num(); ++LODArrayIndex)
                {
                    const int32 LODIndex = Preflight.TargetLODIndices[LODArrayIndex];
                    if (!LODRenderDataAvailable.IsValidIndex(LODArrayIndex) ||
                        !LODRenderDataAvailable[LODArrayIndex])
                    {
                        SlotResult.bSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = TEXT("Render data is unavailable on the Source Mesh.");
                        return;
                    }
                    if (!LODHasVertices.IsValidIndex(LODArrayIndex) || !LODHasVertices[LODArrayIndex])
                    {
                        SlotResult.bSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = TEXT("The Source Mesh LOD has no vertices.");
                        return;
                    }

                    FMeshDescription WorkingMeshDescription = SourceLODSnapshots[LODArrayIndex];
                    const bool       bAllowOverwriteForSlot =
                        bAllowOverwriteExistingChannel ||
                        SlotResult.bGeneratedPayload ||
                        LODIndex != CanonicalDataUVLODIndex;
                    FDWCDataUVGenerationResult UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
                        SourceMesh,
                        LODIndex,
                        SourceUVChannelIndex,
                        SlotResult.CandidateDataUVChannelIndex,
                        bAllowOverwriteForSlot,
                        SlotResult.MaterialSlotIndex,
                        nullptr,
                        true,
                        &WorkingMeshDescription,
                        true,
                        true,
                        &MaterialSlotIndexByName,
                        ConfirmedSlots != nullptr && ConfirmedSlots->Contains(SlotResult.MaterialSlotIndex));
                    SlotResult.ResultLODIndices.Add(LODIndex);
                    SlotResult.Results.Add(UVResult);

                    if (!UVResult.bSucceeded)
                    {
                        SlotResult.bSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = UVResult.Message;
                        SlotResult.ValidationFailure = UVResult.ValidationFailure;
                        return;
                    }
                    if (UVResult.bTargetSlotNotPresent)
                    {
                        continue;
                    }

                    if (!SlotResult.bGeneratedPayload)
                    {
                        SlotResult.CandidateDataUVChannelIndex = UVResult.UVChannelIndex;
                        SlotResult.bGeneratedPayload = true;
                    }
                    else if (UVResult.UVChannelIndex != SlotResult.CandidateDataUVChannelIndex)
                    {
                        SlotResult.bSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = FString::Printf(
                            TEXT("Source validation resolved UV%d, but the slot requires UV%d."),
                            UVResult.UVChannelIndex,
                            SlotResult.CandidateDataUVChannelIndex);
                        return;
                    }
                }
            });

        TArray<FString> FailureMessages;
        int32           SucceededSlotCount = 0;
        bool            bResolvedDataUVChannel = false;
        for (const FSlotSourcePreflightResult& SlotResult : SlotResults)
        {
            TMap<int32, FDWCDataUVGenerationResult>& SourceResultsByLOD =
                Preflight.AnalysisResultsBySlotLOD.FindOrAdd(SlotResult.MaterialSlotIndex);
            for (int32 ResultIndex = 0; ResultIndex < SlotResult.Results.Num(); ++ResultIndex)
            {
                if (SlotResult.ResultLODIndices.IsValidIndex(ResultIndex))
                {
                    SourceResultsByLOD.Add(
                        SlotResult.ResultLODIndices[ResultIndex],
                        SlotResult.Results[ResultIndex]);
                }
            }

            for (const FDWCDataUVGenerationResult& UVResult : SlotResult.Results)
            {
                MergeSlotDiagnostics(Preflight.SlotWarnings, UVResult.SlotWarnings);
                Preflight.ResultSeverity = DWCDataUVResultSeverity::Max(
                    Preflight.ResultSeverity,
                    UVResult.ResultSeverity);
                if (!UVResult.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
                {
                    Preflight.ConfirmationRequiredMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
                }
            }

            if (!SlotResult.bSucceeded)
            {
                Preflight.FailedMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
                if (Preflight.FailureLODIndex == INDEX_NONE)
                {
                    Preflight.FailureLODIndex = SlotResult.FailureLODIndex;
                    Preflight.ValidationFailure = SlotResult.ValidationFailure;
                }
                FailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed Source Mesh validation at LOD%d: %s"),
                    SlotResult.MaterialSlotIndex,
                    SlotResult.FailureLODIndex,
                    *SlotResult.FailureMessage));
                continue;
            }

            ++SucceededSlotCount;
            if (SlotResult.bGeneratedPayload)
            {
                if (!bResolvedDataUVChannel)
                {
                    Preflight.DataUVChannelIndex = SlotResult.CandidateDataUVChannelIndex;
                    bResolvedDataUVChannel = true;
                }
                else if (Preflight.DataUVChannelIndex != SlotResult.CandidateDataUVChannelIndex)
                {
                    Preflight.FailedMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
                    FailureMessages.Add(FString::Printf(
                        TEXT("Material Slot %d resolved UV%d during Source Mesh validation, but the batch requires UV%d."),
                        SlotResult.MaterialSlotIndex,
                        SlotResult.CandidateDataUVChannelIndex,
                        Preflight.DataUVChannelIndex));
                }
            }
        }

        Preflight.SlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            });

        if (bRequireAllMaterialSlots && !Preflight.FailedMaterialSlotIndices.IsEmpty())
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = FailureMessages.IsEmpty()
                                    ? TEXT("One or more material slots failed Source Mesh DWC UV validation.")
                                    : FString::Join(FailureMessages, TEXT("\n"));
            return Preflight;
        }
        if (SucceededSlotCount == 0)
        {
            Preflight.bSucceeded = false;
            Preflight.ResultSeverity = EDWCDataUVResultSeverity::Failed;
            Preflight.Message = FailureMessages.IsEmpty()
                                    ? TEXT("No material slot produced a usable DWC UV layout from the Source Mesh.")
                                    : FString::Join(FailureMessages, TEXT("\n"));
            return Preflight;
        }

        if (!Preflight.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
        {
            Preflight.bRequiresUserConfirmation = true;
            Preflight.ResultSeverity = DWCDataUVResultSeverity::Max(
                Preflight.ResultSeverity,
                EDWCDataUVResultSeverity::ReadyWithWarnings);
            Preflight.Message = FString::Printf(
                TEXT("Some Source Mesh triangles cannot be included in the DWC UV. The affected surface exceeds the %.2f%% automatic limit. Choose how to handle each affected material slot before the Prepared Mesh is changed."),
                DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0);
        }
        return Preflight;
    }
} // namespace DWCDataUVBuildServicePrivate

FDWCDataUVBuildResult FDWCDataUVBuildService::Generate(
    UWetClothingAsset&            Asset,
    const bool                    bForceNewAsset,
    const bool                    bAllowOverwriteExistingDataUVChannel,
    const bool                    bUsePreferredDataUVChannel,
    const FDWCDataUVBuildOptions* Options)
{
    using namespace DWCDataUVBuildServicePrivate;

    FDWCDataUVBuildResult Result;
    USkeletalMesh*        TouchedMesh = Asset.GetRuntimeSkeletalMesh();
    const bool            bReplacingExistingLayout = Asset.HasLockedDataUVLayout();
    const bool            bSourceMeshContentChanged = Asset.HasSourceMeshContentChanged();
    const bool            bEffectiveForceNewAsset = bForceNewAsset || bSourceMeshContentChanged;
    const bool            bMergeWithExistingLayout =
        Options != nullptr && Options->bMergeWithExistingLayout && bReplacingExistingLayout && !bSourceMeshContentChanged;
    const bool bRequireAllMaterialSlots =
        Options != nullptr && Options->bRequireAllMaterialSlots;
    const bool bRebuildPreparedLODsFromSource =
        Options != nullptr && Options->bRebuildPreparedLODsFromSource;
    const TSet<int32>* ConfirmedVisibleExclusionMaterialSlotIndices =
        Options != nullptr ? &Options->ConfirmedVisibleExclusionMaterialSlotIndices : nullptr;
    const TSet<int32>* SkippedMaterialSlotIndices =
        Options != nullptr ? &Options->SkippedMaterialSlotIndices : nullptr;
    if (SkippedMaterialSlotIndices != nullptr)
    {
        Result.SkippedMaterialSlotIndices = *SkippedMaterialSlotIndices;
    }

    TSet<int32> AuthoredWettableMaterialSlotIndices;
    TSet<int32> WettableMaterialSlotIndices;
    for (const FWetClothingAuthoredMaterialSlot& Slot : Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (Slot.bIsWettableSlot && Slot.MaterialSlotIndex != INDEX_NONE)
        {
            AuthoredWettableMaterialSlotIndices.Add(Slot.MaterialSlotIndex);
            if (SkippedMaterialSlotIndices == nullptr ||
                !SkippedMaterialSlotIndices->Contains(Slot.MaterialSlotIndex))
            {
                WettableMaterialSlotIndices.Add(Slot.MaterialSlotIndex);
            }
        }
    }
    if (WettableMaterialSlotIndices.IsEmpty())
    {
        if (!AuthoredWettableMaterialSlotIndices.IsEmpty() &&
            SkippedMaterialSlotIndices != nullptr &&
            !SkippedMaterialSlotIndices->IsEmpty())
        {
            Result.MarkCancelled(TEXT("No DWC UV changes were made because all affected material slots were skipped."));
            return Result;
        }

        SetFailure(Result, TEXT("Select at least one Wettable material slot in Part Edit before generating DWC UV Channel."));
        return Result;
    }

    TArray<int32> SortedWettableMaterialSlotIndices = WettableMaterialSlotIndices.Array();
    SortedWettableMaterialSlotIndices.Sort();

    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    if (SourceMesh == nullptr)
    {
        SetFailure(Result, TEXT("The Wet Clothing Asset has no Source Skeletal Mesh."));
        return Result;
    }

    FDWCEditorMemoryLease DataUVMemoryLease;
    if (Options != nullptr && Options->ResourceGovernor.IsValid())
    {
        FDWCEditorResourceReservationRequest Reservation;
        Reservation.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
        Reservation.Bytes = EstimateDataUVPeakBytes(
            SourceMesh,
            Options->TargetLODIndices,
            ResolveMaxParallelMaterialSlots(Options));
        Reservation.Owner.Key.Namespace = TEXT("DWC.DataUV.Build");
        Reservation.Owner.Key.ResourceGuid = FGuid::NewGuid();
        Reservation.Owner.SessionEpoch = Options->ResourceSessionEpoch.IsValid()
            ? Options->ResourceSessionEpoch
            : FGuid::NewGuid();
        Reservation.Owner.OperationId = FPlatformTime::Cycles64();
        Reservation.Owner.Generation = 1;
        Reservation.DebugName = FString::Printf(
            TEXT("DWC Data UV build for %s"),
            *Asset.GetName());

        EDWCEditorResourceAdmissionResult Admission = EDWCEditorResourceAdmissionResult::InvalidRequest;
        FString ReservationError;
        DataUVMemoryLease = Options->ResourceGovernor->TryAcquireForAdmission(
            Reservation,
            Admission,
            &ReservationError);
        if (!DataUVMemoryLease.IsValid())
        {
            SetFailure(
                Result,
                ReservationError.IsEmpty()
                    ? TEXT("DWC UV generation could not reserve its bounded working memory.")
                    : FString::Printf(
                          TEXT("DWC UV generation could not reserve its bounded working memory: %s"),
                          *ReservationError));
            return Result;
        }
    }

    // Cheap ownership/path validation must finish before invalidation, progress UI,
    // project-wide recovery, or UV analysis begins.
    const FDWCPreparedMeshPreflightResult PreparedMeshPreflight =
        FDWCPreparedMeshResolver::Preflight(Asset, bEffectiveForceNewAsset);
    if (!PreparedMeshPreflight.bCanProceed)
    {
        SetFailure(Result, PreparedMeshPreflight.ErrorMessage.IsEmpty()
                               ? TEXT("The DWC Prepared Mesh preflight failed.")
                               : PreparedMeshPreflight.ErrorMessage);
        return Result;
    }

    // Safety confirmation is intentionally evaluated against the immutable Source Mesh.
    // A previous DWC build may have split Prepared-Mesh VertexInstances and therefore hide
    // the very degenerate/excluded regions that the user is supposed to approve. Nothing
    // below this block resolves or mutates the Prepared Mesh until every warning is decided.
    FSourceMeshSafetyPreflightResult SourceSafetyPreflight;
    const bool                       bUseSourceMeshForSafetyPreflight =
        Options != nullptr && Options->bUseSourceMeshForSafetyPreflight;
    if (bUseSourceMeshForSafetyPreflight)
    {
        SourceSafetyPreflight = RunSourceMeshSafetyPreflight(
            Asset,
            SourceMesh,
            SortedWettableMaterialSlotIndices,
            bEffectiveForceNewAsset,
            bAllowOverwriteExistingDataUVChannel,
            bUsePreferredDataUVChannel,
            Options);

        if (!SourceSafetyPreflight.bSucceeded)
        {
            Result.FailureLODIndex = SourceSafetyPreflight.FailureLODIndex;
            Result.ValidationFailure = SourceSafetyPreflight.ValidationFailure;
            Result.FailedMaterialSlotIndices = SourceSafetyPreflight.FailedMaterialSlotIndices;
            Result.SlotWarnings = SourceSafetyPreflight.SlotWarnings;
            SetFailure(
                Result,
                SourceSafetyPreflight.Message.IsEmpty()
                    ? TEXT("Source Mesh DWC UV validation failed.")
                    : SourceSafetyPreflight.Message);
            return Result;
        }

        if (SourceSafetyPreflight.bRequiresUserConfirmation)
        {
            Result.BuildState = EDWCDataUVBuildState::RequiresConfirmation;
            Result.bSucceeded = false;
            Result.bRequiresUserConfirmation = true;
            Result.DataUVChannelIndex = SourceSafetyPreflight.DataUVChannelIndex;
            Result.WettableMaterialSlotCount = SortedWettableMaterialSlotIndices.Num();
            Result.TargetLODIndices = SourceSafetyPreflight.TargetLODIndices;
            Result.ResultSeverity = SourceSafetyPreflight.ResultSeverity;
            Result.SlotWarnings = SourceSafetyPreflight.SlotWarnings;
            Result.ConfirmationRequiredMaterialSlotIndices =
                SourceSafetyPreflight.ConfirmationRequiredMaterialSlotIndices;
            Result.FailedMaterialSlotIndices = SourceSafetyPreflight.FailedMaterialSlotIndices;

            for (const FDWCDataUVSlotWarning& Warning : Result.SlotWarnings)
            {
                Result.Degenerate3DTriangleCount += Warning.Degenerate3DTriangleCount;
                Result.DegenerateSourceUVTriangleCount += Warning.DegenerateSourceUVTriangleCount;
                Result.InvalidSourceUVTriangleCount += Warning.InvalidSourceUVTriangleCount;
                Result.PackedDegenerateTriangleCount += Warning.PackedDegenerateTriangleCount;
                Result.ExcludedVisibleTriangleCount += Warning.ExcludedVisibleTriangleCount;
                Result.ExcludedVisible3DSurfaceArea += Warning.ExcludedVisible3DSurfaceArea;
                Result.ExcludedVisible3DSurfaceRatio = FMath::Max(
                    Result.ExcludedVisible3DSurfaceRatio,
                    Warning.ExcludedVisible3DSurfaceRatio);
                Result.LargestConnectedExcluded3DSurfaceArea = FMath::Max(
                    Result.LargestConnectedExcluded3DSurfaceArea,
                    Warning.LargestConnectedExcluded3DSurfaceArea);
                Result.LargestConnectedExcluded3DSurfaceRatio = FMath::Max(
                    Result.LargestConnectedExcluded3DSurfaceRatio,
                    Warning.LargestConnectedExcluded3DSurfaceRatio);
                Result.SplitOriginalUVIslandCount += Warning.SplitOriginalUVIslandCount;
                Result.SelfOverlapPairCount += Warning.SelfOverlapPairCount;
                Result.BudgetFallbackIslandCount += Warning.BudgetFallbackIslandCount;
                Result.ExcludedTriangleCount += Warning.Degenerate3DTriangleCount +
                                                Warning.DegenerateSourceUVTriangleCount +
                                                Warning.InvalidSourceUVTriangleCount +
                                                Warning.PackedDegenerateTriangleCount;
            }

            Result.Message = SourceSafetyPreflight.Message;
            return Result;
        }
    }

    const FDWCPreparedMeshResolveResult ResolveResult = FDWCPreparedMeshResolver::Resolve(
        Asset,
        bEffectiveForceNewAsset);
    USkeletalMesh* PreparedMesh = ResolveResult.Mesh;
    TouchedMesh = PreparedMesh != nullptr ? PreparedMesh : TouchedMesh;
    if (PreparedMesh == nullptr)
    {
        SetFailure(Result, ResolveResult.ErrorMessage.IsEmpty()
                               ? TEXT("Failed to resolve the DWC Prepared Skeletal Mesh.")
                               : ResolveResult.ErrorMessage);
        return Result;
    }
    Result.PreparedMesh = PreparedMesh;

    // DWC UV Channel generation is an invalidation boundary. This starts only after
    // Prepared Mesh preflight has succeeded, so path conflicts fail immediately.
    FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(Asset, TouchedMesh);
    ON_SCOPE_EXIT
    {
        FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(Asset, TouchedMesh);
    };

    const USkeletalMesh*           LODReferenceMesh = bRebuildPreparedLODsFromSource ? SourceMesh : PreparedMesh;
    const FSkeletalMeshRenderData* RenderData = LODReferenceMesh != nullptr
                                                    ? LODReferenceMesh->GetResourceForRendering()
                                                    : nullptr;
    if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        SetFailure(
            Result,
            bRebuildPreparedLODsFromSource
                ? TEXT("The Source Skeletal Mesh has no render LOD data for the pristine DWC UV rebuild.")
                : TEXT("The DWC Prepared Skeletal Mesh has no render LOD data."));
        return Result;
    }

    int32   FirstLODIndex = 0;
    int32   LastLODIndex = 0;
    FString LODRangeError;
    if (!ResolveGeneratedLODRange(Asset, *RenderData, FirstLODIndex, LastLODIndex, &LODRangeError))
    {
        SetFailure(Result, LODRangeError);
        return Result;
    }

    TArray<int32> PayloadLODIndices;
    if (Options != nullptr && !Options->TargetLODIndices.IsEmpty())
    {
        const int32 LastAvailableLODIndex = RenderData->LODRenderData.Num() - 1;
        for (const int32 RequestedLODIndex : Options->TargetLODIndices)
        {
            if (RequestedLODIndex >= 0 && RequestedLODIndex <= LastAvailableLODIndex)
            {
                PayloadLODIndices.AddUnique(RequestedLODIndex);
            }
        }
        PayloadLODIndices.Sort();
        if (PayloadLODIndices.IsEmpty())
        {
            SetFailure(Result, TEXT("No valid target LOD was supplied for DWC UV generation."));
            return Result;
        }
    }
    else
    {
        PayloadLODIndices.Reserve(LastLODIndex - FirstLODIndex + 2);
        PayloadLODIndices.Add(CanonicalDataUVLODIndex);
        if (Asset.GetSetupSettings().bBuildGPUWetnessMapSimulationData)
        {
            for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
            {
                PayloadLODIndices.AddUnique(LODIndex);
            }
        }
        PayloadLODIndices.Sort();
    }
    Result.TargetLODIndices = PayloadLODIndices;
    Result.WettableMaterialSlotCount = SortedWettableMaterialSlotIndices.Num();

    FDWCPreparedMeshEditTransaction MeshEditTransaction(PreparedMesh);
    FString                         TransactionError;
    for (const int32 LODIndex : PayloadLODIndices)
    {
        if (!MeshEditTransaction.CaptureEditableLOD(LODIndex, &TransactionError))
        {
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, TransactionError);
            return Result;
        }
    }

    if (bRebuildPreparedLODsFromSource)
    {
        // Rebuild starts from pristine Source Mesh topology. The transaction above keeps the
        // previous Prepared Mesh intact on any later failure. No MeshDescription is committed
        // here; the normal apply/commit phase below owns the final render-data rebuild.
        for (const int32 LODIndex : PayloadLODIndices)
        {
            FString RestoreError;
            if (!RestorePreparedLODFromSource(SourceMesh, PreparedMesh, LODIndex, &RestoreError))
            {
                Result.FailureLODIndex = LODIndex;
                SetFailure(
                    Result,
                    RestoreError.IsEmpty()
                        ? FString::Printf(TEXT("Failed to rebuild Prepared Mesh LOD%d from the Source Mesh."), LODIndex)
                        : RestoreError);
                return Result;
            }
        }
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        const USkeletalMesh* UVValidationMesh = bRebuildPreparedLODsFromSource ? SourceMesh : PreparedMesh;
        const int32          PreparedUVCount = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(UVValidationMesh, LODIndex);
        if (PreparedUVCount <= 0)
        {
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh LOD%d must have at least one UV channel."), LODIndex));
            return Result;
        }
        if (Asset.GetOriginalUVChannelIndex() < 0 || Asset.GetOriginalUVChannelIndex() >= PreparedUVCount)
        {
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(TEXT("The configured Original UV channel is unavailable on the DWC Prepared Skeletal Mesh LOD%d."), LODIndex));
            return Result;
        }
    }

    TArray<FDWCDataUVLODMetadata>         DataUVMetadata;
    TArray<FDWCEditorUVTopologyData>      OriginalUVTopologies;
    TMap<int32, FDWCDataUVLODMetadata>    ExistingDataUVMetadataByLOD;
    TMap<int32, FDWCEditorUVTopologyData> ExistingOriginalUVTopologyByLOD;
    if (bMergeWithExistingLayout)
    {
        DataUVMetadata = Asset.GetDataUVMetadata();
        for (const FDWCDataUVLODMetadata& ExistingMetadata : DataUVMetadata)
        {
            if (PayloadLODIndices.Contains(ExistingMetadata.LODIndex))
            {
                ExistingDataUVMetadataByLOD.Add(ExistingMetadata.LODIndex, ExistingMetadata);
            }
        }
        DataUVMetadata.RemoveAll(
            [&PayloadLODIndices](const FDWCDataUVLODMetadata& Metadata)
            {
                return PayloadLODIndices.Contains(Metadata.LODIndex);
            });
#if WITH_EDITORONLY_DATA
        OriginalUVTopologies = Asset.Derived.Inline.OriginalUVTopologies;
        for (const FDWCEditorUVTopologyData& ExistingTopology : OriginalUVTopologies)
        {
            if (PayloadLODIndices.Contains(ExistingTopology.LODIndex) ||
                ExistingTopology.LODIndex == CanonicalDataUVLODIndex)
            {
                ExistingOriginalUVTopologyByLOD.Add(ExistingTopology.LODIndex, ExistingTopology);
            }
        }
#endif
    }
    DataUVMetadata.Reserve(DataUVMetadata.Num() + PayloadLODIndices.Num());
    OriginalUVTopologies.Reserve(FMath::Max(1, OriginalUVTopologies.Num()));

    int32                         ExcludedTriangleCount = 0;
    int32                         Degenerate3DTriangleCount = 0;
    int32                         DegenerateSourceUVTriangleCount = 0;
    int32                         InvalidSourceUVTriangleCount = 0;
    int32                         PackedDegenerateTriangleCount = 0;
    int32                         ExcludedVisibleTriangleCount = 0;
    double                        ExcludedVisible3DSurfaceArea = 0.0;
    double                        ExcludedVisible3DSurfaceRatio = 0.0;
    double                        LargestConnectedExcluded3DSurfaceArea = 0.0;
    double                        LargestConnectedExcluded3DSurfaceRatio = 0.0;
    int32                         SplitOriginalUVIslandCount = 0;
    int32                         SelfOverlapPairCount = 0;
    int32                         BudgetFallbackIslandCount = 0;
    int32                         ChartBoundarySplitVertexInstanceCount = 0;
    TArray<FDWCDataUVSlotWarning> SlotWarnings;
    double                        TriangleReadMilliseconds = 0.0;
    double                        OriginalIslandBuildMilliseconds = 0.0;
    double                        ChartBuildMilliseconds = 0.0;
    double                        SeamSplitMilliseconds = 0.0;
    double                        PackAndValidateMilliseconds = 0.0;
    bool                          bGeneratedWithWarnings = false;
    EDWCDataUVResultSeverity      OverallSeverity = EDWCDataUVResultSeverity::Ready;
    TArray<FDWCDataUVLODWarning>  LODWarnings;
    TArray<int32>                 GeneratedLODIndices;

    int32 DataUVChannelIndex = Asset.GetDWCDataUVChannelIndex();
    if (DataUVChannelIndex == INDEX_NONE ||
        bForceNewAsset ||
        bUsePreferredDataUVChannel ||
        Asset.GetRuntimeSkeletalMesh() != PreparedMesh)
    {
        DataUVChannelIndex = FMath::Clamp(
            Asset.GetSetupSettings().PreferredDWCDataUVChannelIndex,
            0,
            7);
    }
    if (DataUVChannelIndex == Asset.GetOriginalUVChannelIndex())
    {
        SetFailure(Result, TEXT("DWC UV Channel cannot overwrite the configured Original UV Channel."));
        return Result;
    }

    if (bUseSourceMeshForSafetyPreflight &&
        bRebuildPreparedLODsFromSource &&
        SourceSafetyPreflight.DataUVChannelIndex != INDEX_NONE &&
        SourceSafetyPreflight.DataUVChannelIndex != DataUVChannelIndex)
    {
        SetFailure(Result, FString::Printf(
                               TEXT("Source Mesh analysis resolved DWC UV%d, but the Prepared Mesh rebuild resolved UV%d. The build was stopped before applying an inconsistent plan."),
                               SourceSafetyPreflight.DataUVChannelIndex,
                               DataUVChannelIndex));
        return Result;
    }

    UWetClothingAsset::ClearMeshContentSignatureCache();

    const bool bAllowOverwriteExistingChannel =
        bAllowOverwriteExistingDataUVChannel ||
        Asset.GetDWCDataUVChannelIndex() == DataUVChannelIndex;

    const FSkeletalMeshRenderData* CurrentRenderData = bRebuildPreparedLODsFromSource
                                                           ? SourceMesh->GetResourceForRendering()
                                                           : PreparedMesh->GetResourceForRendering();
    if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(CanonicalDataUVLODIndex))
    {
        Result.FailureLODIndex = CanonicalDataUVLODIndex;
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh has no LOD0 render data."));
        return Result;
    }

    if (CurrentRenderData->LODRenderData[CanonicalDataUVLODIndex].GetNumVertices() <= 0)
    {
        Result.FailureLODIndex = CanonicalDataUVLODIndex;
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh LOD0 has no vertices."));
        return Result;
    }

    // Analyze each material slot on an isolated MeshDescription snapshot. The expensive
    // island/overlap/packing work can run concurrently, while each slot still evaluates its
    // LODs in order so Failed / Not Committed / Not Generated remains exact.
    TSet<int32>                                     SuccessfulMaterialSlotIndices;
    TMap<int32, TArray<FDWCDataUVGenerationResult>> SuccessfulResultsByLOD;
    TArray<FString>                                 SlotFailureMessages;
    bool                                            bResolvedDataUVChannel = false;
    const int32                                     SourceUVChannelIndex = Asset.GetOriginalUVChannelIndex();
    TMap<FName, int32>                              MaterialSlotIndexByName;
    const TArray<FSkeletalMaterial>&                PreparedMaterials = PreparedMesh->GetMaterials();
    for (int32 MaterialIndex = 0; MaterialIndex < PreparedMaterials.Num(); ++MaterialIndex)
    {
        const FSkeletalMaterial& Material = PreparedMaterials[MaterialIndex];
        if (!Material.MaterialSlotName.IsNone())
        {
            MaterialSlotIndexByName.Add(Material.MaterialSlotName, MaterialIndex);
        }
        if (!Material.ImportedMaterialSlotName.IsNone())
        {
            MaterialSlotIndexByName.Add(Material.ImportedMaterialSlotName, MaterialIndex);
        }
    }

    struct FSlotPreflightResult
    {
        int32                                   MaterialSlotIndex = INDEX_NONE;
        int32                                   CandidateDataUVChannelIndex = INDEX_NONE;
        int32                                   FailureLODIndex = INDEX_NONE;
        bool                                    bSucceeded = false;
        bool                                    bGeneratedPayload = false;
        bool                                    bRequiresUserConfirmation = false;
        FDWCDataUVValidationFailure             ValidationFailure;
        TArray<FDWCDataUVSlotLODResult>         Outcomes;
        TMap<int32, FDWCDataUVGenerationResult> AnalysisResultsByLOD;
        FString                                 FailureMessage;
    };

    TArray<FMeshDescription> LODMeshDescriptionSnapshots;
    TArray<bool>             LODRenderDataAvailable;
    TArray<bool>             LODHasVertices;
    LODMeshDescriptionSnapshots.Reserve(PayloadLODIndices.Num());
    LODRenderDataAvailable.Reserve(PayloadLODIndices.Num());
    LODHasVertices.Reserve(PayloadLODIndices.Num());
    for (const int32 LODIndex : PayloadLODIndices)
    {
        const bool bHasRenderLOD = CurrentRenderData != nullptr &&
                                   CurrentRenderData->LODRenderData.IsValidIndex(LODIndex);
        LODRenderDataAvailable.Add(bHasRenderLOD);
        LODHasVertices.Add(
            bHasRenderLOD && CurrentRenderData->LODRenderData[LODIndex].GetNumVertices() > 0);

        const FMeshDescription* MeshDescription = PreparedMesh->GetMeshDescription(LODIndex);
        if (MeshDescription == nullptr)
        {
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(
                                   TEXT("LOD%d has no editable MeshDescription for DWC UV analysis."),
                                   LODIndex));
            return Result;
        }
        LODMeshDescriptionSnapshots.Add(*MeshDescription);
    }

    TArray<FSlotPreflightResult> SlotPreflightResults;
    SlotPreflightResults.SetNum(SortedWettableMaterialSlotIndices.Num());
    const bool bCanReuseSourceAnalysisPlans =
        bUseSourceMeshForSafetyPreflight &&
        bRebuildPreparedLODsFromSource &&
        SourceSafetyPreflight.bSucceeded &&
        SourceSafetyPreflight.TargetLODIndices == PayloadLODIndices;

    if (bCanReuseSourceAnalysisPlans)
    {
        // Source preflight is the authoritative analysis for a pristine rebuild.
        // Reusing its GenerationPlan keeps the user's confirmed exclusion and the
        // committed metadata tied to the exact same Source-Mesh analysis.
        for (int32 SlotArrayIndex = 0; SlotArrayIndex < SortedWettableMaterialSlotIndices.Num(); ++SlotArrayIndex)
        {
            FSlotPreflightResult& SlotResult = SlotPreflightResults[SlotArrayIndex];
            SlotResult.MaterialSlotIndex = SortedWettableMaterialSlotIndices[SlotArrayIndex];
            SlotResult.CandidateDataUVChannelIndex = DataUVChannelIndex;
            SlotResult.Outcomes.Reserve(PayloadLODIndices.Num());

            auto AddOutcome = [&SlotResult](
                                  const int32                        LODIndex,
                                  const EDWCDataUVSlotLODResultState State,
                                  const FString&                     Message = FString())
            {
                FDWCDataUVSlotLODResult& Record = SlotResult.Outcomes.AddDefaulted_GetRef();
                Record.MaterialSlotIndex = SlotResult.MaterialSlotIndex;
                Record.LODIndex = LODIndex;
                Record.State = State;
                Record.Message = Message;
            };

            const TMap<int32, FDWCDataUVGenerationResult>* SourceResultsByLOD =
                SourceSafetyPreflight.AnalysisResultsBySlotLOD.Find(SlotResult.MaterialSlotIndex);
            if (SourceResultsByLOD == nullptr)
            {
                SlotResult.bSucceeded = false;
                SlotResult.FailureMessage = TEXT("Source Mesh preflight did not retain an analysis result for this material slot.");
                for (const int32 LODIndex : PayloadLODIndices)
                {
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotGenerated, SlotResult.FailureMessage);
                }
                continue;
            }

            bool bSlotSucceeded = true;
            for (const int32 LODIndex : PayloadLODIndices)
            {
                const FDWCDataUVGenerationResult* SourceUVResult = SourceResultsByLOD->Find(LODIndex);
                if (SourceUVResult == nullptr)
                {
                    bSlotSucceeded = false;
                    SlotResult.FailureLODIndex = LODIndex;
                    SlotResult.FailureMessage = TEXT("Source Mesh preflight did not retain an analysis result for this LOD.");
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                    break;
                }

                FDWCDataUVGenerationResult UVResult = *SourceUVResult;
                SlotResult.AnalysisResultsByLOD.Add(LODIndex, UVResult);
                if (!UVResult.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
                {
                    SlotResult.bRequiresUserConfirmation = true;
                }
                if (!UVResult.bSucceeded)
                {
                    bSlotSucceeded = false;
                    SlotResult.FailureLODIndex = LODIndex;
                    SlotResult.FailureMessage = UVResult.Message;
                    SlotResult.ValidationFailure = UVResult.ValidationFailure;
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, UVResult.Message);
                    break;
                }
                if (UVResult.bTargetSlotNotPresent)
                {
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotPresent, UVResult.Message);
                    continue;
                }
                if (!UVResult.GenerationPlan.IsValid())
                {
                    bSlotSucceeded = false;
                    SlotResult.FailureLODIndex = LODIndex;
                    SlotResult.FailureMessage = TEXT("Source Mesh analysis succeeded without retaining a DWC UV generation plan.");
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                    break;
                }

                if (!SlotResult.bGeneratedPayload)
                {
                    SlotResult.CandidateDataUVChannelIndex = UVResult.UVChannelIndex;
                    SlotResult.bGeneratedPayload = true;
                }
                else if (UVResult.UVChannelIndex != SlotResult.CandidateDataUVChannelIndex)
                {
                    bSlotSucceeded = false;
                    SlotResult.FailureLODIndex = LODIndex;
                    SlotResult.FailureMessage = FString::Printf(
                        TEXT("Source preflight validated UV%d, but the slot requires UV%d."),
                        UVResult.UVChannelIndex,
                        SlotResult.CandidateDataUVChannelIndex);
                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                    break;
                }

                AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Ready);
            }

            if (!bSlotSucceeded)
            {
                for (FDWCDataUVSlotLODResult& Record : SlotResult.Outcomes)
                {
                    if (Record.State == EDWCDataUVSlotLODResultState::Ready)
                    {
                        Record.State = EDWCDataUVSlotLODResultState::NotCommitted;
                        Record.Message = TEXT("Validated successfully, but was not committed because another LOD in this material slot failed.");
                    }
                }
                for (const int32 LODIndex : PayloadLODIndices)
                {
                    if (!SlotResult.Outcomes.ContainsByPredicate(
                            [LODIndex](const FDWCDataUVSlotLODResult& Record)
                            {
                                return Record.LODIndex == LODIndex;
                            }))
                    {
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotGenerated);
                    }
                }
                SlotResult.bSucceeded = false;
                continue;
            }

            SlotResult.bSucceeded = true;
        }
    }
    else
    {
        RunBoundedMaterialSlotTasks(
            SortedWettableMaterialSlotIndices.Num(),
            ResolveMaxParallelMaterialSlots(Options),
            [&](const int32 SlotArrayIndex)
            {
                FSlotPreflightResult& SlotResult = SlotPreflightResults[SlotArrayIndex];
                SlotResult.MaterialSlotIndex = SortedWettableMaterialSlotIndices[SlotArrayIndex];
                SlotResult.CandidateDataUVChannelIndex = DataUVChannelIndex;
                SlotResult.Outcomes.Reserve(PayloadLODIndices.Num());

                auto AddOutcome = [&SlotResult](
                                      const int32                        LODIndex,
                                      const EDWCDataUVSlotLODResultState State,
                                      const FString&                     Message = FString())
                {
                    FDWCDataUVSlotLODResult& Record = SlotResult.Outcomes.AddDefaulted_GetRef();
                    Record.MaterialSlotIndex = SlotResult.MaterialSlotIndex;
                    Record.LODIndex = LODIndex;
                    Record.State = State;
                    Record.Message = Message;
                };

                bool bSlotSucceeded = true;
                for (int32 LODArrayIndex = 0; LODArrayIndex < PayloadLODIndices.Num(); ++LODArrayIndex)
                {
                    const int32 LODIndex = PayloadLODIndices[LODArrayIndex];
                    if (!LODRenderDataAvailable.IsValidIndex(LODArrayIndex) ||
                        !LODRenderDataAvailable[LODArrayIndex])
                    {
                        bSlotSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = TEXT("Render data is unavailable.");
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                        break;
                    }
                    if (!LODHasVertices.IsValidIndex(LODArrayIndex) ||
                        !LODHasVertices[LODArrayIndex])
                    {
                        bSlotSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = TEXT("The LOD has no vertices.");
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                        break;
                    }

                    FMeshDescription WorkingMeshDescription = LODMeshDescriptionSnapshots[LODArrayIndex];
                    const bool       bAllowOverwriteForSlot =
                        bAllowOverwriteExistingChannel ||
                        SlotResult.bGeneratedPayload ||
                        LODIndex != CanonicalDataUVLODIndex;
                    FDWCDataUVGenerationResult UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
                        PreparedMesh,
                        LODIndex,
                        SourceUVChannelIndex,
                        SlotResult.CandidateDataUVChannelIndex,
                        bAllowOverwriteForSlot,
                        SlotResult.MaterialSlotIndex,
                        nullptr,
                        true,
                        &WorkingMeshDescription,
                        true,
                        true,
                        &MaterialSlotIndexByName,
                        ConfirmedVisibleExclusionMaterialSlotIndices != nullptr &&
                            ConfirmedVisibleExclusionMaterialSlotIndices->Contains(SlotResult.MaterialSlotIndex));
                    SlotResult.AnalysisResultsByLOD.Add(LODIndex, UVResult);
                    if (!UVResult.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
                    {
                        SlotResult.bRequiresUserConfirmation = true;
                    }
                    if (!UVResult.bSucceeded)
                    {
                        bSlotSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = UVResult.Message;
                        SlotResult.ValidationFailure = UVResult.ValidationFailure;
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, UVResult.Message);
                        break;
                    }

                    if (UVResult.bTargetSlotNotPresent)
                    {
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotPresent, UVResult.Message);
                        continue;
                    }

                    if (!SlotResult.bGeneratedPayload)
                    {
                        SlotResult.CandidateDataUVChannelIndex = UVResult.UVChannelIndex;
                        SlotResult.bGeneratedPayload = true;
                    }
                    else if (UVResult.UVChannelIndex != SlotResult.CandidateDataUVChannelIndex)
                    {
                        bSlotSucceeded = false;
                        SlotResult.FailureLODIndex = LODIndex;
                        SlotResult.FailureMessage = FString::Printf(
                            TEXT("Validated UV%d, but the slot requires UV%d."),
                            UVResult.UVChannelIndex,
                            SlotResult.CandidateDataUVChannelIndex);
                        AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, SlotResult.FailureMessage);
                        break;
                    }

                    AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::Ready);
                }

                if (!bSlotSucceeded)
                {
                    for (FDWCDataUVSlotLODResult& Record : SlotResult.Outcomes)
                    {
                        if (Record.State == EDWCDataUVSlotLODResultState::Ready)
                        {
                            Record.State = EDWCDataUVSlotLODResultState::NotCommitted;
                            Record.Message = TEXT("Generated successfully, but was not committed because another LOD in this material slot failed.");
                        }
                    }
                    for (const int32 LODIndex : PayloadLODIndices)
                    {
                        if (!SlotResult.Outcomes.ContainsByPredicate(
                                [LODIndex](const FDWCDataUVSlotLODResult& Record)
                                {
                                    return Record.LODIndex == LODIndex;
                                }))
                        {
                            AddOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotGenerated);
                        }
                    }
                    SlotResult.bSucceeded = false;
                    return;
                }

                SlotResult.bSucceeded = true;
            });
    }

    for (FSlotPreflightResult& SlotResult : SlotPreflightResults)
    {
        Result.SlotLODResults.Append(MoveTemp(SlotResult.Outcomes));
        if (SlotResult.bRequiresUserConfirmation)
        {
            Result.ConfirmationRequiredMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
        }
        if (!SlotResult.bSucceeded)
        {
            Result.FailedMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
            if (Result.FailureLODIndex == INDEX_NONE)
            {
                Result.FailureLODIndex = SlotResult.FailureLODIndex;
                Result.ValidationFailure = SlotResult.ValidationFailure;
            }
            SlotFailureMessages.Add(FString::Printf(
                TEXT("Material Slot %d failed at LOD%d: %s"),
                SlotResult.MaterialSlotIndex,
                SlotResult.FailureLODIndex,
                *SlotResult.FailureMessage));
            continue;
        }

        // A slot absent from every mapped LOD is valid but produces no payload.
        if (!SlotResult.bGeneratedPayload)
        {
            continue;
        }

        if (!bResolvedDataUVChannel)
        {
            DataUVChannelIndex = SlotResult.CandidateDataUVChannelIndex;
            bResolvedDataUVChannel = true;
        }
        else if (DataUVChannelIndex != SlotResult.CandidateDataUVChannelIndex)
        {
            Result.FailedMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
            SlotFailureMessages.Add(FString::Printf(
                TEXT("Material Slot %d resolved UV%d, but the batch requires UV%d."),
                SlotResult.MaterialSlotIndex,
                SlotResult.CandidateDataUVChannelIndex,
                DataUVChannelIndex));
            for (FDWCDataUVSlotLODResult& Record : Result.SlotLODResults)
            {
                if (Record.MaterialSlotIndex == SlotResult.MaterialSlotIndex &&
                    Record.State == EDWCDataUVSlotLODResultState::Ready)
                {
                    Record.State = EDWCDataUVSlotLODResultState::NotCommitted;
                    Record.Message = TEXT("The slot resolved a different destination UV channel and was not committed.");
                }
            }
            continue;
        }

        SuccessfulMaterialSlotIndices.Add(SlotResult.MaterialSlotIndex);
    }

    if (!Result.ConfirmationRequiredMaterialSlotIndices.IsEmpty())
    {
        Result.BuildState = EDWCDataUVBuildState::RequiresConfirmation;
        Result.bSucceeded = false;
        Result.bRequiresUserConfirmation = true;
        Result.PreparedMesh = PreparedMesh;
        Result.DataUVChannelIndex = DataUVChannelIndex;
        Result.WettableMaterialSlotCount = SortedWettableMaterialSlotIndices.Num();
        Result.TargetLODIndices = PayloadLODIndices;
        Result.ResultSeverity = EDWCDataUVResultSeverity::ReadyWithWarnings;

        for (const FSlotPreflightResult& SlotResult : SlotPreflightResults)
        {
            if (!Result.ConfirmationRequiredMaterialSlotIndices.Contains(SlotResult.MaterialSlotIndex))
            {
                continue;
            }
            for (const TPair<int32, FDWCDataUVGenerationResult>& Pair : SlotResult.AnalysisResultsByLOD)
            {
                MergeSlotDiagnostics(Result.SlotWarnings, Pair.Value.SlotWarnings);
                Result.ExcludedVisibleTriangleCount += Pair.Value.ExcludedVisibleTriangleCount;
                Result.ExcludedVisible3DSurfaceArea += Pair.Value.ExcludedVisible3DSurfaceArea;
                Result.ExcludedVisible3DSurfaceRatio = FMath::Max(
                    Result.ExcludedVisible3DSurfaceRatio,
                    Pair.Value.ExcludedVisible3DSurfaceRatio);
                Result.LargestConnectedExcluded3DSurfaceArea = FMath::Max(
                    Result.LargestConnectedExcluded3DSurfaceArea,
                    Pair.Value.LargestConnectedExcluded3DSurfaceArea);
                Result.LargestConnectedExcluded3DSurfaceRatio = FMath::Max(
                    Result.LargestConnectedExcluded3DSurfaceRatio,
                    Pair.Value.LargestConnectedExcluded3DSurfaceRatio);
            }
        }
        Result.SlotWarnings.RemoveAll(
            [&Result](const FDWCDataUVSlotWarning& Warning)
            {
                return !Result.ConfirmationRequiredMaterialSlotIndices.Contains(Warning.MaterialSlotIndex);
            });
        Result.SlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            });
        Result.Message = FString::Printf(
            TEXT("Some triangles cannot be included in the DWC UV for one or more material slots. The affected surface exceeds the %.2f%% automatic limit. Choose whether to generate without those areas, skip the slot, or cancel the build."),
            DWCDataUVSafetyLimits::VisibleExclusionRatio * 100.0);
        return Result;
    }

    if (bRequireAllMaterialSlots && !Result.FailedMaterialSlotIndices.IsEmpty())
    {
        Result.ResultSeverity = EDWCDataUVResultSeverity::Failed;
        const FString FailureDetails = SlotFailureMessages.IsEmpty()
                                           ? TEXT("One or more material slots failed to generate DWC UV data.")
                                           : FString::Join(SlotFailureMessages, TEXT("\n"));
        SetFailure(Result, FString::Printf(
                               TEXT("DWC UV generation was not committed because %d material slot(s) failed.\n%s"),
                               Result.FailedMaterialSlotIndices.Num(),
                               *FailureDetails));
#if WITH_EDITORONLY_DATA
        PersistLastSlotLODResults(Asset, Result, bMergeWithExistingLayout);
        RefreshPersistedFailedSlots(Asset);
        Asset.Derived.Inline.LastDataUVGenerationFailure = Result.Message;
        Asset.MarkPackageDirty();
#endif
        return Result;
    }

    if (SuccessfulMaterialSlotIndices.IsEmpty())
    {
        Result.ResultSeverity = EDWCDataUVResultSeverity::Failed;
        const FString FailureDetails = SlotFailureMessages.IsEmpty()
                                           ? TEXT("No material slot produced a usable DWC UV layout.")
                                           : FString::Join(SlotFailureMessages, TEXT("\n"));
        SetFailure(Result, FString::Printf(
                               TEXT("DWC UV generation failed for all %d material slot(s) in the build.\n%s"),
                               SortedWettableMaterialSlotIndices.Num(),
                               *FailureDetails));
#if WITH_EDITORONLY_DATA
        PersistLastSlotLODResults(Asset, Result, bMergeWithExistingLayout);
        RefreshPersistedFailedSlots(Asset);
        Asset.Derived.Inline.LastDataUVGenerationFailure = Result.Message;
        Asset.MarkPackageDirty();
#endif
        return Result;
    }

    // Apply the plans produced by the parallel workers. Only topology/attribute writes
    // remain serial; the expensive island, overlap and packing work is not repeated.
    TSet<int32>   ModifiedLODIndices;
    TArray<int32> SortedSuccessfulMaterialSlotIndices = SuccessfulMaterialSlotIndices.Array();
    SortedSuccessfulMaterialSlotIndices.Sort();
    TMap<int32, FSlotPreflightResult*> SuccessfulPreflightBySlot;
    for (FSlotPreflightResult& SlotResult : SlotPreflightResults)
    {
        if (SuccessfulMaterialSlotIndices.Contains(SlotResult.MaterialSlotIndex))
        {
            SuccessfulPreflightBySlot.Add(SlotResult.MaterialSlotIndex, &SlotResult);
        }
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        // Partial merge builds preserve DWC UVs owned by untouched slots. A full build clears
        // the destination channel once before applying the first successful slot plan.
        bool bClearedDestinationChannel = bMergeWithExistingLayout;
        for (const int32 MaterialSlotIndex : SortedSuccessfulMaterialSlotIndices)
        {
            FSlotPreflightResult* const* SlotResultPtr = SuccessfulPreflightBySlot.Find(MaterialSlotIndex);
            FSlotPreflightResult*        SlotResult = SlotResultPtr != nullptr ? *SlotResultPtr : nullptr;
            FDWCDataUVGenerationResult*  AnalysisResult = SlotResult != nullptr
                                                              ? SlotResult->AnalysisResultsByLOD.Find(LODIndex)
                                                              : nullptr;
            if (AnalysisResult == nullptr)
            {
                continue;
            }

            if (AnalysisResult->GenerationPlan.IsValid())
            {
                const bool                      bClearThisPlan = !bClearedDestinationChannel;
                const FDWCDataUVPlanApplyResult ApplyResult = FDWCDataUVGenerator::ApplyGenerationPlan(
                    PreparedMesh,
                    LODIndex,
                    *AnalysisResult->GenerationPlan,
                    bClearThisPlan,
                    true);
                if (!ApplyResult.bSucceeded)
                {
                    Result.FailureLODIndex = LODIndex;
                    for (FDWCDataUVSlotLODResult& Record : Result.SlotLODResults)
                    {
                        if (SuccessfulMaterialSlotIndices.Contains(Record.MaterialSlotIndex) &&
                            Record.State == EDWCDataUVSlotLODResultState::Ready)
                        {
                            Record.State = Record.MaterialSlotIndex == MaterialSlotIndex && Record.LODIndex == LODIndex
                                               ? EDWCDataUVSlotLODResultState::Failed
                                               : EDWCDataUVSlotLODResultState::NotCommitted;
                            Record.Message = Record.State == EDWCDataUVSlotLODResultState::Failed
                                                 ? ApplyResult.Message
                                                 : TEXT("Validated successfully, but a later UV plan failed before the LOD was committed.");
                        }
                    }
                    for (const int32 SuccessfulSlotIndex : SuccessfulMaterialSlotIndices)
                    {
                        Result.FailedMaterialSlotIndices.Add(SuccessfulSlotIndex);
                    }
                    Result.GeneratedMaterialSlotIndices.Reset();
                    SetFailure(Result, FString::Printf(
                                           TEXT("LOD%d Material Slot %d DWC UV plan application failed: %s"),
                                           LODIndex,
                                           MaterialSlotIndex,
                                           *ApplyResult.Message));
#if WITH_EDITORONLY_DATA
                    PersistLastSlotLODResults(Asset, Result, bMergeWithExistingLayout);
                    RefreshPersistedFailedSlots(Asset);
                    Asset.Derived.Inline.LastDataUVGenerationFailure = Result.Message;
                    Asset.MarkPackageDirty();
#endif
                    return Result;
                }
                bClearedDestinationChannel = true;
                ModifiedLODIndices.Add(LODIndex);
                AnalysisResult->ChartBoundarySplitVertexInstanceCount =
                    ApplyResult.ChartBoundarySplitVertexInstanceCount;
                AnalysisResult->SeamSplitMilliseconds = ApplyResult.SeamSplitMilliseconds;
            }

            SuccessfulResultsByLOD.FindOrAdd(LODIndex).Add(*AnalysisResult);
        }
    }

    // Commit each changed LOD once, then trigger one skeletal-mesh render-data rebuild instead
    // of SlotCount x LODCount commits and PostEditChange calls.
    TArray<int32> SortedModifiedLODIndices = ModifiedLODIndices.Array();
    SortedModifiedLODIndices.Sort();
    for (const int32 LODIndex : SortedModifiedLODIndices)
    {
        PreparedMesh->CommitMeshDescription(LODIndex);
    }
    if (!SortedModifiedLODIndices.IsEmpty())
    {
        PreparedMesh->PostEditChange();
        PreparedMesh->MarkPackageDirty();
        UWetClothingAsset::ClearMeshContentSignatureCache();
    }

    Result.GeneratedMaterialSlotIndices = SuccessfulMaterialSlotIndices;

    const bool bMustBuildOriginalUVTopology =
        OriginalUVTopologies.IsEmpty() || PayloadLODIndices.Contains(CanonicalDataUVLODIndex);
    if (bMustBuildOriginalUVTopology)
    {
        OriginalUVTopologies.RemoveAll(
            [](const FDWCEditorUVTopologyData& Topology)
            {
                return Topology.LODIndex == CanonicalDataUVLODIndex;
            });

        FDWCEditorUVTopologyData GeneratedTopology;
        FString                  TopologyError;
        if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                CanonicalDataUVLODIndex,
                GeneratedTopology,
                &TopologyError,
                &SuccessfulMaterialSlotIndices))
        {
            Result.GeneratedMaterialSlotIndices.Reset();
            Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
            Result.FailureLODIndex = CanonicalDataUVLODIndex;
            SetFailure(Result, FString::Printf(
                                   TEXT("LOD0 Original UV topology failed for the successfully generated slots: %s"),
                                   *TopologyError));
            return Result;
        }

        if (bMergeWithExistingLayout)
        {
            if (const FDWCEditorUVTopologyData* ExistingTopology =
                    ExistingOriginalUVTopologyByLOD.Find(CanonicalDataUVLODIndex))
            {
                FDWCEditorUVTopologyData MergedTopology = *ExistingTopology;
                MergedTopology.Islands.RemoveAll(
                    [&SuccessfulMaterialSlotIndices](const FDWCOriginalUVIslandTopology& Island)
                    {
                        return SuccessfulMaterialSlotIndices.Contains(Island.MaterialSlotIndex);
                    });
                MergedTopology.Islands.Append(MoveTemp(GeneratedTopology.Islands));
                MergedTopology.LODIndex = GeneratedTopology.LODIndex;
                MergedTopology.UVChannelIndex = GeneratedTopology.UVChannelIndex;
                MergedTopology.BuildSignature = MoveTemp(GeneratedTopology.BuildSignature);
                MergedTopology.GeneratorVersion = GeneratedTopology.GeneratorVersion;
                MergedTopology.bIsValid = !MergedTopology.Islands.IsEmpty();
                OriginalUVTopologies.Add(MoveTemp(MergedTopology));
            }
            else
            {
                OriginalUVTopologies.Add(MoveTemp(GeneratedTopology));
            }
        }
        else
        {
            OriginalUVTopologies.Add(MoveTemp(GeneratedTopology));
        }
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        const TArray<FDWCDataUVGenerationResult>* LODResults = SuccessfulResultsByLOD.Find(LODIndex);
        if (LODResults == nullptr || LODResults->IsEmpty())
        {
            Result.GeneratedMaterialSlotIndices.Reset();
            Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(
                                   TEXT("LOD%d did not retain a complete result for every successful material slot."),
                                   LODIndex));
            return Result;
        }

        TSet<int32>                  MergedMaterialSlotIndices = SuccessfulMaterialSlotIndices;
        const FDWCDataUVLODMetadata* ExistingMetadata =
            bMergeWithExistingLayout ? ExistingDataUVMetadataByLOD.Find(LODIndex) : nullptr;
        if (ExistingMetadata != nullptr)
        {
            if (ExistingMetadata->GeneratedMaterialSlotIndices.IsEmpty())
            {
                // Legacy metadata used an empty list to mean every material slot.
                for (int32 MaterialSlotIndex = 0;
                     MaterialSlotIndex < PreparedMesh->GetMaterials().Num();
                     ++MaterialSlotIndex)
                {
                    MergedMaterialSlotIndices.Add(MaterialSlotIndex);
                }
            }
            else
            {
                for (const int32 MaterialSlotIndex : ExistingMetadata->GeneratedMaterialSlotIndices)
                {
                    if (MaterialSlotIndex != INDEX_NONE)
                    {
                        MergedMaterialSlotIndices.Add(MaterialSlotIndex);
                    }
                }
            }
        }

        FDWCDataUVLODMetadata& Metadata = DataUVMetadata.AddDefaulted_GetRef();
        FString                MetadataError;
        if (!FDWCDataUVMetadataBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                LODIndex,
                DataUVChannelIndex,
                Metadata,
                &MetadataError,
                &MergedMaterialSlotIndices))
        {
            DataUVMetadata.RemoveAt(DataUVMetadata.Num() - 1);
            Result.GeneratedMaterialSlotIndices.Reset();
            Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
            Result.FailureLODIndex = LODIndex;

            const bool bMissingGeneratedChannel =
                MetadataError.Contains(TEXT("does not contain DWC UV Channel"));
            const FString SlotFailureMessage = bMissingGeneratedChannel
                                                   ? FString::Printf(
                                                         TEXT("DWC generated UV%d for LOD%d, but the Prepared Mesh rebuild did not retain the generated channel."),
                                                         DataUVChannelIndex,
                                                         LODIndex)
                                                   : FString::Printf(
                                                         TEXT("LOD%d DWC UV final validation failed: %s"),
                                                         LODIndex,
                                                         *MetadataError);

            // Final metadata validation happens after the per-slot generation records were
            // produced. Propagate the failure back into those records so the UI reports the
            // actual failing LOD instead of a generic missing-diagnostic message.
            for (FDWCDataUVSlotLODResult& Record : Result.SlotLODResults)
            {
                if (!SuccessfulMaterialSlotIndices.Contains(Record.MaterialSlotIndex) ||
                    Record.State != EDWCDataUVSlotLODResultState::Ready)
                {
                    continue;
                }

                if (Record.LODIndex < LODIndex)
                {
                    Record.State = EDWCDataUVSlotLODResultState::NotCommitted;
                    Record.Message = TEXT("Generated successfully, but the DWC UV build was rolled back because a later LOD failed final validation.");
                }
                else if (Record.LODIndex == LODIndex)
                {
                    Record.State = EDWCDataUVSlotLODResultState::Failed;
                    Record.Message = SlotFailureMessage;
                }
                else
                {
                    Record.State = EDWCDataUVSlotLODResultState::NotGenerated;
                    Record.Message = TEXT("Not committed because an earlier LOD failed final validation.");
                }
            }

            if (bMissingGeneratedChannel)
            {
                SetFailure(Result, FString::Printf(
                                       TEXT("DWC UV generation failed for LOD%d because the Prepared Mesh rebuild did not retain the generated UV channel. Technical detail: %s"),
                                       LODIndex,
                                       *MetadataError));
            }
            else
            {
                SetFailure(Result, FString::Printf(
                                       TEXT("LOD%d generated invalid DWC UV Channel metadata: %s"),
                                       LODIndex,
                                       *MetadataError));
            }
#if WITH_EDITORONLY_DATA
            PersistLastSlotLODResults(Asset, Result, bMergeWithExistingLayout);
            RefreshPersistedFailedSlots(Asset);
            Asset.Derived.Inline.LastDataUVGenerationFailure = Result.Message;
            Asset.MarkPackageDirty();
#endif
            return Result;
        }

        Metadata.GeneratedMaterialSlotIndices = MergedMaterialSlotIndices.Array();
        Metadata.GeneratedMaterialSlotIndices.Sort();
        TArray<FDWCDataUVSlotWarning> LODSlotWarnings;
        if (ExistingMetadata != nullptr)
        {
            for (const FDWCDataUVSlotWarning& ExistingWarning : ExistingMetadata->SlotWarnings)
            {
                // Rebuilt slots replace their previous diagnostics. Untouched slots retain theirs.
                if (!SuccessfulMaterialSlotIndices.Contains(ExistingWarning.MaterialSlotIndex))
                {
                    LODSlotWarnings.Add(ExistingWarning);
                }
            }
        }
        for (const FDWCDataUVGenerationResult& UVResult : *LODResults)
        {
            MergeSlotDiagnostics(LODSlotWarnings, UVResult.SlotWarnings);
            bGeneratedWithWarnings = bGeneratedWithWarnings || UVResult.HasWarnings();
            OverallSeverity = DWCDataUVResultSeverity::Max(OverallSeverity, UVResult.ResultSeverity);
            MergeSlotDiagnostics(SlotWarnings, UVResult.SlotWarnings);
            Degenerate3DTriangleCount += UVResult.Degenerate3DTriangleCount;
            DegenerateSourceUVTriangleCount += UVResult.DegenerateSourceUVTriangleCount;
            InvalidSourceUVTriangleCount += UVResult.InvalidSourceUVTriangleCount;
            PackedDegenerateTriangleCount += UVResult.PackedDegenerateTriangleCount;
            ExcludedVisibleTriangleCount += UVResult.ExcludedVisibleTriangleCount;
            ExcludedVisible3DSurfaceArea += UVResult.ExcludedVisible3DSurfaceArea;
            ExcludedVisible3DSurfaceRatio = FMath::Max(ExcludedVisible3DSurfaceRatio, UVResult.ExcludedVisible3DSurfaceRatio);
            LargestConnectedExcluded3DSurfaceArea = FMath::Max(
                LargestConnectedExcluded3DSurfaceArea,
                UVResult.LargestConnectedExcluded3DSurfaceArea);
            LargestConnectedExcluded3DSurfaceRatio = FMath::Max(
                LargestConnectedExcluded3DSurfaceRatio,
                UVResult.LargestConnectedExcluded3DSurfaceRatio);
            ExcludedTriangleCount += UVResult.Degenerate3DTriangleCount +
                                     UVResult.DegenerateSourceUVTriangleCount +
                                     UVResult.InvalidSourceUVTriangleCount +
                                     UVResult.PackedDegenerateTriangleCount;
            SplitOriginalUVIslandCount += UVResult.SplitOriginalUVIslandCount;
            SelfOverlapPairCount += UVResult.SelfOverlapPairCount;
            BudgetFallbackIslandCount += GetBudgetFallbackIslandCount(UVResult.SlotWarnings);
            ChartBoundarySplitVertexInstanceCount += UVResult.ChartBoundarySplitVertexInstanceCount;
            TriangleReadMilliseconds += UVResult.TriangleReadMilliseconds;
            OriginalIslandBuildMilliseconds += UVResult.OriginalIslandBuildMilliseconds;
            ChartBuildMilliseconds += UVResult.ChartBuildMilliseconds;
            SeamSplitMilliseconds += UVResult.SeamSplitMilliseconds;
            PackAndValidateMilliseconds += UVResult.PackAndValidateMilliseconds;
        }
        LODSlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
            });
        Metadata.SlotWarnings = MoveTemp(LODSlotWarnings);
        GeneratedLODIndices.Add(LODIndex);
    }

    if (!Result.FailedMaterialSlotIndices.IsEmpty())
    {
        bGeneratedWithWarnings = true;
        OverallSeverity = DWCDataUVResultSeverity::Max(
            OverallSeverity,
            EDWCDataUVResultSeverity::ReadyWithWarnings);
    }

    if (DataUVMetadata.IsEmpty())
    {
        Result.GeneratedMaterialSlotIndices.Reset();
        Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh produced no DWC UV Channel payloads."));
        return Result;
    }

    DataUVMetadata.Sort(
        [](const FDWCDataUVLODMetadata& A, const FDWCDataUVLODMetadata& B)
        {
            return A.LODIndex < B.LODIndex;
        });
    OriginalUVTopologies.Sort(
        [](const FDWCEditorUVTopologyData& A, const FDWCEditorUVTopologyData& B)
        {
            return A.LODIndex < B.LODIndex;
        });

    int32 OriginalUVIslandCount = 0;
    for (const FDWCEditorUVTopologyData& Topology : OriginalUVTopologies)
    {
        OriginalUVIslandCount += Topology.Islands.Num();
    }

    // Every successful material slot has completed all required LODs at this point.
    // Commit the successful slot set while retaining per-slot failures as diagnostics.
    FString    CommitError;
    const bool bCommitSucceeded = bReplacingExistingLayout
                                      ? Asset.ReplaceDataUVLayout(
                                            PreparedMesh,
                                            DataUVChannelIndex,
                                            MoveTemp(DataUVMetadata),
                                            MoveTemp(OriginalUVTopologies),
                                            &CommitError)
                                      : Asset.CommitInitialDataUVLayout(
                                            PreparedMesh,
                                            DataUVChannelIndex,
                                            MoveTemp(DataUVMetadata),
                                            MoveTemp(OriginalUVTopologies),
                                            &CommitError);
    if (!bCommitSucceeded)
    {
        Result.GeneratedMaterialSlotIndices.Reset();
        Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
        SetFailure(Result, CommitError.IsEmpty() ? TEXT("Failed to commit the DWC UV Channel layout.") : CommitError);
        return Result;
    }
    MeshEditTransaction.Commit();

    Result.BuildState = EDWCDataUVBuildState::Ready;
    Result.bSucceeded = true;
    Result.bRequiresUserConfirmation = false;
    Result.PreparedMesh = PreparedMesh;
    Result.DataUVChannelIndex = DataUVChannelIndex;
    Result.WettableMaterialSlotCount = SortedWettableMaterialSlotIndices.Num();
    Result.TargetLODIndices = PayloadLODIndices;
    Result.GeneratedLODIndices = MoveTemp(GeneratedLODIndices);
    Result.GeneratedLODIndices.Sort();
    Result.LODWarnings = MoveTemp(LODWarnings);
    Result.LODWarnings.Sort(
        [](const FDWCDataUVLODWarning& A, const FDWCDataUVLODWarning& B)
        {
            return A.LODIndex < B.LODIndex;
        });
    Result.OriginalUVIslandCount = OriginalUVIslandCount;
    Result.bGeneratedWithWarnings =
        bGeneratedWithWarnings || OverallSeverity != EDWCDataUVResultSeverity::Ready;
    Result.ResultSeverity = OverallSeverity;
    Result.ExcludedTriangleCount = ExcludedTriangleCount;
    Result.Degenerate3DTriangleCount = Degenerate3DTriangleCount;
    Result.DegenerateSourceUVTriangleCount = DegenerateSourceUVTriangleCount;
    Result.InvalidSourceUVTriangleCount = InvalidSourceUVTriangleCount;
    Result.PackedDegenerateTriangleCount = PackedDegenerateTriangleCount;
    Result.ExcludedVisibleTriangleCount = ExcludedVisibleTriangleCount;
    Result.ExcludedVisible3DSurfaceArea = ExcludedVisible3DSurfaceArea;
    Result.ExcludedVisible3DSurfaceRatio = ExcludedVisible3DSurfaceRatio;
    Result.LargestConnectedExcluded3DSurfaceArea = LargestConnectedExcluded3DSurfaceArea;
    Result.LargestConnectedExcluded3DSurfaceRatio = LargestConnectedExcluded3DSurfaceRatio;
    Result.SplitOriginalUVIslandCount = SplitOriginalUVIslandCount;
    Result.SelfOverlapPairCount = SelfOverlapPairCount;
    Result.BudgetFallbackIslandCount = BudgetFallbackIslandCount;
    Result.SlotWarnings = MoveTemp(SlotWarnings);
    Result.SlotWarnings.Sort(
        [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
        {
            return A.MaterialSlotIndex < B.MaterialSlotIndex;
        });
    Result.ChartBoundarySplitVertexInstanceCount = ChartBoundarySplitVertexInstanceCount;

    TArray<int32> SkippedLODIndices;
    SkippedLODIndices.Reserve(Result.LODWarnings.Num());
    for (const FDWCDataUVLODWarning& LODWarning : Result.LODWarnings)
    {
        SkippedLODIndices.Add(LODWarning.LODIndex);
    }

    const FString GeneratedLODText = BuildLODList(Result.GeneratedLODIndices);
    const FString SkippedLODText = BuildLODList(SkippedLODIndices);
    const TCHAR*  Operation = bReplacingExistingLayout ? TEXT("Rebuilt") : TEXT("Generated and sealed");
    Result.Message = FString::Printf(
        TEXT("%s DWC UV Channel %d for %d of %d material slot(s) in the build. Generated %d of %d target LOD(s): %s. Created %d chart-boundary VertexInstance seam(s), with %d LOD0 Original UV island record(s)."),
        Operation,
        DataUVChannelIndex,
        SuccessfulMaterialSlotIndices.Num(),
        SortedWettableMaterialSlotIndices.Num(),
        Result.GeneratedLODIndices.Num(),
        Result.TargetLODIndices.Num(),
        *GeneratedLODText,
        Result.ChartBoundarySplitVertexInstanceCount,
        Result.OriginalUVIslandCount);
    if (!SkippedLODIndices.IsEmpty())
    {
        Result.Message += FString::Printf(TEXT(" Skipped LOD(s): %s."), *SkippedLODText);
    }
    if (!Result.SkippedMaterialSlotIndices.IsEmpty())
    {
        TArray<int32> SkippedSlots = Result.SkippedMaterialSlotIndices.Array();
        SkippedSlots.Sort();
        TArray<FString> SkippedSlotLabels;
        for (const int32 SkippedSlotIndex : SkippedSlots)
        {
            SkippedSlotLabels.Add(FString::Printf(
                TEXT("Slot %d (%s)"),
                SkippedSlotIndex,
                *ResolveMaterialSlotName(PreparedMesh, SkippedSlotIndex)));
        }
        Result.Message += FString::Printf(
            TEXT(" Skipped material slot(s) were left unchanged: %s."),
            *FString::Join(SkippedSlotLabels, TEXT(", ")));
    }
    if (!Result.FailedMaterialSlotIndices.IsEmpty())
    {
        TArray<int32> FailedSlots = Result.FailedMaterialSlotIndices.Array();
        FailedSlots.Sort();
        TArray<FString> FailedSlotLabels;
        for (const int32 FailedSlotIndex : FailedSlots)
        {
            FailedSlotLabels.Add(FString::Printf(TEXT("Slot %d (%s)"), FailedSlotIndex, *ResolveMaterialSlotName(PreparedMesh, FailedSlotIndex)));
        }
        Result.Message += FString::Printf(
            TEXT(" Successful slots were committed. Failed slot(s): %s."),
            *FString::Join(FailedSlotLabels, TEXT(", ")));
        if (!SlotFailureMessages.IsEmpty())
        {
            Result.Message += TEXT("\n\n") + FString::Join(SlotFailureMessages, TEXT("\n"));
        }
    }

    Result.TimingSummary = FString::Printf(
        TEXT("Timing across generated LODs (ms): triangle read %.1f, Original UV islands %.1f, overlap/chart split %.1f, pack/validate %.1f, seam split %.1f."),
        TriangleReadMilliseconds,
        OriginalIslandBuildMilliseconds,
        ChartBuildMilliseconds,
        PackAndValidateMilliseconds,
        SeamSplitMilliseconds);
    Result.Message += TEXT("\n") + Result.TimingSummary;

    if (Result.bGeneratedWithWarnings)
    {
        const bool bHasGenerationDiagnostics =
            Result.ExcludedTriangleCount > 0 ||
            Result.SelfOverlapPairCount > 0 ||
            Result.SplitOriginalUVIslandCount > 0 ||
            Result.BudgetFallbackIslandCount > 0 ||
            !Result.SlotWarnings.IsEmpty() ||
            !Result.LODWarnings.IsEmpty();
        if (bHasGenerationDiagnostics)
        {
            Result.Message += FString::Printf(
                TEXT("\n\nDWC UV Channel was generated with diagnostics. Excluded triangles: %d (3D degenerate: %d, degenerate Source UV: %d, invalid Source UV: %d, packed degenerate: %d). Excluded visible surface ratio: %.6f%%; largest connected excluded region: %.6f%%. Separated overlapping Source UV triangle pairs: %d. Original UV islands split across packing charts: %d. Budget fallback islands: %d."),
                Result.ExcludedTriangleCount,
                Result.Degenerate3DTriangleCount,
                Result.DegenerateSourceUVTriangleCount,
                Result.InvalidSourceUVTriangleCount,
                Result.PackedDegenerateTriangleCount,
                Result.ExcludedVisible3DSurfaceRatio * 100.0,
                Result.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                Result.SelfOverlapPairCount,
                Result.SplitOriginalUVIslandCount,
                Result.BudgetFallbackIslandCount);
        }

        for (const FDWCDataUVLODWarning& LODWarning : Result.LODWarnings)
        {
            const FString LODWarningLogText = FString::Printf(
                TEXT("LOD%d DWC UV Channel warning: %s. %s"),
                LODWarning.LODIndex,
                *LODWarning.Summary,
                *LODWarning.TechnicalDetails);
            Result.Message += TEXT("\n\n") + LODWarningLogText;
            UE_LOG(LogDWC, Warning, TEXT("%s"), *LODWarningLogText);
        }

        for (const FDWCDataUVSlotWarning& SlotWarning : Result.SlotWarnings)
        {
            if (!SlotWarning.HasWarnings())
            {
                continue;
            }

            const FString SlotWarningLogText = BuildSlotWarningLogText(SlotWarning, PreparedMesh);
            Result.Message += TEXT("\n\n") + SlotWarningLogText;
            if (SlotWarning.ResultSeverity == EDWCDataUVResultSeverity::ReadyWithWarnings)
            {
                UE_LOG(LogDWC, Warning, TEXT("%s"), *SlotWarningLogText);
            }
            else
            {
                UE_LOG(LogDWC, Display, TEXT("%s"), *SlotWarningLogText);
            }
        }
    }

#if WITH_EDITORONLY_DATA
    {
        PersistLastSlotLODResults(Asset, Result, bMergeWithExistingLayout);
        RefreshPersistedFailedSlots(Asset);
        if (!Result.FailedMaterialSlotIndices.IsEmpty())
        {
            Asset.Derived.Inline.LastDataUVGenerationFailure = Result.Message;
        }
        else if (Asset.Derived.Inline.FailedDataUVMaterialSlotIndices.IsEmpty())
        {
            Asset.Derived.Inline.LastDataUVGenerationFailure.Reset();
        }
        Asset.MarkPackageDirty();
    }
#endif

    return Result;
}

FDWCDataUVBuildResult FDWCDataUVBuildService::RelocateChannel(
    UWetClothingAsset& Asset,
    const int32        DestinationUVChannelIndex,
    const bool         bAllowOverwriteExistingDataUVChannel)
{
    using namespace DWCDataUVBuildServicePrivate;

    FDWCDataUVBuildResult Result;
    if (!Asset.HasLockedDataUVLayout())
    {
        SetFailure(Result, TEXT("The WCA has no sealed DWC UV Channel layout to relocate."));
        return Result;
    }

    USkeletalMesh* PreparedMesh = Asset.GetRuntimeSkeletalMesh();
    if (PreparedMesh == nullptr)
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh is unavailable."));
        return Result;
    }

    const int32 SourceUVChannelIndex = Asset.GetDWCDataUVChannelIndex();
    const int32 SafeDestinationUVChannelIndex = FMath::Clamp(DestinationUVChannelIndex, 0, 7);
    if (SafeDestinationUVChannelIndex == Asset.GetOriginalUVChannelIndex())
    {
        SetFailure(Result, TEXT("DWC UV Channel cannot be relocated onto the locked Original UV channel."));
        return Result;
    }
    if (SourceUVChannelIndex == SafeDestinationUVChannelIndex)
    {
        Result.BuildState = EDWCDataUVBuildState::Ready;
        Result.bSucceeded = true;
        Result.PreparedMesh = PreparedMesh;
        Result.Message = FString::Printf(TEXT("DWC UV Channel already uses UV%d. The sealed layout was not changed."), SourceUVChannelIndex);
        return Result;
    }

    const TArray<FDWCDataUVLODMetadata>& ExistingMetadata = Asset.GetDataUVMetadata();
    if (ExistingMetadata.IsEmpty())
    {
        SetFailure(Result, TEXT("The WCA has no DWC UV Channel metadata to relocate."));
        return Result;
    }

    FDWCPreparedMeshEditTransaction MeshEditTransaction(PreparedMesh);
    FString                         TransactionError;
    for (const FDWCDataUVLODMetadata& LODMetadata : ExistingMetadata)
    {
        if (!MeshEditTransaction.CaptureEditableLOD(LODMetadata.LODIndex, &TransactionError))
        {
            SetFailure(Result, TransactionError);
            return Result;
        }
    }

    PreparedMesh->Modify();
    for (const FDWCDataUVLODMetadata& LODMetadata : ExistingMetadata)
    {
        FMeshDescription* MeshDescription = PreparedMesh->GetMeshDescription(LODMetadata.LODIndex);
        if (MeshDescription == nullptr)
        {
            SetFailure(Result, FString::Printf(TEXT("LOD%d does not expose editable mesh data."), LODMetadata.LODIndex));
            return Result;
        }

        FSkeletalMeshAttributes Attributes(*MeshDescription);
        Attributes.Register(true);
        auto        VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
        const int32 ExistingUVChannelCount = VertexInstanceUVs.GetNumChannels();
        if (SourceUVChannelIndex < 0 || SourceUVChannelIndex >= ExistingUVChannelCount)
        {
            SetFailure(Result, FString::Printf(
                                   TEXT("LOD%d does not contain the sealed DWC UV Channel source channel UV%d."),
                                   LODMetadata.LODIndex,
                                   SourceUVChannelIndex));
            return Result;
        }
        if (SafeDestinationUVChannelIndex < ExistingUVChannelCount && !bAllowOverwriteExistingDataUVChannel)
        {
            SetFailure(Result, FString::Printf(
                                   TEXT("LOD%d UV%d already exists. Confirm overwrite in Asset Setup before relocating DWC UV Channel."),
                                   LODMetadata.LODIndex,
                                   SafeDestinationUVChannelIndex));
            return Result;
        }
        if (SafeDestinationUVChannelIndex >= ExistingUVChannelCount)
        {
            VertexInstanceUVs.SetNumChannels(SafeDestinationUVChannelIndex + 1);
        }

        for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
        {
            VertexInstanceUVs.Set(
                VertexInstanceID,
                SafeDestinationUVChannelIndex,
                VertexInstanceUVs.Get(VertexInstanceID, SourceUVChannelIndex));
        }

        PreparedMesh->CommitMeshDescription(LODMetadata.LODIndex);
    }

    PreparedMesh->PostEditChange();
    PreparedMesh->MarkPackageDirty();
    UWetClothingAsset::ClearMeshContentSignatureCache();

    FString CommitError;
    if (!Asset.CommitDataUVChannelRelocation(SafeDestinationUVChannelIndex, &CommitError))
    {
        SetFailure(Result, CommitError.IsEmpty() ? TEXT("Failed to commit the DWC UV Channel relocation.") : CommitError);
        return Result;
    }

    MeshEditTransaction.Commit();
    FWCAGeneratedDataInvalidator::InvalidateAsset(Asset);
    FWCAUVIslandViewCache::InvalidateMesh(PreparedMesh);

    Result.BuildState = EDWCDataUVBuildState::Ready;
    Result.bSucceeded = true;
    Result.PreparedMesh = PreparedMesh;
    Result.DataUVChannelIndex = SafeDestinationUVChannelIndex;
    Result.OriginalUVIslandCount = Asset.FindOriginalUVTopologyForLOD(Asset.GetSimulationLODIndex()) != nullptr
                                       ? Asset.FindOriginalUVTopologyForLOD(Asset.GetSimulationLODIndex())->Islands.Num()
                                       : 0;
    Result.Message = FString::Printf(
        TEXT("Relocated the sealed DWC UV Channel layout from UV%d to UV%d without rebuilding packed charts or Original UV island topology. The previous channel remains unchanged but is no longer referenced by this WCA."),
        SourceUVChannelIndex,
        SafeDestinationUVChannelIndex);
    return Result;
}
