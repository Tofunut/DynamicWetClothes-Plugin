// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class USkeletalMesh;

struct FDWCRevealBakeSurfaceTriangle
{
    int32     TriangleIndex = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
    int32     VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector   Positions[3];
    FVector3f Normals[3] = {
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f)
    };
    FVector3f Tangents[3] = {
        FVector3f(1.0f, 0.0f, 0.0f),
        FVector3f(1.0f, 0.0f, 0.0f),
        FVector3f(1.0f, 0.0f, 0.0f)
    };
    int8      BitangentSigns[3] = { 1, 1, 1 };
    bool      bHasValidImportedTangentBasis = false;
    FVector2D UVs[3];
    FBox      Bounds = FBox(ForceInit);
};

/** Orthonormal frame evaluated at one point on a baked source or target surface. */
struct FDWCRevealBakeSurfaceFrame
{
    FVector Tangent = FVector::ForwardVector;
    FVector Bitangent = FVector::RightVector;
    FVector Normal = FVector::UpVector;

    bool IsValid() const;
};

/**
 * Shared tangent-frame rules for reveal surface baking. Imported render-vertex
 * tangent data is copied into the Stage 2 snapshot so normal reorientation is
 * independent of the DWC Data UV and remains worker-safe.
 */
class FDWCRevealBakeSurfaceFrameBuilder
{
  public:
    static bool TransformImportedBasis(
        const FTransform& BakeTransform,
        const FVector3f& LocalTangent,
        const FVector3f& LocalBitangent,
        const FVector3f& LocalNormal,
        FVector3f& OutTangent,
        FVector3f& OutNormal,
        int8& OutBitangentSign);

    static bool BuildInterpolatedFrame(
        const FDWCRevealBakeSurfaceTriangle& Triangle,
        const FVector& Barycentric,
        FDWCRevealBakeSurfaceFrame& OutFrame);

    static FVector3f ReorientTangentNormal(
        const FVector3f& SourceTangentNormal,
        const FDWCRevealBakeSurfaceFrame& SourceFrame,
        const FDWCRevealBakeSurfaceFrame& TargetFrame);

    /** RG = target tangent normal XY, B = Metallic, A = valid source hit coverage. */
    static FColor EncodeRevealSurface(
        const FVector3f& TargetTangentNormal,
        float Metallic,
        bool bHasValidSourceHit);
};

struct FDWCRevealBakeSurface
{
    FName                                 LayerId;
    int32                                 LayerOrder = 0;
    int32                                 LODIndex = 0;
    int32                                 UVChannelIndex = 0;
    TObjectPtr<USkeletalMesh>             SkeletalMesh = nullptr;
    bool                                  bCanBeRevealSource = true;
    bool                                  bCanBeWetOuterLayer = true;
    bool                                  bBlocksReveal = false;
    float                                 MaxRevealDistance = 5.0f;
    TArray<FDWCRevealBakeSurfaceTriangle> Triangles;
    FBox                                  Bounds = FBox(ForceInit);

    void Reset();
};

class FDWCRevealBakeSurfaceBuilder
{
  public:
    static bool BuildReferencePoseSurface(
        const FDWCBakeResolvedLayer& ResolvedLayer,
        int32                        LODIndex,
        int32                        UVChannelIndex,
        FDWCRevealBakeSurface&       OutSurface,
        FString*                     OutErrorMessage = nullptr);

  private:
    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
