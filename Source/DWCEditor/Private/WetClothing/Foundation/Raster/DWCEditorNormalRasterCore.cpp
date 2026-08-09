//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"

#include "Async/ParallelFor.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"

namespace
{
    float RasterWrapUnit(const float Value)
    {
        return Value - FMath::FloorToFloat(Value);
    }

    float RasterWrappedDelta(const float Value)
    {
        return Value - FMath::RoundToFloat(Value);
    }

    float RasterSmoothStep(const float Edge0, const float Edge1, const float Value)
    {
        if (Edge0 >= Edge1)
        {
            return Value < Edge0 ? 0.0f : 1.0f;
        }
        const float T = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0f, 1.0f);
        return T * T * (3.0f - 2.0f * T);
    }

    void RasterIncludePixel(FIntRect& Rect, bool& bHasRect, const int32 X, const int32 Y)
    {
        if (!bHasRect)
        {
            Rect = FIntRect(X, Y, X + 1, Y + 1);
            bHasRect = true;
            return;
        }
        Rect.Min.X = FMath::Min(Rect.Min.X, X);
        Rect.Min.Y = FMath::Min(Rect.Min.Y, Y);
        Rect.Max.X = FMath::Max(Rect.Max.X, X + 1);
        Rect.Max.Y = FMath::Max(Rect.Max.Y, Y + 1);
    }

    void AddMergedRect(TArray<FIntRect>& Rects, FIntRect Rect)
    {
        if (Rect.IsEmpty())
        {
            return;
        }
        for (int32 Index = 0; Index < Rects.Num();)
        {
            const FIntRect& Existing = Rects[Index];
            const bool bTouches = Rect.Min.X <= Existing.Max.X && Rect.Max.X >= Existing.Min.X &&
                Rect.Min.Y <= Existing.Max.Y && Rect.Max.Y >= Existing.Min.Y;
            if (!bTouches)
            {
                ++Index;
                continue;
            }
            Rect.Min.X = FMath::Min(Rect.Min.X, Existing.Min.X);
            Rect.Min.Y = FMath::Min(Rect.Min.Y, Existing.Min.Y);
            Rect.Max.X = FMath::Max(Rect.Max.X, Existing.Max.X);
            Rect.Max.Y = FMath::Max(Rect.Max.Y, Existing.Max.Y);
            Rects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            Index = 0;
        }
        Rects.Add(Rect);
    }

    template <typename GetNormalType, typename SetNormalType, typename GetCoverageType, typename SetCoverageType>
    FDWCEditorRasterResult RasterizeStampPixels(
        const FDWCEditorNormalStampCommand& Command,
        const FIntPoint CanvasSize,
        const FIntRect& EffectiveClip,
        const bool bHasCoverage,
        GetNormalType&& GetNormal,
        SetNormalType&& SetNormal,
        GetCoverageType&& GetCoverage,
        SetCoverageType&& SetCoverage,
        const FDWCEditorCancellationToken* CancellationToken)
    {
        FDWCEditorRasterResult Result;
        const FVector2f Center(
            Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.X) : Command.Footprint.CenterUV.X,
            Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.Y) : Command.Footprint.CenterUV.Y);
        const FVector2f SafeScale(
            FMath::Max(FMath::Abs(Command.Footprint.Scale.X), UE_SMALL_NUMBER),
            FMath::Max(FMath::Abs(Command.Footprint.Scale.Y), UE_SMALL_NUMBER));
        const float EdgeFadeStart = FMath::Clamp(1.0f - Command.Footprint.Falloff, 0.0f, 0.98f);
        const float CosRotation = FMath::Cos(Command.Footprint.RotationRadians);
        const float SinRotation = FMath::Sin(Command.Footprint.RotationRadians);
        const int32 MinTile = Command.Footprint.bWrap ? -1 : 0;
        const int32 MaxTile = Command.Footprint.bWrap ? 1 : 0;
        bool bHasDirtyRect = false;

        for (int32 TileY = MinTile; TileY <= MaxTile; ++TileY)
        {
            for (int32 TileX = MinTile; TileX <= MaxTile; ++TileX)
            {
                const FVector2f TileCenter = Center + FVector2f(static_cast<float>(TileX), static_cast<float>(TileY));
                const int32 MinX = FMath::Max(
                    FMath::FloorToInt((TileCenter.X - Command.Footprint.RadiusUV) * CanvasSize.X),
                    EffectiveClip.Min.X);
                const int32 MaxX = FMath::Min(
                    FMath::CeilToInt((TileCenter.X + Command.Footprint.RadiusUV) * CanvasSize.X),
                    EffectiveClip.Max.X - 1);
                const int32 MinY = FMath::Max(
                    FMath::FloorToInt((TileCenter.Y - Command.Footprint.RadiusUV) * CanvasSize.Y),
                    EffectiveClip.Min.Y);
                const int32 MaxY = FMath::Min(
                    FMath::CeilToInt((TileCenter.Y + Command.Footprint.RadiusUV) * CanvasSize.Y),
                    EffectiveClip.Max.Y - 1);

                for (int32 Y = MinY; Y <= MaxY; ++Y)
                {
                    if (CancellationToken != nullptr && CancellationToken->IsCanceled())
                    {
                        Result.bSucceeded = false;
                        Result.bCanceled = true;
                        return Result;
                    }
                    for (int32 X = MinX; X <= MaxX; ++X)
                    {
                        const FVector2f PixelUV(
                            (static_cast<float>(X) + 0.5f) / CanvasSize.X,
                            (static_cast<float>(Y) + 0.5f) / CanvasSize.Y);
                        FVector2f Delta = PixelUV - TileCenter;
                        if (Command.Footprint.bWrap)
                        {
                            Delta.X = RasterWrappedDelta(Delta.X);
                            Delta.Y = RasterWrappedDelta(Delta.Y);
                        }
                        const FVector2f Local = Delta / FMath::Max(Command.Footprint.RadiusUV, UE_SMALL_NUMBER);
                        const float Distance = Local.Size();
                        if (Distance > 1.0f)
                        {
                            continue;
                        }

                        const float EdgeFade = 1.0f - RasterSmoothStep(EdgeFadeStart, 1.0f, Distance);
                        if (EdgeFade <= UE_SMALL_NUMBER)
                        {
                            continue;
                        }
                        const float LocalX = (CosRotation * Local.X + SinRotation * Local.Y) / SafeScale.X;
                        const float LocalY = (-SinRotation * Local.X + CosRotation * Local.Y) / SafeScale.Y;
                        if (FMath::Abs(LocalX) > 1.0f || FMath::Abs(LocalY) > 1.0f)
                        {
                            continue;
                        }

                        const FVector2f SourceUV(LocalX * 0.5f + 0.5f, LocalY * 0.5f + 0.5f);
                        const FVector3f Sampled = Command.NormalSource.SampleBilinear(SourceUV);
                        const FVector3f Rotated(
                            Sampled.X * CosRotation - Sampled.Y * SinRotation,
                            Sampled.X * SinRotation + Sampled.Y * CosRotation,
                            Sampled.Z);
                        const float Strength = FMath::Max(Command.Strength * EdgeFade, 0.0f);
                        const FVector3f Detail(Rotated.X * Strength, Rotated.Y * Strength, Rotated.Z);
                        SetNormal(X, Y, FDWCEditorNormalRasterCore::BlendAngleCorrected(
                            GetNormal(X, Y),
                            Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
                        if (bHasCoverage)
                        {
                            const float SourceCoverage = Command.CoverageSource.IsValid()
                                ? Command.CoverageSource.SampleBilinear(SourceUV)
                                : 1.0f;
                            SetCoverage(
                                X,
                                Y,
                                FMath::Max(GetCoverage(X, Y), FMath::Clamp(EdgeFade * SourceCoverage, 0.0f, 1.0f)));
                        }
                        RasterIncludePixel(Result.DirtyRect, bHasDirtyRect, X, Y);
                        ++Result.AffectedPixelCount;
                    }
                }
            }
        }
        Result.bAffectedPixels = bHasDirtyRect;
        return Result;
    }

    float RasterEdge(const FVector2f& A, const FVector2f& B, const FVector2f& P)
    {
        return (B.X - A.X) * (P.Y - A.Y) - (B.Y - A.Y) * (P.X - A.X);
    }

    bool RasterIsTopLeftEdge(const FVector2f& A, const FVector2f& B)
    {
        const FVector2f Delta = B - A;
        return Delta.Y > 0.0f || (FMath::IsNearlyZero(Delta.Y) && Delta.X < 0.0f);
    }

    bool RasterOwnsEdgeSample(const float EdgeValue, const FVector2f& A, const FVector2f& B)
    {
        constexpr float EdgeEpsilon = 1.0e-8f;
        return EdgeValue > EdgeEpsilon ||
            (FMath::Abs(EdgeValue) <= EdgeEpsilon && RasterIsTopLeftEdge(A, B));
    }

    template <typename GetNormalType, typename SetNormalType, typename GetCoverageType, typename SetCoverageType>
    FDWCEditorRasterResult RasterizeProjectedPatchPixels(
        const FDWCEditorProjectedNormalPatchCommand& Command,
        const TConstArrayView<int32> FragmentIndices,
        const bool bUseFragmentSubset,
        const FIntPoint CanvasSize,
        const FIntRect& EffectiveClip,
        const bool bHasCoverage,
        GetNormalType&& GetNormal,
        SetNormalType&& SetNormal,
        GetCoverageType&& GetCoverage,
        SetCoverageType&& SetCoverage,
        const FDWCEditorCancellationToken* CancellationToken,
        FDWCEditorProjectedRasterDiagnostics* Diagnostics)
    {
        if (Diagnostics != nullptr)
        {
            *Diagnostics = FDWCEditorProjectedRasterDiagnostics();
            Diagnostics->SourceFragmentCount = bUseFragmentSubset
                ? FragmentIndices.Num()
                : Command.GetFragments().Num();
        }
        FDWCEditorRasterResult Result;
        bool bHasDirtyRect = false;
        const float EdgeFadeStart = FMath::Clamp(1.0f - Command.Falloff, 0.0f, 0.98f);

        struct FPreparedFragment
        {
            FVector2f Target[3];
            FVector2f Patch[3];
            FVector2f PatchAxisU[3];
            FVector2f PatchAxisV[3];
            float ProjectionInfluence[3];
            float Area = 0.0f;
            int32 MinX = 0;
            int32 MaxX = -1;
            int32 MinY = 0;
            int32 MaxY = -1;
        };

        TArray<FPreparedFragment> PreparedFragments;
        const TArray<FDWCEditorSurfacePatchFragment>& SourceFragments = Command.GetFragments();
        const int32 IterationCount = bUseFragmentSubset
            ? FragmentIndices.Num()
            : SourceFragments.Num();
        PreparedFragments.Reserve(IterationCount);
        uint64 RowReferenceCount = 0;
        for (int32 IterationIndex = 0; IterationIndex < IterationCount; ++IterationIndex)
        {
            const int32 FragmentIndex = bUseFragmentSubset
                ? FragmentIndices[IterationIndex]
                : IterationIndex;
            if (!SourceFragments.IsValidIndex(FragmentIndex))
            {
                continue;
            }
            const FDWCEditorSurfacePatchFragment& Fragment = SourceFragments[FragmentIndex];
            FPreparedFragment Prepared;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                Prepared.Target[Corner] = Fragment.TargetUVs[Corner];
                Prepared.Patch[Corner] = Fragment.PatchCoordinates[Corner];
                Prepared.PatchAxisU[Corner] = Fragment.PatchAxisUInTargetTangent[Corner];
                Prepared.PatchAxisV[Corner] = Fragment.PatchAxisVInTargetTangent[Corner];
                Prepared.ProjectionInfluence[Corner] = Fragment.ProjectionInfluence[Corner];
            }
            Prepared.Area = RasterEdge(
                Prepared.Target[0], Prepared.Target[1], Prepared.Target[2]);
            if (FMath::Abs(Prepared.Area) <= UE_SMALL_NUMBER)
            {
                continue;
            }
            if (Prepared.Area < 0.0f)
            {
                Swap(Prepared.Target[1], Prepared.Target[2]);
                Swap(Prepared.Patch[1], Prepared.Patch[2]);
                Swap(Prepared.PatchAxisU[1], Prepared.PatchAxisU[2]);
                Swap(Prepared.PatchAxisV[1], Prepared.PatchAxisV[2]);
                Swap(Prepared.ProjectionInfluence[1], Prepared.ProjectionInfluence[2]);
                Prepared.Area = -Prepared.Area;
            }

            const float MinU = FMath::Min3(
                Prepared.Target[0].X, Prepared.Target[1].X, Prepared.Target[2].X);
            const float MaxU = FMath::Max3(
                Prepared.Target[0].X, Prepared.Target[1].X, Prepared.Target[2].X);
            const float MinV = FMath::Min3(
                Prepared.Target[0].Y, Prepared.Target[1].Y, Prepared.Target[2].Y);
            const float MaxV = FMath::Max3(
                Prepared.Target[0].Y, Prepared.Target[1].Y, Prepared.Target[2].Y);
            Prepared.MinX = FMath::Max(
                FMath::FloorToInt(MinU * CanvasSize.X), EffectiveClip.Min.X);
            Prepared.MaxX = FMath::Min(
                FMath::CeilToInt(MaxU * CanvasSize.X), EffectiveClip.Max.X - 1);
            Prepared.MinY = FMath::Max(
                FMath::FloorToInt(MinV * CanvasSize.Y), EffectiveClip.Min.Y);
            Prepared.MaxY = FMath::Min(
                FMath::CeilToInt(MaxV * CanvasSize.Y), EffectiveClip.Max.Y - 1);
            if (Prepared.MinX > Prepared.MaxX || Prepared.MinY > Prepared.MaxY)
            {
                continue;
            }
            RowReferenceCount += static_cast<uint64>(Prepared.MaxY - Prepared.MinY + 1);
            PreparedFragments.Add(MoveTemp(Prepared));
        }
        if (Diagnostics != nullptr)
        {
            Diagnostics->PreparedFragmentCount = PreparedFragments.Num();
            Diagnostics->RowReferenceCount = RowReferenceCount;
        }

        const auto RasterizePixel = [&](const FPreparedFragment& Fragment, const int32 X, const int32 Y)
        {
            const FVector2f PixelUV(
                (static_cast<float>(X) + 0.5f) / CanvasSize.X,
                (static_cast<float>(Y) + 0.5f) / CanvasSize.Y);
            const float W0 = RasterEdge(Fragment.Target[1], Fragment.Target[2], PixelUV);
            const float W1 = RasterEdge(Fragment.Target[2], Fragment.Target[0], PixelUV);
            const float W2 = RasterEdge(Fragment.Target[0], Fragment.Target[1], PixelUV);
            if (!RasterOwnsEdgeSample(W0, Fragment.Target[1], Fragment.Target[2]) ||
                !RasterOwnsEdgeSample(W1, Fragment.Target[2], Fragment.Target[0]) ||
                !RasterOwnsEdgeSample(W2, Fragment.Target[0], Fragment.Target[1]))
            {
                return false;
            }

            const float Barycentric[3] = {
                W0 / Fragment.Area, W1 / Fragment.Area, W2 / Fragment.Area
            };
            const FVector2f Local =
                Fragment.Patch[0] * Barycentric[0] +
                Fragment.Patch[1] * Barycentric[1] +
                Fragment.Patch[2] * Barycentric[2];
            const float Distance = Local.Size();
            if (Distance > 1.0f)
            {
                return false;
            }
            const float ProjectionFade = FMath::Clamp(
                Fragment.ProjectionInfluence[0] * Barycentric[0] +
                Fragment.ProjectionInfluence[1] * Barycentric[1] +
                Fragment.ProjectionInfluence[2] * Barycentric[2],
                0.0f,
                1.0f);
            const float EdgeFade =
                (1.0f - RasterSmoothStep(EdgeFadeStart, 1.0f, Distance)) * ProjectionFade;
            if (EdgeFade <= UE_SMALL_NUMBER)
            {
                return false;
            }

            const FVector2f SourceUV = Local * 0.5f + FVector2f(0.5f, 0.5f);
            const FVector3f Sampled = Command.NormalSource.SampleBilinear(SourceUV);
            FVector2f TargetAxisU =
                Fragment.PatchAxisU[0] * Barycentric[0] +
                Fragment.PatchAxisU[1] * Barycentric[1] +
                Fragment.PatchAxisU[2] * Barycentric[2];
            const FVector2f InterpolatedAxisV =
                Fragment.PatchAxisV[0] * Barycentric[0] +
                Fragment.PatchAxisV[1] * Barycentric[1] +
                Fragment.PatchAxisV[2] * Barycentric[2];
            TargetAxisU.Normalize();
            if (TargetAxisU.IsNearlyZero())
            {
                return false;
            }
            FVector2f TargetAxisV(-TargetAxisU.Y, TargetAxisU.X);
            if (FVector2f::DotProduct(TargetAxisV, InterpolatedAxisV) < 0.0f)
            {
                TargetAxisV *= -1.0f;
            }
            const FVector2f TargetXY =
                TargetAxisU * Sampled.X + TargetAxisV * Sampled.Y;
            const float Strength = FMath::Max(Command.Strength * EdgeFade, 0.0f);
            const FVector3f Detail(TargetXY.X * Strength, TargetXY.Y * Strength, Sampled.Z);
            SetNormal(X, Y, FDWCEditorNormalRasterCore::BlendAngleCorrected(
                GetNormal(X, Y),
                Detail.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f))));
            if (bHasCoverage)
            {
                const float SourceCoverage = Command.CoverageSource.IsValid()
                    ? Command.CoverageSource.SampleBilinear(SourceUV)
                    : 1.0f;
                SetCoverage(X, Y, FMath::Max(
                    GetCoverage(X, Y),
                    FMath::Clamp(EdgeFade * SourceCoverage, 0.0f, 1.0f)));
            }
            return true;
        };

        constexpr uint64 MaxParallelRowReferences = 8ull * 1024ull * 1024ull;
        const int32 RowCount = EffectiveClip.Height();
        const bool bUseParallelRows =
            EffectiveClip.Width() * RowCount >= 256 * 256 &&
            RowCount >= 64 && PreparedFragments.Num() >= 4 &&
            RowReferenceCount <= MaxParallelRowReferences;
        if (!bUseParallelRows)
        {
            uint64 CandidatePixelCount = 0;
            for (const FPreparedFragment& Fragment : PreparedFragments)
            {
                if (CancellationToken != nullptr && CancellationToken->IsCanceled())
                {
                    Result.bSucceeded = false;
                    Result.bCanceled = true;
                    return Result;
                }
                for (int32 Y = Fragment.MinY; Y <= Fragment.MaxY; ++Y)
                {
                    CandidatePixelCount += static_cast<uint64>(Fragment.MaxX - Fragment.MinX + 1);
                    for (int32 X = Fragment.MinX; X <= Fragment.MaxX; ++X)
                    {
                        if (RasterizePixel(Fragment, X, Y))
                        {
                            RasterIncludePixel(Result.DirtyRect, bHasDirtyRect, X, Y);
                            ++Result.AffectedPixelCount;
                        }
                    }
                }
            }
            if (Diagnostics != nullptr)
            {
                Diagnostics->CandidatePixelCount = CandidatePixelCount;
            }
            Result.bAffectedPixels = bHasDirtyRect;
            return Result;
        }

        TArray<int32> RowOffsets;
        RowOffsets.Init(0, RowCount + 1);
        for (const FPreparedFragment& Fragment : PreparedFragments)
        {
            for (int32 Y = Fragment.MinY; Y <= Fragment.MaxY; ++Y)
            {
                ++RowOffsets[Y - EffectiveClip.Min.Y + 1];
            }
        }
        for (int32 RowIndex = 1; RowIndex <= RowCount; ++RowIndex)
        {
            RowOffsets[RowIndex] += RowOffsets[RowIndex - 1];
        }
        TArray<int32> WriteOffsets;
        WriteOffsets = RowOffsets;
        TArray<int32> RowFragmentIndices;
        RowFragmentIndices.SetNumUninitialized(RowOffsets.Last());
        for (int32 FragmentIndex = 0; FragmentIndex < PreparedFragments.Num(); ++FragmentIndex)
        {
            const FPreparedFragment& Fragment = PreparedFragments[FragmentIndex];
            for (int32 Y = Fragment.MinY; Y <= Fragment.MaxY; ++Y)
            {
                const int32 RowIndex = Y - EffectiveClip.Min.Y;
                RowFragmentIndices[WriteOffsets[RowIndex]++] = FragmentIndex;
            }
        }

        TArray<int32> RowAffectedCounts;
        TArray<int32> RowMinX;
        TArray<int32> RowMaxX;
        TArray<uint8> RowCanceled;
        TArray<uint64> RowCandidateCounts;
        RowAffectedCounts.Init(0, RowCount);
        RowMinX.Init(MAX_int32, RowCount);
        RowMaxX.Init(MIN_int32, RowCount);
        RowCanceled.Init(0, RowCount);
        if (Diagnostics != nullptr)
        {
            RowCandidateCounts.Init(0, RowCount);
        }
        ParallelFor(RowCount, [&](const int32 RowIndex)
        {
            if (CancellationToken != nullptr && CancellationToken->IsCanceled())
            {
                RowCanceled[RowIndex] = 1;
                return;
            }
            const int32 Y = EffectiveClip.Min.Y + RowIndex;
            int32 AffectedCount = 0;
            int32 MinAffectedX = MAX_int32;
            int32 MaxAffectedX = MIN_int32;
            uint64 CandidateCount = 0;
            for (int32 EntryIndex = RowOffsets[RowIndex];
                 EntryIndex < RowOffsets[RowIndex + 1]; ++EntryIndex)
            {
                if (CancellationToken != nullptr && CancellationToken->IsCanceled())
                {
                    RowCanceled[RowIndex] = 1;
                    return;
                }
                const FPreparedFragment& Fragment =
                    PreparedFragments[RowFragmentIndices[EntryIndex]];
                CandidateCount += static_cast<uint64>(Fragment.MaxX - Fragment.MinX + 1);
                for (int32 X = Fragment.MinX; X <= Fragment.MaxX; ++X)
                {
                    if (RasterizePixel(Fragment, X, Y))
                    {
                        ++AffectedCount;
                        MinAffectedX = FMath::Min(MinAffectedX, X);
                        MaxAffectedX = FMath::Max(MaxAffectedX, X);
                    }
                }
            }
            RowAffectedCounts[RowIndex] = AffectedCount;
            RowMinX[RowIndex] = MinAffectedX;
            RowMaxX[RowIndex] = MaxAffectedX;
            if (Diagnostics != nullptr)
            {
                RowCandidateCounts[RowIndex] = CandidateCount;
            }
        }, EParallelForFlags::Unbalanced);

        for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
        {
            if (RowCanceled[RowIndex] != 0)
            {
                Result.bSucceeded = false;
                Result.bCanceled = true;
                return Result;
            }
            if (RowAffectedCounts[RowIndex] > 0)
            {
                const int32 Y = EffectiveClip.Min.Y + RowIndex;
                RasterIncludePixel(Result.DirtyRect, bHasDirtyRect, RowMinX[RowIndex], Y);
                RasterIncludePixel(Result.DirtyRect, bHasDirtyRect, RowMaxX[RowIndex], Y);
                Result.AffectedPixelCount += RowAffectedCounts[RowIndex];
            }
        }
        if (Diagnostics != nullptr)
        {
            Diagnostics->bUsedParallelRows = true;
            Diagnostics->ParallelRowCount = RowCount;
            for (const uint64 CandidateCount : RowCandidateCounts)
            {
                Diagnostics->CandidatePixelCount += CandidateCount;
            }
        }
        Result.bAffectedPixels = bHasDirtyRect;
        return Result;
    }
}

