#include "DWCDataUVBuildService.h"

#include "DWCDataUVGenerator.h"
#include "DWCDataUVMetadataBuilder.h"
#include "DWCOriginalUVTopologyBuilder.h"
#include "DWCPreparedMeshResolver.h"
#include "DWCPreparedMeshEditTransaction.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "MeshDescription.h"
#include "SkeletalMeshAttributes.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "Utility/DWCLog.h"

namespace DWCDataUVBuildServicePrivate
{
    static constexpr int32 CanonicalDataUVLODIndex = 0;

    void SetFailure(FDWCDataUVBuildResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    void PersistLastSlotLODResults(
        UWetClothingAsset& Asset,
        const FDWCDataUVBuildResult& Result,
        const bool bMergeWithExisting)
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
        const int32 MaterialSlotIndex)
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

    void MergeSlotWarnings(
        TArray<FDWCDataUVSlotWarning>& OutWarnings,
        const TArray<FDWCDataUVSlotWarning>& InWarnings)
    {
        for (const FDWCDataUVSlotWarning& InWarning : InWarnings)
        {
            if (!InWarning.HasWarnings())
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
        const USkeletalMesh* SkeletalMesh)
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
            Lines.Add(FString::Printf(
                TEXT("- Excluded visible 3D surface: %.6f%% (largest connected region %.6f%%)"),
                Warning.ExcludedVisible3DSurfaceRatio * 100.0,
                Warning.LargestConnectedExcluded3DSurfaceRatio * 100.0));
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
            Warning.ResultSeverity == EDWCDataUVResultSeverity::ReadyWithWarnings
                ? TEXT("Ready with warnings")
                : Warning.ResultSeverity == EDWCDataUVResultSeverity::ReadyWithNotes
                    ? TEXT("Ready with notes")
                    : Warning.ResultSeverity == EDWCDataUVResultSeverity::Failed
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
        int32 RangeStart = SortedLODIndices[0];
        int32 RangeEnd = RangeStart;
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
        const UWetClothingAsset& Asset,
        const FSkeletalMeshRenderData& RenderData,
        int32& OutFirstLODIndex,
        int32& OutLastLODIndex,
        FString* OutErrorMessage)
    {
        if (RenderData.LODRenderData.IsEmpty())
        {
            if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh has no render LOD data.");
            return false;
        }

        const int32 LastAvailableLODIndex = RenderData.LODRenderData.Num() - 1;
        OutFirstLODIndex = FMath::Clamp(Asset.GetSetupSettings().FirstGeneratedLODIndex, 0, LastAvailableLODIndex);
        OutLastLODIndex = FMath::Clamp(Asset.GetSetupSettings().LastGeneratedLODIndex, OutFirstLODIndex, LastAvailableLODIndex);
        if (OutErrorMessage) OutErrorMessage->Reset();
        return true;
    }
}

