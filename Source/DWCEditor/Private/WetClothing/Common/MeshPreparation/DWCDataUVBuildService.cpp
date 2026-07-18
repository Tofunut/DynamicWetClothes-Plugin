#include "DWCDataUVBuildService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "ObjectTools.h"
#include "UObject/SoftObjectPath.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "DWCDataUVGenerator.h"

namespace DWCGeneratedDataUVPrivate
{
    void SetFailure(FDWCDataUVBuildResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    USkeletalMesh* ResolveTargetMesh(UWetClothingAsset& Asset, const bool bForceNewAsset, FDWCDataUVBuildResult& Result)
    {
        USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
        if (SourceMesh == nullptr)
        {
            SetFailure(Result, TEXT("The Wet Clothing Asset has no Source Skeletal Mesh."));
            return nullptr;
        }

        if (Asset.GetSetupSettings().bModifySourceMeshForDWCDataUV)
        {
            return SourceMesh;
        }

        if (!bForceNewAsset && Asset.GetDWCSkeletalMesh() != nullptr && Asset.GetDWCSkeletalMesh() != SourceMesh)
        {
            return Asset.GetDWCSkeletalMesh();
        }

        const FString AssetPackageName = Asset.GetOutermost() != nullptr ? Asset.GetOutermost()->GetName() : FString();
        if (!FPackageName::IsValidLongPackageName(AssetPackageName))
        {
            SetFailure(Result, TEXT("The Wet Clothing Asset must be saved before DWC can create a mesh copy."));
            return nullptr;
        }

        const FString WCAFolder = FPackageName::GetLongPackagePath(AssetPackageName);
        const FString GeneratedMeshFolder = WCAFolder / TEXT("Generated") / Asset.GetName() / TEXT("Mesh");
        const FString SourceAssetName = SourceMesh->GetName();
        const FString DWCAssetName = SourceAssetName.EndsWith(TEXT("_DWC"))
            ? SourceAssetName
            : SourceAssetName + TEXT("_DWC");
        const FString TargetPackageName = GeneratedMeshFolder / DWCAssetName;
        const FString TargetObjectPath = TargetPackageName + TEXT(".") + DWCAssetName;

        if (USkeletalMesh* ExistingMesh = LoadObject<USkeletalMesh>(nullptr, *TargetObjectPath))
        {
            if (ExistingMesh == Asset.GetDWCSkeletalMesh())
            {
                return ExistingMesh;
            }

            const FText Warning = FText::FromString(FString::Printf(
                TEXT("A Skeletal Mesh already exists at the deterministic DWC output path:\n\n%s\n\nReplacing it will permanently delete that asset before creating a new DWC mesh copy. Continue?"),
                *TargetObjectPath));
            if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes)
            {
                SetFailure(Result, TEXT("DWC Skeletal Mesh creation was cancelled because the target path is occupied."));
                return nullptr;
            }

            if (!ObjectTools::DeleteSingleObject(ExistingMesh, false))
            {
                SetFailure(Result, TEXT("Failed to remove the existing asset at the DWC Skeletal Mesh output path."));
                return nullptr;
            }
        }
        else if (UObject* ConflictingObject = LoadObject<UObject>(nullptr, *TargetObjectPath))
        {
            SetFailure(Result, FString::Printf(
                TEXT("A non-Skeletal-Mesh asset already exists at the DWC output path: %s"),
                *GetPathNameSafe(ConflictingObject)));
            return nullptr;
        }

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        UObject* DuplicatedObject = AssetToolsModule.Get().DuplicateAsset(
            DWCAssetName,
            GeneratedMeshFolder,
            SourceMesh);
        USkeletalMesh* DWCMesh = Cast<USkeletalMesh>(DuplicatedObject);
        if (DWCMesh == nullptr)
        {
            SetFailure(Result, TEXT("Failed to duplicate the Source Mesh for DWC Data UV generation."));
            return nullptr;
        }

        FAssetRegistryModule::AssetCreated(DWCMesh);
        DWCMesh->MarkPackageDirty();
        return DWCMesh;
    }

