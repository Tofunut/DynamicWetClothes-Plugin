#pragma once

#include "CoreMinimal.h"

class UTexture2D;

struct FWetWrinkleBrushSettings
{
    int32 UVChannelIndex = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    TObjectPtr<UTexture2D> BrushHeightTexture = nullptr;
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
    FVector LocalPosition = FVector::ZeroVector;
    FVector LocalNormal = FVector::UpVector;
    FVector LocalTangent = FVector::ForwardVector;
    FVector LocalBitangent = FVector::RightVector;
    FVector2D UV = FVector2D::ZeroVector;
    FVector Barycentric = FVector::ZeroVector;
    double DistanceSq = TNumericLimits<double>::Max();
};

DECLARE_DELEGATE_OneParam(FOnWetWrinkleSurfaceHitChanged, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
DECLARE_DELEGATE_OneParam(FOnWetWrinklePaintStrokeStarted, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
DECLARE_DELEGATE_OneParam(FOnWetWrinklePaintStampRequested, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
DECLARE_DELEGATE(FOnWetWrinklePaintStrokeEnded);
