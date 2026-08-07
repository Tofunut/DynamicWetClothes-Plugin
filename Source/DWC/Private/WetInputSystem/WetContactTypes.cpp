//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetInputSystem/WetContactTypes.h"

FDWCWaterSurfaceData::FDWCWaterSurfaceData() = default;

FDWCWaterSurfaceData::~FDWCWaterSurfaceData() = default;

FDWCWaterSurfaceData::FDWCWaterSurfaceData(const FDWCWaterSurfaceData& Other) = default;

FDWCWaterSurfaceData::FDWCWaterSurfaceData(FDWCWaterSurfaceData&& Other) = default;

FDWCWaterSurfaceData& FDWCWaterSurfaceData::operator=(const FDWCWaterSurfaceData& Other) = default;

FDWCWaterSurfaceData& FDWCWaterSurfaceData::operator=(FDWCWaterSurfaceData&& Other) = default;

int32 FDWCWaterSurfaceData::GetSampleIndex(const int32 X, const int32 Y) const
{
    return Y * SizeX + X;
}

bool FDWCWaterSurfaceData::IsValidSampleIndex(const int32 X, const int32 Y) const
{
    const int32 SampleIndex = GetSampleIndex(X, Y);
    return X >= 0 &&
           Y >= 0 &&
           X < SizeX &&
           Y < SizeY &&
           SurfaceZ.IsValidIndex(SampleIndex) &&
           Valid.IsValidIndex(SampleIndex) &&
           Valid[SampleIndex] != 0;
}