FVector3f FDWCEditorNormalRasterCore::BlendAngleCorrected(
    const FVector3f& BaseNormal,
    const FVector3f& DetailNormal)
{
    const FVector3f Blended(
        BaseNormal.X + DetailNormal.X,
        BaseNormal.Y + DetailNormal.Y,
        BaseNormal.Z * DetailNormal.Z);
    return Blended.GetSafeNormal(UE_SMALL_NUMBER, FVector3f(0.0f, 0.0f, 1.0f));
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeStamp(
    const FDWCEditorNormalStampCommand& Command,
    FDWCEditorNormalRasterSurface& Surface,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect)
{
    if (!Surface.IsValid() || !Command.NormalSource.IsValid() ||
        Command.Footprint.RadiusUV <= 0.0f || Command.Strength <= 0.0f)
    {
        return FDWCEditorRasterResult();
    }

    const FIntRect SurfaceRect(FIntPoint::ZeroValue, Surface.Size);
    const FIntRect EffectiveClip = ClipRect != nullptr
        ? FIntRect(
            FIntPoint(
                FMath::Max(ClipRect->Min.X, SurfaceRect.Min.X),
                FMath::Max(ClipRect->Min.Y, SurfaceRect.Min.Y)),
            FIntPoint(
                FMath::Min(ClipRect->Max.X, SurfaceRect.Max.X),
                FMath::Min(ClipRect->Max.Y, SurfaceRect.Max.Y)))
        : SurfaceRect;
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeStampPixels(
        Command,
        Surface.Size,
        EffectiveClip,
        Surface.HasCoverage(),
        [&Surface](const int32 X, const int32 Y) { return Surface.GetNormal(Y * Surface.Size.X + X); },
        [&Surface](const int32 X, const int32 Y, const FVector3f& Normal) { Surface.SetNormal(Y * Surface.Size.X + X, Normal); },
        [&Surface](const int32 X, const int32 Y) { return Surface.Coverage[Y * Surface.Size.X + X]; },
        [&Surface](const int32 X, const int32 Y, const float Coverage) { Surface.Coverage[Y * Surface.Size.X + X] = Coverage; },
        CancellationToken);
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeStampRegion(
    const FDWCEditorNormalStampCommand& Command,
    FDWCEditorNormalRasterRegion& Region,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect)
{
    if (!Region.IsValid() || !Command.NormalSource.IsValid() ||
        Command.Footprint.RadiusUV <= 0.0f || Command.Strength <= 0.0f)
    {
        return FDWCEditorRasterResult();
    }
    FIntRect EffectiveClip = Region.Rect;
    if (ClipRect != nullptr)
    {
        EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, ClipRect->Min.X);
        EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, ClipRect->Min.Y);
        EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, ClipRect->Max.X);
        EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, ClipRect->Max.Y);
    }
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeStampPixels(
        Command,
        Region.CanvasSize,
        EffectiveClip,
        Region.Surface.HasCoverage(),
        [&Region](const int32 X, const int32 Y) { return Region.GetNormal(X, Y); },
        [&Region](const int32 X, const int32 Y, const FVector3f& Normal) { Region.SetNormal(X, Y, Normal); },
        [&Region](const int32 X, const int32 Y) { return Region.GetCoverage(X, Y); },
        [&Region](const int32 X, const int32 Y, const float Coverage) { Region.SetCoverage(X, Y, Coverage); },
        CancellationToken);
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
    const FDWCEditorProjectedNormalPatchCommand& Command,
    FDWCEditorNormalRasterSurface& Surface,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect,
    FDWCEditorProjectedRasterDiagnostics* Diagnostics)
{
    if (!Surface.IsValid() || !Command.IsValid())
    {
        return FDWCEditorRasterResult();
    }
    const FIntRect SurfaceRect(FIntPoint::ZeroValue, Surface.Size);
    FIntRect EffectiveClip = ClipRect != nullptr ? *ClipRect : SurfaceRect;
    EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, SurfaceRect.Min.X);
    EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, SurfaceRect.Min.Y);
    EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, SurfaceRect.Max.X);
    EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, SurfaceRect.Max.Y);
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeProjectedPatchPixels(
        Command, TConstArrayView<int32>(), false, Surface.Size, EffectiveClip, Surface.HasCoverage(),
        [&Surface](const int32 X, const int32 Y) { return Surface.GetNormal(Y * Surface.Size.X + X); },
        [&Surface](const int32 X, const int32 Y, const FVector3f& Normal) { Surface.SetNormal(Y * Surface.Size.X + X, Normal); },
        [&Surface](const int32 X, const int32 Y) { return Surface.Coverage[Y * Surface.Size.X + X]; },
        [&Surface](const int32 X, const int32 Y, const float Coverage) { Surface.Coverage[Y * Surface.Size.X + X] = Coverage; },
        CancellationToken,
        Diagnostics);
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeProjectedPatchRegion(
    const FDWCEditorProjectedNormalPatchCommand& Command,
    FDWCEditorNormalRasterRegion& Region,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect,
    FDWCEditorProjectedRasterDiagnostics* Diagnostics)
{
    if (!Region.IsValid() || !Command.IsValid())
    {
        return FDWCEditorRasterResult();
    }
    FIntRect EffectiveClip = Region.Rect;
    if (ClipRect != nullptr)
    {
        EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, ClipRect->Min.X);
        EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, ClipRect->Min.Y);
        EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, ClipRect->Max.X);
        EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, ClipRect->Max.Y);
    }
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeProjectedPatchPixels(
        Command, TConstArrayView<int32>(), false, Region.CanvasSize, EffectiveClip, Region.Surface.HasCoverage(),
        [&Region](const int32 X, const int32 Y) { return Region.GetNormal(X, Y); },
        [&Region](const int32 X, const int32 Y, const FVector3f& Normal) { Region.SetNormal(X, Y, Normal); },
        [&Region](const int32 X, const int32 Y) { return Region.GetCoverage(X, Y); },
        [&Region](const int32 X, const int32 Y, const float Coverage) { Region.SetCoverage(X, Y, Coverage); },
        CancellationToken,
        Diagnostics);
}

