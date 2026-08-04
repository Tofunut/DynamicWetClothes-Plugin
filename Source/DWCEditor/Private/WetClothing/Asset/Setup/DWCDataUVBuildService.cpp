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
            OutWarning.DegenerateSourceUVTriangleCount += InWarning.DegenerateSourceUVTriangleCount;
            OutWarning.InvalidSourceUVTriangleCount += InWarning.InvalidSourceUVTriangleCount;
            OutWarning.SplitOriginalUVIslandCount += InWarning.SplitOriginalUVIslandCount;
            OutWarning.SelfOverlapPairCount += InWarning.SelfOverlapPairCount;
            OutWarning.BudgetFallbackIslandCount += InWarning.BudgetFallbackIslandCount;
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
            TEXT("[DWC UV Channel Warning] Slot %d (%s)"),
            Warning.MaterialSlotIndex,
            *ResolveMaterialSlotName(SkeletalMesh, Warning.MaterialSlotIndex)));
        if (Warning.DegenerateSourceUVTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Degenerate Source UV triangles excluded: %d"),
                Warning.DegenerateSourceUVTriangleCount));
        }
        if (Warning.InvalidSourceUVTriangleCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Invalid Source UV triangles excluded: %d"),
                Warning.InvalidSourceUVTriangleCount));
        }
        if (Warning.SplitOriginalUVIslandCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Self-overlapping Original UV islands: %d"),
                Warning.SplitOriginalUVIslandCount));
            Lines.Add(FString::Printf(
                TEXT("- Overlapping triangle pairs: %d"),
                Warning.SelfOverlapPairCount));
        }
        if (Warning.BudgetFallbackIslandCount > 0)
        {
            Lines.Add(FString::Printf(
                TEXT("- Overlap analysis safety-budget fallbacks: %d"),
                Warning.BudgetFallbackIslandCount));
        }

        TArray<FString> Results;
        if (Warning.DegenerateSourceUVTriangleCount > 0 || Warning.InvalidSourceUVTriangleCount > 0)
        {
            Results.Add(TEXT("problem triangles were excluded before chart generation"));
        }
        if (Warning.SplitOriginalUVIslandCount > 0)
        {
            Results.Add(TEXT("self-overlapping islands were automatically split into non-overlapping charts"));
        }
        if (Warning.BudgetFallbackIslandCount > 0)
        {
            Results.Add(TEXT("budget fallback islands were conservatively split into individual triangle charts"));
        }
        Lines.Add(FString::Printf(TEXT("- Result: %s"), *FString::Join(Results, TEXT("; "))));
        Lines.Add(TEXT("- Usability: Generated DWC UV Channel remains usable; review the source UVs if this was unexpected."));
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
    const bool bUsePreferredDataUVChannel)
{
    using namespace DWCDataUVBuildServicePrivate;

    FDWCDataUVBuildResult Result;
    USkeletalMesh* TouchedMesh = Asset.GetRuntimeSkeletalMesh();
    const bool bReplacingExistingLayout = Asset.HasLockedDataUVLayout();

    // DWC UV Channel generation is an invalidation boundary. Validation failure, user cancellation, and
    // partial generation failure leave the previously committed island/layout payload intact.
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
    PayloadLODIndices.Reserve(LastLODIndex - FirstLODIndex + 2);
    PayloadLODIndices.Add(CanonicalDataUVLODIndex);
    for (int32 LODIndex = FirstLODIndex; LODIndex <= LastLODIndex; ++LODIndex)
    {
        PayloadLODIndices.AddUnique(LODIndex);
    }
    PayloadLODIndices.Sort();
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
    DataUVMetadata.Reserve(PayloadLODIndices.Num());
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;
    OriginalUVTopologies.Reserve(1);

    int32 ExcludedTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
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
    TArray<FDWCDataUVLODWarning> LODWarnings;
    TArray<int32> GeneratedLODIndices;
    auto AddNonCanonicalLODWarning = [&LODWarnings, &bGeneratedWithWarnings](
        const int32 LODIndex,
        const FString& Summary,
        const FString& TechnicalDetails)
    {
        bGeneratedWithWarnings = true;
        FDWCDataUVLODWarning& Warning = LODWarnings.AddDefaulted_GetRef();
        Warning.LODIndex = LODIndex;
        Warning.Summary = Summary;
        Warning.TechnicalDetails = TechnicalDetails;
    };

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

    FDWCDataUVGenerationResult CanonicalUVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
        PreparedMesh,
        CanonicalDataUVLODIndex,
        Asset.GetOriginalUVChannelIndex(),
        DataUVChannelIndex,
        bAllowOverwriteExistingChannel,
        INDEX_NONE,
        &WettableMaterialSlotIndices);
    if (!CanonicalUVResult.bSucceeded)
    {
        Result.FailedMaterialSlotIndices = CanonicalUVResult.FailedMaterialSlotIndices;
        Result.FailureLODIndex = CanonicalDataUVLODIndex;
        Result.ValidationFailure = CanonicalUVResult.ValidationFailure;
        SetFailure(Result, FString::Printf(
            TEXT("LOD0 DWC UV Channel generation failed: %s"),
            *CanonicalUVResult.Message));
        return Result;
    }

    DataUVChannelIndex = CanonicalUVResult.UVChannelIndex;
    UWetClothingAsset::ClearMeshContentSignatureCache();

    FDWCEditorUVTopologyData OriginalUVTopology;
    FString TopologyError;
    if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
            Asset,
            PreparedMesh,
            CanonicalDataUVLODIndex,
            OriginalUVTopology,
            &TopologyError,
            &WettableMaterialSlotIndices))
    {
        Result.FailureLODIndex = CanonicalDataUVLODIndex;
        SetFailure(Result, FString::Printf(
            TEXT("LOD0 Original UV topology failed: %s"),
            *TopologyError));
        return Result;
    }
    OriginalUVTopologies.Add(MoveTemp(OriginalUVTopology));

    for (const int32 LODIndex : PayloadLODIndices)
    {
        CurrentRenderData = PreparedMesh->GetResourceForRendering();
        if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            if (LODIndex != CanonicalDataUVLODIndex)
            {
                AddNonCanonicalLODWarning(
                    LODIndex,
                    TEXT("Render data unavailable"),
                    TEXT("Render data is unavailable, so this non-LOD0 DWC UV Channel data was skipped. LOD0 remains generated."));
                continue;
            }
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh has no LOD%d render data."), LODIndex));
            return Result;
        }

        if (CurrentRenderData->LODRenderData[LODIndex].GetNumVertices() <= 0)
        {
            if (LODIndex != CanonicalDataUVLODIndex)
            {
                AddNonCanonicalLODWarning(
                    LODIndex,
                    TEXT("No render vertices"),
                    TEXT("Render data has no vertices, so this non-LOD0 DWC UV Channel data was skipped. LOD0 remains generated."));
                continue;
            }
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh LOD%d has no vertices."), LODIndex));
            return Result;
        }

        FDWCDataUVGenerationResult UVResult = CanonicalUVResult;
        if (LODIndex != CanonicalDataUVLODIndex)
        {
            // A DWC UV Channel chart boundary is render topology, not just a UV value. Generate
            // each target LOD so it receives its own VertexInstance seams.
            UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
                PreparedMesh,
                LODIndex,
                Asset.GetOriginalUVChannelIndex(),
                DataUVChannelIndex,
                true,
                INDEX_NONE,
                &WettableMaterialSlotIndices);
            if (!UVResult.bSucceeded)
            {
                AddNonCanonicalLODWarning(
                    LODIndex,
                    TEXT("Validation failed"),
                    FString::Printf(
                        TEXT("DWC UV Channel generation did not pass validation, so this LOD was skipped instead of failing the whole build. Original failure: %s"),
                        *UVResult.Message));
                continue;
            }
            if (UVResult.UVChannelIndex != DataUVChannelIndex)
            {
                AddNonCanonicalLODWarning(
                    LODIndex,
                    TEXT("Unexpected UV channel"),
                    FString::Printf(
                        TEXT("Generated DWC UV Channel %d, but this asset requires UV%d. This LOD was skipped; LOD0 remains generated."),
                        UVResult.UVChannelIndex,
                        DataUVChannelIndex));
                continue;
            }

            UWetClothingAsset::ClearMeshContentSignatureCache();
        }

        FDWCDataUVLODMetadata& Metadata = DataUVMetadata.AddDefaulted_GetRef();
        FString MetadataError;
        if (!FDWCDataUVMetadataBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                LODIndex,
                DataUVChannelIndex,
                Metadata,
                &MetadataError))
        {
            if (LODIndex != CanonicalDataUVLODIndex)
            {
                DataUVMetadata.RemoveAt(DataUVMetadata.Num() - 1);
                AddNonCanonicalLODWarning(
                    LODIndex,
                    TEXT("Metadata build failed"),
                    FString::Printf(
                        TEXT("DWC UV Channel metadata could not be built, so this LOD was skipped. LOD0 remains generated. Original failure: %s"),
                        *MetadataError));
                continue;
            }
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d generated invalid DWC UV Channel metadata: %s"),
                LODIndex,
                *MetadataError));
            return Result;
        }
        Metadata.GeneratedMaterialSlotIndices = SortedWettableMaterialSlotIndices;
        Metadata.SlotWarnings = UVResult.SlotWarnings;
        Metadata.SlotWarnings.Sort(
            [](const FDWCDataUVSlotWarning& A, const FDWCDataUVSlotWarning& B)
            {
                return A.MaterialSlotIndex < B.MaterialSlotIndex;
        });
        GeneratedLODIndices.Add(LODIndex);

        bGeneratedWithWarnings = bGeneratedWithWarnings || UVResult.HasWarnings();
        MergeSlotWarnings(SlotWarnings, UVResult.SlotWarnings);
        DegenerateSourceUVTriangleCount += UVResult.DegenerateSourceUVTriangleCount;
        InvalidSourceUVTriangleCount += UVResult.InvalidSourceUVTriangleCount;
        ExcludedTriangleCount += UVResult.DegenerateSourceUVTriangleCount + UVResult.InvalidSourceUVTriangleCount;
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

    if (DataUVMetadata.IsEmpty())
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh produced no DWC UV Channel payloads."));
        return Result;
    }

    int32 OriginalUVIslandCount = 0;
    for (const FDWCEditorUVTopologyData& Topology : OriginalUVTopologies)
    {
        OriginalUVIslandCount += Topology.Islands.Num();
    }

    // Commit the canonical layout plus every non-LOD0 payload that completed successfully.
    // Failed non-LOD0 payloads remain explicit warnings and do not invalidate usable LODs.
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
    Result.bGeneratedWithWarnings = bGeneratedWithWarnings;
    Result.ExcludedTriangleCount = ExcludedTriangleCount;
    Result.DegenerateSourceUVTriangleCount = DegenerateSourceUVTriangleCount;
    Result.InvalidSourceUVTriangleCount = InvalidSourceUVTriangleCount;
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
        TEXT("%s DWC UV Channel %d for %d Wettable material slot(s). Generated %d of %d target LOD(s): %s. Created %d chart-boundary VertexInstance seam(s), with %d LOD0 Original UV island record(s)."),
        Operation,
        DataUVChannelIndex,
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
        Result.Message += FString::Printf(
            TEXT("\n\nDWC UV Channel was generated with warnings. Excluded triangles: %d (degenerate Source UV: %d, invalid Source UV: %d). Split self-overlapping Original UV islands: %d (%d overlap pair(s)). Budget fallback islands: %d."),
            Result.ExcludedTriangleCount,
            Result.DegenerateSourceUVTriangleCount,
            Result.InvalidSourceUVTriangleCount,
            Result.SplitOriginalUVIslandCount,
            Result.SelfOverlapPairCount,
            Result.BudgetFallbackIslandCount);

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
            UE_LOG(LogDWC, Warning, TEXT("%s"), *SlotWarningLogText);
        }
    }

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

