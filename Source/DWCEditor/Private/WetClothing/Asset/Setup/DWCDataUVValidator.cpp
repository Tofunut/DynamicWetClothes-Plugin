#include "DWCDataUVValidator.h"

#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCDataUVValidatorPrivate
{
    struct FPackedTriangleRecord
    {
        int32 SourceTriangleIndex = INDEX_NONE;
        int32 MaterialSlotIndex = INDEX_NONE;
        FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
        FBox2D Bounds = FBox2D(ForceInit);
    };
}

using DWCDataUVValidatorPrivate::FPackedTriangleRecord;

bool FDWCDataUVValidator::Validate(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& Charts,
    const TMap<int32, FVector2f>& PackedUVByVertexInstance,
    TSet<int32>& OutProblemMaterialSlots,
    FString& OutError)
{
    OutProblemMaterialSlots.Reset();
    OutError.Reset();

    TMap<int32, TSet<int32>> TriangleIndicesByMaterial;
    for (const FDWCDataUVChart& Chart : Charts)
    {
        TSet<int32>& TriangleSet = TriangleIndicesByMaterial.FindOrAdd(Chart.MaterialSlotIndex);
        for (const int32 TriangleIndex : Chart.TriangleIndices)
        {
            TriangleSet.Add(TriangleIndex);
        }
    }

    TMap<int32, TArray<FPackedTriangleRecord>> PackedTrianglesByMaterial;
    for (const TPair<int32, TSet<int32>>& MaterialPair : TriangleIndicesByMaterial)
    {
        TArray<FPackedTriangleRecord>& PackedTriangles = PackedTrianglesByMaterial.FindOrAdd(MaterialPair.Key);
        for (const int32 TriangleIndex : MaterialPair.Value)
        {
            if (!Triangles.IsValidIndex(TriangleIndex))
            {
                OutProblemMaterialSlots.Add(MaterialPair.Key);
                OutError = TEXT("Generated DWC UV references an invalid source triangle.");
                continue;
            }

            const FDWCDataUVTriangle& Triangle = Triangles[TriangleIndex];
            FPackedTriangleRecord PackedTriangle;
            PackedTriangle.SourceTriangleIndex = TriangleIndex;
            PackedTriangle.MaterialSlotIndex = MaterialPair.Key;
            PackedTriangle.Bounds = FBox2D(ForceInit);

            bool bHasAllCorners = true;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const FVector2f* PackedUV = PackedUVByVertexInstance.Find(Triangle.VertexInstances[CornerIndex].GetValue());
                if (PackedUV == nullptr)
                {
                    bHasAllCorners = false;
                    break;
                }

                PackedTriangle.UVs[CornerIndex] = FVector2D(PackedUV->X, PackedUV->Y);
                PackedTriangle.Bounds += PackedTriangle.UVs[CornerIndex];
            }

            if (!bHasAllCorners)
            {
                OutProblemMaterialSlots.Add(MaterialPair.Key);
                OutError = FString::Printf(
                    TEXT("Generated DWC UV is missing a triangle corner in material slot %d."),
                    MaterialPair.Key);
                continue;
            }

            constexpr double RangeTolerance = 1.0e-6;
            bool bValidCoordinates = true;
            for (const FVector2D& UV : PackedTriangle.UVs)
            {
                bValidCoordinates &= FDWCUVGeometry::IsFiniteReasonableUV(UV) &&
                                     UV.X >= -RangeTolerance && UV.X <= 1.0 + RangeTolerance &&
                                     UV.Y >= -RangeTolerance && UV.Y <= 1.0 + RangeTolerance;
            }

            if (!bValidCoordinates ||
                FDWCUVGeometry::ComputeTriangleArea2D(PackedTriangle.UVs[0], PackedTriangle.UVs[1], PackedTriangle.UVs[2]) <= 1.0e-12)
            {
                OutProblemMaterialSlots.Add(MaterialPair.Key);
                OutError = FString::Printf(
                    TEXT("Generated DWC UV contains an invalid or degenerate packed triangle in material slot %d."),
                    MaterialPair.Key);
                continue;
            }

            PackedTriangles.Add(MoveTemp(PackedTriangle));
        }
    }

    for (const TPair<int32, TArray<FPackedTriangleRecord>>& MaterialPair : PackedTrianglesByMaterial)
    {
        const int32 MaterialSlotIndex = MaterialPair.Key;
        const TArray<FPackedTriangleRecord>& PackedTriangles = MaterialPair.Value;
        if (PackedTriangles.Num() < 2)
        {
            continue;
        }

        const int32 GridDimension = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(static_cast<double>(PackedTriangles.Num()))), 1, 64);
        TMap<int32, TArray<int32>> CellToTriangleIndices;
        for (int32 LocalIndex = 0; LocalIndex < PackedTriangles.Num(); ++LocalIndex)
        {
            const FBox2D& Bounds = PackedTriangles[LocalIndex].Bounds;
            const int32 MinCellX = FMath::Clamp(FMath::FloorToInt(Bounds.Min.X * GridDimension), 0, GridDimension - 1);
            const int32 MaxCellX = FMath::Clamp(FMath::FloorToInt(Bounds.Max.X * GridDimension), 0, GridDimension - 1);
            const int32 MinCellY = FMath::Clamp(FMath::FloorToInt(Bounds.Min.Y * GridDimension), 0, GridDimension - 1);
            const int32 MaxCellY = FMath::Clamp(FMath::FloorToInt(Bounds.Max.Y * GridDimension), 0, GridDimension - 1);

            for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
            {
                for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
                {
                    CellToTriangleIndices.FindOrAdd(CellY * GridDimension + CellX).Add(LocalIndex);
                }
            }
        }

        TSet<uint64> CandidatePairs;
        for (const TPair<int32, TArray<int32>>& CellPair : CellToTriangleIndices)
        {
            const TArray<int32>& LocalTriangles = CellPair.Value;
            for (int32 AListIndex = 0; AListIndex < LocalTriangles.Num(); ++AListIndex)
            {
                for (int32 BListIndex = AListIndex + 1; BListIndex < LocalTriangles.Num(); ++BListIndex)
                {
                    CandidatePairs.Add(FDWCUVGeometry::MakeTrianglePairKey(LocalTriangles[AListIndex], LocalTriangles[BListIndex]));
                }
            }
        }

        for (const uint64 PairKey : CandidatePairs)
        {
            const int32 LocalA = static_cast<int32>(PairKey >> 32);
            const int32 LocalB = static_cast<int32>(PairKey & 0xffffffffu);
            const FPackedTriangleRecord& A = PackedTriangles[LocalA];
            const FPackedTriangleRecord& B = PackedTriangles[LocalB];
            if (!A.Bounds.Intersect(B.Bounds))
            {
                continue;
            }

            if (FDWCUVGeometry::DoTrianglesOverlapByArea(
                    A.UVs[0], A.UVs[1], A.UVs[2],
                    B.UVs[0], B.UVs[1], B.UVs[2]))
            {
                OutProblemMaterialSlots.Add(MaterialSlotIndex);
                OutError = FString::Printf(
                    TEXT("Generated DWC UV still contains triangle self-overlap in material slot %d."),
                    MaterialSlotIndex);
                break;
            }
        }
    }

    return OutProblemMaterialSlots.Num() == 0;
}