FDWCEditorRasterResult FDWCEditorNormalRasterCore::RasterizeProjectedPatchRegionSubset(
    const FDWCEditorProjectedNormalPatchCommand& Command,
    const TConstArrayView<int32> FragmentIndices,
    FDWCEditorNormalRasterRegion& Region,
    const FDWCEditorCancellationToken* CancellationToken,
    const FIntRect* ClipRect,
    FDWCEditorProjectedRasterDiagnostics* Diagnostics)
{
    if (!Region.IsValid() || !Command.IsValid() || FragmentIndices.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    FIntRect EffectiveClip = Region.Rect;
    if (ClipRect != nullptr)
    {
        EffectiveClip.Min.X = FMath::Max(EffectiveClip.Min.X, ClipRect->Min.X);
        EffectiveClip.Min.Y = FMath::Max(EffectiveClip.Min.Y, ClipRect->Min.Y);
        EffectiveClip.Max.X = FMath::Min(EffectiveClip.Max.X, ClipRect->Max.X);
        EffectiveClip.Max.Y = FMath::Min(EffectiveClip.Max.Y, ClipRect->Max.Y);
    }
    if (EffectiveClip.IsEmpty())
    {
        return FDWCEditorRasterResult();
    }
    return RasterizeProjectedPatchPixels(
        Command, FragmentIndices, true, Region.CanvasSize, EffectiveClip, Region.Surface.HasCoverage(),
        [&Region](const int32 X, const int32 Y) { return Region.GetNormal(X, Y); },
        [&Region](const int32 X, const int32 Y, const FVector3f& Normal) { Region.SetNormal(X, Y, Normal); },
        [&Region](const int32 X, const int32 Y) { return Region.GetCoverage(X, Y); },
        [&Region](const int32 X, const int32 Y, const float Coverage) { Region.SetCoverage(X, Y, Coverage); },
        CancellationToken,
        Diagnostics);
}

void FDWCEditorNormalRasterCore::ComputeStampBounds(
    const FDWCEditorNormalStampCommand& Command,
    const FIntPoint CanvasSize,
    TArray<FIntRect>& OutBounds)
{
    OutBounds.Reset();
    if (CanvasSize.X <= 0 || CanvasSize.Y <= 0 || Command.Footprint.RadiusUV <= 0.0f)
    {
        return;
    }
    const FVector2f Center(
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.X) : Command.Footprint.CenterUV.X,
        Command.Footprint.bWrap ? RasterWrapUnit(Command.Footprint.CenterUV.Y) : Command.Footprint.CenterUV.Y);
    const int32 MinTile = Command.Footprint.bWrap ? -1 : 0;
    const int32 MaxTile = Command.Footprint.bWrap ? 1 : 0;
    for (int32 TileY = MinTile; TileY <= MaxTile; ++TileY)
    {
        for (int32 TileX = MinTile; TileX <= MaxTile; ++TileX)
        {
            const FVector2f TileCenter = Center + FVector2f(static_cast<float>(TileX), static_cast<float>(TileY));
            FIntRect Bounds(
                FMath::FloorToInt((TileCenter.X - Command.Footprint.RadiusUV) * CanvasSize.X),
                FMath::FloorToInt((TileCenter.Y - Command.Footprint.RadiusUV) * CanvasSize.Y),
                FMath::CeilToInt((TileCenter.X + Command.Footprint.RadiusUV) * CanvasSize.X) + 1,
                FMath::CeilToInt((TileCenter.Y + Command.Footprint.RadiusUV) * CanvasSize.Y) + 1);
            Bounds.Min.X = FMath::Clamp(Bounds.Min.X, 0, CanvasSize.X);
            Bounds.Min.Y = FMath::Clamp(Bounds.Min.Y, 0, CanvasSize.Y);
            Bounds.Max.X = FMath::Clamp(Bounds.Max.X, 0, CanvasSize.X);
            Bounds.Max.Y = FMath::Clamp(Bounds.Max.Y, 0, CanvasSize.Y);
            AddMergedRect(OutBounds, Bounds);
        }
    }
}

