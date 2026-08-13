// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"

#include "Algo/Sort.h"

FVector FDWCRevealBakeTexelSampler::InterpolateVector(const FVector& Barycentric, const FVector Values[3])
{
    return Values[0] * Barycentric.X + Values[1] * Barycentric.Y + Values[2] * Barycentric.Z;
}

FVector FDWCRevealBakeTexelSampler::InterpolateDirection(
    const FVector& Barycentric,
    const FVector3f Values[3])
{
    return FVector(Values[0]) * Barycentric.X +
        FVector(Values[1]) * Barycentric.Y +
        FVector(Values[2]) * Barycentric.Z;
}

FIntRect FDWCRevealBakeTexelSampler::MakePixelBoundsFromUVTriangle(
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const FIntPoint&                     Resolution)
{
    FBox2D UVBounds(ForceInit);
    UVBounds += Triangle.UVs[0];
    UVBounds += Triangle.UVs[1];
    UVBounds += Triangle.UVs[2];

    const int32 MinX = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(UVBounds.Min.X * Resolution.X), 0, Resolution.X - 1));
    const int32 MaxX = IntCastChecked<int32>(FMath::Clamp(FMath::CeilToInt(UVBounds.Max.X * Resolution.X), 0, Resolution.X - 1));
    const int32 MinY = IntCastChecked<int32>(FMath::Clamp(FMath::FloorToInt(UVBounds.Min.Y * Resolution.Y), 0, Resolution.Y - 1));
    const int32 MaxY = IntCastChecked<int32>(FMath::Clamp(FMath::CeilToInt(UVBounds.Max.Y * Resolution.Y), 0, Resolution.Y - 1));

    return FIntRect(MinX, MinY, MaxX + 1, MaxY + 1);
}

int32 FDWCRevealBakeTexelSampler::MakePixelKey(const int32 X, const int32 Y, const int32 Width)
{
    return Y * Width + X;
}

FVector FDWCRevealBakeRayProjector::InterpolateVector(const FVector& Barycentric, const FVector Values[3])
{
    return Values[0] * Barycentric.X + Values[1] * Barycentric.Y + Values[2] * Barycentric.Z;
}

FVector FDWCRevealBakeRayProjector::InterpolateDirection(
    const FVector& Barycentric,
    const FVector3f Values[3])
{
    return FVector(Values[0]) * Barycentric.X +
        FVector(Values[1]) * Barycentric.Y +
        FVector(Values[2]) * Barycentric.Z;
}

FVector2D FDWCRevealBakeRayProjector::InterpolateVector2D(const FVector& Barycentric, const FVector2D Values[3])
{
    return Values[0] * Barycentric.X + Values[1] * Barycentric.Y + Values[2] * Barycentric.Z;
}

bool FDWCRevealBakeRayProjector::IntersectRayAabb(
    const FVector& RayOrigin,
    const FVector& RayDirection,
    const FBox&    Bounds,
    const float    MaxDistance)
{
    if (!Bounds.IsValid || MaxDistance <= 0.0f)
    {
        return false;
    }

    double MinDistance = 0.0;
    double MaxAllowedDistance = MaxDistance;

    for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
    {
        const double Origin = RayOrigin[AxisIndex];
        const double Direction = RayDirection[AxisIndex];
        const double BoundsMin = Bounds.Min[AxisIndex];
        const double BoundsMax = Bounds.Max[AxisIndex];

        if (FMath::Abs(Direction) <= RayIntersectionEpsilon)
        {
            if (Origin < BoundsMin || Origin > BoundsMax)
            {
                return false;
            }
            continue;
        }

        const double InverseDirection = 1.0 / Direction;
        double       AxisMinDistance = (BoundsMin - Origin) * InverseDirection;
        double       AxisMaxDistance = (BoundsMax - Origin) * InverseDirection;
        if (AxisMinDistance > AxisMaxDistance)
        {
            Swap(AxisMinDistance, AxisMaxDistance);
        }

        MinDistance = FMath::Max(MinDistance, AxisMinDistance);
        MaxAllowedDistance = FMath::Min(MaxAllowedDistance, AxisMaxDistance);
        if (MinDistance > MaxAllowedDistance)
        {
            return false;
        }
    }

    return MaxAllowedDistance > RayIntersectionEpsilon;
}

