//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothingWrinkleData.generated.h"

class UTexture2D;

UENUM(BlueprintType)
enum class EWetWrinklePatchProjectionMode : uint8
{
    NonUVSeam UMETA(DisplayName = "Non UV Seam"),
    SurfaceDecal UMETA(DisplayName = "UV Seam")
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePatchPlacement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FGuid PatchGuid;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FString DisplayName;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch|Projection")
    EWetWrinklePatchProjectionMode ProjectionMode = EWetWrinklePatchProjectionMode::NonUVSeam;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch|Projection", meta = (ClampMin = "0.1", ClampMax = "20.0"))
    float ProjectionDepthLocal = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch|Projection", meta = (ClampMin = "1.0", ClampMax = "89.0"))
    float MaxSurfaceAngleDegrees = 70.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch|Projection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ProjectionDepthSoftness = 0.2f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch|Projection", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ProjectionAngleSoftness = 0.1f;

    // Canonical placement is the material-slot triangle plus barycentric coordinates.
    // PositionUV remains a derived compatibility value until all raster paths are surface-based.
    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    bool bHasSurfaceAnchor = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    int32 AnchorTriangleID = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);

    // Canonical local mesh-space frame. Data UV is only the raster destination;
    // it must not define the physical orientation of an authored patch.
    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    bool bHasSurfaceFrame = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    FVector3f SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    FVector3f SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);

    // Physical local mesh-space half extents. These are authored in mesh units
    // and remain independent of Data UV island scale and rotation.
    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    bool bHasSurfaceFootprint = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Surface Anchor")
    FVector2f SurfaceHalfExtentLocal = FVector2f::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FVector2D PositionUV = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (ClampMin = "0.0"))
    float BrushRadiusUV = 0.025f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    float RotationRadians = 0.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FVector2D Scale = FVector2D(1.0f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (ClampMin = "0.0", ClampMax = "4.0"))
    float Strength = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    // Canonical normal source for patch preview and bake.
    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    TObjectPtr<UTexture2D> WrinkleNormalTexture = nullptr;

    bool HasValidSurfaceAnchor() const
    {
        const float Sum = AnchorBarycentric.X + AnchorBarycentric.Y + AnchorBarycentric.Z;
        return bHasSurfaceAnchor && AnchorTriangleID != INDEX_NONE &&
            FMath::IsFinite(AnchorBarycentric.X) &&
            FMath::IsFinite(AnchorBarycentric.Y) &&
            FMath::IsFinite(AnchorBarycentric.Z) &&
            AnchorBarycentric.X >= -UE_KINDA_SMALL_NUMBER &&
            AnchorBarycentric.Y >= -UE_KINDA_SMALL_NUMBER &&
            AnchorBarycentric.Z >= -UE_KINDA_SMALL_NUMBER &&
            FMath::IsNearlyEqual(Sum, 1.0f, 0.001f);
    }

    bool HasValidSurfaceFootprint() const
    {
        return bHasSurfaceFootprint &&
            FMath::IsFinite(SurfaceHalfExtentLocal.X) &&
            FMath::IsFinite(SurfaceHalfExtentLocal.Y) &&
            SurfaceHalfExtentLocal.X > UE_SMALL_NUMBER &&
            SurfaceHalfExtentLocal.Y > UE_SMALL_NUMBER;
    }

    bool HasValidSurfaceFrame() const
    {
        if (!bHasSurfaceFrame ||
            !FMath::IsFinite(SurfaceFrameU.X) || !FMath::IsFinite(SurfaceFrameU.Y) || !FMath::IsFinite(SurfaceFrameU.Z) ||
            !FMath::IsFinite(SurfaceFrameV.X) || !FMath::IsFinite(SurfaceFrameV.Y) || !FMath::IsFinite(SurfaceFrameV.Z))
        {
            return false;
        }

        const FVector3f U = SurfaceFrameU.GetSafeNormal();
        const FVector3f V = SurfaceFrameV.GetSafeNormal();
        return !U.IsNearlyZero() && !V.IsNearlyZero() &&
            FMath::Abs(FVector3f::DotProduct(U, V)) <= 0.01f;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetProceduralRidgeStrokePoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FVector2D PositionUV = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    int32 AnchorTriangleID = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
};