void FDWCEditorNormalRasterCore::ComputeProjectedPatchBounds(
    const FDWCEditorProjectedNormalPatchCommand& Command,
    const FIntPoint CanvasSize,
    TArray<FIntRect>& OutBounds)
{
    OutBounds.Reset();
    if (CanvasSize.X <= 0 || CanvasSize.Y <= 0)
    {
        return;
    }
    for (const FDWCEditorSurfacePatchFragment& Fragment : Command.GetFragments())
    {
        FIntRect Bounds(
            FMath::FloorToInt(Fragment.TargetUVBounds.Min.X * CanvasSize.X) - 1,
            FMath::FloorToInt(Fragment.TargetUVBounds.Min.Y * CanvasSize.Y) - 1,
            FMath::CeilToInt(Fragment.TargetUVBounds.Max.X * CanvasSize.X) + 2,
            FMath::CeilToInt(Fragment.TargetUVBounds.Max.Y * CanvasSize.Y) + 2);
        Bounds.Min.X = FMath::Clamp(Bounds.Min.X, 0, CanvasSize.X);
        Bounds.Min.Y = FMath::Clamp(Bounds.Min.Y, 0, CanvasSize.Y);
        Bounds.Max.X = FMath::Clamp(Bounds.Max.X, 0, CanvasSize.X);
        Bounds.Max.Y = FMath::Clamp(Bounds.Max.Y, 0, CanvasSize.Y);
        AddMergedRect(OutBounds, Bounds);
    }
}
