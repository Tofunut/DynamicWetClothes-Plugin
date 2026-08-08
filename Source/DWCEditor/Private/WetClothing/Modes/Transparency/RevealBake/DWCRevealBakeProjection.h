// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

class FDWCEditorCancellationToken;

struct FDWCRevealBakeTexelSample
{
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FVector2D UV = FVector2D::ZeroVector;
    FVector   Position = FVector::ZeroVector;
    FVector   Normal = FVector::UpVector;
    int32     TriangleIndex = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
    FVector   Barycentric = FVector::ZeroVector;
};

struct FDWCRevealBakeRayHit
{
    bool      bHit = false;
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FName     SourceLayerId = NAME_None;
    int32     SourceTriangleIndex = INDEX_NONE;
    int32     SourceMaterialSlotIndex = INDEX_NONE;
    FVector   Position = FVector::ZeroVector;
    FVector   Normal = FVector::UpVector;
    FVector2D SourceUV = FVector2D::ZeroVector;
    float     Distance = 0.0f;
    float     Confidence = 0.0f;
};

struct FDWCRevealBakeTexelSamplingSettings
{
    FIntPoint Resolution = FIntPoint(1024, 1024);
    int32     MaterialSlotIndex = INDEX_NONE;
};

struct FDWCRevealBakeRayProjectionSettings
{
    float RayStartOffset = 0.01f;
    float RayLengthScale = 1.0f;
    float MinHitDistance = 0.0f;
    bool  bRespectSourceLayerOrder = true;
    bool  bRespectBlockers = true;
    bool  bPreferLowerSourceLayerOrder = false;
    bool  bRespectPerSourceMaxDistance = false;
    bool  bUseNormalAlignmentConfidence = false;
};

class FDWCRevealBakeTexelSampler
{
  public:
    static bool BuildOuterTexelSamples(
        const FDWCRevealBakeSurface&               OuterSurface,
        const FDWCRevealBakeTexelSamplingSettings& Settings,
        TArray<FDWCRevealBakeTexelSample>&         OutSamples,
        FString*                                   OutErrorMessage = nullptr,
        int32*                                     OutOverlappedPixelCount = nullptr);

  private:
    static constexpr double BarycentricTolerance = -1.0e-6;
    static constexpr double RayIntersectionEpsilon = 1.0e-8;

    static FVector InterpolateVector(
        const FVector& Barycentric,
        const FVector  Values[3]);

    static FIntRect MakePixelBoundsFromUVTriangle(
        const FDWCRevealBakeSurfaceTriangle& Triangle,
        const FIntPoint&                     Resolution);

    static int32 MakePixelKey(
        int32 X,
        int32 Y,
        int32 Width);

    static bool ComputeBarycentricInUV(
        const FVector2D&                     UV,
        const FDWCRevealBakeSurfaceTriangle& Triangle,
        FVector&                             OutBarycentric);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};

class FDWCRevealBakeRayProjector
{
  public:
    static bool ProjectSamplesToSources(
        const FDWCRevealBakeSurface&                    OuterSurface,
        const TArray<FDWCRevealBakeSurface>&            SourceSurfaces,
        const TArray<FDWCRevealBakeTexelSample>&        Samples,
        const FDWCRevealBakeRayProjectionSettings&      Settings,
        TFunctionRef<void(const FDWCRevealBakeRayHit&)> ConsumeHit,
        FString*                                        OutErrorMessage = nullptr,
        const FDWCEditorCancellationToken*              CancellationToken = nullptr);

  private:
    static constexpr double RayIntersectionEpsilon = 1.0e-8;
    static constexpr int32  BvhLeafTriangleCount = 8;

    struct FCandidateHit
    {
        const FDWCRevealBakeSurface*         SourceSurface = nullptr;
        const FDWCRevealBakeSurfaceTriangle* Triangle = nullptr;
        FVector                              Barycentric = FVector::ZeroVector;
        float                                Distance = 0.0f;
    };

    struct FBakeProjectionTriangleRef
    {
        const FDWCRevealBakeSurface*         SourceSurface = nullptr;
        const FDWCRevealBakeSurfaceTriangle* Triangle = nullptr;
        FBox                                 Bounds = FBox(ForceInit);
        FVector                              Center = FVector::ZeroVector;
    };

    struct FBakeProjectionBvhNode
    {
        FBox  Bounds = FBox(ForceInit);
        int32 LeftChildIndex = INDEX_NONE;
        int32 RightChildIndex = INDEX_NONE;
        int32 FirstTriangleIndex = 0;
        int32 TriangleCount = 0;

        bool IsLeaf() const;
    };

    class FBakeProjectionBvh
    {
      public:
        bool Build(
            const FDWCRevealBakeSurface&               OuterSurface,
            const TArray<FDWCRevealBakeSurface>&       SourceSurfaces,
            const FDWCRevealBakeRayProjectionSettings& Settings);

        void ForEachRayCandidate(
            const FVector&                                        RayOrigin,
            const FVector&                                        RayDirection,
            float                                                 MaxDistance,
            TFunctionRef<void(const FBakeProjectionTriangleRef&)> VisitTriangle) const;

      private:
        int32 BuildNode(TArray<int32>& TriangleRefIndices);
        FBox  CalculateBounds(const TArray<int32>& TriangleRefIndices) const;
        FBox  CalculateCenterBounds(const TArray<int32>& TriangleRefIndices) const;
        bool  CanSplit(const TArray<int32>& TriangleRefIndices) const;
        int32 FindLongestAxis(const FBox& Bounds) const;

        TArray<FBakeProjectionTriangleRef> TriangleRefs;
        TArray<FBakeProjectionBvhNode>     Nodes;
        TArray<int32>                      LeafTriangleRefIndices;
    };

    static FVector InterpolateVector(
        const FVector& Barycentric,
        const FVector  Values[3]);

    static FVector2D InterpolateVector2D(
        const FVector&  Barycentric,
        const FVector2D Values[3]);

    static bool IntersectRayAabb(
        const FVector& RayOrigin,
        const FVector& RayDirection,
        const FBox&    Bounds,
        float          MaxDistance);

    static bool IntersectRayTriangle(
        const FVector&                       RayOrigin,
        const FVector&                       RayDirection,
        const FDWCRevealBakeSurfaceTriangle& Triangle,
        float                                MaxDistance,
        float&                               OutDistance,
        FVector&                             OutBarycentric);

    static FDWCRevealBakeRayHit MakeRayHit(
        const FDWCRevealBakeTexelSample& Sample,
        const FCandidateHit&             Candidate,
        float                            MaxRevealDistance);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
