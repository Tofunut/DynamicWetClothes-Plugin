#pragma once

#include "CoreMinimal.h"

struct FWetWrinkleBrushSettings
{
    int32 UVChannelIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    float BrushRadiusUV = 0.025f;
    float Strength = 1.0f;
    float Falloff = 0.5f;
    float RotationRadians = 0.0f;
    bool bShowPreview = true;
};

struct FWetWrinkleSurfaceHit
{
    bool bHit = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVChannelIndex = 0;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
    FVector2D UV = FVector2D::ZeroVector;
    FVector Barycentric = FVector::ZeroVector;
    double DistanceSq = TNumericLimits<double>::Max();
};

DECLARE_DELEGATE_OneParam(FOnWetWrinkleSurfaceHitChanged, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
