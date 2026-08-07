//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"

class UTexture2D;

struct FWetWrinkleSurfaceHit
{
    bool bHit = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
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
