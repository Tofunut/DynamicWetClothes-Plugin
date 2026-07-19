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
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/DerivedData/DWCEditorDerivedDataInvalidator.h"

namespace DWCDataUVBuildServicePrivate
{
    void SetFailure(FDWCDataUVBuildResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }
}

bool FDWCDataUVBuildService::BuildOriginalUVTopology(
    UWetClothingAsset& Asset,
    FString* OutErrorMessage)
{
    FDWCEditorDerivedDataInvalidator::InvalidateAsset(Asset);
    ON_SCOPE_EXIT
    {
        FDWCEditorDerivedDataInvalidator::InvalidateAsset(Asset);
    };

    USkeletalMesh* PreparedMesh = Asset.GetRuntimeSkeletalMesh();
    const FSkeletalMeshRenderData* RenderData = PreparedMesh != nullptr
        ? PreparedMesh->GetResourceForRendering()
        : nullptr;
    if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh has no render LOD data.");
        return false;
    }

    TArray<FDWCEditorUVTopologyData> Topologies;
    Topologies.Reserve(RenderData->LODRenderData.Num());
    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        if (RenderData->LODRenderData[LODIndex].GetNumVertices() <= 0)
        {
            continue;
        }

        FDWCEditorUVTopologyData Topology;
        if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                LODIndex,
                Topology,
                OutErrorMessage))
        {
            return false;
        }
        Topologies.Add(MoveTemp(Topology));
    }

    if (Topologies.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Skeletal Mesh produced no Original UV topology data.");
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
    FDWCEditorDerivedDataInvalidator::InvalidateDataUVRebuild(Asset, TouchedMesh);
    ON_SCOPE_EXIT
    {
        FDWCEditorDerivedDataInvalidator::InvalidateDataUVRebuild(Asset, TouchedMesh);
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

    FDWCPreparedMeshEditTransaction MeshEditTransaction(PreparedMesh);
    FString TransactionError;
    if (!MeshEditTransaction.CaptureAllEditableLODs(&TransactionError))
    {
        SetFailure(Result, TransactionError);
        return Result;
    }

    const int32 PreparedUVCount = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(PreparedMesh, 0);
    if (PreparedUVCount <= 0)
    {
        SetFailure(Result, TEXT("The DWC Prepared Skeletal Mesh must have at least one UV channel."));
        return Result;
    }
    if (Asset.GetOriginalUVChannelIndex() < 0 || Asset.GetOriginalUVChannelIndex() >= PreparedUVCount)
    {
        SetFailure(Result, TEXT("The configured Original UV channel is unavailable on the DWC Prepared Skeletal Mesh."));
        return Result;
    }

    TArray<FDWCDataUVLODMetadata> DataUVMetadata;
    DataUVMetadata.Reserve(RenderData->LODRenderData.Num());
    TArray<FDWCEditorUVTopologyData> OriginalUVTopologies;
    OriginalUVTopologies.Reserve(RenderData->LODRenderData.Num());

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

    const int32 LODCount = RenderData->LODRenderData.Num();
    for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
    {
        const FSkeletalMeshRenderData* CurrentRenderData = PreparedMesh->GetResourceForRendering();
        if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d render data became unavailable during DWC Data UV generation."),
                LODIndex));
            return Result;
        }

        const FSkeletalMeshLODRenderData& LODData = CurrentRenderData->LODRenderData[LODIndex];
        if (LODData.GetNumVertices() <= 0)
        {
            continue;
        }

        const bool bAllowOverwriteExistingChannel =
            bAllowOverwriteExistingDataUVChannel ||
            Asset.GetDWCDataUVChannelIndex() == DataUVChannelIndex ||
            LODIndex > 0;
        FDWCDataUVGenerationResult UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
            PreparedMesh,
            LODIndex,
            Asset.GetOriginalUVChannelIndex(),
            DataUVChannelIndex,
            bAllowOverwriteExistingChannel,
            INDEX_NONE);
        if (!UVResult.bSucceeded)
        {
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d DWC Data UV generation failed: %s"),
                LODIndex,
                *UVResult.Message));
            return Result;
        }

        DataUVChannelIndex = UVResult.UVChannelIndex;
        UWetClothingAsset::ClearMeshContentSignatureCache();

        FDWCEditorUVTopologyData OriginalUVTopology;
        FString TopologyError;
        if (!FDWCOriginalUVTopologyBuilder::BuildLOD(
                Asset,
                PreparedMesh,
                LODIndex,
                OriginalUVTopology,
                &TopologyError))
        {
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d Original UV topology failed: %s"),
                LODIndex,
                *TopologyError));
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
                &MetadataError))
        {
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d generated invalid DWC Data UV metadata: %s"),
                LODIndex,
                *MetadataError));
            return Result;
        }

        bGeneratedWithWarnings |= UVResult.HasWarnings();
        ExcludedTriangleCount += UVResult.DegenerateSourceUVTriangleCount + UVResult.InvalidSourceUVTriangleCount;
        SplitOriginalUVIslandCount += UVResult.SplitOriginalUVIslandCount;
        SelfOverlapPairCount += UVResult.SelfOverlapPairCount;
        TriangleFallbackChartCount += UVResult.TriangleFallbackChartCount;
        OriginalUVTopologies.Add(MoveTemp(OriginalUVTopology));
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

    // Persistent derived data is committed only after every LOD has generated and validated.
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
        TEXT("Generated DWC Data UV channel %d on the DWC Prepared Skeletal Mesh for %d LOD(s) and %d Original UV island record(s)."),
        DataUVChannelIndex,
        Asset.GetDataUVMetadataLODCount(),
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
