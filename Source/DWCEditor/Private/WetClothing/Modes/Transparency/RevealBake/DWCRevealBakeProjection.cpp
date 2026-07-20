#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeProjection.h"

#include "Algo/Sort.h"

FVector FDWCRevealBakeTexelSampler::InterpolateVector(const FVector& Barycentric, const FVector Values[3])
{
    return Values[0] * Barycentric.X + Values[1] * Barycentric.Y + Values[2] * Barycentric.Z;
}

FIntRect FDWCRevealBakeTexelSampler::MakePixelBoundsFromUVTriangle(
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const FIntPoint&               Resolution)
{
    FBox2D UVBounds(ForceInit);
    UVBounds += Triangle.UVs[0];
    UVBounds += Triangle.UVs[1];
    UVBounds += Triangle.UVs[2];

    const int32 MinX = FMath::Clamp(FMath::FloorToInt(UVBounds.Min.X * Resolution.X), 0, Resolution.X - 1);
    const int32 MaxX = FMath::Clamp(FMath::CeilToInt(UVBounds.Max.X * Resolution.X), 0, Resolution.X - 1);
    const int32 MinY = FMath::Clamp(FMath::FloorToInt(UVBounds.Min.Y * Resolution.Y), 0, Resolution.Y - 1);
    const int32 MaxY = FMath::Clamp(FMath::CeilToInt(UVBounds.Max.Y * Resolution.Y), 0, Resolution.Y - 1);

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
        double AxisMinDistance = (BoundsMin - Origin) * InverseDirection;
        double AxisMaxDistance = (BoundsMax - Origin) * InverseDirection;
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
    const TArray<FDWCRevealBakeSurface>&       SourceSurfaces,
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

void FDWCRevealBakeRayProjector::FBakeProjectionBvh::QueryRay(
    const FVector& RayOrigin,
    const FVector& RayDirection,
    const float    MaxDistance,
    TArray<int32>& OutTriangleRefIndices) const
{
    OutTriangleRefIndices.Reset();
    if (Nodes.Num() == 0)
    {
        return;
    }

    TArray<int32> NodeStack;
    NodeStack.Reserve(64);
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
                    OutTriangleRefIndices.Add(LeafTriangleRefIndices[LeafIndex]);
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

const FDWCRevealBakeRayProjector::FBakeProjectionTriangleRef* FDWCRevealBakeRayProjector::FBakeProjectionBvh::GetTriangleRef(
    const int32 TriangleRefIndex) const
{
    return TriangleRefs.IsValidIndex(TriangleRefIndex) ? &TriangleRefs[TriangleRefIndex] : nullptr;
}

int32 FDWCRevealBakeRayProjector::FBakeProjectionBvh::BuildNode(TArray<int32>& TriangleRefIndices)
{
    const int32 NodeIndex = Nodes.AddDefaulted();
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

    const int32 SplitIndex = TriangleRefIndices.Num() / 2;
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

    TArray<int32> OccupiedPixelTriangles;
    OccupiedPixelTriangles.Init(INDEX_NONE, Settings.Resolution.X * Settings.Resolution.Y);
    TArray<uint8> OverlappedPixels;
    if (OutOverlappedPixelCount != nullptr)
    {
        OverlappedPixels.Init(0, Settings.Resolution.X * Settings.Resolution.Y);
    }
    OutSamples.Reserve(Settings.Resolution.X * Settings.Resolution.Y / 2);

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
                const int32 PixelKey = MakePixelKey(X, Y, Settings.Resolution.X);
                const int32 ExistingTriangleIndex = OccupiedPixelTriangles[PixelKey];
                if (ExistingTriangleIndex != INDEX_NONE)
                {
                    const FDWCRevealBakeSurfaceTriangle& ExistingTriangle = OuterSurface.Triangles[ExistingTriangleIndex];
                    bool bSharesVertex = false;
                    for (int32 ExistingCorner = 0; ExistingCorner < 3 && !bSharesVertex; ++ExistingCorner)
                    {
                        for (int32 NewCorner = 0; NewCorner < 3; ++NewCorner)
                        {
                            bSharesVertex |= ExistingTriangle.VertexIndices[ExistingCorner] == Triangle.VertexIndices[NewCorner];
                        }
                    }
                    if (!bSharesVertex && OutOverlappedPixelCount != nullptr && OverlappedPixels[PixelKey] == 0)
                    {
                        OverlappedPixels[PixelKey] = 1;
                        ++(*OutOverlappedPixelCount);
                    }
                    continue;
                }

                const FVector2D UV(
                    (static_cast<double>(X) + 0.5) / static_cast<double>(Settings.Resolution.X),
                    (static_cast<double>(Y) + 0.5) / static_cast<double>(Settings.Resolution.Y));

                FVector Barycentric;
                if (!ComputeBarycentricInUV(UV, Triangle, Barycentric))
                {
                    continue;
                }

                FDWCRevealBakeTexelSample Sample;
                Sample.Pixel = FIntPoint(X, Y);
                Sample.UV = UV;
                Sample.Position = InterpolateVector(Barycentric, Triangle.Positions);
                Sample.Normal = InterpolateVector(Barycentric, Triangle.Normals).GetSafeNormal();
                Sample.TriangleIndex = Triangle.TriangleIndex;
                Sample.MaterialSlotIndex = Triangle.MaterialSlotIndex;
                Sample.Barycentric = Barycentric;

                OccupiedPixelTriangles[PixelKey] = SurfaceTriangleIndex;
                OutSamples.Add(Sample);
            }
        }
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
    const FVector2D&               UV,
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    FVector&                       OutBarycentric)
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

