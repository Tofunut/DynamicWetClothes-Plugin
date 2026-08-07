//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"

namespace
{
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