bool FDWCRevealBakeRayProjector::FBakeProjectionBvhNode::IsLeaf() const
{
    return LeftChildIndex == INDEX_NONE && RightChildIndex == INDEX_NONE;
}

bool FDWCRevealBakeRayProjector::FBakeProjectionBvh::Build(
    const FDWCRevealBakeSurface&               OuterSurface,
    const TConstArrayView<FDWCRevealBakeSurface> SourceSurfaces,
    const FDWCRevealBakeRayProjectionSettings& Settings)
{
    TriangleRefs.Reset();
    Nodes.Reset();
    LeafTriangleRefIndices.Reset();

    TArray<int32> RootTriangleRefIndices;
    for (const FDWCRevealBakeSurface& SourceSurface : SourceSurfaces)
    {
        if (Settings.bRespectSourceLayerOrder && SourceSurface.LayerOrder >= OuterSurface.LayerOrder)
        {
            continue;
        }

        if (SourceSurface.Triangles.Num() == 0)
        {
            continue;
        }

        for (const FDWCRevealBakeSurfaceTriangle& Triangle : SourceSurface.Triangles)
        {
            FBakeProjectionTriangleRef TriangleRef;
            TriangleRef.SourceSurface = &SourceSurface;
            TriangleRef.Triangle = &Triangle;
            TriangleRef.Bounds = Triangle.Bounds;
            TriangleRef.Center = Triangle.Bounds.GetCenter();

            const int32 TriangleRefIndex = TriangleRefs.Add(TriangleRef);
            RootTriangleRefIndices.Add(TriangleRefIndex);
        }
    }

    if (RootTriangleRefIndices.Num() == 0)
    {
        return false;
    }

    BuildNode(RootTriangleRefIndices);
    return Nodes.Num() > 0;
}

void FDWCRevealBakeRayProjector::FBakeProjectionBvh::ForEachRayCandidate(
    const FVector&                                        RayOrigin,
    const FVector&                                        RayDirection,
    const float                                           MaxDistance,
    TFunctionRef<void(const FBakeProjectionTriangleRef&)> VisitTriangle) const
{
    if (Nodes.Num() == 0)
    {
        return;
    }

    TArray<int32, TInlineAllocator<64>> NodeStack;
    NodeStack.Add(0);

    while (NodeStack.Num() > 0)
    {
        const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
        if (!Nodes.IsValidIndex(NodeIndex))
        {
            continue;
        }

        const FBakeProjectionBvhNode& Node = Nodes[NodeIndex];
        if (!FDWCRevealBakeRayProjector::IntersectRayAabb(RayOrigin, RayDirection, Node.Bounds, MaxDistance))
        {
            continue;
        }

        if (Node.IsLeaf())
        {
            for (int32 TriangleOffset = 0; TriangleOffset < Node.TriangleCount; ++TriangleOffset)
            {
                const int32 LeafIndex = Node.FirstTriangleIndex + TriangleOffset;
                if (LeafTriangleRefIndices.IsValidIndex(LeafIndex))
                {
                    const int32 TriangleRefIndex = LeafTriangleRefIndices[LeafIndex];
                    if (TriangleRefs.IsValidIndex(TriangleRefIndex))
                    {
                        VisitTriangle(TriangleRefs[TriangleRefIndex]);
                    }
                }
            }
            continue;
        }

        if (Node.LeftChildIndex != INDEX_NONE)
        {
            NodeStack.Add(Node.LeftChildIndex);
        }
        if (Node.RightChildIndex != INDEX_NONE)
        {
            NodeStack.Add(Node.RightChildIndex);
        }
    }
}