    bool BuildDataUVMetadataFromMeshChannel(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* Mesh,
        const int32 LODIndex,
        const int32 DWCDataUVChannelIndex,
        FDWCDataUVLODMetadata& OutMetadata,
        FString* OutErrorMessage)
    {
        OutMetadata = FDWCDataUVLODMetadata();
        if (Mesh == nullptr)
        {
            if (OutErrorMessage) *OutErrorMessage = TEXT("No runtime mesh is available.");
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex);
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        if (VertexCount <= 0)
        {
            if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d has no render vertices."), LODIndex);
            return false;
        }

        const int32 NumTexCoords = static_cast<int32>(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
        if (DWCDataUVChannelIndex < 0 || DWCDataUVChannelIndex >= NumTexCoords)
        {
            if (OutErrorMessage)
            {
                *OutErrorMessage = FString::Printf(
                    TEXT("LOD%d does not contain DWC Data UV channel %d."),
                    LODIndex,
                    DWCDataUVChannelIndex);
            }
            return false;
        }

        OutMetadata.bIsValid = true;
        OutMetadata.LODIndex = LODIndex;
        OutMetadata.RenderVertexCount = VertexCount;
        OutMetadata.MaterialSlotCount = Mesh->GetMaterials().Num();
        OutMetadata.UVChannelIndex = DWCDataUVChannelIndex;
        OutMetadata.MeshInputSignature = UWetClothingAsset::BuildMeshContentSignature(
            Asset.GetSourceSkeletalMesh(),
            LODIndex,
            Asset.GetOriginalUVChannelIndex());
        OutMetadata.DataUVOutputSignature = UWetClothingAsset::BuildMeshContentSignature(
            Mesh,
            LODIndex,
            DWCDataUVChannelIndex);
        OutMetadata.GeneratorVersion = 1;

        if (OutMetadata.MeshInputSignature.IsEmpty() || OutMetadata.DataUVOutputSignature.IsEmpty())
        {
            if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d DWC Data UV signature is empty."), LODIndex);
            return false;
        }

        return true;
    }

    bool BuildOriginalUVTopologyData(
        const UWetClothingAsset& Asset,
        USkeletalMesh* SourceMesh,
        const int32 LODIndex,
        FDWCEditorUVTopologyData& OutTopology,
        FString* OutErrorMessage)
    {
        OutTopology = FDWCEditorUVTopologyData();
        if (SourceMesh == nullptr)
        {
            if (OutErrorMessage) *OutErrorMessage = TEXT("No Source Mesh is available.");
            return false;
        }

        OutTopology.LODIndex = LODIndex;
        OutTopology.UVChannelIndex = Asset.GetOriginalUVChannelIndex();
        OutTopology.BuildSignature = UWetClothingAsset::BuildMeshContentSignature(
            SourceMesh, OutTopology.LODIndex, OutTopology.UVChannelIndex);
        if (OutTopology.BuildSignature.IsEmpty())
        {
            if (OutErrorMessage) *OutErrorMessage = TEXT("Failed to build the Source Mesh Original-UV signature.");
            return false;
        }

        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < SourceMesh->GetMaterials().Num(); ++MaterialSlotIndex)
        {
            TArray<FWetClothingAssetUVIsland> Islands;
            FString Error;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                    SourceMesh,
                    OutTopology.LODIndex,
                    OutTopology.UVChannelIndex,
                    MaterialSlotIndex,
                    Islands,
                    &Error))
            {
                if (OutErrorMessage)
                {
                    *OutErrorMessage = FString::Printf(
                        TEXT("Material Slot %d Original-UV analysis failed: %s"),
                        MaterialSlotIndex,
                        *Error);
                }
                return false;
            }

            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                FDWCOriginalUVIslandTopology& Record = OutTopology.Islands.AddDefaulted_GetRef();
                Record.MaterialSlotIndex = MaterialSlotIndex;
                Record.IslandID = Island.UVIslandID;
                Record.TriangleIndices = Island.TriangleIDs;
                Record.UVBounds = Island.UVBounds;
                Record.UVArea = Island.UVArea;
            }
        }

        OutTopology.bIsValid = !OutTopology.Islands.IsEmpty();
        if (!OutTopology.bIsValid)
        {
            if (OutErrorMessage) *OutErrorMessage = TEXT("The Source Mesh contains no Original-UV island records.");
            return false;
        }

        if (OutErrorMessage) OutErrorMessage->Reset();
        return true;
    }
}

