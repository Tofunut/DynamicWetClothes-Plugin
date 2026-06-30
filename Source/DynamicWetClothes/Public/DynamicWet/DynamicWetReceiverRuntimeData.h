#pragma once

#include "CoreMinimal.h"
#include "WetClothing/WetClothingSkeletalMeshCacheBuilder.h"
#include "WetnessProfile.h"

struct FWetnessProfileParameters;
class USkeletalMeshComponent;
class UWetClothingAsset;
struct FDynamicWetReceiverContext;
class FSkeletalMeshLODRenderData;

struct FDynamicWetReceiverVertexNeighbors
{
    TArray<int32> Neighbors;
};

class FDynamicWetReceiverRuntimeData
{
public:
    void ResetWetPartData();
    void ResetNeighborGraph();
    void ResetBoneOptimizationCache();

    TArray<int32> VertexWetPartIDs;
    TArray<FWetnessProfileParameters> VertexWetnessProfileParameters;
    TArray<FLinearColor> VertexWetPartDebugColors;
    TArray<FDynamicWetReceiverVertexNeighbors> NeighborGraph;
    FWetClothingSkeletalMeshOptimizationCache BoneOptimizationCache;
    bool bHasBoneOptimizationCache = false;
};

class FDynamicWetReceiverRuntimeDataBuilder
{
public:
    void InitializeWetnessData(FDynamicWetReceiverContext& Receiver);
    void InitializeWetPartVertexData(FDynamicWetReceiverContext& Receiver);
    bool InitializeWetPartVertexDataFromBakedProfile(
        FDynamicWetReceiverContext& Receiver,
        int32 VertexCount,
        const FWetnessProfileParameters& DefaultParameters);
    void BuildNeighborGraph(FDynamicWetReceiverContext& Receiver);
    bool BuildNeighborGraphFromBakedProfile(FDynamicWetReceiverContext& Receiver, int32 VertexCount);
    void AddNeighbor(FDynamicWetReceiverContext& Receiver, int32 VertexIndex, int32 NeighborIndex);
    void EnsureWetnessBufferSize(FDynamicWetReceiverContext& Receiver, int32 VertexCount);
    bool GetLODRenderData(
        const FDynamicWetReceiverContext& Receiver,
        int32 LODIndex,
        FSkeletalMeshLODRenderData*& OutLODData);
    bool BuildBoneOptimizationCache(FDynamicWetReceiverContext& Receiver, int32 LODIndex = 0);
    bool GetBoneCandidateVertexRange(
        const FDynamicWetReceiverContext& Receiver,
        FName BoneName,
        int32& OutStartOffset,
        int32& OutEndOffset) const;
    bool DoesVertexMatchBoneName(const FDynamicWetReceiverContext& Receiver, int32 VertexIndex, FName BoneName);
};