int32 FDWCRevealBakeRayProjector::FBakeProjectionBvh::BuildNode(TArray<int32>& TriangleRefIndices)
{
    const int32             NodeIndex = Nodes.AddDefaulted();
    FBakeProjectionBvhNode& Node = Nodes[NodeIndex];
    Node.Bounds = CalculateBounds(TriangleRefIndices);

    if (TriangleRefIndices.Num() <= FDWCRevealBakeRayProjector::BvhLeafTriangleCount || !CanSplit(TriangleRefIndices))
    {
        Node.FirstTriangleIndex = LeafTriangleRefIndices.Num();
        Node.TriangleCount = TriangleRefIndices.Num();
        LeafTriangleRefIndices.Append(TriangleRefIndices);
        return NodeIndex;
    }

    const int32 SplitAxis = FindLongestAxis(CalculateCenterBounds(TriangleRefIndices));
    TriangleRefIndices.Sort(
        [this, SplitAxis](const int32 LeftIndex, const int32 RightIndex)
        {
            return TriangleRefs[LeftIndex].Center[SplitAxis] < TriangleRefs[RightIndex].Center[SplitAxis];
        });

    const int32   SplitIndex = TriangleRefIndices.Num() / 2;
    TArray<int32> LeftTriangleRefIndices;
    TArray<int32> RightTriangleRefIndices;
    LeftTriangleRefIndices.Append(TriangleRefIndices.GetData(), SplitIndex);
    RightTriangleRefIndices.Append(
        TriangleRefIndices.GetData() + SplitIndex,
        TriangleRefIndices.Num() - SplitIndex);

    const int32 LeftChildIndex = BuildNode(LeftTriangleRefIndices);
    const int32 RightChildIndex = BuildNode(RightTriangleRefIndices);
    Nodes[NodeIndex].LeftChildIndex = LeftChildIndex;
    Nodes[NodeIndex].RightChildIndex = RightChildIndex;
    return NodeIndex;
}

FBox FDWCRevealBakeRayProjector::FBakeProjectionBvh::CalculateBounds(const TArray<int32>& TriangleRefIndices) const
{
    FBox Bounds(ForceInit);
    for (const int32 TriangleRefIndex : TriangleRefIndices)
    {
        if (TriangleRefs.IsValidIndex(TriangleRefIndex))
        {
            Bounds += TriangleRefs[TriangleRefIndex].Bounds;
        }
    }
    return Bounds;
}

FBox FDWCRevealBakeRayProjector::FBakeProjectionBvh::CalculateCenterBounds(const TArray<int32>& TriangleRefIndices) const
{
    FBox Bounds(ForceInit);
    for (const int32 TriangleRefIndex : TriangleRefIndices)
    {
        if (TriangleRefs.IsValidIndex(TriangleRefIndex))
        {
            Bounds += TriangleRefs[TriangleRefIndex].Center;
        }
    }
    return Bounds;
}

bool FDWCRevealBakeRayProjector::FBakeProjectionBvh::CanSplit(const TArray<int32>& TriangleRefIndices) const
{
    return CalculateCenterBounds(TriangleRefIndices).GetExtent().GetMax() > KINDA_SMALL_NUMBER;
}

int32 FDWCRevealBakeRayProjector::FBakeProjectionBvh::FindLongestAxis(const FBox& Bounds) const
{
    const FVector Extent = Bounds.GetExtent();
    if (Extent.X >= Extent.Y && Extent.X >= Extent.Z)
    {
        return 0;
    }
    if (Extent.Y >= Extent.Z)
    {
        return 1;
    }
    return 2;
}

