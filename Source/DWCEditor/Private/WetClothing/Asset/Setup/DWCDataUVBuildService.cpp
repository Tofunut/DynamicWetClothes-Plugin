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
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"

namespace DWCDataUVBuildServicePrivate
{
    static constexpr int32 CanonicalDataUVLODIndex = 0;

    void SetFailure(FDWCDataUVBuildResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
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

bool FDWCDataUVBuildService::BuildOriginalUVTopology(
    UWetClothingAsset& Asset,
    FString* OutErrorMessage)
{
    FWCAGeneratedDataInvalidator::InvalidateAsset(Asset);
    ON_SCOPE_EXIT
    {
        FWCAGeneratedDataInvalidator::InvalidateAsset(Asset);
    };

    USkeletalMesh* PreparedMesh = Asset.GetRuntimeSkeletalMesh();
    const FSkeletalMeshRenderData* RenderData = PreparedMesh != nullptr
        ? PreparedMesh->GetResourceForRendering()
        : nullptr;
    if (RenderData == nullptr)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh has no render LOD data.");
        return false;
    }

    TArray<FDWCEditorUVTopologyData> Topologies;
    Topologies.Reserve(1);
    const int32 TopologyLODIndex = DWCDataUVBuildServicePrivate::CanonicalDataUVLODIndex;
    if (!RenderData->LODRenderData.IsValidIndex(TopologyLODIndex))
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh has no LOD0 render data.");
        return false;
    }

    if (RenderData->LODRenderData[TopologyLODIndex].GetNumVertices() <= 0)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh LOD0 has no vertices.");
        return false;
    }

    FDWCEditorUVTopologyData& Topology = Topologies.AddDefaulted_GetRef();
    if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
            Asset,
            PreparedMesh,
            TopologyLODIndex,
            Topology,
            OutErrorMessage))
    {
        return false;
    }

    Asset.SetOriginalUVTopologies(MoveTemp(Topologies));
    if (OutErrorMessage) OutErrorMessage->Reset();
    return true;
}