bool FDWCDataUVBuildService::BuildOriginalUVTopology(UWetClothingAsset& Asset, FString* OutErrorMessage)
{
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    const FSkeletalMeshRenderData* RenderData = SourceMesh != nullptr ? SourceMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The runtime mesh has no render LOD data.");
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
        if (!DWCGeneratedDataUVPrivate::BuildOriginalUVTopologyData(
                Asset,
                SourceMesh,
                LODIndex,
                Topology,
                OutErrorMessage))
        {
            return false;
        }
        Topologies.Add(MoveTemp(Topology));
    }

    Asset.SetOriginalUVTopologies(MoveTemp(Topologies));
    return true;
}

FDWCDataUVBuildResult FDWCDataUVBuildService::Generate(UWetClothingAsset& Asset, const bool bForceNewAsset)
{
    using namespace DWCGeneratedDataUVPrivate;

    FDWCDataUVBuildResult Result;
    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    if (SourceMesh == nullptr)
    {
        SetFailure(Result, TEXT("The Wet Clothing Asset has no Source Skeletal Mesh."));
        return Result;
    }

    USkeletalMesh* TargetMesh = ResolveTargetMesh(Asset, bForceNewAsset, Result);
    if (TargetMesh == nullptr)
    {
        return Result;
    }

    const FSkeletalMeshRenderData* RenderData = TargetMesh->GetResourceForRendering();
    if (RenderData == nullptr || RenderData->LODRenderData.IsEmpty())
    {
        SetFailure(Result, TEXT("The runtime mesh has no render LOD data."));
        return Result;
    }

    const int32 SourceUVCount = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(SourceMesh, 0);
    if (SourceUVCount <= 0)
    {
        SetFailure(Result, TEXT("The Source Mesh must have at least one UV channel."));
        return Result;
    }
    if (Asset.GetOriginalUVChannelIndex() < 0 || Asset.GetOriginalUVChannelIndex() >= SourceUVCount)
    {
        SetFailure(Result, TEXT("The configured Original UV channel is unavailable on the Source Mesh."));
        return Result;
    }

    TArray<FDWCDataUVLODMetadata> DataUVMetadata;
    DataUVMetadata.Reserve(RenderData->LODRenderData.Num());
    TArray<FDWCEditorUVTopologyData> Topologies;
    Topologies.Reserve(RenderData->LODRenderData.Num());

    int32 ExcludedTriangleCount = 0;
    int32 SplitSourceIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 TriangleFallbackChartCount = 0;
    bool bGeneratedWithWarnings = false;
    int32 DWCDataUVChannelIndex = Asset.GetDWCDataUVChannelIndex();
    if (DWCDataUVChannelIndex == INDEX_NONE || bForceNewAsset || Asset.GetRuntimeSkeletalMesh() != TargetMesh)
    {
        DWCDataUVChannelIndex = FMath::Clamp(Asset.GetSetupSettings().PreferredDWCDataUVChannelIndex, 0, 7);
    }

    UWetClothingAsset::ClearMeshContentSignatureCache();

    const int32 LODCount = RenderData->LODRenderData.Num();
    for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
    {
        const FSkeletalMeshRenderData* CurrentRenderData = TargetMesh->GetResourceForRendering();
        if (CurrentRenderData == nullptr || !CurrentRenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            SetFailure(Result, FString::Printf(TEXT("LOD%d render data became unavailable during DWC Data UV generation."), LODIndex));
            return Result;
        }

        const FSkeletalMeshLODRenderData& LODData = CurrentRenderData->LODRenderData[LODIndex];
        if (LODData.GetNumVertices() <= 0)
        {
            continue;
        }

        const bool bAllowOverwriteExistingChannel = Asset.GetDWCDataUVChannelIndex() == DWCDataUVChannelIndex ||
                                                    LODIndex > 0 ||
                                                    TargetMesh != SourceMesh;
        FDWCDataUVGenerationResult UVResult = FDWCDataUVGenerator::GenerateForSkeletalMesh(
            TargetMesh,
            LODIndex,
            Asset.GetOriginalUVChannelIndex(),
            DWCDataUVChannelIndex,
            bAllowOverwriteExistingChannel,
            INDEX_NONE);
        if (!UVResult.bSucceeded)
        {
            SetFailure(Result, FString::Printf(TEXT("LOD%d DWC Data UV generation failed: %s"), LODIndex, *UVResult.Message));
            return Result;
        }
        DWCDataUVChannelIndex = UVResult.UVChannelIndex;
        UWetClothingAsset::ClearMeshContentSignatureCache();

        FDWCEditorUVTopologyData Topology;
        FString TopologyError;
        if (!BuildOriginalUVTopologyData(Asset, SourceMesh, LODIndex, Topology, &TopologyError))
        {
            SetFailure(Result, FString::Printf(
                TEXT("LOD%d Original UV topology failed: %s"),
                LODIndex,
                *TopologyError));
            return Result;
        }

        FDWCDataUVLODMetadata& Metadata = DataUVMetadata.AddDefaulted_GetRef();
        FString DataUVPayloadError;
        if (!BuildDataUVMetadataFromMeshChannel(Asset, TargetMesh, LODIndex, DWCDataUVChannelIndex, Metadata, &DataUVPayloadError))
        {
            SetFailure(Result, FString::Printf(TEXT("LOD%d generated an invalid DWC Data UV payload. %s"), LODIndex, *DataUVPayloadError));
            return Result;
        }

        bGeneratedWithWarnings |= UVResult.HasWarnings();
        ExcludedTriangleCount += UVResult.DegenerateSourceUVTriangleCount + UVResult.InvalidSourceUVTriangleCount;
        SplitSourceIslandCount += UVResult.SplitSourceIslandCount;
        SelfOverlapPairCount += UVResult.SelfOverlapPairCount;
        TriangleFallbackChartCount += UVResult.TriangleFallbackChartCount;
        Topologies.Add(MoveTemp(Topology));
    }

    if (DataUVMetadata.IsEmpty())
    {
        SetFailure(Result, TEXT("The Source Mesh produced no DWC Data UV payloads."));
        return Result;
    }

    int32 OriginalIslandCount = 0;
    for (const FDWCEditorUVTopologyData& Topology : Topologies)
    {
        OriginalIslandCount += Topology.Islands.Num();
    }
    Asset.SetGeneratedDataUVTarget(TargetMesh, DWCDataUVChannelIndex);
    Asset.SetDataUVMetadata(MoveTemp(DataUVMetadata));
    Asset.SetOriginalUVTopologies(MoveTemp(Topologies));

    Result.bSucceeded = true;
    Result.GeneratedDataUV = TargetMesh;
    Result.OriginalIslandCount = OriginalIslandCount;
    Result.bGeneratedWithWarnings = bGeneratedWithWarnings;
    Result.ExcludedTriangleCount = ExcludedTriangleCount;
    Result.SplitSourceIslandCount = SplitSourceIslandCount;
    Result.SelfOverlapPairCount = SelfOverlapPairCount;
    Result.TriangleFallbackChartCount = TriangleFallbackChartCount;
    Result.Message = FString::Printf(
        TEXT("Generated DWC Data UV channel %d on %s for %d LOD(s) and %d Original-UV island records."),
        DWCDataUVChannelIndex,
        Asset.GetSetupSettings().bModifySourceMeshForDWCDataUV
            ? TEXT("the Source Skeletal Mesh")
            : TEXT("the DWC Skeletal Mesh"),
        Asset.GetDataUVMetadataLODCount(),
        Result.OriginalIslandCount);

    if (Result.bGeneratedWithWarnings)
    {
        Result.Message += FString::Printf(
            TEXT("\n\nDWC Data UV was generated with warnings. Excluded triangles: %d. Split self-overlapping source islands: %d (%d overlap pair(s)). Triangle fallback charts: %d."),
            Result.ExcludedTriangleCount,
            Result.SplitSourceIslandCount,
            Result.SelfOverlapPairCount,
            Result.TriangleFallbackChartCount);
    }
    return Result;
}
