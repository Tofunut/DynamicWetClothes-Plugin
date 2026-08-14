// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"

class FDWCEditorCancellationToken;

using FDWCRevealBakeProjectionProgressCallback = TFunction<void(int32, int32)>;

struct FDWCRevealBakeTexelSample
{
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FVector2D UV = FVector2D::ZeroVector;
    /** Fraction of the target texel covered by the outer surface, encoded as 0..255. */
    uint8     Coverage = 255;
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
    /** A blocker won this priority layer. Lower-priority reveal sources must not be considered. */
    bool      bBlocked = false;
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FName     SourceLayerId = NAME_None;
    int32     SourceTriangleIndex = INDEX_NONE;
    int32     SourceMaterialSlotIndex = INDEX_NONE;
    FVector   Position = FVector::ZeroVector;
    FVector   Normal = FVector::UpVector;
    FVector2D SourceUV = FVector2D::ZeroVector;
    int32     OuterTriangleIndex = INDEX_NONE;
    FVector   OuterBarycentric = FVector::ZeroVector;
    FVector   SourceBarycentric = FVector::ZeroVector;
    float     Distance = 0.0f;
    float     Confidence = 0.0f;
};

struct FDWCRevealBakeTexelSamplingSettings
{
    FIntPoint Resolution = FIntPoint(1024, 1024);
    int32     MaterialSlotIndex = INDEX_NONE;
};

struct FDWCRevealBakeTile
{
    FIntRect Rect;
    /** Surface-array triangle indices in deterministic source order. */
    TArray<int32> SurfaceTriangleIndices;

    uint64 GetAllocatedBytes() const
    {
        return sizeof(FDWCRevealBakeTile) + SurfaceTriangleIndices.GetAllocatedSize();
    }
};

/** Immutable target-space partition reused by every streamed source pass. */
struct FDWCRevealBakeTileLayout
{
    FIntPoint Resolution = FIntPoint::ZeroValue;
    int32 TileSize = 0;
    int32 TilesX = 0;
    int32 TilesY = 0;
    TArray<FDWCRevealBakeTile> Tiles;
    /** Single-copy fallback used when per-tile references would exceed the cap. */
    TArray<int32> EligibleSurfaceTriangleIndices;
    bool bUsesPerTileTriangleBins = true;

    bool IsValid() const;
    uint64 GetAllocatedBytes() const;
    uint64 EstimateMaximumScratchBytes() const;
};

/** Mutable bounded workspace. Capacity is retained and reused between tiles. */
struct FDWCRevealBakeTileScratch
{
    TArray<int32> OccupiedPixelSamples;
    TArray<uint8> RasterFlags;
    TArray<FDWCRevealBakeTexelSample> Samples;

    void Reset();
    uint64 GetAllocatedBytes() const;
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
    static bool BuildTileLayout(
        const FDWCRevealBakeSurface&               OuterSurface,
        const FDWCRevealBakeTexelSamplingSettings& Settings,
        const TSet<int32>&                         EligibleTriangleIDs,
        int32                                      TileSize,
        FDWCRevealBakeTileLayout&                  OutLayout,
        FString*                                   OutErrorMessage = nullptr);

    /** Rasterizes one tile into reusable scratch. Empty tiles are valid. */
    static bool BuildTileSamples(
        const FDWCRevealBakeSurface&               OuterSurface,
        const FDWCRevealBakeTexelSamplingSettings& Settings,
        const FDWCRevealBakeTileLayout&            Layout,
        int32                                      TileIndex,
        FDWCRevealBakeTileScratch&                 InOutScratch,
        FString*                                   OutErrorMessage = nullptr,
        int32*                                     OutOverlappedPixelCount = nullptr);

    static bool BuildOuterTexelSamples(
        const FDWCRevealBakeSurface&               OuterSurface,
        const FDWCRevealBakeTexelSamplingSettings& Settings,
        TArray<FDWCRevealBakeTexelSample>&         OutSamples,
        FString*                                   OutErrorMessage = nullptr,
        int32*                                     OutOverlappedPixelCount = nullptr);