UENUM(BlueprintType)
enum class EWetProceduralRidgeShape : uint8
{
    Convex,
    Concave,
    Fold
};

UENUM(BlueprintType)
enum class EWetProceduralRidgeEndpointMode : uint8
{
    Pointed,
    Rounded,
    Junction,
    Flared
};

USTRUCT(BlueprintType)
struct DWC_API FWetProceduralRidgeEndpoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    EWetProceduralRidgeEndpointMode Mode = EWetProceduralRidgeEndpointMode::Pointed;

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke")
    FGuid ConnectedStrokeGuid;

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke")
    int32 ConnectedSegmentIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConnectedSegmentT = 0.0f;

    void ResetConnection()
    {
        ConnectedStrokeGuid.Invalidate();
        ConnectedSegmentIndex = INDEX_NONE;
        ConnectedSegmentT = 0.0f;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetProceduralRidgeFlareSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Flared Endpoint", meta = (ClampMin = "0.01", ClampMax = "0.5"))
    float Length = 0.25f;

    UPROPERTY(EditAnywhere, Category = "Flared Endpoint", meta = (ClampMin = "1.0", ClampMax = "5.0"))
    float WidthScale = 2.5f;

    UPROPERTY(EditAnywhere, Category = "Flared Endpoint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float EndStrength = 0.10f;

    UPROPERTY(EditAnywhere, Category = "Flared Endpoint", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Softness = 0.70f;
};

USTRUCT(BlueprintType)
struct DWC_API FWetProceduralRidgeVariationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Natural Variation")
    bool bEnabled = false;

    UPROPERTY(EditAnywhere, Category = "Natural Variation", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float CenterlineAmount = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Natural Variation", meta = (ClampMin = "0.25", ClampMax = "12.0"))
    float CenterlineFrequency = 3.0f;

    UPROPERTY(EditAnywhere, Category = "Natural Variation", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float WidthVariation = 0.10f;

    UPROPERTY(EditAnywhere, Category = "Natural Variation", meta = (ClampMin = "0.25", ClampMax = "12.0"))
    float WidthFrequency = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Natural Variation")
    int32 NoiseSeed = 1337;
};

USTRUCT(BlueprintType)
struct DWC_API FWetProceduralRidgeStroke
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FGuid StrokeGuid;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FString DisplayName;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    bool bEnabled = true;

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    TArray<FWetProceduralRidgeStrokePoint> Points;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    EWetProceduralRidgeShape Shape = EWetProceduralRidgeShape::Convex;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    bool bFlipFoldSide = false;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0001"))
    float WidthUV = 0.025f;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0", ClampMax = "4.0"))
    float Strength = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float StartTaper = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float EndTaper = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FWetProceduralRidgeEndpoint StartEndpoint;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FWetProceduralRidgeEndpoint EndEndpoint;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FWetProceduralRidgeFlareSettings FlareSettings;

    UPROPERTY(EditAnywhere, Category = "Wet Procedural Ridge Stroke")
    FWetProceduralRidgeVariationSettings NaturalVariation;
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleBakeSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "0", ClampMax = "64"))
    int32 PaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bIncludeDisabledPatches = false;
};

UENUM(BlueprintType)
enum class EDWCWrinkleNormalSource : uint8
{
    Baked,
    CustomTexture
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleBakedMapSet
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleNormalMap = nullptr;

#if WITH_EDITORONLY_DATA
    // Authoring-only source consumed while baking the final transparency map.
    // It is intentionally stripped from cooked/runtime WCA data.
    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleMask = nullptr;
#endif

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 PaddingPixels = 8;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    FGuid BakeGuid;
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleRuntimeNormalSource
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Runtime Wrinkle Normal")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Runtime Wrinkle Normal")
    EDWCWrinkleNormalSource Source = EDWCWrinkleNormalSource::Baked;

    UPROPERTY(EditAnywhere, Category = "Runtime Wrinkle Normal")
    TObjectPtr<UTexture2D> CustomWrinkleNormalMap = nullptr;

    UPROPERTY(EditAnywhere, Category = "Runtime Wrinkle Normal")
    bool bUseAlphaAsConvexSeparation = false;
};