FDWCDataUVBuildResult FDWCDataUVBuildService::Generate(
    UWetClothingAsset& Asset,
    const bool bForceNewAsset,
    const bool bAllowOverwriteExistingDataUVChannel,
    const bool bUsePreferredDataUVChannel,
    const FDWCDataUVBuildOptions* Options)
{
    using namespace DWCDataUVBuildServicePrivate;

    FDWCDataUVBuildResult Result;
    USkeletalMesh* TouchedMesh = Asset.GetRuntimeSkeletalMesh();
    const bool bReplacingExistingLayout = Asset.HasLockedDataUVLayout();
    const bool bMergeWithExistingLayout =
        Options != nullptr && Options->bMergeWithExistingLayout && bReplacingExistingLayout;
    const bool bRequireAllMaterialSlots =
        Options != nullptr && Options->bRequireAllMaterialSlots;

    // DWC UV Channel generation is an invalidation boundary. Complete build failure leaves the
    // previously committed payload intact; per-slot failures are isolated and successful slots commit.
    FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(Asset, TouchedMesh);
    ON_SCOPE_EXIT
    {
        FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(Asset, TouchedMesh);
    };

    TSet<int32> WettableMaterialSlotIndices;
    for (const FWetClothingAuthoredMaterialSlot& Slot : Asset.Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        if (Slot.bIsWettableSlot && Slot.MaterialSlotIndex != INDEX_NONE)
        {
            WettableMaterialSlotIndices.Add(Slot.MaterialSlotIndex);
        }
    }
    if (WettableMaterialSlotIndices.IsEmpty())
    {
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

    const FDWCPreparedMeshResolveResult ResolveResult = FDWCPreparedMeshResolver::Resolve(
        Asset,
        bForceNewAsset);
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

    const FSkeletalMeshRenderData* RenderData = PreparedMesh->GetResourceForRendering();
    if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh has no render LOD data."));
        return Result;
    }

    int32 FirstLODIndex = 0;
    int32 LastLODIndex = 0;
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
    FString TransactionError;
    for (const int32 LODIndex : PayloadLODIndices)
    {
        if (!MeshEditTransaction.CaptureEditableLOD(LODIndex, &TransactionError))
        {
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, TransactionError);
            return Result;
        }
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        const int32 PreparedUVCount = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(PreparedMesh, LODIndex);
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

    TArray<FDWCDataUVLODMetadata> DataUVMetadata;
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;
    if (bMergeWithExistingLayout)
    {
        DataUVMetadata = Asset.GetDataUVMetadata();
        DataUVMetadata.RemoveAll(
            [&PayloadLODIndices](const FDWCDataUVLODMetadata& Metadata)
            {
                return PayloadLODIndices.Contains(Metadata.LODIndex);
            });
#if WITH_EDITORONLY_DATA
        OriginalUVTopologies = Asset.Derived.Inline.OriginalUVTopologies;
#endif
    }
    DataUVMetadata.Reserve(DataUVMetadata.Num() + PayloadLODIndices.Num());
    OriginalUVTopologies.Reserve(FMath::Max(1, OriginalUVTopologies.Num()));

    int32 ExcludedTriangleCount = 0;
    int32 Degenerate3DTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
    int32 PackedDegenerateTriangleCount = 0;
    int32 ExcludedVisibleTriangleCount = 0;
    double ExcludedVisible3DSurfaceArea = 0.0;
    double ExcludedVisible3DSurfaceRatio = 0.0;
    double LargestConnectedExcluded3DSurfaceArea = 0.0;
    double LargestConnectedExcluded3DSurfaceRatio = 0.0;
    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 BudgetFallbackIslandCount = 0;
    int32 ChartBoundarySplitVertexInstanceCount = 0;
    TArray<FDWCDataUVSlotWarning> SlotWarnings;
    double TriangleReadMilliseconds = 0.0;
    double OriginalIslandBuildMilliseconds = 0.0;
    double ChartBuildMilliseconds = 0.0;
    double SeamSplitMilliseconds = 0.0;
    double PackAndValidateMilliseconds = 0.0;
    bool bGeneratedWithWarnings = false;
    EDWCDataUVResultSeverity OverallSeverity = EDWCDataUVResultSeverity::Ready;
    TArray<FDWCDataUVLODWarning> LODWarnings;
    TArray<int32> GeneratedLODIndices;

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

    UWetClothingAsset::ClearMeshContentSignatureCache();

    const bool bAllowOverwriteExistingChannel =
        bAllowOverwriteExistingDataUVChannel ||
        Asset.GetDWCDataUVChannelIndex() == DataUVChannelIndex;

    const FSkeletalMeshRenderData* CurrentRenderData = PreparedMesh->GetResourceForRendering();
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

    // Material slots own independent 0-1 DWC UV layouts. Build each slot as its own
    // all-LOD transaction so a failure in one slot cannot roll back other successful slots.
    TSet<int32> SuccessfulMaterialSlotIndices;
    TMap<int32, TArray<FDWCDataUVGenerationResult>> SuccessfulResultsByLOD;
    TArray<FString> SlotFailureMessages;
    bool bResolvedDataUVChannel = false;

    auto AddSlotLODResult = [&Result](
        const int32 MaterialSlotIndex,
        const int32 LODIndex,
        const EDWCDataUVSlotLODResultState State,
        const FString& Message = FString())
    {
        FDWCDataUVSlotLODResult& Record = Result.SlotLODResults.AddDefaulted_GetRef();
        Record.MaterialSlotIndex = MaterialSlotIndex;
        Record.LODIndex = LODIndex;
        Record.State = State;
        Record.Message = Message;
    };

    for (const int32 MaterialSlotIndex : SortedWettableMaterialSlotIndices)
    {
        FDWCPreparedMeshEditTransaction SlotEditTransaction(PreparedMesh);
        bool bCapturedSlotTransaction = true;
        int32 CaptureFailureLOD = INDEX_NONE;
        FString CaptureFailureMessage;
        for (const int32 LODIndex : PayloadLODIndices)
        {
            FString SlotTransactionError;
            if (!SlotEditTransaction.CaptureEditableLOD(LODIndex, &SlotTransactionError))
            {
                bCapturedSlotTransaction = false;
                CaptureFailureLOD = LODIndex;
                CaptureFailureMessage = SlotTransactionError;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
                if (Result.FailureLODIndex == INDEX_NONE)
                {
                    Result.FailureLODIndex = LODIndex;
                }
                SlotFailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed at LOD%d: %s"),
                    MaterialSlotIndex,
                    LODIndex,
                    *SlotTransactionError));
                break;
            }
        }
        if (!bCapturedSlotTransaction)
        {
            for (const int32 LODIndex : PayloadLODIndices)
            {
                AddSlotLODResult(
                    MaterialSlotIndex,
                    LODIndex,
                    LODIndex == CaptureFailureLOD
                        ? EDWCDataUVSlotLODResultState::Failed
                        : EDWCDataUVSlotLODResultState::NotGenerated,
                    LODIndex == CaptureFailureLOD ? CaptureFailureMessage : FString());
            }
            SlotEditTransaction.Rollback();
            UWetClothingAsset::ClearMeshContentSignatureCache();
            continue;
        }

        bool bSlotSucceeded = true;
        bool bSlotGeneratedPayload = false;
        int32 CandidateDataUVChannelIndex = DataUVChannelIndex;
        TArray<FDWCDataUVGenerationResult> SlotLODGenerationResults;
        SlotLODGenerationResults.Reserve(PayloadLODIndices.Num());
        TArray<FDWCDataUVSlotLODResult> SlotOutcomeRecords;
        SlotOutcomeRecords.Reserve(PayloadLODIndices.Num());

        auto AddLocalOutcome = [&SlotOutcomeRecords, MaterialSlotIndex](
            const int32 LODIndex,
            const EDWCDataUVSlotLODResultState State,
            const FString& Message = FString())
        {
            FDWCDataUVSlotLODResult& Record = SlotOutcomeRecords.AddDefaulted_GetRef();
            Record.MaterialSlotIndex = MaterialSlotIndex;
            Record.LODIndex = LODIndex;
            Record.State = State;
            Record.Message = Message;
        };

        for (const int32 LODIndex : PayloadLODIndices)
        {
            CurrentRenderData = PreparedMesh->GetResourceForRendering();
            if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(LODIndex))
            {
                bSlotSucceeded = false;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
                if (Result.FailureLODIndex == INDEX_NONE)
                {
                    Result.FailureLODIndex = LODIndex;
                }
                const FString FailureMessage = TEXT("Render data is unavailable.");
                AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, FailureMessage);
                SlotFailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed at LOD%d: %s"),
                    MaterialSlotIndex,
                    LODIndex,
                    *FailureMessage));
                break;
            }

            if (CurrentRenderData->LODRenderData[LODIndex].GetNumVertices() <= 0)
            {
                bSlotSucceeded = false;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
                if (Result.FailureLODIndex == INDEX_NONE)
                {
                    Result.FailureLODIndex = LODIndex;
                }
                const FString FailureMessage = TEXT("The LOD has no vertices.");
                AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, FailureMessage);
                SlotFailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed at LOD%d: %s"),
                    MaterialSlotIndex,
                    LODIndex,
                    *FailureMessage));
                break;
            }

            const bool bAllowOverwriteForSlot =
                bAllowOverwriteExistingChannel ||
                bResolvedDataUVChannel ||
                LODIndex != CanonicalDataUVLODIndex;
            FDWCDataUVGenerationResult UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
                PreparedMesh,
                LODIndex,
                Asset.GetOriginalUVChannelIndex(),
                CandidateDataUVChannelIndex,
                bAllowOverwriteForSlot,
                MaterialSlotIndex,
                nullptr);
            if (!UVResult.bSucceeded)
            {
                bSlotSucceeded = false;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
                if (Result.FailureLODIndex == INDEX_NONE)
                {
                    Result.FailureLODIndex = LODIndex;
                    Result.ValidationFailure = UVResult.ValidationFailure;
                }
                AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, UVResult.Message);
                SlotFailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed at LOD%d: %s"),
                    MaterialSlotIndex,
                    LODIndex,
                    *UVResult.Message));
                break;
            }

            if (UVResult.bTargetSlotNotPresent)
            {
                AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotPresent, UVResult.Message);
                SlotLODGenerationResults.Add(MoveTemp(UVResult));
                continue;
            }

            if (!bSlotGeneratedPayload)
            {
                CandidateDataUVChannelIndex = UVResult.UVChannelIndex;
                bSlotGeneratedPayload = true;
            }
            else if (UVResult.UVChannelIndex != CandidateDataUVChannelIndex)
            {
                bSlotSucceeded = false;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
                if (Result.FailureLODIndex == INDEX_NONE)
                {
                    Result.FailureLODIndex = LODIndex;
                }
                const FString FailureMessage = FString::Printf(
                    TEXT("Generated UV%d, but the asset requires UV%d."),
                    UVResult.UVChannelIndex,
                    CandidateDataUVChannelIndex);
                AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::Failed, FailureMessage);
                SlotFailureMessages.Add(FString::Printf(
                    TEXT("Material Slot %d failed at LOD%d: %s"),
                    MaterialSlotIndex,
                    LODIndex,
                    *FailureMessage));
                break;
            }

            AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::Ready);
            UWetClothingAsset::ClearMeshContentSignatureCache();
            SlotLODGenerationResults.Add(MoveTemp(UVResult));
        }

        if (!bSlotSucceeded || SlotLODGenerationResults.Num() != PayloadLODIndices.Num())
        {
            // Generated LODs in this slot were provisional. A later failure rolls those
            // changes back, while LODs that were genuinely absent remain Not Present.
            for (FDWCDataUVSlotLODResult& Record : SlotOutcomeRecords)
            {
                if (Record.State == EDWCDataUVSlotLODResultState::Ready)
                {
                    Record.State = EDWCDataUVSlotLODResultState::NotCommitted;
                    Record.Message = TEXT("Generated successfully, but was not committed because another LOD in this material slot failed.");
                }
            }
            for (const int32 LODIndex : PayloadLODIndices)
            {
                if (!SlotOutcomeRecords.ContainsByPredicate(
                        [LODIndex](const FDWCDataUVSlotLODResult& Record)
                        {
                            return Record.LODIndex == LODIndex;
                        }))
                {
                    AddLocalOutcome(LODIndex, EDWCDataUVSlotLODResultState::NotGenerated);
                }
            }
            Result.SlotLODResults.Append(MoveTemp(SlotOutcomeRecords));
            SlotEditTransaction.Rollback();
            UWetClothingAsset::ClearMeshContentSignatureCache();
            continue;
        }

        SlotEditTransaction.Commit();
        Result.SlotLODResults.Append(MoveTemp(SlotOutcomeRecords));

        // A slot that is absent from every mapped LOD needs no DWC payload and is not a failure.
        if (!bSlotGeneratedPayload)
        {
            continue;
        }

        if (!bResolvedDataUVChannel)
        {
            DataUVChannelIndex = CandidateDataUVChannelIndex;
            bResolvedDataUVChannel = true;
        }
        SuccessfulMaterialSlotIndices.Add(MaterialSlotIndex);
        for (int32 ResultIndex = 0; ResultIndex < SlotLODGenerationResults.Num(); ++ResultIndex)
        {
            SuccessfulResultsByLOD.FindOrAdd(PayloadLODIndices[ResultIndex]).Add(MoveTemp(SlotLODGenerationResults[ResultIndex]));
        }
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

    TArray<int32> SortedSuccessfulMaterialSlotIndices = SuccessfulMaterialSlotIndices.Array();
    SortedSuccessfulMaterialSlotIndices.Sort();
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

        FDWCEditorUVTopologyData OriginalUVTopology;
        FString TopologyError;
        if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                CanonicalDataUVLODIndex,
                OriginalUVTopology,
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
        OriginalUVTopologies.Add(MoveTemp(OriginalUVTopology));
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        const TArray<FDWCDataUVGenerationResult>* LODResults = SuccessfulResultsByLOD.Find(LODIndex);
        if (LODResults == nullptr || LODResults->Num() != SuccessfulMaterialSlotIndices.Num())
        {
            Result.GeneratedMaterialSlotIndices.Reset();
            Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d did not retain a complete result for every successful material slot."),
                LODIndex));
            return Result;
        }

        FDWCDataUVLODMetadata& Metadata = DataUVMetadata.AddDefaulted_GetRef();
        FString MetadataError;
        if (!FDWCDataUVMetadataBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                LODIndex,
                DataUVChannelIndex,
                Metadata,
                &MetadataError,
                &SuccessfulMaterialSlotIndices))
        {
            DataUVMetadata.RemoveAt(DataUVMetadata.Num() - 1);
            Result.GeneratedMaterialSlotIndices.Reset();
            Result.FailedMaterialSlotIndices = WettableMaterialSlotIndices;
            Result.FailureLODIndex = LODIndex;
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d generated invalid DWC UV Channel metadata: %s"),
                LODIndex,
                *MetadataError));
            return Result;
        }

        Metadata.GeneratedMaterialSlotIndices = SortedSuccessfulMaterialSlotIndices;
        TArray<FDWCDataUVSlotWarning> LODSlotWarnings;
        for (const FDWCDataUVGenerationResult& UVResult : *LODResults)
        {
            MergeSlotWarnings(LODSlotWarnings, UVResult.SlotWarnings);
            bGeneratedWithWarnings = bGeneratedWithWarnings || UVResult.HasWarnings();
            OverallSeverity = DWCDataUVResultSeverity::Max(OverallSeverity, UVResult.ResultSeverity);
            MergeSlotWarnings(SlotWarnings, UVResult.SlotWarnings);
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
    FString CommitError;
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

    Result.bSucceeded = true;
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
    const TCHAR* Operation = bReplacingExistingLayout ? TEXT("Rebuilt") : TEXT("Generated and sealed");
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
    const int32 DestinationUVChannelIndex,
    const bool bAllowOverwriteExistingDataUVChannel)
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
    FString TransactionError;
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
        auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
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

