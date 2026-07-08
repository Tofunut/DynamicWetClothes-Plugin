#pragma once

#include "CoreMinimal.h"
#include "WetClothingWrinkleData.generated.h"

class UTexture;
class UTexture2D;

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

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Strength = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Falloff = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Patch", meta = (DisplayName = "Patch Normal Texture"))
    TObjectPtr<UTexture2D> NormalPatchTexture = nullptr;

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
struct DWC_API FWetWrinkleBakeSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "16", ClampMax = "8192"))
    int32 DefaultResolution = 1024;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake", meta = (ClampMin = "0", ClampMax = "64"))
    int32 PaddingPixels = 4;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    TArray<int32> TargetLODIndices;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bBakeNormalMap = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bBakeMask = true;

    UPROPERTY(EditAnywhere, Category = "Wet Wrinkle Bake")
    bool bIncludeDisabledPatchStrokes = false;
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
    int32 UVChannelIndex = 2;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleNormalMap = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    TObjectPtr<UTexture2D> BakedWrinkleMask = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 Resolution = 1024;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    int32 PaddingPixels = 4;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    FString BuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Wet Wrinkle Baked")
    FGuid BakeGuid;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingWrinkleData
{
    GENERATED_BODY()

    // Dedicated UV channel for wet wrinkle normal/mask baking. Do not share it with overlapped base material UVs.
    UPROPERTY(EditAnywhere, Category = "Editable")
    int32 WrinkleUVChannelIndex = 2;

    UPROPERTY(EditAnywhere, Category = "Editable")
    TArray<FWetWrinklePatchStroke> EditablePatchStrokes;

    UPROPERTY(EditAnywhere, Category = "Bake")
    FWetWrinkleBakeSettings BakeSettings;

    UPROPERTY(VisibleAnywhere, Category = "Baked")
    TArray<FWetWrinkleBakedMapSet> BakedWrinkleMaps;
};