bool FDWCRevealBakeTexelSampler::BuildOuterTexelSamples(
    const FDWCRevealBakeSurface&               OuterSurface,
    const FDWCRevealBakeTexelSamplingSettings& Settings,
    TArray<FDWCRevealBakeTexelSample>&         OutSamples,
    FString*                                   OutErrorMessage,
    int32*                                     OutOverlappedPixelCount)
{
    OutSamples.Reset();
    if (OutOverlappedPixelCount != nullptr)
    {
        *OutOverlappedPixelCount = 0;
    }

    if (Settings.Resolution.X <= 0 || Settings.Resolution.Y <= 0)
    {
        SetError(OutErrorMessage, TEXT("Texel sampling resolution must be positive."));
        return false;
    }

    if (OuterSurface.Triangles.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("Outer surface contains no triangles."));
        return false;
    }

    const int32 PixelCount = Settings.Resolution.X * Settings.Resolution.Y;
    TArray<int32> OccupiedPixelSamples;
    OccupiedPixelSamples.Init(INDEX_NONE, PixelCount);
    // Low four bits store the 2x2 coverage mask. The high bit prevents
    // duplicate overlap diagnostics without allocating a second full image.
    constexpr uint8 CoverageMaskBits = 0x0f;
    constexpr uint8 OverlapReportedBit = 0x80;
    TArray<uint8> RasterFlags;
    RasterFlags.Init(0, PixelCount);
    // Do not reserve a fraction of a 4K image: FDWCRevealBakeTexelSample is
    // intentionally rich and that eager reservation can exceed the job budget.
    OutSamples.Reserve(FMath::Min(PixelCount, 64 * 1024));

    for (int32 SurfaceTriangleIndex = 0; SurfaceTriangleIndex < OuterSurface.Triangles.Num(); ++SurfaceTriangleIndex)
    {
        const FDWCRevealBakeSurfaceTriangle& Triangle = OuterSurface.Triangles[SurfaceTriangleIndex];
        if (Settings.MaterialSlotIndex != INDEX_NONE && Triangle.MaterialSlotIndex != Settings.MaterialSlotIndex)
        {
            continue;
        }

        const FIntRect PixelBounds = MakePixelBoundsFromUVTriangle(Triangle, Settings.Resolution);
        if (PixelBounds.Width() <= 0 || PixelBounds.Height() <= 0)
        {
            continue;
        }

        for (int32 Y = PixelBounds.Min.Y; Y < PixelBounds.Max.Y; ++Y)
        {
            for (int32 X = PixelBounds.Min.X; X < PixelBounds.Max.X; ++X)
            {
                const uint8 TriangleCoverageMask = ComputeSubpixelMask(
                    X, Y, Settings.Resolution, Triangle);
                if (TriangleCoverageMask == 0)
                {
                    continue;
                }

                const int32 PixelKey = MakePixelKey(X, Y, Settings.Resolution.X);
                const int32 ExistingSampleIndex = OccupiedPixelSamples[PixelKey];
                if (ExistingSampleIndex != INDEX_NONE)
                {
                    const int32 ExistingTriangleIndex = OutSamples[ExistingSampleIndex].TriangleIndex;
                    const FDWCRevealBakeSurfaceTriangle& ExistingTriangle = OuterSurface.Triangles[ExistingTriangleIndex];
                    bool                                 bSharesVertex = false;
                    for (int32 ExistingCorner = 0; ExistingCorner < 3 && !bSharesVertex; ++ExistingCorner)
                    {
                        for (int32 NewCorner = 0; NewCorner < 3; ++NewCorner)
                        {
                            bSharesVertex |= ExistingTriangle.VertexIndices[ExistingCorner] == Triangle.VertexIndices[NewCorner];
                        }
                    }
                    if (!bSharesVertex)
                    {
                        if (OutOverlappedPixelCount != nullptr &&
                            (RasterFlags[PixelKey] & OverlapReportedBit) == 0)
                        {
                            RasterFlags[PixelKey] |= OverlapReportedBit;
                            ++(*OutOverlappedPixelCount);
                        }
                        continue;
                    }

                    RasterFlags[PixelKey] |= TriangleCoverageMask;
                    continue;
                }

                FDWCRevealBakeTexelSample& Sample = OutSamples.AddDefaulted_GetRef();
                Sample.Pixel = FIntPoint(X, Y);
                // During rasterization this is the surface-array index. It is
                // replaced with the persistent triangle ID in the final pass.
                Sample.TriangleIndex = SurfaceTriangleIndex;
                OccupiedPixelSamples[PixelKey] = OutSamples.Num() - 1;
                RasterFlags[PixelKey] |= TriangleCoverageMask;
            }
        }
    }

    constexpr uint8 SubsampleCountByMask[] =
    {
        0, 1, 1, 2, 1, 2, 2, 3,
        1, 2, 2, 3, 2, 3, 3, 4
    };
    constexpr uint8 CoverageBySubsampleCount[] = { 0, 64, 128, 191, 255 };
    for (FDWCRevealBakeTexelSample& Sample : OutSamples)
    {
        const int32 SurfaceTriangleIndex = Sample.TriangleIndex;
        const int32 X = Sample.Pixel.X;
        const int32 Y = Sample.Pixel.Y;
        const int32 PixelKey = MakePixelKey(X, Y, Settings.Resolution.X);
        const FDWCRevealBakeSurfaceTriangle& Triangle = OuterSurface.Triangles[SurfaceTriangleIndex];
        FVector2D UV;
        FVector Barycentric;
        if (!ResolveRepresentativeSample(X, Y, Settings.Resolution, Triangle, UV, Barycentric))
        {
            SetError(OutErrorMessage, TEXT("A covered outer texel has no representative surface point."));
            OutSamples.Reset();
            return false;
        }

        const uint8 CoverageMask = RasterFlags[PixelKey] & CoverageMaskBits;
        const uint8 CoveredSubsampleCount = SubsampleCountByMask[CoverageMask];

        Sample.UV = UV;
        Sample.Coverage = CoverageBySubsampleCount[CoveredSubsampleCount];
        Sample.Position = InterpolateVector(Barycentric, Triangle.Positions);
        Sample.Normal = InterpolateDirection(Barycentric, Triangle.Normals).GetSafeNormal();
        Sample.TriangleIndex = Triangle.TriangleIndex;
        Sample.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        Sample.UVIslandID = Triangle.UVIslandID;
        Sample.Barycentric = Barycentric;
    }

    if (OutSamples.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("No outer texel samples were generated."));
        return false;
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FDWCRevealBakeTexelSampler::ComputeBarycentricInUV(
    const FVector2D&                     UV,
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    FVector&                             OutBarycentric)
{
    const FVector2D A = Triangle.UVs[0];
    const FVector2D B = Triangle.UVs[1];
    const FVector2D C = Triangle.UVs[2];

    const FVector2D V0 = B - A;
    const FVector2D V1 = C - A;
    const FVector2D V2 = UV - A;

    const double Denominator = V0.X * V1.Y - V1.X * V0.Y;
    if (FMath::IsNearlyZero(Denominator, RayIntersectionEpsilon))
    {
        return false;
    }

    const double BaryB = (V2.X * V1.Y - V1.X * V2.Y) / Denominator;
    const double BaryC = (V0.X * V2.Y - V2.X * V0.Y) / Denominator;
    const double BaryA = 1.0 - BaryB - BaryC;

    if (BaryA < BarycentricTolerance || BaryB < BarycentricTolerance || BaryC < BarycentricTolerance)
    {
        return false;
    }

    OutBarycentric = FVector(BaryA, BaryB, BaryC);
    return true;
}

