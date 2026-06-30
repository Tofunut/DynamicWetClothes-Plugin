#include "DynamicWet/DynamicWetContactTypes.h"

FDWCWetSurfaceData::FDWCWetSurfaceData() = default;

FDWCWetSurfaceData::~FDWCWetSurfaceData() = default;

FDWCWetSurfaceData::FDWCWetSurfaceData(const FDWCWetSurfaceData& Other) = default;

FDWCWetSurfaceData::FDWCWetSurfaceData(FDWCWetSurfaceData&& Other) = default;

FDWCWetSurfaceData& FDWCWetSurfaceData::operator=(const FDWCWetSurfaceData& Other) = default;

FDWCWetSurfaceData& FDWCWetSurfaceData::operator=(FDWCWetSurfaceData&& Other) = default;

int32 FDWCWetSurfaceData::GetSampleIndex(const int32 X, const int32 Y) const
{
    return Y * SizeX + X;
}

bool FDWCWetSurfaceData::IsValidSampleIndex(const int32 X, const int32 Y) const
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
