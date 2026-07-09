#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class USkeletalMesh;

struct FDWCRevealBakeSurfaceTriangle
{
    int32 TriangleIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector Positions[3];
    FVector Normals[3];
    FVector2D UVs[3];
    FBox Bounds = FBox(ForceInit);
};

struct FDWCRevealBakeSurface
{
    FName LayerId;
    int32 LayerOrder = 0;
    int32 LODIndex = 0;
    int32 UVChannelIndex = 0;
    TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
    bool bCanBeRevealSource = true;
    bool bCanBeWetOuterLayer = true;
    bool bBlocksReveal = false;
    float MaxRevealDistance = 5.0f;
    TArray<FDWCRevealBakeSurfaceTriangle> Triangles;
    FBox Bounds = FBox(ForceInit);

    void Reset();
};

class FDWCRevealBakeSurfaceBuilder
{
  public:
    static bool BuildReferencePoseSurface(
        const FDWCBakeResolvedLayer& ResolvedLayer,
        int32                        LODIndex,
        int32                        UVChannelIndex,
        FDWCRevealBakeSurface&             OutSurface,
        FString*                     OutErrorMessage = nullptr);

  private:
    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
