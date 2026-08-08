// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

FDWCEditorTextureLease::FDWCEditorTextureLease(FDWCEditorTextureLease&& Other) noexcept
    : State(MoveTemp(Other.State)), Entry(MoveTemp(Other.Entry)), LeaseId(Other.LeaseId)
{
    Other.LeaseId = 0;
}

FDWCEditorTextureLease& FDWCEditorTextureLease::operator=(FDWCEditorTextureLease&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        State = MoveTemp(Other.State);
        Entry = MoveTemp(Other.Entry);
        LeaseId = Other.LeaseId;
        Other.LeaseId = 0;
    }
    return *this;
}

void FDWCEditorTextureLease::Reset()
{
    if (LeaseId != 0)
    {
        if (const TSharedPtr<FDWCEditorTextureLeaseState> PinnedState = State.Pin();
            PinnedState.IsValid() && PinnedState->bAcceptReleases && PinnedState->ReleaseCallback)
        {
            PinnedState->ReleaseCallback(Entry, LeaseId);
        }
    }
    State.Reset();
    Entry.Reset();
    LeaseId = 0;
}

namespace
{
    int64 GetRectArea(const FIntRect& Rect)
    {
        return static_cast<int64>(FMath::Max(Rect.Width(), 0)) * FMath::Max(Rect.Height(), 0);
    }

    void BuildWrappedIntervals(
        const int32                             MinValue,
        const int32                             MaxValue,
        const int32                             Extent,
        TArray<FIntPoint, TInlineAllocator<2>>& OutIntervals)
    {
        OutIntervals.Reset();
        if (Extent <= 0 || MaxValue <= MinValue)
        {
            return;
        }
        if (MaxValue - MinValue >= Extent)
        {
            OutIntervals.Add(FIntPoint(0, Extent));
            return;
        }

        const int32 WrappedMin = ((MinValue % Extent) + Extent) % Extent;
        const int32 WrappedMax = WrappedMin + (MaxValue - MinValue);
        if (WrappedMax <= Extent)
        {
            OutIntervals.Add(FIntPoint(WrappedMin, WrappedMax));
        }
        else
        {
            OutIntervals.Add(FIntPoint(WrappedMin, Extent));
            OutIntervals.Add(FIntPoint(0, WrappedMax - Extent));
        }
    }
} // namespace

void FDWCEditorDirtyRegionSet::Add(
    const FIntRect&  DirtyRect,
    const FIntPoint& TextureSize,
    const bool       bWrap)
{
    if (DirtyRect.IsEmpty() || TextureSize.X <= 0 || TextureSize.Y <= 0)
    {
        return;
    }

    if (!bWrap)
    {
        AddClamped(DirtyRect, TextureSize);
        return;
    }

    TArray<FIntPoint, TInlineAllocator<2>> XIntervals;
    TArray<FIntPoint, TInlineAllocator<2>> YIntervals;
    BuildWrappedIntervals(DirtyRect.Min.X, DirtyRect.Max.X, TextureSize.X, XIntervals);
    BuildWrappedIntervals(DirtyRect.Min.Y, DirtyRect.Max.Y, TextureSize.Y, YIntervals);
    for (const FIntPoint& X : XIntervals)
    {
        for (const FIntPoint& Y : YIntervals)
        {
            AddClamped(FIntRect(X.X, Y.X, X.Y, Y.Y), TextureSize);
        }
    }
}

uint64 FDWCEditorDirtyRegionSet::GetArea() const
{
    uint64 Area = 0;
    for (const FIntRect& Region : Regions)
    {
        Area += static_cast<uint64>(GetRectArea(Region));
    }
    return Area;
}

void FDWCEditorDirtyRegionSet::AddClamped(
    const FIntRect&  DirtyRect,
    const FIntPoint& TextureSize)
{
    FIntRect Pending(
        FMath::Clamp(DirtyRect.Min.X, 0, TextureSize.X),
        FMath::Clamp(DirtyRect.Min.Y, 0, TextureSize.Y),
        FMath::Clamp(DirtyRect.Max.X, 0, TextureSize.X),
        FMath::Clamp(DirtyRect.Max.Y, 0, TextureSize.Y));
    if (Pending.IsEmpty())
    {
        return;
    }

    bool bMerged = false;
    do
    {
        bMerged = false;
        for (int32 Index = Regions.Num() - 1; Index >= 0; --Index)
        {
            const FIntRect& Existing = Regions[Index];
            const bool      bTouches =
                Pending.Min.X <= Existing.Max.X + 1 && Pending.Max.X + 1 >= Existing.Min.X &&
                Pending.Min.Y <= Existing.Max.Y + 1 && Pending.Max.Y + 1 >= Existing.Min.Y;
            if (!bTouches)
            {
                continue;
            }

            Pending = FIntRect(
                FMath::Min(Pending.Min.X, Existing.Min.X),
                FMath::Min(Pending.Min.Y, Existing.Min.Y),
                FMath::Max(Pending.Max.X, Existing.Max.X),
                FMath::Max(Pending.Max.Y, Existing.Max.Y));
            Regions.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            bMerged = true;
        }
    } while (bMerged);

    Regions.Add(Pending);
    ReduceRegionCount();
}

void FDWCEditorDirtyRegionSet::ReduceRegionCount()
{
    while (Regions.Num() > MaxRegions)
    {
        int32 BestA = 0;
        int32 BestB = 1;
        int64 BestAddedArea = MAX_int64;
        for (int32 A = 0; A < Regions.Num() - 1; ++A)
        {
            for (int32 B = A + 1; B < Regions.Num(); ++B)
            {
                const FIntRect Combined(
                    FMath::Min(Regions[A].Min.X, Regions[B].Min.X),
                    FMath::Min(Regions[A].Min.Y, Regions[B].Min.Y),
                    FMath::Max(Regions[A].Max.X, Regions[B].Max.X),
                    FMath::Max(Regions[A].Max.Y, Regions[B].Max.Y));
                const int64 AddedArea = GetRectArea(Combined) - GetRectArea(Regions[A]) - GetRectArea(Regions[B]);
                if (AddedArea < BestAddedArea)
                {
                    BestAddedArea = AddedArea;
                    BestA = A;
                    BestB = B;
                }
            }
        }

        Regions[BestA] = FIntRect(
            FMath::Min(Regions[BestA].Min.X, Regions[BestB].Min.X),
            FMath::Min(Regions[BestA].Min.Y, Regions[BestB].Min.Y),
            FMath::Max(Regions[BestA].Max.X, Regions[BestB].Max.X),
            FMath::Max(Regions[BestA].Max.Y, Regions[BestB].Max.Y));
        Regions.RemoveAt(BestB, 1, EAllowShrinking::No);
    }
}
