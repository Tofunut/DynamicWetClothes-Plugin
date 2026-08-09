//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"

namespace
{
    uint64 GetRectPixels(const FIntRect& Rect)
    {
        return Rect.IsEmpty()
            ? 0
            : static_cast<uint64>(Rect.Width()) * static_cast<uint64>(Rect.Height());
    }

    FIntRect UnionRects(const FIntRect& A, const FIntRect& B)
    {
        return FIntRect(
            FMath::Min(A.Min.X, B.Min.X),
            FMath::Min(A.Min.Y, B.Min.Y),
            FMath::Max(A.Max.X, B.Max.X),
            FMath::Max(A.Max.Y, B.Max.Y));
    }

    bool RectsOverlapOrTouch(const FIntRect& A, const FIntRect& B)
    {
        return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
            A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y;
    }

    uint64 SumRectPixels(const TArray<FIntRect>& Rects)
    {
        uint64 Pixels = 0;
        for (const FIntRect& Rect : Rects)
        {
            const uint64 RectPixels = GetRectPixels(Rect);
            Pixels = Pixels <= MAX_uint64 - RectPixels ? Pixels + RectPixels : MAX_uint64;
        }
        return Pixels;
    }

    void CompactRects(
        const TArray<FIntRect>& Input,
        const FIntPoint TextureSize,
        const int32 MaxRegions,
        TArray<FIntRect>& OutRects)
    {
        OutRects.Reset();
        for (const FIntRect& InputRect : Input)
        {
            FIntRect Pending(
                FMath::Clamp(InputRect.Min.X, 0, TextureSize.X),
                FMath::Clamp(InputRect.Min.Y, 0, TextureSize.Y),
                FMath::Clamp(InputRect.Max.X, 0, TextureSize.X),
                FMath::Clamp(InputRect.Max.Y, 0, TextureSize.Y));
            if (Pending.IsEmpty())
            {
                continue;
            }
            for (int32 Index = 0; Index < OutRects.Num();)
            {
                if (!RectsOverlapOrTouch(Pending, OutRects[Index]))
                {
                    ++Index;
                    continue;
                }
                Pending = UnionRects(Pending, OutRects[Index]);
                OutRects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
                Index = 0;
            }
            OutRects.Add(Pending);
        }

        while (OutRects.Num() > FMath::Max(MaxRegions, 1))
        {
            int32 BestA = 0;
            int32 BestB = 1;
            uint64 BestAddedPixels = MAX_uint64;
            for (int32 A = 0; A < OutRects.Num() - 1; ++A)
            {
                for (int32 B = A + 1; B < OutRects.Num(); ++B)
                {
                    const uint64 CombinedPixels = GetRectPixels(UnionRects(OutRects[A], OutRects[B]));
                    const uint64 ExistingPixels = GetRectPixels(OutRects[A]) + GetRectPixels(OutRects[B]);
                    const uint64 AddedPixels = CombinedPixels > ExistingPixels
                        ? CombinedPixels - ExistingPixels
                        : 0;
                    if (AddedPixels < BestAddedPixels)
                    {
                        BestAddedPixels = AddedPixels;
                        BestA = A;
                        BestB = B;
                    }
                }
            }
            OutRects[BestA] = UnionRects(OutRects[BestA], OutRects[BestB]);
            OutRects.RemoveAt(BestB, 1, EAllowShrinking::No);
        }

        OutRects.Sort([](const FIntRect& A, const FIntRect& B)
        {
            return A.Min.Y != B.Min.Y ? A.Min.Y < B.Min.Y : A.Min.X < B.Min.X;
        });
    }

    uint64 GetUploadCost(const TArray<FIntRect>& Rects, const uint64 RegionPenaltyPixels)
    {
        const uint64 PixelCost = SumRectPixels(Rects);
        const uint64 RegionCost = static_cast<uint64>(Rects.Num()) * RegionPenaltyPixels;
        return PixelCost <= MAX_uint64 - RegionCost ? PixelCost + RegionCost : MAX_uint64;
    }

    bool TryGetRectElementCount(const FIntRect& Rect, uint64& OutCount)
    {
        OutCount = 0;
        if (Rect.IsEmpty() || Rect.Width() <= 0 || Rect.Height() <= 0)
        {
            return false;
        }
        const uint64 Width = static_cast<uint64>(Rect.Width());
        const uint64 Height = static_cast<uint64>(Rect.Height());
        if (Width > MAX_uint64 / Height)
        {
            return false;
        }
        OutCount = Width * Height;
        return true;
    }

    bool TryAddArrayBytes(const uint64 ElementCount, const uint64 ElementBytes, uint64& InOutBytes)
    {
        if (ElementBytes > 0 && ElementCount > MAX_uint64 / ElementBytes)
        {
            return false;
        }
        const uint64 Bytes = ElementCount * ElementBytes;
        if (InOutBytes > MAX_uint64 - Bytes)
        {
            return false;
        }
        InOutBytes += Bytes;
        return true;
    }
}