void FDWCRevealBakeTexelSampler::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    if (OutErrorMessage != nullptr)
    {
        *OutErrorMessage = InMessage;
    }
}

bool FDWCRevealBakeRayProjector::ProjectSamplesToSources(
    const FDWCRevealBakeSurface&               OuterSurface,
    const TArray<FDWCRevealBakeSurface>&       SourceSurfaces,
    const TArray<FDWCRevealBakeTexelSample>&   Samples,
    const FDWCRevealBakeRayProjectionSettings& Settings,
    TArray<FDWCRevealBakeRayHit>&              OutHits,
    FString*                             OutErrorMessage)
{
    OutHits.Reset();

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

    OutHits.Reserve(Samples.Num());

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

    TArray<int32> CandidateTriangleRefIndices;
    for (const FDWCRevealBakeTexelSample& Sample : Samples)
    {
        const FVector RayDirection = -Sample.Normal.GetSafeNormal();
        const FVector RayOrigin = Sample.Position + RayDirection * Settings.RayStartOffset;

        TArray<FCandidateHit> CandidateHits;
        ProjectionBvh.QueryRay(RayOrigin, RayDirection, MaxRevealDistance, CandidateTriangleRefIndices);
        for (const int32 TriangleRefIndex : CandidateTriangleRefIndices)
        {
            const FBakeProjectionTriangleRef* TriangleRef = ProjectionBvh.GetTriangleRef(TriangleRefIndex);
            if (TriangleRef == nullptr || TriangleRef->SourceSurface == nullptr || TriangleRef->Triangle == nullptr)
            {
                continue;
            }

            float Distance = 0.0f;
            FVector Barycentric;
            if (!IntersectRayTriangle(
                    RayOrigin,
                    RayDirection,
                    *TriangleRef->Triangle,
                    MaxRevealDistance,
                    Distance,
                    Barycentric))
            {
                continue;
            }

            if (Distance < FMath::Max(0.0f, Settings.MinHitDistance))
            {
                continue;
            }

            if (Settings.bRespectPerSourceMaxDistance &&
                Distance > FMath::Max(0.0f, TriangleRef->SourceSurface->MaxRevealDistance))
            {
                continue;
            }

            FCandidateHit Candidate;
            Candidate.SourceSurface = TriangleRef->SourceSurface;
            Candidate.Triangle = TriangleRef->Triangle;
            Candidate.Barycentric = Barycentric;
            Candidate.Distance = Distance;
            Candidate.Position = InterpolateVector(Barycentric, TriangleRef->Triangle->Positions);
            Candidate.Normal = InterpolateVector(Barycentric, TriangleRef->Triangle->Normals).GetSafeNormal();
            CandidateHits.Add(Candidate);
        }

        CandidateHits.Sort(
            [&Settings](const FCandidateHit& Left, const FCandidateHit& Right)
            {
                if (Settings.bPreferLowerSourceLayerOrder && Left.SourceSurface != nullptr && Right.SourceSurface != nullptr &&
                    Left.SourceSurface->LayerOrder != Right.SourceSurface->LayerOrder)
                {
                    return Left.SourceSurface->LayerOrder < Right.SourceSurface->LayerOrder;
                }
                return Left.Distance < Right.Distance;
            });

        FDWCRevealBakeRayHit SelectedHit;
        SelectedHit.Pixel = Sample.Pixel;

        for (const FCandidateHit& Candidate : CandidateHits)
        {
            if (Candidate.SourceSurface == nullptr)
            {
                continue;
            }

            if (Candidate.SourceSurface->bCanBeRevealSource)
            {
                SelectedHit = MakeRayHit(Sample, Candidate, MaxRevealDistance);
                if (Settings.bUseNormalAlignmentConfidence)
                {
                    SelectedHit.Confidence = FMath::Clamp(
                        static_cast<float>(FVector::DotProduct(Sample.Normal.GetSafeNormal(), Candidate.Normal.GetSafeNormal())),
                        0.0f,
                        1.0f);
                }
                break;
            }

            if (Settings.bRespectBlockers && Candidate.SourceSurface->bBlocksReveal)
            {
                break;
            }
        }

        OutHits.Add(SelectedHit);
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FDWCRevealBakeRayProjector::IntersectRayTriangle(
    const FVector&                 RayOrigin,
    const FVector&                 RayDirection,
    const FDWCRevealBakeSurfaceTriangle& Triangle,
    const float                    MaxDistance,
    float&                         OutDistance,
    FVector&                       OutBarycentric)
{
    const FVector Edge1 = Triangle.Positions[1] - Triangle.Positions[0];
    const FVector Edge2 = Triangle.Positions[2] - Triangle.Positions[0];
    const FVector P = FVector::CrossProduct(RayDirection, Edge2);
    const double Determinant = FVector::DotProduct(Edge1, P);

    if (FMath::Abs(Determinant) < RayIntersectionEpsilon)
    {
        return false;
    }

    const double InverseDeterminant = 1.0 / Determinant;
    const FVector T = RayOrigin - Triangle.Positions[0];
    const double U = FVector::DotProduct(T, P) * InverseDeterminant;
    if (U < 0.0 || U > 1.0)
    {
        return false;
    }

    const FVector Q = FVector::CrossProduct(T, Edge1);
    const double V = FVector::DotProduct(RayDirection, Q) * InverseDeterminant;
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
    const FCandidateHit&       Candidate,
    const float                MaxRevealDistance)
{
    FDWCRevealBakeRayHit Hit;
    Hit.bHit = true;
    Hit.Pixel = Sample.Pixel;
    Hit.SourceLayerId = Candidate.SourceSurface != nullptr ? Candidate.SourceSurface->LayerId : NAME_None;
    Hit.SourceTriangleIndex = Candidate.Triangle != nullptr ? Candidate.Triangle->TriangleIndex : INDEX_NONE;
    Hit.SourceMaterialSlotIndex = Candidate.Triangle != nullptr ? Candidate.Triangle->MaterialSlotIndex : INDEX_NONE;
    Hit.Position = Candidate.Position;
    Hit.Normal = Candidate.Normal;
    Hit.Distance = Candidate.Distance;
    Hit.Confidence = MaxRevealDistance > 0.0f ? FMath::Clamp(1.0f - Candidate.Distance / MaxRevealDistance, 0.0f, 1.0f) : 0.0f;

    if (Candidate.Triangle != nullptr)
    {
        Hit.SourceUV = InterpolateVector2D(Candidate.Barycentric, Candidate.Triangle->UVs);
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