    /**
     * Rasterizes only the persistent paint-domain mask required when restoring
     * a baked Stage 4 baseline. Unlike BuildOuterTexelSamples, this does not
     * retain position, normal, or barycentric data for every covered texel.
     */
    static bool BuildOuterTexelMaskBuffers(
        const FDWCRevealBakeSurface&               OuterSurface,
        const FDWCRevealBakeTexelSamplingSettings& Settings,
        const TSet<int32>&                         EligibleTriangleIDs,
        TArray<uint8>&                             OutCoverage,
        TArray<int32>&                             OutUVIslandIDs,
        int32&                                     OutCoveredPixelCount,
        FString*                                   OutErrorMessage = nullptr,
        int32*                                     OutOverlappedPixelCount = nullptr);

  private:
    static constexpr double BarycentricTolerance = -1.0e-6;
    static constexpr double RayIntersectionEpsilon = 1.0e-8;

    static FVector InterpolateVector(
        const FVector& Barycentric,
        const FVector  Values[3]);

    static FVector InterpolateDirection(
        const FVector&  Barycentric,
        const FVector3f Values[3]);

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

    static uint8 ComputeSubpixelMask(
        int32                                    X,
        int32                                    Y,
        const FIntPoint&                         Resolution,
        const FDWCRevealBakeSurfaceTriangle&     Triangle);

    static bool ResolveRepresentativeSample(
        int32                                    X,
        int32                                    Y,
        const FIntPoint&                         Resolution,
        const FDWCRevealBakeSurfaceTriangle&     Triangle,
        FVector2D&                               OutUV,
        FVector&                                 OutBarycentric);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};

class FDWCRevealBakeRayProjector
{
  public:
    class FPreparedProjection
    {
      public:
        FPreparedProjection();
        ~FPreparedProjection();
        FPreparedProjection(FPreparedProjection&&);
        FPreparedProjection& operator=(FPreparedProjection&&);

        bool IsValid() const;
        uint64 GetAllocatedBytes() const;

      private:
        struct FImpl;
        TUniquePtr<FImpl> Impl;
        friend class FDWCRevealBakeRayProjector;
    };

    static TUniquePtr<FPreparedProjection> PrepareProjection(
        const FDWCRevealBakeSurface&               OuterSurface,
        TConstArrayView<FDWCRevealBakeSurface>      SourceSurfaces,
        const FDWCRevealBakeRayProjectionSettings& Settings,
        FString*                                   OutErrorMessage = nullptr);

    static bool ProjectPreparedSamples(
        const FPreparedProjection&                      PreparedProjection,
        const TArray<FDWCRevealBakeTexelSample>&        Samples,
        TFunctionRef<void(const FDWCRevealBakeRayHit&)> ConsumeHit,
        FString*                                        OutErrorMessage = nullptr,
        const FDWCEditorCancellationToken*              CancellationToken = nullptr,
        TConstArrayView<int32>                          SampleIndices = {},
        const FDWCRevealBakeProjectionProgressCallback* ProgressCallback = nullptr);

    static bool ProjectSamplesToSources(
        const FDWCRevealBakeSurface&                    OuterSurface,
        TConstArrayView<FDWCRevealBakeSurface>           SourceSurfaces,
        const TArray<FDWCRevealBakeTexelSample>&        Samples,
        const FDWCRevealBakeRayProjectionSettings&      Settings,
        TFunctionRef<void(const FDWCRevealBakeRayHit&)> ConsumeHit,
        FString*                                        OutErrorMessage = nullptr,
        const FDWCEditorCancellationToken*              CancellationToken = nullptr,
        /** Empty means every sample; otherwise only these sample indices are projected. */
        TConstArrayView<int32>                           SampleIndices = {},
        const FDWCRevealBakeProjectionProgressCallback* ProgressCallback = nullptr);

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
            TConstArrayView<FDWCRevealBakeSurface>      SourceSurfaces,
            const FDWCRevealBakeRayProjectionSettings& Settings);

        uint64 GetAllocatedBytes() const;

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

    static FVector InterpolateDirection(
        const FVector&  Barycentric,
        const FVector3f Values[3]);

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