uint8 FDWCRevealBakeTexelSampler::ComputeSubpixelMask(
    const int32 X,
    const int32 Y,
    const FIntPoint& Resolution,
    const FDWCRevealBakeSurfaceTriangle& Triangle)
{
    constexpr double SubpixelOffsets[2] = { 0.25, 0.75 };
    uint8 Mask = 0;
    uint8 Bit = 1;
    for (const double OffsetY : SubpixelOffsets)
    {
        for (const double OffsetX : SubpixelOffsets)
        {
            const FVector2D UV(
                (static_cast<double>(X) + OffsetX) / static_cast<double>(Resolution.X),
                (static_cast<double>(Y) + OffsetY) / static_cast<double>(Resolution.Y));
            FVector Barycentric;
            if (ComputeBarycentricInUV(UV, Triangle, Barycentric))
            {
                Mask |= Bit;
            }
            Bit <<= 1;
        }
    }
    return Mask;
}

bool FDWCRevealBakeTexelSampler::ResolveRepresentativeSample(
    const int32 X,
    const int32 Y,
    const FIntPoint& Resolution,
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    FVector2D& OutUV,
    FVector& OutBarycentric)
{
    OutUV = FVector2D(
        (static_cast<double>(X) + 0.5) / static_cast<double>(Resolution.X),
        (static_cast<double>(Y) + 0.5) / static_cast<double>(Resolution.Y));
    if (ComputeBarycentricInUV(OutUV, Triangle, OutBarycentric))
    {
        return true;
    }

    constexpr double SubpixelOffsets[2] = { 0.25, 0.75 };
    FVector2D UVSum = FVector2D::ZeroVector;
    int32 ValidSubsampleCount = 0;
    for (const double OffsetY : SubpixelOffsets)
    {
        for (const double OffsetX : SubpixelOffsets)
        {
            const FVector2D CandidateUV(
                (static_cast<double>(X) + OffsetX) / static_cast<double>(Resolution.X),
                (static_cast<double>(Y) + OffsetY) / static_cast<double>(Resolution.Y));
            FVector CandidateBarycentric;
            if (ComputeBarycentricInUV(CandidateUV, Triangle, CandidateBarycentric))
            {
                UVSum += CandidateUV;
                ++ValidSubsampleCount;
            }
        }
    }

    if (ValidSubsampleCount == 0)
    {
        return false;
    }

    OutUV = UVSum / static_cast<double>(ValidSubsampleCount);
    return ComputeBarycentricInUV(OutUV, Triangle, OutBarycentric);
}