FDWCEditorSparseUploadDecision FDWCEditorSparseUploadPolicy::Choose(
    const TArray<FIntRect>& SparseRegions,
    const TArray<FIntRect>& BoundedRegions,
    const FIntPoint TextureSize,
    const FDWCEditorSparseUploadPolicyConfig& Config)
{
    FDWCEditorSparseUploadDecision Decision;
    Decision.SourceRegionCount = SparseRegions.Num();
    Decision.SourcePixelCount = SumRectPixels(SparseRegions);
    if (TextureSize.X <= 0 || TextureSize.Y <= 0)
    {
        return Decision;
    }

    TArray<FIntRect> CompactedSparse;
    TArray<FIntRect> CompactedBounded;
    CompactRects(SparseRegions, TextureSize, Config.MaxRegions, CompactedSparse);
    CompactRects(BoundedRegions, TextureSize, Config.MaxRegions, CompactedBounded);
    Decision.BoundedPixelCount = SumRectPixels(CompactedBounded);

    const bool bSparseValid = !CompactedSparse.IsEmpty() && Decision.SourcePixelCount > 0;
    const bool bBoundedValid = !CompactedBounded.IsEmpty();
    const bool bSparseCheaper = !bBoundedValid ||
        GetUploadCost(CompactedSparse, Config.RegionSubmissionPenaltyPixels) <
        GetUploadCost(CompactedBounded, Config.RegionSubmissionPenaltyPixels);

    if (bSparseValid && bSparseCheaper)
    {
        Decision.Plan = CompactedSparse.Num() == SparseRegions.Num()
            ? EDWCEditorSparseUploadPlan::Sparse
            : EDWCEditorSparseUploadPlan::MergedSparse;
        Decision.Regions = MoveTemp(CompactedSparse);
    }
    else
    {
        Decision.Plan = EDWCEditorSparseUploadPlan::Bounded;
        Decision.Regions = MoveTemp(CompactedBounded);
    }
    Decision.PlannedPixelCount = SumRectPixels(Decision.Regions);
    return Decision;
}

bool FDWCEditorPreviewRegionMemory::TryEstimateBGRA8(
    const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
    FDWCEditorPreviewRegionMemoryEstimate& OutEstimate)
{
    OutEstimate = {};
    for (const FDWCEditorBGRA8RegionPayload& Region : Regions)
    {
        uint64 ElementCount = 0;
        if (!TryGetRectElementCount(Region.Rect, ElementCount) ||
            ElementCount != static_cast<uint64>(Region.Pixels.Num()) ||
            !TryAddArrayBytes(ElementCount, sizeof(FColor), OutEstimate.ResultBytes))
        {
            OutEstimate = {};
            return false;
        }
    }
    return !Regions.IsEmpty();
}

bool FDWCEditorPreviewRegionMemory::TryEstimateG8(
    const TArray<FDWCEditorG8RegionPayload>& Regions,
    FDWCEditorPreviewRegionMemoryEstimate& OutEstimate)
{
    OutEstimate = {};
    for (const FDWCEditorG8RegionPayload& Region : Regions)
    {
        uint64 ElementCount = 0;
        if (!TryGetRectElementCount(Region.Rect, ElementCount) ||
            ElementCount != static_cast<uint64>(Region.Pixels.Num()) ||
            !TryAddArrayBytes(ElementCount, sizeof(uint8), OutEstimate.ResultBytes))
        {
            OutEstimate = {};
            return false;
        }
    }
    return !Regions.IsEmpty();
}

bool FDWCEditorPreviewRegionMemory::TryEstimateNormal(
    const TArray<FDWCEditorNormalRegionPayload>& Regions,
    FDWCEditorPreviewRegionMemoryEstimate& OutEstimate)
{
    OutEstimate = {};
    for (const FDWCEditorNormalRegionPayload& Region : Regions)
    {
        uint64 WorkingElementCount = 0;
        uint64 OutputElementCount = 0;
        if (!TryGetRectElementCount(Region.WorkingRect, WorkingElementCount) ||
            !TryGetRectElementCount(Region.OutputRect, OutputElementCount) ||
            WorkingElementCount != static_cast<uint64>(Region.PackedNormalXY.Num()) ||
            (!Region.Coverage.IsEmpty() &&
             WorkingElementCount != static_cast<uint64>(Region.Coverage.Num())) ||
            OutputElementCount != static_cast<uint64>(Region.EncodedPixels.Num()) ||
            !TryAddArrayBytes(WorkingElementCount, sizeof(uint32), OutEstimate.ResultBytes) ||
            (!Region.Coverage.IsEmpty() &&
             !TryAddArrayBytes(WorkingElementCount, sizeof(float), OutEstimate.ResultBytes)) ||
            !TryAddArrayBytes(OutputElementCount, sizeof(FColor), OutEstimate.ResultBytes))
        {
            OutEstimate = {};
            return false;
        }
    }
    return !Regions.IsEmpty();
}