struct DWC_API FWetWrinkleResolvedNormalMap
{
    UTexture2D* Texture = nullptr;
    EDWCWrinkleNormalSource Source = EDWCWrinkleNormalSource::Baked;
    int32 MaterialSlotIndex = INDEX_NONE;
    bool IsValid() const
    {
        return Texture != nullptr && MaterialSlotIndex != INDEX_NONE;
    }
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleCoverageExtractionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage", meta = (ClampMin = "0", ClampMax = "8"))
    int32 InputBlurRadiusPixels = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConvexityThreshold = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage", meta = (ClampMin = "1", ClampMax = "1024"))
    int32 MinimumComponentPixels = 8;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Coverage")
    bool bInvertConvexity = false;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingWrinkleData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Editable")
    TArray<FWetWrinklePatchPlacement> EditablePatches;

    UPROPERTY(EditAnywhere, Category = "Editable")
    TArray<FWetProceduralRidgeStroke> EditableProceduralRidgeStrokes;

    UPROPERTY(EditAnywhere, Category = "Bake")
    FWetWrinkleBakeSettings BakeSettings;

    UPROPERTY(EditAnywhere, Category = "Bake|Coverage")
    FWetWrinkleCoverageExtractionSettings CoverageExtractionSettings;

    // Authored slot-level selection of the normal map used by editor preview and runtime rendering.
    UPROPERTY(EditAnywhere, Category = "Runtime")
    TArray<FWetWrinkleRuntimeNormalSource> RuntimeNormalSources;

    UPROPERTY(VisibleAnywhere, Category = "Baked")
    TArray<FWetWrinkleBakedMapSet> BakedWrinkleMaps;

    const FWetWrinkleRuntimeNormalSource* FindRuntimeNormalSource(int32 MaterialSlotIndex) const
    {
        return RuntimeNormalSources.FindByPredicate(
            [MaterialSlotIndex](const FWetWrinkleRuntimeNormalSource& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    FWetWrinkleRuntimeNormalSource* FindRuntimeNormalSource(int32 MaterialSlotIndex)
    {
        return const_cast<FWetWrinkleRuntimeNormalSource*>(
            static_cast<const FWetClothingWrinkleData*>(this)->FindRuntimeNormalSource(MaterialSlotIndex));
    }

    bool IsUsingCustomWrinkleNormalMap(int32 MaterialSlotIndex) const
    {
        const FWetWrinkleRuntimeNormalSource* Entry = FindRuntimeNormalSource(MaterialSlotIndex);
        return Entry != nullptr && Entry->Source == EDWCWrinkleNormalSource::CustomTexture;
    }

    FWetWrinkleResolvedNormalMap ResolveRuntimeWrinkleNormalMap(int32 MaterialSlotIndex) const
    {
        FWetWrinkleResolvedNormalMap Result;
        Result.MaterialSlotIndex = MaterialSlotIndex;

        const FWetWrinkleRuntimeNormalSource* RuntimeSource = FindRuntimeNormalSource(MaterialSlotIndex);
        if (RuntimeSource != nullptr && RuntimeSource->Source == EDWCWrinkleNormalSource::CustomTexture)
        {
            Result.Source = EDWCWrinkleNormalSource::CustomTexture;
            Result.Texture = RuntimeSource->CustomWrinkleNormalMap.Get();
            return Result;
        }

        if (const FWetWrinkleBakedMapSet* BakedMap = FindBakedWrinkleMap(MaterialSlotIndex))
        {
            Result.Texture = BakedMap->BakedWrinkleNormalMap.Get();
        }
        return Result;
    }

    const FWetWrinkleBakedMapSet* FindBakedWrinkleMap(int32 MaterialSlotIndex) const
    {
        return BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       Candidate.BakedWrinkleNormalMap != nullptr;
            });
    }

    UTexture2D* ResolveBakedWrinkleNormalMap(int32 MaterialSlotIndex) const
    {
        const FWetWrinkleBakedMapSet* Match = FindBakedWrinkleMap(MaterialSlotIndex);
        return Match != nullptr ? Match->BakedWrinkleNormalMap.Get() : nullptr;
    }
};