void FDWCRevealBakeTexelSampler::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    if (OutErrorMessage != nullptr)
    {
        *OutErrorMessage = InMessage;
    }
}

bool FDWCRevealBakeRayProjector::ProjectSamplesToSources(
    const FDWCRevealBakeSurface&                    OuterSurface,
    const TConstArrayView<FDWCRevealBakeSurface>    SourceSurfaces,
    const TArray<FDWCRevealBakeTexelSample>&        Samples,
    const FDWCRevealBakeRayProjectionSettings&      Settings,
    TFunctionRef<void(const FDWCRevealBakeRayHit&)> ConsumeHit,
    FString*                                        OutErrorMessage,
    const FDWCEditorCancellationToken*              CancellationToken,
    const TConstArrayView<int32>                    SampleIndices,
    const FDWCRevealBakeProjectionProgressCallback* ProgressCallback)
{
    if (Samples.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("No texel samples were provided for ray projection."));
        return false;
    }

    if (SourceSurfaces.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("No source surfaces were provided for ray projection."));
        return false;
    }

    const float MaxRevealDistance = FMath::Max(0.0f, OuterSurface.MaxRevealDistance * FMath::Max(Settings.RayLengthScale, 0.0f));
    if (MaxRevealDistance <= 0.0f)
    {
        SetError(OutErrorMessage, TEXT("Max reveal distance must be positive."));
        return false;
    }

    FBakeProjectionBvh ProjectionBvh;
    if (!ProjectionBvh.Build(OuterSurface, SourceSurfaces, Settings))
    {
        SetError(OutErrorMessage, TEXT("No eligible source triangles were available for ray projection."));
        return false;
    }

    const int32 ProjectionSampleCount = SampleIndices.IsEmpty()
        ? Samples.Num()
        : SampleIndices.Num();
    if (ProgressCallback != nullptr)
    {
        (*ProgressCallback)(0, ProjectionSampleCount);
    }
    for (int32 ProjectionSampleIndex = 0; ProjectionSampleIndex < ProjectionSampleCount;
         ++ProjectionSampleIndex)
    {
        if ((ProjectionSampleIndex & 255) == 0 &&
            CancellationToken != nullptr &&
            CancellationToken->IsCanceled())
        {
            SetError(OutErrorMessage, TEXT("Transparency ray projection was canceled."));
            return false;
        }
        if (ProgressCallback != nullptr &&
            ProjectionSampleIndex > 0 &&
            (ProjectionSampleIndex & 2047) == 0)
        {
            (*ProgressCallback)(ProjectionSampleIndex, ProjectionSampleCount);
        }
        const int32 SampleIndex = SampleIndices.IsEmpty()
            ? ProjectionSampleIndex
            : SampleIndices[ProjectionSampleIndex];
        if (!Samples.IsValidIndex(SampleIndex))
        {
            SetError(OutErrorMessage, TEXT("A transparency projection sample index is invalid."));
            return false;
        }
        const FDWCRevealBakeTexelSample& Sample = Samples[SampleIndex];
        const FVector RayDirection = -Sample.Normal.GetSafeNormal();
        const FVector RayOrigin = Sample.Position + RayDirection * Settings.RayStartOffset;

        FCandidateHit BestRevealCandidate;
        FCandidateHit BestBlockerCandidate;
        bool          bHasRevealCandidate = false;
        bool          bHasBlockerCandidate = false;

        const auto IsPreferredCandidate =
            [&Settings](const FCandidateHit& Candidate, const FCandidateHit& Current)
        {
            if (Settings.bPreferLowerSourceLayerOrder &&
                Candidate.SourceSurface != nullptr &&
                Current.SourceSurface != nullptr &&
                Candidate.SourceSurface->LayerOrder != Current.SourceSurface->LayerOrder)
            {
                return Candidate.SourceSurface->LayerOrder < Current.SourceSurface->LayerOrder;
            }
            return Candidate.Distance < Current.Distance;
        };

        ProjectionBvh.ForEachRayCandidate(
            RayOrigin,
            RayDirection,
            MaxRevealDistance,
            [&](const FBakeProjectionTriangleRef& TriangleRef)
            {
                if (TriangleRef.SourceSurface == nullptr || TriangleRef.Triangle == nullptr)
                {
                    return;
                }

                float   Distance = 0.0f;
                FVector Barycentric;
                if (!IntersectRayTriangle(
                        RayOrigin,
                        RayDirection,
                        *TriangleRef.Triangle,
                        MaxRevealDistance,
                        Distance,
                        Barycentric))
                {
                    return;
                }

                if (Distance < FMath::Max(0.0f, Settings.MinHitDistance))
                {
                    return;
                }

                if (Settings.bRespectPerSourceMaxDistance &&
                    Distance > FMath::Max(0.0f, TriangleRef.SourceSurface->MaxRevealDistance))
                {
                    return;
                }

                FCandidateHit Candidate;
                Candidate.SourceSurface = TriangleRef.SourceSurface;
                Candidate.Triangle = TriangleRef.Triangle;
                Candidate.Barycentric = Barycentric;
                Candidate.Distance = Distance;

                if (TriangleRef.SourceSurface->bCanBeRevealSource)
                {
                    if (!bHasRevealCandidate ||
                        IsPreferredCandidate(Candidate, BestRevealCandidate))
                    {
                        BestRevealCandidate = Candidate;
                        bHasRevealCandidate = true;
                    }
                    return;
                }

                if (Settings.bRespectBlockers &&
                    TriangleRef.SourceSurface->bBlocksReveal &&
                    (!bHasBlockerCandidate ||
                     IsPreferredCandidate(Candidate, BestBlockerCandidate)))
                {
                    BestBlockerCandidate = Candidate;
                    bHasBlockerCandidate = true;
                }
            });

        FDWCRevealBakeRayHit SelectedHit;
        SelectedHit.Pixel = Sample.Pixel;

        const bool bBlocked =
            bHasBlockerCandidate &&
            (!bHasRevealCandidate ||
             IsPreferredCandidate(BestBlockerCandidate, BestRevealCandidate));
        SelectedHit.bBlocked = bBlocked;
        if (bBlocked)
        {
            SelectedHit.Distance = BestBlockerCandidate.Distance;
            SelectedHit.SourceLayerId = BestBlockerCandidate.SourceSurface != nullptr
                ? BestBlockerCandidate.SourceSurface->LayerId
                : NAME_None;
        }
        if (bHasRevealCandidate && !bBlocked)
        {
            SelectedHit = MakeRayHit(Sample, BestRevealCandidate, MaxRevealDistance);
            if (Settings.bUseNormalAlignmentConfidence)
            {
                SelectedHit.Confidence = FMath::Clamp(
                    static_cast<float>(FVector::DotProduct(
                        Sample.Normal.GetSafeNormal(),
                        SelectedHit.Normal.GetSafeNormal())),
                    0.0f,
                    1.0f);
            }
        }

        ConsumeHit(SelectedHit);
    }

    if (ProgressCallback != nullptr)
    {
        (*ProgressCallback)(ProjectionSampleCount, ProjectionSampleCount);
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FDWCRevealBakeRayProjector::IntersectRayTriangle(
    const FVector&                       RayOrigin,
    const FVector&                       RayDirection,
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const float                          MaxDistance,
    float&                               OutDistance,
    FVector&                             OutBarycentric)
{
    const FVector Edge1 = Triangle.Positions[1] - Triangle.Positions[0];
    const FVector Edge2 = Triangle.Positions[2] - Triangle.Positions[0];
    const FVector P = FVector::CrossProduct(RayDirection, Edge2);
    const double  Determinant = FVector::DotProduct(Edge1, P);

    if (FMath::Abs(Determinant) < RayIntersectionEpsilon)
    {
        return false;
    }

    const double  InverseDeterminant = 1.0 / Determinant;
    const FVector T = RayOrigin - Triangle.Positions[0];
    const double  U = FVector::DotProduct(T, P) * InverseDeterminant;
    if (U < 0.0 || U > 1.0)
    {
        return false;
    }

    const FVector Q = FVector::CrossProduct(T, Edge1);
    const double  V = FVector::DotProduct(RayDirection, Q) * InverseDeterminant;
    if (V < 0.0 || U + V > 1.0)
    {
        return false;
    }

    const double Distance = FVector::DotProduct(Edge2, Q) * InverseDeterminant;
    if (Distance <= RayIntersectionEpsilon || Distance > MaxDistance)
    {
        return false;
    }

    OutDistance = static_cast<float>(Distance);
    OutBarycentric = FVector(1.0 - U - V, U, V);
    return true;
}

FDWCRevealBakeRayHit FDWCRevealBakeRayProjector::MakeRayHit(
    const FDWCRevealBakeTexelSample& Sample,
    const FCandidateHit&             Candidate,
    const float                      MaxRevealDistance)
{
    FDWCRevealBakeRayHit Hit;
    Hit.bHit = true;
    Hit.Pixel = Sample.Pixel;
    Hit.SourceLayerId = Candidate.SourceSurface != nullptr ? Candidate.SourceSurface->LayerId : NAME_None;
    Hit.OuterTriangleIndex = Sample.TriangleIndex;
    Hit.OuterBarycentric = Sample.Barycentric;
    Hit.SourceTriangleIndex = Candidate.Triangle != nullptr ? Candidate.Triangle->TriangleIndex : INDEX_NONE;
    Hit.SourceMaterialSlotIndex = Candidate.Triangle != nullptr ? Candidate.Triangle->MaterialSlotIndex : INDEX_NONE;
    Hit.Distance = Candidate.Distance;
    Hit.Confidence = MaxRevealDistance > 0.0f ? FMath::Clamp(1.0f - Candidate.Distance / MaxRevealDistance, 0.0f, 1.0f) : 0.0f;

    if (Candidate.Triangle != nullptr)
    {
        Hit.Position = InterpolateVector(Candidate.Barycentric, Candidate.Triangle->Positions);
        Hit.Normal = InterpolateDirection(
            Candidate.Barycentric,
            Candidate.Triangle->Normals).GetSafeNormal();
        Hit.SourceUV = InterpolateVector2D(Candidate.Barycentric, Candidate.Triangle->UVs);
        Hit.SourceBarycentric = Candidate.Barycentric;
    }

    return Hit;
}

void FDWCRevealBakeRayProjector::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    if (OutErrorMessage != nullptr)
    {
        *OutErrorMessage = InMessage;
    }
}