FDWCDataUVBuildResult FDWCDataUVBuildService::Generate(
    UWetClothingAsset& Asset,
    const bool bForceNewAsset,
    const bool bAllowOverwriteExistingDataUVChannel)
{
    using namespace DWCDataUVBuildServicePrivate;

    FDWCDataUVBuildResult Result;
    USkeletalMesh* TouchedMesh = Asset.GetRuntimeSkeletalMesh();

    // An explicit Rebuild is always an invalidation boundary. This applies to success,
    // validation failure, user cancellation, and partial generation failure alike.
    FWCAGeneratedDataInvalidator::InvalidateDataUVRebuild(Asset, TouchedMesh);
    ON_SCOPE_EXIT
    {
        FWCAGeneratedDataInvalidator::InvalidateDataUVRebuild(Asset, TouchedMesh);
    };

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

    FDWCPreparedMeshEditTransaction MeshEditTransaction(PreparedMesh);
    FString TransactionError;
    if (!MeshEditTransaction.CaptureEditableLOD(CanonicalDataUVLODIndex, &TransactionError))
    {
        SetFailure(Result, TransactionError);
        return Result;
    }

    for (const int32 LODIndex : PayloadLODIndices)
    {
        const int32 PreparedUVCount = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(PreparedMesh, LODIndex);
        if (PreparedUVCount <= 0)
        {
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh LOD%d must have at least one UV channel."), LODIndex));
            return Result;
        }
        if (Asset.GetOriginalUVChannelIndex() < 0 || Asset.GetOriginalUVChannelIndex() >= PreparedUVCount)
        {
            SetFailure(Result, FString::Printf(TEXT("The configured Original UV channel is unavailable on the DWC Prepared Skeletal Mesh LOD%d."), LODIndex));
            return Result;
        }
    }

    TArray<FDWCDataUVLODMetadata> DataUVMetadata;
    DataUVMetadata.Reserve(PayloadLODIndices.Num());
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;
    OriginalUVTopologies.Reserve(1);

    int32 ExcludedTriangleCount = 0;
    int32 SplitOriginalUVIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 TriangleFallbackChartCount = 0;
    bool bGeneratedWithWarnings = false;

    int32 DataUVChannelIndex = Asset.GetDWCDataUVChannelIndex();
    if (DataUVChannelIndex == INDEX_NONE || bForceNewAsset || Asset.GetRuntimeSkeletalMesh() != PreparedMesh)
    {
        DataUVChannelIndex = FMath::Clamp(
            Asset.GetSetupSettings().PreferredDWCDataUVChannelIndex,
            0,
            7);
    }
    if (DataUVChannelIndex == Asset.GetOriginalUVChannelIndex())
    {
        SetFailure(Result, TEXT("DWC Data UV Channel cannot overwrite the configured Original UV Channel."));
        return Result;
    }

    UWetClothingAsset::ClearMeshContentSignatureCache();

    const bool bAllowOverwriteExistingChannel =
        bAllowOverwriteExistingDataUVChannel ||
        Asset.GetDWCDataUVChannelIndex() == DataUVChannelIndex;

    const FSkeletalMeshRenderData* CurrentRenderData = PreparedMesh->GetResourceForRendering();
    if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(CanonicalDataUVLODIndex))
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh has no LOD0 render data."));
        return Result;
    }

    if (CurrentRenderData->LODRenderData[CanonicalDataUVLODIndex].GetNumVertices() <= 0)
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh LOD0 has no vertices."));
        return Result;
    }

    FDWCDataUVGenerationResult CanonicalUVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
        PreparedMesh,
        CanonicalDataUVLODIndex,
        Asset.GetOriginalUVChannelIndex(),
        DataUVChannelIndex,
        bAllowOverwriteExistingChannel,
        INDEX_NONE);
    if (!CanonicalUVResult.bSucceeded)
    {
        SetFailure(Result, FString::Printf(
            TEXT("LOD0 DWC Data UV generation failed: %s"),
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
            &TopologyError))
    {
        SetFailure(Result, FString::Printf(
            TEXT("LOD0 Original UV topology failed: %s"),
            *TopologyError));
        return Result;
    }
    OriginalUVTopologies.Add(MoveTemp(OriginalUVTopology));

    bGeneratedWithWarnings = bGeneratedWithWarnings || CanonicalUVResult.HasWarnings();
    ExcludedTriangleCount += CanonicalUVResult.DegenerateSourceUVTriangleCount + CanonicalUVResult.InvalidSourceUVTriangleCount;
    SplitOriginalUVIslandCount += CanonicalUVResult.SplitOriginalUVIslandCount;
    SelfOverlapPairCount += CanonicalUVResult.SelfOverlapPairCount;
    TriangleFallbackChartCount += CanonicalUVResult.TriangleFallbackChartCount;

    for (const int32 LODIndex : PayloadLODIndices)
    {
        CurrentRenderData = PreparedMesh->GetResourceForRendering();
        if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh has no LOD%d render data."), LODIndex));
            return Result;
        }

        if (CurrentRenderData->LODRenderData[LODIndex].GetNumVertices() <= 0)
        {
            SetFailure(Result, FString::Printf(TEXT("The DWC Prepared Skeletal Mesh LOD%d has no vertices."), LODIndex));
            return Result;
        }

        FDWCDataUVGenerationResult UVResult = CanonicalUVResult;
        if (LODIndex != CanonicalDataUVLODIndex)
        {
            UVResult = FDWCDataUVGenerator::TransferFromSourceLOD(
                PreparedMesh,
                CanonicalDataUVLODIndex,
                LODIndex,
                DataUVChannelIndex,
                true,
                INDEX_NONE);
            if (!UVResult.bSucceeded)
            {
                SetFailure(Result, FString::Printf(
                    TEXT("LOD%d DWC Data UV transfer failed: %s"),
                    LODIndex,
                    *UVResult.Message));
                return Result;
            }
            if (UVResult.UVChannelIndex != DataUVChannelIndex)
            {
                SetFailure(Result, FString::Printf(
                    TEXT("LOD%d transferred DWC Data UV channel %d, but this asset requires UV%d for every generated LOD."),
                    LODIndex,
                    UVResult.UVChannelIndex,
                    DataUVChannelIndex));
                return Result;
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
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d generated invalid DWC Data UV metadata: %s"),
                LODIndex,
                *MetadataError));
            return Result;
        }

        bGeneratedWithWarnings = bGeneratedWithWarnings || UVResult.HasWarnings();
        ExcludedTriangleCount += UVResult.DegenerateSourceUVTriangleCount + UVResult.InvalidSourceUVTriangleCount;
        SplitOriginalUVIslandCount += UVResult.SplitOriginalUVIslandCount;
        SelfOverlapPairCount += UVResult.SelfOverlapPairCount;
        TriangleFallbackChartCount += UVResult.TriangleFallbackChartCount;
    }

    if (DataUVMetadata.IsEmpty())
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh produced no DWC Data UV payloads."));
        return Result;
    }

    int32 OriginalUVIslandCount = 0;
    for (const FDWCEditorUVTopologyData& Topology : OriginalUVTopologies)
    {
        OriginalUVIslandCount += Topology.Islands.Num();
    }

    // Persistent generated data is committed only after every target LOD succeeds.
    Asset.SetGeneratedDataUVTarget(PreparedMesh, DataUVChannelIndex);
    Asset.SetDataUVMetadata(MoveTemp(DataUVMetadata));
    Asset.SetOriginalUVTopologies(MoveTemp(OriginalUVTopologies));
    MeshEditTransaction.Commit();

    Result.bSucceeded = true;
    Result.PreparedMesh = PreparedMesh;
    Result.OriginalUVIslandCount = OriginalUVIslandCount;
    Result.bGeneratedWithWarnings = bGeneratedWithWarnings;
    Result.ExcludedTriangleCount = ExcludedTriangleCount;
    Result.SplitOriginalUVIslandCount = SplitOriginalUVIslandCount;
    Result.SelfOverlapPairCount = SelfOverlapPairCount;
    Result.TriangleFallbackChartCount = TriangleFallbackChartCount;
    Result.Message = FString::Printf(
        TEXT("Generated canonical LOD0 DWC Data UV channel %d and transferred it to LOD%d-LOD%d of the DWC Prepared Skeletal Mesh with %d LOD0 Original UV island record(s)."),
        DataUVChannelIndex,
        FirstLODIndex,
        LastLODIndex,
        Result.OriginalUVIslandCount);

    if (Result.bGeneratedWithWarnings)
    {
        Result.Message += FString::Printf(
            TEXT("\n\nDWC Data UV was generated with warnings. Excluded triangles: %d. Split self-overlapping Original UV islands: %d (%d overlap pair(s)). Triangle fallback charts: %d."),
            Result.ExcludedTriangleCount,
            Result.SplitOriginalUVIslandCount,
            Result.SelfOverlapPairCount,
            Result.TriangleFallbackChartCount);
    }

    return Result;
}
