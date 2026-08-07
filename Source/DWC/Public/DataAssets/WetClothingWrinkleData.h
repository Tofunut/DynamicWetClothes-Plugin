//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothingWrinkleData.generated.h"

class UTexture;
class UTexture2D;

USTRUCT()
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

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    int32 AnchorTriangleID = INDEX_NONE;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    TObjectPtr<UTexture> SourceTexture = nullptr;

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

};

USTRUCT()
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

UENUM()
enum class EWetProceduralRidgeShape : uint8
{
    Convex,
    Concave,
    Fold
};

UENUM()
enum class EWetProceduralRidgeEndpointMode : uint8
{
    Pointed,
    Rounded,
    Junction,
    Flared
};

USTRUCT()
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

USTRUCT()
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

USTRUCT()
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

USTRUCT()
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

USTRUCT()
struct DWC_API FWetWrinkleBakeSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "16", ClampMax = "4096"))
    int32 DefaultResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "0", ClampMax = "64"))
    int32 PaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bIncludeDisabledPatches = false;
};

UENUM()
enum class EDWCWrinkleAlphaSemantic : uint8
{
    None = 0,
    ConvexSeparation = 2
};

UENUM()
enum class EDWCWrinkleNormalSource : uint8
{
    Baked,
    CustomTexture
};

USTRUCT()
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

USTRUCT()
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
    bool bHasCoverageAlpha = false;
    EDWCWrinkleAlphaSemantic AlphaSemantic = EDWCWrinkleAlphaSemantic::None;

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

USTRUCT()
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
            Result.bHasCoverageAlpha = RuntimeSource->bUseAlphaAsConvexSeparation && Result.Texture != nullptr;
            Result.AlphaSemantic = Result.bHasCoverageAlpha
                                       ? EDWCWrinkleAlphaSemantic::ConvexSeparation
                                       : EDWCWrinkleAlphaSemantic::None;
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
