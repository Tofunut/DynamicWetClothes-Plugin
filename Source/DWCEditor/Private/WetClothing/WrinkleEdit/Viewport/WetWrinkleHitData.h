#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"

class UTexture2D;
class UWetWrinklePreset;

enum class EWetWrinkleToolMode : uint8
{
    Patch,
    ProceduralRidgeStroke
};

enum class EWetProceduralRidgeEditMode : uint8
{
    Draw,
    Edit
};

struct FWetWrinkleBrushSettings
{
    FWetWrinkleBrushSettings()
    {
        RidgeNaturalVariation.bEnabled = true;
    }

    EWetWrinkleToolMode ToolMode = EWetWrinkleToolMode::Patch;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    TObjectPtr<UWetWrinklePreset> WrinklePreset = nullptr;
    float BrushRadiusUV = 0.025f;
    float Strength = 1.0f;
    float Falloff = 0.5f;
    float RotationRadians = 0.0f;
    float PreviewWetness = 1.0f;
    EWetProceduralRidgeShape RidgeShape = EWetProceduralRidgeShape::Convex;
    bool bFlipRidgeFoldSide = false;
    float RidgeStartTaper = 0.15f;
    float RidgeEndTaper = 0.15f;
    float RidgePointSpacingScale = 0.25f;
    FWetProceduralRidgeFlareSettings RidgeFlareSettings;
    FWetProceduralRidgeVariationSettings RidgeNaturalVariation;
    EWetProceduralRidgeEditMode RidgeEditMode = EWetProceduralRidgeEditMode::Draw;
    bool bRidgeJunctionModeEnabled = true;
    bool bShowPreview = true;
};

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
DECLARE_DELEGATE_OneParam(FOnWetWrinklePaintStrokeStarted, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
DECLARE_DELEGATE_OneParam(FOnWetWrinklePaintStampRequested, const FWetWrinkleSurfaceHit& /*SurfaceHit*/);
DECLARE_DELEGATE(FOnWetWrinklePaintStrokeEnded);
DECLARE_DELEGATE(FOnWetWrinklePaintStrokeCanceled);
