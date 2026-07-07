#pragma once

#include "CoreMinimal.h"
#include "Bake/DWCBakeSurface.h"

struct DWC_API FDWCBakeTexelSample
{
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FVector2D UV = FVector2D::ZeroVector;
    FVector Position = FVector::ZeroVector;
    FVector Normal = FVector::UpVector;
    int32 TriangleIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FVector Barycentric = FVector::ZeroVector;
};

struct DWC_API FDWCBakeRayHit
{
    bool bHit = false;
    FIntPoint Pixel = FIntPoint::ZeroValue;
    FName SourceLayerId = NAME_None;
    int32 SourceTriangleIndex = INDEX_NONE;
    int32 SourceMaterialSlotIndex = INDEX_NONE;
    FVector Position = FVector::ZeroVector;
    FVector Normal = FVector::UpVector;
    FVector2D SourceUV = FVector2D::ZeroVector;
    float Distance = 0.0f;
    float Confidence = 0.0f;
};

struct DWC_API FDWCBakeTexelSamplingSettings
{
    FIntPoint Resolution = FIntPoint(8192, 8192);
    int32 MaterialSlotIndex = INDEX_NONE;
};

struct DWC_API FDWCBakeRayProjectionSettings
{
    float RayStartOffset = 0.01f;
    float RayLengthScale = 1.0f;
    bool bRespectSourceLayerOrder = true;
    bool bRespectBlockers = true;
};

class DWC_API FDWCBakeTexelSampler
{
  public:
    static bool BuildOuterTexelSamples(
        const FDWCBakeSurface&               OuterSurface,
        const FDWCBakeTexelSamplingSettings& Settings,
        TArray<FDWCBakeTexelSample>&         OutSamples,
        FString*                             OutErrorMessage = nullptr);

  private:
    static constexpr double BarycentricTolerance = -1.0e-6;
    static constexpr double RayIntersectionEpsilon = 1.0e-8;

    static FVector InterpolateVector(
        const FVector& Barycentric,
        const FVector  Values[3]);

    static FIntRect MakePixelBoundsFromUVTriangle(
        const FDWCBakeSurfaceTriangle& Triangle,
        const FIntPoint&               Resolution);

    static int32 MakePixelKey(
        int32 X,
        int32 Y,
        int32 Width);

    static bool ComputeBarycentricInUV(
        const FVector2D&                 UV,
        const FDWCBakeSurfaceTriangle&   Triangle,
        FVector&                         OutBarycentric);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};

class DWC_API FDWCBakeRayProjector
{
  public:
    static bool ProjectSamplesToSources(
        const FDWCBakeSurface&                  OuterSurface,
        const TArray<FDWCBakeSurface>&          SourceSurfaces,
        const TArray<FDWCBakeTexelSample>&      Samples,
        const FDWCBakeRayProjectionSettings&    Settings,
        TArray<FDWCBakeRayHit>&                 OutHits,
        FString*                                OutErrorMessage = nullptr);

  private:
    static constexpr double RayIntersectionEpsilon = 1.0e-8;
    static constexpr int32 BvhLeafTriangleCount = 8;

    struct FCandidateHit
    {
        const FDWCBakeSurface* SourceSurface = nullptr;
        const FDWCBakeSurfaceTriangle* Triangle = nullptr;
        FVector Barycentric = FVector::ZeroVector;
        FVector Position = FVector::ZeroVector;
        FVector Normal = FVector::UpVector;
        float Distance = 0.0f;
    };

    struct FBakeProjectionTriangleRef
    {
        const FDWCBakeSurface* SourceSurface = nullptr;
        const FDWCBakeSurfaceTriangle* Triangle = nullptr;
        FBox Bounds = FBox(ForceInit);
        FVector Center = FVector::ZeroVector;
    };

    struct FBakeProjectionBvhNode
    {
        FBox Bounds = FBox(ForceInit);
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
            const FDWCBakeSurface&              OuterSurface,
            const TArray<FDWCBakeSurface>&      SourceSurfaces,
            const FDWCBakeRayProjectionSettings& Settings);

        void QueryRay(
            const FVector& RayOrigin,
            const FVector& RayDirection,
            float          MaxDistance,
            TArray<int32>& OutTriangleRefIndices) const;

        const FBakeProjectionTriangleRef* GetTriangleRef(int32 TriangleRefIndex) const;

      private:
        int32 BuildNode(TArray<int32>& TriangleRefIndices);
        FBox CalculateBounds(const TArray<int32>& TriangleRefIndices) const;
        FBox CalculateCenterBounds(const TArray<int32>& TriangleRefIndices) const;
        bool CanSplit(const TArray<int32>& TriangleRefIndices) const;
        int32 FindLongestAxis(const FBox& Bounds) const;

        TArray<FBakeProjectionTriangleRef> TriangleRefs;
        TArray<FBakeProjectionBvhNode> Nodes;
        TArray<int32> LeafTriangleRefIndices;
    };

    static FVector InterpolateVector(
        const FVector& Barycentric,
        const FVector  Values[3]);

    static FVector2D InterpolateVector2D(
        const FVector&   Barycentric,
        const FVector2D  Values[3]);

    static bool IntersectRayAabb(
        const FVector& RayOrigin,
        const FVector& RayDirection,
        const FBox&    Bounds,
        float          MaxDistance);

    static bool IntersectRayTriangle(
        const FVector&                  RayOrigin,
        const FVector&                  RayDirection,
        const FDWCBakeSurfaceTriangle&  Triangle,
        float                           MaxDistance,
        float&                          OutDistance,
        FVector&                        OutBarycentric);

    static FDWCBakeRayHit MakeRayHit(
        const FDWCBakeTexelSample& Sample,
        const FCandidateHit&       Candidate,
        float                      MaxRevealDistance);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
