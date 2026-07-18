#pragma once

#include "CoreMinimal.h"
#include "WetClothingWrinkleData.generated.h"

class UTexture;
class UTexture2D;
class UWetWrinklePreset;

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePatchPlacement
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    FGuid PatchGuid;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    int32 MaterialSlotIndex = INDEX_NONE;

    // The wrinkle UV channel is stored on FWetClothingWrinkleData. This field is kept for editor paint tools.
    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    int32 UVChannelIndex = 0;

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

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    TObjectPtr<UWetWrinklePreset> WrinklePreset = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch")
    int32 AffectedWetPartID = INDEX_NONE;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Editor Preview")
    bool bHasEditorSurface = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Editor Preview")
    FVector EditorSurfaceLocalPosition = FVector::ZeroVector;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Editor Preview")
    FVector EditorSurfaceLocalNormal = FVector::UpVector;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Editor Preview")
    FVector EditorSurfaceLocalTangent = FVector::ForwardVector;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Patch|Editor Preview")
    FVector EditorSurfaceLocalBitangent = FVector::RightVector;
#endif
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePatchStroke
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke")
    FGuid StrokeGuid;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke")
    FString DisplayName;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke")
    bool bLocked = false;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke", meta = (ClampMin = "0.0"))
    float StrengthMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke", meta = (ClampMin = "0.0"))
    float ScaleMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch Stroke")
    TArray<FWetWrinklePatchPlacement> PatchPlacements;
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

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke")
    int32 UVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Procedural Ridge Stroke")
    int32 LODIndex = 0;

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

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "16", ClampMax = "8192"))
    int32 DefaultResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "0", ClampMax = "64"))
    int32 PaddingPixels = 8;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    TArray<int32> TargetLODIndices;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bBakeNormalMap = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bBakeMask = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bIncludeDisabledPatchStrokes = false;
};

UENUM(BlueprintType)
enum class EDWCWrinkleAlphaSemantic : uint8
{
    None,
    NormalDeviationCoverage_DEPRECATED UMETA(Hidden),
    ConvexSeparation
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleBakedMapSet
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 LODIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleNormalMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleMask = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    bool bHasCoverageAlpha = false;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    EDWCWrinkleAlphaSemantic AlphaSemantic = EDWCWrinkleAlphaSemantic::None;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 AlphaBuildVersion = 0;

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
struct DWC_API FWetWrinkleGeneratedUVSlot
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    int32 UVChannelIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    int32 SourceUVChannelIndex = 0;

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    int32 LODIndex = 0;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    FGuid GeneratedUVBuildGuid;
#endif
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingWrinkleData
{
    GENERATED_BODY()

    // DWC Data UV channel used by wet wrinkle maps. The channel is generated during WCA Setup.
    UPROPERTY(EditAnywhere, Category = "Editable")
    int32 WrinkleUVChannelIndex = 0;

#if WITH_EDITORONLY_DATA
    // Number of UV channels that existed before DWC started appending wrinkle UV channels.
    // Channels below this count are treated as imported mesh data and cannot be deleted from the wrinkle editor.
    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    int32 OriginalUVChannelCount = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    bool bHasGeneratedWrinkleUV = false;

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    FGuid GeneratedWrinkleUVBuildGuid;
#endif

    UPROPERTY(VisibleAnywhere, Category = "Editable|Generated UV")
    TArray<FWetWrinkleGeneratedUVSlot> GeneratedWrinkleUVSlots;

    UPROPERTY(EditAnywhere, Category = "Editable")
    TArray<FWetWrinklePatchStroke> EditablePatchStrokes;

    UPROPERTY(EditAnywhere, Category = "Editable")
    TArray<FWetProceduralRidgeStroke> EditableProceduralRidgeStrokes;

    UPROPERTY(EditAnywhere, Category = "Bake")
    FWetWrinkleBakeSettings BakeSettings;

    UPROPERTY(VisibleAnywhere, Category = "Baked")
    TArray<FWetWrinkleBakedMapSet> BakedWrinkleMaps;

    const FWetWrinkleBakedMapSet* FindBakedWrinkleMap(
        int32 MaterialSlotIndex,
        int32 PreferredUVChannelIndex = INDEX_NONE,
        int32 PreferredLODIndex = INDEX_NONE) const
    {
        if (PreferredUVChannelIndex != INDEX_NONE && PreferredLODIndex != INDEX_NONE)
        {
            if (const FWetWrinkleBakedMapSet* ExactMatch = BakedWrinkleMaps.FindByPredicate(
                    [MaterialSlotIndex, PreferredUVChannelIndex, PreferredLODIndex](const FWetWrinkleBakedMapSet& Candidate)
                    {
                        return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                               Candidate.UVChannelIndex == PreferredUVChannelIndex &&
                               Candidate.LODIndex == PreferredLODIndex &&
                               Candidate.BakedWrinkleNormalMap != nullptr;
                    }))
            {
                return ExactMatch;
            }
        }

        if (PreferredUVChannelIndex != INDEX_NONE)
        {
            if (const FWetWrinkleBakedMapSet* UVMatch = BakedWrinkleMaps.FindByPredicate(
                    [MaterialSlotIndex, PreferredUVChannelIndex](const FWetWrinkleBakedMapSet& Candidate)
                    {
                        return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                               Candidate.UVChannelIndex == PreferredUVChannelIndex &&
                               Candidate.BakedWrinkleNormalMap != nullptr;
                    }))
            {
                return UVMatch;
            }
        }

        return BakedWrinkleMaps.FindByPredicate(
            [MaterialSlotIndex](const FWetWrinkleBakedMapSet& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                       Candidate.BakedWrinkleNormalMap != nullptr;
            });
    }

    UTexture2D* ResolveBakedWrinkleNormalMap(
        int32 MaterialSlotIndex,
        int32 PreferredUVChannelIndex = INDEX_NONE,
        int32 PreferredLODIndex = INDEX_NONE) const
    {
        const FWetWrinkleBakedMapSet* Match = FindBakedWrinkleMap(MaterialSlotIndex, PreferredUVChannelIndex, PreferredLODIndex);
        return Match != nullptr ? Match->BakedWrinkleNormalMap.Get() : nullptr;
    }
};
